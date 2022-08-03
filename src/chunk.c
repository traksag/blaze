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
chunk_bucket chunk_map[CHUNK_MAP_SIZE];

static int
chunk_pos_equal(chunk_pos a, chunk_pos b) {
    // @TODO(traks) make sure this compiles to a single compare. If not, should
    // probably change chunk_pos to be a uint32_t.
    return a.x == b.x && a.z == b.z;
}

static int
hash_chunk_pos(chunk_pos pos) {
    return ((pos.x & 0x1f) << 5) | (pos.z & 0x1f);
}

block_entity_base *
try_get_block_entity(BlockPos pos) {
    // @TODO(traks) return some special block entity instead of NULL?
    if (pos.y < MIN_WORLD_Y) {
        return NULL;
    }
    if (pos.y > MAX_WORLD_Y) {
        return NULL;
    }

    chunk_pos ch_pos = {
        .x = pos.x >> 4,
        .z = pos.z >> 4
    };

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
    chunk_pos ch_pos = {
        .x = pos.x >> 4,
        .z = pos.z >> 4
    };

    Chunk * ch = GetChunkIfLoaded(ch_pos);
    if (ch == NULL) {
        return get_default_block_state(BLOCK_UNKNOWN);
    }

    return ChunkGetBlockState(ch, pos.xyz);
}

SetBlockResult WorldSetBlockState(WorldBlockPos pos, i32 blockState) {
    SetBlockResult res = {0};
    chunk_pos ch_pos = {
        .x = pos.x >> 4,
        .z = pos.z >> 4
    };

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
    // TODO(traks): optimise
    for (int zx = 0; zx < 16 * 16; zx++) {
        ch->motion_blocking_height_map[zx] = 0;
        for (int y = MAX_WORLD_Y; y >= MIN_WORLD_Y; y--) {
            u16 block_state = ChunkGetBlockState(ch, (BlockPos) {zx & 0xf, y, zx >> 4});
            // @TODO(traks) other airs
            if (block_state != 0) {
                ch->motion_blocking_height_map[zx] = y + 1;
                break;
            }
        }
    }
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
    assert(ch->flags & CHUNK_LOADED);

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

Chunk * GetOrCreateChunk(chunk_pos pos) {
    int hash = hash_chunk_pos(pos);
    chunk_bucket * bucket = chunk_map + hash;
    int bucket_size = bucket->size;

    int i;
    for (i = 0; i < bucket_size; i++) {
        if (chunk_pos_equal(bucket->positions[i], pos)) {
            return bucket->chunks + i;
        }
    }

    assert(i < CHUNKS_PER_BUCKET);

    bucket->positions[i] = pos;
    bucket->size++;
    Chunk * ch = bucket->chunks + i;
    *ch = (Chunk) {0};
    return ch;
}

Chunk * GetChunkIfLoaded(chunk_pos pos) {
    int hash = hash_chunk_pos(pos);
    chunk_bucket * bucket = chunk_map + hash;
    int bucket_size = bucket->size;
    Chunk * res = NULL;

    for (int i = 0; i < bucket_size; i++) {
        if (chunk_pos_equal(bucket->positions[i], pos)) {
            Chunk * ch = bucket->chunks + i;
            if (ch->flags & CHUNK_LOADED) {
                res = ch;
            }
            break;
        }
    }

    return res;
}

Chunk * GetChunkIfAvailable(chunk_pos pos) {
    int hash = hash_chunk_pos(pos);
    chunk_bucket * bucket = chunk_map + hash;
    int bucket_size = bucket->size;
    Chunk * res = NULL;

    for (int i = 0; i < bucket_size; i++) {
        if (chunk_pos_equal(bucket->positions[i], pos)) {
            res = bucket->chunks + i;
            break;
        }
    }

    return res;
}

void
clean_up_unused_chunks(void) {
    for (int bucketi = 0; bucketi < ARRAY_SIZE(chunk_map); bucketi++) {
        chunk_bucket * bucket = chunk_map + bucketi;

        for (int chunki = 0; chunki < bucket->size; chunki++) {
            Chunk * ch = bucket->chunks + chunki;
            ch->local_event_count = 0;

            if (ch->available_interest == 0) {
                for (int sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
                    ChunkSection * section = ch->sections + sectionIndex;
                    if (section->nonAirCount != 0) {
                        MemoryPoolAllocation alloc = {.data = section->blockStates, .block = section->blockStatesBlock};
                        FreeInPool(serv->sectionPool, alloc);
                    }
                }
                for (int sectionIndex = 0; sectionIndex < LIGHT_SECTIONS_PER_CHUNK; sectionIndex++) {
                    LightSection * section = ch->lightSections + sectionIndex;
                    MemoryPoolAllocation skyAlloc = {.data = section->skyLight, .block = section->skyLightBlock};
                    FreeInPool(serv->lightingPool, skyAlloc);
                    MemoryPoolAllocation blockAlloc = {.data = section->blockLight, .block = section->blockLightBlock};
                    FreeInPool(serv->lightingPool, blockAlloc);
                }

                int last = bucket->size - 1;
                assert(last >= 0);
                bucket->chunks[chunki] = bucket->chunks[last];
                bucket->positions[chunki] = bucket->positions[last];
                bucket->size--;
                chunki--;
            }
        }
    }
}
