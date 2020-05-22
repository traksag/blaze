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
#include <sys/mman.h>
#include <sys/stat.h>
#include <zlib.h>
#include <stdalign.h>
#include "codec.h"

#define ARRAY_SIZE(x) (sizeof (x) / sizeof *(x))

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define MAX_CHUNK_CACHE_RADIUS (10)

#define MAX_CHUNK_CACHE_DIAM (2 * MAX_CHUNK_CACHE_RADIUS + 1)

#define KEEP_ALIVE_SPACING (10 * 20)

#define KEEP_ALIVE_TIMEOUT (30 * 20)

#define MAX_CHUNK_SENDS_PER_TICK (2)

#define MAX_CHUNK_LOADS_PER_TICK (2)

// must be power of 2
#define MAX_ENTITIES (1024)

enum nbt_tag {
    NBT_TAG_END,
    NBT_TAG_BYTE,
    NBT_TAG_SHORT,
    NBT_TAG_INT,
    NBT_TAG_LONG,
    NBT_TAG_FLOAT,
    NBT_TAG_DOUBLE,
    NBT_TAG_BYTE_ARRAY,
    NBT_TAG_STRING,
    NBT_TAG_LIST,
    NBT_TAG_COMPOUND,
    NBT_TAG_INT_ARRAY,
    NBT_TAG_LONG_ARRAY,

    // our own tags for internal use
    NBT_TAG_LIST_END,
    NBT_TAG_COMPOUND_IN_LIST,
    NBT_TAG_LIST_IN_LIST,
};

// in network id order
enum gamemode {
    GAMEMODE_SURVIVAL,
    GAMEMODE_CREATIVE,
    GAMEMODE_ADVENTURE,
    GAMEMODE_SPECTATOR,
};

// in network id order
enum direction {
    DIRECTION_NEG_Y, // down
    DIRECTION_POS_Y, // up
    DIRECTION_NEG_Z, // north
    DIRECTION_POS_Z, // south
    DIRECTION_NEG_X, // west
    DIRECTION_POS_X, // east
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

#define CHUNK_LOADED (1u << 0)

typedef struct {
    mc_ushort * sections[16];
    mc_ushort non_air_count[16];
    // need shorts to store 257 different heights
    mc_ushort motion_blocking_height_map[256];

    // increment if you want to keep a chunk available in the map, decrement
    // if you no longer care for the chunk.
    // If = 0 the chunk will be removed from the map at some point.
    mc_uint available_interest;
    unsigned flags;

    // @TODO(traks) more changed blocks, better compression, store it per chunk
    // section perhaps. Figure out when this limit can be exceeded. I highly
    // doubt more than 16 blocks will be changed per chunk due to players except
    // if very high player density.
    mc_ushort changed_blocks[16];
    mc_ubyte changed_block_count;
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

typedef struct {
    mc_ubyte max_stack_size;
} item_type;

typedef struct {
    mc_int type;
    mc_ubyte size;
} item_stack;

enum entity_type {
    ENTITY_NULL,
    ENTITY_PLAYER,
};

#define ENTITY_INDEX_MASK (MAX_ENTITIES - 1)

// Top 12 bits are used for the generation, lowest 20 bits can be used for the
// index into the entity table. Bits actually used for the index depends on
// MAX_ENTITIES.
static_assert(MAX_ENTITIES <= (1UL << 20), "MAX_ENTITIES too large");
typedef mc_uint entity_id;

// Player inventory slots are indexed as follows:
//
//  0           the crafting grid result slot
//  1-4         the 2x2 crafting grid slots
//  5-8         the 4 armour slots
//  9-35        the 36 main inventory slots
//  36-44       hotbar slots
//  45          off hand slot
//
// Here are some defines for convenience.
#define PLAYER_SLOTS (46)
#define PLAYER_FIRST_HOTBAR_SLOT (36)
#define PLAYER_LAST_HOTBAR_SLOT (44)
#define PLAYER_OFF_HAND_SLOT (45)

typedef struct {
    unsigned char username[16];
    int username_size;

    item_stack slots_prev_tick[PLAYER_SLOTS];
    item_stack slots[PLAYER_SLOTS];
    static_assert(PLAYER_SLOTS <= 64, "Too many player slots");
    mc_ulong slots_needing_update;
    unsigned char selected_slot;

    unsigned char gamemode;
} player_data;

#define ENTITY_IN_USE ((unsigned) (1 << 0))

#define ENTITY_TELEPORTING ((unsigned) (1 << 1))

#define ENTITY_ON_GROUND ((unsigned) (1 << 2))

typedef struct {
    mc_double x;
    mc_double y;
    mc_double z;
    mc_float rot_x;
    mc_float rot_y;
    entity_id eid;
    unsigned flags;
    unsigned type;

    union {
        player_data player;
    };
} entity_data;

#define PLAYER_BRAIN_IN_USE ((unsigned) (1 << 0))

#define PLAYER_BRAIN_SENT_TELEPORT ((unsigned) (1 << 2))

#define PLAYER_BRAIN_GOT_ALIVE_RESPONSE ((unsigned) (1 << 3))

#define PLAYER_BRAIN_SHIFTING ((unsigned) (1 << 4))

#define PLAYER_BRAIN_SPRINTING ((unsigned) (1 << 5))

#define PLAYER_BRAIN_INITIALISED_TAB_LIST ((unsigned) (1 << 6))

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

    entity_id eid;

    // @TODO(traks) this feels a bit silly, but very simple
    entity_id tracked_entities[MAX_ENTITIES];
} player_brain;

typedef struct {
    unsigned char * ptr;
    mc_int size;
    mc_int index;
} memory_arena;

typedef struct {
    mc_ushort size;
    unsigned char text[512];
} global_msg;

typedef union {
    struct {
        mc_uint buffer_index:22;
        mc_uint tag:5;
        mc_uint element_tag:5;
    };
    mc_uint next_compound_entry;
    mc_uint list_size;
} nbt_tape_entry;

typedef struct {
    mc_uint is_list:1;
    mc_uint element_tag:4;
    mc_uint prev_compound_entry:20;
    mc_uint list_elems_remaining;
} nbt_level_info;

typedef struct {
    mc_ushort base_state;
    unsigned char property_count;
    unsigned char property_specs[8];
    unsigned char default_value_indices[8];
} block_properties;

typedef struct {
    unsigned char resource_loc_size;
    unsigned char resource_loc[43];
    mc_ushort id;
} block_type_spec;

typedef struct {
    unsigned char value_count;
    // name size, name, value size, value, value size, value, etc.
    unsigned char tape[255];
} block_property_spec;

typedef struct {
    entity_id eid;
} tab_list_entry;

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

// All chunks that should be loaded. Stored in a request list to allow for
// ordered loads. If a
// @TODO(traks) appropriate size
static chunk_pos chunk_load_requests[64];
static int chunk_load_request_count;

static entity_data entities[MAX_ENTITIES];
static mc_ushort next_entity_generations[MAX_ENTITIES];
static mc_int entity_count;

// global messages for the current tick
static global_msg global_msgs[16];
static int global_msg_count;

static void * short_lived_scratch;
static mc_int short_lived_scratch_size;

static item_type item_types[1000];
static int item_type_count;

static block_type_spec block_type_table[1000];
static block_properties block_properties_table[1000];
static int block_type_count;

static block_property_spec block_property_specs[128];
static int block_property_spec_count;

static tab_list_entry tab_list_added[64];
static int tab_list_added_count;
static tab_list_entry tab_list_removed[64];
static int tab_list_removed_count;
static tab_list_entry tab_list[1024];
static int tab_list_size;

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

static void *
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

static mc_short
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

static int
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

static entity_data *
resolve_entity(entity_id eid) {
    mc_uint index = eid & ENTITY_INDEX_MASK;
    entity_data * entity = entities + index;
    if (entity->eid != eid || !(entity->flags & ENTITY_IN_USE)) {
        // return the null entity
        return entities;
    }
    return entity;
}

static entity_data *
try_reserve_entity(unsigned type) {
    for (uint32_t i = 0; i < MAX_ENTITIES; i++) {
        entity_data * entity = entities + i;
        if (!(entity->flags & ENTITY_IN_USE)) {
            mc_ushort generation = next_entity_generations[i];
            entity_id eid = ((mc_uint) generation << 20) | i;
            *entity = (entity_data) {0};
            entity->eid = eid;
            entity->type = type;
            entity->flags |= ENTITY_IN_USE;
            next_entity_generations[i] = (generation + 1) & 0xfff;
            entity_count++;
            return entity;
        }
    }
    // first entity used as placeholder for null entity
    return entities;
}

static void
evict_entity(entity_id eid) {
    entity_data * entity = resolve_entity(eid);
    if (entity->type != ENTITY_NULL) {
        entity->flags &= ~ENTITY_IN_USE;
        entity_count--;
    }
}

static void
teleport_player(player_brain * brain, mc_double new_x,
        mc_double new_y, mc_double new_z) {
    brain->current_teleport_id++;
    entity_data * entity = resolve_entity(brain->eid);
    entity->flags |= ENTITY_TELEPORTING;
    entity->x = new_x;
    entity->y = new_y;
    entity->z = new_z;
}

static void
process_move_player_packet(entity_data * entity,
        mc_double new_x, mc_double new_y, mc_double new_z,
        mc_float new_rot_x, mc_float new_rot_y, int on_ground) {
    if ((entity->flags & ENTITY_TELEPORTING) != 0) {
        return;
    }

    entity->x = new_x;
    entity->y = new_y;
    entity->z = new_z;
    entity->rot_x = new_rot_x;
    entity->rot_y = new_rot_y;
    if (on_ground) {
        entity->flags |= ENTITY_ON_GROUND;
    } else {
        entity->flags &= ~ENTITY_ON_GROUND;
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

    int index = ((y & 0xf) << 8) | (z << 4) | x;
    return section[index];
}

static void
recalculate_chunk_motion_blocking_height_map(chunk * ch) {
    for (int zx = 0; zx < 16 * 16; zx++) {
        ch->motion_blocking_height_map[zx] = 0;
        for (int y = 255; y >= 0; y--) {
            mc_ushort block_state = chunk_get_block_state(ch,
                    zx & 0xf, y, zx >> 4);
            // @TODO(traks) other airs
            if (block_state != 0) {
                ch->motion_blocking_height_map[zx] = y + 1;
                break;
            }
        }
    }
}

static void
chunk_set_block_state(chunk * ch, int x, int y, int z, mc_ushort block_state) {
    assert(0 <= x && x < 16);
    assert(0 <= y && y < 256);
    assert(0 <= z && z < 16);
    assert(ch->flags & CHUNK_LOADED);
    // @TODO(traks) somehow ensure this never fails even with tons of players,
    // or make sure we appropriate handle cases in which too many changes occur
    // to a chunk per tick.
    assert(ch->changed_block_count < ARRAY_SIZE(ch->changed_blocks));

    // format is that of the chunk blocks update packet
    ch->changed_blocks[ch->changed_block_count] =
            ((mc_ushort) x << 12) | (z << 8) | y;
    ch->changed_block_count++;

    int section_y = y >> 4;
    mc_ushort * section = ch->sections[section_y];

    if (section == NULL) {
        // @TODO(traks) better allocation scheme
        section = calloc(4096, sizeof (mc_ushort));
        if (section == NULL) {
            log_errno("Failed to allocate section: %s");
            exit(1);
        }
        ch->sections[section_y] = section;
    }

    int index = ((y & 0xf) << 8) | (z << 4) | x;

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

static mc_uint
nbt_move_to_key(net_string matcher, nbt_tape_entry * tape,
        mc_uint start_index, buffer_cursor * cursor) {
    mc_uint i = start_index;
    while (tape[i].tag != NBT_TAG_END) {
        cursor->index = tape[i].buffer_index;
        mc_ushort key_size = net_read_ushort(cursor);
        unsigned char * key = cursor->buf + cursor->index;
        if (key_size == matcher.size
                && memcmp(key, matcher.ptr, matcher.size) == 0) {
            cursor->index += key_size;
            break;
        }
        i = tape[i + 1].next_compound_entry;
    }
    return i;
}

static nbt_tape_entry *
load_nbt(buffer_cursor * cursor, memory_arena * arena, int max_level) {
    // @TODO(traks) Here's another idea for NBT parsing. Instead of using a
    // tape, for each level we could maintain a linked list of blocks of
    // something similar to tape entries. The benefit being that we need to jump
    // over subtrees a lot less (depending on the block size) when iterating
    // through the keys, and that we don't need to track these jumps as is done
    // when constructing the tape.
    nbt_tape_entry * tape = alloc_in_arena(arena, 1048576);
    memory_arena scratch_arena = {
        .ptr = arena->ptr,
        .index = arena->index,
        .size = arena->size
    };
    nbt_level_info * level_info = alloc_in_arena(&scratch_arena,
            (max_level + 1) * sizeof *level_info);
    int cur_tape_index = 0;
    int cur_level = 0;
    level_info[0] = (nbt_level_info) {0};

    mc_ubyte tag = net_read_ubyte(cursor);

    if (tag == NBT_TAG_END) {
        // no nbt data
        goto exit;
    } else if (tag != NBT_TAG_COMPOUND) {
        cursor->error = 1;
        goto exit;
    }

    // skip key of root compound
    mc_ushort key_size = net_read_ushort(cursor);
    if (key_size > cursor->limit - cursor->index) {
        cursor->error = 1;
        goto exit;
    }
    cursor->index += key_size;

    for (;;) {
        if (cur_level == max_level + 1) {
            cursor->error = 1;
            goto exit;
        }
        if (!level_info[cur_level].is_list) {
            // compound
            tag = net_read_ubyte(cursor);
            tape[level_info[cur_level].prev_compound_entry + 1]
                    .next_compound_entry = cur_tape_index;

            if (tag == NBT_TAG_END) {
                tape[cur_tape_index] = (nbt_tape_entry) {.tag = NBT_TAG_END};
                cur_tape_index++;
                cur_level--;

                if (cur_level == -1) {
                    goto exit;
                } else {
                    continue;
                }
            }

            mc_int entry_start = cursor->index;
            key_size = net_read_ushort(cursor);
            if (key_size > cursor->limit - cursor->index) {
                cursor->error = 1;
                goto exit;
            }
            cursor->index += key_size;

            level_info[cur_level].prev_compound_entry = cur_tape_index;
            nbt_tape_entry new_entry = {
                .buffer_index = entry_start,
                .tag = tag
            };
            tape[cur_tape_index] = new_entry;
            cur_tape_index++;
            // increment another time for the next pointer
            cur_tape_index++;
        } else {
            // list
            if (level_info[cur_level].list_elems_remaining == 0) {
                nbt_tape_entry new_entry = {.tag = NBT_TAG_LIST_END};
                tape[cur_tape_index] = new_entry;
                cur_tape_index++;
                cur_level--;
                continue;
            }

            level_info[cur_level].list_elems_remaining--;
            tag = level_info[cur_level].element_tag;

            if (tag == NBT_TAG_COMPOUND) {
                nbt_tape_entry new_entry = {.tag = NBT_TAG_COMPOUND_IN_LIST};
                tape[cur_tape_index] = new_entry;
                cur_tape_index++;
            }
            if (tag == NBT_TAG_LIST) {
                nbt_tape_entry new_entry = {.tag = NBT_TAG_LIST_IN_LIST};
                tape[cur_tape_index] = new_entry;
                cur_tape_index++;
            }
        }

        static mc_byte elem_bytes[] = {0, 1, 2, 4, 8, 4, 8};
        static mc_byte array_elem_bytes[] = {1, 0, 0, 0, 4, 8};
        switch (tag) {
        case NBT_TAG_END:
            // Minecraft uses this sometimes for empty lists even if the
            // element tag differs if the list is non-empty... why...?
            goto exit;
        case NBT_TAG_BYTE:
        case NBT_TAG_SHORT:
        case NBT_TAG_INT:
        case NBT_TAG_LONG:
        case NBT_TAG_FLOAT:
        case NBT_TAG_DOUBLE: {
            int bytes = elem_bytes[tag];
            if (cursor->index > cursor->limit - bytes) {
                cursor->error = 1;
                goto exit;
            } else {
                cursor->index += bytes;
            }
            break;
        }
        case NBT_TAG_BYTE_ARRAY:
        case NBT_TAG_INT_ARRAY:
        case NBT_TAG_LONG_ARRAY: {
            mc_long elem_bytes = array_elem_bytes[tag - NBT_TAG_BYTE_ARRAY];
            mc_long array_size = net_read_uint(cursor);
            if (cursor->index > (mc_long) cursor->limit
                    - elem_bytes * array_size) {
                cursor->error = 1;
                goto exit;
            } else {
                cursor->index += elem_bytes * array_size;
            }
            break;
        }
        case NBT_TAG_STRING: {
            mc_ushort size = net_read_ushort(cursor);
            if (cursor->index > cursor->limit - size) {
                cursor->error = 1;
            } else {
                cursor->index += size;
            }
            break;
        }
        case NBT_TAG_LIST: {
            cur_level++;
            mc_uint element_tag = net_read_ubyte(cursor);
            mc_long list_size = net_read_uint(cursor);
            level_info[cur_level] = (nbt_level_info) {
                .is_list = 1,
                .element_tag = element_tag,
                .list_elems_remaining = list_size
            };

            // append size entry
            nbt_tape_entry new_entry = {.list_size = list_size};
            tape[cur_tape_index] = new_entry;
            cur_tape_index++;
            break;
        }
        case NBT_TAG_COMPOUND:
            cur_level++;
            level_info[cur_level] = (nbt_level_info) {0};
            break;
        default:
            goto exit;
        }
    }

exit:
    return tape;
}

static void
try_read_chunk_from_storage(chunk_pos pos, chunk * ch) {
    // @TODO(traks) error handling and/or error messages for all failure cases
    // in this entire function?
    memory_arena scratch_arena = {
        .ptr = short_lived_scratch,
        .size = short_lived_scratch_size
    };

    __m128i chunk_xz = _mm_set_epi32(0, 0, pos.z, pos.x);
    __m128i region_xz = _mm_srai_epi32(chunk_xz, 5);
    int region_x = _mm_extract_epi32(region_xz, 0);
    int region_z = _mm_extract_epi32(region_xz, 1);

    unsigned char file_name[64];
    int file_name_size = sprintf((void *) file_name,
            "world/region/r.%d.%d.mca", region_x, region_z);

    int region_fd = open((void *) file_name, O_RDONLY);
    if (region_fd == -1) {
        log_errno("Failed to open region file: %s");
        return;
    }

    struct stat region_stat;
    if (fstat(region_fd, &region_stat)) {
        log_errno("Failed to get region file stat: %s");
        close(region_fd);
        return;
    }

    // @TODO(traks) should we unmap this or something?
    void * region_mmap = mmap(NULL, region_stat.st_size, PROT_READ,
            MAP_PRIVATE, region_fd, 0);
    if (region_mmap == MAP_FAILED) {
        log_errno("Failed to mmap region file: %s");
        close(region_fd);
        return;
    }

    // after mmaping we can close the file descriptor
    close(region_fd);

    buffer_cursor cursor = {
        .buf = region_mmap,
        .limit = region_stat.st_size
    };

    // First read from the chunk location table at which sector (4096 byte
    // block) the chunk data starts.
    // @TODO(traks) this & better work for negative values
    int index = ((pos.z & 0x1f) << 5) | (pos.x & 0x1f);
    cursor.index = index << 2;
    mc_uint loc = net_read_uint(&cursor);

    if (loc == 0) {
        // chunk not present in region file
        return;
    }

    mc_uint sector_offset = loc >> 8;
    mc_uint sector_count = loc & 0xff;

    if (sector_offset < 2) {
        log("Chunk data in header");
        return;
    }
    if (sector_count == 0) {
        log("Chunk data uses 0 sectors");
        return;
    }
    if (sector_offset + sector_count > (cursor.limit >> 12)) {
        log("Chunk data out of bounds");
        return;
    }

    cursor.index = sector_offset << 12;
    mc_uint size_in_bytes = net_read_uint(&cursor);

    if (size_in_bytes > (sector_count << 12)) {
        log("Chunk data outside of its sectors");
        return;
    }

    cursor.limit = cursor.index + size_in_bytes;
    mc_ubyte storage_type = net_read_ubyte(&cursor);

    if (cursor.error) {
        log("Chunk header reading error");
        return;
    }

    if (storage_type & 0x80) {
        // @TODO(traks) separate file is used to store the chunk
        log("External chunk storage");
        return;
    }

    int windowBits;

    if (storage_type == 1) {
        // gzip compressed
        // 16 means: gzip stream and determine window size from header
        windowBits = 16;
    } else if (storage_type == 2) {
        // zlib compressed
        // 0 means: zlib stream and determine window size from header
        windowBits = 0;
    } else {
        log("Unknown chunk compression method");
        return;
    }

    // @TODO(traks) perhaps use https://github.com/ebiggers/libdeflate instead
    // of zlib. Using zlib now just because I had the code for it laying around.
    z_stream zstream;
    zstream.zalloc = NULL;
    zstream.zfree = NULL;
    zstream.opaque = NULL;

    if (inflateInit2(&zstream, windowBits) != Z_OK) {
        log("inflateInit failed");
        return;
    }

    zstream.next_in = cursor.buf + cursor.index;
    zstream.avail_in = cursor.limit - cursor.index;

    size_t max_uncompressed_size = 2097152;
    unsigned char * uncompressed = alloc_in_arena(&scratch_arena,
            max_uncompressed_size);

    zstream.next_out = uncompressed;
    zstream.avail_out = max_uncompressed_size;

    if (inflate(&zstream, Z_FINISH) != Z_STREAM_END) {
        log("Failed to finish inflating chunk: %s", zstream.msg);
        return;
    }

    if (inflateEnd(&zstream) != Z_OK) {
        log("inflateEnd failed");
        return;
    }

    if (zstream.avail_in != 0) {
        log("Didn't inflate entire chunk");
        return;
    }

    cursor = (buffer_cursor) {
        .buf = uncompressed,
        .limit = zstream.total_out
    };

    // @TODO(traks) more appropriate max level, currently 64
    nbt_tape_entry * tape = load_nbt(&cursor, &scratch_arena, 64);

    if (cursor.error) {
        log("Failed to read uncompressed NBT data");
        return;
    }

    for (int section_y = 0; section_y < 16; section_y++) {
        assert(ch->sections[section_y] == NULL);
    }

    nbt_move_to_key(NET_STRING("DataVersion"), tape, 0, &cursor);
    mc_int data_version = net_read_int(&cursor);
    if (data_version != 2230) {
        log("Unknown data version %jd", (intmax_t) data_version);
        return;
    }

    mc_uint level_start = nbt_move_to_key(NET_STRING("Level"), tape, 0, &cursor);
    if (tape[level_start].tag != NBT_TAG_COMPOUND) {
        log("NBT Level tag not a compound");
        return;
    }
    level_start += 2;

    mc_uint sections_start = nbt_move_to_key(NET_STRING("Sections"),
            tape, level_start, &cursor);
    mc_uint section_count = tape[sections_start + 2].list_size;
    mc_uint section_start = sections_start + 4;

    // maximum amount of memory the palette will ever use
    mc_ushort * palette_map = alloc_in_arena(&scratch_arena,
            4096 * sizeof (mc_ushort));

    for (mc_uint sectioni = 0; sectioni < section_count; sectioni++) {
        nbt_move_to_key(NET_STRING("Y"), tape, section_start, &cursor);
        mc_byte section_y = net_read_byte(&cursor);

        if (ch->sections[section_y] != NULL) {
            log("Duplicate section Y %d", (int) section_y);
            return;
        }

        int palette_start = nbt_move_to_key(NET_STRING("Palette"),
                tape, section_start, &cursor);

        if (tape[palette_start].tag != NBT_TAG_END) {
            if (section_y < 0 || section_y >= 16) {
                log("Section Y %d with palette", (int) section_y);
                return;
            }

            // @TODO(traks) get rid of this dynamic allocation. May be fine to
            // fail since loading the chunk can fail in all sorts of ways
            // anyhow.
            mc_ushort * section = calloc(sizeof *section, 4096);
            if (section == NULL) {
                log_errno("Failed to allocate section: %s");
                exit(1);
            }
            // Note that the section allocation will be freed when the chunk
            // gets removed somewhere else in the code base.
            ch->sections[section_y] = section;

            mc_uint palette_size = tape[palette_start + 2].list_size;
            mc_uint palettei_start = palette_start + 4;

            for (uint palettei = 0; palettei < palette_size; palettei++) {
                mc_short type_id = 0;

                mc_uint name_start = nbt_move_to_key(NET_STRING("Name"),
                        tape, palettei_start, &cursor);
                if (tape[name_start].tag != NBT_TAG_END) {
                    mc_ushort resource_loc_size = net_read_ushort(&cursor);
                    net_string resource_loc = {
                        .size = resource_loc_size,
                        .ptr = cursor.buf + cursor.index
                    };
                    type_id = resolve_block_type_id(resource_loc);
                    if (type_id == -1) {
                        // @TODO(traks) should probably just error out
                        type_id = 2;
                    }
                }

                mc_ushort stride = 0;

                mc_uint props_start = nbt_move_to_key(NET_STRING("Properties"),
                        tape, palettei_start, &cursor);
                if (tape[props_start].tag == NBT_TAG_COMPOUND) {
                    props_start += 2;
                }

                block_properties * props = block_properties_table + type_id;
                for (int propi = 0; propi < props->property_count; propi++) {
                    block_property_spec * prop_spec = block_property_specs
                            + props->property_specs[propi];
                    net_string prop_name = {
                        .size = prop_spec->tape[0],
                        .ptr = prop_spec->tape + 1
                    };
                    mc_uint val_start = nbt_move_to_key(prop_name,
                            tape, props_start, &cursor);
                    int val_index = -1;
                    if (tape[val_start].tag != NBT_TAG_END) {
                        mc_ushort val_size = net_read_ushort(&cursor);
                        net_string val = {
                            .size = val_size,
                            .ptr = cursor.buf + cursor.index
                        };
                        val_index = find_property_value_index(prop_spec, val);
                    }
                    if (val_index == -1) {
                        val_index = props->default_value_indices[propi];
                    }
                    stride = stride * prop_spec->value_count + val_index;
                }

                palette_map[palettei] = props->base_state + stride;

                // move to end of palette entry compound
                mc_uint i = palettei_start;
                while (tape[i].tag != NBT_TAG_END) {
                    i = tape[i + 1].next_compound_entry;
                }
                palettei_start = i + 2;
            }

            nbt_move_to_key(NET_STRING("BlockStates"),
                    tape, section_start, &cursor);
            mc_uint entry_count = net_read_uint(&cursor);
            mc_uint bits_per_id = entry_count >> 6;
            mc_uint id_mask = (1 << bits_per_id) - 1;
            int offset = 0;
            mc_ulong entry = net_read_ulong(&cursor);

            for (int j = 0; j < 4096; j++) {
                mc_uint id = (entry >> offset) & id_mask;
                mc_uint remaining_bits = 64 - offset;
                if (bits_per_id >= remaining_bits) {
                    entry = net_read_ulong(&cursor);
                    id |= (entry << remaining_bits) & id_mask;
                    offset = bits_per_id - remaining_bits;
                } else {
                    offset += bits_per_id;
                }

                if (id >= palette_size) {
                    log("Out of bounds palette ID");
                    return;
                }

                mc_ushort block_state = palette_map[id];
                section[j] = block_state;

                if (block_state != 0) {
                    ch->non_air_count[section_y]++;
                }
            }

            ch->sections[section_y] = section;
        }

        // move to end of section compound
        mc_uint i = section_start;
        while (tape[i].tag != NBT_TAG_END) {
            i = tape[i + 1].next_compound_entry;
        }
        section_start = i + 2;
    }

    recalculate_chunk_motion_blocking_height_map(ch);

    if (cursor.error) {
        log("Failed to read uncompressed data");
        return;
    }

    ch->flags |= CHUNK_LOADED;
}

static chunk *
get_or_create_chunk(chunk_pos pos) {
    int hash = hash_chunk_pos(pos);
    chunk_bucket * bucket = chunk_map + hash;
    int bucket_size = bucket->size;

    int i;
    for (i = 0; i < bucket_size; i++) {
        if (chunk_pos_equal(bucket->positions[i], pos)) {
            return bucket->chunks + i;
        }
    }

    assert(i < CHUNKS_PER_BUCKET);

    bucket->positions[i] = pos;
    bucket->size++;
    chunk * ch = bucket->chunks + i;
    *ch = (chunk) {0};
    return ch;
}

static chunk *
get_chunk_if_loaded(chunk_pos pos) {
    int hash = hash_chunk_pos(pos);
    chunk_bucket * bucket = chunk_map + hash;
    int bucket_size = bucket->size;

    for (int i = 0; i < bucket_size; i++) {
        if (chunk_pos_equal(bucket->positions[i], pos)) {
            chunk * ch = bucket->chunks + i;
            if (ch->flags & CHUNK_LOADED) {
                return ch;
            }
            return NULL;
        }
    }

    return NULL;
}

static chunk *
get_chunk_if_available(chunk_pos pos) {
    int hash = hash_chunk_pos(pos);
    chunk_bucket * bucket = chunk_map + hash;
    int bucket_size = bucket->size;

    for (int i = 0; i < bucket_size; i++) {
        if (chunk_pos_equal(bucket->positions[i], pos)) {
            return bucket->chunks + i;
        }
    }

    return NULL;
}

static void
send_chunk_fully(buffer_cursor * send_cursor, chunk_pos pos, chunk * ch) {
    // bit mask for included chunk sections; bottom section in least
    // significant bit
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
disconnect_player_now(player_brain * brain) {
    close(brain->sock);
    brain->flags = 0;
    player_brain_count--;
    evict_entity(brain->eid);

    mc_short chunk_cache_min_x = brain->chunk_cache_centre_x - brain->chunk_cache_radius;
    mc_short chunk_cache_max_x = brain->chunk_cache_centre_x + brain->chunk_cache_radius;
    mc_short chunk_cache_min_z = brain->chunk_cache_centre_z - brain->chunk_cache_radius;
    mc_short chunk_cache_max_z = brain->chunk_cache_centre_z + brain->chunk_cache_radius;

    for (mc_short x = chunk_cache_min_x; x <= chunk_cache_max_x; x++) {
        for (mc_short z = chunk_cache_min_z; z <= chunk_cache_max_z; z++) {
            chunk_pos pos = {.x = x, .z = z};
            chunk * ch = get_chunk_if_available(pos);
            assert(ch != NULL);
            ch->available_interest--;
        }
    }
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

                entity_data * entity = try_reserve_entity(ENTITY_PLAYER);

                if (entity->type == ENTITY_NULL) {
                    // @TODO(traks) send some message and disconnect
                    close(init_con->sock);
                    continue;
                }

                player_brain * brain;
                for (int j = 0; j < ARRAY_SIZE(player_brains); j++) {
                    brain = player_brains + j;
                    if ((brain->flags & PLAYER_BRAIN_IN_USE) == 0) {
                        break;
                    }
                }

                player_brain_count++;
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
                brain->last_keep_alive_sent_tick = current_tick;
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

                brain->send_cursor = send_cursor.index;

                teleport_player(brain, 88, 70, 73);

                // @TODO(traks) ensure this can never happen instead of assering
                // it never will hopefully happen
                assert(tab_list_added_count < ARRAY_SIZE(tab_list_added));
                tab_list_added[tab_list_added_count].eid = brain->eid;
                tab_list_added_count++;

                log("Player '%.*s' joined", (int) init_con->username_size,
                        init_con->username);
            }
        }
    }

    // update players

    for (int i = 0; i < ARRAY_SIZE(player_brains); i++) {
        player_brain * brain = player_brains + i;
        if ((brain->flags & PLAYER_BRAIN_IN_USE) == 0) {
            continue;
        }

        entity_data * entity = resolve_entity(brain->eid);
        assert(entity->type == ENTITY_PLAYER);
        int sock = brain->sock;
        ssize_t rec_size = recv(sock, brain->rec_buf + brain->rec_cursor,
                sizeof brain->rec_buf - brain->rec_cursor, 0);

        if (rec_size == 0) {
            disconnect_player_now(brain);
        } else if (rec_size == -1) {
            // EAGAIN means no data received
            if (errno != EAGAIN) {
                log_errno("Couldn't receive protocol data: %s");
                disconnect_player_now(brain);
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
                    disconnect_player_now(brain);
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

                    if ((entity->flags & ENTITY_TELEPORTING)
                            && (brain->flags & PLAYER_BRAIN_SENT_TELEPORT)
                            && teleport_id == brain->current_teleport_id) {
                        entity->flags &= ~ENTITY_TELEPORTING;
                        brain->flags &= ~PLAYER_BRAIN_SENT_TELEPORT;
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
                    net_string chat = net_read_string(&rec_cursor, 256);

                    if (global_msg_count < ARRAY_SIZE(global_msgs)) {
                        global_msg * msg = global_msgs + global_msg_count;
                        global_msg_count++;
                        int text_size = sprintf(
                                (void *) msg->text, "<%.*s> %.*s",
                                (int) entity->player.username_size,
                                entity->player.username,
                                (int) chat.size, chat.ptr);
                        msg->size = text_size;
                    }
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
                    process_move_player_packet(entity, x, y, z,
                            entity->rot_x, entity->rot_y, on_ground);
                    break;
                }
                case 18: { // move player pos rot
                    mc_double x = net_read_double(&rec_cursor);
                    mc_double y = net_read_double(&rec_cursor);
                    mc_double z = net_read_double(&rec_cursor);
                    mc_float rot_y = net_read_float(&rec_cursor);
                    mc_float rot_x = net_read_float(&rec_cursor);
                    int on_ground = net_read_ubyte(&rec_cursor);
                    process_move_player_packet(entity, x, y, z,
                            rot_x, rot_y, on_ground);
                    break;
                }
                case 19: { // move player rot
                    mc_float rot_y = net_read_float(&rec_cursor);
                    mc_float rot_x = net_read_float(&rec_cursor);
                    int on_ground = net_read_ubyte(&rec_cursor);
                    process_move_player_packet(entity,
                            entity->x, entity->y, entity->z,
                            rot_x, rot_y, on_ground);
                    break;
                }
                case 20: { // move player
                    int on_ground = net_read_ubyte(&rec_cursor);
                    process_move_player_packet(entity,
                            entity->x, entity->y, entity->z,
                            entity->rot_x, entity->rot_y, on_ground);
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
                    // @TODO(traks) validate block pos inside world
                    net_block_pos block_pos = net_read_block_pos(&rec_cursor);
                    mc_ubyte direction = net_read_ubyte(&rec_cursor);

                    switch (action) {
                    case 0: { // start destroy block
                        // The player started mining the block. If the player is in
                        // creative mode, the stop and abort packets are not sent.
                        // @TODO(traks) implementation for other gamemodes
                        if (entity->player.gamemode == GAMEMODE_CREATIVE) {
                            // @TODO(traks) ensure block pos is close to the
                            // player and the chunk is sent to the player
                            __m128i xz = _mm_set_epi32(0, 0, block_pos.z, block_pos.x);
                            __m128i chunk_xz = _mm_srai_epi32(xz, 4);
                            chunk_pos pos = {
                                .x = _mm_extract_epi32(chunk_xz, 0),
                                .z = _mm_extract_epi32(chunk_xz, 1)
                            };
                            chunk * ch = get_chunk_if_loaded(pos);
                            if (ch == NULL) {
                                // @TODO(traks) client will still see block as
                                // broken. Does that really matter? A forget
                                // packet will probably reach them soon enough.
                                break;
                            }

                            // @TODO(traks) ANDing signed integers better work
                            int in_chunk_x = block_pos.x & 0xf;
                            int in_chunk_z = block_pos.z & 0xf;
                            chunk_set_block_state(ch, in_chunk_x, block_pos.y,
                                    in_chunk_z, 0);
                        }
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
                        // @TODO(traks) create item entities
                        int sel_slot = entity->player.selected_slot;
                        entity->player.slots[sel_slot] = (item_stack) {0};
                        break;
                    }
                    case 4: { // drop item
                        // @TODO(traks) create item entity
                        int sel_slot = entity->player.selected_slot;
                        item_stack * is = entity->player.slots + sel_slot;
                        if (is->size > 0) {
                            is->size--;
                        } else {
                            *is = (item_stack) {0};
                        }
                        break;
                    }
                    case 5: { // release use item
                        // @TODO(traks)
                        break;
                    }
                    case 6: { // swap held items
                        int sel_slot = entity->player.selected_slot;
                        item_stack * sel = entity->player.slots + sel_slot;
                        item_stack * off = entity->player.slots + PLAYER_OFF_HAND_SLOT;
                        item_stack sel_copy = *sel;
                        *sel = *off;
                        *off = sel_copy;
                        // client doesn't update its view of the inventory for
                        // this packet, so send updates to the client
                        entity->player.slots_needing_update |= (mc_ulong) 1 << sel_slot;
                        entity->player.slots_needing_update |= (mc_ulong) 1 << PLAYER_OFF_HAND_SLOT;
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
                    if (slot > PLAYER_LAST_HOTBAR_SLOT - PLAYER_FIRST_HOTBAR_SLOT) {
                        rec_cursor.error = 1;
                        break;
                    }
                    entity->player.selected_slot = PLAYER_FIRST_HOTBAR_SLOT + slot;
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
                    mc_ushort slot = net_read_ushort(&rec_cursor);
                    mc_ubyte has_item = net_read_ubyte(&rec_cursor);

                    if (slot >= PLAYER_SLOTS) {
                        rec_cursor.error = 1;
                        break;
                    }

                    item_stack * is = entity->player.slots + slot;
                    *is = (item_stack) {0};

                    if (has_item) {
                        is->type = net_read_varint(&rec_cursor);
                        is->size = net_read_ubyte(&rec_cursor);

                        if (is->type < 0 || is->type >= item_type_count) {
                            is->type = 0;
                            entity->player.slots_needing_update |=
                                    (mc_ulong) 1 << slot;
                        }
                        item_type * type = item_types + is->type;
                        if (is->size > type->max_stack_size) {
                            is->size = type->max_stack_size;
                            entity->player.slots_needing_update |=
                                    (mc_ulong) 1 << slot;
                        }

                        memory_arena scratch_arena = {
                            .ptr = short_lived_scratch,
                            .size = short_lived_scratch_size
                        };
                        // @TODO(traks) better value than 64 for the max level
                        nbt_tape_entry * tape = load_nbt(&rec_cursor,
                                &scratch_arena, 64);
                        if (rec_cursor.error) {
                            break;
                        }

                        // @TODO(traks) use NBT data to construct item stack
                    }
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
                    net_block_pos clicked_pos = net_read_block_pos(&rec_cursor);
                    mc_int clicked_face = net_read_varint(&rec_cursor);
                    mc_float click_offset_x = net_read_float(&rec_cursor);
                    mc_float click_offset_y = net_read_float(&rec_cursor);
                    mc_float click_offset_z = net_read_float(&rec_cursor);
                    // @TODO(traks) figure out what this is used for
                    mc_ubyte is_inside = net_read_ubyte(&rec_cursor);

                    // @TODO(traks) if we cancel at any point and don't kick the
                    // client, send some packets to the client to make the
                    // original blocks reappear, otherwise we'll get a desync

                    if (hand != 0 && hand != 1) {
                        rec_cursor.error = 1;
                        break;
                    }
                    if (clicked_face < 0 || clicked_face >= 6) {
                        rec_cursor.error = 1;
                        break;
                    }
                    if (click_offset_x < 0 || click_offset_x > 1
                            || click_offset_y < 0 || click_offset_y > 1
                            || click_offset_z < 0 || click_offset_z > 1) {
                        rec_cursor.error = 1;
                        break;
                    }

                    if (entity->flags & ENTITY_TELEPORTING) {
                        // ignore
                        break;
                    }

                    // @TODO(traks) special handling depending on gamemode

                    // @TODO(traks) ensure clicked block is in one of the sent
                    // chunks inside the player's chunk cache

                    int sel_slot = entity->player.selected_slot;
                    item_stack * sel = entity->player.slots + sel_slot;
                    item_stack * off = entity->player.slots + PLAYER_OFF_HAND_SLOT;
                    item_stack * used = hand == 0 ? sel : off;

                    if (!(brain->flags & PLAYER_BRAIN_SHIFTING)
                            || (sel->type == 0 && off->type == 0)) {
                        // @TODO(traks) use clicked block (button, door, etc.)
                    }

                    // @TODO(traks) check for cooldowns (ender pearls,
                    // chorus fruits)

                    // @TODO(traks) use item type to determine which place
                    // handler to fire
                    item_type * used_type = item_types + used->type;

                    net_block_pos target_pos = clicked_pos;
                    switch (clicked_face) {
                    case DIRECTION_NEG_Y: target_pos.y--; break;
                    case DIRECTION_POS_Y: target_pos.y++; break;
                    case DIRECTION_NEG_Z: target_pos.z--; break;
                    case DIRECTION_POS_Z: target_pos.z++; break;
                    case DIRECTION_NEG_X: target_pos.x--; break;
                    case DIRECTION_POS_X: target_pos.x++; break;
                    }

                    // @TODO(traks) check if target pos is in chunk visible to
                    // the player

                    __m128i xz = _mm_set_epi32(0, 0, target_pos.z, target_pos.x);
                    __m128i chunk_xz = _mm_srai_epi32(xz, 4);
                    chunk_pos pos = {
                        .x = _mm_extract_epi32(chunk_xz, 0),
                        .z = _mm_extract_epi32(chunk_xz, 1)
                    };
                    chunk * ch = get_chunk_if_loaded(pos);
                    if (ch == NULL) {
                        break;
                    }

                    // @TODO(traks) ANDing signed integers better work
                    int in_chunk_x = target_pos.x & 0xf;
                    int in_chunk_z = target_pos.z & 0xf;
                    chunk_set_block_state(ch, in_chunk_x, target_pos.y,
                            in_chunk_z, 2);
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
                    disconnect_player_now(brain);
                    break;
                }
            }

            memmove(rec_cursor.buf, rec_cursor.buf + rec_cursor.index,
                    rec_cursor.limit - rec_cursor.index);
            brain->rec_cursor = rec_cursor.limit - rec_cursor.index;
        }
    }

    // remove players from tab list if necessary
    for (int i = 0; i < tab_list_size; i++) {
        tab_list_entry * entry = tab_list + i;
        entity_data * entity = resolve_entity(entry->eid);
        if (entity->type == ENTITY_NULL) {
            // @TODO(traks) make sure this can never happen instead of hoping
            assert(tab_list_removed_count < ARRAY_SIZE(tab_list_removed));
            tab_list_removed[tab_list_removed_count] = *entry;
            tab_list_removed_count++;
            tab_list[i] = tab_list[tab_list_size - 1];
            tab_list_size--;
            i--;
            continue;
        }
        // @TODO(traks) make sure this can never happen instead of hoping
        assert(entity->type == ENTITY_PLAYER);
    }

    // add players to live tab list
    for (int i = 0; i < tab_list_added_count; i++) {
        tab_list_entry * new_entry = tab_list_added + i;
        assert(tab_list_size < ARRAY_SIZE(tab_list));
        tab_list[tab_list_size] = *new_entry;
        tab_list_size++;
    }

    for (int i = 0; i < ARRAY_SIZE(player_brains); i++) {
        player_brain * brain = player_brains + i;
        if ((brain->flags & PLAYER_BRAIN_IN_USE) == 0) {
            continue;
        }

        entity_data * player = resolve_entity(brain->eid);

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

        if ((player->flags & ENTITY_TELEPORTING)
                && !(brain->flags & PLAYER_BRAIN_SENT_TELEPORT)) {
            // send player position packet
            int out_size = net_varint_size(54) + 8 + 8 + 8 + 4 + 4 + 1
                    + net_varint_size(brain->current_teleport_id);
            net_write_varint(&send_cursor, out_size);
            net_write_varint(&send_cursor, 54);
            net_write_double(&send_cursor, player->x);
            net_write_double(&send_cursor, player->y);
            net_write_double(&send_cursor, player->z);
            net_write_float(&send_cursor, player->rot_y);
            net_write_float(&send_cursor, player->rot_x);
            net_write_ubyte(&send_cursor, 0); // relative arguments
            net_write_varint(&send_cursor, brain->current_teleport_id);

            brain->flags |= PLAYER_BRAIN_SENT_TELEPORT;
        }

        mc_short chunk_cache_min_x = brain->chunk_cache_centre_x - brain->chunk_cache_radius;
        mc_short chunk_cache_min_z = brain->chunk_cache_centre_z - brain->chunk_cache_radius;
        mc_short chunk_cache_max_x = brain->chunk_cache_centre_x + brain->chunk_cache_radius;
        mc_short chunk_cache_max_z = brain->chunk_cache_centre_z + brain->chunk_cache_radius;

        __m128d xz = _mm_set_pd(player->z, player->x);
        __m128d floored_xz = _mm_floor_pd(xz);
        __m128i floored_int_xz = _mm_cvtpd_epi32(floored_xz);
        __m128i new_centre = _mm_srai_epi32(floored_int_xz, 4);
        mc_short new_chunk_cache_centre_x = _mm_extract_epi32(new_centre, 0);
        mc_short new_chunk_cache_centre_z = _mm_extract_epi32(new_centre, 1);
        assert(brain->new_chunk_cache_radius <= MAX_CHUNK_CACHE_RADIUS);
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
                chunk_pos pos = {.x = x, .z = z};
                int index = chunk_cache_index(pos);

                if (x >= new_chunk_cache_min_x && x <= new_chunk_cache_max_x
                        && z >= new_chunk_cache_min_z && z <= new_chunk_cache_max_z) {
                    // old chunk still in new region
                    // send block changes if chunk is visible to the client
                    if (!brain->chunk_cache[index].sent) {
                        continue;
                    }

                    chunk * ch = get_chunk_if_loaded(pos);
                    assert(ch != NULL);
                    if (ch->changed_block_count == 0) {
                        continue;
                    }

                    // send chunk blocks update packet
                    int out_size = net_varint_size(16) + 2 * 4
                            + net_varint_size(ch->changed_block_count);

                    // @TODO(traks) less duplication between this and the part
                    // below
                    for (int i = 0; i < ch->changed_block_count; i++) {
                        mc_ushort pos = ch->changed_blocks[i];
                        mc_ushort block_state = chunk_get_block_state(ch,
                                pos >> 12, pos & 0xff, (pos >> 8) & 0xf);
                        out_size += 2 + net_varint_size(block_state);
                    }

                    net_write_varint(&send_cursor, out_size);
                    net_write_varint(&send_cursor, 16);
                    net_write_int(&send_cursor, x);
                    net_write_int(&send_cursor, z);
                    net_write_varint(&send_cursor, ch->changed_block_count);

                    for (int i = 0; i < ch->changed_block_count; i++) {
                        mc_ushort pos = ch->changed_blocks[i];
                        net_write_ushort(&send_cursor, pos);
                        mc_ushort block_state = chunk_get_block_state(ch,
                                pos >> 12, pos & 0xff, (pos >> 8) & 0xf);
                        net_write_varint(&send_cursor, block_state);
                    }
                    continue;
                }

                // old chunk is not in the new region
                chunk * ch = get_chunk_if_available(pos);
                assert(ch != NULL);
                ch->available_interest--;

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

        // track new chunks
        for (mc_short x = new_chunk_cache_min_x; x <= new_chunk_cache_max_x; x++) {
            for (mc_short z = new_chunk_cache_min_z; z <= new_chunk_cache_max_z; z++) {
                if (x >= chunk_cache_min_x && x <= chunk_cache_max_x
                        && z >= chunk_cache_min_z && z <= chunk_cache_max_z) {
                    // chunk already in old region
                    continue;
                }

                // chunk not in old region
                chunk_pos pos = {.x = x, .z = z};
                chunk * ch = get_or_create_chunk(pos);
                ch->available_interest++;
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
        int newly_loaded_chunks = 0;
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
            chunk_pos pos = {.x = x, .z = z};

            if (newly_loaded_chunks < MAX_CHUNK_LOADS_PER_TICK
                    && chunk_load_request_count < ARRAY_SIZE(chunk_load_requests)) {
                chunk * ch = get_chunk_if_available(pos);
                assert(ch != NULL);
                assert(ch->available_interest > 0);
                if (!(ch->flags & CHUNK_LOADED)) {
                    chunk_load_requests[chunk_load_request_count] = pos;
                    chunk_load_request_count++;
                    newly_loaded_chunks++;
                }
            }

            if (newly_sent_chunks < MAX_CHUNK_SENDS_PER_TICK && !entry->sent) {
                chunk * ch = get_chunk_if_loaded(pos);
                if (ch != NULL) {
                    // send level chunk packet
                    send_chunk_fully(&send_cursor, pos, ch);
                    entry->sent = 1;
                    newly_sent_chunks++;
                }
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

        // send updates in player's own inventory

        for (int i = 0; i < PLAYER_SLOTS; i++) {
            if (!(player->player.slots_needing_update & ((mc_ulong) 1 << i))) {
                continue;
            }

            log("Sending slot update for %d", i);
            item_stack * is = player->player.slots + i;

            // send container set slot packet
            int out_size = net_varint_size(23) + 1 + 2 + 1;
            if (is->type != 0) {
                out_size += net_varint_size(is->type) + 1 + 1;
            }

            net_write_varint(&send_cursor, out_size);
            net_write_varint(&send_cursor, 23);
            net_write_ubyte(&send_cursor, 0); // inventory id
            net_write_ushort(&send_cursor, i);

            if (is->type == 0) {
                net_write_ubyte(&send_cursor, 0); // has item
            } else {
                net_write_ubyte(&send_cursor, 1); // has item
                net_write_varint(&send_cursor, is->type);
                net_write_ubyte(&send_cursor, is->size);
                // @TODO(traks) write NBT (currently just a single end tag)
                net_write_ubyte(&send_cursor, 0);
            }
        }

        player->player.slots_needing_update = 0;
        memcpy(player->player.slots_prev_tick, player->player.slots,
                sizeof player->player.slots);

        // tab list updates

        if (!(brain->flags & PLAYER_BRAIN_INITIALISED_TAB_LIST)) {
            brain->flags |= PLAYER_BRAIN_INITIALISED_TAB_LIST;
            if (tab_list_size > 0) {
                // send player info packet
                int out_size = net_varint_size(52) + net_varint_size(0)
                        + net_varint_size(tab_list_size);
                for (int i = 0; i < tab_list_size; i++) {
                    tab_list_entry * entry = tab_list + i;
                    entity_data * entity = resolve_entity(entry->eid);
                    assert(entity->type == ENTITY_PLAYER);
                    out_size += 16
                            + net_varint_size(entity->player.username_size)
                            + entity->player.username_size
                            + net_varint_size(0)
                            + net_varint_size(entity->player.gamemode)
                            + net_varint_size(0) + 1;
                }

                net_write_varint(&send_cursor, out_size);
                net_write_varint(&send_cursor, 52);
                net_write_varint(&send_cursor, 0); // action: add
                net_write_varint(&send_cursor, tab_list_size);

                for (int i = 0; i < tab_list_size; i++) {
                    tab_list_entry * entry = tab_list + i;
                    entity_data * entity = resolve_entity(entry->eid);
                    assert(entity->type == ENTITY_PLAYER);
                    // @TODO(traks) write UUID
                    net_write_ulong(&send_cursor, 0);
                    net_write_ulong(&send_cursor, entry->eid);
                    net_string username = {
                        .ptr = entity->player.username,
                        .size = entity->player.username_size
                    };
                    net_write_string(&send_cursor, username);
                    net_write_varint(&send_cursor, 0); // num properties
                    net_write_varint(&send_cursor, entity->player.gamemode);
                    net_write_varint(&send_cursor, 0); // latency
                    net_write_ubyte(&send_cursor, 0); // has display name
                }
            }
        } else {
            if (tab_list_removed_count > 0) {
                // send player info packet
                int out_size = net_varint_size(52) + net_varint_size(4)
                        + net_varint_size(tab_list_removed_count)
                        + tab_list_removed_count * 16;
                net_write_varint(&send_cursor, out_size);
                net_write_varint(&send_cursor, 52);
                net_write_varint(&send_cursor, 4); // action: remove
                net_write_varint(&send_cursor, tab_list_removed_count);

                for (int i = 0; i < tab_list_removed_count; i++) {
                    tab_list_entry * entry = tab_list_removed + i;
                    // @TODO(traks) write UUID
                    net_write_ulong(&send_cursor, 0);
                    net_write_ulong(&send_cursor, entry->eid);
                }
            }
            if (tab_list_added_count > 0) {
                // send player info packet
                int out_size = net_varint_size(52) + net_varint_size(0)
                        + net_varint_size(tab_list_added_count);
                for (int i = 0; i < tab_list_added_count; i++) {
                    tab_list_entry * entry = tab_list_added + i;
                    entity_data * entity = resolve_entity(entry->eid);
                    assert(entity->type == ENTITY_PLAYER);
                    out_size += 16
                            + net_varint_size(entity->player.username_size)
                            + entity->player.username_size
                            + net_varint_size(0)
                            + net_varint_size(entity->player.gamemode)
                            + net_varint_size(0) + 1;
                }

                net_write_varint(&send_cursor, out_size);
                net_write_varint(&send_cursor, 52);
                net_write_varint(&send_cursor, 0); // action: add
                net_write_varint(&send_cursor, tab_list_added_count);

                for (int i = 0; i < tab_list_added_count; i++) {
                    tab_list_entry * entry = tab_list_added + i;
                    entity_data * entity = resolve_entity(entry->eid);
                    assert(entity->type == ENTITY_PLAYER);
                    // @TODO(traks) write UUID
                    net_write_ulong(&send_cursor, 0);
                    net_write_ulong(&send_cursor, entry->eid);
                    net_string username = {
                        .ptr = entity->player.username,
                        .size = entity->player.username_size
                    };
                    net_write_string(&send_cursor, username);
                    net_write_varint(&send_cursor, 0); // num properties
                    net_write_varint(&send_cursor, entity->player.gamemode);
                    net_write_varint(&send_cursor, 0); // latency
                    net_write_ubyte(&send_cursor, 0); // has display name
                }
            }
        }

        // entity tracking

        entity_id removed_entities[64];
        int removed_entity_count = 0;

        for (int j = 1; j < MAX_ENTITIES; j++) {
            entity_id tracked_eid = brain->tracked_entities[j];
            entity_data * candidate = entities + j;
            entity_id candidate_eid = candidate->eid;

            if (tracked_eid != 0) {
                // entity is currently being tracked
                if ((candidate->flags & ENTITY_IN_USE)
                        && candidate_eid == tracked_eid) {
                    // entity is still there
                    mc_double dx = candidate->x - player->x;
                    mc_double dy = candidate->y - player->y;
                    mc_double dz = candidate->z - player->z;

                    if (dx * dx + dy * dy + dz * dz < 45 * 45) {
                        // send teleport entity packet
                        int out_size = net_varint_size(87)
                                + net_varint_size(tracked_eid) + 3 * 8 + 3 * 1;
                        net_write_varint(&send_cursor, out_size);
                        net_write_varint(&send_cursor, 87);
                        net_write_varint(&send_cursor, tracked_eid);
                        net_write_double(&send_cursor, candidate->x);
                        net_write_double(&send_cursor, candidate->y);
                        net_write_double(&send_cursor, candidate->z);
                        // @TODO(traks) make sure signed cast to mc_ubyte works
                        net_write_ubyte(&send_cursor, (int) (candidate->rot_y * 256.0f / 360.0f));
                        net_write_ubyte(&send_cursor, (int) (candidate->rot_x * 256.0f / 360.0f));
                        net_write_ubyte(&send_cursor, !!(candidate->flags & ENTITY_ON_GROUND));
                        continue;
                    }
                }

                // entity we tracked is gone or too far away

                if (removed_entity_count == ARRAY_SIZE(removed_entities)) {
                    // no more space to untrack, try again next tick
                    continue;
                }

                brain->tracked_entities[j] = 0;
                removed_entities[removed_entity_count] = tracked_eid;
                removed_entity_count++;
            }

            if ((candidate->flags & ENTITY_IN_USE) && candidate_eid != brain->eid) {
                // candidate is valid for being newly tracked
                mc_double dx = candidate->x - player->x;
                mc_double dy = candidate->y - player->y;
                mc_double dz = candidate->z - player->z;

                if (dx * dx + dy * dy + dz * dz > 40 * 40) {
                    continue;
                }

                switch (candidate->type) {
                case ENTITY_PLAYER: {
                    // send add mob packet
                    int out_size = net_varint_size(3)
                            + net_varint_size(candidate_eid)
                            + 16 + net_varint_size(5)
                            + 3 * 8 + 3 * 1 + 3 * 2;
                    net_write_varint(&send_cursor, out_size);
                    net_write_varint(&send_cursor, 3);
                    net_write_varint(&send_cursor, candidate_eid);
                    // @TODO(traks) appropriate UUID
                    net_write_ulong(&send_cursor, 0);
                    net_write_ulong(&send_cursor, 0);
                    // @TODO(traks) network entity type
                    net_write_varint(&send_cursor, 5);
                    net_write_double(&send_cursor, candidate->x);
                    net_write_double(&send_cursor, candidate->y);
                    net_write_double(&send_cursor, candidate->z);
                    // @TODO(traks) make sure signed cast to mc_ubyte works
                    net_write_ubyte(&send_cursor, (int) (candidate->rot_y * 256.0f / 360.0f));
                    net_write_ubyte(&send_cursor, (int) (candidate->rot_x * 256.0f / 360.0f));
                    // @TODO(traks) y head rotation (what is that?)
                    net_write_ubyte(&send_cursor, 0);
                    // @TODO(traks) entity velocity
                    net_write_ushort(&send_cursor, 0);
                    net_write_ushort(&send_cursor, 0);
                    net_write_ushort(&send_cursor, 0);

                    // int out_size = net_varint_size(5)
                    //         + net_varint_size(candidate_eid)
                    //         + 16 + 3 * 8 + 2 * 1;
                    // net_write_varint(&send_cursor, out_size);
                    // net_write_varint(&send_cursor, 5);
                    // net_write_varint(&send_cursor, candidate_eid);
                    // // @TODO(traks) appropriate UUID
                    // net_write_ulong(&send_cursor, 0);
                    // net_write_ulong(&send_cursor, 0);
                    // // @TODO(traks) network entity type
                    // net_write_double(&send_cursor, candidate->x);
                    // net_write_double(&send_cursor, candidate->y);
                    // net_write_double(&send_cursor, candidate->z);
                    // // @TODO(traks) make sure signed cast to mc_ubyte works
                    // net_write_ubyte(&send_cursor, (int) (candidate->rot_y * 256.0f / 360.0f));
                    // net_write_ubyte(&send_cursor, (int) (candidate->rot_x * 256.0f / 360.0f));
                    break;
                default:
                    continue;
                }
                }

                mc_uint entity_index = candidate_eid & ENTITY_INDEX_MASK;
                brain->tracked_entities[entity_index] = candidate_eid;
            }
        }

        if (removed_entity_count > 0) {
            // send remove entities packet
            int out_size = net_varint_size(56)
                    + net_varint_size(removed_entity_count);
            for (int i = 0; i < removed_entity_count; i++) {
                out_size += net_varint_size(removed_entities[i]);
            }

            net_write_varint(&send_cursor, out_size);
            net_write_varint(&send_cursor, 56);
            net_write_varint(&send_cursor, removed_entity_count);
            for (int i = 0; i < removed_entity_count; i++) {
                net_write_varint(&send_cursor, removed_entities[i]);
            }
        }

        // send chat messages

        for (int i = 0; i < global_msg_count; i++) {
            global_msg * msg = global_msgs + i;

            // @TODO(traks) formatted messages and such
            unsigned char buf[1024];
            int buf_index = 0;
            net_string prefix = NET_STRING("{\"text\":\"");
            net_string suffix = NET_STRING("\"}");

            memcpy(buf + buf_index, prefix.ptr, prefix.size);
            buf_index += prefix.size;

            for (int i = 0; i < msg->size; i++) {
                if (msg->text[i] == '"' || msg->text[i] == '\\') {
                    buf[buf_index] = '\\';
                    buf_index++;
                }
                buf[buf_index] = msg->text[i];
                buf_index++;
            }

            memcpy(buf + buf_index, suffix.ptr, suffix.size);
            buf_index += suffix.size;

            // send chat packet
            int out_size = net_varint_size(15) + net_varint_size(buf_index)
                    + buf_index + 1;
            net_write_varint(&send_cursor, out_size);
            net_write_varint(&send_cursor, 15);
            net_write_varint(&send_cursor, buf_index);
            memcpy(send_cursor.buf + send_cursor.index, buf, buf_index);
            send_cursor.index += buf_index;
            net_write_ubyte(&send_cursor, 0); // chat box position
        }

        // try to write everything to the socket buffer

        brain->send_cursor = send_cursor.index;

        int sock = brain->sock;
        ssize_t send_size = send(sock, brain->send_buf,
                brain->send_cursor, 0);

        if (send_size == -1) {
            // EAGAIN means no data sent
            if (errno != EAGAIN) {
                log_errno("Couldn't send protocol data: %s");
                disconnect_player_now(brain);
            }
        } else {
            memmove(brain->send_buf, brain->send_buf + send_size,
                    brain->send_cursor - send_size);
            brain->send_cursor -= send_size;
        }
    }

    // clear global messages
    global_msg_count = 0;

    // clear tab list updates
    tab_list_added_count = 0;
    tab_list_removed_count = 0;

    // load chunks from requests

    for (int i = 0; i < chunk_load_request_count; i++) {
        chunk_pos pos = chunk_load_requests[i];
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
        try_read_chunk_from_storage(pos, ch);

        if (!(ch->flags & CHUNK_LOADED)) {
            // @TODO(traks) fall back to stone plateau at y = 0 for now
            // clean up some of the mess the chunk loader might've left behind
            // @TODO(traks) perhaps this should be in a separate struct so we
            // can easily clear it
            for (int sectioni = 0; sectioni < 16; sectioni++) {
                if (ch->sections[sectioni] != NULL) {
                    free(ch->sections[sectioni]);
                    ch->sections[sectioni] = NULL;
                }
                ch->non_air_count[sectioni] = 0;
            }

            // @TODO(traks) get rid of allocation
            ch->sections[0] = calloc(sizeof (mc_ushort), 4096);
            if (ch->sections[0] == NULL) {
                log("Failed to allocate chunk section during generation");
                exit(1);
            }

            for (int x = 0; x < 16; x++) {
                for (int z = 0; z < 16; z++) {
                    int index = (z << 4) | x;
                    ch->sections[0][index] = 2;
                    ch->motion_blocking_height_map[index] = 1;
                    ch->non_air_count[0]++;
                }
            }

            ch->flags |= CHUNK_LOADED;
        }
    }

    chunk_load_request_count = 0;

    // update chunks

    for (int bucketi = 0; bucketi < ARRAY_SIZE(chunk_map); bucketi++) {
        chunk_bucket * bucket = chunk_map + bucketi;

        for (int chunki = 0; chunki < bucket->size; chunki++) {
            chunk * ch = bucket->chunks + chunki;
            ch->changed_block_count = 0;

            if (ch->available_interest == 0) {
                for (int sectioni = 0; sectioni < 16; sectioni++) {
                    if (ch->sections[sectioni] != NULL) {
                        free(ch->sections[sectioni]);
                    }
                }

                int last = bucket->size - 1;
                assert(last >= 0);
                bucket->chunks[chunki] = bucket->chunks[last];
                bucket->positions[chunki] = bucket->positions[last];
                bucket->size--;
                chunki--;
            }
        }
    }

    current_tick++;
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
load_item_types(void) {
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
            it = item_types + item_type_count;
            item_type_count++;
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

    // reserve null entity
    entities[0].flags |= ENTITY_IN_USE;

    // allocate memory for arenas
    short_lived_scratch_size = 4194304;
    short_lived_scratch = calloc(short_lived_scratch_size, 1);

    if (short_lived_scratch == NULL) {
        log_errno("Failed to allocate short lived scratch arena: %s");
        exit(1);
    }

    load_item_types();
    load_block_types();

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
