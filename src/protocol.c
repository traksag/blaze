#include <assert.h>
#include <string.h>
#include <math.h>
#include "shared.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

// @TODO(traks) when reading signed ints (i.e. converting uint to int), we still
// determine the two's complement value explicitly. Maybe there's a better way.
// Is unsigned to signed casting well defined?

i32
net_read_varint(BufferCursor * cursor) {
    u32 in = 0;
    unsigned char * data = cursor->data + cursor->index;
    int remaining = cursor->size - cursor->index;
    // first decode the first 1-4 bytes
    int end = MIN(remaining, 4);
    int i;
    for (i = 0; i < end; i++) {
        unsigned char b = data[i];
        in |= (u32) (b & 0x7f) << (i * 7);

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
    in |= (u32) (final & 0xf) << 28;

exit:
    cursor->index += i + 1;
    if (in <= 0x7fffffff) {
        return in;
    } else {
        return (i32) (in - 0x80000000) + (-0x7fffffff - 1);
    }
}

void
net_write_varint(BufferCursor * cursor, i32 val) {
    u32 out = val;
    int remaining = cursor->size - cursor->index;
    unsigned char * data = cursor->data + cursor->index;

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
net_varint_size(i32 val) {
    // @TODO(traks) The current implementation of this function can probably be
    // optimised quite a bit. Maybe use an instruction to get the highest set
    // bit, then divide by 7.

    u32 x = val;
    int res = 1;
    while (x > 0x7f) {
        x >>= 7;
        res++;
    }
    return res;
}

i64
net_read_varlong(BufferCursor * cursor) {
    u64 in = 0;
    unsigned char * data = cursor->data + cursor->index;
    int remaining = cursor->size - cursor->index;
    // first decode the first 1-9 bytes
    int end = MIN(remaining, 9);
    int i;
    for (i = 0; i < end; i++) {
        unsigned char b = data[i];
        in |= (u64) (b & 0x7f) << (i * 7);

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
    in |= (u64) (final & 0x1) << 63;

exit:
    cursor->index += i + 1;
    if (in <= 0x7fffffffffffffff) {
        return in;
    } else {
        return (i64) (in - 0x8000000000000000) + (-0x7fffffffffffffff - 1);
    }
}

void
net_write_varlong(BufferCursor * cursor, i64 val) {
    u64 out = val;
    int remaining = cursor->size - cursor->index;
    unsigned char * data = cursor->data + cursor->index;

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

i32
net_read_int(BufferCursor * cursor) {
    u32 in = net_read_uint(cursor);
    if (in <= 0x7fffffff) {
        return in;
    } else {
        return (i32) (in - 0x80000000) + (-0x7fffffff - 1);
    }
}

void
net_write_int(BufferCursor * cursor, i32 val) {
    net_write_uint(cursor, val);
}

i16
net_read_short(BufferCursor * cursor) {
    u16 in = net_read_ushort(cursor);
    if (in <= 0x7fff) {
        return in;
    } else {
        return (i16) (in - 0x8000) + (-0x7fff - 1);
    }
}

void
net_write_short(BufferCursor * cursor, i16 val) {
    net_write_ushort(cursor, val);
}

i8
net_read_byte(BufferCursor * cursor) {
    u8 in = net_read_ubyte(cursor);
    if (in <= 0x7f) {
        return in;
    } else {
        return (i8) (in - 0x80) + (-0x7f - 1);
    }
}

void
net_write_byte(BufferCursor * cursor, i8 val) {
    net_write_ubyte(cursor, val);
}

i64
net_read_long(BufferCursor * cursor) {
    u64 in = net_read_ulong(cursor);
    if (in <= 0x7fffffffffffffff) {
        return in;
    } else {
        return (i64) (in - 0x8000000000000000) + (-0x7fffffffffffffff - 1);
    }
}

void
net_write_long(BufferCursor * cursor, i64 val) {
    net_write_ulong(cursor, val);
}

u16
net_read_ushort(BufferCursor * cursor) {
    if (cursor->size - cursor->index < 2) {
        cursor->error = 1;
        return 0;
    }

    unsigned char * buf = cursor->data + cursor->index;
    u16 res = 0;
    res |= (u16) buf[0] << 8;
    res |= (u16) buf[1];
    cursor->index += 2;
    return res;
}

void
net_write_ushort(BufferCursor * cursor, u16 val) {
    if (cursor->size - cursor->index < 2) {
        cursor->error = 1;
        return;
    }

    unsigned char * buf = cursor->data + cursor->index;
    buf[0] = (val >> 8) & 0xff;
    buf[1] = (val >> 0) & 0xff;
    cursor->index += 2;
}

u64
net_read_ulong(BufferCursor * cursor) {
    if (cursor->size - cursor->index < 8) {
        cursor->error = 1;
        return 0;
    }

    unsigned char * buf = cursor->data + cursor->index;
    u64 res = 0;
    res |= (u64) buf[0] << 56;
    res |= (u64) buf[1] << 48;
    res |= (u64) buf[2] << 40;
    res |= (u64) buf[3] << 32;
    res |= (u64) buf[4] << 24;
    res |= (u64) buf[5] << 16;
    res |= (u64) buf[6] << 8;
    res |= (u64) buf[7];
    cursor->index += 8;
    return res;
}

void
net_write_ulong(BufferCursor * cursor, u64 val) {
    if (cursor->size - cursor->index < 8) {
        cursor->error = 1;
        return;
    }

    unsigned char * buf = cursor->data + cursor->index;
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

u32
net_read_uint(BufferCursor * cursor) {
    if (cursor->size - cursor->index < 4) {
        cursor->error = 1;
        return 0;
    }

    unsigned char * buf = cursor->data + cursor->index;
    u32 res = 0;
    res |= (u32) buf[0] << 24;
    res |= (u32) buf[1] << 16;
    res |= (u32) buf[2] << 8;
    res |= (u32) buf[3];
    cursor->index += 4;
    return res;
}

void
net_write_uint(BufferCursor * cursor, u32 val) {
    if (cursor->size - cursor->index < 4) {
        cursor->error = 1;
        return;
    }

    unsigned char * buf = cursor->data + cursor->index;
    buf[0] = (val >> 24) & 0xff;
    buf[1] = (val >> 16) & 0xff;
    buf[2] = (val >> 8) & 0xff;
    buf[3] = (val >> 0) & 0xff;
    cursor->index += 4;
}

u8
net_read_ubyte(BufferCursor * cursor) {
    if (cursor->size - cursor->index < 1) {
        cursor->error = 1;
        return 0;
    }

    unsigned char * buf = cursor->data + cursor->index;
    u8 res = buf[0];
    cursor->index += 1;
    return res;
}

void
net_write_ubyte(BufferCursor * cursor, u8 val) {
    if (cursor->size - cursor->index < 1) {
        cursor->error = 1;
        return;
    }

    unsigned char * buf = cursor->data + cursor->index;
    buf[0] = val & 0xff;
    cursor->index += 1;
}

String
net_read_string(BufferCursor * cursor, i32 max_size) {
    i32 size = net_read_varint(cursor);
    String res = {0};
    if (size < 0 || size > cursor->size - cursor->index || size > max_size) {
        cursor->error = 1;
        return res;
    }

    res.size = size;
    res.data = cursor->data + cursor->index;
    cursor->index += size;
    return res;
}

void
net_write_string(BufferCursor * cursor, String val) {
    net_write_varint(cursor, val.size);
    if (cursor->size - cursor->index < val.size) {
        cursor->error = 1;
        return;
    }
    memcpy(cursor->data + cursor->index, val.data, val.size);
    cursor->index += val.size;
}

// @TODO(traks) can we simplify these float and double codec functions? It is
// not unreasonable to assume all our target platforms use IEEE floats and
// doubles, so maybe we can just access the bytes a double/float directly? I'm
// not sure if any of this is UB though... Should also make sure weird floating
// point representations don't cause signals or whatever to be fired. Should
// probably filter out all non-finite representations too.

float
net_read_float(BufferCursor * cursor) {
    u32 in = net_read_uint(cursor);
    int encoded_e = (in >> 23) & 0xff;
    i32 significand = in & 0x7fffff;
    u32 sign = in & 0x80000000;

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
net_write_float_zero(BufferCursor * cursor, int neg) {
    net_write_uint(cursor, neg ? 0x80000000 : 0);
}

static void
net_write_float_inf(BufferCursor * cursor, int neg) {
    net_write_uint(cursor, neg ? 0xff800000 : 0x7f800000);
}

void
net_write_float(BufferCursor * cursor, float val) {
    switch (fpclassify(val)) {
    case FP_NORMAL:
    case FP_SUBNORMAL: {
        int e;
        // The normalised value is in the range [0.5, 1). For example, "1 * 2^0"
        // becomes "0.5 * 2^1". However, the significand in the IEEE 754
        // specification is "1.xxxx...", i.e. the exponent "e" is one higher
        // than the one we need for the IEEE 754 representation.
        float normalised = frexpf(val, &e);
        int encoded_e = e - 1 + 127;

        if (encoded_e >= 0xff) {
            net_write_float_inf(cursor, signbit(val));
            break;
        }
        if (encoded_e < 0) {
            net_write_float_zero(cursor, signbit(val));
            break;
        }

        i32 significand = ldexpf(normalised, 24);
        u32 out;

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

double
net_read_double(BufferCursor * cursor) {
    u64 in = net_read_ulong(cursor);
    int encoded_e = (in >> 52) & 0x7ff;
    i64 significand = in & 0xfffffffffffff;
    u64 sign = in & 0x8000000000000000;

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
net_write_double_zero(BufferCursor * cursor, int neg) {
    net_write_ulong(cursor, neg ? 0x8000000000000000 : 0);
}

static void
net_write_double_inf(BufferCursor * cursor, int neg) {
    net_write_ulong(cursor, neg ? 0xfff0000000000000 : 0x7ff0000000000000);
}

void
net_write_double(BufferCursor * cursor, double val) {
    // @TODO We use functions that depend on the actual type double is.
    switch (fpclassify(val)) {
    case FP_NORMAL:
    case FP_SUBNORMAL: {
        int e;
        // The normalised value is in the range [0.5, 1). For example, "1 * 2^0"
        // becomes "0.5 * 2^1". However, the significand in the IEEE 754
        // specification is "1.xxxx...", i.e. the exponent "e" is one higher
        // than the one we need for the IEEE 754 representation.
        double normalised = frexp(val, &e);
        int encoded_e = e - 1 + 1023;

        if (encoded_e >= 0x7ff) {
            net_write_double_inf(cursor, signbit(val));
            break;
        }
        if (encoded_e < 0) {
            net_write_double_zero(cursor, signbit(val));
            break;
        }

        i64 significand = ldexp(normalised, 53);
        u64 out;

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

BlockPos
net_read_block_pos(BufferCursor * cursor) {
    i64 val = net_read_long(cursor);
    i32 x = val >> 38;
    i32 y = (val << 52) >> 52;
    i32 z = (val << 26) >> 38;
    BlockPos res = {.x = x, .y = y, .z = z};
    return res;
}

void
net_write_block_pos(BufferCursor * cursor, BlockPos val) {
    i64 x = (i64) val.x & 0x3ffffff;
    i64 y = (i64) val.y & 0xfff;
    i64 z = (i64) val.z & 0x3ffffff;
    i64 out = (x << 38) | (z << 12) | y;
    net_write_long(cursor, out);
}

void
net_write_data(BufferCursor * cursor, void * restrict src, size_t size) {
    if (cursor->size - cursor->index < size) {
        cursor->error = 1;
        return;
    }

    memcpy(cursor->data + cursor->index, src, size);
    cursor->index += size;
}
