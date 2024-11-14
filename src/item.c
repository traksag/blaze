#include <math.h>
#include <string.h>
#include "shared.h"
#include "chunk.h"
#include "player.h"

#define PLACE_REPLACING ((unsigned) (1 << 0))
#define PLACE_CAN_PLACE ((unsigned) (1 << 1))

typedef struct {
    WorldBlockPos pos;
    u16 cur_state;
    unsigned char flags;
    i32 cur_type;
} place_target;

typedef struct {
    Entity * player;
    WorldBlockPos clicked_pos;
    i32 clicked_face;
    float click_offset_x;
    float click_offset_y;
    float click_offset_z;
    MemoryArena * scratch_arena;
    block_update_context * buc;
} place_context;

static int
can_replace(i32 place_type, i32 cur_type) {
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
    case BLOCK_LIGHT:
        // @TODO(traks) this allows players to place blocks in mid-air. This is
        // needed for placing water lilies on top of water. Perhaps we should
        // disallow it for other blocks depending on whether this is the clicked
        // block or the relative block?
    // replaceable plants
    case BLOCK_SHORT_GRASS:
    case BLOCK_FERN:
    case BLOCK_DEAD_BUSH:
    case BLOCK_VINE:
    case BLOCK_GLOW_LICHEN:
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
    case BLOCK_HANGING_ROOTS:
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
determine_place_target(WorldBlockPos clicked_pos,
        i32 clicked_face, i32 place_type) {
    place_target res = {0};
    WorldBlockPos target_pos = clicked_pos;
    u16 cur_state = WorldGetBlockState(target_pos);
    i32 cur_type = serv->block_type_by_state[cur_state];
    int replacing = PLACE_REPLACING;

    if (!can_replace(place_type, cur_type)) {
        target_pos = WorldBlockPosRel(target_pos, clicked_face);
        cur_state = WorldGetBlockState(target_pos);
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
place_simple_block(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_snowy_grassy_block(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    u16 state_above = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_POS_Y));
    i32 type_above = serv->block_type_by_state[state_above];

    if (type_above == BLOCK_SNOW_BLOCK || type_above == BLOCK_SNOW) {
        place_info.snowy = 1;
    } else {
        place_info.snowy = 0;
    }

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_plant(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];

    if (!can_plant_survive_on(type_below)) {
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void place_propagule(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];

    if (!can_propagule_survive_on(type_below)) {
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_azalea(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];

    if (!can_azalea_survive_on(type_below)) {
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_lily_pad(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    if (target.cur_type == BLOCK_LAVA
            || get_water_level(target.cur_state) != FLUID_LEVEL_NONE) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    if (!can_lily_pad_survive_on(state_below)) {
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_dead_bush(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];

    if (!can_dead_bush_survive_on(type_below)) {
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_wither_rose(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];

    if (!can_wither_rose_survive_on(type_below)) {
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_nether_plant(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];

    if (!can_nether_plant_survive_on(type_below)) {
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_pressure_plate(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    if (!can_pressure_plate_survive_on(state_below)) {
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
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
place_simple_pillar(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    set_axis_by_clicked_face(&place_info, context.clicked_face);

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_chain(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    set_axis_by_clicked_face(&place_info, context.clicked_face);
    place_info.waterlogged = is_water_source(target.cur_state);

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_slab(place_context context, i32 place_type) {
    WorldBlockPos target_pos = context.clicked_pos;
    u16 cur_state = WorldGetBlockState(target_pos);
    block_state_info cur_info = describe_block_state(cur_state);
    i32 cur_type = cur_info.blockType;

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
        target_pos = WorldBlockPosRel(target_pos, context.clicked_face);
        cur_state = WorldGetBlockState(target_pos);
        cur_info = describe_block_state(cur_state);
        cur_type = cur_info.blockType;

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

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target_pos, place_state);
    push_direct_neighbour_block_updates(target_pos, context.buc);
}

static void
place_sea_pickle(place_context context, i32 place_type) {
    WorldBlockPos target_pos = context.clicked_pos;
    u16 cur_state = WorldGetBlockState(target_pos);
    block_state_info cur_info = describe_block_state(cur_state);
    i32 cur_type = cur_info.blockType;

    int replace_cur = 0;
    if (cur_type == place_type) {
        if (cur_info.pickles < 4) {
            replace_cur = 1;
        }
    } else {
        replace_cur = can_replace(place_type, cur_type);
    }

    if (!replace_cur) {
        target_pos = WorldBlockPosRel(target_pos, context.clicked_face);
        cur_state = WorldGetBlockState(target_pos);
        cur_info = describe_block_state(cur_state);
        cur_type = cur_info.blockType;

        if (cur_type == place_type) {
            if (cur_info.pickles >= 4) {
                return;
            }
        } else if (!can_replace(place_type, cur_type)) {
            return;
        }
    }

    WorldBlockPos posBelow = WorldBlockPosRel(target_pos, DIRECTION_NEG_Y);
    u16 state_below = WorldGetBlockState(posBelow);
    i32 type_below = serv->block_type_by_state[state_below];
    if (!can_sea_pickle_survive_on(state_below, posBelow)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);

    if (place_type == cur_type) {
        place_info.pickles = cur_info.pickles + 1;
    }
    place_info.waterlogged = is_water_source(cur_state);

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target_pos, place_state);
    push_direct_neighbour_block_updates(target_pos, context.buc);
}

static void
place_snow(place_context context, i32 place_type) {
    WorldBlockPos target_pos = context.clicked_pos;
    u16 cur_state = WorldGetBlockState(target_pos);
    block_state_info cur_info = describe_block_state(cur_state);
    i32 cur_type = cur_info.blockType;

    int replace_cur = 0;
    if (cur_type == place_type) {
        if (cur_info.layers < 8 && context.clicked_face == DIRECTION_POS_Y) {
            replace_cur = 1;
        }
    } else {
        replace_cur = can_replace(place_type, cur_type);
    }

    if (!replace_cur) {
        target_pos = WorldBlockPosRel(target_pos, context.clicked_face);
        cur_state = WorldGetBlockState(target_pos);
        cur_info = describe_block_state(cur_state);
        cur_type = cur_info.blockType;

        if (cur_type == place_type) {
            if (cur_info.layers >= 8) {
                return;
            }
        } else if (!can_replace(place_type, cur_type)) {
            return;
        }
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target_pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];
    if (!can_snow_survive_on(state_below)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    if (place_type == cur_type) {
        place_info.layers = cur_info.layers + 1;
    }

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target_pos, place_state);
    push_direct_neighbour_block_updates(target_pos, context.buc);
}

static void
place_leaves(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    // @TODO(traks) calculate distance to nearest log block and modify block
    // state with that information
    block_state_info place_info = describe_default_block_state(place_type);
    place_info.persistent = 1;

    WorldBlockPos target_pos = target.pos;
    u16 cur_state = WorldGetBlockState(target_pos);
    place_info.waterlogged = is_water_source(cur_state);

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_mangrove_roots(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);

    WorldBlockPos target_pos = target.pos;
    u16 cur_state = WorldGetBlockState(target_pos);
    place_info.waterlogged = is_water_source(cur_state);

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_horizontal_facing(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = get_opposite_direction(GetPlayerFacing(context.player));

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_end_portal_frame(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = get_opposite_direction(GetPlayerFacing(context.player));
    place_info.eye = 0;

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_trapdoor(place_context context, i32 place_type) {
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
                GetPlayerFacing(context.player));
    } else if (context.clicked_face == DIRECTION_POS_Y) {
        place_info.half = BLOCK_HALF_BOTTOM;
        place_info.horizontal_facing = get_opposite_direction(
                GetPlayerFacing(context.player));
    } else if (context.clicked_face == DIRECTION_NEG_Y) {
        place_info.half = BLOCK_HALF_TOP;
        place_info.horizontal_facing = get_opposite_direction(
                GetPlayerFacing(context.player));
    } else {
        place_info.half = context.click_offset_y > 0.5f ?
                BLOCK_HALF_TOP : BLOCK_HALF_BOTTOM;
        place_info.horizontal_facing = context.clicked_face;
    }
    place_info.waterlogged = is_water_source(target.cur_state);

    // @TODO(traks) open trapdoor and set powered if necessary

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_fence_gate(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    int player_facing = GetPlayerFacing(context.player);
    place_info.horizontal_facing = player_facing;
    if (player_facing == DIRECTION_POS_X || player_facing == DIRECTION_NEG_X) {
        int neighbour_state_pos = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_POS_Z));
        block_state_info neighbour_info_pos = describe_block_state(neighbour_state_pos);
        int neighbour_state_neg = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Z));
        block_state_info neighbour_info_neg = describe_block_state(neighbour_state_neg);
        if (BlockHasBehaviour(&neighbour_info_pos, BLOCK_BEHAVIOUR_WALL_CONNECT)
                || BlockHasBehaviour(&neighbour_info_neg, BLOCK_BEHAVIOUR_WALL_CONNECT)) {
            place_info.in_wall = 1;
        }
    } else {
        // facing along z axis
        int neighbour_state_pos = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_POS_X));
        block_state_info neighbour_info_pos = describe_block_state(neighbour_state_pos);
        int neighbour_state_neg = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_X));
        block_state_info neighbour_info_neg = describe_block_state(neighbour_state_neg);
        if (BlockHasBehaviour(&neighbour_info_pos, BLOCK_BEHAVIOUR_WALL_CONNECT)
                || BlockHasBehaviour(&neighbour_info_neg, BLOCK_BEHAVIOUR_WALL_CONNECT)) {
            place_info.in_wall = 1;
        }
    }

    // @TODO(traks) open fence gate and set powered if necessary

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_crop(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];

    // @TODO(traks) light level also needs to be sufficient
    switch (type_below) {
    case BLOCK_FARMLAND:
        break;
    default:
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_nether_wart(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];

    switch (type_below) {
    case BLOCK_SOUL_SAND:
        break;
    default:
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_carpet(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];

    if (!can_carpet_survive_on(type_below)) {
        return;
    }

    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_mushroom_block(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);

    int directions[] = {0, 1, 2, 3, 4, 5};

    for (int i = 0; i < 6; i++) {
        WorldBlockPos pos = WorldBlockPosRel(target.pos, directions[i]);
        u16 state = WorldGetBlockState(pos);
        i32 type = serv->block_type_by_state[state];

        // connect to neighbouring mushroom blocks of the same type by setting
        // the six facing properties to true if connected
        if (type == place_type) {
            place_info.values[BLOCK_PROPERTY_NEG_Y + directions[i]] = 0;
        } else {
            place_info.values[BLOCK_PROPERTY_NEG_Y + directions[i]] = 1;
        }
    }

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_end_rod(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);

    int opposite_face = get_opposite_direction(context.clicked_face);

    WorldBlockPos opposite_pos = WorldBlockPosRel(target.pos, opposite_face);
    u16 opposite_state = WorldGetBlockState(opposite_pos);
    block_state_info opposite_info = describe_block_state(opposite_state);

    if (opposite_info.blockType == place_type) {
        if (opposite_info.facing == context.clicked_face) {
            place_info.facing = opposite_face;
        } else {
            place_info.facing = context.clicked_face;
        }
    } else {
        place_info.facing = context.clicked_face;
    }

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_sugar_cane(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }
    if (!can_sugar_cane_survive_at(target.pos)) {
        return;
    }

    u16 place_state = get_default_block_state(place_type);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_dead_coral(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    BlockModel support = BlockDetermineSupportModel(state_below);
    if (!(support.fullFaces & (1 << DIRECTION_POS_Y))) {
        // face below is not sturdy
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.waterlogged = is_full_water(target.cur_state);

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

typedef struct {
    unsigned char directions[6];
} direction_list;

static direction_list
get_directions_by_player_rot(Entity * player) {
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
place_dead_coral_fan(place_context context, i32 base_place_type,
        i32 wall_place_type) {
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

        WorldBlockPos attach_pos = WorldBlockPosRel(target.pos, dir);
        u16 wall_state = WorldGetBlockState(attach_pos);
        int wall_face = get_opposite_direction(dir);
        BlockModel support = BlockDetermineSupportModel(wall_state);

        if (dir == DIRECTION_NEG_Y) {
            seen_neg_y = 1;
            if (support.fullFaces & (1 << wall_face)) {
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
            if (support.fullFaces & (1 << wall_face)) {
                // wall face is sturdy
                selected_dir = dir;
                break;
            }
        }
    }
    if (selected_dir == -1) {
        return;
    }

    i32 place_type = selected_dir == DIRECTION_NEG_Y ?
            base_place_type : wall_place_type;
    block_state_info place_info = describe_default_block_state(place_type);
    place_info.waterlogged = is_full_water(target.cur_state);
    place_info.horizontal_facing = get_opposite_direction(selected_dir);

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_torch(place_context context, i32 base_place_type,
        i32 wall_place_type) {
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

        WorldBlockPos attach_pos = WorldBlockPosRel(target.pos, dir);
        u16 wall_state = WorldGetBlockState(attach_pos);
        int wall_face = get_opposite_direction(dir);
        BlockModel support = BlockDetermineSupportModel(wall_state);

        if (dir == DIRECTION_NEG_Y) {
            seen_neg_y = 1;
            if (support.poleFaces & (1 << wall_face)) {
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
            if (support.fullFaces & (1 << wall_face)) {
                // wall face is sturdy
                selected_dir = dir;
                break;
            }
        }
    }
    if (selected_dir == -1) {
        return;
    }

    i32 place_type = selected_dir == DIRECTION_NEG_Y ?
            base_place_type : wall_place_type;
    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = get_opposite_direction(selected_dir);

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_ladder(place_context context, i32 place_type) {
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

        WorldBlockPos attach_pos = WorldBlockPosRel(target.pos, dir);
        u16 wall_state = WorldGetBlockState(attach_pos);
        BlockModel support = BlockDetermineSupportModel(wall_state);
        int wall_face = get_opposite_direction(dir);

        if (support.fullFaces & (1 << wall_face)) {
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

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_door(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }
    if (target.pos.y >= MAX_WORLD_Y) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    BlockModel support = BlockDetermineSupportModel(state_below);
    if (!(support.fullFaces & (1 << DIRECTION_POS_Y))) {
        // face below is not sturdy
        return;
    }

    u16 state_above = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_POS_Y));
    i32 type_above = serv->block_type_by_state[state_above];

    if (!can_replace(place_type, type_above)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = GetPlayerFacing(context.player);
    place_info.double_block_half = DOUBLE_BLOCK_HALF_LOWER;
    // @TODO(traks) determine side of the hinge
    place_info.door_hinge = DOOR_HINGE_LEFT;
    // @TODO(traks) placed opened door if powered
    place_info.open = 0;
    place_info.powered = 0;

    // place lower half
    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);

    // place upper half
    // TODO(traks): don't place door if top section can't be placed (if there's
    // a block in the way or outside the world bounds)
    place_info.double_block_half = DOUBLE_BLOCK_HALF_UPPER;
    place_state = make_block_state(&place_info);
    WorldSetBlockState(WorldBlockPosRel(target.pos, DIRECTION_POS_Y), place_state);

    // @TODO(traks) don't update door halves themselves, only blocks around them
    push_direct_neighbour_block_updates(target.pos, context.buc);
    push_direct_neighbour_block_updates(
            WorldBlockPosRel(target.pos, DIRECTION_POS_Y),
            context.buc);
}

static void
place_bed(place_context context, i32 place_type, int dye_colour) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    int facing = GetPlayerFacing(context.player);
    WorldBlockPos head_pos = WorldBlockPosRel(target.pos, facing);
    u16 neighbour_state = WorldGetBlockState(head_pos);
    i32 neighbour_type = serv->block_type_by_state[neighbour_state];

    if (!can_replace(place_type, neighbour_type)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = facing;

    // place foot part
    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);

    // place head part
    place_info.bed_part = BED_PART_HEAD;
    place_state = make_block_state(&place_info);
    WorldSetBlockState(head_pos, place_state);

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

    // @TODO(traks) don't update the parts, only the blocks around them
    push_direct_neighbour_block_updates(target.pos, context.buc);
    push_direct_neighbour_block_updates(head_pos, context.buc);
}

static void
place_bamboo(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    if (get_water_level(target.cur_state) != FLUID_LEVEL_NONE
            || target.cur_type == BLOCK_LAVA) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];

    if (!is_bamboo_plantable_on(type_below)) {
        return;
    }

    u16 place_state;

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

    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_stairs(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.horizontal_facing = GetPlayerFacing(context.player);
    if (context.clicked_face == DIRECTION_POS_Y || context.click_offset_y <= 0.5f) {
        place_info.half = BLOCK_HALF_BOTTOM;
    } else {
        place_info.half = BLOCK_HALF_TOP;
    }
    place_info.waterlogged = is_water_source(target.cur_state);
    update_stairs_shape(target.pos, &place_info);

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_fence(place_context context, i32 place_type) {
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

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_pane(place_context context, i32 place_type) {
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

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_wall(place_context context, i32 place_type) {
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

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_rail(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    // @TODO(traks) check if rail can survive on block below and place rail in
    // the correct state (ascending and turned).
    block_state_info place_info = describe_default_block_state(place_type);
    int player_facing = GetPlayerFacing(context.player);
    place_info.rail_shape = player_facing == DIRECTION_NEG_X
            || player_facing == DIRECTION_POS_X ? RAIL_SHAPE_X : RAIL_SHAPE_Z;

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_lever_or_button(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    direction_list list = get_attach_directions_by_preference(context, target);
    int selected_dir = -1;

    for (int i = 0; i < 6; i++) {
        int dir = list.directions[i];
        WorldBlockPos attach_pos = WorldBlockPosRel(target.pos, dir);
        u16 wall_state = WorldGetBlockState(attach_pos);
        int wall_face = get_opposite_direction(dir);
        BlockModel support = BlockDetermineSupportModel(wall_state);

        if (support.fullFaces & (1 << wall_face)) {
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
        place_info.horizontal_facing = GetPlayerFacing(context.player);
        break;
    case DIRECTION_NEG_Y:
        place_info.attach_face = ATTACH_FACE_FLOOR;
        place_info.horizontal_facing = GetPlayerFacing(context.player);
        break;
    default:
        // attach to horizontal wall
        place_info.attach_face = ATTACH_FACE_WALL;
        place_info.horizontal_facing = get_opposite_direction(selected_dir);
    }

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_grindstone(place_context context, i32 place_type) {
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
        place_info.horizontal_facing = GetPlayerFacing(context.player);
        break;
    case DIRECTION_NEG_Y:
        place_info.attach_face = ATTACH_FACE_FLOOR;
        place_info.horizontal_facing = GetPlayerFacing(context.player);
        break;
    default:
        // attach to horizontal wall
        place_info.attach_face = ATTACH_FACE_WALL;
        place_info.horizontal_facing = get_opposite_direction(selected_dir);
    }

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

static void
place_redstone_wire(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    u16 state_below = WorldGetBlockState(WorldBlockPosRel(target.pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];

    if (!can_redstone_wire_survive_on(state_below)) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    // never place as dot
    place_info.redstone_pos_x = 1;
    place_info.redstone_pos_z = 1;
    place_info.redstone_neg_x = 1;
    place_info.redstone_neg_z = 1;
    update_redstone_wire(target.pos, target.cur_state, &place_info, context.buc);
}

static void
place_amethyst_cluster(place_context context, i32 place_type) {
    place_target target = determine_place_target(
            context.clicked_pos, context.clicked_face, place_type);
    if (!(target.flags & PLACE_CAN_PLACE)) {
        return;
    }

    int opposite_face = get_opposite_direction(context.clicked_face);
    WorldBlockPos opposite_pos = WorldBlockPosRel(target.pos, opposite_face);
    u16 opposite_state = WorldGetBlockState(opposite_pos);

    BlockModel support = BlockDetermineSupportModel(opposite_state);
    if (!(support.fullFaces & (1 << context.clicked_face))) {
        return;
    }

    block_state_info place_info = describe_default_block_state(place_type);
    place_info.facing = context.clicked_face;
    place_info.waterlogged = is_water_source(target.cur_state);

    u16 place_state = make_block_state(&place_info);
    WorldSetBlockState(target.pos, place_state);
    push_direct_neighbour_block_updates(target.pos, context.buc);
}

void
process_use_item_on_packet(PlayerController * control,
        i32 hand, BlockPos packetClickedPos, i32 clicked_face,
        float click_offset_x, float click_offset_y, float click_offset_z,
        u8 is_inside, MemoryArena * scratch_arena) {
    Entity * player = ResolveEntity(control->entityId);
    if (control->flags & PLAYER_CONTROL_AWAITING_TELEPORT) {
        // ignore
        return;
    }

    WorldBlockPos clickedPos = {.worldId = player->worldId, .xyz = packetClickedPos};

    int sel_slot = player->selected_slot;
    item_stack * main = player->slots + sel_slot;
    item_stack * off = player->slots + PLAYER_OFF_HAND_SLOT;
    item_stack * used = hand == PLAYER_MAIN_HAND ? main : off;

    int max_updates = 512;
    block_update_context buc = {
        .blocks_to_update = MallocInArena(scratch_arena,
                max_updates * sizeof (block_update)),
        .update_count = 0,
        .max_updates = max_updates
    };

    // @TODO(traks) special handling depending on gamemode. Currently we assume
    // gamemode creative

    // @TODO(traks) ensure clicked block is in one of the sent
    // chunks inside the player's chunk cache

    // @TODO(traks) check for cooldowns (ender pearls,
    // chorus fruits)

    // if the player is not crouching with an item in their hands, try to use
    // the clicked block
    if (!(player->flags & ENTITY_SHIFTING)
            || (main->type == ITEM_AIR && off->type == ITEM_AIR)) {
        int used_block = use_block(control,
                hand, clickedPos, clicked_face,
                click_offset_x, click_offset_y, click_offset_z,
                is_inside, &buc);
        if (used_block) {
            propagate_block_updates(&buc);
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

    // @TODO(traks) take player can_build ability into account

    // @TODO(traks) play block place sound

    // @TODO(traks) finalise some things such as block entity data based on
    // custom item stack name, create bed head part, plant upper part, etc.

    place_context context = {
        .player = player,
        .clicked_pos = clickedPos,
        .clicked_face = clicked_face,
        .click_offset_x = click_offset_x,
        .click_offset_y = click_offset_y,
        .click_offset_z = click_offset_z,
        .scratch_arena = scratch_arena,
        .buc = &buc
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
    case ITEM_DEEPSLATE:
        place_simple_pillar(context, BLOCK_DEEPSLATE);
        break;
    case ITEM_COBBLED_DEEPSLATE:
        place_simple_block(context, BLOCK_COBBLED_DEEPSLATE);
        break;
    case ITEM_POLISHED_DEEPSLATE:
        place_simple_block(context, BLOCK_POLISHED_DEEPSLATE);
        break;
    case ITEM_CALCITE:
        place_simple_block(context, BLOCK_CALCITE);
        break;
    case ITEM_TUFF:
        place_simple_block(context, BLOCK_TUFF);
        break;
    case ITEM_DRIPSTONE_BLOCK:
        place_simple_block(context, BLOCK_DRIPSTONE_BLOCK);
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
    case ITEM_ROOTED_DIRT:
        place_simple_block(context, BLOCK_ROOTED_DIRT);
        break;
    case ITEM_MUD:
        place_simple_block(context, BLOCK_MUD);
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
    case ITEM_MANGROVE_PLANKS:
        place_simple_block(context, BLOCK_MANGROVE_PLANKS);
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
    case ITEM_MANGROVE_PROPAGULE:
        place_propagule(context, BLOCK_MANGROVE_PROPAGULE);
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
    case ITEM_COAL_ORE:
        place_simple_block(context, BLOCK_COAL_ORE);
        break;
    case ITEM_DEEPSLATE_COAL_ORE:
        place_simple_block(context, BLOCK_DEEPSLATE_COAL_ORE);
        break;
    case ITEM_IRON_ORE:
        place_simple_block(context, BLOCK_IRON_ORE);
        break;
    case ITEM_DEEPSLATE_IRON_ORE:
        place_simple_block(context, BLOCK_DEEPSLATE_IRON_ORE);
        break;
    case ITEM_COPPER_ORE:
        place_simple_block(context, BLOCK_COPPER_ORE);
        break;
    case ITEM_DEEPSLATE_COPPER_ORE:
        place_simple_block(context, BLOCK_DEEPSLATE_COPPER_ORE);
        break;
    case ITEM_GOLD_ORE:
        place_simple_block(context, BLOCK_GOLD_ORE);
        break;
    case ITEM_DEEPSLATE_GOLD_ORE:
        place_simple_block(context, BLOCK_DEEPSLATE_GOLD_ORE);
        break;
    case ITEM_REDSTONE_ORE:
        place_simple_block(context, BLOCK_REDSTONE_ORE);
        break;
    case ITEM_DEEPSLATE_REDSTONE_ORE:
        place_simple_block(context, BLOCK_DEEPSLATE_REDSTONE_ORE);
        break;
    case ITEM_EMERALD_ORE:
        place_simple_block(context, BLOCK_EMERALD_ORE);
        break;
    case ITEM_DEEPSLATE_EMERALD_ORE:
        place_simple_block(context, BLOCK_DEEPSLATE_EMERALD_ORE);
        break;
    case ITEM_LAPIS_ORE:
        place_simple_block(context, BLOCK_LAPIS_ORE);
        break;
    case ITEM_DEEPSLATE_LAPIS_ORE:
        place_simple_block(context, BLOCK_DEEPSLATE_LAPIS_ORE);
        break;
    case ITEM_DIAMOND_ORE:
        place_simple_block(context, BLOCK_DIAMOND_ORE);
        break;
    case ITEM_DEEPSLATE_DIAMOND_ORE:
        place_simple_block(context, BLOCK_DEEPSLATE_DIAMOND_ORE);
        break;
    case ITEM_NETHER_GOLD_ORE:
        place_simple_block(context, BLOCK_NETHER_GOLD_ORE);
        break;
    case ITEM_NETHER_QUARTZ_ORE:
        place_simple_block(context, BLOCK_NETHER_QUARTZ_ORE);
        break;
    case ITEM_ANCIENT_DEBRIS:
        place_simple_block(context, BLOCK_ANCIENT_DEBRIS);
        break;
    case ITEM_COAL_BLOCK:
        place_simple_block(context, BLOCK_COAL_BLOCK);
        break;
    case ITEM_RAW_IRON_BLOCK:
        place_simple_block(context, BLOCK_RAW_IRON_BLOCK);
        break;
    case ITEM_RAW_COPPER_BLOCK:
        place_simple_block(context, BLOCK_RAW_COPPER_BLOCK);
        break;
    case ITEM_RAW_GOLD_BLOCK:
        place_simple_block(context, BLOCK_RAW_GOLD_BLOCK);
        break;
    case ITEM_AMETHYST_BLOCK:
        place_simple_block(context, BLOCK_AMETHYST_BLOCK);
        break;
    case ITEM_BUDDING_AMETHYST:
        place_simple_block(context, BLOCK_BUDDING_AMETHYST);
        break;
    case ITEM_IRON_BLOCK:
        place_simple_block(context, BLOCK_IRON_BLOCK);
        break;
    case ITEM_COPPER_BLOCK:
        place_simple_block(context, BLOCK_COPPER_BLOCK);
        break;
    case ITEM_GOLD_BLOCK:
        place_simple_block(context, BLOCK_GOLD_BLOCK);
        break;
    case ITEM_DIAMOND_BLOCK:
        place_simple_block(context, BLOCK_DIAMOND_BLOCK);
        break;
    case ITEM_NETHERITE_BLOCK:
        place_simple_block(context, BLOCK_NETHERITE_BLOCK);
        break;
    case ITEM_EXPOSED_COPPER:
        place_simple_block(context, BLOCK_EXPOSED_COPPER);
        break;
    case ITEM_WEATHERED_COPPER:
        place_simple_block(context, BLOCK_WEATHERED_COPPER);
        break;
    case ITEM_OXIDIZED_COPPER:
        place_simple_block(context, BLOCK_OXIDIZED_COPPER);
        break;
    case ITEM_CUT_COPPER:
        place_simple_block(context, BLOCK_CUT_COPPER);
        break;
    case ITEM_EXPOSED_CUT_COPPER:
        place_simple_block(context, BLOCK_EXPOSED_CUT_COPPER);
        break;
    case ITEM_WEATHERED_CUT_COPPER:
        place_simple_block(context, BLOCK_WEATHERED_CUT_COPPER);
        break;
    case ITEM_OXIDIZED_CUT_COPPER:
        place_simple_block(context, BLOCK_OXIDIZED_CUT_COPPER);
        break;
    case ITEM_CUT_COPPER_STAIRS:
        place_simple_block(context, BLOCK_CUT_COPPER_STAIRS);
        break;
    case ITEM_EXPOSED_CUT_COPPER_STAIRS:
        place_stairs(context, BLOCK_EXPOSED_CUT_COPPER_STAIRS);
        break;
    case ITEM_WEATHERED_CUT_COPPER_STAIRS:
        place_stairs(context, BLOCK_WEATHERED_CUT_COPPER_STAIRS);
        break;
    case ITEM_OXIDIZED_CUT_COPPER_STAIRS:
        place_stairs(context, BLOCK_OXIDIZED_CUT_COPPER_STAIRS);
        break;
    case ITEM_CUT_COPPER_SLAB:
        place_slab(context, BLOCK_CUT_COPPER_SLAB);
        break;
    case ITEM_EXPOSED_CUT_COPPER_SLAB:
        place_slab(context, BLOCK_EXPOSED_CUT_COPPER_SLAB);
        break;
    case ITEM_WEATHERED_CUT_COPPER_SLAB:
        place_slab(context, BLOCK_WEATHERED_CUT_COPPER_SLAB);
        break;
    case ITEM_OXIDIZED_CUT_COPPER_SLAB:
        place_slab(context, BLOCK_OXIDIZED_CUT_COPPER_SLAB);
        break;
    case ITEM_WAXED_COPPER_BLOCK:
        place_simple_block(context, BLOCK_WAXED_COPPER_BLOCK);
        break;
    case ITEM_WAXED_EXPOSED_COPPER:
        place_simple_block(context, BLOCK_WAXED_EXPOSED_COPPER);
        break;
    case ITEM_WAXED_WEATHERED_COPPER:
        place_simple_block(context, BLOCK_WAXED_WEATHERED_COPPER);
        break;
    case ITEM_WAXED_OXIDIZED_COPPER:
        place_simple_block(context, BLOCK_WAXED_OXIDIZED_COPPER);
        break;
    case ITEM_WAXED_CUT_COPPER:
        place_simple_block(context, BLOCK_WAXED_CUT_COPPER);
        break;
    case ITEM_WAXED_EXPOSED_CUT_COPPER:
        place_simple_block(context, BLOCK_WAXED_EXPOSED_CUT_COPPER);
        break;
    case ITEM_WAXED_WEATHERED_CUT_COPPER:
        place_simple_block(context, BLOCK_WAXED_WEATHERED_CUT_COPPER);
        break;
    case ITEM_WAXED_OXIDIZED_CUT_COPPER:
        place_simple_block(context, BLOCK_WAXED_OXIDIZED_CUT_COPPER);
        break;
    case ITEM_WAXED_CUT_COPPER_STAIRS:
        place_stairs(context, BLOCK_WAXED_CUT_COPPER_STAIRS);
        break;
    case ITEM_WAXED_EXPOSED_CUT_COPPER_STAIRS:
        place_stairs(context, BLOCK_WAXED_EXPOSED_CUT_COPPER_STAIRS);
        break;
    case ITEM_WAXED_WEATHERED_CUT_COPPER_STAIRS:
        place_stairs(context, BLOCK_WAXED_WEATHERED_CUT_COPPER_STAIRS);
        break;
    case ITEM_WAXED_OXIDIZED_CUT_COPPER_STAIRS:
        place_stairs(context, BLOCK_WAXED_OXIDIZED_CUT_COPPER_STAIRS);
        break;
    case ITEM_WAXED_CUT_COPPER_SLAB:
        place_slab(context, BLOCK_WAXED_CUT_COPPER_SLAB);
        break;
    case ITEM_WAXED_EXPOSED_CUT_COPPER_SLAB:
        place_slab(context, BLOCK_WAXED_EXPOSED_CUT_COPPER_SLAB);
        break;
    case ITEM_WAXED_WEATHERED_CUT_COPPER_SLAB:
        place_slab(context, BLOCK_WAXED_WEATHERED_CUT_COPPER_SLAB);
        break;
    case ITEM_WAXED_OXIDIZED_CUT_COPPER_SLAB:
        place_slab(context, BLOCK_WAXED_OXIDIZED_CUT_COPPER_SLAB);
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
    case ITEM_MANGROVE_LOG:
        place_simple_pillar(context, BLOCK_MANGROVE_LOG);
        break;
    case ITEM_MANGROVE_ROOTS:
        place_mangrove_roots(context, BLOCK_MANGROVE_ROOTS);
        break;
    case ITEM_MUDDY_MANGROVE_ROOTS:
        place_simple_pillar(context, BLOCK_MUDDY_MANGROVE_ROOTS);
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
    case ITEM_STRIPPED_MANGROVE_LOG:
        place_simple_pillar(context, BLOCK_STRIPPED_MANGROVE_LOG);
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
    case ITEM_STRIPPED_MANGROVE_WOOD:
        place_simple_pillar(context, BLOCK_STRIPPED_MANGROVE_WOOD);
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
    case ITEM_MANGROVE_WOOD:
        place_simple_pillar(context, BLOCK_MANGROVE_WOOD);
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
    case ITEM_MANGROVE_LEAVES:
        place_leaves(context, BLOCK_MANGROVE_LEAVES);
        break;
    case ITEM_AZALEA_LEAVES:
        place_leaves(context, BLOCK_AZALEA_LEAVES);
        break;
    case ITEM_FLOWERING_AZALEA_LEAVES:
        place_leaves(context, BLOCK_FLOWERING_AZALEA_LEAVES);
        break;
    case ITEM_SPONGE:
        break;
    case ITEM_WET_SPONGE:
        break;
    case ITEM_GLASS:
        place_simple_block(context, BLOCK_GLASS);
        break;
    case ITEM_TINTED_GLASS:
        place_simple_block(context, BLOCK_TINTED_GLASS);
        break;
    case ITEM_LAPIS_BLOCK:
        place_simple_block(context, BLOCK_LAPIS_BLOCK);
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
    case ITEM_COBWEB:
        place_simple_block(context, BLOCK_COBWEB);
        break;
    case ITEM_SHORT_GRASS:
        place_plant(context, BLOCK_SHORT_GRASS);
        break;
    case ITEM_FERN:
        place_plant(context, BLOCK_FERN);
        break;
    case ITEM_AZALEA:
        place_azalea(context, BLOCK_AZALEA);
        break;
    case ITEM_FLOWERING_AZALEA:
        place_azalea(context, BLOCK_FLOWERING_AZALEA);
        break;
    case ITEM_DEAD_BUSH:
        place_dead_bush(context, BLOCK_DEAD_BUSH);
        break;
    case ITEM_SEAGRASS:
        break;
    case ITEM_SEA_PICKLE:
        place_sea_pickle(context, BLOCK_SEA_PICKLE);
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
    case ITEM_SPORE_BLOSSOM:
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
    case ITEM_MOSS_CARPET:
        place_carpet(context, BLOCK_MOSS_CARPET);
        break;
    case ITEM_MOSS_BLOCK:
        place_simple_block(context, BLOCK_MOSS_BLOCK);
        break;
    case ITEM_HANGING_ROOTS:
        break;
    case ITEM_BIG_DRIPLEAF:
        break;
    case ITEM_SMALL_DRIPLEAF:
        break;
    case ITEM_BAMBOO:
        place_bamboo(context, BLOCK_BAMBOO);
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
    case ITEM_MANGROVE_SLAB:
        place_slab(context, BLOCK_MANGROVE_SLAB);
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
    case ITEM_MUD_BRICK_SLAB:
        place_slab(context, BLOCK_MUD_BRICK_SLAB);
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
    case ITEM_CHEST:
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
    case ITEM_COBBLESTONE_STAIRS:
        place_stairs(context, BLOCK_COBBLESTONE_STAIRS);
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
    case ITEM_MANGROVE_FENCE:
        place_fence(context, BLOCK_MANGROVE_FENCE);
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
    case ITEM_JACK_O_LANTERN:
        // @TODO(traks) spawn iron golem?
        place_horizontal_facing(context, BLOCK_JACK_O_LANTERN);
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
    case ITEM_SMOOTH_BASALT:
        place_simple_block(context, BLOCK_SMOOTH_BASALT);
        break;
    case ITEM_SOUL_TORCH:
        place_torch(context, BLOCK_SOUL_TORCH, BLOCK_SOUL_WALL_TORCH);
        break;
    case ITEM_GLOWSTONE:
        place_simple_block(context, BLOCK_GLOWSTONE);
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
    case ITEM_INFESTED_DEEPSLATE:
        place_simple_pillar(context, BLOCK_INFESTED_DEEPSLATE);
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
    case ITEM_PACKED_MUD:
        place_simple_block(context, BLOCK_PACKED_MUD);
        break;
    case ITEM_MUD_BRICKS:
        place_simple_block(context, BLOCK_MUD_BRICKS);
        break;
    case ITEM_DEEPSLATE_BRICKS:
        place_simple_block(context, BLOCK_DEEPSLATE_BRICKS);
        break;
    case ITEM_CRACKED_DEEPSLATE_BRICKS:
        place_simple_block(context, BLOCK_CRACKED_DEEPSLATE_BRICKS);
        break;
    case ITEM_DEEPSLATE_TILES:
        place_simple_block(context, BLOCK_DEEPSLATE_TILES);
        break;
    case ITEM_CRACKED_DEEPSLATE_TILES:
        place_simple_block(context, BLOCK_CRACKED_DEEPSLATE_TILES);
        break;
    case ITEM_CHISELED_DEEPSLATE:
        place_simple_block(context, BLOCK_CHISELED_DEEPSLATE);
        break;
    case ITEM_REINFORCED_DEEPSLATE:
        place_simple_block(context, BLOCK_REINFORCED_DEEPSLATE);
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
    case ITEM_GLOW_LICHEN:
        break;
    case ITEM_BRICK_STAIRS:
        place_stairs(context, BLOCK_BRICK_STAIRS);
        break;
    case ITEM_STONE_BRICK_STAIRS:
        place_stairs(context, BLOCK_STONE_BRICK_STAIRS);
        break;
    case ITEM_MUD_BRICK_STAIRS:
        place_stairs(context, BLOCK_MUD_BRICK_STAIRS);
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
    case ITEM_SCULK:
        break;
    case ITEM_SCULK_VEIN:
        break;
    case ITEM_SCULK_CATALYST:
        break;
    case ITEM_SCULK_SHRIEKER:
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
    case ITEM_SANDSTONE_STAIRS:
        place_stairs(context, BLOCK_SANDSTONE_STAIRS);
        break;
    case ITEM_ENDER_CHEST:
        break;
    case ITEM_EMERALD_BLOCK:
        place_simple_block(context, BLOCK_EMERALD_BLOCK);
        break;
    case ITEM_OAK_STAIRS:
        place_stairs(context, BLOCK_OAK_STAIRS);
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
    case ITEM_ACACIA_STAIRS:
        place_stairs(context, BLOCK_ACACIA_STAIRS);
        break;
    case ITEM_DARK_OAK_STAIRS:
        place_stairs(context, BLOCK_DARK_OAK_STAIRS);
        break;
    case ITEM_MANGROVE_STAIRS:
        place_stairs(context, BLOCK_MANGROVE_STAIRS);
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
    case ITEM_MUD_BRICK_WALL:
        place_wall(context, BLOCK_MUD_BRICK_WALL);
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
    case ITEM_COBBLED_DEEPSLATE_WALL:
        place_wall(context, BLOCK_COBBLED_DEEPSLATE_WALL);
        break;
    case ITEM_POLISHED_DEEPSLATE_WALL:
        place_wall(context, BLOCK_POLISHED_DEEPSLATE_WALL);
        break;
    case ITEM_DEEPSLATE_BRICK_WALL:
        place_wall(context, BLOCK_DEEPSLATE_BRICK_WALL);
        break;
    case ITEM_DEEPSLATE_TILE_WALL:
        place_wall(context, BLOCK_DEEPSLATE_TILE_WALL);
        break;
    case ITEM_ANVIL:
        break;
    case ITEM_CHIPPED_ANVIL:
        break;
    case ITEM_DAMAGED_ANVIL:
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
    case ITEM_LIGHT:
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
    case ITEM_PACKED_ICE:
        place_simple_block(context, BLOCK_PACKED_ICE);
        break;
    case ITEM_DIRT_PATH:
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
    case ITEM_COBBLED_DEEPSLATE_STAIRS:
        place_stairs(context, BLOCK_COBBLED_DEEPSLATE_STAIRS);
        break;
    case ITEM_POLISHED_DEEPSLATE_STAIRS:
        place_stairs(context, BLOCK_POLISHED_DEEPSLATE_STAIRS);
        break;
    case ITEM_DEEPSLATE_BRICK_STAIRS:
        place_stairs(context, BLOCK_DEEPSLATE_BRICK_STAIRS);
        break;
    case ITEM_DEEPSLATE_TILE_STAIRS:
        place_stairs(context, BLOCK_DEEPSLATE_TILE_STAIRS);
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
    case ITEM_COBBLED_DEEPSLATE_SLAB:
        place_slab(context, BLOCK_COBBLED_DEEPSLATE_SLAB);
        break;
    case ITEM_POLISHED_DEEPSLATE_SLAB:
        place_slab(context, BLOCK_POLISHED_DEEPSLATE_SLAB);
        break;
    case ITEM_DEEPSLATE_BRICK_SLAB:
        place_slab(context, BLOCK_DEEPSLATE_BRICK_SLAB);
        break;
    case ITEM_DEEPSLATE_TILE_SLAB:
        place_slab(context, BLOCK_DEEPSLATE_TILE_SLAB);
        break;
    case ITEM_SCAFFOLDING:
        break;




    case ITEM_REDSTONE:
        place_redstone_wire(context, BLOCK_REDSTONE_WIRE);
        break;
    case ITEM_REDSTONE_TORCH:
        place_torch(context, BLOCK_REDSTONE_TORCH, BLOCK_REDSTONE_WALL_TORCH);
        break;
    case ITEM_REDSTONE_BLOCK:
        place_simple_block(context, BLOCK_REDSTONE_BLOCK);
        break;
    case ITEM_REPEATER:
        break;
    case ITEM_COMPARATOR:
        break;
    case ITEM_PISTON:
        break;
    case ITEM_STICKY_PISTON:
        break;
    case ITEM_SLIME_BLOCK:
        place_simple_block(context, BLOCK_SLIME_BLOCK);
        break;
    case ITEM_HONEY_BLOCK:
        place_simple_block(context, BLOCK_HONEY_BLOCK);
        break;
    case ITEM_OBSERVER:
        break;
    case ITEM_HOPPER:
        break;
    case ITEM_DISPENSER:
        break;
    case ITEM_DROPPER:
        break;
    case ITEM_LECTERN:
        break;
    case ITEM_TARGET:
        place_simple_block(context, BLOCK_TARGET);
        break;
    case ITEM_LEVER:
        place_lever_or_button(context, BLOCK_LEVER);
        break;
    case ITEM_LIGHTNING_ROD:
        break;
    case ITEM_DAYLIGHT_DETECTOR:
        break;
    case ITEM_SCULK_SENSOR:
        break;
    case ITEM_TRIPWIRE_HOOK:
        break;
    case ITEM_TRAPPED_CHEST:
        break;
    case ITEM_TNT:
        place_simple_block(context, BLOCK_TNT);
        break;
    case ITEM_REDSTONE_LAMP:
        break;
    case ITEM_NOTE_BLOCK:
        break;
    case ITEM_STONE_BUTTON:
        place_lever_or_button(context, BLOCK_STONE_BUTTON);
        break;
    case ITEM_POLISHED_BLACKSTONE_BUTTON:
        place_lever_or_button(context, BLOCK_POLISHED_BLACKSTONE_BUTTON);
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
    case ITEM_MANGROVE_BUTTON:
        place_lever_or_button(context, BLOCK_MANGROVE_BUTTON);
        break;
    case ITEM_CRIMSON_BUTTON:
        place_lever_or_button(context, BLOCK_CRIMSON_BUTTON);
        break;
    case ITEM_WARPED_BUTTON:
        place_lever_or_button(context, BLOCK_WARPED_BUTTON);
        break;
    case ITEM_STONE_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_STONE_PRESSURE_PLATE);
        break;
    case ITEM_POLISHED_BLACKSTONE_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_POLISHED_BLACKSTONE_PRESSURE_PLATE);
        break;
    case ITEM_LIGHT_WEIGHTED_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_LIGHT_WEIGHTED_PRESSURE_PLATE);
        break;
    case ITEM_HEAVY_WEIGHTED_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_HEAVY_WEIGHTED_PRESSURE_PLATE);
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
    case ITEM_MANGROVE_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_MANGROVE_PRESSURE_PLATE);
        break;
    case ITEM_CRIMSON_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_CRIMSON_PRESSURE_PLATE);
        break;
    case ITEM_WARPED_PRESSURE_PLATE:
        place_pressure_plate(context, BLOCK_WARPED_PRESSURE_PLATE);
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
    case ITEM_MANGROVE_DOOR:
        place_door(context, BLOCK_MANGROVE_DOOR);
        break;
    case ITEM_CRIMSON_DOOR:
        place_door(context, BLOCK_CRIMSON_DOOR);
        break;
    case ITEM_WARPED_DOOR:
        place_door(context, BLOCK_WARPED_DOOR);
        break;
    case ITEM_IRON_TRAPDOOR:
        place_trapdoor(context, BLOCK_IRON_TRAPDOOR);
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
    case ITEM_MANGROVE_TRAPDOOR:
        place_trapdoor(context, BLOCK_MANGROVE_TRAPDOOR);
        break;
    case ITEM_CRIMSON_TRAPDOOR:
        place_trapdoor(context, BLOCK_CRIMSON_TRAPDOOR);
        break;
    case ITEM_WARPED_TRAPDOOR:
        place_trapdoor(context, BLOCK_WARPED_TRAPDOOR);
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
    case ITEM_MANGROVE_FENCE_GATE:
        place_fence_gate(context, BLOCK_MANGROVE_FENCE_GATE);
        break;
    case ITEM_CRIMSON_FENCE_GATE:
        place_fence_gate(context, BLOCK_CRIMSON_FENCE_GATE);
        break;
    case ITEM_WARPED_FENCE_GATE:
        place_fence_gate(context, BLOCK_WARPED_FENCE_GATE);
        break;
    case ITEM_POWERED_RAIL:
        break;
    case ITEM_DETECTOR_RAIL:
        break;
    case ITEM_RAIL:
        place_rail(context, BLOCK_RAIL);
        break;
    case ITEM_ACTIVATOR_RAIL:
        break;
    case ITEM_MINECART:
        break;
    case ITEM_CHEST_MINECART:
        break;
    case ITEM_FURNACE_MINECART:
        break;
    case ITEM_TNT_MINECART:
        break;
    case ITEM_HOPPER_MINECART:
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
    case ITEM_MANGROVE_SIGN:
        break;
    case ITEM_CRIMSON_SIGN:
        break;
    case ITEM_WARPED_SIGN:
        break;
    case ITEM_POWDER_SNOW_BUCKET:
        break;
    case ITEM_DRIED_KELP_BLOCK:
        place_simple_block(context, BLOCK_DRIED_KELP_BLOCK);
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
    case ITEM_ALLAY_SPAWN_EGG:
        break;
    case ITEM_AXOLOTL_SPAWN_EGG:
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
    case ITEM_FROG_SPAWN_EGG:
        break;
    case ITEM_GHAST_SPAWN_EGG:
        break;
    case ITEM_GLOW_SQUID_SPAWN_EGG:
        break;
    case ITEM_GOAT_SPAWN_EGG:
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
    case ITEM_TADPOLE_SPAWN_EGG:
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
    case ITEM_WARDEN_SPAWN_EGG:
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
    case ITEM_GLOW_ITEM_FRAME:
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
    case ITEM_MUSIC_DISC_OTHERSIDE:
        break;
    case ITEM_MUSIC_DISC_5:
        break;
    case ITEM_MUSIC_DISC_PIGSTEP:
        break;
    case ITEM_LOOM:
        place_horizontal_facing(context, BLOCK_LOOM);
        break;
    case ITEM_GOAT_HORN:
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
    case ITEM_GLOW_BERRIES:
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
    case ITEM_HONEYCOMB_BLOCK:
        place_simple_block(context, BLOCK_HONEYCOMB_BLOCK);
        break;
    case ITEM_LODESTONE:
        place_simple_block(context, BLOCK_LODESTONE);
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
    case ITEM_CANDLE:
        break;
    case ITEM_WHITE_CANDLE:
        break;
    case ITEM_ORANGE_CANDLE:
        break;
    case ITEM_MAGENTA_CANDLE:
        break;
    case ITEM_LIGHT_BLUE_CANDLE:
        break;
    case ITEM_YELLOW_CANDLE:
        break;
    case ITEM_LIME_CANDLE:
        break;
    case ITEM_PINK_CANDLE:
        break;
    case ITEM_GRAY_CANDLE:
        break;
    case ITEM_LIGHT_GRAY_CANDLE:
        break;
    case ITEM_CYAN_CANDLE:
        break;
    case ITEM_PURPLE_CANDLE:
        break;
    case ITEM_BLUE_CANDLE:
        break;
    case ITEM_BROWN_CANDLE:
        break;
    case ITEM_GREEN_CANDLE:
        break;
    case ITEM_RED_CANDLE:
        break;
    case ITEM_BLACK_CANDLE:
        break;
    case ITEM_SMALL_AMETHYST_BUD:
        place_amethyst_cluster(context, BLOCK_SMALL_AMETHYST_BUD);
        break;
    case ITEM_MEDIUM_AMETHYST_BUD:
        place_amethyst_cluster(context, BLOCK_MEDIUM_AMETHYST_BUD);
        break;
    case ITEM_LARGE_AMETHYST_BUD:
        place_amethyst_cluster(context, BLOCK_LARGE_AMETHYST_BUD);
        break;
    case ITEM_AMETHYST_CLUSTER:
        place_amethyst_cluster(context, BLOCK_AMETHYST_CLUSTER);
        break;
    case ITEM_POINTED_DRIPSTONE:
        break;
    case ITEM_OCHRE_FROGLIGHT:
        place_simple_pillar(context, BLOCK_OCHRE_FROGLIGHT);
        break;
    case ITEM_VERDANT_FROGLIGHT:
        place_simple_pillar(context, BLOCK_VERDANT_FROGLIGHT);
        break;
    case ITEM_PEARLESCENT_FROGLIGHT:
        place_simple_pillar(context, BLOCK_PEARLESCENT_FROGLIGHT);
        break;
    case ITEM_FROGSPAWN:
        break;
    default:
        // no use-on action for the remaining item types
        break;
    }

    propagate_block_updates(&buc);

    // @TODO(traks) perhaps don't send these packets if we do everything as the
    // client expects. Although a nice benefit of these packets is that clients
    // can update a desynced block by clicking on it.

    // @TODO(traks) we shouldn't assert here
    WorldBlockPos changed_pos = clickedPos;
    assert(control->changed_block_count < ARRAY_SIZE(control->changed_blocks));
    control->changed_blocks[control->changed_block_count] = changed_pos;
    control->changed_block_count++;

    changed_pos = WorldBlockPosRel(clickedPos, clicked_face);
    assert(control->changed_block_count < ARRAY_SIZE(control->changed_blocks));
    control->changed_blocks[control->changed_block_count] = changed_pos;
    control->changed_block_count++;
}

u8
get_max_stack_size(i32 item_type) {
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
    case ITEM_POWDER_SNOW_BUCKET:
    case ITEM_MILK_BUCKET:
    case ITEM_PUFFERFISH_BUCKET:
    case ITEM_SALMON_BUCKET:
    case ITEM_COD_BUCKET:
    case ITEM_TROPICAL_FISH_BUCKET:
    case ITEM_AXOLOTL_BUCKET:
    case ITEM_TADPOLE_BUCKET:
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
    case ITEM_OAK_BOAT:
    case ITEM_OAK_CHEST_BOAT:
    case ITEM_SPRUCE_BOAT:
    case ITEM_SPRUCE_CHEST_BOAT:
    case ITEM_BIRCH_BOAT:
    case ITEM_BIRCH_CHEST_BOAT:
    case ITEM_JUNGLE_BOAT:
    case ITEM_JUNGLE_CHEST_BOAT:
    case ITEM_ACACIA_BOAT:
    case ITEM_ACACIA_CHEST_BOAT:
    case ITEM_DARK_OAK_BOAT:
    case ITEM_DARK_OAK_CHEST_BOAT:
    case ITEM_MANGROVE_BOAT:
    case ITEM_MANGROVE_CHEST_BOAT:
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
    case ITEM_BUNDLE:
    case ITEM_SPYGLASS:
        return 1;

    case ITEM_OAK_SIGN:
    case ITEM_SPRUCE_SIGN:
    case ITEM_BIRCH_SIGN:
    case ITEM_JUNGLE_SIGN:
    case ITEM_ACACIA_SIGN:
    case ITEM_DARK_OAK_SIGN:
    case ITEM_MANGROVE_SIGN:
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
