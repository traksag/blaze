#include <zlib.h>
#include <stdlib.h>
#include "packet.h"

#define INTERNAL_HEADER_SIZE (1)
#define INTERNAL_PACKET_PREFIX_SIZE (INTERNAL_HEADER_SIZE + 5)

void BeginPacket(Cursor * cursor, i32 packetId) {
    // TODO(traks): Not sure how I feel about using a mark for this. Perhaps
    // cursors should't have a mark and this should be managed externally by us.
    // Though it is quite convenient
    CursorSetMark(cursor);
    // NOTE(traks): reserve space for internal header and skip some bytes for
    // packet size varint at the start
    CursorSkip(cursor, INTERNAL_PACKET_PREFIX_SIZE);
    WriteVarU32(cursor, packetId);
    // LogInfo("Packet: %d", (int) packetId);
}

// TODO(traks): ideally explicitly finishing a packet shouldn't be necessary. We
// can finish the previous packet when we start the next one
void FinishPacket(Cursor * cursor, i32 markCompress) {
    // NOTE(traks): We use the written data to determine the packet size instead
    // of calculating the packet size up front. The major benefit is that
    // calculating the packet size up front is very error prone and requires a
    // lot of maintainance (in case of packet format changes).
    //
    // The downside is that we have to copy all packet data an additional time,
    // because Mojang decided to encode packet sizes with a variable-size
    // encoding. It's not a huge issue if compression is enabled, because we
    // will need to compress the packets to another buffer anyway.

    if (cursor->error != 0) {
        // NOTE(traks): packet ID could be invalid, but print it anyway
        cursor->index = cursor->mark;
        CursorSkip(cursor, INTERNAL_PACKET_PREFIX_SIZE);
        i32 maybeId = ReadVarU32(cursor);
        LogInfo("Finished invalid packet: %d", maybeId);
        return;
    }

    i32 packetEnd = cursor->index;
    // NOTE(traks): mark is set to the start of the internal header
    cursor->index = cursor->mark;
    i32 packetSize = packetEnd - cursor->index - INTERNAL_PACKET_PREFIX_SIZE;
    assert(packetSize >= 0);

    i32 sizeOffset = INTERNAL_PACKET_PREFIX_SIZE - INTERNAL_HEADER_SIZE - VarU32Size(packetSize);
    i32 internalHeader = sizeOffset;
    if (markCompress) {
        internalHeader |= 0x80;
    }
    WriteU8(cursor, internalHeader);
    CursorSkip(cursor, sizeOffset);
    WriteVarU32(cursor, packetSize);
    assert(packetEnd - cursor->index == packetSize);
    cursor->index = packetEnd;
    // LogInfo("Packet size: %d", (int) packetSize);
}

void FinalisePackets(Cursor * finalCursor, Cursor * sendCursor) {
    if (sendCursor->error) {
        finalCursor->error = 1;
        return;
    }

    BeginTimings(FinalisePackets);

    Cursor * boundedSource = &(Cursor) {0};
    *boundedSource = *sendCursor;
    boundedSource->size = sendCursor->index;
    boundedSource->index = 0;

    // TODO(traks): appropriate value
    i32 maxCompressedSize = 1 << 19;
    // TODO(traks): allocate from an arena somewhere?
    u8 * compressed = malloc(maxCompressedSize);

    while (CursorRemaining(boundedSource) > 0) {
        assert(CursorRemaining(boundedSource) >= INTERNAL_PACKET_PREFIX_SIZE);
        i32 internalHeader = ReadU8(boundedSource);
        i32 sizeOffset = internalHeader & 0x7;
        i32 shouldCompress = internalHeader & 0x80;

        CursorSkip(boundedSource, sizeOffset);

        i32 packetStart = boundedSource->index;
        i32 packetSize = ReadVarU32(boundedSource);
        i32 packetEnd = boundedSource->index + packetSize;
        assert(packetEnd <= boundedSource->size);

        // TODO(traks): these 2 lines are for debugging purposes
        i32 packetId = ReadVarU32(boundedSource);
        boundedSource->index = packetEnd - packetSize;

        if (shouldCompress) {
            // TODO(traks): handle errors properly

            z_stream zstream;
            zstream.zalloc = Z_NULL;
            zstream.zfree = Z_NULL;
            zstream.opaque = Z_NULL;

            if (deflateInit(&zstream, Z_DEFAULT_COMPRESSION) != Z_OK) {
                finalCursor->error = 1;
                break;
            }

            zstream.next_in = boundedSource->data + boundedSource->index;
            zstream.avail_in = packetEnd - boundedSource->index;

            zstream.next_out = compressed;
            zstream.avail_out = maxCompressedSize;

            BeginTimings(Deflate);
            // i64 deflateTimeStart = NanoTime();
            if (deflate(&zstream, Z_FINISH) != Z_STREAM_END) {
                EndTimings(Deflate);
                finalCursor->error = 1;
                break;
            }
            // i64 deflateTimeEnd = NanoTime();
            // if (packetId == CBP_LEVEL_CHUNK_WITH_LIGHT) {
            //     LogInfo("Deflate took %jdµs for size %jd of packet %d", (intmax_t) (deflateTimeEnd - deflateTimeStart) / 1000, (intmax_t) packetSize, packetId);
            // }
            EndTimings(Deflate);

            if (deflateEnd(&zstream) != Z_OK) {
                finalCursor->error = 1;
                break;
            }

            if (zstream.avail_in != 0) {
                finalCursor->error = 1;
                break;
            }

            WriteVarU32(finalCursor, VarU32Size(packetSize) + zstream.total_out);
            WriteVarU32(finalCursor, packetSize);
            WriteData(finalCursor, compressed, zstream.total_out);
        } else {
            // TODO(traks): should check somewhere that no error occurs
            WriteData(finalCursor, boundedSource->data + packetStart, packetEnd - packetStart);
        }

        boundedSource->index = packetEnd;
    }

    free(compressed);
    EndTimings(FinalisePackets);
}
