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
#include "player.h"

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

Entity * ResolveEntity(EntityId id) {
    u32 index = id & ENTITY_INDEX_MASK;
    Entity * res = serv->entities + index;
    if (res->id != id || !(res->flags & ENTITY_IN_USE)) {
        // NOTE(traks): return the null entity
        return serv->entities;
    }
    return res;
}

Entity * TryReserveEntity(i32 type) {
    for (i32 entityIndex = 0; entityIndex < MAX_ENTITIES; entityIndex++) {
        Entity * entity = serv->entities + entityIndex;
        if (!(entity->flags & ENTITY_IN_USE)) {
            u16 generation = serv->next_entity_generations[entityIndex];
            EntityId entityId = ((u32) generation << 20) | entityIndex;

            *entity = (Entity) {0};

            entity->id = entityId;
            // TODO(traks): Proper UUID creation. For players we overwrite this
            entity->uuid = (UUID) {.low = entityId, .high = 0};
            entity->type = type;
            entity->flags |= ENTITY_IN_USE;
            serv->next_entity_generations[entityIndex] = (generation + 1) & 0xfff;
            serv->entity_count++;
            return entity;
        }
    }
    // first entity used as placeholder for null entity
    return serv->entities;
}

void EvictEntity(EntityId id) {
    Entity * entity = ResolveEntity(id);
    if (entity->type != ENTITY_NULL) {
        // TODO(traks): this should probably not get rid of the entity
        // immediately. We should probably wait until the end of the tick to
        // actually remove entities. It's kinda dangerous to mess with entity
        // removal. E.g. if some piece of code resolves an entity ID and some
        // call involving the reference evicts the entity, the entity reference
        // could be garbage.
        entity->flags &= ~ENTITY_IN_USE;
        serv->entity_count--;
    }
}

static void
move_entity(Entity * entity) {
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
tick_entity(Entity * entity, MemoryArena * tick_arena) {
    // @TODO(traks) currently it's possible that an entity is spawned and ticked
    // the same tick. Is that an issue or not? Maybe that causes undesirable
    // off-by-one tick behaviour.

    switch (entity->type) {
    case ENTITY_ITEM: {
        if (entity->contents.type == ITEM_AIR) {
            EvictEntity(entity->id);
            return;
        }

        if (entity->pickup_timeout > 0
                && entity->pickup_timeout != 32767) {
            entity->pickup_timeout--;
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

    // run scheduled block updates
    BeginTimings(ScheduledUpdates);

    MemoryArena scheduled_update_arena = {
        .data = serv->short_lived_scratch,
        .size = serv->short_lived_scratch_size
    };
    propagate_delayed_block_updates(&scheduled_update_arena);

    EndTimings(ScheduledUpdates);

    BeginTimings(TickPlayers);
    {
        MemoryArena tick_arena = {
            .data = serv->short_lived_scratch,
            .size = serv->short_lived_scratch_size
        };
        TickPlayers(&tick_arena);
    }
    EndTimings(TickPlayers);

    // update entities
    BeginTimings(TickEntities);

    for (int i = 0; i < (i32) ARRAY_SIZE(serv->entities); i++) {
        Entity * entity = serv->entities + i;
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
        UUID uuid = serv->tab_list[i];
        PlayerController * control = ResolvePlayer(uuid);
        if (control == NULL) {
            // @TODO(traks) make sure this can never happen instead of hoping
            assert(serv->tab_list_removed_count < (i32) ARRAY_SIZE(serv->tab_list_removed));
            serv->tab_list_removed[serv->tab_list_removed_count] = uuid;
            serv->tab_list_removed_count++;
            serv->tab_list[i] = serv->tab_list[serv->tab_list_size - 1];
            serv->tab_list_size--;
            i--;
        }
    }

    // add players to live tab list
    for (int i = 0; i < serv->tab_list_added_count; i++) {
        UUID uuid = serv->tab_list_added[i];
        assert(serv->tab_list_size < (i32) ARRAY_SIZE(serv->tab_list));
        serv->tab_list[serv->tab_list_size] = uuid;
        serv->tab_list_size++;
    }

    EndTimings(UpdateTabList);

    BeginTimings(SendPlayers);
    {
        MemoryArena tick_arena = {
            .data = serv->short_lived_scratch,
            .size = serv->short_lived_scratch_size
        };
        SendPacketsToPlayers(&tick_arena);
    }
    EndTimings(SendPlayers);

    // clear global messages
    serv->global_msg_count = 0;

    // clear tab list updates
    serv->tab_list_added_count = 0;
    serv->tab_list_removed_count = 0;

    BeginTimings(ClearEntityChanges);

    for (int i = 0; i < (i32) ARRAY_SIZE(serv->entities); i++) {
        Entity * entity = serv->entities + i;
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

    InitPlayerControl();
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

    InitRegistries();
    init_item_data();
    init_block_data();

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
