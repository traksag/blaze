#ifndef NBT_H
#define NBT_H

#include "buf.h"

enum nbt_tag {
    NBT_TAG_END,
    NBT_TAG_BYTE,
    NBT_TAG_SHORT,
    NBT_TAG_INT,
    NBT_TAG_LONG,
    NBT_TAG_FLOAT,
    NBT_TAG_DOUBLE,
    NBT_TAG_BYTE_ARRAY,
    NBT_TAG_STRING,
    NBT_TAG_LIST,
    NBT_TAG_COMPOUND,
    NBT_TAG_INT_ARRAY,
    NBT_TAG_LONG_ARRAY,

    // our own tags for internal use
    NBT_TAG_LIST_END,
    NBT_TAG_COMPOUND_IN_LIST,
    NBT_TAG_LIST_IN_LIST,
};

typedef union {
    struct {
        u32 buffer_index:22;
        u32 tag:5;
        u32 element_tag:5;
    };
    u32 next_compound_entry_offset;
    u32 list_size;
} nbt_tape_entry;

nbt_tape_entry *
nbt_move_to_key(String matcher, nbt_tape_entry * tape,
        BufCursor * cursor);

String
nbt_get_string(String matcher, nbt_tape_entry * tape,
        BufCursor * cursor);

nbt_tape_entry *
nbt_get_compound(String matcher, nbt_tape_entry * tape,
        BufCursor * cursor);

nbt_tape_entry *
load_nbt(BufCursor * cursor, MemoryArena * arena, int max_level);

void
print_nbt(nbt_tape_entry * tape, BufCursor * cursor,
        MemoryArena * arena, int max_levels);

#endif
