#ifndef BASE_H
#define BASE_H

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

// @NOTE(traks) apparently tracy includes headers that define MIN/MAX, so define
// them here in advance to prevent warnings
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#ifdef PROFILE

#include <tracy/TracyC.h>

extern TracyCZoneCtx tracyContexts[64];
extern int tracyContextCount;

#define BeginTimedZone(name) \
    do { \
        int tracyContextIndex = tracyContextCount; \
        tracyContextCount++; \
        TracyCZoneNS(ctx, name, 10, 1); \
        tracyContexts[tracyContextIndex] = ctx; \
    } while (0)

#define EndTimedZone() \
    do { \
        tracyContextCount--; \
        TracyCZoneCtx ctx = tracyContexts[tracyContextCount]; \
        TracyCZoneEnd(ctx); \
    } while (0)

#else

#define BeginTimedZone(name)
#define EndTimedZone()

#endif // PROFILE

#define ARRAY_SIZE(x) (sizeof (x) / sizeof *(x))

#define ABS(a) ((a) < 0 ? -(a) : (a))

#define CLAMP(x, l, u) (MAX(MIN((x), (u)), (l)))

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
    i32 x;
    i32 y;
    i32 z;
} BlockPos;

typedef struct {
    u8 * data;
    i32 size;
} String;

#define STR(x) ((String) {.size = strlen(x), .data = (u8 *) (x)})

i64 program_nano_time();

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

void * MallocInArena(MemoryArena * arena, i32 size);
void * CallocInArena(MemoryArena * arena, i32 size);

typedef struct MemoryPoolBlock MemoryPoolBlock;
struct MemoryPoolBlock {
    MemoryPoolBlock * next;
    MemoryPoolBlock * prev;
    u64 useCount;
};

typedef struct {
    // @NOTE(traks) should be multiple of cache line, so we can safely use
    // different items from different threads
    i32 itemSize;
    i32 itemsPerBlock;
    MemoryPoolBlock nonFullList;
    MemoryPoolBlock fullList;
    MemoryPoolBlock emptyList;
} MemoryPool;

typedef struct {
    MemoryPoolBlock * block;
    void * data;
} MemoryPoolAllocation;

// @NOTE(traks) itemsPerBlock must be multiple of 64
static inline void InitPool(MemoryPool * pool, i32 itemSize, i32 itemsPerBlock) {
    *pool = (MemoryPool) {0};
    // @TODO(traks) align to cache line size
    i32 align = 128;
    itemSize = (itemSize + align - 1) / align * align;
    pool->itemSize = itemSize;
    pool->itemsPerBlock = itemsPerBlock;
    pool->nonFullList.next = &pool->nonFullList;
    pool->fullList.next = &pool->fullList;
    pool->emptyList.next = &pool->emptyList;
    pool->nonFullList.prev = &pool->nonFullList;
    pool->fullList.prev = &pool->fullList;
    pool->emptyList.prev = &pool->emptyList;
}

MemoryPoolAllocation MallocInPool(MemoryPool * pool);
MemoryPoolAllocation CallocInPool(MemoryPool * pool);
void FreeInPool(MemoryPool * pool, MemoryPoolAllocation alloc);

#endif
