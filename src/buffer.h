#ifndef BUFFER_H
#define BUFFER_H

// NOTE(traks): Simple interface for reading from/writing to buffers.
//
// All integer/float types are encoded as big endian, because that's what
// Minecraft uses for all of its data formats. CPU floats are assumed to be
// represented in memory as IEEE 754 binary32 and binary64.

#include <string.h>
#include "base.h"

// NOTE(traks): Clang doesn't define this for me, let's hope this is correct
#ifndef __FLOAT_WORD_ORDER__
#define __FLOAT_WORD_ORDER__ __BYTE_ORDER__
#endif

// TODO(traks): It's very hard to get Clang to emit the proper instructions in
// ALL scenarios. It should really only output 1 instruction, some big endian
// load/store. Any way to get Clang to emit it reliably? Maybe we should just
// inline asm it.

static inline u8 ReadDirectU8(u8 * data) {
    return data[0];
}

static inline u16 ReadDirectU16(u8 * data) {
    u16 res;
    memcpy(&res, data, 2);
    if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) res = __builtin_bswap16(res);
    return res;
}

static inline u32 ReadDirectU32(u8 * data) {
    u32 res;
    memcpy(&res, data, 4);
    if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) res = __builtin_bswap32(res);
    return res;
}

static inline u64 ReadDirectU64(u8 * data) {
    u64 res;
    memcpy(&res, data, 8);
    if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) res = __builtin_bswap64(res);
    return res;
}

static inline f32 ReadDirectF32(u8 * data) {
    union {
        u32 u;
        f32 f;
    } res;
    memcpy(&res.u, data, 4);
    if (__FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__) res.u = __builtin_bswap32(res.u);
    return res.f;
}

static inline f64 ReadDirectF64(u8 * data) {
    union {
        u64 u;
        f64 f;
    } res;
    memcpy(&res.u, data, 8);
    if (__FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__) res.u = __builtin_bswap64(res.u);
    return res.f;
}

static inline void WriteDirectU8(u8 * data, u8 value) {
    data[0] = value;
}

static inline void WriteDirectU16(u8 * data, u16 value) {
    if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) value = __builtin_bswap16(value);
    memcpy(data, &value, 2);
}

static inline void WriteDirectU32(u8 * data, u32 value) {
    if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) value = __builtin_bswap32(value);
    memcpy(data, &value, 4);
}

static inline void WriteDirectU64(u8 * data, u64 value) {
    if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) value = __builtin_bswap64(value);
    memcpy(data, &value, 8);
}

static inline void WriteDirectF32(u8 * data, f32 value) {
    union {
        u32 u;
        f32 f;
    } in;
    in.f = value;
    if (__FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__) in.u = __builtin_bswap32(in.u);
    memcpy(data, &in.u, 4);
}

static inline void WriteDirectF64(u8 * data, f64 value) {
    union {
        u64 u;
        f64 f;
    } in;
    in.f = value;
    if (__FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__) in.u = __builtin_bswap64(in.u);
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
} Cursor;

static inline void CursorSetMark(Cursor * cursor) {
    cursor->mark = cursor->index;
}

static inline void CursorRewind(Cursor * cursor) {
    cursor->index = cursor->mark;
}

static inline i32 CursorRemaining(Cursor * cursor) {
    i32 res = cursor->size - cursor->index;
    return res;
}

static inline i32 CursorSkip(Cursor * cursor, i32 skip) {
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

u32 ReadVarU32(Cursor * cursor);
u64 ReadVarU64(Cursor * cursor);
u8 ReadU8(Cursor * cursor);
u16 ReadU16(Cursor * cursor);
u32 ReadU32(Cursor * cursor);
u64 ReadU64(Cursor * cursor);
f32 ReadF32(Cursor * cursor);
f64 ReadF64(Cursor * cursor);
String ReadVarString(Cursor * cursor, i32 maxSize);
BlockPos ReadBlockPos(Cursor * cursor);
i32 ReadBool(Cursor * cursor);

void WriteVarU32(Cursor * cursor, u32 value);
void WriteVarU64(Cursor * cursor, u64 value);
void WriteU8(Cursor * cursor, u8 value);
void WriteU16(Cursor * cursor, u16 value);
void WriteU32(Cursor * cursor, u32 value);
void WriteU64(Cursor * cursor, u64 value);
void WriteF32(Cursor * cursor, f32 value);
void WriteF64(Cursor * cursor, f64 value);
void WriteVarString(Cursor * cursor, String value);
void WriteBlockPos(Cursor * cursor, BlockPos value);
void WriteData(Cursor * cursor, u8 * restrict data, i32 size);

#endif
