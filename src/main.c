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
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdalign.h>
#include <math.h>
#include "shared.h"

#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

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
    unsigned char send_buf[2048];
    int send_cursor;

    int protocol_state;
    unsigned char username[16];
    int username_size;
} initial_connection;

typedef struct {
    long long start_time;
    long long end_time;
    char * name;
} timed_block;

static initial_connection initial_connections[32];
static int initial_connection_count;

static timed_block timed_blocks[1 << 16];
static int timed_block_count;
static int timed_block_depth_stack[64];
static int cur_timed_block_depth;

#if defined(__APPLE__) && defined(__MACH__)

static mach_timebase_info_data_t timebase_info;
static unsigned long long program_start_time;

static void
init_program_nano_time() {
    mach_timebase_info(&timebase_info);
    program_start_time = mach_absolute_time();
}

static long long
program_nano_time() {
    long long diff = mach_absolute_time() - program_start_time;
    return diff * timebase_info.numer / timebase_info.denom;
}

#else

static struct timespec program_start_time;

static void
init_program_nano_time() {
    clock_gettime(CLOCK_MONOTONIC, &program_start_time);
}

static long long
program_nano_time() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long long diff_sec_nanos = (now.tv_sec - program_start_time.tv_sec) * 1000000000;
    long long diff_nanos = (long long) now.tv_nsec - program_start_time.tv_nsec;
    return diff_sec_nanos + diff_nanos;
}

#endif

void
begin_timed_block(char * name) {
    int i = timed_block_count;
    timed_block_count++;
    timed_block_depth_stack[cur_timed_block_depth] = i;
    cur_timed_block_depth++;
    timed_blocks[i].name = name;
    timed_blocks[i].start_time = program_nano_time();
}

void
end_timed_block() {
    cur_timed_block_depth--;
    timed_block * block = timed_blocks + timed_block_depth_stack[cur_timed_block_depth];
    block->end_time = program_nano_time();
}

void
logs(void * format, ...) {
    char msg[256];
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

void
logs_errno(void * format) {
    char error_msg[64] = {0};
    strerror_r(errno, error_msg, sizeof error_msg);
    logs(format, error_msg);
}

void *
alloc_in_arena(memory_arena * arena, mc_int size) {
    mc_int align = alignof (max_align_t);
    // round up to multiple of align
    mc_int actual_size = (size + align - 1) / align * align;
    assert(arena->size - actual_size >= arena->index);

    void * res = arena->ptr + arena->index;
    arena->index += actual_size;
    return res;
}

static void
handle_sigint(int sig) {
    got_sigint = 1;
}

int
net_string_equal(net_string a, net_string b) {
    return a.size == b.size && memcmp(a.ptr, b.ptr, a.size) == 0;
}

net_block_pos
get_relative_block_pos(net_block_pos pos, int face) {
    switch (face) {
    case DIRECTION_NEG_Y: pos.y--; break;
    case DIRECTION_POS_Y: pos.y++; break;
    case DIRECTION_NEG_Z: pos.z--; break;
    case DIRECTION_POS_Z: pos.z++; break;
    case DIRECTION_NEG_X: pos.x--; break;
    case DIRECTION_POS_X: pos.x++; break;
    case DIRECTION_ZERO: break;
    default:
        assert(0);
    }
    return pos;
}

int
get_opposite_direction(int direction) {
    switch (direction) {
    case DIRECTION_NEG_Y: return DIRECTION_POS_Y;
    case DIRECTION_POS_Y: return DIRECTION_NEG_Y;
    case DIRECTION_NEG_Z: return DIRECTION_POS_Z;
    case DIRECTION_POS_Z: return DIRECTION_NEG_Z;
    case DIRECTION_NEG_X: return DIRECTION_POS_X;
    case DIRECTION_POS_X: return DIRECTION_NEG_X;
    case DIRECTION_ZERO: return DIRECTION_ZERO;
    default:
        assert(0);
    }
}

static mc_ushort
hash_resource_loc(net_string resource_loc, resource_loc_table * table) {
    mc_ushort res = 0;
    unsigned char * string = resource_loc.ptr;
    for (int i = 0; i < resource_loc.size; i++) {
        res = res * 31 + string[i];
    }
    return res & table->size_mask;
}

static void
register_resource_loc(net_string resource_loc, mc_short id,
        resource_loc_table * table) {
    mc_ushort hash = hash_resource_loc(resource_loc, table);

    for (mc_ushort i = hash; ; i = (i + 1) & table->size_mask) {
        assert(((i + 1) & table->size_mask) != hash);
        resource_loc_entry * entry = table->entries + i;

        if (entry->size == 0) {
            assert(resource_loc.size <= RESOURCE_LOC_MAX_SIZE);
            entry->size = resource_loc.size;
            mc_int string_buf_index = table->last_string_buf_index;
            entry->buf_index = string_buf_index;
            entry->id = id;

            assert(string_buf_index + resource_loc.size <= table->string_buf_size);
            memcpy(table->string_buf + string_buf_index, resource_loc.ptr,
                    resource_loc.size);
            table->last_string_buf_index += resource_loc.size;

            assert(id < table->max_ids);
            table->by_id[id] = i;
            break;
        }
    }
}

static void
alloc_resource_loc_table(resource_loc_table * table, mc_int size,
        mc_int string_buf_size, mc_ushort max_ids) {
    *table = (resource_loc_table) {
        .size_mask = size - 1,
        .string_buf_size = string_buf_size,
        .entries = calloc(size, sizeof *table->entries),
        .string_buf = calloc(string_buf_size, 1),
        .by_id = calloc(max_ids, 2),
        .max_ids = max_ids
    };
    assert(table->entries != NULL);
    assert(table->string_buf != NULL);
}

mc_short
resolve_resource_loc_id(net_string resource_loc, resource_loc_table * table) {
    mc_ushort hash = hash_resource_loc(resource_loc, table);
    mc_ushort i = hash;
    for (;;) {
        resource_loc_entry * entry = table->entries + i;
        net_string name = {
            .ptr = table->string_buf + entry->buf_index,
            .size = entry->size
        };
        if (net_string_equal(resource_loc, name)) {
            return entry->id;
        }

        i = (i + 1) & table->size_mask;
        if (i == hash) {
            // @TODO(traks) instead of returning -1 on error, perhaps we should
            // return some default resource location in the table.
            return -1;
        }
    }
}

net_string
get_resource_loc(mc_ushort id, resource_loc_table * table) {
    assert(id < table->max_ids);
    resource_loc_entry * entry = table->entries + table->by_id[id];
    net_string res = {
        .ptr = table->string_buf + entry->buf_index,
        .size = entry->size
    };
    return res;
}

int
find_property_value_index(block_property_spec * prop_spec, net_string val) {
    unsigned char * tape = prop_spec->tape;
    tape += 1 + tape[0];
    for (int i = 0; ; i++) {
        // Note that after the values a 0 follows
        if (tape[0] == 0) {
            return -1;
        }
        net_string real_val = {
            .ptr = tape + 1,
            .size = tape[0]
        };
        if (net_string_equal(real_val, val)) {
            return i;
        }
        tape += 1 + tape[0];
    }
}

entity_base *
resolve_entity(server * serv, entity_id eid) {
    mc_uint index = eid & ENTITY_INDEX_MASK;
    entity_base * entity = serv->entities + index;
    if (entity->eid != eid || !(entity->flags & ENTITY_IN_USE)) {
        // return the null entity
        return serv->entities;
    }
    return entity;
}

entity_base *
try_reserve_entity(server * serv, unsigned type) {
    for (uint32_t i = 0; i < MAX_ENTITIES; i++) {
        entity_base * entity = serv->entities + i;
        if (!(entity->flags & ENTITY_IN_USE)) {
            mc_ushort generation = serv->next_entity_generations[i];
            entity_id eid = ((mc_uint) generation << 20) | i;

            *entity = (entity_base) {0};
            // @NOTE(traks) default initialisation is only guaranteed to
            // initialise the first union member, so we have to manually
            // default initialise the union member based on the entity type
            switch (type) {
            case ENTITY_PLAYER: entity->player = (entity_player) {0}; break;
            case ENTITY_ITEM: entity->item = (entity_item) {0}; break;
            }

            entity->eid = eid;
            entity->type = type;
            entity->flags |= ENTITY_IN_USE;
            serv->next_entity_generations[i] = (generation + 1) & 0xfff;
            serv->entity_count++;
            return entity;
        }
    }
    // first entity used as placeholder for null entity
    return serv->entities;
}

void
evict_entity(server * serv, entity_id eid) {
    entity_base * entity = resolve_entity(serv, eid);
    if (entity->type != ENTITY_NULL) {
        entity->flags &= ~ENTITY_IN_USE;
        serv->entity_count--;
    }
}

static void
move_entity(entity_base * entity, server * serv) {
    double x = entity->x;
    double y = entity->y;
    double z = entity->z;

    double vx = entity->vx;
    double vy = entity->vy;
    double vz = entity->vz;

    double remaining_dt = 1;

    for (int iter = 0; iter < 4; iter++) {
        // @TODO(traks) drag depending on block state below

        // @TODO(traks) fluid handling

        double dx = remaining_dt * vx;
        double dy = remaining_dt * vy;
        double dz = remaining_dt * vz;

        double end_x = x + dx;
        double end_y = y + dy;
        double end_z = z + dz;

        double min_x = MIN(x, end_x);
        double max_x = MAX(x, end_x);
        double min_y = MIN(y, end_y);
        double max_y = MAX(y, end_y);
        double min_z = MIN(z, end_z);
        double max_z = MAX(z, end_z);

        double width = entity->collision_width;
        double height = entity->collision_height;
        min_x -= width / 2;
        max_x += width / 2;
        max_y += height;
        min_z -= width / 2;
        max_z += width / 2;

        mc_int iter_min_x = floor(min_x);
        mc_int iter_max_x = floor(max_x);
        mc_int iter_min_y = floor(min_y);
        mc_int iter_max_y = floor(max_y);
        mc_int iter_min_z = floor(min_z);
        mc_int iter_max_z = floor(max_z);

        mc_ushort hit_state = 0;
        int hit_face;

        double dt = 1;

        for (int block_x = iter_min_x; block_x <= iter_max_x; block_x++) {
            for (int block_y = iter_min_y; block_y <= iter_max_y; block_y++) {
                for (int block_z = iter_min_z; block_z <= iter_max_z; block_z++) {
                    chunk_pos ch_pos = {
                        .x = block_x >> 4,
                        .z = block_z >> 4,
                    };
                    chunk * ch = get_chunk_if_loaded(ch_pos);
                    if (ch == NULL) {
                        // @TODO(traks) what now?
                        return;
                    }

                    // @TODO(traks) can fail if y out of bounds
                    mc_ushort cur_state = chunk_get_block_state(ch,
                            block_x & 0xf, block_y, block_z & 0xf);

                    if (cur_state == 0) {
                        continue;
                    }

                    // @TODO(traks) use block model depending on block state

                    double test_min_x = block_x - width / 2;
                    double test_max_x = block_x + 1 + width / 2;
                    double test_min_y = block_y - height;
                    double test_max_y = block_y + 1;
                    double test_min_z = block_z - width / 2;
                    double test_max_z = block_z + 1 + width / 2;

                    typedef struct {
                        double wall_a;
                        double min_b;
                        double max_b;
                        double min_c;
                        double max_c;
                        double da;
                        double db;
                        double dc;
                        double a;
                        double b;
                        double c;
                        int face;
                    } hit_test;

                    hit_test tests[] = {
                        {test_min_x, test_min_y, test_max_y, test_min_z, test_max_z, dx, dy, dz, x, y, z, DIRECTION_NEG_X},
                        {test_max_x, test_min_y, test_max_y, test_min_z, test_max_z, dx, dy, dz, x, y, z, DIRECTION_POS_X},
                        {test_min_y, test_min_x, test_max_x, test_min_z, test_max_z, dy, dx, dz, y, x, z, DIRECTION_NEG_Y},
                        {test_max_y, test_min_x, test_max_x, test_min_z, test_max_z, dy, dx, dz, y, x, z, DIRECTION_POS_Y},
                        {test_min_z, test_min_y, test_max_y, test_min_x, test_max_x, dz, dy, dx, z, y, x, DIRECTION_NEG_Z},
                        {test_max_z, test_min_y, test_max_y, test_min_x, test_max_x, dz, dy, dx, z, y, x, DIRECTION_POS_Z},
                    };

                    for (int i = 0; i < ARRAY_SIZE(tests); i++) {
                        hit_test * test = tests + i;
                        if (test->da != 0) {
                            double hit_time = (test->wall_a - test->a) / test->da;
                            if (hit_time >= 0 && dt > hit_time) {
                                double hit_b = test->b + hit_time * test->db;
                                if (test->min_b <= hit_b && hit_b <= test->max_b) {
                                    double hit_c = test->c + hit_time * test->dc;
                                    if (test->min_c <= hit_c && hit_c <= test->max_c) {
                                        // @TODO(traks) epsilon
                                        dt = MAX(0, hit_time - 0.001);
                                        hit_state = cur_state;
                                        hit_face = test->face;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        x += dt * dx;
        y += dt * dy;
        z += dt * dz;

        mc_int hit_type = serv->block_type_by_state[hit_state];

        if (hit_state != 0) {
            switch (hit_face) {
            case DIRECTION_NEG_X:
            case DIRECTION_POS_X:
                vx = 0;
                break;
            case DIRECTION_NEG_Y:
                vy = 0;
                break;
            case DIRECTION_POS_Y: {
                double bounce_factor;
                // @TODO(traks) all living entities have bounce factor -1
                switch (entity->type) {
                case ENTITY_PLAYER:
                    if (entity->flags & PLAYER_SHIFTING) {
                        bounce_factor = 0;
                    } else {
                        bounce_factor = -1;
                    }
                    break;
                default:
                    bounce_factor = -0.8;
                }

                switch (hit_type) {
                case BLOCK_SLIME_BLOCK:
                    vy *= bounce_factor;
                    break;
                case BLOCK_WHITE_BED:
                case BLOCK_ORANGE_BED:
                case BLOCK_MAGENTA_BED:
                case BLOCK_LIGHT_BLUE_BED:
                case BLOCK_YELLOW_BED:
                case BLOCK_LIME_BED:
                case BLOCK_PINK_BED:
                case BLOCK_GRAY_BED:
                case BLOCK_LIGHT_GRAY_BED:
                case BLOCK_CYAN_BED:
                case BLOCK_PURPLE_BED:
                case BLOCK_BLUE_BED:
                case BLOCK_BROWN_BED:
                case BLOCK_GREEN_BED:
                case BLOCK_RED_BED:
                case BLOCK_BLACK_BED:
                    vy *= bounce_factor * 0.66;
                    break;
                default:
                    vy = 0;
                }
                break;
            }
            case DIRECTION_NEG_Z:
            case DIRECTION_POS_Z:
                vz = 0;
                break;
            }
        }

        remaining_dt -= dt * remaining_dt;
    }

    entity->x = x;
    entity->y = y;
    entity->z = z;
    entity->vx = vx;
    entity->vy = vy;
    entity->vz = vz;
}

static void
tick_entity(entity_base * entity, server * serv, memory_arena * tick_arena) {
    // @TODO(traks) currently it's possible that an entity is spawned and ticked
    // the same tick. Is that an issue or not? Maybe that causes undesirable
    // off-by-one tick behaviour.

    switch (entity->type) {
    case ENTITY_PLAYER:
        tick_player(entity, serv, tick_arena);
        break;
    case ENTITY_ITEM: {
        if (entity->item.contents.type == ITEM_AIR) {
            evict_entity(serv, entity->eid);
            return;
        }

        if (entity->item.pickup_timeout > 0
                && entity->item.pickup_timeout != 32767) {
            entity->item.pickup_timeout--;
        }

        // gravity acceleration
        entity->vy -= 0.04;

        move_entity(entity, serv);

        // @NOTE(traks) not sure why Minecraft does this
        entity->vx *= 0.98;
        entity->vy *= 0.98;
        entity->vz *= 0.98;
        break;
    }
    }
}

static void
server_tick(server * serv) {
    begin_timed_block("server tick");

    // accept new connections
    begin_timed_block("accept initial connections");

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
        logs("Created initial connection");
    }

    end_timed_block();

    // update initial connections
    begin_timed_block("update initial connections");

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
                logs_errno("Couldn't receive protocol data: %s");
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
                logs("Initial packet %d", packet_id);

                switch (init_con->protocol_state) {
                case PROTOCOL_HANDSHAKE: {
                    if (packet_id != 0) {
                        rec_cursor.error = 1;
                    }

                    // read client intention packet
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

                    // read status request packet
                    // empty

                    memory_arena scratch_arena = {
                        .ptr = serv->short_lived_scratch,
                        .size = serv->short_lived_scratch_size
                    };

                    int list_size = serv->tab_list_size;
                    size_t list_bytes = list_size * sizeof (entity_id);
                    entity_id * list = alloc_in_arena(
                            &scratch_arena, list_bytes);
                    memcpy(list, serv->tab_list, list_bytes);
                    int sample_size = MIN(12, list_size);

                    unsigned char * response = alloc_in_arena(&scratch_arena, 2048);
                    int response_size = 0;
                    response_size += sprintf((char *) response + response_size,
                            "{\"version\":{\"name\":\"%s\",\"protocol\":%d},"
                            "\"players\":{\"max\":%d,\"online\":%d,\"sample\":[",
                            "1.16.2", 751,
                            (int) MAX_PLAYERS, (int) list_size);

                    for (int i = 0; i < sample_size; i++) {
                        int target = i + (rand() % (list_size - i));
                        entity_id * sampled = list + target;

                        if (i > 0) {
                            response[response_size] = ',';
                            response_size += 1;
                        }

                        entity_base * entity = resolve_entity(serv, *sampled);
                        assert(entity->type == ENTITY_PLAYER);
                        // @TODO(traks) actual UUID
                        response_size += sprintf((char *) response + response_size,
                                "{\"id\":\"01234567-89ab-cdef-0123-456789abcdef\","
                                "\"name\":\"%.*s\"}",
                                (int) entity->player.username_size,
                                entity->player.username);

                        *sampled = list[i];
                    }

                    response_size += sprintf((char *) response + response_size,
                            "]},\"description\":{\"text\":\"Running Blaze\"}}");

                    int out_size = net_varint_size(0)
                            + net_varint_size(response_size)
                            + response_size;
                    net_write_varint(&send_cursor, out_size);
                    net_write_varint(&send_cursor, 0);
                    net_write_varint(&send_cursor, response_size);
                    net_write_data(&send_cursor, response, response_size);

                    init_con->protocol_state = PROTOCOL_AWAIT_PING_REQUEST;
                    break;
                }
                case PROTOCOL_AWAIT_PING_REQUEST: {
                    if (packet_id != 1) {
                        rec_cursor.error = 1;
                    }

                    // read ping request packet
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

                    // read hello packet
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
                    logs("Protocol state %d not accepting packets",
                            init_con->protocol_state);
                    rec_cursor.error = 1;
                    break;
                }

                assert(send_cursor.error == 0);

                if (packet_size != rec_cursor.index - packet_start) {
                    rec_cursor.error = 1;
                }

                if (rec_cursor.error != 0) {
                    logs("Initial connection protocol error occurred");
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

    end_timed_block();

    begin_timed_block("send initial connections");

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
                logs_errno("Couldn't send protocol data: %s");
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

                entity_base * entity = try_reserve_entity(serv, ENTITY_PLAYER);

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
                player->rec_buf_size = 65536;
                player->rec_buf = malloc(player->rec_buf_size);

                player->send_buf_size = 1048576;
                player->send_buf = malloc(player->send_buf_size);

                if (player->rec_buf == NULL || player->send_buf == NULL) {
                    // @TODO(traks) send some message on disconnect
                    free(player->send_buf);
                    free(player->rec_buf);
                    evict_entity(serv, entity->eid);
                    close(init_con->sock);
                    continue;
                }

                player->sock = init_con->sock;
                memcpy(player->username, init_con->username,
                        init_con->username_size);
                player->username_size = init_con->username_size;
                player->chunk_cache_radius = -1;
                // @TODO(traks) configurable server-wide global
                player->new_chunk_cache_radius = MAX_CHUNK_CACHE_RADIUS;
                player->last_keep_alive_sent_tick = serv->current_tick;
                entity->flags |= PLAYER_GOT_ALIVE_RESPONSE;
                player->selected_slot = PLAYER_FIRST_HOTBAR_SLOT;
                // @TODO(traks) collision width and height of player depending
                // on player pose
                entity->collision_width = 0.6;
                entity->collision_height = 1.8;
                set_player_gamemode(entity, GAMEMODE_CREATIVE);

                teleport_player(entity, 88, 70, 73, 0, 0);

                // @TODO(traks) ensure this can never happen instead of assering
                // it never will hopefully happen
                assert(serv->tab_list_added_count < ARRAY_SIZE(serv->tab_list_added));
                serv->tab_list_added[serv->tab_list_added_count] = entity->eid;
                serv->tab_list_added_count++;

                logs("Player '%.*s' joined", (int) init_con->username_size,
                        init_con->username);
            }
        }
    }

    end_timed_block();

    // update entities
    begin_timed_block("tick entities");

    for (int i = 0; i < ARRAY_SIZE(serv->entities); i++) {
        entity_base * entity = serv->entities + i;
        if ((entity->flags & ENTITY_IN_USE) == 0) {
            continue;
        }

        memory_arena tick_arena = {
            .ptr = serv->short_lived_scratch,
            .size = serv->short_lived_scratch_size
        };

        tick_entity(entity, serv, &tick_arena);
    }

    end_timed_block();

    begin_timed_block("update tab list");

    // remove players from tab list if necessary
    for (int i = 0; i < serv->tab_list_size; i++) {
        entity_id eid = serv->tab_list[i];
        entity_base * entity = resolve_entity(serv, eid);
        if (entity->type == ENTITY_NULL) {
            // @TODO(traks) make sure this can never happen instead of hoping
            assert(serv->tab_list_removed_count < ARRAY_SIZE(serv->tab_list_removed));
            serv->tab_list_removed[serv->tab_list_removed_count] = eid;
            serv->tab_list_removed_count++;
            serv->tab_list[i] = serv->tab_list[serv->tab_list_size - 1];
            serv->tab_list_size--;
            i--;
            continue;
        }
        // @TODO(traks) make sure this can never happen instead of hoping
        assert(entity->type == ENTITY_PLAYER);
    }

    // add players to live tab list
    for (int i = 0; i < serv->tab_list_added_count; i++) {
        entity_id eid = serv->tab_list_added[i];
        assert(serv->tab_list_size < ARRAY_SIZE(serv->tab_list));
        serv->tab_list[serv->tab_list_size] = eid;
        serv->tab_list_size++;
    }

    end_timed_block();

    begin_timed_block("send players");

    for (int i = 0; i < ARRAY_SIZE(serv->entities); i++) {
        entity_base * entity = serv->entities + i;
        if (entity->type != ENTITY_PLAYER) {
            continue;
        }
        if ((entity->flags & ENTITY_IN_USE) == 0) {
            continue;
        }

        memory_arena tick_arena = {
            .ptr = serv->short_lived_scratch,
            .size = serv->short_lived_scratch_size
        };
        send_packets_to_player(entity, serv, &tick_arena);
    }

    end_timed_block();

    // clear global messages
    serv->global_msg_count = 0;

    // clear tab list updates
    serv->tab_list_added_count = 0;
    serv->tab_list_removed_count = 0;

    begin_timed_block("clear entity changes");

    for (int i = 0; i < ARRAY_SIZE(serv->entities); i++) {
        entity_base * entity = serv->entities + i;
        if ((entity->flags & ENTITY_IN_USE) == 0) {
            continue;
        }

        entity->changed_data = 0;
    }

    end_timed_block();

    // load chunks from requests
    begin_timed_block("load chunks");

    for (int i = 0; i < serv->chunk_load_request_count; i++) {
        chunk_pos pos = serv->chunk_load_requests[i];
        chunk * ch = get_chunk_if_available(pos);
        if (ch == NULL) {
            continue;
        }
        if (ch->available_interest == 0) {
            // no one cares about the chunk anymore, so don't bother loading it
            continue;
        }
        if (ch->flags & CHUNK_LOADED) {
            continue;
        }

        // @TODO(traks) actual chunk loading from whatever storage provider
        memory_arena scratch_arena = {
            .ptr = serv->short_lived_scratch,
            .size = serv->short_lived_scratch_size
        };
        try_read_chunk_from_storage(pos, ch, &scratch_arena, serv);

        if (!(ch->flags & CHUNK_LOADED)) {
            // @TODO(traks) fall back to stone plateau at y = 0 for now
            // clean up some of the mess the chunk loader might've left behind
            // @TODO(traks) perhaps this should be in a separate struct so we
            // can easily clear it
            for (int sectioni = 0; sectioni < 16; sectioni++) {
                if (ch->sections[sectioni] != NULL) {
                    free_chunk_section(ch->sections[sectioni]);
                    ch->sections[sectioni] = NULL;
                }
                ch->non_air_count[sectioni] = 0;
            }

            // @TODO(traks) perhaps should require enough chunk sections to be
            // available for chunk before even trying to load/generate it.
            ch->sections[0] = alloc_chunk_section();
            if (ch->sections[0] == NULL) {
                logs("Failed to allocate chunk section during generation");
                exit(1);
            }

            for (int x = 0; x < 16; x++) {
                for (int z = 0; z < 16; z++) {
                    int index = (z << 4) | x;
                    ch->sections[0]->block_states[index] = 2;
                    ch->motion_blocking_height_map[index] = 1;
                    ch->non_air_count[0]++;
                }
            }

            ch->flags |= CHUNK_LOADED;
        }
    }

    serv->chunk_load_request_count = 0;

    end_timed_block();

    // update chunks
    begin_timed_block("update chunks");
    clean_up_unused_chunks();
    end_timed_block();

    serv->current_tick++;
    end_timed_block();
}

static int
parse_database_line(buffer_cursor * cursor, net_string * args) {
    int arg_count = 0;
    int cur_size = 0;
    int i;
    unsigned char * line = cursor->buf;
    for (i = cursor->index; ; i++) {
        if (i == cursor->limit || line[i] == ' ' || line[i] == '\n') {
            if (cur_size > 0) {
                args[arg_count].ptr = cursor->buf + (i - cur_size);
                args[arg_count].size = cur_size;
                arg_count++;
                cur_size = 0;
            }
            if (i == cursor->limit) {
                break;
            }
            if (line[i] == '\n') {
                i++;
                break;
            }
        } else {
            cur_size++;
        }
    }

    cursor->index = i;
    return arg_count;
}

static void
load_item_types(server * serv) {
    int fd = open("itemtypes.txt", O_RDONLY);
    if (fd == -1) {
        return;
    }

    struct stat stat;
    if (fstat(fd, &stat)) {
        close(fd);
        return;
    }

    // @TODO(traks) should we unmap this or something?
    unsigned char * fmmap = mmap(NULL, stat.st_size, PROT_READ,
            MAP_PRIVATE, fd, 0);
    if (fmmap == MAP_FAILED) {
        close(fd);
        return;
    }

    // after mmaping we can close the file descriptor
    close(fd);

    buffer_cursor cursor = {.buf = fmmap, .limit = stat.st_size};
    net_string args[16];
    mc_ushort id = 0;

    for (;;) {
        int arg_count = parse_database_line(&cursor, args);
        if (arg_count == 0) {
            // empty line
            if (cursor.index == cursor.limit) {
                break;
            }
        } else if (net_string_equal(args[0], NET_STRING("key"))) {
            register_resource_loc(args[1], id, &serv->item_resource_table);
            id++;
        }
    }

    assert(id == serv->item_resource_table.max_ids);
}

static void
load_block_types(server * serv) {
    int fd = open("blocktypes.txt", O_RDONLY);
    if (fd == -1) {
        return;
    }

    struct stat stat;
    if (fstat(fd, &stat)) {
        close(fd);
        return;
    }

    // @TODO(traks) should we unmap this or something?
    unsigned char * fmmap = mmap(NULL, stat.st_size, PROT_READ,
            MAP_PRIVATE, fd, 0);
    if (fmmap == MAP_FAILED) {
        close(fd);
        return;
    }

    // after mmaping we can close the file descriptor
    close(fd);

    buffer_cursor cursor = {.buf = fmmap, .limit = stat.st_size};
    net_string args[32];
    mc_ushort base_state = 0;
    mc_ushort states_for_type = 0;
    mc_ushort type_id;

    block_properties * block_properties_table = serv->block_properties_table;
    block_property_spec * block_property_specs = serv->block_property_specs;
    int max_block_property_specs = ARRAY_SIZE(serv->block_property_specs);
    int block_property_spec_count = 0;
    int block_state_count = 0;
    int block_type_count = 0;

    for (;;) {
        int arg_count = parse_database_line(&cursor, args);
        if (arg_count == 0) {
            // empty line

            assert(block_state_count + states_for_type
                    <= ARRAY_SIZE(serv->block_type_by_state));
            for (int i = 0; i < states_for_type; i++) {
                serv->block_type_by_state[block_state_count + i] = type_id;
            }

            block_state_count += states_for_type;

            if (cursor.index == cursor.limit) {
                break;
            }
        } else if (net_string_equal(args[0], NET_STRING("key"))) {
            net_string key = args[1];
            type_id = block_type_count;
            block_type_count++;
            register_resource_loc(key, type_id, &serv->block_resource_table);

            base_state += states_for_type;
            block_properties_table[type_id].base_state = base_state;
            states_for_type = 1;
        } else if (net_string_equal(args[0], NET_STRING("property"))) {
            states_for_type *= arg_count - 2;
            block_property_spec prop_spec = {0};
            prop_spec.value_count = arg_count - 2;

            int speci = 0;
            for (int i = 1; i < arg_count; i++) {
                // let there be one 0 at the end of the property's tape. We use
                // this to detect the end in other operations.
                assert(sizeof prop_spec.tape - speci - 1 >= 1 + args[i].size);
                prop_spec.tape[speci] = args[i].size;
                speci++;
                memcpy(prop_spec.tape + speci, args[i].ptr, args[i].size);
                speci += args[i].size;
            }

            block_properties * props = block_properties_table + type_id;

            int i;
            for (i = 0; i < block_property_spec_count; i++) {
                if (memcmp(block_property_specs + i,
                        &prop_spec, sizeof prop_spec) == 0) {
                    props->property_specs[props->property_count] = i;
                    props->property_count++;
                    break;
                }
            }
            if (i == block_property_spec_count) {
                assert(block_property_spec_count < max_block_property_specs);
                props->property_specs[props->property_count] = block_property_spec_count;
                props->property_count++;
                block_property_specs[block_property_spec_count] = prop_spec;
                block_property_spec_count++;
            }
        } else if (net_string_equal(args[0], NET_STRING("default_values"))) {
            block_properties * props = block_properties_table + type_id;

            for (int i = 1; i < arg_count; i++) {
                assert(i - 1 < props->property_count);
                block_property_spec * prop_spec = block_property_specs
                        + props->property_specs[i - 1];
                int value_index = find_property_value_index(prop_spec, args[i]);
                assert(value_index != -1);
                props->default_value_indices[i - 1] = value_index;
            }
        }
    }

    serv->block_state_count = block_state_count;
    serv->block_property_spec_count = block_property_spec_count;
    assert(block_type_count == serv->block_resource_table.max_ids);
}

static void
load_entity_types(server * serv) {
    int fd = open("entitytypes.txt", O_RDONLY);
    if (fd == -1) {
        return;
    }

    struct stat stat;
    if (fstat(fd, &stat)) {
        close(fd);
        return;
    }

    // @TODO(traks) should we unmap this or something?
    unsigned char * fmmap = mmap(NULL, stat.st_size, PROT_READ,
            MAP_PRIVATE, fd, 0);
    if (fmmap == MAP_FAILED) {
        close(fd);
        return;
    }

    // after mmaping we can close the file descriptor
    close(fd);

    buffer_cursor cursor = {.buf = fmmap, .limit = stat.st_size};
    net_string args[16];
    mc_ushort entity_types = 0;

    for (;;) {
        int arg_count = parse_database_line(&cursor, args);
        if (arg_count == 0) {
            // empty line
            if (cursor.index == cursor.limit) {
                break;
            }
        } else if (net_string_equal(args[0], NET_STRING("key"))) {
            mc_ushort id = entity_types;
            entity_types++;
            register_resource_loc(args[1], id, &serv->entity_resource_table);
        }
    }

    assert(entity_types == serv->entity_resource_table.max_ids);
}

static void
load_fluid_types(server * serv) {
    int fd = open("fluidtypes.txt", O_RDONLY);
    if (fd == -1) {
        return;
    }

    struct stat stat;
    if (fstat(fd, &stat)) {
        close(fd);
        return;
    }

    // @TODO(traks) should we unmap this or something?
    unsigned char * fmmap = mmap(NULL, stat.st_size, PROT_READ,
            MAP_PRIVATE, fd, 0);
    if (fmmap == MAP_FAILED) {
        close(fd);
        return;
    }

    // after mmaping we can close the file descriptor
    close(fd);

    buffer_cursor cursor = {.buf = fmmap, .limit = stat.st_size};
    net_string args[16];
    mc_ushort fluid_types = 0;

    for (;;) {
        int arg_count = parse_database_line(&cursor, args);
        if (arg_count == 0) {
            // empty line
            if (cursor.index == cursor.limit) {
                break;
            }
        } else if (net_string_equal(args[0], NET_STRING("key"))) {
            mc_ushort id = fluid_types;
            fluid_types++;
            register_resource_loc(args[1], id, &serv->fluid_resource_table);
        }
    }

    assert(fluid_types == serv->fluid_resource_table.max_ids);
}

static void
load_tags(char * file_name, tag_list * tags,
        resource_loc_table * table, server * serv) {
    tags->size = 0;
    int fd = open(file_name, O_RDONLY);
    if (fd == -1) {
        return;
    }

    struct stat stat;
    if (fstat(fd, &stat)) {
        close(fd);
        return;
    }

    // @TODO(traks) should we unmap this or something?
    unsigned char * fmmap = mmap(NULL, stat.st_size, PROT_READ,
            MAP_PRIVATE, fd, 0);
    if (fmmap == MAP_FAILED) {
        close(fd);
        return;
    }

    // after mmaping we can close the file descriptor
    close(fd);

    buffer_cursor cursor = {.buf = fmmap, .limit = stat.st_size};
    net_string args[16];
    tag_spec * tag;

    for (;;) {
        int arg_count = parse_database_line(&cursor, args);
        if (arg_count == 0) {
            // empty line
            if (cursor.index == cursor.limit) {
                break;
            }
        } else if (net_string_equal(args[0], NET_STRING("key"))) {
            assert(tags->size < ARRAY_SIZE(tags->tags));

            tag = tags->tags + tags->size;
            tags->size++;

            *tag = (tag_spec) {0};
            tag->name_index = serv->tag_name_count;
            tag->value_count = 0;
            tag->values_index = serv->tag_value_id_count;

            int name_size = args[1].size;
            assert(name_size <= UCHAR_MAX);
            assert(serv->tag_name_count + 1 + name_size
                    <= ARRAY_SIZE(serv->tag_name_buf));

            serv->tag_name_buf[serv->tag_name_count] = name_size;
            serv->tag_name_count++;
            memcpy(serv->tag_name_buf + serv->tag_name_count, args[1].ptr, name_size);
            serv->tag_name_count += name_size;
        } else if (net_string_equal(args[0], NET_STRING("value"))) {
            mc_short id = resolve_resource_loc_id(args[1], table);
            assert(id != -1);

            assert(tag->values_index + tag->value_count
                    <= ARRAY_SIZE(serv->tag_value_id_buf));
            serv->tag_value_id_buf[tag->values_index + tag->value_count] = id;
            serv->tag_value_id_count++;

            tag->value_count++;
        }
    }
}

static void
init_dimension_types(server * serv) {
    dimension_type * overworld = serv->dimension_types + serv->dimension_type_count;
    serv->dimension_type_count++;

    *overworld = (dimension_type) {
        .fixed_time = -1,
        .coordinate_scale = 1,
        .logical_height = 256,
        .ambient_light = 0
    };

    net_string overworld_name = NET_STRING("minecraft:overworld");
    memcpy(overworld->name, overworld_name.ptr, overworld_name.size);
    overworld->name_size = overworld_name.size;

    net_string overworld_infiniburn = NET_STRING("minecraft:infiniburn_overworld");
    memcpy(overworld->infiniburn, overworld_infiniburn.ptr, overworld_infiniburn.size);
    overworld->infiniburn_size = overworld_infiniburn.size;

    net_string overworld_effects = NET_STRING("minecraft:overworld");
    memcpy(overworld->effects, overworld_effects.ptr, overworld_effects.size);
    overworld->effects_size = overworld_effects.size;

    overworld->flags |= DIMENSION_HAS_SKYLIGHT | DIMENSION_NATURAL
            | DIMENSION_BED_WORKS | DIMENSION_HAS_RAIDS;

    // @TODO(traks) add all the vanilla dimension types
}

static void
init_biomes(server * serv) {
    biome * ocean = serv->biomes + serv->biome_count;
    serv->biome_count++;

    *ocean = (biome) {
        .precipitation = BIOME_PRECIPITATION_RAIN,
        .category = BIOME_CATEGORY_OCEAN,
        .temperature = 0.5,
        .downfall = 0.5,
        .temperature_mod = BIOME_TEMPERATURE_MOD_NONE,
        .depth = -1,
        .scale = 0.1,

        .fog_colour = 12638463,
        .water_colour = 4159204,
        .water_fog_colour = 329011,
        .sky_colour = 8103167,
        .foliage_colour_override = -1,
        .grass_colour_override = -1,
        .grass_colour_mod = BIOME_GRASS_COLOUR_MOD_NONE,
    };

    net_string ocean_name = NET_STRING("minecraft:ocean");
    memcpy(ocean->name, ocean_name.ptr, ocean_name.size);
    ocean->name_size = ocean_name.size;

    biome * plains = serv->biomes + serv->biome_count;
    serv->biome_count++;

    *plains = (biome) {
        .precipitation = BIOME_PRECIPITATION_RAIN,
        .category = BIOME_CATEGORY_PLAINS,
        .temperature = 0.8,
        .downfall = 0.4,
        .temperature_mod = BIOME_TEMPERATURE_MOD_NONE,
        .depth = 0.125,
        .scale = 0.05,

        .fog_colour = 12638463,
        .water_colour = 4159204,
        .water_fog_colour = 329011,
        .sky_colour = 7907327,
        .foliage_colour_override = -1,
        .grass_colour_override = -1,
        .grass_colour_mod = BIOME_GRASS_COLOUR_MOD_NONE,
    };

    net_string plains_name = NET_STRING("minecraft:plains");
    memcpy(plains->name, plains_name.ptr, plains_name.size);
    plains->name_size = plains_name.size;

    // @TODO(traks) add all the vanilla biomes
}

int
main(void) {
    init_program_nano_time();

    logs("Running Blaze");

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
        logs_errno("Failed to create socket: %s");
        exit(1);
    }
    if (server_sock >= FD_SETSIZE) {
        // can't select on this socket
        logs("Socket is FD_SETSIZE or higher");
        exit(1);
    }

    struct sockaddr * addr_ptr = (struct sockaddr *) &server_addr;

    int yes = 1;
    if (setsockopt(server_sock, SOL_SOCKET,
            SO_REUSEADDR, &yes, sizeof yes) == -1) {
        logs_errno("Failed to set sock opt: %s");
        exit(1);
    }

    // @TODO(traks) non-blocking connect? Also note that connect will finish
    // asynchronously if it has been interrupted by a signal.
    if (bind(server_sock, addr_ptr, sizeof server_addr) == -1) {
        logs_errno("Can't bind to address: %s");
        exit(1);
    }

    if (listen(server_sock, 16) == -1) {
        logs_errno("Can't listen: %s");
        exit(1);
    }

    int flags = fcntl(server_sock, F_GETFL, 0);

    if (flags == -1) {
        logs_errno("Can't get socket flags: %s");
        exit(1);
    }

    if (fcntl(server_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        logs_errno("Can't set socket flags: %s");
        exit(1);
    }

    logs("Bound to address");

    server * serv = calloc(sizeof * serv, 1);
    if (serv == NULL) {
        logs_errno("Failed to allocate server struct: %s");
        exit(1);
    }

    // reserve null entity
    serv->entities[0].flags |= ENTITY_IN_USE;
    serv->entities[0].type = ENTITY_NULL;

    // allocate memory for arenas
    serv->short_lived_scratch_size = 4194304;
    serv->short_lived_scratch = calloc(serv->short_lived_scratch_size, 1);
    if (serv->short_lived_scratch == NULL) {
        logs_errno("Failed to allocate short lived scratch arena: %s");
        exit(1);
    }

    // @TODO(traks) better sizes
    alloc_resource_loc_table(&serv->block_resource_table, 1 << 10, 1 << 16, BLOCK_TYPE_COUNT);
    alloc_resource_loc_table(&serv->item_resource_table, 1 << 10, 1 << 16, ITEM_TYPE_COUNT);
    alloc_resource_loc_table(&serv->entity_resource_table, 1 << 10, 1 << 12, ENTITY_TYPE_COUNT);
    alloc_resource_loc_table(&serv->fluid_resource_table, 1 << 10, 1 << 10, 5);

    load_item_types(serv);
    load_block_types(serv);
    load_entity_types(serv);
    load_fluid_types(serv);
    load_tags("blocktags.txt", &serv->block_tags, &serv->block_resource_table, serv);
    load_tags("itemtags.txt", &serv->item_tags, &serv->item_resource_table, serv);
    load_tags("entitytags.txt", &serv->entity_tags, &serv->entity_resource_table, serv);
    load_tags("fluidtags.txt", &serv->fluid_tags, &serv->fluid_resource_table, serv);

    init_dimension_types(serv);
    init_biomes(serv);

    int profiler_sock = -1;

    for (;;) {
        long long start_time = program_nano_time();

        server_tick(serv);

        if (profiler_sock == -1) {
            profiler_sock = socket(AF_INET, SOCK_STREAM, 0);

            struct sockaddr_in profiler_addr = {
                .sin_family = AF_INET,
                .sin_port = htons(16186), // prf
                .sin_addr = {
                    .s_addr = htonl(0x7f000001)
                }
            };

            struct sockaddr * profiler_addr_ptr = (struct sockaddr *) &profiler_addr;
            if (connect(profiler_sock, profiler_addr_ptr, sizeof profiler_addr)) {
                close(profiler_sock);
                profiler_sock = -1;
            } else {
                logs("Connected to profiler");
            }
        }
        if (profiler_sock != -1) {
            buffer_cursor cursor = {
                .buf = serv->short_lived_scratch,
                .limit = serv->short_lived_scratch_size
            };

            // fill in length after writing all the data
            cursor.index += 4;

            net_write_int(&cursor, timed_block_count);
            for (int i = 0; i < timed_block_count; i++) {
                timed_block * block = timed_blocks + i;
                int name_size = strlen(block->name);
                net_write_ubyte(&cursor, name_size);
                net_write_data(&cursor, block->name, name_size);
                net_write_ulong(&cursor, block->start_time);
                net_write_uint(&cursor, block->end_time - block->start_time);
                assert(block->start_time < block->end_time);
            }

            int end = cursor.index;
            cursor.index = 0;
            net_write_uint(&cursor, end - 4);
            cursor.index = end;

            int write_index = 0;
            while (write_index != cursor.index) {
                ssize_t send_size = send(profiler_sock,
                        cursor.buf + write_index,
                        cursor.index - write_index, 0);
                if (send_size == -1) {
                    logs_errno("Disconnected from profiler: %s");
                    close(profiler_sock);
                    profiler_sock = -1;
                    break;
                } else if (send_size == 0) {
                    logs("Profiler closed connection");
                    close(profiler_sock);
                    profiler_sock = -1;
                    break;
                }
                write_index += send_size;
            }
        }

        timed_block_count = 0;
        cur_timed_block_depth = 0;

        long long end_time = program_nano_time();
        long long elapsed_micros = (end_time - start_time) / 1000;

        if (got_sigint) {
            logs("Interrupted");
            break;
        }

        if (elapsed_micros < 50000) {
            usleep(50000 - elapsed_micros);
        }
    }

    logs("Goodbye!");
    return 0;
}
