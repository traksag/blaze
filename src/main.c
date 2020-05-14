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
#include <limits.h>
#include <x86intrin.h>
#include "codec.h"

#define ARRAY_SIZE(x) (sizeof (x) / sizeof *(x))

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define MAX_CHUNK_CACHE_RADIUS (10)

#define MAX_CHUNK_CACHE_DIAM (2 * MAX_CHUNK_CACHE_RADIUS + 1)

#define KEEP_ALIVE_SPACING (10 * 20)

#define KEEP_ALIVE_TIMEOUT (30 * 20)

#define MAX_CHUNK_SENDS_PER_TICK (2)

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

typedef struct {
    mc_short x;
    mc_short z;
} chunk_pos;

typedef struct {
    mc_ushort * sections[16];
    mc_ushort non_air_count[16];
    // need shorts to store 257 different heights
    mc_ushort motion_blocking_height_map[256];
} chunk;

#define CHUNKS_PER_BUCKET (32)

#define CHUNK_MAP_SIZE (1024)

typedef struct chunk_bucket chunk_bucket;

struct chunk_bucket {
    chunk_bucket * next_bucket;
    int size;
    chunk_pos positions[CHUNKS_PER_BUCKET];
    chunk chunks[CHUNKS_PER_BUCKET];
};

#define PLAYER_BRAIN_IN_USE ((unsigned) (1 << 0))

#define PLAYER_BRAIN_TELEPORTING ((unsigned) (1 << 1))

#define PLAYER_BRAIN_SENT_TELEPORT ((unsigned) (1 << 2))

#define PLAYER_BRAIN_GOT_ALIVE_RESPONSE ((unsigned) (1 << 3))

#define PLAYER_BRAIN_SHIFTING ((unsigned) (1 << 4))

#define PLAYER_BRAIN_SPRINTING ((unsigned) (1 << 5))

#define PLAYER_BRAIN_ON_GROUND ((unsigned) (1 << 6))

typedef struct {
    unsigned char sent;
} chunk_cache_entry;

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
    mc_short chunk_cache_centre_x;
    mc_short chunk_cache_centre_z;
    int new_chunk_cache_radius;
    // @TODO(traks) maybe this should just be a bitmap
    chunk_cache_entry chunk_cache[MAX_CHUNK_CACHE_DIAM * MAX_CHUNK_CACHE_DIAM];

    mc_int current_teleport_id;

    unsigned char language[16];
    int language_size;
    mc_int chat_visibility;
    mc_ubyte sees_chat_colours;
    mc_ubyte model_customisation;
    mc_int main_hand;

    mc_ulong last_keep_alive_sent_tick;

    mc_double x;
    mc_double y;
    mc_double z;
    mc_float rot_x;
    mc_float rot_y;
} player_brain;

static initial_connection initial_connections[32];
static int initial_connection_count;

static player_brain player_brains[8];
static int player_brain_count;

// @TODO(traks) don't use a hash map. Performance depends on the chunks loaded,
// which depends on the positions of players in the world. Doesn't seem good
// that players moving to certain locations can cause performance drops...
//
// Our goal is 1000 players, so if we construct our hash and hash map in an
// appropriate way, we can ensure there are at most 1000 entries per bucket.
// Not sure if that's any good.
static chunk_bucket chunk_map[CHUNK_MAP_SIZE];

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
teleport_player(player_brain * brain, mc_double new_x,
        mc_double new_y, mc_double new_z) {
    brain->flags |= PLAYER_BRAIN_TELEPORTING;

    brain->current_teleport_id++;
    brain->x = new_x;
    brain->y = new_y;
    brain->z = new_z;
}

static void
process_move_player_packet(player_brain * brain,
        mc_double new_x, mc_double new_y, mc_double new_z,
        mc_float new_rot_x, mc_float new_rot_y, int on_ground) {
    if ((brain->flags & PLAYER_BRAIN_TELEPORTING) != 0) {
        return;
    }

    brain->x = new_x;
    brain->y = new_y;
    brain->z = new_z;
    brain->rot_x = new_rot_x;
    brain->rot_y = new_rot_y;
    if (on_ground) {
        brain->flags |= PLAYER_BRAIN_ON_GROUND;
    } else {
        brain->flags &= ~PLAYER_BRAIN_ON_GROUND;
    }
}

static int
chunk_cache_index(chunk_pos pos) {
    // Do some remainder operations first so we don't integer overflow. Note
    // that the remainder operator can produce negative numbers.
    int n = MAX_CHUNK_CACHE_DIAM * MAX_CHUNK_CACHE_DIAM;
    long x = (pos.x % n) + n;
    long z = (pos.z % n) + n;
    return (x * MAX_CHUNK_CACHE_DIAM + z) % n;
}

static int
chunk_pos_equal(chunk_pos a, chunk_pos b) {
    // @TODO(traks) make sure this compiles to a single compare. If not, should
    // probably change chunk_pos to be a uint32_t.
    return a.x == b.x && a.z == b.z;
}

static int
hash_chunk_pos(chunk_pos pos) {
    // @TODO(traks) this signed-to-unsigned cast better work
    return (((unsigned) pos.x & 0x1f) << 5) | ((unsigned) pos.z & 0x1f);
}

static mc_ushort
chunk_get_block_state(chunk * ch, int x, int y, int z) {
    assert(0 <= x && x < 16);
    assert(0 <= y && y < 256);
    assert(0 <= z && z < 16);

    int section_y = y >> 4;
    mc_ushort * section = ch->sections[section_y];

    if (section == NULL) {
        return 0;
    }

    int index = (y << 8) | (z << 4) | x;
    return section[index];
}

static void
chunk_set_block_state(chunk * ch, int x, int y, int z, mc_ushort block_state) {
    assert(0 <= x && x < 16);
    assert(0 <= y && y < 256);
    assert(0 <= z && z < 16);

    int section_y = y >> 4;
    mc_ushort * section = ch->sections[section_y];

    if (section == NULL) {
        section = calloc(4096, sizeof (mc_ushort));
        ch->sections[section_y] = section;
    }

    int index = (y << 8) | (z << 4) | x;

    if (section[index] == 0) {
        ch->non_air_count[section_y]++;
    }
    if (block_state == 0) {
        ch->non_air_count[section_y]--;
    }

    section[index] = block_state;

    int height_map_index = (z << 4) | x;

    mc_ushort max_height = ch->motion_blocking_height_map[height_map_index];
    if (y + 1 == max_height) {
        if (block_state == 0) {
            // @TODO(traks) handle other airs
            max_height = 0;

            for (int lower_y = y - 1; lower_y >= 0; lower_y--) {
                if (chunk_get_block_state(ch, x, lower_y, z) != 0) {
                    // @TODO(traks) handle other airs
                    max_height = lower_y + 1;
                }
            }

            ch->motion_blocking_height_map[height_map_index] = max_height;
        }
    } else if (y >= max_height) {
        if (block_state != 0) {
            // @TODO(traks) handle other airs
            ch->motion_blocking_height_map[height_map_index] = y + 1;
        }
    }
}

static chunk *
get_or_create_chunk(chunk_pos pos) {
    int hash = hash_chunk_pos(pos);
    chunk_bucket * bucket = chunk_map + hash;

    for (;;) {
        int bucket_size = bucket->size;
        int i;

        for (i = 0; i < bucket_size; i++) {
            if (chunk_pos_equal(bucket->positions[i], pos)) {
                return bucket->chunks + i;
            }
        }

        if (i < CHUNKS_PER_BUCKET) {
            bucket->positions[i] = pos;
            bucket->size++;
            chunk * ch = bucket->chunks + i;
            *ch = (chunk) {0};

            for (int x = 0; x < 16; x++) {
                for (int z = 0; z < 16; z++) {
                    chunk_set_block_state(ch, x, 0, z, 1);
                }
            }
            return ch;
        }

        // chunk not found, move to next bucket
        chunk_bucket * next = bucket->next_bucket;
        if (next == NULL) {
            // @TODO(traks) get rid of this fallible operation
            next = malloc(sizeof *next);
            if (next == NULL) {
                log_errno("Failed to allocate chunk bucket: %s");
                exit(1);
            }
            next->next_bucket = NULL;
            next->size = 0;

            bucket->next_bucket = next;
        }

        bucket = next;
    }
}

static chunk *
get_chunk_if_loaded(chunk_pos pos) {
    int hash = hash_chunk_pos(pos);
    chunk_bucket * bucket = chunk_map + hash;

    for (;;) {
        int bucket_size = bucket->size;

        for (int i = 0; i < bucket_size; i++) {
            if (chunk_pos_equal(bucket->positions[i], pos)) {
                return bucket->chunks + i;
            }
        }

        // chunk not found, move to next bucket
        bucket = bucket->next_bucket;
        if (bucket == NULL) {
            return NULL;
        }
    }
}

static void
send_chunk_fully(buffer_cursor * send_cursor, chunk_pos pos) {
    // bit mask for included chunk sections; bottom section in least
    // significant bit
    chunk * ch = get_or_create_chunk(pos);
    mc_ushort section_mask = 0;
    for (int i = 0; i < 16; i++) {
        if (ch->sections[i] != NULL) {
            section_mask |= 1 << i;
        }
    }

    // calculate total size of chunk section data
    mc_int section_data_size = 0;

    for (int i = 0; i < 16; i++) {
        mc_ushort * section = ch->sections[i];
        if (section == NULL) {
            continue;
        }

        // size of non-air count + bits per block
        section_data_size += 2 + 1;
        // size of block state data in longs
        section_data_size += net_varint_size(14 * 16 * 16 * 16 / 64);
        // number of bytes used to store block state data
        section_data_size += (14 * 16 * 16 * 16 / 64) * 8;
    }

    net_string height_map_name = NET_STRING("MOTION_BLOCKING");

    int out_size = net_varint_size(34) + 4 + 4 + 1 + net_varint_size(section_mask)
            + 1 + 2 + 1 + 2 + height_map_name.size + 4 + 36 * 8 + 1
            + 1024 * 4
            + net_varint_size(section_data_size) + section_data_size
            + net_varint_size(0);

    // send level chunk packet
    net_write_varint(send_cursor, out_size);
    int packet_start = send_cursor->index;
    net_write_varint(send_cursor, 34);
    net_write_int(send_cursor, pos.x);
    net_write_int(send_cursor, pos.z);
    net_write_ubyte(send_cursor, 1); // full chunk
    net_write_varint(send_cursor, section_mask);

    // height map NBT
    net_write_ubyte(send_cursor, 10);
    // use a zero length string as name for the root compound
    net_write_ushort(send_cursor, 0);

    net_write_ubyte(send_cursor, 12);
    // write name
    net_write_ushort(send_cursor, height_map_name.size);
    memcpy(send_cursor->buf + send_cursor->index, height_map_name.ptr,
            height_map_name.size);
    send_cursor->index += height_map_name.size;

    // number of elements in long array
    net_write_int(send_cursor, 36);
    mc_ulong compacted_map[36] = {0};

    int shift = 0;

    for (int z = 0; z < 16; z++) {
        for (int x = 0; x < 16; x++) {
            mc_ulong height = ch->motion_blocking_height_map[(z << 4) | x];
            int start_long = shift >> 6;
            int offset = shift - (start_long << 6);

            compacted_map[start_long] |= height << offset;

            int bits_remaining = 64 - offset;

            if (bits_remaining < 9) {
                int end_long = start_long + 1;
                compacted_map[end_long] |= height >> bits_remaining;
            }

            shift += 9;
        }
    }

    for (int i = 0; i < 36; i++) {
        net_write_ulong(send_cursor, compacted_map[i]);
    }

    // end of compound
    net_write_ubyte(send_cursor, 0);

    // Biome data. Currently we just set all biome blocks (4x4x4 cubes)
    // to the plains biome.
    for (int i = 0; i < 1024; i++) {
        net_write_int(send_cursor, 1);
    }

    net_write_varint(send_cursor, section_data_size);

    for (int i = 0; i < 16; i++) {
        mc_ushort * section = ch->sections[i];
        if (section == NULL) {
            continue;
        }

        net_write_ushort(send_cursor, ch->non_air_count[i]);
        net_write_ubyte(send_cursor, 14); // bits per block

        // number of longs used for the block states
        net_write_varint(send_cursor, 14 * 16 * 16 * 16 / 64);
        mc_ulong val = 0;
        int offset = 0;

        for (int j = 0; j < 16 * 16 * 16; j++) {
            mc_ulong block_state = section[j];
            val |= block_state << offset;

            if (offset >= 64 - 14) {
                net_write_ulong(send_cursor, val);
                val = block_state >> (64 - offset);
            }

            offset = (offset + 14) % 64;
        }

        if (offset != 0) {
            net_write_ulong(send_cursor, val);
        }
    }

    // number of block entities
    net_write_varint(send_cursor, 0);
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
                brain->chunk_cache_radius = -1;
                // @TODO(traks) configurable server-wide global
                brain->new_chunk_cache_radius = MAX_CHUNK_CACHE_RADIUS;
                brain->last_keep_alive_sent_tick = current_tick;
                brain->flags |= PLAYER_BRAIN_GOT_ALIVE_RESPONSE;

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

                brain->send_cursor = send_cursor.index;

                teleport_player(brain, 88, 70, 73);

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
                    if (brain->last_keep_alive_sent_tick == id) {
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
                    mc_double x = net_read_double(&rec_cursor);
                    mc_double y = net_read_double(&rec_cursor);
                    mc_double z = net_read_double(&rec_cursor);
                    int on_ground = net_read_ubyte(&rec_cursor);
                    process_move_player_packet(brain, x, y, z,
                            brain->rot_x, brain->rot_y, on_ground);
                    break;
                }
                case 18: { // move player pos rot
                    mc_double x = net_read_double(&rec_cursor);
                    mc_double y = net_read_double(&rec_cursor);
                    mc_double z = net_read_double(&rec_cursor);
                    mc_float rot_y = net_read_float(&rec_cursor);
                    mc_float rot_x = net_read_float(&rec_cursor);
                    int on_ground = net_read_ubyte(&rec_cursor);
                    process_move_player_packet(brain, x, y, z,
                            rot_x, rot_y, on_ground);
                    break;
                }
                case 19: { // move player rot
                    mc_float rot_y = net_read_float(&rec_cursor);
                    mc_float rot_x = net_read_float(&rec_cursor);
                    int on_ground = net_read_ubyte(&rec_cursor);
                    process_move_player_packet(brain,
                            brain->x, brain->y, brain->z,
                            rot_x, rot_y, on_ground);
                    break;
                }
                case 20: { // move player
                    int on_ground = net_read_ubyte(&rec_cursor);
                    process_move_player_packet(brain,
                            brain->x, brain->y, brain->z,
                            brain->rot_x, brain->rot_y, on_ground);
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
                    mc_ubyte flags = net_read_ubyte(&rec_cursor);
                    mc_ubyte invulnerable = flags & 0x1;
                    mc_ubyte flying = flags & 0x2;
                    mc_ubyte canFly = flags & 0x4;
                    mc_ubyte insta_build = flags & 0x8;
                    mc_float fly_speed = net_read_float(&rec_cursor);
                    mc_float walk_speed = net_read_float(&rec_cursor);
                    // @TODO(traks) process packet
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
        }
    }

    for (int i = 0; i < ARRAY_SIZE(player_brains); i++) {
        player_brain * brain = player_brains + i;
        if ((brain->flags & PLAYER_BRAIN_IN_USE) == 0) {
            continue;
        }

        // first write all the new packets to our own outgoing packet buffer

        buffer_cursor send_cursor = {
            .limit = sizeof brain->send_buf,
            .buf = brain->send_buf,
            .index = brain->send_cursor
        };

        // send keep alive packet every so often
        if (current_tick - brain->last_keep_alive_sent_tick >= KEEP_ALIVE_SPACING
                && (brain->flags & PLAYER_BRAIN_GOT_ALIVE_RESPONSE)) {
            // send keep alive packet
            net_write_varint(&send_cursor, net_varint_size(33) + 8);
            net_write_varint(&send_cursor, 33);
            net_write_ulong(&send_cursor, current_tick);

            brain->last_keep_alive_sent_tick = current_tick;
            brain->flags &= ~PLAYER_BRAIN_GOT_ALIVE_RESPONSE;
        }

        if ((brain->flags & PLAYER_BRAIN_TELEPORTING)
                && !(brain->flags & PLAYER_BRAIN_SENT_TELEPORT)) {
            // send player position packet
            int out_size = net_varint_size(54) + 8 + 8 + 8 + 4 + 4 + 1
                    + net_varint_size(brain->current_teleport_id);
            net_write_varint(&send_cursor, out_size);
            net_write_varint(&send_cursor, 54);
            net_write_double(&send_cursor, brain->x);
            net_write_double(&send_cursor, brain->y);
            net_write_double(&send_cursor, brain->z);
            net_write_float(&send_cursor, brain->rot_y);
            net_write_float(&send_cursor, brain->rot_x);
            net_write_ubyte(&send_cursor, 0); // relative arguments
            net_write_varint(&send_cursor, brain->current_teleport_id);

            brain->flags |= PLAYER_BRAIN_SENT_TELEPORT;
        }

        mc_short chunk_cache_min_x = brain->chunk_cache_centre_x - brain->chunk_cache_radius;
        mc_short chunk_cache_min_z = brain->chunk_cache_centre_z - brain->chunk_cache_radius;
        mc_short chunk_cache_max_x = brain->chunk_cache_centre_x + brain->chunk_cache_radius;
        mc_short chunk_cache_max_z = brain->chunk_cache_centre_z + brain->chunk_cache_radius;

        __m128d xz = _mm_set_pd(brain->z, brain->x);
        __m128d floored_xz = _mm_floor_pd(xz);
        __m128i floored_int_xz = _mm_cvtpd_epi32(floored_xz);
        __m128i new_centre = _mm_srai_epi32(floored_int_xz, 4);
        mc_short new_chunk_cache_centre_x = _mm_extract_epi32(new_centre, 0);
        mc_short new_chunk_cache_centre_z = _mm_extract_epi32(new_centre, 1);
        mc_short new_chunk_cache_min_x = new_chunk_cache_centre_x - brain->new_chunk_cache_radius;
        mc_short new_chunk_cache_min_z = new_chunk_cache_centre_z - brain->new_chunk_cache_radius;
        mc_short new_chunk_cache_max_x = new_chunk_cache_centre_x + brain->new_chunk_cache_radius;
        mc_short new_chunk_cache_max_z = new_chunk_cache_centre_z + brain->new_chunk_cache_radius;

        if (brain->chunk_cache_centre_x != new_chunk_cache_centre_x
                || brain->chunk_cache_centre_z != new_chunk_cache_centre_z) {
            // send set chunk cache centre packet
            int out_size = net_varint_size(65)
                    + net_varint_size(new_chunk_cache_centre_x)
                    + net_varint_size(new_chunk_cache_centre_z);
            net_write_varint(&send_cursor, out_size);
            net_write_varint(&send_cursor, 65);
            net_write_varint(&send_cursor, new_chunk_cache_centre_x);
            net_write_varint(&send_cursor, new_chunk_cache_centre_z);
        }

        if (brain->chunk_cache_radius != brain->new_chunk_cache_radius) {
            // send set chunk cache radius packet
            int out_size = net_varint_size(66)
                    + net_varint_size(brain->new_chunk_cache_radius);
            net_write_varint(&send_cursor, out_size);
            net_write_varint(&send_cursor, 66);
            net_write_varint(&send_cursor, brain->new_chunk_cache_radius);
        }

        // untrack old chunks
        for (mc_short x = chunk_cache_min_x; x <= chunk_cache_max_x; x++) {
            for (mc_short z = chunk_cache_min_z; z <= chunk_cache_max_z; z++) {
                if (x >= new_chunk_cache_min_x && x <= new_chunk_cache_max_x
                        && z >= new_chunk_cache_min_z && z <= new_chunk_cache_max_z) {
                    // old chunk still in new region
                    continue;
                }

                // old chunk is not in the new region
                int index = chunk_cache_index((chunk_pos) {.x = x, .z = z});

                if (brain->chunk_cache[index].sent) {
                    brain->chunk_cache[index] = (chunk_cache_entry) {0};

                    // send forget level chunk packet
                    int out_size = net_varint_size(30) + 4 + 4;
                    net_write_varint(&send_cursor, out_size);
                    net_write_varint(&send_cursor, 30);
                    net_write_int(&send_cursor, x);
                    net_write_int(&send_cursor, z);
                }
            }
        }

        brain->chunk_cache_radius = brain->new_chunk_cache_radius;
        brain->chunk_cache_centre_x = new_chunk_cache_centre_x;
        brain->chunk_cache_centre_z = new_chunk_cache_centre_z;

        // load and send tracked chunks
        // We iterate in a spiral around the player, so chunks near the player
        // are processed first. This shortens server join times (since players
        // don't need to wait for the chunk they are in to load) and allows
        // players to move around much earlier.
        int newly_sent_chunks = 0;
        int chunk_cache_diam = 2 * brain->new_chunk_cache_radius + 1;
        int chunk_cache_area = chunk_cache_diam * chunk_cache_diam;
        int off_x = 0;
        int off_z = 0;
        int step_x = 1;
        int step_z = 0;
        for (int i = 0; i < chunk_cache_area; i++) {
            int x = new_chunk_cache_centre_x + off_x;
            int z = new_chunk_cache_centre_z + off_z;
            int cache_index = chunk_cache_index((chunk_pos) {.x = x, .z = z});
            chunk_cache_entry * entry = brain->chunk_cache + cache_index;

            if (newly_sent_chunks < MAX_CHUNK_SENDS_PER_TICK && !entry->sent) {
                // send level chunk packet
                send_chunk_fully(&send_cursor, (chunk_pos) {.x = x, .z = z});
                entry->sent = 1;
                newly_sent_chunks++;
            }

            off_x += step_x;
            off_z += step_z;
            // change direction of spiral when we hit a corner
            if (off_x == off_z || (off_x == -off_z && off_x < 0)
                    || (off_x == -off_z + 1 && off_x > 0)) {
                int prev_step_x = step_x;
                step_x = -step_z;
                step_z = prev_step_x;
            }
        }

        brain->send_cursor = send_cursor.index;

        // try to write everything to the socket buffer

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
