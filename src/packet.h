#ifndef PACKET_H
#define PACKET_H

#include "buffer.h"

void BeginPacket(Cursor * cursor, i32 packetId);
void FinishPacket(Cursor * cursor, i32 markCompress);
void FinalisePackets(Cursor * finalCursor, Cursor * sendCursor);

#endif
