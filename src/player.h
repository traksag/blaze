#ifndef PLAYER_H
#define PLAYER_H

#include "shared.h"

#define PLAYER_CHUNK_SENT (0x1 << 0)
#define PLAYER_CHUNK_ADDED_INTEREST (0x1 << 1)

typedef struct {
    u8 flags;
} PlayerChunkCacheEntry;

typedef struct {
    EntityId entityId;

    i64 last_tp_packet_tick;
    i64 last_send_pos_tick;
    i64 last_update_tick;

    unsigned char update_interval;

    double last_sent_x;
    double last_sent_y;
    double last_sent_z;

    // these are always 0 for some entities
    unsigned char last_sent_rot_x;
    unsigned char last_sent_rot_y;
    unsigned char last_sent_head_rot_y;
} tracked_entity;

#define PLAYER_CONTROL_DID_INIT_PACKETS ((u32) 1 << 0)
#define PLAYER_CONTROL_GOT_ALIVE_RESPONSE ((u32) 1 << 2)
#define PLAYER_CONTROL_INITIALISED_TAB_LIST ((u32) 1 << 3)
#define PLAYER_CONTROL_PACKET_COMPRESSION ((u32) 1 << 4)
#define PLAYER_CONTROL_AWAITING_TELEPORT ((u32) 1 << 6)
#define PLAYER_CONTROL_SHOULD_DISCONNECT ((u32) 1 << 7)

typedef struct {
    unsigned char username[16];
    int username_size;
    UUID uuid;

    u32 flags;

    unsigned char gamemode;

    // @NOTE(traks) the server doesn't tell clients the body rotation of
    // players. The client determines the body rotation based on the player's
    // movement and their head rotation. However, we do need to send a players
    // head rotation using the designated packet, otherwise heads won't rotate.

    int sock;

    u8 * recBuffer;
    i32 recBufferSize;
    i32 recWriteCursor;

    u8 * sendBuffer;
    i32 sendBufferSize;
    i32 sendWriteCursor;

    // NOTE(traks): Render/view distance is the client setting. It doesn't
    // include the chunk at the centre, and doesn't include an extra outer
    // border that's used for lighting, connected blocks like chests, etc. The
    // chunk cache radius FOR US does include the extra outer border.
    i32 chunkCacheRadius;
    i32 nextChunkCacheRadius;
    i32 chunkCacheCentreX;
    i32 chunkCacheCentreZ;
    i32 chunkCacheWorldId;
    // @TODO(traks) maybe this should just be a bitmap
    PlayerChunkCacheEntry chunkCache[MAX_CHUNK_CACHE_DIAM * MAX_CHUNK_CACHE_DIAM];

    u32 lastSentTeleportId;

    u8 locale[MAX_PLAYER_LOCALE_SIZE];
    i32 localeSize;
    i32 chatMode;
    i32 seesChatColours;
    u8 skinCustomisation;
    i32 mainHand;
    i32 textFiltering;
    i32 showInStatusList;

    i64 last_keep_alive_sent_tick;

    EntityId entityId;

    // @TODO(traks) this feels a bit silly, but very simple
    tracked_entity tracked_entities[MAX_ENTITIES];

    WorldBlockPos changed_blocks[8];
    u8 changed_block_count;

    // @NOTE(traks) -1 if nothing to acknowledge
    i32 lastAckedBlockChange;
} PlayerController;

void
process_use_item_on_packet(PlayerController * control,
        i32 hand, BlockPos packetClickedPos, i32 clicked_face,
        float click_offset_x, float click_offset_y, float click_offset_z,
        u8 is_inside, MemoryArena * scratch_arena);

int
use_block(PlayerController * control,
        i32 hand, WorldBlockPos clicked_pos, i32 clicked_face,
        float click_offset_x, float click_offset_y, float click_offset_z,
        u8 is_inside, block_update_context * buc);

PlayerController * CreatePlayer(void);
PlayerController * ResolvePlayer(UUID uuid);

void TickPlayers(MemoryArena * arena);
void SendPacketsToPlayers(MemoryArena * arena);

#endif
