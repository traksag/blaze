#include <assert.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <x86intrin.h>
#include <unistd.h>
#include <fcntl.h>
#include <zlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include "shared.h"

// @TODO(traks) don't use a hash map. Performance depends on the chunks loaded,
// which depends on the positions of players in the world. Doesn't seem good
// that players moving to certain locations can cause performance drops...
//
// Our goal is 1000 players, so if we construct our hash and hash map in an
// appropriate way, we can ensure there are at most 1000 entries per bucket.
// Not sure if that's any good.
static chunk_bucket chunk_map[CHUNK_MAP_SIZE];

static chunk_section_bucket * full_chunk_section_buckets;
static chunk_section_bucket * chunk_section_buckets_with_unused;

chunk_section *
alloc_chunk_section() {
    chunk_section_bucket * bucket = chunk_section_buckets_with_unused;
    if (bucket == NULL) {
        // initialises all memory to 0
        bucket = mmap(NULL, sizeof *bucket, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (bucket == MAP_FAILED) {
            return NULL;
        }
        chunk_section_buckets_with_unused = bucket;
    }

    int seci;
    for (seci = 0; seci < CHUNK_SECTIONS_PER_BUCKET; seci++) {
        if (bucket->used_map[seci] == 0) {
            break;
        }
    }

    assert(seci < CHUNK_SECTIONS_PER_BUCKET);
    assert(bucket->used_sections < CHUNK_SECTIONS_PER_BUCKET);
    chunk_section * res = bucket->chunk_sections + seci;
    *res = (chunk_section) {0};
    res->index_in_bucket = seci;
    bucket->used_map[seci] = 1;
    bucket->used_sections++;

    if (bucket->used_sections == CHUNK_SECTIONS_PER_BUCKET) {
        // bucket is full, so move bucket to linked list of full ones
        chunk_section_bucket * next = bucket->next;
        if (next != NULL) {
            next->prev = NULL;
        }
        chunk_section_buckets_with_unused = next;

        bucket->next = full_chunk_section_buckets;
        bucket->prev = NULL;
        if (full_chunk_section_buckets != NULL) {
            assert(full_chunk_section_buckets->prev == NULL);
            full_chunk_section_buckets->prev = bucket;
        }
        full_chunk_section_buckets = bucket;
    }
    return res;
}

void
free_chunk_section(chunk_section * section) {
    int index_in_bucket = section->index_in_bucket;
    chunk_section_bucket * bucket = (void *) (section - index_in_bucket);
    assert(bucket->used_map[index_in_bucket] == 1);
    assert(bucket->used_sections > 0);
    bucket->used_map[index_in_bucket] = 0;
    bucket->used_sections--;

    if (bucket->used_sections == CHUNK_SECTIONS_PER_BUCKET - 1) {
        // bucket went from full to non-full, so move to linked list of buckets
        // with unused entries
        if (bucket->prev != NULL) {
            bucket->prev->next = bucket->next;
        } else {
            full_chunk_section_buckets = bucket->next;
        }
        if (bucket->next != NULL) {
            bucket->next->prev = bucket->prev;
        }

        bucket->prev = NULL;
        bucket->next = chunk_section_buckets_with_unused;
        if (bucket->next != NULL) {
            bucket->next->prev = bucket;
        }
        chunk_section_buckets_with_unused = bucket;
    } else if (bucket->used_sections == 0) {
        if (bucket->prev != NULL) {
            bucket->prev->next = bucket->next;
        } else {
            chunk_section_buckets_with_unused = bucket->next;
        }
        if (bucket->next != NULL) {
            bucket->next->prev = bucket->prev;
        }

        // @TODO(traks) figure out whether this actually unmaps all memory used
        // for the bucket, or if the last page is only partially unmapped or
        // something.
        int bad = munmap(bucket, sizeof *bucket);
        assert(!bad);
    }
}

static int
chunk_pos_equal(chunk_pos a, chunk_pos b) {
    // @TODO(traks) make sure this compiles to a single compare. If not, should
    // probably change chunk_pos to be a uint32_t.
    return a.x == b.x && a.z == b.z;
}

static int
hash_chunk_pos(chunk_pos pos) {
    // @TODO(traks) this signed-to-unsigned cast better work
    return (((unsigned) pos.x & 0x1f) << 5) | ((unsigned) pos.z & 0x1f);
}

mc_ushort
chunk_get_block_state(chunk * ch, int x, int y, int z) {
    assert(0 <= x && x < 16);
    assert(0 <= y && y < 256);
    assert(0 <= z && z < 16);

    int section_y = y >> 4;
    chunk_section * section = ch->sections[section_y];

    if (section == NULL) {
        return 0;
    }

    int index = ((y & 0xf) << 8) | (z << 4) | x;
    return section->block_states[index];
}

static void
recalculate_chunk_motion_blocking_height_map(chunk * ch) {
    for (int zx = 0; zx < 16 * 16; zx++) {
        ch->motion_blocking_height_map[zx] = 0;
        for (int y = 255; y >= 0; y--) {
            mc_ushort block_state = chunk_get_block_state(ch,
                    zx & 0xf, y, zx >> 4);
            // @TODO(traks) other airs
            if (block_state != 0) {
                ch->motion_blocking_height_map[zx] = y + 1;
                break;
            }
        }
    }
}

void
chunk_set_block_state(chunk * ch, int x, int y, int z, mc_ushort block_state) {
    assert(0 <= x && x < 16);
    assert(0 <= y && y < 256);
    assert(0 <= z && z < 16);
    assert(ch->flags & CHUNK_LOADED);
    // @TODO(traks) somehow ensure this never fails even with tons of players,
    // or make sure we appropriate handle cases in which too many changes occur
    // to a chunk per tick.
    assert(ch->changed_block_count < ARRAY_SIZE(ch->changed_blocks));

    // format is that of the chunk blocks update packet
    ch->changed_blocks[ch->changed_block_count] =
            ((mc_ushort) x << 12) | (z << 8) | y;
    ch->changed_block_count++;

    int section_y = y >> 4;
    chunk_section * section = ch->sections[section_y];

    if (section == NULL) {
        // @TODO(traks) instead of making block setting fallible, perhaps
        // getting the chunk should be if chunk sections cannot be allocated
        section = alloc_chunk_section();
        if (section == NULL) {
            logs_errno("Failed to allocate section: %s");
            exit(1);
        }
        ch->sections[section_y] = section;
    }

    int index = ((y & 0xf) << 8) | (z << 4) | x;

    if (section->block_states[index] == 0) {
        ch->non_air_count[section_y]++;
    }
    if (block_state == 0) {
        ch->non_air_count[section_y]--;
    }

    section->block_states[index] = block_state;

    int height_map_index = (z << 4) | x;

    mc_ushort max_height = ch->motion_blocking_height_map[height_map_index];
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
}

void
try_read_chunk_from_storage(chunk_pos pos, chunk * ch,
        memory_arena * scratch_arena,
        block_properties * block_properties_table,
        block_property_spec * block_property_specs) {
    begin_timed_block("read chunk");

    // @TODO(traks) error handling and/or error messages for all failure cases
    // in this entire function?

    __m128i chunk_xz = _mm_set_epi32(0, 0, pos.z, pos.x);
    __m128i region_xz = _mm_srai_epi32(chunk_xz, 5);
    int region_x = _mm_extract_epi32(region_xz, 0);
    int region_z = _mm_extract_epi32(region_xz, 1);

    unsigned char file_name[64];
    int file_name_size = sprintf((void *) file_name,
            "world/region/r.%d.%d.mca", region_x, region_z);

    int region_fd = open((void *) file_name, O_RDONLY);
    if (region_fd == -1) {
        logs_errno("Failed to open region file: %s");
        goto bail;
    }

    struct stat region_stat;
    if (fstat(region_fd, &region_stat)) {
        logs_errno("Failed to get region file stat: %s");
        close(region_fd);
        goto bail;
    }

    // @TODO(traks) should we unmap this or something?
    // begin_timed_block("mmap");
    void * region_mmap = mmap(NULL, region_stat.st_size, PROT_READ,
            MAP_PRIVATE, region_fd, 0);
    // end_timed_block();
    if (region_mmap == MAP_FAILED) {
        logs_errno("Failed to mmap region file: %s");
        close(region_fd);
        goto bail;
    }

    // after mmaping we can close the file descriptor
    close(region_fd);

    buffer_cursor cursor = {
        .buf = region_mmap,
        .limit = region_stat.st_size
    };

    // First read from the chunk location table at which sector (4096 byte
    // block) the chunk data starts.
    // @TODO(traks) this & better work for negative values
    int index = ((pos.z & 0x1f) << 5) | (pos.x & 0x1f);
    cursor.index = index << 2;
    mc_uint loc = net_read_uint(&cursor);

    if (loc == 0) {
        // chunk not present in region file
        goto bail;
    }

    mc_uint sector_offset = loc >> 8;
    mc_uint sector_count = loc & 0xff;

    if (sector_offset < 2) {
        logs("Chunk data in header");
        goto bail;
    }
    if (sector_count == 0) {
        logs("Chunk data uses 0 sectors");
        goto bail;
    }
    if (sector_offset + sector_count > (cursor.limit >> 12)) {
        logs("Chunk data out of bounds");
        goto bail;
    }

    cursor.index = sector_offset << 12;
    mc_uint size_in_bytes = net_read_uint(&cursor);

    if (size_in_bytes > (sector_count << 12)) {
        logs("Chunk data outside of its sectors");
        goto bail;
    }

    cursor.limit = cursor.index + size_in_bytes;
    mc_ubyte storage_type = net_read_ubyte(&cursor);

    if (cursor.error) {
        logs("Chunk header reading error");
        goto bail;
    }

    if (storage_type & 0x80) {
        // @TODO(traks) separate file is used to store the chunk
        logs("External chunk storage");
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
        logs("Unknown chunk compression method");
        goto bail;
    }

    begin_timed_block("inflate");
    // @TODO(traks) perhaps use https://github.com/ebiggers/libdeflate instead
    // of zlib. Using zlib now just because I had the code for it laying around.
    z_stream zstream;
    zstream.zalloc = NULL;
    zstream.zfree = NULL;
    zstream.opaque = NULL;

    if (inflateInit2(&zstream, windowBits) != Z_OK) {
        logs("inflateInit failed");
        goto bail;
    }

    zstream.next_in = cursor.buf + cursor.index;
    zstream.avail_in = cursor.limit - cursor.index;

    size_t max_uncompressed_size = 2097152;
    unsigned char * uncompressed = alloc_in_arena(scratch_arena,
            max_uncompressed_size);

    zstream.next_out = uncompressed;
    zstream.avail_out = max_uncompressed_size;

    if (inflate(&zstream, Z_FINISH) != Z_STREAM_END) {
        logs("Failed to finish inflating chunk: %s", zstream.msg);
        goto bail;
    }

    if (inflateEnd(&zstream) != Z_OK) {
        logs("inflateEnd failed");
        goto bail;
    }

    if (zstream.avail_in != 0) {
        logs("Didn't inflate entire chunk");
        goto bail;
    }
    end_timed_block();

    cursor = (buffer_cursor) {
        .buf = uncompressed,
        .limit = zstream.total_out
    };

    // begin_timed_block("load nbt");
    // @TODO(traks) more appropriate max level, currently 64
    nbt_tape_entry * tape = load_nbt(&cursor, scratch_arena, 64);
    // end_timed_block();

    if (cursor.error) {
        logs("Failed to read uncompressed NBT data");
        goto bail;
    }

    for (int section_y = 0; section_y < 16; section_y++) {
        assert(ch->sections[section_y] == NULL);
    }

    nbt_move_to_key(NET_STRING("DataVersion"), tape, 0, &cursor);
    mc_int data_version = net_read_int(&cursor);
    if (data_version != 2230) {
        logs("Unknown data version %jd", (intmax_t) data_version);
        goto bail;
    }

    mc_uint level_start = nbt_move_to_key(NET_STRING("Level"), tape, 0, &cursor);
    if (tape[level_start].tag != NBT_TAG_COMPOUND) {
        logs("NBT Level tag not a compound");
        goto bail;
    }
    level_start += 2;

    mc_uint sections_start = nbt_move_to_key(NET_STRING("Sections"),
            tape, level_start, &cursor);
    mc_uint section_count = tape[sections_start + 2].list_size;
    mc_uint section_start = sections_start + 4;

    // maximum amount of memory the palette will ever use
    mc_ushort * palette_map = alloc_in_arena(scratch_arena,
            4096 * sizeof (mc_ushort));

    for (mc_uint sectioni = 0; sectioni < section_count; sectioni++) {
        nbt_move_to_key(NET_STRING("Y"), tape, section_start, &cursor);
        mc_byte section_y = net_read_byte(&cursor);

        if (ch->sections[section_y] != NULL) {
            logs("Duplicate section Y %d", (int) section_y);
            goto bail;
        }

        int palette_start = nbt_move_to_key(NET_STRING("Palette"),
                tape, section_start, &cursor);

        if (tape[palette_start].tag != NBT_TAG_END) {
            if (section_y < 0 || section_y >= 16) {
                logs("Section Y %d with palette", (int) section_y);
                goto bail;
            }

            // @TODO(traks) May be fine to fail since loading the chunk can fail
            // in all sorts of ways anyhow...
            chunk_section * section = alloc_chunk_section();
            if (section == NULL) {
                logs_errno("Failed to allocate section: %s");
                exit(1);
            }
            // Note that the section allocation will be freed when the chunk
            // gets removed somewhere else in the code base.
            ch->sections[section_y] = section;

            mc_uint palette_size = tape[palette_start + 2].list_size;
            mc_uint palettei_start = palette_start + 4;

            for (uint palettei = 0; palettei < palette_size; palettei++) {
                mc_short type_id = 0;

                mc_uint name_start = nbt_move_to_key(NET_STRING("Name"),
                        tape, palettei_start, &cursor);
                if (tape[name_start].tag != NBT_TAG_END) {
                    mc_ushort resource_loc_size = net_read_ushort(&cursor);
                    net_string resource_loc = {
                        .size = resource_loc_size,
                        .ptr = cursor.buf + cursor.index
                    };
                    type_id = resolve_block_type_id(resource_loc);
                    if (type_id == -1) {
                        // @TODO(traks) should probably just error out
                        type_id = 2;
                    }
                }

                mc_ushort stride = 0;

                mc_uint props_start = nbt_move_to_key(NET_STRING("Properties"),
                        tape, palettei_start, &cursor);
                if (tape[props_start].tag == NBT_TAG_COMPOUND) {
                    props_start += 2;
                }

                block_properties * props = block_properties_table + type_id;
                for (int propi = 0; propi < props->property_count; propi++) {
                    block_property_spec * prop_spec = block_property_specs
                            + props->property_specs[propi];
                    net_string prop_name = {
                        .size = prop_spec->tape[0],
                        .ptr = prop_spec->tape + 1
                    };
                    mc_uint val_start = nbt_move_to_key(prop_name,
                            tape, props_start, &cursor);
                    int val_index = -1;
                    if (tape[val_start].tag != NBT_TAG_END) {
                        mc_ushort val_size = net_read_ushort(&cursor);
                        net_string val = {
                            .size = val_size,
                            .ptr = cursor.buf + cursor.index
                        };
                        val_index = find_property_value_index(prop_spec, val);
                    }
                    if (val_index == -1) {
                        val_index = props->default_value_indices[propi];
                    }
                    stride = stride * prop_spec->value_count + val_index;
                }

                palette_map[palettei] = props->base_state + stride;

                // move to end of palette entry compound
                mc_uint i = palettei_start;
                while (tape[i].tag != NBT_TAG_END) {
                    i = tape[i + 1].next_compound_entry;
                }
                palettei_start = i + 2;
            }

            nbt_move_to_key(NET_STRING("BlockStates"),
                    tape, section_start, &cursor);
            mc_uint entry_count = net_read_uint(&cursor);
            mc_uint bits_per_id = entry_count >> 6;
            mc_uint id_mask = (1 << bits_per_id) - 1;
            int offset = 0;
            mc_ulong entry = net_read_ulong(&cursor);

            for (int j = 0; j < 4096; j++) {
                mc_uint id = (entry >> offset) & id_mask;
                mc_uint remaining_bits = 64 - offset;
                if (bits_per_id >= remaining_bits) {
                    entry = net_read_ulong(&cursor);
                    id |= (entry << remaining_bits) & id_mask;
                    offset = bits_per_id - remaining_bits;
                } else {
                    offset += bits_per_id;
                }

                if (id >= palette_size) {
                    logs("Out of bounds palette ID");
                    goto bail;
                }

                mc_ushort block_state = palette_map[id];
                section->block_states[j] = block_state;

                if (block_state != 0) {
                    ch->non_air_count[section_y]++;
                }
            }
        }

        // move to end of section compound
        mc_uint i = section_start;
        while (tape[i].tag != NBT_TAG_END) {
            i = tape[i + 1].next_compound_entry;
        }
        section_start = i + 2;
    }

    recalculate_chunk_motion_blocking_height_map(ch);

    if (cursor.error) {
        logs("Failed to read uncompressed data");
        goto bail;
    }

    ch->flags |= CHUNK_LOADED;

bail:
    end_timed_block();
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

    for (int i = 0; i < bucket_size; i++) {
        if (chunk_pos_equal(bucket->positions[i], pos)) {
            chunk * ch = bucket->chunks + i;
            if (ch->flags & CHUNK_LOADED) {
                return ch;
            }
            return NULL;
        }
    }

    return NULL;
}

chunk *
get_chunk_if_available(chunk_pos pos) {
    int hash = hash_chunk_pos(pos);
    chunk_bucket * bucket = chunk_map + hash;
    int bucket_size = bucket->size;

    for (int i = 0; i < bucket_size; i++) {
        if (chunk_pos_equal(bucket->positions[i], pos)) {
            return bucket->chunks + i;
        }
    }

    return NULL;
}

void
clean_up_unused_chunks(void) {
    for (int bucketi = 0; bucketi < ARRAY_SIZE(chunk_map); bucketi++) {
        chunk_bucket * bucket = chunk_map + bucketi;

        for (int chunki = 0; chunki < bucket->size; chunki++) {
            chunk * ch = bucket->chunks + chunki;
            ch->changed_block_count = 0;

            if (ch->available_interest == 0) {
                for (int sectioni = 0; sectioni < 16; sectioni++) {
                    if (ch->sections[sectioni] != NULL) {
                        free_chunk_section(ch->sections[sectioni]);
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