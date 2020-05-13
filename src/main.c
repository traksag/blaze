#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <mach/mach_time.h>
#include "codec.h"

#define ARRAY_SIZE(x) (sizeof (x) / sizeof *(x))

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define MAX_CHUNK_CACHE_RADIUS (10)

enum gamemode {
    GAMEMODE_SURVIVAL,
    GAMEMODE_CREATIVE,
    GAMEMODE_ADVENTURE,
    GAMEMODE_SPECTATOR,
};

static unsigned long long current_tick;
static int server_sock;
static volatile sig_atomic_t got_sigint;

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
    // @TODO(traks) can maybe be a bit smaller
    unsigned char rec_buf[300];
    int rec_cursor;

    // @TODO(traks) figure out appropriate size
    unsigned char send_buf[600];
    int send_cursor;

    int protocol_state;
    unsigned char username[16];
    int username_size;
} initial_connection;

#define PLAYER_BRAIN_IN_USE ((unsigned) (1 << 0))

#define PLAYER_BRAIN_TELEPORTING ((unsigned) (1 << 1))

#define PLAYER_BRAIN_SENT_TELEPORT ((unsigned) (1 << 2))

#define PLAYER_BRAIN_GOT_ALIVE_RESPONSE ((unsigned) (1 << 3))

#define PLAYER_BRAIN_SHIFTING ((unsigned) (1 << 4))

#define PLAYER_BRAIN_SPRINTING ((unsigned) (1 << 5))

typedef struct {
    int sock;
    unsigned flags;
    unsigned char rec_buf[65536];
    int rec_cursor;

    unsigned char send_buf[1048576];
    int send_cursor;

    unsigned char username[16];
    int username_size;
    // The radius of the client's view distance, excluding the centre chunk,
    // and including an extra outer rim the client doesn't render but uses
    // for connected blocks and such.
    int chunk_cache_radius;
    int new_chunk_cache_radius;
    mc_int current_teleport_id;

    unsigned char language[16];
    int language_size;
    mc_int chat_visibility;
    mc_ubyte sees_chat_colours;
    mc_ubyte model_customisation;
    mc_int main_hand;

    mc_ulong last_keep_alive_id;
} player_brain;

static initial_connection initial_connections[32];
static int initial_connection_count;

static player_brain player_brains[8];
static int player_brain_count;

static void
log(void * format, ...) {
    char msg[128];
    va_list ap;
    va_start(ap, format);
    vsnprintf(msg, sizeof msg, format, ap);
    va_end(ap);

    struct timespec now;
    char unknown_time[] = "?? ??? ???? ??:??:??.???";
    char time[25];
    if (clock_gettime(CLOCK_REALTIME, &now)) {
        // Time doesn't fit in time_t. Print out a bunch of question marks
        // instead of the correct time.
        memcpy(time, unknown_time, sizeof unknown_time);
    } else {
        struct tm local;
        if (!localtime_r(&now.tv_sec, &local)) {
            // Failed to convert time to broken-down time. Again, print out a
            // bunch of question marks instead of the current time.
            memcpy(time, unknown_time, sizeof unknown_time);
        } else {
            int milli = now.tv_nsec / 1000000;
            int second = local.tm_sec;
            int minute = local.tm_min;
            int hour = local.tm_hour;
            int day = local.tm_mday;
            int month = local.tm_mon;
            int year = local.tm_year + 1900;
            char * month_name = "???";
            switch (month) {
            case 0: month_name = "Jan"; break;
            case 1: month_name = "Feb"; break;
            case 2: month_name = "Mar"; break;
            case 3: month_name = "Apr"; break;
            case 4: month_name = "May"; break;
            case 5: month_name = "Jun"; break;
            case 6: month_name = "Jul"; break;
            case 7: month_name = "Aug"; break;
            case 8: month_name = "Sep"; break;
            case 9: month_name = "Oct"; break;
            case 10: month_name = "Nov"; break;
            case 11: month_name = "Dec"; break;
            }
            snprintf(time, sizeof time, "%02d %s %04d %02d:%02d:%02d.%03d", day,
                    month_name, year, hour, minute, second, milli);
        }
    }

    // @TODO(traks) buffer messages instead of going through printf each time?
    printf("%s  %s\n", time, msg);
}

static void
log_errno(void * format) {
    char error_msg[64] = {0};
    strerror_r(errno, error_msg, sizeof error_msg);
    log(format, error_msg);
}

static void
handle_sigint(int sig) {
    got_sigint = 1;
}

static void
server_tick(void) {
    // accept new connections

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
    }

    // update initial connections

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
                log_errno("Couldn't receive protocol data: %s");
                close(sock);
                init_con->flags &= ~INITIAL_CONNECTION_IN_USE;
                initial_connection_count--;
            }
        } else {
            init_con->rec_cursor += rec_size;

            buffer_cursor rec_cursor = {
                .buf = init_con->rec_buf,
                .limit = init_con->rec_cursor
            };
            buffer_cursor send_cursor = {
                .buf = init_con->send_buf,
                .limit = sizeof init_con->send_buf,
                .index = init_con->send_cursor
            };

            for (;;) {
                mc_int packet_size = net_read_varint(&rec_cursor);

                if (rec_cursor.error != 0) {
                    // packet size not fully received yet
                    break;
                }
                if (packet_size <= 0 || packet_size > sizeof init_con->rec_buf) {
                    close(sock);
                    init_con->flags &= ~INITIAL_CONNECTION_IN_USE;
                    initial_connection_count--;
                    break;
                }
                if (packet_size > rec_cursor.limit) {
                    // packet not fully received yet
                    break;
                }

                int packet_start = rec_cursor.index;
                mc_int packet_id = net_read_varint(&rec_cursor);

                switch (init_con->protocol_state) {
                case PROTOCOL_HANDSHAKE: {
                    if (packet_id != 0) {
                        rec_cursor.error = 1;
                    }

                    mc_int protocol_version = net_read_varint(&rec_cursor);
                    net_string address = net_read_string(&rec_cursor, 255);
                    mc_ushort port = net_read_ushort(&rec_cursor);
                    mc_int next_state = net_read_varint(&rec_cursor);

                    if (next_state == 1) {
                        init_con->protocol_state = PROTOCOL_AWAIT_STATUS_REQUEST;
                    } else if (next_state == 2) {
                        init_con->protocol_state = PROTOCOL_AWAIT_HELLO;
                    } else {
                        rec_cursor.error = 1;
                    }
                    break;
                }
                case PROTOCOL_AWAIT_STATUS_REQUEST: {
                    if (packet_id != 0) {
                        rec_cursor.error = 1;
                    }

                    char response[] =
                            "{"
                            "  \"version\": {"
                            "    \"name\": \"1.15.2\","
                            "    \"protocol\": 578"
                            "  },"
                            "  \"players\": {"
                            "    \"max\": 100,"
                            "    \"online\": 0,"
                            "    \"sample\": []"
                            "  },"
                            "  \"description\": {"
                            "    \"text\": \"Running Blaze\""
                            "  }"
                            "}";
                    net_string response_str = {
                        .size = sizeof response - 1,
                        .ptr = response
                    };
                    int out_size = net_varint_size(0)
                            + net_varint_size(response_str.size)
                            + response_str.size;
                    net_write_varint(&send_cursor, out_size);
                    net_write_varint(&send_cursor, 0);
                    net_write_string(&send_cursor, response_str);

                    init_con->protocol_state = PROTOCOL_AWAIT_PING_REQUEST;
                    break;
                }
                case PROTOCOL_AWAIT_PING_REQUEST: {
                    if (packet_id != 1) {
                        rec_cursor.error = 1;
                    }

                    mc_ulong payload = net_read_ulong(&rec_cursor);

                    int out_size = net_varint_size(1) + 8;
                    net_write_varint(&send_cursor, out_size);
                    net_write_varint(&send_cursor, 1);
                    net_write_ulong(&send_cursor, payload);
                    break;
                }
                case PROTOCOL_AWAIT_HELLO: {
                    if (packet_id != 0) {
                        rec_cursor.error = 1;
                    }

                    net_string username = net_read_string(&rec_cursor, 16);
                    // @TODO(traks) more username validation
                    if (username.size == 0) {
                        rec_cursor.error = 1;
                        break;
                    }
                    memcpy(init_con->username, username.ptr, username.size);
                    init_con->username_size = username.size;

                    // @TODO(traks) online mode
                    // @TODO(traks) enable compression

                    init_con->protocol_state = PROTOCOL_JOIN_WHEN_SENT;
                    break;
                }
                default:
                    log("Protocol state %d not accepting packets",
                            init_con->protocol_state);
                    rec_cursor.error = 1;
                    break;
                }

                if (packet_size != rec_cursor.index - packet_start) {
                    rec_cursor.error = 1;
                }

                if (rec_cursor.error != 0) {
                    log("Protocol error occurred");
                    close(sock);
                    init_con->flags = 0;
                    initial_connection_count--;
                    break;
                }
            }

            memmove(rec_cursor.buf, rec_cursor.buf + rec_cursor.index,
                    rec_cursor.limit - rec_cursor.index);
            init_con->rec_cursor = rec_cursor.limit - rec_cursor.index;

            init_con->send_cursor = send_cursor.index;
        }
    }

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
                log_errno("Couldn't send protocol data: %s");
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

                if (player_brain_count == ARRAY_SIZE(player_brains)) {
                    // @TODO(traks) send server full message and disconnect
                    close(init_con->sock);
                    continue;
                }

                player_brain * brain;
                for (int j = 0; j < ARRAY_SIZE(player_brains); j++) {
                    brain = player_brains + j;
                    if ((brain->flags & PLAYER_BRAIN_IN_USE) != 0) {
                        continue;
                    }
                }

                *brain = (player_brain) {0};
                brain->flags |= PLAYER_BRAIN_IN_USE;
                brain->sock = init_con->sock;
                memcpy(brain->username, init_con->username,
                        init_con->username_size);
                brain->username_size = init_con->username_size;
                // @TODO(traks) server-wide global
                brain->new_chunk_cache_radius = MAX_CHUNK_CACHE_RADIUS;

                player_brain_count++;

                buffer_cursor send_cursor = {
                    .buf = brain->send_buf,
                    .limit = sizeof brain->send_buf
                };

                // send game profile packet
                net_string username = {brain->username_size, brain->username};
                net_string uuid_str = NET_STRING("01234567-89ab-cdef-0123-456789abcdef");
                int out_size = net_varint_size(2)
                        + net_varint_size(uuid_str.size) + uuid_str.size
                        + net_varint_size(username.size) + username.size;
                net_write_varint(&send_cursor, out_size);
                net_write_varint(&send_cursor, 2);
                net_write_string(&send_cursor, uuid_str);
                net_write_string(&send_cursor, username);

                // send login packet
                net_string level_type = NET_STRING("customized");
                out_size = net_varint_size(38) + 4 + 1 + 4 + 8 + 1
                        + net_varint_size(level_type.size) + level_type.size
                        + net_varint_size(brain->new_chunk_cache_radius - 1) + 1 + 1;
                net_write_varint(&send_cursor, out_size);
                net_write_varint(&send_cursor, 38);
                // @TODO(traks) implement entity IDs
                net_write_uint(&send_cursor, 0);
                net_write_ubyte(&send_cursor, GAMEMODE_CREATIVE);
                net_write_uint(&send_cursor, 0); // environment
                net_write_ulong(&send_cursor, 0); // seed
                net_write_ubyte(&send_cursor, 0); // max players (ignored by client)
                net_write_string(&send_cursor, level_type);
                net_write_varint(&send_cursor, brain->new_chunk_cache_radius - 1);
                net_write_ubyte(&send_cursor, 0); // reduced debug info
                net_write_ubyte(&send_cursor, 1); // show death screen on death

                // @TODO(traks) send player position packet (after chunks?)

                brain->send_cursor = send_cursor.index;

                log("Player '%.*s' joined", (int) brain->username_size,
                        brain->username);
            }
        }
    }

    // update players

    for (int i = 0; i < ARRAY_SIZE(player_brains); i++) {
        player_brain * brain = player_brains + i;
        if ((brain->flags & PLAYER_BRAIN_IN_USE) == 0) {
            continue;
        }

        int sock = brain->sock;
        ssize_t rec_size = recv(sock, brain->rec_buf + brain->rec_cursor,
                sizeof brain->rec_buf - brain->rec_cursor, 0);

        if (rec_size == 0) {
            close(sock);
            brain->flags = 0;
            player_brain_count--;
        } else if (rec_size == -1) {
            // EAGAIN means no data received
            if (errno != EAGAIN) {
                log_errno("Couldn't receive protocol data: %s");
                close(sock);
                brain->flags = 0;
                player_brain_count--;
            }
        } else {
            brain->rec_cursor += rec_size;

            buffer_cursor rec_cursor = {
                .buf = brain->rec_buf,
                .limit = brain->rec_cursor
            };
            buffer_cursor send_cursor = {
                .buf = brain->send_buf,
                .limit = sizeof brain->send_buf,
                .index = brain->send_cursor
            };

            for (;;) {
                mc_int packet_size = net_read_varint(&rec_cursor);

                if (rec_cursor.error != 0) {
                    // packet size not fully received yet
                    break;
                }
                if (packet_size <= 0 || packet_size > sizeof brain->rec_buf) {
                    close(sock);
                    brain->flags = 0;
                    player_brain_count--;
                    break;
                }
                if (packet_size > rec_cursor.limit) {
                    // packet not fully received yet
                    break;
                }

                int packet_start = rec_cursor.index;
                mc_int packet_id = net_read_varint(&rec_cursor);

                switch (packet_id) {
                case 0: { // accept teleport
                    mc_int teleport_id = net_read_varint(&rec_cursor);

                    if ((brain->flags & PLAYER_BRAIN_TELEPORTING)
                            && (brain->flags & PLAYER_BRAIN_SENT_TELEPORT)
                            && teleport_id == brain->current_teleport_id) {
                        brain->flags &= ~(PLAYER_BRAIN_TELEPORTING | PLAYER_BRAIN_SENT_TELEPORT);
                    }
                    break;
                }
                case 1: { // block entity tag query
                    log("Packet block entity tag query");
                    mc_int id = net_read_varint(&rec_cursor);
                    mc_ulong block_pos = net_read_ulong(&rec_cursor);
                    // @TODO(traks) handle packet
                    break;
                }
                case 2: { // change difficulty
                    log("Packet change difficulty");
                    mc_ubyte difficulty = net_read_ubyte(&rec_cursor);
                    // @TODO(traks) handle packet
                    break;
                }
                case 3: { // chat
                    log("Packet chat");
                    net_string msg = net_read_string(&rec_cursor, 256);
                    break;
                }
                case 4: { // client command
                    log("Packet client command");
                    mc_int action = net_read_varint(&rec_cursor);
                    break;
                }
                case 5: { // client information
                    log("Packet client information");
                    net_string language = net_read_string(&rec_cursor, 16);
                    mc_ubyte view_distance = net_read_ubyte(&rec_cursor);
                    mc_int chat_visibility = net_read_varint(&rec_cursor);
                    mc_ubyte sees_chat_colours = net_read_ubyte(&rec_cursor);
                    mc_ubyte model_customisation = net_read_ubyte(&rec_cursor);
                    mc_int main_hand = net_read_varint(&rec_cursor);

                    // View distance is without the extra border of chunks,
                    // while chunk cache radius is with the extra border of
                    // chunks. This clamps the view distance between the minimum
                    // of 2 and the server maximum.
                    brain->new_chunk_cache_radius = MIN(MAX(view_distance, 2),
                            MAX_CHUNK_CACHE_RADIUS - 1) + 1;
                    memcpy(brain->language, language.ptr, language.size);
                    brain->language_size = language.size;
                    brain->sees_chat_colours = sees_chat_colours;
                    brain->model_customisation = model_customisation;
                    brain->main_hand = main_hand;
                    break;
                }
                case 6: { // command suggestion
                    log("Packet command suggestion");
                    mc_int id = net_read_varint(&rec_cursor);
                    net_string command = net_read_string(&rec_cursor, 32500);
                    break;
                }
                case 7: { // container ack
                    log("Packet container ack");
                    mc_ubyte container_id = net_read_ubyte(&rec_cursor);
                    mc_ushort uid = net_read_ushort(&rec_cursor);
                    mc_ubyte accepted = net_read_ubyte(&rec_cursor);
                    break;
                }
                case 8: { // container button click
                    log("Packet container button click");
                    mc_ubyte container_id = net_read_ubyte(&rec_cursor);
                    mc_ubyte button_id = net_read_ubyte(&rec_cursor);
                    break;
                }
                case 9: { // container click
                    log("Packet container click");
                    mc_ubyte container_id = net_read_ubyte(&rec_cursor);
                    mc_ushort slot = net_read_ushort(&rec_cursor);
                    mc_ubyte button = net_read_ubyte(&rec_cursor);
                    mc_ushort uid = net_read_ushort(&rec_cursor);
                    mc_int click_type = net_read_varint(&rec_cursor);
                    // @TODO(traks) read item
                    break;
                }
                case 10: { // container close
                    log("Packet container close");
                    mc_ubyte container_id = net_read_ubyte(&rec_cursor);
                    break;
                }
                case 11: { // custom payload
                    log("Packet custom payload");
                    net_string id = net_read_string(&rec_cursor, 32767);
                    unsigned char * payload = rec_cursor.buf + rec_cursor.index;
                    mc_int payload_size = packet_start + packet_size
                            - rec_cursor.index;

                    if (payload_size > 32767) {
                        // custom payload size too large
                        rec_cursor.error = 1;
                        break;
                    }

                    rec_cursor.index += payload_size;
                    break;
                }
                case 12: { // edit book
                    log("Packet edit book");
                    // @TODO(traks) read packet
                    break;
                }
                case 13: { // entity tag query
                    log("Packet entity tag query");
                    mc_int transaction_id = net_read_varint(&rec_cursor);
                    mc_int entity_id = net_read_varint(&rec_cursor);
                    break;
                }
                case 14: { // interact
                    log("Packet interact");
                    mc_int entity_id = net_read_varint(&rec_cursor);
                    mc_int action = net_read_varint(&rec_cursor);
                    // @TODO further reading
                    break;
                }
                case 15: { // keep alive
                    mc_ulong id = net_read_ulong(&rec_cursor);
                    if (brain->last_keep_alive_id == id) {
                        brain->flags |= PLAYER_BRAIN_GOT_ALIVE_RESPONSE;
                    }
                    break;
                }
                case 16: { // lock difficulty
                    log("Packet lock difficulty");
                    mc_ubyte locked = net_read_ubyte(&rec_cursor);
                    break;
                }
                case 17: { // move player pos
                    // @TODO(traks) read packet
                    break;
                }
                case 18: { // move player pos rot
                    // @TODO(traks) read packet
                    break;
                }
                case 19: { // move player rot
                    // @TODO(traks) read packet
                    break;
                }
                case 20: { // move player
                    // @TODO(traks) read packet
                    break;
                }
                case 21: { // move vehicle
                    log("Packet move vehicle");
                    // @TODO(traks) read packet
                    break;
                }
                case 22: { // paddle boat
                    log("Packet paddle boat");
                    mc_ubyte left = net_read_ubyte(&rec_cursor);
                    mc_ubyte right = net_read_ubyte(&rec_cursor);
                    break;
                }
                case 23: { // pick item
                    log("Packet pick item");
                    mc_int slot = net_read_varint(&rec_cursor);
                    break;
                }
                case 24: { // place recipe
                    log("Packet place recipe");
                    mc_ubyte container_id = net_read_ubyte(&rec_cursor);
                    // @TODO read recipe
                    mc_ubyte shift_down = net_read_ubyte(&rec_cursor);
                    break;
                }
                case 25: { // player abilities
                    log("Packet player abilities");
                    // @TODO(traks) read packet
                    break;
                }
                case 26: { // player action
                    mc_int action = net_read_varint(&rec_cursor);
                    mc_ulong pos = net_read_ulong(&rec_cursor);
                    mc_ubyte direction = net_read_ubyte(&rec_cursor);

                    switch (action) {
                    case 0: { // start destroy block
                        // The player started mining the block. If the player is in
                        // creative mode, the stop and abort packets are not sent.
                        // @TODO(traks)
                        break;
                    }
                    case 1: { // abort destroy block
                        // The player stopped mining the block before it breaks.
                        // @TODO(traks)
                        break;
                    }
                    case 2: { // stop destroy block
                        // The player stopped mining the block because it broke.
                        // @TODO(traks)
                        break;
                    }
                    case 3: { // drop all items
                        // @TODO(traks)
                        break;
                    }
                    case 4: { // drop item
                        // @TODO(traks)
                        break;
                    }
                    case 5: { // release use item
                        // @TODO(traks)
                        break;
                    }
                    case 6: { // swap held items
                        // @TODO(traks)
                        break;
                    }
                    default:
                        rec_cursor.error = 1;
                    }
                    break;
                }
                case 27: { // player command
                    mc_int id = net_read_varint(&rec_cursor);
                    mc_int action = net_read_varint(&rec_cursor);
                    mc_int data = net_read_varint(&rec_cursor);

                    switch (action) {
                    case 0: // press shift key
                        brain->flags |= PLAYER_BRAIN_SHIFTING;
                        break;
                    case 1: // release shift key
                        brain->flags &= ~PLAYER_BRAIN_SHIFTING;
                        break;
                    case 2: // stop sleeping
                        // @TODO(traks)
                        break;
                    case 3: // start sprinting
                        brain->flags |= PLAYER_BRAIN_SPRINTING;
                        break;
                    case 4: // stop sprinting
                        brain->flags &= ~PLAYER_BRAIN_SPRINTING;
                        break;
                    case 5: // start riding jump
                        // @TODO(traks)
                        break;
                    case 6: // stop riding jump
                        // @TODO(traks)
                        break;
                    case 7: // open inventory
                        // @TODO(traks)
                        break;
                    case 8: // start fall flying
                        // @TODO(traks)
                        break;
                    default:
                        rec_cursor.error = 1;
                    }
                    break;
                }
                case 28: { // player input
                    log("Packet player input");
                    // @TODO(traks) read packet
                    break;
                }
                case 29: { // recipe book update
                    log("Packet recipe book update");
                    // @TODO(traks) read packet
                    break;
                }
                case 30: { // rename item
                    log("Packet rename item");
                    net_string name = net_read_string(&rec_cursor, 32767);
                    break;
                }
                case 31: { // resource pack
                    log("Packet resource pack");
                    mc_int action = net_read_varint(&rec_cursor);
                    break;
                }
                case 32: { // seen advancements
                    log("Packet seen advancements");
                    mc_int action = net_read_varint(&rec_cursor);
                    // @TODO(traks) further processing
                    break;
                }
                case 33: { // select trade
                    log("Packet select trade");
                    mc_int item = net_read_varint(&rec_cursor);
                    break;
                }
                case 34: { // set beacon
                    log("Packet set beacon");
                    mc_int primary_effect = net_read_varint(&rec_cursor);
                    mc_int secondary_effect = net_read_varint(&rec_cursor);
                    break;
                }
                case 35: { // set carried item
                    mc_ushort slot = net_read_ushort(&rec_cursor);
                    // @TODO(traks) handle packet
                    break;
                }
                case 36: { // set command block
                    log("Packet set command block");
                    mc_ulong block_pos = net_read_ulong(&rec_cursor);
                    net_string command = net_read_string(&rec_cursor, 32767);
                    mc_int mode = net_read_varint(&rec_cursor);
                    mc_ubyte flags = net_read_ubyte(&rec_cursor);
                    mc_ubyte track_output = (flags & 0x1);
                    mc_ubyte conditional = (flags & 0x2);
                    mc_ubyte automatic = (flags & 0x4);
                    break;
                }
                case 37: { // set command minecart
                    log("Packet set command minecart");
                    mc_int entity_id = net_read_varint(&rec_cursor);
                    net_string command = net_read_string(&rec_cursor, 32767);
                    mc_ubyte track_output = net_read_ubyte(&rec_cursor);
                    break;
                }
                case 38: { // set creative mode slot
                    log("Packet set creative mode slot");
                    mc_ushort slot = net_read_ushort(&rec_cursor);
                    mc_ubyte has_item = net_read_ubyte(&rec_cursor);
                    // @TODO(traks) further reading and processing
                    break;
                }
                case 39: { // set jigsaw block
                    log("Packet set jigsaw block");
                    mc_ulong block_pos = net_read_ulong(&rec_cursor);
                    // @TODO(traks) further reading
                    break;
                }
                case 40: { // set structure block
                    log("Packet set structure block");
                    mc_ulong block_pos = net_read_ulong(&rec_cursor);
                    mc_int update_type = net_read_varint(&rec_cursor);
                    mc_int mode = net_read_varint(&rec_cursor);
                    net_string name = net_read_string(&rec_cursor, 32767);
                    // @TODO(traks) read signed bytes instead
                    mc_ubyte offset_x = net_read_ubyte(&rec_cursor);
                    mc_ubyte offset_y = net_read_ubyte(&rec_cursor);
                    mc_ubyte offset_z = net_read_ubyte(&rec_cursor);
                    mc_ubyte size_x = net_read_ubyte(&rec_cursor);
                    mc_ubyte size_y = net_read_ubyte(&rec_cursor);
                    mc_ubyte size_z = net_read_ubyte(&rec_cursor);
                    mc_int mirror = net_read_varint(&rec_cursor);
                    mc_int rotation = net_read_varint(&rec_cursor);
                    net_string data = net_read_string(&rec_cursor, 12);
                    // @TODO(traks) further reading
                    break;
                }
                case 41: { // sign update
                    log("Packet sign update");
                    mc_ulong block_pos = net_read_ulong(&rec_cursor);
                    net_string lines[4];
                    for (int i = 0; i < ARRAY_SIZE(lines); i++) {
                        lines[i] = net_read_string(&rec_cursor, 384);
                    }
                    break;
                }
                case 42: { // swing
                    log("Packet swing");
                    mc_int hand = net_read_varint(&rec_cursor);
                    break;
                }
                case 43: { // teleport to entity
                    log("Packet teleport to entity");
                    // @TODO(traks) read UUID instead
                    mc_ulong uuid_high = net_read_ulong(&rec_cursor);
                    mc_ulong uuid_low = net_read_ulong(&rec_cursor);
                    break;
                }
                case 44: { // use item on
                    mc_int hand = net_read_varint(&rec_cursor);
                    mc_ulong clicked_block_pos = net_read_ulong(&rec_cursor);
                    mc_int clicked_face = net_read_varint(&rec_cursor);
                    // @TODO(traks) read further
                    break;
                }
                case 45: { // use item
                    log("Packet use item");
                    mc_int hand = net_read_varint(&rec_cursor);
                    break;
                }
                default: {
                    log("Unknown player packet id %jd", (intmax_t) packet_id);
                    rec_cursor.error = 1;
                }
                }

                if (packet_size != rec_cursor.index - packet_start) {
                    rec_cursor.error = 1;
                }

                if (rec_cursor.error != 0) {
                    log("Protocol error occurred");
                    close(sock);
                    brain->flags = 0;
                    player_brain_count--;
                    break;
                }
            }

            memmove(rec_cursor.buf, rec_cursor.buf + rec_cursor.index,
                    rec_cursor.limit - rec_cursor.index);
            brain->rec_cursor = rec_cursor.limit - rec_cursor.index;

            brain->send_cursor = send_cursor.index;
        }
    }

    for (int i = 0; i < ARRAY_SIZE(player_brains); i++) {
        player_brain * brain = player_brains + i;
        if ((brain->flags & PLAYER_BRAIN_IN_USE) == 0) {
            continue;
        }

        int sock = brain->sock;
        ssize_t send_size = send(sock, brain->send_buf,
                brain->send_cursor, 0);

        if (send_size == -1) {
            // EAGAIN means no data sent
            if (errno != EAGAIN) {
                log_errno("Couldn't send protocol data: %s");
                close(sock);
                brain->flags = 0;
                player_brain_count--;
            }
        } else {
            memmove(brain->send_buf, brain->send_buf + send_size,
                    brain->send_cursor - send_size);
            brain->send_cursor -= send_size;
        }
    }

    current_tick++;
}

int
main(void) {
    log("Running Blaze");

    // Ignore SIGPIPE so the server doesn't crash (by getting signals) if a
    // client decides to abruptly close its end of the connection.
    signal(SIGPIPE, SIG_IGN);

    // @TODO(traks) ctrl+c is useful for debugging if the program ends up inside
    // an infinite loop
    // signal(SIGINT, handle_sigint);

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(25565),
        .sin_addr = {
            .s_addr = htonl(0x7f000001)
        }
    };

    server_sock = socket(AF_INET, SOCK_STREAM, 0);

    if (server_sock == -1) {
        log_errno("Failed to create socket: %s");
        exit(1);
    }
    if (server_sock >= FD_SETSIZE) {
        // can't select on this socket
        log("Socket is FD_SETSIZE or higher");
        exit(1);
    }

    struct sockaddr * addr_ptr = (struct sockaddr *) &server_addr;

    int yes = 1;
    if (setsockopt(server_sock, SOL_SOCKET,
            SO_REUSEADDR, &yes, sizeof yes) == -1) {
        log_errno("Failed to set sock opt: %s");
        exit(1);
    }

    // @TODO(traks) non-blocking connect? Also note that connect will finish
    // asynchronously if it has been interrupted by a signal.
    if (bind(server_sock, addr_ptr, sizeof server_addr) == -1) {
        log_errno("Can't bind to address: %s");
        exit(1);
    }

    if (listen(server_sock, 16) == -1) {
        log_errno("Can't listen: %s");
        exit(1);
    }

    int flags = fcntl(server_sock, F_GETFL, 0);

    if (flags == -1) {
        log_errno("Can't get socket flags: %s");
        exit(1);
    }

    if (fcntl(server_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        log_errno("Can't set socket flags: %s");
        exit(1);
    }

    log("Bound to address");

    mach_timebase_info_data_t timebase_info;
    mach_timebase_info(&timebase_info);

    for (;;) {
        unsigned long long start_time = mach_absolute_time();

        server_tick();

        unsigned long long end_time = mach_absolute_time();
        unsigned long long elapsed_micros = (end_time - start_time)
                * timebase_info.numer / timebase_info.denom / 1000;

        if (got_sigint) {
            log("Interrupted");
            break;
        }

        if (elapsed_micros < 50000) {
            usleep(50000 - elapsed_micros);
        }
    }

    log("Goodbye!");
    return 0;
}
