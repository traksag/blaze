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
    // encoding. For compressed packets the same issue applies: we need to write
    // the compression length as a varint.

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

Cursor TryReadPacket(Cursor * recCursor, MemoryArena * arena, i32 shouldDecompress, i32 recBufferSize) {
    Cursor res = {0};
    Cursor * packetCursor = &(Cursor) {0};
    *packetCursor = *recCursor;
    i32 packetSize = ReadVarU32(packetCursor);

    if (packetCursor->error) {
        // NOTE(traks): packet size not fully received yet
        return res;
    }
    // NOTE(traks): subtract 5 because that's an upper bound for the encoded
    // packet size varint. If the packet is too large it'll never fit in the
    // buffer
    if (packetSize > recBufferSize - 5 || packetSize <= 0) {
        LogInfo("Bad packet size: %d", (int) packetSize);
        recCursor->error = 1;
        return res;
    }
    if (packetSize > CursorRemaining(packetCursor)) {
        // NOTE(traks): packet not fully received yet
        return res;
    }

    packetCursor->size = packetCursor->index + packetSize;
    recCursor->index = packetCursor->size;

    if (shouldDecompress) {
        // TODO(traks): ignore the uncompressed packet size for now, since we
        // require all packets to be compressed. When we add a compression
        // threshold, we should validate this!
        ReadVarU32(packetCursor);

        // TODO(traks): move to a zlib alternative that is optimised
        // for single pass inflate/deflate. If we don't end up doing
        // this, make sure the code below is actually correct (when
        // do we need to clean stuff up?)!

        z_stream zstream;
        zstream.zalloc = Z_NULL;
        zstream.zfree = Z_NULL;
        zstream.opaque = Z_NULL;

        if (inflateInit2(&zstream, 0) != Z_OK) {
            LogInfo("inflateInit failed");
            recCursor->error = 1;
            return res;
        }

        zstream.next_in = packetCursor->data + packetCursor->index;
        zstream.avail_in = packetCursor->size - packetCursor->index;

        // TODO(traks): appropriate value?
        size_t maxUncompressedSize = 2 * (1 << 20);
        u8 * uncompressed = MallocInArena(arena, maxUncompressedSize);

        zstream.next_out = uncompressed;
        zstream.avail_out = maxUncompressedSize;

        inflate(&zstream, Z_FINISH);

        if (inflateEnd(&zstream) != Z_OK) {
            LogInfo("inflateEnd failed");
            recCursor->error = 1;
            return res;
        }

        if (zstream.avail_in != 0) {
            LogInfo("Didn't inflate entire packet");
            recCursor->error = 1;
            return res;
        }

        res.data = uncompressed;
        res.size = zstream.total_out;
    } else {
        res.data = packetCursor->data + packetCursor->index;
        res.size = packetCursor->size - packetCursor->index;
    }
    return res;
}
