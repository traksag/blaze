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
static inline void PropagateLight(LightQueue * queue, i32 fromX, i32 fromY, i32 fromZ, i32 dx, i32 dy, i32 dz, i32 dir, i32 fromState, i32 fromValue, i32 lightReduction) {
    i32 toX = (fromX + dx) & 0x3f;
    i32 toY = (fromY + dy) & 0x1ff;
    i32 toZ = (fromZ + dz) & 0x3f;

    i32 sectionIndex = ((toY & 0x1f0) >> 0) | ((toZ & 0x30) >> 2) | ((toX & 0x30) >> 4);
    i32 posIndex = ((toY & 0xf) << 8) | ((toZ & 0xf) << 4) | (toX & 0xf);
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

    LightQueuePush(queue, (LightQueueEntry) {.x = toX, .y = toY, .z = toZ});
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
        i32 x = 16 * chunkDx + baseX;
        i32 z = 16 * chunkDz + baseZ;
        for (i32 h = 0; h < 16; h++) {
            i32 sectionIndex = ((y & 0x1f0) >> 0) | ((z & 0x30) >> 2) | ((x & 0x30) >> 4);
            i32 posIndex = ((y & 0xf) << 8) | ((z & 0xf) << 4) | (x & 0xf);
            i32 value = GetSectionLight(queue->lightSections[sectionIndex], posIndex);
            i32 fromState = queue->blockSections[sectionIndex][posIndex];
            PropagateLight(queue, x, y, z, -chunkDx, 0, -chunkDz, get_opposite_direction(chunkDir), fromState, value, 1);
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
        i32 fromState = queue->blockSections[sectionIndex][posIndex];
        i32 value = GetSectionLight(queue->lightSections[sectionIndex], posIndex);

        PropagateLight(queue, x, y, z, -1, 0, 0, DIRECTION_NEG_X, fromState, value, 1);
        PropagateLight(queue, x, y, z, 1, 0, 0, DIRECTION_POS_X, fromState, value, 1);
        PropagateLight(queue, x, y, z, 0, 0, -1, DIRECTION_NEG_Z, fromState, value, 1);
        PropagateLight(queue, x, y, z, 0, 0, 1, DIRECTION_POS_Z, fromState, value, 1);
        PropagateLight(queue, x, y, z, 0, -1, 0, DIRECTION_NEG_Y, fromState, value, value == 15 ? 0 : 1);
        PropagateLight(queue, x, y, z, 0, 1, 0, DIRECTION_POS_Y, fromState, value, 1);
    }
}

static void PropagateBlockLightFully(LightQueue * queue) {
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
        i32 fromState = queue->blockSections[sectionIndex][posIndex];
        i32 value = GetSectionLight(queue->lightSections[sectionIndex], posIndex);

        PropagateLight(queue, x, y, z, -1, 0, 0, DIRECTION_NEG_X, fromState, value, 1);
        PropagateLight(queue, x, y, z, 1, 0, 0, DIRECTION_POS_X, fromState, value, 1);
        PropagateLight(queue, x, y, z, 0, 0, -1, DIRECTION_NEG_Z, fromState, value, 1);
        PropagateLight(queue, x, y, z, 0, 0, 1, DIRECTION_POS_Z, fromState, value, 1);
        PropagateLight(queue, x, y, z, 0, -1, 0, DIRECTION_NEG_Y, fromState, value, 1);
        PropagateLight(queue, x, y, z, 0, 1, 0, DIRECTION_POS_Y, fromState, value, 1);
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
    // TODO(traks): this isn't entirely correct. If a corner chunk loads, and
    // then unloads before the side chunks do, we don't actually have its
    // lighting. Also if a corner chunk loads and one side chunk loads, then the
    // corner unloads and the other side chunk unloads.
    for (i32 dz = -1; dz <= 1; dz++) {
        for (i32 dx = -1; dx <= 1; dx++) {
            i32 index = GetNeighbourIndex(dx, dz);
            i32 oppositeIndex = GetNeighbourIndex(-dx, -dz);
            Chunk * neighbour = chunkGrid[index];
            if (neighbour != NULL) {
                Chunk * target = chunkGrid[0];
                target->exchangeLightWithNeighbours |= ((u16) 1 << index);
                neighbour->exchangeLightWithNeighbours |= ((u16) 1 << oppositeIndex);

                // TODO(traks): REALLY weird we set the finished loading flag
                // here. Should be taken care of by the chunk loader. The
                // lighting engine should just light and inform others of the
                // current lighting state.
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
        LightQueuePush(&lightQueue, (LightQueueEntry) {.x = zx & 0xf, .y = y, .z = zx >> 4});
    }

    PropagateSkyLightFully(&lightQueue);

    EndTimings(PropagateOwnSkyLight);

    BeginTimings(PropagateNeighbourSkyLight);

    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 15, 0, 0, 1, -1, 0, DIRECTION_NEG_X);
    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 0, 0, 0, 1, 1, 0, DIRECTION_POS_X);
    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 0, 15, 1, 0, 0, -1, DIRECTION_NEG_Z);
    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 0, 0, 1, 0, 0, 1, DIRECTION_POS_Z);
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
                LightQueuePush(&lightQueue, (LightQueueEntry) {.x = zx & 0xf, .y = y, .z = zx >> 4});
            }
        }
    }

    PropagateBlockLightFully(&lightQueue);

    EndTimings(PropagateOwnBlockLight);

    BeginTimings(PropagateNeighbourBlockLight);

    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 15, 0, 0, 1, -1, 0, DIRECTION_NEG_X);
    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 0, 0, 0, 1, 1, 0, DIRECTION_POS_X);
    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 0, 15, 1, 0, 0, -1, DIRECTION_NEG_Z);
    PropagateLightFromNeighbour(&lightQueue, chunkGrid, 0, 0, 1, 0, 0, 1, DIRECTION_POS_Z);
    PropagateBlockLightFully(&lightQueue);

    EndTimings(PropagateNeighbourBlockLight);

    BeginTimings(MarkLightExchanged);
    MarkLightExchanged(chunkGrid);
    EndTimings(MarkLightExchanged);

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
