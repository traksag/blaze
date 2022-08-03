#include "shared.h"
#include "chunk.h"

#define LIGHT_QUEUE_MAP_SIZE (16 * 16 * 16 * LIGHT_SECTIONS_PER_CHUNK)

// @NOTE(traks) we track which blocks are enqueued and don't queue the same
// block twice. Therefore the queue size is limited by the size of the queue
// map. We don't need to worry about ever overflowing the queue. Add a couple of
// extra entries because we may need 1 (or so) more entry to ensure the tail and
// head aren't at the same position if the queue is full.
#define LIGHT_QUEUE_SIZE (LIGHT_QUEUE_MAP_SIZE + 8)

typedef struct {
    i8 x;
    i16 y;
    i8 z;
} LightQueueEntry;

typedef struct {
    // @NOTE(traks) pop at head, push at tail
    i32 head;
    i32 tail;
    LightQueueEntry * entries;
    u64 queuedMap[LIGHT_QUEUE_MAP_SIZE / 64];
    // @NOTE(traks) index as yzx
    u8 * lightSections[4 * 32];
    u16 * blockSections[4 * 32];

    // @TODO(traks) remove debug data
    i32 pushCount;
} LightQueue;

static inline void LightQueuePush(LightQueue * queue, LightQueueEntry entry) {
    i32 mapIndex = (entry.y << 8) | (entry.z << 4) | entry.x;
    i32 longIndex = mapIndex >> 6;
    i32 bitIndex = mapIndex & 0x3f;
    if (queue->queuedMap[longIndex] & ((u64) 1 << bitIndex)) {
        return;
    }
    queue->queuedMap[longIndex] |= ((u64) 1 << bitIndex);

    i32 tail = queue->tail;
    queue->entries[tail] = entry;
    tail = (tail + 1) % LIGHT_QUEUE_SIZE;
    assert(tail != queue->head);
    queue->tail = tail;
    queue->pushCount++;
}

static inline LightQueueEntry LightQueuePop(LightQueue * queue) {
    i32 head = queue->head;
    assert(head != queue->tail);
    LightQueueEntry res = queue->entries[head];
    head = (head + 1) % LIGHT_QUEUE_SIZE;
    queue->head = head;

    i32 mapIndex = (res.y << 8) | (res.z << 4) | res.x;
    i32 longIndex = mapIndex >> 6;
    i32 bitIndex = mapIndex & 0x3f;
    queue->queuedMap[longIndex] &= ~((u64) 1 << bitIndex);
    return res;
}

static inline void PropagateSkyLight(LightQueue * queue, i32 fromX, i32 fromY, i32 fromZ, i32 dx, i32 dy, i32 dz, i32 fromValue, i32 spreadValue) {
    i32 toX = fromX + dx;
    i32 toY = fromY + dy;
    i32 toZ = fromZ + dz;

    i32 sectionIndex = ((toY & 0x1f0) >> 2) | ((toZ & 0x10) >> 3) | ((toX & 0x10) >> 4);
    i32 posIndex = ((toY & 0xf) << 8) | ((toZ & 0xf) << 4) | (toX & 0xf);
    i32 storedValue = GetSectionLight(queue->lightSections[sectionIndex], posIndex);
    // @NOTE(traks) final value is only going to be less than the spread value.
    // Early exit to avoid the block lookup (likely cache miss).
    // @NOTE(traks) also important because the neighbouring chunks have NULL
    // block states in the lookup table. We avoid dereferencing these NULL
    // pointers because the light of the neighbouring chunks in the lookup table
    // is set to full light.
    if (storedValue >= spreadValue) {
        return;
    }

    u16 toState = queue->blockSections[sectionIndex][posIndex];
    i32 finalValue;
    // @TODO(traks) opacity and stuff, other types of air!!
    if (toState == 0) {
        finalValue = spreadValue;
    } else {
        finalValue = 0;
    }

    if (finalValue <= 0) {
        return;
    }
    assert(finalValue <= spreadValue);

    if (storedValue >= finalValue) {
        return;
    }
    SetSectionLight(queue->lightSections[sectionIndex], posIndex, finalValue);

    LightQueuePush(queue, (LightQueueEntry) {toX, toY, toZ});
}


void LightChunk(Chunk * ch) {
    // @NOTE(traks) calculating skylight for 1 chunk statistics in
    // vanilla-generated world, without optimisations:
    // dumb (by eye):
    // ~400,000 pushes
    // ~28 ms
    //
    // fast top (averaged):
    // 15,844 pushes
    // 1.249 ms
    //
    // avoid duplicate pushes (averaged):
    // 1,864 pushes
    // 0.645 ms
    // 0.317 ms optimised
    //
    // avoid block queries (averaged):
    // 1,864 pushes
    // 0.305 ms optimised
    //
    // bit mask for queued map (averaged):
    // 1,864 pushes
    // 0.303 ms optimised
    // -> apparently the extra computations don't affect duration, but might
    // help with cache usage
    //
    // section lookup tables (averaged):
    // 1,859 pushes
    // 0.286 ms optimised
    //
    // don't clear entries array (averaged):
    // 1,859 pushes
    // 0.044 ms optimised
    //
    // fast top extra y layers (averaged):
    // 1,106
    // 0.032 ms optimised

    BeginTimedZone("LightChunk");

    BeginTimedZone("InitLightChunk");

    i64 startTime = program_nano_time();

    BeginTimedZone("Init queue");

    LightQueue skyLightQueue = {0};
    // @NOTE(traks) keep the entries array out of the queue struct, so it isn't
    // zero-initialised above. Zero initialising the entry array can be very
    // slow: hundreds of microseconds for 2^20 entries.
    LightQueueEntry allEntries[LIGHT_QUEUE_SIZE];
    skyLightQueue.entries = allEntries;

    EndTimedZone();

    BeginTimedZone("Set up references");

    // @NOTE(traks) set up section references for easy access
    u16 sectionAir[4096] = {0};
    u8 sectionFullLight[2048];
    memset(sectionFullLight, 0xff, 2048);

    for (i32 i = 0; i < ARRAY_SIZE(skyLightQueue.blockSections); i++) {
        skyLightQueue.lightSections[i] = sectionFullLight;
    }
    for (i32 sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
        u16 * blockStates = ch->sections[sectionIndex].blockStates;
        if (blockStates == NULL) {
            blockStates = sectionAir;
        }
        skyLightQueue.blockSections[(sectionIndex + 1) * 4] = blockStates;
    }
    skyLightQueue.blockSections[0] = sectionAir;
    skyLightQueue.blockSections[(LIGHT_SECTIONS_PER_CHUNK - 1) * 4] = sectionAir;

    for (i32 sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
        skyLightQueue.lightSections[sectionIndex * 4] = ch->lightSections[sectionIndex].skyLight;
    }

    EndTimedZone();

    BeginTimedZone("Top light");

    // @NOTE(traks) first set all y layers to full light level above the highest
    // block in the chunk
    i32 chunkMaxY = MIN_WORLD_Y;
    for (i32 x = 0; x < 16; x++) {
        for (i32 z = 0; z < 16; z++) {
            i32 maxY = ch->motion_blocking_height_map[(z << 4) | x];
            chunkMaxY = MAX(maxY, chunkMaxY);
        }
    }

    i32 lowestSectionFullLight = ((chunkMaxY - MIN_WORLD_Y + 16) >> 4) + 1;
    assert(lowestSectionFullLight >= 1);
    assert(lowestSectionFullLight <= LIGHT_SECTIONS_PER_CHUNK);
    for (i32 sectionIndex = LIGHT_SECTIONS_PER_CHUNK - 1; sectionIndex >= lowestSectionFullLight; sectionIndex--) {
        memset(ch->lightSections[sectionIndex].skyLight, 0xff, 2048);
    }
    i32 extraFullLightOffset = (chunkMaxY & 0xf) * 16 * 16 / 2;
    memset(ch->lightSections[lowestSectionFullLight - 1].skyLight + extraFullLightOffset, 0xff, 2048 - extraFullLightOffset);

    EndTimedZone();

    EndTimedZone();

    // @NOTE(traks) prepare sky light sources for propagation
    for (i32 x = 0; x < 16; x++) {
        for (i32 z = 0; z < 16; z++) {
            i32 y = chunkMaxY - MIN_WORLD_Y + 16;
            LightQueuePush(&skyLightQueue, (LightQueueEntry) {x, y, z});
        }
    }

    // @NOTE(traks) propagate sky light
    for (;;) {
        if (skyLightQueue.head == skyLightQueue.tail) {
            break;
        }

        LightQueueEntry entry = LightQueuePop(&skyLightQueue);
        i32 x = entry.x;
        i32 y = entry.y;
        i32 z = entry.z;

        i32 sectionIndex = ((y & 0x1f0) >> 2) | ((z & 0x10) >> 3) | ((x & 0x10) >> 4);
        i32 posIndex = ((y & 0xf) << 8) | ((z & 0xf) << 4) | (x & 0xf);
        i32 value = GetSectionLight(skyLightQueue.lightSections[sectionIndex], posIndex);

        PropagateSkyLight(&skyLightQueue, x, y, z, -1, 0, 0, value, value - 1);
        PropagateSkyLight(&skyLightQueue, x, y, z, 1, 0, 0, value, value - 1);
        PropagateSkyLight(&skyLightQueue, x, y, z, 0, 0, -1, value, value - 1);
        PropagateSkyLight(&skyLightQueue, x, y, z, 0, 0, 1, value, value - 1);
        PropagateSkyLight(&skyLightQueue, x, y, z, 0, -1, 0, value, value == 15 ? 15 : (value - 1));
        PropagateSkyLight(&skyLightQueue, x, y, z, 0, 1, 0, value, value - 1);
    }

    i64 endTime = program_nano_time();

    EndTimedZone();

    if (DEBUG_LIGHTING_ENGINE) {
        static i64 totalPushCount;
        static i64 totalCallCount;
        static i64 totalElapsedTime;

        totalPushCount += skyLightQueue.pushCount;
        totalCallCount++;
        totalElapsedTime += (endTime - startTime) / 1000;

        LogInfo("pushes: %d, elapsed: %d, calls: %d",
                totalPushCount / totalCallCount,
                totalElapsedTime / totalCallCount,
                totalCallCount);
    }
}

void UpdateLighting() {
    // @TODO(traks) further implementation
    for (i32 bucketIndex = 0; bucketIndex < ARRAY_SIZE(chunk_map); bucketIndex++) {
        chunk_bucket * bucket = chunk_map + bucketIndex;
        for (i32 chunkIndex = 0; chunkIndex < bucket->size; chunkIndex++) {
            Chunk * ch = bucket->chunks + chunkIndex;
            for (i32 sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
                ChunkSection * section = ch->sections + sectionIndex;
                if (section->lastChangeTick == serv->current_tick) {
                    // @NOTE(traks) chunk section has changed blocks
                }
            }
        }
    }
}
