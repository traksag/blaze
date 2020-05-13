#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>

typedef int32_t mc_int;
typedef uint8_t mc_ubyte;
typedef uint16_t mc_ushort;
typedef uint32_t mc_uint;
typedef uint64_t mc_ulong;

typedef struct {
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

mc_int
read_varint(buffer_cursor * cursor);

void
write_varint(buffer_cursor * cursor, mc_int val);

int
varint_size(mc_int val);

mc_ushort
read_ushort(buffer_cursor * cursor);

mc_ulong
read_ulong(buffer_cursor * cursor);

void
write_ulong(buffer_cursor * cursor, mc_ulong val);

void
write_uint(buffer_cursor * cursor, mc_uint val);

mc_ubyte
read_ubyte(buffer_cursor * cursor);

void
write_ubyte(buffer_cursor * cursor, mc_ubyte val);

net_string
read_string(buffer_cursor * cursor, mc_int max_size);

void
write_string(buffer_cursor * cursor, net_string val);

#endif
