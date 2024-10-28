#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include "shared.h"
#include "buffer.h"
#include "packet.h"
#include "player.h"

#define CLIENT_SHOULD_TERMINATE ((u32) 1 << 0)
#define CLIENT_DID_TRANSFER_TO_PLAYER ((u32) 1 << 1)
#define CLIENT_PACKET_COMPRESSION ((u32) 1 << 2)
#define CLIENT_WANT_KNOWN_PACKS ((u32) 1 << 3)
#define CLIENT_GOT_KNOWN_PACKS ((u32) 1 << 4)
#define CLIENT_GOT_CLIENT_INFO ((u32) 1 << 5)
#define CLIENT_WANT_FINISH_CONFIGURATION ((u32) 1 << 6)

enum ProtocolState {
    PROTOCOL_HANDSHAKE,
    PROTOCOL_AWAIT_STATUS_REQUEST,
    PROTOCOL_AWAIT_PING_REQUEST,
    PROTOCOL_AWAIT_HELLO,
    PROTOCOL_AWAIT_LOGIN_ACK,
    PROTOCOL_CONFIGURATION,
    PROTOCOL_JOIN,
};

typedef struct {
    u8 * data;
    i32 cursor;
    i32 size;
} Buffer;

typedef struct {
    int socket;
    u32 flags;

    Buffer recBuf;
    Buffer sendBuf;

    int protocolState;
    UUID uuid;
    unsigned char username[16];
    int usernameSize;

    u8 locale[MAX_PLAYER_LOCALE_SIZE];
    i32 localeSize;
    i32 chunkCacheRadius;
    i32 chatMode;
    i32 seesChatColours;
    u8 skinCustomisation;
    i32 mainHand;
    i32 textFiltering;
    i32 showInStatusList;
} Client;

typedef struct {
    Client * * clientArray;
    i32 clientArraySize;
    int serverSocket;
} Network;

static Network network;

static void CreateClient(int clientSocket) {
    i32 clientIndex;
    for (clientIndex = 0; clientIndex < network.clientArraySize; clientIndex++) {
        if (network.clientArray[clientIndex] == NULL) {
            break;
        }
    }
    if (clientIndex >= network.clientArraySize) {
        LogInfo("No space for more clients");
        close(clientSocket);
        return;
    }

    int flags = fcntl(clientSocket, F_GETFL, 0);
    if (flags == -1) {
        LogInfo("Client failed to get socket flags");
        close(clientSocket);
        return;
    }
    if (fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK) == -1) {
        LogInfo("Client failed to set socket flags");
        close(clientSocket);
        return;
    }

    // NOTE(traks): we write all packet data in one go, so this setting
    // shouldn't affect things too much, except for sending the last couple
    // of packets earlier if they're small.
    //
    // I think this will also send TCP ACKs faster. Might give the client's
    // TCP stack a better approximation for server latency? Not sure.
    int yes = 1;
    if (setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes) == -1) {
        LogInfo("Client failed to set socket options");
        close(clientSocket);
        return;
    }

    // @TODO(traks) should we lower the receive and send buffer sizes? For
    // hanshakes/status/login they don't need to be as large as the default
    // (probably around 200KB).

    // TODO(traks): We should also consider lowering the internal receive/send
    // buffer sizes. The client doesn't send us that much data and we pull it
    // out of the receive buffer every tick, so it doesn't need to be very
    // large.
    //
    // Currently there's a problem related to this on macOS actually. If I
    // connect 400 clients to the server with 2 chunk sends per tick per player,
    // the number of mbufs in use in 'netstat -m' increases rapidly to the
    // limit. Once the limit is reached, all networking seems to break. Very
    // confused about this. Why doesn't it fully preallocate the buffers for
    // each client? Is it done so the OS uses less memory?
    //
    // In any case, we should handle these kinds of things properly. If a send
    // fails (with ENOBUFS), don't send any data for a bit and don't kick the
    // client immediately. If the error persists for too long, kick the client.
    // Also don't send chunks if our backlog of chunk sends across all clients
    // is too large.
    //
    // We need better limits of the number of chunk sends anyhow. Compressing
    // chunk packets is VERY slow. Part of this can be resolved by not inserting
    // 2048 byte light sections of full dark/full bright light. However, if
    // we're starting to have a backlog, we should also dynamically adjust the
    // maximum global number of chunk sends per tick. We still need to ensure
    // each client does receive chunks within a certain time frame. Chunk sends
    // shouldn't be limited to the lucky few. Everyone should get their share of
    // chunks. Possibly specially marked players could get priority.
    //
    // Another idea to spend less time on compression is the following. When a
    // chunk is sent, cache the compressed packet for a while. If multiple
    // players need the chunk, we can then reuse the compressed packet. One
    // major issue with this is that the cached packet needs to be invalidated
    // when the chunk changes. Perhaps we could also append some section
    // change/light change packets to the chunk when that happens. Though that
    // doesn't really seem worth it (especially the light changes).
    //
    // I'm thinking the majority of loaded chunks don't change (even in vanilla
    // survival). So caching compressed chunk packets may be quite effective.
    // Though there's little point if players are spread out.

    Client * client = calloc(1, sizeof *client);
    client->socket = clientSocket;

    // TODO(traks): Should be large enough to:
    //
    //  1. Receive a client intention (handshake) packet, status request and
    //     ping request packet all in one go and store them together in the
    //     buffer.
    //
    //  2. Receive a client intention packet and hello packet and store them
    //     together inside the receive buffer.
    i32 receiveBufferSize = 1 << 10;
    // TODO(traks): figure out appropriate size
    i32 sendBufferSize = 16 << 10;
    client->recBuf = (Buffer) {
        .data = malloc(receiveBufferSize),
        .size = receiveBufferSize,
    };
    client->sendBuf = (Buffer) {
        .data = malloc(sendBufferSize),
        .size = sendBufferSize,
    };

    network.clientArray[clientIndex] = client;
    // LogInfo("Created client");
}

static void FreeClientNoClose(i32 clientIndex) {
    Client * client = network.clientArray[clientIndex];
    assert(client != NULL);
    free(client->recBuf.data);
    free(client->sendBuf.data);
    free(client);
    network.clientArray[clientIndex] = NULL;
}

static void DeleteClient(i32 clientIndex) {
    Client * client = network.clientArray[clientIndex];
    assert(client != NULL);
    close(client->socket);
    FreeClientNoClose(clientIndex);
}

static void ClientMarkTerminate(Client * client) {
    // TODO(traks): we should really be sending disconnect packets back with
    // some error message. Probably also need to wait a bit before actually
    // closing the connection? Otherwise the message may not be received.
    client->flags |= CLIENT_SHOULD_TERMINATE;
}

static void WriteSingleRegistry(Client * client, Cursor * sendCursor, char * name, char * * entries, i32 entryCount) {
    BeginPacket(sendCursor, 7);
    WriteVarString(sendCursor, STR(name));
    WriteVarU32(sendCursor, entryCount);
    for (i32 i = 0; i < entryCount; i++) {
        WriteVarString(sendCursor, STR(entries[i]));
        WriteU8(sendCursor, 0); // no data
    }
    FinishPacket(sendCursor, !!(client->flags & CLIENT_PACKET_COMPRESSION));
}

static void WriteAllRegistries(Client * client, Cursor * sendCursor) {
    char * trimMaterials[] = {
        "minecraft:amethyst",
        "minecraft:copper",
        "minecraft:diamond",
        "minecraft:emerald",
        "minecraft:gold",
        "minecraft:iron",
        "minecraft:lapis",
        "minecraft:netherite",
        "minecraft:quartz",
        "minecraft:redstone",
    };
    WriteSingleRegistry(client, sendCursor, "minecraft:trim_material", trimMaterials, ARRAY_SIZE(trimMaterials));

    char * trimPatterns[] = {
        "minecraft:bolt",
        "minecraft:coast",
        "minecraft:dune",
        "minecraft:eye",
        "minecraft:flow",
        "minecraft:host",
        "minecraft:raiser",
        "minecraft:rib",
        "minecraft:sentry",
        "minecraft:shaper",
        "minecraft:silence",
        "minecraft:snout",
        "minecraft:spire",
        "minecraft:tide",
        "minecraft:vex",
        "minecraft:ward",
        "minecraft:wayfinder",
        "minecraft:wild",
    };
    WriteSingleRegistry(client, sendCursor, "minecraft:trim_pattern", trimPatterns, ARRAY_SIZE(trimPatterns));

    char * bannerPatterns[] = {
        "minecraft:base",
        "minecraft:border",
        "minecraft:bricks",
        "minecraft:circle",
        "minecraft:creeper",
        "minecraft:cross",
        "minecraft:curly_border",
        "minecraft:diagonal_left",
        "minecraft:diagonal_right",
        "minecraft:diagonal_up_left",
        "minecraft:diagonal_up_right",
        "minecraft:flow",
        "minecraft:flower",
        "minecraft:globe",
        "minecraft:gradient",
        "minecraft:gradient_up",
        "minecraft:guster",
        "minecraft:half_horizontal",
        "minecraft:half_horizontal_bottom",
        "minecraft:half_vertical",
        "minecraft:half_vertical_right",
        "minecraft:mojang",
        "minecraft:piglin",
        "minecraft:rhombus",
        "minecraft:skull",
        "minecraft:small_stripes",
        "minecraft:square_bottom_left",
        "minecraft:square_bottom_right",
        "minecraft:square_top_left",
        "minecraft:square_top_right",
        "minecraft:straight_cross",
        "minecraft:stripe_bottom",
        "minecraft:stripe_center",
        "minecraft:stripe_downleft",
        "minecraft:stripe_downright",
        "minecraft:stripe_left",
        "minecraft:stripe_middle",
        "minecraft:stripe_right",
        "minecraft:stripe_top",
        "minecraft:triangle_bottom",
        "minecraft:triangle_top",
        "minecraft:triangles_bottom",
        "minecraft:triangles_top",
    };
    WriteSingleRegistry(client, sendCursor, "minecraft:banner_pattern", bannerPatterns, ARRAY_SIZE(bannerPatterns));

    char * biomes[] = {
        "minecraft:badlands",
        "minecraft:bamboo_jungle",
        "minecraft:basalt_deltas",
        "minecraft:beach",
        "minecraft:birch_forest",
        "minecraft:cherry_grove",
        "minecraft:cold_ocean",
        "minecraft:crimson_forest",
        "minecraft:dark_forest",
        "minecraft:deep_cold_ocean",
        "minecraft:deep_dark",
        "minecraft:deep_frozen_ocean",
        "minecraft:deep_lukewarm_ocean",
        "minecraft:deep_ocean",
        "minecraft:desert",
        "minecraft:dripstone_caves",
        "minecraft:end_barrens",
        "minecraft:end_highlands",
        "minecraft:end_midlands",
        "minecraft:eroded_badlands",
        "minecraft:flower_forest",
        "minecraft:forest",
        "minecraft:frozen_ocean",
        "minecraft:frozen_peaks",
        "minecraft:frozen_river",
        "minecraft:grove",
        "minecraft:ice_spikes",
        "minecraft:jagged_peaks",
        "minecraft:jungle",
        "minecraft:lukewarm_ocean",
        "minecraft:lush_caves",
        "minecraft:mangrove_swamp",
        "minecraft:meadow",
        "minecraft:mushroom_fields",
        "minecraft:nether_wastes",
        "minecraft:ocean",
        "minecraft:old_growth_birch_forest",
        "minecraft:old_growth_pine_taiga",
        "minecraft:old_growth_spruce_taiga",
        "minecraft:plains",
        "minecraft:river",
        "minecraft:savanna",
        "minecraft:savanna_plateau",
        "minecraft:small_end_islands",
        "minecraft:snowy_beach",
        "minecraft:snowy_plains",
        "minecraft:snowy_slopes",
        "minecraft:snowy_taiga",
        "minecraft:soul_sand_valley",
        "minecraft:sparse_jungle",
        "minecraft:stony_peaks",
        "minecraft:stony_shore",
        "minecraft:sunflower_plains",
        "minecraft:swamp",
        "minecraft:taiga",
        "minecraft:the_end",
        "minecraft:the_void",
        "minecraft:warm_ocean",
        "minecraft:warped_forest",
        "minecraft:windswept_forest",
        "minecraft:windswept_gravelly_hills",
        "minecraft:windswept_hills",
        "minecraft:windswept_savanna",
        "minecraft:wooded_badlands",
    };
    WriteSingleRegistry(client, sendCursor, "minecraft:worldgen/biome", biomes, ARRAY_SIZE(biomes));

    char * chatTypes[] = {
        "minecraft:chat",
        "minecraft:emote_command",
        "minecraft:msg_command_incoming",
        "minecraft:msg_command_outgoing",
        "minecraft:say_command",
        "minecraft:team_msg_command_incoming",
        "minecraft:team_msg_command_outgoing",
    };
    WriteSingleRegistry(client, sendCursor, "minecraft:chat_type", chatTypes, ARRAY_SIZE(chatTypes));

    char * damageTypes[] = {
        "minecraft:arrow",
        "minecraft:bad_respawn_point",
        "minecraft:cactus",
        "minecraft:campfire",
        "minecraft:cramming",
        "minecraft:dragon_breath",
        "minecraft:drown",
        "minecraft:dry_out",
        "minecraft:explosion",
        "minecraft:fall",
        "minecraft:falling_anvil",
        "minecraft:falling_block",
        "minecraft:falling_stalactite",
        "minecraft:fireball",
        "minecraft:fireworks",
        "minecraft:fly_into_wall",
        "minecraft:freeze",
        "minecraft:generic",
        "minecraft:generic_kill",
        "minecraft:hot_floor",
        "minecraft:in_fire",
        "minecraft:in_wall",
        "minecraft:indirect_magic",
        "minecraft:lava",
        "minecraft:lightning_bolt",
        "minecraft:magic",
        "minecraft:mob_attack",
        "minecraft:mob_attack_no_aggro",
        "minecraft:mob_projectile",
        "minecraft:on_fire",
        "minecraft:out_of_world",
        "minecraft:outside_border",
        "minecraft:player_attack",
        "minecraft:player_explosion",
        "minecraft:sonic_boom",
        "minecraft:spit",
        "minecraft:stalagmite",
        "minecraft:starve",
        "minecraft:sting",
        "minecraft:sweet_berry_bush",
        "minecraft:thorns",
        "minecraft:thrown",
        "minecraft:trident",
        "minecraft:unattributed_fireball",
        "minecraft:wind_charge",
        "minecraft:wither",
        "minecraft:wither_skull",
    };
    WriteSingleRegistry(client, sendCursor, "minecraft:damage_type", damageTypes, ARRAY_SIZE(damageTypes));

    char * dimensionTypes[] = {
        "minecraft:overworld",
        "minecraft:overworld_caves",
        "minecraft:the_end",
        "minecraft:the_nether",
    };
    WriteSingleRegistry(client, sendCursor, "minecraft:dimension_type", dimensionTypes, ARRAY_SIZE(dimensionTypes));

    char * wolfVariants[] = {
        "minecraft:ashen",
        "minecraft:black",
        "minecraft:chestnut",
        "minecraft:pale",
        "minecraft:rusty",
        "minecraft:snowy",
        "minecraft:spotted",
        "minecraft:striped",
        "minecraft:woods",
    };
    WriteSingleRegistry(client, sendCursor, "minecraft:wolf_variant", wolfVariants, ARRAY_SIZE(wolfVariants));

    char * paintingVariants[] = {
        "minecraft:alban",
        "minecraft:aztec",
        "minecraft:aztec2",
        "minecraft:backyard",
        "minecraft:baroque",
        "minecraft:bomb",
        "minecraft:bouquet",
        "minecraft:burning_skull",
        "minecraft:bust",
        "minecraft:cavebird",
        "minecraft:changing",
        "minecraft:cotan",
        "minecraft:courbet",
        "minecraft:creebet",
        "minecraft:donkey_kong",
        "minecraft:earth",
        "minecraft:endboss",
        "minecraft:fern",
        "minecraft:fighters",
        "minecraft:finding",
        "minecraft:fire",
        "minecraft:graham",
        "minecraft:humble",
        "minecraft:kebab",
        "minecraft:lowmist",
        "minecraft:match",
        "minecraft:meditative",
        "minecraft:orb",
        "minecraft:owlemons",
        "minecraft:passage",
        "minecraft:pigscene",
        "minecraft:plant",
        "minecraft:pointer",
        "minecraft:pond",
        "minecraft:pool",
        "minecraft:prairie_ride",
        "minecraft:sea",
        "minecraft:skeleton",
        "minecraft:skull_and_roses",
        "minecraft:stage",
        "minecraft:sunflowers",
        "minecraft:sunset",
        "minecraft:tides",
        "minecraft:unpacked",
        "minecraft:void",
        "minecraft:wanderer",
        "minecraft:wasteland",
        "minecraft:water",
        "minecraft:wind",
        "minecraft:wither",
    };
    WriteSingleRegistry(client, sendCursor, "minecraft:painting_variant", paintingVariants, ARRAY_SIZE(paintingVariants));
}

static void ClientProcessSinglePacket(Client * client, Cursor * recCursor, Cursor * sendCursor, MemoryArena * arena) {
    i32 packetId = ReadVarU32(recCursor);

    // TODO(traks): We should handle legacy ping requests. They have a different
    // structure than current packets, so some special handling will be needed
    // when we receive them to figure out the packet size and so on.

    switch (client->protocolState) {
    case PROTOCOL_HANDSHAKE: {
        if (packetId != 0) {
            recCursor->error = 1;
        }

        // NOTE(traks): read client intention packet
        i32 protocol_version = ReadVarU32(recCursor);
        String address = ReadVarString(recCursor, 255);
        u16 port = ReadU16(recCursor);
        i32 next_state = ReadVarU32(recCursor);

        if (next_state == 1) {
            client->protocolState = PROTOCOL_AWAIT_STATUS_REQUEST;
        } else if (next_state == 2) {
            // NOTE(traks): login
            if (protocol_version != SERVER_PROTOCOL_VERSION) {
                LogInfo("Client protocol version %jd != %jd", (intmax_t) protocol_version, (intmax_t) SERVER_PROTOCOL_VERSION);
                recCursor->error = 1;
            } else {
                client->protocolState = PROTOCOL_AWAIT_HELLO;
            }
        } else if (next_state == 3) {
            // NOTE(traks): client was transferred to us. Currently we don't
            // allow this for simplicity, so just error out
            recCursor->error = 1;
        } else {
            recCursor->error = 1;
        }
        break;
    }
    case PROTOCOL_AWAIT_STATUS_REQUEST: {
        if (packetId != 0) {
            recCursor->error = 1;
        }

        // NOTE(traks): status request packet is empty

        MemoryArena * scratchArena = &(MemoryArena) {0};
        *scratchArena = *arena;

        int list_size = serv->tab_list_size;
        size_t list_bytes = list_size * sizeof (UUID);
        UUID * list = MallocInArena(scratchArena, list_bytes);
        memcpy(list, serv->tab_list, list_bytes);
        int sample_size = MIN(12, list_size);

        i32 maxResponseSize = 2048;
        unsigned char * response = MallocInArena(scratchArena, maxResponseSize);
        int response_size = 0;
        response_size += snprintf((char *) response + response_size, maxResponseSize - response_size,
                "{\"version\":{\"name\":\"%s\",\"protocol\":%d},"
                "\"players\":{\"max\":%d,\"online\":%d,\"sample\":[",
                SERVER_GAME_VERSION, SERVER_PROTOCOL_VERSION,
                (int) MAX_PLAYERS, (int) list_size);

        // TODO(traks): don't display players with "Allow server listings"
        // turned off, and don't display players we have not yet fully joined
        // (maybe we do this already? should also not count those towards the
        // amount of online players?)
        for (int sampleIndex = 0; sampleIndex < sample_size; sampleIndex++) {
            int target = sampleIndex + (rand() % (list_size - sampleIndex));
            UUID * sampled = list + target;

            if (sampleIndex > 0) {
                response[response_size] = ',';
                response_size += 1;
            }

            PlayerController * control = ResolvePlayer(*sampled);
            assert(control != NULL);
            // @TODO(traks) actual UUID
            response_size += snprintf((char *) response + response_size, maxResponseSize - response_size,
                    "{\"id\":\"01234567-89ab-cdef-0123-456789abcdef\","
                    "\"name\":\"%.*s\"}",
                    (int) control->username_size,
                    control->username);

            *sampled = list[sampleIndex];
        }

        // TODO(traks): support secure chat
        response_size += snprintf((char *) response + response_size, maxResponseSize - response_size,
                "]},\"description\":{\"text\":\"Running Blaze\"},\"enforcesSecureChat\":%s}",
                ENFORCE_SECURE_CHAT ? "true" : "false");

        // NOTE(traks): write status response packet
        BeginPacket(sendCursor, 0);
        WriteVarString(sendCursor, (String) {.data = response, .size = response_size});
        FinishPacket(sendCursor, 0);

        client->protocolState = PROTOCOL_AWAIT_PING_REQUEST;
        break;
    }
    case PROTOCOL_AWAIT_PING_REQUEST: {
        if (packetId != 1) {
            recCursor->error = 1;
        }

        // NOTE(traks): read ping request packet
        u64 payload = ReadU64(recCursor);

        // NOTE(traks): write ping response packet
        BeginPacket(sendCursor, 1);
        WriteU64(sendCursor, payload);
        FinishPacket(sendCursor, 0);

        // TODO(traks): we should really be waiting a bit for the packets to
        // flow to the client
        ClientMarkTerminate(client);
        break;
    }
    case PROTOCOL_AWAIT_HELLO: {
        if (packetId != 0) {
            recCursor->error = 1;
        }

        // NOTE(traks): read hello packet
        String username = ReadVarString(recCursor, 16);
        // @TODO(traks) more username validation
        if (username.size == 0) {
            recCursor->error = 1;
            break;
        }
        memcpy(client->username, username.data, username.size);
        client->usernameSize = username.size;

        // TODO(traks): Apparently vanilla ignores this? I believe Paper
        // generates UUIDs based on the player name in offline mode. Maybe it's
        // better if we do the same?
        UUID uuid = ReadUUID(recCursor);
        client->uuid = uuid;

        // @TODO(traks) online mode

        if (PACKET_COMPRESSION_ENABLED) {
            // NOTE(traks): send login compression packet
            BeginPacket(sendCursor, 3);
            // TODO(traks): for now it is important to compress everything or
            // nothing, because our packet writing utilities don't support a
            // threshold
            WriteVarU32(sendCursor, 0);
            FinishPacket(sendCursor, 0);

            client->flags |= CLIENT_PACKET_COMPRESSION;
        }

        // NOTE(traks): send game profile packet
        BeginPacket(sendCursor, 2);
        WriteUUID(sendCursor, uuid);
        WriteVarString(sendCursor, username);
        WriteVarU32(sendCursor, 0); // no properties for now
        WriteU8(sendCursor, 1); // strict packet processing
        FinishPacket(sendCursor, !!(client->flags & CLIENT_PACKET_COMPRESSION));

        client->protocolState = PROTOCOL_AWAIT_LOGIN_ACK;
        break;
    }
    case PROTOCOL_AWAIT_LOGIN_ACK: {
        if (packetId != 3) {
            recCursor->error = 1;
        }

        // NOTE(traks): we're now in the configuration phase

        // NOTE(traks): send enabled features packet
        BeginPacket(sendCursor, 12);
        WriteVarU32(sendCursor, 1);
        WriteVarString(sendCursor, STR("minecraft:vanilla"));
        FinishPacket(sendCursor, !!(client->flags & CLIENT_PACKET_COMPRESSION));

        // NOTE(traks): send known packs packet
        BeginPacket(sendCursor, 14);
        WriteVarU32(sendCursor, 1);
        WriteVarString(sendCursor, STR("minecraft"));
        WriteVarString(sendCursor, STR("core"));
        WriteVarString(sendCursor, STR(SERVER_GAME_VERSION));
        FinishPacket(sendCursor, !!(client->flags & CLIENT_PACKET_COMPRESSION));
        client->flags |= CLIENT_WANT_KNOWN_PACKS;

        // TODO(traks): we should send all vanilla datapack stuff to the client
        // if it doesn't have the desired core pack. For now we don't do this
        // for simplicity. It's truly a pain to send all that. Sadly 1.21.1 and
        // 1.21 don't use the same pack, even though they're compatible
        // protocol-wise, big bruh moment

        client->protocolState = PROTOCOL_CONFIGURATION;
        break;
    }
    case PROTOCOL_CONFIGURATION: {
        if (packetId == 0) {
            // TODO(traks): this is duplicated in PLAY packet processing

            // NOTE(traks): read client information packet
            String locale = ReadVarString(recCursor, MAX_PLAYER_LOCALE_SIZE);
            memcpy(client->locale, locale.data, locale.size);
            client->localeSize = locale.size;
            i32 viewDistance = ReadU8(recCursor);
            client->chunkCacheRadius = MIN(MAX(viewDistance, 2), MAX_RENDER_DISTANCE) + 1;
            client->chatMode = ReadVarU32(recCursor);
            client->seesChatColours = ReadU8(recCursor);
            client->skinCustomisation = ReadU8(recCursor);
            client->mainHand = ReadVarU32(recCursor);
            client->textFiltering = ReadU8(recCursor);
            client->showInStatusList = ReadU8(recCursor);

            client->flags |= CLIENT_GOT_CLIENT_INFO;
        } else if (packetId == 2) {
            // NOTE(traks): plugin message packet
            String identifier = ReadVarString(recCursor, 32767);
            LogInfo("Got plugin message: %.*s", identifier.size, identifier.data);
            i32 payloadSize = recCursor->size - recCursor->index;
            if (payloadSize > 32767) {
                recCursor->error = 1;
                break;
            }
            CursorSkip(recCursor, payloadSize);
        } else if (packetId == 3) {
            LogInfo("Finish configuration");
            if (!(client->flags & CLIENT_WANT_FINISH_CONFIGURATION)) {
                LogInfo("Received finish configuration ack without request");
                recCursor->error = 1;
                break;
            }

            // NOTE(traks): acknowledgement packet is empty
            client->protocolState = PROTOCOL_JOIN;
        } else if (packetId == 7) {
            if (!(client->flags & CLIENT_WANT_KNOWN_PACKS)) {
                recCursor->error = 1;
                break;
            }
            client->flags &= ~CLIENT_WANT_KNOWN_PACKS;

            // NOTE(traks): read known packs packet
            i32 packCount = ReadVarU32(recCursor);

            // TODO(traks): appropriate limit
            if (packCount > 32) {
                recCursor->error = 1;
                break;
            }

            i32 foundDesiredCorePack = 0;
            for (i32 packIndex = 0; packIndex < packCount; packIndex++) {
                String namespace = ReadVarString(recCursor, 32767);
                String id = ReadVarString(recCursor, 32767);
                String version = ReadVarString(recCursor, 32767);
                if (StringEquals(namespace, STR("minecraft")) && StringEquals(id, STR("core")) && StringEquals(version, STR(SERVER_GAME_VERSION))) {
                    foundDesiredCorePack = 1;
                }
            }

            if (!foundDesiredCorePack) {
                LogInfo("Client doesn't have core pack for %s", SERVER_GAME_VERSION);
                recCursor->error = 1;
                break;
            }

            // NOTE(traks): send all the stupid registries. The only benefit of
            // the core pack is that we don't need to send NBT data of the
            // registry entries
            WriteAllRegistries(client, sendCursor);
            client->flags |= CLIENT_GOT_KNOWN_PACKS;
        } else {
            LogInfo("Unexpected packet %d in configuration phase", (int) packetId);
            recCursor->error = 1;
            break;
        }

        if ((client->flags & CLIENT_GOT_CLIENT_INFO) && (client->flags & CLIENT_GOT_KNOWN_PACKS) && !(client->flags & CLIENT_WANT_FINISH_CONFIGURATION)) {
            // NOTE(traks): send finish configuration packet
            BeginPacket(sendCursor, 3);
            FinishPacket(sendCursor, !!(client->flags & CLIENT_PACKET_COMPRESSION));
            client->flags |= CLIENT_WANT_FINISH_CONFIGURATION;
        }
        break;
    }
    default:
        LogInfo("Protocol state %d not accepting packets", client->protocolState);
        recCursor->error = 1;
        break;
    }
}

static void ClientProcessAllPackets(Client * client) {
    if (client->recBuf.cursor == client->recBuf.size) {
        // NOTE(traks): Should never happen. If there's a full packet in the
        // buffer, we always drain it. Maybe some parse error occurred and we
        // didn't kick the client?
        LogInfo("Client read buffer full");
        ClientMarkTerminate(client);
        return;
    }

    ssize_t receiveSize = recv(client->socket, client->recBuf.data + client->recBuf.cursor, client->recBuf.size - client->recBuf.cursor, 0);

    if (receiveSize == -1) {
        // TODO(traks): or EWOULDBLOCK?
        if (errno == EAGAIN) {
            // NOTE(traks): there is no new data
        } else {
            LogErrno("Couldn't receive protocol data from client: %s");
            ClientMarkTerminate(client);
            return;
        }
    } else {
        client->recBuf.cursor += receiveSize;
    }

    Cursor * recCursor = &(Cursor) {
        .data = client->recBuf.data,
        .size = client->recBuf.cursor,
    };

    // TODO(traks): bad idea to just clobber the entire scratch space, in
    // case anyone else is using it
    MemoryArena * processingArena = &(MemoryArena) {
        .data = serv->short_lived_scratch,
        .size = serv->short_lived_scratch_size
    };

    i32 sendCursorAllocSize = 1 << 20;
    Cursor * sendCursor = &(Cursor) {
        .data = MallocInArena(processingArena, sendCursorAllocSize),
        .size = sendCursorAllocSize,
    };

    for (;;) {
        MemoryArena * loopArena = &(MemoryArena) {0};
        *loopArena = *processingArena;

        Cursor * packetCursor = &(Cursor) {0};
        *packetCursor = TryReadPacket(recCursor, loopArena, !!(client->flags & CLIENT_PACKET_COMPRESSION), client->recBuf.size);

        if (recCursor->error) {
            LogInfo("Incoming packet error");
            ClientMarkTerminate(client);
            return;
        }
        if (packetCursor->size == 0) {
            // NOTE(traks): packet not ready yet
            break;
        }

        ClientProcessSinglePacket(client, packetCursor, sendCursor, loopArena);

        assert(sendCursor->error == 0);

        if (packetCursor->index != packetCursor->size) {
            packetCursor->error = 1;
        }

        if (packetCursor->error != 0) {
            LogInfo("Client protocol error occurred");
            ClientMarkTerminate(client);
            return;
        }

        if (client->flags & CLIENT_SHOULD_TERMINATE) {
            break;
        }
    }

    // NOTE(traks): compact the receive buffer
    memmove(recCursor->data, recCursor->data + recCursor->index, recCursor->size - recCursor->index);
    client->recBuf.cursor = recCursor->size - recCursor->index;

    Cursor * finalCursor = &(Cursor) {
        .data = client->sendBuf.data,
        .size = client->sendBuf.size,
        .index = client->sendBuf.cursor,
    };

    FinalisePackets(finalCursor, sendCursor);

    if (finalCursor->error || sendCursor->error) {
        LogInfo("Failed to finalise packets");
        ClientMarkTerminate(client);
        return;
    }

    client->sendBuf.cursor = finalCursor->index;
}

static void ClientTick(Client * client) {
    assert(client != NULL);

    // NOTE(traks): read incoming data

    BeginTimings(ClientProcessAllPackets);
    ClientProcessAllPackets(client);
    EndTimings(ClientProcessAllPackets);

    // NOTE(traks): send outgoing packets

    ssize_t sendSize = send(client->socket, client->sendBuf.data, client->sendBuf.cursor, 0);

    if (sendSize == -1) {
        if (errno == EAGAIN) {
            // NOTE(traks): The socket's internal send buffer is full
            // TODO(traks): If this error keeps happening, we should probably
            // kick the client
        } else {
            LogErrno("Couldn't send protocol data to client: %s");
            ClientMarkTerminate(client);
            return;
        }
    } else {
        memmove(client->sendBuf.data, client->sendBuf.data + sendSize, client->sendBuf.cursor - sendSize);
        client->sendBuf.cursor -= sendSize;
    }

    if (client->protocolState == PROTOCOL_JOIN && !(client->flags & CLIENT_SHOULD_TERMINATE)) {
        // NOTE(traks): start the PLAY state and transfer the connection to the
        // player entity

        PlayerController * control = CreatePlayer();
        if (control == NULL) {
            // TODO(traks): send some message and disconnect
            ClientMarkTerminate(client);
            return;
        }

        Entity * player = TryReserveEntity(ENTITY_PLAYER);
        if (player->type == ENTITY_NULL) {
            // TODO(traks): send some message and disconnect
            DeletePlayer(control);
            ClientMarkTerminate(client);
            return;
        }

        // @TODO(traks) don't malloc this much when a player joins. AAA
        // games send a lot less than 1MB/tick. For example, according
        // to some website, Fortnite sends about 1.5KB/tick. Although we
        // sometimes have to send a bunch of chunk data, which can be
        // tens of KB. Minecraft even allows up to 2MB of chunk data.

        // TODO(traks): There shouldn't be anything left in the receive
        // buffer/send buffer when we're done here with the initial processing.
        // However, perhaps we should make sure and copy over anything to the
        // player's receive and send buffer?
        control->rec_buf_size = 1 << 16;
        control->rec_buf = malloc(control->rec_buf_size);

        control->send_buf_size = 1 << 20;
        control->send_buf = malloc(control->send_buf_size);

        if (control->rec_buf == NULL || control->send_buf == NULL) {
            // @TODO(traks) send some message on disconnect
            free(control->send_buf);
            free(control->rec_buf);
            EvictEntity(player->id);
            DeletePlayer(control);
            ClientMarkTerminate(client);
            return;
        }

        control->entityId = player->id;
        control->sock = client->socket;
        memcpy(control->username, client->username, client->usernameSize);
        control->username_size = client->usernameSize;
        control->uuid = client->uuid;
        player->uuid = client->uuid;

        memcpy(control->locale, client->locale, client->localeSize);
        control->localeSize = client->localeSize;
        control->chatMode = client->chatMode;
        control->seesChatColours = client->seesChatColours;
        control->skinCustomisation = client->skinCustomisation;
        control->mainHand = client->mainHand;
        control->textFiltering = client->textFiltering;
        control->showInStatusList = client->showInStatusList;
        control->nextChunkCacheRadius = client->chunkCacheRadius;

        control->last_keep_alive_sent_tick = serv->current_tick;
        control->flags |= PLAYER_CONTROL_GOT_ALIVE_RESPONSE;
        if (client->flags & CLIENT_PACKET_COMPRESSION) {
            control->flags |= PLAYER_CONTROL_PACKET_COMPRESSION;
        }
        player->selected_slot = PLAYER_FIRST_HOTBAR_SLOT;
        // @TODO(traks) collision width and height of player depending
        // on player pose
        player->collision_width = 0.6;
        player->collision_height = 1.8;
        SetPlayerGamemode(player, GAMEMODE_CREATIVE);

        player->worldId = 1;
        // TeleportPlayer(player, 88, 70, 73, 0, 0);
        TeleportPlayer(player, 0.5, 140, 0.5, 0, 0);

        // @TODO(traks) ensure this can never happen instead of assering
        // it never will hopefully happen
        assert(serv->tab_list_added_count < (i32) ARRAY_SIZE(serv->tab_list_added));
        serv->tab_list_added[serv->tab_list_added_count] = control->uuid;
        serv->tab_list_added_count++;

        // NOTE(traks): updating the connection will be done on a player basis
        // instead of during initial connection processing
        assert(!(client->flags & CLIENT_SHOULD_TERMINATE));
        client->flags |= CLIENT_DID_TRANSFER_TO_PLAYER;

        LogInfo("Player '%.*s' joined", (int) client->usernameSize, client->username);
        if (serv->global_msg_count < (i32) ARRAY_SIZE(serv->global_msgs)) {
            global_msg * msg = serv->global_msgs + serv->global_msg_count;
            serv->global_msg_count++;
            int textSize = snprintf(
                    (void *) msg->text, sizeof msg->text,
                    "%.*s joined the game",
                    (int) control->username_size, control->username);
            msg->size = textSize;
        }
    }
}

void TickInitialConnections(void) {
    BeginTimings(AcceptInitialConnections);

    for (;;) {
        int accepted = accept(network.serverSocket, NULL, NULL);
        if (accepted == -1) {
            break;
        }
        CreateClient(accepted);
    }

    EndTimings(AcceptInitialConnections);

    BeginTimings(TickClients);

    for (i32 clientIndex = 0; clientIndex < network.clientArraySize; clientIndex++) {
        Client * client = network.clientArray[clientIndex];
        if (client != NULL) {
            ClientTick(client);
            if (client->flags & CLIENT_SHOULD_TERMINATE) {
                DeleteClient(clientIndex);
            } else if (client->flags & CLIENT_DID_TRANSFER_TO_PLAYER) {
                FreeClientNoClose(clientIndex);
            }
        }
    }

    EndTimings(TickClients);
}

void InitNetwork(void) {
    struct sockaddr_in serverAddress = {
        .sin_family = AF_INET,
        .sin_port = htons(25565),
        .sin_addr = {
            .s_addr = htonl(0x7f000001)
            // .s_addr = htonl(0)
        }
    };

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (serverSocket == -1) {
        LogErrno("Failed to create socket: %s");
        exit(1);
    }
    if (serverSocket >= FD_SETSIZE) {
        // can't select on this socket
        LogInfo("Socket is FD_SETSIZE or higher");
        exit(1);
    }

    int yes = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
        LogErrno("Failed to set sock opt: %s");
        exit(1);
    }

    // @TODO(traks) non-blocking connect? Also note that connect will finish
    // asynchronously if it has been interrupted by a signal.
    if (bind(serverSocket, (struct sockaddr *) &serverAddress, sizeof serverAddress) == -1) {
        LogErrno("Can't bind to address: %s");
        exit(1);
    }

    if (listen(serverSocket, 16) == -1) {
        LogErrno("Can't listen: %s");
        exit(1);
    }

    int flags = fcntl(serverSocket, F_GETFL, 0);
    if (flags == -1) {
        LogErrno("Can't get socket flags: %s");
        exit(1);
    }
    if (fcntl(serverSocket, F_SETFL, flags | O_NONBLOCK) == -1) {
        LogErrno("Can't set socket flags: %s");
        exit(1);
    }

    network.serverSocket = serverSocket;

    network.clientArraySize = 32;
    network.clientArray = calloc(1, network.clientArraySize * sizeof *network.clientArray);

    LogInfo("Bound to address");
}
