#include <math.h>
#include "shared.h"

typedef struct {
    chunk * ch;
    chunk_pos ch_pos;
    net_block_pos pos;
    mc_ushort cur_state;
    mc_int cur_type;
    int replacing;
} place_target;

typedef struct {
    server * serv;
    entity_base * player;
    net_block_pos clicked_pos;
    mc_int clicked_face;
    float click_offset_x;
    float click_offset_y;
    float click_offset_z;
    memory_arena * scratch_arena;
} place_context;

static int
can_replace(mc_int place_type, mc_int cur_type) {
    // @TODO(traks) this is probably not OK for things like flint and steel,
    // water buckets, etc., for which there is no clear correspondence between
    // the item type and the placed state
    if (place_type == cur_type) {
        return 0;
    }

    switch (cur_type) {
    // air
    case BLOCK_AIR:
    case BLOCK_VOID_AIR:
    case BLOCK_CAVE_AIR:
        // @TODO(traks) this allows players to place blocks in mid-air. This is
        // needed for placing water lilies on top of water. Perhaps we should
        // disallow it for other blocks depending on whether this is the clicked
        // block or the relative block?
    // replaceable plants
    case BLOCK_GRASS:
    case BLOCK_FERN:
    case BLOCK_DEAD_BUSH:
    case BLOCK_VINE:
    case BLOCK_SUNFLOWER:
    case BLOCK_LILAC:
    case BLOCK_ROSE_BUSH:
    case BLOCK_PEONY:
    case BLOCK_TALL_GRASS:
    case BLOCK_LARGE_FERN:
    case BLOCK_WARPED_ROOTS:
    case BLOCK_NETHER_SPROUTS:
    case BLOCK_CRIMSON_ROOTS:
    case BLOCK_SEAGRASS:
    case BLOCK_TALL_SEAGRASS:
    // fluids
    case BLOCK_WATER:
    case BLOCK_BUBBLE_COLUMN:
    case BLOCK_LAVA:
    // fire
    case BLOCK_FIRE:
    case BLOCK_SOUL_FIRE:
    // misc
    case BLOCK_SNOW:
    case BLOCK_STRUCTURE_VOID:
        return 1;
    default:
        return 0;
    }
}

static place_target
determine_place_target(server * serv, net_block_pos clicked_pos,
        mc_int clicked_face, mc_int place_type) {
    place_target res = {0};
    net_block_pos target_pos = clicked_pos;
    chunk_pos ch_pos = {
        .x = target_pos.x >> 4,
        .z = target_pos.z >> 4
    };
    chunk * ch = get_chunk_if_loaded(ch_pos);
    if (ch == NULL) {
        return res;
    }

    mc_ushort cur_state = chunk_get_block_state(ch,
            target_pos.x & 0xf, target_pos.y, target_pos.z & 0xf);
    mc_int cur_type = serv->block_type_by_state[cur_state];
    int replacing = 1;

    if (!can_replace(place_type, cur_type)) {
        target_pos = get_relative_block_pos(target_pos, clicked_face);
        ch_pos = (chunk_pos) {
            .x = target_pos.x >> 4,
            .z = target_pos.z >> 4
        };
        ch = get_chunk_if_loaded(ch_pos);
        if (ch == NULL) {
            return res;
        }

        cur_state = chunk_get_block_state(ch,
                target_pos.x & 0xf, target_pos.y, target_pos.z & 0xf);
        cur_type = serv->block_type_by_state[cur_state];
        replacing = 0;

        if (!can_replace(place_type, cur_type)) {
            return res;
        }
    }

    res = (place_target) {
        .ch = ch,
        .ch_pos = ch_pos,
        .pos = target_pos,
        .cur_state = cur_state,
        .cur_type = cur_type,
        .replacing = replacing,
    };
    return res;
}

static void
place_simple_block(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    mc_ushort place_state = get_default_block_state(context.serv, place_type);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_snowy_grassy_block(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    block_state_info place_info = describe_default_block_state(context.serv, place_type);
    mc_ushort state_above = chunk_get_block_state(target.ch,
            target.pos.x & 0xf, target.pos.y + 1, target.pos.z & 0xf);
    mc_int type_above = context.serv->block_type_by_state[state_above];

    if (type_above == BLOCK_SNOW_BLOCK || type_above == BLOCK_SNOW) {
        place_info.snowy = 1;
    } else {
        place_info.snowy = 0;
    }

    mc_ushort place_state = make_block_state(context.serv, &place_info);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_plant(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    mc_ushort state_below = chunk_get_block_state(target.ch,
            target.pos.x & 0xf, target.pos.y - 1, target.pos.z & 0xf);
    mc_int type_below = context.serv->block_type_by_state[state_below];

    if (!can_plant_survive_on(type_below)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(context.serv, place_type);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_dead_bush(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    mc_ushort state_below = chunk_get_block_state(target.ch,
            target.pos.x & 0xf, target.pos.y - 1, target.pos.z & 0xf);
    mc_int type_below = context.serv->block_type_by_state[state_below];

    if (!can_dead_bush_survive_on(type_below)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(context.serv, place_type);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_wither_rose(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    mc_ushort state_below = chunk_get_block_state(target.ch,
            target.pos.x & 0xf, target.pos.y - 1, target.pos.z & 0xf);
    mc_int type_below = context.serv->block_type_by_state[state_below];

    if (!can_wither_rose_survive_on(type_below)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(context.serv, place_type);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_nether_plant(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    mc_ushort state_below = chunk_get_block_state(target.ch,
            target.pos.x & 0xf, target.pos.y - 1, target.pos.z & 0xf);
    mc_int type_below = context.serv->block_type_by_state[state_below];

    if (!can_nether_plant_survive_on(type_below)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(context.serv, place_type);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
set_axis_by_clicked_face(block_state_info * place_info, int clicked_face) {
    switch (clicked_face) {
    case DIRECTION_NEG_X:
    case DIRECTION_POS_X:
        place_info->axis = AXIS_X;
        break;
    case DIRECTION_NEG_Y:
    case DIRECTION_POS_Y:
        place_info->axis = AXIS_Y;
        break;
    case DIRECTION_NEG_Z:
    case DIRECTION_POS_Z:
        place_info->axis = AXIS_Z;
        break;
    }
}

static void
place_simple_pillar(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    block_state_info place_info = describe_default_block_state(context.serv, place_type);
    set_axis_by_clicked_face(&place_info, context.clicked_face);

    mc_ushort place_state = make_block_state(context.serv, &place_info);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static int
get_water_level(server * serv, mc_ushort state) {
    block_state_info info = describe_block_state(serv, state);

    switch (info.block_type) {
    case BLOCK_WATER:
        return info.level;
    case BLOCK_BUBBLE_COLUMN:
    case BLOCK_KELP:
    case BLOCK_KELP_PLANT:
    case BLOCK_SEAGRASS:
    case BLOCK_TALL_SEAGRASS:
        return FLUID_LEVEL_SOURCE;
    default:
        if (info.waterlogged) {
            return FLUID_LEVEL_SOURCE;
        }
        return FLUID_LEVEL_NONE;
    }
}

static int
is_water_source(server * serv, mc_ushort state) {
    return get_water_level(serv, state) == FLUID_LEVEL_SOURCE;
}

static int
is_full_water(server * serv, mc_ushort state) {
    int level = get_water_level(serv, state);
    // @TODO(traks) maybe ensure falling level is 8, although Minecraft doesn't
    // differentiate between falling water levels
    return level == FLUID_LEVEL_SOURCE || level == FLUID_LEVEL_FALLING;
}

static void
place_chain(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    block_state_info place_info = describe_default_block_state(context.serv, place_type);
    set_axis_by_clicked_face(&place_info, context.clicked_face);
    place_info.waterlogged = is_water_source(context.serv, target.cur_state);

    mc_ushort place_state = make_block_state(context.serv, &place_info);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_slab(place_context context, mc_int place_type) {
    net_block_pos target_pos = context.clicked_pos;
    chunk_pos ch_pos = {
        .x = target_pos.x >> 4,
        .z = target_pos.z >> 4
    };
    chunk * ch = get_chunk_if_loaded(ch_pos);
    if (ch == NULL) {
        return;
    }

    mc_ushort cur_state = chunk_get_block_state(ch,
            target_pos.x & 0xf, target_pos.y, target_pos.z & 0xf);
    block_state_info cur_info = describe_block_state(context.serv, cur_state);
    mc_int cur_type = cur_info.block_type;

    int replace_cur = 0;
    if (cur_type == place_type) {
        if (cur_info.slab_type == SLAB_TOP) {
            replace_cur = context.clicked_face == DIRECTION_NEG_Y
                    || (context.clicked_face != DIRECTION_POS_Y && context.click_offset_y <= 0.5f);
        } else if  (cur_info.slab_type == SLAB_BOTTOM) {
            replace_cur = context.clicked_face == DIRECTION_POS_Y
                    || (context.clicked_face != DIRECTION_NEG_Y && context.click_offset_y > 0.5f);
        }
    } else {
        replace_cur = can_replace(place_type, cur_type);
    }

    if (!replace_cur) {
        target_pos = get_relative_block_pos(target_pos, context.clicked_face);
        ch_pos = (chunk_pos) {
            .x = target_pos.x >> 4,
            .z = target_pos.z >> 4
        };
        ch = get_chunk_if_loaded(ch_pos);
        if (ch == NULL) {
            return;
        }

        cur_state = chunk_get_block_state(ch,
                target_pos.x & 0xf, target_pos.y, target_pos.z & 0xf);
        cur_info = describe_block_state(context.serv, cur_state);
        cur_type = cur_info.block_type;

        if (cur_type != place_type && !can_replace(place_type, cur_type)) {
            return;
        }
    }

    block_state_info place_info = describe_default_block_state(context.serv, place_type);

    if (place_type == cur_type) {
        place_info.slab_type = SLAB_DOUBLE;
        place_info.waterlogged = 0;
    } else {
        if (context.clicked_face == DIRECTION_POS_Y) {
            place_info.slab_type = SLAB_BOTTOM;
        } else if (context.clicked_face == DIRECTION_NEG_Y) {
            place_info.slab_type = SLAB_TOP;
        } else if (context.click_offset_y <= 0.5f) {
            place_info.slab_type = SLAB_BOTTOM;
        } else {
            place_info.slab_type = SLAB_TOP;
        }

        place_info.waterlogged = is_water_source(context.serv, cur_state);
    }

    mc_ushort place_state = make_block_state(context.serv, &place_info);
    chunk_set_block_state(ch, target_pos.x & 0xf, target_pos.y,
            target_pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target_pos, context.serv, context.scratch_arena);
}

static void
place_leaves(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    // @TODO(traks) calculate distance to nearest log block and modify block
    // state with that information
    mc_ushort place_state = context.serv->block_properties_table[place_type].base_state;
    place_state += 0; // persistent = true

    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
set_horizontal_facing_by_player_facing(block_state_info * place_info,
        entity_base * player) {
    // facing direction is opposite of player facing direction
    float rot_y = player->rot_y;
    int int_rot = (int) floor(rot_y / 90.0f + 0.5f) & 0x3;
    switch (int_rot) {
    case 0: // +Z
        place_info->horizontal_facing = DIRECTION_NEG_Z;
        break;
    case 1: // -X
        place_info->horizontal_facing = DIRECTION_POS_X;
        break;
    case 2: // -Z
        place_info->horizontal_facing = DIRECTION_POS_Z;
        break;
    case 3: // +X
        place_info->horizontal_facing = DIRECTION_NEG_X;
        break;
    }
}

static void
place_horizontal_facing(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    block_state_info place_info = describe_default_block_state(context.serv, place_type);
    set_horizontal_facing_by_player_facing(&place_info, context.player);

    mc_ushort place_state = make_block_state(context.serv, &place_info);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_end_portal_frame(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    block_state_info place_info = describe_default_block_state(context.serv, place_type);
    set_horizontal_facing_by_player_facing(&place_info, context.player);
    place_info.eye = 0;

    mc_ushort place_state = make_block_state(context.serv, &place_info);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_crop(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    mc_ushort state_below = chunk_get_block_state(target.ch,
            target.pos.x & 0xf, target.pos.y - 1, target.pos.z & 0xf);
    mc_int type_below = context.serv->block_type_by_state[state_below];

    // @TODO(traks) light level also needs to be sufficient
    switch (type_below) {
    case BLOCK_FARMLAND:
        break;
    default:
        return;
    }

    mc_ushort place_state = get_default_block_state(context.serv, place_type);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_nether_wart(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    mc_ushort state_below = chunk_get_block_state(target.ch,
            target.pos.x & 0xf, target.pos.y - 1, target.pos.z & 0xf);
    mc_int type_below = context.serv->block_type_by_state[state_below];

    switch (type_below) {
    case BLOCK_SOUL_SAND:
        break;
    default:
        return;
    }

    mc_ushort place_state = get_default_block_state(context.serv, place_type);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_carpet(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    mc_ushort place_state = get_default_block_state(context.serv, place_type);
    mc_ushort state_below = chunk_get_block_state(target.ch,
            target.pos.x & 0xf, target.pos.y - 1, target.pos.z & 0xf);
    mc_int type_below = context.serv->block_type_by_state[state_below];

    if (!can_carpet_survive_on(type_below)) {
        return;
    }

    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_mushroom_block(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    block_state_info place_info = describe_default_block_state(context.serv, place_type);

    int directions[] = {0, 1, 2, 3, 4, 5};

    for (int i = 0; i < 6; i++) {
        net_block_pos pos = get_relative_block_pos(target.pos, directions[i]);
        chunk_pos ch_pos = {
            .x = pos.x >> 4,
            .z = pos.z >> 4
        };
        chunk * ch = get_chunk_if_loaded(ch_pos);
        if (ch == NULL) {
            return;
        }

        mc_ushort state = chunk_get_block_state(ch,
                pos.x & 0xf, pos.y, pos.z & 0xf);
        mc_int type = context.serv->block_type_by_state[state];

        // connect to neighbouring mushroom blocks of the same type by setting
        // the six facing properties to true if connected
        if (type == place_type) {
            place_info.values[BLOCK_PROPERTY_NEG_Y + directions[i]] = 1;
        } else {
            place_info.values[BLOCK_PROPERTY_NEG_Y + directions[i]] = 0;
        }
    }

    mc_ushort place_state = make_block_state(context.serv, &place_info);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_end_rod(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    block_state_info place_info = describe_default_block_state(context.serv, place_type);

    int opposite_face = get_opposite_direction(context.clicked_face);

    net_block_pos opposite_pos = get_relative_block_pos(target.pos, opposite_face);
    chunk_pos opposite_ch_pos = {
        .x = opposite_pos.x >> 4,
        .z = opposite_pos.z >> 4
    };
    chunk * opposite_ch = get_chunk_if_loaded(opposite_ch_pos);
    if (opposite_ch == NULL) {
        return;
    }

    mc_ushort opposite_state = chunk_get_block_state(opposite_ch,
            opposite_pos.x & 0xf, opposite_pos.y, opposite_pos.z & 0xf);
    block_state_info opposite_info = describe_block_state(context.serv, opposite_state);

    if (opposite_info.block_type == place_type) {
        if (opposite_info.facing == context.clicked_face) {
            place_info.facing = opposite_face;
        } else {
            place_info.facing = context.clicked_face;
        }
    } else {
        place_info.facing = context.clicked_face;
    }

    mc_ushort place_state = make_block_state(context.serv, &place_info);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_sugar_cane(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    mc_ushort state_below = chunk_get_block_state(target.ch,
            target.pos.x & 0xf, target.pos.y - 1, target.pos.z & 0xf);
    mc_int type_below = context.serv->block_type_by_state[state_below];

    switch (type_below) {
    case BLOCK_SUGAR_CANE:
        break;
    case BLOCK_GRASS_BLOCK:
    case BLOCK_DIRT:
    case BLOCK_COARSE_DIRT:
    case BLOCK_PODZOL:
    case BLOCK_SAND:
    case BLOCK_RED_SAND: {
        net_block_pos neighbour_pos[4];
        for (int i = 0; i < 4; i++) {
            net_block_pos pos = target.pos;
            pos.y--;
            neighbour_pos[i] = pos;
        }
        neighbour_pos[0].x--;
        neighbour_pos[1].x++;
        neighbour_pos[2].z--;
        neighbour_pos[3].z++;

        for (int i = 0; i < 6; i++) {
            net_block_pos pos = neighbour_pos[i];
            chunk_pos ch_pos = {
                .x = pos.x >> 4,
                .z = pos.z >> 4
            };
            chunk * ch = get_chunk_if_loaded(ch_pos);
            if (ch == NULL) {
                return;
            }

            mc_ushort neighbour_state = chunk_get_block_state(ch,
                    pos.x & 0xf, pos.y, pos.z & 0xf);
            mc_int neighbour_type = context.serv->block_type_by_state[neighbour_state];
            switch (neighbour_type) {
            case BLOCK_WATER:
            case BLOCK_FROSTED_ICE:
                goto set_block;
            default: {
                block_state_info neighbour_info = describe_block_state(context.serv, neighbour_state);
                if (neighbour_info.waterlogged) {
                    goto set_block;
                }
            }
            }
        }

        // no water next to ground block
        return;
    }
    default:
        return;
    }

set_block:;
    mc_ushort place_state = get_default_block_state(context.serv, place_type);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_dead_coral(place_context context, mc_int place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, place_type);
    if (target.ch == NULL) {
        return;
    }

    mc_ushort state_below = chunk_get_block_state(target.ch,
            target.pos.x & 0xf, target.pos.y - 1, target.pos.z & 0xf);
    // @TODO(traks) don't use collision model for this
    block_model * model_below = context.serv->block_models + context.serv->collision_model_by_state[state_below];
    if (!(model_below->full_face_flags & (1 << DIRECTION_POS_Y))) {
        // face below is not sturdy
        return;
    }

    block_state_info place_info = describe_default_block_state(context.serv, place_type);
    place_info.waterlogged = is_full_water(context.serv, target.cur_state);

    mc_ushort place_state = make_block_state(context.serv, &place_info);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

typedef struct {
    unsigned char directions[6];
} direction_list;

static direction_list
get_directions_by_player_rot(entity_base * player) {
    direction_list res = {0};

    float sin_rot_y = sinf(player->rot_y * RADIANS_PER_DEGREE);
    float cos_rot_y = cosf(player->rot_y * RADIANS_PER_DEGREE);
    float sin_rot_x = sinf(player->rot_x * RADIANS_PER_DEGREE);
    float cos_rot_x = cosf(player->rot_x * RADIANS_PER_DEGREE);

    float look_dist_x = ABS(sin_rot_y * cos_rot_x);
    float look_dist_z = ABS(cos_rot_y * cos_rot_x);
    float look_dist_y = ABS(sin_rot_x);

    int ix;
    int iy;
    int iz;

    if (look_dist_x > look_dist_y) {
        if (look_dist_x > look_dist_z) {
            ix = 0;

            if (look_dist_y > look_dist_z) {
                iy = 1;
                iz = 2;
            } else {
                iy = 2;
                iz = 1;
            }
        } else {
            iz = 0;
            ix = 1;
            iy = 2;
        }
    } else {
        if (look_dist_x < look_dist_z) {
            ix = 2;

            if (look_dist_y > look_dist_z) {
                iy = 0;
                iz = 1;
            } else {
                iy = 1;
                iz = 0;
            }
        } else {
            iy = 0;
            ix = 1;
            iz = 2;
        }
    }

    if (sin_rot_y > 0) {
        res.directions[0 + ix] = DIRECTION_NEG_X;
        res.directions[5 - ix] = DIRECTION_POS_X;
    } else {
        res.directions[0 + ix] = DIRECTION_POS_X;
        res.directions[5 - ix] = DIRECTION_NEG_X;
    }

    if (cos_rot_y < 0) {
        res.directions[0 + iz] = DIRECTION_NEG_Z;
        res.directions[5 - iz] = DIRECTION_POS_Z;
    } else {
        res.directions[0 + iz] = DIRECTION_POS_Z;
        res.directions[5 - iz] = DIRECTION_NEG_Z;
    }

    if (sin_rot_x > 0) {
        res.directions[0 + iy] = DIRECTION_NEG_Y;
        res.directions[5 - iy] = DIRECTION_POS_Y;
    } else {
        res.directions[0 + iy] = DIRECTION_POS_Y;
        res.directions[5 - iy] = DIRECTION_NEG_Y;
    }

    return res;
}

static direction_list
get_attach_directions_by_preference(place_context context, place_target target) {
    if (target.replacing) {
        return get_directions_by_player_rot(context.player);
    } else {
        direction_list res = get_directions_by_player_rot(context.player);

        // now modify the direction list to prioritise the face they clicked
        int best_dir = get_opposite_direction(context.clicked_face);

        int last_dir = res.directions[0];
        int i = 1;
        while (last_dir != best_dir) {
            int cur_dir = res.directions[i];
            res.directions[i] = last_dir;
            last_dir = cur_dir;
            i++;
        }
        res.directions[0] = best_dir;

        return res;
    }
}

static void
place_dead_coral_fan(place_context context, mc_int base_place_type,
        mc_int wall_place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, base_place_type);
    if (target.ch == NULL) {
        return;
    }

    // @NOTE(traks) Minecraft prefers horizontal directions over block below, if
    // a horizontal direction appears in this list before NEG_Y.
    direction_list list = get_attach_directions_by_preference(context, target);
    int try_horizontal_dirs = 0;
    int seen_neg_y = 0;
    int selected_dir = -1;

    for (int i = 0; i < 6; i++) {
        int dir = list.directions[i];
        if (dir == DIRECTION_POS_Y) {
            continue;
        }

        net_block_pos attach_pos = get_relative_block_pos(target.pos, dir);
        chunk_pos ch_pos = {
            .x = attach_pos.x >> 4,
            .z = attach_pos.z >> 4,
        };
        chunk * attach_ch = get_chunk_if_loaded(ch_pos);
        if (attach_ch == NULL) {
            return;
        }

        mc_ushort wall_state = chunk_get_block_state(attach_ch,
                attach_pos.x & 0xf, attach_pos.y, attach_pos.z & 0xf);
        // @TODO(traks) don't use collision model for this
        block_model * wall_model = context.serv->block_models + context.serv->collision_model_by_state[wall_state];
        int wall_face = get_opposite_direction(dir);

        if (dir == DIRECTION_NEG_Y) {
            seen_neg_y = 1;
            if (wall_model->full_face_flags & (1 << wall_face)) {
                // wall face is sturdy
                selected_dir = dir;
                if (!try_horizontal_dirs) {
                    break;
                }
            }
        } else {
            if (!seen_neg_y) {
                try_horizontal_dirs = 1;
            }
            if (wall_model->full_face_flags & (1 << wall_face)) {
                // wall face is sturdy
                selected_dir = dir;
                break;
            }
        }
    }
    if (selected_dir == -1) {
        return;
    }

    mc_int place_type = selected_dir == DIRECTION_NEG_Y ?
            base_place_type : wall_place_type;
    block_state_info place_info = describe_default_block_state(context.serv, place_type);
    place_info.waterlogged = is_full_water(context.serv, target.cur_state);
    place_info.horizontal_facing = get_opposite_direction(selected_dir);

    mc_ushort place_state = make_block_state(context.serv, &place_info);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

static void
place_torch(place_context context, mc_int base_place_type,
        mc_int wall_place_type) {
    place_target target = determine_place_target(context.serv,
            context.clicked_pos, context.clicked_face, base_place_type);
    if (target.ch == NULL) {
        return;
    }

    // @NOTE(traks) Minecraft prefers horizontal directions over block below, if
    // a horizontal direction appears in this list before NEG_Y.
    direction_list list = get_attach_directions_by_preference(context, target);
    int try_horizontal_dirs = 0;
    int seen_neg_y = 0;
    int selected_dir = -1;

    for (int i = 0; i < 6; i++) {
        int dir = list.directions[i];
        if (dir == DIRECTION_POS_Y) {
            continue;
        }

        net_block_pos attach_pos = get_relative_block_pos(target.pos, dir);
        chunk_pos ch_pos = {
            .x = attach_pos.x >> 4,
            .z = attach_pos.z >> 4,
        };
        chunk * attach_ch = get_chunk_if_loaded(ch_pos);
        if (attach_ch == NULL) {
            return;
        }

        mc_ushort wall_state = chunk_get_block_state(attach_ch,
                attach_pos.x & 0xf, attach_pos.y, attach_pos.z & 0xf);
        // @TODO(traks) don't use collision model for this
        block_model * wall_model = context.serv->block_models + context.serv->collision_model_by_state[wall_state];
        int wall_face = get_opposite_direction(dir);

        if (dir == DIRECTION_NEG_Y) {
            seen_neg_y = 1;
            if (wall_model->pole_face_flags & (1 << wall_face)) {
                // block has full centre for supporting blocks
                selected_dir = dir;
                if (!try_horizontal_dirs) {
                    break;
                }
            }
        } else {
            if (!seen_neg_y) {
                try_horizontal_dirs = 1;
            }
            if (wall_model->full_face_flags & (1 << wall_face)) {
                // wall face is sturdy
                selected_dir = dir;
                break;
            }
        }
    }
    if (selected_dir == -1) {
        return;
    }

    mc_int place_type = selected_dir == DIRECTION_NEG_Y ?
            base_place_type : wall_place_type;
    block_state_info place_info = describe_default_block_state(context.serv, place_type);
    place_info.horizontal_facing = get_opposite_direction(selected_dir);

    mc_ushort place_state = make_block_state(context.serv, &place_info);
    chunk_set_block_state(target.ch, target.pos.x & 0xf, target.pos.y,
            target.pos.z & 0xf, place_state);
    propagate_block_updates_after_change(target.pos, context.serv, context.scratch_arena);
}

void
process_use_item_on_packet(server * serv, entity_base * player,
        mc_int hand, net_block_pos clicked_pos, mc_int clicked_face,
        float click_offset_x, float click_offset_y, float click_offset_z,
        mc_ubyte is_inside, memory_arena * scratch_arena) {
    if (player->flags & ENTITY_TELEPORTING) {
        // ignore
        return;
    }

    int sel_slot = player->player.selected_slot;
    item_stack * main = player->player.slots + sel_slot;
    item_stack * off = player->player.slots + PLAYER_OFF_HAND_SLOT;
    item_stack * used = hand == PLAYER_MAIN_HAND ? main : off;

    // @TODO(traks) special handling depending on gamemode. Currently we assume
    // gamemode creative

    // @TODO(traks) ensure clicked block is in one of the sent
    // chunks inside the player's chunk cache

    // @TODO(traks) check for cooldowns (ender pearls,
    // chorus fruits)

    // if the player is not crouching with an item in their hands, try to use
    // the clicked block
    if (!((player->flags & PLAYER_SHIFTING)
            && (main->type != ITEM_AIR || off->type != ITEM_AIR))) {
        int used_block = use_block(serv, player,
                hand, clicked_pos, clicked_face,
                click_offset_x, click_offset_y, click_offset_z,
                is_inside, scratch_arena);
        if (used_block) {
            return;
        }
    }

    // try to use the held item

    // @TODO(traks) swing arm if necessary

    // @TODO(traks) send "built too high" action bar message

    // @TODO(traks) if placement cancelled unexpectedly, send block update
    // packet back to the player to fix the possible desync

    // @TODO(traks) adventure mode place allow/deny

    // @TODO(traks) implement all items

    // @TODO(traks) check whether to-be-placed block state collides with any
    // entities in the world that block building

    // @TODO(traks) some block states are not able to 'survive' in all
    // conditions. Check for these.

    // @TODO(traks) use a block item's BlockStateTag to modify the block state
    // after the original state has been placed

    // @TODO(traks) use a block item's BlockEntityTag to modify the block
    // entity's data if the item has a block entity, after the original state
    // and block entity have been placed

    // @TODO(traks) take instabuild player ability into account

    // @TODO(traks) play block place sound

    // @TODO(traks) finalise some things such as block entity data based on
    // custom item stack name, create bed head part, plant upper part, etc.

    place_context context = {
        .serv = serv,
        .player = player,
        .clicked_pos = clicked_pos,
        .clicked_face = clicked_face,
        .click_offset_x = click_offset_x,
        .click_offset_y = click_offset_y,
        .click_offset_z = click_offset_z,
        .scratch_arena = scratch_arena,
    };

    switch (used->type) {
    case ITEM_AIR:
        // nothing to do
        break;
    case ITEM_STONE:
        place_simple_block(context, BLOCK_STONE);
        break;
    case ITEM_GRANITE:
        place_simple_block(context, BLOCK_GRANITE);
        break;
    case ITEM_POLISHED_GRANITE:
        place_simple_block(context, BLOCK_POLISHED_GRANITE);
        break;
    case ITEM_DIORITE:
        place_simple_block(context, BLOCK_DIORITE);
        break;
    case ITEM_POLISHED_DIORITE:
        place_simple_block(context, BLOCK_POLISHED_DIORITE);
        break;
    case ITEM_ANDESITE:
        place_simple_block(context, BLOCK_ANDESITE);
        break;
    case ITEM_POLISHED_ANDESITE:
        place_simple_block(context, BLOCK_POLISHED_ANDESITE);
        break;
    case ITEM_GRASS_BLOCK:
        place_snowy_grassy_block(context, BLOCK_GRASS_BLOCK);
        break;
    case ITEM_DIRT:
        place_simple_block(context, BLOCK_DIRT);
        break;
    case ITEM_COARSE_DIRT:
        place_simple_block(context, BLOCK_COARSE_DIRT);
        break;
    case ITEM_PODZOL:
        place_snowy_grassy_block(context, BLOCK_PODZOL);
        break;
    case ITEM_CRIMSON_NYLIUM:
        place_simple_block(context, BLOCK_CRIMSON_NYLIUM);
        break;
    case ITEM_WARPED_NYLIUM:
        place_simple_block(context, BLOCK_WARPED_NYLIUM);
        break;
    case ITEM_COBBLESTONE:
        place_simple_block(context, BLOCK_COBBLESTONE);
        break;
    case ITEM_OAK_PLANKS:
        place_simple_block(context, BLOCK_OAK_PLANKS);
        break;
    case ITEM_SPRUCE_PLANKS:
        place_simple_block(context, BLOCK_SPRUCE_PLANKS);
        break;
    case ITEM_BIRCH_PLANKS:
        place_simple_block(context, BLOCK_BIRCH_PLANKS);
        break;
    case ITEM_JUNGLE_PLANKS:
        place_simple_block(context, BLOCK_JUNGLE_PLANKS);
        break;
    case ITEM_ACACIA_PLANKS:
        place_simple_block(context, BLOCK_ACACIA_PLANKS);
        break;
    case ITEM_DARK_OAK_PLANKS:
        place_simple_block(context, BLOCK_DARK_OAK_PLANKS);
        break;
    case ITEM_CRIMSON_PLANKS:
        place_simple_block(context, BLOCK_CRIMSON_PLANKS);
        break;
    case ITEM_WARPED_PLANKS:
        place_simple_block(context, BLOCK_WARPED_PLANKS);
        break;
    case ITEM_OAK_SAPLING:
        place_plant(context, BLOCK_OAK_SAPLING);
        break;
    case ITEM_SPRUCE_SAPLING:
        place_plant(context, BLOCK_SPRUCE_SAPLING);
        break;
    case ITEM_BIRCH_SAPLING:
        place_plant(context, BLOCK_BIRCH_SAPLING);
        break;
    case ITEM_JUNGLE_SAPLING:
        place_plant(context, BLOCK_JUNGLE_SAPLING);
        break;
    case ITEM_ACACIA_SAPLING:
        place_plant(context, BLOCK_ACACIA_SAPLING);
        break;
    case ITEM_DARK_OAK_SAPLING:
        place_plant(context, BLOCK_DARK_OAK_SAPLING);
        break;
    case ITEM_BEDROCK:
        place_simple_block(context, BLOCK_BEDROCK);
        break;
    case ITEM_SAND:
        break;
    case ITEM_RED_SAND:
        break;
    case ITEM_GRAVEL:
        break;
    case ITEM_GOLD_ORE:
        place_simple_block(context, BLOCK_GOLD_ORE);
        break;
    case ITEM_IRON_ORE:
        place_simple_block(context, BLOCK_IRON_ORE);
        break;
    case ITEM_COAL_ORE:
        place_simple_block(context, BLOCK_COAL_ORE);
        break;
    case ITEM_NETHER_GOLD_ORE:
        place_simple_block(context, BLOCK_NETHER_GOLD_ORE);
        break;
    case ITEM_OAK_LOG:
        place_simple_pillar(context, BLOCK_OAK_LOG);
        break;
    case ITEM_SPRUCE_LOG:
        place_simple_pillar(context, BLOCK_SPRUCE_LOG);
        break;
    case ITEM_BIRCH_LOG:
        place_simple_pillar(context, BLOCK_BIRCH_LOG);
        break;
    case ITEM_JUNGLE_LOG:
        place_simple_pillar(context, BLOCK_JUNGLE_LOG);
        break;
    case ITEM_ACACIA_LOG:
        place_simple_pillar(context, BLOCK_ACACIA_LOG);
        break;
    case ITEM_DARK_OAK_LOG:
        place_simple_pillar(context, BLOCK_DARK_OAK_LOG);
        break;
    case ITEM_CRIMSON_STEM:
        place_simple_pillar(context, BLOCK_CRIMSON_STEM);
        break;
    case ITEM_WARPED_STEM:
        place_simple_pillar(context, BLOCK_WARPED_STEM);
        break;
    case ITEM_STRIPPED_OAK_LOG:
        place_simple_pillar(context, BLOCK_STRIPPED_OAK_LOG);
        break;
    case ITEM_STRIPPED_SPRUCE_LOG:
        place_simple_pillar(context, BLOCK_STRIPPED_SPRUCE_LOG);
        break;
    case ITEM_STRIPPED_BIRCH_LOG:
        place_simple_pillar(context, BLOCK_STRIPPED_BIRCH_LOG);
        break;
    case ITEM_STRIPPED_JUNGLE_LOG:
        place_simple_pillar(context, BLOCK_STRIPPED_JUNGLE_LOG);
        break;
    case ITEM_STRIPPED_ACACIA_LOG:
        place_simple_pillar(context, BLOCK_STRIPPED_ACACIA_LOG);
        break;
    case ITEM_STRIPPED_DARK_OAK_LOG:
        place_simple_pillar(context, BLOCK_STRIPPED_DARK_OAK_LOG);
        break;
    case ITEM_STRIPPED_CRIMSON_STEM:
        place_simple_pillar(context, BLOCK_STRIPPED_CRIMSON_STEM);
        break;
    case ITEM_STRIPPED_WARPED_STEM:
        place_simple_pillar(context, BLOCK_STRIPPED_WARPED_STEM);
        break;
    case ITEM_STRIPPED_OAK_WOOD:
        place_simple_pillar(context, BLOCK_STRIPPED_OAK_WOOD);
        break;
    case ITEM_STRIPPED_SPRUCE_WOOD:
        place_simple_pillar(context, BLOCK_STRIPPED_SPRUCE_WOOD);
        break;
    case ITEM_STRIPPED_BIRCH_WOOD:
        place_simple_pillar(context, BLOCK_STRIPPED_BIRCH_WOOD);
        break;
    case ITEM_STRIPPED_JUNGLE_WOOD:
        place_simple_pillar(context, BLOCK_STRIPPED_JUNGLE_WOOD);
        break;
    case ITEM_STRIPPED_ACACIA_WOOD:
        place_simple_pillar(context, BLOCK_STRIPPED_ACACIA_WOOD);
        break;
    case ITEM_STRIPPED_DARK_OAK_WOOD:
        place_simple_pillar(context, BLOCK_STRIPPED_DARK_OAK_WOOD);
        break;
    case ITEM_STRIPPED_CRIMSON_HYPHAE:
        place_simple_pillar(context, BLOCK_STRIPPED_CRIMSON_HYPHAE);
        break;
    case ITEM_STRIPPED_WARPED_HYPHAE:
        place_simple_pillar(context, BLOCK_STRIPPED_WARPED_HYPHAE);
        break;
    case ITEM_OAK_WOOD:
        place_simple_pillar(context, BLOCK_OAK_WOOD);
        break;
    case ITEM_SPRUCE_WOOD:
        place_simple_pillar(context, BLOCK_SPRUCE_WOOD);
        break;
    case ITEM_BIRCH_WOOD:
        place_simple_pillar(context, BLOCK_BIRCH_WOOD);
        break;
    case ITEM_JUNGLE_WOOD:
        place_simple_pillar(context, BLOCK_JUNGLE_WOOD);
        break;
    case ITEM_ACACIA_WOOD:
        place_simple_pillar(context, BLOCK_ACACIA_WOOD);
        break;
    case ITEM_DARK_OAK_WOOD:
        place_simple_pillar(context, BLOCK_DARK_OAK_WOOD);
        break;
    case ITEM_CRIMSON_HYPHAE:
        place_simple_pillar(context, BLOCK_CRIMSON_HYPHAE);
        break;
    case ITEM_WARPED_HYPHAE:
        place_simple_pillar(context, BLOCK_WARPED_HYPHAE);
        break;
    case ITEM_OAK_LEAVES:
        place_leaves(context, BLOCK_OAK_LEAVES);
        break;
    case ITEM_SPRUCE_LEAVES:
        place_leaves(context, BLOCK_SPRUCE_LEAVES);
        break;
    case ITEM_BIRCH_LEAVES:
        place_leaves(context, BLOCK_BIRCH_LEAVES);
        break;
    case ITEM_JUNGLE_LEAVES:
        place_leaves(context, BLOCK_JUNGLE_LEAVES);
        break;
    case ITEM_ACACIA_LEAVES:
        place_leaves(context, BLOCK_ACACIA_LEAVES);
        break;
    case ITEM_DARK_OAK_LEAVES:
        place_leaves(context, BLOCK_DARK_OAK_LEAVES);
        break;
    case ITEM_SPONGE:
        break;
    case ITEM_WET_SPONGE:
        break;
    case ITEM_GLASS:
        place_simple_block(context, BLOCK_GLASS);
        break;
    case ITEM_LAPIS_ORE:
        place_simple_block(context, BLOCK_LAPIS_ORE);
        break;
    case ITEM_LAPIS_BLOCK:
        place_simple_block(context, BLOCK_LAPIS_BLOCK);
        break;
    case ITEM_DISPENSER:
        break;
    case ITEM_SANDSTONE:
        place_simple_block(context, BLOCK_SANDSTONE);
        break;
    case ITEM_CHISELED_SANDSTONE:
        place_simple_block(context, BLOCK_CHISELED_SANDSTONE);
        break;
    case ITEM_CUT_SANDSTONE:
        place_simple_block(context, BLOCK_CUT_SANDSTONE);
        break;
    case ITEM_NOTE_BLOCK:
        break;
    case ITEM_POWERED_RAIL:
        break;
    case ITEM_DETECTOR_RAIL:
        break;
    case ITEM_STICKY_PISTON:
        break;
    case ITEM_COBWEB:
        place_simple_block(context, BLOCK_COBWEB);
        break;
    case ITEM_GRASS:
        place_plant(context, BLOCK_GRASS);
        break;
    case ITEM_FERN:
        place_plant(context, BLOCK_FERN);
        break;
    case ITEM_DEAD_BUSH:
        place_dead_bush(context, BLOCK_DEAD_BUSH);
        break;
    case ITEM_SEAGRASS:
        break;
    case ITEM_SEA_PICKLE:
        break;
    case ITEM_PISTON:
        break;
    case ITEM_WHITE_WOOL:
        place_simple_block(context, BLOCK_WHITE_WOOL);
        break;
    case ITEM_ORANGE_WOOL:
        place_simple_block(context, BLOCK_ORANGE_WOOL);
        break;
    case ITEM_MAGENTA_WOOL:
        place_simple_block(context, BLOCK_MAGENTA_WOOL);
        break;
    case ITEM_LIGHT_BLUE_WOOL:
        place_simple_block(context, BLOCK_LIGHT_BLUE_WOOL);
        break;
    case ITEM_YELLOW_WOOL:
        place_simple_block(context, BLOCK_YELLOW_WOOL);
        break;
    case ITEM_LIME_WOOL:
        place_simple_block(context, BLOCK_LIME_WOOL);
        break;
    case ITEM_PINK_WOOL:
        place_simple_block(context, BLOCK_PINK_WOOL);
        break;
    case ITEM_GRAY_WOOL:
        place_simple_block(context, BLOCK_GRAY_WOOL);
        break;
    case ITEM_LIGHT_GRAY_WOOL:
        place_simple_block(context, BLOCK_LIGHT_GRAY_WOOL);
        break;
    case ITEM_CYAN_WOOL:
        place_simple_block(context, BLOCK_CYAN_WOOL);
        break;
    case ITEM_PURPLE_WOOL:
        place_simple_block(context, BLOCK_PURPLE_WOOL);
        break;
    case ITEM_BLUE_WOOL:
        place_simple_block(context, BLOCK_BLUE_WOOL);
        break;
    case ITEM_BROWN_WOOL:
        place_simple_block(context, BLOCK_BROWN_WOOL);
        break;
    case ITEM_GREEN_WOOL:
        place_simple_block(context, BLOCK_GREEN_WOOL);
        break;
    case ITEM_RED_WOOL:
        place_simple_block(context, BLOCK_RED_WOOL);
        break;
    case ITEM_BLACK_WOOL:
        place_simple_block(context, BLOCK_BLACK_WOOL);
        break;
    case ITEM_DANDELION:
        place_plant(context, BLOCK_DANDELION);
        break;
    case ITEM_POPPY:
        place_plant(context, BLOCK_POPPY);
        break;
    case ITEM_BLUE_ORCHID:
        place_plant(context, BLOCK_BLUE_ORCHID);
        break;
    case ITEM_ALLIUM:
        place_plant(context, BLOCK_ALLIUM);
        break;
    case ITEM_AZURE_BLUET:
        place_plant(context, BLOCK_AZURE_BLUET);
        break;
    case ITEM_RED_TULIP:
        place_plant(context, BLOCK_RED_TULIP);
        break;
    case ITEM_ORANGE_TULIP:
        place_plant(context, BLOCK_ORANGE_TULIP);
        break;
    case ITEM_WHITE_TULIP:
        place_plant(context, BLOCK_WHITE_TULIP);
        break;
    case ITEM_PINK_TULIP:
        place_plant(context, BLOCK_PINK_TULIP);
        break;
    case ITEM_OXEYE_DAISY:
        place_plant(context, BLOCK_OXEYE_DAISY);
        break;
    case ITEM_CORNFLOWER:
        place_plant(context, BLOCK_CORNFLOWER);
        break;
    case ITEM_LILY_OF_THE_VALLEY:
        place_plant(context, BLOCK_LILY_OF_THE_VALLEY);
        break;
    case ITEM_WITHER_ROSE:
        place_wither_rose(context, BLOCK_WITHER_ROSE);
        break;
    case ITEM_BROWN_MUSHROOM:
        break;
    case ITEM_RED_MUSHROOM:
        break;
    case ITEM_CRIMSON_FUNGUS:
        place_nether_plant(context, BLOCK_CRIMSON_FUNGUS);
        break;
    case ITEM_WARPED_FUNGUS:
        place_nether_plant(context, BLOCK_WARPED_FUNGUS);
        break;
    case ITEM_CRIMSON_ROOTS:
        place_nether_plant(context, BLOCK_CRIMSON_ROOTS);
        break;
    case ITEM_WARPED_ROOTS:
        place_nether_plant(context, BLOCK_WARPED_ROOTS);
        break;
    case ITEM_NETHER_SPROUTS:
        place_nether_plant(context, BLOCK_NETHER_SPROUTS);
        break;
    case ITEM_WEEPING_VINES:
        break;
    case ITEM_TWISTING_VINES:
        break;
    case ITEM_SUGAR_CANE:
        place_sugar_cane(context, BLOCK_SUGAR_CANE);
        break;
    case ITEM_KELP:
        break;
    case ITEM_BAMBOO:
        break;
    case ITEM_GOLD_BLOCK:
        place_simple_block(context, BLOCK_GOLD_BLOCK);
        break;
    case ITEM_IRON_BLOCK:
        place_simple_block(context, BLOCK_IRON_BLOCK);
        break;
    case ITEM_OAK_SLAB:
        place_slab(context, BLOCK_OAK_SLAB);
        break;
    case ITEM_SPRUCE_SLAB:
        place_slab(context, BLOCK_SPRUCE_SLAB);
        break;
    case ITEM_BIRCH_SLAB:
        place_slab(context, BLOCK_BIRCH_SLAB);
        break;
    case ITEM_JUNGLE_SLAB:
        place_slab(context, BLOCK_JUNGLE_SLAB);
        break;
    case ITEM_ACACIA_SLAB:
        place_slab(context, BLOCK_ACACIA_SLAB);
        break;
    case ITEM_DARK_OAK_SLAB:
        place_slab(context, BLOCK_DARK_OAK_SLAB);
        break;
    case ITEM_CRIMSON_SLAB:
        place_slab(context, BLOCK_CRIMSON_SLAB);
        break;
    case ITEM_WARPED_SLAB:
        place_slab(context, BLOCK_WARPED_SLAB);
        break;
    case ITEM_STONE_SLAB:
        place_slab(context, BLOCK_STONE_SLAB);
        break;
    case ITEM_SMOOTH_STONE_SLAB:
        place_slab(context, BLOCK_SMOOTH_STONE_SLAB);
        break;
    case ITEM_SANDSTONE_SLAB:
        place_slab(context, BLOCK_SANDSTONE_SLAB);
        break;
    case ITEM_CUT_SANDSTONE_SLAB:
        place_slab(context, BLOCK_CUT_SANDSTONE_SLAB);
        break;
    case ITEM_PETRIFIED_OAK_SLAB:
        place_slab(context, BLOCK_PETRIFIED_OAK_SLAB);
        break;
    case ITEM_COBBLESTONE_SLAB:
        place_slab(context, BLOCK_COBBLESTONE_SLAB);
        break;
    case ITEM_BRICK_SLAB:
        place_slab(context, BLOCK_BRICK_SLAB);
        break;
    case ITEM_STONE_BRICK_SLAB:
        place_slab(context, BLOCK_STONE_BRICK_SLAB);
        break;
    case ITEM_NETHER_BRICK_SLAB:
        place_slab(context, BLOCK_NETHER_BRICK_SLAB);
        break;
    case ITEM_QUARTZ_SLAB:
        place_slab(context, BLOCK_QUARTZ_SLAB);
        break;
    case ITEM_RED_SANDSTONE_SLAB:
        place_slab(context, BLOCK_RED_SANDSTONE_SLAB);
        break;
    case ITEM_CUT_RED_SANDSTONE_SLAB:
        place_slab(context, BLOCK_CUT_RED_SANDSTONE_SLAB);
        break;
    case ITEM_PURPUR_SLAB:
        place_slab(context, BLOCK_PURPUR_SLAB);
        break;
    case ITEM_PRISMARINE_SLAB:
        place_slab(context, BLOCK_PRISMARINE_SLAB);
        break;
    case ITEM_PRISMARINE_BRICK_SLAB:
        place_slab(context, BLOCK_PRISMARINE_BRICK_SLAB);
        break;
    case ITEM_DARK_PRISMARINE_SLAB:
        place_slab(context, BLOCK_DARK_PRISMARINE_SLAB);
        break;
    case ITEM_SMOOTH_QUARTZ:
        place_simple_block(context, BLOCK_SMOOTH_QUARTZ);
        break;
    case ITEM_SMOOTH_RED_SANDSTONE:
        place_simple_block(context, BLOCK_SMOOTH_RED_SANDSTONE);
        break;
    case ITEM_SMOOTH_SANDSTONE:
        place_simple_block(context, BLOCK_SMOOTH_SANDSTONE);
        break;
    case ITEM_SMOOTH_STONE:
        place_simple_block(context, BLOCK_SMOOTH_STONE);
        break;
    case ITEM_BRICKS:
        place_simple_block(context, BLOCK_BRICKS);
        break;
    case ITEM_TNT:
        place_simple_block(context, BLOCK_TNT);
        break;
    case ITEM_BOOKSHELF:
        place_simple_block(context, BLOCK_BOOKSHELF);
        break;
    case ITEM_MOSSY_COBBLESTONE:
        place_simple_block(context, BLOCK_MOSSY_COBBLESTONE);
        break;
    case ITEM_OBSIDIAN:
        place_simple_block(context, BLOCK_OBSIDIAN);
        break;
    case ITEM_TORCH:
        place_torch(context, BLOCK_TORCH, BLOCK_WALL_TORCH);
        break;
    case ITEM_END_ROD:
        place_end_rod(context, BLOCK_END_ROD);
        break;
    case ITEM_CHORUS_PLANT:
        break;
    case ITEM_CHORUS_FLOWER:
        break;
    case ITEM_PURPUR_BLOCK:
        place_simple_block(context, BLOCK_PURPUR_BLOCK);
        break;
    case ITEM_PURPUR_PILLAR:
        place_simple_pillar(context, BLOCK_PURPUR_PILLAR);
        break;
    case ITEM_PURPUR_STAIRS:
        break;
    case ITEM_SPAWNER:
        break;
    case ITEM_OAK_STAIRS:
        break;
    case ITEM_CHEST:
        break;
    case ITEM_DIAMOND_ORE:
        place_simple_block(context, BLOCK_DIAMOND_ORE);
        break;
    case ITEM_DIAMOND_BLOCK:
        place_simple_block(context, BLOCK_DIAMOND_BLOCK);
        break;
    case ITEM_CRAFTING_TABLE:
        place_simple_block(context, BLOCK_CRAFTING_TABLE);
        break;
    case ITEM_FARMLAND:
        break;
    case ITEM_FURNACE:
        break;
    case ITEM_LADDER:
        break;
    case ITEM_RAIL:
        break;
    case ITEM_COBBLESTONE_STAIRS:
        break;
    case ITEM_LEVER:
        break;
    case ITEM_STONE_PRESSURE_PLATE:
        break;
    case ITEM_OAK_PRESSURE_PLATE:
        break;
    case ITEM_SPRUCE_PRESSURE_PLATE:
        break;
    case ITEM_BIRCH_PRESSURE_PLATE:
        break;
    case ITEM_JUNGLE_PRESSURE_PLATE:
        break;
    case ITEM_ACACIA_PRESSURE_PLATE:
        break;
    case ITEM_DARK_OAK_PRESSURE_PLATE:
        break;
    case ITEM_CRIMSON_PRESSURE_PLATE:
        break;
    case ITEM_WARPED_PRESSURE_PLATE:
        break;
    case ITEM_POLISHED_BLACKSTONE_PRESSURE_PLATE:
        break;
    case ITEM_REDSTONE_ORE:
        place_simple_block(context, BLOCK_REDSTONE_ORE);
        break;
    case ITEM_REDSTONE_TORCH:
        place_torch(context, BLOCK_REDSTONE_TORCH, BLOCK_REDSTONE_WALL_TORCH);
        break;
    case ITEM_SNOW:
        break;
    case ITEM_ICE:
        place_simple_block(context, BLOCK_ICE);
        break;
    case ITEM_SNOW_BLOCK:
        place_simple_block(context, BLOCK_SNOW_BLOCK);
        break;
    case ITEM_CACTUS:
        break;
    case ITEM_CLAY:
        place_simple_block(context, BLOCK_CLAY);
        break;
    case ITEM_JUKEBOX:
        break;
    case ITEM_OAK_FENCE:
        break;
    case ITEM_SPRUCE_FENCE:
        break;
    case ITEM_BIRCH_FENCE:
        break;
    case ITEM_JUNGLE_FENCE:
        break;
    case ITEM_ACACIA_FENCE:
        break;
    case ITEM_DARK_OAK_FENCE:
        break;
    case ITEM_CRIMSON_FENCE:
        break;
    case ITEM_WARPED_FENCE:
        break;
    case ITEM_PUMPKIN:
        place_simple_block(context, BLOCK_PUMPKIN);
        break;
    case ITEM_CARVED_PUMPKIN:
        // @TODO(traks) spawn iron golem?
        place_horizontal_facing(context, BLOCK_CARVED_PUMPKIN);
        break;
    case ITEM_NETHERRACK:
        place_simple_block(context, BLOCK_NETHERRACK);
        break;
    case ITEM_SOUL_SAND:
        place_simple_block(context, BLOCK_SOUL_SAND);
        break;
    case ITEM_SOUL_SOIL:
        place_simple_block(context, BLOCK_SOUL_SOIL);
        break;
    case ITEM_BASALT:
        place_simple_pillar(context, BLOCK_BASALT);
        break;
    case ITEM_POLISHED_BASALT:
        place_simple_pillar(context, BLOCK_POLISHED_BASALT);
        break;
    case ITEM_SOUL_TORCH:
        place_torch(context, BLOCK_SOUL_TORCH, BLOCK_SOUL_WALL_TORCH);
        break;
    case ITEM_GLOWSTONE:
        place_simple_block(context, BLOCK_GLOWSTONE);
        break;
    case ITEM_JACK_O_LANTERN:
        // @TODO(traks) spawn iron golem?
        place_horizontal_facing(context, BLOCK_JACK_O_LANTERN);
        break;
    case ITEM_OAK_TRAPDOOR:
        break;
    case ITEM_SPRUCE_TRAPDOOR:
        break;
    case ITEM_BIRCH_TRAPDOOR:
        break;
    case ITEM_JUNGLE_TRAPDOOR:
        break;
    case ITEM_ACACIA_TRAPDOOR:
        break;
    case ITEM_DARK_OAK_TRAPDOOR:
        break;
    case ITEM_CRIMSON_TRAPDOOR:
        break;
    case ITEM_WARPED_TRAPDOOR:
        break;
    case ITEM_INFESTED_STONE:
        place_simple_block(context, BLOCK_INFESTED_STONE);
        break;
    case ITEM_INFESTED_COBBLESTONE:
        place_simple_block(context, BLOCK_INFESTED_COBBLESTONE);
        break;
    case ITEM_INFESTED_STONE_BRICKS:
        place_simple_block(context, BLOCK_INFESTED_STONE_BRICKS);
        break;
    case ITEM_INFESTED_MOSSY_STONE_BRICKS:
        place_simple_block(context, BLOCK_INFESTED_MOSSY_STONE_BRICKS);
        break;
    case ITEM_INFESTED_CRACKED_STONE_BRICKS:
        place_simple_block(context, BLOCK_INFESTED_CRACKED_STONE_BRICKS);
        break;
    case ITEM_INFESTED_CHISELED_STONE_BRICKS:
        place_simple_block(context, BLOCK_INFESTED_CHISELED_STONE_BRICKS);
        break;
    case ITEM_STONE_BRICKS:
        place_simple_block(context, BLOCK_STONE_BRICKS);
        break;
    case ITEM_MOSSY_STONE_BRICKS:
        place_simple_block(context, BLOCK_MOSSY_STONE_BRICKS);
        break;
    case ITEM_CRACKED_STONE_BRICKS:
        place_simple_block(context, BLOCK_CRACKED_STONE_BRICKS);
        break;
    case ITEM_CHISELED_STONE_BRICKS:
        place_simple_block(context, BLOCK_CHISELED_STONE_BRICKS);
        break;
    case ITEM_BROWN_MUSHROOM_BLOCK:
        place_mushroom_block(context, BLOCK_BROWN_MUSHROOM_BLOCK);
        break;
    case ITEM_RED_MUSHROOM_BLOCK:
        place_mushroom_block(context, BLOCK_RED_MUSHROOM_BLOCK);
        break;
    case ITEM_MUSHROOM_STEM:
        place_mushroom_block(context, BLOCK_MUSHROOM_STEM);
        break;
    case ITEM_IRON_BARS:
        break;
    case ITEM_CHAIN:
        place_chain(context, BLOCK_CHAIN);
        break;
    case ITEM_GLASS_PANE:
        break;
    case ITEM_MELON:
        place_simple_block(context, BLOCK_MELON);
        break;
    case ITEM_VINE:
        break;
    case ITEM_OAK_FENCE_GATE:
        break;
    case ITEM_SPRUCE_FENCE_GATE:
        break;
    case ITEM_BIRCH_FENCE_GATE:
        break;
    case ITEM_JUNGLE_FENCE_GATE:
        break;
    case ITEM_ACACIA_FENCE_GATE:
        break;
    case ITEM_DARK_OAK_FENCE_GATE:
        break;
    case ITEM_CRIMSON_FENCE_GATE:
        break;
    case ITEM_WARPED_FENCE_GATE:
        break;
    case ITEM_BRICK_STAIRS:
        break;
    case ITEM_STONE_BRICK_STAIRS:
        break;
    case ITEM_MYCELIUM:
        place_snowy_grassy_block(context, BLOCK_MYCELIUM);
        break;
    case ITEM_NETHER_BRICKS:
        place_simple_block(context, BLOCK_NETHER_BRICKS);
        break;
    case ITEM_CRACKED_NETHER_BRICKS:
        place_simple_block(context, BLOCK_CRACKED_NETHER_BRICKS);
        break;
    case ITEM_CHISELED_NETHER_BRICKS:
        place_simple_block(context, BLOCK_CHISELED_NETHER_BRICKS);
        break;
    case ITEM_NETHER_BRICK_FENCE:
        break;
    case ITEM_NETHER_BRICK_STAIRS:
        break;
    case ITEM_ENCHANTING_TABLE:
        break;
    case ITEM_END_PORTAL_FRAME:
        place_end_portal_frame(context, BLOCK_END_PORTAL_FRAME);
        break;
    case ITEM_END_STONE:
        place_simple_block(context, BLOCK_END_STONE);
        break;
    case ITEM_END_STONE_BRICKS:
        place_simple_block(context, BLOCK_END_STONE_BRICKS);
        break;
    case ITEM_DRAGON_EGG:
        break;
    case ITEM_REDSTONE_LAMP:
        break;
    case ITEM_SANDSTONE_STAIRS:
        break;
    case ITEM_EMERALD_ORE:
        place_simple_block(context, BLOCK_EMERALD_ORE);
        break;
    case ITEM_ENDER_CHEST:
        break;
    case ITEM_TRIPWIRE_HOOK:
        break;
    case ITEM_EMERALD_BLOCK:
        place_simple_block(context, BLOCK_EMERALD_BLOCK);
        break;
    case ITEM_SPRUCE_STAIRS:
        break;
    case ITEM_BIRCH_STAIRS:
        break;
    case ITEM_JUNGLE_STAIRS:
        break;
    case ITEM_CRIMSON_STAIRS:
        break;
    case ITEM_WARPED_STAIRS:
        break;
    case ITEM_COMMAND_BLOCK:
        break;
    case ITEM_BEACON:
        break;
    case ITEM_COBBLESTONE_WALL:
        break;
    case ITEM_MOSSY_COBBLESTONE_WALL:
        break;
    case ITEM_BRICK_WALL:
        break;
    case ITEM_PRISMARINE_WALL:
        break;
    case ITEM_RED_SANDSTONE_WALL:
        break;
    case ITEM_MOSSY_STONE_BRICK_WALL:
        break;
    case ITEM_GRANITE_WALL:
        break;
    case ITEM_STONE_BRICK_WALL:
        break;
    case ITEM_NETHER_BRICK_WALL:
        break;
    case ITEM_ANDESITE_WALL:
        break;
    case ITEM_RED_NETHER_BRICK_WALL:
        break;
    case ITEM_SANDSTONE_WALL:
        break;
    case ITEM_END_STONE_BRICK_WALL:
        break;
    case ITEM_DIORITE_WALL:
        break;
    case ITEM_BLACKSTONE_WALL:
        break;
    case ITEM_POLISHED_BLACKSTONE_WALL:
        break;
    case ITEM_POLISHED_BLACKSTONE_BRICK_WALL:
        break;
    case ITEM_STONE_BUTTON:
        break;
    case ITEM_OAK_BUTTON:
        break;
    case ITEM_SPRUCE_BUTTON:
        break;
    case ITEM_BIRCH_BUTTON:
        break;
    case ITEM_JUNGLE_BUTTON:
        break;
    case ITEM_ACACIA_BUTTON:
        break;
    case ITEM_DARK_OAK_BUTTON:
        break;
    case ITEM_CRIMSON_BUTTON:
        break;
    case ITEM_WARPED_BUTTON:
        break;
    case ITEM_POLISHED_BLACKSTONE_BUTTON:
        break;
    case ITEM_ANVIL:
        break;
    case ITEM_CHIPPED_ANVIL:
        break;
    case ITEM_DAMAGED_ANVIL:
        break;
    case ITEM_TRAPPED_CHEST:
        break;
    case ITEM_LIGHT_WEIGHTED_PRESSURE_PLATE:
        break;
    case ITEM_HEAVY_WEIGHTED_PRESSURE_PLATE:
        break;
    case ITEM_DAYLIGHT_DETECTOR:
        break;
    case ITEM_REDSTONE_BLOCK:
        place_simple_block(context, BLOCK_REDSTONE_BLOCK);
        break;
    case ITEM_NETHER_QUARTZ_ORE:
        break;
    case ITEM_HOPPER:
        break;
    case ITEM_CHISELED_QUARTZ_BLOCK:
        place_simple_block(context, BLOCK_CHISELED_QUARTZ_BLOCK);
        break;
    case ITEM_QUARTZ_BLOCK:
        place_simple_block(context, BLOCK_QUARTZ_BLOCK);
        break;
    case ITEM_QUARTZ_BRICKS:
        place_simple_block(context, BLOCK_QUARTZ_BRICKS);
        break;
    case ITEM_QUARTZ_PILLAR:
        place_simple_pillar(context, BLOCK_QUARTZ_PILLAR);
        break;
    case ITEM_QUARTZ_STAIRS:
        break;
    case ITEM_ACTIVATOR_RAIL:
        break;
    case ITEM_DROPPER:
        break;
    case ITEM_WHITE_TERRACOTTA:
        place_simple_block(context, BLOCK_WHITE_TERRACOTTA);
        break;
    case ITEM_ORANGE_TERRACOTTA:
        place_simple_block(context, BLOCK_ORANGE_TERRACOTTA);
        break;
    case ITEM_MAGENTA_TERRACOTTA:
        place_simple_block(context, BLOCK_MAGENTA_TERRACOTTA);
        break;
    case ITEM_LIGHT_BLUE_TERRACOTTA:
        place_simple_block(context, BLOCK_LIGHT_BLUE_TERRACOTTA);
        break;
    case ITEM_YELLOW_TERRACOTTA:
        place_simple_block(context, BLOCK_YELLOW_TERRACOTTA);
        break;
    case ITEM_LIME_TERRACOTTA:
        place_simple_block(context, BLOCK_LIME_TERRACOTTA);
        break;
    case ITEM_PINK_TERRACOTTA:
        place_simple_block(context, BLOCK_PINK_TERRACOTTA);
        break;
    case ITEM_GRAY_TERRACOTTA:
        place_simple_block(context, BLOCK_GRAY_TERRACOTTA);
        break;
    case ITEM_LIGHT_GRAY_TERRACOTTA:
        place_simple_block(context, BLOCK_LIGHT_GRAY_TERRACOTTA);
        break;
    case ITEM_CYAN_TERRACOTTA:
        place_simple_block(context, BLOCK_CYAN_TERRACOTTA);
        break;
    case ITEM_PURPLE_TERRACOTTA:
        place_simple_block(context, BLOCK_PURPLE_TERRACOTTA);
        break;
    case ITEM_BLUE_TERRACOTTA:
        place_simple_block(context, BLOCK_BLUE_TERRACOTTA);
        break;
    case ITEM_BROWN_TERRACOTTA:
        place_simple_block(context, BLOCK_BROWN_TERRACOTTA);
        break;
    case ITEM_GREEN_TERRACOTTA:
        place_simple_block(context, BLOCK_GREEN_TERRACOTTA);
        break;
    case ITEM_RED_TERRACOTTA:
        place_simple_block(context, BLOCK_RED_TERRACOTTA);
        break;
    case ITEM_BLACK_TERRACOTTA:
        place_simple_block(context, BLOCK_BLACK_TERRACOTTA);
        break;
    case ITEM_BARRIER:
        break;
    case ITEM_IRON_TRAPDOOR:
        break;
    case ITEM_HAY_BLOCK:
        place_simple_pillar(context, BLOCK_HAY_BLOCK);
        break;
    case ITEM_WHITE_CARPET:
        place_carpet(context, BLOCK_WHITE_CARPET);
        break;
    case ITEM_ORANGE_CARPET:
        place_carpet(context, BLOCK_ORANGE_CARPET);
        break;
    case ITEM_MAGENTA_CARPET:
        place_carpet(context, BLOCK_MAGENTA_CARPET);
        break;
    case ITEM_LIGHT_BLUE_CARPET:
        place_carpet(context, BLOCK_LIGHT_BLUE_CARPET);
        break;
    case ITEM_YELLOW_CARPET:
        place_carpet(context, BLOCK_YELLOW_CARPET);
        break;
    case ITEM_LIME_CARPET:
        place_carpet(context, BLOCK_LIME_CARPET);
        break;
    case ITEM_PINK_CARPET:
        place_carpet(context, BLOCK_PINK_CARPET);
        break;
    case ITEM_GRAY_CARPET:
        place_carpet(context, BLOCK_GRAY_CARPET);
        break;
    case ITEM_LIGHT_GRAY_CARPET:
        place_carpet(context, BLOCK_LIGHT_GRAY_CARPET);
        break;
    case ITEM_CYAN_CARPET:
        place_carpet(context, BLOCK_CYAN_CARPET);
        break;
    case ITEM_PURPLE_CARPET:
        place_carpet(context, BLOCK_PURPLE_CARPET);
        break;
    case ITEM_BLUE_CARPET:
        place_carpet(context, BLOCK_BLUE_CARPET);
        break;
    case ITEM_BROWN_CARPET:
        place_carpet(context, BLOCK_BROWN_CARPET);
        break;
    case ITEM_GREEN_CARPET:
        place_carpet(context, BLOCK_GREEN_CARPET);
        break;
    case ITEM_RED_CARPET:
        place_carpet(context, BLOCK_RED_CARPET);
        break;
    case ITEM_BLACK_CARPET:
        place_carpet(context, BLOCK_BLACK_CARPET);
        break;
    case ITEM_TERRACOTTA:
        place_simple_block(context, BLOCK_TERRACOTTA);
        break;
    case ITEM_COAL_BLOCK:
        place_simple_block(context, BLOCK_COAL_BLOCK);
        break;
    case ITEM_PACKED_ICE:
        place_simple_block(context, BLOCK_PACKED_ICE);
        break;
    case ITEM_ACACIA_STAIRS:
        break;
    case ITEM_DARK_OAK_STAIRS:
        break;
    case ITEM_SLIME_BLOCK:
        place_simple_block(context, BLOCK_SLIME_BLOCK);
        break;
    case ITEM_GRASS_PATH:
        break;
    case ITEM_SUNFLOWER:
        break;
    case ITEM_LILAC:
        break;
    case ITEM_ROSE_BUSH:
        break;
    case ITEM_PEONY:
        break;
    case ITEM_TALL_GRASS:
        break;
    case ITEM_LARGE_FERN:
        break;
    case ITEM_WHITE_STAINED_GLASS:
        place_simple_block(context, BLOCK_WHITE_STAINED_GLASS);
        break;
    case ITEM_ORANGE_STAINED_GLASS:
        place_simple_block(context, BLOCK_ORANGE_STAINED_GLASS);
        break;
    case ITEM_MAGENTA_STAINED_GLASS:
        place_simple_block(context, BLOCK_MAGENTA_STAINED_GLASS);
        break;
    case ITEM_LIGHT_BLUE_STAINED_GLASS:
        place_simple_block(context, BLOCK_LIGHT_BLUE_STAINED_GLASS);
        break;
    case ITEM_YELLOW_STAINED_GLASS:
        place_simple_block(context, BLOCK_YELLOW_STAINED_GLASS);
        break;
    case ITEM_LIME_STAINED_GLASS:
        place_simple_block(context, BLOCK_LIME_STAINED_GLASS);
        break;
    case ITEM_PINK_STAINED_GLASS:
        place_simple_block(context, BLOCK_PINK_STAINED_GLASS);
        break;
    case ITEM_GRAY_STAINED_GLASS:
        place_simple_block(context, BLOCK_GRAY_STAINED_GLASS);
        break;
    case ITEM_LIGHT_GRAY_STAINED_GLASS:
        place_simple_block(context, BLOCK_LIGHT_GRAY_STAINED_GLASS);
        break;
    case ITEM_CYAN_STAINED_GLASS:
        place_simple_block(context, BLOCK_CYAN_STAINED_GLASS);
        break;
    case ITEM_PURPLE_STAINED_GLASS:
        place_simple_block(context, BLOCK_PURPLE_STAINED_GLASS);
        break;
    case ITEM_BLUE_STAINED_GLASS:
        place_simple_block(context, BLOCK_BLUE_STAINED_GLASS);
        break;
    case ITEM_BROWN_STAINED_GLASS:
        place_simple_block(context, BLOCK_BROWN_STAINED_GLASS);
        break;
    case ITEM_GREEN_STAINED_GLASS:
        place_simple_block(context, BLOCK_GREEN_STAINED_GLASS);
        break;
    case ITEM_RED_STAINED_GLASS:
        place_simple_block(context, BLOCK_RED_STAINED_GLASS);
        break;
    case ITEM_BLACK_STAINED_GLASS:
        place_simple_block(context, BLOCK_BLACK_STAINED_GLASS);
        break;
    case ITEM_WHITE_STAINED_GLASS_PANE:
        break;
    case ITEM_ORANGE_STAINED_GLASS_PANE:
        break;
    case ITEM_MAGENTA_STAINED_GLASS_PANE:
        break;
    case ITEM_LIGHT_BLUE_STAINED_GLASS_PANE:
        break;
    case ITEM_YELLOW_STAINED_GLASS_PANE:
        break;
    case ITEM_LIME_STAINED_GLASS_PANE:
        break;
    case ITEM_PINK_STAINED_GLASS_PANE:
        break;
    case ITEM_GRAY_STAINED_GLASS_PANE:
        break;
    case ITEM_LIGHT_GRAY_STAINED_GLASS_PANE:
        break;
    case ITEM_CYAN_STAINED_GLASS_PANE:
        break;
    case ITEM_PURPLE_STAINED_GLASS_PANE:
        break;
    case ITEM_BLUE_STAINED_GLASS_PANE:
        break;
    case ITEM_BROWN_STAINED_GLASS_PANE:
        break;
    case ITEM_GREEN_STAINED_GLASS_PANE:
        break;
    case ITEM_RED_STAINED_GLASS_PANE:
        break;
    case ITEM_BLACK_STAINED_GLASS_PANE:
        break;
    case ITEM_PRISMARINE:
        place_simple_block(context, BLOCK_PRISMARINE);
        break;
    case ITEM_PRISMARINE_BRICKS:
        place_simple_block(context, BLOCK_PRISMARINE_BRICKS);
        break;
    case ITEM_DARK_PRISMARINE:
        place_simple_block(context, BLOCK_DARK_PRISMARINE);
        break;
    case ITEM_PRISMARINE_STAIRS:
        break;
    case ITEM_PRISMARINE_BRICK_STAIRS:
        break;
    case ITEM_DARK_PRISMARINE_STAIRS:
        break;
    case ITEM_SEA_LANTERN:
        place_simple_block(context, BLOCK_SEA_LANTERN);
        break;
    case ITEM_RED_SANDSTONE:
        place_simple_block(context, BLOCK_RED_SANDSTONE);
        break;
    case ITEM_CHISELED_RED_SANDSTONE:
        place_simple_block(context, BLOCK_CHISELED_RED_SANDSTONE);
        break;
    case ITEM_CUT_RED_SANDSTONE:
        place_simple_block(context, BLOCK_CUT_RED_SANDSTONE);
        break;
    case ITEM_RED_SANDSTONE_STAIRS:
        break;
    case ITEM_REPEATING_COMMAND_BLOCK:
        break;
    case ITEM_CHAIN_COMMAND_BLOCK:
        break;
    case ITEM_MAGMA_BLOCK:
        place_simple_block(context, BLOCK_MAGMA_BLOCK);
        break;
    case ITEM_NETHER_WART_BLOCK:
        place_simple_block(context, BLOCK_NETHER_WART_BLOCK);
        break;
    case ITEM_WARPED_WART_BLOCK:
        place_simple_block(context, BLOCK_WARPED_WART_BLOCK);
        break;
    case ITEM_RED_NETHER_BRICKS:
        place_simple_block(context, BLOCK_RED_NETHER_BRICKS);
        break;
    case ITEM_BONE_BLOCK:
        place_simple_pillar(context, BLOCK_BONE_BLOCK);
        break;
    case ITEM_STRUCTURE_VOID:
        break;
    case ITEM_OBSERVER:
        break;
    case ITEM_SHULKER_BOX:
        break;
    case ITEM_WHITE_SHULKER_BOX:
        break;
    case ITEM_ORANGE_SHULKER_BOX:
        break;
    case ITEM_MAGENTA_SHULKER_BOX:
        break;
    case ITEM_LIGHT_BLUE_SHULKER_BOX:
        break;
    case ITEM_YELLOW_SHULKER_BOX:
        break;
    case ITEM_LIME_SHULKER_BOX:
        break;
    case ITEM_PINK_SHULKER_BOX:
        break;
    case ITEM_GRAY_SHULKER_BOX:
        break;
    case ITEM_LIGHT_GRAY_SHULKER_BOX:
        break;
    case ITEM_CYAN_SHULKER_BOX:
        break;
    case ITEM_PURPLE_SHULKER_BOX:
        break;
    case ITEM_BLUE_SHULKER_BOX:
        break;
    case ITEM_BROWN_SHULKER_BOX:
        break;
    case ITEM_GREEN_SHULKER_BOX:
        break;
    case ITEM_RED_SHULKER_BOX:
        break;
    case ITEM_BLACK_SHULKER_BOX:
        break;
    case ITEM_WHITE_GLAZED_TERRACOTTA:
        break;
    case ITEM_ORANGE_GLAZED_TERRACOTTA:
        break;
    case ITEM_MAGENTA_GLAZED_TERRACOTTA:
        break;
    case ITEM_LIGHT_BLUE_GLAZED_TERRACOTTA:
        break;
    case ITEM_YELLOW_GLAZED_TERRACOTTA:
        break;
    case ITEM_LIME_GLAZED_TERRACOTTA:
        break;
    case ITEM_PINK_GLAZED_TERRACOTTA:
        break;
    case ITEM_GRAY_GLAZED_TERRACOTTA:
        break;
    case ITEM_LIGHT_GRAY_GLAZED_TERRACOTTA:
        break;
    case ITEM_CYAN_GLAZED_TERRACOTTA:
        break;
    case ITEM_PURPLE_GLAZED_TERRACOTTA:
        break;
    case ITEM_BLUE_GLAZED_TERRACOTTA:
        break;
    case ITEM_BROWN_GLAZED_TERRACOTTA:
        break;
    case ITEM_GREEN_GLAZED_TERRACOTTA:
        break;
    case ITEM_RED_GLAZED_TERRACOTTA:
        break;
    case ITEM_BLACK_GLAZED_TERRACOTTA:
        break;
    case ITEM_WHITE_CONCRETE:
        place_simple_block(context, BLOCK_WHITE_CONCRETE);
        break;
    case ITEM_ORANGE_CONCRETE:
        place_simple_block(context, BLOCK_ORANGE_CONCRETE);
        break;
    case ITEM_MAGENTA_CONCRETE:
        place_simple_block(context, BLOCK_MAGENTA_CONCRETE);
        break;
    case ITEM_LIGHT_BLUE_CONCRETE:
        place_simple_block(context, BLOCK_BLUE_CONCRETE);
        break;
    case ITEM_YELLOW_CONCRETE:
        place_simple_block(context, BLOCK_YELLOW_CONCRETE);
        break;
    case ITEM_LIME_CONCRETE:
        place_simple_block(context, BLOCK_LIME_CONCRETE);
        break;
    case ITEM_PINK_CONCRETE:
        place_simple_block(context, BLOCK_PINK_CONCRETE);
        break;
    case ITEM_GRAY_CONCRETE:
        place_simple_block(context, BLOCK_GRAY_CONCRETE);
        break;
    case ITEM_LIGHT_GRAY_CONCRETE:
        place_simple_block(context, BLOCK_LIGHT_GRAY_CONCRETE);
        break;
    case ITEM_CYAN_CONCRETE:
        place_simple_block(context, BLOCK_CYAN_CONCRETE);
        break;
    case ITEM_PURPLE_CONCRETE:
        place_simple_block(context, BLOCK_PURPLE_CONCRETE);
        break;
    case ITEM_BLUE_CONCRETE:
        place_simple_block(context, BLOCK_BLUE_CONCRETE);
        break;
    case ITEM_BROWN_CONCRETE:
        place_simple_block(context, BLOCK_BROWN_CONCRETE);
        break;
    case ITEM_GREEN_CONCRETE:
        place_simple_block(context, BLOCK_GREEN_CONCRETE);
        break;
    case ITEM_RED_CONCRETE:
        place_simple_block(context, BLOCK_RED_CONCRETE);
        break;
    case ITEM_BLACK_CONCRETE:
        place_simple_block(context, BLOCK_BLACK_CONCRETE);
        break;
    case ITEM_WHITE_CONCRETE_POWDER:
        break;
    case ITEM_ORANGE_CONCRETE_POWDER:
        break;
    case ITEM_MAGENTA_CONCRETE_POWDER:
        break;
    case ITEM_LIGHT_BLUE_CONCRETE_POWDER:
        break;
    case ITEM_YELLOW_CONCRETE_POWDER:
        break;
    case ITEM_LIME_CONCRETE_POWDER:
        break;
    case ITEM_PINK_CONCRETE_POWDER:
        break;
    case ITEM_GRAY_CONCRETE_POWDER:
        break;
    case ITEM_LIGHT_GRAY_CONCRETE_POWDER:
        break;
    case ITEM_CYAN_CONCRETE_POWDER:
        break;
    case ITEM_PURPLE_CONCRETE_POWDER:
        break;
    case ITEM_BLUE_CONCRETE_POWDER:
        break;
    case ITEM_BROWN_CONCRETE_POWDER:
        break;
    case ITEM_GREEN_CONCRETE_POWDER:
        break;
    case ITEM_RED_CONCRETE_POWDER:
        break;
    case ITEM_BLACK_CONCRETE_POWDER:
        break;
    case ITEM_TURTLE_EGG:
        break;
    case ITEM_DEAD_TUBE_CORAL_BLOCK:
        place_simple_block(context, BLOCK_DEAD_TUBE_CORAL_BLOCK);
        break;
    case ITEM_DEAD_BRAIN_CORAL_BLOCK:
        place_simple_block(context, BLOCK_DEAD_BRAIN_CORAL_BLOCK);
        break;
    case ITEM_DEAD_BUBBLE_CORAL_BLOCK:
        place_simple_block(context, BLOCK_DEAD_BUBBLE_CORAL_BLOCK);
        break;
    case ITEM_DEAD_FIRE_CORAL_BLOCK:
        place_simple_block(context, BLOCK_DEAD_FIRE_CORAL_BLOCK);
        break;
    case ITEM_DEAD_HORN_CORAL_BLOCK:
        place_simple_block(context, BLOCK_DEAD_HORN_CORAL_BLOCK);
        break;
    case ITEM_TUBE_CORAL_BLOCK:
        break;
    case ITEM_BRAIN_CORAL_BLOCK:
        break;
    case ITEM_BUBBLE_CORAL_BLOCK:
        break;
    case ITEM_FIRE_CORAL_BLOCK:
        break;
    case ITEM_HORN_CORAL_BLOCK:
        break;
    case ITEM_TUBE_CORAL:
        break;
    case ITEM_BRAIN_CORAL:
        break;
    case ITEM_BUBBLE_CORAL:
        break;
    case ITEM_FIRE_CORAL:
        break;
    case ITEM_HORN_CORAL:
        break;
    case ITEM_DEAD_BRAIN_CORAL:
        place_dead_coral(context, BLOCK_DEAD_BRAIN_CORAL);
        break;
    case ITEM_DEAD_BUBBLE_CORAL:
        place_dead_coral(context, BLOCK_DEAD_BUBBLE_CORAL);
        break;
    case ITEM_DEAD_FIRE_CORAL:
        place_dead_coral(context, BLOCK_DEAD_FIRE_CORAL);
        break;
    case ITEM_DEAD_HORN_CORAL:
        place_dead_coral(context, BLOCK_DEAD_HORN_CORAL);
        break;
    case ITEM_DEAD_TUBE_CORAL:
        place_dead_coral(context, BLOCK_DEAD_TUBE_CORAL);
        break;
    case ITEM_TUBE_CORAL_FAN:
        break;
    case ITEM_BRAIN_CORAL_FAN:
        break;
    case ITEM_BUBBLE_CORAL_FAN:
        break;
    case ITEM_FIRE_CORAL_FAN:
        break;
    case ITEM_HORN_CORAL_FAN:
        break;
    case ITEM_DEAD_TUBE_CORAL_FAN:
        place_dead_coral_fan(context, BLOCK_DEAD_TUBE_CORAL_FAN, BLOCK_DEAD_TUBE_CORAL_WALL_FAN);
        break;
    case ITEM_DEAD_BRAIN_CORAL_FAN:
        place_dead_coral_fan(context, BLOCK_DEAD_BRAIN_CORAL_FAN, BLOCK_DEAD_BRAIN_CORAL_WALL_FAN);
        break;
    case ITEM_DEAD_BUBBLE_CORAL_FAN:
        place_dead_coral_fan(context, BLOCK_DEAD_BUBBLE_CORAL_FAN, BLOCK_DEAD_BUBBLE_CORAL_WALL_FAN);
        break;
    case ITEM_DEAD_FIRE_CORAL_FAN:
        place_dead_coral_fan(context, BLOCK_DEAD_FIRE_CORAL_FAN, BLOCK_DEAD_FIRE_CORAL_WALL_FAN);
        break;
    case ITEM_DEAD_HORN_CORAL_FAN:
        place_dead_coral_fan(context, BLOCK_DEAD_HORN_CORAL_FAN, BLOCK_DEAD_HORN_CORAL_WALL_FAN);
        break;
    case ITEM_BLUE_ICE:
        place_simple_block(context, BLOCK_BLUE_ICE);
        break;
    case ITEM_CONDUIT:
        break;
    case ITEM_POLISHED_GRANITE_STAIRS:
        break;
    case ITEM_SMOOTH_RED_SANDSTONE_STAIRS:
        break;
    case ITEM_MOSSY_STONE_BRICK_STAIRS:
        break;
    case ITEM_POLISHED_DIORITE_STAIRS:
        break;
    case ITEM_MOSSY_COBBLESTONE_STAIRS:
        break;
    case ITEM_END_STONE_BRICK_STAIRS:
        break;
    case ITEM_STONE_STAIRS:
        break;
    case ITEM_SMOOTH_SANDSTONE_STAIRS:
        break;
    case ITEM_SMOOTH_QUARTZ_STAIRS:
        break;
    case ITEM_GRANITE_STAIRS:
        break;
    case ITEM_ANDESITE_STAIRS:
        break;
    case ITEM_RED_NETHER_BRICK_STAIRS:
        break;
    case ITEM_POLISHED_ANDESITE_STAIRS:
        break;
    case ITEM_DIORITE_STAIRS:
        break;
    case ITEM_POLISHED_GRANITE_SLAB:
        place_slab(context, BLOCK_POLISHED_GRANITE_SLAB);
        break;
    case ITEM_SMOOTH_RED_SANDSTONE_SLAB:
        place_slab(context, BLOCK_SMOOTH_RED_SANDSTONE_SLAB);
        break;
    case ITEM_MOSSY_STONE_BRICK_SLAB:
        place_slab(context, BLOCK_MOSSY_STONE_BRICK_SLAB);
        break;
    case ITEM_POLISHED_DIORITE_SLAB:
        place_slab(context, BLOCK_POLISHED_DIORITE_SLAB);
        break;
    case ITEM_MOSSY_COBBLESTONE_SLAB:
        place_slab(context, BLOCK_MOSSY_COBBLESTONE_SLAB);
        break;
    case ITEM_END_STONE_BRICK_SLAB:
        place_slab(context, BLOCK_END_STONE_BRICK_SLAB);
        break;
    case ITEM_SMOOTH_SANDSTONE_SLAB:
        place_slab(context, BLOCK_SMOOTH_SANDSTONE_SLAB);
        break;
    case ITEM_SMOOTH_QUARTZ_SLAB:
        place_slab(context, BLOCK_SMOOTH_QUARTZ_SLAB);
        break;
    case ITEM_GRANITE_SLAB:
        place_slab(context, BLOCK_GRANITE_SLAB);
        break;
    case ITEM_ANDESITE_SLAB:
        place_slab(context, BLOCK_ANDESITE_SLAB);
        break;
    case ITEM_RED_NETHER_BRICK_SLAB:
        place_slab(context, BLOCK_RED_NETHER_BRICK_SLAB);
        break;
    case ITEM_POLISHED_ANDESITE_SLAB:
        place_slab(context, BLOCK_POLISHED_ANDESITE_SLAB);
        break;
    case ITEM_DIORITE_SLAB:
        place_slab(context, BLOCK_DIORITE_SLAB);
        break;
    case ITEM_SCAFFOLDING:
        break;
    case ITEM_IRON_DOOR:
        break;
    case ITEM_OAK_DOOR:
        break;
    case ITEM_SPRUCE_DOOR:
        break;
    case ITEM_BIRCH_DOOR:
        break;
    case ITEM_JUNGLE_DOOR:
        break;
    case ITEM_ACACIA_DOOR:
        break;
    case ITEM_DARK_OAK_DOOR:
        break;
    case ITEM_CRIMSON_DOOR:
        break;
    case ITEM_WARPED_DOOR:
        break;
    case ITEM_REPEATER:
        break;
    case ITEM_COMPARATOR:
        break;
    case ITEM_STRUCTURE_BLOCK:
        break;
    case ITEM_JIGSAW:
        break;
    case ITEM_IRON_SHOVEL:
        break;
    case ITEM_IRON_AXE:
        break;
    case ITEM_FLINT_AND_STEEL:
        break;
    case ITEM_WOODEN_SHOVEL:
        break;
    case ITEM_WOODEN_AXE:
        break;
    case ITEM_STONE_SHOVEL:
        break;
    case ITEM_STONE_AXE:
        break;
    case ITEM_DIAMOND_SHOVEL:
        break;
    case ITEM_DIAMOND_AXE:
        break;
    case ITEM_GOLDEN_SHOVEL:
        break;
    case ITEM_GOLDEN_AXE:
        break;
    case ITEM_NETHERITE_SHOVEL:
        break;
    case ITEM_NETHERITE_AXE:
        break;
    case ITEM_STRING:
        break;
    case ITEM_WOODEN_HOE:
        break;
    case ITEM_STONE_HOE:
        break;
    case ITEM_IRON_HOE:
        break;
    case ITEM_DIAMOND_HOE:
        break;
    case ITEM_GOLDEN_HOE:
        break;
    case ITEM_NETHERITE_HOE:
        break;
    case ITEM_WHEAT_SEEDS:
        place_crop(context, BLOCK_WHEAT);
        break;
    case ITEM_PAINTING:
        break;
    case ITEM_OAK_SIGN:
        break;
    case ITEM_SPRUCE_SIGN:
        break;
    case ITEM_BIRCH_SIGN:
        break;
    case ITEM_JUNGLE_SIGN:
        break;
    case ITEM_ACACIA_SIGN:
        break;
    case ITEM_DARK_OAK_SIGN:
        break;
    case ITEM_CRIMSON_SIGN:
        break;
    case ITEM_WARPED_SIGN:
        break;
    case ITEM_MINECART:
        break;
    case ITEM_REDSTONE:
        break;
    case ITEM_DRIED_KELP_BLOCK:
        place_simple_block(context, BLOCK_DRIED_KELP_BLOCK);
        break;
    case ITEM_CHEST_MINECART:
        break;
    case ITEM_FURNACE_MINECART:
        break;
    case ITEM_COMPASS:
        break;
    case ITEM_COCOA_BEANS:
        break;
    case ITEM_BONE_MEAL:
        break;
    case ITEM_CAKE:
        break;
    case ITEM_WHITE_BED:
        break;
    case ITEM_ORANGE_BED:
        break;
    case ITEM_MAGENTA_BED:
        break;
    case ITEM_LIGHT_BLUE_BED:
        break;
    case ITEM_YELLOW_BED:
        break;
    case ITEM_LIME_BED:
        break;
    case ITEM_PINK_BED:
        break;
    case ITEM_GRAY_BED:
        break;
    case ITEM_LIGHT_GRAY_BED:
        break;
    case ITEM_CYAN_BED:
        break;
    case ITEM_PURPLE_BED:
        break;
    case ITEM_BLUE_BED:
        break;
    case ITEM_BROWN_BED:
        break;
    case ITEM_GREEN_BED:
        break;
    case ITEM_RED_BED:
        break;
    case ITEM_BLACK_BED:
        break;
    case ITEM_FILLED_MAP:
        break;
    case ITEM_PUMPKIN_SEEDS:
        place_crop(context, BLOCK_PUMPKIN_STEM);
        break;
    case ITEM_MELON_SEEDS:
        place_crop(context, BLOCK_MELON_STEM);
        break;
    case ITEM_NETHER_WART:
        place_nether_wart(context, BLOCK_NETHER_WART);
        break;
    case ITEM_BREWING_STAND:
        break;
    case ITEM_CAULDRON:
        place_simple_block(context, BLOCK_CAULDRON);
        break;
    case ITEM_ENDER_EYE:
        break;
    case ITEM_BAT_SPAWN_EGG:
        break;
    case ITEM_BEE_SPAWN_EGG:
        break;
    case ITEM_BLAZE_SPAWN_EGG:
        break;
    case ITEM_CAT_SPAWN_EGG:
        break;
    case ITEM_CAVE_SPIDER_SPAWN_EGG:
        break;
    case ITEM_CHICKEN_SPAWN_EGG:
        break;
    case ITEM_COD_SPAWN_EGG:
        break;
    case ITEM_COW_SPAWN_EGG:
        break;
    case ITEM_CREEPER_SPAWN_EGG:
        break;
    case ITEM_DOLPHIN_SPAWN_EGG:
        break;
    case ITEM_DONKEY_SPAWN_EGG:
        break;
    case ITEM_DROWNED_SPAWN_EGG:
        break;
    case ITEM_ELDER_GUARDIAN_SPAWN_EGG:
        break;
    case ITEM_ENDERMAN_SPAWN_EGG:
        break;
    case ITEM_ENDERMITE_SPAWN_EGG:
        break;
    case ITEM_EVOKER_SPAWN_EGG:
        break;
    case ITEM_FOX_SPAWN_EGG:
        break;
    case ITEM_GHAST_SPAWN_EGG:
        break;
    case ITEM_GUARDIAN_SPAWN_EGG:
        break;
    case ITEM_HOGLIN_SPAWN_EGG:
        break;
    case ITEM_HORSE_SPAWN_EGG:
        break;
    case ITEM_HUSK_SPAWN_EGG:
        break;
    case ITEM_LLAMA_SPAWN_EGG:
        break;
    case ITEM_MAGMA_CUBE_SPAWN_EGG:
        break;
    case ITEM_MOOSHROOM_SPAWN_EGG:
        break;
    case ITEM_MULE_SPAWN_EGG:
        break;
    case ITEM_OCELOT_SPAWN_EGG:
        break;
    case ITEM_PANDA_SPAWN_EGG:
        break;
    case ITEM_PARROT_SPAWN_EGG:
        break;
    case ITEM_PHANTOM_SPAWN_EGG:
        break;
    case ITEM_PIG_SPAWN_EGG:
        break;
    case ITEM_PIGLIN_SPAWN_EGG:
        break;
    case ITEM_PIGLIN_BRUTE_SPAWN_EGG:
        break;
    case ITEM_PILLAGER_SPAWN_EGG:
        break;
    case ITEM_POLAR_BEAR_SPAWN_EGG:
        break;
    case ITEM_PUFFERFISH_SPAWN_EGG:
        break;
    case ITEM_RABBIT_SPAWN_EGG:
        break;
    case ITEM_RAVAGER_SPAWN_EGG:
        break;
    case ITEM_SALMON_SPAWN_EGG:
        break;
    case ITEM_SHEEP_SPAWN_EGG:
        break;
    case ITEM_SHULKER_SPAWN_EGG:
        break;
    case ITEM_SILVERFISH_SPAWN_EGG:
        break;
    case ITEM_SKELETON_SPAWN_EGG:
        break;
    case ITEM_SKELETON_HORSE_SPAWN_EGG:
        break;
    case ITEM_SLIME_SPAWN_EGG:
        break;
    case ITEM_SPIDER_SPAWN_EGG:
        break;
    case ITEM_SQUID_SPAWN_EGG:
        break;
    case ITEM_STRAY_SPAWN_EGG:
        break;
    case ITEM_STRIDER_SPAWN_EGG:
        break;
    case ITEM_TRADER_LLAMA_SPAWN_EGG:
        break;
    case ITEM_TROPICAL_FISH_SPAWN_EGG:
        break;
    case ITEM_TURTLE_SPAWN_EGG:
        break;
    case ITEM_VEX_SPAWN_EGG:
        break;
    case ITEM_VILLAGER_SPAWN_EGG:
        break;
    case ITEM_VINDICATOR_SPAWN_EGG:
        break;
    case ITEM_WANDERING_TRADER_SPAWN_EGG:
        break;
    case ITEM_WITCH_SPAWN_EGG:
        break;
    case ITEM_WITHER_SKELETON_SPAWN_EGG:
        break;
    case ITEM_WOLF_SPAWN_EGG:
        break;
    case ITEM_ZOGLIN_SPAWN_EGG:
        break;
    case ITEM_ZOMBIE_SPAWN_EGG:
        break;
    case ITEM_ZOMBIE_HORSE_SPAWN_EGG:
        break;
    case ITEM_ZOMBIE_VILLAGER_SPAWN_EGG:
        break;
    case ITEM_ZOMBIFIED_PIGLIN_SPAWN_EGG:
        break;
    case ITEM_FIRE_CHARGE:
        break;
    case ITEM_WRITABLE_BOOK:
        break;
    case ITEM_WRITTEN_BOOK:
        break;
    case ITEM_ITEM_FRAME:
        break;
    case ITEM_FLOWER_POT:
        place_simple_block(context, BLOCK_FLOWER_POT);
        break;
    case ITEM_CARROT:
        place_crop(context, BLOCK_CARROTS);
        break;
    case ITEM_POTATO:
        place_crop(context, BLOCK_POTATOES);
        break;
    case ITEM_SKELETON_SKULL:
        break;
    case ITEM_WITHER_SKELETON_SKULL:
        break;
    case ITEM_PLAYER_HEAD:
        break;
    case ITEM_ZOMBIE_HEAD:
        break;
    case ITEM_CREEPER_HEAD:
        break;
    case ITEM_DRAGON_HEAD:
        break;
    case ITEM_FIREWORK_ROCKET:
        break;
    case ITEM_TNT_MINECART:
        break;
    case ITEM_HOPPER_MINECART:
        break;
    case ITEM_ARMOR_STAND:
        break;
    case ITEM_LEAD:
        break;
    case ITEM_COMMAND_BLOCK_MINECART:
        break;
    case ITEM_WHITE_BANNER:
        break;
    case ITEM_ORANGE_BANNER:
        break;
    case ITEM_MAGENTA_BANNER:
        break;
    case ITEM_LIGHT_BLUE_BANNER:
        break;
    case ITEM_YELLOW_BANNER:
        break;
    case ITEM_LIME_BANNER:
        break;
    case ITEM_PINK_BANNER:
        break;
    case ITEM_GRAY_BANNER:
        break;
    case ITEM_LIGHT_GRAY_BANNER:
        break;
    case ITEM_CYAN_BANNER:
        break;
    case ITEM_PURPLE_BANNER:
        break;
    case ITEM_BLUE_BANNER:
        break;
    case ITEM_BROWN_BANNER:
        break;
    case ITEM_GREEN_BANNER:
        break;
    case ITEM_RED_BANNER:
        break;
    case ITEM_BLACK_BANNER:
        break;
    case ITEM_END_CRYSTAL:
        break;
    case ITEM_BEETROOT_SEEDS:
        place_crop(context, BLOCK_BEETROOTS);
        break;
    case ITEM_DEBUG_STICK:
        break;
    case ITEM_MUSIC_DISC_13:
        break;
    case ITEM_MUSIC_DISC_CAT:
        break;
    case ITEM_MUSIC_DISC_BLOCKS:
        break;
    case ITEM_MUSIC_DISC_CHIRP:
        break;
    case ITEM_MUSIC_DISC_FAR:
        break;
    case ITEM_MUSIC_DISC_MALL:
        break;
    case ITEM_MUSIC_DISC_MELLOHI:
        break;
    case ITEM_MUSIC_DISC_STAL:
        break;
    case ITEM_MUSIC_DISC_STRAD:
        break;
    case ITEM_MUSIC_DISC_WARD:
        break;
    case ITEM_MUSIC_DISC_11:
        break;
    case ITEM_MUSIC_DISC_WAIT:
        break;
    case ITEM_MUSIC_DISC_PIGSTEP:
        break;
    case ITEM_LOOM:
        place_horizontal_facing(context, BLOCK_LOOM);
        break;
    case ITEM_COMPOSTER:
        place_simple_block(context, BLOCK_COMPOSTER);
        break;
    case ITEM_BARREL:
        break;
    case ITEM_SMOKER:
        break;
    case ITEM_BLAST_FURNACE:
        break;
    case ITEM_CARTOGRAPHY_TABLE:
        place_simple_block(context, BLOCK_CARTOGRAPHY_TABLE);
        break;
    case ITEM_FLETCHING_TABLE:
        place_simple_block(context, BLOCK_FLETCHING_TABLE);
        break;
    case ITEM_GRINDSTONE:
        break;
    case ITEM_LECTERN:
        break;
    case ITEM_SMITHING_TABLE:
        place_simple_block(context, BLOCK_SMITHING_TABLE);
        break;
    case ITEM_STONECUTTER:
        break;
    case ITEM_BELL:
        break;
    case ITEM_LANTERN:
        break;
    case ITEM_SOUL_LANTERN:
        break;
    case ITEM_SWEET_BERRIES:
        place_plant(context, BLOCK_SWEET_BERRY_BUSH);
        break;
    case ITEM_CAMPFIRE:
        break;
    case ITEM_SOUL_CAMPFIRE:
        break;
    case ITEM_SHROOMLIGHT:
        place_simple_block(context, BLOCK_SHROOMLIGHT);
        break;
    case ITEM_BEE_NEST:
        break;
    case ITEM_BEEHIVE:
        break;
    case ITEM_HONEY_BLOCK:
        place_simple_block(context, BLOCK_HONEY_BLOCK);
        break;
    case ITEM_HONEYCOMB_BLOCK:
        place_simple_block(context, BLOCK_HONEYCOMB_BLOCK);
        break;
    case ITEM_LODESTONE:
        place_simple_block(context, BLOCK_LODESTONE);
        break;
    case ITEM_NETHERITE_BLOCK:
        place_simple_block(context, BLOCK_NETHERITE_BLOCK);
        break;
    case ITEM_ANCIENT_DEBRIS:
        place_simple_block(context, BLOCK_ANCIENT_DEBRIS);
        break;
    case ITEM_TARGET:
        place_simple_block(context, BLOCK_TARGET);
        break;
    case ITEM_CRYING_OBSIDIAN:
        place_simple_block(context, BLOCK_CRYING_OBSIDIAN);
        break;
    case ITEM_BLACKSTONE:
        place_simple_block(context, BLOCK_BLACKSTONE);
        break;
    case ITEM_BLACKSTONE_SLAB:
        place_slab(context, BLOCK_BLACKSTONE_SLAB);
        break;
    case ITEM_BLACKSTONE_STAIRS:
        break;
    case ITEM_GILDED_BLACKSTONE:
        place_simple_block(context, BLOCK_GILDED_BLACKSTONE);
        break;
    case ITEM_POLISHED_BLACKSTONE:
        place_simple_block(context, BLOCK_POLISHED_BLACKSTONE);
        break;
    case ITEM_POLISHED_BLACKSTONE_SLAB:
        place_slab(context, BLOCK_POLISHED_BLACKSTONE_SLAB);
        break;
    case ITEM_POLISHED_BLACKSTONE_STAIRS:
        break;
    case ITEM_CHISELED_POLISHED_BLACKSTONE:
        place_simple_block(context, BLOCK_CHISELED_POLISHED_BLACKSTONE);
        break;
    case ITEM_POLISHED_BLACKSTONE_BRICKS:
        place_simple_block(context, BLOCK_POLISHED_BLACKSTONE_BRICKS);
        break;
    case ITEM_POLISHED_BLACKSTONE_BRICK_SLAB:
        place_slab(context, BLOCK_POLISHED_BLACKSTONE_BRICK_SLAB);
        break;
    case ITEM_POLISHED_BLACKSTONE_BRICK_STAIRS:
        break;
    case ITEM_CRACKED_POLISHED_BLACKSTONE_BRICKS:
        place_simple_block(context, BLOCK_CRACKED_POLISHED_BLACKSTONE_BRICKS);
        break;
    case ITEM_RESPAWN_ANCHOR:
        place_simple_block(context, BLOCK_RESPAWN_ANCHOR);
        break;
    default:
        // no use-on action for the remaining item types
        return;
    }

    // @TODO(traks) perhaps don't send these packets if we do everything as the
    // client expects. Although a nice benefit of these packets is that clients
    // can update a desynced block by clicking on it.

    // @TODO(traks) we shouldn't assert here
    net_block_pos changed_pos = clicked_pos;
    assert(player->player.changed_block_count < ARRAY_SIZE(player->player.changed_blocks));
    player->player.changed_blocks[player->player.changed_block_count] = changed_pos;
    player->player.changed_block_count++;

    changed_pos = get_relative_block_pos(clicked_pos, clicked_face);
    assert(player->player.changed_block_count < ARRAY_SIZE(player->player.changed_blocks));
    player->player.changed_blocks[player->player.changed_block_count] = changed_pos;
    player->player.changed_block_count++;
}

mc_ubyte
get_max_stack_size(mc_int item_type) {
    switch (item_type) {
    case ITEM_AIR:
        return 0;

    case ITEM_TURTLE_HELMET:
    case ITEM_FLINT_AND_STEEL:
    case ITEM_BOW:
    case ITEM_WOODEN_SWORD:
    case ITEM_WOODEN_SHOVEL:
    case ITEM_WOODEN_PICKAXE:
    case ITEM_WOODEN_AXE:
    case ITEM_WOODEN_HOE:
    case ITEM_STONE_SWORD:
    case ITEM_STONE_SHOVEL:
    case ITEM_STONE_PICKAXE:
    case ITEM_STONE_AXE:
    case ITEM_STONE_HOE:
    case ITEM_GOLDEN_SWORD:
    case ITEM_GOLDEN_SHOVEL:
    case ITEM_GOLDEN_PICKAXE:
    case ITEM_GOLDEN_AXE:
    case ITEM_GOLDEN_HOE:
    case ITEM_IRON_SWORD:
    case ITEM_IRON_SHOVEL:
    case ITEM_IRON_PICKAXE:
    case ITEM_IRON_AXE:
    case ITEM_IRON_HOE:
    case ITEM_DIAMOND_SWORD:
    case ITEM_DIAMOND_SHOVEL:
    case ITEM_DIAMOND_PICKAXE:
    case ITEM_DIAMOND_AXE:
    case ITEM_DIAMOND_HOE:
    case ITEM_NETHERITE_SWORD:
    case ITEM_NETHERITE_SHOVEL:
    case ITEM_NETHERITE_PICKAXE:
    case ITEM_NETHERITE_AXE:
    case ITEM_NETHERITE_HOE:
    case ITEM_MUSHROOM_STEW:
    case ITEM_LEATHER_HELMET:
    case ITEM_LEATHER_CHESTPLATE:
    case ITEM_LEATHER_LEGGINGS:
    case ITEM_LEATHER_BOOTS:
    case ITEM_CHAINMAIL_HELMET:
    case ITEM_CHAINMAIL_CHESTPLATE:
    case ITEM_CHAINMAIL_LEGGINGS:
    case ITEM_CHAINMAIL_BOOTS:
    case ITEM_IRON_HELMET:
    case ITEM_IRON_CHESTPLATE:
    case ITEM_IRON_LEGGINGS:
    case ITEM_IRON_BOOTS:
    case ITEM_DIAMOND_HELMET:
    case ITEM_DIAMOND_CHESTPLATE:
    case ITEM_DIAMOND_LEGGINGS:
    case ITEM_DIAMOND_BOOTS:
    case ITEM_GOLDEN_HELMET:
    case ITEM_GOLDEN_CHESTPLATE:
    case ITEM_GOLDEN_LEGGINGS:
    case ITEM_GOLDEN_BOOTS:
    case ITEM_NETHERITE_HELMET:
    case ITEM_NETHERITE_CHESTPLATE:
    case ITEM_NETHERITE_LEGGINGS:
    case ITEM_NETHERITE_BOOTS:
    case ITEM_WATER_BUCKET:
    case ITEM_LAVA_BUCKET:
    case ITEM_MINECART:
    case ITEM_SADDLE:
    case ITEM_OAK_BOAT:
    case ITEM_MILK_BUCKET:
    case ITEM_PUFFERFISH_BUCKET:
    case ITEM_SALMON_BUCKET:
    case ITEM_COD_BUCKET:
    case ITEM_TROPICAL_FISH_BUCKET:
    case ITEM_CHEST_MINECART:
    case ITEM_FURNACE_MINECART:
    case ITEM_FISHING_ROD:
    case ITEM_CAKE:
    case ITEM_WHITE_BED:
    case ITEM_ORANGE_BED:
    case ITEM_MAGENTA_BED:
    case ITEM_LIGHT_BLUE_BED:
    case ITEM_YELLOW_BED:
    case ITEM_LIME_BED:
    case ITEM_PINK_BED:
    case ITEM_GRAY_BED:
    case ITEM_LIGHT_GRAY_BED:
    case ITEM_CYAN_BED:
    case ITEM_PURPLE_BED:
    case ITEM_BLUE_BED:
    case ITEM_BROWN_BED:
    case ITEM_GREEN_BED:
    case ITEM_RED_BED:
    case ITEM_BLACK_BED:
    case ITEM_SHEARS:
    case ITEM_POTION:
    case ITEM_WRITABLE_BOOK:
    case ITEM_CARROT_ON_A_STICK:
    case ITEM_WARPED_FUNGUS_ON_A_STICK:
    case ITEM_ENCHANTED_BOOK:
    case ITEM_TNT_MINECART:
    case ITEM_HOPPER_MINECART:
    case ITEM_RABBIT_STEW:
    case ITEM_IRON_HORSE_ARMOR:
    case ITEM_GOLDEN_HORSE_ARMOR:
    case ITEM_DIAMOND_HORSE_ARMOR:
    case ITEM_LEATHER_HORSE_ARMOR:
    case ITEM_COMMAND_BLOCK_MINECART:
    case ITEM_BEETROOT_SOUP:
    case ITEM_SPLASH_POTION:
    case ITEM_LINGERING_POTION:
    case ITEM_SHIELD:
    case ITEM_ELYTRA:
    case ITEM_SPRUCE_BOAT:
    case ITEM_BIRCH_BOAT:
    case ITEM_JUNGLE_BOAT:
    case ITEM_ACACIA_BOAT:
    case ITEM_DARK_OAK_BOAT:
    case ITEM_TOTEM_OF_UNDYING:
    case ITEM_KNOWLEDGE_BOOK:
    case ITEM_DEBUG_STICK:
    case ITEM_MUSIC_DISC_13:
    case ITEM_MUSIC_DISC_CAT:
    case ITEM_MUSIC_DISC_BLOCKS:
    case ITEM_MUSIC_DISC_CHIRP:
    case ITEM_MUSIC_DISC_FAR:
    case ITEM_MUSIC_DISC_MALL:
    case ITEM_MUSIC_DISC_MELLOHI:
    case ITEM_MUSIC_DISC_STAL:
    case ITEM_MUSIC_DISC_STRAD:
    case ITEM_MUSIC_DISC_WARD:
    case ITEM_MUSIC_DISC_11:
    case ITEM_MUSIC_DISC_WAIT:
    case ITEM_MUSIC_DISC_PIGSTEP:
    case ITEM_TRIDENT:
    case ITEM_CROSSBOW:
    case ITEM_SUSPICIOUS_STEW:
    case ITEM_FLOWER_BANNER_PATTERN:
    case ITEM_CREEPER_BANNER_PATTERN:
    case ITEM_SKULL_BANNER_PATTERN:
    case ITEM_MOJANG_BANNER_PATTERN:
    case ITEM_GLOBE_BANNER_PATTERN:
    case ITEM_PIGLIN_BANNER_PATTERN:
        return 1;

    case ITEM_OAK_SIGN:
    case ITEM_SPRUCE_SIGN:
    case ITEM_BIRCH_SIGN:
    case ITEM_JUNGLE_SIGN:
    case ITEM_ACACIA_SIGN:
    case ITEM_DARK_OAK_SIGN:
    case ITEM_CRIMSON_SIGN:
    case ITEM_WARPED_SIGN:
    case ITEM_BUCKET:
    case ITEM_SNOWBALL:
    case ITEM_EGG:
    case ITEM_ENDER_PEARL:
    case ITEM_WRITTEN_BOOK:
    case ITEM_ARMOR_STAND:
    case ITEM_WHITE_BANNER:
    case ITEM_ORANGE_BANNER:
    case ITEM_MAGENTA_BANNER:
    case ITEM_LIGHT_BLUE_BANNER:
    case ITEM_YELLOW_BANNER:
    case ITEM_LIME_BANNER:
    case ITEM_PINK_BANNER:
    case ITEM_GRAY_BANNER:
    case ITEM_LIGHT_GRAY_BANNER:
    case ITEM_CYAN_BANNER:
    case ITEM_PURPLE_BANNER:
    case ITEM_BLUE_BANNER:
    case ITEM_BROWN_BANNER:
    case ITEM_GREEN_BANNER:
    case ITEM_RED_BANNER:
    case ITEM_BLACK_BANNER:
        return 16;

    default:
        return 64;
    }
}
