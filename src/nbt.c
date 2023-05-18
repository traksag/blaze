#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "nbt.h"

// @TODO(traks) earlier we used a tape-based NBT parser, but I switched it over
// to this one because I had it lying around and the API is much nicer. However,
// it is slower than the tape-based one at parsing. Perhaps redo the tape-based
// parser while keeping the current API.

// set to 1 to print debug info
#if 0
#define DebugPrintf(...) printf(__VA_ARGS__)
#define DebugPuts(msg) puts(msg)
#else
#define DebugPrintf(format, ...)
#define DebugPuts(msg)
#endif

enum NbtInternalType {
    NBT_ITYPE_U8,
    NBT_ITYPE_U16,
    NBT_ITYPE_U32,
    NBT_ITYPE_U64,
    NBT_ITYPE_FLOAT,
    NBT_ITYPE_DOUBLE,
    NBT_ITYPE_ARRAY_U8,
    NBT_ITYPE_STRING,

    NBT_ITYPE_LIST_EMPTY,
    NBT_ITYPE_LIST_U8,
    NBT_ITYPE_LIST_U16,
    NBT_ITYPE_LIST_U32,
    NBT_ITYPE_LIST_U64,
    NBT_ITYPE_LIST_FLOAT,
    NBT_ITYPE_LIST_DOUBLE,
    NBT_ITYPE_LIST_ARRAY_U8,
    NBT_ITYPE_LIST_STRING,
    NBT_ITYPE_LIST_LIST,
    NBT_ITYPE_LIST_COMPOUND,
    NBT_ITYPE_LIST_ARRAY_U32,
    NBT_ITYPE_LIST_ARRAY_U64,

    NBT_ITYPE_COMPOUND,
    NBT_ITYPE_ARRAY_U32,
    NBT_ITYPE_ARRAY_U64,
};

typedef struct NbtKey NbtKey;

// @TODO(traks) fit into less bytes
struct NbtKey {
    union {
        unsigned char * key;
        NbtKey * linkedKey;
    };
    u16 keySize;
    u8 isLink;
};

typedef struct NbtValue NbtValue;

struct NbtValue {
    i32 valueSize;
    i8 internalType;
    i8 elemTag;
    u8 isLink;
    union {
        u64 intValue;
        float floatValue;
        double doubleValue;
        unsigned char * arrayData;
        unsigned char * stringValue;
        unsigned char * listData;
        NbtValue * listElements;
        struct {
            NbtKey * compoundKeys;
            NbtValue * compoundValues;
        };
        NbtValue * linkedValue;
    };
};

#define NBT_LEVEL_BLOCK_SIZE (64)

typedef struct {
    u8 curKeyIndex;
    u8 curValueIndex;
    u32 curListIndex;
    NbtValue * parent;
    NbtKey * keys;
    NbtValue * values;
} NbtLevel;

// @NOTE(traks) only used to avoid some branches internally. External code is
// able to create empty compounds without access to this value.
static NbtValue emptyValue;

static NbtValue * MakeNbtCompoundEntry(NbtLevel * level, MemoryArena * arena, u16 keySize, unsigned char * key) {
    NbtKey * linkKey = NULL;
    NbtValue * linkValue = NULL;

    if (level->curKeyIndex == NBT_LEVEL_BLOCK_SIZE - 1) {
        level->keys[level->curKeyIndex].isLink = 1;
        linkKey = level->keys + level->curKeyIndex;
        level->keys = NULL;
    }
    if (level->curValueIndex == NBT_LEVEL_BLOCK_SIZE - 1) {
        level->values[level->curValueIndex].isLink = 1;
        linkValue = level->values + level->curValueIndex;
        level->values = NULL;
    }

    if (level->keys == NULL) {
        level->keys = CallocInArena(arena, NBT_LEVEL_BLOCK_SIZE * sizeof (NbtKey));
        level->curKeyIndex = 0;
    }
    if (level->values == NULL) {
        level->values = CallocInArena(arena, NBT_LEVEL_BLOCK_SIZE * sizeof (NbtValue));
        level->curValueIndex = 0;
    }

    if (level->parent->valueSize == 0) {
        level->parent->compoundKeys = level->keys + level->curKeyIndex;
        level->parent->compoundValues = level->values + level->curValueIndex;
    }

    // valueSize = number of entries in the compound
    level->parent->valueSize++;

    level->keys[level->curKeyIndex].key = key;
    level->keys[level->curKeyIndex].keySize = keySize;
    NbtValue * res = level->values + level->curValueIndex;

    if (linkKey != NULL) {
        linkKey->linkedKey = level->keys + level->curKeyIndex;
    }
    if (linkValue != NULL) {
        linkValue->linkedValue = level->values + level->curValueIndex;
    }

    level->curKeyIndex++;
    level->curValueIndex++;
    return res;
}

static NbtValue * MakeNbtListEntry(NbtLevel * level, MemoryArena * arena) {
    NbtValue * linkValue = NULL;

    if (level->curValueIndex == NBT_LEVEL_BLOCK_SIZE - 1) {
        level->values[level->curValueIndex].isLink = 1;
        linkValue = level->values + level->curValueIndex;
        level->values = NULL;
    }

    if (level->values == NULL) {
        level->values = CallocInArena(arena, NBT_LEVEL_BLOCK_SIZE * sizeof (NbtValue));
        level->curValueIndex = 0;
    }

    if (level->curListIndex == 0) {
        level->parent->listElements = level->values + level->curValueIndex;
    }

    level->curListIndex++;

    NbtValue * res = level->values + level->curValueIndex;

    if (linkValue != NULL) {
        linkValue->linkedValue = level->values + level->curValueIndex;
    }

    level->curValueIndex++;
    return res;
}

// TODO(traks): improve error reporting for debugging purposes, when we need to
// write nbt data ourselves
NbtCompound NbtRead(Cursor * buf, MemoryArena * arena) {
    BeginTimings(NbtRead);
    DebugPuts("-----");

    // process first tag
    uint8_t tag = ReadU8(buf);

    int error = 0;
    NbtValue * root = NULL;

    if (tag == NBT_TAG_END) {
        // @TODO(traks) empty compound
        goto bail;
    } else if (tag != NBT_TAG_COMPOUND) {
        DebugPuts("Root tag is not a compound");
        error = 1;
        goto bail;
    }

    // skip key
    unsigned char * key = buf->data + buf->index;
    uint16_t keySize = ReadU16(buf);
    if (buf->size - buf->index < keySize) {
        DebugPuts("Key too large");
        buf->error = 1;
        goto bail;
    }
    buf->index += keySize;

    // @TODO(traks) more appropriate max level
    int maxLevels = 64;
    // @NOTE(traks) Reserve one more level to make the code below simpler: we can
    // assume in each level that there is a next level.
    NbtLevel * levels = MallocInArena(arena, (sizeof *levels) * (maxLevels + 1));
    for (int i = 0; i < maxLevels; i++) {
        levels[i] = (NbtLevel) {0};
    }

    int levelIndex = 0;
    root = MallocInArena(arena, sizeof *root);
    *root = (NbtValue) {
        .internalType = NBT_ITYPE_COMPOUND
    };
    levels[0].parent = root;

    for (;;) {
        if (levelIndex < 0) {
            break;
        }
        if (levelIndex == maxLevels) {
            DebugPuts("Too many levels");
            error = 1;
            goto bail;
        }

        NbtLevel * curLevel = levels + levelIndex;
        NbtValue * entry;

        if (curLevel->parent->internalType == NBT_ITYPE_COMPOUND) {
            tag = ReadU8(buf);

            if (tag == NBT_TAG_END) {
                levelIndex--;
                continue;
            }

            // get key
            keySize = ReadU16(buf);

            if (keySize > buf->size - buf->index) {
                DebugPuts("Key size invalid");
                error = 1;
                goto bail;
            }

            key = buf->data + buf->index;
            buf->index += keySize;

            entry = MakeNbtCompoundEntry(curLevel, arena, keySize, key);

            for (int i = 0; i < levelIndex; i++) {
                DebugPrintf("  ");
            }

            DebugPrintf("%.*s: ", (int) keySize, key);
        } else {
            // list level

            tag = curLevel->parent->elemTag;
            int curListIndex = curLevel->curListIndex;

            if (curListIndex == curLevel->parent->valueSize) {
                // end of list
                levelIndex--;
                continue;
            }

            entry = MakeNbtListEntry(curLevel, arena);

            for (int i = 0; i < levelIndex; i++) {
                DebugPrintf("  ");
            }
        }

        switch (tag) {
        case NBT_TAG_BYTE: {
            entry->internalType = NBT_ITYPE_U8;
            entry->intValue = ReadU8(buf);
            DebugPrintf("%jd\n", (intmax_t) (i8) entry->intValue);
            break;
        }
        case NBT_TAG_SHORT: {
            entry->internalType = NBT_ITYPE_U16;
            entry->intValue = ReadU16(buf);
            DebugPrintf("%jd\n", (intmax_t) (i16) entry->intValue);
            break;
        }
        case NBT_TAG_INT: {
            entry->internalType = NBT_ITYPE_U32;
            entry->intValue = ReadU32(buf);
            DebugPrintf("%jd\n", (intmax_t) (i32) entry->intValue);
            break;
        }
        case NBT_TAG_LONG: {
            entry->internalType = NBT_ITYPE_U64;
            entry->intValue = ReadU64(buf);
            DebugPrintf("%jd\n", (intmax_t) (i64) entry->intValue);
            break;
        }
        case NBT_TAG_FLOAT: {
            entry->internalType = NBT_ITYPE_FLOAT;
            entry->floatValue = ReadF32(buf);
            DebugPrintf("%f\n", entry->floatValue);
            break;
        }
        case NBT_TAG_DOUBLE: {
            entry->internalType = NBT_ITYPE_DOUBLE;
            entry->doubleValue = ReadF64(buf);
            DebugPrintf("%f\n", entry->doubleValue);
            break;
        }
        case NBT_TAG_BYTE_ARRAY: {
            entry->internalType = NBT_ITYPE_ARRAY_U8;
            entry->valueSize = ReadU32(buf);
            entry->arrayData = buf->data + buf->index;

            DebugPrintf("byte array %ju\n", (uintmax_t) entry->valueSize);

            if (entry->valueSize * 1 > buf->size - buf->index) {
                DebugPuts("Array size out of bounds");
                error = 1;
                goto bail;
            }

            buf->index += entry->valueSize * 1;
            break;
        }
        case NBT_TAG_INT_ARRAY: {
            entry->internalType = NBT_ITYPE_ARRAY_U32;
            entry->valueSize = ReadU32(buf);
            entry->arrayData = buf->data + buf->index;

            DebugPrintf("int array %ju\n", (uintmax_t) entry->valueSize);

            if (entry->valueSize * 4 > buf->size - buf->index) {
                DebugPuts("Array size out of bounds");
                error = 1;
                goto bail;
            }

            buf->index += entry->valueSize * 4;
            break;
        }
        case NBT_TAG_LONG_ARRAY: {
            entry->internalType = NBT_ITYPE_ARRAY_U64;
            entry->valueSize = ReadU32(buf);
            entry->arrayData = buf->data + buf->index;

            DebugPrintf("long array %ju\n", (uintmax_t) entry->valueSize);

            if (entry->valueSize * 8 > buf->size - buf->index) {
                DebugPuts("Array size out of bounds");
                error = 1;
                goto bail;
            }

            buf->index += entry->valueSize * 8;
            break;
        }
        case NBT_TAG_STRING: {
            entry->internalType = NBT_ITYPE_STRING;
            entry->valueSize = ReadU16(buf);
            entry->stringValue = buf->data + buf->index;

            if (entry->valueSize > buf->size - buf->index) {
                DebugPuts("String size out of bounds");
                error = 1;
                goto bail;
            }

            DebugPrintf("\"%.*s\"\n", (int) entry->valueSize, entry->stringValue);

            buf->index += entry->valueSize;
            break;
        }
        case NBT_TAG_LIST: {
            entry->elemTag = ReadU8(buf);
            entry->valueSize = ReadU32(buf);

            switch (entry->elemTag) {
            case NBT_TAG_END: {
                // empty list
                DebugPuts("empty list");
                entry->internalType = NBT_ITYPE_LIST_EMPTY;

                if (entry->valueSize != 0) {
                    DebugPuts("Empty list has non-zero size");
                    error = 1;
                    goto bail;
                }
                break;
            }
            case NBT_TAG_BYTE: {
                DebugPrintf("byte list %ju\n", (uintmax_t) entry->valueSize);
                entry->internalType = NBT_ITYPE_LIST_U8;
                entry->listData = buf->data + buf->index;

                if (entry->valueSize * 1 > buf->size - buf->index) {
                    DebugPuts("Array size out of bounds");
                    error = 1;
                    goto bail;
                }

                buf->index += entry->valueSize * 1;
                break;
            }
            case NBT_TAG_SHORT: {
                DebugPrintf("short list %ju\n", (uintmax_t) entry->valueSize);
                entry->internalType = NBT_ITYPE_LIST_U16;
                entry->listData = buf->data + buf->index;

                if (entry->valueSize * 2 > buf->size - buf->index) {
                    DebugPuts("Array size out of bounds");
                    error = 1;
                    goto bail;
                }

                buf->index += entry->valueSize * 2;
                break;
            }
            case NBT_TAG_INT: {
                DebugPrintf("int list %ju\n", (uintmax_t) entry->valueSize);
                entry->internalType = NBT_ITYPE_LIST_U32;
                entry->listData = buf->data + buf->index;

                if (entry->valueSize * 4 > buf->size - buf->index) {
                    DebugPuts("Array size out of bounds");
                    error = 1;
                    goto bail;
                }

                buf->index += entry->valueSize * 4;
                break;
            }
            case NBT_TAG_LONG: {
                DebugPrintf("long list %ju\n", (uintmax_t) entry->valueSize);
                entry->internalType = NBT_ITYPE_LIST_U64;
                entry->listData = buf->data + buf->index;

                if (entry->valueSize * 8 > buf->size - buf->index) {
                    DebugPuts("Array size out of bounds");
                    error = 1;
                    goto bail;
                }

                buf->index += entry->valueSize * 8;
                break;
            }
            case NBT_TAG_FLOAT: {
                DebugPrintf("float list %ju\n", (uintmax_t) entry->valueSize);
                entry->internalType = NBT_ITYPE_LIST_FLOAT;
                entry->listData = buf->data + buf->index;

                if (entry->valueSize * 4 > buf->size - buf->index) {
                    DebugPuts("Array size out of bounds");
                    error = 1;
                    goto bail;
                }

                buf->index += entry->valueSize * 4;
                break;
            }
            case NBT_TAG_DOUBLE: {
                DebugPrintf("double list %ju\n", (uintmax_t) entry->valueSize);
                entry->internalType = NBT_ITYPE_LIST_DOUBLE;
                entry->listData = buf->data + buf->index;

                if (entry->valueSize * 8 > buf->size - buf->index) {
                    DebugPuts("Array size out of bounds");
                    error = 1;
                    goto bail;
                }

                buf->index += entry->valueSize * 8;
                break;
            }
            case NBT_TAG_BYTE_ARRAY: {
                DebugPrintf("byte array list %ju\n", (uintmax_t) entry->valueSize);
                entry->internalType = NBT_ITYPE_LIST_ARRAY_U8;

                levelIndex++;
                levels[levelIndex].parent = entry;
                levels[levelIndex].curListIndex = 0;
                break;
            }
            case NBT_TAG_INT_ARRAY: {
                DebugPrintf("int array list %ju\n", (uintmax_t) entry->valueSize);
                entry->internalType = NBT_ITYPE_LIST_ARRAY_U32;

                levelIndex++;
                levels[levelIndex].parent = entry;
                levels[levelIndex].curListIndex = 0;
                break;
            }
            case NBT_TAG_LONG_ARRAY: {
                DebugPrintf("long array list %ju\n", (uintmax_t) entry->valueSize);
                entry->internalType = NBT_ITYPE_LIST_ARRAY_U64;

                levelIndex++;
                levels[levelIndex].parent = entry;
                levels[levelIndex].curListIndex = 0;
                break;
            }
            case NBT_TAG_STRING: {
                DebugPrintf("string list %ju\n", (uintmax_t) entry->valueSize);
                entry->internalType = NBT_ITYPE_LIST_STRING;

                levelIndex++;
                levels[levelIndex].parent = entry;
                levels[levelIndex].curListIndex = 0;
                break;
            }
            case NBT_TAG_LIST: {
                DebugPrintf("list list %ju\n", (uintmax_t) entry->valueSize);
                entry->internalType = NBT_ITYPE_LIST_LIST;

                levelIndex++;
                levels[levelIndex].parent = entry;
                levels[levelIndex].curListIndex = 0;
                break;
            }
            case NBT_TAG_COMPOUND: {
                DebugPrintf("compound list %ju\n", (uintmax_t) entry->valueSize);
                entry->internalType = NBT_ITYPE_LIST_COMPOUND;

                levelIndex++;
                levels[levelIndex].parent = entry;
                levels[levelIndex].curListIndex = 0;
                break;
            }
            default: {
                DebugPrintf("Unknown element tag: %d\n", entry->elemTag);
                error = 1;
                goto bail;
            }
            }
            break;
        }
        case NBT_TAG_COMPOUND: {
            DebugPuts("compound");
            entry->internalType = NBT_ITYPE_COMPOUND;

            // move to next level
            levelIndex++;
            levels[levelIndex].parent = entry;
            break;
        }
        default: {
            DebugPrintf("Unknown tag: %d\n", tag);
            error = 1;
            goto bail;
        }
        }
    }

bail:
    EndTimings(NbtRead);
    if (error) {
        root = NULL;
    }
    return (NbtCompound) {.internal = root};
}

typedef struct {
    i32 curIndex;

    // @NOTE(traks) compound stuff
    NbtKey * keys;
    NbtValue * values;
    i32 keyOffset;
    i32 valueOffset;

    // @NOTE(traks) list stuff
    NbtValue * curElem;

    // @NOTE(traks) compound/list that this level represents
    NbtValue * value;
} NbtPrintLevel;

static void PrintIndent(i32 indent) {
    for (i32 i = 0; i <= indent; i++) {
        printf("  ");
    }
}

void NbtPrint(NbtCompound * compound) {
    NbtValue * first = compound->internal;
    if (first == NULL) {
        puts("{}");
        return;
    }
    assert(first->internalType == NBT_ITYPE_COMPOUND);

    NbtPrintLevel levels[512] = {0};
    levels[0] = (NbtPrintLevel) {
        .value = first,
    };
    i32 levelIndex = 0;

    printf("{");

    for (;;) {
        if (levelIndex < 0) {
            break;
        }
        if (levelIndex >= ARRAY_SIZE(levels) - 1) {
            // @NOTE(traks) might overflow this round, bail!
            return;
        }

        NbtPrintLevel * curLevel = levels + levelIndex;

        NbtValue * value;
        i32 endWithNewLine = 0;
        i32 endWithComma = 0;

        if (curLevel->value->internalType == NBT_ITYPE_COMPOUND) {
            printf("\n");

            if (curLevel->curIndex >= curLevel->value->valueSize) {
                levelIndex--;
                PrintIndent(levelIndex);
                printf("}");
                continue;
            }

            if (curLevel->curIndex == 0) {
                curLevel->keys = curLevel->value->compoundKeys;
                curLevel->values = curLevel->value->compoundValues;
                curLevel->keyOffset = 0;
                curLevel->valueOffset = 0;
            }

            NbtKey * entryKey = curLevel->keys + (curLevel->curIndex - curLevel->keyOffset);
            value = curLevel->values + (curLevel->curIndex - curLevel->valueOffset);

            if (entryKey->isLink) {
                curLevel->keys = entryKey->linkedKey;
                curLevel->keyOffset = curLevel->curIndex;
                entryKey = curLevel->keys + (curLevel->curIndex - curLevel->keyOffset);
            }
            if (value->isLink) {
                curLevel->values = value->linkedValue;
                curLevel->valueOffset = curLevel->curIndex;
                value = curLevel->values + (curLevel->curIndex - curLevel->valueOffset);
            }

            curLevel->curIndex++;

            PrintIndent(levelIndex);
            printf("%.*s: ", entryKey->keySize, entryKey->key);
            endWithNewLine = 1;
        } else {
            // @NOTE(traks) list of lists or list of compounds

            if (curLevel->curIndex >= curLevel->value->valueSize) {
                levelIndex--;
                printf("\n");
                PrintIndent(levelIndex);
                printf("]");
                continue;
            }

            if (curLevel->curIndex == 0) {
                printf("\n");
            } else {
                printf(",\n");
            }

            if (curLevel->curIndex == 0) {
                curLevel->curElem = curLevel->value->listElements;
            }

            NbtValue * elem = curLevel->curElem;
            if (elem->isLink) {
                elem = elem->linkedValue;
                curLevel->curElem = elem;
            }

            value = elem;

            curLevel->curElem++;
            curLevel->curIndex++;

            PrintIndent(levelIndex);
            endWithNewLine = 1;
        }

        switch (value->internalType) {
        case NBT_ITYPE_U8: {
            // @NOTE(traks) print signed values for section indices
            printf("(i8) %zd", (intmax_t) (i8) value->intValue);
            break;
        }
        case NBT_ITYPE_U16: {
            printf("(i16) %zd", (intmax_t) (i16) value->intValue);
            break;
        }
        case NBT_ITYPE_U32: {
            printf("(i32) %zd", (intmax_t) (i32) value->intValue);
            break;
        }
        case NBT_ITYPE_U64: {
            printf("(i64) %zd", (intmax_t) value->intValue);
            break;
        }
        case NBT_ITYPE_FLOAT: {
            printf("(f32) %f", value->floatValue);
            break;
        }
        case NBT_ITYPE_DOUBLE: {
            printf("(f64) %f", value->doubleValue);
            break;
        }
        case NBT_ITYPE_STRING: {
            printf("\"%.*s\"", value->valueSize, value->stringValue);
            break;
        }
        case NBT_ITYPE_ARRAY_U8: {
            printf("i8[]");
            break;
        }
        case NBT_ITYPE_ARRAY_U32: {
            printf("i32[]");
            break;
        }
        case NBT_ITYPE_ARRAY_U64: {
            printf("i64[]");
            break;
        }
        case NBT_ITYPE_LIST_EMPTY: {
            printf("[]");
            break;
        }
        case NBT_ITYPE_LIST_U8: {
            printf("[i8]");
            break;
        }
        case NBT_ITYPE_LIST_U16: {
            printf("[i16]");
            break;
        }
        case NBT_ITYPE_LIST_U32: {
            printf("[i32]");
            break;
        }
        case NBT_ITYPE_LIST_U64: {
            printf("[i64]");
            break;
        }
        case NBT_ITYPE_LIST_FLOAT: {
            printf("[f32]");
            break;
        }
        case NBT_ITYPE_LIST_DOUBLE: {
            printf("[f64]");
            break;
        }
        case NBT_ITYPE_LIST_ARRAY_U8: {
            printf("[u8[]]");
            break;
        }
        case NBT_ITYPE_LIST_ARRAY_U32: {
            printf("[u32[]]");
            break;
        }
        case NBT_ITYPE_LIST_ARRAY_U64: {
            printf("[u64[]]");
            break;
        }
        case NBT_ITYPE_LIST_STRING: {
            NbtValue * elem = value->listElements;
            printf("[");
            for (i32 i = 0; i < value->valueSize; i++) {
                if (i) {
                    printf(", ");
                }
                if (elem->isLink) {
                    elem = elem->linkedValue;
                }
                printf("\"%.*s\"", elem->valueSize, elem->stringValue);
                elem++;
            }
            printf("]");
            break;
        }
        case NBT_ITYPE_LIST_LIST:
        case NBT_ITYPE_LIST_COMPOUND: {
            if (value->valueSize == 0) {
                printf("[]");
            } else {
                levelIndex++;
                levels[levelIndex] = (NbtPrintLevel) {
                    .value = value
                };
                printf("[");
            }
            break;
        }
        case NBT_ITYPE_COMPOUND: {
            if (value->valueSize == 0) {
                printf("{}");
            } else {
                levelIndex++;
                levels[levelIndex] = (NbtPrintLevel) {
                    .value = value
                };
                printf("{");
            }
            break;
        }
        }
    }
    printf("\n");
}

static NbtValue * NbtSearch(NbtCompound * compound, String key) {
    NbtValue * internal = compound->internal;
    if (internal == NULL) {
        return &emptyValue;
    }
    assert(internal->internalType == NBT_ITYPE_COMPOUND);

    NbtKey * keys = internal->compoundKeys;
    NbtValue * values = internal->compoundValues;
    int keyOffset = 0;
    int valueOffset = 0;

    for (int i = 0; i < internal->valueSize; i++) {
        NbtKey * entryKey = keys + (i - keyOffset);
        NbtValue * value = values + (i - valueOffset);

        if (entryKey->isLink) {
            keys = entryKey->linkedKey;
            keyOffset = i;
            entryKey = keys + (i - keyOffset);
        }
        if (value->isLink) {
            values = value->linkedValue;
            valueOffset = i;
            value = values + (i - valueOffset);
        }

        if (entryKey->keySize == key.size && memcmp(key.data, entryKey->key, key.size) == 0) {
            return value;
        }
    }

    return &emptyValue;
}

u8 NbtGetU8(NbtCompound * compound, String key) {
    NbtValue * found = NbtSearch(compound, key);
    if (found->internalType != NBT_ITYPE_U8) return 0;
    return found->intValue;
}

u16 NbtGetU16(NbtCompound * compound, String key) {
    NbtValue * found = NbtSearch(compound, key);
    if (found->internalType != NBT_ITYPE_U16) return 0;
    return found->intValue;
}

u32 NbtGetU32(NbtCompound * compound, String key) {
    NbtValue * found = NbtSearch(compound, key);
    if (found->internalType != NBT_ITYPE_U32) return 0;
    return found->intValue;
}

u64 NbtGetU64(NbtCompound * compound, String key) {
    NbtValue * found = NbtSearch(compound, key);
    if (found->internalType != NBT_ITYPE_U64) return 0;
    return found->intValue;
}

float NbtGetFloat(NbtCompound * compound, String key) {
    NbtValue * found = NbtSearch(compound, key);
    if (found->internalType != NBT_ITYPE_FLOAT) return 0;
    return found->floatValue;
}

double NbtGetDouble(NbtCompound * compound, String key) {
    NbtValue * found = NbtSearch(compound, key);
    if (found->internalType != NBT_ITYPE_DOUBLE) return 0;
    return found->doubleValue;
}

String NbtGetString(NbtCompound * compound, String key) {
    NbtValue * found = NbtSearch(compound, key);
    if (found->internalType != NBT_ITYPE_STRING) return (String) {0};
    String res = {
        .size = found->valueSize,
        .data = found->stringValue
    };
    return res;
}

NbtList NbtGetArrayU8(NbtCompound * compound, String key) {
    NbtValue * found = NbtSearch(compound, key);
    if (found->internalType != NBT_ITYPE_ARRAY_U8) {
        NbtList res = {0};
        return res;
    };
    NbtList res = {.listData = found->listData, .size = found->valueSize};
    return res;
}

NbtList NbtGetArrayU32(NbtCompound * compound, String key) {
    NbtValue * found = NbtSearch(compound, key);
    if (found->internalType != NBT_ITYPE_ARRAY_U32) {
        NbtList res = {0};
        return res;
    };
    NbtList res = {.listData = found->listData, .size = found->valueSize};
    return res;
}

NbtList NbtGetArrayU64(NbtCompound * compound, String key) {
    NbtValue * found = NbtSearch(compound, key);
    if (found->internalType != NBT_ITYPE_ARRAY_U64) {
        NbtList res = {0};
        return res;
    };
    NbtList res = {.listData = found->listData, .size = found->valueSize};
    return res;
}

NbtList NbtGetList(NbtCompound * compound, String key, int elemType) {
    NbtValue * found = NbtSearch(compound, key);
    if (found->internalType != NBT_ITYPE_LIST_EMPTY + 1 + elemType
            && found->internalType != NBT_ITYPE_LIST_EMPTY) {
        NbtList res = {0};
        return res;
    };
    NbtList res = {
        .listData = found->listData,
        .size = found->valueSize,
        .curComplexValue = found->listElements
    };
    return res;
}

NbtCompound NbtGetCompound(NbtCompound * compound, String key) {
    NbtValue * found = NbtSearch(compound, key);
    if (found->internalType != NBT_ITYPE_COMPOUND) {
        NbtCompound res = {0};
        return res;
    };
    NbtCompound res = {.internal = found};
    return res;
}

i32 NbtIsEmpty(NbtCompound * compound) {
    NbtValue * internal = compound->internal;
    if (internal == NULL) {
        return 1;
    }
    assert(internal->internalType == NBT_ITYPE_COMPOUND);
    return (internal->valueSize == 0);
}

// these first few functions also work for arrays because the arrayData and
// listData overlap

u8 NbtNextU8(NbtList * list) {
    u8 res = ReadDirectU8(list->listData + 1 * list->index);
    list->index++;
    return res;
}

u16 NbtNextU16(NbtList * list) {
    u16 res = ReadDirectU16(list->listData + 2 * list->index);
    list->index++;
    return res;
}

u32 NbtNextU32(NbtList * list) {
    u32 res = ReadDirectU32(list->listData + 4 * list->index);
    list->index++;
    return res;
}

u64 NbtNextU64(NbtList * list) {
    u64 res = ReadDirectU64(list->listData + 8 * list->index);
    list->index++;
    return res;
}

float NbtNextFloat(NbtList * list) {
    float res = ReadDirectF32(list->listData + 4 * list->index);
    list->index++;
    return res;
}

double NbtNextDouble(NbtList * list) {
    double res = ReadDirectF64(list->listData + 8 * list->index);
    list->index++;
    return res;
}

String NbtNextString(NbtList * list) {
    NbtValue * value = list->curComplexValue;
    if (value->isLink) {
        value = value->linkedValue;
        list->curComplexValue = value;
    }
    list->curComplexValue = ((NbtValue *) list->curComplexValue) + 1;
    list->index++;
    String res = {
        .size = value->valueSize,
        .data = value->stringValue
    };
    return res;
}

NbtList NbtNextArrayU8(NbtList * list) {
    NbtValue * value = list->curComplexValue;
    if (value->isLink) {
        value = value->linkedValue;
        list->curComplexValue = value;
    }
    list->curComplexValue = ((NbtValue *) list->curComplexValue) + 1;
    list->index++;
    NbtList res = {
        .size = value->valueSize,
        .listData = value->listData
    };
    return res;
}

NbtList NbtNextArrayU32(NbtList * list) {
    NbtValue * value = list->curComplexValue;
    if (value->isLink) {
        value = value->linkedValue;
        list->curComplexValue = value;
    }
    list->curComplexValue = ((NbtValue *) list->curComplexValue) + 1;
    list->index++;
    NbtList res = {
        .size = value->valueSize,
        .listData = value->listData
    };
    return res;
}

NbtList NbtNextArrayU64(NbtList * list) {
    NbtValue * value = list->curComplexValue;
    if (value->isLink) {
        value = value->linkedValue;
        list->curComplexValue = value;
    }
    list->curComplexValue = ((NbtValue *) list->curComplexValue) + 1;
    list->index++;
    NbtList res = {
        .size = value->valueSize,
        .listData = value->listData
    };
    return res;
}

NbtList NbtNextList(NbtList * list, int elemType) {
    NbtValue * value = list->curComplexValue;
    if (value->isLink) {
        value = value->linkedValue;
        list->curComplexValue = value;
    }
    list->curComplexValue = ((NbtValue *) list->curComplexValue) + 1;
    list->index++;
    if (value->elemTag != 1 + elemType && value->elemTag != 0) {
        NbtList res = {0};
        return res;
    }
    NbtList res = {
        .size = value->valueSize,
        .listData = value->listData,
        .curComplexValue = value->listElements,
    };
    return res;
}

NbtCompound NbtNextCompound(NbtList * list) {
    NbtValue * value = list->curComplexValue;
    if (value->isLink) {
        value = value->linkedValue;
        list->curComplexValue = value;
    }
    list->curComplexValue = ((NbtValue *) list->curComplexValue) + 1;
    list->index++;
    NbtCompound res = {.internal = value};
    return res;
}
