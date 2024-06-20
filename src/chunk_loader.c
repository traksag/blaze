#ifndef CHUNK_LOADER_H
#define CHUNK_LOADER_H

#include <stdatomic.h>
#include <stdlib.h>
#include <sys/mman.h>
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

// TODO(traks): Here's yet another idea. Instead of storing interest per chunk,
// let actors register rectangular regions they have interest in. The chunk
// system will take care of loading all the chunks in all the provided regions.
//
// There are several benefits to such a system. Actors don't need to rate limit
// themselves to prevent overflowing the system. Actors also don't need to track
// which chunks they have loaded to unload them once they lose interest. The
// chunk system knows can be smarter about the load order (e.g. load chunks
// region by region). Storing interest per chunk feels very heavy on the
// management side: if 1000 players load roughly the same set of chunks, that's
// 441 sets player interest with 1000 entries each. Though I'm not sure how the
// proposed alternative would fare in this scenario.
//
// It may be complicated to do specific load orders with such a system though.
// For example, currently we load chunks in a spiral around players. This could
// get painful to implement unless the chunk system has special code for it.
// Perhaps players could expand their region of interest as soon as their
// previous interest region finished loading. Might get a bit hectic though, not
// sure.

// TODO(traks): Ideally we want something like the following:
// - Instead of seeking and reading 1 chunk at a time, it's much much better on
//   HDDs to merge chunk reads into one big sequential read. On my HDD a block
//   size of 8KiB (2 chunk sectors, chunks barely get larger than this in
//   survival world) has a throughput of about 3MiB/s. If I increase the block
//   size by a factor n, the throughput increases roughly by a factor sqrt(n).
//   Thus sqrt(n) times as much data can be read in the same time. This provides
//   a way to determine whether it's faster to merge multiple chunks into a
//   single read vs. reading them all separately. Maybe 500-5000 chunks per tick
//   is reasonable depending on HDD/SDD (if chunks are 2 sectors each).
// - Loading chunks near players is much more important than loading chunks that
//   are further away. Nearby chunks should therefore have a higher priority in
//   case chunk loading can't keep up with the demand. Moreover, progress should
//   always be made in reasonable time for every player in terms of nearby chunk
//   loads. It shouldn't be the case that a player with missing nearby chunks is
//   waiting for chunk loads because another player has faraway chunks that are
//   being loaded.
// - If you're flying around with elytra or in gamemode spectator or ice boat
//   racing, we might need to load chunks preemptively, to decrease the load
//   latency. This can also have the additional benefit that preemptively read
//   chunks can be merged with normally read chunks, which could increase
//   throughput.
// - To allow for all of this to happen, the only solution I see is to supply
//   the chunk loading system with all available information: every player
//   entity should tell the system what chunks it wants and which ones it might
//   require in the future, along with some priority.
// - Based on memory available and configured limits, things like preemtive
//   chunk loads can be restricted to a certain amount of memory.

typedef struct {
    PackedWorldChunkPos packedPos;
    Chunk * chunk;
} ChunkHashEntry;

typedef struct {
    ChunkHashEntry * entries;
    // NOTE(traks): must be power of 2
    i32 arraySize;
    u32 sizeMask;
    i32 useCount;
} ChunkHashMap;

typedef struct {
    PackedWorldChunkPos packedPos;
} ChunkUpdateRequest;

// NOTE(traks): this is a ring buffer
typedef struct {
    ChunkUpdateRequest * entries;
    // NOTE(traks): must be power of 2
    u32 arraySize;
    u32 sizeMask;
    u32 useCount;
    u32 startIndex;
} ChunkUpdateRequestList;

static ChunkHashMap chunkIndex;
static ChunkUpdateRequestList updateRequests;
static _Atomic i64 sectionBlocksMemoryUsage;
static _Atomic i64 sectionLightMemoryUsage;

// NOTE(traks): jenkins one at a time
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

static u32 HashWorldChunkPos(PackedWorldChunkPos pos) {
    u64 key = pos.packed;
    u32 hash = HashU64(key);
    return hash;
}

static i32 ChunkHashEntryIsEmpty(ChunkHashEntry * entry) {
    return entry->packedPos.packed == 0;
}

static ChunkHashEntry * FindChunkHashEntryOrEmpty(PackedWorldChunkPos packedPos, u32 startIndex) {
    ChunkHashEntry * res = NULL;
    for (i32 offset = 0; offset < chunkIndex.arraySize; offset++) {
        i32 index = (startIndex + offset) & chunkIndex.sizeMask;
        ChunkHashEntry * entry = chunkIndex.entries + index;
        if (ChunkHashEntryIsEmpty(entry) || packedPos.packed == entry->packedPos.packed) {
            res = entry;
            break;
        }
    }
    // TODO(traks): crash if it is NULL?
    assert(res != NULL);
    return res;
}

static void GrowChunkHashMap() {
    // NOTE(traks): need a bit of wiggle room for integer operations
    assert(chunkIndex.arraySize < (1 << 20));

    i32 oldSize = chunkIndex.arraySize;
    ChunkHashEntry * oldEntries = chunkIndex.entries;
    chunkIndex.arraySize = MAX(2 * oldSize, 128);
    chunkIndex.sizeMask = chunkIndex.arraySize - 1;
    chunkIndex.entries = calloc(1, chunkIndex.arraySize * sizeof *chunkIndex.entries);

    for (i32 entryIndex = 0; entryIndex < oldSize; entryIndex++) {
        ChunkHashEntry * oldEntry = oldEntries + entryIndex;
        if (!ChunkHashEntryIsEmpty(oldEntry)) {
            ChunkHashEntry * freeEntry = FindChunkHashEntryOrEmpty(oldEntry->packedPos, HashWorldChunkPos(oldEntry->packedPos));
            *freeEntry = *oldEntry;
        }
    }

    free(oldEntries);
}

static void RemoveHashEntry(ChunkHashEntry * entryToRemove) {
    assert(!ChunkHashEntryIsEmpty(entryToRemove));
    chunkIndex.useCount--;
    u32 chainStart = entryToRemove - chunkIndex.entries;
    u32 indexToFill = chainStart;
    chunkIndex.entries[chainStart] = (ChunkHashEntry) {0};
    for (i32 offset = 1; offset < chunkIndex.arraySize; offset++) {
        u32 curIndex = (chainStart + offset) & chunkIndex.sizeMask;
        ChunkHashEntry * chained = chunkIndex.entries + curIndex;
        if (ChunkHashEntryIsEmpty(chained)) {
            break;
        }
        u32 desiredIndex = HashWorldChunkPos(chained->packedPos) & chunkIndex.sizeMask;
        i32 shouldFill = (indexToFill < curIndex ?
                (desiredIndex <= indexToFill || curIndex < desiredIndex)
                : (desiredIndex <= indexToFill && curIndex < desiredIndex));
        if (shouldFill) {
            // NOTE(traks): move current item to the slot we need to fill
            chunkIndex.entries[indexToFill] = *chained;
            *chained = (ChunkHashEntry) {0};
            indexToFill = curIndex;
        }
    }
}

static void FreeChunk(WorldChunkPos pos) {
    PackedWorldChunkPos packedPos = PackWorldChunkPos(pos);
    u32 hash = HashWorldChunkPos(packedPos);
    ChunkHashEntry * entry = FindChunkHashEntryOrEmpty(packedPos, hash);
    assert(!ChunkHashEntryIsEmpty(entry));
    Chunk * chunk = entry->chunk;
    assert(!(chunk->loaderFlags & CHUNK_LOADER_REQUESTING_UPDATE));

    for (int sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
        ChunkSection * section = chunk->sections + sectionIndex;
        FreeAndClearSectionBlocks(&section->blocks);
    }
    for (int sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
        LightSection * section = chunk->lightSections + sectionIndex;
        FreeSectionLight(section->skyLight);
        FreeSectionLight(section->blockLight);
    }

    free(chunk);
    RemoveHashEntry(entry);
}

static void PushUpdateRequest(ChunkHashEntry * entry) {
    assert(!ChunkHashEntryIsEmpty(entry));

    Chunk * chunk = entry->chunk;

    if (chunk->loaderFlags & CHUNK_LOADER_REQUESTING_UPDATE) {
        return;
    }

    chunk->loaderFlags |= CHUNK_LOADER_REQUESTING_UPDATE;

    if (updateRequests.useCount >= updateRequests.arraySize) {
        // NOTE(traks): need a bit of wiggle room for integer operations
        assert(chunkIndex.arraySize < (1 << 20));
        u32 oldSize = updateRequests.arraySize;
        updateRequests.arraySize = MAX(2 * oldSize, 128);
        updateRequests.sizeMask = updateRequests.arraySize - 1;
        updateRequests.entries = reallocf(updateRequests.entries, updateRequests.arraySize * sizeof *updateRequests.entries);
        // NOTE(traks): wrap the start around to the end of the old buffer
        memcpy(updateRequests.entries + oldSize, updateRequests.entries, oldSize * sizeof *updateRequests.entries);
    }

    u32 placementIndex = (updateRequests.startIndex + updateRequests.useCount) & updateRequests.sizeMask;
    updateRequests.entries[placementIndex] = (ChunkUpdateRequest) {
        .packedPos = entry->packedPos,
    };
    updateRequests.useCount++;
}

static ChunkHashEntry * PopUpdateRequest(void) {
    assert(updateRequests.useCount > 0);
    ChunkUpdateRequest request = updateRequests.entries[updateRequests.startIndex];
    updateRequests.startIndex = (updateRequests.startIndex + 1) & updateRequests.sizeMask;
    updateRequests.useCount--;

    ChunkHashEntry * entry = FindChunkHashEntryOrEmpty(request.packedPos, HashWorldChunkPos(request.packedPos));
    assert(!ChunkHashEntryIsEmpty(entry));
    Chunk * chunk = entry->chunk;
    chunk->loaderFlags &= ~CHUNK_LOADER_REQUESTING_UPDATE;
    return entry;
}

static ChunkHashEntry * GetOrCreateChunk(WorldChunkPos pos) {
    if (chunkIndex.useCount >= chunkIndex.arraySize / 2) {
        GrowChunkHashMap();
    }

    PackedWorldChunkPos packedPos = PackWorldChunkPos(pos);
    u32 hash = HashWorldChunkPos(packedPos);
    ChunkHashEntry * entry = FindChunkHashEntryOrEmpty(packedPos, hash);
    if (ChunkHashEntryIsEmpty(entry)) {
        assert(pos.worldId != 0);
        Chunk * chunk = calloc(1, sizeof *chunk);
        *entry = (ChunkHashEntry) {
            .packedPos = packedPos,
            .chunk = chunk,
        };
        chunkIndex.useCount++;
        chunk->pos = pos;
    }
    return entry;
}

void AddChunkInterest(WorldChunkPos pos, i32 interest) {
    for (i32 dx = -1; dx <= 1; dx++) {
        for (i32 dz = -1; dz <= 1; dz++) {
            WorldChunkPos actualPos = pos;
            actualPos.x += dx;
            actualPos.z += dz;
            ChunkHashEntry * entry = GetOrCreateChunk(actualPos);
            Chunk * chunk = entry->chunk;
            if (dx == 0 && dz == 0) {
                chunk->interestCount += interest;
                assert(chunk->interestCount >= 0);
            } else {
                chunk->neighbourInterestCount += interest;
                assert(chunk->neighbourInterestCount >= 0);
            }
            PushUpdateRequest(entry);
        }
    }
}

Chunk * GetChunkIfLoaded(WorldChunkPos pos) {
    Chunk * res = NULL;
    PackedWorldChunkPos packedPos = PackWorldChunkPos(pos);
    u32 hash = HashWorldChunkPos(packedPos);
    ChunkHashEntry * entry = FindChunkHashEntryOrEmpty(packedPos, hash);
    if (!ChunkHashEntryIsEmpty(entry)) {
        Chunk * found = entry->chunk;
        if (found->loaderFlags & CHUNK_LOADER_READY) {
            res = found;
        }
    }
    return res;
}

Chunk * GetChunkInternal(WorldChunkPos pos) {
    Chunk * res = NULL;
    PackedWorldChunkPos packedPos = PackWorldChunkPos(pos);
    u32 hash = HashWorldChunkPos(packedPos);
    ChunkHashEntry * entry = FindChunkHashEntryOrEmpty(packedPos, hash);
    if (!ChunkHashEntryIsEmpty(entry)) {
        res = entry->chunk;
    }
    return res;
}

void CollectLoadedChunks(WorldChunkPos from, WorldChunkPos to, Chunk * * chunkArray) {
    i32 jumpZ = to.x - from.x + 1;
    for (i32 x = from.x; x <= to.x; x++) {
        for (i32 z = from.z; z <= to.z; z++) {
            WorldChunkPos pos = {.worldId = from.worldId, .x = x, .z = z};
            if (from.x <= x && x <= to.x && from.z <= z && z <= to.z) {
                Chunk * chunk = GetChunkIfLoaded(pos);
                chunkArray[(z - from.z) * jumpZ + (x - from.x)] = chunk;
            }
        }
    }
}

static void LoadChunkAsync(void * arg) {
    Chunk * chunk = arg;

    // TODO(traks): turn this into thread local or something?
    i32 scratchSize = 4 * (1 << 20);
    MemoryArena scratchArena = {
        .size = scratchSize,
        .data = malloc(scratchSize)
    };

    for (i32 sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
        LightSection * section = chunk->lightSections + sectionIndex;
        section->skyLight = CallocSectionLight();
        section->blockLight = CallocSectionLight();
    }

    WorldLoadChunk(chunk, &scratchArena);

    free(scratchArena.data);

    atomic_fetch_or_explicit(&chunk->atomicFlags, CHUNK_ATOMIC_FINISHED_LOAD, memory_order_release);
}

static void UpdateChunk(ChunkHashEntry * entry) {
    Chunk * chunk = entry->chunk;
    if (chunk->interestCount == 0 && chunk->neighbourInterestCount == 0) {
        // TODO(traks): might want to keep the entry around for a little while
        // instead of aggressively unloading
        i32 chunkLoading = (chunk->loaderFlags & CHUNK_LOADER_STARTED_LOAD) && !(chunk->loaderFlags & CHUNK_LOADER_FINISHED_LOAD);

        if (!chunkLoading) {
            FreeChunk(UnpackWorldChunkPos(entry->packedPos));
            return;
        }

        // NOTE(traks): can't unload, so try unloading later
        PushUpdateRequest(entry);
    }

    if ((chunk->interestCount > 0 || chunk->neighbourInterestCount > 0) && !(chunk->loaderFlags & CHUNK_LOADER_STARTED_LOAD)) {
        chunk->loaderFlags |= CHUNK_LOADER_STARTED_LOAD;
        PushTaskToQueue(serv->backgroundQueue, LoadChunkAsync, chunk);
    }

    if ((chunk->loaderFlags & CHUNK_LOADER_STARTED_LOAD) && !(chunk->loaderFlags & CHUNK_LOADER_FINISHED_LOAD)) {
        u32 atomicFlags = atomic_load_explicit(&chunk->atomicFlags, memory_order_acquire);
        if (atomicFlags & CHUNK_ATOMIC_FINISHED_LOAD) {
            chunk->loaderFlags |= CHUNK_LOADER_FINISHED_LOAD;
            if (atomicFlags & CHUNK_ATOMIC_LOAD_SUCCESS) {
                chunk->loaderFlags |= CHUNK_LOADER_LOAD_SUCCESS;
            } else {
                // TODO(traks): what to do with the chunk??
                LogInfo("Failed to load chunk");
            }
        } else {
            // NOTE(traks): not yet loaded, poll again later
            PushUpdateRequest(entry);
        }
    }

    if ((chunk->loaderFlags & CHUNK_LOADER_LOAD_SUCCESS) && !(chunk->loaderFlags & CHUNK_LOADER_LIT_SELF)) {
        LightChunkAndExchangeWithNeighbours(chunk);
        chunk->loaderFlags |= CHUNK_LOADER_LIT_SELF;
        // NOTE(traks): Update neighbours and the chunk itself, to check if
        // any are fully ready (fully lit by all neighbours)
        for (i32 dx = -1; dx <= 1; dx++) {
            for (i32 dz = -1; dz <= 1; dz++) {
                WorldChunkPos neighbourPos = UnpackWorldChunkPos(entry->packedPos);
                neighbourPos.x += dx;
                neighbourPos.z += dz;
                PackedWorldChunkPos packedNeighbourPos = PackWorldChunkPos(neighbourPos);
                u32 neighbourHash = HashWorldChunkPos(packedNeighbourPos);
                ChunkHashEntry * neighbourEntry = FindChunkHashEntryOrEmpty(packedNeighbourPos, neighbourHash);
                if (!ChunkHashEntryIsEmpty(neighbourEntry)) {
                    PushUpdateRequest(neighbourEntry);
                }
            }
        }
    }

    if ((chunk->loaderFlags & CHUNK_LOADER_LIT_SELF) && !(chunk->loaderFlags & CHUNK_LOADER_FULLY_LIT)) {
        // NOTE(traks): check if all neighbours have been lit too
        i32 allNeighboursLit = 1;
        for (i32 dx = -1; dx <= 1; dx++) {
            for (i32 dz = -1; dz <= 1; dz++) {
                WorldChunkPos neighbourPos = UnpackWorldChunkPos(entry->packedPos);
                neighbourPos.x += dx;
                neighbourPos.z += dz;
                Chunk * neighbour = GetChunkInternal(neighbourPos);
                if (neighbour == NULL || !(neighbour->loaderFlags & CHUNK_LOADER_LIT_SELF)) {
                    allNeighboursLit = 0;
                    goto checkNeighboursLitEnd;
                }
            }
        }
checkNeighboursLitEnd:
        if (allNeighboursLit) {
            chunk->loaderFlags |= CHUNK_LOADER_FULLY_LIT;
            // TODO(traks): Should we be marking chunks with no interest (only
            // neighbour interest) also as ready?
            chunk->loaderFlags |= CHUNK_LOADER_READY;
        }
    }
}

void TickChunkLoader(void) {
    i32 maxRemainingChunkUpdates = 64;
    while (updateRequests.useCount > 0 && maxRemainingChunkUpdates > 0) {
        ChunkHashEntry * entry = PopUpdateRequest();
        UpdateChunk(entry);
        maxRemainingChunkUpdates--;

        // TODO(traks): Not ideal, but currently we need this because lighting
        // chunks is very laggy
        if (NanoTime() >= serv->currentTickStartNanos + 40000000LL) {
            break;
        }
    }

    if ((serv->current_tick % (10 * 20)) == 0) {
        i64 blocksMemory = atomic_load_explicit(&sectionBlocksMemoryUsage, memory_order_relaxed);
        i64 lightMemory = atomic_load_explicit(&sectionLightMemoryUsage, memory_order_relaxed);
        LogInfo("Section memory usage: %.0fMB (blocks), %.0fMB (light)", blocksMemory / 1000000.0, lightMemory / 1000000.0);
    }
}

SectionBlocks CallocSectionBlocks() {
    SectionBlocks res = {0};
    i32 allocSize = 2 * 4096;
    res.blockStates = calloc(1, allocSize);
    atomic_fetch_add_explicit(&sectionBlocksMemoryUsage, allocSize, memory_order_relaxed);
    return res;
}

void FreeAndClearSectionBlocks(SectionBlocks * blocks) {
    if (!SectionIsNull(blocks)) {
        i32 allocSize = 2 * 4096;
        atomic_fetch_add_explicit(&sectionBlocksMemoryUsage, -allocSize, memory_order_relaxed);
        free(blocks->blockStates);
    }
    *blocks = (SectionBlocks) {0};
}

void * CallocSectionLight() {
    i32 size = 4096;
    void * res = calloc(1, size);
    atomic_fetch_add_explicit(&sectionLightMemoryUsage, size, memory_order_relaxed);
    return res;
}

void FreeSectionLight(void * data) {
    i32 size = 4096;
    free(data);
    if (data != NULL) {
        atomic_fetch_add_explicit(&sectionLightMemoryUsage, -size, memory_order_relaxed);
    }
}

#endif
