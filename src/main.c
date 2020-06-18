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
#include <x86intrin.h>
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
    unsigned char resource_loc_size;
    unsigned char resource_loc[43];
    mc_ushort id;
} block_type_spec;

typedef struct {
    long long start_time;
    long long end_time;
    char * name;
} timed_block;

static initial_connection initial_connections[32];
static int initial_connection_count;

static block_type_spec block_type_table[1000];
static block_properties block_properties_table[1000];
static int block_type_count;

static block_property_spec block_property_specs[128];
static int block_property_spec_count;

static timed_block timed_blocks[1000];
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

static unsigned long long
program_nano_time() {
    unsigned long long diff = mach_absolute_time() - program_start_time;
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

static int
net_string_equal(net_string a, net_string b) {
    return a.size == b.size && memcmp(a.ptr, b.ptr, a.size) == 0;
}

static mc_ushort
hash_block_resource_location(net_string resource_loc) {
    mc_ushort res = 0;
    unsigned char * string = resource_loc.ptr;
    for (int i = 0; i < resource_loc.size; i++) {
        res = res * 31 + string[i];
    }
    return res % ARRAY_SIZE(block_type_table);
}

mc_short
resolve_block_type_id(net_string resource_loc) {
    mc_ushort hash = hash_block_resource_location(resource_loc);
    mc_ushort i = hash;
    for (;;) {
        block_type_spec * spec = block_type_table + i;
        net_string entry = {
            .ptr = spec->resource_loc,
            .size = spec->resource_loc_size
        };
        if (net_string_equal(resource_loc, entry)) {
            return spec->id;
        }

        i = (i + 1) % ARRAY_SIZE(block_type_table);
        if (i == hash) {
            // @TODO(traks) instead of returning -1 on error, perhaps we should
            // return some special value that also points into
            // block type id -> something tables to some default unknown block
            // type value. Could e.g. use sponge for that.
            return -1;
        }
    }
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

entity_data *
resolve_entity(server * serv, entity_id eid) {
    mc_uint index = eid & ENTITY_INDEX_MASK;
    entity_data * entity = serv->entities + index;
    if (entity->eid != eid || !(entity->flags & ENTITY_IN_USE)) {
        // return the null entity
        return serv->entities;
    }
    return entity;
}

static entity_data *
try_reserve_entity(server * serv, unsigned type) {
    for (uint32_t i = 0; i < MAX_ENTITIES; i++) {
        entity_data * entity = serv->entities + i;
        if (!(entity->flags & ENTITY_IN_USE)) {
            mc_ushort generation = serv->next_entity_generations[i];
            entity_id eid = ((mc_uint) generation << 20) | i;
            // @TODO(traks) should we default-initialise the type-specific data
            // as well? I think this only default-initialises the first entry
            // in the union.
            *entity = (entity_data) {0};
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
    entity_data * entity = resolve_entity(serv, eid);
    if (entity->type != ENTITY_NULL) {
        entity->flags &= ~ENTITY_IN_USE;
        serv->entity_count--;
    }
}

static void
server_tick(server * serv) {
    begin_timed_block("server tick");
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
        logs("Created initial connection");
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

                    memory_arena scratch_arena = {
                        .ptr = serv->short_lived_scratch,
                        .size = serv->short_lived_scratch_size
                    };

                    int list_size = serv->tab_list_size;
                    size_t list_bytes = list_size * sizeof (tab_list_entry);
                    tab_list_entry * list = alloc_in_arena(
                            &scratch_arena, list_bytes);
                    memcpy(list, serv->tab_list, list_bytes);
                    int sample_size = MIN(12, list_size);

                    unsigned char * response = alloc_in_arena(&scratch_arena, 2048);
                    int response_size = 0;
                    response_size += sprintf((char *) response + response_size,
                            "{\"version\":{\"name\":\"1.15.2\",\"protocol\":578},"
                            "\"players\":{\"max\":%d,\"online\":%d,\"sample\":[",
                            (int) MAX_PLAYERS, (int) list_size);

                    for (int i = 0; i < sample_size; i++) {
                        int target = i + (rand() % (list_size - i));
                        tab_list_entry * sampled = list + target;

                        if (i > 0) {
                            response[response_size] = ',';
                            response_size += 1;
                        }

                        entity_data * entity = resolve_entity(serv, sampled->eid);
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

                if (serv->player_brain_count == ARRAY_SIZE(serv->player_brains)) {
                    // @TODO(traks) send server full message and disconnect
                    close(init_con->sock);
                    continue;
                }

                entity_data * entity = try_reserve_entity(serv, ENTITY_PLAYER);

                if (entity->type == ENTITY_NULL) {
                    // @TODO(traks) send some message and disconnect
                    close(init_con->sock);
                    continue;
                }

                player_brain * brain;
                for (int j = 0; j < ARRAY_SIZE(serv->player_brains); j++) {
                    brain = serv->player_brains + j;
                    if ((brain->flags & PLAYER_BRAIN_IN_USE) == 0) {
                        break;
                    }
                }

                serv->player_brain_count++;
                *brain = (player_brain) {0};
                brain->flags |= PLAYER_BRAIN_IN_USE;

                brain->sock = init_con->sock;
                brain->eid = entity->eid;
                memcpy(entity->player.username, init_con->username,
                        init_con->username_size);
                entity->player.username_size = init_con->username_size;
                brain->chunk_cache_radius = -1;
                // @TODO(traks) configurable server-wide global
                brain->new_chunk_cache_radius = MAX_CHUNK_CACHE_RADIUS;
                brain->last_keep_alive_sent_tick = serv->current_tick;
                brain->flags |= PLAYER_BRAIN_GOT_ALIVE_RESPONSE;
                entity->player.selected_slot = PLAYER_FIRST_HOTBAR_SLOT;
                entity->player.gamemode = GAMEMODE_CREATIVE;

                buffer_cursor send_cursor = {
                    .buf = brain->send_buf,
                    .limit = sizeof brain->send_buf
                };

                // send game profile packet
                net_string username = {init_con->username_size, init_con->username};
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
                net_write_uint(&send_cursor, entity->eid);
                net_write_ubyte(&send_cursor, entity->player.gamemode);
                net_write_uint(&send_cursor, 0); // environment
                net_write_ulong(&send_cursor, 0); // seed
                net_write_ubyte(&send_cursor, 0); // max players (ignored by client)
                net_write_string(&send_cursor, level_type);
                net_write_varint(&send_cursor, brain->new_chunk_cache_radius - 1);
                net_write_ubyte(&send_cursor, 0); // reduced debug info
                net_write_ubyte(&send_cursor, 1); // show death screen on death

                // send set carried item packet
                net_write_varint(&send_cursor, net_varint_size(64) + 1);
                net_write_varint(&send_cursor, 64);
                net_write_ubyte(&send_cursor, entity->player.selected_slot
                        - PLAYER_FIRST_HOTBAR_SLOT);

                assert(send_cursor.error == 0);
                brain->send_cursor = send_cursor.index;

                teleport_player(brain, entity, 88, 70, 73, 0, 0);

                // @TODO(traks) ensure this can never happen instead of assering
                // it never will hopefully happen
                assert(serv->tab_list_added_count < ARRAY_SIZE(serv->tab_list_added));
                serv->tab_list_added[serv->tab_list_added_count].eid = brain->eid;
                serv->tab_list_added_count++;

                logs("Player '%.*s' joined", (int) init_con->username_size,
                        init_con->username);
            }
        }
    }

    // update players

    for (int i = 0; i < ARRAY_SIZE(serv->player_brains); i++) {
        player_brain * brain = serv->player_brains + i;
        if ((brain->flags & PLAYER_BRAIN_IN_USE) == 0) {
            continue;
        }
        tick_player_brain(brain, serv);
    }

    // remove players from tab list if necessary
    for (int i = 0; i < serv->tab_list_size; i++) {
        tab_list_entry * entry = serv->tab_list + i;
        entity_data * entity = resolve_entity(serv, entry->eid);
        if (entity->type == ENTITY_NULL) {
            // @TODO(traks) make sure this can never happen instead of hoping
            assert(serv->tab_list_removed_count < ARRAY_SIZE(serv->tab_list_removed));
            serv->tab_list_removed[serv->tab_list_removed_count] = *entry;
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
        tab_list_entry * new_entry = serv->tab_list_added + i;
        assert(serv->tab_list_size < ARRAY_SIZE(serv->tab_list));
        serv->tab_list[serv->tab_list_size] = *new_entry;
        serv->tab_list_size++;
    }

    for (int i = 0; i < ARRAY_SIZE(serv->player_brains); i++) {
        player_brain * brain = serv->player_brains + i;
        if ((brain->flags & PLAYER_BRAIN_IN_USE) == 0) {
            continue;
        }
        send_packets_to_player(brain, serv);
    }

    // clear global messages
    serv->global_msg_count = 0;

    // clear tab list updates
    serv->tab_list_added_count = 0;
    serv->tab_list_removed_count = 0;

    // load chunks from requests

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
        try_read_chunk_from_storage(pos, ch, &scratch_arena,
                block_properties_table, block_property_specs);

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

    // update chunks
    clean_up_unused_chunks();

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
    item_type * it;

    while (cursor.index != cursor.limit) {
        int arg_count = parse_database_line(&cursor, args);
        if (arg_count == 0) {
            // empty line
        } else if (net_string_equal(args[0], NET_STRING("key"))) {
            it = serv->item_types + serv->item_type_count;
            serv->item_type_count++;
        } else if (net_string_equal(args[0], NET_STRING("max_stack_size"))) {
            it->max_stack_size = atoi(args[1].ptr);
        }
    }
}

static void
load_block_types(void) {
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
    block_type_spec * type_spec;

    while (cursor.index != cursor.limit) {
        int arg_count = parse_database_line(&cursor, args);
        if (arg_count == 0) {
            // empty line
        } else if (net_string_equal(args[0], NET_STRING("key"))) {
            net_string key = args[1];
            mc_ushort hash = hash_block_resource_location(key);
            mc_ushort type_id = block_type_count;
            block_type_count++;

            for (mc_ushort i = hash; ;
                    i = (i + 1) % ARRAY_SIZE(block_type_table)) {
                type_spec = block_type_table + i;
                if (type_spec->resource_loc_size == 0) {
                    assert(key.size <= sizeof type_spec->resource_loc);
                    type_spec->resource_loc_size = key.size;
                    memcpy(type_spec->resource_loc, key.ptr, key.size);
                    type_spec->id = type_id;
                    break;
                }
            }

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

            block_properties * props = block_properties_table + type_spec->id;

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
                assert(block_property_spec_count < ARRAY_SIZE(block_property_specs));
                props->property_specs[props->property_count] = block_property_spec_count;
                props->property_count++;
                block_property_specs[block_property_spec_count] = prop_spec;
                block_property_spec_count++;
            }
        } else if (net_string_equal(args[0], NET_STRING("default_values"))) {
            block_properties * props = block_properties_table + type_spec->id;

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

    // allocate memory for arenas
    serv->short_lived_scratch_size = 4194304;
    serv->short_lived_scratch = calloc(serv->short_lived_scratch_size, 1);
    if (serv->short_lived_scratch == NULL) {
        logs_errno("Failed to allocate short lived scratch arena: %s");
        exit(1);
    }

    load_item_types(serv);
    load_block_types();

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
            }
        }
        if (profiler_sock != -1) {
            buffer_cursor cursor = {
                .buf = serv->short_lived_scratch,
                .limit = serv->short_lived_scratch_size
            };

            // fill in length after writing all the data
            cursor.index += 2;

            net_write_int(&cursor, timed_block_count);
            for (int i = 0; i < timed_block_count; i++) {
                timed_block * block = timed_blocks + i;
                int name_size = strlen(block->name);
                net_write_ubyte(&cursor, name_size);
                net_write_data(&cursor, block->name, name_size);
                net_write_ulong(&cursor, block->start_time);
                net_write_ulong(&cursor, block->end_time);
                assert(block->start_time < block->end_time);
            }

            int end = cursor.index;
            cursor.index = 0;
            net_write_ushort(&cursor, end - 2);
            cursor.index = end;

            int write_index = 0;
            while (write_index != cursor.index) {
                ssize_t send_size = send(profiler_sock,
                        cursor.buf + write_index,
                        cursor.index - write_index, 0);
                if (send_size == -1 || send_size == 0) {
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
