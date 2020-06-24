#include <string.h>
#include "shared.h"

typedef struct {
    mc_uint is_list:1;
    mc_uint element_tag:4;
    mc_uint prev_compound_entry:20;
    mc_uint list_elems_remaining;
} nbt_level_info;

mc_uint
nbt_move_to_key(net_string matcher, nbt_tape_entry * tape,
        mc_uint start_index, buffer_cursor * cursor) {
    mc_uint i = start_index;
    while (tape[i].tag != NBT_TAG_END) {
        cursor->index = tape[i].buffer_index;
        mc_ushort key_size = net_read_ushort(cursor);
        unsigned char * key = cursor->buf + cursor->index;
        if (key_size == matcher.size
                && memcmp(key, matcher.ptr, matcher.size) == 0) {
            cursor->index += key_size;
            break;
        }
        i = tape[i + 1].next_compound_entry;
    }
    return i;
}

nbt_tape_entry *
load_nbt(buffer_cursor * cursor, memory_arena * arena, int max_level) {
    // @TODO(traks) Here's another idea for NBT parsing. Instead of using a
    // tape, for each level we could maintain a linked list of blocks of
    // something similar to tape entries. The benefit being that we need to jump
    // over subtrees a lot less (depending on the block size) when iterating
    // through the keys, and that we don't need to track these jumps as is done
    // when constructing the tape.
    begin_timed_block("load nbt");

    int max_entries = 65536;
    nbt_tape_entry * tape = alloc_in_arena(arena, max_entries * sizeof *tape);
    memory_arena scratch_arena = {
        .ptr = arena->ptr,
        .index = arena->index,
        .size = arena->size
    };
    nbt_level_info * level_info = alloc_in_arena(&scratch_arena,
            (max_level + 1) * sizeof *level_info);
    int cur_tape_index = 0;
    int cur_level = 0;
    level_info[0] = (nbt_level_info) {0};

    mc_ubyte tag = net_read_ubyte(cursor);

    if (tag == NBT_TAG_END) {
        // no nbt data
        goto bail;
    } else if (tag != NBT_TAG_COMPOUND) {
        cursor->error = 1;
        goto bail;
    }

    // skip key of root compound
    mc_ushort key_size = net_read_ushort(cursor);
    if (key_size > cursor->limit - cursor->index) {
        cursor->error = 1;
        goto bail;
    }
    cursor->index += key_size;

    for (;;) {
        if (cur_tape_index >= max_entries - 10) {
            logs("Max NBT tape index reached");
            cursor->error = 1;
            goto bail;
        }
        if (cur_level == max_level + 1) {
            logs("Max NBT level reached");
            cursor->error = 1;
            goto bail;
        }
        if (!level_info[cur_level].is_list) {
            // compound
            tag = net_read_ubyte(cursor);
            // @NOTE(traks) we need to be a bit careful here in case this is the
            // first entry of the compound, because then there is no previous
            // entry yet. Currently the previous entry is just the current entry
            // for the first one, so we write to the next tape entry. This is of
            // course not a problem (and circumvents if-statements and such).
            tape[level_info[cur_level].prev_compound_entry + 1]
                    .next_compound_entry = cur_tape_index;

            if (tag == NBT_TAG_END) {
                tape[cur_tape_index] = (nbt_tape_entry) {.tag = NBT_TAG_END};
                cur_tape_index++;
                cur_level--;

                if (cur_level == -1) {
                    goto bail;
                } else {
                    continue;
                }
            }

            mc_int entry_start = cursor->index;
            key_size = net_read_ushort(cursor);
            if (key_size > cursor->limit - cursor->index) {
                cursor->error = 1;
                goto bail;
            }
            cursor->index += key_size;

            level_info[cur_level].prev_compound_entry = cur_tape_index;
            nbt_tape_entry new_entry = {
                .buffer_index = entry_start,
                .tag = tag
            };
            tape[cur_tape_index] = new_entry;
            cur_tape_index++;
            // increment another time for the next pointer
            cur_tape_index++;
        } else {
            // list
            if (level_info[cur_level].list_elems_remaining == 0) {
                nbt_tape_entry new_entry = {.tag = NBT_TAG_LIST_END};
                tape[cur_tape_index] = new_entry;
                cur_tape_index++;
                cur_level--;
                continue;
            }

            level_info[cur_level].list_elems_remaining--;
            tag = level_info[cur_level].element_tag;

            if (tag == NBT_TAG_COMPOUND) {
                nbt_tape_entry new_entry = {.tag = NBT_TAG_COMPOUND_IN_LIST};
                tape[cur_tape_index] = new_entry;
                cur_tape_index++;
            }
            if (tag == NBT_TAG_LIST) {
                nbt_tape_entry new_entry = {.tag = NBT_TAG_LIST_IN_LIST};
                tape[cur_tape_index] = new_entry;
                cur_tape_index++;
            }
        }

        static mc_byte elem_bytes[] = {0, 1, 2, 4, 8, 4, 8};
        static mc_byte array_elem_bytes[] = {1, 0, 0, 0, 4, 8};
        switch (tag) {
        case NBT_TAG_END:
            // Minecraft uses this sometimes for empty lists even if the
            // element tag differs if the list is non-empty... why...?
            goto bail;
        case NBT_TAG_BYTE:
        case NBT_TAG_SHORT:
        case NBT_TAG_INT:
        case NBT_TAG_LONG:
        case NBT_TAG_FLOAT:
        case NBT_TAG_DOUBLE: {
            int bytes = elem_bytes[tag];
            if (cursor->index > cursor->limit - bytes) {
                cursor->error = 1;
                goto bail;
            } else {
                cursor->index += bytes;
            }
            break;
        }
        case NBT_TAG_BYTE_ARRAY:
        case NBT_TAG_INT_ARRAY:
        case NBT_TAG_LONG_ARRAY: {
            mc_long elem_bytes = array_elem_bytes[tag - NBT_TAG_BYTE_ARRAY];
            mc_long array_size = net_read_uint(cursor);
            if (cursor->index > (mc_long) cursor->limit
                    - elem_bytes * array_size) {
                cursor->error = 1;
                goto bail;
            } else {
                cursor->index += elem_bytes * array_size;
            }
            break;
        }
        case NBT_TAG_STRING: {
            mc_ushort size = net_read_ushort(cursor);
            if (cursor->index > cursor->limit - size) {
                cursor->error = 1;
            } else {
                cursor->index += size;
            }
            break;
        }
        case NBT_TAG_LIST: {
            cur_level++;
            mc_uint element_tag = net_read_ubyte(cursor);
            mc_long list_size = net_read_uint(cursor);
            level_info[cur_level] = (nbt_level_info) {
                .is_list = 1,
                .element_tag = element_tag,
                .list_elems_remaining = list_size
            };

            // append size entry
            nbt_tape_entry new_entry = {.list_size = list_size};
            tape[cur_tape_index] = new_entry;
            cur_tape_index++;
            break;
        }
        case NBT_TAG_COMPOUND:
            cur_level++;
            level_info[cur_level] = (nbt_level_info) {
                .is_list = 0,
                .prev_compound_entry = cur_tape_index
            };
            break;
        default:
            goto bail;
        }
    }

bail:
    end_timed_block();
    return tape;
}
