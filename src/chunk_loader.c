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

#define MAX_LOADED_CHUNKS (1024)

#define CHUNK_HASH_IN_USE ((u32) 0x1 << 0)

typedef struct {
    Chunk chunk;
} ChunkHolder;

typedef struct {
    PackedWorldChunkPos pos;
    ChunkHolder * holder;
    u32 flags;
} ChunkHashEntry;

typedef struct {
    ChunkHashEntry * entries;
    // NOTE(traks): must be power of 2
    u32 arraySize;
    u32 sizeMask;
    u32 useCount;
} ChunkHashMap;

static ChunkHashMap chunkIndex;

typedef struct {
    Chunk * chunk;
} ChunkLoadRequest;

static ChunkLoadRequest chunkLoadRequests[MAX_LOADED_CHUNKS];
static i32 chunkLoadRequestWriterIndex;
static i32 chunkLoadRequestReaderIndex;

typedef struct {
    Chunk * chunk;
} ChunkUnloadRequest;

static ChunkUnloadRequest chunkUnloadRequests[MAX_LOADED_CHUNKS];
static i32 chunkUnloadRequestWriterIndex;
static i32 chunkUnloadRequestReaderIndex;

// NOTE(traks): multi thread writer, single thread reader
static Chunk * chunkCompleteRequests[MAX_LOADED_CHUNKS];
static _Atomic i32 chunkCompleteRequestWriterClaiming;
static _Atomic i32 chunkCompleteRequestWriterIndex;
static _Atomic i32 chunkCompleteRequestReaderIndex;

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
    return !(entry->flags & CHUNK_HASH_IN_USE);
}

static ChunkHashEntry * FindChunkHashEntryOrEmpty(PackedWorldChunkPos pos, u32 startIndex) {
    ChunkHashEntry * res = NULL;
    for (u32 offset = 0; offset < chunkIndex.arraySize; offset++) {
        u32 index = (startIndex + offset) & chunkIndex.sizeMask;
        ChunkHashEntry * entry = chunkIndex.entries + index;
        if (ChunkHashEntryIsEmpty(entry) || pos.packed == entry->pos.packed) {
            res = entry;
            break;
        }
    }
    // TODO(traks): crash if it is NULL?
    assert(res != NULL);
    return res;
}

static void GrowChunkHashMap() {
    // NOTE(traks): need a bit of wiggle room for reindexing and so on
    assert(chunkIndex.arraySize < (1 << 28));

    u32 oldSize = chunkIndex.arraySize;
    ChunkHashEntry * oldEntries = chunkIndex.entries;
    chunkIndex.arraySize = MAX(2 * oldSize, 128);
    chunkIndex.sizeMask = chunkIndex.arraySize - 1;
    chunkIndex.entries = calloc(1, chunkIndex.arraySize * sizeof *chunkIndex.entries);

    for (i32 entryIndex = 0; entryIndex < oldSize; entryIndex++) {
        ChunkHashEntry * oldEntry = oldEntries + entryIndex;
        if (!ChunkHashEntryIsEmpty(oldEntry)) {
            ChunkHashEntry * freeEntry = FindChunkHashEntryOrEmpty(oldEntry->pos, HashWorldChunkPos(oldEntry->pos));
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
    for (u32 offset = 1; offset < chunkIndex.arraySize; offset++) {
        u32 curIndex = (chainStart + offset) & chunkIndex.sizeMask;
        ChunkHashEntry * chained = chunkIndex.entries + curIndex;
        if (ChunkHashEntryIsEmpty(chained)) {
            break;
        }
        u32 desiredIndex = HashWorldChunkPos(chained->pos) & chunkIndex.sizeMask;
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
    if (entry == NULL || ChunkHashEntryIsEmpty(entry)) {
        return;
    }
    free(entry->holder);
    RemoveHashEntry(entry);
}

static Chunk * GetOrCreateChunk(WorldChunkPos pos) {
    if (chunkIndex.useCount >= chunkIndex.arraySize / 2) {
        GrowChunkHashMap();
    }

    Chunk * res = NULL;
    PackedWorldChunkPos packedPos = PackWorldChunkPos(pos);
    u32 hash = HashWorldChunkPos(packedPos);
    ChunkHashEntry * entry = FindChunkHashEntryOrEmpty(packedPos, hash);
    if (ChunkHashEntryIsEmpty(entry)) {
        ChunkHolder * holder = calloc(1, sizeof *holder);
        *entry = (ChunkHashEntry) {
            .pos = packedPos,
            .holder = holder,
            .flags = CHUNK_HASH_IN_USE
        };
        chunkIndex.useCount++;
        res = &holder->chunk;
        res->pos = pos;
    } else {
        res = &entry->holder->chunk;
    }
    return res;
}

static void ScheduleLoad(Chunk * chunk) {
    i32 writerIndex = chunkLoadRequestWriterIndex;
    i32 nextWriterIndex = (writerIndex + 1) % ARRAY_SIZE(chunkLoadRequests);
    i32 readerIndex = chunkLoadRequestReaderIndex;
    // TODO(traks): don't fail here
    if (nextWriterIndex != readerIndex && !(chunk->flags & CHUNK_WAS_ON_LOAD_REQUEST_LIST)) {
        // NOTE(traks): never remove chunks that are being loaded, since
        // another thread could be accessing the chunk data
        chunk->flags |= CHUNK_WAS_ON_LOAD_REQUEST_LIST | CHUNK_FORCE_KEEP;
        chunkLoadRequests[writerIndex % ARRAY_SIZE(chunkLoadRequests)] = (ChunkLoadRequest) {chunk};
        chunkLoadRequestWriterIndex = nextWriterIndex;
    }
}

static void ScheduleUnload(Chunk * chunk) {
    i32 writerIndex = chunkUnloadRequestWriterIndex;
    i32 nextWriterIndex = (writerIndex + 1) % ARRAY_SIZE(chunkUnloadRequests);
    i32 readerIndex = chunkUnloadRequestReaderIndex;
    // TODO(traks): don't fail here
    if (nextWriterIndex != readerIndex && !(chunk->flags & CHUNK_WAS_ON_UNLOAD_REQUEST_LIST)) {
        chunk->flags |= CHUNK_WAS_ON_UNLOAD_REQUEST_LIST;
        chunkUnloadRequests[writerIndex % ARRAY_SIZE(chunkUnloadRequests)] = (ChunkUnloadRequest) {chunk};
        chunkUnloadRequestWriterIndex = nextWriterIndex;
    }
}

void AddChunkInterest(WorldChunkPos pos, i32 interest) {
    Chunk * chunk = GetOrCreateChunk(pos);
    // TODO(traks): shouldn't need this...
    assert(chunk != NULL);
    if (chunk != NULL) {
        chunk->interestCount += interest;
        assert(chunk->interestCount >= 0);

        if (chunk->interestCount > 0) {
            ScheduleLoad(chunk);
        } else if (chunk->interestCount == 0) {
            ScheduleUnload(chunk);
        }
    }
}

void PushChunksFinishedLoading(i32 worldId, Chunk * * chunkArray, i32 chunkCount) {
    i32 chunkArrayIndex = 0;
    for (;;) {
        if (chunkArrayIndex >= chunkCount) {
            break;
        }

        Chunk * chunk = chunkArray[chunkArrayIndex];
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
        chunkArrayIndex++;
    }
}

Chunk * GetChunkIfLoaded(WorldChunkPos pos) {
    Chunk * res = NULL;
    PackedWorldChunkPos packedPos = PackWorldChunkPos(pos);
    u32 hash = HashWorldChunkPos(packedPos);
    ChunkHashEntry * entry = FindChunkHashEntryOrEmpty(packedPos, hash);
    if (!ChunkHashEntryIsEmpty(entry)) {
        Chunk * found = &entry->holder->chunk;
        if (found->flags & CHUNK_FINISHED_LOADING) {
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
        res = &entry->holder->chunk;
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
                if (chunk != NULL && (chunk->flags & CHUNK_FINISHED_LOADING)) {
                    chunkArray[(z - from.z) * jumpZ + (x - from.x)] = chunk;
                }
            }
        }
    }
}

void TickChunkLoader(void) {
    for (i32 loops = 0; loops < 64; loops++) {
        i32 readerIndex = chunkUnloadRequestReaderIndex;
        i32 writerIndex = chunkUnloadRequestWriterIndex;
        if (readerIndex == writerIndex) {
            // NOTE(traks): nothing anymore to read
            break;
        }
        i32 nextReaderIndex = (readerIndex + 1) % ARRAY_SIZE(chunkUnloadRequests);
        ChunkUnloadRequest unloadRequest = chunkUnloadRequests[readerIndex];
        Chunk * chunk = unloadRequest.chunk;

        chunkUnloadRequestReaderIndex = nextReaderIndex;

        if ((chunk->flags & CHUNK_FORCE_KEEP) || chunk->interestCount > 0) {
            // NOTE(traks): allow chunk to be rescheduled for unloading
            chunk->flags &= ~CHUNK_WAS_ON_UNLOAD_REQUEST_LIST;
            continue;
        }

        assert(chunk->flags & CHUNK_BLOCKS_LOADED);

        // NOTE(traks): remove the chunk
        for (int sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
            ChunkSection * section = chunk->sections + sectionIndex;
            free(section->blockStates);
        }
        for (int sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
            LightSection * section = chunk->lightSections + sectionIndex;
            free(section->skyLight);
            free(section->blockLight);
        }

        FreeChunk(chunk->pos);
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
    WorldLoadChunk(chunk, &scratchArena);
    free(scratchArena.data);
    PushChunksFinishedLoading(chunk->pos.worldId, &chunk, 1);
}

static void ScheduleLoadTasks() {
    for (i32 loops = 0; loops < 64; loops++) {
        i32 readerIndex = chunkLoadRequestReaderIndex;
        i32 writerIndex = chunkLoadRequestWriterIndex;
        if (readerIndex == writerIndex) {
            // NOTE(traks): nothing anymore to read
            break;
        }
        i32 nextReaderIndex = (readerIndex + 1) % ARRAY_SIZE(chunkLoadRequests);
        ChunkLoadRequest loadRequest = chunkLoadRequests[readerIndex];
        Chunk * chunk = loadRequest.chunk;

        for (i32 sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
            ChunkSection * section = chunk->sections + sectionIndex;
            section->blockStates = calloc(1, 4096 * sizeof *section->blockStates);
        }

        for (i32 sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
            LightSection * section = chunk->lightSections + sectionIndex;
            section->skyLight = calloc(1, 2048);
            section->blockLight = calloc(1, 2048);
        }

        if (!PushTaskToQueue(serv->backgroundQueue, LoadChunkAsync, chunk)) {
            break;
        }

        chunkLoadRequestReaderIndex = nextReaderIndex;
    }
}

void LoadChunks() {
    ScheduleLoadTasks();

    i32 startIndex = atomic_load_explicit(&chunkCompleteRequestReaderIndex, memory_order_relaxed);
    i32 endIndex = atomic_load_explicit(&chunkCompleteRequestWriterIndex, memory_order_acquire);
    for (i32 index = startIndex; index < endIndex; index++) {
        Chunk * chunk = chunkCompleteRequests[index % ARRAY_SIZE(chunkCompleteRequests)];
        atomic_fetch_add_explicit(&chunkCompleteRequestReaderIndex, 1, memory_order_acq_rel);

        MemoryArena scratch_arena = {
            .data = serv->short_lived_scratch,
            .size = serv->short_lived_scratch_size
        };

        // TODO(traks): deal with chunk load failure

        for (i32 sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
            ChunkSection * section = chunk->sections + sectionIndex;
            if (section->nonAirCount == 0) {
                free(section->blockStates);
                section->blockStates = NULL;
            }
        }

        chunk->flags &= ~(CHUNK_FORCE_KEEP);
        chunk->flags |= CHUNK_BLOCKS_LOADED;

        // TODO(traks): consider force loading a 3x3 around this chunk before
        // marking this chunk as fully loaded. And also keep the 3x3 loaded!
        // Keep the 3x3 "invisibly" loaded. The majority of the server code only
        // cares  about chunks that are fully loaded, not chunks that are
        // partially loaded.
        LightChunkAndExchangeWithNeighbours(chunk);

        if (chunk->interestCount == 0) {
            // TODO(traks): we need to reschedule for removal in case someone
            // scheduled for removal before we finished loading the chunk (so
            // the chunk was force loaded and unloads got ignored). Kinda
            // annoying we have to do this. Any better way?
            ScheduleUnload(chunk);
        }
    }
}

#endif
