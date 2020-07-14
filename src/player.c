#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <x86intrin.h>
#include <math.h>
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
    SBP_RECIPE_BOOK_UPDATE,
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
    CBP_CHUNK_BLOCKS_UPDATE,
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

static_assert(SERVERBOUND_PACKET_COUNT == 47, "Packet count mismatch");
static_assert(CLIENTBOUND_PACKET_COUNT == 92, "Packet count mismatch");

void
teleport_player(player_brain * brain, entity_data * entity,
        mc_double new_x, mc_double new_y, mc_double new_z,
        mc_float new_rot_x, mc_float new_rot_y) {
    brain->current_teleport_id++;
    entity->flags |= ENTITY_TELEPORTING;
    entity->x = new_x;
    entity->y = new_y;
    entity->z = new_z;
    entity->player.head_rot_x = new_rot_x;
    entity->player.head_rot_y = new_rot_y;
}

static void
process_move_player_packet(entity_data * entity,
        mc_double new_x, mc_double new_y, mc_double new_z,
        mc_float new_head_rot_x, mc_float new_head_rot_y, int on_ground) {
    if ((entity->flags & ENTITY_TELEPORTING) != 0) {
        return;
    }

    // @TODO(traks) if new x, y, z out of certain bounds, don't update entity
    // x, y, z to prevent NaN errors and extreme precision loss, etc.

    entity->x = new_x;
    entity->y = new_y;
    entity->z = new_z;
    entity->player.head_rot_x = new_head_rot_x;
    entity->player.head_rot_y = new_head_rot_y;
    if (on_ground) {
        entity->flags |= ENTITY_ON_GROUND;
    } else {
        entity->flags &= ~ENTITY_ON_GROUND;
    }
}

static void
process_packet(entity_data * entity, player_brain * brain,
        buffer_cursor * rec_cursor, server * serv, mc_int packet_size,
        memory_arena * process_arena) {
    int packet_start = rec_cursor->index;
    mc_int packet_id = net_read_varint(rec_cursor);

    switch (packet_id) {
    case SBP_ACCEPT_TELEPORT: {
        mc_int teleport_id = net_read_varint(rec_cursor);

        if ((entity->flags & ENTITY_TELEPORTING)
                && (brain->flags & PLAYER_BRAIN_SENT_TELEPORT)
                && teleport_id == brain->current_teleport_id) {
            entity->flags &= ~ENTITY_TELEPORTING;
            brain->flags &= ~PLAYER_BRAIN_SENT_TELEPORT;
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
                    (int) entity->player.username_size,
                    entity->player.username,
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
        brain->new_chunk_cache_radius = MIN(MAX(view_distance, 2),
                MAX_CHUNK_CACHE_RADIUS - 1) + 1;
        memcpy(brain->language, language.ptr, language.size);
        brain->language_size = language.size;
        brain->sees_chat_colours = sees_chat_colours;
        brain->model_customisation = model_customisation;
        brain->main_hand = main_hand;
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

            if (is->type < 0 || is->type >= serv->item_type_count) {
                is->type = 0;
                // @TODO(traks) handle error (send slot updates?)
            }
            item_type * type = serv->item_types + is->type;
            if (is->size > type->max_stack_size) {
                is->size = type->max_stack_size;
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
        mc_int payload_size = packet_start + packet_size
                - rec_cursor->index;

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

            if (is->type < 0 || is->type >= serv->item_type_count) {
                is->type = 0;
                // @TODO(traks) handle error
            }
            item_type * type = serv->item_types + is->type;
            if (is->size > type->max_stack_size) {
                is->size = type->max_stack_size;
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
            mc_float x = net_read_float(rec_cursor);
            mc_float y = net_read_float(rec_cursor);
            mc_float z = net_read_float(rec_cursor);
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
        if (brain->last_keep_alive_sent_tick == id) {
            brain->flags |= PLAYER_BRAIN_GOT_ALIVE_RESPONSE;
        }
        break;
    }
    case SBP_LOCK_DIFFICULTY: {
        logs("Packet lock difficulty");
        mc_ubyte locked = net_read_ubyte(rec_cursor);
        break;
    }
    case SBP_MOVE_PLAYER_POS: {
        mc_double x = net_read_double(rec_cursor);
        mc_double y = net_read_double(rec_cursor);
        mc_double z = net_read_double(rec_cursor);
        int on_ground = net_read_ubyte(rec_cursor);
        process_move_player_packet(entity, x, y, z,
                entity->player.head_rot_x,
                entity->player.head_rot_y, on_ground);
        break;
    }
    case SBP_MOVE_PLAYER_POS_ROT: {
        mc_double x = net_read_double(rec_cursor);
        mc_double y = net_read_double(rec_cursor);
        mc_double z = net_read_double(rec_cursor);
        mc_float head_rot_y = net_read_float(rec_cursor);
        mc_float head_rot_x = net_read_float(rec_cursor);
        int on_ground = net_read_ubyte(rec_cursor);
        process_move_player_packet(entity, x, y, z,
                head_rot_x, head_rot_y, on_ground);
        break;
    }
    case SBP_MOVE_PLAYER_ROT: {
        mc_float head_rot_y = net_read_float(rec_cursor);
        mc_float head_rot_x = net_read_float(rec_cursor);
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
                entity->player.head_rot_x,
                entity->player.head_rot_y, on_ground);
        break;
    }
    case SBP_MOVE_VEHICLE: {
        logs("Packet move vehicle");
        mc_double x = net_read_double(rec_cursor);
        mc_double y = net_read_double(rec_cursor);
        mc_double z = net_read_double(rec_cursor);
        mc_float rot_y = net_read_float(rec_cursor);
        mc_float rot_x = net_read_float(rec_cursor);
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
            if (entity->player.gamemode == GAMEMODE_CREATIVE) {
                // @TODO(traks) ensure block pos is close to the
                // player and the chunk is sent to the player
                __m128i xz = _mm_set_epi32(0, 0, block_pos.z, block_pos.x);
                __m128i chunk_xz = _mm_srai_epi32(xz, 4);
                chunk_pos pos = {
                    .x = _mm_extract_epi32(chunk_xz, 0),
                    .z = _mm_extract_epi32(chunk_xz, 1)
                };
                chunk * ch = get_chunk_if_loaded(pos);
                if (ch == NULL) {
                    // @TODO(traks) client will still see block as
                    // broken. Does that really matter? A forget
                    // packet will probably reach them soon enough.
                    break;
                }

                // @TODO(traks) ANDing signed integers better work
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
            int sel_slot = entity->player.selected_slot;
            item_stack * is = entity->player.slots + sel_slot;

            // @NOTE(traks) client updates its view of the item stack size
            // itself, so no need to send updates for the slot if nothing
            // special happens
            if (is->size > 0) {
                entity_data * item = try_reserve_entity(serv, ENTITY_ITEM);
                if (item->type == ENTITY_NULL) {
                    entity->player.slots_needing_update |= (mc_ulong) 1 << sel_slot;
                    break;
                }

                // @TODO(traks) higher spawn position
                item->x = entity->x;
                item->y = entity->y;
                item->z = entity->z;
                item->item.contents = *is;

                is->size = 0;
            }
            break;
        }
        case 4: { // drop item
            int sel_slot = entity->player.selected_slot;
            item_stack * is = entity->player.slots + sel_slot;

            // @NOTE(traks) client updates its view of the item stack size
            // itself, so no need to send updates for the slot if nothing
            // special happens
            if (is->size > 0) {
                entity_data * item = try_reserve_entity(serv, ENTITY_ITEM);
                if (item->type == ENTITY_NULL) {
                    entity->player.slots_needing_update |= (mc_ulong) 1 << sel_slot;
                    break;
                }

                // @TODO(traks) higher spawn position
                item->x = entity->x;
                item->y = entity->y;
                item->z = entity->z;
                item->item.contents = *is;
                item->item.contents.size = 1;

                is->size--;
            }
            break;
        }
        case 5: { // release use item
            // @TODO(traks)
            break;
        }
        case 6: { // swap held items
            int sel_slot = entity->player.selected_slot;
            item_stack * sel = entity->player.slots + sel_slot;
            item_stack * off = entity->player.slots + PLAYER_OFF_HAND_SLOT;
            item_stack sel_copy = *sel;
            *sel = *off;
            *off = sel_copy;
            // client doesn't update its view of the inventory for
            // this packet, so send updates to the client
            entity->player.slots_needing_update |= (mc_ulong) 1 << sel_slot;
            entity->player.slots_needing_update |= (mc_ulong) 1 << PLAYER_OFF_HAND_SLOT;
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
            brain->flags |= PLAYER_BRAIN_SHIFTING;
            break;
        case 1: // release shift key
            brain->flags &= ~PLAYER_BRAIN_SHIFTING;
            break;
        case 2: // stop sleeping
            // @TODO(traks)
            break;
        case 3: // start sprinting
            brain->flags |= PLAYER_BRAIN_SPRINTING;
            break;
        case 4: // stop sprinting
            brain->flags &= ~PLAYER_BRAIN_SPRINTING;
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
    case SBP_RECIPE_BOOK_UPDATE: {
        logs("Packet recipe book update");
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
        entity->player.selected_slot = PLAYER_FIRST_HOTBAR_SLOT + slot;
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

        item_stack * is = entity->player.slots + slot;
        *is = (item_stack) {0};

        if (has_item) {
            is->type = net_read_varint(rec_cursor);
            is->size = net_read_ubyte(rec_cursor);

            if (is->type < 0 || is->type >= serv->item_type_count) {
                is->type = 0;
                entity->player.slots_needing_update |=
                        (mc_ulong) 1 << slot;
            }
            item_type * type = serv->item_types + is->type;
            if (is->size > type->max_stack_size) {
                is->size = type->max_stack_size;
                entity->player.slots_needing_update |=
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
        mc_int hand = net_read_varint(rec_cursor);
        net_block_pos clicked_pos = net_read_block_pos(rec_cursor);
        mc_int clicked_face = net_read_varint(rec_cursor);
        mc_float click_offset_x = net_read_float(rec_cursor);
        mc_float click_offset_y = net_read_float(rec_cursor);
        mc_float click_offset_z = net_read_float(rec_cursor);
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

        if (entity->flags & ENTITY_TELEPORTING) {
            // ignore
            break;
        }

        // @TODO(traks) special handling depending on gamemode

        // @TODO(traks) ensure clicked block is in one of the sent
        // chunks inside the player's chunk cache

        int sel_slot = entity->player.selected_slot;
        item_stack * sel = entity->player.slots + sel_slot;
        item_stack * off = entity->player.slots + PLAYER_OFF_HAND_SLOT;
        item_stack * used = hand == 0 ? sel : off;

        if (!(brain->flags & PLAYER_BRAIN_SHIFTING)
                || (sel->type == 0 && off->type == 0)) {
            // @TODO(traks) use clicked block (button, door, etc.)
        }

        // @TODO(traks) check for cooldowns (ender pearls,
        // chorus fruits)

        // @TODO(traks) use item type to determine which place
        // handler to fire
        item_type * used_type = serv->item_types + used->type;

        net_block_pos target_pos = clicked_pos;
        switch (clicked_face) {
        case DIRECTION_NEG_Y: target_pos.y--; break;
        case DIRECTION_POS_Y: target_pos.y++; break;
        case DIRECTION_NEG_Z: target_pos.z--; break;
        case DIRECTION_POS_Z: target_pos.z++; break;
        case DIRECTION_NEG_X: target_pos.x--; break;
        case DIRECTION_POS_X: target_pos.x++; break;
        }

        // @TODO(traks) check if target pos is in chunk visible to
        // the player

        __m128i xz = _mm_set_epi32(0, 0, target_pos.z, target_pos.x);
        __m128i chunk_xz = _mm_srai_epi32(xz, 4);
        chunk_pos pos = {
            .x = _mm_extract_epi32(chunk_xz, 0),
            .z = _mm_extract_epi32(chunk_xz, 1)
        };
        chunk * ch = get_chunk_if_loaded(pos);
        if (ch == NULL) {
            break;
        }

        // @TODO(traks) ANDing signed integers better work
        int in_chunk_x = target_pos.x & 0xf;
        int in_chunk_z = target_pos.z & 0xf;
        chunk_set_block_state(ch, in_chunk_x, target_pos.y,
                in_chunk_z, 2);
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
finish_packet_and_send(buffer_cursor * send_cursor, player_brain * brain) {
    // We use the written data to determine the packet size instead of
    // calculating the packet size up front. The major benefit is that
    // calculating the packet size up front is very error prone and requires a
    // lot of maintainance (in case of packet format changes).
    //
    // The downside is that we need to write the packet data to a separate
    // buffer and copy it to the send buffer afterwards, because Mojang decided
    // to encode packet sizes with a variable-size encoding. Although with
    // packet compression enabled (which everyone probably wants!) we need to
    // write to a separate buffer anyway.

    // @TODO(traks) instead of copying the packet to the send buffer each time,
    // maybe write all packets to a separate buffer, then copy all packets at
    // once to the send buffer
    mc_int packet_size = send_cursor->index - 5;
    mc_int start_index = 5 - net_varint_size(packet_size);
    send_cursor->index = start_index;
    net_write_varint(send_cursor, packet_size);

    buffer_cursor packet_cursor = {
        .buf = brain->send_buf,
        .limit = sizeof brain->send_buf,
        .index = brain->send_cursor
    };

    // @TODO(traks) should check somewhere that no error occurs
    net_write_data(&packet_cursor, send_cursor->buf + start_index,
            packet_size + 5 - start_index);
    brain->send_cursor = packet_cursor.index;
}

static void
send_chunk_fully(buffer_cursor * send_cursor, chunk_pos pos, chunk * ch,
        player_brain * brain) {
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

    net_string height_map_name = NET_STRING("MOTION_BLOCKING");

    begin_packet(send_cursor, CBP_LEVEL_CHUNK);
    net_write_int(send_cursor, pos.x);
    net_write_int(send_cursor, pos.z);
    net_write_ubyte(send_cursor, 1); // full chunk
    net_write_ubyte(send_cursor, 1); // forget old data
    net_write_varint(send_cursor, section_mask);

    // height map NBT
    net_write_ubyte(send_cursor, 10);
    // use a zero length string as name for the root compound
    net_write_ushort(send_cursor, 0);

    net_write_ubyte(send_cursor, 12);
    // write name
    net_write_ushort(send_cursor, height_map_name.size);
    net_write_data(send_cursor, height_map_name.ptr, height_map_name.size);

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

    // end of compound
    net_write_ubyte(send_cursor, 0);

    // Biome data. Currently we just set all biome blocks (4x4x4 cubes)
    // to the plains biome.
    for (int i = 0; i < 1024; i++) {
        net_write_int(send_cursor, 1);
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
    finish_packet_and_send(send_cursor, brain);

    end_timed_block();
}

static void
send_light_update(buffer_cursor * send_cursor, chunk_pos pos, chunk * ch,
        player_brain * brain) {
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
    finish_packet_and_send(send_cursor, brain);

    end_timed_block();
}

static void
disconnect_player_now(player_brain * brain, server * serv) {
    close(brain->sock);
    brain->flags = 0;
    serv->player_brain_count--;
    evict_entity(serv, brain->eid);

    mc_short chunk_cache_min_x = brain->chunk_cache_centre_x - brain->chunk_cache_radius;
    mc_short chunk_cache_max_x = brain->chunk_cache_centre_x + brain->chunk_cache_radius;
    mc_short chunk_cache_min_z = brain->chunk_cache_centre_z - brain->chunk_cache_radius;
    mc_short chunk_cache_max_z = brain->chunk_cache_centre_z + brain->chunk_cache_radius;

    for (mc_short x = chunk_cache_min_x; x <= chunk_cache_max_x; x++) {
        for (mc_short z = chunk_cache_min_z; z <= chunk_cache_max_z; z++) {
            chunk_pos pos = {.x = x, .z = z};
            chunk * ch = get_chunk_if_available(pos);
            assert(ch != NULL);
            ch->available_interest--;
        }
    }
}

static mc_float
degree_diff(mc_float to, mc_float from) {
    mc_float div = (to - from) / 360 + 0.5f;
    mc_float mod = div - floor(div);
    mc_float res = mod * 360 - 180;
    return res;
}

void
tick_player_brain(player_brain * brain, server * serv,
        memory_arena * tick_arena) {
    begin_timed_block("tick player");

    entity_data * entity = resolve_entity(serv, brain->eid);
    assert(entity->type == ENTITY_PLAYER);
    int sock = brain->sock;
    ssize_t rec_size = recv(sock, brain->rec_buf + brain->rec_cursor,
            sizeof brain->rec_buf - brain->rec_cursor, 0);

    mc_double initial_x = entity->x;
    mc_double initial_y = entity->y;
    mc_double initial_z = entity->z;

    if (rec_size == 0) {
        disconnect_player_now(brain, serv);
    } else if (rec_size == -1) {
        // EAGAIN means no data received
        if (errno != EAGAIN) {
            logs_errno("Couldn't receive protocol data: %s");
            disconnect_player_now(brain, serv);
        }
    } else {
        brain->rec_cursor += rec_size;

        buffer_cursor rec_cursor = {
            .buf = brain->rec_buf,
            .limit = brain->rec_cursor
        };

        for (;;) {
            mc_int packet_size = net_read_varint(&rec_cursor);

            if (rec_cursor.error != 0) {
                // packet size not fully received yet
                break;
            }
            if (packet_size <= 0 || packet_size > sizeof brain->rec_buf) {
                disconnect_player_now(brain, serv);
                break;
            }
            if (packet_size > rec_cursor.limit) {
                // packet not fully received yet
                break;
            }

            int packet_start = rec_cursor.index;
            memory_arena process_arena = *tick_arena;

            process_packet(entity, brain, &rec_cursor, serv, packet_size,
                    &process_arena);

            if (packet_size != rec_cursor.index - packet_start) {
                rec_cursor.error = 1;
            }

            if (rec_cursor.error != 0) {
                logs("Player protocol error occurred");
                disconnect_player_now(brain, serv);
                break;
            }
        }

        memmove(rec_cursor.buf, rec_cursor.buf + rec_cursor.index,
                rec_cursor.limit - rec_cursor.index);
        brain->rec_cursor = rec_cursor.limit - rec_cursor.index;
    }

    // @TODO(traks) only here because players could be disconnected and get
    // all their data cleaned up immediately if some packet handling error
    // occurs above. Eventually we should handle errors more gracefully.
    // Then this check shouldn't be necessary anymore.
    if (!(brain->flags & PLAYER_BRAIN_IN_USE)) {
        goto bail;
    }

    if (!(entity->flags & ENTITY_TELEPORTING)) {
        mc_double move_dx = entity->x - initial_x;
        mc_double move_dy = entity->y - initial_y;
        mc_double move_dz = entity->z - initial_z;
    }

bail:
    end_timed_block();
}

void
send_packets_to_player(player_brain * brain, server * serv,
        memory_arena * tick_arena) {
    begin_timed_block("send packets");

    entity_data * player = resolve_entity(serv, brain->eid);
    size_t max_uncompressed_packet_size = 1 << 20;
    buffer_cursor send_cursor = {
        .buf = alloc_in_arena(tick_arena, max_uncompressed_packet_size),
        .limit = max_uncompressed_packet_size
    };

    if (!(brain->flags & PLAYER_BRAIN_DID_INIT_PACKETS)) {
        brain->flags |= PLAYER_BRAIN_DID_INIT_PACKETS;

        // send game profile packet
        begin_packet(&send_cursor, 2);
        net_write_ulong(&send_cursor, 0x0123456789abcdef);
        net_write_ulong(&send_cursor, 0x0123456789abcdef);
        net_string username = {
            .size = player->player.username_size,
            .ptr = player->player.username
        };
        net_write_string(&send_cursor, username);
        finish_packet_and_send(&send_cursor, brain);

        net_string level_name = NET_STRING("blaze:main");
        net_string dimension_type = NET_STRING("minecraft:overworld");
        net_string dimension_str = NET_STRING("dimension");
        net_string name_str = NET_STRING("name");
        net_string has_skylight = NET_STRING("has_skylight");
        net_string has_ceiling = NET_STRING("has_ceiling");
        net_string ultrawarm = NET_STRING("ultrawarm");
        net_string natural = NET_STRING("natural");
        net_string shrunk = NET_STRING("shrunk");
        net_string piglin_safe = NET_STRING("piglin_safe");
        net_string bed_works = NET_STRING("bed_works");
        net_string respawn_anchor_works = NET_STRING("respawn_anchor_works");
        net_string has_raids = NET_STRING("has_raids");
        net_string logical_height = NET_STRING("logical_height");
        net_string infiniburn = NET_STRING("infiniburn");
        net_string infiniburn_tag = NET_STRING("minecraft:infiniburn_overworld");
        net_string ambient_light = NET_STRING("ambient_light");

        begin_packet(&send_cursor, CBP_LOGIN);
        net_write_uint(&send_cursor, player->eid);
        net_write_ubyte(&send_cursor, player->player.gamemode); // current gamemode
        net_write_ubyte(&send_cursor, player->player.gamemode); // previous gamemode

        // all levels/worlds currently available on the server
        net_write_varint(&send_cursor, 1); // number of levels
        net_write_string(&send_cursor, level_name);

        // All dimension types on the server. Currently dimension types
        // have the following configurable properties:
        //
        //  - fixed_time (optional long): time of day always equals this
        //  - has_skylight (bool): sky light levels, whether it can
        //    thunder, whether daylight sensors work, etc.
        //  - has_ceiling (bool): affects thunder, map rendering, mob
        //    spawning algorithm
        //  - ultrawarm (bool): whether water can be placed and how far
        //    and how fast lava flows
        //  - natural (bool): whether players can sleep and whether
        //    zombified piglin can spawn from portals
        //  - shrunk (bool): nether coordinates or overworld coordinates
        //    (to determine new coordinates when moving between worlds)
        //  - piglin_safe (bool): false if piglins convert to zombified
        //    piglins as in the vanilla overworld
        //  - bed_works (bool): true if beds can set spawn point
        //  - respawn_anchor_works (bool): true if respawn anchors can
        //    set spawn point
        //  - has_raids (bool): whether raids spawn
        //  - logical_height (int in [0, 256]): seems to only affect
        //    chorus fruit teleportation and nether portal spawning, not
        //    the actual maximum world height
        //  - infiniburn (resource loc): the resource location of a
        //    block tag that is used to check whether fire should keep
        //    burning forever on tagged blocks
        //  - ambient_light (float): not used
        //
        // None seem to affect anything client-side.

        net_write_ubyte(&send_cursor, NBT_TAG_COMPOUND);
        net_write_ushort(&send_cursor, 0); // 0-length name

        net_write_ubyte(&send_cursor, NBT_TAG_LIST);
        net_write_ushort(&send_cursor, dimension_str.size);
        net_write_data(&send_cursor, dimension_str.ptr, dimension_str.size);
        net_write_ubyte(&send_cursor, NBT_TAG_COMPOUND); // element type
        net_write_int(&send_cursor, 1); // number of elements

        net_write_ubyte(&send_cursor, NBT_TAG_STRING);
        net_write_ushort(&send_cursor, name_str.size);
        net_write_data(&send_cursor, name_str.ptr, name_str.size);
        net_write_ushort(&send_cursor, dimension_type.size);
        net_write_data(&send_cursor, dimension_type.ptr, dimension_type.size);

        net_write_ubyte(&send_cursor, NBT_TAG_BYTE);
        net_write_ushort(&send_cursor, has_skylight.size);
        net_write_data(&send_cursor, has_skylight.ptr, has_skylight.size);
        net_write_ubyte(&send_cursor, 1);

        net_write_ubyte(&send_cursor, NBT_TAG_BYTE);
        net_write_ushort(&send_cursor, has_ceiling.size);
        net_write_data(&send_cursor, has_ceiling.ptr, has_ceiling.size);
        net_write_ubyte(&send_cursor, 0);

        net_write_ubyte(&send_cursor, NBT_TAG_BYTE);
        net_write_ushort(&send_cursor, ultrawarm.size);
        net_write_data(&send_cursor, ultrawarm.ptr, ultrawarm.size);
        net_write_ubyte(&send_cursor, 0);

        net_write_ubyte(&send_cursor, NBT_TAG_BYTE);
        net_write_ushort(&send_cursor, natural.size);
        net_write_data(&send_cursor, natural.ptr, natural.size);
        net_write_ubyte(&send_cursor, 1);

        net_write_ubyte(&send_cursor, NBT_TAG_BYTE);
        net_write_ushort(&send_cursor, shrunk.size);
        net_write_data(&send_cursor, shrunk.ptr, shrunk.size);
        net_write_ubyte(&send_cursor, 0);

        net_write_ubyte(&send_cursor, NBT_TAG_BYTE);
        net_write_ushort(&send_cursor, piglin_safe.size);
        net_write_data(&send_cursor, piglin_safe.ptr, piglin_safe.size);
        net_write_ubyte(&send_cursor, 0);

        net_write_ubyte(&send_cursor, NBT_TAG_BYTE);
        net_write_ushort(&send_cursor, bed_works.size);
        net_write_data(&send_cursor, bed_works.ptr, bed_works.size);
        net_write_ubyte(&send_cursor, 1);

        net_write_ubyte(&send_cursor, NBT_TAG_BYTE);
        net_write_ushort(&send_cursor, respawn_anchor_works.size);
        net_write_data(&send_cursor, respawn_anchor_works.ptr, respawn_anchor_works.size);
        net_write_ubyte(&send_cursor, 0);

        net_write_ubyte(&send_cursor, NBT_TAG_BYTE);
        net_write_ushort(&send_cursor, has_raids.size);
        net_write_data(&send_cursor, has_raids.ptr, has_raids.size);
        net_write_ubyte(&send_cursor, 0);

        net_write_ubyte(&send_cursor, NBT_TAG_INT);
        net_write_ushort(&send_cursor, logical_height.size);
        net_write_data(&send_cursor, logical_height.ptr, logical_height.size);
        net_write_int(&send_cursor, 256);

        net_write_ubyte(&send_cursor, NBT_TAG_STRING);
        net_write_ushort(&send_cursor, infiniburn.size);
        net_write_data(&send_cursor, infiniburn.ptr, infiniburn.size);
        net_write_ushort(&send_cursor, infiniburn_tag.size);
        net_write_data(&send_cursor, infiniburn_tag.ptr, infiniburn_tag.size);

        net_write_ubyte(&send_cursor, NBT_TAG_FLOAT);
        net_write_ushort(&send_cursor, ambient_light.size);
        net_write_data(&send_cursor, ambient_light.ptr, ambient_light.size);
        net_write_float(&send_cursor, 0);

        net_write_ubyte(&send_cursor, NBT_TAG_END); // end of element compound
        net_write_ubyte(&send_cursor, NBT_TAG_END); // end of root compound

        net_write_string(&send_cursor, dimension_type);
        net_write_string(&send_cursor, level_name);

        net_write_ulong(&send_cursor, 0); // seed
        net_write_ubyte(&send_cursor, 0); // max players (ignored by client)
        net_write_varint(&send_cursor, brain->new_chunk_cache_radius - 1);
        net_write_ubyte(&send_cursor, 0); // reduced debug info
        net_write_ubyte(&send_cursor, 1); // show death screen on death
        net_write_ubyte(&send_cursor, 0); // is debug
        net_write_ubyte(&send_cursor, 0); // is flat
        finish_packet_and_send(&send_cursor, brain);

        begin_packet(&send_cursor, CBP_SET_CARRIED_ITEM);
        net_write_ubyte(&send_cursor, player->player.selected_slot
                - PLAYER_FIRST_HOTBAR_SLOT);
        finish_packet_and_send(&send_cursor, brain);

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
        finish_packet_and_send(&send_cursor, brain);

        begin_packet(&send_cursor, CBP_CUSTOM_PAYLOAD);
        net_string brand_str = NET_STRING("minecraft:brand");
        net_string brand = NET_STRING("blaze");
        net_write_string(&send_cursor, brand_str);
        net_write_string(&send_cursor, brand);
        finish_packet_and_send(&send_cursor, brain);

        begin_packet(&send_cursor, CBP_CHANGE_DIFFICULTY);
        net_write_ubyte(&send_cursor, 2); // difficulty normal
        net_write_ubyte(&send_cursor, 0); // locked
        finish_packet_and_send(&send_cursor, brain);

        begin_packet(&send_cursor, CBP_PLAYER_ABILITIES);
        // @NOTE(traks) actually need abilities to make creative mode players
        // be able to fly
        // bitmap: invulnerable, (is flying), can fly, instabuild
        mc_ubyte ability_flags = 0x4;
        net_write_ubyte(&send_cursor, ability_flags);
        net_write_float(&send_cursor, 0.05); // flying speed
        net_write_float(&send_cursor, 0.1); // walking speed
        finish_packet_and_send(&send_cursor, brain);
    }

    // send keep alive packet every so often
    if (serv->current_tick - brain->last_keep_alive_sent_tick >= KEEP_ALIVE_SPACING
            && (brain->flags & PLAYER_BRAIN_GOT_ALIVE_RESPONSE)) {
        begin_packet(&send_cursor, CBP_KEEP_ALIVE);
        net_write_ulong(&send_cursor, serv->current_tick);
        finish_packet_and_send(&send_cursor, brain);

        brain->last_keep_alive_sent_tick = serv->current_tick;
        brain->flags &= ~PLAYER_BRAIN_GOT_ALIVE_RESPONSE;
    }

    if ((player->flags & ENTITY_TELEPORTING)
            && !(brain->flags & PLAYER_BRAIN_SENT_TELEPORT)) {
        begin_packet(&send_cursor, CBP_PLAYER_POSITION);
        net_write_double(&send_cursor, player->x);
        net_write_double(&send_cursor, player->y);
        net_write_double(&send_cursor, player->z);
        net_write_float(&send_cursor, player->player.head_rot_y);
        net_write_float(&send_cursor, player->player.head_rot_x);
        net_write_ubyte(&send_cursor, 0); // relative arguments
        net_write_varint(&send_cursor, brain->current_teleport_id);
        finish_packet_and_send(&send_cursor, brain);

        brain->flags |= PLAYER_BRAIN_SENT_TELEPORT;
    }

    begin_timed_block("update chunk cache");

    mc_short chunk_cache_min_x = brain->chunk_cache_centre_x - brain->chunk_cache_radius;
    mc_short chunk_cache_min_z = brain->chunk_cache_centre_z - brain->chunk_cache_radius;
    mc_short chunk_cache_max_x = brain->chunk_cache_centre_x + brain->chunk_cache_radius;
    mc_short chunk_cache_max_z = brain->chunk_cache_centre_z + brain->chunk_cache_radius;

    // @TODO(traks) there's also an intrinsic for flooring multiple doubles
    // at once. Figure out what systems can use that and perhaps move all
    // this intrinsics stuff into vector math functions with fallback to
    // stdlib scalar operations. Although shifting negative numbers is
    // undefined...
    __m128d floored_xz = _mm_set_pd(floor(player->z), floor(player->x));
    __m128i floored_int_xz = _mm_cvtpd_epi32(floored_xz);
    __m128i new_centre = _mm_srai_epi32(floored_int_xz, 4);
    mc_short new_chunk_cache_centre_x = _mm_extract_epi32(new_centre, 0);
    mc_short new_chunk_cache_centre_z = _mm_extract_epi32(new_centre, 1);
    assert(brain->new_chunk_cache_radius <= MAX_CHUNK_CACHE_RADIUS);
    mc_short new_chunk_cache_min_x = new_chunk_cache_centre_x - brain->new_chunk_cache_radius;
    mc_short new_chunk_cache_min_z = new_chunk_cache_centre_z - brain->new_chunk_cache_radius;
    mc_short new_chunk_cache_max_x = new_chunk_cache_centre_x + brain->new_chunk_cache_radius;
    mc_short new_chunk_cache_max_z = new_chunk_cache_centre_z + brain->new_chunk_cache_radius;

    if (brain->chunk_cache_centre_x != new_chunk_cache_centre_x
            || brain->chunk_cache_centre_z != new_chunk_cache_centre_z) {
        begin_packet(&send_cursor, CBP_SET_CHUNK_CACHE_CENTRE);
        net_write_varint(&send_cursor, new_chunk_cache_centre_x);
        net_write_varint(&send_cursor, new_chunk_cache_centre_z);
        finish_packet_and_send(&send_cursor, brain);
    }

    if (brain->chunk_cache_radius != brain->new_chunk_cache_radius) {
        begin_packet(&send_cursor, CBP_SET_CHUNK_CACHE_RADIUS);
        net_write_varint(&send_cursor, brain->new_chunk_cache_radius);
        finish_packet_and_send(&send_cursor, brain);
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
                if (!brain->chunk_cache[index].sent) {
                    continue;
                }

                chunk * ch = get_chunk_if_loaded(pos);
                assert(ch != NULL);
                if (ch->changed_block_count == 0) {
                    continue;
                }

                begin_packet(&send_cursor, CBP_CHUNK_BLOCKS_UPDATE);
                net_write_int(&send_cursor, x);
                net_write_int(&send_cursor, z);
                net_write_varint(&send_cursor, ch->changed_block_count);

                for (int i = 0; i < ch->changed_block_count; i++) {
                    mc_ushort pos = ch->changed_blocks[i];
                    net_write_ushort(&send_cursor, pos);
                    mc_ushort block_state = chunk_get_block_state(ch,
                            pos >> 12, pos & 0xff, (pos >> 8) & 0xf);
                    net_write_varint(&send_cursor, block_state);
                }
                finish_packet_and_send(&send_cursor, brain);
                continue;
            }

            // old chunk is not in the new region
            chunk * ch = get_chunk_if_available(pos);
            assert(ch != NULL);
            ch->available_interest--;

            if (brain->chunk_cache[index].sent) {
                brain->chunk_cache[index] = (chunk_cache_entry) {0};

                begin_packet(&send_cursor, CBP_FORGET_LEVEL_CHUNK);
                net_write_int(&send_cursor, x);
                net_write_int(&send_cursor, z);
                finish_packet_and_send(&send_cursor, brain);
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

    brain->chunk_cache_radius = brain->new_chunk_cache_radius;
    brain->chunk_cache_centre_x = new_chunk_cache_centre_x;
    brain->chunk_cache_centre_z = new_chunk_cache_centre_z;

    end_timed_block();

    // load and send tracked chunks
    begin_timed_block("load and send chunks");

    // We iterate in a spiral around the player, so chunks near the player
    // are processed first. This shortens server join times (since players
    // don't need to wait for the chunk they are in to load) and allows
    // players to move around much earlier.
    int newly_sent_chunks = 0;
    int newly_loaded_chunks = 0;
    int chunk_cache_diam = 2 * brain->new_chunk_cache_radius + 1;
    int chunk_cache_area = chunk_cache_diam * chunk_cache_diam;
    int off_x = 0;
    int off_z = 0;
    int step_x = 1;
    int step_z = 0;
    for (int i = 0; i < chunk_cache_area; i++) {
        int x = new_chunk_cache_centre_x + off_x;
        int z = new_chunk_cache_centre_z + off_z;
        int cache_index = chunk_cache_index((chunk_pos) {.x = x, .z = z});
        chunk_cache_entry * entry = brain->chunk_cache + cache_index;
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
                send_chunk_fully(&send_cursor, pos, ch, brain);
                send_light_update(&send_cursor, pos, ch, brain);
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
        if (!(player->player.slots_needing_update & ((mc_ulong) 1 << i))) {
            continue;
        }

        logs("Sending slot update for %d", i);
        item_stack * is = player->player.slots + i;

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
        finish_packet_and_send(&send_cursor, brain);
    }

    player->player.slots_needing_update = 0;
    memcpy(player->player.slots_prev_tick, player->player.slots,
            sizeof player->player.slots);

    end_timed_block();

    // tab list updates
    begin_timed_block("send tab list");

    if (!(brain->flags & PLAYER_BRAIN_INITIALISED_TAB_LIST)) {
        brain->flags |= PLAYER_BRAIN_INITIALISED_TAB_LIST;
        if (serv->tab_list_size > 0) {
            begin_packet(&send_cursor, CBP_PLAYER_INFO);
            net_write_varint(&send_cursor, 0); // action: add
            net_write_varint(&send_cursor, serv->tab_list_size);

            for (int i = 0; i < serv->tab_list_size; i++) {
                tab_list_entry * entry = serv->tab_list + i;
                entity_data * entity = resolve_entity(serv, entry->eid);
                assert(entity->type == ENTITY_PLAYER);
                // @TODO(traks) write UUID
                net_write_ulong(&send_cursor, 0);
                net_write_ulong(&send_cursor, entry->eid);
                net_string username = {
                    .ptr = entity->player.username,
                    .size = entity->player.username_size
                };
                net_write_string(&send_cursor, username);
                net_write_varint(&send_cursor, 0); // num properties
                net_write_varint(&send_cursor, entity->player.gamemode);
                net_write_varint(&send_cursor, 0); // latency
                net_write_ubyte(&send_cursor, 0); // has display name
            }
            finish_packet_and_send(&send_cursor, brain);
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
            finish_packet_and_send(&send_cursor, brain);
        }
        if (serv->tab_list_added_count > 0) {
            begin_packet(&send_cursor, CBP_PLAYER_INFO);
            net_write_varint(&send_cursor, 0); // action: add
            net_write_varint(&send_cursor, serv->tab_list_added_count);

            for (int i = 0; i < serv->tab_list_added_count; i++) {
                tab_list_entry * entry = serv->tab_list_added + i;
                entity_data * entity = resolve_entity(serv, entry->eid);
                assert(entity->type == ENTITY_PLAYER);
                // @TODO(traks) write UUID
                net_write_ulong(&send_cursor, 0);
                net_write_ulong(&send_cursor, entry->eid);
                net_string username = {
                    .ptr = entity->player.username,
                    .size = entity->player.username_size
                };
                net_write_string(&send_cursor, username);
                net_write_varint(&send_cursor, 0); // num properties
                net_write_varint(&send_cursor, entity->player.gamemode);
                net_write_varint(&send_cursor, 0); // latency
                net_write_ubyte(&send_cursor, 0); // has display name
            }
            finish_packet_and_send(&send_cursor, brain);
        }
    }

    end_timed_block();

    // entity tracking
    begin_timed_block("track entities");

    entity_id removed_entities[64];
    int removed_entity_count = 0;

    for (int j = 1; j < MAX_ENTITIES; j++) {
        entity_id tracked_eid = brain->tracked_entities[j];
        entity_data * candidate = serv->entities + j;
        entity_id candidate_eid = candidate->eid;

        if (tracked_eid != 0) {
            // entity is currently being tracked
            if ((candidate->flags & ENTITY_IN_USE)
                    && candidate_eid == tracked_eid) {
                // entity is still there
                mc_double dx = candidate->x - player->x;
                mc_double dy = candidate->y - player->y;
                mc_double dz = candidate->z - player->z;
                mc_float rot_x = 0;
                mc_float rot_y = 0;

                switch (candidate->type) {
                case ENTITY_PLAYER:
                    rot_x = candidate->player.head_rot_x;
                    rot_y = candidate->player.head_rot_y;

                    begin_packet(&send_cursor, CBP_ROTATE_HEAD);
                    net_write_varint(&send_cursor, candidate_eid);
                    // @TODO(traks) make sure signed cast to mc_ubyte works
                    net_write_ubyte(&send_cursor, (int)
                            (candidate->player.head_rot_y * 256.0f / 360.0f));
                    finish_packet_and_send(&send_cursor, brain);
                    break;
                }

                if (dx * dx + dy * dy + dz * dz < 45 * 45) {
                    begin_packet(&send_cursor, CBP_TELEPORT_ENTITY);
                    net_write_varint(&send_cursor, tracked_eid);
                    net_write_double(&send_cursor, candidate->x);
                    net_write_double(&send_cursor, candidate->y);
                    net_write_double(&send_cursor, candidate->z);
                    // @TODO(traks) make sure signed cast to mc_ubyte works
                    net_write_ubyte(&send_cursor, (int) (rot_y * 256.0f / 360.0f));
                    net_write_ubyte(&send_cursor, (int) (rot_x * 256.0f / 360.0f));
                    net_write_ubyte(&send_cursor, !!(candidate->flags & ENTITY_ON_GROUND));
                    finish_packet_and_send(&send_cursor, brain);
                    continue;
                }
            }

            // entity we tracked is gone or too far away

            if (removed_entity_count == ARRAY_SIZE(removed_entities)) {
                // no more space to untrack, try again next tick
                continue;
            }

            brain->tracked_entities[j] = 0;
            removed_entities[removed_entity_count] = tracked_eid;
            removed_entity_count++;
        }

        if ((candidate->flags & ENTITY_IN_USE) && candidate_eid != brain->eid) {
            // candidate is valid for being newly tracked
            mc_double dx = candidate->x - player->x;
            mc_double dy = candidate->y - player->y;
            mc_double dz = candidate->z - player->z;

            if (dx * dx + dy * dy + dz * dz > 40 * 40) {
                continue;
            }

            switch (candidate->type) {
            case ENTITY_PLAYER: {
                begin_packet(&send_cursor, CBP_ADD_PLAYER);
                net_write_varint(&send_cursor, candidate_eid);
                // @TODO(traks) appropriate UUID
                net_write_ulong(&send_cursor, 0);
                net_write_ulong(&send_cursor, candidate_eid);
                net_write_double(&send_cursor, candidate->x);
                net_write_double(&send_cursor, candidate->y);
                net_write_double(&send_cursor, candidate->z);
                // @TODO(traks) make sure signed cast to mc_ubyte works
                net_write_ubyte(&send_cursor, (int)
                        (candidate->player.head_rot_y * 256.0f / 360.0f));
                net_write_ubyte(&send_cursor, (int)
                        (candidate->player.head_rot_x * 256.0f / 360.0f));
                finish_packet_and_send(&send_cursor, brain);

                begin_packet(&send_cursor, CBP_ROTATE_HEAD);
                net_write_varint(&send_cursor, candidate_eid);
                // @TODO(traks) make sure signed cast to mc_ubyte works
                net_write_ubyte(&send_cursor, (int)
                        (candidate->player.head_rot_y * 256.0f / 360.0f));
                finish_packet_and_send(&send_cursor, brain);
                break;
            case ENTITY_ITEM: {
                // begin_packet(&send_cursor, CBP_ADD_MOB);
                // net_write_varint(&send_cursor, candidate_eid);
                // // @TODO(traks) appropriate UUID
                // net_write_ulong(&send_cursor, 0);
                // net_write_ulong(&send_cursor, 0);
                // net_write_varint(&send_cursor, ENTITY_SQUID);
                // net_write_double(&send_cursor, candidate->x);
                // net_write_double(&send_cursor, candidate->y);
                // net_write_double(&send_cursor, candidate->z);
                // // @TODO(traks) make sure signed cast to mc_ubyte works
                // net_write_ubyte(&send_cursor, 0);
                // net_write_ubyte(&send_cursor, 0);
                // // @TODO(traks) y head rotation (what is that?)
                // net_write_ubyte(&send_cursor, 0);
                // // @TODO(traks) entity velocity
                // net_write_short(&send_cursor, 0);
                // net_write_short(&send_cursor, 0);
                // net_write_short(&send_cursor, 0);
                // finish_packet_and_send(&send_cursor, brain);
                begin_packet(&send_cursor, CBP_ADD_ENTITY);
                net_write_varint(&send_cursor, candidate_eid);
                // @TODO(traks) appropriate UUID
                net_write_ulong(&send_cursor, 0);
                net_write_ulong(&send_cursor, candidate_eid);
                net_write_varint(&send_cursor, candidate->type);
                net_write_double(&send_cursor, candidate->x);
                net_write_double(&send_cursor, candidate->y);
                net_write_double(&send_cursor, candidate->z);
                // @TODO(traks) x and y rotation
                net_write_ubyte(&send_cursor, 0);
                net_write_ubyte(&send_cursor, 0);
                // @TODO(traks) entity data
                net_write_int(&send_cursor, 0);
                // @TODO(traks) velocity
                net_write_short(&send_cursor, 0);
                net_write_short(&send_cursor, 0);
                net_write_short(&send_cursor, 0);
                finish_packet_and_send(&send_cursor, brain);

                begin_packet(&send_cursor, CBP_SET_ENTITY_DATA);
                net_write_varint(&send_cursor, candidate_eid);

                net_write_ubyte(&send_cursor, 7); // set item contents
                net_write_varint(&send_cursor, 6); // data type: item stack

                net_write_ubyte(&send_cursor, 1); // has item
                item_stack * is = &candidate->item.contents;
                net_write_varint(&send_cursor, is->type);
                net_write_ubyte(&send_cursor, is->size);
                // @TODO(traks) write NBT (currently just a single end tag)
                net_write_ubyte(&send_cursor, 0);

                net_write_ubyte(&send_cursor, 0xff); // end of entity data
                finish_packet_and_send(&send_cursor, brain);
                break;
            }
            default:
                continue;
            }
            }

            mc_uint entity_index = candidate_eid & ENTITY_INDEX_MASK;
            brain->tracked_entities[entity_index] = candidate_eid;
        }
    }

    if (removed_entity_count > 0) {
        begin_packet(&send_cursor, CBP_REMOVE_ENTITIES);
        net_write_varint(&send_cursor, removed_entity_count);
        for (int i = 0; i < removed_entity_count; i++) {
            net_write_varint(&send_cursor, removed_entities[i]);
        }
        finish_packet_and_send(&send_cursor, brain);
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
        finish_packet_and_send(&send_cursor, brain);
    }

    end_timed_block();

    // try to write everything to the socket buffer

    assert(send_cursor.error == 0);

    int sock = brain->sock;

    begin_timed_block("send()");
    ssize_t send_size = send(sock, brain->send_buf,
            brain->send_cursor, 0);
    end_timed_block();

    if (send_size == -1) {
        // EAGAIN means no data sent
        if (errno != EAGAIN) {
            logs_errno("Couldn't send protocol data: %s");
            disconnect_player_now(brain, serv);
        }
    } else {
        memmove(brain->send_buf, brain->send_buf + send_size,
                brain->send_cursor - send_size);
        brain->send_cursor -= send_size;
    }

    end_timed_block();
}
