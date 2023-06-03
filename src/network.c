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

static int server_sock;

#define INITIAL_CONNECTION_IN_USE ((unsigned) (1 << 0))

enum initial_protocol_state {
    PROTOCOL_HANDSHAKE,
    PROTOCOL_AWAIT_STATUS_REQUEST,
    PROTOCOL_AWAIT_PING_REQUEST,
    PROTOCOL_AWAIT_HELLO,
    PROTOCOL_JOIN_WHEN_SENT,
};

typedef struct {
    int sock;
    unsigned flags;

    // Should be large enough to:
    //
    //  1. Receive a client intention (handshake) packet, status request and
    //     ping request packet all in one go and store them together in the
    //     buffer.
    //
    //  2. Receive a client intention packet and hello packet and store them
    //     together inside the receive buffer.
    unsigned char rec_buf[1024];
    int rec_cursor;

    // @TODO(traks) figure out appropriate size
    unsigned char send_buf[2048];
    int send_cursor;

    int protocol_state;
    unsigned char username[16];
    int username_size;
} initial_connection;

static initial_connection initial_connections[32];
static int initial_connection_count;

void TickInitialConnections(void) {
    // accept new connections
    BeginTimings(AcceptInitialConnections);

    for (;;) {
        int accepted = accept(server_sock, NULL, NULL);
        if (accepted == -1) {
            break;
        }

        if (initial_connection_count == ARRAY_SIZE(initial_connections)) {
            close(accepted);
            continue;
        }

        int flags = fcntl(accepted, F_GETFL, 0);

        if (flags == -1) {
            close(accepted);
            continue;
        }

        if (fcntl(accepted, F_SETFL, flags | O_NONBLOCK) == -1) {
            close(accepted);
            continue;
        }

        // NOTE(traks): we write all packet data in one go, so this setting
        // shouldn't affect things too much, except for sending the last couple
        // of packets earlier if they're small.
        //
        // I think this will also send TCP ACKs faster. Might give the client's
        // TCP stack a better approximation for server latency? Not sure.
        int yes = 1;
        if (setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes) == -1) {
            close(accepted);
            continue;
        }

        // @TODO(traks) should we lower the receive and send buffer sizes? For
        // hanshakes/status/login they don't need to be as large as the default
        // (probably around 200KB).

        initial_connection * new_connection;
        for (int i = 0; ; i++) {
            new_connection = initial_connections + i;
            if ((new_connection->flags & INITIAL_CONNECTION_IN_USE) == 0) {
                break;
            }
        }

        *new_connection = (initial_connection) {0};
        new_connection->sock = accepted;
        new_connection->flags |= INITIAL_CONNECTION_IN_USE;
        initial_connection_count++;
        LogInfo("Created initial connection");
    }

    EndTimings(AcceptInitialConnections);

    // update initial connections
    BeginTimings(UpdateInitialConnections);

    for (int i = 0; i < ARRAY_SIZE(initial_connections); i++) {
        initial_connection * init_con = initial_connections + i;
        if ((init_con->flags & INITIAL_CONNECTION_IN_USE) == 0) {
            continue;
        }

        int sock = init_con->sock;
        ssize_t rec_size = recv(sock, init_con->rec_buf + init_con->rec_cursor,
                sizeof init_con->rec_buf - init_con->rec_cursor, 0);

        if (rec_size == 0) {
            close(sock);
            init_con->flags &= ~INITIAL_CONNECTION_IN_USE;
            initial_connection_count--;
        } else if (rec_size == -1) {
            // EAGAIN means no data received
            if (errno != EAGAIN) {
                LogErrno("Couldn't receive protocol data: %s");
                close(sock);
                init_con->flags &= ~INITIAL_CONNECTION_IN_USE;
                initial_connection_count--;
            }
        } else {
            init_con->rec_cursor += rec_size;

            Cursor rec_cursor = {
                .data = init_con->rec_buf,
                .size = init_con->rec_cursor
            };
            Cursor send_cursor = {
                .data = init_con->send_buf,
                .size = sizeof init_con->send_buf,
                .index = init_con->send_cursor
            };

            for (;;) {
                i32 packet_size = ReadVarU32(&rec_cursor);

                if (rec_cursor.error != 0) {
                    // packet size not fully received yet
                    break;
                }
                if (packet_size <= 0 || packet_size > sizeof init_con->rec_buf) {
                    LogInfo("Packet size error: %d", packet_size);
                    close(sock);
                    init_con->flags &= ~INITIAL_CONNECTION_IN_USE;
                    initial_connection_count--;
                    break;
                }
                if (packet_size > rec_cursor.size) {
                    // packet not fully received yet
                    break;
                }

                int packet_start = rec_cursor.index;
                i32 packet_id = ReadVarU32(&rec_cursor);
                LogInfo("Initial packet %d", packet_id);

                switch (init_con->protocol_state) {
                case PROTOCOL_HANDSHAKE: {
                    if (packet_id != 0) {
                        rec_cursor.error = 1;
                    }

                    // read client intention packet
                    i32 protocol_version = ReadVarU32(&rec_cursor);
                    String address = ReadVarString(&rec_cursor, 255);
                    u16 port = ReadU16(&rec_cursor);
                    i32 next_state = ReadVarU32(&rec_cursor);

                    if (next_state == 1) {
                        init_con->protocol_state = PROTOCOL_AWAIT_STATUS_REQUEST;
                    } else if (next_state == 2) {
                        if (protocol_version != SERVER_PROTOCOL_VERSION) {
                            LogInfo("Client protocol version %jd != %jd",
                                    (intmax_t) protocol_version,
                                    (intmax_t) SERVER_PROTOCOL_VERSION);
                            rec_cursor.error = 1;
                        } else {
                            init_con->protocol_state = PROTOCOL_AWAIT_HELLO;
                        }
                    } else {
                        rec_cursor.error = 1;
                    }
                    break;
                }
                case PROTOCOL_AWAIT_STATUS_REQUEST: {
                    if (packet_id != 0) {
                        rec_cursor.error = 1;
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

                    int out_size = VarU32Size(0)
                            + VarU32Size(response_size)
                            + response_size;
                    int start = send_cursor.index;
                    WriteVarU32(&send_cursor, out_size);
                    WriteVarU32(&send_cursor, 0);
                    WriteVarU32(&send_cursor, response_size);
                    WriteData(&send_cursor, response, response_size);

                    init_con->protocol_state = PROTOCOL_AWAIT_PING_REQUEST;
                    break;
                }
                case PROTOCOL_AWAIT_PING_REQUEST: {
                    if (packet_id != 1) {
                        rec_cursor.error = 1;
                    }

                    // read ping request packet
                    u64 payload = ReadU64(&rec_cursor);

                    int out_size = VarU32Size(1) + 8;
                    WriteVarU32(&send_cursor, out_size);
                    WriteVarU32(&send_cursor, 1);
                    WriteU64(&send_cursor, payload);
                    break;
                }
                case PROTOCOL_AWAIT_HELLO: {
                    if (packet_id != 0) {
                        rec_cursor.error = 1;
                    }

                    // read hello packet
                    String username = ReadVarString(&rec_cursor, 16);
                    // @TODO(traks) more username validation
                    if (username.size == 0) {
                        rec_cursor.error = 1;
                        break;
                    }
                    memcpy(init_con->username, username.data, username.size);
                    init_con->username_size = username.size;

                    i32 hasUuid = ReadU8(&rec_cursor);
                    if (hasUuid) {
                        u64 uuid_high = ReadU64(&rec_cursor);
                        u64 uuid_low = ReadU64(&rec_cursor);
                        // TODO(traks): do something with the UUID
                    }

                    // @TODO(traks) online mode
                    // @TODO(traks) enable compression

                    init_con->protocol_state = PROTOCOL_JOIN_WHEN_SENT;
                    break;
                }
                default:
                    LogInfo("Protocol state %d not accepting packets",
                            init_con->protocol_state);
                    rec_cursor.error = 1;
                    break;
                }

                assert(send_cursor.error == 0);

                if (packet_size != rec_cursor.index - packet_start) {
                    rec_cursor.error = 1;
                }

                if (rec_cursor.error != 0) {
                    LogInfo("Initial connection protocol error occurred");
                    close(sock);
                    init_con->flags = 0;
                    initial_connection_count--;
                    break;
                }
            }

            memmove(rec_cursor.data, rec_cursor.data + rec_cursor.index,
                    rec_cursor.size - rec_cursor.index);
            init_con->rec_cursor = rec_cursor.size - rec_cursor.index;

            init_con->send_cursor = send_cursor.index;
        }
    }

    EndTimings(UpdateInitialConnections);

    BeginTimings(SendInitialConnections);

    for (int i = 0; i < ARRAY_SIZE(initial_connections); i++) {
        initial_connection * init_con = initial_connections + i;
        if ((init_con->flags & INITIAL_CONNECTION_IN_USE) == 0) {
            continue;
        }

        int sock = init_con->sock;
        ssize_t send_size = send(sock, init_con->send_buf,
                init_con->send_cursor, 0);

        if (send_size == -1) {
            // EAGAIN means no data sent
            if (errno != EAGAIN) {
                LogErrno("Couldn't send protocol data: %s");
                close(sock);
                init_con->flags = 0;
                initial_connection_count--;
            }
        } else {
            memmove(init_con->send_buf, init_con->send_buf + send_size,
                    init_con->send_cursor - send_size);
            init_con->send_cursor -= send_size;

            if (init_con->send_cursor == 0
                    && init_con->protocol_state == PROTOCOL_JOIN_WHEN_SENT) {
                init_con->flags = 0;
                initial_connection_count--;

                entity_base * entity = try_reserve_entity(ENTITY_PLAYER);

                if (entity->type == ENTITY_NULL) {
                    // @TODO(traks) send some message and disconnect
                    close(init_con->sock);
                    continue;
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
                    close(init_con->sock);
                    continue;
                }

                player->sock = init_con->sock;
                memcpy(player->username, init_con->username,
                        init_con->username_size);
                player->username_size = init_con->username_size;
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

                // teleport_player(entity, 88, 70, 73, 0, 0);
                teleport_player(entity, 0.5, 140, 0.5, 0, 0);

                // @TODO(traks) ensure this can never happen instead of assering
                // it never will hopefully happen
                assert(serv->tab_list_added_count < ARRAY_SIZE(serv->tab_list_added));
                serv->tab_list_added[serv->tab_list_added_count] = entity->eid;
                serv->tab_list_added_count++;

                LogInfo("Player '%.*s' joined", (int) init_con->username_size,
                        init_con->username);
            }
        }
    }

    EndTimings(SendInitialConnections);
}

void InitNetwork(void) {
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(25565),
        .sin_addr = {
            .s_addr = htonl(0x7f000001)
            // .s_addr = htonl(0)
        }
    };

    server_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (server_sock == -1) {
        LogErrno("Failed to create socket: %s");
        exit(1);
    }
    if (server_sock >= FD_SETSIZE) {
        // can't select on this socket
        LogInfo("Socket is FD_SETSIZE or higher");
        exit(1);
    }

    struct sockaddr * addr_ptr = (struct sockaddr *) &server_addr;

    int yes = 1;
    if (setsockopt(server_sock, SOL_SOCKET,
            SO_REUSEADDR, &yes, sizeof yes) == -1) {
        LogErrno("Failed to set sock opt: %s");
        exit(1);
    }

    // @TODO(traks) non-blocking connect? Also note that connect will finish
    // asynchronously if it has been interrupted by a signal.
    if (bind(server_sock, addr_ptr, sizeof server_addr) == -1) {
        LogErrno("Can't bind to address: %s");
        exit(1);
    }

    if (listen(server_sock, 16) == -1) {
        LogErrno("Can't listen: %s");
        exit(1);
    }

    int flags = fcntl(server_sock, F_GETFL, 0);

    if (flags == -1) {
        LogErrno("Can't get socket flags: %s");
        exit(1);
    }

    if (fcntl(server_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        LogErrno("Can't set socket flags: %s");
        exit(1);
    }

    LogInfo("Bound to address");
}
