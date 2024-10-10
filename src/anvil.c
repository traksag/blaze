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
#include "buffer.h"
#include "nbt.h"
#include "chunk.h"

static void FillBufferFromFile(int fd, Cursor * cursor) {
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

static void UnpackStoredLight(u8 * target, u8 * source) {
    for (i32 i = 0; i < 2048; i++) {
        SetSectionLight(target, 2 * i, source[i] & 0xf);
        SetSectionLight(target, 2 * i + 1, source[i] >> 4);
    }
}

void WorldLoadChunk(Chunk * chunk, MemoryArena * scratchArena) {
    BeginTimings(ReadChunk);

    // @TODO(traks) error handling and/or error messages for all failure cases
    // in this entire function?

    int region_fd = -1;
    WorldChunkPos chunkPos = chunk->pos;

    int region_x = chunkPos.x >> 5;
    int region_z = chunkPos.z >> 5;

    char * worldName = NULL;
    if (chunkPos.worldId == 1) {
        worldName = "world";
    }

    if (worldName == NULL) {
        LogInfo("Unknown world ID: %lld", (i64) chunkPos.worldId);
        goto bail;
    }

    unsigned char file_name[64];
    int file_name_size = snprintf((void *) file_name, sizeof file_name, "%s/region/r.%d.%d.mca", worldName, region_x, region_z);

    region_fd = open((void *) file_name, O_RDONLY);
    if (region_fd == -1) {
        LogErrno("Failed to open region file: %s");
        goto bail;
    }

    struct stat region_stat;
    if (fstat(region_fd, &region_stat)) {
        LogErrno("Failed to get region file stat: %s");
        goto bail;
    }

    Cursor header_cursor = {
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
    u32 loc = ReadU32(&header_cursor);

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

    Cursor cursor = {
        .data = MallocInArena(scratchArena, sector_count << 12),
        .size = sector_count << 12
    };
    FillBufferFromFile(region_fd, &cursor);

    u32 size_in_bytes = ReadU32(&cursor);

    if ((i32) size_in_bytes > cursor.size - cursor.index) {
        LogInfo("Chunk data outside of its sectors");
        goto bail;
    }

    cursor.size = cursor.index + size_in_bytes;
    u8 storage_type = ReadU8(&cursor);

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

    cursor = (Cursor) {
        .data = uncompressed,
        .size = zstream.total_out
    };

    NbtCompound chunkNbt = NbtRead(&cursor, scratchArena);

    if (cursor.error) {
        LogInfo("Failed to load NBT data");
        goto bail;
    }

    // NbtPrint(&chunkNbt);

    i32 dataVersion = NbtGetU32(&chunkNbt, STR("DataVersion"));
    if (dataVersion != SERVER_WORLD_VERSION) {
        LogInfo("Data version %jd != %jd", (intmax_t) dataVersion, (intmax_t) SERVER_WORLD_VERSION);
        goto bail;
    }

    String status = NbtGetString(&chunkNbt, STR("Status"));
    if (!net_string_equal(status, STR("minecraft:full"))) {
        // @TODO(traks) this message gets spammed on the edges of pregenerated
        // terrain. Maybe turn it into a debug message.
        LogInfo("Chunk not fully generated, status: %.*s", status.size, status.data);
        goto bail;
    }

    i32 lightIsStored = NbtGetU8(&chunkNbt, STR("isLightOn"));
    // @TODO(traks) remove; just used for testing lighting engine
    // TODO(traks): figure out how we want to handle stored light and how we
    // want to propagate it to other chunks, etc.
    lightIsStored = 0;

    NbtList sectionList = NbtGetList(&chunkNbt, STR("sections"), NBT_COMPOUND);
    i32 numSections = sectionList.size;

    u32 maxPaletteEntries = 4096;
    u16 * paletteMap = MallocInArena(scratchArena, maxPaletteEntries * sizeof (u16));
    u8 sectionsWithBlocks[MAX_SECTION - MIN_SECTION + 1] = {0};

    if (numSections > LIGHT_SECTIONS_PER_CHUNK) {
        LogInfo("Too many chunk sections: %ju", (uintmax_t) numSections);
        goto bail;
    }

    for (i32 sectionNbtIndex = 0; sectionNbtIndex < numSections; sectionNbtIndex++) {
        NbtCompound sectionNbt = NbtNextCompound(&sectionList);
        // NOTE(traks): Should be u8, but sometimes this is an u32 in the wild.
        // Allow any int type, because we might as well
        i8 sectionY = (i8) NbtGetUAny(&sectionNbt, STR("Y"));

        NbtCompound blockStatesNbt = NbtGetCompound(&sectionNbt, STR("block_states"));
        NbtList palette = NbtGetList(&blockStatesNbt, STR("palette"), NBT_COMPOUND);
        NbtList blockData = NbtGetArrayU64(&blockStatesNbt, STR("data"));

        if (palette.size > 0) {
            if (sectionY < MIN_SECTION || sectionY > MAX_SECTION) {
                LogInfo("Invalid section Y %d with palette", (i32) sectionY);
                goto bail;
            }

            i32 sectionIndex = sectionY - MIN_SECTION;
            ChunkSection * section = chunk->sections + sectionIndex;
            SectionBlocks * blocks = &section->blocks;

            if (sectionsWithBlocks[sectionIndex]) {
                LogInfo("Duplicate block section for Y %d", (i32) sectionY);
                goto bail;
            }
            sectionsWithBlocks[sectionIndex] = 1;

            u32 paletteSize = palette.size;

            if (paletteSize <= 0 || paletteSize > maxPaletteEntries) {
                LogInfo("Invalid palette size %ju", (uintmax_t) paletteSize);
                goto bail;
            }

            for (u32 paletteIndex = 0; paletteIndex < paletteSize; paletteIndex++) {
                NbtCompound paletteEntryNbt = NbtNextCompound(&palette);
                String resourceLoc = NbtGetString(&paletteEntryNbt, STR("Name"));
                i16 typeId = resolve_resource_loc_id(resourceLoc, &serv->block_resource_table);

                if (typeId < 0) {
                    LogInfo("Encountered invalid block type");
                    goto bail;
                }

                u32 stride = 0;

                NbtCompound propsNbt = NbtGetCompound(&paletteEntryNbt, STR("Properties"));

                block_properties * props = serv->block_properties_table + typeId;
                for (u32 propIndex = 0; propIndex < props->property_count; propIndex++) {
                    block_property_spec * propSpec = serv->block_property_specs + props->property_specs[propIndex];
                    String propName = {
                        .size = propSpec->tape[0],
                        .data = propSpec->tape + 1
                    };

                    String propVal = NbtGetString(&propsNbt, propName);

                    i32 valueIndex = find_property_value_index(propSpec, propVal);
                    if (valueIndex < 0) {
                        valueIndex = props->default_value_indices[propIndex];
                    }

                    stride = stride * propSpec->value_count + valueIndex;
                }

                i32 blockState = props->base_state + stride;
                assert(blockState < serv->vanilla_block_state_count);
                paletteMap[paletteIndex] = blockState;
            }

            if (paletteSize == 1) {
                // NOTE(traks): Block data may be missing! The code below won't
                // work in that case, so we need some special handling.
                u32 blockState = paletteMap[0];
                for (i32 posIndex = 0; posIndex < 4096; posIndex++) {
                    SectionSetBlockState(blocks, posIndex, blockState);
                }

                // TODO(traks): handle cave air and void air
                if (blockState != 0) {
                    section->nonAirCount = 4096;
                }
            } else {
                i32 bitsPerBlock = CeilLog2U32(paletteSize);
                // NOTE(traks): Vanilla tweaks bits-per-block in this way. Note
                // that in chunk storage, 9+ bits per block doesn't get rounded
                // up to the maximum number of bits per block!
                if (bitsPerBlock < 4) {
                    bitsPerBlock = 4;
                }
                u32 blocksPerLong = 64 / bitsPerBlock;
                u32 expectedNumberOfLongs = (4096 + blocksPerLong - 1) / blocksPerLong;
                u32 mask = ((u32) 1 << bitsPerBlock) - 1;
                i32 bitOffset = 0;

                if (blockData.size != expectedNumberOfLongs) {
                    LogInfo("Expected %d longs, but got %d", (i32) expectedNumberOfLongs, (i32) blockData.size);
                    goto bail;
                }

                u64 entry = NbtNextU64(&blockData);

                for (i32 posIndex = 0; posIndex < 4096; posIndex++) {
                    if (bitOffset > 64 - bitsPerBlock) {
                        entry = NbtNextU64(&blockData);
                        bitOffset = 0;
                    }

                    u32 paletteIndex = (entry >> bitOffset) & mask;
                    bitOffset += bitsPerBlock;

                    if (paletteIndex >= paletteSize) {
                        LogInfo("Out of bounds palette index %d >= %d in section Y %d", paletteIndex, paletteSize, (i32) sectionY);
                        goto bail;
                    }

                    u32 blockState = paletteMap[paletteIndex];
                    SectionSetBlockState(blocks, posIndex, blockState);

                    // TODO(traks): handle cave air and void air
                    if (blockState != 0) {
                        section->nonAirCount++;
                    }
                }
            }
        }

        // TODO(traks): for now we don't load stored light, because we need to
        // invalidate it if any of the surrounding chunks got light updates
        // while this chunk was unloaded. Not sure if stuff like that is even
        // fixable? How would we detect if this chunk got unloaded before the
        // most recent light updates to neighbour chunk got saved to disk.
        if (lightIsStored && 0) {
            if (sectionY < MIN_SECTION - 1 || sectionY > MAX_SECTION + 1) {
                LogInfo("Section Y %d with light", (int) sectionY);
                goto bail;
            }

            i32 lightSectionIndex = sectionY - MIN_SECTION + 1;
            LightSection * lightSection = chunk->lightSections + lightSectionIndex;

            NbtList skyLight = NbtGetArrayU8(&sectionNbt, STR("SkyLight"));
            NbtList blockLight = NbtGetArrayU8(&sectionNbt, STR("BlockLight"));

            if (skyLight.size == 2048) {
                UnpackStoredLight(lightSection->skyLight, skyLight.listData);
            }
            if (blockLight.size == 2048) {
                UnpackStoredLight(lightSection->blockLight, blockLight.listData);
            }
        }
    }

    ChunkRecalculateMotionBlockingHeightMap(chunk);

    if (cursor.error) {
        LogInfo("Failed to decipher NBT data");
        // NbtPrint(&chunkNbt);
        goto bail;
    }

    // TODO(traks): not used at the moment
    if (lightIsStored && 0) {
        // NOTE(traks): can't set flags async at the moment, not thread safe!
        // chunk->statusFlags |= CHUNK_GOT_LIGHT;
    }

    atomic_fetch_or_explicit(&chunk->atomicFlags, CHUNK_ATOMIC_LOAD_SUCCESS, memory_order_relaxed);

bail:
    EndTimings(ReadChunk);

    if (region_fd != -1) {
        BeginTimings(CloseFile);
        close(region_fd);
        EndTimings(CloseFile);
    }
}
