#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <x86intrin.h>
#include <math.h>
#include "shared.h"

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
    entity->player.body_rot_y = new_rot_y;
}

static void
process_move_player_packet(entity_data * entity,
        mc_double new_x, mc_double new_y, mc_double new_z,
        mc_float new_head_rot_x, mc_float new_head_rot_y, int on_ground) {
    if ((entity->flags & ENTITY_TELEPORTING) != 0) {
        return;
    }

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
send_chunk_fully(buffer_cursor * send_cursor, chunk_pos pos, chunk * ch) {
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

    int out_size = net_varint_size(33) + 4 + 4 + 1 + 1 + net_varint_size(section_mask)
            + 1 + 2 + 1 + 2 + height_map_name.size + 4 + 36 * 8 + 1
            + 1024 * 4
            + net_varint_size(section_data_size) + section_data_size
            + net_varint_size(0);

    // send level chunk packet
    net_write_varint(send_cursor, out_size);
    int packet_start = send_cursor->index;
    net_write_varint(send_cursor, 33);
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
    // @TODO(traks) got
    //
    //  IndexOutOfBoundsException: readerIndex(47463) + length(1) exceeds
    //  writerIndex(47463): PooledUnsafeDirectByteBuf(...)
    //
    // Presumably this was a chunk packet the client tried to read but somehow
    // they ended up outside the packet buffer.
    assert(out_size == send_cursor->index - packet_start);

    end_timed_block();
}

static void
send_light_update(buffer_cursor * send_cursor, chunk_pos pos, chunk * ch) {
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

    mc_int out_size = net_varint_size(36)
            + net_varint_size(pos.x) + net_varint_size(pos.z) + 1
            + net_varint_size(sky_light_mask)
            + net_varint_size(block_light_mask)
            + net_varint_size(zero_sky_light_mask)
            + net_varint_size(zero_block_light_mask);

    for (int i = 0; i < 18; i++) {
        if (sky_light_mask & (1 << i)) {
            out_size += net_varint_size(2048) + 2048;
        }
        if (block_light_mask & (1 << i)) {
            out_size += net_varint_size(2048) + 2048;
        }
    }

    // send light update packet
    net_write_varint(send_cursor, out_size);
    net_write_varint(send_cursor, 36);
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

void
tick_player_brain(player_brain * brain, server * serv) {
    begin_timed_block("tick player");

    entity_data * entity = resolve_entity(serv, brain->eid);
    assert(entity->type == ENTITY_PLAYER);
    int sock = brain->sock;
    ssize_t rec_size = recv(sock, brain->rec_buf + brain->rec_cursor,
            sizeof brain->rec_buf - brain->rec_cursor, 0);

    mc_double start_move_x = entity->x;
    mc_double start_move_y = entity->y;
    mc_double start_move_z = entity->z;

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
            mc_int packet_id = net_read_varint(&rec_cursor);

            switch (packet_id) {
            case 0: { // accept teleport
                mc_int teleport_id = net_read_varint(&rec_cursor);

                if ((entity->flags & ENTITY_TELEPORTING)
                        && (brain->flags & PLAYER_BRAIN_SENT_TELEPORT)
                        && teleport_id == brain->current_teleport_id) {
                    entity->flags &= ~ENTITY_TELEPORTING;
                    brain->flags &= ~PLAYER_BRAIN_SENT_TELEPORT;
                }
                break;
            }
            case 1: { // block entity tag query
                logs("Packet block entity tag query");
                mc_int id = net_read_varint(&rec_cursor);
                mc_ulong block_pos = net_read_ulong(&rec_cursor);
                // @TODO(traks) handle packet
                break;
            }
            case 2: { // change difficulty
                logs("Packet change difficulty");
                mc_ubyte difficulty = net_read_ubyte(&rec_cursor);
                // @TODO(traks) handle packet
                break;
            }
            case 3: { // chat
                net_string chat = net_read_string(&rec_cursor, 256);

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
            case 4: { // client command
                logs("Packet client command");
                mc_int action = net_read_varint(&rec_cursor);
                break;
            }
            case 5: { // client information
                logs("Packet client information");
                net_string language = net_read_string(&rec_cursor, 16);
                mc_ubyte view_distance = net_read_ubyte(&rec_cursor);
                mc_int chat_visibility = net_read_varint(&rec_cursor);
                mc_ubyte sees_chat_colours = net_read_ubyte(&rec_cursor);
                mc_ubyte model_customisation = net_read_ubyte(&rec_cursor);
                mc_int main_hand = net_read_varint(&rec_cursor);

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
            case 6: { // command suggestion
                logs("Packet command suggestion");
                mc_int id = net_read_varint(&rec_cursor);
                net_string command = net_read_string(&rec_cursor, 32500);
                break;
            }
            case 7: { // container ack
                logs("Packet container ack");
                mc_ubyte container_id = net_read_ubyte(&rec_cursor);
                mc_ushort uid = net_read_ushort(&rec_cursor);
                mc_ubyte accepted = net_read_ubyte(&rec_cursor);
                break;
            }
            case 8: { // container button click
                logs("Packet container button click");
                mc_ubyte container_id = net_read_ubyte(&rec_cursor);
                mc_ubyte button_id = net_read_ubyte(&rec_cursor);
                break;
            }
            case 9: { // container click
                logs("Packet container click");
                mc_ubyte container_id = net_read_ubyte(&rec_cursor);
                mc_ushort slot = net_read_ushort(&rec_cursor);
                mc_ubyte button = net_read_ubyte(&rec_cursor);
                mc_ushort uid = net_read_ushort(&rec_cursor);
                mc_int click_type = net_read_varint(&rec_cursor);
                // @TODO(traks) read item
                break;
            }
            case 10: { // container close
                logs("Packet container close");
                mc_ubyte container_id = net_read_ubyte(&rec_cursor);
                break;
            }
            case 11: { // custom payload
                logs("Packet custom payload");
                net_string id = net_read_string(&rec_cursor, 32767);
                unsigned char * payload = rec_cursor.buf + rec_cursor.index;
                mc_int payload_size = packet_start + packet_size
                        - rec_cursor.index;

                if (payload_size > 32767) {
                    // custom payload size too large
                    rec_cursor.error = 1;
                    break;
                }

                rec_cursor.index += payload_size;
                break;
            }
            case 12: { // edit book
                logs("Packet edit book");
                // @TODO(traks) read packet
                break;
            }
            case 13: { // entity tag query
                logs("Packet entity tag query");
                mc_int transaction_id = net_read_varint(&rec_cursor);
                mc_int entity_id = net_read_varint(&rec_cursor);
                break;
            }
            case 14: { // interact
                logs("Packet interact");
                mc_int entity_id = net_read_varint(&rec_cursor);
                mc_int action = net_read_varint(&rec_cursor);
                // @TODO further reading
                break;
            }
            case 15: { // jigsaw generate
                logs("Packet jigsaw generate");
                net_block_pos block_pos = net_read_block_pos(&rec_cursor);
                mc_int levels = net_read_varint(&rec_cursor);
                mc_ubyte keep_jigsaws = net_read_ubyte(&rec_cursor);
                // @TODO(traks) processing
                break;
            }
            case 16: { // keep alive
                mc_ulong id = net_read_ulong(&rec_cursor);
                if (brain->last_keep_alive_sent_tick == id) {
                    brain->flags |= PLAYER_BRAIN_GOT_ALIVE_RESPONSE;
                }
                break;
            }
            case 17: { // lock difficulty
                logs("Packet lock difficulty");
                mc_ubyte locked = net_read_ubyte(&rec_cursor);
                break;
            }
            case 18: { // move player pos
                mc_double x = net_read_double(&rec_cursor);
                mc_double y = net_read_double(&rec_cursor);
                mc_double z = net_read_double(&rec_cursor);
                int on_ground = net_read_ubyte(&rec_cursor);
                process_move_player_packet(entity, x, y, z,
                        entity->player.head_rot_x,
                        entity->player.head_rot_y, on_ground);
                break;
            }
            case 19: { // move player pos rot
                mc_double x = net_read_double(&rec_cursor);
                mc_double y = net_read_double(&rec_cursor);
                mc_double z = net_read_double(&rec_cursor);
                mc_float head_rot_y = net_read_float(&rec_cursor);
                mc_float head_rot_x = net_read_float(&rec_cursor);
                int on_ground = net_read_ubyte(&rec_cursor);
                process_move_player_packet(entity, x, y, z,
                        head_rot_x, head_rot_y, on_ground);
                break;
            }
            case 20: { // move player rot
                mc_float head_rot_y = net_read_float(&rec_cursor);
                mc_float head_rot_x = net_read_float(&rec_cursor);
                int on_ground = net_read_ubyte(&rec_cursor);
                process_move_player_packet(entity,
                        entity->x, entity->y, entity->z,
                        head_rot_x, head_rot_y, on_ground);
                break;
            }
            case 21: { // move player
                int on_ground = net_read_ubyte(&rec_cursor);
                process_move_player_packet(entity,
                        entity->x, entity->y, entity->z,
                        entity->player.head_rot_x,
                        entity->player.head_rot_y, on_ground);
                break;
            }
            case 22: { // move vehicle
                logs("Packet move vehicle");
                // @TODO(traks) read packet
                break;
            }
            case 23: { // paddle boat
                logs("Packet paddle boat");
                mc_ubyte left = net_read_ubyte(&rec_cursor);
                mc_ubyte right = net_read_ubyte(&rec_cursor);
                break;
            }
            case 24: { // pick item
                logs("Packet pick item");
                mc_int slot = net_read_varint(&rec_cursor);
                break;
            }
            case 25: { // place recipe
                logs("Packet place recipe");
                mc_ubyte container_id = net_read_ubyte(&rec_cursor);
                // @TODO read recipe
                mc_ubyte shift_down = net_read_ubyte(&rec_cursor);
                break;
            }
            case 26: { // player abilities
                logs("Packet player abilities");
                mc_ubyte flags = net_read_ubyte(&rec_cursor);
                mc_ubyte flying = flags & 0x2;
                // @TODO(traks) process packet
                break;
            }
            case 27: { // player action
                mc_int action = net_read_varint(&rec_cursor);
                // @TODO(traks) validate block pos inside world
                net_block_pos block_pos = net_read_block_pos(&rec_cursor);
                mc_ubyte direction = net_read_ubyte(&rec_cursor);

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
                    entity->player.slots[sel_slot] = (item_stack) {0};
                    break;
                }
                case 4: { // drop item
                    // @TODO(traks) create item entity
                    int sel_slot = entity->player.selected_slot;
                    item_stack * is = entity->player.slots + sel_slot;
                    if (is->size > 0) {
                        is->size--;
                    } else {
                        *is = (item_stack) {0};
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
                    rec_cursor.error = 1;
                }
                break;
            }
            case 28: { // player command
                mc_int id = net_read_varint(&rec_cursor);
                mc_int action = net_read_varint(&rec_cursor);
                mc_int data = net_read_varint(&rec_cursor);

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
                    rec_cursor.error = 1;
                }
                break;
            }
            case 29: { // player input
                logs("Packet player input");
                // @TODO(traks) read packet
                break;
            }
            case 30: { // recipe book update
                logs("Packet recipe book update");
                // @TODO(traks) read packet
                break;
            }
            case 31: { // rename item
                logs("Packet rename item");
                net_string name = net_read_string(&rec_cursor, 32767);
                break;
            }
            case 32: { // resource pack
                logs("Packet resource pack");
                mc_int action = net_read_varint(&rec_cursor);
                break;
            }
            case 33: { // seen advancements
                logs("Packet seen advancements");
                mc_int action = net_read_varint(&rec_cursor);
                // @TODO(traks) further processing
                break;
            }
            case 34: { // select trade
                logs("Packet select trade");
                mc_int item = net_read_varint(&rec_cursor);
                break;
            }
            case 35: { // set beacon
                logs("Packet set beacon");
                mc_int primary_effect = net_read_varint(&rec_cursor);
                mc_int secondary_effect = net_read_varint(&rec_cursor);
                break;
            }
            case 36: { // set carried item
                mc_ushort slot = net_read_ushort(&rec_cursor);
                if (slot > PLAYER_LAST_HOTBAR_SLOT - PLAYER_FIRST_HOTBAR_SLOT) {
                    rec_cursor.error = 1;
                    break;
                }
                entity->player.selected_slot = PLAYER_FIRST_HOTBAR_SLOT + slot;
                break;
            }
            case 37: { // set command block
                logs("Packet set command block");
                mc_ulong block_pos = net_read_ulong(&rec_cursor);
                net_string command = net_read_string(&rec_cursor, 32767);
                mc_int mode = net_read_varint(&rec_cursor);
                mc_ubyte flags = net_read_ubyte(&rec_cursor);
                mc_ubyte track_output = (flags & 0x1);
                mc_ubyte conditional = (flags & 0x2);
                mc_ubyte automatic = (flags & 0x4);
                break;
            }
            case 38: { // set command minecart
                logs("Packet set command minecart");
                mc_int entity_id = net_read_varint(&rec_cursor);
                net_string command = net_read_string(&rec_cursor, 32767);
                mc_ubyte track_output = net_read_ubyte(&rec_cursor);
                break;
            }
            case 39: { // set creative mode slot
                mc_ushort slot = net_read_ushort(&rec_cursor);
                mc_ubyte has_item = net_read_ubyte(&rec_cursor);

                if (slot >= PLAYER_SLOTS) {
                    rec_cursor.error = 1;
                    break;
                }

                item_stack * is = entity->player.slots + slot;
                *is = (item_stack) {0};

                if (has_item) {
                    is->type = net_read_varint(&rec_cursor);
                    is->size = net_read_ubyte(&rec_cursor);

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

                    memory_arena scratch_arena = {
                        .ptr = serv->short_lived_scratch,
                        .size = serv->short_lived_scratch_size
                    };
                    // @TODO(traks) better value than 64 for the max level
                    nbt_tape_entry * tape = load_nbt(&rec_cursor,
                            &scratch_arena, 64);
                    if (rec_cursor.error) {
                        break;
                    }

                    // @TODO(traks) use NBT data to construct item stack
                }
                break;
            }
            case 40: { // set jigsaw block
                logs("Packet set jigsaw block");
                mc_ulong block_pos = net_read_ulong(&rec_cursor);
                // @TODO(traks) further reading
                break;
            }
            case 41: { // set structure block
                logs("Packet set structure block");
                mc_ulong block_pos = net_read_ulong(&rec_cursor);
                mc_int update_type = net_read_varint(&rec_cursor);
                mc_int mode = net_read_varint(&rec_cursor);
                net_string name = net_read_string(&rec_cursor, 32767);
                // @TODO(traks) read signed bytes instead
                mc_ubyte offset_x = net_read_ubyte(&rec_cursor);
                mc_ubyte offset_y = net_read_ubyte(&rec_cursor);
                mc_ubyte offset_z = net_read_ubyte(&rec_cursor);
                mc_ubyte size_x = net_read_ubyte(&rec_cursor);
                mc_ubyte size_y = net_read_ubyte(&rec_cursor);
                mc_ubyte size_z = net_read_ubyte(&rec_cursor);
                mc_int mirror = net_read_varint(&rec_cursor);
                mc_int rotation = net_read_varint(&rec_cursor);
                net_string data = net_read_string(&rec_cursor, 12);
                // @TODO(traks) further reading
                break;
            }
            case 42: { // sign update
                logs("Packet sign update");
                mc_ulong block_pos = net_read_ulong(&rec_cursor);
                net_string lines[4];
                for (int i = 0; i < ARRAY_SIZE(lines); i++) {
                    lines[i] = net_read_string(&rec_cursor, 384);
                }
                break;
            }
            case 43: { // swing
                logs("Packet swing");
                mc_int hand = net_read_varint(&rec_cursor);
                break;
            }
            case 44: { // teleport to entity
                logs("Packet teleport to entity");
                // @TODO(traks) read UUID instead
                mc_ulong uuid_high = net_read_ulong(&rec_cursor);
                mc_ulong uuid_low = net_read_ulong(&rec_cursor);
                break;
            }
            case 45: { // use item on
                mc_int hand = net_read_varint(&rec_cursor);
                net_block_pos clicked_pos = net_read_block_pos(&rec_cursor);
                mc_int clicked_face = net_read_varint(&rec_cursor);
                mc_float click_offset_x = net_read_float(&rec_cursor);
                mc_float click_offset_y = net_read_float(&rec_cursor);
                mc_float click_offset_z = net_read_float(&rec_cursor);
                // @TODO(traks) figure out what this is used for
                mc_ubyte is_inside = net_read_ubyte(&rec_cursor);

                // @TODO(traks) if we cancel at any point and don't kick the
                // client, send some packets to the client to make the
                // original blocks reappear, otherwise we'll get a desync

                if (hand != 0 && hand != 1) {
                    rec_cursor.error = 1;
                    break;
                }
                if (clicked_face < 0 || clicked_face >= 6) {
                    rec_cursor.error = 1;
                    break;
                }
                if (click_offset_x < 0 || click_offset_x > 1
                        || click_offset_y < 0 || click_offset_y > 1
                        || click_offset_z < 0 || click_offset_z > 1) {
                    rec_cursor.error = 1;
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
            case 46: { // use item
                logs("Packet use item");
                mc_int hand = net_read_varint(&rec_cursor);
                break;
            }
            default: {
                logs("Unknown player packet id %jd", (intmax_t) packet_id);
                rec_cursor.error = 1;
            }
            }

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
        mc_double move_dx = entity->x - start_move_x;
        mc_double move_dy = entity->y - start_move_y;
        mc_double move_dz = entity->z - start_move_z;

        // @TODO(traks) rotate player body depending on head rotation and
        // direction the player is moving to
    }

bail:
    end_timed_block();
}

void
send_packets_to_player(player_brain * brain, server * serv) {
    begin_timed_block("send packets");

    entity_data * player = resolve_entity(serv, brain->eid);

    // first write all the new packets to our own outgoing packet buffer

    buffer_cursor send_cursor = {
        .limit = sizeof brain->send_buf,
        .buf = brain->send_buf,
        .index = brain->send_cursor
    };

    if (!(brain->flags & PLAYER_BRAIN_DID_INIT_PACKETS)) {
        brain->flags |= PLAYER_BRAIN_DID_INIT_PACKETS;

        // send game profile packet
        net_string username = {
            .size = player->player.username_size,
            .ptr = player->player.username
        };
        int out_size = net_varint_size(2) + 16
                + net_varint_size(username.size) + username.size;
        net_write_varint(&send_cursor, out_size);
        net_write_varint(&send_cursor, 2);
        net_write_ulong(&send_cursor, 0x0123456789abcdef);
        net_write_ulong(&send_cursor, 0x0123456789abcdef);
        net_write_string(&send_cursor, username);

        // send login packet
        net_string level_name = NET_STRING("blaze:main");
        net_string dimension_type = NET_STRING("minecraft:overworld");
        out_size = net_varint_size(37) + 4 + 1 + 1
                + net_varint_size(1)
                + net_varint_size(level_name.size) + level_name.size;

        // dimension type nbt
        out_size += 1 + 2;
        net_string dimension_str = NET_STRING("dimension");
        out_size += 1 + 2 + dimension_str.size + 1 + 4;

        net_string name_str = NET_STRING("name");
        out_size += 1 + 2 + name_str.size + 2 + dimension_type.size;
        net_string has_skylight = NET_STRING("has_skylight");
        out_size += 1 + 2 + has_skylight.size + 1;
        net_string has_ceiling = NET_STRING("has_ceiling");
        out_size += 1 + 2 + has_ceiling.size + 1;
        net_string ultrawarm = NET_STRING("ultrawarm");
        out_size += 1 + 2 + ultrawarm.size + 1;
        net_string natural = NET_STRING("natural");
        out_size += 1 + 2 + natural.size + 1;
        net_string shrunk = NET_STRING("shrunk");
        out_size += 1 + 2 + shrunk.size + 1;
        net_string piglin_safe = NET_STRING("piglin_safe");
        out_size += 1 + 2 + piglin_safe.size + 1;
        net_string bed_works = NET_STRING("bed_works");
        out_size += 1 + 2 + bed_works.size + 1;
        net_string respawn_anchor_works = NET_STRING("respawn_anchor_works");
        out_size += 1 + 2 + respawn_anchor_works.size + 1;
        net_string has_raids = NET_STRING("has_raids");
        out_size += 1 + 2 + has_raids.size + 1;
        net_string logical_height = NET_STRING("logical_height");
        out_size += 1 + 2 + logical_height.size + 4;
        net_string infiniburn = NET_STRING("infiniburn");
        net_string infiniburn_tag = NET_STRING("minecraft:infiniburn_overworld");
        out_size += 1 + 2 + infiniburn.size + 2 + infiniburn_tag.size;
        net_string ambient_light = NET_STRING("ambient_light");
        out_size += 1 + 2 + ambient_light.size + 4;

        out_size += 1 + 1; // end tags

        out_size += net_varint_size(dimension_type.size) + dimension_type.size
                + net_varint_size(level_name.size) + level_name.size
                + 8 + 1
                + net_varint_size(brain->new_chunk_cache_radius - 1)
                + 1 + 1 + 1 + 1;

        net_write_varint(&send_cursor, out_size);
        int start = send_cursor.index;
        net_write_varint(&send_cursor, 37);
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

        // send set carried item packet
        net_write_varint(&send_cursor, net_varint_size(63) + 1);
        net_write_varint(&send_cursor, 63);
        net_write_ubyte(&send_cursor, player->player.selected_slot
                - PLAYER_FIRST_HOTBAR_SLOT);

        // send update tags packet
        // out_size = net_varint_size(91)
        //         + net_varint_size(0)
        //         + net_varint_size(0)
        //         + net_varint_size(0)
        //         + net_varint_size(0);
        // net_write_varint(&send_cursor, out_size);
        // net_write_varint(&send_cursor, 91);
        // // block tags
        // net_write_varint(&send_cursor, 0);
        // // item tags
        // net_write_varint(&send_cursor, 0);
        // // entity tags
        // net_write_varint(&send_cursor, 0);
        // // fluid tags
        // net_write_varint(&send_cursor, 0);

        // send custom payload packet
        net_string brand_str = NET_STRING("minecraft:brand");
        net_string brand = NET_STRING("blaze");
        out_size = net_varint_size(24)
                + net_varint_size(brand_str.size) + brand_str.size
                + net_varint_size(brand.size) + brand.size;
        net_write_varint(&send_cursor, out_size);
        net_write_varint(&send_cursor, 24);
        net_write_string(&send_cursor, brand_str);
        net_write_string(&send_cursor, brand);

        // send change difficulty packet
        out_size = net_varint_size(13) + 1 + 1;
        net_write_varint(&send_cursor, out_size);
        net_write_varint(&send_cursor, 13);
        net_write_ubyte(&send_cursor, 2); // difficulty normal
        net_write_ubyte(&send_cursor, 0); // locked

        // send player abilities packet
        out_size = net_varint_size(49) + 1 + 4 + 4;
        net_write_varint(&send_cursor, out_size);
        net_write_varint(&send_cursor, 49);
        // @NOTE(traks) actually need abilities to make creative mode players
        // be able to fly
        // bitmap: invulnerable, (is flying), can fly, instabuild
        mc_ubyte ability_flags = 0x4;
        net_write_ubyte(&send_cursor, ability_flags);
        net_write_float(&send_cursor, 0.05); // flying speed
        net_write_float(&send_cursor, 0.1); // walking speed
    }

    // send keep alive packet every so often
    if (serv->current_tick - brain->last_keep_alive_sent_tick >= KEEP_ALIVE_SPACING
            && (brain->flags & PLAYER_BRAIN_GOT_ALIVE_RESPONSE)) {
        // send keep alive packet
        net_write_varint(&send_cursor, net_varint_size(32) + 8);
        net_write_varint(&send_cursor, 32);
        net_write_ulong(&send_cursor, serv->current_tick);

        brain->last_keep_alive_sent_tick = serv->current_tick;
        brain->flags &= ~PLAYER_BRAIN_GOT_ALIVE_RESPONSE;
    }

    if ((player->flags & ENTITY_TELEPORTING)
            && !(brain->flags & PLAYER_BRAIN_SENT_TELEPORT)) {
        // send player position packet
        int out_size = net_varint_size(53) + 8 + 8 + 8 + 4 + 4 + 1
                + net_varint_size(brain->current_teleport_id);
        net_write_varint(&send_cursor, out_size);
        net_write_varint(&send_cursor, 53);
        net_write_double(&send_cursor, player->x);
        net_write_double(&send_cursor, player->y);
        net_write_double(&send_cursor, player->z);
        net_write_float(&send_cursor, player->player.head_rot_y);
        net_write_float(&send_cursor, player->player.head_rot_x);
        net_write_ubyte(&send_cursor, 0); // relative arguments
        net_write_varint(&send_cursor, brain->current_teleport_id);

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
        // send set chunk cache centre packet
        int out_size = net_varint_size(64)
                + net_varint_size(new_chunk_cache_centre_x)
                + net_varint_size(new_chunk_cache_centre_z);
        net_write_varint(&send_cursor, out_size);
        net_write_varint(&send_cursor, 64);
        net_write_varint(&send_cursor, new_chunk_cache_centre_x);
        net_write_varint(&send_cursor, new_chunk_cache_centre_z);
    }

    if (brain->chunk_cache_radius != brain->new_chunk_cache_radius) {
        // send set chunk cache radius packet
        int out_size = net_varint_size(65)
                + net_varint_size(brain->new_chunk_cache_radius);
        net_write_varint(&send_cursor, out_size);
        net_write_varint(&send_cursor, 65);
        net_write_varint(&send_cursor, brain->new_chunk_cache_radius);
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

                // send chunk blocks update packet
                int out_size = net_varint_size(15) + 2 * 4
                        + net_varint_size(ch->changed_block_count);

                // @TODO(traks) less duplication between this and the part
                // below
                for (int i = 0; i < ch->changed_block_count; i++) {
                    mc_ushort pos = ch->changed_blocks[i];
                    mc_ushort block_state = chunk_get_block_state(ch,
                            pos >> 12, pos & 0xff, (pos >> 8) & 0xf);
                    out_size += 2 + net_varint_size(block_state);
                }

                net_write_varint(&send_cursor, out_size);
                net_write_varint(&send_cursor, 15);
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
                continue;
            }

            // old chunk is not in the new region
            chunk * ch = get_chunk_if_available(pos);
            assert(ch != NULL);
            ch->available_interest--;

            if (brain->chunk_cache[index].sent) {
                brain->chunk_cache[index] = (chunk_cache_entry) {0};

                // send forget level chunk packet
                int out_size = net_varint_size(29) + 4 + 4;
                net_write_varint(&send_cursor, out_size);
                net_write_varint(&send_cursor, 29);
                net_write_int(&send_cursor, x);
                net_write_int(&send_cursor, z);
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
                send_chunk_fully(&send_cursor, pos, ch);
                send_light_update(&send_cursor, pos, ch);
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

        // send container set slot packet
        int out_size = net_varint_size(22) + 1 + 2 + 1;
        if (is->type != 0) {
            out_size += net_varint_size(is->type) + 1 + 1;
        }

        net_write_varint(&send_cursor, out_size);
        net_write_varint(&send_cursor, 22);
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
            // send player info packet
            int out_size = net_varint_size(51) + net_varint_size(0)
                    + net_varint_size(serv->tab_list_size);
            for (int i = 0; i < serv->tab_list_size; i++) {
                tab_list_entry * entry = serv->tab_list + i;
                entity_data * entity = resolve_entity(serv, entry->eid);
                assert(entity->type == ENTITY_PLAYER);
                out_size += 16
                        + net_varint_size(entity->player.username_size)
                        + entity->player.username_size
                        + net_varint_size(0)
                        + net_varint_size(entity->player.gamemode)
                        + net_varint_size(0) + 1;
            }

            net_write_varint(&send_cursor, out_size);
            net_write_varint(&send_cursor, 51);
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
        }
    } else {
        if (serv->tab_list_removed_count > 0) {
            // send player info packet
            int out_size = net_varint_size(51) + net_varint_size(4)
                    + net_varint_size(serv->tab_list_removed_count)
                    + serv->tab_list_removed_count * 16;
            net_write_varint(&send_cursor, out_size);
            net_write_varint(&send_cursor, 51);
            net_write_varint(&send_cursor, 4); // action: remove
            net_write_varint(&send_cursor, serv->tab_list_removed_count);

            for (int i = 0; i < serv->tab_list_removed_count; i++) {
                tab_list_entry * entry = serv->tab_list_removed + i;
                // @TODO(traks) write UUID
                net_write_ulong(&send_cursor, 0);
                net_write_ulong(&send_cursor, entry->eid);
            }
        }
        if (serv->tab_list_added_count > 0) {
            // send player info packet
            int out_size = net_varint_size(51) + net_varint_size(0)
                    + net_varint_size(serv->tab_list_added_count);
            for (int i = 0; i < serv->tab_list_added_count; i++) {
                tab_list_entry * entry = serv->tab_list_added + i;
                entity_data * entity = resolve_entity(serv, entry->eid);
                assert(entity->type == ENTITY_PLAYER);
                out_size += 16
                        + net_varint_size(entity->player.username_size)
                        + entity->player.username_size
                        + net_varint_size(0)
                        + net_varint_size(entity->player.gamemode)
                        + net_varint_size(0) + 1;
            }

            net_write_varint(&send_cursor, out_size);
            net_write_varint(&send_cursor, 51);
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
                    rot_y = candidate->player.body_rot_y;
                    break;
                }

                if (dx * dx + dy * dy + dz * dz < 45 * 45) {
                    // send teleport entity packet
                    int out_size = net_varint_size(86)
                            + net_varint_size(tracked_eid) + 3 * 8 + 3 * 1;
                    net_write_varint(&send_cursor, out_size);
                    net_write_varint(&send_cursor, 86);
                    net_write_varint(&send_cursor, tracked_eid);
                    net_write_double(&send_cursor, candidate->x);
                    net_write_double(&send_cursor, candidate->y);
                    net_write_double(&send_cursor, candidate->z);
                    // @TODO(traks) make sure signed cast to mc_ubyte works
                    net_write_ubyte(&send_cursor, (int) (rot_y * 256.0f / 360.0f));
                    net_write_ubyte(&send_cursor, (int) (rot_x * 256.0f / 360.0f));
                    net_write_ubyte(&send_cursor, !!(candidate->flags & ENTITY_ON_GROUND));

                    // send rotate head packet
                    out_size = net_varint_size(59)
                            + net_varint_size(candidate_eid) + 1;
                    net_write_varint(&send_cursor, out_size);
                    net_write_varint(&send_cursor, 59);
                    net_write_varint(&send_cursor, candidate_eid);
                    // @TODO(traks) make sure signed cast to mc_ubyte works
                    net_write_ubyte(&send_cursor, (int)
                            (candidate->player.head_rot_y * 256.0f / 360.0f));
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
                // // send add mob packet
                // int out_size = net_varint_size(2)
                //         + net_varint_size(candidate_eid)
                //         + 16 + net_varint_size(5)
                //         + 3 * 8 + 3 * 1 + 3 * 2;
                // net_write_varint(&send_cursor, out_size);
                // net_write_varint(&send_cursor, 2);
                // net_write_varint(&send_cursor, candidate_eid);
                // // @TODO(traks) appropriate UUID
                // net_write_ulong(&send_cursor, 0);
                // net_write_ulong(&send_cursor, 0);
                // // @TODO(traks) network entity type
                // net_write_varint(&send_cursor, 5);
                // net_write_double(&send_cursor, candidate->x);
                // net_write_double(&send_cursor, candidate->y);
                // net_write_double(&send_cursor, candidate->z);
                // // @TODO(traks) make sure signed cast to mc_ubyte works
                // net_write_ubyte(&send_cursor, (int) (candidate->rot_y * 256.0f / 360.0f));
                // net_write_ubyte(&send_cursor, (int) (candidate->rot_x * 256.0f / 360.0f));
                // // @TODO(traks) y head rotation (what is that?)
                // net_write_ubyte(&send_cursor, 0);
                // // @TODO(traks) entity velocity
                // net_write_ushort(&send_cursor, 0);
                // net_write_ushort(&send_cursor, 0);
                // net_write_ushort(&send_cursor, 0);

                // send add player packet
                int out_size = net_varint_size(4)
                        + net_varint_size(candidate_eid)
                        + 16 + 3 * 8 + 2 * 1;
                net_write_varint(&send_cursor, out_size);
                net_write_varint(&send_cursor, 4);
                net_write_varint(&send_cursor, candidate_eid);
                // @TODO(traks) appropriate UUID
                net_write_ulong(&send_cursor, 0);
                net_write_ulong(&send_cursor, candidate_eid);
                // @TODO(traks) network entity type
                net_write_double(&send_cursor, candidate->x);
                net_write_double(&send_cursor, candidate->y);
                net_write_double(&send_cursor, candidate->z);
                // @TODO(traks) make sure signed cast to mc_ubyte works
                net_write_ubyte(&send_cursor, (int)
                        (candidate->player.body_rot_y * 256.0f / 360.0f));
                net_write_ubyte(&send_cursor, (int)
                        (candidate->player.head_rot_x * 256.0f / 360.0f));

                // send rotate head packet
                out_size = net_varint_size(59)
                        + net_varint_size(candidate_eid) + 1;
                net_write_varint(&send_cursor, out_size);
                net_write_varint(&send_cursor, 59);
                net_write_varint(&send_cursor, candidate_eid);
                // @TODO(traks) make sure signed cast to mc_ubyte works
                net_write_ubyte(&send_cursor, (int)
                        (candidate->player.head_rot_y * 256.0f / 360.0f));
                break;
            default:
                continue;
            }
            }

            mc_uint entity_index = candidate_eid & ENTITY_INDEX_MASK;
            brain->tracked_entities[entity_index] = candidate_eid;
        }
    }

    if (removed_entity_count > 0) {
        // send remove entities packet
        int out_size = net_varint_size(55)
                + net_varint_size(removed_entity_count);
        for (int i = 0; i < removed_entity_count; i++) {
            out_size += net_varint_size(removed_entities[i]);
        }

        net_write_varint(&send_cursor, out_size);
        net_write_varint(&send_cursor, 55);
        net_write_varint(&send_cursor, removed_entity_count);
        for (int i = 0; i < removed_entity_count; i++) {
            net_write_varint(&send_cursor, removed_entities[i]);
        }
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

        // send chat packet
        int out_size = net_varint_size(14) + net_varint_size(buf_index)
                + buf_index + 1 + 16;
        net_write_varint(&send_cursor, out_size);
        net_write_varint(&send_cursor, 14);
        net_write_varint(&send_cursor, buf_index);
        net_write_data(&send_cursor, buf, buf_index);
        net_write_ubyte(&send_cursor, 0); // chat box position
        // @TODO(traks) write sender UUID. If UUID equals 0, client displays it
        // regardless of client settings
        net_write_ulong(&send_cursor, 0);
        net_write_ulong(&send_cursor, 0);
    }

    end_timed_block();

    // try to write everything to the socket buffer

    assert(send_cursor.error == 0);
    brain->send_cursor = send_cursor.index;

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
