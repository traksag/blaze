#include "shared.h"
#include "chunk.h"

// NOTE(traks): Add a couple of extra entries because we may need 1 (or so) more
// entry to ensure the tail and head aren't at the same position if the queue is
// full.
// TODO(traks): I doubt we're ever going to have all blocks in the 9 chunks
// enqueued. Is there some better theoretical limit? Another problem currently
// is that blocks can be enqueued multiple times (can the limit ever be
// exceeded?).
#define LIGHT_QUEUE_SIZE (9 * 16 * 16 * 16 * LIGHT_SECTIONS_PER_CHUNK + 8)

typedef struct {
    // NOTE(traks): position is the offset from y = min sky light level
    // (i.e. min world height - 16) in the lowest corner of the centre chunk
    i16 y;
    i8 z;
    i8 x;
} LightQueueEntry;

typedef struct {
    // NOTE(traks): contains which positions to propagate from
    LightQueueEntry * entries;
    i32 readIndex;
    i32 writeIndex;
    // NOTE(traks): index as yzx
    u8 * lightSections[4 * 4 * 32];
    u16 * blockSections[4 * 4 * 32];
} LightQueue;

static inline void LightQueuePush(LightQueue * queue, LightQueueEntry entry) {
    i32 writeIndex = queue->writeIndex;
    queue->entries[writeIndex] = entry;
    writeIndex = (writeIndex + 1) % LIGHT_QUEUE_SIZE;
    assert(writeIndex != queue->readIndex);
    queue->writeIndex = writeIndex;
}

static inline LightQueueEntry LightQueuePop(LightQueue * queue) {
    i32 readIndex = queue->readIndex;
    assert(readIndex != queue->writeIndex);
    LightQueueEntry res = queue->entries[readIndex];
    readIndex = (readIndex + 1) % LIGHT_QUEUE_SIZE;
    queue->readIndex = readIndex;
    return res;
}

// NOTE(traks): update a neighbour's skylight and push the neighbour to the
// queue if further propagation is necessary
static inline void PropagateSkyLight(LightQueue * queue, i32 fromX, i32 fromY, i32 fromZ, i32 dx, i32 dy, i32 dz, i32 fromValue, i32 spreadValue) {
    i32 toX = (fromX + dx) & 0x3f;
    i32 toY = (fromY + dy) & 0x1ff;
    i32 toZ = (fromZ + dz) & 0x3f;

    i32 sectionIndex = ((toY & 0x1f0) >> 0) | ((toZ & 0x30) >> 2) | ((toX & 0x30) >> 4);
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

    LightQueuePush(queue, (LightQueueEntry) {.x = toX, .y = toY, .z = toZ});
}

static i32 GetNeighbourIndex(i32 dx, i32 dz) {
    i32 res = ((dz & 0x3) << 2) | (dx & 0x3);
    return res;
}

static void PropagateSkyLightFromNeighbour(LightQueue * queue, Chunk * * chunkGrid, i32 baseX, i32 baseZ, i32 addX, i32 addZ, i32 chunkDx, i32 chunkDz) {
    Chunk * from = chunkGrid[GetNeighbourIndex(chunkDx, chunkDz)];
    if (from == NULL) {
        // NOTE(traks): null chunks have max sky light to prevent propagating
        // into it. Don't propagate that max light out of it!
        return;
    }

    i32 startY = LIGHT_SECTIONS_PER_CHUNK * 16 - 1;
    for (i32 y = startY; y >= 0; y--) {
        i32 x = 16 * chunkDx + baseX;
        i32 z = 16 * chunkDz + baseZ;
        for (i32 h = 0; h < 16; h++) {
            i32 sectionIndex = ((y & 0x1f0) >> 0) | ((z & 0x30) >> 2) | ((x & 0x30) >> 4);
            i32 posIndex = ((y & 0xf) << 8) | ((z & 0xf) << 4) | (x & 0xf);
            i32 value = GetSectionLight(queue->lightSections[sectionIndex], posIndex);
            PropagateSkyLight(queue, x, y, z, -chunkDx, 0, -chunkDz, value, value - 1);
            x += addX;
            z += addZ;
        }
    }
}

static void PropagateSkyLightFully(LightQueue * queue) {
    for (;;) {
        if (queue->readIndex == queue->writeIndex) {
            break;
        }

        LightQueueEntry entry = LightQueuePop(queue);
        i32 x = entry.x;
        i32 y = entry.y;
        i32 z = entry.z;

        i32 sectionIndex = ((y & 0x1f0) >> 0) | ((z & 0x30) >> 2) | ((x & 0x30) >> 4);
        i32 posIndex = ((y & 0xf) << 8) | ((z & 0xf) << 4) | (x & 0xf);
        i32 value = GetSectionLight(queue->lightSections[sectionIndex], posIndex);

        PropagateSkyLight(queue, x, y, z, -1, 0, 0, value, value - 1);
        PropagateSkyLight(queue, x, y, z, 1, 0, 0, value, value - 1);
        PropagateSkyLight(queue, x, y, z, 0, 0, -1, value, value - 1);
        PropagateSkyLight(queue, x, y, z, 0, 0, 1, value, value - 1);
        PropagateSkyLight(queue, x, y, z, 0, -1, 0, value, value == 15 ? 15 : (value - 1));
        PropagateSkyLight(queue, x, y, z, 0, 1, 0, value, value - 1);
    }
}

static void LoadChunkGrid(Chunk * targetChunk, Chunk * * chunkGrid) {
    chunkGrid[0] = targetChunk;
    for (i32 dz = -1; dz <= 1; dz++) {
        for (i32 dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dz == 0) {
                continue;
            }
            WorldChunkPos mid = targetChunk->pos;
            WorldChunkPos pos = mid;
            pos.x += dx;
            pos.z += dz;
            i32 index = GetNeighbourIndex(dx, dz);
            Chunk * neighbour = GetChunkInternal(pos);
            if (neighbour != NULL && neighbour->exchangeLightWithNeighbours & 1) {
                // NOTE(traks): the neighbouring chunk lit itself, so we can
                // exchange light with it
                chunkGrid[index] = neighbour;
            }
        }
    }
}

static void MarkLightExchanged(Chunk * * chunkGrid) {
    for (i32 dz = -1; dz <= 1; dz++) {
        for (i32 dx = -1; dx <= 1; dx++) {
            i32 index = GetNeighbourIndex(dx, dz);
            i32 oppositeIndex = GetNeighbourIndex(-dx, -dz);
            Chunk * neighbour = chunkGrid[index];
            if (neighbour != NULL) {
                Chunk * target = chunkGrid[0];
                target->exchangeLightWithNeighbours |= ((u16) 1 << index);
                neighbour->exchangeLightWithNeighbours |= ((u16) 1 << oppositeIndex);

                // TODO(traks): kinda weird we set finished loading here
                if (target->exchangeLightWithNeighbours == 0b1011000010111011) {
                    target->flags |= CHUNK_FULLY_LIT | CHUNK_FINISHED_LOADING;
                }
                if (neighbour->exchangeLightWithNeighbours == 0b1011000010111011) {
                    neighbour->flags |= CHUNK_FULLY_LIT | CHUNK_FINISHED_LOADING;
                }
            }
        }
    }
}

void LightChunkAndExchangeWithNeighbours(Chunk * targetChunk) {
    // TODO(traks): this takes in the order of 1 ms per call. In the past I
    // tried filling empty sections at the top of the world for extra speed.
    // However, that doesn't work well for Skygrid maps. Consider propagating a
    // column at a time using the chunk's heightmap first, before doing general
    // propagation.
    //
    // Here's another interesting approach. Per section we compute a layer that
    // we stack 16 times. This has the major benefit of being able to set
    // multiple bytes at once (instead of doing work per nibble if e.g. iterate
    // through column until hit height map, then move to next column). Moreover,
    // this also handles things like Skygrid well, while the empty-section
    // approach fails miserably.
    //
    // Can probably go even faster by setting up a nice data structure to figure
    // out at which Y levels which columns start their height map. Then don't
    // even need to do per section I imagine.
    //
    // Should implement a fully functioning lighting engine before diving in
    // deep though.

    BeginTimings(LightChunk);

    BeginTimings(LoadChunkGrid);

    Chunk * chunkGrid[4 * 4] = {0};
    LoadChunkGrid(targetChunk, chunkGrid);

    EndTimings(LoadChunkGrid);

    BeginTimings(InitQueue);

    LightQueue skyLightQueue = {0};
    // NOTE(traks): keep the entries array out of the queue struct, so it isn't
    // zero-initialised above. Zero initialising the entry array can be very
    // slow: hundreds of microseconds for 2^20 entries.
    LightQueueEntry allEntries[LIGHT_QUEUE_SIZE];
    skyLightQueue.entries = allEntries;

    EndTimings(InitQueue);

    BeginTimings(InitReferences);

    // NOTE(traks): set up section references for easy access
    u16 sectionAir[4096] = {0};
    u8 sectionFullLight[2048];
    memset(sectionFullLight, 0xff, 2048);

    for (i32 i = 0; i < ARRAY_SIZE(skyLightQueue.blockSections); i++) {
        skyLightQueue.lightSections[i] = sectionFullLight;
    }

    for (i32 zx = 0; zx < 16; zx++) {
        Chunk * chunk = chunkGrid[zx];

        for (i32 sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
            u16 * blockStates = NULL;
            if (chunk != NULL) {
                blockStates = chunk->sections[sectionIndex].blockStates;
            }
            if (blockStates == NULL) {
                blockStates = sectionAir;
            }
            i32 gridIndex = ((sectionIndex + 1) << 4) | zx;
            skyLightQueue.blockSections[gridIndex] = blockStates;
        }

        skyLightQueue.blockSections[(0 << 4) | zx] = sectionAir;
        skyLightQueue.blockSections[((LIGHT_SECTIONS_PER_CHUNK - 1) << 4) | zx] = sectionAir;

        if (chunk != NULL) {
            for (i32 sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
                skyLightQueue.lightSections[(sectionIndex << 4) | zx] = chunk->lightSections[sectionIndex].skyLight;
            }
        }
    }

    EndTimings(InitReferences);

    BeginTimings(PropagateOwnSkyLight);

    // NOTE(traks): prepare sky light sources for propagation
    for (i32 zx = 0; zx < 16 * 16; zx++) {
        i32 y = (MAX_WORLD_Y - MIN_WORLD_Y + 1) + 16 + 16;
        LightQueuePush(&skyLightQueue, (LightQueueEntry) {.x = zx & 0xf, .y = y, .z = zx >> 4});
    }

    PropagateSkyLightFully(&skyLightQueue);

    EndTimings(PropagateOwnSkyLight);

    BeginTimings(PropagateNeighbourSkyLight);

    PropagateSkyLightFromNeighbour(&skyLightQueue, chunkGrid, 15, 0, 0, 1, -1, 0);
    PropagateSkyLightFromNeighbour(&skyLightQueue, chunkGrid, 0, 0, 0, 1, 1, 0);
    PropagateSkyLightFromNeighbour(&skyLightQueue, chunkGrid, 0, 15, 1, 0, 0, -1);
    PropagateSkyLightFromNeighbour(&skyLightQueue, chunkGrid, 0, 0, 1, 0, 0, 1);
    PropagateSkyLightFully(&skyLightQueue);

    EndTimings(PropagateNeighbourSkyLight);

    BeginTimings(MarkLightExchanged);
    MarkLightExchanged(chunkGrid);
    EndTimings(MarkLightExchanged);

    EndTimings(LightChunk);
}

void UpdateLighting() {
    // @TODO(traks) further implementation
    /*
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
    */
}
