#ifndef BUFFER_H
#define BUFFER_H

#include <string.h>
#include "base.h"

// @NOTE(traks) Clang doesn't define this for me
#ifndef __FLOAT_WORD_ORDER__
#define __FLOAT_WORD_ORDER__ __BYTE_ORDER__
#endif

static inline u8 BufGetU8(u8 * data) {
    return data[0];
}

static inline u16 BufGetU16(u8 * data) {
    u16 res;
    memcpy(&res, data, 2);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    res = __builtin_bswap16(res);
    #endif
    return res;
}

static inline u32 BufGetU32(u8 * data) {
    u32 res;
    memcpy(&res, data, 4);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    res = __builtin_bswap32(res);
    #endif
    return res;
}

static inline u64 BufGetU64(u8 * data) {
    u64 res;
    memcpy(&res, data, 8);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    res = __builtin_bswap64(res);
    #endif
    return res;
}

static inline f32 BufGetF32(u8 * data) {
    union {
        u32 u;
        f32 f;
    } res;
    memcpy(&res.u, data, 4);
    #if __FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__
    res.u = __builtin_bswap32(res.u);
    #endif
    return res.f;
}

static inline f64 BufGetF64(u8 * data) {
    union {
        u64 u;
        f64 f;
    } res;
    memcpy(&res.u, data, 8);
    #if __FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__
    res.u = __builtin_bswap64(res.u);
    #endif
    return res.f;
}

static inline void BufPutU8(u8 * data, u8 value) {
    data[0] = value;
}

static inline void BufPutU16(u8 * data, u16 value) {
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    value = __builtin_bswap16(value);
    #endif
    memcpy(data, &value, 2);
}

static inline void BufPutU32(u8 * data, u32 value) {
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    value = __builtin_bswap32(value);
    #endif
    memcpy(data, &value, 4);
}

static inline void BufPutU64(u8 * data, u64 value) {
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    value = __builtin_bswap64(value);
    #endif
    memcpy(data, &value, 8);
}

static inline void BufPutF32(u8 * data, f32 value) {
    union {
        u32 u;
        f32 f;
    } in;
    in.f = value;
    #if __FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__
    in.u = __builtin_bswap32(in.u);
    #endif
    memcpy(data, &in.u, 4);
}

static inline void BufPutF64(u8 * data, f64 value) {
    union {
        u64 u;
        f64 f;
    } in;
    in.f = value;
    #if __FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__
    in.u = __builtin_bswap64(in.u);
    #endif
    memcpy(data, &in.u, 8);
}

typedef struct {
    // @TODO(traks) could the compiler think that the buffer points to some
    // buffer that contains this struct itself, meaning it has to reload fields
    // after we write something to it?
    u8 * data;
    i32 size;
    i32 index;
    i32 error;
    i32 mark;
} BufCursor;

static inline i32 CursorRemaining(BufCursor * cursor) {
    i32 res = cursor->size - cursor->index;
    return res;
}

static inline i32 CursorSkip(BufCursor * cursor, i64 skip) {
    assert(skip >= 0);
    if (cursor->index <= cursor->size - skip) {
        cursor->index += skip;
        return 1;
    } else {
        cursor->error = 1;
        // @NOTE(traks) Especially for reading buffers, don't let errors break
        // progress assumptions, so we don't end up in infinite loops, etc.
        // For writing it doesn't matter whether we skip in case of errors,
        // since the buffer will be discarded anyway due to the error.
        cursor->index = cursor->size;
        return 0;
    }
}

static inline i32 VarU32Size(u32 val) {
    i32 leadingZeroBits = __builtin_clz(val | 1);
    i32 highestOneBit = (31 - leadingZeroBits);
    return highestOneBit / 7 + 1;
}

u32 CursorGetVarU32(BufCursor * cursor);
u64 CursorGetVarU64(BufCursor * cursor);
u8 CursorGetU8(BufCursor * cursor);
u16 CursorGetU16(BufCursor * cursor);
u32 CursorGetU32(BufCursor * cursor);
u64 CursorGetU64(BufCursor * cursor);
f32 CursorGetF32(BufCursor * cursor);
f64 CursorGetF64(BufCursor * cursor);
String CursorGetVarString(BufCursor * cursor, i32 maxSize);
BlockPos CursorGetBlockPos(BufCursor * cursor);
i32 CursorGetBool(BufCursor * cursor);

void CursorPutVarU32(BufCursor * cursor, u32 value);
void CursorPutVarU64(BufCursor * cursor, u64 value);
void CursorPutU8(BufCursor * cursor, u8 value);
void CursorPutU16(BufCursor * cursor, u16 value);
void CursorPutU32(BufCursor * cursor, u32 value);
void CursorPutU64(BufCursor * cursor, u64 value);
void CursorPutF32(BufCursor * cursor, f32 value);
void CursorPutF64(BufCursor * cursor, f64 value);
void CursorPutVarString(BufCursor * cursor, String value);
void CursorPutBlockPos(BufCursor * cursor, BlockPos value);
void CursorPutData(BufCursor * cursor, u8 * restrict data, i32 size);

#endif
