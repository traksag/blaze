#include <stdatomic.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "shared.h"
#include "nbt.h"
#include "chunk.h"

// TODO(traks): may need to add some sort of spacial indexing to this, so we
// don't need to look through all changed chunks to find those in a small region
typedef struct {
    PackedWorldChunkPos * entries;
    u32 arraySize;
} ChangedChunkList;

static ChangedChunkList changedChunks;

static inline void ChunkMarkChanged(Chunk * chunk) {
    if (chunk->lastBlockChangeTick != serv->current_tick) {
        chunk->lastBlockChangeTick = serv->current_tick;
        chunk->changedBlockSections = 0;

        changedChunks.entries[changedChunks.arraySize] = PackWorldChunkPos(chunk->pos);
        changedChunks.arraySize++;
    }
}

static void ClearChangedChunks() {
    changedChunks.arraySize = 0;
}

i32 CollectChangedChunks(WorldChunkPos from, WorldChunkPos to, Chunk * * chunkArray) {
    i32 jumpZ = to.x - from.x + 1;
    i32 count = 0;
    for (u32 i = 0; i < changedChunks.arraySize; i++) {
        PackedWorldChunkPos packedPos = changedChunks.entries[i];
        WorldChunkPos pos = UnpackWorldChunkPos(packedPos);

        if (pos.worldId == from.worldId && from.x <= pos.x && pos.x <= to.x && from.z <= pos.z && pos.z <= to.z) {
            Chunk * chunk = GetChunkIfLoaded(pos);
            if (chunk != NULL) {
                chunkArray[count] = chunk;
                count++;
            }
        }
    }
    return count;
}

void InitChunkSystem() {
    void * changedMem = mmap(NULL, (1 << 20), PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);
    if (changedMem == MAP_FAILED) {
        LogInfo("Failed to map memory for chunks");
        exit(1);
    }
    changedChunks.entries = changedMem;
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

    for (int i = 0; i < (i32) ARRAY_SIZE(ch->block_entities); i++) {
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

void SectionSetBlockState(SectionBlocks * blocks, u32 index, i32 blockState) {
    assert(index <= 0xfff);
    assert(0 <= blockState && blockState < serv->vanilla_block_state_count);

    if (SectionIsNull(blocks)) {
        if (blockState == 0) {
            return;
        }
        *blocks = CallocSectionBlocks();
    }
    blocks->blockStates[index] = blockState;
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
    SectionBlocks * blocks = &ch->sections[sectionIndex].blocks;
    i32 index = SectionPosToIndex((BlockPos) {pos.x & 0xf, pos.y & 0xf, pos.z & 0xf});
    return SectionGetBlockState(blocks, index);
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
                i32 blockState = SectionGetBlockState(&section->blocks, (y << 8) | zx);
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
    assert(ch->loaderFlags & CHUNK_LOADER_READY);

    // @TODO(traks) somehow ensure this never fails even with tons of players,
    // or make sure we appropriate handle cases in which too many changes occur
    // to a chunk per tick.

    i32 sectionIndex = (pos.y - MIN_WORLD_Y) >> 4;
    ChunkSection * section = ch->sections + sectionIndex;
    i32 index = SectionPosToIndex((BlockPos) {pos.x & 0xf, pos.y & 0xf, pos.z & 0xf});

    // @TODO(traks) also check for cave air and void air? Should probably avoid
    // the == 0 check either way and use block type lookup or property check
    i32 oldBlockState = SectionGetBlockState(&section->blocks, index);
    if (oldBlockState == 0) {
        section->nonAirCount++;
    }
    if (blockState == 0) {
        section->nonAirCount--;
    }

    res.oldState = oldBlockState;
    res.newState = blockState;

    SectionSetBlockState(&section->blocks, index, blockState);

    if (section->nonAirCount == 0) {
        FreeAndClearSectionBlocks(&section->blocks);
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

    // NOTE(traks): the tick arena is automatically cleared at the end of each
    // tick, so reallocate memory if we're in a new tick
    ChunkMarkChanged(ch);
    if (!(ch->changedBlockSections & ((u32) 1 << sectionIndex))) {
        ch->changedBlockSections |= (u32) 1 << sectionIndex;
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

void TickChunkSystem(void) {
    ClearChangedChunks();
}
