#include <string.h>
#include "codec.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

int32_t
read_varint(buffer_cursor * cursor) {
    uint32_t in = 0;
    unsigned char * data = cursor->buf + cursor->index;
    int remaining = cursor->limit - cursor->index;
    // first decode the first 1-4 bytes
    int end = MIN(remaining, 4);
    int i;
    for (i = 0; i < end; i++) {
        unsigned char b = data[i];
        in |= (uint32_t) (b & 0x7f) << (i * 7);

        if ((b & 0x80) == 0) {
            // final byte marker found
            goto exit;
        }
    }

    // The first bytes were decoded. If we reached the end of the buffer, it is
    // missing the final 5th byte.
    if (remaining < 5) {
        cursor->error = 1;
        return 0;
    }
    unsigned char final = data[4];
    in |= (uint_least32_t) (final & 0xf) << 28;

exit:
    cursor->index += i + 1;
    if (in <= 0x7fffffff) {
        return in;
    } else {
        return (int32_t) (in - 0x80000000) + (-0x7fffffff - 1);
    }
}

void
write_varint(buffer_cursor * cursor, int32_t val) {
    uint32_t out;
    // convert to two's complement representation
    if (val >= 0) {
        out = val;
    } else {
        out = (uint32_t) (val + 0x7fffffff + 1) + 0x80000000;
    }

    int remaining = cursor->limit - cursor->index;
    unsigned char * data = cursor->buf + cursor->index;

    // write each block of 7 bits until no more are necessary
    int i = 0;
    for (;;) {
        if (i >= remaining) {
            cursor->error = 1;
            return;
        }
        if (out <= 0x7f) {
            data[i] = out;
            cursor->index += i + 1;
            break;
        } else {
            data[i] = 0x80 | (out & 0x7f);
            out >>= 7;
            i++;
        }
    }
}

int
varint_size(int32_t val) {
    // @TODO(traks) The current implementation of this function can probably be
    // optimised quite a bit. Maybe use an instruction to get the highest set
    // bit, then divide by 7.

    uint32_t x;
    // convert to two's complement representation
    if (val >= 0) {
        x = val;
    } else {
        x = (uint32_t) (val + 0x7fffffff + 1) + 0x80000000;
    }

    int res = 1;
    while (x > 0x7f) {
        x >>= 7;
        res++;
    }
    return res;
}

uint16_t
read_ushort(buffer_cursor * cursor) {
    if (cursor->limit - cursor->index < 2) {
        cursor->error = 1;
        return 0;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    uint16_t res = 0;
    res |= (uint16_t) buf[0] << 8;
    res |= (uint16_t) buf[1];
    cursor->index += 2;
    return res;
}

uint64_t
read_ulong(buffer_cursor * cursor) {
    if (cursor->limit - cursor->index < 8) {
        cursor->error = 1;
        return 0;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    uint64_t res = 0;
    res |= (uint64_t) buf[0] << 54;
    res |= (uint64_t) buf[1] << 48;
    res |= (uint64_t) buf[2] << 40;
    res |= (uint64_t) buf[3] << 32;
    res |= (uint64_t) buf[4] << 24;
    res |= (uint64_t) buf[5] << 16;
    res |= (uint64_t) buf[6] << 8;
    res |= (uint64_t) buf[7];
    cursor->index += 8;
    return res;
}

void
write_ulong(buffer_cursor * cursor, uint64_t val) {
    if (cursor->limit - cursor->index < 8) {
        cursor->error = 1;
        return;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    buf[0] = (val >> 56) & 0xff;
    buf[1] = (val >> 48) & 0xff;
    buf[2] = (val >> 40) & 0xff;
    buf[3] = (val >> 32) & 0xff;
    buf[4] = (val >> 24) & 0xff;
    buf[5] = (val >> 16) & 0xff;
    buf[6] = (val >> 8) & 0xff;
    buf[7] = (val >> 0) & 0xff;
    cursor->index += 8;
}

void
write_uint(buffer_cursor * cursor, uint32_t val) {
    if (cursor->limit - cursor->index < 4) {
        cursor->error = 1;
        return;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    buf[0] = (val >> 24) & 0xff;
    buf[1] = (val >> 16) & 0xff;
    buf[2] = (val >> 8) & 0xff;
    buf[3] = (val >> 0) & 0xff;
    cursor->index += 4;
}

uint8_t
read_ubyte(buffer_cursor * cursor) {
    if (cursor->limit - cursor->index < 1) {
        cursor->error = 1;
        return 0;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    uint8_t res = buf[0];
    cursor->index += 1;
    return res;
}

void
write_ubyte(buffer_cursor * cursor, uint8_t val) {
    if (cursor->limit - cursor->index < 1) {
        cursor->error = 1;
        return;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    buf[0] = val & 0xff;
    cursor->index += 1;
}

net_string
read_string(buffer_cursor * cursor, int32_t max_size) {
    int32_t size = read_varint(cursor);
    net_string res = {0};
    if (size < 0 || size > cursor->limit - cursor->index || size > max_size) {
        cursor->error = 1;
        return res;
    }

    res.size = size;
    res.ptr = cursor->buf + cursor->index;
    cursor->index += size;
    return res;
}

void
write_string(buffer_cursor * cursor, net_string val) {
    write_varint(cursor, val.size);
    if (cursor->limit - cursor->index < val.size) {
        cursor->error = 1;
        return;
    }
    memcpy(cursor->buf + cursor->index, val.ptr, val.size);
    cursor->index += val.size;
}
