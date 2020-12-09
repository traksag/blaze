#include <math.h>
#include <string.h>
#include "shared.h"

#define PLACE_REPLACING ((unsigned) (1 << 0))
#define PLACE_CAN_PLACE ((unsigned) (1 << 1))

typedef struct {
    net_block_pos pos;
    mc_ushort cur_state;
    unsigned char flags;
    mc_int cur_type;
} place_target;

typedef struct {
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

    // @TODO(traks) revisit which blocks can be replaced by which blocks. E.g.
    // tall flower can't be replaced, slabs can be replaced by slabs of the same
    // type to form a double slab, etc.

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
determine_place_target(net_block_pos clicked_pos,
        mc_int clicked_face, mc_int place_type) {
    place_target res = {0};
    net_block_pos target_pos = clicked_pos;
    mc_ushort cur_state = try_get_block_state(target_pos);
    mc_int cur_type = serv->block_type_by_state[cur_state];
    int replacing = PLACE_REPLACING;

    if (!can_replace(place_type, cur_type)) {
        target_pos = get_relative_block_pos(target_pos, clicked_face);
        cur_state = try_get_block_state(target_pos);
        cur_type = serv->block_type_by_state[cur_state];
        replacing = 0;

        if (!can_replace(place_type, cur_type)) {
            return res;
        }
    }

    res = (place_target) {
        .pos = target_pos,
        .cur_state = cur_state,
        .cur_type = cur_type,
        .flags = PLACE_CAN_PLACE | replacing,
    };
    return res;
}

static void
place_simple_block(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(place_type);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_snowy_grassy_block(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    mc_ushort state_above = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_POS_Y));
    mc_int type_above = serv->block_type_by_state[state_above];

    if (type_above == BLOCK_SNOW_BLOCK || type_above == BLOCK_SNOW) {
        place_info.snowy = 1;
    } else {
        place_info.snowy = 0;
    }

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_plant(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    mc_int type_below = serv->block_type_by_state[state_below];

    if (!can_plant_survive_on(type_below)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(place_type);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_lily_pad(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    if (target.cur_type == BLOCK_LAVA
            || get_water_level(target.cur_state) != FLUID_LEVEL_NONE) {
        return;
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    if (!can_lily_pad_survive_on(state_below)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(place_type);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_dead_bush(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    mc_int type_below = serv->block_type_by_state[state_below];

    if (!can_dead_bush_survive_on(type_below)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(place_type);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_wither_rose(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    mc_int type_below = serv->block_type_by_state[state_below];

    if (!can_wither_rose_survive_on(type_below)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(place_type);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_nether_plant(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    mc_int type_below = serv->block_type_by_state[state_below];

    if (!can_nether_plant_survive_on(type_below)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(place_type);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_pressure_plate(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    if (!can_pressure_plate_survive_on(state_below)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(place_type);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
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
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    set_axis_by_clicked_face(&place_info, context.clicked_face);

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_chain(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    set_axis_by_clicked_face(&place_info, context.clicked_face);
    place_info.waterlogged = is_water_source(target.cur_state);

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_slab(place_context context, mc_int place_type) {
    net_block_pos target_pos = context.clicked_pos;
    mc_ushort cur_state = try_get_block_state(target_pos);
    block_state_info cur_info = describe_block_state(cur_state);
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
        cur_state = try_get_block_state(target_pos);
        cur_info = describe_block_state(cur_state);
        cur_type = cur_info.block_type;

        if (cur_type != place_type && !can_replace(place_type, cur_type)) {
            return;
        }
    }

    block_state_info place_info = describe_default_block_state(place_type);

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

        place_info.waterlogged = is_water_source(cur_state);
    }

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target_pos, place_state);
    propagate_block_updates_after_change(target_pos, context.scratch_arena);
}

static void
place_sea_pickle(place_context context, mc_int place_type) {
    net_block_pos target_pos = context.clicked_pos;
    mc_ushort cur_state = try_get_block_state(target_pos);
    block_state_info cur_info = describe_block_state(cur_state);
    mc_int cur_type = cur_info.block_type;

    int replace_cur = 0;
    if (cur_type == place_type) {
        if (cur_info.pickles < 4) {
            replace_cur = 1;
        }
    } else {
        replace_cur = can_replace(place_type, cur_type);
    }

    if (!replace_cur) {
        target_pos = get_relative_block_pos(target_pos, context.clicked_face);
        cur_state = try_get_block_state(target_pos);
        cur_info = describe_block_state(cur_state);
        cur_type = cur_info.block_type;

        if (cur_type == place_type) {
            if (cur_info.pickles >= 4) {
                return;
            }
        } else if (!can_replace(place_type, cur_type)) {
            return;
        }
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target_pos, DIRECTION_NEG_Y));
    mc_int type_below = serv->block_type_by_state[state_below];
    if (!can_sea_pickle_survive_on(state_below)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);

    if (place_type == cur_type) {
        place_info.pickles = cur_info.pickles + 1;
    }
    place_info.waterlogged = is_water_source(cur_state);

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target_pos, place_state);
    propagate_block_updates_after_change(target_pos, context.scratch_arena);
}

static void
place_snow(place_context context, mc_int place_type) {
    net_block_pos target_pos = context.clicked_pos;
    mc_ushort cur_state = try_get_block_state(target_pos);
    block_state_info cur_info = describe_block_state(cur_state);
    mc_int cur_type = cur_info.block_type;

    int replace_cur = 0;
    if (cur_type == place_type) {
        if (cur_info.layers < 8 && context.clicked_face == DIRECTION_POS_Y) {
            replace_cur = 1;
        }
    } else {
        replace_cur = can_replace(place_type, cur_type);
    }

    if (!replace_cur) {
        target_pos = get_relative_block_pos(target_pos, context.clicked_face);
        cur_state = try_get_block_state(target_pos);
        cur_info = describe_block_state(cur_state);
        cur_type = cur_info.block_type;

        if (cur_type == place_type) {
            if (cur_info.layers >= 8) {
                return;
            }
        } else if (!can_replace(place_type, cur_type)) {
            return;
        }
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target_pos, DIRECTION_NEG_Y));
    mc_int type_below = serv->block_type_by_state[state_below];
    if (!can_snow_survive_on(state_below)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    if (place_type == cur_type) {
        place_info.layers = cur_info.layers + 1;
    }

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target_pos, place_state);
    propagate_block_updates_after_change(target_pos, context.scratch_arena);
}

static void
place_leaves(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    // @TODO(traks) calculate distance to nearest log block and modify block
    // state with that information
    mc_ushort place_state = serv->block_properties_table[place_type].base_state;
    place_state += 0; // persistent = true

    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_horizontal_facing(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = get_opposite_direction(get_player_facing(context.player));

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_end_portal_frame(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = get_opposite_direction(get_player_facing(context.player));
    place_info.eye = 0;

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_trapdoor(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    if (target.flags & PLACE_REPLACING) {
        place_info.half = context.clicked_face == DIRECTION_POS_Y ?
                BLOCK_HALF_BOTTOM : BLOCK_HALF_TOP;
        place_info.horizontal_facing = get_opposite_direction(
                get_player_facing(context.player));
    } else if (context.clicked_face == DIRECTION_POS_Y) {
        place_info.half = BLOCK_HALF_BOTTOM;
        place_info.horizontal_facing = get_opposite_direction(
                get_player_facing(context.player));
    } else if (context.clicked_face == DIRECTION_NEG_Y) {
        place_info.half = BLOCK_HALF_TOP;
        place_info.horizontal_facing = get_opposite_direction(
                get_player_facing(context.player));
    } else {
        place_info.half = context.click_offset_y > 0.5f ?
                BLOCK_HALF_TOP : BLOCK_HALF_BOTTOM;
        place_info.horizontal_facing = context.clicked_face;
    }
    place_info.waterlogged = is_water_source(target.cur_state);

    // @TODO(traks) open trapdoor and set powered if necessary

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_fence_gate(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    int player_facing = get_player_facing(context.player);
    place_info.horizontal_facing = player_facing;
    if (player_facing == DIRECTION_POS_X || player_facing == DIRECTION_NEG_X) {
        int neighbour_state_pos = try_get_block_state(
                get_relative_block_pos(target.pos, DIRECTION_POS_Z));
        int neighbour_state_neg = try_get_block_state(
                get_relative_block_pos(target.pos, DIRECTION_NEG_Z));
        if (is_wall(serv->block_type_by_state[neighbour_state_pos])
                || is_wall(serv->block_type_by_state[neighbour_state_neg])) {
            place_info.in_wall = 1;
        }
    } else {
        // facing along z axis
        int neighbour_state_pos = try_get_block_state(
                get_relative_block_pos(target.pos, DIRECTION_POS_X));
        int neighbour_state_neg = try_get_block_state(
                get_relative_block_pos(target.pos, DIRECTION_NEG_X));
        if (is_wall(serv->block_type_by_state[neighbour_state_pos])
                || is_wall(serv->block_type_by_state[neighbour_state_neg])) {
            place_info.in_wall = 1;
        }
    }

    // @TODO(traks) open fence gate and set powered if necessary

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_crop(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    mc_int type_below = serv->block_type_by_state[state_below];

    // @TODO(traks) light level also needs to be sufficient
    switch (type_below) {
    case BLOCK_FARMLAND:
        break;
    default:
        return;
    }

    mc_ushort place_state = get_default_block_state(place_type);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_nether_wart(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    mc_int type_below = serv->block_type_by_state[state_below];

    switch (type_below) {
    case BLOCK_SOUL_SAND:
        break;
    default:
        return;
    }

    mc_ushort place_state = get_default_block_state(place_type);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_carpet(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(place_type);
    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    mc_int type_below = serv->block_type_by_state[state_below];

    if (!can_carpet_survive_on(type_below)) {
        return;
    }

    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_mushroom_block(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);

    int directions[] = {0, 1, 2, 3, 4, 5};

    for (int i = 0; i < 6; i++) {
        net_block_pos pos = get_relative_block_pos(target.pos, directions[i]);
        mc_ushort state = try_get_block_state(pos);
        mc_int type = serv->block_type_by_state[state];

        // connect to neighbouring mushroom blocks of the same type by setting
        // the six facing properties to true if connected
        if (type == place_type) {
            place_info.values[BLOCK_PROPERTY_NEG_Y + directions[i]] = 0;
        } else {
            place_info.values[BLOCK_PROPERTY_NEG_Y + directions[i]] = 1;
        }
    }

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_end_rod(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);

    int opposite_face = get_opposite_direction(context.clicked_face);

    net_block_pos opposite_pos = get_relative_block_pos(target.pos, opposite_face);
    mc_ushort opposite_state = try_get_block_state(opposite_pos);
    block_state_info opposite_info = describe_block_state(opposite_state);

    if (opposite_info.block_type == place_type) {
        if (opposite_info.facing == context.clicked_face) {
            place_info.facing = opposite_face;
        } else {
            place_info.facing = context.clicked_face;
        }
    } else {
        place_info.facing = context.clicked_face;
    }

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_sugar_cane(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }
    if (!can_sugar_cane_survive_at(target.pos)) {
        return;
    }

    mc_ushort place_state = get_default_block_state(place_type);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_dead_coral(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    support_model support = get_support_model(state_below);
    if (!(support.full_face_flags & (1 << DIRECTION_POS_Y))) {
        // face below is not sturdy
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.waterlogged = is_full_water(target.cur_state);

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
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
    if (target.flags & PLACE_REPLACING) {
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
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, base_place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
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
        mc_ushort wall_state = try_get_block_state(attach_pos);
        int wall_face = get_opposite_direction(dir);
        support_model support = get_support_model(wall_state);

        if (dir == DIRECTION_NEG_Y) {
            seen_neg_y = 1;
            if (support.full_face_flags & (1 << wall_face)) {
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
            if (support.full_face_flags & (1 << wall_face)) {
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
    block_state_info place_info = describe_default_block_state(place_type);
    place_info.waterlogged = is_full_water(target.cur_state);
    place_info.horizontal_facing = get_opposite_direction(selected_dir);

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_torch(place_context context, mc_int base_place_type,
        mc_int wall_place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, base_place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
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
        mc_ushort wall_state = try_get_block_state(attach_pos);
        int wall_face = get_opposite_direction(dir);
        support_model support = get_support_model(wall_state);

        if (dir == DIRECTION_NEG_Y) {
            seen_neg_y = 1;
            if (support.pole_face_flags & (1 << wall_face)) {
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
            if (support.full_face_flags & (1 << wall_face)) {
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
    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = get_opposite_direction(selected_dir);

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_ladder(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    direction_list list = get_attach_directions_by_preference(context, target);
    int selected_dir = -1;

    for (int i = 0; i < 6; i++) {
        int dir = list.directions[i];
        if (dir == DIRECTION_POS_Y || dir == DIRECTION_NEG_Y) {
            continue;
        }

        net_block_pos attach_pos = get_relative_block_pos(target.pos, dir);
        mc_ushort wall_state = try_get_block_state(attach_pos);
        support_model support = get_support_model(wall_state);
        int wall_face = get_opposite_direction(dir);

        if (support.full_face_flags & (1 << wall_face)) {
            // wall face is sturdy
            selected_dir = dir;
            break;
        }
    }
    if (selected_dir == -1) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = get_opposite_direction(selected_dir);
    place_info.waterlogged = is_water_source(target.cur_state);

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_door(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }
    if (target.pos.y >= MAX_WORLD_Y) {
        return;
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    support_model support = get_support_model(state_below);
    if (!(support.full_face_flags & (1 << DIRECTION_POS_Y))) {
        // face below is not sturdy
        return;
    }

    mc_ushort state_above = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_POS_Y));
    mc_int type_above = serv->block_type_by_state[state_above];

    if (!can_replace(place_type, type_above)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = get_player_facing(context.player);
    place_info.double_block_half = DOUBLE_BLOCK_HALF_LOWER;
    // @TODO(traks) determine side of the hinge
    place_info.door_hinge = DOOR_HINGE_LEFT;
    // @TODO(traks) placed opened door if powered
    place_info.open = 0;
    place_info.powered = 0;

    // place lower half
    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);

    // place upper half
    place_info.double_block_half = DOUBLE_BLOCK_HALF_UPPER;
    place_state = make_block_state(&place_info);
    try_set_block_state(get_relative_block_pos(target.pos, DIRECTION_POS_Y), place_state);

    // @TODO(traks) process updates for both halves in one loop
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
    propagate_block_updates_after_change(get_relative_block_pos(target.pos, DIRECTION_POS_Y),
            context.scratch_arena);
}

static void
place_bed(place_context context, mc_int place_type, int dye_colour) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    int facing = get_player_facing(context.player);
    net_block_pos head_pos = get_relative_block_pos(target.pos, facing);
    mc_ushort neighbour_state = try_get_block_state(head_pos);
    mc_int neighbour_type = serv->block_type_by_state[neighbour_state];

    if (!can_replace(place_type, neighbour_type)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = facing;

    // place foot part
    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);

    // place head part
    place_info.bed_part = BED_PART_HEAD;
    place_state = make_block_state(&place_info);
    try_set_block_state(head_pos, place_state);

    // @TODO(traks) flesh out all this block entity business.
    block_entity_base * block_entity = try_get_block_entity(target.pos);
    if (block_entity != NULL) {
        block_entity->flags = BLOCK_ENTITY_IN_USE;
        block_entity->type = BLOCK_ENTITY_BED;
        block_entity->bed.dye_colour = dye_colour;
    }

    block_entity = try_get_block_entity(head_pos);
    if (block_entity != NULL) {
        block_entity->flags = BLOCK_ENTITY_IN_USE;
        block_entity->type = BLOCK_ENTITY_BED;
        block_entity->bed.dye_colour = dye_colour;
    }

    // @TODO(traks) process updates for both parts in one loop
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
    propagate_block_updates_after_change(head_pos, context.scratch_arena);
}

static void
place_bamboo(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    if (get_water_level(target.cur_state) != FLUID_LEVEL_NONE
            || target.cur_type == BLOCK_LAVA) {
        return;
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    mc_int type_below = serv->block_type_by_state[state_below];

    if (!is_bamboo_plantable_on(type_below)) {
        return;
    }

    mc_ushort place_state;

    switch (type_below) {
    case BLOCK_BAMBOO_SAPLING:
        place_state = get_default_block_state(place_type);
        break;
    case BLOCK_BAMBOO:
        place_state = state_below;
        break;
    default:
        place_state = get_default_block_state(BLOCK_BAMBOO_SAPLING);
    }

    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_stairs(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = get_player_facing(context.player);
    if (context.clicked_face == DIRECTION_POS_Y || context.click_offset_y <= 0.5f) {
        place_info.half = BLOCK_HALF_BOTTOM;
    } else {
        place_info.half = BLOCK_HALF_TOP;
    }
    place_info.waterlogged = is_water_source(target.cur_state);
    update_stairs_shape(target.pos, &place_info);

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_fence(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.waterlogged = is_water_source(target.cur_state);
    int neighbour_directions[] = {DIRECTION_NEG_Z, DIRECTION_POS_Z, DIRECTION_NEG_X, DIRECTION_POS_X};
    for (int i = 0; i < 4; i++) {
        int face = neighbour_directions[i];
        update_fence_shape(target.pos, &place_info, face);
    }

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_pane(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.waterlogged = is_water_source(target.cur_state);
    int neighbour_directions[] = {DIRECTION_NEG_Z, DIRECTION_POS_Z, DIRECTION_NEG_X, DIRECTION_POS_X};
    for (int i = 0; i < 4; i++) {
        int face = neighbour_directions[i];
        update_pane_shape(target.pos, &place_info, face);
    }

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_wall(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.waterlogged = is_water_source(target.cur_state);
    int neighbour_directions[] = {DIRECTION_NEG_Z, DIRECTION_POS_Z, DIRECTION_NEG_X, DIRECTION_POS_X, DIRECTION_POS_Y};
    for (int i = 0; i < 4; i++) {
        int face = neighbour_directions[i];
        update_wall_shape(target.pos, &place_info, face);
    }

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_rail(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    // @TODO(traks) check if rail can survive on block below and place rail in
    // the correct state (ascending and turned).
    block_state_info place_info = describe_default_block_state(place_type);
    int player_facing = get_player_facing(context.player);
    place_info.rail_shape = player_facing == DIRECTION_NEG_X
            || player_facing == DIRECTION_POS_X ? RAIL_SHAPE_X : RAIL_SHAPE_Z;

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_lever_or_button(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    direction_list list = get_attach_directions_by_preference(context, target);
    int selected_dir = -1;

    for (int i = 0; i < 6; i++) {
        int dir = list.directions[i];
        net_block_pos attach_pos = get_relative_block_pos(target.pos, dir);
        mc_ushort wall_state = try_get_block_state(attach_pos);
        int wall_face = get_opposite_direction(dir);
        support_model support = get_support_model(wall_state);

        if (support.full_face_flags & (1 << wall_face)) {
            // wall face is sturdy
            selected_dir = dir;
            break;
        }
    }
    if (selected_dir == -1) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    switch (selected_dir) {
    case DIRECTION_POS_Y:
        place_info.attach_face = ATTACH_FACE_CEILING;
        place_info.horizontal_facing = get_player_facing(context.player);
        break;
    case DIRECTION_NEG_Y:
        place_info.attach_face = ATTACH_FACE_FLOOR;
        place_info.horizontal_facing = get_player_facing(context.player);
        break;
    default:
        // attach to horizontal wall
        place_info.attach_face = ATTACH_FACE_WALL;
        place_info.horizontal_facing = get_opposite_direction(selected_dir);
    }

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_grindstone(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    direction_list list = get_attach_directions_by_preference(context, target);
    int selected_dir = list.directions[0];

    block_state_info place_info = describe_default_block_state(place_type);
    switch (selected_dir) {
    case DIRECTION_POS_Y:
        place_info.attach_face = ATTACH_FACE_CEILING;
        place_info.horizontal_facing = get_player_facing(context.player);
        break;
    case DIRECTION_NEG_Y:
        place_info.attach_face = ATTACH_FACE_FLOOR;
        place_info.horizontal_facing = get_player_facing(context.player);
        break;
    default:
        // attach to horizontal wall
        place_info.attach_face = ATTACH_FACE_WALL;
        place_info.horizontal_facing = get_opposite_direction(selected_dir);
    }

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

static void
place_redstone_wire(place_context context, mc_int place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    mc_ushort state_below = try_get_block_state(
            get_relative_block_pos(target.pos, DIRECTION_NEG_Y));
    mc_int type_below = serv->block_type_by_state[state_below];

    if (!can_redstone_wire_survive_on(state_below)) {
        return;
    }

    // @TODO(traks) finish this function

    block_state_info place_info = describe_default_block_state(place_type);

    int neighbour_directions[] = {DIRECTION_NEG_Z, DIRECTION_POS_Z, DIRECTION_NEG_X, DIRECTION_POS_X};
    for (int i = 0; i < 4; i++) {
        int face = neighbour_directions[i];
        mc_ubyte * field;
        switch (face) {
        case DIRECTION_POS_X: field = &place_info.redstone_pos_x;
        case DIRECTION_NEG_Z: field = &place_info.redstone_neg_z;
        case DIRECTION_POS_Z: field = &place_info.redstone_pos_z;
        case DIRECTION_NEG_X: field = &place_info.redstone_neg_x;
        }

        mc_ushort neighbour_state = try_get_block_state(
                get_relative_block_pos(target.pos, face));
        mc_int neighbour_type = serv->block_type_by_state[neighbour_state];

        if (neighbour_type == BLOCK_REDSTONE_WIRE) {
            *field = REDSTONE_SIDE_SIDE;
        } else {
            *field = REDSTONE_SIDE_NONE;
        }
    }

    mc_ushort place_state = make_block_state(&place_info);
    try_set_block_state(target.pos, place_state);
    propagate_block_updates_after_change(target.pos, context.scratch_arena);
}

void
process_use_item_on_packet(entity_base * player,
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
        int used_block = use_block(player,
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
        place_sea_pickle(context, BLOCK_SEA_PICKLE);
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
        place_bamboo(context, BLOCK_BAMBOO);
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
        place_stairs(context, BLOCK_PURPUR_STAIRS);
        break;
    case ITEM_SPAWNER:
        break;
    case ITEM_OAK_STAIRS:
        place_stairs(context, BLOCK_OAK_STAIRS);
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
        // @TODO(traks) need to be able to determine if material of block above
        // is solid or not, to implement farmland
        break;
    case ITEM_FURNACE:
        break;
    case ITEM_LADDER:
        place_ladder(context, BLOCK_LADDER);
        break;
    case ITEM_RAIL:
        place_rail(context, BLOCK_RAIL);
        break;
    case ITEM_COBBLESTONE_STAIRS:
        place_stairs(context, BLOCK_COBBLESTONE_STAIRS);
        break;
    case ITEM_LEVER:
        place_lever_or_button(context, BLOCK_LEVER);
        break;
    case ITEM_STONE_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_STONE_PRESSURE_PLATE);
        break;
    case ITEM_OAK_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_OAK_PRESSURE_PLATE);
        break;
    case ITEM_SPRUCE_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_SPRUCE_PRESSURE_PLATE);
        break;
    case ITEM_BIRCH_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_BIRCH_PRESSURE_PLATE);
        break;
    case ITEM_JUNGLE_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_JUNGLE_PRESSURE_PLATE);
        break;
    case ITEM_ACACIA_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_ACACIA_PRESSURE_PLATE);
        break;
    case ITEM_DARK_OAK_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_DARK_OAK_PRESSURE_PLATE);
        break;
    case ITEM_CRIMSON_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_CRIMSON_PRESSURE_PLATE);
        break;
    case ITEM_WARPED_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_WARPED_PRESSURE_PLATE);
        break;
    case ITEM_POLISHED_BLACKSTONE_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_POLISHED_BLACKSTONE_PRESSURE_PLATE);
        break;
    case ITEM_REDSTONE_ORE:
        place_simple_block(context, BLOCK_REDSTONE_ORE);
        break;
    case ITEM_REDSTONE_TORCH:
        place_torch(context, BLOCK_REDSTONE_TORCH, BLOCK_REDSTONE_WALL_TORCH);
        break;
    case ITEM_SNOW:
        place_snow(context, BLOCK_SNOW);
        break;
    case ITEM_ICE:
        place_simple_block(context, BLOCK_ICE);
        break;
    case ITEM_SNOW_BLOCK:
        place_simple_block(context, BLOCK_SNOW_BLOCK);
        break;
    case ITEM_CACTUS:
        // @TODO(traks) need to determine solid materials to implement this
        break;
    case ITEM_CLAY:
        place_simple_block(context, BLOCK_CLAY);
        break;
    case ITEM_JUKEBOX:
        break;
    case ITEM_OAK_FENCE:
        place_fence(context, BLOCK_OAK_FENCE);
        break;
    case ITEM_SPRUCE_FENCE:
        place_fence(context, BLOCK_SPRUCE_FENCE);
        break;
    case ITEM_BIRCH_FENCE:
        place_fence(context, BLOCK_BIRCH_FENCE);
        break;
    case ITEM_JUNGLE_FENCE:
        place_fence(context, BLOCK_JUNGLE_FENCE);
        break;
    case ITEM_ACACIA_FENCE:
        place_fence(context, BLOCK_ACACIA_FENCE);
        break;
    case ITEM_DARK_OAK_FENCE:
        place_fence(context, BLOCK_DARK_OAK_FENCE);
        break;
    case ITEM_CRIMSON_FENCE:
        place_fence(context, BLOCK_CRIMSON_FENCE);
        break;
    case ITEM_WARPED_FENCE:
        place_fence(context, BLOCK_WARPED_FENCE);
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
        place_trapdoor(context, BLOCK_OAK_TRAPDOOR);
        break;
    case ITEM_SPRUCE_TRAPDOOR:
        place_trapdoor(context, BLOCK_SPRUCE_TRAPDOOR);
        break;
    case ITEM_BIRCH_TRAPDOOR:
        place_trapdoor(context, BLOCK_BIRCH_TRAPDOOR);
        break;
    case ITEM_JUNGLE_TRAPDOOR:
        place_trapdoor(context, BLOCK_JUNGLE_TRAPDOOR);
        break;
    case ITEM_ACACIA_TRAPDOOR:
        place_trapdoor(context, BLOCK_ACACIA_TRAPDOOR);
        break;
    case ITEM_DARK_OAK_TRAPDOOR:
        place_trapdoor(context, BLOCK_DARK_OAK_TRAPDOOR);
        break;
    case ITEM_CRIMSON_TRAPDOOR:
        place_trapdoor(context, BLOCK_CRIMSON_TRAPDOOR);
        break;
    case ITEM_WARPED_TRAPDOOR:
        place_trapdoor(context, BLOCK_WARPED_TRAPDOOR);
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
        place_pane(context, BLOCK_IRON_BARS);
        break;
    case ITEM_CHAIN:
        place_chain(context, BLOCK_CHAIN);
        break;
    case ITEM_GLASS_PANE:
        place_pane(context, BLOCK_GLASS_PANE);
        break;
    case ITEM_MELON:
        place_simple_block(context, BLOCK_MELON);
        break;
    case ITEM_VINE:
        break;
    case ITEM_OAK_FENCE_GATE:
        place_fence_gate(context, BLOCK_OAK_FENCE_GATE);
        break;
    case ITEM_SPRUCE_FENCE_GATE:
        place_fence_gate(context, BLOCK_SPRUCE_FENCE_GATE);
        break;
    case ITEM_BIRCH_FENCE_GATE:
        place_fence_gate(context, BLOCK_BIRCH_FENCE_GATE);
        break;
    case ITEM_JUNGLE_FENCE_GATE:
        place_fence_gate(context, BLOCK_JUNGLE_FENCE_GATE);
        break;
    case ITEM_ACACIA_FENCE_GATE:
        place_fence_gate(context, BLOCK_ACACIA_FENCE_GATE);
        break;
    case ITEM_DARK_OAK_FENCE_GATE:
        place_fence_gate(context, BLOCK_DARK_OAK_FENCE_GATE);
        break;
    case ITEM_CRIMSON_FENCE_GATE:
        place_fence_gate(context, BLOCK_CRIMSON_FENCE_GATE);
        break;
    case ITEM_WARPED_FENCE_GATE:
        place_fence_gate(context, BLOCK_WARPED_FENCE_GATE);
        break;
    case ITEM_BRICK_STAIRS:
        place_stairs(context, BLOCK_BRICK_STAIRS);
        break;
    case ITEM_STONE_BRICK_STAIRS:
        place_stairs(context, BLOCK_STONE_BRICK_STAIRS);
        break;
    case ITEM_MYCELIUM:
        place_snowy_grassy_block(context, BLOCK_MYCELIUM);
        break;
    case ITEM_LILY_PAD:
        place_lily_pad(context, BLOCK_LILY_PAD);
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
        place_fence(context, BLOCK_NETHER_BRICK_FENCE);
        break;
    case ITEM_NETHER_BRICK_STAIRS:
        place_stairs(context, BLOCK_NETHER_BRICK_STAIRS);
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
        place_stairs(context, BLOCK_SANDSTONE_STAIRS);
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
        place_stairs(context, BLOCK_SPRUCE_STAIRS);
        break;
    case ITEM_BIRCH_STAIRS:
        place_stairs(context, BLOCK_BIRCH_STAIRS);
        break;
    case ITEM_JUNGLE_STAIRS:
        place_stairs(context, BLOCK_JUNGLE_STAIRS);
        break;
    case ITEM_CRIMSON_STAIRS:
        place_stairs(context, BLOCK_CRIMSON_STAIRS);
        break;
    case ITEM_WARPED_STAIRS:
        place_stairs(context, BLOCK_WARPED_STAIRS);
        break;
    case ITEM_COMMAND_BLOCK:
        break;
    case ITEM_BEACON:
        break;
    case ITEM_COBBLESTONE_WALL:
        place_wall(context, BLOCK_COBBLESTONE_WALL);
        break;
    case ITEM_MOSSY_COBBLESTONE_WALL:
        place_wall(context, BLOCK_MOSSY_COBBLESTONE_WALL);
        break;
    case ITEM_BRICK_WALL:
        place_wall(context, BLOCK_BRICK_WALL);
        break;
    case ITEM_PRISMARINE_WALL:
        place_wall(context, BLOCK_PRISMARINE_WALL);
        break;
    case ITEM_RED_SANDSTONE_WALL:
        place_wall(context, BLOCK_RED_SANDSTONE_WALL);
        break;
    case ITEM_MOSSY_STONE_BRICK_WALL:
        place_wall(context, BLOCK_MOSSY_STONE_BRICK_WALL);
        break;
    case ITEM_GRANITE_WALL:
        place_wall(context, BLOCK_GRANITE_WALL);
        break;
    case ITEM_STONE_BRICK_WALL:
        place_wall(context, BLOCK_STONE_BRICK_WALL);
        break;
    case ITEM_NETHER_BRICK_WALL:
        place_wall(context, BLOCK_NETHER_BRICK_WALL);
        break;
    case ITEM_ANDESITE_WALL:
        place_wall(context, BLOCK_ANDESITE_WALL);
        break;
    case ITEM_RED_NETHER_BRICK_WALL:
        place_wall(context, BLOCK_RED_NETHER_BRICK_WALL);
        break;
    case ITEM_SANDSTONE_WALL:
        place_wall(context, BLOCK_SANDSTONE_WALL);
        break;
    case ITEM_END_STONE_BRICK_WALL:
        place_wall(context, BLOCK_END_STONE_BRICK_WALL);
        break;
    case ITEM_DIORITE_WALL:
        place_wall(context, BLOCK_DIORITE_WALL);
        break;
    case ITEM_BLACKSTONE_WALL:
        place_wall(context, BLOCK_BLACKSTONE_WALL);
        break;
    case ITEM_POLISHED_BLACKSTONE_WALL:
        place_wall(context, BLOCK_POLISHED_BLACKSTONE_WALL);
        break;
    case ITEM_POLISHED_BLACKSTONE_BRICK_WALL:
        place_wall(context, BLOCK_POLISHED_BLACKSTONE_BRICK_WALL);
        break;
    case ITEM_STONE_BUTTON:
        place_lever_or_button(context, BLOCK_STONE_BUTTON);
        break;
    case ITEM_OAK_BUTTON:
        place_lever_or_button(context, BLOCK_OAK_BUTTON);
        break;
    case ITEM_SPRUCE_BUTTON:
        place_lever_or_button(context, BLOCK_SPRUCE_BUTTON);
        break;
    case ITEM_BIRCH_BUTTON:
        place_lever_or_button(context, BLOCK_BIRCH_BUTTON);
        break;
    case ITEM_JUNGLE_BUTTON:
        place_lever_or_button(context, BLOCK_JUNGLE_BUTTON);
        break;
    case ITEM_ACACIA_BUTTON:
        place_lever_or_button(context, BLOCK_ACACIA_BUTTON);
        break;
    case ITEM_DARK_OAK_BUTTON:
        place_lever_or_button(context, BLOCK_DARK_OAK_BUTTON);
        break;
    case ITEM_CRIMSON_BUTTON:
        place_lever_or_button(context, BLOCK_CRIMSON_BUTTON);
        break;
    case ITEM_WARPED_BUTTON:
        place_lever_or_button(context, BLOCK_WARPED_BUTTON);
        break;
    case ITEM_POLISHED_BLACKSTONE_BUTTON:
        place_lever_or_button(context, BLOCK_POLISHED_BLACKSTONE_BUTTON);
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
        place_pressure_plate(context, BLOCK_LIGHT_WEIGHTED_PRESSURE_PLATE);
        break;
    case ITEM_HEAVY_WEIGHTED_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_HEAVY_WEIGHTED_PRESSURE_PLATE);
        break;
    case ITEM_DAYLIGHT_DETECTOR:
        break;
    case ITEM_REDSTONE_BLOCK:
        place_simple_block(context, BLOCK_REDSTONE_BLOCK);
        break;
    case ITEM_NETHER_QUARTZ_ORE:
        place_simple_block(context, BLOCK_NETHER_QUARTZ_ORE);
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
        place_stairs(context, BLOCK_QUARTZ_STAIRS);
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
        place_simple_block(context, BLOCK_BARRIER);
        break;
    case ITEM_IRON_TRAPDOOR:
        place_trapdoor(context, BLOCK_IRON_TRAPDOOR);
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
        place_stairs(context, BLOCK_ACACIA_STAIRS);
        break;
    case ITEM_DARK_OAK_STAIRS:
        place_stairs(context, BLOCK_DARK_OAK_STAIRS);
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
        place_pane(context, BLOCK_WHITE_STAINED_GLASS_PANE);
        break;
    case ITEM_ORANGE_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_ORANGE_STAINED_GLASS_PANE);
        break;
    case ITEM_MAGENTA_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_MAGENTA_STAINED_GLASS_PANE);
        break;
    case ITEM_LIGHT_BLUE_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_LIGHT_BLUE_STAINED_GLASS_PANE);
        break;
    case ITEM_YELLOW_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_YELLOW_STAINED_GLASS_PANE);
        break;
    case ITEM_LIME_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_LIME_STAINED_GLASS_PANE);
        break;
    case ITEM_PINK_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_PINK_STAINED_GLASS_PANE);
        break;
    case ITEM_GRAY_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_GRAY_STAINED_GLASS_PANE);
        break;
    case ITEM_LIGHT_GRAY_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_LIGHT_GRAY_STAINED_GLASS_PANE);
        break;
    case ITEM_CYAN_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_CYAN_STAINED_GLASS_PANE);
        break;
    case ITEM_PURPLE_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_PURPLE_STAINED_GLASS_PANE);
        break;
    case ITEM_BLUE_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_BLUE_STAINED_GLASS_PANE);
        break;
    case ITEM_BROWN_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_BROWN_STAINED_GLASS_PANE);
        break;
    case ITEM_GREEN_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_GREEN_STAINED_GLASS_PANE);
        break;
    case ITEM_RED_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_RED_STAINED_GLASS_PANE);
        break;
    case ITEM_BLACK_STAINED_GLASS_PANE:
        place_pane(context, BLOCK_BLACK_STAINED_GLASS_PANE);
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
        place_stairs(context, BLOCK_PRISMARINE_STAIRS);
        break;
    case ITEM_PRISMARINE_BRICK_STAIRS:
        place_stairs(context, BLOCK_PRISMARINE_BRICK_STAIRS);
        break;
    case ITEM_DARK_PRISMARINE_STAIRS:
        place_stairs(context, BLOCK_DARK_PRISMARINE_STAIRS);
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
        place_stairs(context, BLOCK_RED_SANDSTONE_STAIRS);
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
        place_simple_block(context, BLOCK_STRUCTURE_VOID);
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
        place_horizontal_facing(context, BLOCK_WHITE_GLAZED_TERRACOTTA);
        break;
    case ITEM_ORANGE_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_ORANGE_GLAZED_TERRACOTTA);
        break;
    case ITEM_MAGENTA_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_MAGENTA_GLAZED_TERRACOTTA);
        break;
    case ITEM_LIGHT_BLUE_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_LIGHT_BLUE_GLAZED_TERRACOTTA);
        break;
    case ITEM_YELLOW_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_YELLOW_GLAZED_TERRACOTTA);
        break;
    case ITEM_LIME_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_LIME_GLAZED_TERRACOTTA);
        break;
    case ITEM_PINK_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_PINK_GLAZED_TERRACOTTA);
        break;
    case ITEM_GRAY_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_GRAY_GLAZED_TERRACOTTA);
        break;
    case ITEM_LIGHT_GRAY_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_LIGHT_GRAY_GLAZED_TERRACOTTA);
        break;
    case ITEM_CYAN_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_CYAN_GLAZED_TERRACOTTA);
        break;
    case ITEM_PURPLE_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_PURPLE_GLAZED_TERRACOTTA);
        break;
    case ITEM_BLUE_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_BLUE_GLAZED_TERRACOTTA);
        break;
    case ITEM_BROWN_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_BROWN_GLAZED_TERRACOTTA);
        break;
    case ITEM_GREEN_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_GREEN_GLAZED_TERRACOTTA);
        break;
    case ITEM_RED_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_RED_GLAZED_TERRACOTTA);
        break;
    case ITEM_BLACK_GLAZED_TERRACOTTA:
        place_horizontal_facing(context, BLOCK_BLACK_GLAZED_TERRACOTTA);
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
        place_stairs(context, BLOCK_POLISHED_GRANITE_STAIRS);
        break;
    case ITEM_SMOOTH_RED_SANDSTONE_STAIRS:
        place_stairs(context, BLOCK_SMOOTH_RED_SANDSTONE_STAIRS);
        break;
    case ITEM_MOSSY_STONE_BRICK_STAIRS:
        place_stairs(context, BLOCK_MOSSY_STONE_BRICK_STAIRS);
        break;
    case ITEM_POLISHED_DIORITE_STAIRS:
        place_stairs(context, BLOCK_POLISHED_DIORITE_STAIRS);
        break;
    case ITEM_MOSSY_COBBLESTONE_STAIRS:
        place_stairs(context, BLOCK_MOSSY_COBBLESTONE_STAIRS);
        break;
    case ITEM_END_STONE_BRICK_STAIRS:
        place_stairs(context, BLOCK_END_STONE_BRICK_STAIRS);
        break;
    case ITEM_STONE_STAIRS:
        place_stairs(context, BLOCK_STONE_STAIRS);
        break;
    case ITEM_SMOOTH_SANDSTONE_STAIRS:
        place_stairs(context, BLOCK_SMOOTH_SANDSTONE_STAIRS);
        break;
    case ITEM_SMOOTH_QUARTZ_STAIRS:
        place_stairs(context, BLOCK_SMOOTH_QUARTZ_STAIRS);
        break;
    case ITEM_GRANITE_STAIRS:
        place_stairs(context, BLOCK_GRANITE_STAIRS);
        break;
    case ITEM_ANDESITE_STAIRS:
        place_stairs(context, BLOCK_ANDESITE_STAIRS);
        break;
    case ITEM_RED_NETHER_BRICK_STAIRS:
        place_stairs(context, BLOCK_RED_NETHER_BRICK_STAIRS);
        break;
    case ITEM_POLISHED_ANDESITE_STAIRS:
        place_stairs(context, BLOCK_POLISHED_ANDESITE_STAIRS);
        break;
    case ITEM_DIORITE_STAIRS:
        place_stairs(context, BLOCK_DIORITE_STAIRS);
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
        place_door(context, BLOCK_IRON_DOOR);
        break;
    case ITEM_OAK_DOOR:
        place_door(context, BLOCK_OAK_DOOR);
        break;
    case ITEM_SPRUCE_DOOR:
        place_door(context, BLOCK_SPRUCE_DOOR);
        break;
    case ITEM_BIRCH_DOOR:
        place_door(context, BLOCK_BIRCH_DOOR);
        break;
    case ITEM_JUNGLE_DOOR:
        place_door(context, BLOCK_JUNGLE_DOOR);
        break;
    case ITEM_ACACIA_DOOR:
        place_door(context, BLOCK_ACACIA_DOOR);
        break;
    case ITEM_DARK_OAK_DOOR:
        place_door(context, BLOCK_DARK_OAK_DOOR);
        break;
    case ITEM_CRIMSON_DOOR:
        place_door(context, BLOCK_CRIMSON_DOOR);
        break;
    case ITEM_WARPED_DOOR:
        place_door(context, BLOCK_WARPED_DOOR);
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
        place_redstone_wire(context, BLOCK_REDSTONE_WIRE);
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
        place_bed(context, BLOCK_WHITE_BED, DYE_COLOUR_WHITE);
        break;
    case ITEM_ORANGE_BED:
        place_bed(context, BLOCK_ORANGE_BED, DYE_COLOUR_ORANGE);
        break;
    case ITEM_MAGENTA_BED:
        place_bed(context, BLOCK_MAGENTA_BED, DYE_COLOUR_MAGENTA);
        break;
    case ITEM_LIGHT_BLUE_BED:
        place_bed(context, BLOCK_LIGHT_BLUE_BED, DYE_COLOUR_LIGHT_BLUE);
        break;
    case ITEM_YELLOW_BED:
        place_bed(context, BLOCK_YELLOW_BED, DYE_COLOUR_YELLOW);
        break;
    case ITEM_LIME_BED:
        place_bed(context, BLOCK_LIME_BED, DYE_COLOUR_LIME);
        break;
    case ITEM_PINK_BED:
        place_bed(context, BLOCK_PINK_BED, DYE_COLOUR_PINK);
        break;
    case ITEM_GRAY_BED:
        place_bed(context, BLOCK_GRAY_BED, DYE_COLOUR_GRAY);
        break;
    case ITEM_LIGHT_GRAY_BED:
        place_bed(context, BLOCK_LIGHT_GRAY_BED, DYE_COLOUR_LIGHT_GRAY);
        break;
    case ITEM_CYAN_BED:
        place_bed(context, BLOCK_CYAN_BED, DYE_COLOUR_CYAN);
        break;
    case ITEM_PURPLE_BED:
        place_bed(context, BLOCK_PURPLE_BED, DYE_COLOUR_PURPLE);
        break;
    case ITEM_BLUE_BED:
        place_bed(context, BLOCK_BLUE_BED, DYE_COLOUR_BLUE);
        break;
    case ITEM_BROWN_BED:
        place_bed(context, BLOCK_BROWN_BED, DYE_COLOUR_BROWN);
        break;
    case ITEM_GREEN_BED:
        place_bed(context, BLOCK_GREEN_BED, DYE_COLOUR_GREEN);
        break;
    case ITEM_RED_BED:
        place_bed(context, BLOCK_RED_BED, DYE_COLOUR_RED);
        break;
    case ITEM_BLACK_BED:
        place_bed(context, BLOCK_BLACK_BED, DYE_COLOUR_BLACK);
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
        place_grindstone(context, BLOCK_GRINDSTONE);
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
        place_stairs(context, BLOCK_BLACKSTONE_STAIRS);
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
        place_stairs(context, BLOCK_POLISHED_BLACKSTONE_STAIRS);
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
        place_stairs(context, BLOCK_POLISHED_BLACKSTONE_BRICK_STAIRS);
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

static void
register_item_type(mc_int item_type, char * resource_loc) {
    net_string key = {
        .size = strlen(resource_loc),
        .ptr = resource_loc
    };
    resource_loc_table * table = &serv->item_resource_table;
    register_resource_loc(key, item_type, table);
    assert(net_string_equal(key, get_resource_loc(item_type, table)));
    assert(item_type == resolve_resource_loc_id(key, table));
}

void
init_item_data(void) {
    register_item_type(ITEM_AIR, "minecraft:air");
    register_item_type(ITEM_STONE, "minecraft:stone");
    register_item_type(ITEM_GRANITE, "minecraft:granite");
    register_item_type(ITEM_POLISHED_GRANITE, "minecraft:polished_granite");
    register_item_type(ITEM_DIORITE, "minecraft:diorite");
    register_item_type(ITEM_POLISHED_DIORITE, "minecraft:polished_diorite");
    register_item_type(ITEM_ANDESITE, "minecraft:andesite");
    register_item_type(ITEM_POLISHED_ANDESITE, "minecraft:polished_andesite");
    register_item_type(ITEM_GRASS_BLOCK, "minecraft:grass_block");
    register_item_type(ITEM_DIRT, "minecraft:dirt");
    register_item_type(ITEM_COARSE_DIRT, "minecraft:coarse_dirt");
    register_item_type(ITEM_PODZOL, "minecraft:podzol");
    register_item_type(ITEM_CRIMSON_NYLIUM, "minecraft:crimson_nylium");
    register_item_type(ITEM_WARPED_NYLIUM, "minecraft:warped_nylium");
    register_item_type(ITEM_COBBLESTONE, "minecraft:cobblestone");
    register_item_type(ITEM_OAK_PLANKS, "minecraft:oak_planks");
    register_item_type(ITEM_SPRUCE_PLANKS, "minecraft:spruce_planks");
    register_item_type(ITEM_BIRCH_PLANKS, "minecraft:birch_planks");
    register_item_type(ITEM_JUNGLE_PLANKS, "minecraft:jungle_planks");
    register_item_type(ITEM_ACACIA_PLANKS, "minecraft:acacia_planks");
    register_item_type(ITEM_DARK_OAK_PLANKS, "minecraft:dark_oak_planks");
    register_item_type(ITEM_CRIMSON_PLANKS, "minecraft:crimson_planks");
    register_item_type(ITEM_WARPED_PLANKS, "minecraft:warped_planks");
    register_item_type(ITEM_OAK_SAPLING, "minecraft:oak_sapling");
    register_item_type(ITEM_SPRUCE_SAPLING, "minecraft:spruce_sapling");
    register_item_type(ITEM_BIRCH_SAPLING, "minecraft:birch_sapling");
    register_item_type(ITEM_JUNGLE_SAPLING, "minecraft:jungle_sapling");
    register_item_type(ITEM_ACACIA_SAPLING, "minecraft:acacia_sapling");
    register_item_type(ITEM_DARK_OAK_SAPLING, "minecraft:dark_oak_sapling");
    register_item_type(ITEM_BEDROCK, "minecraft:bedrock");
    register_item_type(ITEM_SAND, "minecraft:sand");
    register_item_type(ITEM_RED_SAND, "minecraft:red_sand");
    register_item_type(ITEM_GRAVEL, "minecraft:gravel");
    register_item_type(ITEM_GOLD_ORE, "minecraft:gold_ore");
    register_item_type(ITEM_IRON_ORE, "minecraft:iron_ore");
    register_item_type(ITEM_COAL_ORE, "minecraft:coal_ore");
    register_item_type(ITEM_NETHER_GOLD_ORE, "minecraft:nether_gold_ore");
    register_item_type(ITEM_OAK_LOG, "minecraft:oak_log");
    register_item_type(ITEM_SPRUCE_LOG, "minecraft:spruce_log");
    register_item_type(ITEM_BIRCH_LOG, "minecraft:birch_log");
    register_item_type(ITEM_JUNGLE_LOG, "minecraft:jungle_log");
    register_item_type(ITEM_ACACIA_LOG, "minecraft:acacia_log");
    register_item_type(ITEM_DARK_OAK_LOG, "minecraft:dark_oak_log");
    register_item_type(ITEM_CRIMSON_STEM, "minecraft:crimson_stem");
    register_item_type(ITEM_WARPED_STEM, "minecraft:warped_stem");
    register_item_type(ITEM_STRIPPED_OAK_LOG, "minecraft:stripped_oak_log");
    register_item_type(ITEM_STRIPPED_SPRUCE_LOG, "minecraft:stripped_spruce_log");
    register_item_type(ITEM_STRIPPED_BIRCH_LOG, "minecraft:stripped_birch_log");
    register_item_type(ITEM_STRIPPED_JUNGLE_LOG, "minecraft:stripped_jungle_log");
    register_item_type(ITEM_STRIPPED_ACACIA_LOG, "minecraft:stripped_acacia_log");
    register_item_type(ITEM_STRIPPED_DARK_OAK_LOG, "minecraft:stripped_dark_oak_log");
    register_item_type(ITEM_STRIPPED_CRIMSON_STEM, "minecraft:stripped_crimson_stem");
    register_item_type(ITEM_STRIPPED_WARPED_STEM, "minecraft:stripped_warped_stem");
    register_item_type(ITEM_STRIPPED_OAK_WOOD, "minecraft:stripped_oak_wood");
    register_item_type(ITEM_STRIPPED_SPRUCE_WOOD, "minecraft:stripped_spruce_wood");
    register_item_type(ITEM_STRIPPED_BIRCH_WOOD, "minecraft:stripped_birch_wood");
    register_item_type(ITEM_STRIPPED_JUNGLE_WOOD, "minecraft:stripped_jungle_wood");
    register_item_type(ITEM_STRIPPED_ACACIA_WOOD, "minecraft:stripped_acacia_wood");
    register_item_type(ITEM_STRIPPED_DARK_OAK_WOOD, "minecraft:stripped_dark_oak_wood");
    register_item_type(ITEM_STRIPPED_CRIMSON_HYPHAE, "minecraft:stripped_crimson_hyphae");
    register_item_type(ITEM_STRIPPED_WARPED_HYPHAE, "minecraft:stripped_warped_hyphae");
    register_item_type(ITEM_OAK_WOOD, "minecraft:oak_wood");
    register_item_type(ITEM_SPRUCE_WOOD, "minecraft:spruce_wood");
    register_item_type(ITEM_BIRCH_WOOD, "minecraft:birch_wood");
    register_item_type(ITEM_JUNGLE_WOOD, "minecraft:jungle_wood");
    register_item_type(ITEM_ACACIA_WOOD, "minecraft:acacia_wood");
    register_item_type(ITEM_DARK_OAK_WOOD, "minecraft:dark_oak_wood");
    register_item_type(ITEM_CRIMSON_HYPHAE, "minecraft:crimson_hyphae");
    register_item_type(ITEM_WARPED_HYPHAE, "minecraft:warped_hyphae");
    register_item_type(ITEM_OAK_LEAVES, "minecraft:oak_leaves");
    register_item_type(ITEM_SPRUCE_LEAVES, "minecraft:spruce_leaves");
    register_item_type(ITEM_BIRCH_LEAVES, "minecraft:birch_leaves");
    register_item_type(ITEM_JUNGLE_LEAVES, "minecraft:jungle_leaves");
    register_item_type(ITEM_ACACIA_LEAVES, "minecraft:acacia_leaves");
    register_item_type(ITEM_DARK_OAK_LEAVES, "minecraft:dark_oak_leaves");
    register_item_type(ITEM_SPONGE, "minecraft:sponge");
    register_item_type(ITEM_WET_SPONGE, "minecraft:wet_sponge");
    register_item_type(ITEM_GLASS, "minecraft:glass");
    register_item_type(ITEM_LAPIS_ORE, "minecraft:lapis_ore");
    register_item_type(ITEM_LAPIS_BLOCK, "minecraft:lapis_block");
    register_item_type(ITEM_DISPENSER, "minecraft:dispenser");
    register_item_type(ITEM_SANDSTONE, "minecraft:sandstone");
    register_item_type(ITEM_CHISELED_SANDSTONE, "minecraft:chiseled_sandstone");
    register_item_type(ITEM_CUT_SANDSTONE, "minecraft:cut_sandstone");
    register_item_type(ITEM_NOTE_BLOCK, "minecraft:note_block");
    register_item_type(ITEM_POWERED_RAIL, "minecraft:powered_rail");
    register_item_type(ITEM_DETECTOR_RAIL, "minecraft:detector_rail");
    register_item_type(ITEM_STICKY_PISTON, "minecraft:sticky_piston");
    register_item_type(ITEM_COBWEB, "minecraft:cobweb");
    register_item_type(ITEM_GRASS, "minecraft:grass");
    register_item_type(ITEM_FERN, "minecraft:fern");
    register_item_type(ITEM_DEAD_BUSH, "minecraft:dead_bush");
    register_item_type(ITEM_SEAGRASS, "minecraft:seagrass");
    register_item_type(ITEM_SEA_PICKLE, "minecraft:sea_pickle");
    register_item_type(ITEM_PISTON, "minecraft:piston");
    register_item_type(ITEM_WHITE_WOOL, "minecraft:white_wool");
    register_item_type(ITEM_ORANGE_WOOL, "minecraft:orange_wool");
    register_item_type(ITEM_MAGENTA_WOOL, "minecraft:magenta_wool");
    register_item_type(ITEM_LIGHT_BLUE_WOOL, "minecraft:light_blue_wool");
    register_item_type(ITEM_YELLOW_WOOL, "minecraft:yellow_wool");
    register_item_type(ITEM_LIME_WOOL, "minecraft:lime_wool");
    register_item_type(ITEM_PINK_WOOL, "minecraft:pink_wool");
    register_item_type(ITEM_GRAY_WOOL, "minecraft:gray_wool");
    register_item_type(ITEM_LIGHT_GRAY_WOOL, "minecraft:light_gray_wool");
    register_item_type(ITEM_CYAN_WOOL, "minecraft:cyan_wool");
    register_item_type(ITEM_PURPLE_WOOL, "minecraft:purple_wool");
    register_item_type(ITEM_BLUE_WOOL, "minecraft:blue_wool");
    register_item_type(ITEM_BROWN_WOOL, "minecraft:brown_wool");
    register_item_type(ITEM_GREEN_WOOL, "minecraft:green_wool");
    register_item_type(ITEM_RED_WOOL, "minecraft:red_wool");
    register_item_type(ITEM_BLACK_WOOL, "minecraft:black_wool");
    register_item_type(ITEM_DANDELION, "minecraft:dandelion");
    register_item_type(ITEM_POPPY, "minecraft:poppy");
    register_item_type(ITEM_BLUE_ORCHID, "minecraft:blue_orchid");
    register_item_type(ITEM_ALLIUM, "minecraft:allium");
    register_item_type(ITEM_AZURE_BLUET, "minecraft:azure_bluet");
    register_item_type(ITEM_RED_TULIP, "minecraft:red_tulip");
    register_item_type(ITEM_ORANGE_TULIP, "minecraft:orange_tulip");
    register_item_type(ITEM_WHITE_TULIP, "minecraft:white_tulip");
    register_item_type(ITEM_PINK_TULIP, "minecraft:pink_tulip");
    register_item_type(ITEM_OXEYE_DAISY, "minecraft:oxeye_daisy");
    register_item_type(ITEM_CORNFLOWER, "minecraft:cornflower");
    register_item_type(ITEM_LILY_OF_THE_VALLEY, "minecraft:lily_of_the_valley");
    register_item_type(ITEM_WITHER_ROSE, "minecraft:wither_rose");
    register_item_type(ITEM_BROWN_MUSHROOM, "minecraft:brown_mushroom");
    register_item_type(ITEM_RED_MUSHROOM, "minecraft:red_mushroom");
    register_item_type(ITEM_CRIMSON_FUNGUS, "minecraft:crimson_fungus");
    register_item_type(ITEM_WARPED_FUNGUS, "minecraft:warped_fungus");
    register_item_type(ITEM_CRIMSON_ROOTS, "minecraft:crimson_roots");
    register_item_type(ITEM_WARPED_ROOTS, "minecraft:warped_roots");
    register_item_type(ITEM_NETHER_SPROUTS, "minecraft:nether_sprouts");
    register_item_type(ITEM_WEEPING_VINES, "minecraft:weeping_vines");
    register_item_type(ITEM_TWISTING_VINES, "minecraft:twisting_vines");
    register_item_type(ITEM_SUGAR_CANE, "minecraft:sugar_cane");
    register_item_type(ITEM_KELP, "minecraft:kelp");
    register_item_type(ITEM_BAMBOO, "minecraft:bamboo");
    register_item_type(ITEM_GOLD_BLOCK, "minecraft:gold_block");
    register_item_type(ITEM_IRON_BLOCK, "minecraft:iron_block");
    register_item_type(ITEM_OAK_SLAB, "minecraft:oak_slab");
    register_item_type(ITEM_SPRUCE_SLAB, "minecraft:spruce_slab");
    register_item_type(ITEM_BIRCH_SLAB, "minecraft:birch_slab");
    register_item_type(ITEM_JUNGLE_SLAB, "minecraft:jungle_slab");
    register_item_type(ITEM_ACACIA_SLAB, "minecraft:acacia_slab");
    register_item_type(ITEM_DARK_OAK_SLAB, "minecraft:dark_oak_slab");
    register_item_type(ITEM_CRIMSON_SLAB, "minecraft:crimson_slab");
    register_item_type(ITEM_WARPED_SLAB, "minecraft:warped_slab");
    register_item_type(ITEM_STONE_SLAB, "minecraft:stone_slab");
    register_item_type(ITEM_SMOOTH_STONE_SLAB, "minecraft:smooth_stone_slab");
    register_item_type(ITEM_SANDSTONE_SLAB, "minecraft:sandstone_slab");
    register_item_type(ITEM_CUT_SANDSTONE_SLAB, "minecraft:cut_sandstone_slab");
    register_item_type(ITEM_PETRIFIED_OAK_SLAB, "minecraft:petrified_oak_slab");
    register_item_type(ITEM_COBBLESTONE_SLAB, "minecraft:cobblestone_slab");
    register_item_type(ITEM_BRICK_SLAB, "minecraft:brick_slab");
    register_item_type(ITEM_STONE_BRICK_SLAB, "minecraft:stone_brick_slab");
    register_item_type(ITEM_NETHER_BRICK_SLAB, "minecraft:nether_brick_slab");
    register_item_type(ITEM_QUARTZ_SLAB, "minecraft:quartz_slab");
    register_item_type(ITEM_RED_SANDSTONE_SLAB, "minecraft:red_sandstone_slab");
    register_item_type(ITEM_CUT_RED_SANDSTONE_SLAB, "minecraft:cut_red_sandstone_slab");
    register_item_type(ITEM_PURPUR_SLAB, "minecraft:purpur_slab");
    register_item_type(ITEM_PRISMARINE_SLAB, "minecraft:prismarine_slab");
    register_item_type(ITEM_PRISMARINE_BRICK_SLAB, "minecraft:prismarine_brick_slab");
    register_item_type(ITEM_DARK_PRISMARINE_SLAB, "minecraft:dark_prismarine_slab");
    register_item_type(ITEM_SMOOTH_QUARTZ, "minecraft:smooth_quartz");
    register_item_type(ITEM_SMOOTH_RED_SANDSTONE, "minecraft:smooth_red_sandstone");
    register_item_type(ITEM_SMOOTH_SANDSTONE, "minecraft:smooth_sandstone");
    register_item_type(ITEM_SMOOTH_STONE, "minecraft:smooth_stone");
    register_item_type(ITEM_BRICKS, "minecraft:bricks");
    register_item_type(ITEM_TNT, "minecraft:tnt");
    register_item_type(ITEM_BOOKSHELF, "minecraft:bookshelf");
    register_item_type(ITEM_MOSSY_COBBLESTONE, "minecraft:mossy_cobblestone");
    register_item_type(ITEM_OBSIDIAN, "minecraft:obsidian");
    register_item_type(ITEM_TORCH, "minecraft:torch");
    register_item_type(ITEM_END_ROD, "minecraft:end_rod");
    register_item_type(ITEM_CHORUS_PLANT, "minecraft:chorus_plant");
    register_item_type(ITEM_CHORUS_FLOWER, "minecraft:chorus_flower");
    register_item_type(ITEM_PURPUR_BLOCK, "minecraft:purpur_block");
    register_item_type(ITEM_PURPUR_PILLAR, "minecraft:purpur_pillar");
    register_item_type(ITEM_PURPUR_STAIRS, "minecraft:purpur_stairs");
    register_item_type(ITEM_SPAWNER, "minecraft:spawner");
    register_item_type(ITEM_OAK_STAIRS, "minecraft:oak_stairs");
    register_item_type(ITEM_CHEST, "minecraft:chest");
    register_item_type(ITEM_DIAMOND_ORE, "minecraft:diamond_ore");
    register_item_type(ITEM_DIAMOND_BLOCK, "minecraft:diamond_block");
    register_item_type(ITEM_CRAFTING_TABLE, "minecraft:crafting_table");
    register_item_type(ITEM_FARMLAND, "minecraft:farmland");
    register_item_type(ITEM_FURNACE, "minecraft:furnace");
    register_item_type(ITEM_LADDER, "minecraft:ladder");
    register_item_type(ITEM_RAIL, "minecraft:rail");
    register_item_type(ITEM_COBBLESTONE_STAIRS, "minecraft:cobblestone_stairs");
    register_item_type(ITEM_LEVER, "minecraft:lever");
    register_item_type(ITEM_STONE_PRESSURE_PLATE, "minecraft:stone_pressure_plate");
    register_item_type(ITEM_OAK_PRESSURE_PLATE, "minecraft:oak_pressure_plate");
    register_item_type(ITEM_SPRUCE_PRESSURE_PLATE, "minecraft:spruce_pressure_plate");
    register_item_type(ITEM_BIRCH_PRESSURE_PLATE, "minecraft:birch_pressure_plate");
    register_item_type(ITEM_JUNGLE_PRESSURE_PLATE, "minecraft:jungle_pressure_plate");
    register_item_type(ITEM_ACACIA_PRESSURE_PLATE, "minecraft:acacia_pressure_plate");
    register_item_type(ITEM_DARK_OAK_PRESSURE_PLATE, "minecraft:dark_oak_pressure_plate");
    register_item_type(ITEM_CRIMSON_PRESSURE_PLATE, "minecraft:crimson_pressure_plate");
    register_item_type(ITEM_WARPED_PRESSURE_PLATE, "minecraft:warped_pressure_plate");
    register_item_type(ITEM_POLISHED_BLACKSTONE_PRESSURE_PLATE, "minecraft:polished_blackstone_pressure_plate");
    register_item_type(ITEM_REDSTONE_ORE, "minecraft:redstone_ore");
    register_item_type(ITEM_REDSTONE_TORCH, "minecraft:redstone_torch");
    register_item_type(ITEM_SNOW, "minecraft:snow");
    register_item_type(ITEM_ICE, "minecraft:ice");
    register_item_type(ITEM_SNOW_BLOCK, "minecraft:snow_block");
    register_item_type(ITEM_CACTUS, "minecraft:cactus");
    register_item_type(ITEM_CLAY, "minecraft:clay");
    register_item_type(ITEM_JUKEBOX, "minecraft:jukebox");
    register_item_type(ITEM_OAK_FENCE, "minecraft:oak_fence");
    register_item_type(ITEM_SPRUCE_FENCE, "minecraft:spruce_fence");
    register_item_type(ITEM_BIRCH_FENCE, "minecraft:birch_fence");
    register_item_type(ITEM_JUNGLE_FENCE, "minecraft:jungle_fence");
    register_item_type(ITEM_ACACIA_FENCE, "minecraft:acacia_fence");
    register_item_type(ITEM_DARK_OAK_FENCE, "minecraft:dark_oak_fence");
    register_item_type(ITEM_CRIMSON_FENCE, "minecraft:crimson_fence");
    register_item_type(ITEM_WARPED_FENCE, "minecraft:warped_fence");
    register_item_type(ITEM_PUMPKIN, "minecraft:pumpkin");
    register_item_type(ITEM_CARVED_PUMPKIN, "minecraft:carved_pumpkin");
    register_item_type(ITEM_NETHERRACK, "minecraft:netherrack");
    register_item_type(ITEM_SOUL_SAND, "minecraft:soul_sand");
    register_item_type(ITEM_SOUL_SOIL, "minecraft:soul_soil");
    register_item_type(ITEM_BASALT, "minecraft:basalt");
    register_item_type(ITEM_POLISHED_BASALT, "minecraft:polished_basalt");
    register_item_type(ITEM_SOUL_TORCH, "minecraft:soul_torch");
    register_item_type(ITEM_GLOWSTONE, "minecraft:glowstone");
    register_item_type(ITEM_JACK_O_LANTERN, "minecraft:jack_o_lantern");
    register_item_type(ITEM_OAK_TRAPDOOR, "minecraft:oak_trapdoor");
    register_item_type(ITEM_SPRUCE_TRAPDOOR, "minecraft:spruce_trapdoor");
    register_item_type(ITEM_BIRCH_TRAPDOOR, "minecraft:birch_trapdoor");
    register_item_type(ITEM_JUNGLE_TRAPDOOR, "minecraft:jungle_trapdoor");
    register_item_type(ITEM_ACACIA_TRAPDOOR, "minecraft:acacia_trapdoor");
    register_item_type(ITEM_DARK_OAK_TRAPDOOR, "minecraft:dark_oak_trapdoor");
    register_item_type(ITEM_CRIMSON_TRAPDOOR, "minecraft:crimson_trapdoor");
    register_item_type(ITEM_WARPED_TRAPDOOR, "minecraft:warped_trapdoor");
    register_item_type(ITEM_INFESTED_STONE, "minecraft:infested_stone");
    register_item_type(ITEM_INFESTED_COBBLESTONE, "minecraft:infested_cobblestone");
    register_item_type(ITEM_INFESTED_STONE_BRICKS, "minecraft:infested_stone_bricks");
    register_item_type(ITEM_INFESTED_MOSSY_STONE_BRICKS, "minecraft:infested_mossy_stone_bricks");
    register_item_type(ITEM_INFESTED_CRACKED_STONE_BRICKS, "minecraft:infested_cracked_stone_bricks");
    register_item_type(ITEM_INFESTED_CHISELED_STONE_BRICKS, "minecraft:infested_chiseled_stone_bricks");
    register_item_type(ITEM_STONE_BRICKS, "minecraft:stone_bricks");
    register_item_type(ITEM_MOSSY_STONE_BRICKS, "minecraft:mossy_stone_bricks");
    register_item_type(ITEM_CRACKED_STONE_BRICKS, "minecraft:cracked_stone_bricks");
    register_item_type(ITEM_CHISELED_STONE_BRICKS, "minecraft:chiseled_stone_bricks");
    register_item_type(ITEM_BROWN_MUSHROOM_BLOCK, "minecraft:brown_mushroom_block");
    register_item_type(ITEM_RED_MUSHROOM_BLOCK, "minecraft:red_mushroom_block");
    register_item_type(ITEM_MUSHROOM_STEM, "minecraft:mushroom_stem");
    register_item_type(ITEM_IRON_BARS, "minecraft:iron_bars");
    register_item_type(ITEM_CHAIN, "minecraft:chain");
    register_item_type(ITEM_GLASS_PANE, "minecraft:glass_pane");
    register_item_type(ITEM_MELON, "minecraft:melon");
    register_item_type(ITEM_VINE, "minecraft:vine");
    register_item_type(ITEM_OAK_FENCE_GATE, "minecraft:oak_fence_gate");
    register_item_type(ITEM_SPRUCE_FENCE_GATE, "minecraft:spruce_fence_gate");
    register_item_type(ITEM_BIRCH_FENCE_GATE, "minecraft:birch_fence_gate");
    register_item_type(ITEM_JUNGLE_FENCE_GATE, "minecraft:jungle_fence_gate");
    register_item_type(ITEM_ACACIA_FENCE_GATE, "minecraft:acacia_fence_gate");
    register_item_type(ITEM_DARK_OAK_FENCE_GATE, "minecraft:dark_oak_fence_gate");
    register_item_type(ITEM_CRIMSON_FENCE_GATE, "minecraft:crimson_fence_gate");
    register_item_type(ITEM_WARPED_FENCE_GATE, "minecraft:warped_fence_gate");
    register_item_type(ITEM_BRICK_STAIRS, "minecraft:brick_stairs");
    register_item_type(ITEM_STONE_BRICK_STAIRS, "minecraft:stone_brick_stairs");
    register_item_type(ITEM_MYCELIUM, "minecraft:mycelium");
    register_item_type(ITEM_LILY_PAD, "minecraft:lily_pad");
    register_item_type(ITEM_NETHER_BRICKS, "minecraft:nether_bricks");
    register_item_type(ITEM_CRACKED_NETHER_BRICKS, "minecraft:cracked_nether_bricks");
    register_item_type(ITEM_CHISELED_NETHER_BRICKS, "minecraft:chiseled_nether_bricks");
    register_item_type(ITEM_NETHER_BRICK_FENCE, "minecraft:nether_brick_fence");
    register_item_type(ITEM_NETHER_BRICK_STAIRS, "minecraft:nether_brick_stairs");
    register_item_type(ITEM_ENCHANTING_TABLE, "minecraft:enchanting_table");
    register_item_type(ITEM_END_PORTAL_FRAME, "minecraft:end_portal_frame");
    register_item_type(ITEM_END_STONE, "minecraft:end_stone");
    register_item_type(ITEM_END_STONE_BRICKS, "minecraft:end_stone_bricks");
    register_item_type(ITEM_DRAGON_EGG, "minecraft:dragon_egg");
    register_item_type(ITEM_REDSTONE_LAMP, "minecraft:redstone_lamp");
    register_item_type(ITEM_SANDSTONE_STAIRS, "minecraft:sandstone_stairs");
    register_item_type(ITEM_EMERALD_ORE, "minecraft:emerald_ore");
    register_item_type(ITEM_ENDER_CHEST, "minecraft:ender_chest");
    register_item_type(ITEM_TRIPWIRE_HOOK, "minecraft:tripwire_hook");
    register_item_type(ITEM_EMERALD_BLOCK, "minecraft:emerald_block");
    register_item_type(ITEM_SPRUCE_STAIRS, "minecraft:spruce_stairs");
    register_item_type(ITEM_BIRCH_STAIRS, "minecraft:birch_stairs");
    register_item_type(ITEM_JUNGLE_STAIRS, "minecraft:jungle_stairs");
    register_item_type(ITEM_CRIMSON_STAIRS, "minecraft:crimson_stairs");
    register_item_type(ITEM_WARPED_STAIRS, "minecraft:warped_stairs");
    register_item_type(ITEM_COMMAND_BLOCK, "minecraft:command_block");
    register_item_type(ITEM_BEACON, "minecraft:beacon");
    register_item_type(ITEM_COBBLESTONE_WALL, "minecraft:cobblestone_wall");
    register_item_type(ITEM_MOSSY_COBBLESTONE_WALL, "minecraft:mossy_cobblestone_wall");
    register_item_type(ITEM_BRICK_WALL, "minecraft:brick_wall");
    register_item_type(ITEM_PRISMARINE_WALL, "minecraft:prismarine_wall");
    register_item_type(ITEM_RED_SANDSTONE_WALL, "minecraft:red_sandstone_wall");
    register_item_type(ITEM_MOSSY_STONE_BRICK_WALL, "minecraft:mossy_stone_brick_wall");
    register_item_type(ITEM_GRANITE_WALL, "minecraft:granite_wall");
    register_item_type(ITEM_STONE_BRICK_WALL, "minecraft:stone_brick_wall");
    register_item_type(ITEM_NETHER_BRICK_WALL, "minecraft:nether_brick_wall");
    register_item_type(ITEM_ANDESITE_WALL, "minecraft:andesite_wall");
    register_item_type(ITEM_RED_NETHER_BRICK_WALL, "minecraft:red_nether_brick_wall");
    register_item_type(ITEM_SANDSTONE_WALL, "minecraft:sandstone_wall");
    register_item_type(ITEM_END_STONE_BRICK_WALL, "minecraft:end_stone_brick_wall");
    register_item_type(ITEM_DIORITE_WALL, "minecraft:diorite_wall");
    register_item_type(ITEM_BLACKSTONE_WALL, "minecraft:blackstone_wall");
    register_item_type(ITEM_POLISHED_BLACKSTONE_WALL, "minecraft:polished_blackstone_wall");
    register_item_type(ITEM_POLISHED_BLACKSTONE_BRICK_WALL, "minecraft:polished_blackstone_brick_wall");
    register_item_type(ITEM_STONE_BUTTON, "minecraft:stone_button");
    register_item_type(ITEM_OAK_BUTTON, "minecraft:oak_button");
    register_item_type(ITEM_SPRUCE_BUTTON, "minecraft:spruce_button");
    register_item_type(ITEM_BIRCH_BUTTON, "minecraft:birch_button");
    register_item_type(ITEM_JUNGLE_BUTTON, "minecraft:jungle_button");
    register_item_type(ITEM_ACACIA_BUTTON, "minecraft:acacia_button");
    register_item_type(ITEM_DARK_OAK_BUTTON, "minecraft:dark_oak_button");
    register_item_type(ITEM_CRIMSON_BUTTON, "minecraft:crimson_button");
    register_item_type(ITEM_WARPED_BUTTON, "minecraft:warped_button");
    register_item_type(ITEM_POLISHED_BLACKSTONE_BUTTON, "minecraft:polished_blackstone_button");
    register_item_type(ITEM_ANVIL, "minecraft:anvil");
    register_item_type(ITEM_CHIPPED_ANVIL, "minecraft:chipped_anvil");
    register_item_type(ITEM_DAMAGED_ANVIL, "minecraft:damaged_anvil");
    register_item_type(ITEM_TRAPPED_CHEST, "minecraft:trapped_chest");
    register_item_type(ITEM_LIGHT_WEIGHTED_PRESSURE_PLATE, "minecraft:light_weighted_pressure_plate");
    register_item_type(ITEM_HEAVY_WEIGHTED_PRESSURE_PLATE, "minecraft:heavy_weighted_pressure_plate");
    register_item_type(ITEM_DAYLIGHT_DETECTOR, "minecraft:daylight_detector");
    register_item_type(ITEM_REDSTONE_BLOCK, "minecraft:redstone_block");
    register_item_type(ITEM_NETHER_QUARTZ_ORE, "minecraft:nether_quartz_ore");
    register_item_type(ITEM_HOPPER, "minecraft:hopper");
    register_item_type(ITEM_CHISELED_QUARTZ_BLOCK, "minecraft:chiseled_quartz_block");
    register_item_type(ITEM_QUARTZ_BLOCK, "minecraft:quartz_block");
    register_item_type(ITEM_QUARTZ_BRICKS, "minecraft:quartz_bricks");
    register_item_type(ITEM_QUARTZ_PILLAR, "minecraft:quartz_pillar");
    register_item_type(ITEM_QUARTZ_STAIRS, "minecraft:quartz_stairs");
    register_item_type(ITEM_ACTIVATOR_RAIL, "minecraft:activator_rail");
    register_item_type(ITEM_DROPPER, "minecraft:dropper");
    register_item_type(ITEM_WHITE_TERRACOTTA, "minecraft:white_terracotta");
    register_item_type(ITEM_ORANGE_TERRACOTTA, "minecraft:orange_terracotta");
    register_item_type(ITEM_MAGENTA_TERRACOTTA, "minecraft:magenta_terracotta");
    register_item_type(ITEM_LIGHT_BLUE_TERRACOTTA, "minecraft:light_blue_terracotta");
    register_item_type(ITEM_YELLOW_TERRACOTTA, "minecraft:yellow_terracotta");
    register_item_type(ITEM_LIME_TERRACOTTA, "minecraft:lime_terracotta");
    register_item_type(ITEM_PINK_TERRACOTTA, "minecraft:pink_terracotta");
    register_item_type(ITEM_GRAY_TERRACOTTA, "minecraft:gray_terracotta");
    register_item_type(ITEM_LIGHT_GRAY_TERRACOTTA, "minecraft:light_gray_terracotta");
    register_item_type(ITEM_CYAN_TERRACOTTA, "minecraft:cyan_terracotta");
    register_item_type(ITEM_PURPLE_TERRACOTTA, "minecraft:purple_terracotta");
    register_item_type(ITEM_BLUE_TERRACOTTA, "minecraft:blue_terracotta");
    register_item_type(ITEM_BROWN_TERRACOTTA, "minecraft:brown_terracotta");
    register_item_type(ITEM_GREEN_TERRACOTTA, "minecraft:green_terracotta");
    register_item_type(ITEM_RED_TERRACOTTA, "minecraft:red_terracotta");
    register_item_type(ITEM_BLACK_TERRACOTTA, "minecraft:black_terracotta");
    register_item_type(ITEM_BARRIER, "minecraft:barrier");
    register_item_type(ITEM_IRON_TRAPDOOR, "minecraft:iron_trapdoor");
    register_item_type(ITEM_HAY_BLOCK, "minecraft:hay_block");
    register_item_type(ITEM_WHITE_CARPET, "minecraft:white_carpet");
    register_item_type(ITEM_ORANGE_CARPET, "minecraft:orange_carpet");
    register_item_type(ITEM_MAGENTA_CARPET, "minecraft:magenta_carpet");
    register_item_type(ITEM_LIGHT_BLUE_CARPET, "minecraft:light_blue_carpet");
    register_item_type(ITEM_YELLOW_CARPET, "minecraft:yellow_carpet");
    register_item_type(ITEM_LIME_CARPET, "minecraft:lime_carpet");
    register_item_type(ITEM_PINK_CARPET, "minecraft:pink_carpet");
    register_item_type(ITEM_GRAY_CARPET, "minecraft:gray_carpet");
    register_item_type(ITEM_LIGHT_GRAY_CARPET, "minecraft:light_gray_carpet");
    register_item_type(ITEM_CYAN_CARPET, "minecraft:cyan_carpet");
    register_item_type(ITEM_PURPLE_CARPET, "minecraft:purple_carpet");
    register_item_type(ITEM_BLUE_CARPET, "minecraft:blue_carpet");
    register_item_type(ITEM_BROWN_CARPET, "minecraft:brown_carpet");
    register_item_type(ITEM_GREEN_CARPET, "minecraft:green_carpet");
    register_item_type(ITEM_RED_CARPET, "minecraft:red_carpet");
    register_item_type(ITEM_BLACK_CARPET, "minecraft:black_carpet");
    register_item_type(ITEM_TERRACOTTA, "minecraft:terracotta");
    register_item_type(ITEM_COAL_BLOCK, "minecraft:coal_block");
    register_item_type(ITEM_PACKED_ICE, "minecraft:packed_ice");
    register_item_type(ITEM_ACACIA_STAIRS, "minecraft:acacia_stairs");
    register_item_type(ITEM_DARK_OAK_STAIRS, "minecraft:dark_oak_stairs");
    register_item_type(ITEM_SLIME_BLOCK, "minecraft:slime_block");
    register_item_type(ITEM_GRASS_PATH, "minecraft:grass_path");
    register_item_type(ITEM_SUNFLOWER, "minecraft:sunflower");
    register_item_type(ITEM_LILAC, "minecraft:lilac");
    register_item_type(ITEM_ROSE_BUSH, "minecraft:rose_bush");
    register_item_type(ITEM_PEONY, "minecraft:peony");
    register_item_type(ITEM_TALL_GRASS, "minecraft:tall_grass");
    register_item_type(ITEM_LARGE_FERN, "minecraft:large_fern");
    register_item_type(ITEM_WHITE_STAINED_GLASS, "minecraft:white_stained_glass");
    register_item_type(ITEM_ORANGE_STAINED_GLASS, "minecraft:orange_stained_glass");
    register_item_type(ITEM_MAGENTA_STAINED_GLASS, "minecraft:magenta_stained_glass");
    register_item_type(ITEM_LIGHT_BLUE_STAINED_GLASS, "minecraft:light_blue_stained_glass");
    register_item_type(ITEM_YELLOW_STAINED_GLASS, "minecraft:yellow_stained_glass");
    register_item_type(ITEM_LIME_STAINED_GLASS, "minecraft:lime_stained_glass");
    register_item_type(ITEM_PINK_STAINED_GLASS, "minecraft:pink_stained_glass");
    register_item_type(ITEM_GRAY_STAINED_GLASS, "minecraft:gray_stained_glass");
    register_item_type(ITEM_LIGHT_GRAY_STAINED_GLASS, "minecraft:light_gray_stained_glass");
    register_item_type(ITEM_CYAN_STAINED_GLASS, "minecraft:cyan_stained_glass");
    register_item_type(ITEM_PURPLE_STAINED_GLASS, "minecraft:purple_stained_glass");
    register_item_type(ITEM_BLUE_STAINED_GLASS, "minecraft:blue_stained_glass");
    register_item_type(ITEM_BROWN_STAINED_GLASS, "minecraft:brown_stained_glass");
    register_item_type(ITEM_GREEN_STAINED_GLASS, "minecraft:green_stained_glass");
    register_item_type(ITEM_RED_STAINED_GLASS, "minecraft:red_stained_glass");
    register_item_type(ITEM_BLACK_STAINED_GLASS, "minecraft:black_stained_glass");
    register_item_type(ITEM_WHITE_STAINED_GLASS_PANE, "minecraft:white_stained_glass_pane");
    register_item_type(ITEM_ORANGE_STAINED_GLASS_PANE, "minecraft:orange_stained_glass_pane");
    register_item_type(ITEM_MAGENTA_STAINED_GLASS_PANE, "minecraft:magenta_stained_glass_pane");
    register_item_type(ITEM_LIGHT_BLUE_STAINED_GLASS_PANE, "minecraft:light_blue_stained_glass_pane");
    register_item_type(ITEM_YELLOW_STAINED_GLASS_PANE, "minecraft:yellow_stained_glass_pane");
    register_item_type(ITEM_LIME_STAINED_GLASS_PANE, "minecraft:lime_stained_glass_pane");
    register_item_type(ITEM_PINK_STAINED_GLASS_PANE, "minecraft:pink_stained_glass_pane");
    register_item_type(ITEM_GRAY_STAINED_GLASS_PANE, "minecraft:gray_stained_glass_pane");
    register_item_type(ITEM_LIGHT_GRAY_STAINED_GLASS_PANE, "minecraft:light_gray_stained_glass_pane");
    register_item_type(ITEM_CYAN_STAINED_GLASS_PANE, "minecraft:cyan_stained_glass_pane");
    register_item_type(ITEM_PURPLE_STAINED_GLASS_PANE, "minecraft:purple_stained_glass_pane");
    register_item_type(ITEM_BLUE_STAINED_GLASS_PANE, "minecraft:blue_stained_glass_pane");
    register_item_type(ITEM_BROWN_STAINED_GLASS_PANE, "minecraft:brown_stained_glass_pane");
    register_item_type(ITEM_GREEN_STAINED_GLASS_PANE, "minecraft:green_stained_glass_pane");
    register_item_type(ITEM_RED_STAINED_GLASS_PANE, "minecraft:red_stained_glass_pane");
    register_item_type(ITEM_BLACK_STAINED_GLASS_PANE, "minecraft:black_stained_glass_pane");
    register_item_type(ITEM_PRISMARINE, "minecraft:prismarine");
    register_item_type(ITEM_PRISMARINE_BRICKS, "minecraft:prismarine_bricks");
    register_item_type(ITEM_DARK_PRISMARINE, "minecraft:dark_prismarine");
    register_item_type(ITEM_PRISMARINE_STAIRS, "minecraft:prismarine_stairs");
    register_item_type(ITEM_PRISMARINE_BRICK_STAIRS, "minecraft:prismarine_brick_stairs");
    register_item_type(ITEM_DARK_PRISMARINE_STAIRS, "minecraft:dark_prismarine_stairs");
    register_item_type(ITEM_SEA_LANTERN, "minecraft:sea_lantern");
    register_item_type(ITEM_RED_SANDSTONE, "minecraft:red_sandstone");
    register_item_type(ITEM_CHISELED_RED_SANDSTONE, "minecraft:chiseled_red_sandstone");
    register_item_type(ITEM_CUT_RED_SANDSTONE, "minecraft:cut_red_sandstone");
    register_item_type(ITEM_RED_SANDSTONE_STAIRS, "minecraft:red_sandstone_stairs");
    register_item_type(ITEM_REPEATING_COMMAND_BLOCK, "minecraft:repeating_command_block");
    register_item_type(ITEM_CHAIN_COMMAND_BLOCK, "minecraft:chain_command_block");
    register_item_type(ITEM_MAGMA_BLOCK, "minecraft:magma_block");
    register_item_type(ITEM_NETHER_WART_BLOCK, "minecraft:nether_wart_block");
    register_item_type(ITEM_WARPED_WART_BLOCK, "minecraft:warped_wart_block");
    register_item_type(ITEM_RED_NETHER_BRICKS, "minecraft:red_nether_bricks");
    register_item_type(ITEM_BONE_BLOCK, "minecraft:bone_block");
    register_item_type(ITEM_STRUCTURE_VOID, "minecraft:structure_void");
    register_item_type(ITEM_OBSERVER, "minecraft:observer");
    register_item_type(ITEM_SHULKER_BOX, "minecraft:shulker_box");
    register_item_type(ITEM_WHITE_SHULKER_BOX, "minecraft:white_shulker_box");
    register_item_type(ITEM_ORANGE_SHULKER_BOX, "minecraft:orange_shulker_box");
    register_item_type(ITEM_MAGENTA_SHULKER_BOX, "minecraft:magenta_shulker_box");
    register_item_type(ITEM_LIGHT_BLUE_SHULKER_BOX, "minecraft:light_blue_shulker_box");
    register_item_type(ITEM_YELLOW_SHULKER_BOX, "minecraft:yellow_shulker_box");
    register_item_type(ITEM_LIME_SHULKER_BOX, "minecraft:lime_shulker_box");
    register_item_type(ITEM_PINK_SHULKER_BOX, "minecraft:pink_shulker_box");
    register_item_type(ITEM_GRAY_SHULKER_BOX, "minecraft:gray_shulker_box");
    register_item_type(ITEM_LIGHT_GRAY_SHULKER_BOX, "minecraft:light_gray_shulker_box");
    register_item_type(ITEM_CYAN_SHULKER_BOX, "minecraft:cyan_shulker_box");
    register_item_type(ITEM_PURPLE_SHULKER_BOX, "minecraft:purple_shulker_box");
    register_item_type(ITEM_BLUE_SHULKER_BOX, "minecraft:blue_shulker_box");
    register_item_type(ITEM_BROWN_SHULKER_BOX, "minecraft:brown_shulker_box");
    register_item_type(ITEM_GREEN_SHULKER_BOX, "minecraft:green_shulker_box");
    register_item_type(ITEM_RED_SHULKER_BOX, "minecraft:red_shulker_box");
    register_item_type(ITEM_BLACK_SHULKER_BOX, "minecraft:black_shulker_box");
    register_item_type(ITEM_WHITE_GLAZED_TERRACOTTA, "minecraft:white_glazed_terracotta");
    register_item_type(ITEM_ORANGE_GLAZED_TERRACOTTA, "minecraft:orange_glazed_terracotta");
    register_item_type(ITEM_MAGENTA_GLAZED_TERRACOTTA, "minecraft:magenta_glazed_terracotta");
    register_item_type(ITEM_LIGHT_BLUE_GLAZED_TERRACOTTA, "minecraft:light_blue_glazed_terracotta");
    register_item_type(ITEM_YELLOW_GLAZED_TERRACOTTA, "minecraft:yellow_glazed_terracotta");
    register_item_type(ITEM_LIME_GLAZED_TERRACOTTA, "minecraft:lime_glazed_terracotta");
    register_item_type(ITEM_PINK_GLAZED_TERRACOTTA, "minecraft:pink_glazed_terracotta");
    register_item_type(ITEM_GRAY_GLAZED_TERRACOTTA, "minecraft:gray_glazed_terracotta");
    register_item_type(ITEM_LIGHT_GRAY_GLAZED_TERRACOTTA, "minecraft:light_gray_glazed_terracotta");
    register_item_type(ITEM_CYAN_GLAZED_TERRACOTTA, "minecraft:cyan_glazed_terracotta");
    register_item_type(ITEM_PURPLE_GLAZED_TERRACOTTA, "minecraft:purple_glazed_terracotta");
    register_item_type(ITEM_BLUE_GLAZED_TERRACOTTA, "minecraft:blue_glazed_terracotta");
    register_item_type(ITEM_BROWN_GLAZED_TERRACOTTA, "minecraft:brown_glazed_terracotta");
    register_item_type(ITEM_GREEN_GLAZED_TERRACOTTA, "minecraft:green_glazed_terracotta");
    register_item_type(ITEM_RED_GLAZED_TERRACOTTA, "minecraft:red_glazed_terracotta");
    register_item_type(ITEM_BLACK_GLAZED_TERRACOTTA, "minecraft:black_glazed_terracotta");
    register_item_type(ITEM_WHITE_CONCRETE, "minecraft:white_concrete");
    register_item_type(ITEM_ORANGE_CONCRETE, "minecraft:orange_concrete");
    register_item_type(ITEM_MAGENTA_CONCRETE, "minecraft:magenta_concrete");
    register_item_type(ITEM_LIGHT_BLUE_CONCRETE, "minecraft:light_blue_concrete");
    register_item_type(ITEM_YELLOW_CONCRETE, "minecraft:yellow_concrete");
    register_item_type(ITEM_LIME_CONCRETE, "minecraft:lime_concrete");
    register_item_type(ITEM_PINK_CONCRETE, "minecraft:pink_concrete");
    register_item_type(ITEM_GRAY_CONCRETE, "minecraft:gray_concrete");
    register_item_type(ITEM_LIGHT_GRAY_CONCRETE, "minecraft:light_gray_concrete");
    register_item_type(ITEM_CYAN_CONCRETE, "minecraft:cyan_concrete");
    register_item_type(ITEM_PURPLE_CONCRETE, "minecraft:purple_concrete");
    register_item_type(ITEM_BLUE_CONCRETE, "minecraft:blue_concrete");
    register_item_type(ITEM_BROWN_CONCRETE, "minecraft:brown_concrete");
    register_item_type(ITEM_GREEN_CONCRETE, "minecraft:green_concrete");
    register_item_type(ITEM_RED_CONCRETE, "minecraft:red_concrete");
    register_item_type(ITEM_BLACK_CONCRETE, "minecraft:black_concrete");
    register_item_type(ITEM_WHITE_CONCRETE_POWDER, "minecraft:white_concrete_powder");
    register_item_type(ITEM_ORANGE_CONCRETE_POWDER, "minecraft:orange_concrete_powder");
    register_item_type(ITEM_MAGENTA_CONCRETE_POWDER, "minecraft:magenta_concrete_powder");
    register_item_type(ITEM_LIGHT_BLUE_CONCRETE_POWDER, "minecraft:light_blue_concrete_powder");
    register_item_type(ITEM_YELLOW_CONCRETE_POWDER, "minecraft:yellow_concrete_powder");
    register_item_type(ITEM_LIME_CONCRETE_POWDER, "minecraft:lime_concrete_powder");
    register_item_type(ITEM_PINK_CONCRETE_POWDER, "minecraft:pink_concrete_powder");
    register_item_type(ITEM_GRAY_CONCRETE_POWDER, "minecraft:gray_concrete_powder");
    register_item_type(ITEM_LIGHT_GRAY_CONCRETE_POWDER, "minecraft:light_gray_concrete_powder");
    register_item_type(ITEM_CYAN_CONCRETE_POWDER, "minecraft:cyan_concrete_powder");
    register_item_type(ITEM_PURPLE_CONCRETE_POWDER, "minecraft:purple_concrete_powder");
    register_item_type(ITEM_BLUE_CONCRETE_POWDER, "minecraft:blue_concrete_powder");
    register_item_type(ITEM_BROWN_CONCRETE_POWDER, "minecraft:brown_concrete_powder");
    register_item_type(ITEM_GREEN_CONCRETE_POWDER, "minecraft:green_concrete_powder");
    register_item_type(ITEM_RED_CONCRETE_POWDER, "minecraft:red_concrete_powder");
    register_item_type(ITEM_BLACK_CONCRETE_POWDER, "minecraft:black_concrete_powder");
    register_item_type(ITEM_TURTLE_EGG, "minecraft:turtle_egg");
    register_item_type(ITEM_DEAD_TUBE_CORAL_BLOCK, "minecraft:dead_tube_coral_block");
    register_item_type(ITEM_DEAD_BRAIN_CORAL_BLOCK, "minecraft:dead_brain_coral_block");
    register_item_type(ITEM_DEAD_BUBBLE_CORAL_BLOCK, "minecraft:dead_bubble_coral_block");
    register_item_type(ITEM_DEAD_FIRE_CORAL_BLOCK, "minecraft:dead_fire_coral_block");
    register_item_type(ITEM_DEAD_HORN_CORAL_BLOCK, "minecraft:dead_horn_coral_block");
    register_item_type(ITEM_TUBE_CORAL_BLOCK, "minecraft:tube_coral_block");
    register_item_type(ITEM_BRAIN_CORAL_BLOCK, "minecraft:brain_coral_block");
    register_item_type(ITEM_BUBBLE_CORAL_BLOCK, "minecraft:bubble_coral_block");
    register_item_type(ITEM_FIRE_CORAL_BLOCK, "minecraft:fire_coral_block");
    register_item_type(ITEM_HORN_CORAL_BLOCK, "minecraft:horn_coral_block");
    register_item_type(ITEM_TUBE_CORAL, "minecraft:tube_coral");
    register_item_type(ITEM_BRAIN_CORAL, "minecraft:brain_coral");
    register_item_type(ITEM_BUBBLE_CORAL, "minecraft:bubble_coral");
    register_item_type(ITEM_FIRE_CORAL, "minecraft:fire_coral");
    register_item_type(ITEM_HORN_CORAL, "minecraft:horn_coral");
    register_item_type(ITEM_DEAD_BRAIN_CORAL, "minecraft:dead_brain_coral");
    register_item_type(ITEM_DEAD_BUBBLE_CORAL, "minecraft:dead_bubble_coral");
    register_item_type(ITEM_DEAD_FIRE_CORAL, "minecraft:dead_fire_coral");
    register_item_type(ITEM_DEAD_HORN_CORAL, "minecraft:dead_horn_coral");
    register_item_type(ITEM_DEAD_TUBE_CORAL, "minecraft:dead_tube_coral");
    register_item_type(ITEM_TUBE_CORAL_FAN, "minecraft:tube_coral_fan");
    register_item_type(ITEM_BRAIN_CORAL_FAN, "minecraft:brain_coral_fan");
    register_item_type(ITEM_BUBBLE_CORAL_FAN, "minecraft:bubble_coral_fan");
    register_item_type(ITEM_FIRE_CORAL_FAN, "minecraft:fire_coral_fan");
    register_item_type(ITEM_HORN_CORAL_FAN, "minecraft:horn_coral_fan");
    register_item_type(ITEM_DEAD_TUBE_CORAL_FAN, "minecraft:dead_tube_coral_fan");
    register_item_type(ITEM_DEAD_BRAIN_CORAL_FAN, "minecraft:dead_brain_coral_fan");
    register_item_type(ITEM_DEAD_BUBBLE_CORAL_FAN, "minecraft:dead_bubble_coral_fan");
    register_item_type(ITEM_DEAD_FIRE_CORAL_FAN, "minecraft:dead_fire_coral_fan");
    register_item_type(ITEM_DEAD_HORN_CORAL_FAN, "minecraft:dead_horn_coral_fan");
    register_item_type(ITEM_BLUE_ICE, "minecraft:blue_ice");
    register_item_type(ITEM_CONDUIT, "minecraft:conduit");
    register_item_type(ITEM_POLISHED_GRANITE_STAIRS, "minecraft:polished_granite_stairs");
    register_item_type(ITEM_SMOOTH_RED_SANDSTONE_STAIRS, "minecraft:smooth_red_sandstone_stairs");
    register_item_type(ITEM_MOSSY_STONE_BRICK_STAIRS, "minecraft:mossy_stone_brick_stairs");
    register_item_type(ITEM_POLISHED_DIORITE_STAIRS, "minecraft:polished_diorite_stairs");
    register_item_type(ITEM_MOSSY_COBBLESTONE_STAIRS, "minecraft:mossy_cobblestone_stairs");
    register_item_type(ITEM_END_STONE_BRICK_STAIRS, "minecraft:end_stone_brick_stairs");
    register_item_type(ITEM_STONE_STAIRS, "minecraft:stone_stairs");
    register_item_type(ITEM_SMOOTH_SANDSTONE_STAIRS, "minecraft:smooth_sandstone_stairs");
    register_item_type(ITEM_SMOOTH_QUARTZ_STAIRS, "minecraft:smooth_quartz_stairs");
    register_item_type(ITEM_GRANITE_STAIRS, "minecraft:granite_stairs");
    register_item_type(ITEM_ANDESITE_STAIRS, "minecraft:andesite_stairs");
    register_item_type(ITEM_RED_NETHER_BRICK_STAIRS, "minecraft:red_nether_brick_stairs");
    register_item_type(ITEM_POLISHED_ANDESITE_STAIRS, "minecraft:polished_andesite_stairs");
    register_item_type(ITEM_DIORITE_STAIRS, "minecraft:diorite_stairs");
    register_item_type(ITEM_POLISHED_GRANITE_SLAB, "minecraft:polished_granite_slab");
    register_item_type(ITEM_SMOOTH_RED_SANDSTONE_SLAB, "minecraft:smooth_red_sandstone_slab");
    register_item_type(ITEM_MOSSY_STONE_BRICK_SLAB, "minecraft:mossy_stone_brick_slab");
    register_item_type(ITEM_POLISHED_DIORITE_SLAB, "minecraft:polished_diorite_slab");
    register_item_type(ITEM_MOSSY_COBBLESTONE_SLAB, "minecraft:mossy_cobblestone_slab");
    register_item_type(ITEM_END_STONE_BRICK_SLAB, "minecraft:end_stone_brick_slab");
    register_item_type(ITEM_SMOOTH_SANDSTONE_SLAB, "minecraft:smooth_sandstone_slab");
    register_item_type(ITEM_SMOOTH_QUARTZ_SLAB, "minecraft:smooth_quartz_slab");
    register_item_type(ITEM_GRANITE_SLAB, "minecraft:granite_slab");
    register_item_type(ITEM_ANDESITE_SLAB, "minecraft:andesite_slab");
    register_item_type(ITEM_RED_NETHER_BRICK_SLAB, "minecraft:red_nether_brick_slab");
    register_item_type(ITEM_POLISHED_ANDESITE_SLAB, "minecraft:polished_andesite_slab");
    register_item_type(ITEM_DIORITE_SLAB, "minecraft:diorite_slab");
    register_item_type(ITEM_SCAFFOLDING, "minecraft:scaffolding");
    register_item_type(ITEM_IRON_DOOR, "minecraft:iron_door");
    register_item_type(ITEM_OAK_DOOR, "minecraft:oak_door");
    register_item_type(ITEM_SPRUCE_DOOR, "minecraft:spruce_door");
    register_item_type(ITEM_BIRCH_DOOR, "minecraft:birch_door");
    register_item_type(ITEM_JUNGLE_DOOR, "minecraft:jungle_door");
    register_item_type(ITEM_ACACIA_DOOR, "minecraft:acacia_door");
    register_item_type(ITEM_DARK_OAK_DOOR, "minecraft:dark_oak_door");
    register_item_type(ITEM_CRIMSON_DOOR, "minecraft:crimson_door");
    register_item_type(ITEM_WARPED_DOOR, "minecraft:warped_door");
    register_item_type(ITEM_REPEATER, "minecraft:repeater");
    register_item_type(ITEM_COMPARATOR, "minecraft:comparator");
    register_item_type(ITEM_STRUCTURE_BLOCK, "minecraft:structure_block");
    register_item_type(ITEM_JIGSAW, "minecraft:jigsaw");
    register_item_type(ITEM_TURTLE_HELMET, "minecraft:turtle_helmet");
    register_item_type(ITEM_SCUTE, "minecraft:scute");
    register_item_type(ITEM_FLINT_AND_STEEL, "minecraft:flint_and_steel");
    register_item_type(ITEM_APPLE, "minecraft:apple");
    register_item_type(ITEM_BOW, "minecraft:bow");
    register_item_type(ITEM_ARROW, "minecraft:arrow");
    register_item_type(ITEM_COAL, "minecraft:coal");
    register_item_type(ITEM_CHARCOAL, "minecraft:charcoal");
    register_item_type(ITEM_DIAMOND, "minecraft:diamond");
    register_item_type(ITEM_IRON_INGOT, "minecraft:iron_ingot");
    register_item_type(ITEM_GOLD_INGOT, "minecraft:gold_ingot");
    register_item_type(ITEM_NETHERITE_INGOT, "minecraft:netherite_ingot");
    register_item_type(ITEM_NETHERITE_SCRAP, "minecraft:netherite_scrap");
    register_item_type(ITEM_WOODEN_SWORD, "minecraft:wooden_sword");
    register_item_type(ITEM_WOODEN_SHOVEL, "minecraft:wooden_shovel");
    register_item_type(ITEM_WOODEN_PICKAXE, "minecraft:wooden_pickaxe");
    register_item_type(ITEM_WOODEN_AXE, "minecraft:wooden_axe");
    register_item_type(ITEM_WOODEN_HOE, "minecraft:wooden_hoe");
    register_item_type(ITEM_STONE_SWORD, "minecraft:stone_sword");
    register_item_type(ITEM_STONE_SHOVEL, "minecraft:stone_shovel");
    register_item_type(ITEM_STONE_PICKAXE, "minecraft:stone_pickaxe");
    register_item_type(ITEM_STONE_AXE, "minecraft:stone_axe");
    register_item_type(ITEM_STONE_HOE, "minecraft:stone_hoe");
    register_item_type(ITEM_GOLDEN_SWORD, "minecraft:golden_sword");
    register_item_type(ITEM_GOLDEN_SHOVEL, "minecraft:golden_shovel");
    register_item_type(ITEM_GOLDEN_PICKAXE, "minecraft:golden_pickaxe");
    register_item_type(ITEM_GOLDEN_AXE, "minecraft:golden_axe");
    register_item_type(ITEM_GOLDEN_HOE, "minecraft:golden_hoe");
    register_item_type(ITEM_IRON_SWORD, "minecraft:iron_sword");
    register_item_type(ITEM_IRON_SHOVEL, "minecraft:iron_shovel");
    register_item_type(ITEM_IRON_PICKAXE, "minecraft:iron_pickaxe");
    register_item_type(ITEM_IRON_AXE, "minecraft:iron_axe");
    register_item_type(ITEM_IRON_HOE, "minecraft:iron_hoe");
    register_item_type(ITEM_DIAMOND_SWORD, "minecraft:diamond_sword");
    register_item_type(ITEM_DIAMOND_SHOVEL, "minecraft:diamond_shovel");
    register_item_type(ITEM_DIAMOND_PICKAXE, "minecraft:diamond_pickaxe");
    register_item_type(ITEM_DIAMOND_AXE, "minecraft:diamond_axe");
    register_item_type(ITEM_DIAMOND_HOE, "minecraft:diamond_hoe");
    register_item_type(ITEM_NETHERITE_SWORD, "minecraft:netherite_sword");
    register_item_type(ITEM_NETHERITE_SHOVEL, "minecraft:netherite_shovel");
    register_item_type(ITEM_NETHERITE_PICKAXE, "minecraft:netherite_pickaxe");
    register_item_type(ITEM_NETHERITE_AXE, "minecraft:netherite_axe");
    register_item_type(ITEM_NETHERITE_HOE, "minecraft:netherite_hoe");
    register_item_type(ITEM_STICK, "minecraft:stick");
    register_item_type(ITEM_BOWL, "minecraft:bowl");
    register_item_type(ITEM_MUSHROOM_STEW, "minecraft:mushroom_stew");
    register_item_type(ITEM_STRING, "minecraft:string");
    register_item_type(ITEM_FEATHER, "minecraft:feather");
    register_item_type(ITEM_GUNPOWDER, "minecraft:gunpowder");
    register_item_type(ITEM_WHEAT_SEEDS, "minecraft:wheat_seeds");
    register_item_type(ITEM_WHEAT, "minecraft:wheat");
    register_item_type(ITEM_BREAD, "minecraft:bread");
    register_item_type(ITEM_LEATHER_HELMET, "minecraft:leather_helmet");
    register_item_type(ITEM_LEATHER_CHESTPLATE, "minecraft:leather_chestplate");
    register_item_type(ITEM_LEATHER_LEGGINGS, "minecraft:leather_leggings");
    register_item_type(ITEM_LEATHER_BOOTS, "minecraft:leather_boots");
    register_item_type(ITEM_CHAINMAIL_HELMET, "minecraft:chainmail_helmet");
    register_item_type(ITEM_CHAINMAIL_CHESTPLATE, "minecraft:chainmail_chestplate");
    register_item_type(ITEM_CHAINMAIL_LEGGINGS, "minecraft:chainmail_leggings");
    register_item_type(ITEM_CHAINMAIL_BOOTS, "minecraft:chainmail_boots");
    register_item_type(ITEM_IRON_HELMET, "minecraft:iron_helmet");
    register_item_type(ITEM_IRON_CHESTPLATE, "minecraft:iron_chestplate");
    register_item_type(ITEM_IRON_LEGGINGS, "minecraft:iron_leggings");
    register_item_type(ITEM_IRON_BOOTS, "minecraft:iron_boots");
    register_item_type(ITEM_DIAMOND_HELMET, "minecraft:diamond_helmet");
    register_item_type(ITEM_DIAMOND_CHESTPLATE, "minecraft:diamond_chestplate");
    register_item_type(ITEM_DIAMOND_LEGGINGS, "minecraft:diamond_leggings");
    register_item_type(ITEM_DIAMOND_BOOTS, "minecraft:diamond_boots");
    register_item_type(ITEM_GOLDEN_HELMET, "minecraft:golden_helmet");
    register_item_type(ITEM_GOLDEN_CHESTPLATE, "minecraft:golden_chestplate");
    register_item_type(ITEM_GOLDEN_LEGGINGS, "minecraft:golden_leggings");
    register_item_type(ITEM_GOLDEN_BOOTS, "minecraft:golden_boots");
    register_item_type(ITEM_NETHERITE_HELMET, "minecraft:netherite_helmet");
    register_item_type(ITEM_NETHERITE_CHESTPLATE, "minecraft:netherite_chestplate");
    register_item_type(ITEM_NETHERITE_LEGGINGS, "minecraft:netherite_leggings");
    register_item_type(ITEM_NETHERITE_BOOTS, "minecraft:netherite_boots");
    register_item_type(ITEM_FLINT, "minecraft:flint");
    register_item_type(ITEM_PORKCHOP, "minecraft:porkchop");
    register_item_type(ITEM_COOKED_PORKCHOP, "minecraft:cooked_porkchop");
    register_item_type(ITEM_PAINTING, "minecraft:painting");
    register_item_type(ITEM_GOLDEN_APPLE, "minecraft:golden_apple");
    register_item_type(ITEM_ENCHANTED_GOLDEN_APPLE, "minecraft:enchanted_golden_apple");
    register_item_type(ITEM_OAK_SIGN, "minecraft:oak_sign");
    register_item_type(ITEM_SPRUCE_SIGN, "minecraft:spruce_sign");
    register_item_type(ITEM_BIRCH_SIGN, "minecraft:birch_sign");
    register_item_type(ITEM_JUNGLE_SIGN, "minecraft:jungle_sign");
    register_item_type(ITEM_ACACIA_SIGN, "minecraft:acacia_sign");
    register_item_type(ITEM_DARK_OAK_SIGN, "minecraft:dark_oak_sign");
    register_item_type(ITEM_CRIMSON_SIGN, "minecraft:crimson_sign");
    register_item_type(ITEM_WARPED_SIGN, "minecraft:warped_sign");
    register_item_type(ITEM_BUCKET, "minecraft:bucket");
    register_item_type(ITEM_WATER_BUCKET, "minecraft:water_bucket");
    register_item_type(ITEM_LAVA_BUCKET, "minecraft:lava_bucket");
    register_item_type(ITEM_MINECART, "minecraft:minecart");
    register_item_type(ITEM_SADDLE, "minecraft:saddle");
    register_item_type(ITEM_REDSTONE, "minecraft:redstone");
    register_item_type(ITEM_SNOWBALL, "minecraft:snowball");
    register_item_type(ITEM_OAK_BOAT, "minecraft:oak_boat");
    register_item_type(ITEM_LEATHER, "minecraft:leather");
    register_item_type(ITEM_MILK_BUCKET, "minecraft:milk_bucket");
    register_item_type(ITEM_PUFFERFISH_BUCKET, "minecraft:pufferfish_bucket");
    register_item_type(ITEM_SALMON_BUCKET, "minecraft:salmon_bucket");
    register_item_type(ITEM_COD_BUCKET, "minecraft:cod_bucket");
    register_item_type(ITEM_TROPICAL_FISH_BUCKET, "minecraft:tropical_fish_bucket");
    register_item_type(ITEM_BRICK, "minecraft:brick");
    register_item_type(ITEM_CLAY_BALL, "minecraft:clay_ball");
    register_item_type(ITEM_DRIED_KELP_BLOCK, "minecraft:dried_kelp_block");
    register_item_type(ITEM_PAPER, "minecraft:paper");
    register_item_type(ITEM_BOOK, "minecraft:book");
    register_item_type(ITEM_SLIME_BALL, "minecraft:slime_ball");
    register_item_type(ITEM_CHEST_MINECART, "minecraft:chest_minecart");
    register_item_type(ITEM_FURNACE_MINECART, "minecraft:furnace_minecart");
    register_item_type(ITEM_EGG, "minecraft:egg");
    register_item_type(ITEM_COMPASS, "minecraft:compass");
    register_item_type(ITEM_FISHING_ROD, "minecraft:fishing_rod");
    register_item_type(ITEM_CLOCK, "minecraft:clock");
    register_item_type(ITEM_GLOWSTONE_DUST, "minecraft:glowstone_dust");
    register_item_type(ITEM_COD, "minecraft:cod");
    register_item_type(ITEM_SALMON, "minecraft:salmon");
    register_item_type(ITEM_TROPICAL_FISH, "minecraft:tropical_fish");
    register_item_type(ITEM_PUFFERFISH, "minecraft:pufferfish");
    register_item_type(ITEM_COOKED_COD, "minecraft:cooked_cod");
    register_item_type(ITEM_COOKED_SALMON, "minecraft:cooked_salmon");
    register_item_type(ITEM_INK_SAC, "minecraft:ink_sac");
    register_item_type(ITEM_COCOA_BEANS, "minecraft:cocoa_beans");
    register_item_type(ITEM_LAPIS_LAZULI, "minecraft:lapis_lazuli");
    register_item_type(ITEM_WHITE_DYE, "minecraft:white_dye");
    register_item_type(ITEM_ORANGE_DYE, "minecraft:orange_dye");
    register_item_type(ITEM_MAGENTA_DYE, "minecraft:magenta_dye");
    register_item_type(ITEM_LIGHT_BLUE_DYE, "minecraft:light_blue_dye");
    register_item_type(ITEM_YELLOW_DYE, "minecraft:yellow_dye");
    register_item_type(ITEM_LIME_DYE, "minecraft:lime_dye");
    register_item_type(ITEM_PINK_DYE, "minecraft:pink_dye");
    register_item_type(ITEM_GRAY_DYE, "minecraft:gray_dye");
    register_item_type(ITEM_LIGHT_GRAY_DYE, "minecraft:light_gray_dye");
    register_item_type(ITEM_CYAN_DYE, "minecraft:cyan_dye");
    register_item_type(ITEM_PURPLE_DYE, "minecraft:purple_dye");
    register_item_type(ITEM_BLUE_DYE, "minecraft:blue_dye");
    register_item_type(ITEM_BROWN_DYE, "minecraft:brown_dye");
    register_item_type(ITEM_GREEN_DYE, "minecraft:green_dye");
    register_item_type(ITEM_RED_DYE, "minecraft:red_dye");
    register_item_type(ITEM_BLACK_DYE, "minecraft:black_dye");
    register_item_type(ITEM_BONE_MEAL, "minecraft:bone_meal");
    register_item_type(ITEM_BONE, "minecraft:bone");
    register_item_type(ITEM_SUGAR, "minecraft:sugar");
    register_item_type(ITEM_CAKE, "minecraft:cake");
    register_item_type(ITEM_WHITE_BED, "minecraft:white_bed");
    register_item_type(ITEM_ORANGE_BED, "minecraft:orange_bed");
    register_item_type(ITEM_MAGENTA_BED, "minecraft:magenta_bed");
    register_item_type(ITEM_LIGHT_BLUE_BED, "minecraft:light_blue_bed");
    register_item_type(ITEM_YELLOW_BED, "minecraft:yellow_bed");
    register_item_type(ITEM_LIME_BED, "minecraft:lime_bed");
    register_item_type(ITEM_PINK_BED, "minecraft:pink_bed");
    register_item_type(ITEM_GRAY_BED, "minecraft:gray_bed");
    register_item_type(ITEM_LIGHT_GRAY_BED, "minecraft:light_gray_bed");
    register_item_type(ITEM_CYAN_BED, "minecraft:cyan_bed");
    register_item_type(ITEM_PURPLE_BED, "minecraft:purple_bed");
    register_item_type(ITEM_BLUE_BED, "minecraft:blue_bed");
    register_item_type(ITEM_BROWN_BED, "minecraft:brown_bed");
    register_item_type(ITEM_GREEN_BED, "minecraft:green_bed");
    register_item_type(ITEM_RED_BED, "minecraft:red_bed");
    register_item_type(ITEM_BLACK_BED, "minecraft:black_bed");
    register_item_type(ITEM_COOKIE, "minecraft:cookie");
    register_item_type(ITEM_FILLED_MAP, "minecraft:filled_map");
    register_item_type(ITEM_SHEARS, "minecraft:shears");
    register_item_type(ITEM_MELON_SLICE, "minecraft:melon_slice");
    register_item_type(ITEM_DRIED_KELP, "minecraft:dried_kelp");
    register_item_type(ITEM_PUMPKIN_SEEDS, "minecraft:pumpkin_seeds");
    register_item_type(ITEM_MELON_SEEDS, "minecraft:melon_seeds");
    register_item_type(ITEM_BEEF, "minecraft:beef");
    register_item_type(ITEM_COOKED_BEEF, "minecraft:cooked_beef");
    register_item_type(ITEM_CHICKEN, "minecraft:chicken");
    register_item_type(ITEM_COOKED_CHICKEN, "minecraft:cooked_chicken");
    register_item_type(ITEM_ROTTEN_FLESH, "minecraft:rotten_flesh");
    register_item_type(ITEM_ENDER_PEARL, "minecraft:ender_pearl");
    register_item_type(ITEM_BLAZE_ROD, "minecraft:blaze_rod");
    register_item_type(ITEM_GHAST_TEAR, "minecraft:ghast_tear");
    register_item_type(ITEM_GOLD_NUGGET, "minecraft:gold_nugget");
    register_item_type(ITEM_NETHER_WART, "minecraft:nether_wart");
    register_item_type(ITEM_POTION, "minecraft:potion");
    register_item_type(ITEM_GLASS_BOTTLE, "minecraft:glass_bottle");
    register_item_type(ITEM_SPIDER_EYE, "minecraft:spider_eye");
    register_item_type(ITEM_FERMENTED_SPIDER_EYE, "minecraft:fermented_spider_eye");
    register_item_type(ITEM_BLAZE_POWDER, "minecraft:blaze_powder");
    register_item_type(ITEM_MAGMA_CREAM, "minecraft:magma_cream");
    register_item_type(ITEM_BREWING_STAND, "minecraft:brewing_stand");
    register_item_type(ITEM_CAULDRON, "minecraft:cauldron");
    register_item_type(ITEM_ENDER_EYE, "minecraft:ender_eye");
    register_item_type(ITEM_GLISTERING_MELON_SLICE, "minecraft:glistering_melon_slice");
    register_item_type(ITEM_BAT_SPAWN_EGG, "minecraft:bat_spawn_egg");
    register_item_type(ITEM_BEE_SPAWN_EGG, "minecraft:bee_spawn_egg");
    register_item_type(ITEM_BLAZE_SPAWN_EGG, "minecraft:blaze_spawn_egg");
    register_item_type(ITEM_CAT_SPAWN_EGG, "minecraft:cat_spawn_egg");
    register_item_type(ITEM_CAVE_SPIDER_SPAWN_EGG, "minecraft:cave_spider_spawn_egg");
    register_item_type(ITEM_CHICKEN_SPAWN_EGG, "minecraft:chicken_spawn_egg");
    register_item_type(ITEM_COD_SPAWN_EGG, "minecraft:cod_spawn_egg");
    register_item_type(ITEM_COW_SPAWN_EGG, "minecraft:cow_spawn_egg");
    register_item_type(ITEM_CREEPER_SPAWN_EGG, "minecraft:creeper_spawn_egg");
    register_item_type(ITEM_DOLPHIN_SPAWN_EGG, "minecraft:dolphin_spawn_egg");
    register_item_type(ITEM_DONKEY_SPAWN_EGG, "minecraft:donkey_spawn_egg");
    register_item_type(ITEM_DROWNED_SPAWN_EGG, "minecraft:drowned_spawn_egg");
    register_item_type(ITEM_ELDER_GUARDIAN_SPAWN_EGG, "minecraft:elder_guardian_spawn_egg");
    register_item_type(ITEM_ENDERMAN_SPAWN_EGG, "minecraft:enderman_spawn_egg");
    register_item_type(ITEM_ENDERMITE_SPAWN_EGG, "minecraft:endermite_spawn_egg");
    register_item_type(ITEM_EVOKER_SPAWN_EGG, "minecraft:evoker_spawn_egg");
    register_item_type(ITEM_FOX_SPAWN_EGG, "minecraft:fox_spawn_egg");
    register_item_type(ITEM_GHAST_SPAWN_EGG, "minecraft:ghast_spawn_egg");
    register_item_type(ITEM_GUARDIAN_SPAWN_EGG, "minecraft:guardian_spawn_egg");
    register_item_type(ITEM_HOGLIN_SPAWN_EGG, "minecraft:hoglin_spawn_egg");
    register_item_type(ITEM_HORSE_SPAWN_EGG, "minecraft:horse_spawn_egg");
    register_item_type(ITEM_HUSK_SPAWN_EGG, "minecraft:husk_spawn_egg");
    register_item_type(ITEM_LLAMA_SPAWN_EGG, "minecraft:llama_spawn_egg");
    register_item_type(ITEM_MAGMA_CUBE_SPAWN_EGG, "minecraft:magma_cube_spawn_egg");
    register_item_type(ITEM_MOOSHROOM_SPAWN_EGG, "minecraft:mooshroom_spawn_egg");
    register_item_type(ITEM_MULE_SPAWN_EGG, "minecraft:mule_spawn_egg");
    register_item_type(ITEM_OCELOT_SPAWN_EGG, "minecraft:ocelot_spawn_egg");
    register_item_type(ITEM_PANDA_SPAWN_EGG, "minecraft:panda_spawn_egg");
    register_item_type(ITEM_PARROT_SPAWN_EGG, "minecraft:parrot_spawn_egg");
    register_item_type(ITEM_PHANTOM_SPAWN_EGG, "minecraft:phantom_spawn_egg");
    register_item_type(ITEM_PIG_SPAWN_EGG, "minecraft:pig_spawn_egg");
    register_item_type(ITEM_PIGLIN_SPAWN_EGG, "minecraft:piglin_spawn_egg");
    register_item_type(ITEM_PIGLIN_BRUTE_SPAWN_EGG, "minecraft:piglin_brute_spawn_egg");
    register_item_type(ITEM_PILLAGER_SPAWN_EGG, "minecraft:pillager_spawn_egg");
    register_item_type(ITEM_POLAR_BEAR_SPAWN_EGG, "minecraft:polar_bear_spawn_egg");
    register_item_type(ITEM_PUFFERFISH_SPAWN_EGG, "minecraft:pufferfish_spawn_egg");
    register_item_type(ITEM_RABBIT_SPAWN_EGG, "minecraft:rabbit_spawn_egg");
    register_item_type(ITEM_RAVAGER_SPAWN_EGG, "minecraft:ravager_spawn_egg");
    register_item_type(ITEM_SALMON_SPAWN_EGG, "minecraft:salmon_spawn_egg");
    register_item_type(ITEM_SHEEP_SPAWN_EGG, "minecraft:sheep_spawn_egg");
    register_item_type(ITEM_SHULKER_SPAWN_EGG, "minecraft:shulker_spawn_egg");
    register_item_type(ITEM_SILVERFISH_SPAWN_EGG, "minecraft:silverfish_spawn_egg");
    register_item_type(ITEM_SKELETON_SPAWN_EGG, "minecraft:skeleton_spawn_egg");
    register_item_type(ITEM_SKELETON_HORSE_SPAWN_EGG, "minecraft:skeleton_horse_spawn_egg");
    register_item_type(ITEM_SLIME_SPAWN_EGG, "minecraft:slime_spawn_egg");
    register_item_type(ITEM_SPIDER_SPAWN_EGG, "minecraft:spider_spawn_egg");
    register_item_type(ITEM_SQUID_SPAWN_EGG, "minecraft:squid_spawn_egg");
    register_item_type(ITEM_STRAY_SPAWN_EGG, "minecraft:stray_spawn_egg");
    register_item_type(ITEM_STRIDER_SPAWN_EGG, "minecraft:strider_spawn_egg");
    register_item_type(ITEM_TRADER_LLAMA_SPAWN_EGG, "minecraft:trader_llama_spawn_egg");
    register_item_type(ITEM_TROPICAL_FISH_SPAWN_EGG, "minecraft:tropical_fish_spawn_egg");
    register_item_type(ITEM_TURTLE_SPAWN_EGG, "minecraft:turtle_spawn_egg");
    register_item_type(ITEM_VEX_SPAWN_EGG, "minecraft:vex_spawn_egg");
    register_item_type(ITEM_VILLAGER_SPAWN_EGG, "minecraft:villager_spawn_egg");
    register_item_type(ITEM_VINDICATOR_SPAWN_EGG, "minecraft:vindicator_spawn_egg");
    register_item_type(ITEM_WANDERING_TRADER_SPAWN_EGG, "minecraft:wandering_trader_spawn_egg");
    register_item_type(ITEM_WITCH_SPAWN_EGG, "minecraft:witch_spawn_egg");
    register_item_type(ITEM_WITHER_SKELETON_SPAWN_EGG, "minecraft:wither_skeleton_spawn_egg");
    register_item_type(ITEM_WOLF_SPAWN_EGG, "minecraft:wolf_spawn_egg");
    register_item_type(ITEM_ZOGLIN_SPAWN_EGG, "minecraft:zoglin_spawn_egg");
    register_item_type(ITEM_ZOMBIE_SPAWN_EGG, "minecraft:zombie_spawn_egg");
    register_item_type(ITEM_ZOMBIE_HORSE_SPAWN_EGG, "minecraft:zombie_horse_spawn_egg");
    register_item_type(ITEM_ZOMBIE_VILLAGER_SPAWN_EGG, "minecraft:zombie_villager_spawn_egg");
    register_item_type(ITEM_ZOMBIFIED_PIGLIN_SPAWN_EGG, "minecraft:zombified_piglin_spawn_egg");
    register_item_type(ITEM_EXPERIENCE_BOTTLE, "minecraft:experience_bottle");
    register_item_type(ITEM_FIRE_CHARGE, "minecraft:fire_charge");
    register_item_type(ITEM_WRITABLE_BOOK, "minecraft:writable_book");
    register_item_type(ITEM_WRITTEN_BOOK, "minecraft:written_book");
    register_item_type(ITEM_EMERALD, "minecraft:emerald");
    register_item_type(ITEM_ITEM_FRAME, "minecraft:item_frame");
    register_item_type(ITEM_FLOWER_POT, "minecraft:flower_pot");
    register_item_type(ITEM_CARROT, "minecraft:carrot");
    register_item_type(ITEM_POTATO, "minecraft:potato");
    register_item_type(ITEM_BAKED_POTATO, "minecraft:baked_potato");
    register_item_type(ITEM_POISONOUS_POTATO, "minecraft:poisonous_potato");
    register_item_type(ITEM_MAP, "minecraft:map");
    register_item_type(ITEM_GOLDEN_CARROT, "minecraft:golden_carrot");
    register_item_type(ITEM_SKELETON_SKULL, "minecraft:skeleton_skull");
    register_item_type(ITEM_WITHER_SKELETON_SKULL, "minecraft:wither_skeleton_skull");
    register_item_type(ITEM_PLAYER_HEAD, "minecraft:player_head");
    register_item_type(ITEM_ZOMBIE_HEAD, "minecraft:zombie_head");
    register_item_type(ITEM_CREEPER_HEAD, "minecraft:creeper_head");
    register_item_type(ITEM_DRAGON_HEAD, "minecraft:dragon_head");
    register_item_type(ITEM_CARROT_ON_A_STICK, "minecraft:carrot_on_a_stick");
    register_item_type(ITEM_WARPED_FUNGUS_ON_A_STICK, "minecraft:warped_fungus_on_a_stick");
    register_item_type(ITEM_NETHER_STAR, "minecraft:nether_star");
    register_item_type(ITEM_PUMPKIN_PIE, "minecraft:pumpkin_pie");
    register_item_type(ITEM_FIREWORK_ROCKET, "minecraft:firework_rocket");
    register_item_type(ITEM_FIREWORK_STAR, "minecraft:firework_star");
    register_item_type(ITEM_ENCHANTED_BOOK, "minecraft:enchanted_book");
    register_item_type(ITEM_NETHER_BRICK, "minecraft:nether_brick");
    register_item_type(ITEM_QUARTZ, "minecraft:quartz");
    register_item_type(ITEM_TNT_MINECART, "minecraft:tnt_minecart");
    register_item_type(ITEM_HOPPER_MINECART, "minecraft:hopper_minecart");
    register_item_type(ITEM_PRISMARINE_SHARD, "minecraft:prismarine_shard");
    register_item_type(ITEM_PRISMARINE_CRYSTALS, "minecraft:prismarine_crystals");
    register_item_type(ITEM_RABBIT, "minecraft:rabbit");
    register_item_type(ITEM_COOKED_RABBIT, "minecraft:cooked_rabbit");
    register_item_type(ITEM_RABBIT_STEW, "minecraft:rabbit_stew");
    register_item_type(ITEM_RABBIT_FOOT, "minecraft:rabbit_foot");
    register_item_type(ITEM_RABBIT_HIDE, "minecraft:rabbit_hide");
    register_item_type(ITEM_ARMOR_STAND, "minecraft:armor_stand");
    register_item_type(ITEM_IRON_HORSE_ARMOR, "minecraft:iron_horse_armor");
    register_item_type(ITEM_GOLDEN_HORSE_ARMOR, "minecraft:golden_horse_armor");
    register_item_type(ITEM_DIAMOND_HORSE_ARMOR, "minecraft:diamond_horse_armor");
    register_item_type(ITEM_LEATHER_HORSE_ARMOR, "minecraft:leather_horse_armor");
    register_item_type(ITEM_LEAD, "minecraft:lead");
    register_item_type(ITEM_NAME_TAG, "minecraft:name_tag");
    register_item_type(ITEM_COMMAND_BLOCK_MINECART, "minecraft:command_block_minecart");
    register_item_type(ITEM_MUTTON, "minecraft:mutton");
    register_item_type(ITEM_COOKED_MUTTON, "minecraft:cooked_mutton");
    register_item_type(ITEM_WHITE_BANNER, "minecraft:white_banner");
    register_item_type(ITEM_ORANGE_BANNER, "minecraft:orange_banner");
    register_item_type(ITEM_MAGENTA_BANNER, "minecraft:magenta_banner");
    register_item_type(ITEM_LIGHT_BLUE_BANNER, "minecraft:light_blue_banner");
    register_item_type(ITEM_YELLOW_BANNER, "minecraft:yellow_banner");
    register_item_type(ITEM_LIME_BANNER, "minecraft:lime_banner");
    register_item_type(ITEM_PINK_BANNER, "minecraft:pink_banner");
    register_item_type(ITEM_GRAY_BANNER, "minecraft:gray_banner");
    register_item_type(ITEM_LIGHT_GRAY_BANNER, "minecraft:light_gray_banner");
    register_item_type(ITEM_CYAN_BANNER, "minecraft:cyan_banner");
    register_item_type(ITEM_PURPLE_BANNER, "minecraft:purple_banner");
    register_item_type(ITEM_BLUE_BANNER, "minecraft:blue_banner");
    register_item_type(ITEM_BROWN_BANNER, "minecraft:brown_banner");
    register_item_type(ITEM_GREEN_BANNER, "minecraft:green_banner");
    register_item_type(ITEM_RED_BANNER, "minecraft:red_banner");
    register_item_type(ITEM_BLACK_BANNER, "minecraft:black_banner");
    register_item_type(ITEM_END_CRYSTAL, "minecraft:end_crystal");
    register_item_type(ITEM_CHORUS_FRUIT, "minecraft:chorus_fruit");
    register_item_type(ITEM_POPPED_CHORUS_FRUIT, "minecraft:popped_chorus_fruit");
    register_item_type(ITEM_BEETROOT, "minecraft:beetroot");
    register_item_type(ITEM_BEETROOT_SEEDS, "minecraft:beetroot_seeds");
    register_item_type(ITEM_BEETROOT_SOUP, "minecraft:beetroot_soup");
    register_item_type(ITEM_DRAGON_BREATH, "minecraft:dragon_breath");
    register_item_type(ITEM_SPLASH_POTION, "minecraft:splash_potion");
    register_item_type(ITEM_SPECTRAL_ARROW, "minecraft:spectral_arrow");
    register_item_type(ITEM_TIPPED_ARROW, "minecraft:tipped_arrow");
    register_item_type(ITEM_LINGERING_POTION, "minecraft:lingering_potion");
    register_item_type(ITEM_SHIELD, "minecraft:shield");
    register_item_type(ITEM_ELYTRA, "minecraft:elytra");
    register_item_type(ITEM_SPRUCE_BOAT, "minecraft:spruce_boat");
    register_item_type(ITEM_BIRCH_BOAT, "minecraft:birch_boat");
    register_item_type(ITEM_JUNGLE_BOAT, "minecraft:jungle_boat");
    register_item_type(ITEM_ACACIA_BOAT, "minecraft:acacia_boat");
    register_item_type(ITEM_DARK_OAK_BOAT, "minecraft:dark_oak_boat");
    register_item_type(ITEM_TOTEM_OF_UNDYING, "minecraft:totem_of_undying");
    register_item_type(ITEM_SHULKER_SHELL, "minecraft:shulker_shell");
    register_item_type(ITEM_IRON_NUGGET, "minecraft:iron_nugget");
    register_item_type(ITEM_KNOWLEDGE_BOOK, "minecraft:knowledge_book");
    register_item_type(ITEM_DEBUG_STICK, "minecraft:debug_stick");
    register_item_type(ITEM_MUSIC_DISC_13, "minecraft:music_disc_13");
    register_item_type(ITEM_MUSIC_DISC_CAT, "minecraft:music_disc_cat");
    register_item_type(ITEM_MUSIC_DISC_BLOCKS, "minecraft:music_disc_blocks");
    register_item_type(ITEM_MUSIC_DISC_CHIRP, "minecraft:music_disc_chirp");
    register_item_type(ITEM_MUSIC_DISC_FAR, "minecraft:music_disc_far");
    register_item_type(ITEM_MUSIC_DISC_MALL, "minecraft:music_disc_mall");
    register_item_type(ITEM_MUSIC_DISC_MELLOHI, "minecraft:music_disc_mellohi");
    register_item_type(ITEM_MUSIC_DISC_STAL, "minecraft:music_disc_stal");
    register_item_type(ITEM_MUSIC_DISC_STRAD, "minecraft:music_disc_strad");
    register_item_type(ITEM_MUSIC_DISC_WARD, "minecraft:music_disc_ward");
    register_item_type(ITEM_MUSIC_DISC_11, "minecraft:music_disc_11");
    register_item_type(ITEM_MUSIC_DISC_WAIT, "minecraft:music_disc_wait");
    register_item_type(ITEM_MUSIC_DISC_PIGSTEP, "minecraft:music_disc_pigstep");
    register_item_type(ITEM_TRIDENT, "minecraft:trident");
    register_item_type(ITEM_PHANTOM_MEMBRANE, "minecraft:phantom_membrane");
    register_item_type(ITEM_NAUTILUS_SHELL, "minecraft:nautilus_shell");
    register_item_type(ITEM_HEART_OF_THE_SEA, "minecraft:heart_of_the_sea");
    register_item_type(ITEM_CROSSBOW, "minecraft:crossbow");
    register_item_type(ITEM_SUSPICIOUS_STEW, "minecraft:suspicious_stew");
    register_item_type(ITEM_LOOM, "minecraft:loom");
    register_item_type(ITEM_FLOWER_BANNER_PATTERN, "minecraft:flower_banner_pattern");
    register_item_type(ITEM_CREEPER_BANNER_PATTERN, "minecraft:creeper_banner_pattern");
    register_item_type(ITEM_SKULL_BANNER_PATTERN, "minecraft:skull_banner_pattern");
    register_item_type(ITEM_MOJANG_BANNER_PATTERN, "minecraft:mojang_banner_pattern");
    register_item_type(ITEM_GLOBE_BANNER_PATTERN, "minecraft:globe_banner_pattern");
    register_item_type(ITEM_PIGLIN_BANNER_PATTERN, "minecraft:piglin_banner_pattern");
    register_item_type(ITEM_COMPOSTER, "minecraft:composter");
    register_item_type(ITEM_BARREL, "minecraft:barrel");
    register_item_type(ITEM_SMOKER, "minecraft:smoker");
    register_item_type(ITEM_BLAST_FURNACE, "minecraft:blast_furnace");
    register_item_type(ITEM_CARTOGRAPHY_TABLE, "minecraft:cartography_table");
    register_item_type(ITEM_FLETCHING_TABLE, "minecraft:fletching_table");
    register_item_type(ITEM_GRINDSTONE, "minecraft:grindstone");
    register_item_type(ITEM_LECTERN, "minecraft:lectern");
    register_item_type(ITEM_SMITHING_TABLE, "minecraft:smithing_table");
    register_item_type(ITEM_STONECUTTER, "minecraft:stonecutter");
    register_item_type(ITEM_BELL, "minecraft:bell");
    register_item_type(ITEM_LANTERN, "minecraft:lantern");
    register_item_type(ITEM_SOUL_LANTERN, "minecraft:soul_lantern");
    register_item_type(ITEM_SWEET_BERRIES, "minecraft:sweet_berries");
    register_item_type(ITEM_CAMPFIRE, "minecraft:campfire");
    register_item_type(ITEM_SOUL_CAMPFIRE, "minecraft:soul_campfire");
    register_item_type(ITEM_SHROOMLIGHT, "minecraft:shroomlight");
    register_item_type(ITEM_HONEYCOMB, "minecraft:honeycomb");
    register_item_type(ITEM_BEE_NEST, "minecraft:bee_nest");
    register_item_type(ITEM_BEEHIVE, "minecraft:beehive");
    register_item_type(ITEM_HONEY_BOTTLE, "minecraft:honey_bottle");
    register_item_type(ITEM_HONEY_BLOCK, "minecraft:honey_block");
    register_item_type(ITEM_HONEYCOMB_BLOCK, "minecraft:honeycomb_block");
    register_item_type(ITEM_LODESTONE, "minecraft:lodestone");
    register_item_type(ITEM_NETHERITE_BLOCK, "minecraft:netherite_block");
    register_item_type(ITEM_ANCIENT_DEBRIS, "minecraft:ancient_debris");
    register_item_type(ITEM_TARGET, "minecraft:target");
    register_item_type(ITEM_CRYING_OBSIDIAN, "minecraft:crying_obsidian");
    register_item_type(ITEM_BLACKSTONE, "minecraft:blackstone");
    register_item_type(ITEM_BLACKSTONE_SLAB, "minecraft:blackstone_slab");
    register_item_type(ITEM_BLACKSTONE_STAIRS, "minecraft:blackstone_stairs");
    register_item_type(ITEM_GILDED_BLACKSTONE, "minecraft:gilded_blackstone");
    register_item_type(ITEM_POLISHED_BLACKSTONE, "minecraft:polished_blackstone");
    register_item_type(ITEM_POLISHED_BLACKSTONE_SLAB, "minecraft:polished_blackstone_slab");
    register_item_type(ITEM_POLISHED_BLACKSTONE_STAIRS, "minecraft:polished_blackstone_stairs");
    register_item_type(ITEM_CHISELED_POLISHED_BLACKSTONE, "minecraft:chiseled_polished_blackstone");
    register_item_type(ITEM_POLISHED_BLACKSTONE_BRICKS, "minecraft:polished_blackstone_bricks");
    register_item_type(ITEM_POLISHED_BLACKSTONE_BRICK_SLAB, "minecraft:polished_blackstone_brick_slab");
    register_item_type(ITEM_POLISHED_BLACKSTONE_BRICK_STAIRS, "minecraft:polished_blackstone_brick_stairs");
    register_item_type(ITEM_CRACKED_POLISHED_BLACKSTONE_BRICKS, "minecraft:cracked_polished_blackstone_bricks");
    register_item_type(ITEM_RESPAWN_ANCHOR, "minecraft:respawn_anchor");
}
