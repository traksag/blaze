#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <zlib.h>
#include "shared.h"

// Implicit packet IDs for ease of updating. Updating packet IDs manually is a
// pain because packet types are ordered alphabetically and Mojang doesn't
// provide an explicit list of packet IDs.
enum serverbound_packet_type {
    SBP_ACCEPT_TELEPORT,
    SBP_BLOCK_ENTITY_TAG_QUERY,
    SBP_CHANGE_DIFFICULTY,
    SBP_CHAT,
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
    SERVERBOUND_PACKET_COUNT,
};

enum clientbound_packet_type {
    CBP_ADD_ENTITY,
    CBP_ADD_EXPERIENCE_ORB,
    CBP_ADD_MOB,
    CBP_ADD_PAINTING,
    CBP_ADD_PLAYER,
    CBP_ADD_VIBRATION_SIGNAL,
    CBP_ANIMATE,
    CBP_AWARD_STATS,
    CBP_BLOCK_BREAK_ACK,
    CBP_BLOCK_DESTRUCTION,
    CBP_BLOCK_ENTITY_DATA,
    CBP_BLOCK_EVENT,
    CBP_BLOCK_UPDATE,
    CBP_BOSS_EVENT,
    CBP_CHANGE_DIFFICULTY,
    CBP_CHAT,
    CBP_CLEAR_TITLES,
    CBP_COMMANDS,
    CBP_COMMAND_SUGGESTIONS,
    CBP_CONTAINER_CLOSE,
    CBP_CONTAINER_SET_CONTENT,
    CBP_CONTAINER_SET_DATA,
    CBP_CONTAINER_SET_SLOT,
    CBP_COOLDOWN,
    CBP_CUSTOM_PAYLOAD,
    CBP_CUSTOM_SOUND,
    CBP_DISCONNECT,
    CBP_ENTITY_EVENT,
    CBP_EXPLODE,
    CBP_FORGET_LEVEL_CHUNK,
    CBP_GAME_EVENT,
    CBP_HORSE_SCREEN_OPEN,
    CBP_INITIALISE_BORDER,
    CBP_KEEP_ALIVE,
    CBP_LEVEL_CHUNK,
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
    CBP_PLAYER_COMBAT_END,
    CBP_PLAYER_COMBAT_ENTER,
    CBP_PLAYER_COMBAT_KILL,
    CBP_PLAYER_INFO,
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
    CBP_SET_SET_SUBTITLE_TEXT,
    CBP_SET_TIME,
    CBP_SET_TITLES,
    CBP_SET_TITLE_TEXT,
    CBP_SOUND_ENTITY,
    CBP_SOUND,
    CBP_STOP_SOUND,
    CBP_TAB_LIST,
    CBP_TAG_QUERY,
    CBP_TAKE_ITEM_ENTITY,
    CBP_TELEPORT_ENTITY,
    CBP_UPDATE_ADVANCEMENTS,
    CBP_UPDATE_ATTRIBUTES,
    CBP_UPDATE_MOVE_EFFECT,
    CBP_UPDATE_RECIPES,
    CBP_UPDATE_TAGS,
    CLIENTBOUND_PACKET_COUNT,
};

static void
nbt_write_key(BufferCursor * cursor, u8 tag, String key) {
    net_write_ubyte(cursor, tag);
    net_write_ushort(cursor, key.size);
    net_write_data(cursor, key.data, key.size);
}

static void
nbt_write_string(BufferCursor * cursor, String val) {
    net_write_ushort(cursor, val.size);
    net_write_data(cursor, val.data, val.size);
}

void
teleport_player(entity_base * entity,
        double new_x, double new_y, double new_z,
        float new_rot_x, float new_rot_y) {
    entity_player * player = &entity->player;
    player->current_teleport_id++;
    entity->flags |= ENTITY_TELEPORTING;
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
process_packet(entity_base * entity, BufferCursor * rec_cursor,
        MemoryArena * process_arena) {
    // @NOTE(traks) we need to handle packets in the order in which they arive,
    // so e.g. the client can move the player to a position, perform some
    // action, and then move the player a bit further, all in the same tick.
    //
    // This seems the most natural way to do it, and gives us e.g. as much
    // information about the player's position when they perform a certain
    // action.

    entity_player * player = &entity->player;
    i32 packet_id = net_read_varint(rec_cursor);

    switch (packet_id) {
    case SBP_ACCEPT_TELEPORT: {
        i32 teleport_id = net_read_varint(rec_cursor);

        if ((entity->flags & ENTITY_TELEPORTING)
                && (entity->flags & PLAYER_SENT_TELEPORT)
                && teleport_id == player->current_teleport_id) {
            entity->flags &= ~ENTITY_TELEPORTING;
            entity->flags &= ~PLAYER_SENT_TELEPORT;
        }
        break;
    }
    case SBP_BLOCK_ENTITY_TAG_QUERY: {
        logs("Packet block entity tag query");
        i32 id = net_read_varint(rec_cursor);
        u64 block_pos = net_read_ulong(rec_cursor);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_CHANGE_DIFFICULTY: {
        logs("Packet change difficulty");
        u8 difficulty = net_read_ubyte(rec_cursor);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_CHAT: {
        String chat = net_read_string(rec_cursor, 256);

        if (serv->global_msg_count < ARRAY_SIZE(serv->global_msgs)) {
            global_msg * msg = serv->global_msgs + serv->global_msg_count;
            serv->global_msg_count++;
            int text_size = sprintf(
                    (void *) msg->text, "<%.*s> %.*s",
                    (int) player->username_size,
                    player->username,
                    (int) chat.size, chat.data);
            msg->size = text_size;
        }
        break;
    }
    case SBP_CLIENT_COMMAND: {
        logs("Packet client command");
        i32 action = net_read_varint(rec_cursor);
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
        logs("Packet client information");
        String language = net_read_string(rec_cursor, 16);
        u8 view_distance = net_read_ubyte(rec_cursor);
        i32 chat_visibility = net_read_varint(rec_cursor);
        u8 sees_chat_colours = net_read_ubyte(rec_cursor);
        u8 model_customisation = net_read_ubyte(rec_cursor);
        i32 main_hand = net_read_varint(rec_cursor);
        u8 text_filtering = net_read_ubyte(rec_cursor);

        // View distance is without the extra border of chunks,
        // while chunk cache radius is with the extra border of
        // chunks. This clamps the view distance between the minimum
        // of 2 and the server maximum.
        player->new_chunk_cache_radius = MIN(MAX(view_distance, 2),
                MAX_CHUNK_CACHE_RADIUS - 1) + 1;
        memcpy(player->language, language.data, language.size);
        player->language_size = language.size;
        player->sees_chat_colours = sees_chat_colours;
        player->model_customisation = model_customisation;
        player->main_hand = main_hand;
        player->text_filtering = text_filtering;
        break;
    }
    case SBP_COMMAND_SUGGESTION: {
        logs("Packet command suggestion");
        i32 id = net_read_varint(rec_cursor);
        String command = net_read_string(rec_cursor, 32500);
        break;
    }
    case SBP_CONTAINER_BUTTON_CLICK: {
        logs("Packet container button click");
        u8 container_id = net_read_ubyte(rec_cursor);
        u8 button_id = net_read_ubyte(rec_cursor);
        break;
    }
    case SBP_CONTAINER_CLICK: {
        logs("Packet container click");
        u8 container_id = net_read_ubyte(rec_cursor);
        i32 state_id = net_read_varint(rec_cursor);
        u16 slot = net_read_ushort(rec_cursor);
        u8 button = net_read_ubyte(rec_cursor);
        i32 click_type = net_read_varint(rec_cursor);
        i32 changed_slot_count = net_read_varint(rec_cursor);
        if (changed_slot_count > 100 || changed_slot_count < 0) {
            // @TODO(traks) better filtering of high values
            rec_cursor->error = 1;
            break;
        }

        // @TODO(traks) do something with the changed slots?

        for (int i = 0; i < changed_slot_count; i++) {
            u16 changed_slot = net_read_ushort(rec_cursor);
            // @TODO(traks) read item function
            u8 has_item = net_read_ubyte(rec_cursor);
            if (has_item) {
                // @TODO(traks) is this the new item stack or what?
                item_stack new_iss = {0};
                item_stack * new_is = &new_iss;
                new_is->type = net_read_varint(rec_cursor);
                new_is->size = net_read_ubyte(rec_cursor);
                // @TODO(traks) validate size and type as below

                // @TODO(traks) better value than 64 for the max level
                nbt_tape_entry * tape = load_nbt(rec_cursor, process_arena, 64);
                if (rec_cursor->error) {
                    break;
                }

                // @TODO(traks) use NBT data to construct item stack
            }
        }

        if (rec_cursor->error) {
            break;
        }

        u8 has_item = net_read_ubyte(rec_cursor);
        item_stack cursor_iss = {0};
        item_stack * cursor_is = &cursor_iss;

        if (has_item) {
            cursor_is->type = net_read_varint(rec_cursor);
            cursor_is->size = net_read_ubyte(rec_cursor);

            if (cursor_is->type < 0 || cursor_is->type >= ITEM_TYPE_COUNT || cursor_is->size == 0) {
                cursor_is->type = 0;
                // @TODO(traks) handle error (send slot updates?)
            }
            u8 max_size = get_max_stack_size(cursor_is->type);
            if (cursor_is->size > max_size) {
                cursor_is->size = max_size;
                // @TODO(traks) handle error (send slot updates?)
            }

            // @TODO(traks) better value than 64 for the max level
            nbt_tape_entry * tape = load_nbt(rec_cursor, process_arena, 64);
            if (rec_cursor->error) {
                break;
            }

            // @TODO(traks) use NBT data to construct item stack
        }

        // @TODO(traks) actually handle the event
        break;
    }
    case SBP_CONTAINER_CLOSE: {
        logs("Packet container close");
        u8 container_id = net_read_ubyte(rec_cursor);
        break;
    }
    case SBP_CUSTOM_PAYLOAD: {
        logs("Packet custom payload");
        String id = net_read_string(rec_cursor, 32767);
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
        logs("Packet edit book");
        i32 slot = net_read_varint(rec_cursor);
        i32 page_count = net_read_varint(rec_cursor);
        if (page_count > 200 || page_count < 0) {
            rec_cursor->error = 1;
            break;
        }
        for (int i = 0; i < page_count; i++) {
            String page = net_read_string(rec_cursor, 8192);
        }

        u8 has_title = net_read_ubyte(rec_cursor);
        String title = {0};
        if (has_title) {
            title = net_read_string(rec_cursor, 128);
        }

        // @TODO(traks) handle the packet
        break;
    }
    case SBP_ENTITY_TAG_QUERY: {
        logs("Packet entity tag query");
        i32 transaction_id = net_read_varint(rec_cursor);
        i32 entity_id = net_read_varint(rec_cursor);
        break;
    }
    case SBP_INTERACT: {
        logs("Packet interact");
        i32 entity_id = net_read_varint(rec_cursor);
        i32 action = net_read_varint(rec_cursor);

        switch (action) {
        case 0: { // interact
            i32 hand = net_read_varint(rec_cursor);
            u8 secondary_action = net_read_ubyte(rec_cursor);
            // @TODO(traks) implement
            break;
        }
        case 1: { // attack
            u8 secondary_action = net_read_ubyte(rec_cursor);
            // @TODO(traks) implement
            break;
        }
        case 2: { // interact at
            float x = net_read_float(rec_cursor);
            float y = net_read_float(rec_cursor);
            float z = net_read_float(rec_cursor);
            i32 hand = net_read_varint(rec_cursor);
            u8 secondary_action = net_read_ubyte(rec_cursor);
            // @TODO(traks) implement
            break;
        }
        default:
            rec_cursor->error = 1;
        }
        break;
    }
    case SBP_JIGSAW_GENERATE: {
        logs("Packet jigsaw generate");
        BlockPos block_pos = net_read_block_pos(rec_cursor);
        i32 levels = net_read_varint(rec_cursor);
        u8 keep_jigsaws = net_read_ubyte(rec_cursor);
        // @TODO(traks) processing
        break;
    }
    case SBP_KEEP_ALIVE: {
        u64 id = net_read_ulong(rec_cursor);
        if (player->last_keep_alive_sent_tick == id) {
            entity->flags |= PLAYER_GOT_ALIVE_RESPONSE;
        }
        break;
    }
    case SBP_LOCK_DIFFICULTY: {
        logs("Packet lock difficulty");
        u8 locked = net_read_ubyte(rec_cursor);
        break;
    }
    case SBP_MOVE_PLAYER_POS: {
        double x = net_read_double(rec_cursor);
        double y = net_read_double(rec_cursor);
        double z = net_read_double(rec_cursor);
        int on_ground = net_read_ubyte(rec_cursor);
        process_move_player_packet(entity, x, y, z,
                entity->rot_x, entity->rot_y, on_ground);
        break;
    }
    case SBP_MOVE_PLAYER_POS_ROT: {
        double x = net_read_double(rec_cursor);
        double y = net_read_double(rec_cursor);
        double z = net_read_double(rec_cursor);
        float head_rot_y = net_read_float(rec_cursor);
        float head_rot_x = net_read_float(rec_cursor);
        int on_ground = net_read_ubyte(rec_cursor);
        process_move_player_packet(entity, x, y, z,
                head_rot_x, head_rot_y, on_ground);
        break;
    }
    case SBP_MOVE_PLAYER_ROT: {
        float head_rot_y = net_read_float(rec_cursor);
        float head_rot_x = net_read_float(rec_cursor);
        int on_ground = net_read_ubyte(rec_cursor);
        process_move_player_packet(entity,
                entity->x, entity->y, entity->z,
                head_rot_x, head_rot_y, on_ground);
        break;
    }
    case SBP_MOVE_PLAYER_STATUS: {
        int on_ground = net_read_ubyte(rec_cursor);
        process_move_player_packet(entity,
                entity->x, entity->y, entity->z,
                entity->rot_x, entity->rot_y, on_ground);
        break;
    }
    case SBP_MOVE_VEHICLE: {
        logs("Packet move vehicle");
        double x = net_read_double(rec_cursor);
        double y = net_read_double(rec_cursor);
        double z = net_read_double(rec_cursor);
        float rot_y = net_read_float(rec_cursor);
        float rot_x = net_read_float(rec_cursor);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_PADDLE_BOAT: {
        logs("Packet paddle boat");
        u8 left = net_read_ubyte(rec_cursor);
        u8 right = net_read_ubyte(rec_cursor);
        break;
    }
    case SBP_PICK_ITEM: {
        logs("Packet pick item");
        i32 slot = net_read_varint(rec_cursor);
        break;
    }
    case SBP_PLACE_RECIPE: {
        logs("Packet place recipe");
        u8 container_id = net_read_ubyte(rec_cursor);
        String recipe = net_read_string(rec_cursor, 32767);
        u8 shift_down = net_read_ubyte(rec_cursor);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_PLAYER_ABILITIES: {
        logs("Packet player abilities");
        u8 flags = net_read_ubyte(rec_cursor);
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
        i32 action = net_read_varint(rec_cursor);
        // @TODO(traks) validate block pos inside world
        BlockPos block_pos = net_read_block_pos(rec_cursor);
        u8 direction = net_read_ubyte(rec_cursor);

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
                // player and the chunk is sent to the player

                chunk_pos pos = {
                    .x = block_pos.x >> 4,
                    .z = block_pos.z >> 4
                };
                chunk * ch = get_chunk_if_loaded(pos);
                if (ch == NULL) {
                    // @TODO(traks) client will still see block as
                    // broken. Does that really matter? A forget
                    // packet will probably reach them soon enough.
                    break;
                }

                // @TODO(traks) better block breaking logic. E.g. new state
                // should be water source block if waterlogged block is broken.
                // Should move this stuff to some generic block breaking
                // function, so we can do proper block updating of redstone dust
                // and stuff.
                int max_updates = 512;
                block_update_context buc = {
                    .blocks_to_update = alloc_in_arena(process_arena,
                            max_updates * sizeof (block_update)),
                    .update_count = 0,
                    .max_updates = max_updates
                };

                u16 new_state = 0;
                chunk_set_block_state(ch, block_pos.x & 0xf, block_pos.y,
                        block_pos.z & 0xf, new_state);
                push_direct_neighbour_block_updates(block_pos, &buc);
                propagate_block_updates(&buc);

                // @TODO(traks) rewrite so we don't need an assert
                assert(player->block_break_ack_count
                        < ARRAY_SIZE(player->block_break_acks));
                player->block_break_acks[player->block_break_ack_count] = (block_break_ack) {
                    .pos = block_pos,
                    .new_state = new_state,
                    .action = action,
                    .success = 1,
                };
            }
            break;
        }
        case 1: { // abort destroy block
            // The player stopped mining the block before it breaks.
            // @TODO(traks)
            break;
        }
        case 2: { // stop destroy block
            // The player stopped mining the block because it broke.
            // @TODO(traks)
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
        i32 id = net_read_varint(rec_cursor);
        i32 action = net_read_varint(rec_cursor);
        i32 data = net_read_varint(rec_cursor);

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
        logs("Packet player input");
        // @TODO(traks) read packet
        break;
    }
    case SBP_PONG: {
        logs("Packet pong");
        i32 id = net_read_int(rec_cursor);
        // @TODO(traks) read packet
        break;
    }
    case SBP_RECIPE_BOOK_CHANGE_SETTINGS: {
        logs("Packet recipe book change settings");
        // @TODO(traks) read packet
        break;
    }
    case SBP_RECIPE_BOOK_SEEN_RECIPE: {
        logs("Packet recipe book seen recipe");
        // @TODO(traks) read packet
        break;
    }
    case SBP_RENAME_ITEM: {
        logs("Packet rename item");
        String name = net_read_string(rec_cursor, 32767);
        break;
    }
    case SBP_RESOURCE_PACK: {
        logs("Packet resource pack");
        i32 action = net_read_varint(rec_cursor);
        break;
    }
    case SBP_SEEN_ADVANCEMENTS: {
        logs("Packet seen advancements");
        i32 action = net_read_varint(rec_cursor);
        // @TODO(traks) further processing
        break;
    }
    case SBP_SELECT_TRADE: {
        logs("Packet select trade");
        i32 item = net_read_varint(rec_cursor);
        break;
    }
    case SBP_SET_BEACON: {
        logs("Packet set beacon");
        i32 primary_effect = net_read_varint(rec_cursor);
        i32 secondary_effect = net_read_varint(rec_cursor);
        break;
    }
    case SBP_SET_CARRIED_ITEM: {
        logs("Set carried item");
        u16 slot = net_read_ushort(rec_cursor);
        if (slot > PLAYER_LAST_HOTBAR_SLOT - PLAYER_FIRST_HOTBAR_SLOT) {
            rec_cursor->error = 1;
            break;
        }
        player->selected_slot = PLAYER_FIRST_HOTBAR_SLOT + slot;
        break;
    }
    case SBP_SET_COMMAND_BLOCK: {
        logs("Packet set command block");
        u64 block_pos = net_read_ulong(rec_cursor);
        String command = net_read_string(rec_cursor, 32767);
        i32 mode = net_read_varint(rec_cursor);
        u8 flags = net_read_ubyte(rec_cursor);
        u8 track_output = (flags & 0x1);
        u8 conditional = (flags & 0x2);
        u8 automatic = (flags & 0x4);
        break;
    }
    case SBP_SET_COMMAND_MINECART: {
        logs("Packet set command minecart");
        i32 entity_id = net_read_varint(rec_cursor);
        String command = net_read_string(rec_cursor, 32767);
        u8 track_output = net_read_ubyte(rec_cursor);
        break;
    }
    case SBP_SET_CREATIVE_MODE_SLOT: {
        logs("Set creative mode slot");
        u16 slot = net_read_ushort(rec_cursor);
        u8 has_item = net_read_ubyte(rec_cursor);

        if (slot >= PLAYER_SLOTS) {
            rec_cursor->error = 1;
            break;
        }

        item_stack * is = player->slots + slot;
        *is = (item_stack) {0};

        if (has_item) {
            is->type = net_read_varint(rec_cursor);
            is->size = net_read_ubyte(rec_cursor);

            if (is->type < 0 || is->type >= ITEM_TYPE_COUNT || is->size == 0) {
                is->type = 0;
                player->slots_needing_update |=
                        (u64) 1 << slot;
            }

            String type_name = get_resource_loc(is->type, &serv->item_resource_table);
            logs("Set creative slot: %.*s", (int) type_name.size, type_name.data);

            u8 max_size = get_max_stack_size(is->type);
            if (is->size > max_size) {
                is->size = max_size;
                player->slots_needing_update |=
                        (u64) 1 << slot;
            }

            // @TODO(traks) better value than 64 for the max level
            nbt_tape_entry * tape = load_nbt(rec_cursor, process_arena, 64);
            if (rec_cursor->error) {
                break;
            }

            // @TODO(traks) use NBT data to construct item stack
        }
        break;
    }
    case SBP_SET_JIGSAW_BLOCK: {
        logs("Packet set jigsaw block");
        u64 block_pos = net_read_ulong(rec_cursor);
        String name = net_read_string(rec_cursor, 32767);
        String target = net_read_string(rec_cursor, 32767);
        String pool = net_read_string(rec_cursor, 32767);
        String final_state = net_read_string(rec_cursor, 32767);
        String joint = net_read_string(rec_cursor, 32767);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_SET_STRUCTURE_BLOCK: {
        logs("Packet set structure block");
        u64 block_pos = net_read_ulong(rec_cursor);
        i32 update_type = net_read_varint(rec_cursor);
        i32 mode = net_read_varint(rec_cursor);
        String name = net_read_string(rec_cursor, 32767);
        // @TODO(traks) read signed bytes instead
        u8 offset_x = net_read_ubyte(rec_cursor);
        u8 offset_y = net_read_ubyte(rec_cursor);
        u8 offset_z = net_read_ubyte(rec_cursor);
        u8 size_x = net_read_ubyte(rec_cursor);
        u8 size_y = net_read_ubyte(rec_cursor);
        u8 size_z = net_read_ubyte(rec_cursor);
        i32 mirror = net_read_varint(rec_cursor);
        i32 rotation = net_read_varint(rec_cursor);
        String data = net_read_string(rec_cursor, 12);
        // @TODO(traks) further reading
        break;
    }
    case SBP_SIGN_UPDATE: {
        logs("Packet sign update");
        u64 block_pos = net_read_ulong(rec_cursor);
        String lines[4];
        for (int i = 0; i < ARRAY_SIZE(lines); i++) {
            lines[i] = net_read_string(rec_cursor, 384);
        }
        break;
    }
    case SBP_SWING: {
        // logs("Packet swing");
        i32 hand = net_read_varint(rec_cursor);
        break;
    }
    case SBP_TELEPORT_TO_ENTITY: {
        logs("Packet teleport to entity");
        // @TODO(traks) read UUID instead
        u64 uuid_high = net_read_ulong(rec_cursor);
        u64 uuid_low = net_read_ulong(rec_cursor);
        break;
    }
    case SBP_USE_ITEM_ON: {
        logs("Packet use item on");
        i32 hand = net_read_varint(rec_cursor);
        BlockPos clicked_pos = net_read_block_pos(rec_cursor);
        i32 clicked_face = net_read_varint(rec_cursor);
        float click_offset_x = net_read_float(rec_cursor);
        float click_offset_y = net_read_float(rec_cursor);
        float click_offset_z = net_read_float(rec_cursor);
        // @TODO(traks) figure out what this is used for
        u8 is_inside = net_read_ubyte(rec_cursor);

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
        break;
    }
    case SBP_USE_ITEM: {
        logs("Packet use item");
        i32 hand = net_read_varint(rec_cursor);
        break;
    }
    default: {
        logs("Unknown player packet id %jd", (intmax_t) packet_id);
        rec_cursor->error = 1;
    }
    }
}

static int
chunk_cache_index(chunk_pos pos) {
    // Do some remainder operations first so we don't integer overflow. Note
    // that the remainder operator can produce negative numbers.
    int n = MAX_CHUNK_CACHE_DIAM * MAX_CHUNK_CACHE_DIAM;
    long x = (pos.x % n) + n;
    long z = (pos.z % n) + n;
    return (x * MAX_CHUNK_CACHE_DIAM + z) % n;
}

static void
begin_packet(BufferCursor * send_cursor, i32 id) {
    if (send_cursor->size - send_cursor->index < 6) {
        send_cursor->error = 1;
        return;
    }

    send_cursor->mark = send_cursor->index;
    // reserve space for internal header
    send_cursor->index += 1;
    // skip some bytes for packet size varint at the start
    send_cursor->index += 5;
    net_write_varint(send_cursor, id);
}

static void
finish_packet(BufferCursor * send_cursor, entity_base * player) {
    // We use the written data to determine the packet size instead of
    // calculating the packet size up front. The major benefit is that
    // calculating the packet size up front is very error prone and requires a
    // lot of maintainance (in case of packet format changes).
    //
    // The downside is that we have to copy all packet data an additional time,
    // because Mojang decided to encode packet sizes with a variable-size
    // encoding.

    if (send_cursor->error != 0) {
        // @NOTE(traks) the cursor mark may be invalid
        return;
    }

    int packet_end = send_cursor->index;
    send_cursor->index = send_cursor->mark;
    i32 packet_size = packet_end - send_cursor->index - 6;

    int size_offset = 5 - net_varint_size(packet_size);
    int internal_header = size_offset;
    if (player->flags & PLAYER_PACKET_COMPRESSION) {
        internal_header |= 0x80;
    }
    send_cursor->data[send_cursor->index] = internal_header;
    send_cursor->index += 1 + size_offset;

    net_write_varint(send_cursor, packet_size);

    send_cursor->index = packet_end;
}

static void
send_chunk_fully(BufferCursor * send_cursor, chunk_pos pos, chunk * ch,
        entity_base * entity, MemoryArena * tick_arena) {
    begin_timed_block("send chunk fully");

    // bit mask for included chunk sections; bottom section in least
    // significant bit. May be multiple longs if more than 64 sections height
    u64 section_mask = 0;
    for (int i = 0; i < 16; i++) {
        if (ch->sections[i] != NULL) {
            section_mask |= 1 << i;
        }
    }

    // calculate total size of chunk section data
    i32 section_data_size = 0;
    // @TODO(traks) compute bits per block using block type table
    int bits_per_block = 15;
    int blocks_per_long = 64 / bits_per_block;

    for (int i = 0; i < 16; i++) {
        chunk_section * section = ch->sections[i];
        if (section == NULL) {
            continue;
        }

        // size of non-air count + bits per block
        section_data_size += 2 + 1;
        // size of block state data in longs
        int longs = 16 * 16 * 16 / blocks_per_long;
        section_data_size += net_varint_size(longs);
        // number of bytes used to store block state data
        section_data_size += longs * 8;
    }

    begin_packet(send_cursor, CBP_LEVEL_CHUNK);
    net_write_int(send_cursor, pos.x);
    net_write_int(send_cursor, pos.z);
    // bitset of available sections, spread over multiple longs
    net_write_varint(send_cursor, 1);
    net_write_ulong(send_cursor, section_mask);

    // height map NBT
    nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR(""));

    nbt_write_key(send_cursor, NBT_TAG_LONG_ARRAY, STR("MOTION_BLOCKING"));
    // number of elements in long array
    net_write_int(send_cursor, 36);
    u64 compacted_map[36] = {0};

    int shift = 0;

    for (int z = 0; z < 16; z++) {
        for (int x = 0; x < 16; x++) {
            u64 height = ch->motion_blocking_height_map[(z << 4) | x];
            int start_long = shift >> 6;
            int offset = shift - (start_long << 6);

            compacted_map[start_long] |= height << offset;

            int bits_remaining = 64 - offset;

            if (bits_remaining < 9) {
                int end_long = start_long + 1;
                compacted_map[end_long] |= height >> bits_remaining;
            }

            shift += 9;
        }
    }

    for (int i = 0; i < 36; i++) {
        net_write_ulong(send_cursor, compacted_map[i]);
    }

    net_write_ubyte(send_cursor, NBT_TAG_END);
    // end of height map

    // Biome data. Currently we just set all biome blocks (4x4x4 cubes)
    // to the plains biome.
    net_write_varint(send_cursor, 1024);
    for (int i = 0; i < 1024; i++) {
        net_write_varint(send_cursor, 1);
    }

    net_write_varint(send_cursor, section_data_size);

    for (int i = 0; i < 16; i++) {
        chunk_section * section = ch->sections[i];
        if (section == NULL) {
            continue;
        }

        net_write_ushort(send_cursor, ch->non_air_count[i]);
        net_write_ubyte(send_cursor, bits_per_block);

        // number of longs used for the block states
        int longs = 16 * 16 * 16 / blocks_per_long;
        net_write_varint(send_cursor, longs);
        u64 val = 0;
        int offset = 0;

        for (int j = 0; j < 16 * 16 * 16; j++) {
            u64 block_state = section->block_states[j];
            val |= block_state << offset;
            offset += bits_per_block;

            if (offset > 64 - bits_per_block) {
                net_write_ulong(send_cursor, val);
                val = 0;
                offset = 0;
            }
        }

        if (offset != 0) {
            net_write_ulong(send_cursor, val);
        }
    }

    // number of block entities
    net_write_varint(send_cursor, 0);
    finish_packet(send_cursor, entity);

    end_timed_block();
}

static void
send_light_update(BufferCursor * send_cursor, chunk_pos pos, chunk * ch,
        entity_base * entity, MemoryArena * tick_arena) {
    // There are 18 chunk sections from 1 section below the world to 1 section
    // above the world. The lowest chunk section comes first (and is the least
    // significant bit).

    begin_timed_block("send light update");

    // @TODO(traks) send the real lighting data

    // light sections present as arrays in this packet
    u64 sky_light_mask = 0x3ffff;
    u64 block_light_mask = 0x3ffff;
    // sections with all light values equal to 0
    u64 zero_sky_light_mask = 0;
    u64 zero_block_light_mask = 0;

    begin_packet(send_cursor, CBP_LIGHT_UPDATE);
    net_write_varint(send_cursor, pos.x);
    net_write_varint(send_cursor, pos.z);
    net_write_ubyte(send_cursor, 1); // trust edges
    net_write_varint(send_cursor, 1);
    net_write_ulong(send_cursor, sky_light_mask);
    net_write_varint(send_cursor, 1);
    net_write_ulong(send_cursor, block_light_mask);
    net_write_varint(send_cursor, 1);
    net_write_ulong(send_cursor, zero_sky_light_mask);
    net_write_varint(send_cursor, 1);
    net_write_ulong(send_cursor, zero_block_light_mask);

    net_write_varint(send_cursor, 18);
    for (int i = 0; i < 18; i++) {
        net_write_varint(send_cursor, 2048);
        for (int j = 0; j < 4096; j += 2) {
            u8 light = 0xff;
            net_write_ubyte(send_cursor, light);
        }
    }

    net_write_varint(send_cursor, 18);
    for (int i = 0; i < 18; i++) {
        net_write_varint(send_cursor, 2048);
        for (int j = 0; j < 4096; j += 2) {
            u8 light = 0;
            net_write_ubyte(send_cursor, light);
        }
    }
    finish_packet(send_cursor, entity);

    end_timed_block();
}

static void
disconnect_player_now(entity_base * entity) {
    entity_player * player = &entity->player;
    close(player->sock);

    i16 chunk_cache_min_x = player->chunk_cache_centre_x - player->chunk_cache_radius;
    i16 chunk_cache_max_x = player->chunk_cache_centre_x + player->chunk_cache_radius;
    i16 chunk_cache_min_z = player->chunk_cache_centre_z - player->chunk_cache_radius;
    i16 chunk_cache_max_z = player->chunk_cache_centre_z + player->chunk_cache_radius;

    for (i16 x = chunk_cache_min_x; x <= chunk_cache_max_x; x++) {
        for (i16 z = chunk_cache_min_z; z <= chunk_cache_max_z; z++) {
            chunk_pos pos = {.x = x, .z = z};
            chunk * ch = get_chunk_if_available(pos);
            assert(ch != NULL);
            ch->available_interest--;
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
    begin_timed_block("tick player");

    assert(player->type == ENTITY_PLAYER);
    int sock = player->player.sock;
    ssize_t rec_size = recv(sock, player->player.rec_buf + player->player.rec_cursor,
            player->player.rec_buf_size - player->player.rec_cursor, 0);

    if (rec_size == 0) {
        disconnect_player_now(player);
    } else if (rec_size == -1) {
        // EAGAIN means no data received
        if (errno != EAGAIN) {
            logs_errno("Couldn't receive protocol data: %s");
            disconnect_player_now(player);
        }
    } else {
        player->player.rec_cursor += rec_size;

        BufferCursor rec_cursor = {
            .data = player->player.rec_buf,
            .size = player->player.rec_cursor
        };

        // @TODO(traks) rate limit incoming packets per player

        for (;;) {
            BufferCursor packet_cursor = rec_cursor;
            i32 packet_size = net_read_varint(&packet_cursor);

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
                net_read_varint(&packet_cursor);

                // @TODO(traks) move to a zlib alternative that is optimised
                // for single pass inflate/deflate. If we don't end up doing
                // this, make sure the code below is actually correct (when
                // do we need to clean stuff up?)!

                z_stream zstream;
                zstream.zalloc = Z_NULL;
                zstream.zfree = Z_NULL;
                zstream.opaque = Z_NULL;

                if (inflateInit2(&zstream, 0) != Z_OK) {
                    logs("inflateInit failed");
                    disconnect_player_now(player);
                    break;
                }

                zstream.next_in = packet_cursor.data + packet_cursor.index;
                zstream.avail_in = packet_cursor.size - packet_cursor.index;

                size_t max_uncompressed_size = 2 * (1 << 20);
                unsigned char * uncompressed = alloc_in_arena(&process_arena,
                        max_uncompressed_size);

                zstream.next_out = uncompressed;
                zstream.avail_out = max_uncompressed_size;

                if (inflate(&zstream, Z_FINISH) != Z_STREAM_END) {
                    logs("Failed to finish inflating packet: %s", zstream.msg);
                    disconnect_player_now(player);
                    break;
                }

                if (inflateEnd(&zstream) != Z_OK) {
                    logs("inflateEnd failed");
                    disconnect_player_now(player);
                    break;
                }

                if (zstream.avail_in != 0) {
                    logs("Didn't inflate entire packet");
                    disconnect_player_now(player);
                    break;
                }

                packet_cursor = (BufferCursor) {
                    .data = uncompressed,
                    .size = zstream.total_out,
                };
            }

            process_packet(player, &packet_cursor, &process_arena);

            if (packet_cursor.error != 0) {
                logs("Player protocol error occurred");
                disconnect_player_now(player);
                break;
            }

            if (packet_cursor.index != packet_cursor.size) {
                logs("Player protocol packet not fully read");
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
    end_timed_block();
}

static void
nbt_write_dimension_type(BufferCursor * send_cursor,
        dimension_type * dim_type) {
    if (dim_type->fixed_time != -1) {
        nbt_write_key(send_cursor, NBT_TAG_INT, STR("fixed_time"));
        net_write_int(send_cursor, dim_type->fixed_time);
    }

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("has_skylight"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_HAS_SKYLIGHT));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("has_ceiling"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_HAS_CEILING));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("ultrawarm"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_ULTRAWARM));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("natural"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_NATURAL));

    nbt_write_key(send_cursor, NBT_TAG_DOUBLE, STR("coordinate_scale"));
    net_write_double(send_cursor, dim_type->coordinate_scale);

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("piglin_safe"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_PIGLIN_SAFE));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("bed_works"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_BED_WORKS));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("respawn_anchor_works"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_RESPAWN_ANCHOR_WORKS));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("has_raids"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_HAS_RAIDS));

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("min_y"));
    net_write_int(send_cursor, dim_type->min_y);

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("height"));
    net_write_int(send_cursor, dim_type->height);

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("logical_height"));
    net_write_int(send_cursor, dim_type->logical_height);

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
    net_write_float(send_cursor, dim_type->ambient_light);
}

static void
nbt_write_biome(BufferCursor * send_cursor, biome * b) {
    nbt_write_key(send_cursor, NBT_TAG_STRING, STR("precipitation"));
    switch (b->precipitation) {
    case BIOME_PRECIPITATION_NONE:
        nbt_write_string(send_cursor, STR("none"));
        break;
    case BIOME_PRECIPITATION_RAIN:
        nbt_write_string(send_cursor, STR("rain"));
        break;
    case BIOME_PRECIPITATION_SNOW:
        nbt_write_string(send_cursor, STR("snow"));
        break;
    default:
        assert(0);
    }

    nbt_write_key(send_cursor, NBT_TAG_FLOAT, STR("temperature"));
    net_write_float(send_cursor, b->temperature);

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
    net_write_float(send_cursor, b->downfall);

    nbt_write_key(send_cursor, NBT_TAG_STRING, STR("category"));
    switch (b->category) {
    case BIOME_CATEGORY_NONE:
        nbt_write_string(send_cursor, STR("none"));
        break;
    case BIOME_CATEGORY_TAIGA:
        nbt_write_string(send_cursor, STR("taiga"));
        break;
    case BIOME_CATEGORY_EXTREME_HILLS:
        nbt_write_string(send_cursor, STR("extreme_hills"));
        break;
    case BIOME_CATEGORY_JUNGLE:
        nbt_write_string(send_cursor, STR("jungle"));
        break;
    case BIOME_CATEGORY_MESA:
        nbt_write_string(send_cursor, STR("mesa"));
        break;
    case BIOME_CATEGORY_PLAINS:
        nbt_write_string(send_cursor, STR("plains"));
        break;
    case BIOME_CATEGORY_SAVANNA:
        nbt_write_string(send_cursor, STR("savanna"));
        break;
    case BIOME_CATEGORY_ICY:
        nbt_write_string(send_cursor, STR("icy"));
        break;
    case BIOME_CATEGORY_THE_END:
        nbt_write_string(send_cursor, STR("the_end"));
        break;
    case BIOME_CATEGORY_BEACH:
        nbt_write_string(send_cursor, STR("beach"));
        break;
    case BIOME_CATEGORY_FOREST:
        nbt_write_string(send_cursor, STR("forest"));
        break;
    case BIOME_CATEGORY_OCEAN:
        nbt_write_string(send_cursor, STR("ocean"));
        break;
    case BIOME_CATEGORY_DESERT:
        nbt_write_string(send_cursor, STR("desert"));
        break;
    case BIOME_CATEGORY_RIVER:
        nbt_write_string(send_cursor, STR("river"));
        break;
    case BIOME_CATEGORY_SWAMP:
        nbt_write_string(send_cursor, STR("swamp"));
        break;
    case BIOME_CATEGORY_MUSHROOM:
        nbt_write_string(send_cursor, STR("mushroom"));
        break;
    case BIOME_CATEGORY_NETHER:
        nbt_write_string(send_cursor, STR("nether"));
        break;
    case BIOME_CATEGORY_UNDERGROUND:
        nbt_write_string(send_cursor, STR("underground"));
        break;
    }

    nbt_write_key(send_cursor, NBT_TAG_FLOAT, STR("depth"));
    net_write_float(send_cursor, b->depth);

    nbt_write_key(send_cursor, NBT_TAG_FLOAT, STR("scale"));
    net_write_float(send_cursor, b->scale);

    // special effects
    nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("effects"));

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("fog_color"));
    net_write_int(send_cursor, b->fog_colour);

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("water_color"));
    net_write_int(send_cursor, b->water_colour);

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("water_fog_color"));
    net_write_int(send_cursor, b->water_fog_colour);

    nbt_write_key(send_cursor, NBT_TAG_INT, STR("sky_color"));
    net_write_int(send_cursor, b->sky_colour);

    if (b->foliage_colour_override != -1) {
        nbt_write_key(send_cursor, NBT_TAG_INT, STR("foliage_color"));
        net_write_int(send_cursor, b->foliage_colour_override);
    }

    if (b->grass_colour_override != -1) {
        nbt_write_key(send_cursor, NBT_TAG_INT, STR("grass_color"));
        net_write_int(send_cursor, b->grass_colour_override);
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
        net_write_int(send_cursor, b->mood_sound_tick_delay);

        nbt_write_key(send_cursor, NBT_TAG_INT, STR("block_search_extent"));
        net_write_int(send_cursor, b->mood_sound_block_search_extent);

        nbt_write_key(send_cursor, NBT_TAG_DOUBLE, STR("offset"));
        net_write_int(send_cursor, b->mood_sound_offset);

        net_write_ubyte(send_cursor, NBT_TAG_END);
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
        net_write_int(send_cursor, b->additions_sound_tick_chance);

        net_write_ubyte(send_cursor, NBT_TAG_END);
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
        net_write_int(send_cursor, b->music_min_delay);

        nbt_write_key(send_cursor, NBT_TAG_INT, STR("max_delay"));
        net_write_int(send_cursor, b->music_max_delay);

        nbt_write_key(send_cursor, NBT_TAG_BYTE, STR("replace_current_music"));
        net_write_ubyte(send_cursor, b->music_replace_current_music);

        net_write_ubyte(send_cursor, NBT_TAG_END);
        // music end
    }

    net_write_ubyte(send_cursor, NBT_TAG_END);
    // special effects end
}

static void
send_changed_entity_data(BufferCursor * send_cursor, entity_base * player,
        entity_base * entity, u32 changed_data) {
    if (changed_data == 0) {
        return;
    }

    begin_packet(send_cursor, CBP_SET_ENTITY_DATA);
    net_write_varint(send_cursor, entity->eid);

    if (changed_data & (1 << ENTITY_DATA_FLAGS)) {
        net_write_ubyte(send_cursor, ENTITY_DATA_FLAGS);
        net_write_varint(send_cursor, ENTITY_DATA_TYPE_BYTE);
        u8 flags = 0;
        // @TODO(traks) shared flags
        flags |= !!(entity->flags & ENTITY_VISUAL_FIRE) << 0;
        flags |= !!(entity->flags & ENTITY_SHIFTING) << 1;
        flags |= !!(entity->flags & ENTITY_SPRINTING) << 3;
        flags |= !!(entity->flags & ENTITY_SWIMMING) << 4;
        flags |= !!(entity->flags & ENTITY_INVISIBLE) << 5;
        flags |= !!(entity->flags & ENTITY_GLOWING) << 6;
        // flags |= !!(entity->flags & ENTITY_FALL_FLYING) << 7;
        net_write_ubyte(send_cursor, flags);
    }

    if (changed_data & (1 << ENTITY_DATA_AIR_SUPPLY)) {
        net_write_ubyte(send_cursor, ENTITY_DATA_AIR_SUPPLY);
        net_write_varint(send_cursor, ENTITY_DATA_TYPE_INT);
        net_write_varint(send_cursor, entity->air_supply);
    }

    // @TODO(traks) custom names
    // net_write_ubyte(send_cursor, ENTITY_BASE_DATA_CUSTOM_NAME);

    if (changed_data & (1 << ENTITY_DATA_CUSTOM_NAME_VISIBLE)) {
        net_write_ubyte(send_cursor, ENTITY_DATA_CUSTOM_NAME_VISIBLE);
        net_write_varint(send_cursor, ENTITY_DATA_TYPE_BOOL);
        net_write_ubyte(send_cursor, !!(entity->flags & ENTITY_CUSTOM_NAME_VISIBLE));
    }

    if (changed_data & (1 << ENTITY_DATA_SILENT)) {
        net_write_ubyte(send_cursor, ENTITY_DATA_SILENT);
        net_write_varint(send_cursor, ENTITY_DATA_TYPE_BOOL);
        net_write_ubyte(send_cursor, !!(entity->flags & ENTITY_SILENT));
    }

    if (changed_data & (1 << ENTITY_DATA_NO_GRAVITY)) {
        net_write_ubyte(send_cursor, ENTITY_DATA_NO_GRAVITY);
        net_write_varint(send_cursor, ENTITY_DATA_TYPE_BOOL);
        net_write_ubyte(send_cursor, !!(entity->flags & ENTITY_NO_GRAVITY));
    }

    if (changed_data & (1 << ENTITY_DATA_POSE)) {
        net_write_ubyte(send_cursor, ENTITY_DATA_POSE);
        net_write_varint(send_cursor, ENTITY_DATA_TYPE_POSE);
        net_write_varint(send_cursor, entity->pose);
    }

    switch (entity->type) {
    case ENTITY_PLAYER: {
        // @TODO(traks)
        break;
    }
    case ENTITY_ITEM: {
        if (changed_data & (1 << ENTITY_DATA_ITEM)) {
            net_write_ubyte(send_cursor, ENTITY_DATA_ITEM);
            net_write_varint(send_cursor, ENTITY_DATA_TYPE_ITEM_STACK);
            net_write_ubyte(send_cursor, 1); // has item
            item_stack * is = &entity->item.contents;
            net_write_varint(send_cursor, is->type);
            net_write_ubyte(send_cursor, is->size);
            // @TODO(traks) write NBT (currently just a single end tag)
            net_write_ubyte(send_cursor, NBT_TAG_END);
        }
        break;
    }
    }

    net_write_ubyte(send_cursor, 0xff); // end of entity data
    finish_packet(send_cursor, player);
}

static void
send_take_item_entity_packet(entity_base * player, BufferCursor * send_cursor,
        entity_id taker_id, entity_id pickup_id, u8 pickup_size) {
    begin_packet(send_cursor, CBP_TAKE_ITEM_ENTITY);
    net_write_varint(send_cursor, pickup_id);
    net_write_varint(send_cursor, taker_id);
    net_write_varint(send_cursor, pickup_size);
    finish_packet(send_cursor, player);
}

static void
try_update_tracked_entity(entity_base * player,
        BufferCursor * send_cursor, MemoryArena * tick_arena,
        tracked_entity * tracked, entity_base * entity) {
    if (serv->current_tick - tracked->last_update_tick < tracked->update_interval
            && entity->changed_data == 0) {
        return;
    }

    tracked->last_update_tick = serv->current_tick;

    double dx = entity->x - tracked->last_sent_x;
    double dy = entity->y - tracked->last_sent_y;
    double dz = entity->z - tracked->last_sent_z;
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
        i16 encoded_dx = floor(dx * 4096.0);
        i16 encoded_dy = floor(dy * 4096.0);
        i16 encoded_dz = floor(dz * 4096.0);

        if (sent_pos && sent_rot) {
            begin_packet(send_cursor, CBP_MOVE_ENTITY_POS_ROT);
            net_write_varint(send_cursor, entity->eid);
            net_write_short(send_cursor, encoded_dx);
            net_write_short(send_cursor, encoded_dy);
            net_write_short(send_cursor, encoded_dz);
            net_write_ubyte(send_cursor, encoded_rot_y);
            net_write_ubyte(send_cursor, encoded_rot_x);
            net_write_ubyte(send_cursor, !!(player->flags & ENTITY_ON_GROUND));
            finish_packet(send_cursor, player);
        } else if (sent_pos) {
            begin_packet(send_cursor, CBP_MOVE_ENTITY_POS);
            net_write_varint(send_cursor, entity->eid);
            net_write_short(send_cursor, encoded_dx);
            net_write_short(send_cursor, encoded_dy);
            net_write_short(send_cursor, encoded_dz);
            net_write_ubyte(send_cursor, !!(player->flags & ENTITY_ON_GROUND));
            finish_packet(send_cursor, player);
        } else if (sent_rot) {
            begin_packet(send_cursor, CBP_MOVE_ENTITY_ROT);
            net_write_varint(send_cursor, entity->eid);
            net_write_ubyte(send_cursor, encoded_rot_y);
            net_write_ubyte(send_cursor, encoded_rot_x);
            net_write_ubyte(send_cursor, !!(player->flags & ENTITY_ON_GROUND));
            finish_packet(send_cursor, player);
        }

        if (sent_pos) {
            i64 encoded_last_x = floor(tracked->last_sent_x * 4096.0);
            i64 encoded_last_y = floor(tracked->last_sent_y * 4096.0);
            i64 encoded_last_z = floor(tracked->last_sent_z * 4096.0);

            // @NOTE(traks) this is the way the Minecraft client calculates the
            // new position. We emulate this to reduce accumulated precision
            // loss across many move packets.
            tracked->last_sent_x = (encoded_last_x + encoded_dx) / 4096.0;
            tracked->last_sent_y = (encoded_last_y + encoded_dy) / 4096.0;
            tracked->last_sent_z = (encoded_last_z + encoded_dz) / 4096.0;

            tracked->last_send_pos_tick = serv->current_tick;
        }
        if (sent_rot) {
            tracked->last_sent_rot_x = encoded_rot_x;
            tracked->last_sent_rot_y = encoded_rot_y;
        }
    } else {
        begin_packet(send_cursor, CBP_TELEPORT_ENTITY);
        net_write_varint(send_cursor, entity->eid);
        net_write_double(send_cursor, entity->x);
        net_write_double(send_cursor, entity->y);
        net_write_double(send_cursor, entity->z);
        net_write_ubyte(send_cursor, encoded_rot_y);
        net_write_ubyte(send_cursor, encoded_rot_x);
        net_write_ubyte(send_cursor, !!(player->flags & ENTITY_ON_GROUND));
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
        net_write_varint(send_cursor, entity->eid);
        net_write_short(send_cursor, CLAMP(entity->vx, -3.9, 3.9) * 8000);
        net_write_short(send_cursor, CLAMP(entity->vy, -3.9, 3.9) * 8000);
        net_write_short(send_cursor, CLAMP(entity->vz, -3.9, 3.9) * 8000);
        finish_packet(send_cursor, player);
    }

    switch (entity->type) {
    case ENTITY_PLAYER: {
        if (encoded_rot_y != tracked->last_sent_head_rot_y) {
            tracked->last_sent_head_rot_y = encoded_rot_y;

            begin_packet(send_cursor, CBP_ROTATE_HEAD);
            net_write_varint(send_cursor, entity->eid);
            net_write_ubyte(send_cursor, encoded_rot_y);
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
        BufferCursor * send_cursor, MemoryArena * tick_arena,
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
        net_write_varint(send_cursor, entity->eid);
        // @TODO(traks) appropriate UUID
        net_write_ulong(send_cursor, 0);
        net_write_ulong(send_cursor, entity->eid);
        net_write_double(send_cursor, entity->x);
        net_write_double(send_cursor, entity->y);
        net_write_double(send_cursor, entity->z);
        net_write_ubyte(send_cursor, encoded_rot_y);
        net_write_ubyte(send_cursor, encoded_rot_x);
        finish_packet(send_cursor, player);

        tracked->last_sent_head_rot_y = encoded_rot_y;

        begin_packet(send_cursor, CBP_ROTATE_HEAD);
        net_write_varint(send_cursor, entity->eid);
        net_write_ubyte(send_cursor, encoded_rot_y);
        finish_packet(send_cursor, player);
        break;
    }
    case ENTITY_ITEM: {
        tracked->update_interval = 20;

        // begin_packet(send_cursor, CBP_ADD_MOB);
        // net_write_varint(send_cursor, entity->eid);
        // // @TODO(traks) appropriate UUID
        // net_write_ulong(send_cursor, 0);
        // net_write_ulong(send_cursor, 0);
        // net_write_varint(send_cursor, ENTITY_SQUID);
        // net_write_double(send_cursor, entity->x);
        // net_write_double(send_cursor, entity->y);
        // net_write_double(send_cursor, entity->z);
        // net_write_ubyte(send_cursor, 0);
        // net_write_ubyte(send_cursor, 0);
        // // @TODO(traks) y head rotation (what is that?)
        // net_write_ubyte(send_cursor, 0);
        // // @TODO(traks) entity velocity
        // net_write_short(send_cursor, 0);
        // net_write_short(send_cursor, 0);
        // net_write_short(send_cursor, 0);
        // finish_packet(send_cursor, player);
        begin_packet(send_cursor, CBP_ADD_ENTITY);
        net_write_varint(send_cursor, entity->eid);
        // @TODO(traks) appropriate UUID
        net_write_ulong(send_cursor, 0);
        net_write_ulong(send_cursor, entity->eid);
        net_write_varint(send_cursor, entity->type);
        net_write_double(send_cursor, entity->x);
        net_write_double(send_cursor, entity->y);
        net_write_double(send_cursor, entity->z);
        // rotation of items is ignored
        net_write_ubyte(send_cursor, 0); // x rot
        net_write_ubyte(send_cursor, 0); // y rot
        // this kind of entity data not used for items
        net_write_int(send_cursor, 0);
        // @NOTE(traks) for some reason Minecraft ignores the initial velocity
        // and initialises it to random values. To solve this, we send the
        // velocity in a separate packet below.
        net_write_short(send_cursor, 0);
        net_write_short(send_cursor, 0);
        net_write_short(send_cursor, 0);
        finish_packet(send_cursor, player);

        begin_packet(send_cursor, CBP_SET_ENTITY_MOTION);
        net_write_varint(send_cursor, entity->eid);
        net_write_short(send_cursor, CLAMP(entity->vx, -3.9, 3.9) * 8000);
        net_write_short(send_cursor, CLAMP(entity->vy, -3.9, 3.9) * 8000);
        net_write_short(send_cursor, CLAMP(entity->vz, -3.9, 3.9) * 8000);
        finish_packet(send_cursor, player);
        break;
    }
    }

    send_changed_entity_data(send_cursor, player, entity, 0xffffffff);
}

static void
send_player_abilities(BufferCursor * send_cursor, entity_base * player) {
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

    net_write_ubyte(send_cursor, ability_flags);
    net_write_float(send_cursor, 0.05); // flying speed
    net_write_float(send_cursor, 0.1); // walking speed
    finish_packet(send_cursor, player);
}

void
send_packets_to_player(entity_base * player, MemoryArena * tick_arena) {
    begin_timed_block("send packets");

    size_t max_uncompressed_packet_size = 1 << 20;
    BufferCursor send_cursor_ = {
        .data = alloc_in_arena(tick_arena, max_uncompressed_packet_size),
        .size = max_uncompressed_packet_size
    };
    BufferCursor * send_cursor = &send_cursor_;

    if (!(player->flags & PLAYER_DID_INIT_PACKETS)) {
        player->flags |= PLAYER_DID_INIT_PACKETS;

        if (PACKET_COMPRESSION_ENABLED) {
            // send login compression packet
            begin_packet(send_cursor, 3);
            net_write_varint(send_cursor, 0);
            finish_packet(send_cursor, player);

            player->flags |= PLAYER_PACKET_COMPRESSION;
        }

        // send game profile packet
        begin_packet(send_cursor, 2);
        // @TODO(traks) send UUID
        net_write_ulong(send_cursor, 0);
        net_write_ulong(send_cursor, player->eid);
        String username = {
            .size = player->player.username_size,
            .data = player->player.username
        };
        net_write_string(send_cursor, username);
        finish_packet(send_cursor, player);

        String level_name = STR("blaze:main");

        begin_packet(send_cursor, CBP_LOGIN);
        net_write_uint(send_cursor, player->eid);
        net_write_ubyte(send_cursor, 0); // hardcore
        net_write_ubyte(send_cursor, player->player.gamemode); // current gamemode
        net_write_ubyte(send_cursor, 255); // previous gamemode, -1 = none

        // all levels/worlds currently available on the server
        // @NOTE(traks) This list is used for tab completions
        net_write_varint(send_cursor, 1); // number of levels
        net_write_string(send_cursor, level_name);

        // Send all dimension-related NBT data

        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR(""));

        // write dimension types
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("minecraft:dimension_type"));

        nbt_write_key(send_cursor, NBT_TAG_STRING, STR("type"));
        nbt_write_string(send_cursor, STR("minecraft:dimension_type"));

        nbt_write_key(send_cursor, NBT_TAG_LIST, STR("value"));
        net_write_ubyte(send_cursor, NBT_TAG_COMPOUND);
        net_write_int(send_cursor, serv->dimension_type_count);
        for (int i = 0; i < serv->dimension_type_count; i++) {
            dimension_type * dim_type = serv->dimension_types + i;

            nbt_write_key(send_cursor, NBT_TAG_STRING, STR("name"));
            String name = {
                .data = dim_type->name,
                .size = dim_type->name_size
            };
            nbt_write_string(send_cursor, name);

            nbt_write_key(send_cursor, NBT_TAG_INT, STR("id"));
            net_write_int(send_cursor, i);

            nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("element"));
            nbt_write_dimension_type(send_cursor, dim_type);
            net_write_ubyte(send_cursor, NBT_TAG_END);

            net_write_ubyte(send_cursor, NBT_TAG_END);
        }

        net_write_ubyte(send_cursor, NBT_TAG_END);
        // end of dimension types

        // write biomes
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("minecraft:worldgen/biome"));

        nbt_write_key(send_cursor, NBT_TAG_STRING, STR("type"));
        nbt_write_string(send_cursor, STR("minecraft:worldgen/biome"));

        nbt_write_key(send_cursor, NBT_TAG_LIST, STR("value"));
        net_write_ubyte(send_cursor, NBT_TAG_COMPOUND);
        net_write_int(send_cursor, serv->biome_count);
        for (int i = 0; i < serv->biome_count; i++) {
            biome * b = serv->biomes + i;

            nbt_write_key(send_cursor, NBT_TAG_STRING, STR("name"));
            String name = {
                .data = b->name,
                .size = b->name_size
            };
            nbt_write_string(send_cursor, name);

            nbt_write_key(send_cursor, NBT_TAG_INT, STR("id"));
            net_write_int(send_cursor, i);

            nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR("element"));
            nbt_write_biome(send_cursor, b);
            net_write_ubyte(send_cursor, NBT_TAG_END);

            net_write_ubyte(send_cursor, NBT_TAG_END);
        }

        net_write_ubyte(send_cursor, NBT_TAG_END);
        // end of biomes

        net_write_ubyte(send_cursor, NBT_TAG_END);

        // dimension type NBT data of level player is joining
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, STR(""));
        nbt_write_dimension_type(send_cursor, serv->dimension_types);
        net_write_ubyte(send_cursor, NBT_TAG_END);

        // level name the player is joining
        net_write_string(send_cursor, level_name);

        net_write_ulong(send_cursor, 0); // hashed seed
        net_write_varint(send_cursor, 0); // max players (ignored by client)
        net_write_varint(send_cursor, player->player.new_chunk_cache_radius - 1);
        net_write_ubyte(send_cursor, 0); // reduced debug info
        net_write_ubyte(send_cursor, 1); // show death screen on death
        net_write_ubyte(send_cursor, 0); // is debug
        net_write_ubyte(send_cursor, 0); // is flat
        finish_packet(send_cursor, player);

        begin_packet(send_cursor, CBP_SET_CARRIED_ITEM);
        net_write_ubyte(send_cursor,
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

        net_write_varint(send_cursor, ARRAY_SIZE(tag_lists));
        for (int tagsi = 0; tagsi < ARRAY_SIZE(tag_lists); tagsi++) {
            tag_list * tags = tag_lists[tagsi];

            String name = {
                .size = tags->name_size,
                .data = tags->name
            };

            net_write_string(send_cursor, name);
            net_write_varint(send_cursor, tags->size);

            for (int i = 0; i < tags->size; i++) {
                tag_spec * tag = tags->tags + i;
                unsigned char * name_size = serv->tag_name_buf + tag->name_index;
                String tag_name = {
                    .size = *name_size,
                    .data = name_size + 1
                };

                net_write_string(send_cursor, tag_name);
                net_write_varint(send_cursor, tag->value_count);

                for (int vali = 0; vali < tag->value_count; vali++) {
                    i32 val = serv->tag_value_id_buf[tag->values_index + vali];
                    net_write_varint(send_cursor, val);
                }
            }
        }
        finish_packet(send_cursor, player);

        begin_packet(send_cursor, CBP_CUSTOM_PAYLOAD);
        String brand_str = STR("minecraft:brand");
        String brand = STR("blaze");
        net_write_string(send_cursor, brand_str);
        net_write_string(send_cursor, brand);
        finish_packet(send_cursor, player);

        begin_packet(send_cursor, CBP_CHANGE_DIFFICULTY);
        net_write_ubyte(send_cursor, 2); // difficulty normal
        net_write_ubyte(send_cursor, 0); // locked
        finish_packet(send_cursor, player);

        send_player_abilities(send_cursor, player);

        send_changed_entity_data(send_cursor, player, player, player->changed_data);

        // reset changed data, because all data is sent already and we don't
        // want to send the same data twice
        player->changed_data = 0;
    }

    // send keep alive packet every so often
    if (serv->current_tick - player->player.last_keep_alive_sent_tick >= KEEP_ALIVE_SPACING
            && (player->flags & PLAYER_GOT_ALIVE_RESPONSE)) {
        begin_packet(send_cursor, CBP_KEEP_ALIVE);
        net_write_ulong(send_cursor, serv->current_tick);
        finish_packet(send_cursor, player);

        player->player.last_keep_alive_sent_tick = serv->current_tick;
        player->flags &= ~PLAYER_GOT_ALIVE_RESPONSE;
    }

    if ((player->flags & ENTITY_TELEPORTING)
            && !(player->flags & PLAYER_SENT_TELEPORT)) {
        begin_packet(send_cursor, CBP_PLAYER_POSITION);
        net_write_double(send_cursor, player->x);
        net_write_double(send_cursor, player->y);
        net_write_double(send_cursor, player->z);
        net_write_float(send_cursor, player->rot_y);
        net_write_float(send_cursor, player->rot_x);
        net_write_ubyte(send_cursor, 0); // relative arguments
        net_write_varint(send_cursor, player->player.current_teleport_id);
        net_write_ubyte(send_cursor, 0); // dismount vehicle
        finish_packet(send_cursor, player);

        player->flags |= PLAYER_SENT_TELEPORT;
    }

    if (player->changed_data & PLAYER_GAMEMODE_CHANGED) {
        begin_packet(send_cursor, CBP_GAME_EVENT);
        net_write_ubyte(send_cursor, PACKET_GAME_EVENT_CHANGE_GAMEMODE);
        net_write_float(send_cursor, player->player.gamemode);
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

    // send block break acks
    for (int i = 0; i < player->player.block_break_ack_count; i++) {
        block_break_ack * ack = player->player.block_break_acks + i;

        begin_packet(send_cursor, CBP_BLOCK_BREAK_ACK);
        net_write_block_pos(send_cursor, ack->pos);
        net_write_varint(send_cursor, ack->new_state);
        net_write_varint(send_cursor, ack->action);
        net_write_ubyte(send_cursor, ack->success);
        finish_packet(send_cursor, player);
    }
    player->player.block_break_ack_count = 0;

    // send block changes for this player only
    for (int i = 0; i < player->player.changed_block_count; i++) {
        BlockPos pos = player->player.changed_blocks[i];
        u16 block_state = try_get_block_state(pos);
        if (block_state >= serv->vanilla_block_state_count) {
            // catches unknown blocks
            continue;
        }

        begin_packet(send_cursor, CBP_BLOCK_UPDATE);
        net_write_block_pos(send_cursor, pos);
        net_write_varint(send_cursor, block_state);
        finish_packet(send_cursor, player);
    }
    player->player.changed_block_count = 0;

    begin_timed_block("update chunk cache");

    i16 chunk_cache_min_x = player->player.chunk_cache_centre_x - player->player.chunk_cache_radius;
    i16 chunk_cache_min_z = player->player.chunk_cache_centre_z - player->player.chunk_cache_radius;
    i16 chunk_cache_max_x = player->player.chunk_cache_centre_x + player->player.chunk_cache_radius;
    i16 chunk_cache_max_z = player->player.chunk_cache_centre_z + player->player.chunk_cache_radius;

    i16 new_chunk_cache_centre_x = (i32) floor(player->x) >> 4;
    i16 new_chunk_cache_centre_z = (i32) floor(player->z) >> 4;
    assert(player->player.new_chunk_cache_radius <= MAX_CHUNK_CACHE_RADIUS);
    i16 new_chunk_cache_min_x = new_chunk_cache_centre_x - player->player.new_chunk_cache_radius;
    i16 new_chunk_cache_min_z = new_chunk_cache_centre_z - player->player.new_chunk_cache_radius;
    i16 new_chunk_cache_max_x = new_chunk_cache_centre_x + player->player.new_chunk_cache_radius;
    i16 new_chunk_cache_max_z = new_chunk_cache_centre_z + player->player.new_chunk_cache_radius;

    if (player->player.chunk_cache_centre_x != new_chunk_cache_centre_x
            || player->player.chunk_cache_centre_z != new_chunk_cache_centre_z) {
        begin_packet(send_cursor, CBP_SET_CHUNK_CACHE_CENTRE);
        net_write_varint(send_cursor, new_chunk_cache_centre_x);
        net_write_varint(send_cursor, new_chunk_cache_centre_z);
        finish_packet(send_cursor, player);
    }

    if (player->player.chunk_cache_radius != player->player.new_chunk_cache_radius) {
        begin_packet(send_cursor, CBP_SET_CHUNK_CACHE_RADIUS);
        net_write_varint(send_cursor, player->player.new_chunk_cache_radius);
        finish_packet(send_cursor, player);
    }

    // untrack old chunks
    for (i16 x = chunk_cache_min_x; x <= chunk_cache_max_x; x++) {
        for (i16 z = chunk_cache_min_z; z <= chunk_cache_max_z; z++) {
            chunk_pos pos = {.x = x, .z = z};
            int index = chunk_cache_index(pos);

            if (x >= new_chunk_cache_min_x && x <= new_chunk_cache_max_x
                    && z >= new_chunk_cache_min_z && z <= new_chunk_cache_max_z) {
                // old chunk still in new region
                // send block changes if chunk is visible to the client
                if (!player->player.chunk_cache[index].sent) {
                    continue;
                }

                chunk * ch = get_chunk_if_loaded(pos);
                assert(ch != NULL);

                if (ch->changed_block_count != 0) {
                    for (int section = 0; section < 16; section++) {
                        int changed_blocks_size = ARRAY_SIZE(ch->changed_blocks);
                        compact_chunk_block_pos sec_changed_blocks[changed_blocks_size];
                        int sec_changed_block_count = 0;

                        for (int i = 0; i < ch->changed_block_count; i++) {
                            compact_chunk_block_pos pos = ch->changed_blocks[i];
                            if ((pos.y >> 4) == section) {
                                sec_changed_blocks[sec_changed_block_count] = pos;
                                sec_changed_block_count++;
                            }
                        }

                        if (sec_changed_block_count == 0) {
                            continue;
                        }

                        begin_packet(send_cursor, CBP_SECTION_BLOCKS_UPDATE);
                        u64 section_pos =
                                ((u64) (x & 0x3fffff) << 42)
                                | ((u64) (z & 0x3fffff) << 20)
                                | (u64) (section & 0xfffff);
                        net_write_ulong(send_cursor, section_pos);
                        // @TODO(traks) appropriate value for this
                        net_write_ubyte(send_cursor, 1); // suppress light updates
                        net_write_varint(send_cursor, sec_changed_block_count);

                        for (int i = 0; i < sec_changed_block_count; i++) {
                            compact_chunk_block_pos pos = sec_changed_blocks[i];
                            i64 block_state = chunk_get_block_state(ch,
                                    pos.x, pos.y, pos.z);
                            i64 encoded = (block_state << 12)
                                    | (pos.x << 8) | (pos.z << 4) | (pos.y & 0xf);
                            net_write_varlong(send_cursor, encoded);
                        }
                        finish_packet(send_cursor, player);
                    }
                }

                for (int i = 0; i < ch->local_event_count; i++) {
                    level_event * event = ch->local_events + i;

                    begin_packet(send_cursor, CBP_LEVEL_EVENT);
                    net_write_int(send_cursor, event->type);
                    net_write_block_pos(send_cursor, event->pos);
                    net_write_int(send_cursor, event->data);
                    net_write_ubyte(send_cursor, 0); // is global event
                    finish_packet(send_cursor, player);
                }
                continue;
            }

            // old chunk is not in the new region
            chunk * ch = get_chunk_if_available(pos);
            assert(ch != NULL);
            ch->available_interest--;

            if (player->player.chunk_cache[index].sent) {
                player->player.chunk_cache[index] = (chunk_cache_entry) {0};

                begin_packet(send_cursor, CBP_FORGET_LEVEL_CHUNK);
                net_write_int(send_cursor, x);
                net_write_int(send_cursor, z);
                finish_packet(send_cursor, player);
            }
        }
    }

    // track new chunks
    for (i16 x = new_chunk_cache_min_x; x <= new_chunk_cache_max_x; x++) {
        for (i16 z = new_chunk_cache_min_z; z <= new_chunk_cache_max_z; z++) {
            if (x >= chunk_cache_min_x && x <= chunk_cache_max_x
                    && z >= chunk_cache_min_z && z <= chunk_cache_max_z) {
                // chunk already in old region
                continue;
            }

            // chunk not in old region
            chunk_pos pos = {.x = x, .z = z};
            chunk * ch = get_or_create_chunk(pos);
            ch->available_interest++;
        }
    }

    player->player.chunk_cache_radius = player->player.new_chunk_cache_radius;
    player->player.chunk_cache_centre_x = new_chunk_cache_centre_x;
    player->player.chunk_cache_centre_z = new_chunk_cache_centre_z;

    end_timed_block();

    // load and send tracked chunks
    begin_timed_block("load and send chunks");

    // We iterate in a spiral around the player, so chunks near the player
    // are processed first. This shortens server join times (since players
    // don't need to wait for the chunk they are in to load) and allows
    // players to move around much earlier.
    int newly_sent_chunks = 0;
    int newly_loaded_chunks = 0;
    int chunk_cache_diam = 2 * player->player.new_chunk_cache_radius + 1;
    int chunk_cache_area = chunk_cache_diam * chunk_cache_diam;
    int off_x = 0;
    int off_z = 0;
    int step_x = 1;
    int step_z = 0;
    for (int i = 0; i < chunk_cache_area; i++) {
        int x = new_chunk_cache_centre_x + off_x;
        int z = new_chunk_cache_centre_z + off_z;
        int cache_index = chunk_cache_index((chunk_pos) {.x = x, .z = z});
        chunk_cache_entry * entry = player->player.chunk_cache + cache_index;
        chunk_pos pos = {.x = x, .z = z};

        if (newly_loaded_chunks < MAX_CHUNK_LOADS_PER_TICK
                && serv->chunk_load_request_count
                < ARRAY_SIZE(serv->chunk_load_requests)) {
            chunk * ch = get_chunk_if_available(pos);
            assert(ch != NULL);
            assert(ch->available_interest > 0);
            if (!(ch->flags & CHUNK_LOADED)) {
                serv->chunk_load_requests[serv->chunk_load_request_count] = pos;
                serv->chunk_load_request_count++;
                newly_loaded_chunks++;
            }
        }

        if (newly_sent_chunks < MAX_CHUNK_SENDS_PER_TICK && !entry->sent) {
            chunk * ch = get_chunk_if_loaded(pos);
            if (ch != NULL) {
                // send chunk blocks and lighting
                send_chunk_fully(send_cursor, pos, ch, player, tick_arena);
                send_light_update(send_cursor, pos, ch, player, tick_arena);
                entry->sent = 1;
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

    end_timed_block();

    // send updates in player's own inventory
    begin_timed_block("send inventory");

    for (int i = 0; i < PLAYER_SLOTS; i++) {
        if (!(player->player.slots_needing_update & ((u64) 1 << i))) {
            continue;
        }

        logs("Sending slot update for %d", i);
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
        net_write_byte(send_cursor, 0 ); // container id
        // @TODO(traks) figure out what this is used for
        net_write_varint(send_cursor, 0); // state id
        net_write_ushort(send_cursor, i);

        if (is->type == 0) {
            net_write_ubyte(send_cursor, 0); // has item
        } else {
            net_write_ubyte(send_cursor, 1); // has item
            net_write_varint(send_cursor, is->type);
            net_write_ubyte(send_cursor, is->size);
            // @TODO(traks) write NBT (currently just a single end tag)
            net_write_ubyte(send_cursor, 0);
        }
        finish_packet(send_cursor, player);
    }

    player->player.slots_needing_update = 0;
    memcpy(player->player.slots_prev_tick, player->player.slots,
            sizeof player->player.slots);

    end_timed_block();

    // tab list updates
    begin_timed_block("send tab list");

    if (!(player->flags & PLAYER_INITIALISED_TAB_LIST)) {
        player->flags |= PLAYER_INITIALISED_TAB_LIST;
        if (serv->tab_list_size > 0) {
            begin_packet(send_cursor, CBP_PLAYER_INFO);
            net_write_varint(send_cursor, 0); // action: add
            net_write_varint(send_cursor, serv->tab_list_size);

            for (int i = 0; i < serv->tab_list_size; i++) {
                entity_id eid = serv->tab_list[i];
                entity_base * player = resolve_entity(eid);
                assert(player->type == ENTITY_PLAYER);
                // @TODO(traks) write UUID
                net_write_ulong(send_cursor, 0);
                net_write_ulong(send_cursor, eid);
                String username = {
                    .data = player->player.username,
                    .size = player->player.username_size
                };
                net_write_string(send_cursor, username);
                net_write_varint(send_cursor, 0); // num properties
                net_write_varint(send_cursor, player->player.gamemode);
                net_write_varint(send_cursor, 0); // latency
                net_write_ubyte(send_cursor, 0); // has display name
            }
            finish_packet(send_cursor, player);
        }
    } else {
        if (serv->tab_list_removed_count > 0) {
            begin_packet(send_cursor, CBP_PLAYER_INFO);
            net_write_varint(send_cursor, 4); // action: remove
            net_write_varint(send_cursor, serv->tab_list_removed_count);

            for (int i = 0; i < serv->tab_list_removed_count; i++) {
                entity_id eid = serv->tab_list_removed[i];
                // @TODO(traks) write UUID
                net_write_ulong(send_cursor, 0);
                net_write_ulong(send_cursor, eid);
            }
            finish_packet(send_cursor, player);
        }
        if (serv->tab_list_added_count > 0) {
            begin_packet(send_cursor, CBP_PLAYER_INFO);
            net_write_varint(send_cursor, 0); // action: add
            net_write_varint(send_cursor, serv->tab_list_added_count);

            for (int i = 0; i < serv->tab_list_added_count; i++) {
                entity_id eid = serv->tab_list_added[i];
                entity_base * player = resolve_entity(eid);
                assert(player->type == ENTITY_PLAYER);
                // @TODO(traks) write UUID
                net_write_ulong(send_cursor, 0);
                net_write_ulong(send_cursor, eid);
                String username = {
                    .data = player->player.username,
                    .size = player->player.username_size
                };
                net_write_string(send_cursor, username);
                net_write_varint(send_cursor, 0); // num properties
                net_write_varint(send_cursor, player->player.gamemode);
                net_write_varint(send_cursor, 0); // latency
                net_write_ubyte(send_cursor, 0); // has display name
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
                begin_packet(send_cursor, CBP_PLAYER_INFO);
                net_write_varint(send_cursor, 1); // action: update gamemode
                net_write_varint(send_cursor, 1); // changed entries
                // @TODO(traks) write uuid
                net_write_ulong(send_cursor, 0);
                net_write_ulong(send_cursor, entity->eid);
                net_write_varint(send_cursor, entity->player.gamemode);
                finish_packet(send_cursor, player);
            }
        }
    }

    end_timed_block();

    // entity tracking
    begin_timed_block("track entities");

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
            if (dx * dx + dy * dy + dz * dz < 45 * 45) {
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

            if (dx * dx + dy * dy + dz * dz > 40 * 40) {
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
        net_write_varint(send_cursor, removed_entity_count);
        for (int i = 0; i < removed_entity_count; i++) {
            net_write_varint(send_cursor, removed_entities[i]);
        }
        finish_packet(send_cursor, player);
    }

    end_timed_block();

    // send chat messages
    begin_timed_block("send chat");

    for (int i = 0; i < serv->global_msg_count; i++) {
        global_msg * msg = serv->global_msgs + i;

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

        begin_packet(send_cursor, CBP_CHAT);
        net_write_varint(send_cursor, buf_index);
        net_write_data(send_cursor, buf, buf_index);
        net_write_ubyte(send_cursor, 0); // chat box position
        // @TODO(traks) write sender UUID. If UUID equals 0, client displays it
        // regardless of client settings
        net_write_ulong(send_cursor, 0);
        net_write_ulong(send_cursor, 0);
        finish_packet(send_cursor, player);
    }

    end_timed_block();

    // try to write everything to the socket buffer

    if (send_cursor->error != 0) {
        // just disconnect the player
        logs("Failed to create packets");
        disconnect_player_now(player);
        goto bail;
    }

    begin_timed_block("finalise packets");

    BufferCursor final_cursor_ = {
        .data = player->player.send_buf,
        .size = player->player.send_buf_size,
        .index = player->player.send_cursor
    };
    BufferCursor * final_cursor = &final_cursor_;

    send_cursor->size = send_cursor->index;
    send_cursor->index = 0;
    while (send_cursor->index != send_cursor->size) {
        int internal_header = send_cursor->data[send_cursor->index];
        int size_offset = internal_header & 0x7;
        int should_compress = internal_header & 0x80;

        send_cursor->index += 1 + size_offset;

        int packet_start = send_cursor->index;
        i32 packet_size = net_read_varint(send_cursor);
        int packet_end = send_cursor->index + packet_size;

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
            unsigned char * compressed = alloc_in_arena(&temp_arena,
                    max_compressed_size);

            zstream.next_out = compressed;
            zstream.avail_out = max_compressed_size;

            if (deflate(&zstream, Z_FINISH) != Z_STREAM_END) {
                final_cursor->error = 1;
                break;
            }

            if (deflateEnd(&zstream) != Z_OK) {
                final_cursor->error = 1;
                break;
            }

            if (zstream.avail_in != 0) {
                final_cursor->error = 1;
                break;
            }

            net_write_varint(final_cursor, net_varint_size(packet_size) + zstream.total_out);
            net_write_varint(final_cursor, packet_size);
            net_write_data(final_cursor, compressed, zstream.total_out);
        } else {
            // @TODO(traks) should check somewhere that no error occurs
            net_write_data(final_cursor, send_cursor->data + packet_start,
                    packet_end - packet_start);
        }

        send_cursor->index = packet_end;
    }

    end_timed_block();

    if (final_cursor->error != 0) {
        // just disconnect the player
        logs("Failed to finalise packets");
        disconnect_player_now(player);
        goto bail;
    }

    begin_timed_block("send()");
    ssize_t send_size = send(player->player.sock, final_cursor->data,
            final_cursor->index, 0);
    end_timed_block();

    if (send_size == -1) {
        // EAGAIN means no data sent
        if (errno != EAGAIN) {
            logs_errno("Couldn't send protocol data: %s");
            disconnect_player_now(player);
        }
    } else {
        memmove(final_cursor->data, final_cursor->data + send_size,
                final_cursor->index - send_size);
        player->player.send_cursor = final_cursor->index - send_size;
    }

bail:
    end_timed_block();
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
