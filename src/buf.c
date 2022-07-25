#include "buf.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

u32 CursorGetVarU32(BufCursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    int maxLen = MIN(cursor->size - cursor->index, 5);
    int len = 0;
    for (int i = 0; i < maxLen; i++) {
        if ((data[i] & 0x80) == 0) {
            len = i + 1;
            maxLen = len;
            break;
        }
    }

    // @NOTE(traks) add max length so progress is made in case length is 0.
    cursor->index += maxLen;
    // @NOTE(traks) 'or' as to not override any existing errors
    cursor->error |= !len;

    u32 res = 0;
    for (int i = 0; i < len; i++) {
        res |= (u32) (data[i] & 0x7f) << (i * 7);
    }
    return res;
}

u64 CursorGetVarU64(BufCursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    int maxLen = MIN(cursor->size - cursor->index, 10);
    int len = 0;
    for (int i = 0; i < maxLen; i++) {
        if ((data[i] & 0x80) == 0) {
            len = i + 1;
            maxLen = len;
            break;
        }
    }

    // @NOTE(traks) add max length so progress is made in case length is 0
    cursor->index += maxLen;
    // @NOTE(traks) 'or' as to not override any existing errors
    cursor->error |= !len;

    u64 res = 0;
    for (int i = 0; i < len; i++) {
        res |= (u64) (data[i] & 0x7f) << (i * 7);
    }
    return res;
}

u8 CursorGetU8(BufCursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    return CursorSkip(cursor, 1) ? BufGetU8(data) : 0;
}

u16 CursorGetU16(BufCursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    return CursorSkip(cursor, 2) ? BufGetU16(data) : 0;
}

u32 CursorGetU32(BufCursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    return CursorSkip(cursor, 4) ? BufGetU32(data) : 0;
}

u64 CursorGetU64(BufCursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    return CursorSkip(cursor, 8) ? BufGetU64(data) : 0;
}

f32 CursorGetF32(BufCursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    return CursorSkip(cursor, 4) ? BufGetF32(data) : 0;
}

f64 CursorGetF64(BufCursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    return CursorSkip(cursor, 8) ? BufGetF64(data) : 0;
}

String CursorGetVarString(BufCursor * cursor, i32 maxSize) {
    i64 size = CursorGetVarU32(cursor);
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, size)) {
        if (size <= maxSize) {
            return (String) {.size = size, .data = data};
        }
        cursor->error = 1;
    }
    return (String) {0};
}

BlockPos CursorGetBlockPos(BufCursor * cursor) {
    i64 in = CursorGetU64(cursor);
    BlockPos res = {
        .x = in >> 38,
        .y = (in << 52) >> 52,
        .z = (in << 26) >> 38
    };
    return res;
}

i32 CursorGetBool(BufCursor * cursor) {
    u8 res = CursorGetU8(cursor);
    return !!res;
}

void CursorPutVarU32(BufCursor * cursor, u32 value) {
    for (;;) {
        if (cursor->index == cursor->size) {
            cursor->error = 1;
            break;
        }
        u8 out = value & 0x7f;
        value >>= 7;

        cursor->data[cursor->index] = out;
        if (value == 0) {
            cursor->index++;
            break;
        }
        cursor->data[cursor->index] |= 0x80;
        cursor->index++;
    }
}

void CursorPutVarU64(BufCursor * cursor, u64 value) {
    for (;;) {
        if (cursor->index == cursor->size) {
            cursor->error = 1;
            break;
        }
        u8 out = value & 0x7f;
        value >>= 7;

        cursor->data[cursor->index] = out;
        if (value == 0) {
            cursor->index++;
            break;
        }
        cursor->data[cursor->index] |= 0x80;
        cursor->index++;
    }
}

void CursorPutU8(BufCursor * cursor, u8 value) {
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, 1)) BufPutU8(data, value);
}

void CursorPutU16(BufCursor * cursor, u16 value) {
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, 2)) BufPutU16(data, value);
}

void CursorPutU32(BufCursor * cursor, u32 value) {
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, 4)) BufPutU32(data, value);
}

void CursorPutU64(BufCursor * cursor, u64 value) {
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, 8)) BufPutU64(data, value);
}

void CursorPutF32(BufCursor * cursor, f32 value) {
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, 4)) BufPutF32(data, value);
}

void CursorPutF64(BufCursor * cursor, f64 value) {
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, 8)) BufPutF64(data, value);
}

void CursorPutVarString(BufCursor * cursor, String value) {
    CursorPutVarU32(cursor, value.size);
    CursorPutData(cursor, value.data, value.size);
}

void CursorPutBlockPos(BufCursor * cursor, BlockPos value) {
    u64 in = ((u64) (value.x & 0x3ffffff) << 38)
            | ((u64) (value.z & 0x3ffffff) << 12)
            | ((u64) (value.y & 0xfff));
    CursorPutU64(cursor, in);
}

void CursorPutData(BufCursor * cursor, u8 * restrict data, i32 size) {
    u8 * dest = cursor->data + cursor->index;
    if (CursorSkip(cursor, size)) {
        for (i32 i = 0; i < size; i++) {
            dest[i] = data[i];
        }
    }
}
