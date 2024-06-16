#ifndef NBT_H
#define NBT_H

#include "buffer.h"

typedef struct {
    void * internal;
} NbtCompound;

typedef struct {
    unsigned char * listData;
    u32 index;
    u32 size;
    void * curComplexValue;
} NbtList;

enum NbtType {
    NBT_U8,
    NBT_U16,
    NBT_U32,
    NBT_U64,
    NBT_FLOAT,
    NBT_DOUBLE,
    NBT_ARRAY_U8,
    NBT_STRING,
    NBT_LIST,
    NBT_COMPOUND,
    NBT_ARRAY_U32,
    NBT_ARRAY_U64,
};

enum NbtTag {
    NBT_TAG_END,
    NBT_TAG_BYTE,
    NBT_TAG_SHORT,
    NBT_TAG_INT,
    NBT_TAG_LONG,
    NBT_TAG_FLOAT,
    NBT_TAG_DOUBLE,
    NBT_TAG_BYTE_ARRAY,
    NBT_TAG_STRING,
    NBT_TAG_LIST,
    NBT_TAG_COMPOUND,
    NBT_TAG_INT_ARRAY,
    NBT_TAG_LONG_ARRAY,
};

NbtCompound NbtRead(Cursor * buf, MemoryArena * arena);
void NbtPrint(NbtCompound * compound);

// TODO(traks): It would be nice if these functions did some sort of error
// reporting if the lookup failed or if the lookup succeeded but the value had
// the wrong type.
u8 NbtGetU8(NbtCompound * compound, String key);
u16 NbtGetU16(NbtCompound * compound, String key);
u32 NbtGetU32(NbtCompound * compound, String key);
u64 NbtGetU64(NbtCompound * compound, String key);
u64 NbtGetUAny(NbtCompound * compound, String key);
float NbtGetFloat(NbtCompound * compound, String key);
double NbtGetDouble(NbtCompound * compound, String key);
String NbtGetString(NbtCompound * compound, String key);
NbtList NbtGetArrayU8(NbtCompound * compound, String key);
NbtList NbtGetArrayU32(NbtCompound * compound, String key);
NbtList NbtGetArrayU64(NbtCompound * compound, String key);
NbtList NbtGetList(NbtCompound * compound, String key, i32 elemType);
NbtCompound NbtGetCompound(NbtCompound * compound, String key);
i32 NbtIsEmpty(NbtCompound * compound);

u8 NbtNextU8(NbtList * list);
u16 NbtNextU16(NbtList * list);
u32 NbtNextU32(NbtList * list);
u64 NbtNextU64(NbtList * list);
float NbtNextFloat(NbtList * list);
double NbtNextDouble(NbtList * list);
String NbtNextString(NbtList * list);
NbtList NbtNextArrayU8(NbtList * list);
NbtList NbtNextArrayU32(NbtList * list);
NbtList NbtNextArrayU64(NbtList * list);
NbtList NbtNextList(NbtList * list, i32 elemType);
NbtCompound NbtNextCompound(NbtList * list);

#endif
