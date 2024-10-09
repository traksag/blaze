#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdalign.h>
#include <math.h>
#include <sys/mman.h>
#include "shared.h"
#include "buffer.h"
#include "chunk.h"
#include "network.h"

#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

static volatile sig_atomic_t interruptCount;

server * serv;

#if defined(__APPLE__) && defined(__MACH__)

static mach_timebase_info_data_t timebaseInfo;
static i64 programStartTime;

static void InitNanoTime() {
    mach_timebase_info(&timebaseInfo);
    programStartTime = mach_absolute_time();
}

i64 NanoTime() {
    i64 diff = mach_absolute_time() - programStartTime;
    return diff * timebaseInfo.numer / timebaseInfo.denom;
}

#else

static struct timespec programStartTime;

static void InitNanoTime() {
    clock_gettime(CLOCK_MONOTONIC, &programStartTime);
}

i64 NanoTime() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    i64 diffSeconds = ((i64) now.tv_sec - programStartTime.tv_sec) * (i64) 1000000000;
    i64 diffNanos = (i64) now.tv_nsec - programStartTime.tv_nsec;
    return diffSeconds + diffNanos;
}

#endif

void LogInfo(void * format, ...) {
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
LogErrno(void * format) {
    char error_msg[64] = {0};
    strerror_r(errno, error_msg, sizeof error_msg);
    LogInfo(format, error_msg);
}

static void OnSigInt(int sig) {
    interruptCount++;
}

int
net_string_equal(String a, String b) {
    return a.size == b.size && memcmp(a.data, b.data, a.size) == 0;
}

BlockPos
get_relative_block_pos(BlockPos pos, int face) {
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
        return 0;
    }
}

int
get_direction_axis(int direction) {
    switch (direction) {
    case DIRECTION_NEG_Y: return AXIS_Y;
    case DIRECTION_POS_Y: return AXIS_Y;
    case DIRECTION_NEG_Z: return AXIS_Z;
    case DIRECTION_POS_Z: return AXIS_Z;
    case DIRECTION_NEG_X: return AXIS_X;
    case DIRECTION_POS_X: return AXIS_X;
    default:
        assert(0);
        return 0;
    }
}

static u16
hash_resource_loc(String resource_loc, resource_loc_table * table) {
    u16 res = 0;
    unsigned char * string = resource_loc.data;
    for (int i = 0; i < resource_loc.size; i++) {
        res = res * 31 + string[i];
    }
    return res & table->size_mask;
}

void
register_resource_loc(String resource_loc, i16 id,
        resource_loc_table * table) {
    u16 hash = hash_resource_loc(resource_loc, table);

    for (u16 i = hash; ; i = (i + 1) & table->size_mask) {
        assert(((i + 1) & table->size_mask) != hash);
        resource_loc_entry * entry = table->entries + i;

        if (entry->size == 0) {
            assert(resource_loc.size <= RESOURCE_LOC_MAX_SIZE);
            entry->size = resource_loc.size;
            i32 string_buf_index = table->last_string_buf_index;
            entry->buf_index = string_buf_index;
            entry->id = id;

            assert(string_buf_index + resource_loc.size <= table->string_buf_size);
            memcpy(table->string_buf + string_buf_index, resource_loc.data,
                    resource_loc.size);
            table->last_string_buf_index += resource_loc.size;

            assert(id < table->max_ids);
            table->by_id[id] = i;
            break;
        }
    }
}

static void
alloc_resource_loc_table(resource_loc_table * table, i32 size,
        i32 string_buf_size, u16 max_ids) {
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

i16
resolve_resource_loc_id(String resource_loc, resource_loc_table * table) {
    u16 hash = hash_resource_loc(resource_loc, table);
    u16 i = hash;
    for (;;) {
        resource_loc_entry * entry = table->entries + i;
        String name = {
            .data = table->string_buf + entry->buf_index,
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

String
get_resource_loc(u16 id, resource_loc_table * table) {
    assert(id < table->max_ids);
    resource_loc_entry * entry = table->entries + table->by_id[id];
    String res = {
        .data = table->string_buf + entry->buf_index,
        .size = entry->size
    };
    return res;
}

int
find_property_value_index(block_property_spec * prop_spec, String val) {
    unsigned char * tape = prop_spec->tape;
    tape += 1 + tape[0];
    for (int i = 0; ; i++) {
        // Note that after the values a 0 follows
        if (tape[0] == 0) {
            return -1;
        }
        String real_val = {
            .data = tape + 1,
            .size = tape[0]
        };
        if (net_string_equal(real_val, val)) {
            return i;
        }
        tape += 1 + tape[0];
    }
}

entity_base *
resolve_entity(entity_id eid) {
    u32 index = eid & ENTITY_INDEX_MASK;
    entity_base * entity = serv->entities + index;
    if (entity->eid != eid || !(entity->flags & ENTITY_IN_USE)) {
        // return the null entity
        return serv->entities;
    }
    return entity;
}

entity_base *
try_reserve_entity(unsigned type) {
    for (uint32_t i = 0; i < MAX_ENTITIES; i++) {
        entity_base * entity = serv->entities + i;
        if (!(entity->flags & ENTITY_IN_USE)) {
            u16 generation = serv->next_entity_generations[i];
            entity_id eid = ((u32) generation << 20) | i;

            *entity = (entity_base) {0};
            // @NOTE(traks) default initialisation is only guaranteed to
            // initialise the first union member, so we have to manually
            // default initialise the union member based on the entity type
            switch (type) {
            case ENTITY_PLAYER: entity->player = (entity_player) {0}; break;
            case ENTITY_ITEM: entity->item = (entity_item) {0}; break;
            }

            entity->eid = eid;
            // TODO(traks): Proper UUID creation. For players we overwrite this
            entity->uuid = (UUID) {.low = eid, .high = 0};
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
evict_entity(entity_id eid) {
    entity_base * entity = resolve_entity(eid);
    if (entity->type != ENTITY_NULL) {
        entity->flags &= ~ENTITY_IN_USE;
        serv->entity_count--;
    }
}

static void
move_entity(entity_base * entity) {
    // @TODO(traks) Currently our collision system seems to be very different
    // from Minecraft's collision system, which causes client-server desyncs
    // when dropping item entities, etc. There are currently also uses with
    // items following through the ground and then popping back up on the
    // client's side.

    // @TODO(traks) when you drop an item from high in the air, it often
    // teleports slightly up after falling for a little while. From what I
    // recall, vanilla was sometimes doing two item entity movements in one
    // tick. What's really going on?

    i32 worldId = entity->worldId;
    double x = entity->x;
    double y = entity->y;
    double z = entity->z;

    double vx = entity->vx;
    double vy = entity->vy;
    double vz = entity->vz;

    double remaining_dt = 1;

    int on_ground = 0;

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

        // extend collision testing by 1 block, because the collision boxes of
        // some blocks extend past a 1x1x1 volume. Examples are: fences and
        // shulker boxes.
        min_x -= 1;
        min_y -= 1;
        min_z -= 1;
        max_x += 1;
        max_y += 1;
        max_z += 1;

        i32 iter_min_x = floor(min_x);
        i32 iter_max_x = floor(max_x);
        i32 iter_min_y = floor(min_y);
        i32 iter_max_y = floor(max_y);
        i32 iter_min_z = floor(min_z);
        i32 iter_max_z = floor(max_z);

        u16 hit_state = 0;
        int hit_face = 0;

        double dt = 1;

        for (int block_x = iter_min_x; block_x <= iter_max_x; block_x++) {
            for (int block_y = iter_min_y; block_y <= iter_max_y; block_y++) {
                for (int block_z = iter_min_z; block_z <= iter_max_z; block_z++) {
                    WorldBlockPos block_pos = {.worldId = worldId, .x = block_x, .y = block_y, .z = block_z};
                    u16 cur_state = WorldGetBlockState(block_pos);
                    BlockModel model = BlockDetermineCollisionModel(cur_state, block_pos);

                    for (int boxi = 0; boxi < model.size; boxi++) {
                        BoundingBox * box = model.boxes + boxi;

                        double test_min_x = block_x + box->minX - width / 2;
                        double test_max_x = block_x + box->maxX + width / 2;
                        double test_min_y = block_y + box->minY - height;
                        double test_max_y = block_y + box->maxY;
                        double test_min_z = block_z + box->minZ - width / 2;
                        double test_max_z = block_z + box->maxZ + width / 2;

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

                        for (int i = 0; i < (i32) ARRAY_SIZE(tests); i++) {
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
        }

        x += dt * dx;
        y += dt * dy;
        z += dt * dz;

        i32 hit_type = serv->block_type_by_state[hit_state];

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
                    if (entity->flags & ENTITY_SHIFTING) {
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
                    on_ground = 1;
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

    entity->flags &= ~ENTITY_ON_GROUND;
    if (on_ground) {
        entity->flags |= ENTITY_ON_GROUND;
    }
}

static void
tick_entity(entity_base * entity, MemoryArena * tick_arena) {
    // @TODO(traks) currently it's possible that an entity is spawned and ticked
    // the same tick. Is that an issue or not? Maybe that causes undesirable
    // off-by-one tick behaviour.

    switch (entity->type) {
    case ENTITY_PLAYER:
        tick_player(entity, tick_arena);
        break;
    case ENTITY_ITEM: {
        if (entity->item.contents.type == ITEM_AIR) {
            evict_entity(entity->eid);
            return;
        }

        if (entity->item.pickup_timeout > 0
                && entity->item.pickup_timeout != 32767) {
            entity->item.pickup_timeout--;
        }

        // gravity acceleration
        entity->vy -= 0.04;

        move_entity(entity);

        float drag = 0.98;

        if (entity->flags & ENTITY_ON_GROUND) {
            // Bit weird, but this is how MC works. Allows items to slide on
            // slabs if ice is below it.
            WorldBlockPos ground = {
                .worldId = entity->worldId,
                .x = floor(entity->x),
                .y = floor(entity->y - 0.99),
                .z = floor(entity->z),
            };

            u16 ground_state = WorldGetBlockState(ground);
            i32 ground_type = serv->block_type_by_state[ground_state];

            // Minecraft block friction
            float friction;
            switch (ground_type) {
            case BLOCK_ICE: friction = 0.98f; break;
            case BLOCK_SLIME_BLOCK: friction = 0.8f; break;
            case BLOCK_PACKED_ICE: friction = 0.98f; break;
            case BLOCK_FROSTED_ICE: friction = 0.98f; break;
            case BLOCK_BLUE_ICE: friction = 0.989f; break;
            default: friction = 0.6f; break;
            }

            drag *= friction;
        }

        entity->vx *= drag;
        entity->vy *= 0.98;
        entity->vz *= drag;

        if (entity->flags & ENTITY_ON_GROUND) {
            entity->vy *= -0.5;
        }
        break;
    }
    }
}

static void
server_tick(void) {
    BeginTimings(ServerTick);

    TickInitialConnections();

    // run scheduled block updates
    BeginTimings(ScheduledUpdates);

    MemoryArena scheduled_update_arena = {
        .data = serv->short_lived_scratch,
        .size = serv->short_lived_scratch_size
    };
    propagate_delayed_block_updates(&scheduled_update_arena);

    EndTimings(ScheduledUpdates);

    // update entities
    BeginTimings(TickEntities);

    for (int i = 0; i < (i32) ARRAY_SIZE(serv->entities); i++) {
        entity_base * entity = serv->entities + i;
        if ((entity->flags & ENTITY_IN_USE) == 0) {
            continue;
        }

        MemoryArena tick_arena = {
            .data = serv->short_lived_scratch,
            .size = serv->short_lived_scratch_size
        };

        tick_entity(entity, &tick_arena);
    }

    EndTimings(TickEntities);

    BeginTimings(UpdateTabList);

    // remove players from tab list if necessary
    for (int i = 0; i < serv->tab_list_size; i++) {
        entity_id eid = serv->tab_list[i];
        entity_base * entity = resolve_entity(eid);
        if (entity->type == ENTITY_NULL) {
            // @TODO(traks) make sure this can never happen instead of hoping
            assert(serv->tab_list_removed_count < (i32) ARRAY_SIZE(serv->tab_list_removed));
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
        assert(serv->tab_list_size < (i32) ARRAY_SIZE(serv->tab_list));
        serv->tab_list[serv->tab_list_size] = eid;
        serv->tab_list_size++;
    }

    EndTimings(UpdateTabList);

    BeginTimings(SendPlayers);

    for (int i = 0; i < (i32) ARRAY_SIZE(serv->entities); i++) {
        entity_base * entity = serv->entities + i;
        if (entity->type != ENTITY_PLAYER) {
            continue;
        }
        if ((entity->flags & ENTITY_IN_USE) == 0) {
            continue;
        }

        MemoryArena tick_arena = {
            .data = serv->short_lived_scratch,
            .size = serv->short_lived_scratch_size
        };
        send_packets_to_player(entity, &tick_arena);
    }

    EndTimings(SendPlayers);

    // clear global messages
    serv->global_msg_count = 0;

    // clear tab list updates
    serv->tab_list_added_count = 0;
    serv->tab_list_removed_count = 0;

    BeginTimings(ClearEntityChanges);

    for (int i = 0; i < (i32) ARRAY_SIZE(serv->entities); i++) {
        entity_base * entity = serv->entities + i;
        if ((entity->flags & ENTITY_IN_USE) == 0) {
            continue;
        }

        entity->changed_data = 0;
    }

    EndTimings(ClearEntityChanges);

    BeginTimings(TickChunkSystem);
    TickChunkSystem();
    EndTimings(TickChunkSystem);

    // update chunks
    BeginTimings(TickChunkLoader);
    TickChunkLoader();
    EndTimings(TickChunkLoader);

    serv->current_tick++;
    EndTimings(ServerTick);
}

static int
parse_database_line(Cursor * cursor, String * args) {
    int arg_count = 0;
    int cur_size = 0;
    int i;
    unsigned char * line = cursor->data;
    for (i = cursor->index; ; i++) {
        if (i == cursor->size || line[i] == ' ' || line[i] == '\n') {
            if (cur_size > 0) {
                args[arg_count].data = cursor->data + (i - cur_size);
                args[arg_count].size = cur_size;
                arg_count++;
                cur_size = 0;
            }
            if (i == cursor->size) {
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

static Cursor
read_file(MemoryArena * arena, char * file_name) {
    int fd = open(file_name, O_RDONLY);
    if (fd == -1) {
        LogErrno("Failed to open file: %s");
        LogInfo("File in question was '%s'", file_name);
        exit(1);
    }

    struct stat stat;
    if (fstat(fd, &stat)) {
        LogErrno("Failed to fstat file: %s");
        LogInfo("File in question was '%s'", file_name);
        exit(1);
    }

    int file_size = stat.st_size;
    unsigned char * buf = MallocInArena(arena, file_size);
    int read_index = 0;

    while (read_index < file_size) {
        int bytes_read = read(fd, buf + read_index, file_size - read_index);
        if (bytes_read == -1) {
            LogErrno("Failed to read: %s");
            LogInfo("File in question was '%s'", file_name);
            exit(1);
        }
        if (bytes_read == 0) {
            LogInfo("File size changed while reading '%s'", file_name);
            exit(1);
        }

        read_index += bytes_read;
    }

    close(fd);

    Cursor res = {
        .data = buf,
        .size = file_size
    };
    return res;
}

static void
register_entity_type(i32 entity_type, char * resource_loc) {
    String key = STR(resource_loc);
    resource_loc_table * table = &serv->entity_resource_table;
    register_resource_loc(key, entity_type, table);
    assert(net_string_equal(key, get_resource_loc(entity_type, table)));
    assert(entity_type == resolve_resource_loc_id(key, table));
}

static void
init_entity_data(void) {
    register_entity_type(ENTITY_ALLAY, "minecraft:allay");
    register_entity_type(ENTITY_AREA_EFFECT_CLOUD, "minecraft:area_effect_cloud");
    register_entity_type(ENTITY_ARMADILLO, "minecraft:armadillo");
    register_entity_type(ENTITY_ARMOR_STAND, "minecraft:armor_stand");
    register_entity_type(ENTITY_ARROW, "minecraft:arrow");
    register_entity_type(ENTITY_AXOLOTL, "minecraft:axolotl");
    register_entity_type(ENTITY_BAT, "minecraft:bat");
    register_entity_type(ENTITY_BEE, "minecraft:bee");
    register_entity_type(ENTITY_BLAZE, "minecraft:blaze");
    register_entity_type(ENTITY_BLOCK_DISPLAY, "minecraft:block_display");
    register_entity_type(ENTITY_BOAT, "minecraft:boat");
    register_entity_type(ENTITY_BOGGED, "minecraft:bogged");
    register_entity_type(ENTITY_BREEZE, "minecraft:breeze");
    register_entity_type(ENTITY_BREEZE_WIND_CHARGE, "minecraft:breeze_wind_charge");
    register_entity_type(ENTITY_CAMEL, "minecraft:camel");
    register_entity_type(ENTITY_CAT, "minecraft:cat");
    register_entity_type(ENTITY_CAVE_SPIDER, "minecraft:cave_spider");
    register_entity_type(ENTITY_CHEST_BOAT, "minecraft:chest_boat");
    register_entity_type(ENTITY_CHEST_MINECART, "minecraft:chest_minecart");
    register_entity_type(ENTITY_CHICKEN, "minecraft:chicken");
    register_entity_type(ENTITY_COD, "minecraft:cod");
    register_entity_type(ENTITY_COMMAND_BLOCK_MINECART, "minecraft:command_block_minecart");
    register_entity_type(ENTITY_COW, "minecraft:cow");
    register_entity_type(ENTITY_CREEPER, "minecraft:creeper");
    register_entity_type(ENTITY_DOLPHIN, "minecraft:dolphin");
    register_entity_type(ENTITY_DONKEY, "minecraft:donkey");
    register_entity_type(ENTITY_DRAGON_FIREBALL, "minecraft:dragon_fireball");
    register_entity_type(ENTITY_DROWNED, "minecraft:drowned");
    register_entity_type(ENTITY_EGG, "minecraft:egg");
    register_entity_type(ENTITY_ELDER_GUARDIAN, "minecraft:elder_guardian");
    register_entity_type(ENTITY_END_CRYSTAL, "minecraft:end_crystal");
    register_entity_type(ENTITY_ENDER_DRAGON, "minecraft:ender_dragon");
    register_entity_type(ENTITY_ENDER_PEARL, "minecraft:ender_pearl");
    register_entity_type(ENTITY_ENDERMAN, "minecraft:enderman");
    register_entity_type(ENTITY_ENDERMITE, "minecraft:endermite");
    register_entity_type(ENTITY_EVOKER, "minecraft:evoker");
    register_entity_type(ENTITY_EVOKER_FANGS, "minecraft:evoker_fangs");
    register_entity_type(ENTITY_EXPERIENCE_BOTTLE, "minecraft:experience_bottle");
    register_entity_type(ENTITY_EXPERIENCE_ORB, "minecraft:experience_orb");
    register_entity_type(ENTITY_EYE_OF_ENDER, "minecraft:eye_of_ender");
    register_entity_type(ENTITY_FALLING_BLOCK, "minecraft:falling_block");
    register_entity_type(ENTITY_FIREWORK_ROCKET, "minecraft:firework_rocket");
    register_entity_type(ENTITY_FOX, "minecraft:fox");
    register_entity_type(ENTITY_FROG, "minecraft:frog");
    register_entity_type(ENTITY_FURNACE_MINECART, "minecraft:furnace_minecart");
    register_entity_type(ENTITY_GHAST, "minecraft:ghast");
    register_entity_type(ENTITY_GIANT, "minecraft:giant");
    register_entity_type(ENTITY_GLOW_ITEM_FRAME, "minecraft:glow_item_frame");
    register_entity_type(ENTITY_GLOW_SQUID, "minecraft:glow_squid");
    register_entity_type(ENTITY_GOAT, "minecraft:goat");
    register_entity_type(ENTITY_GUARDIAN, "minecraft:guardian");
    register_entity_type(ENTITY_HOGLIN, "minecraft:hoglin");
    register_entity_type(ENTITY_HOPPER_MINECART, "minecraft:hopper_minecart");
    register_entity_type(ENTITY_HORSE, "minecraft:horse");
    register_entity_type(ENTITY_HUSK, "minecraft:husk");
    register_entity_type(ENTITY_ILLUSIONER, "minecraft:illusioner");
    register_entity_type(ENTITY_INTERACTION, "minecraft:interaction");
    register_entity_type(ENTITY_IRON_GOLEM, "minecraft:iron_golem");
    register_entity_type(ENTITY_ITEM, "minecraft:item");
    register_entity_type(ENTITY_ITEM_DISPLAY, "minecraft:item_display");
    register_entity_type(ENTITY_ITEM_FRAME, "minecraft:item_frame");
    register_entity_type(ENTITY_OMINOUS_ITEM_SPAWNER, "minecraft:ominous_item_spawner");
    register_entity_type(ENTITY_FIREBALL, "minecraft:fireball");
    register_entity_type(ENTITY_LEASH_KNOT, "minecraft:leash_knot");
    register_entity_type(ENTITY_LIGHTNING_BOLT, "minecraft:lightning_bolt");
    register_entity_type(ENTITY_LLAMA, "minecraft:llama");
    register_entity_type(ENTITY_LLAMA_SPIT, "minecraft:llama_spit");
    register_entity_type(ENTITY_MAGMA_CUBE, "minecraft:magma_cube");
    register_entity_type(ENTITY_MARKER, "minecraft:marker");
    register_entity_type(ENTITY_MINECART, "minecraft:minecart");
    register_entity_type(ENTITY_MOOSHROOM, "minecraft:mooshroom");
    register_entity_type(ENTITY_MULE, "minecraft:mule");
    register_entity_type(ENTITY_OCELOT, "minecraft:ocelot");
    register_entity_type(ENTITY_PAINTING, "minecraft:painting");
    register_entity_type(ENTITY_PANDA, "minecraft:panda");
    register_entity_type(ENTITY_PARROT, "minecraft:parrot");
    register_entity_type(ENTITY_PHANTOM, "minecraft:phantom");
    register_entity_type(ENTITY_PIG, "minecraft:pig");
    register_entity_type(ENTITY_PIGLIN, "minecraft:piglin");
    register_entity_type(ENTITY_PIGLIN_BRUTE, "minecraft:piglin_brute");
    register_entity_type(ENTITY_PILLAGER, "minecraft:pillager");
    register_entity_type(ENTITY_POLAR_BEAR, "minecraft:polar_bear");
    register_entity_type(ENTITY_POTION, "minecraft:potion");
    register_entity_type(ENTITY_PUFFERFISH, "minecraft:pufferfish");
    register_entity_type(ENTITY_RABBIT, "minecraft:rabbit");
    register_entity_type(ENTITY_RAVAGER, "minecraft:ravager");
    register_entity_type(ENTITY_SALMON, "minecraft:salmon");
    register_entity_type(ENTITY_SHEEP, "minecraft:sheep");
    register_entity_type(ENTITY_SHULKER, "minecraft:shulker");
    register_entity_type(ENTITY_SHULKER_BULLET, "minecraft:shulker_bullet");
    register_entity_type(ENTITY_SILVERFISH, "minecraft:silverfish");
    register_entity_type(ENTITY_SKELETON, "minecraft:skeleton");
    register_entity_type(ENTITY_SKELETON_HORSE, "minecraft:skeleton_horse");
    register_entity_type(ENTITY_SLIME, "minecraft:slime");
    register_entity_type(ENTITY_SMALL_FIREBALL, "minecraft:small_fireball");
    register_entity_type(ENTITY_SNIFFER, "minecraft:sniffer");
    register_entity_type(ENTITY_SNOW_GOLEM, "minecraft:snow_golem");
    register_entity_type(ENTITY_SNOWBALL, "minecraft:snowball");
    register_entity_type(ENTITY_SPAWNER_MINECART, "minecraft:spawner_minecart");
    register_entity_type(ENTITY_SPECTRAL_ARROW, "minecraft:spectral_arrow");
    register_entity_type(ENTITY_SPIDER, "minecraft:spider");
    register_entity_type(ENTITY_SQUID, "minecraft:squid");
    register_entity_type(ENTITY_STRAY, "minecraft:stray");
    register_entity_type(ENTITY_STRIDER, "minecraft:strider");
    register_entity_type(ENTITY_TADPOLE, "minecraft:tadpole");
    register_entity_type(ENTITY_TEXT_DISPLAY, "minecraft:text_display");
    register_entity_type(ENTITY_TNT, "minecraft:tnt");
    register_entity_type(ENTITY_TNT_MINECART, "minecraft:tnt_minecart");
    register_entity_type(ENTITY_TRADER_LLAMA, "minecraft:trader_llama");
    register_entity_type(ENTITY_TRIDENT, "minecraft:trident");
    register_entity_type(ENTITY_TROPICAL_FISH, "minecraft:tropical_fish");
    register_entity_type(ENTITY_TURTLE, "minecraft:turtle");
    register_entity_type(ENTITY_VEX, "minecraft:vex");
    register_entity_type(ENTITY_VILLAGER, "minecraft:villager");
    register_entity_type(ENTITY_VINDICATOR, "minecraft:vindicator");
    register_entity_type(ENTITY_WANDERING_TRADER, "minecraft:wandering_trader");
    register_entity_type(ENTITY_WARDEN, "minecraft:warden");
    register_entity_type(ENTITY_WIND_CHARGE, "minecraft:wind_charge");
    register_entity_type(ENTITY_WITCH, "minecraft:witch");
    register_entity_type(ENTITY_WITHER, "minecraft:wither");
    register_entity_type(ENTITY_WITHER_SKELETON, "minecraft:wither_skeleton");
    register_entity_type(ENTITY_WITHER_SKULL, "minecraft:wither_skull");
    register_entity_type(ENTITY_WOLF, "minecraft:wolf");
    register_entity_type(ENTITY_ZOGLIN, "minecraft:zoglin");
    register_entity_type(ENTITY_ZOMBIE, "minecraft:zombie");
    register_entity_type(ENTITY_ZOMBIE_HORSE, "minecraft:zombie_horse");
    register_entity_type(ENTITY_ZOMBIE_VILLAGER, "minecraft:zombie_villager");
    register_entity_type(ENTITY_ZOMBIFIED_PIGLIN, "minecraft:zombified_piglin");
    register_entity_type(ENTITY_PLAYER, "minecraft:player");
    register_entity_type(ENTITY_FISHING_BOBBER, "minecraft:fishing_bobber");
    register_entity_type(ENTITY_NULL, "minecraft:null");
}

static void
register_fluid_type(i32 fluid_type, char * resource_loc) {
    String key = STR(resource_loc);
    resource_loc_table * table = &serv->fluid_resource_table;
    register_resource_loc(key, fluid_type, table);
    assert(net_string_equal(key, get_resource_loc(fluid_type, table)));
    assert(fluid_type == resolve_resource_loc_id(key, table));
}

static void
init_fluid_data(void) {
    register_fluid_type(0, "minecraft:empty");
    register_fluid_type(1, "minecraft:flowing_water");
    register_fluid_type(2, "minecraft:water");
    register_fluid_type(3, "minecraft:flowing_lava");
    register_fluid_type(4, "minecraft:lava");
}

static void
register_game_event_type(i32 game_event_type, char * resource_loc) {
    String key = STR(resource_loc);
    resource_loc_table * table = &serv->game_event_resource_table;
    register_resource_loc(key, game_event_type, table);
    assert(net_string_equal(key, get_resource_loc(game_event_type, table)));
    assert(game_event_type == resolve_resource_loc_id(key, table));
}

static void
init_game_event_data(void) {
    register_game_event_type(GAME_EVENT_BLOCK_ACTIVATE, "minecraft:block_activate");
    register_game_event_type(GAME_EVENT_BLOCK_ATTACH, "minecraft:block_attach");
    register_game_event_type(GAME_EVENT_BLOCK_CHANGE, "minecraft:block_change");
    register_game_event_type(GAME_EVENT_BLOCK_CLOSE, "minecraft:block_close");
    register_game_event_type(GAME_EVENT_BLOCK_DEACTIVATE, "minecraft:block_deactivate");
    register_game_event_type(GAME_EVENT_BLOCK_DESTROY, "minecraft:block_destroy");
    register_game_event_type(GAME_EVENT_BLOCK_DETACH, "minecraft:block_detach");
    register_game_event_type(GAME_EVENT_BLOCK_OPEN, "minecraft:block_open");
    register_game_event_type(GAME_EVENT_BLOCK_PLACE, "minecraft:block_place");
    register_game_event_type(GAME_EVENT_CONTAINER_CLOSE, "minecraft:container_close");
    register_game_event_type(GAME_EVENT_CONTAINER_OPEN, "minecraft:container_open");
    register_game_event_type(GAME_EVENT_DRINK, "minecraft:drink");
    register_game_event_type(GAME_EVENT_EAT, "minecraft:eat");
    register_game_event_type(GAME_EVENT_ELYTRA_GLIDE, "minecraft:elytra_glide");
    register_game_event_type(GAME_EVENT_ENTITY_DAMAGE, "minecraft:entity_damage");
    register_game_event_type(GAME_EVENT_ENTITY_DIE, "minecraft:entity_die");
    register_game_event_type(GAME_EVENT_ENTITY_DISMOUNT, "minecraft:entity_dismount");
    register_game_event_type(GAME_EVENT_ENTITY_INTERACT, "minecraft:entity_interact");
    register_game_event_type(GAME_EVENT_ENTITY_MOUNT, "minecraft:entity_mount");
    register_game_event_type(GAME_EVENT_ENTITY_PLACE, "minecraft:entity_place");
    register_game_event_type(GAME_EVENT_ENTITY_ACTION, "minecraft:entity_action");
    register_game_event_type(GAME_EVENT_EQUIP, "minecraft:equip");
    register_game_event_type(GAME_EVENT_EXPLODE, "minecraft:explode");
    register_game_event_type(GAME_EVENT_FLAP, "minecraft:flap");
    register_game_event_type(GAME_EVENT_FLUID_PICKUP, "minecraft:fluid_pickup");
    register_game_event_type(GAME_EVENT_FLUID_PLACE, "minecraft:fluid_place");
    register_game_event_type(GAME_EVENT_HIT_GROUND, "minecraft:hit_ground");
    register_game_event_type(GAME_EVENT_INSTRUMENT_PLAY, "minecraft:instrument_play");
    register_game_event_type(GAME_EVENT_ITEM_INTERACT_FINISH, "minecraft:item_interact_finish");
    register_game_event_type(GAME_EVENT_ITEM_INTERACT_START, "minecraft:item_interact_start");
    register_game_event_type(GAME_EVENT_JUKEBOX_PLAY, "minecraft:jukebox_play");
    register_game_event_type(GAME_EVENT_JUKEBOX_STOP_PLAY, "minecraft:jukebox_stop_play");
    register_game_event_type(GAME_EVENT_LIGHTNING_STRIKE, "minecraft:lightning_strike");
    register_game_event_type(GAME_EVENT_NOTE_BLOCK_PLAY, "minecraft:note_block_play");
    register_game_event_type(GAME_EVENT_PRIME_FUSE, "minecraft:prime_fuse");
    register_game_event_type(GAME_EVENT_PROJECTILE_LAND, "minecraft:projectile_land");
    register_game_event_type(GAME_EVENT_PROJECTILE_SHOOT, "minecraft:projectile_shoot");
    register_game_event_type(GAME_EVENT_SCULK_SENSOR_TENDRILS_CLICKING, "minecraft:sculk_sensor_tendrils_clicking");
    register_game_event_type(GAME_EVENT_SHEAR, "minecraft:shear");
    register_game_event_type(GAME_EVENT_SHRIEK, "minecraft:shriek");
    register_game_event_type(GAME_EVENT_SPLASH, "minecraft:splash");
    register_game_event_type(GAME_EVENT_STEP, "minecraft:step");
    register_game_event_type(GAME_EVENT_SWIM, "minecraft:swim");
    register_game_event_type(GAME_EVENT_TELEPORT, "minecraft:teleport");
    register_game_event_type(GAME_EVENT_UNEQUIP, "minecraft:unequip");
    register_game_event_type(GAME_EVENT_RESONATE_1, "minecraft:resonate_1");
    register_game_event_type(GAME_EVENT_RESONATE_2, "minecraft:resonate_2");
    register_game_event_type(GAME_EVENT_RESONATE_3, "minecraft:resonate_3");
    register_game_event_type(GAME_EVENT_RESONATE_4, "minecraft:resonate_4");
    register_game_event_type(GAME_EVENT_RESONATE_5, "minecraft:resonate_5");
    register_game_event_type(GAME_EVENT_RESONATE_6, "minecraft:resonate_6");
    register_game_event_type(GAME_EVENT_RESONATE_7, "minecraft:resonate_7");
    register_game_event_type(GAME_EVENT_RESONATE_8, "minecraft:resonate_8");
    register_game_event_type(GAME_EVENT_RESONATE_9, "minecraft:resonate_9");
    register_game_event_type(GAME_EVENT_RESONATE_10, "minecraft:resonate_10");
    register_game_event_type(GAME_EVENT_RESONATE_11, "minecraft:resonate_11");
    register_game_event_type(GAME_EVENT_RESONATE_12, "minecraft:resonate_12");
    register_game_event_type(GAME_EVENT_RESONATE_13, "minecraft:resonate_13");
    register_game_event_type(GAME_EVENT_RESONATE_14, "minecraft:resonate_14");
    register_game_event_type(GAME_EVENT_RESONATE_15, "minecraft:resonate_15");
}

static void
load_tags(char * file_name, char * list_name, tag_list * tags, resource_loc_table * table) {
    LogInfo("Loading tags: %s", list_name);
    MemoryArena arena = {
        .data = serv->short_lived_scratch,
        .size = serv->short_lived_scratch_size,
    };
    Cursor cursor = read_file(&arena, file_name);

    tags->name_size = strlen(list_name);
    memcpy(tags->name, list_name, strlen(list_name));

    String args[16];
    tag_spec * tag = NULL;

    for (;;) {
        int arg_count = parse_database_line(&cursor, args);
        if (arg_count == 0) {
            // empty line
            if (cursor.index == cursor.size) {
                break;
            }
        } else if (net_string_equal(args[0], STR("key"))) {
            assert(tags->size < (i32) ARRAY_SIZE(tags->tags));

            tag = tags->tags + tags->size;
            tags->size++;

            *tag = (tag_spec) {0};
            tag->name_index = serv->tag_name_count;
            tag->value_count = 0;
            tag->values_index = serv->tag_value_id_count;

            int name_size = args[1].size;
            assert(name_size <= UCHAR_MAX);
            assert(serv->tag_name_count + 1 + name_size <= (i32) ARRAY_SIZE(serv->tag_name_buf));

            serv->tag_name_buf[serv->tag_name_count] = name_size;
            serv->tag_name_count++;
            memcpy(serv->tag_name_buf + serv->tag_name_count, args[1].data, name_size);
            serv->tag_name_count += name_size;
        } else if (net_string_equal(args[0], STR("value"))) {
            i16 id = resolve_resource_loc_id(args[1], table);
            if (id == -1) {
                LogInfo("Failed to resolve \"%.*s\"", args[1].size, args[1].data);
                assert(0);
            }

            assert(tag->values_index + tag->value_count <= (i32) ARRAY_SIZE(serv->tag_value_id_buf));
            serv->tag_value_id_buf[tag->values_index + tag->value_count] = id;
            serv->tag_value_id_count++;

            tag->value_count++;
        }
    }
}

int
main(void) {
    InitNanoTime();

    LogInfo("Running Blaze");

    // Ignore SIGPIPE so the server doesn't crash (by getting signals) if a
    // client decides to abruptly close its end of the connection.
    signal(SIGPIPE, SIG_IGN);

    // NOTE(traks): without this, sleep times can be delayed by 3 ms. With this,
    // the delay is in the order of 100 us - 1 ms (at least on macOS).
    // TODO(traks): do we really want to set scheduler policy and priority?
    pthread_t mainThread = pthread_self();
    i32 mainThreadSchedPolicy = SCHED_FIFO;
    i32 minSchedPrio = sched_get_priority_min(mainThreadSchedPolicy);
    i32 maxSchedPrio = sched_get_priority_max(mainThreadSchedPolicy);
    struct sched_param mainThreadSchedParam = {
#ifdef PROFILE
        // NOTE(traks): This gives much more consistent timings
        .sched_priority = maxSchedPrio
#else
        .sched_priority = minSchedPrio + (maxSchedPrio - minSchedPrio) / 2
#endif
    };
    pthread_setschedparam(mainThread, SCHED_FIFO, &mainThreadSchedParam);

    // @TODO(traks) ctrl+c is useful for debugging if the program ends up inside
    // an infinite loop
    // signal(SIGINT, OnSigInt);

    InitNetwork();

    serv = calloc(sizeof * serv, 1);
    if (serv == NULL) {
        LogErrno("Failed to allocate server struct: %s");
        exit(1);
    }

    // reserve null entity
    serv->entities[0].flags |= ENTITY_IN_USE;
    serv->entities[0].type = ENTITY_NULL;

    // allocate memory for arenas
    serv->short_lived_scratch_size = 4 * (1 << 20);
    serv->short_lived_scratch = calloc(serv->short_lived_scratch_size, 1);
    if (serv->short_lived_scratch == NULL) {
        LogErrno("Failed to allocate short lived scratch arena: %s");
        exit(1);
    }

    // @TODO(traks) must be able to grow
    i32 tickArenaSize = 1 << 20;
    MemoryArena tickArena = {
        .size = tickArenaSize,
        .data = calloc(tickArenaSize, 1)
    };
    serv->tickArena = &tickArena;

    // @TODO(traks) use this arena for other stuff we allocate above as well
    i32 permanentArenaSize = 1 << 20;
    MemoryArena permanentArena = {
        .size = permanentArenaSize,
        .data = calloc(permanentArenaSize, 1)
    };
    serv->permanentArena = &permanentArena;

    // @TODO(traks) better sizes
    alloc_resource_loc_table(&serv->block_resource_table, 1 << 11, 1 << 16, ACTUAL_BLOCK_TYPE_COUNT);
    alloc_resource_loc_table(&serv->item_resource_table, 1 << 11, 1 << 16, ITEM_TYPE_COUNT);
    alloc_resource_loc_table(&serv->entity_resource_table, 1 << 10, 1 << 12, ENTITY_TYPE_COUNT);
    alloc_resource_loc_table(&serv->fluid_resource_table, 1 << 10, 1 << 10, 5);
    alloc_resource_loc_table(&serv->game_event_resource_table, 1 << 10, 1 << 12, GAME_EVENT_TYPE_COUNT);

    init_item_data();
    init_block_data();
    init_entity_data();
    init_fluid_data();
    init_game_event_data();
    load_tags("blocktags.txt", "minecraft:block", &serv->block_tags, &serv->block_resource_table);
    load_tags("itemtags.txt", "minecraft:item", &serv->item_tags, &serv->item_resource_table);
    load_tags("entitytags.txt", "minecraft:entity_type", &serv->entity_tags, &serv->entity_resource_table);
    load_tags("fluidtags.txt", "minecraft:fluid", &serv->fluid_tags, &serv->fluid_resource_table);
    load_tags("gameeventtags.txt", "minecraft:game_event", &serv->game_event_tags, &serv->game_event_resource_table);

    // @NOTE(traks) chunk sections assume that no changes happen in tick 0, so
    // initialise tick number to something larger than 0 to be safe
    serv->current_tick = 10;

    TaskQueue * backgroundQueue = MallocInArena(serv->permanentArena, sizeof *backgroundQueue);
    CreateTaskQueue(backgroundQueue, 2);
    serv->backgroundQueue = backgroundQueue;

    InitChunkSystem();

    LogInfo("Entering tick loop");

    i64 desiredTickStart = NanoTime();

    for (;;) {
#ifdef PROFILE
        TracyCFrameMark
#endif

        serv->tickArena->index = 0;
        serv->currentTickStartNanos = desiredTickStart;
        server_tick();

        if (interruptCount > 0) {
            LogInfo("Interrupted");
            break;
        }

        i64 actualTickEnd = NanoTime();

        // NOTE(traks): update tick starts this way to avoid time drift
        i64 nextDesiredTickStart = desiredTickStart + 50000000LL;

        // NOTE(traks): if the tick is taking longer than expected, accept the
        // lag and update the desired tick start appropriately. Without this,
        // the server will run a bunch of short ticks after a period of lag, to
        // catch up with the desired tick start. That could be desirable for a
        // small period of lag, but not for prolonged periods of lag.
        if (actualTickEnd > nextDesiredTickStart) {
            LogInfo("Tick took too long: %lldms", (actualTickEnd - desiredTickStart) / 1000000);
            nextDesiredTickStart = actualTickEnd;
        }

        // NOTE(traks): nanosleep can exit early due to interrupts, so we need
        // to sleep again in such cases
        for (;;) {
            i64 timeRemaining = nextDesiredTickStart - NanoTime();
            if (timeRemaining >= 10000) {
                struct timespec sleepTime = {
                    .tv_nsec = timeRemaining
                };
                // TODO(traks): on macOS this has a pretty signiciant drift. Can
                // be off by 1 millisecond!
                nanosleep(&sleepTime, NULL);
            } else {
                desiredTickStart = nextDesiredTickStart;
                break;
            }
        }
    }

    LogInfo("Goodbye!");
    return 0;
}
