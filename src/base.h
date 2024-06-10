#ifndef BASE_H
#define BASE_H

#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include <assert.h>
#include <string.h>

// @NOTE(traks) apparently tracy includes headers that define MIN/MAX, so define
// them here in advance to prevent warnings
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#ifdef PROFILE

#include <tracy/TracyC.h>

#define BeginTimings(name) TracyCZoneNS(name##ctx, #name, 10, 1)

#define EndTimings(name) TracyCZoneEnd(name##ctx)

#else

#define BeginTimings(name) i32 name##ctx = 0
#define EndTimings(name) name##ctx = 1

#endif // PROFILE

#define ARRAY_SIZE(x) (sizeof (x) / sizeof *(x))

#define ABS(a) ((a) < 0 ? -(a) : (a))

#define CLAMP(x, l, u) (MAX(MIN((x), (u)), (l)))

#define MOD(a, b) (((a) % (b)) < 0 ? ((a) % (b)) + (b) : (a) % (b))

#define PI (3.141592653589f)

#define DEGREES_PER_RADIAN (360.0f / (2.0f * PI))

#define RADIANS_PER_DEGREE ((2.0f * PI) / 360.0f)

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
// @NOTE(traks) assumed to be encoded as IEEE 754 binary32 and binary64
typedef float f32;
typedef double f64;

typedef struct {
    f32 x;
    f32 y;
    f32 z;
} fvec3;

typedef struct {
    f32 minX;
    f32 minY;
    f32 minZ;
    f32 maxX;
    f32 maxY;
    f32 maxZ;
} BoundingBox;

typedef struct {
    i32 x;
    i32 y;
    i32 z;
} BlockPos;

typedef struct {
    // NOTE(traks): world ID of 0 is an invalid world
    i32 worldId;
    union {
        BlockPos xyz;
        struct {
            i32 x;
            i32 y;
            i32 z;
        };
    };
} WorldBlockPos;

typedef struct {
    i32 x;
    i32 z;
} ChunkPos;

typedef struct {
    i32 worldId;
    union {
        ChunkPos xz;
        struct {
            i32 x;
            i32 z;
        };
    };
} WorldChunkPos;

typedef struct {
    u8 * data;
    i32 size;
} String;

#define STR(x) ((String) {.size = strlen(x), .data = (u8 *) (x)})

i64 NanoTime(void);

// @NOTE(traks) make sure you're not logging user input directly, but as e.g.
// LogInfo("%s", userMessage), otherwise users can crash the server by pasting
// formatting symbols in chat.
void LogInfo(void * format, ...);
void LogErrno(void * format);

typedef struct {
    u8 * data;
    i32 size;
    i32 index;
} MemoryArena;

typedef struct {
    MemoryArena * arena;
    i32 startIndex;
} TempMemoryArena;

static void ClearArena(MemoryArena * arena) {
    arena->index = 0;
}

static void * MallocInArena(MemoryArena * arena, i32 size) {
    i32 align = alignof (max_align_t);
    // round up to multiple of align
    i32 actual_size = (size + align - 1) / align * align;

    if (arena->size - actual_size < arena->index) {
        // TODO(traks): in some cases asserting might be preferable. Maybe add
        // field to MemoryArena to enable assertings. Don't want assertions in
        // net code though or code where there is a chance the arena could be
        // overrun.
        return NULL;
    }

    void * res = arena->data + arena->index;
    arena->index += actual_size;
    return res;
}

static void * CallocInArena(MemoryArena * arena, i32 size) {
    i32 align = alignof (max_align_t);
    // round up to multiple of align
    i32 actual_size = (size + align - 1) / align * align;

    if (arena->size - actual_size < arena->index) {
        // TODO(traks): in some cases asserting might be preferable. Maybe add
        // field to MemoryArena to enable assertings. Don't want assertions in
        // net code though or code where there is a chance the arena could be
        // overrun.
        return NULL;
    }

    void * res = arena->data + arena->index;
    arena->index += actual_size;

    memset(res, 0, actual_size);
    return res;
}

static MemoryArena SubArena(MemoryArena * arena, i32 size) {
    MemoryArena res = {0};
    res.data = MallocInArena(arena, size);
    res.size = res.data == NULL ? 0 : size;
    return res;
}

static TempMemoryArena BeginTempArena(MemoryArena * arena) {
    TempMemoryArena res = {0};
    res.arena = arena;
    res.startIndex = arena->index;
    return res;
}

static void EndTempArena(TempMemoryArena * temp) {
    temp->arena->index = temp->startIndex;
}

#endif
