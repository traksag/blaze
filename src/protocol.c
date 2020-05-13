#include <string.h>
#include "codec.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

mc_int
net_read_varint(buffer_cursor * cursor) {
    mc_uint in = 0;
    unsigned char * data = cursor->buf + cursor->index;
    int remaining = cursor->limit - cursor->index;
    // first decode the first 1-4 bytes
    int end = MIN(remaining, 4);
    int i;
    for (i = 0; i < end; i++) {
        unsigned char b = data[i];
        in |= (mc_uint) (b & 0x7f) << (i * 7);

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
    in |= (mc_uint) (final & 0xf) << 28;

exit:
    cursor->index += i + 1;
    if (in <= 0x7fffffff) {
        return in;
    } else {
        return (mc_int) (in - 0x80000000) + (-0x7fffffff - 1);
    }
}

void
net_write_varint(buffer_cursor * cursor, mc_int val) {
    mc_uint out;
    // convert to two's complement representation
    if (val >= 0) {
        out = val;
    } else {
        out = (mc_uint) (val + 0x7fffffff + 1) + 0x80000000;
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
net_varint_size(mc_int val) {
    // @TODO(traks) The current implementation of this function can probably be
    // optimised quite a bit. Maybe use an instruction to get the highest set
    // bit, then divide by 7.

    mc_uint x;
    // convert to two's complement representation
    if (val >= 0) {
        x = val;
    } else {
        x = (mc_uint) (val + 0x7fffffff + 1) + 0x80000000;
    }

    int res = 1;
    while (x > 0x7f) {
        x >>= 7;
        res++;
    }
    return res;
}

mc_ushort
net_read_ushort(buffer_cursor * cursor) {
    if (cursor->limit - cursor->index < 2) {
        cursor->error = 1;
        return 0;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    mc_ushort res = 0;
    res |= (mc_ushort) buf[0] << 8;
    res |= (mc_ushort) buf[1];
    cursor->index += 2;
    return res;
}

mc_ulong
net_read_ulong(buffer_cursor * cursor) {
    if (cursor->limit - cursor->index < 8) {
        cursor->error = 1;
        return 0;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    mc_ulong res = 0;
    res |= (mc_ulong) buf[0] << 54;
    res |= (mc_ulong) buf[1] << 48;
    res |= (mc_ulong) buf[2] << 40;
    res |= (mc_ulong) buf[3] << 32;
    res |= (mc_ulong) buf[4] << 24;
    res |= (mc_ulong) buf[5] << 16;
    res |= (mc_ulong) buf[6] << 8;
    res |= (mc_ulong) buf[7];
    cursor->index += 8;
    return res;
}

void
net_write_ulong(buffer_cursor * cursor, mc_ulong val) {
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
net_write_uint(buffer_cursor * cursor, mc_uint val) {
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

mc_ubyte
net_read_ubyte(buffer_cursor * cursor) {
    if (cursor->limit - cursor->index < 1) {
        cursor->error = 1;
        return 0;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    mc_ubyte res = buf[0];
    cursor->index += 1;
    return res;
}

void
net_write_ubyte(buffer_cursor * cursor, mc_ubyte val) {
    if (cursor->limit - cursor->index < 1) {
        cursor->error = 1;
        return;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    buf[0] = val & 0xff;
    cursor->index += 1;
}

net_string
net_read_string(buffer_cursor * cursor, mc_int max_size) {
    mc_int size = net_read_varint(cursor);
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
net_write_string(buffer_cursor * cursor, net_string val) {
    net_write_varint(cursor, val.size);
    if (cursor->limit - cursor->index < val.size) {
        cursor->error = 1;
        return;
    }
    memcpy(cursor->buf + cursor->index, val.ptr, val.size);
    cursor->index += val.size;
}
