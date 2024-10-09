#ifndef PACKET_H
#define PACKET_H

#include "buffer.h"

void BeginPacket(Cursor * cursor, i32 packetId);
void FinishPacket(Cursor * cursor, i32 markCompress);
void FinalisePackets(Cursor * finalCursor, Cursor * sendCursor);
Cursor TryReadPacket(Cursor * recCursor, MemoryArena * arena, i32 shouldDecompress, i32 recBufferSize);

#endif
