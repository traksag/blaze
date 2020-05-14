#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>

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
net_read_varint(buffer_cursor * cursor);

void
net_write_varint(buffer_cursor * cursor, mc_int val);

int
net_varint_size(mc_int val);

mc_int
net_read_int(buffer_cursor * cursor);

void
net_write_int(buffer_cursor * cursor, mc_int val);

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

#endif
