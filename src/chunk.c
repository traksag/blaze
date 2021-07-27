#include <assert.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <zlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
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
    return ((pos.x & 0x1f) << 5) | (pos.z & 0x1f);
}

block_entity_base *
try_get_block_entity(net_block_pos pos) {
    // @TODO(traks) return some special block entity instead of NULL?
    if (pos.y < 0) {
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

    compact_chunk_block_pos chunk_block_pos = {
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
try_get_block_state(net_block_pos pos) {
    if (pos.y < 0) {
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
try_set_block_state(net_block_pos pos, u16 block_state) {
    if (pos.y < 0) {
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
    assert(0 <= y && y < 256);
    assert(0 <= z && z < 16);

    int section_y = y >> 4;
    chunk_section * section = ch->sections[section_y];

    if (section == NULL) {
        // @TODO(traks) would it be possible to have a block type -> default state
        // lookup here and have it expand to a constant once we pregenerate all
        // the data tables?
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
            u16 block_state = chunk_get_block_state(ch,
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
chunk_set_block_state(chunk * ch, int x, int y, int z, u16 block_state) {
    assert(0 <= x && x < 16);
    assert(0 <= y && y < 256);
    assert(0 <= z && z < 16);
    assert(ch->flags & CHUNK_LOADED);
    // @TODO(traks) somehow ensure this never fails even with tons of players,
    // or make sure we appropriate handle cases in which too many changes occur
    // to a chunk per tick.

    // @TODO(traks) This is currently O(N^2) where N is the number of different
    // blocks we changed in the chunk in a single tick. Should be faster.
    int match = 0;
    for (int i = 0; i < ch->changed_block_count; i++) {
        compact_chunk_block_pos entry = ch->changed_blocks[i];
        if (entry.x == x && entry.y == y && entry.z == z) {
            match = 1;
            break;
        }
    }

    if (!match) {
        assert(ch->changed_block_count < ARRAY_SIZE(ch->changed_blocks));

        compact_chunk_block_pos pos = {.x = x, .y = y, .z = z};
        ch->changed_blocks[ch->changed_block_count] = pos;
        ch->changed_block_count++;
    }

    int section_y = y >> 4;
    chunk_section * section = ch->sections[section_y];

    if (section == NULL) {
        // @TODO(traks) instead of making block setting fallible, perhaps
        // getting the chunk should fail if chunk sections cannot be allocated
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

    u16 max_height = ch->motion_blocking_height_map[height_map_index];
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

static int
ceil_log2u(u32 x) {
    assert(x != 0);
    // @TODO(traks) use lzcnt if available. Maybe not necessary with -O3.
    // @NOTE(traks) based on floor log2 from
    // https://graphics.stanford.edu/~seander/bithacks.html
    x--;

    int res;
    int shift;

    res = (x > 0xffff) << 4;
    x >>= res;

    shift = (x > 0xff) << 3;
    x >>= shift;
    res |= shift;

    shift = (x > 0xf) << 2;
    x >>= shift;
    res |= shift;

    shift = (x > 0x3) << 1;
    x >>= shift;
    res |= shift;

    res |= (x >> 1);

    res++;

    return res;
}

static void
fill_buffer_from_file(int fd, buffer_cursor * cursor) {
    int start_index = cursor->index;

    while (cursor->index < cursor->limit) {
        int bytes_read = read(fd, cursor->buf + cursor->index,
                cursor->limit - cursor->index);
        if (bytes_read == -1) {
            logs_errno("Failed to read region file: %s");
            cursor->error = 1;
            break;
        }
        if (bytes_read == 0) {
            logs("Wanted %d bytes from region file, but got %d",
                    cursor->limit - start_index,
                    cursor->index - start_index);
            cursor->error = 1;
            break;
        }

        cursor->index += bytes_read;
    }

    cursor->limit = cursor->index;
    cursor->index = start_index;
}

void
try_read_chunk_from_storage(chunk_pos pos, chunk * ch,
        memory_arena * scratch_arena) {
    begin_timed_block("read chunk");

    // @TODO(traks) error handling and/or error messages for all failure cases
    // in this entire function?

    int region_x = pos.x >> 5;
    int region_z = pos.z >> 5;

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
        goto bail;
    }

    buffer_cursor header_cursor = {
        .buf = alloc_in_arena(scratch_arena, 4096),
        .limit = 4096
    };
    fill_buffer_from_file(region_fd, &header_cursor);
    if (header_cursor.error) {
        goto bail;
    }

    // First read from the chunk location table at which sector (4096 byte
    // block) the chunk data starts.
    int index = ((pos.z & 0x1f) << 5) | (pos.x & 0x1f);
    header_cursor.index = index << 2;
    u32 loc = net_read_uint(&header_cursor);

    if (loc == 0) {
        // chunk not present in region file
        goto bail;
    }

    u32 sector_offset = loc >> 8;
    u32 sector_count = loc & 0xff;

    if (sector_offset < 2) {
        logs("Chunk data in header");
        goto bail;
    }
    if (sector_count == 0) {
        logs("Chunk data uses 0 sectors");
        goto bail;
    }
    if (((sector_offset + sector_count) << 12) > region_stat.st_size) {
        logs("Chunk data out of bounds");
        goto bail;
    }

    if (lseek(region_fd, sector_offset << 12, SEEK_SET) == -1) {
        logs_errno("Failed to seek to chunk data: %s");
        goto bail;
    }

    buffer_cursor cursor = {
        .buf = alloc_in_arena(scratch_arena, sector_count << 12),
        .limit = sector_count << 12
    };
    fill_buffer_from_file(region_fd, &cursor);

    u32 size_in_bytes = net_read_uint(&cursor);

    if (size_in_bytes > cursor.limit - cursor.index) {
        logs("Chunk data outside of its sectors");
        goto bail;
    }

    cursor.limit = cursor.index + size_in_bytes;
    u8 storage_type = net_read_ubyte(&cursor);

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
    // If we don't end up doing this, make sure the code below is actually
    // correct (when do we need to clean stuff up?)!
    z_stream zstream;
    zstream.zalloc = Z_NULL;
    zstream.zfree = Z_NULL;
    zstream.opaque = Z_NULL;

    if (inflateInit2(&zstream, windowBits) != Z_OK) {
        logs("inflateInit failed");
        end_timed_block();
        goto bail;
    }

    zstream.next_in = cursor.buf + cursor.index;
    zstream.avail_in = cursor.limit - cursor.index;

    // @TODO(traks) can be many many times larger in case of e.g. NBT data with
    // tons and tons of empty lists.
    size_t max_uncompressed_size = 2 * (1 << 20);
    unsigned char * uncompressed = alloc_in_arena(scratch_arena,
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
        logs("Chunk inflate stream error");
        break;
    case Z_DATA_ERROR:
        logs("Failed to finish inflating chunk: %s", zstream.msg);
        break;
    case Z_MEM_ERROR:
        logs("Chunk inflate not enough memory");
        break;
    case Z_BUF_ERROR:
    case Z_OK:
        logs("Uncompressed chunk size too large");
        break;
    }

    if (inflateEnd(&zstream) != Z_OK) {
        logs("inflateEnd failed");
        end_timed_block();
        goto bail;
    }

    // bail in case of any errors above
    if (zstream.avail_in != 0) {
        end_timed_block();
        goto bail;
    }
    end_timed_block();

    cursor = (buffer_cursor) {
        .buf = uncompressed,
        .limit = zstream.total_out
    };

    // @TODO(traks) more appropriate max level
    int max_levels = 1024;
    nbt_tape_entry * chunk_nbt = load_nbt(&cursor, scratch_arena, max_levels);

    if (cursor.error) {
        logs("Failed to load NBT data");
        goto bail;
    }

    // print_nbt(chunk_nbt, &cursor, scratch_arena, max_levels);

    for (int section_y = 0; section_y < 16; section_y++) {
        assert(ch->sections[section_y] == NULL);
    }

    nbt_move_to_key(NET_STRING("DataVersion"), chunk_nbt, &cursor);
    i32 data_version = net_read_int(&cursor);
    if (data_version != SERVER_WORLD_VERSION) {
        logs("Data version %jd != %jd", (intmax_t) data_version,
                (intmax_t) SERVER_WORLD_VERSION);
        goto bail;
    }

    nbt_tape_entry * level_nbt = nbt_get_compound(NET_STRING("Level"),
            chunk_nbt, &cursor);

    net_string status = nbt_get_string(NET_STRING("Status"), level_nbt, &cursor);
    if (!net_string_equal(status, NET_STRING("full"))) {
        // @TODO(traks) this message gets spammed on the edges of pregenerated
        // terrain. Maybe turn it into a debug message.
        logs("Chunk not fully generated");
        goto bail;
    }

    nbt_tape_entry * section_start = nbt_move_to_key(NET_STRING("Sections"),
            level_nbt, &cursor);

    u32 section_count;
    nbt_tape_entry * section_nbt;

    if (section_start->tag != NBT_TAG_LIST) {
        section_count = 0;
        section_nbt = NULL;
    } else {
        section_count = section_start[2].list_size;
        section_nbt = section_start + 3;
    }

    // maximum amount of memory the palette will ever use
    int max_palette_map_size = 4096;
    u16 * palette_map = alloc_in_arena(scratch_arena,
            max_palette_map_size * sizeof (u16));

    if (section_count > 18) {
        logs("Too many chunk sections: %ju", (uintmax_t) section_count);
        goto bail;
    }

    for (u32 sectioni = 0; sectioni < section_count; sectioni++) {
        nbt_move_to_key(NET_STRING("Y"), section_nbt, &cursor);
        i8 section_y = net_read_byte(&cursor);

        nbt_tape_entry * palette_start = nbt_move_to_key(NET_STRING("Palette"),
                section_nbt, &cursor);

        if (palette_start->tag != NBT_TAG_END) {
            if (section_y < 0 || section_y >= 16) {
                logs("Section Y %d with palette", (int) section_y);
                goto bail;
            }

            if (ch->sections[section_y] != NULL) {
                logs("Duplicate section Y %d", (int) section_y);
                goto bail;
            }

            chunk_section * section = alloc_chunk_section();
            if (section == NULL) {
                logs_errno("Failed to allocate section: %s");
                goto bail;
            }
            // Note that the section allocation will be freed when the chunk
            // gets removed somewhere else in the code base.
            ch->sections[section_y] = section;

            u32 palette_size = palette_start[2].list_size;
            nbt_tape_entry * palette_entry = palette_start + 3;

            if (palette_size == 0 || palette_size > max_palette_map_size) {
                logs("Invalid palette size %ju", (uintmax_t) palette_size);
                goto bail;
            }

            for (uint palettei = 0; palettei < palette_size; palettei++) {
                net_string resource_loc = nbt_get_string(NET_STRING("Name"),
                        palette_entry, &cursor);
                i16 type_id = resolve_resource_loc_id(resource_loc,
                        &serv->block_resource_table);
                if (type_id == -1) {
                    // @TODO(traks) should probably just error out
                    type_id = 2;
                }

                u16 stride = 0;

                nbt_tape_entry * props_nbt = nbt_get_compound(
                        NET_STRING("Properties"), palette_entry, &cursor);

                block_properties * props = serv->block_properties_table + type_id;
                for (int propi = 0; propi < props->property_count; propi++) {
                    block_property_spec * prop_spec = serv->block_property_specs
                            + props->property_specs[propi];
                    net_string prop_name = {
                        .size = prop_spec->tape[0],
                        .ptr = prop_spec->tape + 1
                    };

                    net_string prop_val = nbt_get_string(prop_name,
                            props_nbt, &cursor);

                    int val_index = find_property_value_index(prop_spec, prop_val);
                    if (val_index == -1) {
                        val_index = props->default_value_indices[propi];
                    }

                    stride = stride * prop_spec->value_count + val_index;
                }

                palette_map[palettei] = props->base_state + stride;

                // move to end of palette entry compound
                while (palette_entry->tag != NBT_TAG_END) {
                    palette_entry++;
                    palette_entry += palette_entry->next_compound_entry_offset;
                }
                palette_entry += 1;
            }

            nbt_move_to_key(NET_STRING("BlockStates"), section_nbt, &cursor);
            u32 entry_count = net_read_uint(&cursor);

            if (entry_count > 4096) {
                logs("Too many entries: %ju", (uintmax_t) entry_count);
                goto bail;
            }

            int palette_size_ceil_log2 = ceil_log2u(palette_size);
            int bits_per_id = MAX(4, palette_size_ceil_log2);
            u32 id_mask = (1 << bits_per_id) - 1;
            int offset = 0;
            u64 entry = net_read_ulong(&cursor);

            for (int j = 0; j < 4096; j++) {
                u32 id = (entry >> offset) & id_mask;
                offset += bits_per_id;
                if (offset > 64 - bits_per_id) {
                    entry = net_read_ulong(&cursor);
                    offset = 0;
                }

                if (id >= palette_size) {
                    logs("Out of bounds palette ID");
                    goto bail;
                }

                u16 block_state = palette_map[id];
                section->block_states[j] = block_state;

                if (block_state != 0) {
                    ch->non_air_count[section_y]++;
                }
            }
        }

        // move to end of section compound
        while (section_nbt->tag != NBT_TAG_END) {
            section_nbt++;
            section_nbt += section_nbt->next_compound_entry_offset;
        }
        section_nbt += 1;
    }

    recalculate_chunk_motion_blocking_height_map(ch);

    if (cursor.error) {
        logs("Failed to decipher NBT data");
        print_nbt(chunk_nbt, &cursor, scratch_arena, max_levels);
        goto bail;
    }

    ch->flags |= CHUNK_LOADED;

bail:
    end_timed_block();

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
            ch->changed_block_count = 0;
            ch->local_event_count = 0;

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
