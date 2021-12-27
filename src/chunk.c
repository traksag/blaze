#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <zlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include "shared.h"
#include "nbt.h"

// @TODO(traks) don't use a hash map. Performance depends on the chunks loaded,
// which depends on the positions of players in the world. Doesn't seem good
// that players moving to certain locations can cause performance drops...
//
// Our goal is 1000 players, so if we construct our hash and hash map in an
// appropriate way, we can ensure there are at most 1000 entries per bucket.
// Not sure if that's any good.
static chunk_bucket chunk_map[CHUNK_MAP_SIZE];

ChunkSection * AllocChunkSection() {
    MemoryPoolAllocation alloc = CallocInPool(serv->sectionPool);
    ChunkSection * res = alloc.data;
    res->block = alloc.block;
    return res;
}

void FreeChunkSection(ChunkSection * section) {
    MemoryPoolAllocation alloc = {.block = section->block, .data = section};
    FreeInPool(serv->sectionPool, alloc);
}

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

    chunk * ch = get_chunk_if_loaded(ch_pos);
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

u16
try_get_block_state(BlockPos pos) {
    if (pos.y < MIN_WORLD_Y) {
        return get_default_block_state(BLOCK_VOID_AIR);
    }
    if (pos.y > MAX_WORLD_Y) {
        return get_default_block_state(BLOCK_AIR);
    }

    chunk_pos ch_pos = {
        .x = pos.x >> 4,
        .z = pos.z >> 4
    };

    chunk * ch = get_chunk_if_loaded(ch_pos);
    if (ch == NULL) {
        return get_default_block_state(BLOCK_UNKNOWN);
    }

    return chunk_get_block_state(ch, pos.x & 0xf, pos.y, pos.z & 0xf);
}

void
try_set_block_state(BlockPos pos, u16 block_state) {
    if (pos.y < MIN_WORLD_Y) {
        assert(0);
        return;
    }
    if (pos.y > MAX_WORLD_Y) {
        assert(0);
        return;
    }
    if (block_state >= serv->vanilla_block_state_count) {
        // catches unknown blocks
        assert(0);
        return;
    }

    chunk_pos ch_pos = {
        .x = pos.x >> 4,
        .z = pos.z >> 4
    };

    chunk * ch = get_chunk_if_loaded(ch_pos);
    if (ch == NULL) {
        return;
    }

    return chunk_set_block_state(ch, pos.x & 0xf, pos.y, pos.z & 0xf, block_state);
}

u16
chunk_get_block_state(chunk * ch, int x, int y, int z) {
    assert(0 <= x && x < 16);
    assert(MIN_WORLD_Y <= y && y <= MAX_WORLD_Y);
    assert(0 <= z && z < 16);

    int sectionIndex = (y - MIN_WORLD_Y) >> 4;
    ChunkSection * section = ch->sections[sectionIndex];

    if (section == NULL) {
        // @TODO(traks) would it be possible to have a block type -> default state
        // lookup here and have it expand to a constant once we pregenerate all
        // the data tables?
        return 0;
    }

    int index = ((y & 0xf) << 8) | (z << 4) | x;
    return section->blockStates[index];
}

static void
recalculate_chunk_motion_blocking_height_map(chunk * ch) {
    for (int zx = 0; zx < 16 * 16; zx++) {
        ch->motion_blocking_height_map[zx] = 0;
        for (int y = MAX_WORLD_Y; y >= MIN_WORLD_Y; y--) {
            u16 block_state = chunk_get_block_state(ch, zx & 0xf, y, zx >> 4);
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

void
chunk_set_block_state(chunk * ch, int x, int y, int z, u16 block_state) {
    assert(0 <= x && x < 16);
    assert(MIN_WORLD_Y <= y && y <= MAX_WORLD_Y);
    assert(0 <= z && z < 16);
    assert(ch->flags & CHUNK_LOADED);
    // @TODO(traks) somehow ensure this never fails even with tons of players,
    // or make sure we appropriate handle cases in which too many changes occur
    // to a chunk per tick.

    int sectionIndex = (y - MIN_WORLD_Y) >> 4;
    ChunkSection * section = ch->sections[sectionIndex];

    if (section == NULL) {
        // @TODO(traks) instead of making block setting fallible, perhaps
        // getting the chunk should fail if chunk sections cannot be allocated
        section = AllocChunkSection();
        if (section == NULL) {
            LogErrno("Failed to allocate section: %s");
            exit(1);
        }
        ch->sections[sectionIndex] = section;
    }

    int index = SectionPosToIndex((BlockPos) {x, y, z});

    if (section->blockStates[index] == 0) {
        ch->non_air_count[sectionIndex]++;
    }
    if (block_state == 0) {
        ch->non_air_count[sectionIndex]--;
    }

    section->blockStates[index] = block_state;

    // @NOTE(traks) update height map

    int height_map_index = (z << 4) | x;

    i16 max_height = ch->motion_blocking_height_map[height_map_index];
    if (y + 1 == max_height) {
        if (block_state == 0) {
            // @TODO(traks) handle other airs
            max_height = 0;

            for (int lower_y = y - 1; lower_y >= 0; lower_y--) {
                if (chunk_get_block_state(ch, x, lower_y, z) != 0) {
                    // @TODO(traks) handle other airs
                    max_height = lower_y + 1;
                }
            }

            ch->motion_blocking_height_map[height_map_index] = max_height;
        }
    } else if (y >= max_height) {
        if (block_state != 0) {
            // @TODO(traks) handle other airs
            ch->motion_blocking_height_map[height_map_index] = y + 1;
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
}

static void
fill_buffer_from_file(int fd, BufCursor * cursor) {
    int start_index = cursor->index;

    while (cursor->index < cursor->size) {
        int bytes_read = read(fd, cursor->data + cursor->index,
                cursor->size - cursor->index);
        if (bytes_read == -1) {
            LogErrno("Failed to read region file: %s");
            cursor->error = 1;
            break;
        }
        if (bytes_read == 0) {
            LogInfo("Wanted %d bytes from region file, but got %d",
                    cursor->size - start_index,
                    cursor->index - start_index);
            cursor->error = 1;
            break;
        }

        cursor->index += bytes_read;
    }

    cursor->size = cursor->index;
    cursor->index = start_index;
}

void
try_read_chunk_from_storage(chunk_pos pos, chunk * ch,
        MemoryArena * scratch_arena) {
    BeginTimedZone("read chunk");

    // @TODO(traks) error handling and/or error messages for all failure cases
    // in this entire function?

    int region_x = pos.x >> 5;
    int region_z = pos.z >> 5;

    unsigned char file_name[64];
    int file_name_size = sprintf((void *) file_name,
            "world/region/r.%d.%d.mca", region_x, region_z);

    int region_fd = open((void *) file_name, O_RDONLY);
    if (region_fd == -1) {
        LogErrno("Failed to open region file: %s");
        goto bail;
    }

    struct stat region_stat;
    if (fstat(region_fd, &region_stat)) {
        LogErrno("Failed to get region file stat: %s");
        goto bail;
    }

    BufCursor header_cursor = {
        .data = MallocInArena(scratch_arena, 4096),
        .size = 4096
    };
    fill_buffer_from_file(region_fd, &header_cursor);
    if (header_cursor.error) {
        goto bail;
    }

    // First read from the chunk location table at which sector (4096 byte
    // block) the chunk data starts.
    int index = ((pos.z & 0x1f) << 5) | (pos.x & 0x1f);
    header_cursor.index = index << 2;
    u32 loc = CursorGetU32(&header_cursor);

    if (loc == 0) {
        // chunk not present in region file
        goto bail;
    }

    u32 sector_offset = loc >> 8;
    u32 sector_count = loc & 0xff;

    if (sector_offset < 2) {
        LogInfo("Chunk data in header");
        goto bail;
    }
    if (sector_count == 0) {
        LogInfo("Chunk data uses 0 sectors");
        goto bail;
    }
    if (((sector_offset + sector_count) << 12) > region_stat.st_size) {
        LogInfo("Chunk data out of bounds");
        goto bail;
    }

    if (lseek(region_fd, sector_offset << 12, SEEK_SET) == -1) {
        LogErrno("Failed to seek to chunk data: %s");
        goto bail;
    }

    BufCursor cursor = {
        .data = MallocInArena(scratch_arena, sector_count << 12),
        .size = sector_count << 12
    };
    fill_buffer_from_file(region_fd, &cursor);

    u32 size_in_bytes = CursorGetU32(&cursor);

    if (size_in_bytes > cursor.size - cursor.index) {
        LogInfo("Chunk data outside of its sectors");
        goto bail;
    }

    cursor.size = cursor.index + size_in_bytes;
    u8 storage_type = CursorGetU8(&cursor);

    if (cursor.error) {
        LogInfo("Chunk header reading error");
        goto bail;
    }

    if (storage_type & 0x80) {
        // @TODO(traks) separate file is used to store the chunk
        LogInfo("External chunk storage");
        goto bail;
    }

    int windowBits;

    if (storage_type == 1) {
        // gzip compressed
        // 16 means: gzip stream and determine window size from header
        windowBits = 16;
    } else if (storage_type == 2) {
        // zlib compressed
        // 0 means: zlib stream and determine window size from header
        windowBits = 0;
    } else {
        LogInfo("Unknown chunk compression method");
        goto bail;
    }

    BeginTimedZone("inflate");
    // @TODO(traks) perhaps use https://github.com/ebiggers/libdeflate instead
    // of zlib. Using zlib now just because I had the code for it laying around.
    // If we don't end up doing this, make sure the code below is actually
    // correct (when do we need to clean stuff up?)!
    z_stream zstream;
    zstream.zalloc = Z_NULL;
    zstream.zfree = Z_NULL;
    zstream.opaque = Z_NULL;

    if (inflateInit2(&zstream, windowBits) != Z_OK) {
        LogInfo("inflateInit failed");
        EndTimedZone();
        goto bail;
    }

    zstream.next_in = cursor.data + cursor.index;
    zstream.avail_in = cursor.size - cursor.index;

    // @TODO(traks) can be many many times larger in case of e.g. NBT data with
    // tons and tons of empty lists.
    size_t max_uncompressed_size = 2 * (1 << 20);
    unsigned char * uncompressed = MallocInArena(scratch_arena,
            max_uncompressed_size);

    zstream.next_out = uncompressed;
    zstream.avail_out = max_uncompressed_size;

    int inflate_status = inflate(&zstream, Z_FINISH);
    switch (inflate_status) {
    case Z_STREAM_END:
        // all good
        break;
    case Z_NEED_DICT:
    case Z_STREAM_ERROR:
        LogInfo("Chunk inflate stream error");
        break;
    case Z_DATA_ERROR:
        LogInfo("Failed to finish inflating chunk: %s", zstream.msg);
        break;
    case Z_MEM_ERROR:
        LogInfo("Chunk inflate not enough memory");
        break;
    case Z_BUF_ERROR:
    case Z_OK:
        LogInfo("Uncompressed chunk size too large");
        break;
    }

    if (inflateEnd(&zstream) != Z_OK) {
        LogInfo("inflateEnd failed");
        EndTimedZone();
        goto bail;
    }

    // bail in case of any errors above
    if (zstream.avail_in != 0) {
        EndTimedZone();
        goto bail;
    }
    EndTimedZone();

    cursor = (BufCursor) {
        .data = uncompressed,
        .size = zstream.total_out
    };

    NbtCompound chunkNbt = NbtRead(&cursor, scratch_arena);

    if (cursor.error) {
        LogInfo("Failed to load NBT data");
        goto bail;
    }

    // NbtPrint(&chunkNbt);

    for (int section_y = 0; section_y < SECTIONS_PER_CHUNK; section_y++) {
        assert(ch->sections[section_y] == NULL);
    }

    i32 dataVersion = NbtGetU32(&chunkNbt, STR("DataVersion"));
    if (dataVersion != SERVER_WORLD_VERSION) {
        LogInfo("Data version %jd != %jd", (intmax_t) dataVersion, (intmax_t) SERVER_WORLD_VERSION);
        goto bail;
    }

    String status = NbtGetString(&chunkNbt, STR("Status"));
    if (!net_string_equal(status, STR("full")) && !net_string_equal(status, STR("empty"))) {
        // @TODO(traks) this message gets spammed on the edges of pregenerated
        // terrain. Maybe turn it into a debug message.
        LogInfo("Chunk not fully generated, status: %.*s", status.size, status.data);
        goto bail;
    }

    NbtList sectionList = NbtGetList(&chunkNbt, STR("sections"), NBT_COMPOUND);
    i32 numSections = sectionList.size;

    // maximum amount of memory the palette will ever use
    int max_palette_map_size = 4096;
    u16 * palette_map = MallocInArena(scratch_arena, max_palette_map_size * sizeof (u16));

    if (numSections > SECTIONS_PER_CHUNK) {
        LogInfo("Too many chunk sections: %ju", (uintmax_t) numSections);
        goto bail;
    }

    for (u32 sectioni = 0; sectioni < numSections; sectioni++) {
        NbtCompound sectionNbt = NbtNextCompound(&sectionList);
        i8 sectionY = NbtGetU8(&sectionNbt, STR("Y"));

        NbtCompound blockStatesNbt = NbtGetCompound(&sectionNbt, STR("block_states"));
        NbtList palette = NbtGetList(&blockStatesNbt, STR("palette"), NBT_COMPOUND);
        NbtList blockData = NbtGetArrayU64(&blockStatesNbt, STR("data"));

        if (palette.size > 0 && blockData.size > 0) {
            if (sectionY < MIN_SECTION || sectionY > MAX_SECTION) {
                LogInfo("Section Y %d with palette", (int) sectionY);
                goto bail;
            }

            i32 sectionIndex = sectionY - MIN_SECTION;

            if (ch->sections[sectionIndex] != NULL) {
                LogInfo("Duplicate section Y %d", (int) sectionY);
                goto bail;
            }

            ChunkSection * section = AllocChunkSection();
            if (section == NULL) {
                LogErrno("Failed to allocate section: %s");
                goto bail;
            }
            // Note that the section allocation will be freed when the chunk
            // gets removed somewhere else in the code base.
            ch->sections[sectionIndex] = section;

            u32 paletteSize = palette.size;

            if (paletteSize == 0 || paletteSize > max_palette_map_size) {
                LogInfo("Invalid palette size %ju", (uintmax_t) paletteSize);
                goto bail;
            }

            for (uint palettei = 0; palettei < paletteSize; palettei++) {
                NbtCompound paletteEntryNbt = NbtNextCompound(&palette);
                String resource_loc = NbtGetString(&paletteEntryNbt, STR("Name"));
                i16 type_id = resolve_resource_loc_id(resource_loc,
                        &serv->block_resource_table);
                if (type_id == -1) {
                    // @TODO(traks) should probably just error out
                    type_id = 2;
                }

                u16 stride = 0;

                NbtCompound propsNbt = NbtGetCompound(&paletteEntryNbt, STR("Properties"));

                block_properties * props = serv->block_properties_table + type_id;
                for (int propi = 0; propi < props->property_count; propi++) {
                    block_property_spec * prop_spec = serv->block_property_specs
                            + props->property_specs[propi];
                    String prop_name = {
                        .size = prop_spec->tape[0],
                        .data = prop_spec->tape + 1
                    };

                    String propVal = NbtGetString(&propsNbt, prop_name);

                    int val_index = find_property_value_index(prop_spec, propVal);
                    if (val_index == -1) {
                        val_index = props->default_value_indices[propi];
                    }

                    stride = stride * prop_spec->value_count + val_index;
                }

                palette_map[palettei] = props->base_state + stride;
            }

            u32 entryCount = blockData.size;

            if (entryCount > 4096) {
                LogInfo("Too many entries: %ju", (uintmax_t) entryCount);
                goto bail;
            }

            int palette_size_ceil_log2 = CeilLog2U32(paletteSize);
            int bits_per_id = MAX(4, palette_size_ceil_log2);
            u32 id_mask = (1 << bits_per_id) - 1;
            int offset = 0;
            u64 entry = NbtNextU64(&blockData);

            for (int j = 0; j < 4096; j++) {
                u32 id = (entry >> offset) & id_mask;
                offset += bits_per_id;
                if (offset > 64 - bits_per_id) {
                    entry = NbtNextU64(&blockData);
                    offset = 0;
                }

                if (id >= paletteSize) {
                    LogInfo("Out of bounds palette ID");
                    goto bail;
                }

                u16 block_state = palette_map[id];
                section->blockStates[j] = block_state;

                if (block_state != 0) {
                    ch->non_air_count[sectionIndex]++;
                }
            }
        }
    }

    recalculate_chunk_motion_blocking_height_map(ch);

    if (cursor.error) {
        LogInfo("Failed to decipher NBT data");
        // NbtPrint(&chunkNbt);
        goto bail;
    }

    ch->flags |= CHUNK_LOADED;

bail:
    EndTimedZone();

    if (region_fd != -1) {
        close(region_fd);
    }
}

chunk *
get_or_create_chunk(chunk_pos pos) {
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
    chunk * ch = bucket->chunks + i;
    *ch = (chunk) {0};
    return ch;
}

chunk *
get_chunk_if_loaded(chunk_pos pos) {
    int hash = hash_chunk_pos(pos);
    chunk_bucket * bucket = chunk_map + hash;
    int bucket_size = bucket->size;
    chunk * res = NULL;

    for (int i = 0; i < bucket_size; i++) {
        if (chunk_pos_equal(bucket->positions[i], pos)) {
            chunk * ch = bucket->chunks + i;
            if (ch->flags & CHUNK_LOADED) {
                res = ch;
            }
            break;
        }
    }

    return res;
}

chunk *
get_chunk_if_available(chunk_pos pos) {
    int hash = hash_chunk_pos(pos);
    chunk_bucket * bucket = chunk_map + hash;
    int bucket_size = bucket->size;
    chunk * res = NULL;

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
            chunk * ch = bucket->chunks + chunki;
            ch->local_event_count = 0;

            if (ch->available_interest == 0) {
                for (int sectioni = 0; sectioni < SECTIONS_PER_CHUNK; sectioni++) {
                    if (ch->sections[sectioni] != NULL) {
                        FreeChunkSection(ch->sections[sectioni]);
                    }
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
