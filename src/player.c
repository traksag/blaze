#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <zlib.h>
#include "shared.h"
#include "nbt.h"
#include "chunk.h"

// Implicit packet IDs for ease of updating. Updating packet IDs manually is a
// pain because packet types are ordered alphabetically and Mojang doesn't
// provide an explicit list of packet IDs.
enum serverbound_packet_type {
    SBP_ACCEPT_TELEPORT,
    SBP_BLOCK_ENTITY_TAG_QUERY,
    SBP_CHANGE_DIFFICULTY,
    SBP_CHAT_ACK,
    SBP_CHAT_COMMAND,
    SBP_CHAT,
    SBP_CHAT_SESSION_UPDATE,
    SBP_CLIENT_COMMAND,
    SBP_CLIENT_INFORMATION,
    SBP_COMMAND_SUGGESTION,
    SBP_CONTAINER_BUTTON_CLICK,
    SBP_CONTAINER_CLICK,
    SBP_CONTAINER_CLOSE,
    SBP_CUSTOM_PAYLOAD,
    SBP_EDIT_BOOK,
    SBP_ENTITY_TAG_QUERY,
    SBP_INTERACT,
    SBP_JIGSAW_GENERATE,
    SBP_KEEP_ALIVE,
    SBP_LOCK_DIFFICULTY,
    SBP_MOVE_PLAYER_POS,
    SBP_MOVE_PLAYER_POS_ROT,
    SBP_MOVE_PLAYER_ROT,
    SBP_MOVE_PLAYER_STATUS,
    SBP_MOVE_VEHICLE,
    SBP_PADDLE_BOAT,
    SBP_PICK_ITEM,
    SBP_PLACE_RECIPE,
    SBP_PLAYER_ABILITIES,
    SBP_PLAYER_ACTION,
    SBP_PLAYER_COMMAND,
    SBP_PLAYER_INPUT,
    SBP_PONG,
    SBP_RECIPE_BOOK_CHANGE_SETTINGS,
    SBP_RECIPE_BOOK_SEEN_RECIPE,
    SBP_RENAME_ITEM,
    SBP_RESOURCE_PACK,
    SBP_SEEN_ADVANCEMENTS,
    SBP_SELECT_TRADE,
    SBP_SET_BEACON,
    SBP_SET_CARRIED_ITEM,
    SBP_SET_COMMAND_BLOCK,
    SBP_SET_COMMAND_MINECART,
    SBP_SET_CREATIVE_MODE_SLOT,
    SBP_SET_JIGSAW_BLOCK,
    SBP_SET_STRUCTURE_BLOCK,
    SBP_SIGN_UPDATE,
    SBP_SWING,
    SBP_TELEPORT_TO_ENTITY,
    SBP_USE_ITEM_ON,
    SBP_USE_ITEM,
    SBP_VEC_DELTA_CODEC,
    SERVERBOUND_PACKET_COUNT,
};

enum clientbound_packet_type {
    CBP_BUNDLE,
    CBP_ADD_ENTITY,
    CBP_ADD_EXPERIENCE_ORB,
    CBP_ADD_PLAYER,
    CBP_ANIMATE,
    CBP_AWARD_STATS,
    CBP_BLOCK_CHANGED_ACK,
    CBP_BLOCK_DESTRUCTION,
    CBP_BLOCK_ENTITY_DATA,
    CBP_BLOCK_EVENT,
    CBP_BLOCK_UPDATE,
    CBP_BOSS_EVENT,
    CBP_CHANGE_DIFFICULTY,
    CBP_CHUNKS_BIOMES,
    CBP_CLEAR_TITLES,
    CBP_COMMANDS,
    CBP_COMMAND_SUGGESTIONS,
    CBP_CONTAINER_CLOSE,
    CBP_CONTAINER_SET_CONTENT,
    CBP_CONTAINER_SET_DATA,
    CBP_CONTAINER_SET_SLOT,
    CBP_COOLDOWN,
    CBP_CUSTOM_CHAT_COMPLETIONS,
    CBP_CUSTOM_PAYLOAD,
    CBP_DAMAGE_EVENT,
    CBP_DELETE_CHAT,
    CBP_DISCONNECT,
    CBP_DISGUISED_CHAT,
    CBP_ENTITY_EVENT,
    CBP_EXPLODE,
    CBP_FORGET_LEVEL_CHUNK,
    CBP_GAME_EVENT,
    CBP_HORSE_SCREEN_OPEN,
    CBP_HURT_ANIMATION,
    CBP_INITIALISE_BORDER,
    CBP_KEEP_ALIVE,
    CBP_LEVEL_CHUNK_WITH_LIGHT,
    CBP_LEVEL_EVENT,
    CBP_LEVEL_PARTICLES,
    CBP_LIGHT_UPDATE,
    CBP_LOGIN,
    CBP_MAP_ITEM_DATA,
    CBP_MERCHANT_OFFERS,
    CBP_MOVE_ENTITY_POS,
    CBP_MOVE_ENTITY_POS_ROT,
    CBP_MOVE_ENTITY_ROT,
    CBP_MOVE_VEHICLE,
    CBP_OPEN_BOOK,
    CBP_OPEN_SCREEN,
    CBP_OPEN_SIGN_EDITOR,
    CBP_PING,
    CBP_PLACE_GHOST_RECIPE,
    CBP_PLAYER_ABILITIES,
    CBP_PLAYER_CHAT,
    CBP_PLAYER_COMBAT_END,
    CBP_PLAYER_COMBAT_ENTER,
    CBP_PLAYER_COMBAT_KILL,
    CBP_PLAYER_INFO_REMOVE,
    CBP_PLAYER_INFO_UPDATE,
    CBP_PLAYER_LOOK_AT,
    CBP_PLAYER_POSITION,
    CBP_RECIPE,
    CBP_REMOVE_ENTITIES,
    CBP_REMOVE_MOB_EFFECT,
    CBP_RESOURCE_PACK,
    CBP_RESPAWN,
    CBP_ROTATE_HEAD,
    CBP_SECTION_BLOCKS_UPDATE,
    CBP_SELECT_ADVANCEMENTS_TAB,
    CBP_SERVER_DATA,
    CBP_SET_ACTION_BAR_TEXT,
    CBP_SET_BORDER_CENTRE,
    CBP_SET_BORDER_LERP_SIZE,
    CBP_SET_BORDER_SIZE,
    CBP_SET_BORDER_WARNING_DELAY,
    CBP_SET_BORDER_WARNING_DISTANCE,
    CBP_SET_CAMERA,
    CBP_SET_CARRIED_ITEM,
    CBP_SET_CHUNK_CACHE_CENTRE,
    CBP_SET_CHUNK_CACHE_RADIUS,
    CBP_SET_DEFAULT_SPAWN_POSITION,
    CBP_SET_DISPLAY_OBJECTIVE,
    CBP_SET_ENTITY_DATA,
    CBP_SET_ENTITY_LINK,
    CBP_SET_ENTITY_MOTION,
    CBP_SET_EQUIPMENT,
    CBP_SET_EXPERIENCE,
    CBP_SET_HEALTH,
    CBP_SET_OBJECTIVE,
    CBP_SET_PASSENGERS,
    CBP_SET_PLAYER_TEAM,
    CBP_SET_SCORE,
    CBP_SET_SIMULATION_DISTANCE,
    CBP_SET_SET_SUBTITLE_TEXT,
    CBP_SET_TIME,
    CBP_SET_TITLES,
    CBP_SET_TITLE_TEXT,
    CBP_SOUND_ENTITY,
    CBP_SOUND,
    CBP_STOP_SOUND,
    CBP_SYSTEM_CHAT,
    CBP_TAB_LIST,
    CBP_TAG_QUERY,
    CBP_TAKE_ITEM_ENTITY,
    CBP_TELEPORT_ENTITY,
    CBP_UPDATE_ADVANCEMENTS,
    CBP_UPDATE_ATTRIBUTES,
    CBP_UPDATE_ENABLE_FEATURES,
    CBP_UPDATE_MOB_EFFECT,
    CBP_UPDATE_RECIPES,
    CBP_UPDATE_TAGS,
    CLIENTBOUND_PACKET_COUNT,
};

static void
nbt_write_key(Cursor * cursor, u8 tag, String key) {
    WriteU8(cursor, tag);
    WriteU16(cursor, key.size);
    WriteData(cursor, key.data, key.size);
}

static void
nbt_write_string(Cursor * cursor, String val) {
    WriteU16(cursor, val.size);
    WriteData(cursor, val.data, val.size);
}

void
teleport_player(entity_base * entity,
        double new_x, double new_y, double new_z,
        float new_rot_x, float new_rot_y) {
    entity_player * player = &entity->player;
    player->current_teleport_id++;
    entity->flags |= ENTITY_TELEPORTING;
    // NOTE(traks): invalidate any in-progress teleports
    entity->flags &= ~PLAYER_SENT_TELEPORT;

    entity->x = new_x;
    entity->y = new_y;
    entity->z = new_z;
    entity->rot_x = new_rot_x;
    entity->rot_y = new_rot_y;
}

void
set_player_gamemode(entity_base * player, int new_gamemode) {
    if (player->player.gamemode != new_gamemode) {
        player->changed_data |= PLAYER_GAMEMODE_CHANGED;
    }

    player->player.gamemode = new_gamemode;
    unsigned old_flags = player->flags;

    switch (new_gamemode) {
    case GAMEMODE_SURVIVAL:
        player->flags &= ~PLAYER_FLYING;
        player->flags &= ~PLAYER_CAN_FLY;
        player->flags &= ~PLAYER_INSTABUILD;
        player->flags &= ~ENTITY_INVULNERABLE;
        player->flags |= PLAYER_CAN_BUILD;
        player->changed_data |= PLAYER_ABILITIES_CHANGED;

        // @TODO(traks) should be based on active effects
        player->flags &= ~ENTITY_INVISIBLE;
        player->changed_data |= 1 << ENTITY_DATA_FLAGS;

        // @TODO(traks) show effect particles and set ambience
        break;
    case GAMEMODE_CREATIVE:
        // @NOTE(traks) actually need abilities to make creative mode players
        // able to fly
        player->flags |= PLAYER_CAN_FLY;
        player->flags |= PLAYER_INSTABUILD;
        player->flags |= ENTITY_INVULNERABLE;
        player->flags |= PLAYER_CAN_BUILD;
        player->changed_data |= PLAYER_ABILITIES_CHANGED;

        // @TODO(traks) should be based on active effects
        player->flags &= ~ENTITY_INVISIBLE;
        player->changed_data |= 1 << ENTITY_DATA_FLAGS;

        // @TODO(traks) show effect particles and set ambience
        break;
    case GAMEMODE_ADVENTURE:
        player->flags &= ~PLAYER_FLYING;
        player->flags &= ~PLAYER_CAN_FLY;
        player->flags &= ~PLAYER_INSTABUILD;
        player->flags &= ~ENTITY_INVULNERABLE;
        player->flags &= ~PLAYER_CAN_BUILD;
        player->changed_data |= PLAYER_ABILITIES_CHANGED;

        // @TODO(traks) should be based on active effects
        player->flags &= ~ENTITY_INVISIBLE;
        player->changed_data |= 1 << ENTITY_DATA_FLAGS;

        // @TODO(traks) show effect particles and set ambience
        break;
    case GAMEMODE_SPECTATOR:
        player->flags |= PLAYER_FLYING;
        player->flags |= PLAYER_CAN_FLY;
        player->flags &= ~PLAYER_INSTABUILD;
        player->flags |= ENTITY_INVULNERABLE;
        player->flags &= ~PLAYER_CAN_BUILD;
        player->changed_data |= PLAYER_ABILITIES_CHANGED;

        // @NOTE(traks) if we don't make the player invisible, their head will
        // be fully opaque for both themselves (the model rendered in the
        // inventory GUI) and to other players.
        player->flags |= ENTITY_INVISIBLE;
        player->changed_data |= 1 << ENTITY_DATA_FLAGS;

        player->flags &= ~LIVING_EFFECT_AMBIENCE;
        player->changed_data |= 1 << ENTITY_DATA_EFFECT_AMBIENCE;

        player->effect_colour = 0;
        player->changed_data |= 1 << ENTITY_DATA_EFFECT_COLOUR;
        break;
    }
}

static void
process_move_player_packet(entity_base * player,
        double new_x, double new_y, double new_z,
        float new_head_rot_x, float new_head_rot_y, int on_ground) {
    if ((player->flags & ENTITY_TELEPORTING) != 0) {
        return;
    }

    // @TODO(traks) if new x, y, z out of certain bounds, don't update player
    // x, y, z to prevent NaN errors and extreme precision loss, etc.

    player->x = new_x;
    player->y = new_y;
    player->z = new_z;
    player->rot_x = new_head_rot_x;
    player->rot_y = new_head_rot_y;
    if (on_ground) {
        player->flags |= ENTITY_ON_GROUND;
    } else {
        player->flags &= ~ENTITY_ON_GROUND;
    }
}

static int
drop_item(entity_base * player, item_stack * is, unsigned char drop_size) {
    entity_base * item = try_reserve_entity(ENTITY_ITEM);
    if (item->type == ENTITY_NULL) {
        return 0;
    }

    // @TODO(traks) this has to depend on player's current pose
    double eye_y = player->y + 1.62;

    item->worldId = player->worldId;
    item->x = player->x;
    item->y = eye_y - 0.3;
    item->z = player->z;

    item->collision_width = 0.25;
    item->collision_height = 0.25;

    item->item.contents = *is;
    item->item.contents.size = drop_size;
    item->item.pickup_timeout = 40;

    float sin_rot_y = sinf(player->rot_y * RADIANS_PER_DEGREE);
    float cos_rot_y = cosf(player->rot_y * RADIANS_PER_DEGREE);
    float sin_rot_x = sinf(player->rot_x * RADIANS_PER_DEGREE);
    float cos_rot_x = cosf(player->rot_x * RADIANS_PER_DEGREE);

    // @TODO(traks) random offset
    item->vx = 0.3f * -sin_rot_y * cos_rot_x;
    item->vy = 0.3f * -sin_rot_x + 0.1;
    item->vz = 0.3f * cos_rot_y * cos_rot_x;
    return 1;
}

static void
AckBlockChange(entity_player * player, u32 sequenceNumber) {
    // TODO(traks): disconnect player if sequence number negative?
    i32 signedNumber = sequenceNumber;
    player->lastAckedBlockChange = MAX(player->lastAckedBlockChange, signedNumber);
}

static void
process_packet(entity_base * entity, Cursor * rec_cursor,
        MemoryArena * process_arena) {
    // @NOTE(traks) we need to handle packets in the order in which they arive,
    // so e.g. the client can move the player to a position, perform some
    // action, and then move the player a bit further, all in the same tick.
    //
    // This seems the most natural way to do it, and gives us e.g. as much
    // information about the player's position when they perform a certain
    // action.

    entity_player * player = &entity->player;
    i32 packet_id = ReadVarU32(rec_cursor);

    switch (packet_id) {
    case SBP_ACCEPT_TELEPORT: {
        LogInfo("Packet accept teleport");
        i32 teleport_id = ReadVarU32(rec_cursor);

        if ((entity->flags & ENTITY_TELEPORTING)
                && (entity->flags & PLAYER_SENT_TELEPORT)
                && teleport_id == player->current_teleport_id) {
            LogInfo("The teleport ID is correct!");
            entity->flags &= ~ENTITY_TELEPORTING;
            entity->flags &= ~PLAYER_SENT_TELEPORT;
        }
        break;
    }
    case SBP_BLOCK_ENTITY_TAG_QUERY: {
        LogInfo("Packet block entity tag query");
        i32 id = ReadVarU32(rec_cursor);
        u64 block_pos = ReadU64(rec_cursor);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_CHANGE_DIFFICULTY: {
        LogInfo("Packet change difficulty");
        u8 difficulty = ReadU8(rec_cursor);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_CHAT_ACK: {
        LogInfo("Chat ack");
        u32 offset = ReadVarU32(rec_cursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CHAT_COMMAND: {
        LogInfo("Packet chat command");
        String command = ReadVarString(rec_cursor, 256);
        u64 timestamp = ReadU64(rec_cursor);
        u64 salt = ReadU64(rec_cursor);

        // @NOTE(traks) signatures for command arguments or something. Not sure
        // if anything is being done with them in 1.19.
        u32 argumentCount = ReadVarU32(rec_cursor);
        argumentCount = MIN(argumentCount, 8);
        for (u32 argIndex = 0; argIndex < argumentCount; argIndex++) {
            String key = ReadVarString(rec_cursor, 16);
            u32 valueSize = ReadVarU32(rec_cursor);
            valueSize = MIN(valueSize, rec_cursor->size);
            u8 * value = rec_cursor->data + rec_cursor->index;
        }

        // NOTE(traks): last seen messages acknowledgement
        u32 offset = ReadVarU32(rec_cursor);
        for (i32 ackIndex = 0; ackIndex < 3; ackIndex++) {
            u8 ackData = ReadU8(rec_cursor);
        }
        break;
    }
    case SBP_CHAT: {
        String chat = ReadVarString(rec_cursor, 256);
        u64 timestamp = ReadU64(rec_cursor);
        u64 salt = ReadU64(rec_cursor);

        // @TODO(traks) validate signature?
        i32 hasSignature = ReadU8(rec_cursor);
        if (hasSignature) {
            u32 signatureSize = ReadVarU32(rec_cursor);
            signatureSize = MIN(signatureSize, rec_cursor->size);
            u8 * signature = rec_cursor->data + rec_cursor->index;
            rec_cursor->index += signatureSize;
        }

        // NOTE(traks): last seen messages acknowledgement
        u32 offset = ReadVarU32(rec_cursor);
        for (i32 ackIndex = 0; ackIndex < 3; ackIndex++) {
            u8 ackData = ReadU8(rec_cursor);
        }

        // TODO(traks): filter out bad characters from the message

        if (serv->global_msg_count < ARRAY_SIZE(serv->global_msgs)) {
            global_msg * msg = serv->global_msgs + serv->global_msg_count;
            serv->global_msg_count++;
            int text_size = sprintf(
                    (void *) msg->text, "<%.*s> %.*s",
                    (int) player->username_size,
                    player->username,
                    (int) chat.size, chat.data);
            msg->size = text_size;
            // LogInfo("%.*s", msg->size, msg->text);
        }
        break;
    }
    case SBP_CHAT_SESSION_UPDATE: {
        LogInfo("Packet chat session update");
        // NOTE(traks): some UUID
        u64 uuid_high = ReadU64(rec_cursor);
        u64 uuid_low = ReadU64(rec_cursor);
        u64 timestamp = ReadU64(rec_cursor);
        // NOTE(traks): public key data
        CursorSkip(rec_cursor, 512);
        // NOTE(traks): signature data
        CursorSkip(rec_cursor, 4096);

        // TODO(traks): handle packet
        break;
    }
    case SBP_CLIENT_COMMAND: {
        LogInfo("Packet client command");
        i32 action = ReadVarU32(rec_cursor);
        switch (action) {
        case 0: { // perform respawn
            // @TODO(traks) implement
            break;
        }
        case 1: { // request statistics
            // @TODO(traks) implement
            break;
        }
        default: {
            rec_cursor->error = 1;
        }
        }
        break;
    }
    case SBP_CLIENT_INFORMATION: {
        LogInfo("Packet client information");
        String language = ReadVarString(rec_cursor, 16);
        u8 view_distance = ReadU8(rec_cursor);
        i32 chat_visibility = ReadVarU32(rec_cursor);
        u8 sees_chat_colours = ReadU8(rec_cursor);
        u8 model_customisation = ReadU8(rec_cursor);
        i32 main_hand = ReadVarU32(rec_cursor);
        u8 text_filtering = ReadU8(rec_cursor);
        u8 allowInStatusList = ReadU8(rec_cursor);

        // View distance is without the extra border of chunks,
        // while chunk cache radius is with the extra border of
        // chunks. This clamps the view distance between the minimum
        // of 2 and the server maximum.
        player->nextChunkCacheRadius = MIN(MAX(view_distance, 2), MAX_CHUNK_CACHE_RADIUS - 1) + 1;
        memcpy(player->language, language.data, language.size);
        player->language_size = language.size;
        player->sees_chat_colours = sees_chat_colours;
        player->model_customisation = model_customisation;
        player->main_hand = main_hand;
        player->text_filtering = text_filtering;
        player->allowInStatusList = allowInStatusList;
        break;
    }
    case SBP_COMMAND_SUGGESTION: {
        LogInfo("Packet command suggestion");
        i32 id = ReadVarU32(rec_cursor);
        String command = ReadVarString(rec_cursor, 32500);
        break;
    }
    case SBP_CONTAINER_BUTTON_CLICK: {
        LogInfo("Packet container button click");
        u8 container_id = ReadU8(rec_cursor);
        u8 button_id = ReadU8(rec_cursor);
        break;
    }
    case SBP_CONTAINER_CLICK: {
        LogInfo("Packet container click");
        u8 container_id = ReadU8(rec_cursor);
        i32 state_id = ReadVarU32(rec_cursor);
        u16 slot = ReadU16(rec_cursor);
        u8 button = ReadU8(rec_cursor);
        i32 click_type = ReadVarU32(rec_cursor);
        i32 changed_slot_count = ReadVarU32(rec_cursor);
        if (changed_slot_count > 100 || changed_slot_count < 0) {
            // @TODO(traks) better filtering of high values
            rec_cursor->error = 1;
            break;
        }

        // @TODO(traks) do something with the changed slots?

        for (int i = 0; i < changed_slot_count; i++) {
            u16 changed_slot = ReadU16(rec_cursor);
            // @TODO(traks) read item function
            u8 has_item = ReadU8(rec_cursor);
            if (has_item) {
                // @TODO(traks) is this the new item stack or what?
                item_stack new_iss = {0};
                item_stack * new_is = &new_iss;
                new_is->type = ReadVarU32(rec_cursor);
                new_is->size = ReadU8(rec_cursor);
                // @TODO(traks) validate size and type as below

                NbtCompound itemNbt = NbtRead(rec_cursor, process_arena);
                if (rec_cursor->error) {
                    break;
                }

                // @TODO(traks) use NBT data to construct item stack
            }
        }

        if (rec_cursor->error) {
            break;
        }

        u8 has_item = ReadU8(rec_cursor);
        item_stack cursor_iss = {0};
        item_stack * cursor_is = &cursor_iss;

        if (has_item) {
            cursor_is->type = ReadVarU32(rec_cursor);
            cursor_is->size = ReadU8(rec_cursor);

            if (cursor_is->type < 0 || cursor_is->type >= ITEM_TYPE_COUNT || cursor_is->size == 0) {
                cursor_is->type = 0;
                // @TODO(traks) handle error (send slot updates?)
            }
            u8 max_size = get_max_stack_size(cursor_is->type);
            if (cursor_is->size > max_size) {
                cursor_is->size = max_size;
                // @TODO(traks) handle error (send slot updates?)
            }

            NbtCompound itemNbt = NbtRead(rec_cursor, process_arena);
            if (rec_cursor->error) {
                break;
            }

            // @TODO(traks) use NBT data to construct item stack
        }

        // @TODO(traks) actually handle the event
        break;
    }
    case SBP_CONTAINER_CLOSE: {
        LogInfo("Packet container close");
        u8 container_id = ReadU8(rec_cursor);
        break;
    }
    case SBP_CUSTOM_PAYLOAD: {
        LogInfo("Packet custom payload");
        String id = ReadVarString(rec_cursor, 32767);
        unsigned char * payload = rec_cursor->data + rec_cursor->index;
        i32 payload_size = rec_cursor->size - rec_cursor->index;

        if (payload_size > 32767) {
            // custom payload size too large
            rec_cursor->error = 1;
            break;
        }

        rec_cursor->index += payload_size;
        break;
    }
    case SBP_EDIT_BOOK: {
        LogInfo("Packet edit book");
        i32 slot = ReadVarU32(rec_cursor);
        i32 page_count = ReadVarU32(rec_cursor);
        if (page_count > 200 || page_count < 0) {
            rec_cursor->error = 1;
            break;
        }
        for (int i = 0; i < page_count; i++) {
            String page = ReadVarString(rec_cursor, 8192);
        }

        u8 has_title = ReadU8(rec_cursor);
        String title = {0};
        if (has_title) {
            title = ReadVarString(rec_cursor, 128);
        }

        // @TODO(traks) handle the packet
        break;
    }
    case SBP_ENTITY_TAG_QUERY: {
        LogInfo("Packet entity tag query");
        i32 transaction_id = ReadVarU32(rec_cursor);
        i32 entity_id = ReadVarU32(rec_cursor);
        break;
    }
    case SBP_INTERACT: {
        LogInfo("Packet interact");
        i32 entity_id = ReadVarU32(rec_cursor);
        i32 action = ReadVarU32(rec_cursor);

        switch (action) {
        case 0: { // interact
            i32 hand = ReadVarU32(rec_cursor);
            u8 secondary_action = ReadU8(rec_cursor);
            // @TODO(traks) implement
            break;
        }
        case 1: { // attack
            u8 secondary_action = ReadU8(rec_cursor);
            // @TODO(traks) implement
            break;
        }
        case 2: { // interact at
            float x = ReadF32(rec_cursor);
            float y = ReadF32(rec_cursor);
            float z = ReadF32(rec_cursor);
            i32 hand = ReadVarU32(rec_cursor);
            u8 secondary_action = ReadU8(rec_cursor);
            // @TODO(traks) implement
            break;
        }
        default:
            rec_cursor->error = 1;
        }
        break;
    }
    case SBP_JIGSAW_GENERATE: {
        LogInfo("Packet jigsaw generate");
        BlockPos block_pos = ReadBlockPos(rec_cursor);
        i32 levels = ReadVarU32(rec_cursor);
        u8 keep_jigsaws = ReadU8(rec_cursor);
        // @TODO(traks) processing
        break;
    }
    case SBP_KEEP_ALIVE: {
        u64 id = ReadU64(rec_cursor);
        if (player->last_keep_alive_sent_tick == id) {
            entity->flags |= PLAYER_GOT_ALIVE_RESPONSE;
        }
        break;
    }
    case SBP_LOCK_DIFFICULTY: {
        LogInfo("Packet lock difficulty");
        u8 locked = ReadU8(rec_cursor);
        break;
    }
    case SBP_MOVE_PLAYER_POS: {
        double x = ReadF64(rec_cursor);
        double y = ReadF64(rec_cursor);
        double z = ReadF64(rec_cursor);
        int on_ground = ReadU8(rec_cursor);
        process_move_player_packet(entity, x, y, z,
                entity->rot_x, entity->rot_y, on_ground);
        break;
    }
    case SBP_MOVE_PLAYER_POS_ROT: {
        double x = ReadF64(rec_cursor);
        double y = ReadF64(rec_cursor);
        double z = ReadF64(rec_cursor);
        float head_rot_y = ReadF32(rec_cursor);
        float head_rot_x = ReadF32(rec_cursor);
        int on_ground = ReadU8(rec_cursor);
        process_move_player_packet(entity, x, y, z,
                head_rot_x, head_rot_y, on_ground);
        break;
    }
    case SBP_MOVE_PLAYER_ROT: {
        float head_rot_y = ReadF32(rec_cursor);
        float head_rot_x = ReadF32(rec_cursor);
        int on_ground = ReadU8(rec_cursor);
        process_move_player_packet(entity,
                entity->x, entity->y, entity->z,
                head_rot_x, head_rot_y, on_ground);
        break;
    }
    case SBP_MOVE_PLAYER_STATUS: {
        int on_ground = ReadU8(rec_cursor);
        process_move_player_packet(entity,
                entity->x, entity->y, entity->z,
                entity->rot_x, entity->rot_y, on_ground);
        break;
    }
    case SBP_MOVE_VEHICLE: {
        LogInfo("Packet move vehicle");
        double x = ReadF64(rec_cursor);
        double y = ReadF64(rec_cursor);
        double z = ReadF64(rec_cursor);
        float rot_y = ReadF32(rec_cursor);
        float rot_x = ReadF32(rec_cursor);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_PADDLE_BOAT: {
        LogInfo("Packet paddle boat");
        u8 left = ReadU8(rec_cursor);
        u8 right = ReadU8(rec_cursor);
        break;
    }
    case SBP_PICK_ITEM: {
        LogInfo("Packet pick item");
        i32 slot = ReadVarU32(rec_cursor);
        break;
    }
    case SBP_PLACE_RECIPE: {
        LogInfo("Packet place recipe");
        u8 container_id = ReadU8(rec_cursor);
        String recipe = ReadVarString(rec_cursor, 32767);
        u8 shift_down = ReadU8(rec_cursor);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_PLAYER_ABILITIES: {
        LogInfo("Packet player abilities");
        u8 flags = ReadU8(rec_cursor);
        u8 flying = flags & 0x2;
        // @TODO(traks) validate whether the player can toggle fly
        if (flying) {
            if (entity->flags & PLAYER_CAN_FLY) {
                entity->flags |= PLAYER_FLYING;
            } else {
                entity->changed_data |= PLAYER_ABILITIES_CHANGED;
            }
        } else {
            entity->flags &= ~PLAYER_FLYING;
        }
        break;
    }
    case SBP_PLAYER_ACTION: {
        i32 action = ReadVarU32(rec_cursor);
        // @TODO(traks) validate block pos inside world
        BlockPos block_pos = ReadBlockPos(rec_cursor);
        u8 direction = ReadU8(rec_cursor);
        u32 sequenceNumber = ReadVarU32(rec_cursor);

        // @NOTE(traks) destroying blocks in survival works as follows:
        //
        //  1. first client sends start packet
        //  2. if player stops mining before block is broken, client sends
        //     abort packet
        //  3. if player breaks block on client side, then stop packet is sent
        //
        // However, if abort packet is sent, and the player starts mining the
        // same block again (without mining another block in between), no start
        // packet is sent, but a stop packet is sent and the server is supposed
        // to break the block!
        //
        // It seems that stopping mining and continuing mining with saved mining
        // progress, is handled client side and not server side.

        switch (action) {
        case 0: { // start destroy block
            // The player started mining the block. If the player is in
            // creative mode, the stop and abort packets are not sent.
            // @TODO(traks) implementation for other gamemodes
            if (player->gamemode == GAMEMODE_CREATIVE) {
                // @TODO(traks) ensure block pos is close to the
                // player and the chunk is sent to the player. Ensure not in
                // teleport, etc. Might get handled automatically if we deal
                // with the world ID properly (e.g. it's an invalid world while
                // the player is teleporting).

                WorldBlockPos destroyPos = {.worldId = entity->worldId, .xyz = block_pos};
                SetBlockResult destroy = WorldSetBlockState(destroyPos, get_default_block_state(BLOCK_AIR));
                if (destroy.failed) {
                    break;
                }

                // @TODO(traks) better block breaking logic. E.g. new state
                // should be water source block if waterlogged block is broken.
                // Should move this stuff to some generic block breaking
                // function, so we can do proper block updating of redstone dust
                // and stuff.
                int max_updates = 512;
                block_update_context buc = {
                    .blocks_to_update = MallocInArena(process_arena,
                            max_updates * sizeof (block_update)),
                    .update_count = 0,
                    .max_updates = max_updates
                };

                push_direct_neighbour_block_updates(destroyPos, &buc);
                propagate_block_updates(&buc);
                AckBlockChange(player, sequenceNumber);
            }
            break;
        }
        case 1: { // abort destroy block
            // The player stopped mining the block before it breaks.
            // @TODO(traks)
            AckBlockChange(player, sequenceNumber);
            break;
        }
        case 2: { // stop destroy block
            // The player stopped mining the block because it broke.
            // @TODO(traks)
            AckBlockChange(player, sequenceNumber);
            break;
        }
        case 3: { // drop all items
            // @TODO(traks) create item entities
            int sel_slot = player->selected_slot;
            item_stack * is = player->slots + sel_slot;

            // @NOTE(traks) client updates its view of the item stack size
            // itself, so no need to send updates for the slot if nothing
            // special happens
            if (is->size > 0) {
                if (drop_item(entity, is, is->size)) {
                    is->size = 0;
                    is->type = ITEM_AIR;
                } else {
                    player->slots_needing_update |= (u64) 1 << sel_slot;
                }
            }
            break;
        }
        case 4: { // drop item
            int sel_slot = player->selected_slot;
            item_stack * is = player->slots + sel_slot;

            // @NOTE(traks) client updates its view of the item stack size
            // itself, so no need to send updates for the slot if nothing
            // special happens
            if (is->size > 0) {
                if (drop_item(entity, is, 1)) {
                    is->size -= 1;
                    if (is->size == 0) {
                        is->type = ITEM_AIR;
                    }
                } else {
                    player->slots_needing_update |= (u64) 1 << sel_slot;
                }
            }
            break;
        }
        case 5: { // release use item
            // @TODO(traks)
            break;
        }
        case 6: { // swap held items
            int sel_slot = player->selected_slot;
            item_stack * sel = player->slots + sel_slot;
            item_stack * off = player->slots + PLAYER_OFF_HAND_SLOT;
            item_stack sel_copy = *sel;
            *sel = *off;
            *off = sel_copy;
            // client doesn't update its view of the inventory for
            // this packet, so send updates to the client
            player->slots_needing_update |= (u64) 1 << sel_slot;
            player->slots_needing_update |= (u64) 1 << PLAYER_OFF_HAND_SLOT;
            break;
        }
        default:
            rec_cursor->error = 1;
        }
        break;
    }
    case SBP_PLAYER_COMMAND: {
        i32 id = ReadVarU32(rec_cursor);
        i32 action = ReadVarU32(rec_cursor);
        i32 data = ReadVarU32(rec_cursor);

        switch (action) {
        case 0: // press shift key
            entity->flags |= ENTITY_SHIFTING;
            entity->changed_data |= 1 << ENTITY_DATA_FLAGS;
            if (!(entity->flags & PLAYER_FLYING)) {
                entity->pose = ENTITY_POSE_SHIFTING;
                entity->changed_data |= 1 << ENTITY_DATA_POSE;
            }
            break;
        case 1: // release shift key
            entity->flags &= ~ENTITY_SHIFTING;
            entity->changed_data |= 1 << ENTITY_DATA_FLAGS;
            entity->pose = ENTITY_POSE_STANDING;
            entity->changed_data |= 1 << ENTITY_DATA_POSE;
            break;
        case 2: // stop sleeping
            // @TODO(traks)
            break;
        case 3: // start sprinting
            entity->flags |= ENTITY_SPRINTING;
            entity->changed_data |= 1 << ENTITY_DATA_FLAGS;
            break;
        case 4: // stop sprinting
            entity->flags &= ~ENTITY_SPRINTING;
            entity->changed_data |= 1 << ENTITY_DATA_FLAGS;
            break;
        case 5: // start riding jump
            // @TODO(traks)
            break;
        case 6: // stop riding jump
            // @TODO(traks)
            break;
        case 7: // open inventory
            // @TODO(traks)
            break;
        case 8: // start fall flying
            // @TODO(traks)
            break;
        default:
            rec_cursor->error = 1;
        }
        break;
    }
    case SBP_PLAYER_INPUT: {
        LogInfo("Packet player input");
        // @TODO(traks) read packet
        break;
    }
    case SBP_PONG: {
        LogInfo("Packet pong");
        i32 id = ReadU32(rec_cursor);
        // @TODO(traks) read packet
        break;
    }
    case SBP_RECIPE_BOOK_CHANGE_SETTINGS: {
        LogInfo("Packet recipe book change settings");
        // @TODO(traks) read packet
        break;
    }
    case SBP_RECIPE_BOOK_SEEN_RECIPE: {
        LogInfo("Packet recipe book seen recipe");
        // @TODO(traks) read packet
        break;
    }
    case SBP_RENAME_ITEM: {
        LogInfo("Packet rename item");
        String name = ReadVarString(rec_cursor, 32767);
        break;
    }
    case SBP_RESOURCE_PACK: {
        LogInfo("Packet resource pack");
        i32 action = ReadVarU32(rec_cursor);
        break;
    }
    case SBP_SEEN_ADVANCEMENTS: {
        LogInfo("Packet seen advancements");
        i32 action = ReadVarU32(rec_cursor);
        // @TODO(traks) further processing
        break;
    }
    case SBP_SELECT_TRADE: {
        LogInfo("Packet select trade");
        i32 item = ReadVarU32(rec_cursor);
        break;
    }
    case SBP_SET_BEACON: {
        LogInfo("Packet set beacon");
        i32 primary_effect = ReadVarU32(rec_cursor);
        i32 secondary_effect = ReadVarU32(rec_cursor);
        break;
    }
    case SBP_SET_CARRIED_ITEM: {
        LogInfo("Set carried item");
        u16 slot = ReadU16(rec_cursor);
        if (slot > PLAYER_LAST_HOTBAR_SLOT - PLAYER_FIRST_HOTBAR_SLOT) {
            rec_cursor->error = 1;
            break;
        }
        player->selected_slot = PLAYER_FIRST_HOTBAR_SLOT + slot;
        break;
    }
    case SBP_SET_COMMAND_BLOCK: {
        LogInfo("Packet set command block");
        u64 block_pos = ReadU64(rec_cursor);
        String command = ReadVarString(rec_cursor, 32767);
        i32 mode = ReadVarU32(rec_cursor);
        u8 flags = ReadU8(rec_cursor);
        u8 track_output = (flags & 0x1);
        u8 conditional = (flags & 0x2);
        u8 automatic = (flags & 0x4);
        break;
    }
    case SBP_SET_COMMAND_MINECART: {
        LogInfo("Packet set command minecart");
        i32 entity_id = ReadVarU32(rec_cursor);
        String command = ReadVarString(rec_cursor, 32767);
        u8 track_output = ReadU8(rec_cursor);
        break;
    }
    case SBP_SET_CREATIVE_MODE_SLOT: {
        LogInfo("Set creative mode slot");
        u16 slot = ReadU16(rec_cursor);
        u8 has_item = ReadU8(rec_cursor);

        if (slot >= PLAYER_SLOTS) {
            rec_cursor->error = 1;
            break;
        }

        item_stack * is = player->slots + slot;
        *is = (item_stack) {0};

        if (has_item) {
            is->type = ReadVarU32(rec_cursor);
            is->size = ReadU8(rec_cursor);

            if (is->type < 0 || is->type >= ITEM_TYPE_COUNT || is->size == 0) {
                is->type = 0;
                player->slots_needing_update |=
                        (u64) 1 << slot;
            }

            String type_name = get_resource_loc(is->type, &serv->item_resource_table);
            LogInfo("Set creative slot: %.*s", (int) type_name.size, type_name.data);

            u8 max_size = get_max_stack_size(is->type);
            if (is->size > max_size) {
                is->size = max_size;
                player->slots_needing_update |=
                        (u64) 1 << slot;
            }

            // @TODO(traks) better value than 64 for the max level
            NbtCompound itemNbt = NbtRead(rec_cursor, process_arena);
            if (rec_cursor->error) {
                break;
            }

            // @TODO(traks) use NBT data to construct item stack
        }
        break;
    }
    case SBP_SET_JIGSAW_BLOCK: {
        LogInfo("Packet set jigsaw block");
        u64 block_pos = ReadU64(rec_cursor);
        String name = ReadVarString(rec_cursor, 32767);
        String target = ReadVarString(rec_cursor, 32767);
        String pool = ReadVarString(rec_cursor, 32767);
        String final_state = ReadVarString(rec_cursor, 32767);
        String joint = ReadVarString(rec_cursor, 32767);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_SET_STRUCTURE_BLOCK: {
        LogInfo("Packet set structure block");
        u64 block_pos = ReadU64(rec_cursor);
        i32 update_type = ReadVarU32(rec_cursor);
        i32 mode = ReadVarU32(rec_cursor);
        String name = ReadVarString(rec_cursor, 32767);
        // @TODO(traks) read signed bytes instead
        u8 offset_x = ReadU8(rec_cursor);
        u8 offset_y = ReadU8(rec_cursor);
        u8 offset_z = ReadU8(rec_cursor);
        u8 size_x = ReadU8(rec_cursor);
        u8 size_y = ReadU8(rec_cursor);
        u8 size_z = ReadU8(rec_cursor);
        i32 mirror = ReadVarU32(rec_cursor);
        i32 rotation = ReadVarU32(rec_cursor);
        String data = ReadVarString(rec_cursor, 12);
        // @TODO(traks) further reading
        break;
    }
    case SBP_SIGN_UPDATE: {
        LogInfo("Packet sign update");
        u64 block_pos = ReadU64(rec_cursor);
        String lines[4];
        for (int i = 0; i < ARRAY_SIZE(lines); i++) {
            lines[i] = ReadVarString(rec_cursor, 384);
        }
        break;
    }
    case SBP_SWING: {
        // LogInfo("Packet swing");
        i32 hand = ReadVarU32(rec_cursor);
        break;
    }
    case SBP_TELEPORT_TO_ENTITY: {
        LogInfo("Packet teleport to entity");
        // @TODO(traks) read UUID instead
        u64 uuid_high = ReadU64(rec_cursor);
        u64 uuid_low = ReadU64(rec_cursor);
        break;
    }
    case SBP_USE_ITEM_ON: {
        LogInfo("Packet use item on");
        i32 hand = ReadVarU32(rec_cursor);
        BlockPos clicked_pos = ReadBlockPos(rec_cursor);
        i32 clicked_face = ReadVarU32(rec_cursor);
        float click_offset_x = ReadF32(rec_cursor);
        float click_offset_y = ReadF32(rec_cursor);
        float click_offset_z = ReadF32(rec_cursor);
        // @TODO(traks) figure out what this is used for
        u8 is_inside = ReadU8(rec_cursor);
        u32 sequenceNumber = ReadVarU32(rec_cursor);

        // @TODO(traks) if we cancel at any point and don't kick the
        // client, send some packets to the client to make the
        // original blocks reappear, otherwise we'll get a desync

        if (hand != 0 && hand != 1) {
            rec_cursor->error = 1;
            break;
        }
        if (clicked_face < 0 || clicked_face >= 6) {
            rec_cursor->error = 1;
            break;
        }
        if (click_offset_x < 0 || click_offset_x > 1
                || click_offset_y < 0 || click_offset_y > 1
                || click_offset_z < 0 || click_offset_z > 1) {
            rec_cursor->error = 1;
            break;
        }

        // @TODO(traks) ensure clicked pos is inside the world and the eventual
        // target position as well, so no assertions fire when setting the block
        // in the chunk (e.g. because of negative y)

        process_use_item_on_packet(entity, hand, clicked_pos,
                clicked_face, click_offset_x, click_offset_y, click_offset_z,
                is_inside, process_arena);
        AckBlockChange(player, sequenceNumber);
        break;
    }
    case SBP_USE_ITEM: {
        LogInfo("Packet use item");
        i32 hand = ReadVarU32(rec_cursor);
        u32 sequenceNumber = ReadVarU32(rec_cursor);

        AckBlockChange(player, sequenceNumber);
        break;
    }
    default: {
        LogInfo("Unknown player packet id %jd", (intmax_t) packet_id);
        rec_cursor->error = 1;
    }
    }
}

static int
chunk_cache_index(ChunkPos pos) {
    // Do some remainder operations first so we don't integer overflow. Note
    // that the remainder operator can produce negative numbers.
    int n = MAX_CHUNK_CACHE_DIAM * MAX_CHUNK_CACHE_DIAM;
    long x = (pos.x % n) + n;
    long z = (pos.z % n) + n;
    return (x * MAX_CHUNK_CACHE_DIAM + z) % n;
}

static void
begin_packet(Cursor * send_cursor, i32 id) {
    if (send_cursor->size - send_cursor->index < 6) {
        send_cursor->error = 1;
        return;
    }

    // LogInfo("Packet: %d", (int) id);

    send_cursor->mark = send_cursor->index;
    // NOTE(traks): reserve space for internal header and skip some bytes for
    // packet size varint at the start
    CursorSkip(send_cursor, 1 + 5);
    WriteVarU32(send_cursor, id);
}

static void
finish_packet(Cursor * send_cursor, entity_base * player) {
    // We use the written data to determine the packet size instead of
    // calculating the packet size up front. The major benefit is that
    // calculating the packet size up front is very error prone and requires a
    // lot of maintainance (in case of packet format changes).
    //
    // The downside is that we have to copy all packet data an additional time,
    // because Mojang decided to encode packet sizes with a variable-size
    // encoding.

    if (send_cursor->error != 0 || send_cursor->index == send_cursor->size) {
        // @NOTE(traks) packet ID could be invalid, but print it anyway
        send_cursor->index = send_cursor->mark;
        CursorSkip(send_cursor, 6);
        i32 maybeId = ReadVarU32(send_cursor);
        LogInfo("Finished invalid packet: %d", maybeId);
        return;
    }

    int packet_end = send_cursor->index;
    send_cursor->index = send_cursor->mark;
    i32 packet_size = packet_end - send_cursor->index - 6;

    int size_offset = 5 - VarU32Size(packet_size);
    int internal_header = size_offset;
    if (player->flags & PLAYER_PACKET_COMPRESSION) {
        internal_header |= 0x80;
    }
    send_cursor->data[send_cursor->index] = internal_header;
    send_cursor->index += 1 + size_offset;

    WriteVarU32(send_cursor, packet_size);

    send_cursor->index = packet_end;

    // LogInfo("Packet size: %d", (int) packet_size);
}

void
send_chunk_fully(Cursor * send_cursor, Chunk * ch,
        entity_base * entity, MemoryArena * tick_arena) {
    BeginTimings(SendChunkFully);

    // TODO(traks): make uncompressed data as compact as possible, so the
    // compressor doesn't choke on these packets. As of writing this, these
    // packets are ~170KiB, and take ~2ms to compress. If we send a chunk to
    // 1000 players every tick, that's 2 seconds in a tick of 50ms, so we need
    // 40 CPU cores for that.
    begin_packet(send_cursor, CBP_LEVEL_CHUNK_WITH_LIGHT);
    WriteU32(send_cursor, ch->pos.x);
    WriteU32(send_cursor, ch->pos.z);

    BeginTimings(WriteHeightMap);

    // @NOTE(traks) height map NBT
    {
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR(""));

        nbt_write_key(send_cursor, NBT_TAG_LONG_ARRAY, STR("MOTION_BLOCKING"));
        // number of elements in long array
        i32 bitsPerValue = CeilLog2U32(WORLD_HEIGHT + 1);
        i32 valuesPerLong = 64 / bitsPerValue;
        i32 longs = (16 * 16  + valuesPerLong - 1) / valuesPerLong;
        WriteU32(send_cursor, longs);
        u64 val = 0;
        int offset = 0;

        for (int j = 0; j < 16 * 16; j++) {
            u64 height = ch->motion_blocking_height_map[j];
            val |= height << offset;
            offset += bitsPerValue;

            if (offset > 64 - bitsPerValue) {
                WriteU64(send_cursor, val);
                val = 0;
                offset = 0;
            }
        }

        if (offset != 0) {
            WriteU64(send_cursor, val);
        }

        WriteU8(send_cursor, NBT_TAG_END);
    }

    EndTimings(WriteHeightMap);

    // @NOTE(traks) calculate size of block data

    BeginTimings(WriteBlocks);

    // calculate total size of chunk section data
    i32 section_data_size = 0;
    // @TODO(traks) compute bits per block using block type table
    int bits_per_block = CeilLog2U32(serv->vanilla_block_state_count);
    int blocks_per_long = 64 / bits_per_block;

    for (int i = 0; i < SECTIONS_PER_CHUNK; i++) {
        ChunkSection * section = ch->sections + i;
        if (section->nonAirCount == 0) {
            section_data_size += 2 + 1 + 1 + 1;
        } else {
            // size of non-air count + bits per block
            section_data_size += 2 + 1;
            // size of block state data in longs
            int longs = (16 * 16 * 16 + blocks_per_long - 1) / blocks_per_long;
            section_data_size += VarU32Size(longs);
            // number of bytes used to store block state data
            section_data_size += longs * 8;
        }

        // @NOTE(traks) size of biome data
        section_data_size += 1 + 1 + 1;
    }

    // @NOTE(traks) write block data

    WriteVarU32(send_cursor, section_data_size);

    for (i32 i = 0; i < SECTIONS_PER_CHUNK; i++) {
        ChunkSection * section = ch->sections + i;
        WriteU16(send_cursor, section->nonAirCount); // # of non-air blocks
        if (section->nonAirCount == 0) {
            WriteU8(send_cursor, 0);
            WriteVarU32(send_cursor, 0);
            WriteVarU32(send_cursor, 0);
        } else {
            WriteU8(send_cursor, bits_per_block);

            // number of longs used for the block states
            int longs = (16 * 16 * 16 + blocks_per_long - 1) / blocks_per_long;
            WriteVarU32(send_cursor, longs);
            u64 val = 0;
            int offset = 0;
            assert(blocks_per_long == 4);

            u8 * cursorData = send_cursor->data + send_cursor->index;
            if (CursorSkip(send_cursor, longs * 8)) {
                for (i32 longIndex = 0; longIndex < longs; longIndex++) {
                    u16 blockStates[4];
                    blockStates[0] = SectionGetBlockState(section->blockData, 4 * longIndex + 0);
                    blockStates[1] = SectionGetBlockState(section->blockData, 4 * longIndex + 1);
                    blockStates[2] = SectionGetBlockState(section->blockData, 4 * longIndex + 2);
                    blockStates[3] = SectionGetBlockState(section->blockData, 4 * longIndex + 3);
                    u64 longValue = ((u64) blockStates[0])
                            | ((u64) blockStates[1] << 15)
                            | ((u64) blockStates[2] << 30)
                            | ((u64) blockStates[3] << 45);
                    WriteDirectU64(cursorData + (8 * longIndex), longValue);
                }
            }
        }

        // @NOTE(traks) write biome data. Currently we just write all plains
        // biome -> 0 bit palette
        WriteU8(send_cursor, 0);
        WriteVarU32(send_cursor, 1);
        WriteVarU32(send_cursor, 0);
    }

    // number of block entities
    WriteVarU32(send_cursor, 0);

    EndTimings(WriteBlocks);

    // @NOTE(traks) now write lighting data

    // NOTE(traks): not sure why the server is even sending light values to the
    // client. The client can calculate it itself. It almost takes longer to
    // compress these packets than to compute the light. Also is a waste of
    // bandwidth.

    BeginTimings(WriteLight);

    i32 lightSections = LIGHT_SECTIONS_PER_CHUNK;
    // @NOTE(traks) light sections present as arrays in this packet
    u64 sky_light_mask = (0x1 << lightSections) - 1;
    u64 block_light_mask = (0x1 << lightSections) - 1;
    // @NOTE(traks) sections with all light values equal to 0
    u64 zero_sky_light_mask = 0;
    u64 zero_block_light_mask = 0;

    // @TODO(traks) figure out what trust edges actually does
    WriteU8(send_cursor, 1); // trust edges
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, sky_light_mask);
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, block_light_mask);
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, zero_sky_light_mask);
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, zero_block_light_mask);

    WriteVarU32(send_cursor, lightSections);
    for (int sectionIndex = 0; sectionIndex < lightSections; sectionIndex++) {
        LightSection * section = ch->lightSections + sectionIndex;
        WriteVarU32(send_cursor, 2048);
        WriteData(send_cursor, section->skyLight, 2048);
    }

    WriteVarU32(send_cursor, lightSections);
    for (int sectionIndex = 0; sectionIndex < lightSections; sectionIndex++) {
        LightSection * section = ch->lightSections + sectionIndex;
        WriteVarU32(send_cursor, 2048);
        WriteData(send_cursor, section->blockLight, 2048);
    }

    EndTimings(WriteLight);

    finish_packet(send_cursor, entity);

    EndTimings(SendChunkFully);
}

static void
send_light_update(Cursor * send_cursor, ChunkPos pos, Chunk * ch,
        entity_base * entity, MemoryArena * tick_arena) {
    BeginTimings(SendLightUpdate);

    // @TODO(traks) send the real lighting data

    begin_packet(send_cursor, CBP_LIGHT_UPDATE);
    WriteVarU32(send_cursor, pos.x);
    WriteVarU32(send_cursor, pos.z);

    // light sections present as arrays in this packet
    // @NOTE(traks) add 2 for light below the world and light above the world
    i32 lightSections = SECTIONS_PER_CHUNK + 2;
    u64 sky_light_mask = (0x1 << lightSections) - 1;
    u64 block_light_mask = (0x1 << lightSections) - 1;
    // sections with all light values equal to 0
    u64 zero_sky_light_mask = 0;
    u64 zero_block_light_mask = 0;

    WriteU8(send_cursor, 1); // trust edges
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, sky_light_mask);
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, block_light_mask);
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, zero_sky_light_mask);
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, zero_block_light_mask);

    WriteVarU32(send_cursor, lightSections);
    for (int i = 0; i < lightSections; i++) {
        WriteVarU32(send_cursor, 2048);
        for (int j = 0; j < 4096; j += 2) {
            u8 light = 0xff;
            WriteU8(send_cursor, light);
        }
    }

    WriteVarU32(send_cursor, lightSections);
    for (int i = 0; i < lightSections; i++) {
        WriteVarU32(send_cursor, 2048);
        for (int j = 0; j < 4096; j += 2) {
            u8 light = 0;
            WriteU8(send_cursor, light);
        }
    }

    finish_packet(send_cursor, entity);

    EndTimings(SendLightUpdate);
}

static void SendPlayerTeleport(entity_base * player, Cursor * send_cursor) {
    begin_packet(send_cursor, CBP_PLAYER_POSITION);
    WriteF64(send_cursor, player->x);
    WriteF64(send_cursor, player->y);
    WriteF64(send_cursor, player->z);
    WriteF32(send_cursor, player->rot_y);
    WriteF32(send_cursor, player->rot_x);
    WriteU8(send_cursor, 0); // relative arguments
    WriteVarU32(send_cursor, player->player.current_teleport_id);
    finish_packet(send_cursor, player);

    player->flags |= PLAYER_SENT_TELEPORT;
}

// TODO(traks): Perhaps we shouldn't remove players mid-tick. As of writing
// this, this is causing some issues. If a network error occurs, the player will
// be disconnected immediately. But they might still be in the tab list
// (live/added/removed). If another player updates its tablist then, it will try
// to resolve the disconnected player and will fail, because the player entity
// has already been removed. The entire tab list system is written to be only
// updated once per tick, so this causes issues.
//
// It's probably fine to remove players mid tick. The tab list system should be
// changed so you don't need to resolve entities to send tab list data.
static void
disconnect_player_now(entity_base * entity) {
    entity_player * player = &entity->player;
    close(player->sock);

    i32 chunk_cache_min_x = player->chunkCacheCentreX - player->chunkCacheRadius;
    i32 chunk_cache_max_x = player->chunkCacheCentreX + player->chunkCacheRadius;
    i32 chunk_cache_min_z = player->chunkCacheCentreZ - player->chunkCacheRadius;
    i32 chunk_cache_max_z = player->chunkCacheCentreZ + player->chunkCacheRadius;

    for (i32 x = chunk_cache_min_x; x <= chunk_cache_max_x; x++) {
        for (i32 z = chunk_cache_min_z; z <= chunk_cache_max_z; z++) {
            WorldChunkPos pos = {.worldId = entity->worldId, .x = x, .z = z};
            PlayerChunkCacheEntry * cacheEntry = player->chunkCache + chunk_cache_index(pos.xz);
            if (cacheEntry->flags & PLAYER_CHUNK_ADDED_INTEREST) {
                AddChunkInterest(pos, -1);
            }
        }
    }

    free(player->rec_buf);
    free(player->send_buf);

    evict_entity(entity->eid);
}

static float
degree_diff(float to, float from) {
    float div = (to - from) / 360 + 0.5f;
    float mod = div - floor(div);
    float res = mod * 360 - 180;
    return res;
}

static void
merge_stack_to_player_slot(entity_base * player, int slot, item_stack * to_add) {
    // @TODO(traks) also ensure damage levels and NBT data are similar
    item_stack * is = player->player.slots + slot;

    if (is->type == to_add->type) {
        int max_stack_size = get_max_stack_size(is->type);
        int add = MIN(max_stack_size - is->size, to_add->size);
        is->size += add;
        to_add->size -= add;
        if (to_add->size == 0) {
            to_add->type = ITEM_AIR;
        }

        if (add != 0) {
            player->player.slots_needing_update |= (u64) 1 << slot;
        }
    }
}

void
add_stack_to_player_inventory(entity_base * player, item_stack * to_add) {
    merge_stack_to_player_slot(player, player->player.selected_slot, to_add);
    merge_stack_to_player_slot(player, PLAYER_OFF_HAND_SLOT, to_add);

    for (int i = PLAYER_FIRST_HOTBAR_SLOT; i <= PLAYER_LAST_HOTBAR_SLOT; i++) {
        merge_stack_to_player_slot(player, i, to_add);
    }
    for (int i = PLAYER_FIRST_MAIN_INV_SLOT; i <= PLAYER_LAST_MAIN_INV_SLOT; i++) {
        merge_stack_to_player_slot(player, i, to_add);
    }

    if (to_add->size != 0) {
        // try to put remaining stack in empty spot of inventory
        for (int i = PLAYER_FIRST_HOTBAR_SLOT; i <= PLAYER_LAST_HOTBAR_SLOT; i++) {
            item_stack * is = player->player.slots + i;
            if (is->type == ITEM_AIR) {
                *is = *to_add;
                *to_add = (item_stack) {0};
                player->player.slots_needing_update |= (u64) 1 << i;
                return;
            }
        }
        for (int i = PLAYER_FIRST_MAIN_INV_SLOT; i <= PLAYER_LAST_MAIN_INV_SLOT; i++) {
            item_stack * is = player->player.slots + i;
            if (is->type == ITEM_AIR) {
                *is = *to_add;
                *to_add = (item_stack) {0};
                player->player.slots_needing_update |= (u64) 1 << i;
                return;
            }
        }
    }
}

void
tick_player(entity_base * player, MemoryArena * tick_arena) {
    BeginTimings(TickPlayer);

    // @TODO(traks) remove
    if (0) {
        i32 x = floor(player->x);
        i32 y = floor(player->y);
        i32 z = floor(player->z);
        WorldChunkPos chunkPos = {.worldId = player->worldId, .x = x >> 4, .z = z >> 4};
        Chunk * ch = GetChunkIfLoaded(chunkPos);
        if (ch != NULL) {
            i32 basedY = y - MIN_WORLD_Y + 16;
            i32 sectionIndex = basedY >> 4;
            i32 posIndex = ((basedY & 0xf) << 8) | ((z & 0xf) << 4) | (x & 0xf);
            i32 skyLight = GetSectionLight(ch->lightSections[basedY >> 4].skyLight, posIndex);
            i32 blockLight = GetSectionLight(ch->lightSections[basedY >> 4].blockLight, posIndex);
            i32 maxHeight = ch->motion_blocking_height_map[((z & 0xf) << 4) | (x & 0xf)];

            if (serv->global_msg_count < ARRAY_SIZE(serv->global_msgs)) {
                global_msg * msg = serv->global_msgs + serv->global_msg_count;
                serv->global_msg_count++;
                int text_size = sprintf(
                        (void *) msg->text, "sky light %d, block light %d, max height: %d",
                        skyLight, blockLight, maxHeight);
                msg->size = text_size;
            }
        }
    }

    assert(player->type == ENTITY_PLAYER);
    int sock = player->player.sock;

    // TODO(traks): We might want to move this to an event-based system (such as
    // epoll/kqueue/etc.). Not sure if offloading some of the work to a
    // different thread is really worth it (haven't tested). However, it seems
    // like TCP ACKs aren't sent back to the client until our program actually
    // reads from the OS buffers (at least on macOS). I'm thinking this might
    // give the client's TCP stack a wrong impression about the server latency.
    //
    // In the future we might also want to respond to certain packets
    // immediately (such as tab completes, statistics requests and chat
    // previewing), instead of waiting for the next tick. Since waiting could
    // delay the handling of the packet upward of 50ms.
    //
    // We also should actually also respond to server list pings as soon as
    // possible, so the client's ping counter is accurate.
    ssize_t rec_size = recv(sock, player->player.rec_buf + player->player.rec_cursor,
            player->player.rec_buf_size - player->player.rec_cursor, 0);

    if (rec_size == 0) {
        disconnect_player_now(player);
    } else if (rec_size == -1) {
        // EAGAIN means no data received
        if (errno != EAGAIN) {
            LogErrno("Couldn't receive protocol data from player: %s");
            disconnect_player_now(player);
        }
    } else {
        player->player.rec_cursor += rec_size;

        Cursor rec_cursor = {
            .data = player->player.rec_buf,
            .size = player->player.rec_cursor
        };

        // @TODO(traks) rate limit incoming packets per player

        for (;;) {
            Cursor packet_cursor = rec_cursor;
            i32 packet_size = ReadVarU32(&packet_cursor);

            if (packet_cursor.error != 0) {
                // packet size not fully received yet
                break;
            }
            if (packet_size > player->player.rec_buf_size - 5 || packet_size <= 0) {
                disconnect_player_now(player);
                break;
            }
            if (packet_size > packet_cursor.size - packet_cursor.index) {
                // packet not fully received yet
                break;
            }

            MemoryArena process_arena = *tick_arena;
            packet_cursor.size = packet_cursor.index + packet_size;
            rec_cursor.index = packet_cursor.size;

            if (player->flags & PLAYER_PACKET_COMPRESSION) {
                // ignore the uncompressed packet size, since we require all
                // packets to be compressed
                ReadVarU32(&packet_cursor);

                // @TODO(traks) move to a zlib alternative that is optimised
                // for single pass inflate/deflate. If we don't end up doing
                // this, make sure the code below is actually correct (when
                // do we need to clean stuff up?)!

                z_stream zstream;
                zstream.zalloc = Z_NULL;
                zstream.zfree = Z_NULL;
                zstream.opaque = Z_NULL;

                if (inflateInit2(&zstream, 0) != Z_OK) {
                    LogInfo("inflateInit failed");
                    disconnect_player_now(player);
                    break;
                }

                zstream.next_in = packet_cursor.data + packet_cursor.index;
                zstream.avail_in = packet_cursor.size - packet_cursor.index;

                size_t max_uncompressed_size = 2 * (1 << 20);
                unsigned char * uncompressed = MallocInArena(&process_arena,
                        max_uncompressed_size);

                zstream.next_out = uncompressed;
                zstream.avail_out = max_uncompressed_size;

                if (inflate(&zstream, Z_FINISH) != Z_STREAM_END) {
                    LogInfo("Failed to finish inflating packet: %s", zstream.msg);
                    disconnect_player_now(player);
                    break;
                }

                if (inflateEnd(&zstream) != Z_OK) {
                    LogInfo("inflateEnd failed");
                    disconnect_player_now(player);
                    break;
                }

                if (zstream.avail_in != 0) {
                    LogInfo("Didn't inflate entire packet");
                    disconnect_player_now(player);
                    break;
                }

                packet_cursor = (Cursor) {
                    .data = uncompressed,
                    .size = zstream.total_out,
                };
            }

            process_packet(player, &packet_cursor, &process_arena);

            if (packet_cursor.error != 0) {
                LogInfo("Player protocol error occurred");
                disconnect_player_now(player);
                break;
            }

            if (packet_cursor.index != packet_cursor.size) {
                LogInfo("Player protocol packet not fully read");
                disconnect_player_now(player);
                break;
            }
        }

        memmove(rec_cursor.data, rec_cursor.data + rec_cursor.index,
                rec_cursor.size - rec_cursor.index);
        player->player.rec_cursor = rec_cursor.size - rec_cursor.index;
    }

    // @TODO(traks) only here because players could be disconnected and get
    // all their data cleaned up immediately if some packet handling error
    // occurs above. Eventually we should handle errors more gracefully.
    // Then this check shouldn't be necessary anymore.
    if (!(player->flags & ENTITY_IN_USE)) {
        goto bail;
    }

    // try to pick up nearby items
    for (int i = 0; i < ARRAY_SIZE(serv->entities); i++) {
        entity_base * entity = serv->entities + i;
        if ((entity->flags & ENTITY_IN_USE) == 0) {
            continue;
        }
        if (entity->type != ENTITY_ITEM) {
            continue;
        }
        if (entity->worldId != player->worldId) {
            continue;
        }
        if (entity->item.pickup_timeout != 0) {
            continue;
        }

        double test_min_x = player->x - player->collision_width / 2 - 1;
        double test_min_y = player->y - 0.5;
        double test_min_z = player->z - player->collision_width / 2 - 1;
        double test_max_x = player->x + player->collision_width / 2 + 1;
        double test_max_y = player->y + player->collision_height + 0.5;
        double test_max_z = player->z + player->collision_width / 2 + 1;

        double bb_min_x = entity->x - entity->collision_width / 2;
        double bb_min_y = entity->y;
        double bb_min_z = entity->z - entity->collision_width / 2;
        double bb_max_x = entity->x + entity->collision_width / 2;
        double bb_max_y = entity->y + entity->collision_height;
        double bb_max_z = entity->z + entity->collision_width / 2;

        if (test_min_x <= bb_max_x && test_max_x >= bb_min_x
                && test_min_y <= bb_max_y && test_max_y >= bb_min_y
                && test_min_z <= bb_max_z && test_max_z >= bb_min_z) {
            // boxes intersect

            item_stack * contents = &entity->item.contents;
            int initial_size = contents->size;
            add_stack_to_player_inventory(player, contents);

            int picked_up_size = initial_size - contents->size;

            if (picked_up_size != 0) {
                // prepare to send a packet for the pickup animation
                player->player.picked_up_item_id = entity->eid;
                player->player.picked_up_item_size = picked_up_size;
                player->player.picked_up_tick = serv->current_tick;

                // @TODO(traks) we currently restrict to at most one pickup per
                // tick. Should this be increased? 1 stack per tick is probably
                // fast enough.
                break;
            }
        }
    }

bail:
    EndTimings(TickPlayer);
}

static void
nbt_write_dimension_type(Cursor * send_cursor,
        dimension_type * dim_type) {
    if (dim_type->fixed_time != -1) {
        nbt_write_key(send_cursor, NBT_TAG_INT, STR("fixed_time"));
        WriteU32(send_cursor, dim_type->fixed_time);
    }

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("has_skylight"));
    WriteU8(send_cursor, !!(dim_type->flags & DIMENSION_HAS_SKYLIGHT));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("has_ceiling"));
    WriteU8(send_cursor, !!(dim_type->flags & DIMENSION_HAS_CEILING));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("ultrawarm"));
    WriteU8(send_cursor, !!(dim_type->flags & DIMENSION_ULTRAWARM));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("natural"));
    WriteU8(send_cursor, !!(dim_type->flags & DIMENSION_NATURAL));

    nbt_write_key(send_cursor, NBT_TAG_DOUBLE, STR("coordinate_scale"));
    WriteF64(send_cursor, dim_type->coordinate_scale);

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("piglin_safe"));
    WriteU8(send_cursor, !!(dim_type->flags & DIMENSION_PIGLIN_SAFE));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("bed_works"));
    WriteU8(send_cursor, !!(dim_type->flags & DIMENSION_BED_WORKS));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("respawn_anchor_works"));
    WriteU8(send_cursor, !!(dim_type->flags & DIMENSION_RESPAWN_ANCHOR_WORKS));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("has_raids"));
    WriteU8(send_cursor, !!(dim_type->flags & DIMENSION_HAS_RAIDS));

    // TODO(traks): this actually defines a sampler, e.g. an int range.
    // Currently we just do constant 0 for simplicity.
    nbt_write_key(send_cursor, NBT_TAG_INT, STR("monster_spawn_light_level"));
    WriteU32(send_cursor, 0);

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("monster_spawn_block_light_limit"));
    WriteU32(send_cursor, dim_type->monsterSpawnMaxBlockLight);

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("min_y"));
    WriteU32(send_cursor, dim_type->min_y);

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("height"));
    WriteU32(send_cursor, dim_type->height);

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("logical_height"));
    WriteU32(send_cursor, dim_type->logical_height);

    nbt_write_key(send_cursor, NBT_TAG_STRING, STR("infiniburn"));
    String infiniburn = {
        .data = dim_type->infiniburn,
        .size = dim_type->infiniburn_size
    };
    nbt_write_string(send_cursor, infiniburn);

    nbt_write_key(send_cursor, NBT_TAG_STRING, STR("effects"));
    String effects = {
        .data = dim_type->effects,
        .size = dim_type->effects_size
    };
    nbt_write_string(send_cursor, effects);

    nbt_write_key(send_cursor, NBT_TAG_FLOAT, STR("ambient_light"));
    WriteF32(send_cursor, dim_type->ambient_light);
}

static void
nbt_write_biome(Cursor * send_cursor, biome * b) {
    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("has_precipitation"));
    WriteU8(send_cursor, b->has_precipitation);

    nbt_write_key(send_cursor, NBT_TAG_FLOAT, STR("temperature"));
    WriteF32(send_cursor, b->temperature);

    if (b->temperature_mod != BIOME_TEMPERATURE_MOD_NONE) {
        nbt_write_key(send_cursor, NBT_TAG_STRING, STR("temperature_modifier"));
        switch (b->temperature_mod) {
        case BIOME_TEMPERATURE_MOD_FROZEN:
            nbt_write_string(send_cursor, STR("frozen"));
            break;
        default:
            assert(0);
        }
    }

    nbt_write_key(send_cursor, NBT_TAG_FLOAT, STR("downfall"));
    WriteF32(send_cursor, b->downfall);

    // TODO(traks): there are more options for these special effects I think.
    // Just using the required ones for now. Can fill in the optional ones later

    // special effects
    nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("effects"));

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("fog_color"));
    WriteU32(send_cursor, b->fog_colour);

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("water_color"));
    WriteU32(send_cursor, b->water_colour);

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("water_fog_color"));
    WriteU32(send_cursor, b->water_fog_colour);

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("sky_color"));
    WriteU32(send_cursor, b->sky_colour);

    if (b->foliage_colour_override != -1) {
        nbt_write_key(send_cursor, NBT_TAG_INT, STR("foliage_color"));
        WriteU32(send_cursor, b->foliage_colour_override);
    }

    if (b->grass_colour_override != -1) {
        nbt_write_key(send_cursor, NBT_TAG_INT, STR("grass_color"));
        WriteU32(send_cursor, b->grass_colour_override);
    }

    if (b->grass_colour_mod != BIOME_GRASS_COLOUR_MOD_NONE) {
        nbt_write_key(send_cursor, NBT_TAG_STRING, STR("grass_color_modifier"));
        switch (b->grass_colour_mod) {
        case BIOME_GRASS_COLOUR_MOD_DARK_FOREST:
            nbt_write_string(send_cursor, STR("dark_forest"));
            break;
        case BIOME_GRASS_COLOUR_MOD_SWAMP:
            nbt_write_string(send_cursor, STR("swamp"));
            break;
        default:
            assert(0);
        }
    }

    // @TODO(traks) ambient particle effects

    if (b->ambient_sound_size > 0) {
        nbt_write_key(send_cursor, NBT_TAG_STRING, STR("ambient_sound"));
        String ambient_sound = {
            .data = b->ambient_sound,
            .size = b->ambient_sound_size
        };
        nbt_write_string(send_cursor, ambient_sound);
    }

    if (b->mood_sound_size > 0) {
        // mood sound
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("mood_sound"));

        nbt_write_key(send_cursor, NBT_TAG_STRING, STR("sound"));
        String mood_sound = {
            .data = b->mood_sound,
            .size = b->mood_sound_size
        };
        nbt_write_string(send_cursor, mood_sound);

        nbt_write_key(send_cursor, NBT_TAG_INT, STR("tick_delay"));
        WriteU32(send_cursor, b->mood_sound_tick_delay);

        nbt_write_key(send_cursor, NBT_TAG_INT, STR("block_search_extent"));
        WriteU32(send_cursor, b->mood_sound_block_search_extent);

        nbt_write_key(send_cursor, NBT_TAG_DOUBLE, STR("offset"));
        WriteU32(send_cursor, b->mood_sound_offset);

        WriteU8(send_cursor, NBT_TAG_END);
        // mood sound end
    }

    if (b->additions_sound_size > 0) {
        // additions sound
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("additions_sound"));

        nbt_write_key(send_cursor, NBT_TAG_STRING, STR("sound"));
        String additions_sound = {
            .data = b->additions_sound,
            .size = b->additions_sound_size
        };
        nbt_write_string(send_cursor, additions_sound);

        nbt_write_key(send_cursor, NBT_TAG_DOUBLE, STR("tick_chance"));
        WriteU32(send_cursor, b->additions_sound_tick_chance);

        WriteU8(send_cursor, NBT_TAG_END);
        // additions sound end
    }

    if (b->music_sound_size > 0) {
        // music
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("music"));

        nbt_write_key(send_cursor, NBT_TAG_STRING, STR("sound"));
        String music_sound = {
            .data = b->music_sound,
            .size = b->music_sound_size
        };
        nbt_write_string(send_cursor, music_sound);

        nbt_write_key(send_cursor, NBT_TAG_INT, STR("min_delay"));
        WriteU32(send_cursor, b->music_min_delay);

        nbt_write_key(send_cursor, NBT_TAG_INT, STR("max_delay"));
        WriteU32(send_cursor, b->music_max_delay);

        nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("replace_current_music"));
        WriteU8(send_cursor, b->music_replace_current_music);

        WriteU8(send_cursor, NBT_TAG_END);
        // music end
    }

    WriteU8(send_cursor, NBT_TAG_END);
    // special effects end
}

static void NbtWriteDamageTypeWithScaling(Cursor * send_cursor, i32 id, char * name, char * messageId, f32 exhaustion, char * scaling) {
    {
        nbt_write_key(send_cursor, NBT_TAG_STRING, STR("name"));
        nbt_write_string(send_cursor, STR(name));

        nbt_write_key(send_cursor, NBT_TAG_INT, STR("id"));
        WriteU32(send_cursor, id);

        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("element"));
        {
            nbt_write_key(send_cursor, NBT_TAG_FLOAT, STR("exhaustion"));
            WriteF32(send_cursor, exhaustion);

            nbt_write_key(send_cursor, NBT_TAG_STRING, STR("message_id"));
            nbt_write_string(send_cursor, STR(messageId));

            nbt_write_key(send_cursor, NBT_TAG_STRING, STR("scaling"));
            nbt_write_string(send_cursor, STR(scaling));
        }
        WriteU8(send_cursor, NBT_TAG_END);
    }
    WriteU8(send_cursor, NBT_TAG_END);
}

static void NbtWriteDamageType(Cursor * send_cursor, i32 id, char * name, char * messageId, f32 exhaustion) {
    NbtWriteDamageTypeWithScaling(send_cursor, id, name, messageId, exhaustion, "when_caused_by_living_non_player");
}

static void
send_changed_entity_data(Cursor * send_cursor, entity_base * player,
        entity_base * entity, u32 changed_data) {
    if (changed_data == 0) {
        return;
    }

    begin_packet(send_cursor, CBP_SET_ENTITY_DATA);
    WriteVarU32(send_cursor, entity->eid);

    if (changed_data & (1 << ENTITY_DATA_FLAGS)) {
        WriteU8(send_cursor, ENTITY_DATA_FLAGS);
        WriteVarU32(send_cursor, ENTITY_DATA_TYPE_BYTE);
        u8 flags = 0;
        // @TODO(traks) shared flags
        flags |= !!(entity->flags & ENTITY_VISUAL_FIRE) << 0;
        flags |= !!(entity->flags & ENTITY_SHIFTING) << 1;
        flags |= !!(entity->flags & ENTITY_SPRINTING) << 3;
        flags |= !!(entity->flags & ENTITY_SWIMMING) << 4;
        flags |= !!(entity->flags & ENTITY_INVISIBLE) << 5;
        flags |= !!(entity->flags & ENTITY_GLOWING) << 6;
        // flags |= !!(entity->flags & ENTITY_FALL_FLYING) << 7;
        WriteU8(send_cursor, flags);
    }

    if (changed_data & (1 << ENTITY_DATA_AIR_SUPPLY)) {
        WriteU8(send_cursor, ENTITY_DATA_AIR_SUPPLY);
        WriteVarU32(send_cursor, ENTITY_DATA_TYPE_INT);
        WriteVarU32(send_cursor, entity->air_supply);
    }

    // @TODO(traks) custom names
    // WriteU8(send_cursor, ENTITY_BASE_DATA_CUSTOM_NAME);

    if (changed_data & (1 << ENTITY_DATA_CUSTOM_NAME_VISIBLE)) {
        WriteU8(send_cursor, ENTITY_DATA_CUSTOM_NAME_VISIBLE);
        WriteVarU32(send_cursor, ENTITY_DATA_TYPE_BOOL);
        WriteU8(send_cursor, !!(entity->flags & ENTITY_CUSTOM_NAME_VISIBLE));
    }

    if (changed_data & (1 << ENTITY_DATA_SILENT)) {
        WriteU8(send_cursor, ENTITY_DATA_SILENT);
        WriteVarU32(send_cursor, ENTITY_DATA_TYPE_BOOL);
        WriteU8(send_cursor, !!(entity->flags & ENTITY_SILENT));
    }

    if (changed_data & (1 << ENTITY_DATA_NO_GRAVITY)) {
        WriteU8(send_cursor, ENTITY_DATA_NO_GRAVITY);
        WriteVarU32(send_cursor, ENTITY_DATA_TYPE_BOOL);
        WriteU8(send_cursor, !!(entity->flags & ENTITY_NO_GRAVITY));
    }

    if (changed_data & (1 << ENTITY_DATA_POSE)) {
        WriteU8(send_cursor, ENTITY_DATA_POSE);
        WriteVarU32(send_cursor, ENTITY_DATA_TYPE_POSE);
        WriteVarU32(send_cursor, entity->pose);
    }

    switch (entity->type) {
    case ENTITY_PLAYER: {
        // @TODO(traks)
        break;
    }
    case ENTITY_ITEM: {
        if (changed_data & (1 << ENTITY_DATA_ITEM)) {
            WriteU8(send_cursor, ENTITY_DATA_ITEM);
            WriteVarU32(send_cursor, ENTITY_DATA_TYPE_ITEM_STACK);
            WriteU8(send_cursor, 1); // has item
            item_stack * is = &entity->item.contents;
            WriteVarU32(send_cursor, is->type);
            WriteU8(send_cursor, is->size);
            // @TODO(traks) write NBT (currently just a single end tag)
            WriteU8(send_cursor, NBT_TAG_END);
        }
        break;
    }
    }

    WriteU8(send_cursor, 0xff); // end of entity data
    finish_packet(send_cursor, player);
}

static void
send_take_item_entity_packet(entity_base * player, Cursor * send_cursor,
        entity_id taker_id, entity_id pickup_id, u8 pickup_size) {
    begin_packet(send_cursor, CBP_TAKE_ITEM_ENTITY);
    WriteVarU32(send_cursor, pickup_id);
    WriteVarU32(send_cursor, taker_id);
    WriteVarU32(send_cursor, pickup_size);
    finish_packet(send_cursor, player);
}

static void
try_update_tracked_entity(entity_base * player,
        Cursor * send_cursor, MemoryArena * tick_arena,
        tracked_entity * tracked, entity_base * entity) {
    // TODO(traks): There are a bunch of timers in play here. The update
    // interval timer, the TP packet timer, the position packet timer, etc. If
    // these happen to line up at the same tick for a bunch of players (e.g. if
    // a bunch of players join during the same tick), that'll be bad times.
    //
    // We should spread out these timers among multiple ticks based on the
    // player count. This spreading out should be done dynamically. There
    // shouldn't be any fixed interval timers.
    //
    // Ideally the packets for the tracked entities for a single player would
    // also be spread out among multiple ticks. It's better to spread out
    // network bandwidth than to flood it every so often. Flooding will result
    // in temporary network lag, spreading out won't.
    if (serv->current_tick - tracked->last_update_tick < tracked->update_interval
            && entity->changed_data == 0) {
        return;
    }

    tracked->last_update_tick = serv->current_tick;

    double newX = entity->x;
    double newY = entity->y;
    double newZ = entity->z;
    double dx = newX - tracked->last_sent_x;
    double dy = newY - tracked->last_sent_y;
    double dz = newZ - tracked->last_sent_z;
    // @TODO(traks) better epsilons
    double dmax = 32767.0 / 4096.0 - 0.001;
    double dmin = -32768.0 / 4096.0 + 0.001;

    unsigned char encoded_rot_x = (int) (entity->rot_x * 256.0f / 360.0f) & 0xff;
    unsigned char encoded_rot_y = (int) (entity->rot_y * 256.0f / 360.0f) & 0xff;

    if (dx > dmin && dx < dmax
            && dy > dmin && dy < dmax
            && dz > dmin && dz < dmax
            && serv->current_tick - tracked->last_tp_packet_tick < 200) {
        // resend position every once in a while in case of floating point
        // rounding errors and discrepancies between our view of the position
        // and the encoded position
        int sent_pos = (dx * dx + dy * dy + dz * dz > 0)
                || (serv->current_tick - tracked->last_send_pos_tick >= 100);
        int sent_rot = (encoded_rot_x - tracked->last_sent_rot_x) != 0
                || (encoded_rot_y - tracked->last_sent_rot_y) != 0;
        // TODO(traks): ensure no overflow
        i64 encodedLastX = round(tracked->last_sent_x * 4096.0);
        i64 encodedLastY = round(tracked->last_sent_y * 4096.0);
        i64 encodedLastZ = round(tracked->last_sent_z * 4096.0);
        i64 encodedNewX = round(newX * 4096.0);
        i64 encodedNewY = round(newY * 4096.0);
        i64 encodedNewZ = round(newZ * 4096.0);
        i16 encoded_dx = encodedNewX - encodedLastX;
        i16 encoded_dy = encodedNewY - encodedLastY;
        i16 encoded_dz = encodedNewZ - encodedLastZ;

        if (sent_pos && sent_rot) {
            begin_packet(send_cursor, CBP_MOVE_ENTITY_POS_ROT);
            WriteVarU32(send_cursor, entity->eid);
            WriteU16(send_cursor, encoded_dx);
            WriteU16(send_cursor, encoded_dy);
            WriteU16(send_cursor, encoded_dz);
            WriteU8(send_cursor, encoded_rot_y);
            WriteU8(send_cursor, encoded_rot_x);
            WriteU8(send_cursor, !!(player->flags & ENTITY_ON_GROUND));
            finish_packet(send_cursor, player);
        } else if (sent_pos) {
            begin_packet(send_cursor, CBP_MOVE_ENTITY_POS);
            WriteVarU32(send_cursor, entity->eid);
            WriteU16(send_cursor, encoded_dx);
            WriteU16(send_cursor, encoded_dy);
            WriteU16(send_cursor, encoded_dz);
            WriteU8(send_cursor, !!(player->flags & ENTITY_ON_GROUND));
            finish_packet(send_cursor, player);
        } else if (sent_rot) {
            begin_packet(send_cursor, CBP_MOVE_ENTITY_ROT);
            WriteVarU32(send_cursor, entity->eid);
            WriteU8(send_cursor, encoded_rot_y);
            WriteU8(send_cursor, encoded_rot_x);
            WriteU8(send_cursor, !!(player->flags & ENTITY_ON_GROUND));
            finish_packet(send_cursor, player);
        }

        if (sent_pos) {
            // @NOTE(traks) this is the way the Minecraft client calculates the
            // new position. We emulate this to reduce accumulated precision
            // loss across many move packets.
            tracked->last_sent_x = encoded_dx == 0 ? tracked->last_sent_x : (encodedLastX + encoded_dx) / 4096.0;
            tracked->last_sent_y = encoded_dy == 0 ? tracked->last_sent_y : (encodedLastY + encoded_dy) / 4096.0;
            tracked->last_sent_z = encoded_dz == 0 ? tracked->last_sent_z : (encodedLastZ + encoded_dz) / 4096.0;

            tracked->last_send_pos_tick = serv->current_tick;
        }
        if (sent_rot) {
            tracked->last_sent_rot_x = encoded_rot_x;
            tracked->last_sent_rot_y = encoded_rot_y;
        }
    } else {
        begin_packet(send_cursor, CBP_TELEPORT_ENTITY);
        WriteVarU32(send_cursor, entity->eid);
        WriteF64(send_cursor, entity->x);
        WriteF64(send_cursor, entity->y);
        WriteF64(send_cursor, entity->z);
        WriteU8(send_cursor, encoded_rot_y);
        WriteU8(send_cursor, encoded_rot_x);
        WriteU8(send_cursor, !!(player->flags & ENTITY_ON_GROUND));
        finish_packet(send_cursor, player);

        tracked->last_tp_packet_tick = serv->current_tick;

        tracked->last_sent_x = entity->x;
        tracked->last_sent_y = entity->y;
        tracked->last_sent_z = entity->z;
        tracked->last_sent_rot_x = encoded_rot_x;
        tracked->last_sent_rot_y = encoded_rot_y;
        tracked->last_send_pos_tick = serv->current_tick;
    }

    if (entity->type != ENTITY_PLAYER) {
        begin_packet(send_cursor, CBP_SET_ENTITY_MOTION);
        WriteVarU32(send_cursor, entity->eid);
        WriteU16(send_cursor, CLAMP(entity->vx, -3.9, 3.9) * 8000);
        WriteU16(send_cursor, CLAMP(entity->vy, -3.9, 3.9) * 8000);
        WriteU16(send_cursor, CLAMP(entity->vz, -3.9, 3.9) * 8000);
        finish_packet(send_cursor, player);
    }

    switch (entity->type) {
    case ENTITY_PLAYER: {
        if (encoded_rot_y != tracked->last_sent_head_rot_y) {
            tracked->last_sent_head_rot_y = encoded_rot_y;

            begin_packet(send_cursor, CBP_ROTATE_HEAD);
            WriteVarU32(send_cursor, entity->eid);
            WriteU8(send_cursor, encoded_rot_y);
            finish_packet(send_cursor, player);
        }

        if (entity->player.picked_up_tick == serv->current_tick) {
            send_take_item_entity_packet(player, send_cursor,
                    entity->eid, entity->player.picked_up_item_id,
                    entity->player.picked_up_item_size);
        }
        break;
    }
    }

    send_changed_entity_data(send_cursor, player, entity, entity->changed_data);
}

static void
start_tracking_entity(entity_base * player,
        Cursor * send_cursor, MemoryArena * tick_arena,
        tracked_entity * tracked, entity_base * entity) {
    *tracked = (tracked_entity) {0};
    tracked->eid = entity->eid;

    tracked->last_sent_x = entity->x;
    tracked->last_sent_y = entity->y;
    tracked->last_sent_z = entity->z;
    unsigned char encoded_rot_x = (int) (entity->rot_x * 256.0f / 360.0f) & 0xff;
    unsigned char encoded_rot_y = (int) (entity->rot_y * 256.0f / 360.0f) & 0xff;
    tracked->last_sent_rot_x = encoded_rot_x;
    tracked->last_sent_rot_y = encoded_rot_y;

    tracked->last_tp_packet_tick = serv->current_tick;
    tracked->last_send_pos_tick = serv->current_tick;
    tracked->last_update_tick = serv->current_tick;

    switch (entity->type) {
    case ENTITY_PLAYER: {
        tracked->update_interval = 2;

        begin_packet(send_cursor, CBP_ADD_PLAYER);
        WriteVarU32(send_cursor, entity->eid);
        // @TODO(traks) appropriate UUID
        WriteU64(send_cursor, 0);
        WriteU64(send_cursor, entity->eid);
        WriteF64(send_cursor, entity->x);
        WriteF64(send_cursor, entity->y);
        WriteF64(send_cursor, entity->z);
        WriteU8(send_cursor, encoded_rot_y);
        WriteU8(send_cursor, encoded_rot_x);
        finish_packet(send_cursor, player);

        tracked->last_sent_head_rot_y = encoded_rot_y;

        begin_packet(send_cursor, CBP_ROTATE_HEAD);
        WriteVarU32(send_cursor, entity->eid);
        WriteU8(send_cursor, encoded_rot_y);
        finish_packet(send_cursor, player);
        break;
    }
    case ENTITY_ITEM: {
        tracked->update_interval = 20;

        // begin_packet(send_cursor, CBP_ADD_MOB);
        // WriteVarU32(send_cursor, entity->eid);
        // // @TODO(traks) appropriate UUID
        // WriteU64(send_cursor, 0);
        // WriteU64(send_cursor, 0);
        // WriteVarU32(send_cursor, ENTITY_SQUID);
        // WriteF64(send_cursor, entity->x);
        // WriteF64(send_cursor, entity->y);
        // WriteF64(send_cursor, entity->z);
        // WriteU8(send_cursor, 0);
        // WriteU8(send_cursor, 0);
        // // @TODO(traks) y head rotation (what is that?)
        // WriteU8(send_cursor, 0);
        // // @TODO(traks) entity velocity
        // WriteU16(send_cursor, 0);
        // WriteU16(send_cursor, 0);
        // WriteU16(send_cursor, 0);
        // finish_packet(send_cursor, player);
        begin_packet(send_cursor, CBP_ADD_ENTITY);
        WriteVarU32(send_cursor, entity->eid);
        // @TODO(traks) appropriate UUID
        WriteU64(send_cursor, 0);
        WriteU64(send_cursor, entity->eid);
        WriteVarU32(send_cursor, entity->type);
        WriteF64(send_cursor, entity->x);
        WriteF64(send_cursor, entity->y);
        WriteF64(send_cursor, entity->z);
        // rotation of items is ignored
        WriteU8(send_cursor, 0); // x rot
        WriteU8(send_cursor, 0); // y rot
        WriteU8(send_cursor, 0); // y head rot
        // this kind of entity data not used for items
        WriteVarU32(send_cursor, 0);
        // @NOTE(traks) for some reason Minecraft ignores the initial velocity
        // and initialises it to random values. To solve this, we send the
        // velocity in a separate packet below.
        WriteU16(send_cursor, 0);
        WriteU16(send_cursor, 0);
        WriteU16(send_cursor, 0);
        finish_packet(send_cursor, player);

        begin_packet(send_cursor, CBP_SET_ENTITY_MOTION);
        WriteVarU32(send_cursor, entity->eid);
        WriteU16(send_cursor, CLAMP(entity->vx, -3.9, 3.9) * 8000);
        WriteU16(send_cursor, CLAMP(entity->vy, -3.9, 3.9) * 8000);
        WriteU16(send_cursor, CLAMP(entity->vz, -3.9, 3.9) * 8000);
        finish_packet(send_cursor, player);
        break;
    }
    }

    send_changed_entity_data(send_cursor, player, entity, 0xffffffff);
}

static void
send_player_abilities(Cursor * send_cursor, entity_base * player) {
    begin_packet(send_cursor, CBP_PLAYER_ABILITIES);
    u8 ability_flags = 0;

    if (player->flags & ENTITY_INVULNERABLE) {
        ability_flags |= 0x1;
    }
    if (player->flags & PLAYER_FLYING) {
        ability_flags |= 0x2;
    }
    if (player->flags & PLAYER_CAN_FLY) {
        ability_flags |= 0x4;
    }
    if (player->flags & PLAYER_INSTABUILD) {
        ability_flags |= 0x8;
    }

    WriteU8(send_cursor, ability_flags);
    WriteF32(send_cursor, 0.05); // flying speed
    WriteF32(send_cursor, 0.1); // walking speed
    finish_packet(send_cursor, player);
}

static void UpdateChunkCache(entity_base * player, Cursor * sendCursor) {
    i32 chunkCacheMinX = player->player.chunkCacheCentreX - player->player.chunkCacheRadius;
    i32 chunkCacheMinZ = player->player.chunkCacheCentreZ - player->player.chunkCacheRadius;
    i32 chunkCacheMaxX = player->player.chunkCacheCentreX + player->player.chunkCacheRadius;
    i32 chunkCacheMaxZ = player->player.chunkCacheCentreZ + player->player.chunkCacheRadius;

    i32 nextChunkCacheCentreX = (i32) floor(player->x) >> 4;
    i32 nextChunkCacheCentreZ = (i32) floor(player->z) >> 4;
    assert(player->player.nextChunkCacheRadius <= MAX_CHUNK_CACHE_RADIUS);
    i32 nextChunkCacheMinX = nextChunkCacheCentreX - player->player.nextChunkCacheRadius;
    i32 nextChunkCacheMinZ = nextChunkCacheCentreZ - player->player.nextChunkCacheRadius;
    i32 nextChunkCacheMaxX = nextChunkCacheCentreX + player->player.nextChunkCacheRadius;
    i32 nextChunkCacheMaxZ = nextChunkCacheCentreZ + player->player.nextChunkCacheRadius;

    if (player->player.chunkCacheCentreX != nextChunkCacheCentreX
            || player->player.chunkCacheCentreZ != nextChunkCacheCentreZ) {
        begin_packet(sendCursor, CBP_SET_CHUNK_CACHE_CENTRE);
        WriteVarU32(sendCursor, nextChunkCacheCentreX);
        WriteVarU32(sendCursor, nextChunkCacheCentreZ);
        finish_packet(sendCursor, player);
    }

    if (player->player.chunkCacheRadius != player->player.nextChunkCacheRadius) {
        // TODO(traks): also send set simulation distance packet?
        begin_packet(sendCursor, CBP_SET_CHUNK_CACHE_RADIUS);
        WriteVarU32(sendCursor, player->player.nextChunkCacheRadius);
        finish_packet(sendCursor, player);
    }

    // NOTE(traks): untrack old chunks
    for (i32 x = chunkCacheMinX; x <= chunkCacheMaxX; x++) {
        for (i32 z = chunkCacheMinZ; z <= chunkCacheMaxZ; z++) {
            WorldChunkPos pos = {.worldId = player->worldId, .x = x, .z = z};
            i32 index = chunk_cache_index(pos.xz);
            PlayerChunkCacheEntry * cacheEntry = player->player.chunkCache + index;

            if (x >= nextChunkCacheMinX && x <= nextChunkCacheMaxX
                    && z >= nextChunkCacheMinZ && z <= nextChunkCacheMaxZ) {
                // NOTE(traks): old chunk still in new region
                continue;
            }

            if (cacheEntry->flags & PLAYER_CHUNK_ADDED_INTEREST) {
                AddChunkInterest(pos, -1);
            }

            if (cacheEntry->flags & PLAYER_CHUNK_SENT) {
                begin_packet(sendCursor, CBP_FORGET_LEVEL_CHUNK);
                WriteU32(sendCursor, x);
                WriteU32(sendCursor, z);
                finish_packet(sendCursor, player);
            }

            *cacheEntry = (PlayerChunkCacheEntry) {0};
        }
    }

    player->player.chunkCacheRadius = player->player.nextChunkCacheRadius;
    player->player.chunkCacheCentreX = nextChunkCacheCentreX;
    player->player.chunkCacheCentreZ = nextChunkCacheCentreZ;
}

static void SendTrackedBlockChanges(entity_base * player, Cursor * sendCursor, MemoryArena * tickArena) {
    i32 chunkCacheDiam = 2 * player->player.chunkCacheRadius + 1;
    i32 chunkCacheMinX = player->player.chunkCacheCentreX - player->player.chunkCacheRadius;
    i32 chunkCacheMaxX = player->player.chunkCacheCentreX + player->player.chunkCacheRadius;
    i32 chunkCacheMinZ = player->player.chunkCacheCentreZ - player->player.chunkCacheRadius;
    i32 chunkCacheMaxZ = player->player.chunkCacheCentreZ + player->player.chunkCacheRadius;
    Chunk * * changedChunks = CallocInArena(tickArena, MAX_CHUNK_CACHE_DIAM * MAX_CHUNK_CACHE_DIAM * sizeof (Chunk *));
    BeginTimings(CollectLoadedChunks);
    i32 changedChunkCount = CollectChangedChunks(
            (WorldChunkPos) {.worldId = player->worldId, .x = chunkCacheMinX, .z = chunkCacheMinZ},
            (WorldChunkPos) {.worldId = player->worldId, .x = chunkCacheMaxX, .z = chunkCacheMaxZ},
            changedChunks);
    EndTimings(CollectLoadedChunks);

    for (i32 chunkIndex = 0; chunkIndex < changedChunkCount; chunkIndex++) {
        Chunk * chunk = changedChunks[chunkIndex];
        WorldChunkPos pos = chunk->pos;
        i32 index = chunk_cache_index(pos.xz);
        PlayerChunkCacheEntry * cacheEntry = player->player.chunkCache + index;

        if (!(cacheEntry->flags & PLAYER_CHUNK_SENT)) {
            continue;
        }

        Chunk * ch = changedChunks[chunkIndex];
        assert(ch != NULL);

        for (i32 sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
            i32 sectionY = sectionIndex + MIN_SECTION;
            ChunkSection * section = ch->sections + sectionIndex;
            if (!(chunk->changedBlockSections & ((u32) 1 << sectionIndex))) {
                continue;
            }

            assert(section->changedBlockCount > 0);

            // @TODO(traks) in case of tons of block changes in all
            // sections combined, we shouldn't spam clients with section
            // change packets. We should limit the maximum number of
            // packet data for this packet type and if the limit is
            // exceeded, reload chunks for clients.

            begin_packet(sendCursor, CBP_SECTION_BLOCKS_UPDATE);
            u64 section_pos = ((u64) (pos.x & 0x3fffff) << 42)
                    | ((u64) (pos.z & 0x3fffff) << 20)
                    | (u64) (sectionY & 0xfffff);
            WriteU64(sendCursor, section_pos);
            // @TODO(traks) appropriate value for this
            WriteU8(sendCursor, 1); // suppress light updates
            WriteVarU32(sendCursor, section->changedBlockCount);

            for (i32 i = 0; i < section->changedBlockSetMask + 1; i++) {
                if (section->changedBlockSet[i] != 0) {
                    BlockPos posInSection = SectionIndexToPos(section->changedBlockSet[i] & 0xfff);
                    i64 block_state = ChunkGetBlockState(ch, (BlockPos) {posInSection.x, posInSection.y + sectionY * 16, posInSection.z});
                    i64 encoded = (block_state << 12) | (posInSection.x << 8) | (posInSection.z << 4) | (posInSection.y & 0xf);
                    WriteVarU64(sendCursor, encoded);
                }
            }

            finish_packet(sendCursor, player);
        }

        if (ch->lastLocalEventTick == serv->current_tick) {
            for (i32 i = 0; i < ch->localEventCount; i++) {
                level_event * event = ch->localEvents + i;

                begin_packet(sendCursor, CBP_LEVEL_EVENT);
                WriteU32(sendCursor, event->type);
                WriteBlockPos(sendCursor, event->pos);
                WriteU32(sendCursor, event->data);
                WriteU8(sendCursor, 0); // is global event
                finish_packet(sendCursor, player);
            }
        }
    }
}

// @TODO(traks) I wonder if this function should be sending packets to all
// players at once instead of to only a single player. That would allow us to
// cache compressed chunk packets, copy packets that get sent to all players,
// use the CPU cache better, etc.
void
send_packets_to_player(entity_base * player, MemoryArena * tick_arena) {
    BeginTimings(SendPackets);

    size_t max_uncompressed_packet_size = 1 << 20;
    Cursor send_cursor_ = {
        .data = MallocInArena(tick_arena, max_uncompressed_packet_size),
        .size = max_uncompressed_packet_size
    };
    Cursor * send_cursor = &send_cursor_;

    if (!(player->flags & PLAYER_DID_INIT_PACKETS)) {
        player->flags |= PLAYER_DID_INIT_PACKETS;

        if (PACKET_COMPRESSION_ENABLED) {
            // send login compression packet
            begin_packet(send_cursor, 3);
            WriteVarU32(send_cursor, 0);
            finish_packet(send_cursor, player);

            player->flags |= PLAYER_PACKET_COMPRESSION;
        }

        // send game profile packet
        begin_packet(send_cursor, 2);
        // @TODO(traks) send UUID
        WriteU64(send_cursor, 0);
        WriteU64(send_cursor, player->eid);
        String username = {
            .size = player->player.username_size,
            .data = player->player.username
        };
        WriteVarString(send_cursor, username);
        WriteVarU32(send_cursor, 0); // no properties for now
        finish_packet(send_cursor, player);

        String level_name = STR("blaze:main");

        begin_packet(send_cursor, CBP_LOGIN);
        WriteU32(send_cursor, player->eid);
        WriteU8(send_cursor, 0); // hardcore
        WriteU8(send_cursor, player->player.gamemode); // current gamemode
        WriteU8(send_cursor, 255); // previous gamemode, -1 = none

        // all levels/worlds currently available on the server
        // @NOTE(traks) This list is used for tab completions
        WriteVarU32(send_cursor, 1); // number of levels
        WriteVarString(send_cursor, level_name);

        // send all synchronised registries
        Cursor nbtCursor = *send_cursor;
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR(""));

        // write dimension types
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("minecraft:dimension_type"));
        {
            nbt_write_key(send_cursor, NBT_TAG_STRING, STR("type"));
            nbt_write_string(send_cursor, STR("minecraft:dimension_type"));

            nbt_write_key(send_cursor, NBT_TAG_LIST, STR("value"));
            WriteU8(send_cursor, NBT_TAG_COMPOUND);
            WriteU32(send_cursor, serv->dimension_type_count);
            for (int i = 0; i < serv->dimension_type_count; i++) {
                dimension_type * dim_type = serv->dimension_types + i;

                nbt_write_key(send_cursor, NBT_TAG_STRING, STR("name"));
                String name = {
                    .data = dim_type->name,
                    .size = dim_type->name_size
                };
                nbt_write_string(send_cursor, name);

                nbt_write_key(send_cursor, NBT_TAG_INT, STR("id"));
                WriteU32(send_cursor, i);

                nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("element"));
                {
                    nbt_write_dimension_type(send_cursor, dim_type);
                }
                WriteU8(send_cursor, NBT_TAG_END);

                WriteU8(send_cursor, NBT_TAG_END);
            }
        }
        WriteU8(send_cursor, NBT_TAG_END);

        // write biomes
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("minecraft:worldgen/biome"));
        {
            nbt_write_key(send_cursor, NBT_TAG_STRING, STR("type"));
            nbt_write_string(send_cursor, STR("minecraft:worldgen/biome"));

            nbt_write_key(send_cursor, NBT_TAG_LIST, STR("value"));
            WriteU8(send_cursor, NBT_TAG_COMPOUND);
            WriteU32(send_cursor, serv->biome_count);
            for (int i = 0; i < serv->biome_count; i++) {
                biome * b = serv->biomes + i;

                nbt_write_key(send_cursor, NBT_TAG_STRING, STR("name"));
                String name = {
                    .data = b->name,
                    .size = b->name_size
                };
                nbt_write_string(send_cursor, name);

                nbt_write_key(send_cursor, NBT_TAG_INT, STR("id"));
                WriteU32(send_cursor, i);

                nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("element"));
                {
                    nbt_write_biome(send_cursor, b);
                }
                WriteU8(send_cursor, NBT_TAG_END);

                WriteU8(send_cursor, NBT_TAG_END);
            }
        }
        WriteU8(send_cursor, NBT_TAG_END);

        // write chat types
        // TODO(traks): make this whole chat type business neater...
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("minecraft:chat_type"));
        {
            nbt_write_key(send_cursor, NBT_TAG_STRING, STR("type"));
            nbt_write_string(send_cursor, STR("minecraft:chat_type"));

            nbt_write_key(send_cursor, NBT_TAG_LIST, STR("value"));
            WriteU8(send_cursor, NBT_TAG_COMPOUND);
            WriteU32(send_cursor, 1);

            // chat
            {
                nbt_write_key(send_cursor, NBT_TAG_STRING, STR("name"));
                nbt_write_string(send_cursor, STR("minecraft:chat"));
                nbt_write_key(send_cursor, NBT_TAG_INT, STR("id"));
                WriteU32(send_cursor, 0);
                nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("element"));
                {
                    nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("chat"));
                    {
                        nbt_write_key(send_cursor, NBT_TAG_STRING, STR("translation_key"));
                        nbt_write_string(send_cursor, STR("chat.type.text"));

                        nbt_write_key(send_cursor, NBT_TAG_LIST, STR("parameters"));
                        WriteU8(send_cursor, NBT_TAG_STRING);
                        WriteU32(send_cursor, 2);
                        nbt_write_string(send_cursor, STR("sender"));
                        nbt_write_string(send_cursor, STR("content"));
                    }
                    WriteU8(send_cursor, NBT_TAG_END);

                    nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("narration"));
                    {
                        nbt_write_key(send_cursor, NBT_TAG_STRING, STR("translation_key"));
                        nbt_write_string(send_cursor, STR("chat.type.text.narrate"));

                        nbt_write_key(send_cursor, NBT_TAG_LIST, STR("parameters"));
                        WriteU8(send_cursor, NBT_TAG_STRING);
                        WriteU32(send_cursor, 2);
                        nbt_write_string(send_cursor, STR("sender"));
                        nbt_write_string(send_cursor, STR("content"));
                    }
                    WriteU8(send_cursor, NBT_TAG_END);
                }
                WriteU8(send_cursor, NBT_TAG_END);
            }
            WriteU8(send_cursor, NBT_TAG_END);
        }
        WriteU8(send_cursor, NBT_TAG_END);

        // write trim materials
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("minecraft:trim_material"));
        {
            nbt_write_key(send_cursor, NBT_TAG_STRING, STR("type"));
            nbt_write_string(send_cursor, STR("minecraft:trim_material"));

            nbt_write_key(send_cursor, NBT_TAG_LIST, STR("value"));
            WriteU8(send_cursor, NBT_TAG_COMPOUND);
            WriteU32(send_cursor, 0);
        }
        WriteU8(send_cursor, NBT_TAG_END);

        // write trim patterns
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("minecraft:trim_pattern"));
        {
            nbt_write_key(send_cursor, NBT_TAG_STRING, STR("type"));
            nbt_write_string(send_cursor, STR("minecraft:trim_pattern"));

            nbt_write_key(send_cursor, NBT_TAG_LIST, STR("value"));
            WriteU8(send_cursor, NBT_TAG_COMPOUND);
            WriteU32(send_cursor, 0);
        }
        WriteU8(send_cursor, NBT_TAG_END);

        // write damage types
        // NOTE(traks): Currently (1.19.4), we must send ALL vanilla damage
        // types to the client. Otherwise it'll throw internally and it'll be
        // stuck on the login screen. The client doesn't disconnect, and no
        // error message pops up on the screen.
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("minecraft:damage_type"));
        {
            nbt_write_key(send_cursor, NBT_TAG_STRING, STR("type"));
            nbt_write_string(send_cursor, STR("minecraft:damage_type"));

            nbt_write_key(send_cursor, NBT_TAG_LIST, STR("value"));
            WriteU8(send_cursor, NBT_TAG_COMPOUND);
            WriteU32(send_cursor, 42);

            // TODO(traks): these IDs don't seem to be the same as vanilla. Does
            // that matter? I wouldn't think so
            NbtWriteDamageType(send_cursor, 0, "minecraft:in_fire", "inFire", 0.1f);
            NbtWriteDamageType(send_cursor, 1, "minecraft:lightning_bolt", "lightningBolt", 0.1f);
            NbtWriteDamageType(send_cursor, 2, "minecraft:on_fire", "onFire", 0.0f);
            NbtWriteDamageType(send_cursor, 3, "minecraft:lava", "lava", 0.1f);
            NbtWriteDamageType(send_cursor, 4, "minecraft:hot_floor", "hotFloor", 0.1f);
            NbtWriteDamageType(send_cursor, 5, "minecraft:in_wall", "inWall", 0.0f);
            NbtWriteDamageType(send_cursor, 6, "minecraft:cramming", "cramming", 0.0f);
            NbtWriteDamageType(send_cursor, 7, "minecraft:drown", "drown", 0.0f);
            NbtWriteDamageType(send_cursor, 8, "minecraft:starve", "starve", 0.0f);
            NbtWriteDamageType(send_cursor, 9, "minecraft:cactus", "cactus", 0.1f);
            NbtWriteDamageType(send_cursor, 10, "minecraft:fall", "fall", 0.0f);
            NbtWriteDamageType(send_cursor, 11, "minecraft:fly_into_wall", "flyIntoWall", 0.0f);
            NbtWriteDamageType(send_cursor, 12, "minecraft:out_of_world", "outOfWorld", 0.0f);
            NbtWriteDamageType(send_cursor, 13, "minecraft:generic", "generic", 0.0f);
            NbtWriteDamageType(send_cursor, 14, "minecraft:magic", "magic", 0.0f);
            NbtWriteDamageType(send_cursor, 15, "minecraft:wither", "wither", 0.0f);
            NbtWriteDamageType(send_cursor, 16, "minecraft:dragon_breath", "dragonBreath", 0.0f);
            NbtWriteDamageType(send_cursor, 17, "minecraft:dry_out", "dryout", 0.1f);
            NbtWriteDamageType(send_cursor, 18, "minecraft:sweet_berry_bush", "sweetBerryBush", 0.1f);
            NbtWriteDamageType(send_cursor, 19, "minecraft:freeze", "freeze", 0.0f);
            NbtWriteDamageType(send_cursor, 20, "minecraft:stalagmite", "stalagmite", 0.0f);
            NbtWriteDamageType(send_cursor, 21, "minecraft:falling_block", "fallingBlock", 0.1f);
            NbtWriteDamageType(send_cursor, 22, "minecraft:falling_anvil", "anvil", 0.1f);
            NbtWriteDamageType(send_cursor, 23, "minecraft:falling_stalactite", "fallingStalactite", 0.1f);
            NbtWriteDamageType(send_cursor, 24, "minecraft:sting", "sting", 0.1f);
            NbtWriteDamageType(send_cursor, 25, "minecraft:mob_attack", "mob", 0.1f);
            NbtWriteDamageType(send_cursor, 26, "minecraft:mob_attack_no_aggro", "mob", 0.1f);
            NbtWriteDamageType(send_cursor, 27, "minecraft:player_attack", "player", 0.1f);
            NbtWriteDamageType(send_cursor, 28, "minecraft:arrow", "arrow", 0.1f);
            NbtWriteDamageType(send_cursor, 29, "minecraft:trident", "trident", 0.1f);
            NbtWriteDamageType(send_cursor, 30, "minecraft:mob_projectile", "mob", 0.1f);
            NbtWriteDamageType(send_cursor, 31, "minecraft:fireworks", "fireworks", 0.1f);
            NbtWriteDamageType(send_cursor, 32, "minecraft:fireball", "onFire", 0.1f);
            NbtWriteDamageType(send_cursor, 33, "minecraft:unattributed_fireball", "fireball", 0.1f);
            NbtWriteDamageType(send_cursor, 34, "minecraft:wither_skull", "witherSkull", 0.1f);
            NbtWriteDamageType(send_cursor, 35, "minecraft:thrown", "thrown", 0.1f);
            NbtWriteDamageType(send_cursor, 36, "minecraft:indirect_magic", "indirectMagic", 0.0f);
            NbtWriteDamageType(send_cursor, 37, "minecraft:thorns", "thorns", 0.1f);
            NbtWriteDamageTypeWithScaling(send_cursor, 38, "minecraft:explosion", "explosion", 0.1f, "always");
            NbtWriteDamageTypeWithScaling(send_cursor, 39, "minecraft:player_explosion", "explosion.player", 0.1f, "always");
            NbtWriteDamageTypeWithScaling(send_cursor, 40, "minecraft:sonic_boom", "sonic_boom", 0.0f, "always");
            NbtWriteDamageTypeWithScaling(send_cursor, 41, "minecraft:bad_respawn_point", "badRespawnPoint", 0.1f, "always");
        }
        WriteU8(send_cursor, NBT_TAG_END);

        WriteU8(send_cursor, NBT_TAG_END); // end of registries

        nbtCursor.size = send_cursor->size;
        // NOTE(traks): for debugging purposes
        // NbtCompound nbtCompound = NbtRead(&nbtCursor, tick_arena);
        // NbtPrint(&nbtCompound);

        // dimension type of level player is joining
        String dimTypeName = {
            .data = serv->dimension_types[0].name,
            .size = serv->dimension_types[0].name_size
        };
        WriteVarString(send_cursor, dimTypeName);

        // level name the player is joining
        WriteVarString(send_cursor, level_name);

        WriteU64(send_cursor, 0); // hashed seed
        WriteVarU32(send_cursor, 0); // max players (ignored by client)
        WriteVarU32(send_cursor, player->player.nextChunkCacheRadius - 1); // chunk radius
        // @TODO(traks) figure out why the client needs to know the simulation
        // distance and what it uses it for
        WriteVarU32(send_cursor, player->player.nextChunkCacheRadius - 1); // simulation distance
        WriteU8(send_cursor, 0); // reduced debug info
        WriteU8(send_cursor, 1); // show death screen on death
        WriteU8(send_cursor, 0); // is debug
        WriteU8(send_cursor, 0); // is flat
        WriteU8(send_cursor, 0); // has death location, world + block pos after if true
        finish_packet(send_cursor, player);

        begin_packet(send_cursor, CBP_UPDATE_ENABLE_FEATURES);
        WriteVarU32(send_cursor, 1);
        WriteVarString(send_cursor, STR("minecraft:vanilla"));
        finish_packet(send_cursor, player);

        begin_packet(send_cursor, CBP_SET_CARRIED_ITEM);
        WriteU8(send_cursor,
                player->player.selected_slot - PLAYER_FIRST_HOTBAR_SLOT);
        finish_packet(send_cursor, player);

        begin_packet(send_cursor, CBP_UPDATE_TAGS);

        tag_list * tag_lists[] = {
            &serv->block_tags,
            &serv->item_tags,
            &serv->fluid_tags,
            &serv->entity_tags,
            &serv->game_event_tags,
        };

        WriteVarU32(send_cursor, ARRAY_SIZE(tag_lists));
        for (int tagsi = 0; tagsi < ARRAY_SIZE(tag_lists); tagsi++) {
            tag_list * tags = tag_lists[tagsi];

            String name = {
                .size = tags->name_size,
                .data = tags->name
            };

            WriteVarString(send_cursor, name);
            WriteVarU32(send_cursor, tags->size);

            for (int i = 0; i < tags->size; i++) {
                tag_spec * tag = tags->tags + i;
                unsigned char * name_size = serv->tag_name_buf + tag->name_index;
                String tag_name = {
                    .size = *name_size,
                    .data = name_size + 1
                };

                WriteVarString(send_cursor, tag_name);
                WriteVarU32(send_cursor, tag->value_count);

                for (int vali = 0; vali < tag->value_count; vali++) {
                    i32 val = serv->tag_value_id_buf[tag->values_index + vali];
                    WriteVarU32(send_cursor, val);
                }
            }
        }
        finish_packet(send_cursor, player);

        begin_packet(send_cursor, CBP_CUSTOM_PAYLOAD);
        String brand_str = STR("minecraft:brand");
        String brand = STR("Blaze");
        WriteVarString(send_cursor, brand_str);
        WriteVarString(send_cursor, brand);
        finish_packet(send_cursor, player);

        begin_packet(send_cursor, CBP_CHANGE_DIFFICULTY);
        WriteU8(send_cursor, 2); // difficulty normal
        WriteU8(send_cursor, 0); // locked
        finish_packet(send_cursor, player);

        send_player_abilities(send_cursor, player);

        send_changed_entity_data(send_cursor, player, player, player->changed_data);

        begin_packet(send_cursor, CBP_SERVER_DATA);
        String prefix = STR("{\"text\":\"");
        String suffix = STR("\"}");
        // TODO(traks): not sure what this version of the MOTD is used for. But
        // should be the same as server list MOTD probably
        WriteVarString(send_cursor, STR("{\"text\":\"Running Blaze\"}"));
        WriteU8(send_cursor, 0); // no icon data
        // NOTE(traks): Not sure what this is all used for. In case all chat
        // messages are sent as system chat, setting this to true disables the
        // popup that warns you about secure chat when joining the server.
        u8 enforcesSecureChat = 1;
        WriteU8(send_cursor, enforcesSecureChat);
        finish_packet(send_cursor, player);

        // NOTE(traks): sync the player location, and then send the spawn
        // location packet, which tells the client to close the
        // "Loading terrain..." screen as soon as it can render the chunk the
        // player is in

        // TODO(traks): Only do this after we have sent the player a couple of
        // chunks around the spawn chunk, so they can move around immediately
        // after the screen closes?

        // NOTE(traks): ensure player is teleporting
        player->flags |= ENTITY_TELEPORTING;
        SendPlayerTeleport(player, send_cursor);

        begin_packet(send_cursor, CBP_SET_DEFAULT_SPAWN_POSITION);
        // TODO(traks): specific value not important for now
        WriteBlockPos(send_cursor, (BlockPos) {0, 0, 0});
        WriteF32(send_cursor, 0); // yaw
        finish_packet(send_cursor, player);

        // reset changed data, because all data is sent already and we don't
        // want to send the same data twice
        player->changed_data = 0;

        player->player.lastAckedBlockChange = -1;
    }

    // send keep alive packet every so often
    if (serv->current_tick - player->player.last_keep_alive_sent_tick >= KEEP_ALIVE_SPACING
            && (player->flags & PLAYER_GOT_ALIVE_RESPONSE)) {
        begin_packet(send_cursor, CBP_KEEP_ALIVE);
        WriteU64(send_cursor, serv->current_tick);
        finish_packet(send_cursor, player);

        player->player.last_keep_alive_sent_tick = serv->current_tick;
        player->flags &= ~PLAYER_GOT_ALIVE_RESPONSE;
    }

    if ((player->flags & ENTITY_TELEPORTING)
            && !(player->flags & PLAYER_SENT_TELEPORT)) {
        LogInfo("Sending teleport to player");
        SendPlayerTeleport(player, send_cursor);
    }

    if (player->changed_data & PLAYER_GAMEMODE_CHANGED) {
        begin_packet(send_cursor, CBP_GAME_EVENT);
        WriteU8(send_cursor, PACKET_GAME_EVENT_CHANGE_GAMEMODE);
        WriteF32(send_cursor, player->player.gamemode);
        finish_packet(send_cursor, player);
    }

    if (player->changed_data & PLAYER_ABILITIES_CHANGED) {
        send_player_abilities(send_cursor, player);
    }

    send_changed_entity_data(send_cursor, player, player, player->changed_data);

    if (player->player.picked_up_tick == serv->current_tick) {
        send_take_item_entity_packet(player, send_cursor,
                player->eid, player->player.picked_up_item_id,
                player->player.picked_up_item_size);
    }

    // NOTE(traks): send block change ack
    if (player->player.lastAckedBlockChange >= 0) {
        begin_packet(send_cursor, CBP_BLOCK_CHANGED_ACK);
        WriteVarU32(send_cursor, player->player.lastAckedBlockChange);
        finish_packet(send_cursor, player);
        player->player.lastAckedBlockChange = -1;
    }

    // send block changes for this player only
    for (int i = 0; i < player->player.changed_block_count; i++) {
        WorldBlockPos pos = player->player.changed_blocks[i];
        if (pos.worldId != player->worldId) {
            continue;
        }
        u16 block_state = WorldGetBlockState(pos);
        if (block_state >= serv->vanilla_block_state_count) {
            // catches unknown blocks
            continue;
        }

        begin_packet(send_cursor, CBP_BLOCK_UPDATE);
        WriteBlockPos(send_cursor, pos.xyz);
        WriteVarU32(send_cursor, block_state);
        finish_packet(send_cursor, player);
    }
    player->player.changed_block_count = 0;

    BeginTimings(UpdateChunkCache);
    UpdateChunkCache(player, send_cursor);
    EndTimings(UpdateChunkCache);

    BeginTimings(SendTrackedBlockChanges);
    SendTrackedBlockChanges(player, send_cursor, tick_arena);
    EndTimings(SendTrackedBlockChanges);

    // load and send tracked chunks
    BeginTimings(LoadAndSendChunks);

    // @TODO(traks) Don't send chunks if there are still chunks being sent to
    // the client. Don't want to overflow the connection with chunks and lag
    // everything else. Is there any way to figure out if a chunk has been
    // received by the client? Or is there no way to tell if a chunk is still
    // buffered by the OS or TCP?

    // TODO(traks): would also be really cool if we could dynamically adjust the
    // chunk send rate based on the client's bandwidth. For example, send ping
    // packet then chunk packet, see how long it takes until we get the pong
    // packet. In case of low bandwidth, we should send chunks less frequently.
    // In case of high bandwidth, we can send chunks more rapidly.

    // We iterate in a spiral around the player, so chunks near the player
    // are processed first. This shortens server join times (since players
    // don't need to wait for the chunk they are in to load) and allows
    // players to move around much earlier.
    int newly_sent_chunks = 0;
    int newInterestAdded = 0;
    int chunk_cache_diam = 2 * player->player.chunkCacheRadius + 1;
    int chunk_cache_area = chunk_cache_diam * chunk_cache_diam;
    int off_x = 0;
    int off_z = 0;
    int step_x = 1;
    int step_z = 0;
    for (int i = 0; i < chunk_cache_area; i++) {
        int x = player->player.chunkCacheCentreX + off_x;
        int z = player->player.chunkCacheCentreZ + off_z;
        int cache_index = chunk_cache_index((ChunkPos) {.x = x, .z = z});
        PlayerChunkCacheEntry * cacheEntry = player->player.chunkCache + cache_index;
        WorldChunkPos pos = {.worldId = player->worldId, .x = x, .z = z};

        if (!(cacheEntry->flags & PLAYER_CHUNK_ADDED_INTEREST)
                && newInterestAdded < MAX_CHUNK_LOADS_PER_TICK) {
            AddChunkInterest(pos, 1);
            cacheEntry->flags |= PLAYER_CHUNK_ADDED_INTEREST;
            newInterestAdded++;
        }

        if (!(cacheEntry->flags & PLAYER_CHUNK_SENT)
                && newly_sent_chunks < MAX_CHUNK_SENDS_PER_TICK) {
            Chunk * ch = GetChunkIfLoaded(pos);
            if (ch != NULL) {
                // send chunk blocks and lighting
                send_chunk_fully(send_cursor, ch, player, tick_arena);
                cacheEntry->flags |= PLAYER_CHUNK_SENT;
                newly_sent_chunks++;
            }
        }

        off_x += step_x;
        off_z += step_z;
        // change direction of spiral when we hit a corner
        if (off_x == off_z || (off_x == -off_z && off_x < 0)
                || (off_x == -off_z + 1 && off_x > 0)) {
            int prev_step_x = step_x;
            step_x = -step_z;
            step_z = prev_step_x;
        }
    }

    EndTimings(LoadAndSendChunks);

    // send updates in player's own inventory
    BeginTimings(SendInventory);

    for (int i = 0; i < PLAYER_SLOTS; i++) {
        if (!(player->player.slots_needing_update & ((u64) 1 << i))) {
            continue;
        }

        LogInfo("Sending slot update for %d", i);
        item_stack * is = player->player.slots + i;

        // @TODO(traks) under certain conditions, the client may not modify its
        // local version of the inventory menu when we send this packet. Work
        // around this?

        begin_packet(send_cursor, CBP_CONTAINER_SET_SLOT);
        // @NOTE(traks) container ids:
        // -2 = own inventory
        // -1 = item held in cursor
        // 0 = inventory menu
        // 1, 2, ... = other container menus
        WriteU8(send_cursor, 0 ); // container id
        // @TODO(traks) figure out what this is used for
        WriteVarU32(send_cursor, 0); // state id
        WriteU16(send_cursor, i);

        if (is->type == 0) {
            WriteU8(send_cursor, 0); // has item
        } else {
            WriteU8(send_cursor, 1); // has item
            WriteVarU32(send_cursor, is->type);
            WriteU8(send_cursor, is->size);
            // @TODO(traks) write NBT (currently just a single end tag)
            WriteU8(send_cursor, 0);
        }
        finish_packet(send_cursor, player);
    }

    player->player.slots_needing_update = 0;
    memcpy(player->player.slots_prev_tick, player->player.slots,
            sizeof player->player.slots);

    EndTimings(SendInventory);

    // tab list updates
    BeginTimings(SendTabList);

    if (!(player->flags & PLAYER_INITIALISED_TAB_LIST)) {
        player->flags |= PLAYER_INITIALISED_TAB_LIST;
        if (serv->tab_list_size > 0) {
            begin_packet(send_cursor, CBP_PLAYER_INFO_UPDATE);
            u8 actionBits = 0b111111; // everything
            WriteU8(send_cursor, actionBits);
            WriteVarU32(send_cursor, serv->tab_list_size);

            for (int i = 0; i < serv->tab_list_size; i++) {
                entity_id eid = serv->tab_list[i];
                entity_base * tabListPlayer = resolve_entity(eid);
                // TODO(traks): If a player disconnects mid tick (e.g. due to a
                // networking error), this assert can fail, because the player
                // entity will be gone. Fix this. For example by storing all
                // required data in the tab list array, so we don't need to
                // resolve entities.
                // assert(tabListPlayer->type == ENTITY_PLAYER);
                // @TODO(traks) write UUID
                WriteU64(send_cursor, 0);
                WriteU64(send_cursor, eid);
                String username = {
                    .data = tabListPlayer->player.username,
                    .size = tabListPlayer->player.username_size
                };
                WriteVarString(send_cursor, username);
                WriteVarU32(send_cursor, 0); // num properties
                WriteU8(send_cursor, 0); // has message signing key
                WriteVarU32(send_cursor, tabListPlayer->player.gamemode);
                WriteU8(send_cursor, 1); // listed
                WriteVarU32(send_cursor, 0); // latency
                WriteU8(send_cursor, 0); // has display name
            }
            finish_packet(send_cursor, player);
        }
    } else {
        if (serv->tab_list_removed_count > 0) {
            begin_packet(send_cursor, CBP_PLAYER_INFO_REMOVE);
            WriteVarU32(send_cursor, serv->tab_list_removed_count);

            for (int i = 0; i < serv->tab_list_removed_count; i++) {
                entity_id eid = serv->tab_list_removed[i];
                // @TODO(traks) write UUID
                WriteU64(send_cursor, 0);
                WriteU64(send_cursor, eid);
            }
            finish_packet(send_cursor, player);
        }
        if (serv->tab_list_added_count > 0) {
            begin_packet(send_cursor, CBP_PLAYER_INFO_UPDATE);

            // NOTE(traks): actions:
            // 0 = add player
            // 1 = init chat
            // 2 = update game mode
            // 3 = update listed
            // 4 = update latency
            // 5 = update display name

            // TODO(traks): init chat?

            u8 actionBits = 0b111111; // everything
            WriteU8(send_cursor, actionBits);
            WriteVarU32(send_cursor, serv->tab_list_added_count);

            for (int i = 0; i < serv->tab_list_added_count; i++) {
                entity_id eid = serv->tab_list_added[i];
                entity_base * tabListPlayer = resolve_entity(eid);
                // TODO(traks): If a player disconnects mid tick (e.g. due to a
                // networking error), this assert can fail, because the player
                // entity will be gone. Fix this. For example by storing all
                // required data in the tab list array, so we don't need to
                // resolve entities.
                // assert(tabListPlayer->type == ENTITY_PLAYER);
                // @TODO(traks) write UUID
                WriteU64(send_cursor, 0);
                WriteU64(send_cursor, eid);

                // NOTE(traks): after the UUID, write all the data of the player
                // per action, in order

                String username = {
                    .data = tabListPlayer->player.username,
                    .size = tabListPlayer->player.username_size
                };
                WriteVarString(send_cursor, username);
                WriteVarU32(send_cursor, 0); // num properties
                WriteU8(send_cursor, 0); // has message signing key
                WriteVarU32(send_cursor, tabListPlayer->player.gamemode);
                WriteU8(send_cursor, 1); // listed
                WriteVarU32(send_cursor, 0); // latency
                WriteU8(send_cursor, 0); // has display name
            }
            finish_packet(send_cursor, player);
        }

        for (int i = 0; i < MAX_ENTITIES; i++) {
            entity_base * entity = serv->entities + i;
            if (!(entity->flags & ENTITY_IN_USE)) {
                continue;
            }
            if (entity->type != ENTITY_PLAYER) {
                continue;
            }

            if (entity->changed_data & PLAYER_GAMEMODE_CHANGED) {
                begin_packet(send_cursor, CBP_PLAYER_INFO_UPDATE);
                WriteVarU32(send_cursor, 0b000100); // action: update gamemode
                WriteVarU32(send_cursor, 1); // changed entries
                // @TODO(traks) write uuid
                WriteU64(send_cursor, 0);
                WriteU64(send_cursor, entity->eid);
                WriteVarU32(send_cursor, entity->player.gamemode);
                finish_packet(send_cursor, player);
            }
        }
    }

    EndTimings(SendTabList);

    // entity tracking
    BeginTimings(TrackEntities);

    entity_id removed_entities[64];
    int removed_entity_count = 0;

    for (int j = 1; j < MAX_ENTITIES; j++) {
        entity_base * candidate = serv->entities + j;
        tracked_entity * tracked = player->player.tracked_entities + j;
        int is_tracking_candidate = (tracked->eid == candidate->eid);

        if ((candidate->flags & ENTITY_IN_USE)
                && is_tracking_candidate) {
            // Candidate is a valid entity and we're already tracking it
            double dx = candidate->x - player->x;
            double dy = candidate->y - player->y;
            double dz = candidate->z - player->z;
            if (candidate->worldId == player->worldId && dx * dx + dy * dy + dz * dz < 45 * 45) {
                try_update_tracked_entity(player,
                        send_cursor, tick_arena, tracked, candidate);
                continue;
            }
        }

        // several states are possible: the candidate isn't a valid entity, or
        // we aren't tracking it yet, or the currently tracked entity was
        // removed, or the tracked entity is out of range

        if (tracked->eid != 0) {
            // remove tracked entity if we were tracking an entity
            if (removed_entity_count == ARRAY_SIZE(removed_entities)) {
                // no more space to untrack, try again next tick
                continue;
            }

            removed_entities[removed_entity_count] = tracked->eid;
            tracked->eid = 0;
            removed_entity_count++;
        }

        if ((candidate->flags & ENTITY_IN_USE) && candidate->eid != player->eid
                && !is_tracking_candidate) {
            // candidate is a valid entity and wasn't tracked already
            double dx = candidate->x - player->x;
            double dy = candidate->y - player->y;
            double dz = candidate->z - player->z;

            if (candidate->worldId != player->worldId || dx * dx + dy * dy + dz * dz > 40 * 40) {
                // @NOTE(traks) Don't start tracking until close enough. This
                // radius should be lower than the untrack radius: if the track
                // radius is larger than the untrack radius, then there's a zone
                // in which we continuously track and untrack every tick. This
                // is a waste of bandwidth.
                continue;
            }

            start_tracking_entity(player,
                    send_cursor, tick_arena, tracked, candidate);
        }
    }

    if (removed_entity_count > 0) {
        begin_packet(send_cursor, CBP_REMOVE_ENTITIES);
        WriteVarU32(send_cursor, removed_entity_count);
        for (int i = 0; i < removed_entity_count; i++) {
            WriteVarU32(send_cursor, removed_entities[i]);
        }
        finish_packet(send_cursor, player);
    }

    EndTimings(TrackEntities);

    // send chat messages
    BeginTimings(SendChat);

    for (int msgIndex = 0; msgIndex < serv->global_msg_count; msgIndex++) {
        global_msg * msg = serv->global_msgs + msgIndex;

        // @TODO(traks) formatted messages and such
        unsigned char buf[1024];
        int buf_index = 0;
        String prefix = STR("{\"text\":\"");
        String suffix = STR("\"}");

        memcpy(buf + buf_index, prefix.data, prefix.size);
        buf_index += prefix.size;

        for (int i = 0; i < msg->size; i++) {
            if (msg->text[i] == '"' || msg->text[i] == '\\') {
                buf[buf_index] = '\\';
                buf_index++;
            }
            buf[buf_index] = msg->text[i];
            buf_index++;
        }

        memcpy(buf + buf_index, suffix.data, suffix.size);
        buf_index += suffix.size;

        String jsonMessage = {
            .size = buf_index,
            .data = buf,
        };

        // TODO(traks): use player chat packet for this with annoying signing.
        // Also will make chat narration work properly as "sender says message"
        // (see the chat types we define in the login packet).
        begin_packet(send_cursor, CBP_SYSTEM_CHAT);
        WriteVarString(send_cursor, jsonMessage);
        WriteU8(send_cursor, 0); // action bar or chat log
        // @TODO(traks) write sender UUID. If UUID equals 0, client displays it
        // regardless of client settings
        // WriteU64(send_cursor, 0);
        // WriteU64(send_cursor, 0);
        finish_packet(send_cursor, player);
    }

    EndTimings(SendChat);

    // try to write everything to the socket buffer

    if (send_cursor->error != 0) {
        // just disconnect the player
        LogInfo("Failed to create packets");
        disconnect_player_now(player);
        goto bail;
    }

    BeginTimings(FinalisePackets);

    Cursor final_cursor_ = {
        .data = player->player.send_buf,
        .size = player->player.send_buf_size,
        .index = player->player.send_cursor
    };
    Cursor * final_cursor = &final_cursor_;

    send_cursor->size = send_cursor->index;
    send_cursor->index = 0;
    while (send_cursor->index != send_cursor->size) {
        int internal_header = send_cursor->data[send_cursor->index];
        int size_offset = internal_header & 0x7;
        int should_compress = internal_header & 0x80;

        send_cursor->index += 1 + size_offset;

        int packet_start = send_cursor->index;
        i32 packet_size = ReadVarU32(send_cursor);
        int packet_end = send_cursor->index + packet_size;

        // TODO(traks): these 2 lines are debugging code
        i32 packetId = ReadVarU32(send_cursor);
        send_cursor->index = packet_end - packet_size;

        if (should_compress) {
            // @TODO(traks) handle errors properly

            z_stream zstream;
            zstream.zalloc = Z_NULL;
            zstream.zfree = Z_NULL;
            zstream.opaque = Z_NULL;

            if (deflateInit(&zstream, Z_DEFAULT_COMPRESSION) != Z_OK) {
                final_cursor->error = 1;
                break;
            }

            zstream.next_in = send_cursor->data + send_cursor->index;
            zstream.avail_in = packet_end - send_cursor->index;

            MemoryArena temp_arena = *tick_arena;
            // @TODO(traks) appropriate value
            size_t max_compressed_size = 1 << 19;
            unsigned char * compressed = MallocInArena(&temp_arena,
                    max_compressed_size);

            zstream.next_out = compressed;
            zstream.avail_out = max_compressed_size;

            BeginTimings(Deflate);
            // i64 deflateTimeStart = NanoTime();
            if (deflate(&zstream, Z_FINISH) != Z_STREAM_END) {
                EndTimings(Deflate);
                final_cursor->error = 1;
                break;
            }
            // i64 deflateTimeEnd = NanoTime();
            // if (packetId == CBP_LEVEL_CHUNK_WITH_LIGHT) {
            //     LogInfo("Deflate took %jdµs for size %jd of packet %d", (intmax_t) (deflateTimeEnd - deflateTimeStart) / 1000, (intmax_t) packet_size, packetId);
            // }
            EndTimings(Deflate);

            if (deflateEnd(&zstream) != Z_OK) {
                final_cursor->error = 1;
                break;
            }

            if (zstream.avail_in != 0) {
                final_cursor->error = 1;
                break;
            }

            WriteVarU32(final_cursor, VarU32Size(packet_size) + zstream.total_out);
            WriteVarU32(final_cursor, packet_size);
            WriteData(final_cursor, compressed, zstream.total_out);
        } else {
            // @TODO(traks) should check somewhere that no error occurs
            WriteData(final_cursor, send_cursor->data + packet_start,
                    packet_end - packet_start);
        }

        send_cursor->index = packet_end;
    }

    EndTimings(FinalisePackets);

    if (final_cursor->error != 0) {
        // just disconnect the player
        LogInfo("Failed to finalise packets");
        disconnect_player_now(player);
        goto bail;
    }

    BeginTimings(SystemSend);
    ssize_t send_size = send(player->player.sock, final_cursor->data,
            final_cursor->index, 0);
    EndTimings(SystemSend);

    if (send_size == -1) {
        // EAGAIN means no data sent
        if (errno != EAGAIN) {
            LogErrno("Couldn't send protocol data to player: %s");
            disconnect_player_now(player);
        }
    } else {
        memmove(final_cursor->data, final_cursor->data + send_size,
                final_cursor->index - send_size);
        player->player.send_cursor = final_cursor->index - send_size;
    }

bail:
    EndTimings(SendPackets);
}

int
get_player_facing(entity_base * player) {
    float rot_y = player->rot_y;
    int int_rot = (int) floor(rot_y / 90.0f + 0.5f) & 0x3;
    switch (int_rot) {
    case 0: return DIRECTION_POS_Z;
    case 1: return DIRECTION_NEG_X;
    case 2: return DIRECTION_NEG_Z;
    case 3: return DIRECTION_POS_X;
    default:
        assert(0);
        return -1;
    }
}
