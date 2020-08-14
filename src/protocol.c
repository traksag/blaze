#include <assert.h>
#include <string.h>
#include <math.h>
#include "shared.h"

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

mc_long
net_read_varlong(buffer_cursor * cursor) {
    mc_ulong in = 0;
    unsigned char * data = cursor->buf + cursor->index;
    int remaining = cursor->limit - cursor->index;
    // first decode the first 1-9 bytes
    int end = MIN(remaining, 9);
    int i;
    for (i = 0; i < end; i++) {
        unsigned char b = data[i];
        in |= (mc_ulong) (b & 0x7f) << (i * 7);

        if ((b & 0x80) == 0) {
            // final byte marker found
            goto exit;
        }
    }

    // The first bytes were decoded. If we reached the end of the buffer, it is
    // missing the final 10th byte.
    if (remaining < 10) {
        cursor->error = 1;
        return 0;
    }
    unsigned char final = data[9];
    in |= (mc_ulong) (final & 0x1) << 63;

exit:
    cursor->index += i + 1;
    if (in <= 0x7fffffffffffffff) {
        return in;
    } else {
        return (mc_long) (in - 0x8000000000000000) + (-0x7fffffffffffffff - 1);
    }
}

void
net_write_varlong(buffer_cursor * cursor, mc_long val) {
    mc_ulong out;
    // convert to two's complement representation
    if (val >= 0) {
        out = val;
    } else {
        out = (mc_ulong) (val + 0x7fffffffffffffff + 1) + 0x8000000000000000;
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

mc_int
net_read_int(buffer_cursor * cursor) {
    mc_uint in = net_read_uint(cursor);
    if (in <= 0x7fffffff) {
        return in;
    } else {
        return (mc_int) (in - 0x80000000) + (-0x7fffffff - 1);
    }
}

void
net_write_int(buffer_cursor * cursor, mc_int val) {
    mc_uint out;
    // convert to two's complement representation
    if (val >= 0) {
        out = val;
    } else {
        out = (mc_uint) (val + 0x7fffffff + 1) + 0x80000000;
    }
    net_write_uint(cursor, out);
}

mc_short
net_read_short(buffer_cursor * cursor) {
    mc_ushort in = net_read_ushort(cursor);
    if (in <= 0x7fff) {
        return in;
    } else {
        return (mc_short) (in - 0x8000) + (-0x7fff - 1);
    }
}

void
net_write_short(buffer_cursor * cursor, mc_short val) {
    mc_ushort out;
    // convert to two's complement representation
    if (val >= 0) {
        out = val;
    } else {
        out = (mc_uint) (val + 0x7fff + 1) + 0x8000;
    }
    net_write_ushort(cursor, out);
}

mc_byte
net_read_byte(buffer_cursor * cursor) {
    mc_ubyte in = net_read_ubyte(cursor);
    if (in <= 0x7f) {
        return in;
    } else {
        return (mc_byte) (in - 0x80) + (-0x7f - 1);
    }
}

void
net_write_byte(buffer_cursor * cursor, mc_byte val) {
    mc_ubyte out;
    // convert to two's complement representation
    if (val >= 0) {
        out = val;
    } else {
        out = (mc_ubyte) (val + 0x7f + 1) + 0x80;
    }
    net_write_ubyte(cursor, out);
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

void
net_write_ushort(buffer_cursor * cursor, mc_ushort val) {
    if (cursor->limit - cursor->index < 2) {
        cursor->error = 1;
        return;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    buf[0] = (val >> 8) & 0xff;
    buf[1] = (val >> 0) & 0xff;
    cursor->index += 2;
}

mc_ulong
net_read_ulong(buffer_cursor * cursor) {
    if (cursor->limit - cursor->index < 8) {
        cursor->error = 1;
        return 0;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    mc_ulong res = 0;
    res |= (mc_ulong) buf[0] << 56;
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

mc_uint
net_read_uint(buffer_cursor * cursor) {
    if (cursor->limit - cursor->index < 4) {
        cursor->error = 1;
        return 0;
    }

    unsigned char * buf = cursor->buf + cursor->index;
    mc_uint res = 0;
    res |= (mc_uint) buf[0] << 24;
    res |= (mc_uint) buf[1] << 16;
    res |= (mc_uint) buf[2] << 8;
    res |= (mc_uint) buf[3];
    cursor->index += 4;
    return res;
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

mc_float
net_read_float(buffer_cursor * cursor) {
    mc_uint in = net_read_uint(cursor);
    int encoded_e = (in >> 23) & 0xff;
    mc_int significand = in & 0x7fffff;
    mc_uint sign = in & 0x80000000;

    if (encoded_e == 0) {
        if (significand == 0) {
            return 0;
        } else {
            // subnormal number
            significand = sign ? -significand : significand;
            return ldexpf(significand, encoded_e - 127 - 23);
        }
    } else if (encoded_e == 0x7ff) {
        if (significand == 0) {
            return sign ? -INFINITY : INFINITY;
        } else {
            return NAN;
        }
    } else {
        // normal number
        significand |= 0x800000;
        significand = sign ? -significand : significand;
        return ldexp(significand, encoded_e - 127 - 23);
    }
}

static void
net_write_float_zero(buffer_cursor * cursor, int neg) {
    net_write_uint(cursor, neg ? 0x80000000 : 0);
}

static void
net_write_float_inf(buffer_cursor * cursor, int neg) {
    net_write_uint(cursor, neg ? 0xff800000 : 0x7f800000);
}

void
net_write_float(buffer_cursor * cursor, mc_float val) {
    switch (fpclassify(val)) {
    case FP_NORMAL:
    case FP_SUBNORMAL: {
        int e;
        // The normalised value is in the range [0.5, 1). For example, "1 * 2^0"
        // becomes "0.5 * 2^1". However, the significand in the IEEE 754
        // specification is "1.xxxx...", i.e. the exponent "e" is one higher
        // than the one we need for the IEEE 754 representation.
        mc_float normalised = frexpf(val, &e);
        int encoded_e = e - 1 + 127;

        if (encoded_e >= 0xff) {
            net_write_float_inf(cursor, signbit(val));
            break;
        }
        if (encoded_e < 0) {
            net_write_float_zero(cursor, signbit(val));
            break;
        }

        mc_int significand = ldexpf(normalised, 24);
        mc_uint out;

        if (significand < 0) {
            significand = -significand;
            out = 0x80000000;
        } else {
            out = 0;
        }

        assert(significand >= (int_least32_t) 1 << 23);
        assert(significand < (int_least32_t) 1 << 24);

        if (encoded_e > 0) {
            // encode normal
            significand -= (int_least32_t) 1 << 23;
        } else {
            // encode subnormal
            significand >>= 1;
        }

        out |= (uint_least32_t) encoded_e << 23;
        out |= significand;
        net_write_uint(cursor, out);
        break;
    }
    case FP_ZERO:
        net_write_float_zero(cursor, signbit(val));
        break;
    case FP_INFINITE:
        net_write_float_inf(cursor, signbit(val));
        break;
    case FP_NAN:
        net_write_uint(cursor, signbit(val) ? 0xff800001 : 0x7f800001);
        break;
    default:
        cursor->error = 1;
    }
}

mc_double
net_read_double(buffer_cursor * cursor) {
    mc_ulong in = net_read_ulong(cursor);
    int encoded_e = (in >> 52) & 0x7ff;
    mc_long significand = in & 0xfffffffffffff;
    mc_ulong sign = in & 0x8000000000000000;

    if (encoded_e == 0) {
        if (significand == 0) {
            return 0;
        } else {
            // subnormal number
            significand = sign ? -significand : significand;
            return ldexp(significand, encoded_e - 1023 - 52);
        }
    } else if (encoded_e == 0x7ff) {
        if (significand == 0) {
            return sign ? -INFINITY : INFINITY;
        } else {
            return NAN;
        }
    } else {
        // normal number
        significand |= 0x10000000000000;
        significand = sign ? -significand : significand;
        return ldexp(significand, encoded_e - 1023 - 52);
    }
}

static void
net_write_double_zero(buffer_cursor * cursor, int neg) {
    net_write_ulong(cursor, neg ? 0x8000000000000000 : 0);
}

static void
net_write_double_inf(buffer_cursor * cursor, int neg) {
    net_write_ulong(cursor, neg ? 0xfff0000000000000 : 0x7ff0000000000000);
}

void
net_write_double(buffer_cursor * cursor, mc_double val) {
    // @TODO We use functions that depend on the actual type mc_double is.
    switch (fpclassify(val)) {
    case FP_NORMAL:
    case FP_SUBNORMAL: {
        int e;
        // The normalised value is in the range [0.5, 1). For example, "1 * 2^0"
        // becomes "0.5 * 2^1". However, the significand in the IEEE 754
        // specification is "1.xxxx...", i.e. the exponent "e" is one higher
        // than the one we need for the IEEE 754 representation.
        mc_double normalised = frexp(val, &e);
        int encoded_e = e - 1 + 1023;

        if (encoded_e >= 0x7ff) {
            net_write_double_inf(cursor, signbit(val));
            break;
        }
        if (encoded_e < 0) {
            net_write_double_zero(cursor, signbit(val));
            break;
        }

        mc_long significand = ldexp(normalised, 53);
        mc_ulong out;

        if (significand < 0) {
            significand = -significand;
            out = 0x8000000000000000;
        } else {
            out = 0;
        }

        assert(significand >= (int_least64_t) 1 << 52);
        assert(significand < (int_least64_t) 1 << 53);

        if (encoded_e > 0) {
            // encode normal
            significand -= (int_least64_t) 1 << 52;
        } else {
            // encode subnormal
            significand >>= 1;
        }

        out |= (uint_least64_t) encoded_e << 52;
        out |= significand;
        net_write_ulong(cursor, out);
        break;
    }
    case FP_ZERO:
        net_write_double_zero(cursor, signbit(val));
        break;
    case FP_INFINITE:
        net_write_double_inf(cursor, signbit(val));
        break;
    case FP_NAN:
        net_write_ulong(cursor, signbit(val) ?
                0xfff0000000000001 : 0x7ff0000000000001);
        break;
    default:
        cursor->error = 1;
    }
}

net_block_pos
net_read_block_pos(buffer_cursor * cursor) {
    mc_ulong val = net_read_ulong(cursor);

    // @TODO(traks) make this faster
    mc_int x = val >> 38;
    mc_int y = val & 0xfff;
    mc_int z = (val >> 12) & 0x3ffffff;

    if (x >= 0x2000000) {
        x = x - 0x3ffffff - 1;
    }
    if (z >= 0x2000000) {
        z = z - 0x3ffffff - 1;
    }

    net_block_pos res = {
        .x = x,
        .y = y,
        .z = z
    };
    return res;
}

void
net_write_data(buffer_cursor * cursor, void * restrict src, size_t size) {
    if (cursor->limit - cursor->index < size) {
        cursor->error = 1;
        return;
    }

    memcpy(cursor->buf + cursor->index, src, size);
    cursor->index += size;
}
