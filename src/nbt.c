#include <string.h>
#include <stdio.h>
#include "shared.h"

typedef struct {
    unsigned char is_list;
    unsigned char element_tag;
    mc_ushort prev_compound_entry;
    mc_uint list_elems_remaining;
} nbt_level_info;

typedef struct {
    unsigned char is_list;
    unsigned char element_tag;
    mc_uint list_size;
    mc_uint list_index;
    mc_uint entry_index;
} printer_level_info;

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
load_nbt(buffer_cursor * cursor, memory_arena * arena, int max_levels) {
    // Currently the tape format is as follows:
    //
    //  * An entry inside a compound has an NBT tag indicating the type and a
    //    buffer index pointing into the buffer starting at the key size, then
    //    the key, and then the tag's value. If the NBT tag is a list, then the
    //    element tag is set.
    //  * Every compounds ends with an end tag (without no buffer index).
    //  * After non-end tags inside a compound, there's a tape entry with
    //    next_compound_entry set to the absolute index of the next compound
    //    entry in the tape.
    //  * After list tags inside a compound, there's of course first a
    //    next_compound_entry tape entry, but after that there's also a tape
    //    entry with list_size set.
    //  * A list containing only 'primitive' (non-compound and non-list)
    //    elements doesn't have any additional tape entries. The contents can
    //    simply be read linearly from the buffer.
    //  * Lists of compounds consist of the above header and after that all the
    //    tape entries in the various compounds as described above for
    //    compounds.
    //  * Lists of lists consist of the above header and an additional header
    //    consisting of a tape entry for the element tag and a tape entry for
    //    the list size. In case a sublist contains primitive elements, the
    //    buffer_index points to the NBT tags inside the buffer.

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
            (max_levels + 1) * sizeof *level_info);
    int cur_tape_index = 0;
    int cur_level = 0;
    level_info[0] = (nbt_level_info) {0};

    mc_ubyte tag = net_read_ubyte(cursor);
    int error = 0;

    if (tag == NBT_TAG_END) {
        // no nbt data
        tape[cur_tape_index] = (nbt_tape_entry) {.tag = NBT_TAG_END};
        goto bail;
    } else if (tag != NBT_TAG_COMPOUND) {
        logs("Root tag not a compound");
        error = 1;
        goto bail;
    }

    // skip key of root compound
    mc_ushort key_size = net_read_ushort(cursor);
    if (key_size > cursor->limit - cursor->index) {
        logs("Key too long: %ju", (uintmax_t) key_size);
        error = 1;
        goto bail;
    }
    cursor->index += key_size;

    for (;;) {
        if (cur_tape_index >= max_entries - 10) {
            logs("Max NBT tape index reached");
            error = 1;
            goto bail;
        }
        if (cur_level == max_levels) {
            logs("Max NBT level reached");
            error = 1;
            goto bail;
        }

        nbt_tape_entry * base_entry = NULL;

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
                logs("Key too long: %ju", (uintmax_t) key_size);
                error = 1;
                goto bail;
            }
            cursor->index += key_size;

            level_info[cur_level].prev_compound_entry = cur_tape_index;
            base_entry = tape + cur_tape_index;
            *base_entry = (nbt_tape_entry) {
                .buffer_index = entry_start,
                .tag = tag
            };
            cur_tape_index++;
            // increment another time for the next pointer
            cur_tape_index++;
        } else {
            // list
            if (level_info[cur_level].list_elems_remaining == 0) {
                cur_level--;
                continue;
            }

            level_info[cur_level].list_elems_remaining--;
            tag = level_info[cur_level].element_tag;

            if (tag == NBT_TAG_LIST) {
                base_entry = tape + cur_tape_index;
                *base_entry = (nbt_tape_entry) {
                    .buffer_index = cursor->index
                };
                cur_tape_index++;
            }
        }

        static mc_byte elem_bytes[] = {0, 1, 2, 4, 8, 4, 8};
        static mc_byte array_elem_bytes[] = {1, 0, 0, 0, 4, 8};
        switch (tag) {
        case NBT_TAG_BYTE:
        case NBT_TAG_SHORT:
        case NBT_TAG_INT:
        case NBT_TAG_LONG:
        case NBT_TAG_FLOAT:
        case NBT_TAG_DOUBLE: {
            int bytes = elem_bytes[tag];
            if (cursor->index > cursor->limit - bytes) {
                logs("NBT value overflows buffer");
                error = 1;
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
                logs("NBT value overflows buffer");
                error = 1;
                goto bail;
            } else {
                cursor->index += elem_bytes * array_size;
            }
            break;
        }
        case NBT_TAG_STRING: {
            mc_ushort size = net_read_ushort(cursor);
            if (cursor->index > cursor->limit - size) {
                logs("NBT value overflows buffer");
                error = 1;
                goto bail;
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

            base_entry->element_tag = element_tag;

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
            logs("Unknown tag: %ju", (uintmax_t) tag);
            error = 1;
            goto bail;
        }
    }

bail:
    if (error) {
        // in case of errors, return a tape with a single end tag, so users can
        // ignore errors and use the returned tape without worrying about
        // incomplete tapes
        tape[0] = (nbt_tape_entry) {.tag = NBT_TAG_END};
        cursor->error = error;
    }

    end_timed_block();
    return tape;
}

void
print_nbt(nbt_tape_entry * tape, buffer_cursor * cursor,
        memory_arena * arena, int max_levels) {
    int cur_tape_index = 0;
    int cur_level = 0;

    printer_level_info * level_info = alloc_in_arena(arena,
            (max_levels + 1) * sizeof *level_info);
    level_info[0] = (printer_level_info) {0};

    for (;;) {
        assert(cur_level < max_levels);

        nbt_tape_entry * base_entry = NULL;
        mc_ubyte tag;

        if (!level_info[cur_level].is_list) {
            // inside compound
            base_entry = tape + cur_tape_index;
            cur_tape_index++;
            tag = base_entry->tag;

            if (level_info[cur_level].entry_index == 0) {
                printf("{");
            }

            if (tag == NBT_TAG_END) {
                printf("}");
                cur_level--;
                if (cur_level == -1) {
                    break;
                }
                continue;
            }

            if (level_info[cur_level].entry_index > 0) {
                printf(",");
            }

            level_info[cur_level].entry_index++;

            cursor->index = base_entry->buffer_index;
            mc_ushort key_size = net_read_ushort(cursor);
            unsigned char * key = cursor->buf + cursor->index;
            cursor->index += key_size;
            printf("\"%.*s\":", (int) key_size, key);

            // skip the tape entry with next_compound_entry set
            cur_tape_index++;
        } else {
            // inside list
            if (level_info[cur_level].list_index == 0) {
                printf("[");
            }

            if (level_info[cur_level].list_index == level_info[cur_level].list_size) {
                printf("]");
                cur_level--;
                continue;
            }

            if (level_info[cur_level].list_index > 0) {
                printf(",");
            }

            level_info[cur_level].list_index++;
            tag = level_info[cur_level].element_tag;

            if (tag == NBT_TAG_LIST) {
                base_entry = tape + cur_tape_index;
                cur_tape_index++;
                cursor->index = base_entry->buffer_index;
            }
        }

        switch (tag) {
        case NBT_TAG_BYTE: {
            mc_ubyte val = net_read_ubyte(cursor);
            printf("%ju", (uintmax_t) val);
            break;
        }
        case NBT_TAG_SHORT: {
            mc_ushort val = net_read_ushort(cursor);
            printf("%ju", (uintmax_t) val);
            break;
        }
        case NBT_TAG_INT: {
            mc_int val = net_read_int(cursor);
            printf("%jd", (intmax_t) val);
            break;
        }
        case NBT_TAG_LONG: {
            mc_ulong val = net_read_ulong(cursor);
            printf("%ju", (uintmax_t) val);
            break;
        }
        case NBT_TAG_FLOAT: {
            mc_float val = net_read_float(cursor);
            printf("%f", val);
            break;
        }
        case NBT_TAG_DOUBLE: {
            mc_double val = net_read_double(cursor);
            printf("%f", val);
            break;
        }
        case NBT_TAG_BYTE_ARRAY: {
            // too much data to print
            printf("[bytes]");
            break;
        }
        case NBT_TAG_STRING: {
            mc_ushort val_size = net_read_ushort(cursor);
            unsigned char * val = cursor->buf + cursor->index;
            printf("\"%.*s\"", (int) val_size, val);
            break;
        }
        case NBT_TAG_LIST: {
            mc_uint list_size = tape[cur_tape_index].list_size;
            cur_tape_index++;

            cur_level++;
            level_info[cur_level] = (printer_level_info) {
                .is_list = 1,
                .element_tag = base_entry->element_tag,
                .list_size = list_size
            };
            break;
        }
        case NBT_TAG_COMPOUND: {
            cur_level++;
            level_info[cur_level] = (printer_level_info) {0};
            break;
        }
        case NBT_TAG_INT_ARRAY: {
            // too much data to print
            printf("[ints]");
            break;
        }
        case NBT_TAG_LONG_ARRAY:
            // too much data to print
            printf("[longs]");
            break;
        }
    }

    printf("\n");
}
