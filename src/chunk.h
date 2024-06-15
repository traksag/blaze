#ifndef CHUNK_H
#define CHUNK_H

#include "shared.h"

typedef struct {
    // NOTE(traks): index as yzx
    // NOTE(traks): Can possibly be NULL if the entire section is air
    u16 * blockStates;
    u16 * changedBlockSet;
    i32 changedBlockSetMask;
    i32 changedBlockCount;
    u16 nonAirCount;
} ChunkSection;

typedef struct {
    // @OTE(traks): sky light and block light are 4 bits per entry. Currently
    // never NULL, even if all light is 0.
    // NOTE(traks): index as yzx
    u8 * skyLight;
    u8 * blockLight;
} LightSection;

#define CHUNK_ATOMIC_FINISHED_LOAD ((u32) 0x1 << 0)
#define CHUNK_ATOMIC_LOAD_SUCCESS ((u32) 0x1 << 1)

#define CHUNK_LOADER_REQUESTING_UPDATE ((u32) 0x1 << 0)
#define CHUNK_LOADER_FINISHED_LOAD ((u32) 0x1 << 1)
#define CHUNK_LOADER_STARTED_LOAD ((u32) 0x1 << 2)
#define CHUNK_LOADER_GOT_LIGHT ((u32) 0x1 << 3)
#define CHUNK_LOADER_LOAD_SUCCESS ((u32) 0x1 << 4)
#define CHUNK_LOADER_READY ((u32) 0x1 << 5)
#define CHUNK_LOADER_LIT_SELF ((u32) 0x1 << 6)
#define CHUNK_LOADER_FULLY_LIT ((u32) 0x1 << 7)

typedef struct {
    ChunkSection sections[SECTIONS_PER_CHUNK];
    LightSection lightSections[LIGHT_SECTIONS_PER_CHUNK];
    // @NOTE(traks) index as zx
    i16 motion_blocking_height_map[256];

    WorldChunkPos pos;

    i64 lastBlockChangeTick;
    u32 changedBlockSections;

    // @TODO(traks) allow more block entities. Possibly use an internally
    // chained hashmap for this. The question is, where do we allocate this
    // hashmap in? We may need some more general-purpose allocator. Could
    // restrict to allocation sizes of 2^13, 2^12, 2^11, etc. and have separate
    // linked lists for each. Maybe pull blocks from 2^13 list to 2^12 list,
    // from 2^12 list to 2^11, etc. when they need more memory.

    // @TODO(traks) flesh out all this block entity business. What if getting
    // block entity fails? Remove block entities if block gets removed. Load
    // block entities from region files. Send block entities to players. Send
    // block entity updates to players.
    block_entity_base block_entities[10];

    level_event localEvents[64];
    i64 lastLocalEventTick;
    u8 localEventCount;

    // NOTE(traks): Modified by the thread that populates the chunk data
    // asynchronously to communicate with the main thread
    _Atomic u32 atomicFlags;
    // NOTE(traks): This protects access to the chunk while its data is being
    // populated asynchronously. At the moment this should only be touched
    // (read/write) from the main thread.
    u32 loaderFlags;
    // increment if you want to keep a chunk available in the map, decrement
    // if you no longer care for the chunk.
    // If = 0 the chunk will be removed from the map at some point.
    i32 interestCount;
    i32 neighbourInterestCount;
} Chunk;

static inline i32 SectionPosToIndex(BlockPos pos) {
    return (pos.y << 8) | (pos.z << 4) | pos.x;
}

static inline BlockPos SectionIndexToPos(i32 index) {
    BlockPos res = {
        index & 0xf,
        (index >> 8),
        (index >> 4) & 0xf
    };
    return res;
}

static inline WorldChunkPos WorldBlockPosChunk(WorldBlockPos pos) {
    WorldChunkPos res = {
        .worldId = pos.worldId,
        .x = pos.x >> 4,
        .z = pos.z >> 4,
    };
    return res;
}

typedef struct {
    u64 packed;
} PackedWorldChunkPos;

static inline PackedWorldChunkPos PackWorldChunkPos(WorldChunkPos pos) {
    u64 packed = ((u64) (pos.worldId & 0xfff) << 44)
            | ((u64) (pos.x & 0x3fffff) << 22)
            | ((u64) (pos.z & 0x3fffff) << 0);
    PackedWorldChunkPos res = {.packed = packed};
    return res;
}

static inline WorldChunkPos UnpackWorldChunkPos(PackedWorldChunkPos pos) {
    WorldChunkPos res = {
        .worldId = pos.packed >> 44,
        .x = ((i32) (pos.packed >> 22) << 10) >> 10,
        .z = ((i32) (pos.packed >> 0) << 10) >> 10,
    };
    return res;
}

void AddChunkInterest(WorldChunkPos pos, i32 interest);
i32 PopChunksToLoad(i32 worldId, Chunk * * chunkArray, i32 maxChunks);
Chunk * GetChunkIfLoaded(WorldChunkPos pos);
// NOTE(traks): Before accessing the chunk data, be sure to check the chunk's
// flags to see if the chunk data is already available! The data could be in the
// process of being loaded, which will not be fun :(
Chunk * GetChunkInternal(WorldChunkPos pos);
// NOTE(traks): chunkArray will hold the data, may need to zero-initialise it.
// It is indexed as zx
void CollectLoadedChunks(WorldChunkPos from, WorldChunkPos to, Chunk * * chunkArray);
i32 CollectChangedChunks(WorldChunkPos from, WorldChunkPos to, Chunk * * chunkArray);

typedef struct {
    i32 oldState;
    i32 newState;
    i32 failed;
} SetBlockResult;

// NOTE(traks): pos can be in world coordinates instead of chunk coordinates.
// Makes this more convenient to use. Less error conditions = good!
SetBlockResult ChunkSetBlockState(Chunk * ch, BlockPos pos, i32 blockState);
i32 ChunkGetBlockState(Chunk * ch, BlockPos pos);

SetBlockResult WorldSetBlockState(WorldBlockPos pos, i32 blockState);
i32 WorldGetBlockState(WorldBlockPos pos);
void WorldLoadChunk(Chunk * chunk, MemoryArena * scratchArena);

static inline u8 GetSectionLight(u8 * lightArray, i32 posIndex) {
    i32 byteIndex = posIndex / 2;
    i32 shift = (posIndex & 0x1) * 4;
    return (lightArray[byteIndex] >> shift) & 0xf;
}

static inline void SetSectionLight(u8 * lightArray, i32 posIndex, u8 light) {
    i32 byteIndex = posIndex / 2;
    i32 shift = (posIndex & 0x1) * 4;
    i32 mask = 0xf0 >> shift;
    lightArray[byteIndex] = (lightArray[byteIndex] & mask) | (light << shift);
}

// @NOTE(traks) assumes all light sections are present in the chunk and assumes
// all light values are equal to 0
void LightChunk(Chunk * ch);
void LightChunkAndExchangeWithNeighbours(Chunk * targetChunk);

void ChunkRecalculateMotionBlockingHeightMap(Chunk * ch);

void InitChunkSystem(void);
void TickChunkSystem(void);

void TickChunkLoader(void);

void * CallocSectionBlocks(void);
void FreeSectionBlocks(void * data);
void * CallocSectionLight(void);
void FreeSectionLight(void * data);

#endif
