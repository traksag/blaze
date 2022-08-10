#ifndef CHUNK_H
#define CHUNK_H

#include "shared.h"

typedef struct {
    // @NOTE(traks) index as yzx
    // @NOTE(traks) NULL if section is air
    u16 * blockStates;
    MemoryPoolBlock * blockStatesBlock;
    u16 * changedBlockSet;
    i32 changedBlockSetMask;
    i32 changedBlockCount;
    i64 lastChangeTick;
    u16 nonAirCount;
} ChunkSection;

typedef struct {
    // @NOTE(traks) sky light and block light are 4 bits per entry. Currently
    // never NULL, even if all light is 0.
    // @NOTE(traks) index as yzx
    u8 * skyLight;
    MemoryPoolBlock * skyLightBlock;
    u8 * blockLight;
    MemoryPoolBlock * blockLightBlock;
} LightSection;

#define CHUNK_FINISHED_LOADING (0x1 << 0)
#define CHUNK_WAS_ON_LOAD_REQUEST_LIST (0x1 << 1)
#define CHUNK_FORCE_KEEP (0x1 << 3)
#define CHUNK_LIT (0x1 << 4)

typedef struct {
    ChunkSection sections[SECTIONS_PER_CHUNK];
    LightSection lightSections[LIGHT_SECTIONS_PER_CHUNK];
    // @NOTE(traks) index as zx
    i16 motion_blocking_height_map[256];

    // increment if you want to keep a chunk available in the map, decrement
    // if you no longer care for the chunk.
    // If = 0 the chunk will be removed from the map at some point.
    i32 interestCount;
    i32 chunkStatus;
    unsigned flags;

    WorldChunkPos pos;

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

    level_event local_events[64];
    u8 local_event_count;
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

void AddChunkInterest(WorldChunkPos pos, i32 interest);
i32 PopChunksToLoad(i32 worldId, Chunk * * chunkArray, i32 maxChunks);
void PushChunksFinishedLoading(i32 worldId, Chunk * * chunkArray, i32 chunkCount);
Chunk * GetChunkIfLoaded(WorldChunkPos pos);

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

ChunkSection * AllocChunkSection(void);
void FreeChunkSection(ChunkSection * section);

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

void ChunkRecalculateMotionBlockingHeightMap(Chunk * ch);

void LoadChunks();

void InitAnvil();

#endif
