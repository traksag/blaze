#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <zlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "shared.h"
#include "buf.h"
#include "nbt.h"
#include "chunk.h"

static void FillBufferFromFile(int fd, BufCursor * cursor) {
    BeginTimings(ReadFile);

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

    EndTimings(ReadFile);
}

void WorldLoadChunk(Chunk * chunk, MemoryArena * scratchArena) {
    BeginTimings(ReadChunk);

    // @TODO(traks) error handling and/or error messages for all failure cases
    // in this entire function?

    WorldChunkPos chunkPos = chunk->pos;

    int region_x = chunkPos.x >> 5;
    int region_z = chunkPos.z >> 5;

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
        .data = MallocInArena(scratchArena, 4096),
        .size = 4096
    };
    FillBufferFromFile(region_fd, &header_cursor);
    if (header_cursor.error) {
        goto bail;
    }

    // First read from the chunk location table at which sector (4096 byte
    // block) the chunk data starts.
    int index = ((chunkPos.z & 0x1f) << 5) | (chunkPos.x & 0x1f);
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
        .data = MallocInArena(scratchArena, sector_count << 12),
        .size = sector_count << 12
    };
    FillBufferFromFile(region_fd, &cursor);

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

    BeginTimings(Inflate);
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
        EndTimings(Inflate);
        goto bail;
    }

    zstream.next_in = cursor.data + cursor.index;
    zstream.avail_in = cursor.size - cursor.index;

    // @TODO(traks) can be many many times larger in case of e.g. NBT data with
    // tons and tons of empty lists.
    size_t max_uncompressed_size = 2 * (1 << 20);
    unsigned char * uncompressed = MallocInArena(scratchArena,
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
        EndTimings(Inflate);
        goto bail;
    }

    // bail in case of any errors above
    if (zstream.avail_in != 0) {
        EndTimings(Inflate);
        goto bail;
    }
    EndTimings(Inflate);

    cursor = (BufCursor) {
        .data = uncompressed,
        .size = zstream.total_out
    };

    NbtCompound chunkNbt = NbtRead(&cursor, scratchArena);

    if (cursor.error) {
        LogInfo("Failed to load NBT data");
        goto bail;
    }

    // NbtPrint(&chunkNbt);

    for (int section_y = 0; section_y < SECTIONS_PER_CHUNK; section_y++) {
        assert(chunk->sections[section_y].blockStates != NULL);
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

    i32 lightIsStored = NbtGetU8(&chunkNbt, STR("isLightOn"));
    // @TODO(traks) remove; just used for testing lighting engine
    lightIsStored = 0;

    NbtList sectionList = NbtGetList(&chunkNbt, STR("sections"), NBT_COMPOUND);
    i32 numSections = sectionList.size;

    // maximum amount of memory the palette will ever use
    int max_palette_map_size = 4096;
    u16 * palette_map = MallocInArena(scratchArena, max_palette_map_size * sizeof (u16));

    if (numSections > LIGHT_SECTIONS_PER_CHUNK) {
        LogInfo("Too many chunk sections: %ju", (uintmax_t) numSections);
        goto bail;
    }

    for (u32 sectioni = 0; sectioni < numSections; sectioni++) {
        NbtCompound sectionNbt = NbtNextCompound(&sectionList);
        i8 sectionY = NbtGetU8(&sectionNbt, STR("Y"));

        NbtCompound blockStatesNbt = NbtGetCompound(&sectionNbt, STR("block_states"));
        NbtList palette = NbtGetList(&blockStatesNbt, STR("palette"), NBT_COMPOUND);
        NbtList blockData = NbtGetArrayU64(&blockStatesNbt, STR("data"));

        if (palette.size > 0) {
            if (sectionY < MIN_SECTION || sectionY > MAX_SECTION) {
                LogInfo("Section Y %d with palette", (int) sectionY);
                goto bail;
            }

            i32 sectionIndex = sectionY - MIN_SECTION;
            ChunkSection * section = chunk->sections + sectionIndex;

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

            i32 palette_size_ceil_log2 = CeilLog2U32(paletteSize);
            i32 bits_per_id = MAX(4, palette_size_ceil_log2);
            u32 id_mask = (1 << bits_per_id) - 1;
            i32 offset = 0;

            if (paletteSize == 1) {
                // NOTE(traks): if the palette size is 1, the block data may be
                // missing! The code below won't work in that case, so we need
                // some special handling.
                u16 block_state = palette_map[0];
                for (i32 j = 0; j < 4096; j++) {
                    section->blockStates[j] = block_state;
                    // @TODO(traks) handle cave air and void air
                    if (block_state != 0) {
                        section->nonAirCount++;
                    }
                }
            } else {
                if (blockData.size > 4096) {
                    LogInfo("Too many entries: %ju", (uintmax_t) blockData.size);
                    goto bail;
                }

                i32 idsPerLong = 64 / bits_per_id;
                if (idsPerLong * blockData.size < 4096) {
                    LogInfo("Not enough entries %jd with bits per ID %jd",
                            (intmax_t) blockData.size, (intmax_t) bits_per_id);
                    goto bail;
                }

                u64 entry = NbtNextU64(&blockData);

                for (i32 j = 0; j < 4096; j++) {
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

                    // @TODO(traks) handle cave air and void air
                    if (block_state != 0) {
                        section->nonAirCount++;
                    }
                }
            }
        }

        if (lightIsStored) {
            if (sectionY < MIN_SECTION - 1 || sectionY > MAX_SECTION + 1) {
                LogInfo("Section Y %d with light", (int) sectionY);
                goto bail;
            }

            i32 lightSectionIndex = sectionY - MIN_SECTION + 1;
            LightSection * lightSection = chunk->lightSections + lightSectionIndex;

            NbtList skyLight = NbtGetArrayU8(&sectionNbt, STR("SkyLight"));
            NbtList blockLight = NbtGetArrayU8(&sectionNbt, STR("BlockLight"));

            if (skyLight.size == 2048) {
                memcpy(lightSection->skyLight, skyLight.listData, 2048);
            }
            if (blockLight.size == 2048) {
                memcpy(lightSection->blockLight, blockLight.listData, 2048);
            }
        }
    }

    ChunkRecalculateMotionBlockingHeightMap(chunk);

    if (cursor.error) {
        LogInfo("Failed to decipher NBT data");
        // NbtPrint(&chunkNbt);
        goto bail;
    }

    if (lightIsStored) {
        chunk->flags |= CHUNK_FULLY_LIT;
    }

    chunk->flags |= CHUNK_LOAD_SUCCESS;

bail:
    EndTimings(ReadChunk);

    if (region_fd != -1) {
        BeginTimings(CloseFile);
        close(region_fd);
        EndTimings(CloseFile);
    }
}
