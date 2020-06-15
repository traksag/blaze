#ifndef CODEC_H
#define CODEC_H

#include <stddef.h>
#include <stdint.h>

#define ARRAY_SIZE(x) (sizeof (x) / sizeof *(x))

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef int8_t mc_byte;
typedef int16_t mc_short;
typedef int32_t mc_int;
typedef int64_t mc_long;
typedef uint8_t mc_ubyte;
typedef uint16_t mc_ushort;
typedef uint32_t mc_uint;
typedef uint64_t mc_ulong;
typedef float mc_float;
typedef double mc_double;

typedef struct {
    unsigned char * ptr;
    mc_int size;
    mc_int index;
} memory_arena;

typedef struct {
    mc_int x;
    mc_int y;
    mc_int z;
} net_block_pos;

typedef struct {
    // @TODO(traks) could the compiler think that the buffer points to some
    // buffer that contains this struct itself, meaning it has to reload fields
    // after we write something to it?
    unsigned char * buf;
    int limit;
    int index;
    int error;
} buffer_cursor;

#define NET_STRING(x) ((net_string) {.size = sizeof (x) - 1, .ptr = (x)})

typedef struct {
    mc_int size;
    void * ptr;
} net_string;

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
        mc_uint buffer_index:22;
        mc_uint tag:5;
        mc_uint element_tag:5;
    };
    mc_uint next_compound_entry;
    mc_uint list_size;
} nbt_tape_entry;

typedef struct {
    mc_short x;
    mc_short z;
} chunk_pos;

#define CHUNK_LOADED (1u << 0)

typedef struct {
    int index_in_bucket;
    mc_ushort block_states[4096];
} chunk_section;

#define CHUNK_SECTIONS_PER_BUCKET (64)

typedef struct chunk_section_bucket chunk_section_bucket;

struct chunk_section_bucket {
    chunk_section chunk_sections[CHUNK_SECTIONS_PER_BUCKET];
    // @TODO(traks) we use 2 * CHUNK_SECTIONS_PER_BUCKET 4096-byte pages for the
    // block states in the chunk sections. How much of the next page do we use?
    chunk_section_bucket * next;
    chunk_section_bucket * prev;
    int used_sections;
    // @TODO(traks) store this in longs?
    unsigned char used_map[CHUNK_SECTIONS_PER_BUCKET];
};

typedef struct {
    chunk_section * sections[16];
    mc_ushort non_air_count[16];
    // need shorts to store 257 different heights
    mc_ushort motion_blocking_height_map[256];

    // increment if you want to keep a chunk available in the map, decrement
    // if you no longer care for the chunk.
    // If = 0 the chunk will be removed from the map at some point.
    mc_uint available_interest;
    unsigned flags;

    // @TODO(traks) more changed blocks, better compression, store it per chunk
    // section perhaps. Figure out when this limit can be exceeded. I highly
    // doubt more than 16 blocks will be changed per chunk due to players except
    // if very high player density.
    mc_ushort changed_blocks[16];
    mc_ubyte changed_block_count;
} chunk;

#define CHUNKS_PER_BUCKET (32)

#define CHUNK_MAP_SIZE (1024)

typedef struct chunk_bucket chunk_bucket;

struct chunk_bucket {
    chunk_bucket * next_bucket;
    int size;
    chunk_pos positions[CHUNKS_PER_BUCKET];
    chunk chunks[CHUNKS_PER_BUCKET];
};

typedef struct {
    unsigned char value_count;
    // name size, name, value size, value, value size, value, etc.
    unsigned char tape[255];
} block_property_spec;

typedef struct {
    mc_ushort base_state;
    unsigned char property_count;
    unsigned char property_specs[8];
    unsigned char default_value_indices[8];
} block_properties;

void
logs(void * format, ...);

void
logs_errno(void * format);

void *
alloc_in_arena(memory_arena * arena, mc_int size);

mc_int
net_read_varint(buffer_cursor * cursor);

void
net_write_varint(buffer_cursor * cursor, mc_int val);

int
net_varint_size(mc_int val);

mc_int
net_read_int(buffer_cursor * cursor);

void
net_write_int(buffer_cursor * cursor, mc_int val);

mc_byte
net_read_byte(buffer_cursor * cursor);

void
net_write_byte(buffer_cursor * cursor, mc_byte val);

mc_ushort
net_read_ushort(buffer_cursor * cursor);

void
net_write_ushort(buffer_cursor * cursor, mc_ushort val);

mc_ulong
net_read_ulong(buffer_cursor * cursor);

void
net_write_ulong(buffer_cursor * cursor, mc_ulong val);

mc_uint
net_read_uint(buffer_cursor * cursor);

void
net_write_uint(buffer_cursor * cursor, mc_uint val);

mc_ubyte
net_read_ubyte(buffer_cursor * cursor);

void
net_write_ubyte(buffer_cursor * cursor, mc_ubyte val);

net_string
net_read_string(buffer_cursor * cursor, mc_int max_size);

void
net_write_string(buffer_cursor * cursor, net_string val);

mc_float
net_read_float(buffer_cursor * cursor);

void
net_write_float(buffer_cursor * cursor, mc_float val);

mc_double
net_read_double(buffer_cursor * cursor);

void
net_write_double(buffer_cursor * cursor, mc_double val);

net_block_pos
net_read_block_pos(buffer_cursor * cursor);

void
net_write_data(buffer_cursor * cursor, void * restrict src, size_t size);

mc_uint
nbt_move_to_key(net_string matcher, nbt_tape_entry * tape,
        mc_uint start_index, buffer_cursor * cursor);

nbt_tape_entry *
load_nbt(buffer_cursor * cursor, memory_arena * arena, int max_level);

void
begin_timed_block(char * name);

void
end_timed_block();

mc_short
resolve_block_type_id(net_string resource_loc);

int
find_property_value_index(block_property_spec * prop_spec, net_string val);

chunk *
get_or_create_chunk(chunk_pos pos);

chunk *
get_chunk_if_loaded(chunk_pos pos);

chunk *
get_chunk_if_available(chunk_pos pos);

void
chunk_set_block_state(chunk * ch, int x, int y, int z, mc_ushort block_state);

mc_ushort
chunk_get_block_state(chunk * ch, int x, int y, int z);

void
try_read_chunk_from_storage(chunk_pos pos, chunk * ch,
        memory_arena * scratch_arena,
        block_properties * block_properties_table,
        block_property_spec * block_property_specs);

chunk_section *
alloc_chunk_section(void);

void
free_chunk_section(chunk_section * section);

void
clean_up_unused_chunks(void);

#endif
