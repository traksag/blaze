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

#define CLIENT_SHOULD_TERMINATE ((u32) 1 << 0)
#define CLIENT_DID_TRANSFER_TO_PLAYER ((u32) 1 << 1)

enum ProtocolState {
    PROTOCOL_HANDSHAKE,
    PROTOCOL_AWAIT_STATUS_REQUEST,
    PROTOCOL_AWAIT_PING_REQUEST,
    PROTOCOL_AWAIT_HELLO,
    PROTOCOL_JOIN_WHEN_SENT,
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
    Buffer recPacketBuf;
    i32 recPacketBufConsumed;

    int protocolState;
    unsigned char username[16];
    int usernameSize;
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
    i32 receiveBufferSize = 1024;
    // TODO(traks): figure out appropriate size
    i32 sendBufferSize = 2048;
    i32 receivePacketBufferSize = 1024;
    client->recBuf = (Buffer) {
        .data = malloc(receiveBufferSize),
        .size = receiveBufferSize,
    };
    client->sendBuf = (Buffer) {
        .data = malloc(sendBufferSize),
        .size = sendBufferSize,
    };
    client->recPacketBuf = (Buffer) {
        .data = malloc(receivePacketBufferSize),
        .size = receivePacketBufferSize,
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
    client->flags |= CLIENT_SHOULD_TERMINATE;
}

static void ClientReadAllPackets(Client * client) {
    if (client->recBuf.cursor == client->recBuf.size) {
        // NOTE(traks): Should never happen. If there's a full packet in the
        // buffer, we always drain it. Maybe some parse error occurred and we
        // didn't kick the client?
        LogInfo("Client read buffer full");
        ClientMarkTerminate(client);
        return;
    }

    ssize_t receiveSize = recv(client->socket, client->recBuf.data + client->recBuf.cursor, client->recBuf.size - client->recBuf.cursor, 0);

    if (receiveSize == 0) {
        // NOTE(traks): client closed its end of the connection
        LogInfo("Client disconnected itself");
        ClientMarkTerminate(client);
        return;
    } else if (receiveSize == -1) {
        if (errno == EAGAIN) {
            // NOTE(traks): EAGAIN means there was no new data in the socket's
            // internal receive buffer
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

    // NOTE(traks): compact the incoming packet buffer
    memmove(client->recPacketBuf.data, client->recPacketBuf.data + client->recPacketBufConsumed, client->recPacketBuf.cursor - client->recPacketBufConsumed);
    client->recPacketBuf.cursor -= client->recPacketBufConsumed;
    client->recPacketBufConsumed = 0;

    for (;;) {
        CursorSetMark(recCursor);
        i32 packetSizeRemaining = ReadVarU32(recCursor);
        i32 prefixSize = recCursor->index - recCursor->mark;
        CursorRewind(recCursor);

        if (recCursor->error != 0) {
            // NOTE(traks): packet size not fully received yet
            break;
        }

        i32 maxRemaining = client->recBuf.size - prefixSize;
        if (packetSizeRemaining <= 0 || packetSizeRemaining > maxRemaining) {
            LogInfo("Packet size error: %d, max: %d", packetSizeRemaining, maxRemaining);
            ClientMarkTerminate(client);
            return;
        }

        i32 totalPacketSize = packetSizeRemaining + prefixSize;
        if (totalPacketSize > CursorRemaining(recCursor)) {
            // NOTE(traks): packet not fully received yet
            break;
        }

        if (totalPacketSize > (client->recPacketBuf.size - client->recPacketBuf.cursor)) {
            LogInfo("Client sending too much data");
            ClientMarkTerminate(client);
            return;
        }

        memcpy(client->recPacketBuf.data + client->recPacketBuf.cursor, recCursor->data + recCursor->index, totalPacketSize);
        client->recPacketBuf.cursor += totalPacketSize;

        recCursor->index += totalPacketSize;
    }

    memmove(recCursor->data, recCursor->data + recCursor->index, recCursor->size - recCursor->index);
    client->recBuf.cursor = recCursor->size - recCursor->index;
}

static Cursor ClientGetNextPacket(Client * client) {
    Cursor res = {0};
    if (client->recPacketBufConsumed == client->recPacketBuf.cursor) {
        return res;
    }

    Cursor * temp = &(Cursor) {
        .data = client->recPacketBuf.data,
        .size = client->recPacketBuf.cursor,
        .index = client->recPacketBufConsumed,
    };
    i32 packetSizeRemaining = ReadVarU32(temp);

    res.data = temp->data + temp->index;
    res.size = packetSizeRemaining;

    client->recPacketBufConsumed = temp->index + packetSizeRemaining;
    return res;
}

static void ClientProcessSinglePacket(Client * client, Cursor * recCursor, Cursor * sendCursor) {
    i32 packetId = ReadVarU32(recCursor);

    switch (client->protocolState) {
    case PROTOCOL_HANDSHAKE: {
        if (packetId != 0) {
            recCursor->error = 1;
        }

        // read client intention packet
        i32 protocol_version = ReadVarU32(recCursor);
        String address = ReadVarString(recCursor, 255);
        u16 port = ReadU16(recCursor);
        i32 next_state = ReadVarU32(recCursor);

        if (next_state == 1) {
            client->protocolState = PROTOCOL_AWAIT_STATUS_REQUEST;
        } else if (next_state == 2) {
            if (protocol_version != SERVER_PROTOCOL_VERSION) {
                LogInfo("Client protocol version %jd != %jd", (intmax_t) protocol_version, (intmax_t) SERVER_PROTOCOL_VERSION);
                recCursor->error = 1;
            } else {
                client->protocolState = PROTOCOL_AWAIT_HELLO;
            }
        } else {
            recCursor->error = 1;
        }
        break;
    }
    case PROTOCOL_AWAIT_STATUS_REQUEST: {
        if (packetId != 0) {
            recCursor->error = 1;
        }

        // read status request packet
        // empty

        MemoryArena scratch_arena = {
            .data = serv->short_lived_scratch,
            .size = serv->short_lived_scratch_size
        };

        int list_size = serv->tab_list_size;
        size_t list_bytes = list_size * sizeof (entity_id);
        entity_id * list = MallocInArena(
                &scratch_arena, list_bytes);
        memcpy(list, serv->tab_list, list_bytes);
        int sample_size = MIN(12, list_size);

        unsigned char * response = MallocInArena(&scratch_arena, 2048);
        int response_size = 0;
        response_size += sprintf((char *) response + response_size,
                "{\"version\":{\"name\":\"%s\",\"protocol\":%d},"
                "\"players\":{\"max\":%d,\"online\":%d,\"sample\":[",
                SERVER_GAME_VERSION, SERVER_PROTOCOL_VERSION,
                (int) MAX_PLAYERS, (int) list_size);

        for (int sampleIndex = 0; sampleIndex < sample_size; sampleIndex++) {
            int target = sampleIndex + (rand() % (list_size - sampleIndex));
            entity_id * sampled = list + target;

            if (sampleIndex > 0) {
                response[response_size] = ',';
                response_size += 1;
            }

            entity_base * entity = resolve_entity(*sampled);
            // @TODO(traks) this assert fired, not sure how that
            // happened. Can leave it for now, since these things
            // will may need to be rewritten anyway if we decide to
            // move all initial session stuff to a separate thread.
            assert(entity->type == ENTITY_PLAYER);
            // @TODO(traks) actual UUID
            response_size += sprintf((char *) response + response_size,
                    "{\"id\":\"01234567-89ab-cdef-0123-456789abcdef\","
                    "\"name\":\"%.*s\"}",
                    (int) entity->player.username_size,
                    entity->player.username);

            *sampled = list[sampleIndex];
        }

        // TODO(traks): implement chat previewing
        response_size += sprintf((char *) response + response_size,
                "]},\"description\":{\"text\":\"Running Blaze\"}}");

        int out_size = VarU32Size(0) + VarU32Size(response_size) + response_size;
        int start = sendCursor->index;
        WriteVarU32(sendCursor, out_size);
        WriteVarU32(sendCursor, 0);
        WriteVarU32(sendCursor, response_size);
        WriteData(sendCursor, response, response_size);

        client->protocolState = PROTOCOL_AWAIT_PING_REQUEST;
        break;
    }
    case PROTOCOL_AWAIT_PING_REQUEST: {
        if (packetId != 1) {
            recCursor->error = 1;
        }

        // read ping request packet
        u64 payload = ReadU64(recCursor);

        int out_size = VarU32Size(1) + 8;
        WriteVarU32(sendCursor, out_size);
        WriteVarU32(sendCursor, 1);
        WriteU64(sendCursor, payload);
        break;
    }
    case PROTOCOL_AWAIT_HELLO: {
        if (packetId != 0) {
            recCursor->error = 1;
        }

        // read hello packet
        String username = ReadVarString(recCursor, 16);
        // @TODO(traks) more username validation
        if (username.size == 0) {
            recCursor->error = 1;
            break;
        }
        memcpy(client->username, username.data, username.size);
        client->usernameSize = username.size;

        i32 hasUuid = ReadU8(recCursor);
        if (hasUuid) {
            u64 uuid_high = ReadU64(recCursor);
            u64 uuid_low = ReadU64(recCursor);
            // TODO(traks): do something with the UUID
        }

        // @TODO(traks) online mode
        // @TODO(traks) enable compression

        client->protocolState = PROTOCOL_JOIN_WHEN_SENT;
        break;
    }
    default:
        LogInfo("Protocol state %d not accepting packets", client->protocolState);
        recCursor->error = 1;
        break;
    }
}

static void ClientProcessAllPackets(Client * client) {
    Cursor * sendCursor = &(Cursor) {
        .data = client->sendBuf.data,
        .size = client->sendBuf.size,
        .index = client->sendBuf.cursor,
    };

    for (;;) {
        Cursor packetCursor = ClientGetNextPacket(client);
        if (packetCursor.data == NULL) {
            break;
        }

        ClientProcessSinglePacket(client, &packetCursor, sendCursor);

        assert(sendCursor->error == 0);

        if (packetCursor.index != packetCursor.size) {
            packetCursor.error = 1;
        }

        if (packetCursor.error != 0) {
            LogInfo("Client protocol error occurred");
            ClientMarkTerminate(client);
            return;
        }
    }

    client->sendBuf.cursor = sendCursor->index;
}

static void ClientTick(Client * client) {
    assert(client != NULL);

    // NOTE(traks): read incoming data

    BeginTimings(ClientReadAllPackets);
    ClientReadAllPackets(client);
    EndTimings(ClientReadAllPackets);

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

    // NOTE(traks): start the PLAY state and transfer the connection to the
    // player entity

    if (client->sendBuf.cursor == 0 && client->protocolState == PROTOCOL_JOIN_WHEN_SENT) {
        entity_base * entity = try_reserve_entity(ENTITY_PLAYER);

        if (entity->type == ENTITY_NULL) {
            // @TODO(traks) send some message and disconnect
            ClientMarkTerminate(client);
            return;
        }

        entity_player * player = &entity->player;

        // @TODO(traks) don't malloc this much when a player joins. AAA
        // games send a lot less than 1MB/tick. For example, according
        // to some website, Fortnite sends about 1.5KB/tick. Although we
        // sometimes have to send a bunch of chunk data, which can be
        // tens of KB. Minecraft even allows up to 2MB of chunk data.
        player->rec_buf_size = 1 << 16;
        player->rec_buf = malloc(player->rec_buf_size);

        player->send_buf_size = 1 << 20;
        player->send_buf = malloc(player->send_buf_size);

        if (player->rec_buf == NULL || player->send_buf == NULL) {
            // @TODO(traks) send some message on disconnect
            free(player->send_buf);
            free(player->rec_buf);
            evict_entity(entity->eid);
            ClientMarkTerminate(client);
            return;
        }

        player->sock = client->socket;
        memcpy(player->username, client->username, client->usernameSize);
        player->username_size = client->usernameSize;
        player->chunkCacheRadius = -1;
        // @TODO(traks) configurable server-wide global
        player->nextChunkCacheRadius = MAX_CHUNK_CACHE_RADIUS;
        player->last_keep_alive_sent_tick = serv->current_tick;
        entity->flags |= PLAYER_GOT_ALIVE_RESPONSE;
        player->selected_slot = PLAYER_FIRST_HOTBAR_SLOT;
        // @TODO(traks) collision width and height of player depending
        // on player pose
        entity->collision_width = 0.6;
        entity->collision_height = 1.8;
        set_player_gamemode(entity, GAMEMODE_CREATIVE);

        entity->worldId = 1;
        // teleport_player(entity, 88, 70, 73, 0, 0);
        teleport_player(entity, 0.5, 140, 0.5, 0, 0);

        // @TODO(traks) ensure this can never happen instead of assering
        // it never will hopefully happen
        assert(serv->tab_list_added_count < (i32) ARRAY_SIZE(serv->tab_list_added));
        serv->tab_list_added[serv->tab_list_added_count] = entity->eid;
        serv->tab_list_added_count++;

        client->flags |= CLIENT_DID_TRANSFER_TO_PLAYER;

        LogInfo("Player '%.*s' joined", (int) client->usernameSize, client->username);
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
