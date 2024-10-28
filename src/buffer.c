#include "buffer.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

u32 ReadVarU32(Cursor * cursor) {
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

u64 ReadVarU64(Cursor * cursor) {
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

u8 ReadU8(Cursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    return CursorSkip(cursor, 1) ? ReadDirectU8(data) : 0;
}

u16 ReadU16(Cursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    return CursorSkip(cursor, 2) ? ReadDirectU16(data) : 0;
}

u32 ReadU32(Cursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    return CursorSkip(cursor, 4) ? ReadDirectU32(data) : 0;
}

u64 ReadU64(Cursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    return CursorSkip(cursor, 8) ? ReadDirectU64(data) : 0;
}

f32 ReadF32(Cursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    return CursorSkip(cursor, 4) ? ReadDirectF32(data) : 0;
}

f64 ReadF64(Cursor * cursor) {
    u8 * data = cursor->data + cursor->index;
    return CursorSkip(cursor, 8) ? ReadDirectF64(data) : 0;
}

String ReadVarString(Cursor * cursor, i32 maxSize) {
    // TODO(traks): validate UTF-8?
    i64 size = ReadVarU32(cursor);
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, size)) {
        if (size <= maxSize) {
            return (String) {.size = size, .data = data};
        }
        cursor->error = 1;
    }
    return (String) {0};
}

BlockPos ReadBlockPos(Cursor * cursor) {
    i64 in = ReadU64(cursor);
    BlockPos res = {
        .x = in >> 38,
        .y = (in << 52) >> 52,
        .z = (in << 26) >> 38
    };
    return res;
}

i32 ReadBool(Cursor * cursor) {
    u8 res = ReadU8(cursor);
    return !!res;
}

UUID ReadUUID(Cursor * cursor) {
    // TODO(traks): validate this?
    u64 high = ReadU64(cursor);
    u64 low = ReadU64(cursor);
    UUID res = {.low = low, .high = high};
    return res;
}

u8 * ReadData(Cursor * cursor, i32 size) {
    u8 * res = cursor->data + cursor->index;
    return CursorSkip(cursor, size) ? res : NULL;
}

void WriteVarU32(Cursor * cursor, u32 value) {
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

void WriteVarU64(Cursor * cursor, u64 value) {
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

void WriteU8(Cursor * cursor, u8 value) {
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, 1)) WriteDirectU8(data, value);
}

void WriteU16(Cursor * cursor, u16 value) {
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, 2)) WriteDirectU16(data, value);
}

void WriteU32(Cursor * cursor, u32 value) {
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, 4)) WriteDirectU32(data, value);
}

void WriteU64(Cursor * cursor, u64 value) {
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, 8)) WriteDirectU64(data, value);
}

void WriteF32(Cursor * cursor, f32 value) {
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, 4)) WriteDirectF32(data, value);
}

void WriteF64(Cursor * cursor, f64 value) {
    u8 * data = cursor->data + cursor->index;
    if (CursorSkip(cursor, 8)) WriteDirectF64(data, value);
}

void WriteVarString(Cursor * cursor, String value) {
    WriteVarU32(cursor, value.size);
    WriteData(cursor, value.data, value.size);
}

void WriteBlockPos(Cursor * cursor, BlockPos value) {
    u64 in = ((u64) (value.x & 0x3ffffff) << 38)
            | ((u64) (value.z & 0x3ffffff) << 12)
            | ((u64) (value.y & 0xfff));
    WriteU64(cursor, in);
}

void WriteUUID(Cursor * cursor, UUID value) {
    WriteU64(cursor, value.high);
    WriteU64(cursor, value.low);
}

void WriteData(Cursor * cursor, u8 * restrict data, i32 size) {
    u8 * dest = cursor->data + cursor->index;
    if (CursorSkip(cursor, size)) {
        for (i32 i = 0; i < size; i++) {
            dest[i] = data[i];
        }
    }
}
