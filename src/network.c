#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include "shared.h"
#include "buffer.h"
#include "packet.h"
#include "player.h"

// TODO(traks): should increase this
#define MAX_INITIAL_CONNECTIONS (32)

#define INITIAL_CONNECTION_TIMEOUT_MILLIS ((i32) 10 * 1000)

#define MAX_ACCEPT_ROUNDS (16)

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
    PROTOCOL_AWAIT_CLOSE,
    PROTOCOL_AWAIT_HELLO,
    PROTOCOL_AWAIT_LOGIN_ACK,
    PROTOCOL_CONFIGURATION,
    PROTOCOL_JOIN,
};

typedef struct {
    u8 * data;
    i32 writeCursor;
    i32 size;
} Buffer;

typedef struct {
    int socket;
    u32 flags;

    Buffer recBuf;
    Buffer sendBuf;

    i64 lastUpdateNanos;

    i32 protocolState;
    UUID uuid;
    u8 username[MAX_PLAYER_NAME_SIZE];
    i32 usernameSize;

    u8 locale[MAX_PLAYER_LOCALE_SIZE];
    i32 localeSize;
    i32 chunkCacheRadius;
    i32 chatMode;
    i32 seesChatColours;
    u8 skinCustomisation;
    i32 mainHand;
    i32 textFiltering;
    i32 showInStatusList;
    i32 particleStatus;
} Client;

typedef struct {
    Client * clientArray[MAX_INITIAL_CONNECTIONS];
    struct pollfd pollArray[MAX_INITIAL_CONNECTIONS + 1];
    i32 clientCount;
    int serverSocket;
    pthread_t thread;
    MemoryArena pollArena;
} Network;

static Network network;

static void CreateClient(int clientSocket, i64 nanoTime) {
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

    i32 clientIndex = network.clientCount;
    if (clientIndex >= (i32) ARRAY_SIZE(network.clientArray)) {
        LogInfo("No space for more clients");
        close(clientSocket);
        return;
    }
    network.clientCount++;

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
    client->lastUpdateNanos = nanoTime;

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
    i32 sendBufferSize = 32 << 10;
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

static void WriteRegistryEntries(Client * client, Cursor * sendCursor, Registry * registry) {
    // NOTE(traks): registry data packet
    BeginPacket(sendCursor, 7);
    String registryName = {.data = registry->name, .size = registry->nameSize};
    WriteVarString(sendCursor, registryName);
    WriteVarU32(sendCursor, registry->entryCount);
    for (i32 id = 0; id < registry->entryCount; id++) {
        String entryName = ResolveRegistryEntryName(registry, id);
        WriteVarString(sendCursor, entryName);
        WriteU8(sendCursor, 0); // no data
    }
    FinishPacket(sendCursor, !!(client->flags & CLIENT_PACKET_COMPRESSION));
}

static void WriteAllRegistries(Client * client, Cursor * sendCursor) {
    Registry * registries[] = {
        &serv->blockRegistry,
        &serv->itemRegistry,
        &serv->entityTypeRegistry,
        &serv->fluidRegistry,
        &serv->gameEventRegistry,
        &serv->biomeRegistry,
        &serv->chatTypeRegistry,
        &serv->trimPatternRegistry,
        &serv->trimMaterialRegistry,
        &serv->wolfVariantRegistry,
        &serv->paintingVariantRegistry,
        &serv->dimensionTypeRegistry,
        &serv->damageTypeRegistry,
        &serv->bannerPatternRegistry,
        &serv->enchantmentRegistry,
        &serv->jukeboxSongRegistry,
        &serv->instrumentRegistry,
    };

    for (i32 registryIndex = 0; registryIndex < (i32) ARRAY_SIZE(registries); registryIndex++) {
        Registry * registry = registries[registryIndex];
        if (registry->sendEntriesToClients) {
            WriteRegistryEntries(client, sendCursor, registry);
        }
    }

    Registry * registriesWithTags[ARRAY_SIZE(registries)];
    i32 registriesWithTagsCount = 0;

    for (i32 registryIndex = 0; registryIndex < (i32) ARRAY_SIZE(registries); registryIndex++) {
        Registry * registry = registries[registryIndex];
        if (registry->tagCount > 0) {
            registriesWithTags[registriesWithTagsCount++] = registry;
        }
    }

    // NOTE(traks): update tags packet
    BeginPacket(sendCursor, 13);
    WriteVarU32(sendCursor, registriesWithTagsCount);
    for (i32 registryIndex = 0; registryIndex < registriesWithTagsCount; registryIndex++) {
        Registry * registry = registriesWithTags[registryIndex];
        String registryName = {.data = registry->name, .size = registry->nameSize};

        WriteVarString(sendCursor, registryName);
        WriteVarU32(sendCursor, registry->tagCount);

        for (i32 tagIndex = 0; tagIndex < registry->tagCount; tagIndex++) {
            RegistryTagInfo tag = registry->tagBuffer[tagIndex];
            String tagName = GetRegistryString(registry, tag.nameIndex);
            i32 * tagValues = registry->tagValueBuffer + tag.valueIndex;
            i32 valueCount = tagValues[0];
            tagValues++;

            WriteVarString(sendCursor, tagName);
            WriteVarU32(sendCursor, valueCount);

            for (i32 valueIndex = 0; valueIndex < valueCount; valueIndex++) {
                i32 valueId = tagValues[valueIndex];
                WriteVarU32(sendCursor, valueId);
            }
        }
    }
    FinishPacket(sendCursor, !!(client->flags & CLIENT_PACKET_COMPRESSION));
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

        i32 listSize = MAX_PLAYERS;
        PlayerListEntry * list = MallocInArena(scratchArena, listSize * sizeof *list);
        listSize = CopyPlayerList(list, listSize);
        i32 sampleSize = MIN(12, listSize);

        i32 maxResponseSize = 2048;
        unsigned char * response = MallocInArena(scratchArena, maxResponseSize);
        int response_size = 0;
        response_size += snprintf((char *) response + response_size, maxResponseSize - response_size,
                "{\"version\":{\"name\":\"%s\",\"protocol\":%d},"
                "\"players\":{\"max\":%d,\"online\":%d,\"sample\":[",
                SERVER_GAME_VERSION, SERVER_PROTOCOL_VERSION,
                (int) MAX_PLAYERS, (int) listSize);

        // TODO(traks): don't display players with "Allow server listings"
        // turned off, and don't display players who have not yet fully joined
        // (maybe we do this already? should also not count those towards the
        // amount of online players?)
        for (i32 sampleIndex = 0; sampleIndex < sampleSize; sampleIndex++) {
            i32 targetIndex = sampleIndex + (rand() % (listSize - sampleIndex));
            PlayerListEntry * sampled = &list[targetIndex];

            if (sampleIndex > 0) {
                response[response_size] = ',';
                response_size += 1;
            }

            // TODO(traks): actual UUID
            response_size += snprintf((char *) response + response_size, maxResponseSize - response_size,
                    "{\"id\":\"01234567-89ab-cdef-0123-456789abcdef\","
                    "\"name\":\"%.*s\"}",
                    (int) sampled->usernameSize,
                    sampled->username);

            *sampled = list[sampleIndex];
        }

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

        client->protocolState = PROTOCOL_AWAIT_CLOSE;
        break;
    }
    case PROTOCOL_AWAIT_CLOSE: {
        // NOTE(traks): shouldn't be receiving any packets
        recCursor->error = 1;
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

        // NOTE(traks): send login finish packet
        BeginPacket(sendCursor, 2);
        WriteUUID(sendCursor, uuid);
        WriteVarString(sendCursor, username);
        WriteVarU32(sendCursor, 0); // no properties for now
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
            client->particleStatus = ReadVarU32(recCursor);

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
    if (client->recBuf.writeCursor == client->recBuf.size) {
        // NOTE(traks): Should never happen. If there's a full packet in the
        // buffer, we always drain it. Maybe some parse error occurred and we
        // didn't kick the client?
        LogInfo("Client read buffer full");
        ClientMarkTerminate(client);
        return;
    }

    ssize_t receiveSize = recv(client->socket, client->recBuf.data + client->recBuf.writeCursor, client->recBuf.size - client->recBuf.writeCursor, 0);

    if (receiveSize == -1) {
        // TODO(traks): or EWOULDBLOCK?
        if (errno == EAGAIN) {
            // NOTE(traks): there is no new data
        } else {
            LogErrno("Couldn't receive protocol data from client: %s");
            ClientMarkTerminate(client);
        }
        return;
    }
    client->recBuf.writeCursor += receiveSize;

    Cursor * recCursor = &(Cursor) {
        .data = client->recBuf.data,
        .size = client->recBuf.writeCursor,
    };

    MemoryArena * processingArena = &(MemoryArena) {0};
    *processingArena = network.pollArena;

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
    client->recBuf.writeCursor -= recCursor->index;

    Cursor * finalCursor = &(Cursor) {
        .data = client->sendBuf.data,
        .size = client->sendBuf.size,
        .index = client->sendBuf.writeCursor,
    };

    FinalisePackets(finalCursor, sendCursor);

    if (finalCursor->error || sendCursor->error) {
        LogInfo("Failed to finalise packets");
        ClientMarkTerminate(client);
        return;
    }

    client->sendBuf.writeCursor = finalCursor->index;

    if (client->protocolState == PROTOCOL_JOIN && !(client->flags & CLIENT_SHOULD_TERMINATE)) {
        // NOTE(traks): start the PLAY state and transfer the connection to the
        // player entity
        if (client->sendBuf.writeCursor > 0 || client->recBuf.writeCursor > 0) {
            // NOTE(traks): we still have inbound/outbound pending data, huh?
            ClientMarkTerminate(client);
            LogInfo("Client is still transferring data while joining");
            return;
        }

        JoinRequest request = {0};
        request.socket = client->socket;
        request.packetCompression = !!(client->flags & CLIENT_PACKET_COMPRESSION);
        request.uuid = client->uuid;
        memcpy(request.username, client->username, client->usernameSize);
        request.usernameSize = client->usernameSize;
        memcpy(request.locale, client->locale, client->localeSize);
        request.localeSize = client->localeSize;
        request.chunkCacheRadius = client->chunkCacheRadius;
        request.chatMode = client->chatMode;
        request.seesChatColours = client->seesChatColours;
        request.skinCustomisation = client->skinCustomisation;
        request.mainHand = client->mainHand;
        request.textFiltering = client->textFiltering;
        request.showInStatusList = client->showInStatusList;
        request.particleStatus = client->particleStatus;
        if (!QueuePlayerJoin(request)) {
            LogInfo("Join queue is full");
            ClientMarkTerminate(client);
        } else {
            LogInfo("Transferring client to player");
            client->flags |= CLIENT_DID_TRANSFER_TO_PLAYER;
        }
    }
}

// NOTE(traks): It is kind of imperative we read/write asynchronously from the
// main tick loop, instead of e.g. reading/writing only on the main thread at
// the start/end of each tick. This allows us to respond immediately to ping
// requests during status requests and during normal gameplay. This is necessary
// to compute accurate ping times.
//
// It is also important because it allows us to continuously write to clients.
// If the OS write buffer can't hold all the packets we're trying to send, this
// method allows us to still flush all the packets before the next tick ends.
// For example, we can stream more chunks to clients this way.
static void * RunNetwork(void * arg) {
    for (;;) {
        // NOTE(traks): build new poll structs
        BeginTimings(BuildPollStructs);
        i32 pollCount = 0;
        network.pollArray[pollCount++] = (struct pollfd) {.fd = network.serverSocket, .events = POLLRDNORM};
        for (i32 clientIndex = 0; clientIndex < network.clientCount; clientIndex++) {
            Client * client = network.clientArray[clientIndex];
            struct pollfd * pollEntry = &network.pollArray[pollCount++];
            *pollEntry = (struct pollfd) {.fd = client->socket};
            pollEntry->events |= POLLRDNORM;
            if (client->sendBuf.writeCursor > 0) {
                pollEntry->events |= POLLWRNORM;
            }
        }
        EndTimings(BuildPollStructs);

        int pollTimeout = -1;
        if (pollCount > 1) {
            pollTimeout = INITIAL_CONNECTION_TIMEOUT_MILLIS;
        }

        i32 readyCount = poll(network.pollArray, pollCount, pollTimeout);
        if (readyCount == -1) {
            if (errno == EINTR) {
                continue;
            }
            LogErrno("Failed to poll network connections: %s");
            break;
        }

        i64 nanoTime = NanoTime();

        // NOTE(traks): run updates
        BeginTimings(ProcessPollUpdates);
        {
            struct pollfd * pollEntry = &network.pollArray[0];
            if (pollEntry->revents & (POLLERR | POLLNVAL | POLLHUP)) {
                LogInfo("Failed to poll network server socket");
                break;
            }
            if (pollEntry->revents & POLLRDNORM) {
                for (i32 round = 0; round < MAX_ACCEPT_ROUNDS; round++) {
                    int accepted = accept(network.serverSocket, NULL, NULL);
                    if (accepted == -1) {
                        if (errno == EINTR) {
                            continue;
                        }
                        if (errno == EWOULDBLOCK) {
                            // NOTE(traks): no more new connections
                            break;
                        }
                        LogErrno("Failed to accept socket: %s");
                        break;
                    }
                    CreateClient(accepted, nanoTime);
                }
            }
        }
        // NOTE(traks): the accept loop above could have created more clients.
        // Be careful not to check the poll structs for those!
        for (i32 clientIndex = 0; clientIndex < pollCount - 1; clientIndex++) {
            Client * client = network.clientArray[clientIndex];
            struct pollfd * pollEntry = &network.pollArray[clientIndex + 1];
            assert(client->socket == pollEntry->fd);

            if (pollEntry->revents == 0) {
                if (nanoTime - client->lastUpdateNanos > INITIAL_CONNECTION_TIMEOUT_MILLIS * (i64) 1000000) {
                    LogInfo("Client was inactive for too long");
                    ClientMarkTerminate(client);
                }
                continue;
            }

            client->lastUpdateNanos = nanoTime;

            if (pollEntry->revents & (POLLERR | POLLNVAL)) {
                LogInfo("Bad poll");
                ClientMarkTerminate(client);
            }
            if (pollEntry->revents & POLLRDNORM) {
                // NOTE(traks): first read, then write, because reading may
                // generate more outbound data
                ClientProcessAllPackets(client);
            }
            if (pollEntry->revents & POLLWRNORM) {
                ssize_t sendSize = send(client->socket, client->sendBuf.data, client->sendBuf.writeCursor, 0);
                if (sendSize == -1) {
                    LogErrno("Couldn't send protocol data to client: %s");
                    ClientMarkTerminate(client);
                    continue;
                }
                memmove(client->sendBuf.data, client->sendBuf.data + sendSize, client->sendBuf.writeCursor - sendSize);
                client->sendBuf.writeCursor -= sendSize;
            }
            if (pollEntry->revents & POLLHUP) {
                // NOTE(traks): do this after reading, because there may
                // still be some data for us left to read after a disconnect
                LogInfo("Client disconnected");
                ClientMarkTerminate(client);
            }
        }
        EndTimings(ProcessPollUpdates);

        // NOTE(traks): clean up closed sockets
        BeginTimings(PollCleanup);
        for (i32 clientIndex = 0; clientIndex < network.clientCount; clientIndex++) {
            Client * client = network.clientArray[clientIndex];
            if (client->flags & CLIENT_DID_TRANSFER_TO_PLAYER) {
                // NOTE(traks): don't terminate even if the termination flag is
                // set, because the socket has been transferred to the player
                // controller now
                FreeClientNoClose(clientIndex);
                network.clientArray[clientIndex] = network.clientArray[network.clientCount - 1];
                network.clientCount--;
                clientIndex--;
            } else if (client->flags & CLIENT_SHOULD_TERMINATE) {
                LogInfo("Terminating client");
                DeleteClient(clientIndex);
                network.clientArray[clientIndex] = network.clientArray[network.clientCount - 1];
                network.clientCount--;
                clientIndex--;
            }
        }
        EndTimings(PollCleanup);
    }

    // NOTE(traks): clean up connections on errors, so the clients know
    // immediately something went wrong
    close(network.serverSocket);
    for (i32 clientIndex = 0; clientIndex < network.clientCount; clientIndex++) {
        Client * client = network.clientArray[clientIndex];
        close(client->socket);
    }
    network.clientCount = 0;
    return NULL;
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

    if (listen(serverSocket, MAX_ACCEPT_ROUNDS) == -1) {
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

    i32 arenaSize = 4 << 20;
    network.pollArena = (MemoryArena) {
        .size = arenaSize,
        .data = malloc(arenaSize),
    };
    if (network.pollArena.data == NULL) {
        LogInfo("Failed to allocate network arena memory");
        exit(1);
    }

    if (pthread_create(&network.thread, NULL, RunNetwork, NULL)) {
        LogInfo("Failed to create networking thread");
        exit(1);
    }

    LogInfo("Bound to address");
}
