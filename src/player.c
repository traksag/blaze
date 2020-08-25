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
    SBP_CONTAINER_ACK,
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
    SBP_MOVE_PLAYER,
    SBP_MOVE_VEHICLE,
    SBP_PADDLE_BOAT,
    SBP_PICK_ITEM,
    SBP_PLACE_RECIPE,
    SBP_PLAYER_ABILITIES,
    SBP_PLAYER_ACTION,
    SBP_PLAYER_COMMAND,
    SBP_PLAYER_INPUT,
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
    CBP_COMMANDS,
    CBP_COMMAND_SUGGESTIONS,
    CBP_CONTAINER_ACK,
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
    CBP_MOVE_ENTITY,
    CBP_MOVE_VEHICLE,
    CBP_OPEN_BOOK,
    CBP_OPEN_SCREEN,
    CBP_OPEN_SIGN_EDITOR,
    CBP_PLACE_GHOST_RECIPE,
    CBP_PLAYER_ABILITIES,
    CBP_PLAYER_COMBAT,
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
    CBP_SELECT_ADVANCEMENTS,
    CBP_SET_BORDER,
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
    CBP_SET_TIME,
    CBP_SET_TITLES,
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
nbt_write_key(buffer_cursor * cursor, mc_ubyte tag, net_string key) {
    net_write_ubyte(cursor, tag);
    net_write_ushort(cursor, key.size);
    net_write_data(cursor, key.ptr, key.size);
}

static void
nbt_write_string(buffer_cursor * cursor, net_string val) {
    net_write_ushort(cursor, val.size);
    net_write_data(cursor, val.ptr, val.size);
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
    player->head_rot_x = new_rot_x;
    player->head_rot_y = new_rot_y;
}

static void
process_move_player_packet(entity_base * entity,
        double new_x, double new_y, double new_z,
        float new_head_rot_x, float new_head_rot_y, int on_ground) {
    if ((entity->flags & ENTITY_TELEPORTING) != 0) {
        return;
    }

    // @TODO(traks) if new x, y, z out of certain bounds, don't update entity
    // x, y, z to prevent NaN errors and extreme precision loss, etc.

    entity_player * player = &entity->player;
    entity->x = new_x;
    entity->y = new_y;
    entity->z = new_z;
    player->head_rot_x = new_head_rot_x;
    player->head_rot_y = new_head_rot_y;
    if (on_ground) {
        entity->flags |= ENTITY_ON_GROUND;
    } else {
        entity->flags &= ~ENTITY_ON_GROUND;
    }
}

static int
drop_item(entity_base * player, item_stack * is, server * serv) {
    entity_base * item = try_reserve_entity(serv, ENTITY_ITEM);
    if (item->type == ENTITY_NULL) {
        return 0;
    }

    item->x = player->x;
    // @TODO(traks) this has to depend on player's current pose
    item->y = player->y + 1.5;
    item->z = player->z;
    item->item.contents = *is;
    item->item.pickup_timeout = 40;

    float sin_rot_y = sinf(player->player.head_rot_y * RADIANS_PER_DEGREE);
    float cos_rot_y = cosf(player->player.head_rot_y * RADIANS_PER_DEGREE);
    float sin_rot_x = sinf(player->player.head_rot_x * RADIANS_PER_DEGREE);
    float cos_rot_x = cosf(player->player.head_rot_x * RADIANS_PER_DEGREE);

    // @TODO(traks) random offset
    item->item.vx = 0.3f * -sin_rot_y * cos_rot_x;
    item->item.vy = 0.3f * -sin_rot_x;
    item->item.vz = 0.3f * cos_rot_y * cos_rot_x;
    return 1;
}

static void
process_packet(entity_base * entity, buffer_cursor * rec_cursor,
        server * serv, memory_arena * process_arena) {
    // @NOTE(traks) we need to handle packets in the order in which they arive,
    // so e.g. the client can move the player to a position, perform some
    // action, and then move the player a bit further, all in the same tick.
    //
    // This seems the most natural way to do it, and gives us e.g. as much
    // information about the player's position when they perform a certain
    // action.

    entity_player * player = &entity->player;
    mc_int packet_id = net_read_varint(rec_cursor);

    switch (packet_id) {
    case SBP_ACCEPT_TELEPORT: {
        mc_int teleport_id = net_read_varint(rec_cursor);

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
        mc_int id = net_read_varint(rec_cursor);
        mc_ulong block_pos = net_read_ulong(rec_cursor);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_CHANGE_DIFFICULTY: {
        logs("Packet change difficulty");
        mc_ubyte difficulty = net_read_ubyte(rec_cursor);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_CHAT: {
        net_string chat = net_read_string(rec_cursor, 256);

        if (serv->global_msg_count < ARRAY_SIZE(serv->global_msgs)) {
            global_msg * msg = serv->global_msgs + serv->global_msg_count;
            serv->global_msg_count++;
            int text_size = sprintf(
                    (void *) msg->text, "<%.*s> %.*s",
                    (int) player->username_size,
                    player->username,
                    (int) chat.size, chat.ptr);
            msg->size = text_size;
        }
        break;
    }
    case SBP_CLIENT_COMMAND: {
        logs("Packet client command");
        mc_int action = net_read_varint(rec_cursor);
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
        net_string language = net_read_string(rec_cursor, 16);
        mc_ubyte view_distance = net_read_ubyte(rec_cursor);
        mc_int chat_visibility = net_read_varint(rec_cursor);
        mc_ubyte sees_chat_colours = net_read_ubyte(rec_cursor);
        mc_ubyte model_customisation = net_read_ubyte(rec_cursor);
        mc_int main_hand = net_read_varint(rec_cursor);

        // View distance is without the extra border of chunks,
        // while chunk cache radius is with the extra border of
        // chunks. This clamps the view distance between the minimum
        // of 2 and the server maximum.
        player->new_chunk_cache_radius = MIN(MAX(view_distance, 2),
                MAX_CHUNK_CACHE_RADIUS - 1) + 1;
        memcpy(player->language, language.ptr, language.size);
        player->language_size = language.size;
        player->sees_chat_colours = sees_chat_colours;
        player->model_customisation = model_customisation;
        player->main_hand = main_hand;
        break;
    }
    case SBP_COMMAND_SUGGESTION: {
        logs("Packet command suggestion");
        mc_int id = net_read_varint(rec_cursor);
        net_string command = net_read_string(rec_cursor, 32500);
        break;
    }
    case SBP_CONTAINER_ACK: {
        logs("Packet container ack");
        mc_ubyte container_id = net_read_ubyte(rec_cursor);
        mc_ushort uid = net_read_ushort(rec_cursor);
        mc_ubyte accepted = net_read_ubyte(rec_cursor);
        break;
    }
    case SBP_CONTAINER_BUTTON_CLICK: {
        logs("Packet container button click");
        mc_ubyte container_id = net_read_ubyte(rec_cursor);
        mc_ubyte button_id = net_read_ubyte(rec_cursor);
        break;
    }
    case SBP_CONTAINER_CLICK: {
        logs("Packet container click");
        mc_ubyte container_id = net_read_ubyte(rec_cursor);
        mc_ushort slot = net_read_ushort(rec_cursor);
        mc_ubyte button = net_read_ubyte(rec_cursor);
        mc_ushort uid = net_read_ushort(rec_cursor);
        mc_int click_type = net_read_varint(rec_cursor);

        // @NOTE(traks) the item is only used for comparing against some result
        // computed by the server
        mc_ubyte has_item = net_read_ubyte(rec_cursor);
        item_stack iss = {0};
        item_stack * is = &iss;

        if (has_item) {
            is->type = net_read_varint(rec_cursor);
            is->size = net_read_ubyte(rec_cursor);

            if (is->type < 0 || is->type >= ITEM_TYPE_COUNT || is->size == 0) {
                is->type = 0;
                // @TODO(traks) handle error (send slot updates?)
            }
            mc_ubyte max_size = get_max_stack_size(is->type);
            if (is->size > max_size) {
                is->size = max_size;
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
        mc_ubyte container_id = net_read_ubyte(rec_cursor);
        break;
    }
    case SBP_CUSTOM_PAYLOAD: {
        logs("Packet custom payload");
        net_string id = net_read_string(rec_cursor, 32767);
        unsigned char * payload = rec_cursor->buf + rec_cursor->index;
        mc_int payload_size = rec_cursor->limit - rec_cursor->index;

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
        mc_ubyte has_item = net_read_ubyte(rec_cursor);
        item_stack iss = {0};
        item_stack * is = &iss;

        if (has_item) {
            is->type = net_read_varint(rec_cursor);
            is->size = net_read_ubyte(rec_cursor);

            if (is->type < 0 || is->type >= ITEM_TYPE_COUNT || is->size == 0) {
                is->type = 0;
                // @TODO(traks) handle error
            }
            mc_ubyte max_size = get_max_stack_size(is->type);
            if (is->size > max_size) {
                is->size = max_size;
                // @TODO(traks) handle error
            }

            // @TODO(traks) better value than 64 for the max level
            nbt_tape_entry * tape = load_nbt(rec_cursor, process_arena, 64);
            if (rec_cursor->error) {
                break;
            }

            // @TODO(traks) use NBT data to construct item stack
        }

        mc_ubyte signing = net_read_ubyte(rec_cursor);
        mc_int hand = net_read_varint(rec_cursor);
        // @TODO(traks) handle the event
        break;
    }
    case SBP_ENTITY_TAG_QUERY: {
        logs("Packet entity tag query");
        mc_int transaction_id = net_read_varint(rec_cursor);
        mc_int entity_id = net_read_varint(rec_cursor);
        break;
    }
    case SBP_INTERACT: {
        logs("Packet interact");
        mc_int entity_id = net_read_varint(rec_cursor);
        mc_int action = net_read_varint(rec_cursor);

        switch (action) {
        case 0: { // interact
            mc_int hand = net_read_varint(rec_cursor);
            mc_ubyte secondary_action = net_read_ubyte(rec_cursor);
            // @TODO(traks) implement
            break;
        }
        case 1: { // attack
            mc_ubyte secondary_action = net_read_ubyte(rec_cursor);
            // @TODO(traks) implement
            break;
        }
        case 2: { // interact at
            float x = net_read_float(rec_cursor);
            float y = net_read_float(rec_cursor);
            float z = net_read_float(rec_cursor);
            mc_int hand = net_read_varint(rec_cursor);
            mc_ubyte secondary_action = net_read_ubyte(rec_cursor);
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
        net_block_pos block_pos = net_read_block_pos(rec_cursor);
        mc_int levels = net_read_varint(rec_cursor);
        mc_ubyte keep_jigsaws = net_read_ubyte(rec_cursor);
        // @TODO(traks) processing
        break;
    }
    case SBP_KEEP_ALIVE: {
        mc_ulong id = net_read_ulong(rec_cursor);
        if (player->last_keep_alive_sent_tick == id) {
            entity->flags |= PLAYER_GOT_ALIVE_RESPONSE;
        }
        break;
    }
    case SBP_LOCK_DIFFICULTY: {
        logs("Packet lock difficulty");
        mc_ubyte locked = net_read_ubyte(rec_cursor);
        break;
    }
    case SBP_MOVE_PLAYER_POS: {
        double x = net_read_double(rec_cursor);
        double y = net_read_double(rec_cursor);
        double z = net_read_double(rec_cursor);
        int on_ground = net_read_ubyte(rec_cursor);
        process_move_player_packet(entity, x, y, z,
                player->head_rot_x, player->head_rot_y, on_ground);
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
    case SBP_MOVE_PLAYER: {
        int on_ground = net_read_ubyte(rec_cursor);
        process_move_player_packet(entity,
                entity->x, entity->y, entity->z,
                player->head_rot_x, player->head_rot_y, on_ground);
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
        mc_ubyte left = net_read_ubyte(rec_cursor);
        mc_ubyte right = net_read_ubyte(rec_cursor);
        break;
    }
    case SBP_PICK_ITEM: {
        logs("Packet pick item");
        mc_int slot = net_read_varint(rec_cursor);
        break;
    }
    case SBP_PLACE_RECIPE: {
        logs("Packet place recipe");
        mc_ubyte container_id = net_read_ubyte(rec_cursor);
        net_string recipe = net_read_string(rec_cursor, 32767);
        mc_ubyte shift_down = net_read_ubyte(rec_cursor);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_PLAYER_ABILITIES: {
        logs("Packet player abilities");
        mc_ubyte flags = net_read_ubyte(rec_cursor);
        mc_ubyte flying = flags & 0x2;
        // @TODO(traks) process packet
        break;
    }
    case SBP_PLAYER_ACTION: {
        mc_int action = net_read_varint(rec_cursor);
        // @TODO(traks) validate block pos inside world
        net_block_pos block_pos = net_read_block_pos(rec_cursor);
        mc_ubyte direction = net_read_ubyte(rec_cursor);

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

                int in_chunk_x = block_pos.x & 0xf;
                int in_chunk_z = block_pos.z & 0xf;
                chunk_set_block_state(ch, in_chunk_x, block_pos.y,
                        in_chunk_z, 0);
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
                if (drop_item(entity, is, serv)) {
                    is->size = 0;
                    is->type = ITEM_AIR;
                } else {
                    player->slots_needing_update |= (mc_ulong) 1 << sel_slot;
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
                int size = is->size;
                is->size = 1;
                if (drop_item(entity, is, serv)) {
                    is->size = size - 1;
                    if (is->size == 0) {
                        is->type = ITEM_AIR;
                    }
                } else {
                    player->slots_needing_update |= (mc_ulong) 1 << sel_slot;
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
            player->slots_needing_update |= (mc_ulong) 1 << sel_slot;
            player->slots_needing_update |= (mc_ulong) 1 << PLAYER_OFF_HAND_SLOT;
            break;
        }
        default:
            rec_cursor->error = 1;
        }
        break;
    }
    case SBP_PLAYER_COMMAND: {
        mc_int id = net_read_varint(rec_cursor);
        mc_int action = net_read_varint(rec_cursor);
        mc_int data = net_read_varint(rec_cursor);

        switch (action) {
        case 0: // press shift key
            entity->flags |= PLAYER_SHIFTING;
            break;
        case 1: // release shift key
            entity->flags &= ~PLAYER_SHIFTING;
            break;
        case 2: // stop sleeping
            // @TODO(traks)
            break;
        case 3: // start sprinting
            entity->flags |= PLAYER_SPRINTING;
            break;
        case 4: // stop sprinting
            entity->flags &= ~PLAYER_SPRINTING;
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
        net_string name = net_read_string(rec_cursor, 32767);
        break;
    }
    case SBP_RESOURCE_PACK: {
        logs("Packet resource pack");
        mc_int action = net_read_varint(rec_cursor);
        break;
    }
    case SBP_SEEN_ADVANCEMENTS: {
        logs("Packet seen advancements");
        mc_int action = net_read_varint(rec_cursor);
        // @TODO(traks) further processing
        break;
    }
    case SBP_SELECT_TRADE: {
        logs("Packet select trade");
        mc_int item = net_read_varint(rec_cursor);
        break;
    }
    case SBP_SET_BEACON: {
        logs("Packet set beacon");
        mc_int primary_effect = net_read_varint(rec_cursor);
        mc_int secondary_effect = net_read_varint(rec_cursor);
        break;
    }
    case SBP_SET_CARRIED_ITEM: {
        mc_ushort slot = net_read_ushort(rec_cursor);
        if (slot > PLAYER_LAST_HOTBAR_SLOT - PLAYER_FIRST_HOTBAR_SLOT) {
            rec_cursor->error = 1;
            break;
        }
        player->selected_slot = PLAYER_FIRST_HOTBAR_SLOT + slot;
        break;
    }
    case SBP_SET_COMMAND_BLOCK: {
        logs("Packet set command block");
        mc_ulong block_pos = net_read_ulong(rec_cursor);
        net_string command = net_read_string(rec_cursor, 32767);
        mc_int mode = net_read_varint(rec_cursor);
        mc_ubyte flags = net_read_ubyte(rec_cursor);
        mc_ubyte track_output = (flags & 0x1);
        mc_ubyte conditional = (flags & 0x2);
        mc_ubyte automatic = (flags & 0x4);
        break;
    }
    case SBP_SET_COMMAND_MINECART: {
        logs("Packet set command minecart");
        mc_int entity_id = net_read_varint(rec_cursor);
        net_string command = net_read_string(rec_cursor, 32767);
        mc_ubyte track_output = net_read_ubyte(rec_cursor);
        break;
    }
    case SBP_SET_CREATIVE_MODE_SLOT: {
        mc_ushort slot = net_read_ushort(rec_cursor);
        mc_ubyte has_item = net_read_ubyte(rec_cursor);

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
                        (mc_ulong) 1 << slot;
            }

            net_string type_name = get_resource_loc(is->type, &serv->item_resource_table);
            logs("Set creative slot: %.*s", (int) type_name.size, type_name.ptr);

            mc_ubyte max_size = get_max_stack_size(is->type);
            if (is->size > max_size) {
                is->size = max_size;
                player->slots_needing_update |=
                        (mc_ulong) 1 << slot;
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
        mc_ulong block_pos = net_read_ulong(rec_cursor);
        net_string name = net_read_string(rec_cursor, 32767);
        net_string target = net_read_string(rec_cursor, 32767);
        net_string pool = net_read_string(rec_cursor, 32767);
        net_string final_state = net_read_string(rec_cursor, 32767);
        net_string joint = net_read_string(rec_cursor, 32767);
        // @TODO(traks) handle packet
        break;
    }
    case SBP_SET_STRUCTURE_BLOCK: {
        logs("Packet set structure block");
        mc_ulong block_pos = net_read_ulong(rec_cursor);
        mc_int update_type = net_read_varint(rec_cursor);
        mc_int mode = net_read_varint(rec_cursor);
        net_string name = net_read_string(rec_cursor, 32767);
        // @TODO(traks) read signed bytes instead
        mc_ubyte offset_x = net_read_ubyte(rec_cursor);
        mc_ubyte offset_y = net_read_ubyte(rec_cursor);
        mc_ubyte offset_z = net_read_ubyte(rec_cursor);
        mc_ubyte size_x = net_read_ubyte(rec_cursor);
        mc_ubyte size_y = net_read_ubyte(rec_cursor);
        mc_ubyte size_z = net_read_ubyte(rec_cursor);
        mc_int mirror = net_read_varint(rec_cursor);
        mc_int rotation = net_read_varint(rec_cursor);
        net_string data = net_read_string(rec_cursor, 12);
        // @TODO(traks) further reading
        break;
    }
    case SBP_SIGN_UPDATE: {
        logs("Packet sign update");
        mc_ulong block_pos = net_read_ulong(rec_cursor);
        net_string lines[4];
        for (int i = 0; i < ARRAY_SIZE(lines); i++) {
            lines[i] = net_read_string(rec_cursor, 384);
        }
        break;
    }
    case SBP_SWING: {
        // logs("Packet swing");
        mc_int hand = net_read_varint(rec_cursor);
        break;
    }
    case SBP_TELEPORT_TO_ENTITY: {
        logs("Packet teleport to entity");
        // @TODO(traks) read UUID instead
        mc_ulong uuid_high = net_read_ulong(rec_cursor);
        mc_ulong uuid_low = net_read_ulong(rec_cursor);
        break;
    }
    case SBP_USE_ITEM_ON: {
        logs("Packet use item on");
        mc_int hand = net_read_varint(rec_cursor);
        net_block_pos clicked_pos = net_read_block_pos(rec_cursor);
        mc_int clicked_face = net_read_varint(rec_cursor);
        float click_offset_x = net_read_float(rec_cursor);
        float click_offset_y = net_read_float(rec_cursor);
        float click_offset_z = net_read_float(rec_cursor);
        // @TODO(traks) figure out what this is used for
        mc_ubyte is_inside = net_read_ubyte(rec_cursor);

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

        process_use_item_on_packet(serv, entity, hand, clicked_pos,
                clicked_face, click_offset_x, click_offset_y, click_offset_z,
                is_inside);
        break;
    }
    case SBP_USE_ITEM: {
        logs("Packet use item");
        mc_int hand = net_read_varint(rec_cursor);
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
begin_packet(buffer_cursor * send_cursor, mc_int id) {
    // skip some bytes for packet size varint at the start
    send_cursor->index = 5;
    net_write_varint(send_cursor, id);
}

static void
finish_packet_and_send(buffer_cursor * send_cursor, entity_base * entity,
        memory_arena * scratch_arena) {
    // We use the written data to determine the packet size instead of
    // calculating the packet size up front. The major benefit is that
    // calculating the packet size up front is very error prone and requires a
    // lot of maintainance (in case of packet format changes).
    //
    // The downside is that we have to copy all packet data an additional time,
    // because Mojang decided to encode packet sizes with a variable-size
    // encoding.

    // @TODO(traks) instead of copying the packet to the send buffer each time,
    // maybe write all packets to a separate buffer, then copy all packets at
    // once to the send buffer
    mc_int packet_size = send_cursor->index - 5;
    mc_int start_index = 5 - net_varint_size(packet_size);
    send_cursor->index = start_index;
    net_write_varint(send_cursor, packet_size);

    entity_player * player = &entity->player;

    buffer_cursor packet_cursor = {
        .buf = player->send_buf,
        .limit = player->send_buf_size,
        .index = player->send_cursor
    };

    if (entity->flags & PLAYER_PACKET_COMPRESSION) {
        // @TODO(traks) handle errors properly

        z_stream zstream;
        zstream.zalloc = Z_NULL;
        zstream.zfree = Z_NULL;
        zstream.opaque = Z_NULL;

        if (deflateInit2(&zstream, Z_BEST_COMPRESSION,
                Z_DEFLATED, 15, 9, Z_DEFAULT_STRATEGY) != Z_OK) {
            logs("deflateInit failed");
            return;
        }

        zstream.next_in = send_cursor->buf + send_cursor->index;
        zstream.avail_in = send_cursor->limit - send_cursor->index;

        memory_arena temp_arena = *scratch_arena;
        // @TODO(traks) appropriate value
        size_t max_compressed_size = 1 << 19;
        unsigned char * compressed = alloc_in_arena(&temp_arena,
                max_compressed_size);

        zstream.next_out = compressed;
        zstream.avail_out = max_compressed_size;

        if (deflate(&zstream, Z_FINISH) != Z_STREAM_END) {
            logs("Failed to finish deflating packet: %s", zstream.msg);
            return;
        }

        if (deflateEnd(&zstream) != Z_OK) {
            logs("deflateEnd failed");
            return;
        }

        if (zstream.avail_in != 0) {
            logs("Didn't deflate entire packet");
            return;
        }

        net_write_varint(&packet_cursor, net_varint_size(packet_size) + zstream.total_out);
        net_write_varint(&packet_cursor, packet_size);
        net_write_data(&packet_cursor, compressed, zstream.total_out);
        player->send_cursor = packet_cursor.index;
    } else {
        // @TODO(traks) should check somewhere that no error occurs
        net_write_data(&packet_cursor, send_cursor->buf + start_index,
                packet_size + 5 - start_index);
        player->send_cursor = packet_cursor.index;
    }
}

static void
send_chunk_fully(buffer_cursor * send_cursor, chunk_pos pos, chunk * ch,
        entity_base * entity, memory_arena * tick_arena) {
    begin_timed_block("send chunk fully");

    // bit mask for included chunk sections; bottom section in least
    // significant bit
    mc_ushort section_mask = 0;
    for (int i = 0; i < 16; i++) {
        if (ch->sections[i] != NULL) {
            section_mask |= 1 << i;
        }
    }

    // calculate total size of chunk section data
    mc_int section_data_size = 0;
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
    net_write_ubyte(send_cursor, 1); // full chunk
    net_write_varint(send_cursor, section_mask);

    // height map NBT
    nbt_write_key(send_cursor, NBT_TAG_COMPOUND, NET_STRING(""));

    nbt_write_key(send_cursor, NBT_TAG_LONG_ARRAY, NET_STRING("MOTION_BLOCKING"));
    // number of elements in long array
    net_write_int(send_cursor, 36);
    mc_ulong compacted_map[36] = {0};

    int shift = 0;

    for (int z = 0; z < 16; z++) {
        for (int x = 0; x < 16; x++) {
            mc_ulong height = ch->motion_blocking_height_map[(z << 4) | x];
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
        mc_ulong val = 0;
        int offset = 0;

        for (int j = 0; j < 16 * 16 * 16; j++) {
            mc_ulong block_state = section->block_states[j];
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
    finish_packet_and_send(send_cursor, entity, tick_arena);

    end_timed_block();
}

static void
send_light_update(buffer_cursor * send_cursor, chunk_pos pos, chunk * ch,
        entity_base * entity, memory_arena * tick_arena) {
    // There are 18 chunk sections from 1 section below the world to 1 section
    // above the world. The lowest chunk section comes first (and is the least
    // significant bit).

    begin_timed_block("send light update");

    // @TODO(traks) send the real lighting data

    // light sections present as arrays in this packet
    mc_int sky_light_mask = 0x3ffff;
    mc_int block_light_mask = 0x3ffff;
    // sections with all light values equal to 0
    mc_int zero_sky_light_mask = 0;
    mc_int zero_block_light_mask = 0;

    begin_packet(send_cursor, CBP_LIGHT_UPDATE);
    net_write_varint(send_cursor, pos.x);
    net_write_varint(send_cursor, pos.z);
    net_write_ubyte(send_cursor, 1); // trust edges
    net_write_varint(send_cursor, sky_light_mask);
    net_write_varint(send_cursor, block_light_mask);
    net_write_varint(send_cursor, zero_sky_light_mask);
    net_write_varint(send_cursor, zero_block_light_mask);

    for (int i = 0; i < 18; i++) {
        net_write_varint(send_cursor, 2048);
        for (int j = 0; j < 4096; j += 2) {
            mc_ubyte light = 0xff;
            net_write_ubyte(send_cursor, light);
        }
    }

    for (int i = 0; i < 18; i++) {
        net_write_varint(send_cursor, 2048);
        for (int j = 0; j < 4096; j += 2) {
            mc_ubyte light = 0;
            net_write_ubyte(send_cursor, light);
        }
    }
    finish_packet_and_send(send_cursor, entity, tick_arena);

    end_timed_block();
}

static void
disconnect_player_now(entity_base * entity, server * serv) {
    entity_player * player = &entity->player;
    close(player->sock);

    mc_short chunk_cache_min_x = player->chunk_cache_centre_x - player->chunk_cache_radius;
    mc_short chunk_cache_max_x = player->chunk_cache_centre_x + player->chunk_cache_radius;
    mc_short chunk_cache_min_z = player->chunk_cache_centre_z - player->chunk_cache_radius;
    mc_short chunk_cache_max_z = player->chunk_cache_centre_z + player->chunk_cache_radius;

    for (mc_short x = chunk_cache_min_x; x <= chunk_cache_max_x; x++) {
        for (mc_short z = chunk_cache_min_z; z <= chunk_cache_max_z; z++) {
            chunk_pos pos = {.x = x, .z = z};
            chunk * ch = get_chunk_if_available(pos);
            assert(ch != NULL);
            ch->available_interest--;
        }
    }

    free(player->rec_buf);
    free(player->send_buf);

    evict_entity(serv, entity->eid);
}

static float
degree_diff(float to, float from) {
    float div = (to - from) / 360 + 0.5f;
    float mod = div - floor(div);
    float res = mod * 360 - 180;
    return res;
}

void
tick_player(entity_base * entity, server * serv,
        memory_arena * tick_arena) {
    begin_timed_block("tick player");

    entity_player * player = &entity->player;
    assert(entity->type == ENTITY_PLAYER);
    int sock = player->sock;
    ssize_t rec_size = recv(sock, player->rec_buf + player->rec_cursor,
            player->rec_buf_size - player->rec_cursor, 0);

    if (rec_size == 0) {
        disconnect_player_now(entity, serv);
    } else if (rec_size == -1) {
        // EAGAIN means no data received
        if (errno != EAGAIN) {
            logs_errno("Couldn't receive protocol data: %s");
            disconnect_player_now(entity, serv);
        }
    } else {
        player->rec_cursor += rec_size;

        buffer_cursor rec_cursor = {
            .buf = player->rec_buf,
            .limit = player->rec_cursor
        };

        // @TODO(traks) rate limit incoming packets per player

        for (;;) {
            buffer_cursor packet_cursor = rec_cursor;
            mc_int packet_size = net_read_varint(&packet_cursor);

            if (packet_cursor.error != 0) {
                // packet size not fully received yet
                break;
            }
            if (packet_size > player->rec_buf_size - 5 || packet_size <= 0) {
                disconnect_player_now(entity, serv);
                break;
            }
            if (packet_size > packet_cursor.limit - packet_cursor.index) {
                // packet not fully received yet
                break;
            }

            memory_arena process_arena = *tick_arena;
            packet_cursor.limit = packet_cursor.index + packet_size;
            rec_cursor.index = packet_cursor.limit;

            if (entity->flags & PLAYER_PACKET_COMPRESSION) {
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
                    disconnect_player_now(entity, serv);
                    break;
                }

                zstream.next_in = packet_cursor.buf + packet_cursor.index;
                zstream.avail_in = packet_cursor.limit - packet_cursor.index;

                size_t max_uncompressed_size = 2 * (1 << 20);
                unsigned char * uncompressed = alloc_in_arena(&process_arena,
                        max_uncompressed_size);

                zstream.next_out = uncompressed;
                zstream.avail_out = max_uncompressed_size;

                if (inflate(&zstream, Z_FINISH) != Z_STREAM_END) {
                    logs("Failed to finish inflating packet: %s", zstream.msg);
                    disconnect_player_now(entity, serv);
                    break;
                }

                if (inflateEnd(&zstream) != Z_OK) {
                    logs("inflateEnd failed");
                    disconnect_player_now(entity, serv);
                    break;
                }

                if (zstream.avail_in != 0) {
                    logs("Didn't inflate entire packet");
                    disconnect_player_now(entity, serv);
                    break;
                }

                packet_cursor = (buffer_cursor) {
                    .buf = uncompressed,
                    .limit = zstream.total_out,
                };
            }

            process_packet(entity, &packet_cursor, serv, &process_arena);

            if (packet_cursor.error != 0) {
                logs("Player protocol error occurred");
                disconnect_player_now(entity, serv);
                break;
            }

            if (packet_cursor.index != packet_cursor.limit) {
                logs("Player protocol packet not fully read");
                disconnect_player_now(entity, serv);
                break;
            }
        }

        memmove(rec_cursor.buf, rec_cursor.buf + rec_cursor.index,
                rec_cursor.limit - rec_cursor.index);
        player->rec_cursor = rec_cursor.limit - rec_cursor.index;
    }

    // @TODO(traks) only here because players could be disconnected and get
    // all their data cleaned up immediately if some packet handling error
    // occurs above. Eventually we should handle errors more gracefully.
    // Then this check shouldn't be necessary anymore.
    if (!(entity->flags & ENTITY_IN_USE)) {
        goto bail;
    }

bail:
    end_timed_block();
}

static void
nbt_write_dimension_type(buffer_cursor * send_cursor,
        dimension_type * dim_type) {
    if (dim_type->fixed_time != -1) {
        nbt_write_key(send_cursor, NBT_TAG_INT, NET_STRING("fixed_time"));
        net_write_int(send_cursor, dim_type->fixed_time);
    }

    nbt_write_key(send_cursor, NBT_TAG_BYTE, NET_STRING("has_skylight"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_HAS_SKYLIGHT));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, NET_STRING("has_ceiling"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_HAS_CEILING));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, NET_STRING("ultrawarm"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_ULTRAWARM));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, NET_STRING("natural"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_NATURAL));

    nbt_write_key(send_cursor, NBT_TAG_DOUBLE, NET_STRING("coordinate_scale"));
    net_write_double(send_cursor, dim_type->coordinate_scale);

    nbt_write_key(send_cursor, NBT_TAG_BYTE, NET_STRING("piglin_safe"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_PIGLIN_SAFE));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, NET_STRING("bed_works"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_BED_WORKS));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, NET_STRING("respawn_anchor_works"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_RESPAWN_ANCHOR_WORKS));

    nbt_write_key(send_cursor, NBT_TAG_BYTE, NET_STRING("has_raids"));
    net_write_ubyte(send_cursor, !!(dim_type->flags & DIMENSION_HAS_RAIDS));

    nbt_write_key(send_cursor, NBT_TAG_INT, NET_STRING("logical_height"));
    net_write_int(send_cursor, dim_type->logical_height);

    nbt_write_key(send_cursor, NBT_TAG_STRING, NET_STRING("infiniburn"));
    net_string infiniburn = {
        .ptr = dim_type->infiniburn,
        .size = dim_type->infiniburn_size
    };
    nbt_write_string(send_cursor, infiniburn);

    nbt_write_key(send_cursor, NBT_TAG_STRING, NET_STRING("effects"));
    net_string effects = {
        .ptr = dim_type->effects,
        .size = dim_type->effects_size
    };
    nbt_write_string(send_cursor, effects);

    nbt_write_key(send_cursor, NBT_TAG_FLOAT, NET_STRING("ambient_light"));
    net_write_float(send_cursor, dim_type->ambient_light);
}

static void
nbt_write_biome(buffer_cursor * send_cursor, biome * b) {
    nbt_write_key(send_cursor, NBT_TAG_STRING, NET_STRING("precipitation"));
    switch (b->precipitation) {
    case BIOME_PRECIPITATION_NONE:
        nbt_write_string(send_cursor, NET_STRING("none"));
        break;
    case BIOME_PRECIPITATION_RAIN:
        nbt_write_string(send_cursor, NET_STRING("rain"));
        break;
    case BIOME_PRECIPITATION_SNOW:
        nbt_write_string(send_cursor, NET_STRING("snow"));
        break;
    default:
        assert(0);
    }

    nbt_write_key(send_cursor, NBT_TAG_FLOAT, NET_STRING("temperature"));
    net_write_float(send_cursor, b->temperature);

    if (b->temperature_mod != BIOME_TEMPERATURE_MOD_NONE) {
        nbt_write_key(send_cursor, NBT_TAG_STRING, NET_STRING("temperature_modifier"));
        switch (b->temperature_mod) {
        case BIOME_TEMPERATURE_MOD_FROZEN:
            nbt_write_string(send_cursor, NET_STRING("frozen"));
            break;
        default:
            assert(0);
        }
    }

    nbt_write_key(send_cursor, NBT_TAG_FLOAT, NET_STRING("downfall"));
    net_write_float(send_cursor, b->downfall);

    nbt_write_key(send_cursor, NBT_TAG_STRING, NET_STRING("category"));
    switch (b->category) {
    case BIOME_CATEGORY_NONE:
        nbt_write_string(send_cursor, NET_STRING("none"));
        break;
    case BIOME_CATEGORY_TAIGA:
        nbt_write_string(send_cursor, NET_STRING("taiga"));
        break;
    case BIOME_CATEGORY_EXTREME_HILLS:
        nbt_write_string(send_cursor, NET_STRING("extreme_hills"));
        break;
    case BIOME_CATEGORY_JUNGLE:
        nbt_write_string(send_cursor, NET_STRING("jungle"));
        break;
    case BIOME_CATEGORY_MESA:
        nbt_write_string(send_cursor, NET_STRING("mesa"));
        break;
    case BIOME_CATEGORY_PLAINS:
        nbt_write_string(send_cursor, NET_STRING("plains"));
        break;
    case BIOME_CATEGORY_SAVANNA:
        nbt_write_string(send_cursor, NET_STRING("savanna"));
        break;
    case BIOME_CATEGORY_ICY:
        nbt_write_string(send_cursor, NET_STRING("icy"));
        break;
    case BIOME_CATEGORY_THE_END:
        nbt_write_string(send_cursor, NET_STRING("the_end"));
        break;
    case BIOME_CATEGORY_BEACH:
        nbt_write_string(send_cursor, NET_STRING("beach"));
        break;
    case BIOME_CATEGORY_FOREST:
        nbt_write_string(send_cursor, NET_STRING("forest"));
        break;
    case BIOME_CATEGORY_OCEAN:
        nbt_write_string(send_cursor, NET_STRING("ocean"));
        break;
    case BIOME_CATEGORY_DESERT:
        nbt_write_string(send_cursor, NET_STRING("desert"));
        break;
    case BIOME_CATEGORY_RIVER:
        nbt_write_string(send_cursor, NET_STRING("river"));
        break;
    case BIOME_CATEGORY_SWAMP:
        nbt_write_string(send_cursor, NET_STRING("swamp"));
        break;
    case BIOME_CATEGORY_MUSHROOM:
        nbt_write_string(send_cursor, NET_STRING("mushroom"));
        break;
    case BIOME_CATEGORY_NETHER:
        nbt_write_string(send_cursor, NET_STRING("nether"));
        break;
    }

    nbt_write_key(send_cursor, NBT_TAG_FLOAT, NET_STRING("depth"));
    net_write_float(send_cursor, b->depth);

    nbt_write_key(send_cursor, NBT_TAG_FLOAT, NET_STRING("scale"));
    net_write_float(send_cursor, b->scale);

    // special effects
    nbt_write_key(send_cursor, NBT_TAG_COMPOUND, NET_STRING("effects"));

    nbt_write_key(send_cursor, NBT_TAG_INT, NET_STRING("fog_color"));
    net_write_int(send_cursor, b->fog_colour);

    nbt_write_key(send_cursor, NBT_TAG_INT, NET_STRING("water_color"));
    net_write_int(send_cursor, b->water_colour);

    nbt_write_key(send_cursor, NBT_TAG_INT, NET_STRING("water_fog_color"));
    net_write_int(send_cursor, b->water_fog_colour);

    nbt_write_key(send_cursor, NBT_TAG_INT, NET_STRING("sky_color"));
    net_write_int(send_cursor, b->sky_colour);

    if (b->foliage_colour_override != -1) {
        nbt_write_key(send_cursor, NBT_TAG_INT, NET_STRING("foliage_color"));
        net_write_int(send_cursor, b->foliage_colour_override);
    }

    if (b->grass_colour_override != -1) {
        nbt_write_key(send_cursor, NBT_TAG_INT, NET_STRING("grass_color"));
        net_write_int(send_cursor, b->grass_colour_override);
    }

    if (b->grass_colour_mod != BIOME_GRASS_COLOUR_MOD_NONE) {
        nbt_write_key(send_cursor, NBT_TAG_STRING, NET_STRING("grass_color_modifier"));
        switch (b->grass_colour_mod) {
        case BIOME_GRASS_COLOUR_MOD_DARK_FOREST:
            nbt_write_string(send_cursor, NET_STRING("dark_forest"));
            break;
        case BIOME_GRASS_COLOUR_MOD_SWAMP:
            nbt_write_string(send_cursor, NET_STRING("swamp"));
            break;
        default:
            assert(0);
        }
    }

    // @TODO(traks) ambient particle effects

    if (b->ambient_sound_size > 0) {
        nbt_write_key(send_cursor, NBT_TAG_STRING, NET_STRING("ambient_sound"));
        net_string ambient_sound = {
            .ptr = b->ambient_sound,
            .size = b->ambient_sound_size
        };
        nbt_write_string(send_cursor, ambient_sound);
    }

    if (b->mood_sound_size > 0) {
        // mood sound
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, NET_STRING("mood_sound"));

        nbt_write_key(send_cursor, NBT_TAG_STRING, NET_STRING("sound"));
        net_string mood_sound = {
            .ptr = b->mood_sound,
            .size = b->mood_sound_size
        };
        nbt_write_string(send_cursor, mood_sound);

        nbt_write_key(send_cursor, NBT_TAG_INT, NET_STRING("tick_delay"));
        net_write_int(send_cursor, b->mood_sound_tick_delay);

        nbt_write_key(send_cursor, NBT_TAG_INT, NET_STRING("block_search_extent"));
        net_write_int(send_cursor, b->mood_sound_block_search_extent);

        nbt_write_key(send_cursor, NBT_TAG_DOUBLE, NET_STRING("offset"));
        net_write_int(send_cursor, b->mood_sound_offset);

        net_write_ubyte(send_cursor, NBT_TAG_END);
        // mood sound end
    }

    if (b->additions_sound_size > 0) {
        // additions sound
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, NET_STRING("additions_sound"));

        nbt_write_key(send_cursor, NBT_TAG_STRING, NET_STRING("sound"));
        net_string additions_sound = {
            .ptr = b->additions_sound,
            .size = b->additions_sound_size
        };
        nbt_write_string(send_cursor, additions_sound);

        nbt_write_key(send_cursor, NBT_TAG_DOUBLE, NET_STRING("tick_chance"));
        net_write_int(send_cursor, b->additions_sound_tick_chance);

        net_write_ubyte(send_cursor, NBT_TAG_END);
        // additions sound end
    }

    if (b->music_sound_size > 0) {
        // music
        nbt_write_key(send_cursor, NBT_TAG_COMPOUND, NET_STRING("music"));

        nbt_write_key(send_cursor, NBT_TAG_STRING, NET_STRING("sound"));
        net_string music_sound = {
            .ptr = b->music_sound,
            .size = b->music_sound_size
        };
        nbt_write_string(send_cursor, music_sound);

        nbt_write_key(send_cursor, NBT_TAG_INT, NET_STRING("min_delay"));
        net_write_int(send_cursor, b->music_min_delay);

        nbt_write_key(send_cursor, NBT_TAG_INT, NET_STRING("max_delay"));
        net_write_int(send_cursor, b->music_max_delay);

        nbt_write_key(send_cursor, NBT_TAG_BYTE, NET_STRING("replace_current_music"));
        net_write_ubyte(send_cursor, b->music_replace_current_music);

        net_write_ubyte(send_cursor, NBT_TAG_END);
        // music end
    }

    net_write_ubyte(send_cursor, NBT_TAG_END);
    // special effects end
}

static void
send_tracked_entity_packets(entity_base * player, server * serv,
        buffer_cursor * send_cursor, memory_arena * tick_arena,
        entity_base * tracked) {
    float rot_x = 0;
    float rot_y = 0;

    switch (tracked->type) {
    case ENTITY_PLAYER:
        rot_x = tracked->player.head_rot_x;
        rot_y = tracked->player.head_rot_y;

        begin_packet(send_cursor, CBP_ROTATE_HEAD);
        net_write_varint(send_cursor, tracked->eid);
        net_write_ubyte(send_cursor,
                (int) (tracked->player.head_rot_y * 256.0f / 360.0f) & 0xff);
        finish_packet_and_send(send_cursor, player, tick_arena);
        break;
    }

    begin_packet(send_cursor, CBP_TELEPORT_ENTITY);
    net_write_varint(send_cursor, tracked->eid);
    net_write_double(send_cursor, tracked->x);
    net_write_double(send_cursor, tracked->y);
    net_write_double(send_cursor, tracked->z);
    net_write_ubyte(send_cursor,
            (int) (rot_y * 256.0f / 360.0f) & 0xff);
    net_write_ubyte(send_cursor,
            (int) (rot_x * 256.0f / 360.0f) & 0xff);
    net_write_ubyte(send_cursor, !!(tracked->flags & ENTITY_ON_GROUND));
    finish_packet_and_send(send_cursor, player, tick_arena);

    // if (tracked->type == ENTITY_ITEM) {
    //     begin_packet(send_cursor, CBP_SET_ENTITY_MOTION);
    //     net_write_varint(send_cursor, tracked->eid);
    //     net_write_short(send_cursor, CLAMP(tracked->item.vx, -3.9, 3.9) * 8000);
    //     net_write_short(send_cursor, CLAMP(tracked->item.vy, -3.9, 3.9) * 8000);
    //     net_write_short(send_cursor, CLAMP(tracked->item.vz, -3.9, 3.9) * 8000);
    //     finish_packet_and_send(send_cursor, player, tick_arena);
    // }
}

static void
send_add_entity_packet(entity_base * player, server * serv,
        buffer_cursor * send_cursor, memory_arena * tick_arena,
        entity_base * tracked) {
    switch (tracked->type) {
    case ENTITY_PLAYER: {
        begin_packet(send_cursor, CBP_ADD_PLAYER);
        net_write_varint(send_cursor, tracked->eid);
        // @TODO(traks) appropriate UUID
        net_write_ulong(send_cursor, 0);
        net_write_ulong(send_cursor, tracked->eid);
        net_write_double(send_cursor, tracked->x);
        net_write_double(send_cursor, tracked->y);
        net_write_double(send_cursor, tracked->z);
        net_write_ubyte(send_cursor,
                (int) (tracked->player.head_rot_y * 256.0f / 360.0f) & 0xff);
        net_write_ubyte(send_cursor,
                (int) (tracked->player.head_rot_x * 256.0f / 360.0f) & 0xff);
        finish_packet_and_send(send_cursor, player, tick_arena);

        begin_packet(send_cursor, CBP_ROTATE_HEAD);
        net_write_varint(send_cursor, tracked->eid);
        net_write_ubyte(send_cursor,
                (int) (tracked->player.head_rot_y * 256.0f / 360.0f) & 0xff);
        finish_packet_and_send(send_cursor, player, tick_arena);
        break;
    }
    case ENTITY_ITEM: {
        // begin_packet(send_cursor, CBP_ADD_MOB);
        // net_write_varint(send_cursor, tracked->eid);
        // // @TODO(traks) appropriate UUID
        // net_write_ulong(send_cursor, 0);
        // net_write_ulong(send_cursor, 0);
        // net_write_varint(send_cursor, ENTITY_SQUID);
        // net_write_double(send_cursor, tracked->x);
        // net_write_double(send_cursor, tracked->y);
        // net_write_double(send_cursor, tracked->z);
        // net_write_ubyte(send_cursor, 0);
        // net_write_ubyte(send_cursor, 0);
        // // @TODO(traks) y head rotation (what is that?)
        // net_write_ubyte(send_cursor, 0);
        // // @TODO(traks) entity velocity
        // net_write_short(send_cursor, 0);
        // net_write_short(send_cursor, 0);
        // net_write_short(send_cursor, 0);
        // finish_packet_and_send(send_cursor, player, tick_arena);
        begin_packet(send_cursor, CBP_ADD_ENTITY);
        net_write_varint(send_cursor, tracked->eid);
        // @TODO(traks) appropriate UUID
        net_write_ulong(send_cursor, 0);
        net_write_ulong(send_cursor, tracked->eid);
        net_write_varint(send_cursor, tracked->type);
        net_write_double(send_cursor, tracked->x);
        net_write_double(send_cursor, tracked->y);
        net_write_double(send_cursor, tracked->z);
        // @TODO(traks) x and y rotation
        net_write_ubyte(send_cursor, 0);
        net_write_ubyte(send_cursor, 0);
        // @TODO(traks) player data
        net_write_int(send_cursor, 0);
        net_write_short(send_cursor, CLAMP(tracked->item.vx, -3.9, 3.9) * 8000);
        net_write_short(send_cursor, CLAMP(tracked->item.vy, -3.9, 3.9) * 8000);
        net_write_short(send_cursor, CLAMP(tracked->item.vz, -3.9, 3.9) * 8000);
        finish_packet_and_send(send_cursor, player, tick_arena);

        begin_packet(send_cursor, CBP_SET_ENTITY_DATA);
        net_write_varint(send_cursor, tracked->eid);

        net_write_ubyte(send_cursor, 7); // set item contents
        net_write_varint(send_cursor, 6); // data type: item stack

        net_write_ubyte(send_cursor, 1); // has item
        item_stack * is = &tracked->item.contents;
        net_write_varint(send_cursor, is->type);
        net_write_ubyte(send_cursor, is->size);
        // @TODO(traks) write NBT (currently just a single end tag)
        net_write_ubyte(send_cursor, 0);

        net_write_ubyte(send_cursor, 0xff); // end of entity data
        finish_packet_and_send(send_cursor, player, tick_arena);
        break;
    }
    }
}

void
send_packets_to_player(entity_base * entity, server * serv,
        memory_arena * tick_arena) {
    begin_timed_block("send packets");

    entity_player * player = &entity->player;
    size_t max_uncompressed_packet_size = 1 << 20;
    buffer_cursor send_cursor = {
        .buf = alloc_in_arena(tick_arena, max_uncompressed_packet_size),
        .limit = max_uncompressed_packet_size
    };

    if (!(entity->flags & PLAYER_DID_INIT_PACKETS)) {
        entity->flags |= PLAYER_DID_INIT_PACKETS;

        if (PACKET_COMPRESSION_ENABLED) {
            // send login compression packet
            begin_packet(&send_cursor, 3);
            net_write_varint(&send_cursor, 0);
            finish_packet_and_send(&send_cursor, entity, tick_arena);

            entity->flags |= PLAYER_PACKET_COMPRESSION;
        }

        // send game profile packet
        begin_packet(&send_cursor, 2);
        net_write_ulong(&send_cursor, 0x0123456789abcdef);
        net_write_ulong(&send_cursor, 0x0123456789abcdef);
        net_string username = {
            .size = player->username_size,
            .ptr = player->username
        };
        net_write_string(&send_cursor, username);
        finish_packet_and_send(&send_cursor, entity, tick_arena);

        net_string level_name = NET_STRING("blaze:main");

        begin_packet(&send_cursor, CBP_LOGIN);
        net_write_uint(&send_cursor, player->eid);
        net_write_ubyte(&send_cursor, 0); // hardcore
        net_write_ubyte(&send_cursor, player->gamemode); // current gamemode
        net_write_ubyte(&send_cursor, player->gamemode); // previous gamemode

        // all levels/worlds currently available on the server
        // @NOTE(traks) This list is used for tab completions
        net_write_varint(&send_cursor, 1); // number of levels
        net_write_string(&send_cursor, level_name);

        // Send all dimension-related NBT data

        nbt_write_key(&send_cursor, NBT_TAG_COMPOUND, NET_STRING(""));

        // write dimension types
        nbt_write_key(&send_cursor, NBT_TAG_COMPOUND, NET_STRING("minecraft:dimension_type"));

        nbt_write_key(&send_cursor, NBT_TAG_STRING, NET_STRING("type"));
        nbt_write_string(&send_cursor, NET_STRING("minecraft:dimension_type"));

        nbt_write_key(&send_cursor, NBT_TAG_LIST, NET_STRING("value"));
        net_write_ubyte(&send_cursor, NBT_TAG_COMPOUND);
        net_write_int(&send_cursor, serv->dimension_type_count);
        for (int i = 0; i < serv->dimension_type_count; i++) {
            dimension_type * dim_type = serv->dimension_types + i;

            nbt_write_key(&send_cursor, NBT_TAG_STRING, NET_STRING("name"));
            net_string name = {
                .ptr = dim_type->name,
                .size = dim_type->name_size
            };
            nbt_write_string(&send_cursor, name);

            nbt_write_key(&send_cursor, NBT_TAG_INT, NET_STRING("id"));
            net_write_int(&send_cursor, i);

            nbt_write_key(&send_cursor, NBT_TAG_COMPOUND, NET_STRING("element"));
            nbt_write_dimension_type(&send_cursor, dim_type);
            net_write_ubyte(&send_cursor, NBT_TAG_END);

            net_write_ubyte(&send_cursor, NBT_TAG_END);
        }

        net_write_ubyte(&send_cursor, NBT_TAG_END);
        // end of dimension types

        // write biomes
        nbt_write_key(&send_cursor, NBT_TAG_COMPOUND, NET_STRING("minecraft:worldgen/biome"));

        nbt_write_key(&send_cursor, NBT_TAG_STRING, NET_STRING("type"));
        nbt_write_string(&send_cursor, NET_STRING("minecraft:worldgen/biome"));

        nbt_write_key(&send_cursor, NBT_TAG_LIST, NET_STRING("value"));
        net_write_ubyte(&send_cursor, NBT_TAG_COMPOUND);
        net_write_int(&send_cursor, serv->biome_count);
        for (int i = 0; i < serv->biome_count; i++) {
            biome * b = serv->biomes + i;

            nbt_write_key(&send_cursor, NBT_TAG_STRING, NET_STRING("name"));
            net_string name = {
                .ptr = b->name,
                .size = b->name_size
            };
            nbt_write_string(&send_cursor, name);

            nbt_write_key(&send_cursor, NBT_TAG_INT, NET_STRING("id"));
            net_write_int(&send_cursor, i);

            nbt_write_key(&send_cursor, NBT_TAG_COMPOUND, NET_STRING("element"));
            nbt_write_biome(&send_cursor, b);
            net_write_ubyte(&send_cursor, NBT_TAG_END);

            net_write_ubyte(&send_cursor, NBT_TAG_END);
        }

        net_write_ubyte(&send_cursor, NBT_TAG_END);
        // end of biomes

        net_write_ubyte(&send_cursor, NBT_TAG_END);

        // dimension type NBT data of level player is joining
        nbt_write_key(&send_cursor, NBT_TAG_COMPOUND, NET_STRING(""));
        nbt_write_dimension_type(&send_cursor, serv->dimension_types);
        net_write_ubyte(&send_cursor, NBT_TAG_END);

        // level name the player is joining
        net_write_string(&send_cursor, level_name);

        net_write_ulong(&send_cursor, 0); // seed
        net_write_varint(&send_cursor, 0); // max players (ignored by client)
        net_write_varint(&send_cursor, player->new_chunk_cache_radius - 1);
        net_write_ubyte(&send_cursor, 0); // reduced debug info
        net_write_ubyte(&send_cursor, 1); // show death screen on death
        net_write_ubyte(&send_cursor, 0); // is debug
        net_write_ubyte(&send_cursor, 0); // is flat
        finish_packet_and_send(&send_cursor, entity, tick_arena);

        begin_packet(&send_cursor, CBP_SET_CARRIED_ITEM);
        net_write_ubyte(&send_cursor,
                player->selected_slot - PLAYER_FIRST_HOTBAR_SLOT);
        finish_packet_and_send(&send_cursor, entity, tick_arena);

        begin_packet(&send_cursor, CBP_UPDATE_TAGS);

        // Note that the order of the elements in the array has to match the
        // order of the tag lists in the packet.
        tag_list * tag_lists[] = {
            &serv->block_tags,
            &serv->item_tags,
            &serv->fluid_tags,
            &serv->entity_tags,
        };

        for (int tagsi = 0; tagsi < ARRAY_SIZE(tag_lists); tagsi++) {
            tag_list * tags = tag_lists[tagsi];
            net_write_varint(&send_cursor, tags->size);

            for (int i = 0; i < tags->size; i++) {
                tag_spec * tag = tags->tags + i;
                unsigned char * name_size = serv->tag_name_buf + tag->name_index;

                net_write_varint(&send_cursor, *name_size);
                net_write_data(&send_cursor, name_size + 1, *name_size);
                net_write_varint(&send_cursor, tag->value_count);

                for (int vali = 0; vali < tag->value_count; vali++) {
                    mc_int val = serv->tag_value_id_buf[tag->values_index + vali];
                    net_write_varint(&send_cursor, val);
                }
            }
        }
        finish_packet_and_send(&send_cursor, entity, tick_arena);

        begin_packet(&send_cursor, CBP_CUSTOM_PAYLOAD);
        net_string brand_str = NET_STRING("minecraft:brand");
        net_string brand = NET_STRING("blaze");
        net_write_string(&send_cursor, brand_str);
        net_write_string(&send_cursor, brand);
        finish_packet_and_send(&send_cursor, entity, tick_arena);

        begin_packet(&send_cursor, CBP_CHANGE_DIFFICULTY);
        net_write_ubyte(&send_cursor, 2); // difficulty normal
        net_write_ubyte(&send_cursor, 0); // locked
        finish_packet_and_send(&send_cursor, entity, tick_arena);

        begin_packet(&send_cursor, CBP_PLAYER_ABILITIES);
        // @NOTE(traks) actually need abilities to make creative mode players
        // be able to fly
        // bitmap: invulnerable, (is flying), can fly, instabuild
        mc_ubyte ability_flags = 0x4;
        net_write_ubyte(&send_cursor, ability_flags);
        net_write_float(&send_cursor, 0.05); // flying speed
        net_write_float(&send_cursor, 0.1); // walking speed
        finish_packet_and_send(&send_cursor, entity, tick_arena);
    }

    // send keep alive packet every so often
    if (serv->current_tick - player->last_keep_alive_sent_tick >= KEEP_ALIVE_SPACING
            && (entity->flags & PLAYER_GOT_ALIVE_RESPONSE)) {
        begin_packet(&send_cursor, CBP_KEEP_ALIVE);
        net_write_ulong(&send_cursor, serv->current_tick);
        finish_packet_and_send(&send_cursor, entity, tick_arena);

        player->last_keep_alive_sent_tick = serv->current_tick;
        entity->flags &= ~PLAYER_GOT_ALIVE_RESPONSE;
    }

    if ((entity->flags & ENTITY_TELEPORTING)
            && !(entity->flags & PLAYER_SENT_TELEPORT)) {
        begin_packet(&send_cursor, CBP_PLAYER_POSITION);
        net_write_double(&send_cursor, entity->x);
        net_write_double(&send_cursor, entity->y);
        net_write_double(&send_cursor, entity->z);
        net_write_float(&send_cursor, player->head_rot_y);
        net_write_float(&send_cursor, player->head_rot_x);
        net_write_ubyte(&send_cursor, 0); // relative arguments
        net_write_varint(&send_cursor, player->current_teleport_id);
        finish_packet_and_send(&send_cursor, entity, tick_arena);

        entity->flags |= PLAYER_SENT_TELEPORT;
    }

    // send block changes for this player only
    for (int i = 0; i < player->changed_block_count; i++) {
        net_block_pos pos = player->changed_blocks[i];
        chunk_pos ch_pos = {
            .x = pos.x >> 4,
            .z = pos.z >> 4
        };
        chunk * ch = get_chunk_if_loaded(ch_pos);
        if (ch == NULL) {
            continue;
        }

        mc_ushort block_state = chunk_get_block_state(ch,
                pos.x & 0xf, pos.y, pos.z & 0xf);

        begin_packet(&send_cursor, CBP_BLOCK_UPDATE);
        net_write_block_pos(&send_cursor, pos);
        net_write_varint(&send_cursor, block_state);
        finish_packet_and_send(&send_cursor, entity, tick_arena);
    }
    player->changed_block_count = 0;

    begin_timed_block("update chunk cache");

    mc_short chunk_cache_min_x = player->chunk_cache_centre_x - player->chunk_cache_radius;
    mc_short chunk_cache_min_z = player->chunk_cache_centre_z - player->chunk_cache_radius;
    mc_short chunk_cache_max_x = player->chunk_cache_centre_x + player->chunk_cache_radius;
    mc_short chunk_cache_max_z = player->chunk_cache_centre_z + player->chunk_cache_radius;

    mc_short new_chunk_cache_centre_x = (mc_int) floor(entity->x) >> 4;
    mc_short new_chunk_cache_centre_z = (mc_int) floor(entity->z) >> 4;
    assert(player->new_chunk_cache_radius <= MAX_CHUNK_CACHE_RADIUS);
    mc_short new_chunk_cache_min_x = new_chunk_cache_centre_x - player->new_chunk_cache_radius;
    mc_short new_chunk_cache_min_z = new_chunk_cache_centre_z - player->new_chunk_cache_radius;
    mc_short new_chunk_cache_max_x = new_chunk_cache_centre_x + player->new_chunk_cache_radius;
    mc_short new_chunk_cache_max_z = new_chunk_cache_centre_z + player->new_chunk_cache_radius;

    if (player->chunk_cache_centre_x != new_chunk_cache_centre_x
            || player->chunk_cache_centre_z != new_chunk_cache_centre_z) {
        begin_packet(&send_cursor, CBP_SET_CHUNK_CACHE_CENTRE);
        net_write_varint(&send_cursor, new_chunk_cache_centre_x);
        net_write_varint(&send_cursor, new_chunk_cache_centre_z);
        finish_packet_and_send(&send_cursor, entity, tick_arena);
    }

    if (player->chunk_cache_radius != player->new_chunk_cache_radius) {
        begin_packet(&send_cursor, CBP_SET_CHUNK_CACHE_RADIUS);
        net_write_varint(&send_cursor, player->new_chunk_cache_radius);
        finish_packet_and_send(&send_cursor, entity, tick_arena);
    }

    // untrack old chunks
    for (mc_short x = chunk_cache_min_x; x <= chunk_cache_max_x; x++) {
        for (mc_short z = chunk_cache_min_z; z <= chunk_cache_max_z; z++) {
            chunk_pos pos = {.x = x, .z = z};
            int index = chunk_cache_index(pos);

            if (x >= new_chunk_cache_min_x && x <= new_chunk_cache_max_x
                    && z >= new_chunk_cache_min_z && z <= new_chunk_cache_max_z) {
                // old chunk still in new region
                // send block changes if chunk is visible to the client
                if (!player->chunk_cache[index].sent) {
                    continue;
                }

                chunk * ch = get_chunk_if_loaded(pos);
                assert(ch != NULL);
                if (ch->changed_block_count == 0) {
                    continue;
                }

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

                    begin_packet(&send_cursor, CBP_SECTION_BLOCKS_UPDATE);
                    mc_ulong section_pos =
                            ((mc_ulong) (x & 0x3fffff) << 42)
                            | ((mc_ulong) (z & 0x3fffff) << 20)
                            | (mc_ulong) (section & 0xfffff);
                    net_write_ulong(&send_cursor, section_pos);
                    // @TODO(traks) appropriate value for this
                    net_write_ubyte(&send_cursor, 1); // suppress light updates
                    net_write_varint(&send_cursor, sec_changed_block_count);

                    for (int i = 0; i < sec_changed_block_count; i++) {
                        compact_chunk_block_pos pos = sec_changed_blocks[i];
                        mc_long block_state = chunk_get_block_state(ch,
                                pos.x, pos.y, pos.z);
                        mc_long encoded = (block_state << 12)
                                | (pos.x << 8) | (pos.z << 4) | (pos.y & 0xf);
                        net_write_varlong(&send_cursor, encoded);
                    }
                    finish_packet_and_send(&send_cursor, entity, tick_arena);
                }

                continue;
            }

            // old chunk is not in the new region
            chunk * ch = get_chunk_if_available(pos);
            assert(ch != NULL);
            ch->available_interest--;

            if (player->chunk_cache[index].sent) {
                player->chunk_cache[index] = (chunk_cache_entry) {0};

                begin_packet(&send_cursor, CBP_FORGET_LEVEL_CHUNK);
                net_write_int(&send_cursor, x);
                net_write_int(&send_cursor, z);
                finish_packet_and_send(&send_cursor, entity, tick_arena);
            }
        }
    }

    // track new chunks
    for (mc_short x = new_chunk_cache_min_x; x <= new_chunk_cache_max_x; x++) {
        for (mc_short z = new_chunk_cache_min_z; z <= new_chunk_cache_max_z; z++) {
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

    player->chunk_cache_radius = player->new_chunk_cache_radius;
    player->chunk_cache_centre_x = new_chunk_cache_centre_x;
    player->chunk_cache_centre_z = new_chunk_cache_centre_z;

    end_timed_block();

    // load and send tracked chunks
    begin_timed_block("load and send chunks");

    // We iterate in a spiral around the player, so chunks near the player
    // are processed first. This shortens server join times (since players
    // don't need to wait for the chunk they are in to load) and allows
    // players to move around much earlier.
    int newly_sent_chunks = 0;
    int newly_loaded_chunks = 0;
    int chunk_cache_diam = 2 * player->new_chunk_cache_radius + 1;
    int chunk_cache_area = chunk_cache_diam * chunk_cache_diam;
    int off_x = 0;
    int off_z = 0;
    int step_x = 1;
    int step_z = 0;
    for (int i = 0; i < chunk_cache_area; i++) {
        int x = new_chunk_cache_centre_x + off_x;
        int z = new_chunk_cache_centre_z + off_z;
        int cache_index = chunk_cache_index((chunk_pos) {.x = x, .z = z});
        chunk_cache_entry * entry = player->chunk_cache + cache_index;
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
                send_chunk_fully(&send_cursor, pos, ch, entity, tick_arena);
                send_light_update(&send_cursor, pos, ch, entity, tick_arena);
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
        if (!(player->slots_needing_update & ((mc_ulong) 1 << i))) {
            continue;
        }

        logs("Sending slot update for %d", i);
        item_stack * is = player->slots + i;

        begin_packet(&send_cursor, CBP_CONTAINER_SET_SLOT);
        net_write_ubyte(&send_cursor, 0); // inventory id
        net_write_ushort(&send_cursor, i);

        if (is->type == 0) {
            net_write_ubyte(&send_cursor, 0); // has item
        } else {
            net_write_ubyte(&send_cursor, 1); // has item
            net_write_varint(&send_cursor, is->type);
            net_write_ubyte(&send_cursor, is->size);
            // @TODO(traks) write NBT (currently just a single end tag)
            net_write_ubyte(&send_cursor, 0);
        }
        finish_packet_and_send(&send_cursor, entity, tick_arena);
    }

    player->slots_needing_update = 0;
    memcpy(player->slots_prev_tick, player->slots, sizeof player->slots);

    end_timed_block();

    // tab list updates
    begin_timed_block("send tab list");

    if (!(entity->flags & PLAYER_INITIALISED_TAB_LIST)) {
        entity->flags |= PLAYER_INITIALISED_TAB_LIST;
        if (serv->tab_list_size > 0) {
            begin_packet(&send_cursor, CBP_PLAYER_INFO);
            net_write_varint(&send_cursor, 0); // action: add
            net_write_varint(&send_cursor, serv->tab_list_size);

            for (int i = 0; i < serv->tab_list_size; i++) {
                tab_list_entry * entry = serv->tab_list + i;
                entity_base * entity = resolve_entity(serv, entry->eid);
                assert(entity->type == ENTITY_PLAYER);
                // @TODO(traks) write UUID
                net_write_ulong(&send_cursor, 0);
                net_write_ulong(&send_cursor, entry->eid);
                net_string username = {
                    .ptr = player->username,
                    .size = player->username_size
                };
                net_write_string(&send_cursor, username);
                net_write_varint(&send_cursor, 0); // num properties
                net_write_varint(&send_cursor, player->gamemode);
                net_write_varint(&send_cursor, 0); // latency
                net_write_ubyte(&send_cursor, 0); // has display name
            }
            finish_packet_and_send(&send_cursor, entity, tick_arena);
        }
    } else {
        if (serv->tab_list_removed_count > 0) {
            begin_packet(&send_cursor, CBP_PLAYER_INFO);
            net_write_varint(&send_cursor, 4); // action: remove
            net_write_varint(&send_cursor, serv->tab_list_removed_count);

            for (int i = 0; i < serv->tab_list_removed_count; i++) {
                tab_list_entry * entry = serv->tab_list_removed + i;
                // @TODO(traks) write UUID
                net_write_ulong(&send_cursor, 0);
                net_write_ulong(&send_cursor, entry->eid);
            }
            finish_packet_and_send(&send_cursor, entity, tick_arena);
        }
        if (serv->tab_list_added_count > 0) {
            begin_packet(&send_cursor, CBP_PLAYER_INFO);
            net_write_varint(&send_cursor, 0); // action: add
            net_write_varint(&send_cursor, serv->tab_list_added_count);

            for (int i = 0; i < serv->tab_list_added_count; i++) {
                tab_list_entry * entry = serv->tab_list_added + i;
                entity_base * entity = resolve_entity(serv, entry->eid);
                assert(entity->type == ENTITY_PLAYER);
                // @TODO(traks) write UUID
                net_write_ulong(&send_cursor, 0);
                net_write_ulong(&send_cursor, entry->eid);
                net_string username = {
                    .ptr = player->username,
                    .size = player->username_size
                };
                net_write_string(&send_cursor, username);
                net_write_varint(&send_cursor, 0); // num properties
                net_write_varint(&send_cursor, player->gamemode);
                net_write_varint(&send_cursor, 0); // latency
                net_write_ubyte(&send_cursor, 0); // has display name
            }
            finish_packet_and_send(&send_cursor, entity, tick_arena);
        }
    }

    end_timed_block();

    // entity tracking
    begin_timed_block("track entities");

    entity_id removed_entities[64];
    int removed_entity_count = 0;

    for (int j = 1; j < MAX_ENTITIES; j++) {
        entity_id tracked_eid = player->tracked_entities[j];
        entity_base * candidate = serv->entities + j;

        if (tracked_eid != 0) {
            // entity is currently being tracked
            if ((candidate->flags & ENTITY_IN_USE)
                    && candidate->eid == tracked_eid) {
                // entity is still there
                double dx = candidate->x - entity->x;
                double dy = candidate->y - entity->y;
                double dz = candidate->z - entity->z;
                if (dx * dx + dy * dy + dz * dz < 45 * 45) {
                    send_tracked_entity_packets(entity, serv,
                            &send_cursor, tick_arena, candidate);
                    continue;
                }
            }

            // entity we tracked is gone or too far away

            if (removed_entity_count == ARRAY_SIZE(removed_entities)) {
                // no more space to untrack, try again next tick
                continue;
            }

            player->tracked_entities[j] = 0;
            removed_entities[removed_entity_count] = tracked_eid;
            removed_entity_count++;
        }

        if ((candidate->flags & ENTITY_IN_USE) && candidate->eid != entity->eid) {
            // candidate is valid for being newly tracked
            double dx = candidate->x - entity->x;
            double dy = candidate->y - entity->y;
            double dz = candidate->z - entity->z;

            if (dx * dx + dy * dy + dz * dz > 40 * 40) {
                continue;
            }

            send_add_entity_packet(entity, serv,
                    &send_cursor, tick_arena, candidate);

            mc_uint entity_index = candidate->eid & ENTITY_INDEX_MASK;
            player->tracked_entities[entity_index] = candidate->eid;
        }
    }

    if (removed_entity_count > 0) {
        begin_packet(&send_cursor, CBP_REMOVE_ENTITIES);
        net_write_varint(&send_cursor, removed_entity_count);
        for (int i = 0; i < removed_entity_count; i++) {
            net_write_varint(&send_cursor, removed_entities[i]);
        }
        finish_packet_and_send(&send_cursor, entity, tick_arena);
    }

    end_timed_block();

    // send chat messages
    begin_timed_block("send chat");

    for (int i = 0; i < serv->global_msg_count; i++) {
        global_msg * msg = serv->global_msgs + i;

        // @TODO(traks) formatted messages and such
        unsigned char buf[1024];
        int buf_index = 0;
        net_string prefix = NET_STRING("{\"text\":\"");
        net_string suffix = NET_STRING("\"}");

        memcpy(buf + buf_index, prefix.ptr, prefix.size);
        buf_index += prefix.size;

        for (int i = 0; i < msg->size; i++) {
            if (msg->text[i] == '"' || msg->text[i] == '\\') {
                buf[buf_index] = '\\';
                buf_index++;
            }
            buf[buf_index] = msg->text[i];
            buf_index++;
        }

        memcpy(buf + buf_index, suffix.ptr, suffix.size);
        buf_index += suffix.size;

        begin_packet(&send_cursor, CBP_CHAT);
        net_write_varint(&send_cursor, buf_index);
        net_write_data(&send_cursor, buf, buf_index);
        net_write_ubyte(&send_cursor, 0); // chat box position
        // @TODO(traks) write sender UUID. If UUID equals 0, client displays it
        // regardless of client settings
        net_write_ulong(&send_cursor, 0);
        net_write_ulong(&send_cursor, 0);
        finish_packet_and_send(&send_cursor, entity, tick_arena);
    }

    end_timed_block();

    // try to write everything to the socket buffer

    assert(send_cursor.error == 0);

    int sock = player->sock;

    begin_timed_block("send()");
    ssize_t send_size = send(sock, player->send_buf,
            player->send_cursor, 0);
    end_timed_block();

    if (send_size == -1) {
        // EAGAIN means no data sent
        if (errno != EAGAIN) {
            logs_errno("Couldn't send protocol data: %s");
            disconnect_player_now(entity, serv);
        }
    } else {
        memmove(player->send_buf, player->send_buf + send_size,
                player->send_cursor - send_size);
        player->send_cursor -= send_size;
    }

    end_timed_block();
}
