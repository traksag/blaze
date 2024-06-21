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

// #define MEASURE_BANDWIDTH

typedef struct {
    // NOTE(traks): Holds the position we want to propagate further from. It is
    // represented as the offset from y = min sky light level (i.e. min world
    // height - 16) in the lowest x,z corner of the centre chunk. See the
    // conversion functions below for further encoding details.
    u32 data;
} LightQueueEntry;

typedef struct {
    // NOTE(traks): contains which positions to propagate from
    LightQueueEntry * entries;
    i32 writeIndex;
    // NOTE(traks): index as yzx
    u8 * lightSections[4 * 4 * 32];
    SectionBlocks blockSections[4 * 4 * 32];
#ifdef MEASURE_BANDWIDTH
    i64 blockAccessCount;
    i64 lightAccessCount;
#endif
} LightQueue;

static inline u32 PosFromXYZ(i32 x, i32 y, i32 z) {
    // NOTE(traks): We encode positions this way, because it allows us to
    // subtract and add offsets easily. Subtracts borrow bits from the 1's in
    // the special bit mask. We XOR so a negative input coordinate borrows.
    // TODO(traks): We really only need 1 borrow bit per coordinate, though the
    // shifts and bit masks become less nice. Does free up the top byte, instead
    // of just the top hex digit!
    u32 res = (0x0e00c0c0) ^ ((y & 0x3ff) << 16) ^ ((z & 0x7f) << 8) ^ (x & 0x7f);
    return res;
}

static inline i32 XYZToSectionIndex(i32 x, i32 y, i32 z) {
    i32 res = (y & 0x1f0) | ((z & 0x30) >> 2) | ((x & 0x30) >> 4);
    return res;
}

static inline i32 PosToSectionIndex(u32 pos) {
    i32 res = ((pos & 0x1f00000) >> 16) | ((pos & 0x3000) >> 10) | ((pos & 0x30) >> 4);
    return res;
}

static inline i32 PosToSectionPosIndex(u32 pos) {
    i32 res = ((pos & 0xf0000) >> 8) | ((pos & 0xf00) >> 4) | (pos & 0xf);
    return res;
}

static inline i32 PosToX(u32 pos) {
    return pos & 0x3f;
}

static inline i32 PosToZ(u32 pos) {
    return (pos >> 8) & 0x3f;
}

static inline i32 PosToY(u32 pos) {
    return (pos >> 16) & 0x1ff;
}

static inline LightQueueEntry PackEntry(u32 pos) {
    LightQueueEntry res = {.data = pos};
    return res;
}

static inline u32 GetEntryPos(LightQueueEntry entry) {
    return entry.data;
}

static inline void LightQueuePush(LightQueue * queue, LightQueueEntry entry) {
    i32 writeIndex = queue->writeIndex;
    if (writeIndex >= LIGHT_QUEUE_SIZE) {
        // TODO(traks): What should we do when this happens? Grow? Or should we
        // ensure this can never happen theoretically by making the queue size
        // large enough?
        assert(0);
        return;
    }
    queue->entries[writeIndex] = entry;
    queue->writeIndex++;
}

// NOTE(traks): update a neighbour's light and push the neighbour to the
// queue if further propagation is necessary
static inline void PropagateLight(LightQueue * queue, u32 toPos, i32 dir, i32 fromState, i32 fromValue, i32 lightReduction) {
    i32 sectionIndex = PosToSectionIndex(toPos);
    i32 posIndex = PosToSectionPosIndex(toPos);
    i32 storedValue = GetSectionLight(queue->lightSections[sectionIndex], posIndex);
#ifdef MEASURE_BANDWIDTH
    queue->lightAccessCount++;
#endif
    i32 spreadValue = fromValue - lightReduction;
    // NOTE(traks): final value is only going to be less than the spread value.
    // Early exit to avoid the block lookup (likely cache miss).
    // NOTE(traks): also important to avoid propagating into unloaded chunks or
    // invalid sections. These are full air and have max light.
    if (storedValue >= spreadValue) {
        return;
    }

    i32 toState = SectionGetBlockState(&queue->blockSections[sectionIndex], posIndex);
    i32 reductionOfState = serv->lightReductionByState[toState];
    spreadValue = fromValue - MAX(lightReduction, reductionOfState);
#ifdef MEASURE_BANDWIDTH
    queue->blockAccessCount++;
#endif

    if (storedValue >= spreadValue) {
        return;
    }

    if (!FindLightCanPropagate(fromState, toState, dir)) {
        return;
    }

    SetSectionLight(queue->lightSections[sectionIndex], posIndex, spreadValue);

    LightQueuePush(queue, PackEntry(toPos));
}

static i32 GetNeighbourIndex(i32 dx, i32 dz) {
    i32 res = ((dz & 0x3) << 2) | (dx & 0x3);
    return res;
}

static void PropagateLightFromNeighbour(LightQueue * queue, Chunk * * chunkGrid, i32 baseX, i32 baseZ, i32 addX, i32 addZ, i32 chunkDx, i32 chunkDz, i32 chunkDir) {
    Chunk * from = chunkGrid[GetNeighbourIndex(chunkDx, chunkDz)];
    if (from == NULL) {
        // NOTE(traks): null chunks have max sky light to prevent propagating
        // into it. Don't propagate that max light out of it!
        return;
    }

    i32 startY = LIGHT_SECTIONS_PER_CHUNK * 16 - 1;
    for (i32 y = startY; y >= 0; y--) {
        i32 x = baseX;
        i32 z = baseZ;
        for (i32 h = 0; h < 16; h++) {
            i32 sectionIndex = ((y & 0x1f0) >> 0) | ((z & 0x30) >> 2) | ((x & 0x30) >> 4);
            i32 posIndex = ((y & 0xf) << 8) | ((z & 0xf) << 4) | (x & 0xf);
            i32 value = GetSectionLight(queue->lightSections[sectionIndex], posIndex);
            i32 fromState = SectionGetBlockState(&queue->blockSections[sectionIndex], posIndex);
            u32 toPos = PosFromXYZ(x - chunkDx, y, z - chunkDz);
            PropagateLight(queue, toPos, get_opposite_direction(chunkDir), fromState, value, 1);
            x += addX;
            z += addZ;
        }
    }
}

static void PropagateMaxSkyLightDown(LightQueue * queue) {
    for (i32 z = 0; z < 16; z++) {
        for (i32 x = 0; x < 16; x++) {
            i32 fromState = 0;
            for (i32 y = 16 * LIGHT_SECTIONS_PER_CHUNK - 1; y >= 0; y--) {
                i32 sectionIndex = XYZToSectionIndex(x, y, z);
                i32 posIndex = ((y & 0xf) << 8) | ((z & 0xf) << 4) | (x & 0xf);
                i32 toState = SectionGetBlockState(&queue->blockSections[sectionIndex], posIndex);
#ifdef MEASURE_BANDWIDTH
                queue->blockAccessCount++;
#endif
                i32 reductionOfState = serv->lightReductionByState[toState];
                if (reductionOfState > 0) {
                    break;
                }
                if (!FindLightCanPropagate(fromState, toState, DIRECTION_NEG_Y)) {
                    break;
                }

                SetSectionLight(queue->lightSections[sectionIndex], posIndex, 15);
                u32 toPos = PosFromXYZ(x, y, z);
                LightQueuePush(queue, PackEntry(toPos));
                fromState = toState;
            }
        }
    }
}

static void PropagateLightFully(LightQueue * queue) {
    i32 readIndex = 0;
    while (readIndex < queue->writeIndex) {
        LightQueueEntry entry = queue->entries[readIndex];
        readIndex++;

        u32 fromPos = GetEntryPos(entry);
        i32 sectionIndex = PosToSectionIndex(fromPos);
        i32 posIndex = PosToSectionPosIndex(fromPos);
        i32 fromState = SectionGetBlockState(&queue->blockSections[sectionIndex], posIndex);
        i32 value = GetSectionLight(queue->lightSections[sectionIndex], posIndex);
#ifdef MEASURE_BANDWIDTH
        queue->blockAccessCount++;
        queue->lightAccessCount++;
#endif

        // TODO(traks): The order in which we propagate light may be important
        // for performance. It shouldn't depend on whatever the order of the
        // direction enum is.
        i32 shift[] = {-0x10000, 0x10000, -0x100, 0x100, -0x1, 0x1};
        PropagateLight(queue, fromPos - 0x10000, DIRECTION_NEG_Y, fromState, value, 1);
        PropagateLight(queue, fromPos + 0x10000, DIRECTION_POS_Y, fromState, value, 1);
        PropagateLight(queue, fromPos - 0x100, DIRECTION_NEG_Z, fromState, value, 1);
        PropagateLight(queue, fromPos + 0x100, DIRECTION_POS_Z, fromState, value, 1);
        PropagateLight(queue, fromPos - 0x1, DIRECTION_NEG_X, fromState, value, 1);
        PropagateLight(queue, fromPos + 0x1, DIRECTION_POS_X, fromState, value, 1);
    }

    // NOTE(traks): reset write index for the next round
    queue->writeIndex = 0;
}

static void DoSkyLight(LightQueue * queue, Chunk * * chunkGrid) {
    BeginTimings(InitSkyLightReferences);

    for (i32 zx = 0; zx < 16; zx++) {
        Chunk * chunk = chunkGrid[zx];
        if (chunk == NULL) {
            continue;
        }
        for (i32 sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
            queue->lightSections[(sectionIndex << 4) | zx] = chunk->lightSections[sectionIndex].skyLight;
        }
    }

    EndTimings(InitSkyLightReferences);

    BeginTimings(PrepareSkyLightSources);
    i64 skyStartTime = NanoTime();
    PropagateMaxSkyLightDown(queue);
    EndTimings(PrepareSkyLightSources);

    BeginTimings(PropagateOwnSkyLight);
    PropagateLightFully(queue);
    EndTimings(PropagateOwnSkyLight);

    BeginTimings(PrepareNeighbourSkyLightSources);
    PropagateLightFromNeighbour(queue, chunkGrid, -1, 0, 0, 1, -1, 0, DIRECTION_NEG_X);
    PropagateLightFromNeighbour(queue, chunkGrid, 16, 0, 0, 1, 1, 0, DIRECTION_POS_X);
    PropagateLightFromNeighbour(queue, chunkGrid, 0, -1, 1, 0, 0, -1, DIRECTION_NEG_Z);
    PropagateLightFromNeighbour(queue, chunkGrid, 0, 16, 1, 0, 0, 1, DIRECTION_POS_Z);
    EndTimings(PrepareNeighbourSkyLightSources);

    BeginTimings(PropagateNeighbourSkyLight);
    PropagateLightFully(queue);
    i64 skyEndTime = NanoTime();
    EndTimings(PropagateNeighbourSkyLight);

#ifdef MEASURE_BANDWIDTH
    LogInfo("[Sky] Bw: %.0fMB/s, Dedup: %.0fMB/s (+Block = %.0f%%, +Light = %.0f%%)",
            (queue->blockAccessCount * 2.0 + queue->lightAccessCount) / (f64) (skyEndTime - skyStartTime) * 1000.0,
            (2 * 4096 * 24 + 4096 * 26) / (f64) (skyEndTime - skyStartTime) * 1000.0,
            100 * queue->blockAccessCount / (f64) (4096 * 24),
            100 * queue->lightAccessCount / (f64) (4096 * 26));
#endif
}

static void DoBlockLight(LightQueue * queue, Chunk * * chunkGrid) {
    BeginTimings(InitBlockLightReferences);

    for (i32 zx = 0; zx < 16; zx++) {
        Chunk * chunk = chunkGrid[zx];
        if (chunk == NULL) {
            continue;
        }
        for (i32 sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
            queue->lightSections[(sectionIndex << 4) | zx] = chunk->lightSections[sectionIndex].blockLight;
        }
    }

    EndTimings(InitBlockLightReferences);

    BeginTimings(PrepareBlockLightSources);
    i64 blockStartTime = NanoTime();

    // NOTE(traks): prepare block light sources for propagation
    for (i32 y = 16; y < 16 + WORLD_HEIGHT; y++) {
        for (i32 zx = 0; zx < 16 * 16; zx++) {
            i32 sectionIndex = (y & 0xff0) | 0;
            i32 posIndex = ((y & 0xf) << 8) | zx;
            i32 blockState = SectionGetBlockState(&queue->blockSections[sectionIndex], posIndex);
#ifdef MEASURE_BANDWIDTH
            queue->blockAccessCount++;
#endif
            i32 emitted = serv->emittedLightByState[blockState];
            if (emitted > 0) {
                SetSectionLight(queue->lightSections[sectionIndex], posIndex, emitted);
                u32 pos = PosFromXYZ(zx & 0xf, y, zx >> 4);
                LightQueuePush(queue, PackEntry(pos));
            }
        }
    }

    EndTimings(PrepareBlockLightSources);

    BeginTimings(PropagateOwnBlockLight);
    PropagateLightFully(queue);
    EndTimings(PropagateOwnBlockLight);

    BeginTimings(PrepareNeighbourBlockLightSources);
    PropagateLightFromNeighbour(queue, chunkGrid, -1, 0, 0, 1, -1, 0, DIRECTION_NEG_X);
    PropagateLightFromNeighbour(queue, chunkGrid, 16, 0, 0, 1, 1, 0, DIRECTION_POS_X);
    PropagateLightFromNeighbour(queue, chunkGrid, 0, -1, 1, 0, 0, -1, DIRECTION_NEG_Z);
    PropagateLightFromNeighbour(queue, chunkGrid, 0, 16, 1, 0, 0, 1, DIRECTION_POS_Z);
    EndTimings(PrepareNeighbourBlockLightSources);

    BeginTimings(PropagateNeighbourBlockLight);
    PropagateLightFully(queue);
    i64 blockEndTime = NanoTime();
    EndTimings(PropagateNeighbourBlockLight);

#ifdef MEASURE_BANDWIDTH
    LogInfo("[Block] Bw: %.0fMB/s, Dedup: %.0fMB/s (+Block = %.0f%%, +Light = %.0f%%)",
            (queue->blockAccessCount * 2.0 + queue->lightAccessCount) / (f64) (blockEndTime - blockStartTime) * 1000.0,
            (2 * 4096 * 24 + 4096 * 26) / (f64) (blockEndTime - blockStartTime) * 1000.0,
            100 * queue->blockAccessCount / (f64) (4096 * 24),
            100 * queue->lightAccessCount / (f64) (4096 * 26));
#endif
}

static void LoadChunkGrid(Chunk * targetChunk, Chunk * * chunkGrid) {
    chunkGrid[0] = targetChunk;
#ifdef MEASURE_BANDWIDTH
    return;
#endif
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
            if (neighbour != NULL && (neighbour->loaderFlags & CHUNK_LOADER_LIT_SELF)) {
                // NOTE(traks): the neighbouring chunk lit itself, so we can
                // exchange light with it
                chunkGrid[index] = neighbour;
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

    LightQueue lightQueue = {0};
    // NOTE(traks): keep the entries array out of the queue struct, so it isn't
    // zero-initialised above. Zero initialising the entry array can be very
    // slow: hundreds of microseconds for 2^20 entries.
    LightQueueEntry allEntries[LIGHT_QUEUE_SIZE];
    lightQueue.entries = allEntries;

    // NOTE(traks): set up section references for easy access
    SectionBlocks sectionAir = {0};
    u8 sectionFullLight[4096];
    memset(sectionFullLight, 0xff, 4096);

    for (i32 i = 0; i < (i32) ARRAY_SIZE(lightQueue.blockSections); i++) {
        lightQueue.blockSections[i] = sectionAir;
        lightQueue.lightSections[i] = sectionFullLight;
    }

    for (i32 zx = 0; zx < 16; zx++) {
        Chunk * chunk = chunkGrid[zx];
        if (chunk == NULL) {
            continue;
        }

        for (i32 sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
            SectionBlocks blocks = chunk->sections[sectionIndex].blocks;
            i32 gridIndex = ((sectionIndex + 1) << 4) | zx;
            lightQueue.blockSections[gridIndex] = blocks;
        }
    }

    EndTimings(InitQueue);

#ifdef MEASURE_BANDWIDTH
    LogInfo("Chunk: %d, %d", targetChunk->pos.x, targetChunk->pos.z);
#endif

    DoSkyLight(&lightQueue, chunkGrid);
#ifdef MEASURE_BANDWIDTH
    lightQueue.blockAccessCount = 0;
    lightQueue.lightAccessCount = 0;
#endif
    DoBlockLight(&lightQueue, chunkGrid);

    EndTimings(LightChunk);
}

void UpdateLighting(void) {
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
