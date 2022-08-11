#include <stdatomic.h>
#include "shared.h"
#include "nbt.h"
#include "chunk.h"

// @TODO(traks) don't use a hash map. Performance depends on the chunks loaded,
// which depends on the positions of players in the world. Doesn't seem good
// that players moving to certain locations can cause performance drops...
//
// Our goal is 1000 players, so if we construct our hash and hash map in an
// appropriate way, we can ensure there are at most 1000 entries per bucket.
// Not sure if that's any good.

#define MAX_LOADED_CHUNKS (1024)

// TODO(traks): we could also include the ID of the one with interest into the
// chunk hash. Then we can't really run into issues where someone over-releasing
// their interest frees up chunks that others are still interested in. Though to
// be honest, this sounds more useful for debugging, and like massive overhead
// for production servers (441 * 1000 is a lot of entries...)
//
// We can then also immediately add a virtual chunk system. Though we will
// probably need to be able to refer to block data and other stuff separately
// and have everything be copy on write.
//
// There are some pretty interesting things you can do with a virtual chunk
// system. For example, set up duplicate worlds, so staff/builders can modify
// lobbies/games and then sync the changes to the actual world. It's also
// possible to set up per-player zones this way that are fully interactible and
// have minimal memory usage. It automatically copies chunks if stuff is
// modified. Flat worlds and plot worlds also have less memory usage this way.
// Resetting game arenas is also trivial with virtual chunks. And hosting
// multiple instances of the same minigame is also easy (set up mirror worlds).
//
// We could also add salt to whatever ID system we use, so it's harder for
// players to force hash collisions.

// TODO(traks): may be worthwhile to store multiple chunks under one hash. E.g.
// a 4x4 per hash seems good (or even 8x8 so we can fit array of 16-bit
// interestCounts into 1 cache lane). That adds a border of waste around player
// loaded chunks (roughly 0.5-0.65 of the total is wasted) (linear!). However,
// the number of entries is decreased by a factor of 16 (quadratic!).
//
// Might also be a bit more cache friendly, since if you access 1 chunk, you
// probably also want to access some neighbouring chunks.
typedef struct {
    WorldChunkPos pos;
    i32 tableIndex;
    // @TODO(traks): maybe don't memoise this
    i32 hash;
} ChunkHashEntry;

typedef struct ChunkMapEntry ChunkMapEntry;

struct ChunkMapEntry {
    union {
        Chunk chunk;
        ChunkMapEntry * nextFreeListEntry;
    };
    i32 used;
};

static ChunkMapEntry chunkMap[MAX_LOADED_CHUNKS];
static ChunkHashEntry chunkHashes[MAX_LOADED_CHUNKS];
static ChunkMapEntry * chunkFreeList;
static i32 chunkMapMaxUsed;

typedef struct {
    Chunk * chunk;
} ChunkLoadRequest;

// NOTE(traks): single thread writer, multi thread reader
static ChunkLoadRequest chunkLoadRequests[MAX_LOADED_CHUNKS];
static _Atomic i32 chunkLoadRequestWriterIndex;
static _Atomic i32 chunkLoadRequestReaderIndex;

// NOTE(traks): multi thread writer, single thread reader
static Chunk * chunkCompleteRequests[MAX_LOADED_CHUNKS];
static _Atomic i32 chunkCompleteRequestWriterClaiming;
static _Atomic i32 chunkCompleteRequestWriterIndex;
static _Atomic i32 chunkCompleteRequestReaderIndex;

static i32 WorldChunkPosEqual(WorldChunkPos a, WorldChunkPos b) {
    // @TODO(traks) make sure this compiles to a single compare. If not, should
    // probably change chunk_pos to be a uint32_t.
    return a.worldId == b.worldId && a.x == b.x && a.z == b.z;
}

// NOTe(traks): jenkins one at a time
static inline u32 HashU64(u64 key) {
    u8 * bytes = (u8 *) &key;
    i32 length = 8;
    u32 hash = 0;
    for (i32 i = 0; i < length; i++) {
        hash += bytes[i];
        hash += hash << 10;
        hash ^= hash >> 6;
    }
    hash += hash << 3;
    hash ^= hash >> 11;
    hash += hash << 15;
    return hash;
}

static i32 HashWorldChunkPos(WorldChunkPos pos) {
    // NOTE(traks): never return 0 as hash. That means empty hash slot
    u64 key = (((u64) pos.worldId & 0xfff) << 44) | (((u64) pos.x & 0x3fffff) << 22) | ((u64) pos.z & 0x3fffff);
    u32 hash = HashU64(key) | 0x80000000;
    return hash;
}

static i32 ChunkHashEntryIsEmpty(ChunkHashEntry * entry) {
    return entry->hash == 0;
}

static ChunkHashEntry * FindChunkHashEntryOrEmpty(WorldChunkPos pos) {
    i32 hash = HashWorldChunkPos(pos);
    ChunkHashEntry * res = NULL;
    for (i32 offset = 0; offset < ARRAY_SIZE(chunkHashes); offset++) {
        i32 index = MOD(hash + offset, ARRAY_SIZE(chunkHashes));
        res = chunkHashes + index;
        if (ChunkHashEntryIsEmpty(res) || WorldChunkPosEqual(pos, res->pos)) {
            break;
        }
    }
    return res;
}

static ChunkMapEntry * ReserveNextChunkInMap() {
    ChunkMapEntry * res = NULL;
    if (chunkMapMaxUsed < ARRAY_SIZE(chunkMap)) {
        res = chunkMap + chunkMapMaxUsed;
        res->used = 1;
        chunkMapMaxUsed++;
    } else {
        if (chunkFreeList == NULL) {
            res = NULL;
        } else {
            res = chunkFreeList;
            chunkFreeList = chunkFreeList->nextFreeListEntry;
            res->used = 1;
        }
    }
    return res;
}

static void FreeChunkFromMap(ChunkMapEntry * entry) {
    entry->used = 0;
    entry->nextFreeListEntry = chunkFreeList;
    chunkFreeList = entry;
}

static void RemoveHashEntryAt(i32 theIndex) {
    i32 chainStart = theIndex;
    i32 lastSlotToFill = chainStart;
    for (i32 offset = 1; offset < ARRAY_SIZE(chunkHashes); offset++) {
        i32 index = MOD(chainStart + offset, ARRAY_SIZE(chunkHashes));
        ChunkHashEntry * chained = chunkHashes + index;
        if (ChunkHashEntryIsEmpty(chained)) {
            break;
        }
        i32 desiredSlotIndex = MOD(chained->hash, ARRAY_SIZE(chunkHashes));
        // NOTE(traks): handle cases
        // ... lastSlotToFill ... index ...
        // and
        // ... index ... lastSlotToFill ...
        // separately
        i32 isBetween = (lastSlotToFill < index ?
                (lastSlotToFill < desiredSlotIndex && desiredSlotIndex <= index)
                : (lastSlotToFill < desiredSlotIndex || desiredSlotIndex <= index));
        if (!isBetween) {
            // NOTE(traks): move current item to last empty slot
            chunkHashes[lastSlotToFill] = *chained;
            chained->hash = 0;
            lastSlotToFill = index;
        }
    }
}

static Chunk * FindChunk(WorldChunkPos pos) {
    ChunkHashEntry * entry = FindChunkHashEntryOrEmpty(pos);
    if (entry == NULL || ChunkHashEntryIsEmpty(entry)) {
        return NULL;
    }
    Chunk * res = (Chunk *) (chunkMap + entry->tableIndex);
    return res;
}

static void FreeChunk(WorldChunkPos pos) {
    ChunkHashEntry * entry = FindChunkHashEntryOrEmpty(pos);
    if (entry == NULL || ChunkHashEntryIsEmpty(entry)) {
        return;
    }
    FreeChunkFromMap(chunkMap + entry->tableIndex);
    entry->hash = 0;
    i32 theIndex = entry - chunkHashes;
    RemoveHashEntryAt(theIndex);
}

block_entity_base *
try_get_block_entity(WorldBlockPos pos) {
    // @TODO(traks) return some special block entity instead of NULL?
    if (pos.y < MIN_WORLD_Y) {
        return NULL;
    }
    if (pos.y > MAX_WORLD_Y) {
        return NULL;
    }

    WorldChunkPos ch_pos = WorldBlockPosChunk(pos);
    Chunk * ch = GetChunkIfLoaded(ch_pos);
    if (ch == NULL) {
        return NULL;
    }

    CompactChunkBlockPos chunk_block_pos = {
        .x = pos.x & 0xf,
        .y = pos.y,
        .z = pos.z & 0xf,
    };

    for (int i = 0; i < ARRAY_SIZE(ch->block_entities); i++) {
        block_entity_base * block_entity = ch->block_entities + i;
        if (!(block_entity->flags & BLOCK_ENTITY_IN_USE)) {
            block_entity->pos = chunk_block_pos;
            block_entity->type = BLOCK_ENTITY_NULL;
            return block_entity;
        } else if (memcmp(&block_entity->pos, &chunk_block_pos,
                sizeof chunk_block_pos) == 0) {
            return block_entity;
        }
    }
    return NULL;
}

i32 WorldGetBlockState(WorldBlockPos pos) {
    WorldChunkPos ch_pos = WorldBlockPosChunk(pos);
    Chunk * ch = GetChunkIfLoaded(ch_pos);
    if (ch == NULL) {
        return get_default_block_state(BLOCK_UNKNOWN);
    }

    return ChunkGetBlockState(ch, pos.xyz);
}

SetBlockResult WorldSetBlockState(WorldBlockPos pos, i32 blockState) {
    SetBlockResult res = {0};
    WorldChunkPos ch_pos = WorldBlockPosChunk(pos);
    Chunk * ch = GetChunkIfLoaded(ch_pos);
    if (ch == NULL) {
        res.oldState = get_default_block_state(BLOCK_UNKNOWN);
        res.newState = res.oldState;
        res.failed = 1;
        return res;
    }

    res = ChunkSetBlockState(ch, pos.xyz, blockState);
    return res;
}

i32 ChunkGetBlockState(Chunk * ch, BlockPos pos) {
    if (pos.y < MIN_WORLD_Y) {
        return get_default_block_state(BLOCK_VOID_AIR);
    }
    if (pos.y > MAX_WORLD_Y) {
        return get_default_block_state(BLOCK_AIR);
    }

    int sectionIndex = (pos.y - MIN_WORLD_Y) >> 4;
    ChunkSection * section = ch->sections + sectionIndex;

    if (section->nonAirCount == 0) {
        // @TODO(traks) Any way to expand this to a constant at compile time?
        // (Similar for other uses across the entire project.)
        return get_default_block_state(BLOCK_AIR);
    }

    i32 index = SectionPosToIndex((BlockPos) {pos.x & 0xf, pos.y & 0xf, pos.z & 0xf});
    return section->blockStates[index];
}

void ChunkRecalculateMotionBlockingHeightMap(Chunk * ch) {
    // TODO(traks): optimise. Measuring taken during chunk loading while joining
    // server (441 chunks). Average time per chunk:
    //
    // dumb implementation:
    // 233 us
    //
    // by section:
    // 4.35 us
    //
    // skip top sections:
    // 2.85 us

    BeginTimings(RecalculateHeightMap);

    i32 highestSectionWithBlocks = SECTIONS_PER_CHUNK - 1;
    while (highestSectionWithBlocks >= 0) {
        ChunkSection * section = ch->sections + highestSectionWithBlocks;
        if (section->nonAirCount > 0) {
            break;
        }
        highestSectionWithBlocks--;
    }

    for (i32 zx = 0; zx < 16 * 16; zx++) {
        ch->motion_blocking_height_map[zx] = MIN_WORLD_Y;
        for (i32 sectionIndex = highestSectionWithBlocks; sectionIndex >= 0; sectionIndex--) {
            ChunkSection * section = ch->sections + sectionIndex;
            if (section->nonAirCount == 0) {
                continue;
            }

            for (i32 y = 15; y >= 0; y--) {
                i32 blockState = section->blockStates[(y << 8) | zx];
                // @TODO(traks) other airs
                if (blockState != 0) {
                    ch->motion_blocking_height_map[zx] = MIN_WORLD_Y + (sectionIndex << 4) + y + 1;
                    goto finishedZx;
                }
            }
        }
finishedZx:;
    }

    // TODO(traks): this is actually faster than the above (rougly 2x faster for
    // Skygrid). Basically it does 4 columns at the same time.
    /*
    for (i32 zx = 0; zx < 16 * 16; zx += 4) {
        i32 done = 0;
        ch->motion_blocking_height_map[zx + 0] = MIN_WORLD_Y;
        ch->motion_blocking_height_map[zx + 1] = MIN_WORLD_Y;
        ch->motion_blocking_height_map[zx + 2] = MIN_WORLD_Y;
        ch->motion_blocking_height_map[zx + 3] = MIN_WORLD_Y;

        for (i32 sectionIndex = highestSectionWithBlocks; sectionIndex >= 0; sectionIndex--) {
            ChunkSection * section = ch->sections + sectionIndex;
            if (section->nonAirCount == 0) {
                continue;
            }

            u64 * bulkBlockStates = (u64 *) section->blockStates + (zx / 4);
            for (i32 y = 15; y >= 0; y--) {
                u64 blockStateRow = bulkBlockStates[y << 6];
                // @TODO(traks) other airs
                if (blockStateRow != 0) {
                    if (blockStateRow & 0xffffL) {
                        ch->motion_blocking_height_map[zx + 0] = MIN_WORLD_Y + (sectionIndex << 4) + y + 1;
                        done |= (0x1 << 0);
                    }
                    if (blockStateRow & 0xffff0000L) {
                        ch->motion_blocking_height_map[zx + 1] = MIN_WORLD_Y + (sectionIndex << 4) + y + 1;
                        done |= (0x1 << 1);
                    }
                    if (blockStateRow & 0xffff00000000L) {
                        ch->motion_blocking_height_map[zx + 2] = MIN_WORLD_Y + (sectionIndex << 4) + y + 1;
                        done |= (0x1 << 2);
                    }
                    if (blockStateRow & 0xffff000000000000L) {
                        ch->motion_blocking_height_map[zx + 3] = MIN_WORLD_Y + (sectionIndex << 4) + y + 1;
                        done |= (0x1 << 3);
                    }
                    if (done == 0xf) {
                        goto finishedZx;
                    }
                }
            }
        }
finishedZx:;
    }
    */

    EndTimings(RecalculateHeightMap);
}

static inline i32 HashChangedBlockPos(i32 index, i32 hashMask) {
    // @TODO(traks) better hash?
    return index & hashMask;
}

SetBlockResult ChunkSetBlockState(Chunk * ch, BlockPos pos, i32 blockState) {
    SetBlockResult res = {0};
    if (pos.y < MIN_WORLD_Y || pos.y > MAX_WORLD_Y
            || blockState >= serv->vanilla_block_state_count || blockState < 0) {
        assert(0);
        res.oldState = ChunkGetBlockState(ch, pos);
        res.newState = res.oldState;
        res.failed = 1;
        return res;
    }

    // TODO(traks): should we really be checking this? If so, might want to move
    // this to the if-check above
    assert(ch->flags & CHUNK_FINISHED_LOADING);

    // @TODO(traks) somehow ensure this never fails even with tons of players,
    // or make sure we appropriate handle cases in which too many changes occur
    // to a chunk per tick.

    i32 sectionIndex = (pos.y - MIN_WORLD_Y) >> 4;
    ChunkSection * section = ch->sections + sectionIndex;

    if (section->nonAirCount == 0) {
        // TODO(traks): return error if can't allocate chunk
        MemoryPoolAllocation alloc = CallocInPool(serv->sectionPool);
        section->blockStates = alloc.data;
        section->blockStatesBlock = alloc.block;
    }

    i32 index = SectionPosToIndex((BlockPos) {pos.x & 0xf, pos.y & 0xf, pos.z & 0xf});

    // @TODO(traks) also check for cave air and void air? Should probably avoid
    // the == 0 check either way and use block type lookup or property check
    if (section->blockStates[index] == 0) {
        section->nonAirCount++;
    }
    if (blockState == 0) {
        section->nonAirCount--;
    }

    res.oldState = section->blockStates[index];
    res.newState = blockState;

    section->blockStates[index] = blockState;

    if (section->nonAirCount == 0) {
        MemoryPoolAllocation alloc = {.data = section->blockStates, .block = section->blockStatesBlock};
        section->blockStates = NULL;
        section->blockStatesBlock = NULL;
        FreeInPool(serv->sectionPool, alloc);
    }

    // @NOTE(traks) update height map

    i32 height_map_index = ((pos.z & 0xf) << 4) | (pos.x & 0xf);

    i16 max_height = ch->motion_blocking_height_map[height_map_index];
    if (pos.y + 1 == max_height) {
        if (blockState == 0) {
            // @TODO(traks) handle other airs
            max_height = 0;

            for (int lower_y = pos.y - 1; lower_y >= MIN_WORLD_Y; lower_y--) {
                if (ChunkGetBlockState(ch, (BlockPos) {pos.x, lower_y, pos.z}) != 0) {
                    // @TODO(traks) handle other airs
                    max_height = lower_y + 1;
                    break;
                }
            }

            ch->motion_blocking_height_map[height_map_index] = max_height;
        }
    } else if (pos.y >= max_height) {
        if (blockState != 0) {
            // @TODO(traks) handle other airs
            ch->motion_blocking_height_map[height_map_index] = pos.y + 1;
        }
    }

    // @NOTE(traks) update changed block list

    if (section->lastChangeTick != serv->current_tick) {
        section->lastChangeTick = serv->current_tick;
        // @NOTE(traks) must be power of 2
        i32 initialSize = 128;
        section->changedBlockSet = CallocInArena(serv->tickArena, 128 * sizeof *section->changedBlockSet);
        section->changedBlockSetMask = initialSize - 1;
        section->changedBlockCount = 0;
    }

    i32 maxProbe = 4;

doHash:;
    i32 hash = HashChangedBlockPos(index, section->changedBlockSetMask);
    i32 offset = 0;
    for (; offset < maxProbe; offset++) {
        i32 setIndex = (hash + offset) & section->changedBlockSetMask;
        if (section->changedBlockSet[setIndex] == 0) {
            section->changedBlockSet[setIndex] = 0x8000 | index;
            section->changedBlockCount++;
            break;
        } else if (section->changedBlockSet[setIndex] == (0x8000 | index)) {
            break;
        }
    }
    if (offset == maxProbe) {
        // @NOTE(traks) no free position found, grow hash set, rehash and
        // then try inserting the new block pos again
        i32 newSize = 2 * (section->changedBlockSetMask + 1);
        i32 newMask = newSize - 1;
        u16 * newSet = CallocInArena(serv->tickArena, newSize * sizeof *newSet);
        for (i32 i = 0; i < section->changedBlockSetMask + 1; i++) {
            if (section->changedBlockSet[i] != 0) {
                i32 reIndex = section->changedBlockSet[i] & 0x7fff;
                i32 reHash = HashChangedBlockPos(reIndex, newMask);
                for (i32 reOffset = 0; reOffset < maxProbe; reOffset++) {
                    i32 setIndex = (reHash + reOffset) & newMask;
                    if (newSet[setIndex] == 0) {
                        newSet[setIndex] = 0x8000 | reIndex;
                        break;
                    }
                }
            }
        }

        section->changedBlockSet = newSet;
        section->changedBlockSetMask = newMask;

        goto doHash;
    }
    assert(section->changedBlockCount <= 4096);

    return res;
}

static Chunk * GetOrCreateChunk(WorldChunkPos pos) {
    Chunk * res = NULL;
    ChunkHashEntry * entry = FindChunkHashEntryOrEmpty(pos);
    if (entry == NULL) {
        return NULL;
    }
    if (ChunkHashEntryIsEmpty(entry)) {
        ChunkMapEntry * mapEntry = ReserveNextChunkInMap();
        assert(mapEntry != NULL);
        entry->pos = pos;
        entry->tableIndex = mapEntry - chunkMap;
        entry->hash = HashWorldChunkPos(pos);
        res = (Chunk *) mapEntry;
        *res = (Chunk) {0};
        res->pos = pos;

        Chunk * chunk = res;
        for (i32 sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
            ChunkSection * section = chunk->sections + sectionIndex;
            MemoryPoolAllocation alloc = CallocInPool(serv->sectionPool);
            section->blockStates = alloc.data;
            section->blockStatesBlock = alloc.block;
        }

        for (i32 sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
            LightSection * section = chunk->lightSections + sectionIndex;
            MemoryPoolAllocation alloc = CallocInPool(serv->lightingPool);
            section->skyLight = alloc.data;
            section->skyLightBlock = alloc.block;
            alloc = CallocInPool(serv->lightingPool);
            section->blockLight = alloc.data;
            section->blockLightBlock = alloc.block;
        }
    } else {
        res = (Chunk *) (chunkMap + entry->tableIndex);
    }
    return res;
}

void AddChunkInterest(WorldChunkPos pos, i32 interest) {
    Chunk * chunk = GetOrCreateChunk(pos);
    // TODO(traks): shouldn't need this...
    assert(chunk != NULL);
    if (chunk != NULL) {
        chunk->interestCount += interest;
        assert(chunk->interestCount >= 0);

        if (chunk->interestCount > 0) {
            i32 writerIndex = atomic_load_explicit(&chunkLoadRequestWriterIndex, memory_order_relaxed);
            i32 nextWriterIndex = writerIndex + 1;
            i32 readerIndex = atomic_load_explicit(&chunkLoadRequestReaderIndex, memory_order_acquire);
            if (((nextWriterIndex - readerIndex) % ARRAY_SIZE(chunkLoadRequests)) != 0
                    && !(chunk->flags & CHUNK_WAS_ON_LOAD_REQUEST_LIST)) {
                // NOTE(traks): never remove chunks that are being loaded, since
                // another thread could be accessing the chunk data
                chunk->flags |= CHUNK_WAS_ON_LOAD_REQUEST_LIST | CHUNK_FORCE_KEEP;
                chunkLoadRequests[writerIndex % ARRAY_SIZE(chunkLoadRequests)] = (ChunkLoadRequest) {chunk};
                atomic_store_explicit(&chunkLoadRequestWriterIndex, nextWriterIndex, memory_order_release);
            }
        }
    }
}

i32 PopChunksToLoad(i32 worldId, Chunk * * chunkArray, i32 maxChunks) {
    // TODO(traks): don't ignore world ID
    i32 chunkCount = 0;
    for (i32 loops = 0; loops < 64; loops++) {
        if (chunkCount >= maxChunks) {
            break;
        }

        i32 readerIndex = atomic_load_explicit(&chunkLoadRequestReaderIndex, memory_order_acquire);
        i32 writerIndex = atomic_load_explicit(&chunkLoadRequestWriterIndex, memory_order_acquire);
        if (readerIndex == writerIndex) {
            // NOTE(traks): nothing anymore to read
            break;
        }
        i32 nextReaderIndex = readerIndex + 1;
        ChunkLoadRequest loadRequest = chunkLoadRequests[readerIndex % ARRAY_SIZE(chunkLoadRequests)];
        if (atomic_compare_exchange_weak_explicit(&chunkLoadRequestReaderIndex, &readerIndex, nextReaderIndex, memory_order_acq_rel, memory_order_relaxed)) {
            Chunk * chunk = loadRequest.chunk;
            chunkArray[chunkCount] = chunk;
            chunkCount++;
        }
    }
    return chunkCount;
}

void PushChunksFinishedLoading(i32 worldId, Chunk * * chunkArray, i32 chunkCount) {
    i32 chunkIndex = 0;
    for (;;) {
        if (chunkIndex >= chunkCount) {
            break;
        }

        Chunk * chunk = chunkArray[chunkIndex];
        // NOTE(traks): claim our slot
        // TODO(traks): make sure new writer index never equals reader index
        // (limit how many chunks can be in flight at a time)
        i32 writerClaim = atomic_fetch_add_explicit(&chunkCompleteRequestWriterClaiming, 1, memory_order_acq_rel);
        chunkCompleteRequests[writerClaim % ARRAY_SIZE(chunkCompleteRequests)] = chunk;
        for (;;) {
            if (atomic_compare_exchange_weak_explicit(&chunkCompleteRequestWriterIndex, &writerClaim, writerClaim + 1, memory_order_acq_rel, memory_order_relaxed)) {
                break;
            }
        }
        chunkIndex++;
    }
}

Chunk * GetChunkIfLoaded(WorldChunkPos pos) {
    Chunk * res = FindChunk(pos);
    if (res == NULL || !(res->flags & CHUNK_FINISHED_LOADING)) {
        return NULL;
    }
    return res;
}

void
clean_up_unused_chunks(void) {
    for (i32 index = 0; index < ARRAY_SIZE(chunkMap); index++) {
        ChunkMapEntry * mapEntry = chunkMap + index;
        if (mapEntry->used) {
            Chunk * chunk = (Chunk *) mapEntry;

            if (chunk->flags & CHUNK_FORCE_KEEP) {
                continue;
            }

            chunk->local_event_count = 0;

            if (chunk->interestCount == 0) {
                for (int sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
                    ChunkSection * section = chunk->sections + sectionIndex;
                    if (section->blockStates) {
                        MemoryPoolAllocation alloc = {.data = section->blockStates, .block = section->blockStatesBlock};
                        FreeInPool(serv->sectionPool, alloc);
                    }
                }
                for (int sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
                    LightSection * section = chunk->lightSections + sectionIndex;
                    MemoryPoolAllocation skyAlloc = {.data = section->skyLight, .block = section->skyLightBlock};
                    FreeInPool(serv->lightingPool, skyAlloc);
                    MemoryPoolAllocation blockAlloc = {.data = section->blockLight, .block = section->blockLightBlock};
                    FreeInPool(serv->lightingPool, blockAlloc);
                }

                FreeChunk(chunk->pos);
            }
        }
    }
}

void LoadChunks() {
    i32 startIndex = atomic_load_explicit(&chunkCompleteRequestReaderIndex, memory_order_relaxed);
    i32 endIndex = atomic_load_explicit(&chunkCompleteRequestWriterIndex, memory_order_acquire);
    for (i32 index = startIndex; index < endIndex; index++) {
        Chunk * chunk = chunkCompleteRequests[index % ARRAY_SIZE(chunkCompleteRequests)];
        atomic_fetch_add_explicit(&chunkCompleteRequestReaderIndex, 1, memory_order_acq_rel);

        MemoryArena scratch_arena = {
            .data = serv->short_lived_scratch,
            .size = serv->short_lived_scratch_size
        };

        for (i32 sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
            ChunkSection * section = chunk->sections + sectionIndex;
            if (section->nonAirCount == 0) {
                MemoryPoolAllocation alloc = {.data = section->blockStates, .block = section->blockStatesBlock};
                section->blockStates = NULL;
                section->blockStatesBlock = NULL;
                FreeInPool(serv->sectionPool, alloc);
            }
        }

        // TODO(traks): something is wrong here
        if (!(chunk->flags & CHUNK_LIT) || 1) {
            LightChunk(chunk);
            chunk->flags |= CHUNK_LIT;
        }

        chunk->flags &= ~(CHUNK_FORCE_KEEP);
        chunk->flags |= CHUNK_FINISHED_LOADING;
    }
}
