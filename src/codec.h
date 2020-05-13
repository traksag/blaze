#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>

typedef struct {
    unsigned char * buf;
    int limit;
    int index;
    int error;
} buffer_cursor;

#define NET_STRING(x) ((net_string) {.size = sizeof (x) - 1, .ptr = (x)})

typedef struct {
    int32_t size;
    void * ptr;
} net_string;

int32_t
read_varint(buffer_cursor * cursor);

void
write_varint(buffer_cursor * cursor, int32_t val);

int
varint_size(int32_t val);

uint16_t
read_ushort(buffer_cursor * cursor);

uint64_t
read_ulong(buffer_cursor * cursor);

void
write_ulong(buffer_cursor * cursor, uint64_t val);

void
write_uint(buffer_cursor * cursor, uint32_t val);

uint8_t
read_ubyte(buffer_cursor * cursor);

void
write_ubyte(buffer_cursor * cursor, uint8_t val);

net_string
read_string(buffer_cursor * cursor, int32_t max_size);

void
write_string(buffer_cursor * cursor, net_string val);

#endif
