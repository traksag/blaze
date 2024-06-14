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
    // NOTE(traks): Holds:
    // - The position we want to propagate further from. It is represented as
    //   the offset from y = min sky light level (i.e. min world height - 16) in
    //   the lowest x,z corner of the centre chunk.
    // - The direction we propagated from to get here, so we can avoid
    //   propagating backwards.
    //
    // See the conversion functions below for how everything is encoded.
    u32 data;
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

static inline LightQueueEntry PackEntry(u32 pos, u32 dir) {
    u32 data = (dir << 28) | pos;
    LightQueueEntry res = {.data = data};
    return res;
}

static inline u32 GetEntryPos(u32 entryData) {
    return (entryData & 0xfffffff);
}

static inline i32 GetEntryDir(u32 entryData) {
    return (entryData >> 28);
}

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

typedef struct {
    f32 edge;
    f32 minA;
    f32 minB;
    f32 maxA;
    f32 maxB;
} PropagationTest;

static PropagationTest BoxToPropagationTest(BoundingBox box, i32 dir) {
    PropagationTest res = {0};
    switch (dir) {
    case DIRECTION_NEG_Y: res = (PropagationTest) {1 - box.minY, box.minX, box.minZ, box.maxX, box.maxZ};
    case DIRECTION_POS_Y: res = (PropagationTest) {box.maxY, box.minX, box.minZ, box.maxX, box.maxZ};
    case DIRECTION_NEG_Z: res = (PropagationTest) {1 - box.minZ, box.minX, box.minY, box.maxX, box.maxY};
    case DIRECTION_POS_Z: res = (PropagationTest) {box.maxZ, box.minX, box.minY, box.maxX, box.maxY};
    case DIRECTION_NEG_X: res = (PropagationTest) {1 - box.minX, box.minY, box.minZ, box.maxY, box.maxZ};
    case DIRECTION_POS_X: res = (PropagationTest) {box.maxX, box.minY, box.minZ, box.maxY, box.maxZ};
    }
    return res;
}

// TODO(traks): this stuff should ideally be in blockinfo.h
// TODO(traks): most block models are not used for light propagation, because
// most blocks have an empty light blocking model. We can probably just
// precompute all this and dump it into a table for each from block + to block
// + direction combination. Resulting table will likely be small.
static i32 BlockLightCanPropagate(i32 fromState, i32 toState, i32 dir) {
    BlockModel fromModel = serv->staticBlockModels[serv->lightBlockingModelByState[fromState]];
    BlockModel toModel = serv->staticBlockModels[serv->lightBlockingModelByState[toState]];

    i32 testCount = 0;
    PropagationTest tests[16];

    for (i32 i = 0; i < fromModel.size; i++) {
        PropagationTest test = BoxToPropagationTest(fromModel.boxes[i], dir);
        if (test.edge >= 1) {
            tests[testCount++] = test;
        }
    }
    for (i32 i = 0; i < toModel.size; i++) {
        PropagationTest test = BoxToPropagationTest(fromModel.boxes[i], get_opposite_direction(dir));
        if (test.edge >= 1) {
            tests[testCount++] = test;
        }
    }

    f32 curA = 0;
    for (;;) {
        f32 curB = 0;
        f32 nextA = curA;
        for (i32 testIndex = 0; testIndex < testCount; testIndex++) {
            PropagationTest test = tests[testIndex];
            if (test.minA <= curA && test.maxA >= curA && test.minB <= curB && test.maxB >= curB) {
                curB = test.maxB;
                nextA = MIN(nextA, test.maxA);
            }
        }
        if (nextA == curA) {
            break;
        }
    }

    return (curA != 1);
}

// NOTE(traks): update a neighbour's light and push the neighbour to the
// queue if further propagation is necessary
static inline void PropagateLight(LightQueue * queue, u32 toPos, i32 dir, i32 fromState, i32 fromValue, i32 lightReduction) {
    i32 sectionIndex = PosToSectionIndex(toPos);
    i32 posIndex = PosToSectionPosIndex(toPos);
    i32 storedValue = GetSectionLight(queue->lightSections[sectionIndex], posIndex);
    i32 spreadValue = fromValue - lightReduction;
    // NOTE(traks): final value is only going to be less than the spread value.
    // Early exit to avoid the block lookup (likely cache miss).
    // NOTE(traks): also important to avoid propagating into unloaded chunks or
    // invalid sections. These are full air and have max light.
    if (storedValue >= spreadValue) {
        return;
    }

    i32 toState = queue->blockSections[sectionIndex][posIndex];
    i32 reductionOfState = serv->lightReductionByState[toState];
    spreadValue = fromValue - MAX(lightReduction, reductionOfState);

    if (storedValue >= spreadValue) {
        return;
    }

    if (!BlockLightCanPropagate(fromState, toState, dir)) {
        return;
    }

    SetSectionLight(queue->lightSections[sectionIndex], posIndex, spreadValue);

    LightQueuePush(queue, PackEntry(toPos, get_opposite_direction(dir)));
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
            i32 fromState = queue->blockSections[sectionIndex][posIndex];
            u32 toPos = PosFromXYZ(x - chunkDx, y, z - chunkDz);
            PropagateLight(queue, toPos, get_opposite_direction(chunkDir), fromState, value, 1);
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

        i32 sectionIndex = PosToSectionIndex(entry.data);
        i32 posIndex = PosToSectionPosIndex(entry.data);
        i32 fromState = queue->blockSections[sectionIndex][posIndex];
        i32 value = GetSectionLight(queue->lightSections[sectionIndex], posIndex);

        // TODO(traks): The order in which we propagate light may be important
        // for performance. It shouldn't depend on whatever the order of the
        // direction enum is.
        i32 lightReduction[] = {(value == 15 ? 0 : 1), 1, 1, 1, 1, 1};
        i32 shift[] = {-0x10000, 0x10000, -0x100, 0x100, -0x1, 0x1};
        u32 pos = GetEntryPos(entry.data);
        u32 fromDir = GetEntryDir(entry.data);
        for (i32 dir = 0; dir < 6; dir++) {
            if (dir != fromDir) {
                PropagateLight(queue, pos + shift[dir], dir, fromState, value, lightReduction[dir]);
            }
        }
    }
}

static void PropagateBlockLightFully(LightQueue * queue) {
    for (;;) {
        if (queue->readIndex == queue->writeIndex) {
            break;
        }

        LightQueueEntry entry = LightQueuePop(queue);

        i32 sectionIndex = PosToSectionIndex(entry.data);
        i32 posIndex = PosToSectionPosIndex(entry.data);
        i32 fromState = queue->blockSections[sectionIndex][posIndex];
        i32 value = GetSectionLight(queue->lightSections[sectionIndex], posIndex);

        // TODO(traks): The order in which we propagate light may be important
        // for performance. It shouldn't depend on whatever the order of the
        // direction enum is.
        i32 shift[] = {-0x10000, 0x10000, -0x100, 0x100, -0x1, 0x1};
        u32 pos = GetEntryPos(entry.data);
        u32 fromDir = GetEntryDir(entry.data);
        for (i32 dir = 0; dir < 6; dir++) {
            if (dir != fromDir) {
                PropagateLight(queue, pos + shift[dir], dir, fromState, value, 1);
            }
        }
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
    u16 sectionAir[4096] = {0};
    u8 sectionFullLight[2048];
    memset(sectionFullLight, 0xff, 2048);

    for (i32 i = 0; i < ARRAY_SIZE(lightQueue.blockSections); i++) {
        lightQueue.blockSections[i] = sectionAir;
        lightQueue.lightSections[i] = sectionFullLight;
    }

    for (i32 zx = 0; zx < 16; zx++) {
        Chunk * chunk = chunkGrid[zx];
        if (chunk == NULL) {
            continue;
        }

        for (i32 sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
            u16 * blockStates = chunk->sections[sectionIndex].blockStates;
            if (blockStates == NULL) {
                continue;
            }
            i32 gridIndex = ((sectionIndex + 1) << 4) | zx;
            lightQueue.blockSections[gridIndex] = blockStates;
        }
    }

    EndTimings(InitQueue);

    BeginTimings(InitSkyLightReferences);

    for (i32 zx = 0; zx < 16; zx++) {
        Chunk * chunk = chunkGrid[zx];
        if (chunk == NULL) {
            continue;
        }
        for (i32 sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
            lightQueue.lightSections[(sectionIndex << 4) | zx] = chunk->lightSections[sectionIndex].skyLight;
        }
    }

    EndTimings(InitSkyLightReferences);

    BeginTimings(PropagateOwnSkyLight);

    // NOTE(traks): prepare sky light sources for propagation
    for (i32 zx = 0; zx < 16 * 16; zx++) {
        i32 y = (MAX_WORLD_Y - MIN_WORLD_Y + 1) + 16 + 16;
        u32 pos = PosFromXYZ(zx & 0xf, y, zx >> 4);
        LightQueuePush(&lightQueue, PackEntry(pos, DIRECTION_POS_Y));
    }

    PropagateSkyLightFully(&lightQueue);

    EndTimings(PropagateOwnSkyLight);

    BeginTimings(PropagateNeighbourSkyLight);

    PropagateLightFromNeighbour(&lightQueue, chunkGrid, -1, 0, 0, 1, -1, 0, DIRECTION_NEG_X);
    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 16, 0, 0, 1, 1, 0, DIRECTION_POS_X);
    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 0, -1, 1, 0, 0, -1, DIRECTION_NEG_Z);
    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 0, 16, 1, 0, 0, 1, DIRECTION_POS_Z);
    PropagateSkyLightFully(&lightQueue);

    EndTimings(PropagateNeighbourSkyLight);

    BeginTimings(InitBlockLightReferences);

    for (i32 zx = 0; zx < 16; zx++) {
        Chunk * chunk = chunkGrid[zx];
        if (chunk == NULL) {
            continue;
        }
        for (i32 sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
            lightQueue.lightSections[(sectionIndex << 4) | zx] = chunk->lightSections[sectionIndex].blockLight;
        }
    }

    EndTimings(InitBlockLightReferences);

    BeginTimings(PropagateOwnBlockLight);

    // NOTE(traks): prepare block light sources for propagation
    for (i32 y = 16; y < 16 + WORLD_HEIGHT; y++) {
        for (i32 zx = 0; zx < 16 * 16; zx++) {
            i32 sectionIndex = (y & 0xff0) | 0;
            i32 posIndex = ((y & 0xf) << 8) | zx;
            i32 blockState = lightQueue.blockSections[sectionIndex][posIndex];
            i32 emitted = serv->emittedLightByState[blockState];
            if (emitted > 0) {
                SetSectionLight(lightQueue.lightSections[sectionIndex], posIndex, emitted);
                u32 pos = PosFromXYZ(zx & 0xf, y, zx >> 4);
                LightQueuePush(&lightQueue, PackEntry(pos, DIRECTION_ZERO));
            }
        }
    }

    PropagateBlockLightFully(&lightQueue);

    EndTimings(PropagateOwnBlockLight);

    BeginTimings(PropagateNeighbourBlockLight);

    PropagateLightFromNeighbour(&lightQueue, chunkGrid, -1, 0, 0, 1, -1, 0, DIRECTION_NEG_X);
    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 16, 0, 0, 1, 1, 0, DIRECTION_POS_X);
    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 0, -1, 1, 0, 0, -1, DIRECTION_NEG_Z);
    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 0, 16, 1, 0, 0, 1, DIRECTION_POS_Z);
    PropagateBlockLightFully(&lightQueue);

    EndTimings(PropagateNeighbourBlockLight);

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
