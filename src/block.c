#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "shared.h"

// @TODO(traks) An issue with having a fixed update order, is that redstone
// contraptions can break if they're rotated. Perhaps making the update order
// depend on the direction from which the update was triggered, will make it
// rotation independent? Reflectional symmetry seems to be an issue still
// though.
static unsigned char update_order[] = {
    DIRECTION_NEG_X, DIRECTION_POS_X,
    DIRECTION_NEG_Z, DIRECTION_POS_Z,
    DIRECTION_NEG_Y, DIRECTION_POS_Y,
};

static int
get_horizontal_direction_index(int dir) {
    switch (dir) {
    case DIRECTION_POS_X: return 0;
    case DIRECTION_NEG_Z: return 1;
    case DIRECTION_POS_Z: return 2;
    case DIRECTION_NEG_X: return 3;
    default:
        assert(0);
        return 0;
    }
}

static void
push_block_update(BlockPos pos, int from_dir,
        block_update_context * buc) {
    if (buc->update_count >= buc->max_updates) {
        return;
    }
    buc->blocks_to_update[buc->update_count] = (block_update) {
        .pos = pos,
        .from_direction = from_dir,
    };
    buc->update_count++;
}

static void
push_neighbour_block_update(BlockPos pos, int dir,
        block_update_context * buc) {
    push_block_update(get_relative_block_pos(pos, dir),
            get_opposite_direction(dir), buc);
}

void
push_direct_neighbour_block_updates(BlockPos pos,
        block_update_context * buc) {
    if (buc->max_updates - buc->update_count < 6) {
        return;
    }

    for (int j = 0; j < 6; j++) {
        int to_direction = update_order[j];
        BlockPos neighbour = get_relative_block_pos(pos, to_direction);
        buc->blocks_to_update[buc->update_count] = (block_update) {
            .pos = neighbour,
            .from_direction = get_opposite_direction(to_direction),
        };
        buc->update_count++;
    }
}

static void
schedule_block_update(BlockPos pos, int from_direction, int delay) {
    assert(delay > 0);
    int count = serv->scheduled_block_update_count;
    if (count == ARRAY_SIZE(serv->scheduled_block_updates)) {
        // @TODO(traks) do something better once we revamp the scheduled block
        // update data structure
        assert(0);
    }

    scheduled_block_update new = {
        .pos = pos,
        .from_direction = from_direction,
        .for_tick = serv->current_tick + delay
    };
    serv->scheduled_block_updates[count] = new;
    serv->scheduled_block_update_count++;
}

static void
mark_block_state_property(block_state_info * info, int prop) {
    info->available_properties[prop >> 6] |= (u64) 1 << (prop & 0x3f);
}

int
has_block_state_property(block_state_info * info, int prop) {
    return !!(info->available_properties[prop >> 6] & ((u64) 1 << (prop & 0x3f)));
}

block_state_info
describe_block_state(u16 block_state) {
    block_state_info res = {0};
    i32 block_type = serv->block_type_by_state[block_state];
    block_properties * props = serv->block_properties_table + block_type;
    u16 base_state = props->base_state;
    int offset = block_state - base_state;

    for (int i = props->property_count - 1; i >= 0; i--) {
        int id = props->property_specs[i];
        block_property_spec * spec = serv->block_property_specs + id;
        int value_index = offset % spec->value_count;

        mark_block_state_property(&res, id);

        // decode value index for easier use
        int value;
        switch (id) {
        case BLOCK_PROPERTY_ATTACHED:
        case BLOCK_PROPERTY_BOTTOM:
        case BLOCK_PROPERTY_CONDITIONAL:
        case BLOCK_PROPERTY_DISARMED:
        case BLOCK_PROPERTY_DRAG:
        case BLOCK_PROPERTY_ENABLED:
        case BLOCK_PROPERTY_EXTENDED:
        case BLOCK_PROPERTY_EYE:
        case BLOCK_PROPERTY_FALLING:
        case BLOCK_PROPERTY_HANGING:
        case BLOCK_PROPERTY_HAS_BOTTLE_0:
        case BLOCK_PROPERTY_HAS_BOTTLE_1:
        case BLOCK_PROPERTY_HAS_BOTTLE_2:
        case BLOCK_PROPERTY_HAS_RECORD:
        case BLOCK_PROPERTY_HAS_BOOK:
        case BLOCK_PROPERTY_INVERTED:
        case BLOCK_PROPERTY_IN_WALL:
        case BLOCK_PROPERTY_LIT:
        case BLOCK_PROPERTY_LOCKED:
        case BLOCK_PROPERTY_OCCUPIED:
        case BLOCK_PROPERTY_OPEN:
        case BLOCK_PROPERTY_PERSISTENT:
        case BLOCK_PROPERTY_POWERED:
        case BLOCK_PROPERTY_SHORT_PISTON:
        case BLOCK_PROPERTY_SIGNAL_FIRE:
        case BLOCK_PROPERTY_SNOWY:
        case BLOCK_PROPERTY_TRIGGERED:
        case BLOCK_PROPERTY_UNSTABLE:
        case BLOCK_PROPERTY_WATERLOGGED:
        case BLOCK_PROPERTY_VINE_END:
        case BLOCK_PROPERTY_BERRIES:
        case BLOCK_PROPERTY_NEG_Y:
        case BLOCK_PROPERTY_POS_Y:
        case BLOCK_PROPERTY_NEG_Z:
        case BLOCK_PROPERTY_POS_Z:
        case BLOCK_PROPERTY_NEG_X:
        case BLOCK_PROPERTY_POS_X:
            value = !value_index;
            break;
        case BLOCK_PROPERTY_HORIZONTAL_AXIS:
            value = value_index == 0 ? AXIS_X : AXIS_Z;
            break;
        case BLOCK_PROPERTY_FACING:
            switch (value_index) {
            case 0: value = DIRECTION_NEG_Z; break;
            case 1: value = DIRECTION_POS_X; break;
            case 2: value = DIRECTION_POS_Z; break;
            case 3: value = DIRECTION_NEG_X; break;
            case 4: value = DIRECTION_POS_Y; break;
            case 5: value = DIRECTION_NEG_Y; break;
            }
            break;
        case BLOCK_PROPERTY_FACING_HOPPER:
            switch (value_index) {
            case 0: value = DIRECTION_NEG_Y; break;
            case 1: value = DIRECTION_NEG_Z; break;
            case 2: value = DIRECTION_POS_Z; break;
            case 3: value = DIRECTION_NEG_X; break;
            case 4: value = DIRECTION_POS_X; break;
            }
            break;
        case BLOCK_PROPERTY_HORIZONTAL_FACING:
            switch (value_index) {
            case 0: value = DIRECTION_NEG_Z; break;
            case 1: value = DIRECTION_POS_Z; break;
            case 2: value = DIRECTION_NEG_X; break;
            case 3: value = DIRECTION_POS_X; break;
            }
            break;
        case BLOCK_PROPERTY_CANDLES:
        case BLOCK_PROPERTY_DELAY:
        case BLOCK_PROPERTY_DISTANCE:
        case BLOCK_PROPERTY_EGGS:
        case BLOCK_PROPERTY_LAYERS:
        case BLOCK_PROPERTY_PICKLES:
        case BLOCK_PROPERTY_ROTATION_16:
            value = value_index + 1;
            break;
        case BLOCK_PROPERTY_LEVEL:
            switch (value_index) {
            case 0: value = FLUID_LEVEL_SOURCE; break;
            case 1: value = FLUID_LEVEL_FLOWING_7; break;
            case 2: value = FLUID_LEVEL_FLOWING_6; break;
            case 3: value = FLUID_LEVEL_FLOWING_5; break;
            case 4: value = FLUID_LEVEL_FLOWING_4; break;
            case 5: value = FLUID_LEVEL_FLOWING_3; break;
            case 6: value = FLUID_LEVEL_FLOWING_2; break;
            case 7: value = FLUID_LEVEL_FLOWING_1; break;
            // minecraft doesn't distinguish falling levels [8, 15]
            default: value = FLUID_LEVEL_FALLING; break;
            }
            break;
        case BLOCK_PROPERTY_REDSTONE_POS_X:
        case BLOCK_PROPERTY_REDSTONE_NEG_Z:
        case BLOCK_PROPERTY_REDSTONE_POS_Z:
        case BLOCK_PROPERTY_REDSTONE_NEG_X:
            switch (value_index) {
            case 0: value = REDSTONE_SIDE_UP; break;
            case 1: value = REDSTONE_SIDE_SIDE; break;
            case 2: value = REDSTONE_SIDE_NONE; break;
            }
            break;
        case BLOCK_PROPERTY_VERTICAL_DIRECTION:
            switch (value_index) {
            case 0: value = DIRECTION_POS_Y; break;
            case 1: value = DIRECTION_NEG_Y; break;
            }
            break;
        default:
            value = value_index;
        }

        res.values[id] = value;

        offset = offset / spec->value_count;
    }

    res.block_type = block_type;
    return res;
}

u16
get_default_block_state(i32 block_type) {
    block_properties * props = serv->block_properties_table + block_type;
    int offset = 0;

    for (int i = 0; i < props->property_count; i++) {
        int id = props->property_specs[i];
        int value_index = props->default_value_indices[i];
        int value_count = serv->block_property_specs[id].value_count;

        offset = offset * value_count + value_index;
    }

    u16 default_state = props->base_state + offset;
    return default_state;
}

block_state_info
describe_default_block_state(i32 block_type) {
    return describe_block_state(get_default_block_state(block_type));
}

u16
make_block_state(block_state_info * info) {
    block_properties * props = serv->block_properties_table + info->block_type;
    int offset = 0;

    for (int i = 0; i < props->property_count; i++) {
        int id = props->property_specs[i];

        int value = info->values[id];
        int value_index;
        // convert value to value index
        switch (id) {
        case BLOCK_PROPERTY_ATTACHED:
        case BLOCK_PROPERTY_BOTTOM:
        case BLOCK_PROPERTY_CONDITIONAL:
        case BLOCK_PROPERTY_DISARMED:
        case BLOCK_PROPERTY_DRAG:
        case BLOCK_PROPERTY_ENABLED:
        case BLOCK_PROPERTY_EXTENDED:
        case BLOCK_PROPERTY_EYE:
        case BLOCK_PROPERTY_FALLING:
        case BLOCK_PROPERTY_HANGING:
        case BLOCK_PROPERTY_HAS_BOTTLE_0:
        case BLOCK_PROPERTY_HAS_BOTTLE_1:
        case BLOCK_PROPERTY_HAS_BOTTLE_2:
        case BLOCK_PROPERTY_HAS_RECORD:
        case BLOCK_PROPERTY_HAS_BOOK:
        case BLOCK_PROPERTY_INVERTED:
        case BLOCK_PROPERTY_IN_WALL:
        case BLOCK_PROPERTY_LIT:
        case BLOCK_PROPERTY_LOCKED:
        case BLOCK_PROPERTY_OCCUPIED:
        case BLOCK_PROPERTY_OPEN:
        case BLOCK_PROPERTY_PERSISTENT:
        case BLOCK_PROPERTY_POWERED:
        case BLOCK_PROPERTY_SHORT_PISTON:
        case BLOCK_PROPERTY_SIGNAL_FIRE:
        case BLOCK_PROPERTY_SNOWY:
        case BLOCK_PROPERTY_TRIGGERED:
        case BLOCK_PROPERTY_UNSTABLE:
        case BLOCK_PROPERTY_WATERLOGGED:
        case BLOCK_PROPERTY_VINE_END:
        case BLOCK_PROPERTY_BERRIES:
        case BLOCK_PROPERTY_NEG_Y:
        case BLOCK_PROPERTY_POS_Y:
        case BLOCK_PROPERTY_NEG_Z:
        case BLOCK_PROPERTY_POS_Z:
        case BLOCK_PROPERTY_NEG_X:
        case BLOCK_PROPERTY_POS_X:
            value_index = !value;
            break;
        case BLOCK_PROPERTY_HORIZONTAL_AXIS:
            value_index = value == AXIS_X ? 0 : 1;
            break;
        case BLOCK_PROPERTY_FACING:
            switch (value) {
            case DIRECTION_NEG_Z: value_index = 0; break;
            case DIRECTION_POS_X: value_index = 1; break;
            case DIRECTION_POS_Z: value_index = 2; break;
            case DIRECTION_NEG_X: value_index = 3; break;
            case DIRECTION_POS_Y: value_index = 4; break;
            case DIRECTION_NEG_Y: value_index = 5; break;
            }
            break;
        case BLOCK_PROPERTY_FACING_HOPPER:
            switch (value) {
            case DIRECTION_NEG_Y: value_index = 0; break;
            case DIRECTION_NEG_Z: value_index = 1; break;
            case DIRECTION_POS_Z: value_index = 2; break;
            case DIRECTION_NEG_X: value_index = 3; break;
            case DIRECTION_POS_X: value_index = 4; break;
            }
            break;
        case BLOCK_PROPERTY_HORIZONTAL_FACING:
            switch (value) {
            case DIRECTION_NEG_Z: value_index = 0; break;
            case DIRECTION_POS_Z: value_index = 1; break;
            case DIRECTION_NEG_X: value_index = 2; break;
            case DIRECTION_POS_X: value_index = 3; break;
            }
            break;
        case BLOCK_PROPERTY_CANDLES:
        case BLOCK_PROPERTY_DELAY:
        case BLOCK_PROPERTY_DISTANCE:
        case BLOCK_PROPERTY_EGGS:
        case BLOCK_PROPERTY_LAYERS:
        case BLOCK_PROPERTY_PICKLES:
        case BLOCK_PROPERTY_ROTATION_16:
            value_index = value - 1;
            break;
        case BLOCK_PROPERTY_LEVEL:
            switch (value) {
            case FLUID_LEVEL_SOURCE: value_index = 0; break;
            case FLUID_LEVEL_FLOWING_7: value_index = 1; break;
            case FLUID_LEVEL_FLOWING_6: value_index = 2; break;
            case FLUID_LEVEL_FLOWING_5: value_index = 3; break;
            case FLUID_LEVEL_FLOWING_4: value_index = 4; break;
            case FLUID_LEVEL_FLOWING_3: value_index = 5; break;
            case FLUID_LEVEL_FLOWING_2: value_index = 6; break;
            case FLUID_LEVEL_FLOWING_1: value_index = 7; break;
            case FLUID_LEVEL_FALLING: value_index = 8; break;
            }
            break;
        case BLOCK_PROPERTY_REDSTONE_POS_X:
        case BLOCK_PROPERTY_REDSTONE_NEG_Z:
        case BLOCK_PROPERTY_REDSTONE_POS_Z:
        case BLOCK_PROPERTY_REDSTONE_NEG_X:
            switch (value) {
            case REDSTONE_SIDE_UP: value_index = 0; break;
            case REDSTONE_SIDE_SIDE: value_index = 1; break;
            case REDSTONE_SIDE_NONE: value_index = 2; break;
            }
            break;
        case BLOCK_PROPERTY_VERTICAL_DIRECTION:
            switch (value_index) {
            case DIRECTION_POS_Y: value_index = 0; break;
            case DIRECTION_NEG_Y: value_index = 1; break;
            }
            break;
        default:
            value_index = value;
        }

        int value_count = serv->block_property_specs[id].value_count;

        offset = offset * value_count + value_index;
    }

    u16 res = props->base_state + offset;
    return res;
}

static void
break_block(BlockPos pos) {
    u16 cur_state = try_get_block_state(pos);
    i32 cur_type = serv->block_type_by_state[cur_state];

    // @TODO(traks) block setting for this
    if (cur_type != BLOCK_FIRE && cur_type != BLOCK_SOUL_FIRE) {
        chunk_pos ch_pos = {
            .x = pos.x >> 4,
            .z = pos.z >> 4
        };
        chunk * ch = get_chunk_if_loaded(ch_pos);
        if (ch != NULL) {
            if (ch->local_event_count < ARRAY_SIZE(ch->local_events)) {
                ch->local_events[ch->local_event_count] = (level_event) {
                    .type = LEVEL_EVENT_PARTICLES_DESTROY_BLOCK,
                    .pos = pos,
                    .data = cur_state,
                };
                ch->local_event_count++;
            }
        }
    }

    try_set_block_state(pos, get_default_block_state(BLOCK_AIR));
}

// used to check whether redstone power travels through a block state. Also used
// to check whether redstone wire connects diagonally through a block state;
// this function returns false for those states.
static int
conducts_redstone(u16 block_state, BlockPos pos) {
    i32 block_type = serv->block_type_by_state[block_state];

    switch (block_type) {
    // A bunch of materials are excluded in this list: air, carpets and a bunch
    // of other blocks that have non-full collision box.
    // plants
    case BLOCK_CHORUS_PLANT:
    case BLOCK_CHORUS_FLOWER:
    // leaves
    case BLOCK_JUNGLE_LEAVES:
    case BLOCK_OAK_LEAVES:
    case BLOCK_SPRUCE_LEAVES:
    case BLOCK_DARK_OAK_LEAVES:
    case BLOCK_ACACIA_LEAVES:
    case BLOCK_BIRCH_LEAVES:
    // glass
    case BLOCK_GLASS:
    case BLOCK_GLOWSTONE:
    case BLOCK_BEACON:
    case BLOCK_SEA_LANTERN:
    case BLOCK_CONDUIT:
    case BLOCK_WHITE_STAINED_GLASS:
    case BLOCK_ORANGE_STAINED_GLASS:
    case BLOCK_MAGENTA_STAINED_GLASS:
    case BLOCK_LIGHT_BLUE_STAINED_GLASS:
    case BLOCK_YELLOW_STAINED_GLASS:
    case BLOCK_LIME_STAINED_GLASS:
    case BLOCK_PINK_STAINED_GLASS:
    case BLOCK_GRAY_STAINED_GLASS:
    case BLOCK_LIGHT_GRAY_STAINED_GLASS:
    case BLOCK_CYAN_STAINED_GLASS:
    case BLOCK_PURPLE_STAINED_GLASS:
    case BLOCK_BLUE_STAINED_GLASS:
    case BLOCK_BROWN_STAINED_GLASS:
    case BLOCK_GREEN_STAINED_GLASS:
    case BLOCK_RED_STAINED_GLASS:
    case BLOCK_BLACK_STAINED_GLASS:
    case BLOCK_TINTED_GLASS:
    // misc
    case BLOCK_SCAFFOLDING:
    case BLOCK_TNT:
    case BLOCK_ICE:
    case BLOCK_FROSTED_ICE:
    // extra (explicit blocking)
    case BLOCK_REDSTONE_BLOCK:
    case BLOCK_OBSERVER:
    case BLOCK_PISTON:
    case BLOCK_STICKY_PISTON:
    case BLOCK_MOVING_PISTON:
        return 0;
    // explicitly allowed
    case BLOCK_SOUL_SAND:
        return 1;
    default: {
        block_model model = get_collision_model(block_state, pos);
        if (model.flags & BLOCK_MODEL_IS_FULL) {
            return 1;
        }
        return 0;
    }
    }
}

static void
translate_model(block_model * model, float dx, float dy, float dz) {
    for (int i = 0; i < model->box_count; i++) {
        model->boxes[i].min_x += dx;
        model->boxes[i].min_y += dy;
        model->boxes[i].min_z += dz;
        model->boxes[i].max_x += dx;
        model->boxes[i].max_y += dy;
        model->boxes[i].max_z += dz;
    }
}

block_model
get_collision_model(u16 block_state, BlockPos pos) {
    i32 block_type = serv->block_type_by_state[block_state];
    block_model res;

    switch (block_type) {
    case BLOCK_BAMBOO: {
        u64 seed = ((u64) pos.x * 3129871) ^ ((u64) pos.z * 116129781);
        seed = seed * seed * 42317861 + seed * 11;
        seed >>= 16;
        res = serv->block_models[serv->collision_model_by_state[block_state]];
        translate_model(&res, ((seed & 0xf) / 15.0f - 0.5f) * 0.5f, 0,
                (((seed >> 8) & 0xf) / 15.0f - 0.5f) * 0.5f);
        break;
    }
    case BLOCK_WATER:
    case BLOCK_LAVA: {
        // @TODO(traks) let striders walk on water/lava source blocks
        res = serv->block_models[serv->collision_model_by_state[block_state]];
        break;
    }
    case BLOCK_MOVING_PISTON: {
        // @TODO(traks) use block entity to determine collision model
        res = serv->block_models[serv->collision_model_by_state[block_state]];
        break;
    }
    case BLOCK_SCAFFOLDING: {
        // @TODO(traks) collision model should depend on whether entity is
        // shifting
        res = serv->block_models[serv->collision_model_by_state[block_state]];
        break;
    }
    case BLOCK_POWDER_SNOW: {
        // @TODO(traks) collision model depends on what the entity is wearing
        // and some other stuff that depends on the entity and the surroundings
        // in the world
        res = serv->block_models[serv->collision_model_by_state[block_state]];
        break;
    }
    default:
        res = serv->block_models[serv->collision_model_by_state[block_state]];
    }
    return res;
}

support_model
get_support_model(u16 block_state) {
    i32 block_type = serv->block_type_by_state[block_state];
    support_model res;

    switch (block_type) {
    // block types with special support models
    case BLOCK_JUNGLE_LEAVES:
    case BLOCK_OAK_LEAVES:
    case BLOCK_SPRUCE_LEAVES:
    case BLOCK_DARK_OAK_LEAVES:
    case BLOCK_ACACIA_LEAVES:
    case BLOCK_BIRCH_LEAVES:
    case BLOCK_AZALEA_LEAVES:
    case BLOCK_FLOWERING_AZALEA_LEAVES:
        res = serv->support_models[BLOCK_MODEL_EMPTY];
        break;
    case BLOCK_SNOW:
        // @TODO(traks)
        break;
    case BLOCK_SOUL_SAND:
        res = serv->support_models[BLOCK_MODEL_FULL];
        break;
    // some block types have special collision models and therefore also have
    // special support models
    case BLOCK_BAMBOO:
        // @TODO(traks) I don't think bamboo can ever support a block, but
        // should make sure this is the case
        res = serv->support_models[BLOCK_MODEL_EMPTY];
        break;
    case BLOCK_MOVING_PISTON: {
        // @TODO(traks) use block entity to determine support model
        res = serv->support_models[serv->collision_model_by_state[block_state]];
        break;
    }
    case BLOCK_SCAFFOLDING: {
        // @TODO(traks) what should we return here?
        res = serv->support_models[serv->collision_model_by_state[block_state]];
        break;
    }
    case BLOCK_POWDER_SNOW: {
        res = serv->support_models[BLOCK_MODEL_EMPTY];
        break;
    }
    default:
        // default to using the collision model as support model
        res = serv->support_models[serv->collision_model_by_state[block_state]];
    }
    return res;
}

int
get_water_level(u16 state) {
    block_state_info info = describe_block_state(state);

    switch (info.block_type) {
    // @TODO(traks) return this for lava as well?
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

int
is_water_source(u16 state) {
    return get_water_level(state) == FLUID_LEVEL_SOURCE;
}

int
is_full_water(u16 state) {
    int level = get_water_level(state);
    // @TODO(traks) maybe ensure falling level is 8, although Minecraft doesn't
    // differentiate between falling water levels
    return level == FLUID_LEVEL_SOURCE || level == FLUID_LEVEL_FALLING;
}

int
can_plant_survive_on(i32 type_below) {
    // @TODO(traks) actually this is dirt block tag + farmland block
    switch (type_below) {
    case BLOCK_DIRT:
    case BLOCK_GRASS_BLOCK:
    case BLOCK_PODZOL:
    case BLOCK_COARSE_DIRT:
    case BLOCK_MYCELIUM:
    case BLOCK_ROOTED_DIRT:
    case BLOCK_MOSS_BLOCK:
    case BLOCK_FARMLAND:
        return 1;
    default:
        return 0;
    }
}

int
can_lily_pad_survive_on(u16 state_below) {
    // @TODO(traks) state at lily pad location must not contain fluid if lily
    // pad is being placed
    if (is_water_source(state_below)) {
        return 1;
    }

    i32 type_below = serv->block_type_by_state[state_below];
    switch (type_below) {
    case BLOCK_ICE:
    case BLOCK_FROSTED_ICE:
    case BLOCK_PACKED_ICE:
    case BLOCK_BLUE_ICE:
        return 1;
    default:
        return 0;
    }
}

int
can_carpet_survive_on(i32 type_below) {
    switch (type_below) {
    case BLOCK_AIR:
    case BLOCK_VOID_AIR:
    case BLOCK_CAVE_AIR:
        return 0;
    default:
        return 1;
    }
}

int
can_dead_bush_survive_on(i32 type_below) {
    switch (type_below) {
    case BLOCK_SAND:
    case BLOCK_RED_SAND:
    case BLOCK_TERRACOTTA:
    case BLOCK_WHITE_TERRACOTTA:
    case BLOCK_ORANGE_TERRACOTTA:
    case BLOCK_MAGENTA_TERRACOTTA:
    case BLOCK_LIGHT_BLUE_TERRACOTTA:
    case BLOCK_YELLOW_TERRACOTTA:
    case BLOCK_LIME_TERRACOTTA:
    case BLOCK_PINK_TERRACOTTA:
    case BLOCK_GRAY_TERRACOTTA:
    case BLOCK_LIGHT_GRAY_TERRACOTTA:
    case BLOCK_CYAN_TERRACOTTA:
    case BLOCK_PURPLE_TERRACOTTA:
    case BLOCK_BLUE_TERRACOTTA:
    case BLOCK_BROWN_TERRACOTTA:
    case BLOCK_GREEN_TERRACOTTA:
    case BLOCK_RED_TERRACOTTA:
    case BLOCK_BLACK_TERRACOTTA:
    // block tag dirt
    case BLOCK_DIRT:
    case BLOCK_GRASS_BLOCK:
    case BLOCK_PODZOL:
    case BLOCK_COARSE_DIRT:
    case BLOCK_MYCELIUM:
    case BLOCK_ROOTED_DIRT:
    case BLOCK_MOSS_BLOCK:
        return 1;
    default:
        return 0;
    }
}

int
can_wither_rose_survive_on(i32 type_below) {
    switch (type_below) {
    case BLOCK_NETHERRACK:
    case BLOCK_SOUL_SAND:
    case BLOCK_SOUL_SOIL:
        return 1;
    default:
        return can_plant_survive_on(type_below);
    }
}

int
can_azalea_survive_on(i32 type_below) {
    switch (type_below) {
    case BLOCK_CLAY:
        return 1;
    default:
        return can_plant_survive_on(type_below);
    }
}

int
can_big_dripleaf_survive_on(u16 state_below) {
    i32 type_below = serv->block_type_by_state[state_below];
    switch (type_below) {
    case BLOCK_BIG_DRIPLEAF_STEM:
    case BLOCK_BIG_DRIPLEAF:
        return 1;
    default: {
        support_model support = get_support_model(state_below);
        if (support.full_face_flags & (1 << DIRECTION_POS_Y)) {
            return 1;
        }
        return 0;
    }
    }
}

int
can_big_dripleaf_stem_survive_at(BlockPos cur_pos) {
    u16 state_below = try_get_block_state(
            get_relative_block_pos(cur_pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];
    support_model support_below = get_support_model(state_below);
    u16 state_above = try_get_block_state(
            get_relative_block_pos(cur_pos, DIRECTION_POS_Y));
    i32 type_above = serv->block_type_by_state[state_above];

    if ((type_below == BLOCK_BIG_DRIPLEAF_STEM || support_below.full_face_flags & (1 << DIRECTION_POS_Y))
            && (type_above == BLOCK_BIG_DRIPLEAF_STEM || type_above == BLOCK_BIG_DRIPLEAF)) {
        return 1;
    }
    return 0;
}

int
can_small_dripleaf_survive_at(i32 type_below, u16 cur_state) {
    switch (type_below) {
    // small dripleaf placeable block tag
    case BLOCK_CLAY:
    case BLOCK_MOSS_BLOCK:
        return 1;
    default:
        if (is_water_source(cur_state) && can_plant_survive_on(type_below)) {
            return 1;
        }
        return 0;
    }
}

int
can_nether_plant_survive_on(i32 type_below) {
    switch (type_below) {
    case BLOCK_SOUL_SOIL:
    // nylium block tag
    case BLOCK_WARPED_NYLIUM:
    case BLOCK_CRIMSON_NYLIUM:
        return 1;
    default:
        return can_plant_survive_on(type_below);
    }
}

int
is_bamboo_plantable_on(i32 type_below) {
    switch (type_below) {
    // bamboo plantable on block tag
    case BLOCK_SAND:
    case BLOCK_RED_SAND:
    case BLOCK_DIRT:
    case BLOCK_GRASS_BLOCK:
    case BLOCK_PODZOL:
    case BLOCK_COARSE_DIRT:
    case BLOCK_MYCELIUM:
    case BLOCK_ROOTED_DIRT:
    case BLOCK_MOSS_BLOCK:
    case BLOCK_BAMBOO:
    case BLOCK_BAMBOO_SAPLING:
    case BLOCK_GRAVEL:
        return 1;
    default:
        return 0;
    }
}

int
can_sea_pickle_survive_on(u16 state_below) {
    // @TODO(traks) is this correct?
    support_model support = get_support_model(state_below);
    if (support.non_empty_face_flags & (1 << DIRECTION_POS_Y)) {
        return 1;
    }
    return 0;
}

int
can_snow_survive_on(u16 state_below) {
    i32 type_below = serv->block_type_by_state[state_below];
    switch (type_below) {
    case BLOCK_ICE:
    case BLOCK_PACKED_ICE:
    case BLOCK_BARRIER:
        return 0;
    case BLOCK_HONEY_BLOCK:
    case BLOCK_SOUL_SAND:
        return 1;
    default: {
        // @TODO(traks) is this the correct model to use?
        support_model support = get_support_model(state_below);
        if (support.full_face_flags & (1 << DIRECTION_POS_Y)) {
            return 1;
        }
        return 0;
    }
    }
}

int
can_pressure_plate_survive_on(u16 state_below) {
    // @TODO(traks) can survive if top face is circle too (e.g. cauldron)
    support_model support = get_support_model(state_below);
    if (support.pole_face_flags & (1 << DIRECTION_POS_Y)) {
        return 1;
    }
    return 0;
}

int
can_redstone_wire_survive_on(u16 state_below) {
    support_model support = get_support_model(state_below);
    i32 type_below = serv->block_type_by_state[state_below];
    if (support.full_face_flags & (1 << DIRECTION_POS_Y)) {
        return 1;
    } else if (type_below == BLOCK_HOPPER) {
        return 1;
    }
    return 0;
}

int
can_sugar_cane_survive_at(BlockPos cur_pos) {
    u16 state_below = try_get_block_state(
            get_relative_block_pos(cur_pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];

    switch (type_below) {
    case BLOCK_SUGAR_CANE:
        return 1;
    case BLOCK_GRASS_BLOCK:
    case BLOCK_DIRT:
    case BLOCK_COARSE_DIRT:
    case BLOCK_PODZOL:
    case BLOCK_SAND:
    case BLOCK_RED_SAND: {
        BlockPos neighbour_pos[4];
        for (int i = 0; i < 4; i++) {
            BlockPos pos = cur_pos;
            pos.y--;
            neighbour_pos[i] = pos;
        }
        neighbour_pos[0].x--;
        neighbour_pos[1].x++;
        neighbour_pos[2].z--;
        neighbour_pos[3].z++;

        // check blocks next to ground block for water
        for (int i = 0; i < 4; i++) {
            BlockPos pos = neighbour_pos[i];
            u16 neighbour_state = try_get_block_state(pos);
            i32 neighbour_type = serv->block_type_by_state[neighbour_state];
            switch (neighbour_type) {
            case BLOCK_FROSTED_ICE:
                return 1;
            default:
                if (get_water_level(neighbour_state) != FLUID_LEVEL_NONE) {
                    return 1;
                }
            }
        }

        // no water found
        return 0;
    }
    default:
        return 0;
    }
}

// @TODO(traks) maybe store this kind of info in block properties and block
// state info structs. Use stairs block tag?
static int
is_stairs(i32 block_type) {
    switch (block_type) {
    case BLOCK_OAK_STAIRS:
    case BLOCK_SPRUCE_STAIRS:
    case BLOCK_BIRCH_STAIRS:
    case BLOCK_JUNGLE_STAIRS:
    case BLOCK_ACACIA_STAIRS:
    case BLOCK_DARK_OAK_STAIRS:
    case BLOCK_CRIMSON_STAIRS:
    case BLOCK_WARPED_STAIRS:
    case BLOCK_COBBLESTONE_STAIRS:
    case BLOCK_SANDSTONE_STAIRS:
    case BLOCK_NETHER_BRICK_STAIRS:
    case BLOCK_STONE_BRICK_STAIRS:
    case BLOCK_BRICK_STAIRS:
    case BLOCK_PURPUR_STAIRS:
    case BLOCK_QUARTZ_STAIRS:
    case BLOCK_RED_SANDSTONE_STAIRS:
    case BLOCK_PRISMARINE_BRICK_STAIRS:
    case BLOCK_PRISMARINE_STAIRS:
    case BLOCK_DARK_PRISMARINE_STAIRS:
    case BLOCK_POLISHED_GRANITE_STAIRS:
    case BLOCK_SMOOTH_RED_SANDSTONE_STAIRS:
    case BLOCK_MOSSY_STONE_BRICK_STAIRS:
    case BLOCK_POLISHED_DIORITE_STAIRS:
    case BLOCK_MOSSY_COBBLESTONE_STAIRS:
    case BLOCK_END_STONE_BRICK_STAIRS:
    case BLOCK_STONE_STAIRS:
    case BLOCK_SMOOTH_SANDSTONE_STAIRS:
    case BLOCK_SMOOTH_QUARTZ_STAIRS:
    case BLOCK_GRANITE_STAIRS:
    case BLOCK_ANDESITE_STAIRS:
    case BLOCK_RED_NETHER_BRICK_STAIRS:
    case BLOCK_POLISHED_ANDESITE_STAIRS:
    case BLOCK_DIORITE_STAIRS:
    case BLOCK_BLACKSTONE_STAIRS:
    case BLOCK_POLISHED_BLACKSTONE_BRICK_STAIRS:
    case BLOCK_POLISHED_BLACKSTONE_STAIRS:
    case BLOCK_COBBLED_DEEPSLATE_STAIRS:
    case BLOCK_POLISHED_DEEPSLATE_STAIRS:
    case BLOCK_DEEPSLATE_TILE_STAIRS:
    case BLOCK_DEEPSLATE_BRICK_STAIRS:
    case BLOCK_OXIDIZED_CUT_COPPER_STAIRS:
    case BLOCK_WEATHERED_CUT_COPPER_STAIRS:
    case BLOCK_EXPOSED_CUT_COPPER_STAIRS:
    case BLOCK_CUT_COPPER_STAIRS:
    case BLOCK_WAXED_WEATHERED_CUT_COPPER_STAIRS:
    case BLOCK_WAXED_EXPOSED_CUT_COPPER_STAIRS:
    case BLOCK_WAXED_CUT_COPPER_STAIRS:
    case BLOCK_WAXED_OXIDIZED_CUT_COPPER_STAIRS:
        return 1;
    default:
        return 0;
    }
}

static int
rotate_direction_clockwise(int direction) {
    switch (direction) {
    case DIRECTION_NEG_Z: return DIRECTION_POS_X;
    case DIRECTION_POS_X: return DIRECTION_POS_Z;
    case DIRECTION_POS_Z: return DIRECTION_NEG_X;
    case DIRECTION_NEG_X: return DIRECTION_NEG_Z;
    default:
        assert(0);
        return 0;
    }
}

static int
rotate_direction_counter_clockwise(int direction) {
    switch (direction) {
    case DIRECTION_NEG_Z: return DIRECTION_NEG_X;
    case DIRECTION_NEG_X: return DIRECTION_POS_Z;
    case DIRECTION_POS_Z: return DIRECTION_POS_X;
    case DIRECTION_POS_X: return DIRECTION_NEG_Z;
    default:
        assert(0);
        return 0;
    }
}

void
update_stairs_shape(BlockPos pos, block_state_info * cur_info) {
    cur_info->stairs_shape = STAIRS_SHAPE_STRAIGHT;

    // first look on left and right of stairs block to see if there are other
    // stairs there it must connect to
    int force_connect_right = 0;
    int force_connect_left = 0;

    u16 state_right = try_get_block_state(get_relative_block_pos(pos,
            rotate_direction_clockwise(cur_info->horizontal_facing)));
    block_state_info info_right = describe_block_state(state_right);
    if (is_stairs(info_right.block_type)) {
        if (info_right.half == cur_info->half && info_right.horizontal_facing == cur_info->horizontal_facing) {
            force_connect_right = 1;
        }
    }

    u16 state_left = try_get_block_state(get_relative_block_pos(pos, rotate_direction_counter_clockwise(cur_info->horizontal_facing)));
    block_state_info info_left = describe_block_state(state_left);
    if (is_stairs(info_left.block_type)) {
        if (info_left.half == cur_info->half && info_left.horizontal_facing == cur_info->horizontal_facing) {
            force_connect_left = 1;
        }
    }

    // try to connect with stairs in front
    u16 state_front = try_get_block_state(get_relative_block_pos(pos,
            get_opposite_direction(cur_info->horizontal_facing)));
    block_state_info info_front = describe_block_state(state_front);
    if (is_stairs(info_front.block_type)) {
        if (info_front.half == cur_info->half) {
            if (cur_info->horizontal_facing == rotate_direction_clockwise(info_front.horizontal_facing)) {
                if (!force_connect_left) {
                    cur_info->stairs_shape = STAIRS_SHAPE_INNER_LEFT;
                }
            } else if (rotate_direction_clockwise(cur_info->horizontal_facing) == info_front.horizontal_facing) {
                if (!force_connect_right) {
                    cur_info->stairs_shape = STAIRS_SHAPE_INNER_RIGHT;
                }
            }
        }
    }

    // try to connect with stairs behind
    u16 state_behind = try_get_block_state(get_relative_block_pos(pos,
            cur_info->horizontal_facing));
    block_state_info info_behind = describe_block_state(state_behind);
    if (is_stairs(info_behind.block_type)) {
        if (info_behind.half == cur_info->half) {
            if (cur_info->horizontal_facing == rotate_direction_clockwise(info_behind.horizontal_facing)) {
                if (!force_connect_right) {
                    cur_info->stairs_shape = STAIRS_SHAPE_OUTER_LEFT;
                }
            } else if (rotate_direction_clockwise(cur_info->horizontal_facing) == info_behind.horizontal_facing) {
                if (!force_connect_left) {
                    cur_info->stairs_shape = STAIRS_SHAPE_OUTER_RIGHT;
                }
            }
        }
    }
}

void
update_pane_shape(BlockPos pos,
        block_state_info * cur_info, int from_direction) {
    BlockPos neighbour_pos = get_relative_block_pos(pos, from_direction);
    u16 neighbour_state = try_get_block_state(neighbour_pos);
    i32 neighbour_type = serv->block_type_by_state[neighbour_state];

    *(&cur_info->neg_y + from_direction) = 0;

    switch (neighbour_type) {
    // iron bars, panes
    case BLOCK_IRON_BARS:
    case BLOCK_GLASS_PANE:
    case BLOCK_WHITE_STAINED_GLASS_PANE:
    case BLOCK_ORANGE_STAINED_GLASS_PANE:
    case BLOCK_MAGENTA_STAINED_GLASS_PANE:
    case BLOCK_LIGHT_BLUE_STAINED_GLASS_PANE:
    case BLOCK_YELLOW_STAINED_GLASS_PANE:
    case BLOCK_LIME_STAINED_GLASS_PANE:
    case BLOCK_PINK_STAINED_GLASS_PANE:
    case BLOCK_GRAY_STAINED_GLASS_PANE:
    case BLOCK_LIGHT_GRAY_STAINED_GLASS_PANE:
    case BLOCK_CYAN_STAINED_GLASS_PANE:
    case BLOCK_PURPLE_STAINED_GLASS_PANE:
    case BLOCK_BLUE_STAINED_GLASS_PANE:
    case BLOCK_BROWN_STAINED_GLASS_PANE:
    case BLOCK_GREEN_STAINED_GLASS_PANE:
    case BLOCK_RED_STAINED_GLASS_PANE:
    case BLOCK_BLACK_STAINED_GLASS_PANE:
    // walls block tag
    case BLOCK_COBBLESTONE_WALL:
    case BLOCK_MOSSY_COBBLESTONE_WALL:
    case BLOCK_BRICK_WALL:
    case BLOCK_PRISMARINE_WALL:
    case BLOCK_RED_SANDSTONE_WALL:
    case BLOCK_MOSSY_STONE_BRICK_WALL:
    case BLOCK_GRANITE_WALL:
    case BLOCK_STONE_BRICK_WALL:
    case BLOCK_NETHER_BRICK_WALL:
    case BLOCK_ANDESITE_WALL:
    case BLOCK_RED_NETHER_BRICK_WALL:
    case BLOCK_SANDSTONE_WALL:
    case BLOCK_END_STONE_BRICK_WALL:
    case BLOCK_DIORITE_WALL:
    case BLOCK_BLACKSTONE_WALL:
    case BLOCK_POLISHED_BLACKSTONE_BRICK_WALL:
    case BLOCK_POLISHED_BLACKSTONE_WALL:
    case BLOCK_COBBLED_DEEPSLATE_WALL:
    case BLOCK_POLISHED_DEEPSLATE_WALL:
    case BLOCK_DEEPSLATE_TILE_WALL:
    case BLOCK_DEEPSLATE_BRICK_WALL:
        // can attach to these blocks
        break;
    // leaves
    case BLOCK_JUNGLE_LEAVES:
    case BLOCK_OAK_LEAVES:
    case BLOCK_SPRUCE_LEAVES:
    case BLOCK_DARK_OAK_LEAVES:
    case BLOCK_ACACIA_LEAVES:
    case BLOCK_BIRCH_LEAVES:
    case BLOCK_AZALEA_LEAVES:
    case BLOCK_FLOWERING_AZALEA_LEAVES:
    // misc
    case BLOCK_BARRIER:
    case BLOCK_PUMPKIN:
    case BLOCK_CARVED_PUMPKIN:
    case BLOCK_JACK_O_LANTERN:
    case BLOCK_MELON:
    // shulker boxes block tag
    case BLOCK_SHULKER_BOX:
    case BLOCK_BLACK_SHULKER_BOX:
    case BLOCK_BLUE_SHULKER_BOX:
    case BLOCK_BROWN_SHULKER_BOX:
    case BLOCK_CYAN_SHULKER_BOX:
    case BLOCK_GRAY_SHULKER_BOX:
    case BLOCK_GREEN_SHULKER_BOX:
    case BLOCK_LIGHT_BLUE_SHULKER_BOX:
    case BLOCK_LIGHT_GRAY_SHULKER_BOX:
    case BLOCK_LIME_SHULKER_BOX:
    case BLOCK_MAGENTA_SHULKER_BOX:
    case BLOCK_ORANGE_SHULKER_BOX:
    case BLOCK_PINK_SHULKER_BOX:
    case BLOCK_PURPLE_SHULKER_BOX:
    case BLOCK_RED_SHULKER_BOX:
    case BLOCK_WHITE_SHULKER_BOX:
    case BLOCK_YELLOW_SHULKER_BOX:
        // can't attach to these blocks
        return;
    default: {
        support_model support = get_support_model(neighbour_state);
        if (support.full_face_flags & (1 << get_opposite_direction(from_direction))) {
            // can connect to sturdy faces of remaining blocks
            break;
        }
        return;
    }
    }

    *(&cur_info->neg_y + from_direction) = 1;
}

// @TODO(traks) block tag?
static int
is_wooden_fence(i32 block_type) {
    switch (block_type) {
    case BLOCK_OAK_FENCE:
    case BLOCK_ACACIA_FENCE:
    case BLOCK_DARK_OAK_FENCE:
    case BLOCK_SPRUCE_FENCE:
    case BLOCK_BIRCH_FENCE:
    case BLOCK_JUNGLE_FENCE:
    case BLOCK_CRIMSON_FENCE:
    case BLOCK_WARPED_FENCE:
        return 1;
    default:
        return 0;
    }
}

// @TODO(traks) block tag?
int
is_wall(i32 block_type) {
    switch (block_type) {
    case BLOCK_COBBLESTONE_WALL:
    case BLOCK_MOSSY_COBBLESTONE_WALL:
    case BLOCK_BRICK_WALL:
    case BLOCK_PRISMARINE_WALL:
    case BLOCK_RED_SANDSTONE_WALL:
    case BLOCK_MOSSY_STONE_BRICK_WALL:
    case BLOCK_GRANITE_WALL:
    case BLOCK_STONE_BRICK_WALL:
    case BLOCK_NETHER_BRICK_WALL:
    case BLOCK_ANDESITE_WALL:
    case BLOCK_RED_NETHER_BRICK_WALL:
    case BLOCK_SANDSTONE_WALL:
    case BLOCK_END_STONE_BRICK_WALL:
    case BLOCK_DIORITE_WALL:
    case BLOCK_BLACKSTONE_WALL:
    case BLOCK_POLISHED_BLACKSTONE_BRICK_WALL:
    case BLOCK_POLISHED_BLACKSTONE_WALL:
    case BLOCK_COBBLED_DEEPSLATE_WALL:
    case BLOCK_POLISHED_DEEPSLATE_WALL:
    case BLOCK_DEEPSLATE_TILE_WALL:
    case BLOCK_DEEPSLATE_BRICK_WALL:
        return 1;
    default:
        return 0;
    }
}

void
update_fence_shape(BlockPos pos,
        block_state_info * cur_info, int from_direction) {
    BlockPos neighbour_pos = get_relative_block_pos(pos, from_direction);
    u16 neighbour_state = try_get_block_state(neighbour_pos);
    i32 neighbour_type = serv->block_type_by_state[neighbour_state];

    *(&cur_info->neg_y + from_direction) = 0;

    switch (neighbour_type) {
    // all leaves
    case BLOCK_JUNGLE_LEAVES:
    case BLOCK_OAK_LEAVES:
    case BLOCK_SPRUCE_LEAVES:
    case BLOCK_DARK_OAK_LEAVES:
    case BLOCK_ACACIA_LEAVES:
    case BLOCK_BIRCH_LEAVES:
    case BLOCK_AZALEA_LEAVES:
    case BLOCK_FLOWERING_AZALEA_LEAVES:
    // misc
    case BLOCK_BARRIER:
    case BLOCK_PUMPKIN:
    case BLOCK_CARVED_PUMPKIN:
    case BLOCK_JACK_O_LANTERN:
    case BLOCK_MELON:
    // shulker boxes block tag
    case BLOCK_SHULKER_BOX:
    case BLOCK_BLACK_SHULKER_BOX:
    case BLOCK_BLUE_SHULKER_BOX:
    case BLOCK_BROWN_SHULKER_BOX:
    case BLOCK_CYAN_SHULKER_BOX:
    case BLOCK_GRAY_SHULKER_BOX:
    case BLOCK_GREEN_SHULKER_BOX:
    case BLOCK_LIGHT_BLUE_SHULKER_BOX:
    case BLOCK_LIGHT_GRAY_SHULKER_BOX:
    case BLOCK_LIME_SHULKER_BOX:
    case BLOCK_MAGENTA_SHULKER_BOX:
    case BLOCK_ORANGE_SHULKER_BOX:
    case BLOCK_PINK_SHULKER_BOX:
    case BLOCK_PURPLE_SHULKER_BOX:
    case BLOCK_RED_SHULKER_BOX:
    case BLOCK_WHITE_SHULKER_BOX:
    case BLOCK_YELLOW_SHULKER_BOX:
        // can't attach to these blocks
        return;
    default: {
        if (is_wooden_fence(neighbour_type) && is_wooden_fence(cur_info->block_type)) {
            break;
        }
        if (neighbour_type == cur_info->block_type) {
            // allow nether brick fences to connect
            break;
        }

        support_model support = get_support_model(neighbour_state);
        if (support.full_face_flags & (1 << get_opposite_direction(from_direction))) {
            // can connect to sturdy faces of remaining blocks
            break;
        }
        return;
    }
    }

    *(&cur_info->neg_y + from_direction) = 1;
}

void
update_wall_shape(BlockPos pos,
        block_state_info * cur_info, int from_direction) {
    BlockPos neighbour_pos = get_relative_block_pos(pos, from_direction);
    u16 neighbour_state = try_get_block_state(neighbour_pos);
    i32 neighbour_type = serv->block_type_by_state[neighbour_state];
    block_state_info neighbour_info = describe_block_state(neighbour_state);

    if (from_direction == DIRECTION_POS_Y) {
        // @TODO(traks) something special
        return;
    }

    int connect = 0;

    switch (neighbour_type) {
    // leaves
    case BLOCK_JUNGLE_LEAVES:
    case BLOCK_OAK_LEAVES:
    case BLOCK_SPRUCE_LEAVES:
    case BLOCK_DARK_OAK_LEAVES:
    case BLOCK_ACACIA_LEAVES:
    case BLOCK_BIRCH_LEAVES:
    case BLOCK_AZALEA_LEAVES:
    case BLOCK_FLOWERING_AZALEA_LEAVES:
    // misc
    case BLOCK_BARRIER:
    case BLOCK_PUMPKIN:
    case BLOCK_CARVED_PUMPKIN:
    case BLOCK_JACK_O_LANTERN:
    case BLOCK_MELON:
    // shulker box block tag
    case BLOCK_SHULKER_BOX:
    case BLOCK_BLACK_SHULKER_BOX:
    case BLOCK_BLUE_SHULKER_BOX:
    case BLOCK_BROWN_SHULKER_BOX:
    case BLOCK_CYAN_SHULKER_BOX:
    case BLOCK_GRAY_SHULKER_BOX:
    case BLOCK_GREEN_SHULKER_BOX:
    case BLOCK_LIGHT_BLUE_SHULKER_BOX:
    case BLOCK_LIGHT_GRAY_SHULKER_BOX:
    case BLOCK_LIME_SHULKER_BOX:
    case BLOCK_MAGENTA_SHULKER_BOX:
    case BLOCK_ORANGE_SHULKER_BOX:
    case BLOCK_PINK_SHULKER_BOX:
    case BLOCK_PURPLE_SHULKER_BOX:
    case BLOCK_RED_SHULKER_BOX:
    case BLOCK_WHITE_SHULKER_BOX:
    case BLOCK_YELLOW_SHULKER_BOX:
        // can't attach to these blocks
        break;
    // iron bars & panes
    case BLOCK_IRON_BARS:
    case BLOCK_GLASS_PANE:
    case BLOCK_WHITE_STAINED_GLASS_PANE:
    case BLOCK_ORANGE_STAINED_GLASS_PANE:
    case BLOCK_MAGENTA_STAINED_GLASS_PANE:
    case BLOCK_LIGHT_BLUE_STAINED_GLASS_PANE:
    case BLOCK_YELLOW_STAINED_GLASS_PANE:
    case BLOCK_LIME_STAINED_GLASS_PANE:
    case BLOCK_PINK_STAINED_GLASS_PANE:
    case BLOCK_GRAY_STAINED_GLASS_PANE:
    case BLOCK_LIGHT_GRAY_STAINED_GLASS_PANE:
    case BLOCK_CYAN_STAINED_GLASS_PANE:
    case BLOCK_PURPLE_STAINED_GLASS_PANE:
    case BLOCK_BLUE_STAINED_GLASS_PANE:
    case BLOCK_BROWN_STAINED_GLASS_PANE:
    case BLOCK_GREEN_STAINED_GLASS_PANE:
    case BLOCK_RED_STAINED_GLASS_PANE:
    case BLOCK_BLACK_STAINED_GLASS_PANE:
        // can connect to these
        connect = 1;
        break;
    // fence gates
    case BLOCK_OAK_FENCE_GATE:
    case BLOCK_SPRUCE_FENCE_GATE:
    case BLOCK_BIRCH_FENCE_GATE:
    case BLOCK_JUNGLE_FENCE_GATE:
    case BLOCK_ACACIA_FENCE_GATE:
    case BLOCK_DARK_OAK_FENCE_GATE:
    case BLOCK_CRIMSON_FENCE_GATE:
    case BLOCK_WARPED_FENCE_GATE: {
        // try connect to fence gate in wall
        int facing = neighbour_info.horizontal_facing;
        int rotated = rotate_direction_clockwise(facing);
        if (rotated == from_direction || rotated == get_opposite_direction(from_direction)) {
            // fence gate pointing in good direction
            connect = 1;
        }
        break;
    }
    default: {
        if (is_wall(neighbour_type)) {
            // connect to other walls
            connect = 1;
            break;
        }

        support_model support = get_support_model(neighbour_state);
        if (support.full_face_flags & (1 << get_opposite_direction(from_direction))) {
            // can connect to sturdy faces of remaining blocks
            connect = 1;
            break;
        }
    }
    }

    int wall_side = WALL_SIDE_NONE;
    if (connect) {
        // @TODO(traks) make wall tall if necessary
        wall_side = WALL_SIDE_LOW;
    }
    // @TODO(traks) raise post if necessary

    switch (from_direction) {
    case DIRECTION_POS_X: cur_info->wall_pos_x = wall_side; break;
    case DIRECTION_NEG_X: cur_info->wall_neg_x = wall_side; break;
    case DIRECTION_POS_Z: cur_info->wall_pos_z = wall_side; break;
    case DIRECTION_NEG_Z: cur_info->wall_neg_z = wall_side; break;
    }
}

static int
is_redstone_wire_dot(block_state_info * info) {
    // @TODO(traks) can make this faster probably
    return !info->redstone_pos_x && !info->redstone_neg_z
            && !info->redstone_pos_z && !info->redstone_neg_x;
}

static int
can_redstone_wire_connect_horizontally(u16 block_state, int to_dir) {
    i32 block_type = serv->block_type_by_state[block_state];
    block_state_info state_info = describe_block_state(block_state);

    switch (block_type) {
    case BLOCK_REDSTONE_WIRE:
        return 1;
    case BLOCK_REPEATER: {
        int facing = state_info.horizontal_facing;
        if (facing == to_dir || facing == get_opposite_direction(to_dir)) {
            return 1;
        }
        return 0;
    }
    case BLOCK_OBSERVER: {
        int facing = state_info.facing;
        return (facing == to_dir);
    }
    // redstone power sources
    case BLOCK_STONE_PRESSURE_PLATE:
    case BLOCK_OAK_PRESSURE_PLATE:
    case BLOCK_SPRUCE_PRESSURE_PLATE:
    case BLOCK_BIRCH_PRESSURE_PLATE:
    case BLOCK_JUNGLE_PRESSURE_PLATE:
    case BLOCK_ACACIA_PRESSURE_PLATE:
    case BLOCK_DARK_OAK_PRESSURE_PLATE:
    case BLOCK_LIGHT_WEIGHTED_PRESSURE_PLATE:
    case BLOCK_HEAVY_WEIGHTED_PRESSURE_PLATE:
    case BLOCK_CRIMSON_PRESSURE_PLATE:
    case BLOCK_WARPED_PRESSURE_PLATE:
    case BLOCK_POLISHED_BLACKSTONE_PRESSURE_PLATE:
    case BLOCK_STONE_BUTTON:
    case BLOCK_OAK_BUTTON:
    case BLOCK_SPRUCE_BUTTON:
    case BLOCK_BIRCH_BUTTON:
    case BLOCK_JUNGLE_BUTTON:
    case BLOCK_ACACIA_BUTTON:
    case BLOCK_DARK_OAK_BUTTON:
    case BLOCK_CRIMSON_BUTTON:
    case BLOCK_WARPED_BUTTON:
    case BLOCK_POLISHED_BLACKSTONE_BUTTON:
    case BLOCK_DAYLIGHT_DETECTOR:
    case BLOCK_DETECTOR_RAIL:
    case BLOCK_COMPARATOR:
    case BLOCK_LECTERN:
    case BLOCK_LEVER:
    case BLOCK_REDSTONE_BLOCK:
    case BLOCK_REDSTONE_TORCH:
    case BLOCK_REDSTONE_WALL_TORCH:
    case BLOCK_TARGET:
    case BLOCK_TRAPPED_CHEST:
    case BLOCK_TRIPWIRE_HOOK:
        return 1;
    default:
        return 0;
    }
}

static int
get_emitted_redstone_power(u16 block_state, int dir,
        int to_wire, int ignore_wires) {
    i32 block_type = serv->block_type_by_state[block_state];
    block_state_info info = describe_block_state(block_state);

    switch (block_type) {
    case BLOCK_STONE_PRESSURE_PLATE:
    case BLOCK_OAK_PRESSURE_PLATE:
    case BLOCK_SPRUCE_PRESSURE_PLATE:
    case BLOCK_BIRCH_PRESSURE_PLATE:
    case BLOCK_JUNGLE_PRESSURE_PLATE:
    case BLOCK_ACACIA_PRESSURE_PLATE:
    case BLOCK_DARK_OAK_PRESSURE_PLATE:
    case BLOCK_CRIMSON_PRESSURE_PLATE:
    case BLOCK_WARPED_PRESSURE_PLATE:
    case BLOCK_POLISHED_BLACKSTONE_PRESSURE_PLATE:
    case BLOCK_STONE_BUTTON:
    case BLOCK_OAK_BUTTON:
    case BLOCK_SPRUCE_BUTTON:
    case BLOCK_BIRCH_BUTTON:
    case BLOCK_JUNGLE_BUTTON:
    case BLOCK_ACACIA_BUTTON:
    case BLOCK_DARK_OAK_BUTTON:
    case BLOCK_CRIMSON_BUTTON:
    case BLOCK_WARPED_BUTTON:
    case BLOCK_POLISHED_BLACKSTONE_BUTTON:
    case BLOCK_DETECTOR_RAIL:
    case BLOCK_LECTERN:
    case BLOCK_LEVER:
    case BLOCK_TRIPWIRE_HOOK: {
        if (info.powered) {
            return 15;
        }
        return 0;
    }
    case BLOCK_LIGHT_WEIGHTED_PRESSURE_PLATE:
    case BLOCK_HEAVY_WEIGHTED_PRESSURE_PLATE:
    case BLOCK_DAYLIGHT_DETECTOR:
    case BLOCK_TARGET: {
        return info.power;
    }
    case BLOCK_COMPARATOR: {
        int facing = info.horizontal_facing;
        if (info.powered && facing == get_opposite_direction(dir)) {
            // @TODO(traks) get power from block entity
            return 0;
        }
        return 0;
    }
    case BLOCK_REPEATER: {
        int facing = info.horizontal_facing;
        if (info.powered && facing == get_opposite_direction(dir)) {
            return 15;
        }
        return 0;
    }
    case BLOCK_OBSERVER: {
        if (info.powered && info.facing == get_opposite_direction(dir)) {
            return 15;
        }
        return 0;
    }
    case BLOCK_REDSTONE_BLOCK: {
        return 15;
    }
    case BLOCK_REDSTONE_WIRE: {
        if (ignore_wires) {
            return 0;
        }
        if (dir == DIRECTION_POS_Y) {
            return 0;
        } else if (dir == DIRECTION_NEG_Y) {
            if (to_wire) {
                return 0;
            }
            return info.power;
        } else {
            int prop = BLOCK_PROPERTY_REDSTONE_POS_X
                    + get_horizontal_direction_index(dir);
            // return power if the wire is connected on the direction's side. If
            // we're emitting to another redstone wire, decrease power by 1.
            if (info.values[prop]) {
                return to_wire ? MAX(info.power, 1) - 1 : info.power;
            }
            return 0;
        }
    }
    case BLOCK_REDSTONE_TORCH: {
        // standing torch!
        return info.lit && dir != DIRECTION_NEG_Y ? 15 : 0;
    }
    case BLOCK_REDSTONE_WALL_TORCH: {
        return info.lit && info.facing != get_opposite_direction(dir) ? 15 : 0;
    }
    case BLOCK_TRAPPED_CHEST: {
        // @TODO(traks) use block entity to determine power level
        return 0;
    }
    default:
        return 0;
    }
}

static int
get_conducted_redstone_power(u16 block_state, int dir,
        int to_wire, int ignore_wires) {
    i32 block_type = serv->block_type_by_state[block_state];
    block_state_info info = describe_block_state(block_state);

    switch (block_type) {
    case BLOCK_STONE_PRESSURE_PLATE:
    case BLOCK_OAK_PRESSURE_PLATE:
    case BLOCK_SPRUCE_PRESSURE_PLATE:
    case BLOCK_BIRCH_PRESSURE_PLATE:
    case BLOCK_JUNGLE_PRESSURE_PLATE:
    case BLOCK_ACACIA_PRESSURE_PLATE:
    case BLOCK_DARK_OAK_PRESSURE_PLATE:
    case BLOCK_CRIMSON_PRESSURE_PLATE:
    case BLOCK_WARPED_PRESSURE_PLATE:
    case BLOCK_POLISHED_BLACKSTONE_PRESSURE_PLATE:
    case BLOCK_DETECTOR_RAIL:
    case BLOCK_LECTERN: {
        if (info.powered && dir == DIRECTION_NEG_Y) {
            return 15;
        }
        return 0;
    }
    case BLOCK_STONE_BUTTON:
    case BLOCK_OAK_BUTTON:
    case BLOCK_SPRUCE_BUTTON:
    case BLOCK_BIRCH_BUTTON:
    case BLOCK_JUNGLE_BUTTON:
    case BLOCK_ACACIA_BUTTON:
    case BLOCK_DARK_OAK_BUTTON:
    case BLOCK_CRIMSON_BUTTON:
    case BLOCK_WARPED_BUTTON:
    case BLOCK_POLISHED_BLACKSTONE_BUTTON:
    case BLOCK_LEVER: {
        if (info.powered) {
            int back_side;
            if (info.attach_face == ATTACH_FACE_FLOOR) {
                back_side = DIRECTION_NEG_Y;
            } else if (info.attach_face == ATTACH_FACE_CEILING) {
                back_side = DIRECTION_POS_Y;
            } else {
                back_side = get_opposite_direction(info.horizontal_facing);
            }
            return back_side == dir ? 15 : 0;
        }
        return 0;
    }
    case BLOCK_TRIPWIRE_HOOK: {
        int facing = info.horizontal_facing;
        if (info.powered && facing == get_opposite_direction(dir)) {
            return 15;
        }
        return 0;
    }
    case BLOCK_LIGHT_WEIGHTED_PRESSURE_PLATE:
    case BLOCK_HEAVY_WEIGHTED_PRESSURE_PLATE: {
        if (dir == DIRECTION_NEG_Y) {
            return info.power;
        }
        return 0;
    }
    case BLOCK_COMPARATOR: {
        int facing = info.horizontal_facing;
        if (info.powered && facing == get_opposite_direction(dir)) {
            // @TODO(traks) get power from block entity
            return 0;
        }
        return 0;
    }
    case BLOCK_REPEATER: {
        int facing = info.horizontal_facing;
        if (info.powered && facing == get_opposite_direction(dir)) {
            return 15;
        }
        return 0;
    }
    case BLOCK_OBSERVER: {
        if (info.powered && info.facing == get_opposite_direction(dir)) {
            return 15;
        }
        return 0;
    }
    case BLOCK_REDSTONE_WIRE: {
        if (to_wire || ignore_wires) {
            // redstone wire doesn't conduct power through blocks to other
            // redstone wires
            return 0;
        }
        if (dir == DIRECTION_POS_Y) {
            return 0;
        } else if (dir == DIRECTION_NEG_Y) {
            return info.power;
        } else {
            int prop = BLOCK_PROPERTY_REDSTONE_POS_X
                    + get_horizontal_direction_index(dir);
            // return power if the wire is connected on the direction's side
            return info.values[prop] ? info.power : 0;
        }
    }
    case BLOCK_REDSTONE_TORCH:
    case BLOCK_REDSTONE_WALL_TORCH: {
        // standing torch!
        return info.lit && dir != DIRECTION_POS_Y ? 15 : 0;
    }
    case BLOCK_TRAPPED_CHEST: {
        // @TODO(traks) use block entity to determine power level
        return 0;
    }
    default:
        return 0;
    }
}

static int
get_redstone_side_power(BlockPos pos, int dir, int to_wire,
        int ignore_wires) {
    BlockPos side_pos = get_relative_block_pos(pos, dir);
    int opp_dir = get_opposite_direction(dir);
    u16 side_state = try_get_block_state(side_pos);
    int res = get_emitted_redstone_power(side_state, opp_dir,
            to_wire, ignore_wires);

    if (conducts_redstone(side_state, side_pos)) {
        for (int dir_on_side = 0; dir_on_side < 6; dir_on_side++) {
            if (dir_on_side == opp_dir) {
                // don't include the power of the original block in the
                // calculation
                continue;
            }
            if (res == 15) {
                break;
            }

            u16 state = try_get_block_state(
                    get_relative_block_pos(side_pos, dir_on_side));
            int power = get_conducted_redstone_power(
                    state, get_opposite_direction(dir_on_side),
                    to_wire, ignore_wires);
            res = MAX(res, power);
        }
    }
    return res;
}

static int
is_redstone_wire_connected(BlockPos pos, block_state_info * info) {
    BlockPos pos_above = get_relative_block_pos(pos, DIRECTION_POS_Y);
    u16 state_above = try_get_block_state(pos_above);
    int conductor_above = conducts_redstone(state_above, pos_above);

    // order of redstone side entries in block state info struct
    int directions[] = {
        DIRECTION_POS_X, DIRECTION_NEG_Z, DIRECTION_POS_Z, DIRECTION_NEG_X,
    };

    for (int i = 0; i < 4; i++) {
        int dir = directions[i];
        int opp_dir = get_opposite_direction(dir);
        BlockPos pos_side = get_relative_block_pos(pos, dir);
        u16 state_side = try_get_block_state(pos_side);

        if (!conductor_above) {
            // try to connect diagonally up
            BlockPos dest_pos = get_relative_block_pos(
                    pos_side, DIRECTION_POS_Y);
            u16 dest_state = try_get_block_state(dest_pos);
            i32 dest_type = serv->block_type_by_state[dest_state];

            // can only connect diagonally to redstone wire
            if (dest_type == BLOCK_REDSTONE_WIRE) {
                return 1;
            }
        }

        if (can_redstone_wire_connect_horizontally(state_side, dir)) {
            return 1;
        }

        if (!conducts_redstone(state_side, pos_side)) {
            // try to connect diagonally down
            BlockPos dest_pos = get_relative_block_pos(
                    pos_side, DIRECTION_NEG_Y);
            u16 dest_state = try_get_block_state(dest_pos);
            i32 dest_type = serv->block_type_by_state[dest_state];

            // can only connect diagonally to redstone wire
            if (dest_type == BLOCK_REDSTONE_WIRE) {
                return 1;
            }
        }
    }
    return 0;
}

typedef struct {
    // first index is horizontal direction, second index is top, middle, bottom
    // of possible connection.
    // @NOTE(traks) It is important to note that connected does not imply
    // redstone power can travel from one dust to the other; think of redstone
    // slab towers.
    unsigned char connected[4][3];
    unsigned char sides[4];
    unsigned char power;
    unsigned char wire_out[4][3];
    unsigned char wire_in[4][3];
} redstone_wire_env;

int
update_redstone_wire(BlockPos pos, u16 in_world_state,
        block_state_info * base_info, block_update_context * buc) {
    BlockPos pos_above = get_relative_block_pos(pos, DIRECTION_POS_Y);
    u16 state_above = try_get_block_state(pos_above);
    int conductor_above = conducts_redstone(state_above, pos_above);
    int was_dot = is_redstone_wire_dot(base_info);

    // order of redstone side entries in block state info struct
    int directions[] = {
        DIRECTION_POS_X, DIRECTION_NEG_Z, DIRECTION_POS_Z, DIRECTION_NEG_X,
    };

    // first gather data from the blocks around the redstone wire, so we can do
    // calculations with it afterwards
    redstone_wire_env env = {0};

    for (int i = 0; i < 4; i++) {
        int dir = directions[i];
        int opp_dir = get_opposite_direction(dir);
        BlockPos pos_side = get_relative_block_pos(pos, dir);
        u16 state_side = try_get_block_state(pos_side);
        i32 type_side = serv->block_type_by_state[state_side];
        block_state_info side_info = describe_block_state(state_side);
        int new_side = REDSTONE_SIDE_NONE;

        if (!conductor_above) {
            // try to connect diagonally up
            BlockPos dest_pos = get_relative_block_pos(
                    pos_side, DIRECTION_POS_Y);
            u16 dest_state = try_get_block_state(dest_pos);
            i32 dest_type = serv->block_type_by_state[dest_state];

            // can only connect diagonally to redstone wire
            if (dest_type == BLOCK_REDSTONE_WIRE) {
                env.connected[i][0] = 1;
                support_model model = get_support_model(state_side);
                if (model.full_face_flags & (1 << opp_dir)) {
                    new_side = REDSTONE_SIDE_UP;
                } else {
                    new_side = REDSTONE_SIDE_SIDE;
                }
            }
        }

        if (can_redstone_wire_connect_horizontally(state_side, dir)) {
            env.connected[i][1] = 1;
            if (new_side == REDSTONE_SIDE_NONE) {
                new_side = REDSTONE_SIDE_SIDE;
            }
        }

        int conductor_side = conducts_redstone(state_side, pos_side);

        if (!conductor_side) {
            // try to connect diagonally down
            BlockPos dest_pos = get_relative_block_pos(
                    pos_side, DIRECTION_NEG_Y);
            u16 dest_state = try_get_block_state(dest_pos);
            i32 dest_type = serv->block_type_by_state[dest_state];

            // can only connect diagonally to redstone wire
            if (dest_type == BLOCK_REDSTONE_WIRE) {
                env.connected[i][2] = 1;
                if (new_side == REDSTONE_SIDE_NONE) {
                    new_side = REDSTONE_SIDE_SIDE;
                }
            }
        }

        env.sides[i] = new_side;
    }

    // Now actually do something with the collected data

    int is_dot = !(env.sides[0] | env.sides[1] | env.sides[2] | env.sides[3]);

    if (was_dot && is_dot) {
        // redstone wire remains a dot
    } else {
        // @TODO(traks) we should also make sure redstone torches on blocks,
        // repeaters, etc. are updated when redstone dust is redirected. This is
        // going to be a massive pain to implement for every redstone component
        // with this sytem...

        for (int i = 0; i < 4; i++) {
            base_info->values[BLOCK_PROPERTY_REDSTONE_POS_X + i] = env.sides[i];
        }

        int side_pos_x = env.sides[0];
        int side_neg_z = env.sides[1];
        int side_pos_z = env.sides[2];
        int side_neg_x = env.sides[3];

        if (!side_pos_x && !side_neg_x) {
            if (!side_neg_z) {
                base_info->redstone_neg_z = REDSTONE_SIDE_SIDE;
            }
            if (!side_pos_z) {
                base_info->redstone_pos_z = REDSTONE_SIDE_SIDE;
            }
        }
        if (!side_pos_z && !side_neg_z) {
            if (!side_neg_x) {
                base_info->redstone_neg_x = REDSTONE_SIDE_SIDE;
            }
            if (!side_pos_x) {
                base_info->redstone_pos_x = REDSTONE_SIDE_SIDE;
            }
        }
    }

    u16 new_state = make_block_state(base_info);
    if (new_state == in_world_state) {
        return 0;
    }

    try_set_block_state(pos, new_state);

    // @TODO(traks) update direct neighbours and diagonal neighbours in the
    // global update order
    for (int i = 0; i < 4; i++) {
        int dir = directions[i];
        int opp_dir = get_opposite_direction(dir);
        BlockPos side_pos = get_relative_block_pos(pos, dir);
        BlockPos above_pos = get_relative_block_pos(side_pos, DIRECTION_POS_Y);
        if (env.connected[i][0]) {
            push_block_update(above_pos, opp_dir, buc);
        }
        BlockPos below_pos = get_relative_block_pos(side_pos, DIRECTION_NEG_Y);
        if (env.connected[i][2]) {
            push_block_update(below_pos, opp_dir, buc);
        }
    }

    push_direct_neighbour_block_updates(pos, buc);
    return 1;
}

redstone_wire_env
calculate_redstone_wire_env(BlockPos pos, u16 block_state,
        block_state_info * info, int ignore_same_line_power) {
    BlockPos pos_above = get_relative_block_pos(pos, DIRECTION_POS_Y);
    u16 state_above = try_get_block_state(pos_above);
    int conductor_above = conducts_redstone(state_above, pos_above);
    BlockPos pos_below = get_relative_block_pos(pos, DIRECTION_NEG_Y);
    u16 state_below = try_get_block_state(pos_below);
    int conductor_below = conducts_redstone(state_below, pos_below);

    // order of redstone side entries in block state info struct
    int directions[] = {
        DIRECTION_POS_X, DIRECTION_NEG_Z, DIRECTION_POS_Z, DIRECTION_NEG_X,
    };

    // first gather data from the blocks around the redstone wire, so we can do
    // calculations with it afterwards

    redstone_wire_env env = {0};
    int powers[4][3] = {0};

    for (int i = 0; i < 4; i++) {
        int dir = directions[i];
        int opp_dir = get_opposite_direction(dir);
        BlockPos pos_side = get_relative_block_pos(pos, dir);
        u16 state_side = try_get_block_state(pos_side);
        i32 type_side = serv->block_type_by_state[state_side];
        block_state_info side_info = describe_block_state(state_side);
        int conductor_side = conducts_redstone(state_side, pos_side);
        int new_side = REDSTONE_SIDE_NONE;

        // first determine what this redstone wire should connect to, and what
        // the power is of diagonal redstone wires

        if (!conductor_above) {
            // try to connect diagonally up
            BlockPos dest_pos = get_relative_block_pos(
                    pos_side, DIRECTION_POS_Y);
            u16 dest_state = try_get_block_state(dest_pos);
            i32 dest_type = serv->block_type_by_state[dest_state];

            // can only connect diagonally to redstone wire
            if (dest_type == BLOCK_REDSTONE_WIRE) {
                block_state_info dest_info = describe_block_state(dest_state);
                env.connected[i][0] = 1;
                support_model model = get_support_model(state_side);
                if (model.full_face_flags & (1 << opp_dir)) {
                    new_side = REDSTONE_SIDE_UP;
                } else {
                    new_side = REDSTONE_SIDE_SIDE;
                }

                if (conductor_side) {
                    powers[i][0] = MAX(dest_info.power, 1) - 1;
                    env.wire_in[i][0] = 1;
                }

                env.wire_out[i][0] = 1;
            }
        }

        if (can_redstone_wire_connect_horizontally(state_side, dir)) {
            env.connected[i][1] = 1;
            if (new_side == REDSTONE_SIDE_NONE) {
                new_side = REDSTONE_SIDE_SIDE;
            }
        }
        if (type_side == BLOCK_REDSTONE_WIRE) {
            env.wire_in[i][1] = 1;
            env.wire_out[i][1] = 1;
        }

        powers[i][1] = get_redstone_side_power(pos, dir, 1, ignore_same_line_power);

        if (!conductor_side) {
            // try to connect diagonally down
            BlockPos dest_pos = get_relative_block_pos(
                    pos_side, DIRECTION_NEG_Y);
            u16 dest_state = try_get_block_state(dest_pos);
            i32 dest_type = serv->block_type_by_state[dest_state];

            // can only connect diagonally to redstone wire
            if (dest_type == BLOCK_REDSTONE_WIRE) {
                block_state_info dest_info = describe_block_state(dest_state);
                env.connected[i][2] = 1;
                if (new_side == REDSTONE_SIDE_NONE) {
                    new_side = REDSTONE_SIDE_SIDE;
                }

                env.wire_in[i][2] = 1;
                if (conductor_below) {
                    env.wire_out[i][2] = 1;
                }

                if (!ignore_same_line_power || !conductor_below) {
                    powers[i][2] = MAX(dest_info.power, 1) - 1;
                }
            }
        }

        env.sides[i] = new_side;
    }

    // Now actually do something with the collected data

    int was_dot = is_redstone_wire_dot(info);
    int is_dot = !(env.sides[0] | env.sides[1] | env.sides[2] | env.sides[3]);

    if (was_dot && is_dot) {
        // keep the redstone wire as a dot
    } else {
        // Update connection visuals to make the wire look a bit nicer. The
        // visuals sadly don't give an accurate representation of the actual
        // connections of this wire.
        int side_pos_x = env.sides[0];
        int side_neg_z = env.sides[1];
        int side_pos_z = env.sides[2];
        int side_neg_x = env.sides[3];

        if (!side_pos_x && !side_neg_x) {
            if (!side_neg_z) {
                env.sides[1] = REDSTONE_SIDE_SIDE;
            }
            if (!side_pos_z) {
                env.sides[2] = REDSTONE_SIDE_SIDE;
            }
        }
        if (!side_pos_z && !side_neg_z) {
            if (!side_neg_x) {
                env.sides[3] = REDSTONE_SIDE_SIDE;
            }
            if (!side_pos_x) {
                env.sides[0] = REDSTONE_SIDE_SIDE;
            }
        }
    }

    // Finally, calculate the power level of the redstone wire as determined by
    // the surrounding blocks

    int new_power = 0;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) {
            new_power = MAX(new_power, powers[i][j]);
        }
    }

    new_power = MAX(new_power, get_redstone_side_power(pos, DIRECTION_NEG_Y, 1,
            ignore_same_line_power));
    new_power = MAX(new_power, get_redstone_side_power(pos, DIRECTION_POS_Y, 1,
            ignore_same_line_power));
    env.power = new_power;
    return env;
}

typedef struct {
    BlockPos pos;
    unsigned char distance;
} redstone_wire_pos;

static void
update_redstone_line(BlockPos start_pos) {
    u16 start_state = try_get_block_state(start_pos);
    block_state_info start_info = describe_block_state(start_state);
    redstone_wire_env start_env = calculate_redstone_wire_env(
            start_pos, start_state, &start_info, 0);

    // @TODO(traks) we should also make sure redstone torches on blocks,
    // repeaters, etc. are updated when power level changes or redstone dust is
    // redirected. This is going to be a massive pain to implement for every
    // redstone component with this sytem.

    if (start_env.power == start_info.power) {
        return;
    }

    // order of redstone side entries in block state info struct
    int directions[] = {
        DIRECTION_POS_X, DIRECTION_NEG_Z, DIRECTION_POS_Z, DIRECTION_NEG_X,
    };

    if (start_env.power > start_info.power) {
        // power went up, spread it around!

        BlockPos wires[500];
        int wire_count = 0;
        wires[0] = start_pos;
        wire_count++;

        start_info.power = start_env.power;
        u16 new_start_state = make_block_state(&start_info);
        try_set_block_state(start_pos, new_start_state);

        for (int i = 0; i < wire_count; i++) {
            BlockPos wire_pos = wires[i];
            u16 state = try_get_block_state(wire_pos);
            block_state_info info = describe_block_state(state);
            redstone_wire_env env = calculate_redstone_wire_env(
                    wire_pos, state, &info, 0);

            for (int i = 0; i < 4; i++) {
                BlockPos rel = get_relative_block_pos(
                        wire_pos, directions[i]);
                rel = get_relative_block_pos(rel, DIRECTION_POS_Y);

                for (int j = 0; j < 3; j++) {
                    if (env.wire_out[i][j]) {
                        u16 out_state = try_get_block_state(rel);
                        block_state_info out_info = describe_block_state(out_state);
                        redstone_wire_env out_env = calculate_redstone_wire_env(
                                rel, out_state, &out_info, 0);
                        if (out_env.power > out_info.power) {
                            out_info.power = out_env.power;
                            try_set_block_state(rel, make_block_state(&out_info));
                            wires[wire_count] = rel;
                            wire_count++;
                        }
                    }
                    rel = get_relative_block_pos(rel, DIRECTION_NEG_Y);
                }
            }
        }
    } else {
        // power went down

        redstone_wire_pos wires[500];
        int wire_count = 0;
        wires[0] = (redstone_wire_pos) {.pos = start_pos, .distance = 0};
        wire_count++;

        BlockPos sources[50];
        int source_count = 0;

        redstone_wire_env lineless_env = calculate_redstone_wire_env(
                start_pos, start_state, &start_info, 1);
        if (lineless_env.power > 0) {
            sources[0] = start_pos;
            source_count++;
        }

        for (int i = 0; i < wire_count; i++) {
            BlockPos wire_pos = wires[i].pos;
            int distance = wires[i].distance;
            u16 state = try_get_block_state(wire_pos);
            block_state_info info = describe_block_state(state);
            redstone_wire_env env = calculate_redstone_wire_env(
                    wire_pos, state, &info, 1);

            for (int i = 0; i < 4; i++) {
                BlockPos rel = get_relative_block_pos(
                        wire_pos, directions[i]);
                rel = get_relative_block_pos(rel, DIRECTION_POS_Y);

                for (int j = 0; j < 3; j++) {
                    if (env.wire_out[i][j]) {
                        block_state_info out_info = describe_block_state(
                                try_get_block_state(rel));
                        if (out_info.power == start_info.power - distance - 1
                                && out_info.power > 0) {
                            redstone_wire_env out_env = calculate_redstone_wire_env(
                                    wire_pos, state, &info, 1);

                            if (out_env.power > 0) {
                                sources[source_count] = wire_pos;
                                source_count++;
                            }

                            out_info.power = 0;
                            try_set_block_state(rel, make_block_state(&out_info));

                            // this neighbour is exactly
                            wires[wire_count] = (redstone_wire_pos) {
                                .pos = rel,
                                .distance = distance + 1
                            };
                            wire_count++;
                        }
                    }
                    rel = get_relative_block_pos(rel, DIRECTION_NEG_Y);
                }
            }
        }

        for (int i = 0; i < wire_count; i++) {
            BlockPos wire_pos = wires[i].pos;
            int distance = wires[i].distance;
            u16 state = try_get_block_state(wire_pos);
            block_state_info info = describe_block_state(state);
            int cur_power = info.power;
            redstone_wire_env env = calculate_redstone_wire_env(
                    wire_pos, state, &info, 1);

            info.power = 0;
            try_set_block_state(wire_pos, make_block_state(&info));

            if (start_info.power - distance < cur_power) {
                // the wire is powered by something other than the original wire
                // of which the power decreased, so the power of the current
                // wire won't change because of the decreased power
                sources[source_count] = wire_pos;
                source_count++;
                continue;
            }

            if (env.power > 0) {
                sources[source_count] = wire_pos;
                source_count++;
            }

            for (int i = 0; i < 4; i++) {
                BlockPos rel = get_relative_block_pos(
                        wire_pos, directions[i]);
                rel = get_relative_block_pos(rel, DIRECTION_POS_Y);

                for (int j = 0; j < 3; j++) {
                    if (env.wire_out[i][j]) {
                        block_state_info out_info = describe_block_state(
                                try_get_block_state(rel));
                        if (out_info.power > 0) {
                            wires[wire_count] = (redstone_wire_pos) {
                                .pos = rel,
                                .distance = distance + 1
                            };
                            wire_count++;
                        }
                    }
                    rel = get_relative_block_pos(rel, DIRECTION_NEG_Y);
                }
            }
        }

        for (int i = 0; i < source_count; i++) {
            update_redstone_line(sources[i]);
        }
    }
}

// @TODO(traks) can we perhaps get rid of the from_direction for simplicity?
static int
update_block(BlockPos pos, int from_direction, int is_delayed,
        block_update_context * buc) {
    // @TODO(traks) ideally all these chunk lookups and block lookups should be
    // cached to make a single block update as fast as possible. It is after all
    // incredibly easy to create tons of block updates in a single tick.

    u16 cur_state = try_get_block_state(pos);
    block_state_info cur_info = describe_block_state(cur_state);
    i32 cur_type = cur_info.block_type;

    BlockPos from_pos = get_relative_block_pos(pos, from_direction);
    u16 from_state = try_get_block_state(from_pos);
    block_state_info from_info = describe_block_state(from_state);
    i32 from_type = from_info.block_type;

    // @TODO(traks) drop items if the block is broken

    // @TODO(traks) remove block entity data

    switch (cur_type) {
    case BLOCK_GRASS_BLOCK:
    case BLOCK_PODZOL:
    case BLOCK_MYCELIUM: {
        if (from_direction != DIRECTION_POS_Y) {
            return 0;
        }

        i32 type_above = from_type;
        if (type_above == BLOCK_SNOW_BLOCK || type_above == BLOCK_SNOW) {
            cur_info.snowy = 1;
        } else {
            cur_info.snowy = 0;
        }

        u16 new_state = make_block_state(&cur_info);
        if (new_state == cur_state) {
            return 0;
        }
        try_set_block_state(pos, new_state);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_OAK_SAPLING:
    case BLOCK_SPRUCE_SAPLING:
    case BLOCK_BIRCH_SAPLING:
    case BLOCK_JUNGLE_SAPLING:
    case BLOCK_ACACIA_SAPLING:
    case BLOCK_DARK_OAK_SAPLING:
    case BLOCK_DANDELION:
    case BLOCK_POPPY:
    case BLOCK_BLUE_ORCHID:
    case BLOCK_ALLIUM:
    case BLOCK_AZURE_BLUET:
    case BLOCK_RED_TULIP:
    case BLOCK_ORANGE_TULIP:
    case BLOCK_WHITE_TULIP:
    case BLOCK_PINK_TULIP:
    case BLOCK_OXEYE_DAISY:
    case BLOCK_CORNFLOWER:
    case BLOCK_LILY_OF_THE_VALLEY:
    case BLOCK_SWEET_BERRY_BUSH:
    case BLOCK_GRASS:
    case BLOCK_FERN: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        i32 type_below = from_type;
        if (can_plant_survive_on(type_below)) {
            return 0;
        }
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_WATER:
        break;
    case BLOCK_LAVA:
        break;
    case BLOCK_SAND:
        break;
    case BLOCK_RED_SAND:
        break;
    case BLOCK_GRAVEL:
        break;
    case BLOCK_OAK_LEAVES:
    case BLOCK_SPRUCE_LEAVES:
    case BLOCK_BIRCH_LEAVES:
    case BLOCK_JUNGLE_LEAVES:
    case BLOCK_ACACIA_LEAVES:
    case BLOCK_DARK_OAK_LEAVES:
    case BLOCK_AZALEA_LEAVES:
    case BLOCK_FLOWERING_AZALEA_LEAVES:
        break;
    case BLOCK_SPONGE:
        break;
    case BLOCK_DISPENSER:
        break;
    case BLOCK_NOTE_BLOCK:
        break;
    case BLOCK_WHITE_BED:
    case BLOCK_ORANGE_BED:
    case BLOCK_MAGENTA_BED:
    case BLOCK_LIGHT_BLUE_BED:
    case BLOCK_YELLOW_BED:
    case BLOCK_LIME_BED:
    case BLOCK_PINK_BED:
    case BLOCK_GRAY_BED:
    case BLOCK_LIGHT_GRAY_BED:
    case BLOCK_CYAN_BED:
    case BLOCK_PURPLE_BED:
    case BLOCK_BLUE_BED:
    case BLOCK_BROWN_BED:
    case BLOCK_GREEN_BED:
    case BLOCK_RED_BED:
    case BLOCK_BLACK_BED: {
        int facing = cur_info.horizontal_facing;
        if (from_direction == facing) {
            if (cur_info.bed_part == BED_PART_FOOT) {
                u16 new_state;
                if (from_type == cur_type && from_info.bed_part == BED_PART_HEAD) {
                    cur_info.occupied = from_info.occupied;
                    new_state = make_block_state(&cur_info);

                    if (new_state == cur_state) {
                        return 0;
                    }
                } else {
                    new_state = 0;
                }

                try_set_block_state(pos, new_state);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        } else if (from_direction == get_opposite_direction(facing)) {
            if (cur_info.bed_part == BED_PART_HEAD) {
                u16 new_state;
                if (from_type == cur_type && from_info.bed_part == BED_PART_FOOT) {
                    cur_info.occupied = from_info.occupied;
                    new_state = make_block_state(&cur_info);

                    if (new_state == cur_state) {
                        return 0;
                    }
                } else {
                    new_state = 0;
                }

                try_set_block_state(pos, new_state);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_POWERED_RAIL:
        break;
    case BLOCK_DETECTOR_RAIL:
        break;
    case BLOCK_STICKY_PISTON:
        break;
    case BLOCK_DEAD_BUSH: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        i32 type_below = from_type;
        if (can_dead_bush_survive_on(type_below)) {
            return 0;
        }
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_SEAGRASS:
        break;
    case BLOCK_TALL_SEAGRASS:
        break;
    case BLOCK_PISTON:
        break;
    case BLOCK_PISTON_HEAD:
        break;
    case BLOCK_MOVING_PISTON:
        break;
    case BLOCK_WITHER_ROSE: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        i32 type_below = from_type;
        if (can_wither_rose_survive_on(type_below)) {
            return 0;
        }
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BROWN_MUSHROOM:
        break;
    case BLOCK_RED_MUSHROOM:
        break;
    case BLOCK_TNT:
        break;
    case BLOCK_TORCH:
    case BLOCK_SOUL_TORCH:
    case BLOCK_CANDLE:
    case BLOCK_WHITE_CANDLE:
    case BLOCK_ORANGE_CANDLE:
    case BLOCK_MAGENTA_CANDLE:
    case BLOCK_LIGHT_BLUE_CANDLE:
    case BLOCK_YELLOW_CANDLE:
    case BLOCK_LIME_CANDLE:
    case BLOCK_PINK_CANDLE:
    case BLOCK_GRAY_CANDLE:
    case BLOCK_LIGHT_GRAY_CANDLE:
    case BLOCK_CYAN_CANDLE:
    case BLOCK_PURPLE_CANDLE:
    case BLOCK_BLUE_CANDLE:
    case BLOCK_BROWN_CANDLE:
    case BLOCK_GREEN_CANDLE:
    case BLOCK_RED_CANDLE:
    case BLOCK_BLACK_CANDLE: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        support_model support = get_support_model(from_state);
        if (support.pole_face_flags & (1 << DIRECTION_POS_Y)) {
            return 0;
        }

        // block below cannot support us
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_WALL_TORCH:
    case BLOCK_SOUL_WALL_TORCH:
    case BLOCK_LADDER: {
        if (from_direction != get_opposite_direction(cur_info.horizontal_facing)) {
            return 0;
        }

        support_model support = get_support_model(from_state);
        if (support.full_face_flags & (1 << cur_info.horizontal_facing)) {
            return 0;
        }

        // wall block cannot support us
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_FIRE:
        break;
    case BLOCK_SOUL_FIRE:
        break;
    case BLOCK_SPAWNER:
        break;
    case BLOCK_OAK_STAIRS:
    case BLOCK_COBBLESTONE_STAIRS:
    case BLOCK_BRICK_STAIRS:
    case BLOCK_STONE_BRICK_STAIRS:
    case BLOCK_NETHER_BRICK_STAIRS:
    case BLOCK_SANDSTONE_STAIRS:
    case BLOCK_SPRUCE_STAIRS:
    case BLOCK_BIRCH_STAIRS:
    case BLOCK_JUNGLE_STAIRS:
    case BLOCK_QUARTZ_STAIRS:
    case BLOCK_ACACIA_STAIRS:
    case BLOCK_DARK_OAK_STAIRS:
    case BLOCK_PRISMARINE_STAIRS:
    case BLOCK_PRISMARINE_BRICK_STAIRS:
    case BLOCK_DARK_PRISMARINE_STAIRS:
    case BLOCK_RED_SANDSTONE_STAIRS:
    case BLOCK_PURPUR_STAIRS:
    case BLOCK_POLISHED_GRANITE_STAIRS:
    case BLOCK_SMOOTH_RED_SANDSTONE_STAIRS:
    case BLOCK_MOSSY_STONE_BRICK_STAIRS:
    case BLOCK_POLISHED_DIORITE_STAIRS:
    case BLOCK_MOSSY_COBBLESTONE_STAIRS:
    case BLOCK_END_STONE_BRICK_STAIRS:
    case BLOCK_STONE_STAIRS:
    case BLOCK_SMOOTH_SANDSTONE_STAIRS:
    case BLOCK_SMOOTH_QUARTZ_STAIRS:
    case BLOCK_GRANITE_STAIRS:
    case BLOCK_ANDESITE_STAIRS:
    case BLOCK_RED_NETHER_BRICK_STAIRS:
    case BLOCK_POLISHED_ANDESITE_STAIRS:
    case BLOCK_DIORITE_STAIRS:
    case BLOCK_CRIMSON_STAIRS:
    case BLOCK_WARPED_STAIRS:
    case BLOCK_BLACKSTONE_STAIRS:
    case BLOCK_POLISHED_BLACKSTONE_BRICK_STAIRS:
    case BLOCK_POLISHED_BLACKSTONE_STAIRS:
    case BLOCK_OXIDIZED_CUT_COPPER_STAIRS:
    case BLOCK_WEATHERED_CUT_COPPER_STAIRS:
    case BLOCK_EXPOSED_CUT_COPPER_STAIRS:
    case BLOCK_CUT_COPPER_STAIRS:
    case BLOCK_WAXED_OXIDIZED_CUT_COPPER_STAIRS:
    case BLOCK_WAXED_WEATHERED_CUT_COPPER_STAIRS:
    case BLOCK_WAXED_EXPOSED_CUT_COPPER_STAIRS:
    case BLOCK_WAXED_CUT_COPPER_STAIRS:
    case BLOCK_COBBLED_DEEPSLATE_STAIRS:
    case BLOCK_POLISHED_DEEPSLATE_STAIRS:
    case BLOCK_DEEPSLATE_TILE_STAIRS:
    case BLOCK_DEEPSLATE_BRICK_STAIRS: {
        if (from_direction == DIRECTION_NEG_Y
                || from_direction == DIRECTION_POS_Y) {
            return 0;
        }

        int cur_shape = cur_info.stairs_shape;
        update_stairs_shape(pos, &cur_info);
        if (cur_shape == cur_info.stairs_shape) {
            return 0;
        }
        try_set_block_state(pos, make_block_state(&cur_info));
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_CHEST:
        break;
    case BLOCK_REDSTONE_WIRE: {
        if (from_direction == DIRECTION_NEG_Y) {
            if (!can_redstone_wire_survive_on(from_state)) {
                try_set_block_state(pos, 0);
                // @TODO(traks) also update diagonal redstone wires
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
            return 0;
        } else {
            // @TODO(traks) completely working implementation
            int res = update_redstone_wire(pos, cur_state, &cur_info, buc);
            update_redstone_line(pos);
            return res;
        }
    }
    case BLOCK_WHEAT:
    case BLOCK_BEETROOTS:
    case BLOCK_CARROTS:
    case BLOCK_POTATOES: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        // @TODO(traks) light level also needs to be sufficient
        i32 type_below = from_type;
        if (type_below == BLOCK_FARMLAND) {
            return 0;
        }

        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_FARMLAND:
        break;
    case BLOCK_FURNACE:
        break;
    case BLOCK_OAK_SIGN:
        break;
    case BLOCK_SPRUCE_SIGN:
        break;
    case BLOCK_BIRCH_SIGN:
        break;
    case BLOCK_ACACIA_SIGN:
        break;
    case BLOCK_JUNGLE_SIGN:
        break;
    case BLOCK_DARK_OAK_SIGN:
        break;
    case BLOCK_OAK_DOOR:
    case BLOCK_IRON_DOOR:
    case BLOCK_SPRUCE_DOOR:
    case BLOCK_BIRCH_DOOR:
    case BLOCK_JUNGLE_DOOR:
    case BLOCK_ACACIA_DOOR:
    case BLOCK_DARK_OAK_DOOR:
    case BLOCK_CRIMSON_DOOR:
    case BLOCK_WARPED_DOOR: {
        if (from_direction == DIRECTION_POS_Y) {
            if (cur_info.double_block_half == DOUBLE_BLOCK_HALF_LOWER) {
                u16 new_state;
                if (from_type == cur_type
                        && from_info.double_block_half == DOUBLE_BLOCK_HALF_UPPER) {
                    cur_info = from_info;
                    cur_info.double_block_half = DOUBLE_BLOCK_HALF_LOWER;
                    new_state = make_block_state(&cur_info);

                    if (new_state == cur_state) {
                        return 0;
                    }
                } else {
                    new_state = 0;
                }

                try_set_block_state(pos, new_state);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        } else if (from_direction == DIRECTION_NEG_Y) {
            if (cur_info.double_block_half == DOUBLE_BLOCK_HALF_UPPER) {
                u16 new_state;
                if (from_type == cur_type
                        && from_info.double_block_half == DOUBLE_BLOCK_HALF_LOWER) {
                    cur_info = from_info;
                    cur_info.double_block_half = DOUBLE_BLOCK_HALF_UPPER;
                    new_state = make_block_state(&cur_info);

                    if (new_state == cur_state) {
                        return 0;
                    }
                } else {
                    new_state = 0;
                }

                try_set_block_state(pos, new_state);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            } else {
                support_model support = get_support_model(from_state);
                if (support.full_face_flags & (1 << DIRECTION_POS_Y)) {
                    return 0;
                }

                try_set_block_state(pos, 0);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_RAIL:
        break;
    case BLOCK_OAK_WALL_SIGN:
        break;
    case BLOCK_SPRUCE_WALL_SIGN:
        break;
    case BLOCK_BIRCH_WALL_SIGN:
        break;
    case BLOCK_ACACIA_WALL_SIGN:
        break;
    case BLOCK_JUNGLE_WALL_SIGN:
        break;
    case BLOCK_DARK_OAK_WALL_SIGN:
        break;
    case BLOCK_STONE_PRESSURE_PLATE:
    case BLOCK_OAK_PRESSURE_PLATE:
    case BLOCK_SPRUCE_PRESSURE_PLATE:
    case BLOCK_BIRCH_PRESSURE_PLATE:
    case BLOCK_JUNGLE_PRESSURE_PLATE:
    case BLOCK_ACACIA_PRESSURE_PLATE:
    case BLOCK_DARK_OAK_PRESSURE_PLATE:
    case BLOCK_LIGHT_WEIGHTED_PRESSURE_PLATE:
    case BLOCK_HEAVY_WEIGHTED_PRESSURE_PLATE:
    case BLOCK_CRIMSON_PRESSURE_PLATE:
    case BLOCK_WARPED_PRESSURE_PLATE:
    case BLOCK_POLISHED_BLACKSTONE_PRESSURE_PLATE: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }
        if (can_pressure_plate_survive_on(from_state)) {
            return 0;
        }
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_REDSTONE_TORCH:
        break;
    case BLOCK_REDSTONE_WALL_TORCH:
        break;
    case BLOCK_STONE_BUTTON:
    case BLOCK_OAK_BUTTON:
    case BLOCK_SPRUCE_BUTTON:
    case BLOCK_BIRCH_BUTTON:
    case BLOCK_JUNGLE_BUTTON:
    case BLOCK_ACACIA_BUTTON:
    case BLOCK_DARK_OAK_BUTTON:
    case BLOCK_CRIMSON_BUTTON:
    case BLOCK_WARPED_BUTTON:
    case BLOCK_POLISHED_BLACKSTONE_BUTTON:
    case BLOCK_LEVER: {
        int wall_dir;
        switch (cur_info.attach_face) {
        case ATTACH_FACE_FLOOR:
            wall_dir = DIRECTION_NEG_Y;
            break;
        case ATTACH_FACE_CEILING:
            wall_dir = DIRECTION_POS_Y;
            break;
        default:
            // attach face wall
            wall_dir = get_opposite_direction(cur_info.horizontal_facing);
        }
        if (from_direction != wall_dir) {
            return 0;
        }

        support_model support = get_support_model(from_state);
        if (support.full_face_flags & (1 << from_direction)) {
            return 0;
        }

        // invalid wall block
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_SNOW: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }
        if (can_snow_survive_on(from_state)) {
            return 0;
        }
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_CACTUS:
        break;
    case BLOCK_SUGAR_CANE: {
        if (can_sugar_cane_survive_at(pos)) {
            return 0;
        }
        if (is_delayed) {
            break_block(pos);
            push_direct_neighbour_block_updates(pos, buc);
            return 1;
        } else {
            schedule_block_update(pos, from_direction, 1);
            return 0;
        }
    }
    case BLOCK_JUKEBOX:
        break;
    case BLOCK_OAK_FENCE:
    case BLOCK_NETHER_BRICK_FENCE:
    case BLOCK_SPRUCE_FENCE:
    case BLOCK_BIRCH_FENCE:
    case BLOCK_JUNGLE_FENCE:
    case BLOCK_ACACIA_FENCE:
    case BLOCK_DARK_OAK_FENCE:
    case BLOCK_CRIMSON_FENCE:
    case BLOCK_WARPED_FENCE: {
        // @TODO(traks) update water

        if (from_direction == DIRECTION_NEG_Y
                || from_direction == DIRECTION_POS_Y) {
            return 0;
        }
        update_fence_shape(pos, &cur_info, from_direction);
        u16 new_state = make_block_state(&cur_info);
        if (new_state == cur_state) {
            return 0;
        }
        try_set_block_state(pos, new_state);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_SOUL_SAND:
        break;
    case BLOCK_NETHER_PORTAL:
        break;
    case BLOCK_CAKE:
    case BLOCK_CANDLE_CAKE:
    case BLOCK_WHITE_CANDLE_CAKE:
    case BLOCK_ORANGE_CANDLE_CAKE:
    case BLOCK_MAGENTA_CANDLE_CAKE:
    case BLOCK_LIGHT_BLUE_CANDLE_CAKE:
    case BLOCK_YELLOW_CANDLE_CAKE:
    case BLOCK_LIME_CANDLE_CAKE:
    case BLOCK_PINK_CANDLE_CAKE:
    case BLOCK_GRAY_CANDLE_CAKE:
    case BLOCK_LIGHT_GRAY_CANDLE_CAKE:
    case BLOCK_CYAN_CANDLE_CAKE:
    case BLOCK_PURPLE_CANDLE_CAKE:
    case BLOCK_BLUE_CANDLE_CAKE:
    case BLOCK_BROWN_CANDLE_CAKE:
    case BLOCK_GREEN_CANDLE_CAKE:
    case BLOCK_RED_CANDLE_CAKE:
    case BLOCK_BLACK_CANDLE_CAKE:
        break;
    case BLOCK_REPEATER:
        break;
    case BLOCK_OAK_TRAPDOOR:
        break;
    case BLOCK_SPRUCE_TRAPDOOR:
        break;
    case BLOCK_BIRCH_TRAPDOOR:
        break;
    case BLOCK_JUNGLE_TRAPDOOR:
        break;
    case BLOCK_ACACIA_TRAPDOOR:
        break;
    case BLOCK_DARK_OAK_TRAPDOOR:
        break;
    case BLOCK_BROWN_MUSHROOM_BLOCK:
    case BLOCK_RED_MUSHROOM_BLOCK:
    case BLOCK_MUSHROOM_STEM: {
        if (from_type != cur_type) {
            return 0;
        }

        // connect to neighbouring mushroom block of the same type
        cur_info.values[BLOCK_PROPERTY_NEG_Y + from_direction] = 0;
        u16 new_state = make_block_state(&cur_info);
        if (new_state == cur_state) {
            return 0;
        }
        try_set_block_state(pos, new_state);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_IRON_BARS:
    case BLOCK_GLASS_PANE:
    case BLOCK_WHITE_STAINED_GLASS_PANE:
    case BLOCK_ORANGE_STAINED_GLASS_PANE:
    case BLOCK_MAGENTA_STAINED_GLASS_PANE:
    case BLOCK_LIGHT_BLUE_STAINED_GLASS_PANE:
    case BLOCK_YELLOW_STAINED_GLASS_PANE:
    case BLOCK_LIME_STAINED_GLASS_PANE:
    case BLOCK_PINK_STAINED_GLASS_PANE:
    case BLOCK_GRAY_STAINED_GLASS_PANE:
    case BLOCK_LIGHT_GRAY_STAINED_GLASS_PANE:
    case BLOCK_CYAN_STAINED_GLASS_PANE:
    case BLOCK_PURPLE_STAINED_GLASS_PANE:
    case BLOCK_BLUE_STAINED_GLASS_PANE:
    case BLOCK_BROWN_STAINED_GLASS_PANE:
    case BLOCK_GREEN_STAINED_GLASS_PANE:
    case BLOCK_RED_STAINED_GLASS_PANE:
    case BLOCK_BLACK_STAINED_GLASS_PANE: {
        // @TODO(traks) update water

        if (from_direction == DIRECTION_NEG_Y
                || from_direction == DIRECTION_POS_Y) {
            return 0;
        }
        update_pane_shape(pos, &cur_info, from_direction);
        u16 new_state = make_block_state(&cur_info);
        if (new_state == cur_state) {
            return 0;
        }
        try_set_block_state(pos, new_state);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_CHAIN:
        break;
    case BLOCK_ATTACHED_PUMPKIN_STEM:
        break;
    case BLOCK_ATTACHED_MELON_STEM:
        break;
    case BLOCK_PUMPKIN_STEM:
    case BLOCK_MELON_STEM: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        i32 type_below = from_type;
        if (type_below == BLOCK_FARMLAND) {
            return 0;
        }

        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_VINE:
        break;
    case BLOCK_GLOW_LICHEN:
        break;
    case BLOCK_OAK_FENCE_GATE:
    case BLOCK_SPRUCE_FENCE_GATE:
    case BLOCK_BIRCH_FENCE_GATE:
    case BLOCK_JUNGLE_FENCE_GATE:
    case BLOCK_ACACIA_FENCE_GATE:
    case BLOCK_DARK_OAK_FENCE_GATE:
    case BLOCK_CRIMSON_FENCE_GATE:
    case BLOCK_WARPED_FENCE_GATE: {
        int facing = cur_info.horizontal_facing;
        int rotated = rotate_direction_clockwise(facing);
        if (rotated != from_direction && rotated != get_opposite_direction(from_direction)) {
            return 0;
        }

        cur_info.in_wall = 0;
        if (facing == DIRECTION_POS_X || facing == DIRECTION_NEG_X) {
            int neighbour_state_pos = try_get_block_state(
                    get_relative_block_pos(pos, DIRECTION_POS_Z));
            int neighbour_state_neg = try_get_block_state(
                    get_relative_block_pos(pos, DIRECTION_NEG_Z));
            if (is_wall(serv->block_type_by_state[neighbour_state_pos])
                    || is_wall(serv->block_type_by_state[neighbour_state_neg])) {
                cur_info.in_wall = 1;
            }
        } else {
            // facing along z axis
            int neighbour_state_pos = try_get_block_state(
                    get_relative_block_pos(pos, DIRECTION_POS_X));
            int neighbour_state_neg = try_get_block_state(
                    get_relative_block_pos(pos, DIRECTION_NEG_X));
            if (is_wall(serv->block_type_by_state[neighbour_state_pos])
                    || is_wall(serv->block_type_by_state[neighbour_state_neg])) {
                cur_info.in_wall = 1;
            }
        }

        u16 new_state = make_block_state(&cur_info);
        if (new_state == cur_state) {
            return 0;
        }
        try_set_block_state(pos, new_state);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_LILY_PAD: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }
        if (can_lily_pad_survive_on(from_state)) {
            return 0;
        }
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_NETHER_WART: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        i32 type_below = from_type;
        if (type_below == BLOCK_SOUL_SAND) {
            return 0;
        }

        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_ENCHANTING_TABLE:
        break;
    case BLOCK_DRAGON_EGG:
        break;
    case BLOCK_REDSTONE_LAMP:
        break;
    case BLOCK_COCOA:
        break;
    case BLOCK_ENDER_CHEST:
        break;
    case BLOCK_TRIPWIRE_HOOK:
        break;
    case BLOCK_TRIPWIRE:
        break;
    case BLOCK_COMMAND_BLOCK:
        break;
    case BLOCK_BEACON:
        break;
    case BLOCK_COBBLESTONE_WALL:
    case BLOCK_MOSSY_COBBLESTONE_WALL:
    case BLOCK_BRICK_WALL:
    case BLOCK_PRISMARINE_WALL:
    case BLOCK_RED_SANDSTONE_WALL:
    case BLOCK_MOSSY_STONE_BRICK_WALL:
    case BLOCK_GRANITE_WALL:
    case BLOCK_STONE_BRICK_WALL:
    case BLOCK_NETHER_BRICK_WALL:
    case BLOCK_ANDESITE_WALL:
    case BLOCK_RED_NETHER_BRICK_WALL:
    case BLOCK_SANDSTONE_WALL:
    case BLOCK_END_STONE_BRICK_WALL:
    case BLOCK_DIORITE_WALL:
    case BLOCK_BLACKSTONE_WALL:
    case BLOCK_POLISHED_BLACKSTONE_BRICK_WALL:
    case BLOCK_POLISHED_BLACKSTONE_WALL:
    case BLOCK_COBBLED_DEEPSLATE_WALL:
    case BLOCK_POLISHED_DEEPSLATE_WALL:
    case BLOCK_DEEPSLATE_TILE_WALL:
    case BLOCK_DEEPSLATE_BRICK_WALL: {
        // @TODO(traks) update water

        if (from_direction == DIRECTION_NEG_Y) {
            return 0;
        }
        update_wall_shape(pos, &cur_info, from_direction);
        u16 new_state = make_block_state(&cur_info);
        if (new_state == cur_state) {
            return 0;
        }
        try_set_block_state(pos, new_state);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_ANVIL:
        break;
    case BLOCK_CHIPPED_ANVIL:
        break;
    case BLOCK_DAMAGED_ANVIL:
        break;
    case BLOCK_TRAPPED_CHEST:
        break;
    case BLOCK_COMPARATOR:
        break;
    case BLOCK_HOPPER:
        break;
    case BLOCK_ACTIVATOR_RAIL:
        break;
    case BLOCK_DROPPER:
        break;
    case BLOCK_IRON_TRAPDOOR:
        break;
    case BLOCK_PRISMARINE_SLAB:
    case BLOCK_PRISMARINE_BRICK_SLAB:
    case BLOCK_DARK_PRISMARINE_SLAB:
    case BLOCK_OAK_SLAB:
    case BLOCK_SPRUCE_SLAB:
    case BLOCK_BIRCH_SLAB:
    case BLOCK_JUNGLE_SLAB:
    case BLOCK_ACACIA_SLAB:
    case BLOCK_DARK_OAK_SLAB:
    case BLOCK_STONE_SLAB:
    case BLOCK_SMOOTH_STONE_SLAB:
    case BLOCK_SANDSTONE_SLAB:
    case BLOCK_CUT_SANDSTONE_SLAB:
    case BLOCK_PETRIFIED_OAK_SLAB:
    case BLOCK_COBBLESTONE_SLAB:
    case BLOCK_BRICK_SLAB:
    case BLOCK_STONE_BRICK_SLAB:
    case BLOCK_NETHER_BRICK_SLAB:
    case BLOCK_QUARTZ_SLAB:
    case BLOCK_RED_SANDSTONE_SLAB:
    case BLOCK_CUT_RED_SANDSTONE_SLAB:
    case BLOCK_PURPUR_SLAB:
    case BLOCK_POLISHED_GRANITE_SLAB:
    case BLOCK_SMOOTH_RED_SANDSTONE_SLAB:
    case BLOCK_MOSSY_STONE_BRICK_SLAB:
    case BLOCK_POLISHED_DIORITE_SLAB:
    case BLOCK_MOSSY_COBBLESTONE_SLAB:
    case BLOCK_END_STONE_BRICK_SLAB:
    case BLOCK_SMOOTH_SANDSTONE_SLAB:
    case BLOCK_SMOOTH_QUARTZ_SLAB:
    case BLOCK_GRANITE_SLAB:
    case BLOCK_ANDESITE_SLAB:
    case BLOCK_RED_NETHER_BRICK_SLAB:
    case BLOCK_POLISHED_ANDESITE_SLAB:
    case BLOCK_DIORITE_SLAB:
    case BLOCK_CRIMSON_SLAB:
    case BLOCK_WARPED_SLAB:
    case BLOCK_BLACKSTONE_SLAB:
    case BLOCK_POLISHED_BLACKSTONE_BRICK_SLAB:
    case BLOCK_POLISHED_BLACKSTONE_SLAB:
    case BLOCK_OXIDIZED_CUT_COPPER_SLAB:
    case BLOCK_WEATHERED_CUT_COPPER_SLAB:
    case BLOCK_EXPOSED_CUT_COPPER_SLAB:
    case BLOCK_CUT_COPPER_SLAB:
    case BLOCK_WAXED_OXIDIZED_CUT_COPPER_SLAB:
    case BLOCK_WAXED_WEATHERED_CUT_COPPER_SLAB:
    case BLOCK_WAXED_EXPOSED_CUT_COPPER_SLAB:
    case BLOCK_WAXED_CUT_COPPER_SLAB:
    case BLOCK_COBBLED_DEEPSLATE_SLAB:
    case BLOCK_POLISHED_DEEPSLATE_SLAB:
    case BLOCK_DEEPSLATE_TILE_SLAB:
    case BLOCK_DEEPSLATE_BRICK_SLAB:
        // @TODO(traks) fluid update
        break;
    case BLOCK_WHITE_CARPET:
    case BLOCK_ORANGE_CARPET:
    case BLOCK_MAGENTA_CARPET:
    case BLOCK_LIGHT_BLUE_CARPET:
    case BLOCK_YELLOW_CARPET:
    case BLOCK_LIME_CARPET:
    case BLOCK_PINK_CARPET:
    case BLOCK_GRAY_CARPET:
    case BLOCK_LIGHT_GRAY_CARPET:
    case BLOCK_CYAN_CARPET:
    case BLOCK_PURPLE_CARPET:
    case BLOCK_BLUE_CARPET:
    case BLOCK_BROWN_CARPET:
    case BLOCK_GREEN_CARPET:
    case BLOCK_RED_CARPET:
    case BLOCK_BLACK_CARPET:
    case BLOCK_MOSS_CARPET: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        i32 type_below = from_type;
        if (can_carpet_survive_on(type_below)) {
            return 0;
        }
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_SUNFLOWER:
    case BLOCK_LILAC:
    case BLOCK_ROSE_BUSH:
    case BLOCK_PEONY:
    case BLOCK_TALL_GRASS:
    case BLOCK_LARGE_FERN: {
        if (cur_info.double_block_half == DOUBLE_BLOCK_HALF_UPPER) {
            if (from_direction == DIRECTION_NEG_Y && (from_type != cur_type
                    || from_info.double_block_half != DOUBLE_BLOCK_HALF_LOWER)) {
                try_set_block_state(pos, 0);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        } else {
            if (from_direction == DIRECTION_NEG_Y) {
                if (!can_plant_survive_on(from_type)) {
                    try_set_block_state(pos, 0);
                    push_direct_neighbour_block_updates(pos, buc);
                    return 1;
                }
            } else if (from_direction == DIRECTION_POS_Y) {
                if (from_type != cur_type
                        || from_info.double_block_half != DOUBLE_BLOCK_HALF_UPPER) {
                    try_set_block_state(pos, 0);
                    push_direct_neighbour_block_updates(pos, buc);
                    return 1;
                }
            }
        }
        return 0;
    }
    case BLOCK_WHITE_BANNER:
        break;
    case BLOCK_ORANGE_BANNER:
        break;
    case BLOCK_MAGENTA_BANNER:
        break;
    case BLOCK_LIGHT_BLUE_BANNER:
        break;
    case BLOCK_YELLOW_BANNER:
        break;
    case BLOCK_LIME_BANNER:
        break;
    case BLOCK_PINK_BANNER:
        break;
    case BLOCK_GRAY_BANNER:
        break;
    case BLOCK_LIGHT_GRAY_BANNER:
        break;
    case BLOCK_CYAN_BANNER:
        break;
    case BLOCK_PURPLE_BANNER:
        break;
    case BLOCK_BLUE_BANNER:
        break;
    case BLOCK_BROWN_BANNER:
        break;
    case BLOCK_GREEN_BANNER:
        break;
    case BLOCK_RED_BANNER:
        break;
    case BLOCK_BLACK_BANNER:
        break;
    case BLOCK_WHITE_WALL_BANNER:
        break;
    case BLOCK_ORANGE_WALL_BANNER:
        break;
    case BLOCK_MAGENTA_WALL_BANNER:
        break;
    case BLOCK_LIGHT_BLUE_WALL_BANNER:
        break;
    case BLOCK_YELLOW_WALL_BANNER:
        break;
    case BLOCK_LIME_WALL_BANNER:
        break;
    case BLOCK_PINK_WALL_BANNER:
        break;
    case BLOCK_GRAY_WALL_BANNER:
        break;
    case BLOCK_LIGHT_GRAY_WALL_BANNER:
        break;
    case BLOCK_CYAN_WALL_BANNER:
        break;
    case BLOCK_PURPLE_WALL_BANNER:
        break;
    case BLOCK_BLUE_WALL_BANNER:
        break;
    case BLOCK_BROWN_WALL_BANNER:
        break;
    case BLOCK_GREEN_WALL_BANNER:
        break;
    case BLOCK_RED_WALL_BANNER:
        break;
    case BLOCK_BLACK_WALL_BANNER:
        break;
    case BLOCK_CHORUS_PLANT:
        break;
    case BLOCK_CHORUS_FLOWER:
        break;
    case BLOCK_DIRT_PATH:
        break;
    case BLOCK_REPEATING_COMMAND_BLOCK:
        break;
    case BLOCK_CHAIN_COMMAND_BLOCK:
        break;
    case BLOCK_FROSTED_ICE:
        break;
    case BLOCK_MAGMA_BLOCK:
        break;
    case BLOCK_OBSERVER:
        break;
    case BLOCK_SHULKER_BOX:
        break;
    case BLOCK_WHITE_SHULKER_BOX:
        break;
    case BLOCK_ORANGE_SHULKER_BOX:
        break;
    case BLOCK_MAGENTA_SHULKER_BOX:
        break;
    case BLOCK_LIGHT_BLUE_SHULKER_BOX:
        break;
    case BLOCK_YELLOW_SHULKER_BOX:
        break;
    case BLOCK_LIME_SHULKER_BOX:
        break;
    case BLOCK_PINK_SHULKER_BOX:
        break;
    case BLOCK_GRAY_SHULKER_BOX:
        break;
    case BLOCK_LIGHT_GRAY_SHULKER_BOX:
        break;
    case BLOCK_CYAN_SHULKER_BOX:
        break;
    case BLOCK_PURPLE_SHULKER_BOX:
        break;
    case BLOCK_BLUE_SHULKER_BOX:
        break;
    case BLOCK_BROWN_SHULKER_BOX:
        break;
    case BLOCK_GREEN_SHULKER_BOX:
        break;
    case BLOCK_RED_SHULKER_BOX:
        break;
    case BLOCK_BLACK_SHULKER_BOX:
        break;
    case BLOCK_WHITE_CONCRETE_POWDER:
        break;
    case BLOCK_ORANGE_CONCRETE_POWDER:
        break;
    case BLOCK_MAGENTA_CONCRETE_POWDER:
        break;
    case BLOCK_LIGHT_BLUE_CONCRETE_POWDER:
        break;
    case BLOCK_YELLOW_CONCRETE_POWDER:
        break;
    case BLOCK_LIME_CONCRETE_POWDER:
        break;
    case BLOCK_PINK_CONCRETE_POWDER:
        break;
    case BLOCK_GRAY_CONCRETE_POWDER:
        break;
    case BLOCK_LIGHT_GRAY_CONCRETE_POWDER:
        break;
    case BLOCK_CYAN_CONCRETE_POWDER:
        break;
    case BLOCK_PURPLE_CONCRETE_POWDER:
        break;
    case BLOCK_BLUE_CONCRETE_POWDER:
        break;
    case BLOCK_BROWN_CONCRETE_POWDER:
        break;
    case BLOCK_GREEN_CONCRETE_POWDER:
        break;
    case BLOCK_RED_CONCRETE_POWDER:
        break;
    case BLOCK_BLACK_CONCRETE_POWDER:
        break;
    case BLOCK_KELP:
        break;
    case BLOCK_KELP_PLANT:
        break;
    case BLOCK_TUBE_CORAL_BLOCK:
        break;
    case BLOCK_BRAIN_CORAL_BLOCK:
        break;
    case BLOCK_BUBBLE_CORAL_BLOCK:
        break;
    case BLOCK_FIRE_CORAL_BLOCK:
        break;
    case BLOCK_HORN_CORAL_BLOCK:
        break;
    case BLOCK_DEAD_TUBE_CORAL:
        break;
    case BLOCK_DEAD_BRAIN_CORAL:
        break;
    case BLOCK_DEAD_BUBBLE_CORAL:
        break;
    case BLOCK_DEAD_FIRE_CORAL:
        break;
    case BLOCK_DEAD_HORN_CORAL:
        break;
    case BLOCK_TUBE_CORAL:
        break;
    case BLOCK_BRAIN_CORAL:
        break;
    case BLOCK_BUBBLE_CORAL:
        break;
    case BLOCK_FIRE_CORAL:
        break;
    case BLOCK_HORN_CORAL:
        break;
    case BLOCK_DEAD_TUBE_CORAL_FAN:
        break;
    case BLOCK_DEAD_BRAIN_CORAL_FAN:
        break;
    case BLOCK_DEAD_BUBBLE_CORAL_FAN:
        break;
    case BLOCK_DEAD_FIRE_CORAL_FAN:
        break;
    case BLOCK_DEAD_HORN_CORAL_FAN:
        break;
    case BLOCK_TUBE_CORAL_FAN:
        break;
    case BLOCK_BRAIN_CORAL_FAN:
        break;
    case BLOCK_BUBBLE_CORAL_FAN:
        break;
    case BLOCK_FIRE_CORAL_FAN:
        break;
    case BLOCK_HORN_CORAL_FAN:
        break;
    case BLOCK_DEAD_TUBE_CORAL_WALL_FAN:
        break;
    case BLOCK_DEAD_BRAIN_CORAL_WALL_FAN:
        break;
    case BLOCK_DEAD_BUBBLE_CORAL_WALL_FAN:
        break;
    case BLOCK_DEAD_FIRE_CORAL_WALL_FAN:
        break;
    case BLOCK_DEAD_HORN_CORAL_WALL_FAN:
        break;
    case BLOCK_TUBE_CORAL_WALL_FAN:
        break;
    case BLOCK_BRAIN_CORAL_WALL_FAN:
        break;
    case BLOCK_BUBBLE_CORAL_WALL_FAN:
        break;
    case BLOCK_FIRE_CORAL_WALL_FAN:
        break;
    case BLOCK_HORN_CORAL_WALL_FAN:
        break;
    case BLOCK_SEA_PICKLE: {
        // @TODO(traks) water scheduled tick
        if (from_direction == DIRECTION_NEG_Y) {
            if (!can_sea_pickle_survive_on(from_state)) {
                break_block(pos);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_CONDUIT:
        break;
    case BLOCK_BAMBOO_SAPLING: {
        if (from_direction == DIRECTION_NEG_Y) {
            if (!is_bamboo_plantable_on(from_type)) {
                break_block(pos);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        } else if (from_direction == DIRECTION_POS_Y) {
            if (from_type == BLOCK_BAMBOO) {
                u16 new_state = get_default_block_state(BLOCK_BAMBOO);
                try_set_block_state(pos, new_state);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_BAMBOO: {
        if (from_direction == DIRECTION_NEG_Y) {
            if (!is_bamboo_plantable_on(from_type)) {
                if (is_delayed) {
                    break_block(pos);
                    push_direct_neighbour_block_updates(pos, buc);
                    return 1;
                } else {
                    schedule_block_update(pos, from_direction, 1);
                }
            }
        } else if (from_direction == DIRECTION_POS_Y) {
            if (from_type == BLOCK_BAMBOO && from_info.age_1 > cur_info.age_1) {
                cur_info.age_1++;
                u16 new_state = make_block_state(&cur_info);
                try_set_block_state(pos, new_state);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_BUBBLE_COLUMN:
        break;
    case BLOCK_SCAFFOLDING:
        break;
    case BLOCK_BELL:
        break;
    case BLOCK_LANTERN:
        break;
    case BLOCK_SOUL_LANTERN:
        break;
    case BLOCK_CAMPFIRE:
        break;
    case BLOCK_SOUL_CAMPFIRE:
        break;
    case BLOCK_WARPED_FUNGUS:
    case BLOCK_CRIMSON_FUNGUS:
    case BLOCK_WARPED_ROOTS:
    case BLOCK_CRIMSON_ROOTS:
    case BLOCK_NETHER_SPROUTS:
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        i32 type_below = from_type;
        if (can_nether_plant_survive_on(type_below)) {
            return 0;
        }
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    case BLOCK_WEEPING_VINES:
        break;
    case BLOCK_WEEPING_VINES_PLANT:
        break;
    case BLOCK_TWISTING_VINES:
        break;
    case BLOCK_TWISTING_VINES_PLANT:
        break;
    case BLOCK_CRIMSON_TRAPDOOR:
        break;
    case BLOCK_WARPED_TRAPDOOR:
        break;
    case BLOCK_CRIMSON_SIGN:
        break;
    case BLOCK_WARPED_SIGN:
        break;
    case BLOCK_CRIMSON_WALL_SIGN:
        break;
    case BLOCK_WARPED_WALL_SIGN:
        break;
    case BLOCK_STRUCTURE_BLOCK:
        break;
    case BLOCK_BEE_NEST:
        break;
    case BLOCK_BEEHIVE:
        break;
    case BLOCK_AMETHYST_CLUSTER:
    case BLOCK_LARGE_AMETHYST_BUD:
    case BLOCK_MEDIUM_AMETHYST_BUD:
    case BLOCK_SMALL_AMETHYST_BUD: {
        if (from_direction != get_opposite_direction(cur_info.facing)) {
            return 0;
        }

        support_model support = get_support_model(from_state);
        if (support.full_face_flags & (1 << cur_info.facing)) {
            return 0;
        }

        // wall block cannot support us
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_SCULK_SENSOR:
        break;
    case BLOCK_LIGHTNING_ROD:
        // @TODO(traks) fluid update
        break;
    case BLOCK_POINTED_DRIPSTONE:
        break;
    case BLOCK_CAVE_VINES:
        break;
    case BLOCK_CAVE_VINES_PLANT:
        break;
    case BLOCK_SPORE_BLOSSOM: {
        if (from_direction != DIRECTION_POS_Y) {
            return 0;
        }

        support_model support = get_support_model(from_state);
        if (support.pole_face_flags & (1 << DIRECTION_NEG_Y)) {
            return 0;
        }

        // block above cannot support us
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_AZALEA:
    case BLOCK_FLOWERING_AZALEA: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        i32 type_below = from_type;
        if (can_azalea_survive_on(type_below)) {
            return 0;
        }
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BIG_DRIPLEAF: {
        // @TODO(traks) fluid update
        if (from_direction == DIRECTION_NEG_Y) {
            if (!can_azalea_survive_on(from_state)) {
                break_block(pos);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        } else if (from_direction == DIRECTION_POS_Y) {
            if (from_type == cur_type) {
                // transform into stem block with same properties
                cur_info.block_type = BLOCK_BIG_DRIPLEAF_STEM;
                u16 new_state = make_block_state(&cur_info);
                try_set_block_state(pos, new_state);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_BIG_DRIPLEAF_STEM: {
        // @TODO(traks) fluid update
        if (from_direction == DIRECTION_NEG_Y || from_direction == DIRECTION_POS_Y) {
            if (!can_big_dripleaf_stem_survive_at(pos)) {
                if (is_delayed) {
                    break_block(pos);
                    push_direct_neighbour_block_updates(pos, buc);
                    return 1;
                } else {
                    schedule_block_update(pos, from_direction, 1);
                    return 0;
                }
            }
        }
        return 0;
    }
    case BLOCK_SMALL_DRIPLEAF: {
        // @TODO(traks) fluid update
        if (cur_info.double_block_half == DOUBLE_BLOCK_HALF_UPPER) {
            if (from_direction == DIRECTION_NEG_Y && (from_type != cur_type
                    || from_info.double_block_half != DOUBLE_BLOCK_HALF_LOWER)) {
                try_set_block_state(pos, 0);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        } else {
            if (from_direction == DIRECTION_NEG_Y) {
                if (!can_small_dripleaf_survive_at(from_type, cur_state)) {
                    try_set_block_state(pos, 0);
                    push_direct_neighbour_block_updates(pos, buc);
                    return 1;
                }
            } else if (from_direction == DIRECTION_POS_Y) {
                if (from_type != cur_type
                        || from_info.double_block_half != DOUBLE_BLOCK_HALF_UPPER) {
                    try_set_block_state(pos, 0);
                    push_direct_neighbour_block_updates(pos, buc);
                    return 1;
                }
            }
        }
        return 0;
    }
    case BLOCK_HANGING_ROOTS:
    default:
         // nothing
         break;
    }
    return 0;
}

void
propagate_delayed_block_updates(MemoryArena * scratch_arena) {
    MemoryArena temp_arena = *scratch_arena;
    int max_updates = 512;
    block_update_context buc = {
        .blocks_to_update = alloc_in_arena(&temp_arena,
                max_updates * sizeof (block_update)),
        .update_count = 0,
        .max_updates = max_updates
    };

    int sbu_count = serv->scheduled_block_update_count;
    for (int i = 0; i < sbu_count; i++) {
        scheduled_block_update sbu = serv->scheduled_block_updates[i];
        if (sbu.for_tick != serv->current_tick) {
            continue;
        }

        // move last to current position
        sbu_count--;
        serv->scheduled_block_updates[i] = serv->scheduled_block_updates[sbu_count];
        i--;

        // @TODO(traks) also count these for number of updates
        BlockPos pos = sbu.pos;
        update_block(pos, sbu.from_direction, 1, &buc);
    }

    serv->scheduled_block_update_count = sbu_count;

    for (int i = 0; i < buc.update_count; i++) {
        BlockPos pos = buc.blocks_to_update[i].pos;
        int from_direction = buc.blocks_to_update[i].from_direction;
        update_block(pos, from_direction, 0, &buc);
    }
}

void
propagate_block_updates(block_update_context * buc) {
    for (int i = 0; i < buc->update_count; i++) {
        BlockPos pos = buc->blocks_to_update[i].pos;
        int from_direction = buc->blocks_to_update[i].from_direction;
        update_block(pos, from_direction, 0, buc);
    }
}

int
use_block(entity_base * player,
        i32 hand, BlockPos clicked_pos, i32 clicked_face,
        float click_offset_x, float click_offset_y, float click_offset_z,
        u8 is_inside, block_update_context * buc) {
    u16 cur_state = try_get_block_state(clicked_pos);
    block_state_info cur_info = describe_block_state(cur_state);
    i32 cur_type = cur_info.block_type;

    switch (cur_type) {
    case BLOCK_DISPENSER:
        // @TODO
        return 0;
    case BLOCK_NOTE_BLOCK:
        // @TODO
        return 0;
    case BLOCK_WHITE_BED:
    case BLOCK_ORANGE_BED:
    case BLOCK_MAGENTA_BED:
    case BLOCK_LIGHT_BLUE_BED:
    case BLOCK_YELLOW_BED:
    case BLOCK_LIME_BED:
    case BLOCK_PINK_BED:
    case BLOCK_GRAY_BED:
    case BLOCK_LIGHT_GRAY_BED:
    case BLOCK_CYAN_BED:
    case BLOCK_PURPLE_BED:
    case BLOCK_BLUE_BED:
    case BLOCK_BROWN_BED:
    case BLOCK_GREEN_BED:
    case BLOCK_RED_BED:
    case BLOCK_BLACK_BED:
        // @TODO
        return 0;
    case BLOCK_TNT:
        // @TODO
        return 0;
    case BLOCK_CHEST:
        // @TODO
        return 0;
    case BLOCK_REDSTONE_WIRE: {
        if (is_redstone_wire_dot(&cur_info)) {
            cur_info.redstone_pos_x = REDSTONE_SIDE_SIDE;
            cur_info.redstone_pos_z = REDSTONE_SIDE_SIDE;
            cur_info.redstone_neg_x = REDSTONE_SIDE_SIDE;
            cur_info.redstone_neg_z = REDSTONE_SIDE_SIDE;
            u16 new_state = make_block_state(&cur_info);
            try_set_block_state(clicked_pos, new_state);
            push_direct_neighbour_block_updates(clicked_pos, buc);
            return 1;
        } else if (!is_redstone_wire_connected(clicked_pos, &cur_info)) {
            cur_info.redstone_pos_x = REDSTONE_SIDE_NONE;
            cur_info.redstone_pos_z = REDSTONE_SIDE_NONE;
            cur_info.redstone_neg_x = REDSTONE_SIDE_NONE;
            cur_info.redstone_neg_z = REDSTONE_SIDE_NONE;
            u16 new_state = make_block_state(&cur_info);
            try_set_block_state(clicked_pos, new_state);
            push_direct_neighbour_block_updates(clicked_pos, buc);
            return 1;
        }
        return 0;
    }
    case BLOCK_CRAFTING_TABLE:
        // @TODO
        return 0;
    case BLOCK_FURNACE:
        // @TODO
        return 0;
    case BLOCK_OAK_SIGN:
    case BLOCK_SPRUCE_SIGN:
    case BLOCK_BIRCH_SIGN:
    case BLOCK_ACACIA_SIGN:
    case BLOCK_JUNGLE_SIGN:
    case BLOCK_DARK_OAK_SIGN:
    case BLOCK_OAK_WALL_SIGN:
    case BLOCK_CRIMSON_SIGN:
    case BLOCK_WARPED_SIGN:
        // @TODO
        return 0;
    case BLOCK_SPRUCE_WALL_SIGN:
    case BLOCK_BIRCH_WALL_SIGN:
    case BLOCK_ACACIA_WALL_SIGN:
    case BLOCK_JUNGLE_WALL_SIGN:
    case BLOCK_DARK_OAK_WALL_SIGN:
    case BLOCK_CRIMSON_WALL_SIGN:
    case BLOCK_WARPED_WALL_SIGN:
        // @TODO
        return 0;
    case BLOCK_LEVER: {
        // @TODO play flip sound
        cur_info.powered = !cur_info.powered;
        u16 new_state = make_block_state(&cur_info);
        try_set_block_state(clicked_pos, new_state);
        push_direct_neighbour_block_updates(clicked_pos, buc);
        return 1;
    }
    case BLOCK_OAK_DOOR:
    case BLOCK_SPRUCE_DOOR:
    case BLOCK_BIRCH_DOOR:
    case BLOCK_JUNGLE_DOOR:
    case BLOCK_ACACIA_DOOR:
    case BLOCK_DARK_OAK_DOOR:
    case BLOCK_CRIMSON_DOOR:
    case BLOCK_WARPED_DOOR: {
        // @TODO(traks) play opening/closing sound
        cur_info.open = !cur_info.open;
        u16 new_state = make_block_state(&cur_info);
        try_set_block_state(clicked_pos, new_state);
        // this will cause the other half of the door to switch states
        // @TODO(traks) perhaps we should just update the other half of the door
        // immediately and push block updates from there as well
        push_direct_neighbour_block_updates(clicked_pos, buc);
        return 1;
    }
    case BLOCK_REDSTONE_ORE:
    case BLOCK_DEEPSLATE_REDSTONE_ORE:
        // @TODO
        return 0;
    case BLOCK_STONE_BUTTON:
    case BLOCK_OAK_BUTTON:
    case BLOCK_SPRUCE_BUTTON:
    case BLOCK_BIRCH_BUTTON:
    case BLOCK_JUNGLE_BUTTON:
    case BLOCK_ACACIA_BUTTON:
    case BLOCK_DARK_OAK_BUTTON:
    case BLOCK_CRIMSON_BUTTON:
    case BLOCK_WARPED_BUTTON:
    case BLOCK_POLISHED_BLACKSTONE_BUTTON:
        // @TODO
        return 0;
    case BLOCK_JUKEBOX:
        // @TODO
        return 0;
    case BLOCK_OAK_FENCE:
    case BLOCK_NETHER_BRICK_FENCE:
    case BLOCK_SPRUCE_FENCE:
    case BLOCK_BIRCH_FENCE:
    case BLOCK_JUNGLE_FENCE:
    case BLOCK_ACACIA_FENCE:
    case BLOCK_DARK_OAK_FENCE:
    case BLOCK_CRIMSON_FENCE:
    case BLOCK_WARPED_FENCE:
        // @TODO
        return 0;
    case BLOCK_PUMPKIN:
        // @TODO
        return 0;
    case BLOCK_CAKE:
        // @TODO
        return 0;
    case BLOCK_REPEATER: {
        // @TODO(traks) do stuff if there's a signal going through the repeater
        // currently?
        cur_info.delay = (cur_info.delay & 0x3) + 1;
        u16 new_state = make_block_state(&cur_info);
        try_set_block_state(clicked_pos, new_state);
        push_direct_neighbour_block_updates(clicked_pos, buc);
        return 1;
    }
    case BLOCK_OAK_TRAPDOOR:
    case BLOCK_SPRUCE_TRAPDOOR:
    case BLOCK_BIRCH_TRAPDOOR:
    case BLOCK_JUNGLE_TRAPDOOR:
    case BLOCK_ACACIA_TRAPDOOR:
    case BLOCK_DARK_OAK_TRAPDOOR:
    case BLOCK_CRIMSON_TRAPDOOR:
    case BLOCK_WARPED_TRAPDOOR: {
        // @TODO(traks) play opening/closing sound
        cur_info.open = !cur_info.open;
        u16 new_state = make_block_state(&cur_info);
        try_set_block_state(clicked_pos, new_state);
        push_direct_neighbour_block_updates(clicked_pos, buc);
        return 1;
    }
    case BLOCK_OAK_FENCE_GATE:
    case BLOCK_SPRUCE_FENCE_GATE:
    case BLOCK_BIRCH_FENCE_GATE:
    case BLOCK_JUNGLE_FENCE_GATE:
    case BLOCK_ACACIA_FENCE_GATE:
    case BLOCK_DARK_OAK_FENCE_GATE:
    case BLOCK_CRIMSON_FENCE_GATE:
    case BLOCK_WARPED_FENCE_GATE: {
        if (cur_info.open) {
            cur_info.open = 0;
        } else {
            int player_facing = get_player_facing(player);
            if (cur_info.horizontal_facing == get_opposite_direction(player_facing)) {
                cur_info.horizontal_facing = player_facing;
            }

            cur_info.open = 1;
        }

        u16 new_state = make_block_state(&cur_info);
        try_set_block_state(clicked_pos, new_state);
        push_direct_neighbour_block_updates(clicked_pos, buc);

        // @TODO(traks) broadcast level event for opening/closing fence gate
        return 1;
    }
    case BLOCK_ENCHANTING_TABLE:
        // @TODO
        return 0;
    case BLOCK_BREWING_STAND:
        // @TODO
        return 0;
    case BLOCK_CAULDRON:
    case BLOCK_WATER_CAULDRON:
    case BLOCK_LAVA_CAULDRON:
    case BLOCK_POWDER_SNOW_CAULDRON:
        // @TODO
        return 0;
    case BLOCK_DRAGON_EGG:
        // @TODO
        return 0;
    case BLOCK_ENDER_CHEST:
        // @TODO
        return 0;
    case BLOCK_COMMAND_BLOCK:
        // @TODO
        return 0;
    case BLOCK_REPEATING_COMMAND_BLOCK:
        // @TODO
        return 0;
    case BLOCK_CHAIN_COMMAND_BLOCK:
        // @TODO
        return 0;
    case BLOCK_BEACON:
        // @TODO
        return 0;
    case BLOCK_FLOWER_POT:
        // @TODO
        return 0;
    case BLOCK_POTTED_OAK_SAPLING:
        // @TODO
        return 0;
    case BLOCK_POTTED_SPRUCE_SAPLING:
        // @TODO
        return 0;
    case BLOCK_POTTED_BIRCH_SAPLING:
        // @TODO
        return 0;
    case BLOCK_POTTED_JUNGLE_SAPLING:
        // @TODO
        return 0;
    case BLOCK_POTTED_ACACIA_SAPLING:
        // @TODO
        return 0;
    case BLOCK_POTTED_DARK_OAK_SAPLING:
        // @TODO
        return 0;
    case BLOCK_POTTED_FERN:
        // @TODO
        return 0;
    case BLOCK_POTTED_DANDELION:
        // @TODO
        return 0;
    case BLOCK_POTTED_POPPY:
        // @TODO
        return 0;
    case BLOCK_POTTED_BLUE_ORCHID:
        // @TODO
        return 0;
    case BLOCK_POTTED_ALLIUM:
        // @TODO
        return 0;
    case BLOCK_POTTED_AZURE_BLUET:
        // @TODO
        return 0;
    case BLOCK_POTTED_RED_TULIP:
        // @TODO
        return 0;
    case BLOCK_POTTED_ORANGE_TULIP:
        // @TODO
        return 0;
    case BLOCK_POTTED_WHITE_TULIP:
        // @TODO
        return 0;
    case BLOCK_POTTED_PINK_TULIP:
        // @TODO
        return 0;
    case BLOCK_POTTED_OXEYE_DAISY:
        // @TODO
        return 0;
    case BLOCK_POTTED_CORNFLOWER:
        // @TODO
        return 0;
    case BLOCK_POTTED_LILY_OF_THE_VALLEY:
        // @TODO
        return 0;
    case BLOCK_POTTED_WITHER_ROSE:
        // @TODO
        return 0;
    case BLOCK_POTTED_RED_MUSHROOM:
        // @TODO
        return 0;
    case BLOCK_POTTED_BROWN_MUSHROOM:
        // @TODO
        return 0;
    case BLOCK_POTTED_DEAD_BUSH:
        // @TODO
        return 0;
    case BLOCK_POTTED_CACTUS:
        // @TODO
        return 0;
    case BLOCK_POTTED_BAMBOO:
        // @TODO
        return 0;
    case BLOCK_POTTED_CRIMSON_FUNGUS:
        // @TODO
        return 0;
    case BLOCK_POTTED_WARPED_FUNGUS:
        // @TODO
        return 0;
    case BLOCK_POTTED_CRIMSON_ROOTS:
        // @TODO
        return 0;
    case BLOCK_POTTED_WARPED_ROOTS:
        // @TODO
        return 0;
    case BLOCK_POTTED_AZALEA_BUSH:
        // @TODO
        return 0;
    case BLOCK_POTTED_FLOWERING_AZALEA_BUSH:
        // @TODO
        return 0;
    case BLOCK_ANVIL:
    case BLOCK_CHIPPED_ANVIL:
    case BLOCK_DAMAGED_ANVIL:
        // @TODO
        return 0;
    case BLOCK_TRAPPED_CHEST:
        // @TODO
        return 0;
    case BLOCK_COMPARATOR: {
        // @TODO(traks) do stuff if there's a signal going through the
        // comparator currently
        // @TODO(traks) play sound
        cur_info.mode_comparator = !cur_info.mode_comparator;
        u16 new_state = make_block_state(&cur_info);
        try_set_block_state(clicked_pos, new_state);
        push_direct_neighbour_block_updates(clicked_pos, buc);
        return 1;
    }
    case BLOCK_DAYLIGHT_DETECTOR: {
        // @TODO(traks) update output signal
        cur_info.inverted = !cur_info.inverted;
        u16 new_state = make_block_state(&cur_info);
        try_set_block_state(clicked_pos, new_state);
        push_direct_neighbour_block_updates(clicked_pos, buc);
        return 1;
    }
    case BLOCK_HOPPER:
        // @TODO
        return 0;
    case BLOCK_DROPPER:
        // @TODO
        return 0;
    case BLOCK_LIGHT:
        // @TODO
        return 0;
    case BLOCK_SHULKER_BOX:
    case BLOCK_WHITE_SHULKER_BOX:
    case BLOCK_ORANGE_SHULKER_BOX:
    case BLOCK_MAGENTA_SHULKER_BOX:
    case BLOCK_LIGHT_BLUE_SHULKER_BOX:
    case BLOCK_YELLOW_SHULKER_BOX:
    case BLOCK_LIME_SHULKER_BOX:
    case BLOCK_PINK_SHULKER_BOX:
    case BLOCK_GRAY_SHULKER_BOX:
    case BLOCK_LIGHT_GRAY_SHULKER_BOX:
    case BLOCK_CYAN_SHULKER_BOX:
    case BLOCK_PURPLE_SHULKER_BOX:
    case BLOCK_BLUE_SHULKER_BOX:
    case BLOCK_BROWN_SHULKER_BOX:
    case BLOCK_GREEN_SHULKER_BOX:
    case BLOCK_RED_SHULKER_BOX:
    case BLOCK_BLACK_SHULKER_BOX:
        // @TODO
        return 0;
    case BLOCK_LOOM:
        // @TODO
        return 0;
    case BLOCK_BARREL:
        // @TODO
        return 0;
    case BLOCK_SMOKER:
        // @TODO
        return 0;
    case BLOCK_BLAST_FURNACE:
        // @TODO
        return 0;
    case BLOCK_CARTOGRAPHY_TABLE:
        // @TODO
        return 0;
    case BLOCK_FLETCHING_TABLE:
        // @TODO
        return 0;
    case BLOCK_GRINDSTONE:
        // @TODO
        return 0;
    case BLOCK_LECTERN:
        // @TODO
        return 0;
    case BLOCK_SMITHING_TABLE:
        // @TODO
        return 0;
    case BLOCK_STONECUTTER:
        // @TODO
        return 0;
    case BLOCK_BELL:
        // @TODO
        return 0;
    case BLOCK_CAMPFIRE:
    case BLOCK_SOUL_CAMPFIRE:
        // @TODO
        return 0;
    case BLOCK_SWEET_BERRY_BUSH:
        // @TODO
        return 0;
    case BLOCK_STRUCTURE_BLOCK:
        // @TODO
        return 0;
    case BLOCK_JIGSAW:
        // @TODO
        return 0;
    case BLOCK_COMPOSTER:
        // @TODO
        return 0;
    case BLOCK_BEE_NEST:
    case BLOCK_BEEHIVE:
        // @TODO
        return 0;
    case BLOCK_RESPAWN_ANCHOR:
        // @TODO
        return 0;
    case BLOCK_CANDLE:
    case BLOCK_WHITE_CANDLE:
    case BLOCK_ORANGE_CANDLE:
    case BLOCK_MAGENTA_CANDLE:
    case BLOCK_LIGHT_BLUE_CANDLE:
    case BLOCK_YELLOW_CANDLE:
    case BLOCK_LIME_CANDLE:
    case BLOCK_PINK_CANDLE:
    case BLOCK_GRAY_CANDLE:
    case BLOCK_LIGHT_GRAY_CANDLE:
    case BLOCK_CYAN_CANDLE:
    case BLOCK_PURPLE_CANDLE:
    case BLOCK_BLUE_CANDLE:
    case BLOCK_BROWN_CANDLE:
    case BLOCK_GREEN_CANDLE:
    case BLOCK_RED_CANDLE:
    case BLOCK_BLACK_CANDLE: {
        item_stack * main = player->player.slots + player->player.selected_slot;
        item_stack * off = player->player.slots + PLAYER_OFF_HAND_SLOT;
        item_stack * used = hand == PLAYER_MAIN_HAND ? main : off;

        if ((player->flags & PLAYER_CAN_BUILD) && used->size == 0 && cur_info.lit) {
            cur_info.lit = 0;
            u16 new_state = make_block_state(&cur_info);
            try_set_block_state(clicked_pos, new_state);
            push_direct_neighbour_block_updates(clicked_pos, buc);
            // @TODO(traks) particles, sounds, game events
            return 1;
        }
        return 0;
    }
    case BLOCK_CANDLE_CAKE:
    case BLOCK_WHITE_CANDLE_CAKE:
    case BLOCK_ORANGE_CANDLE_CAKE:
    case BLOCK_MAGENTA_CANDLE_CAKE:
    case BLOCK_LIGHT_BLUE_CANDLE_CAKE:
    case BLOCK_YELLOW_CANDLE_CAKE:
    case BLOCK_LIME_CANDLE_CAKE:
    case BLOCK_PINK_CANDLE_CAKE:
    case BLOCK_GRAY_CANDLE_CAKE:
    case BLOCK_LIGHT_GRAY_CANDLE_CAKE:
    case BLOCK_CYAN_CANDLE_CAKE:
    case BLOCK_PURPLE_CANDLE_CAKE:
    case BLOCK_BLUE_CANDLE_CAKE:
    case BLOCK_BROWN_CANDLE_CAKE:
    case BLOCK_GREEN_CANDLE_CAKE:
    case BLOCK_RED_CANDLE_CAKE:
    case BLOCK_BLACK_CANDLE_CAKE: {
        // @TODO
        return 0;
    }
    case BLOCK_CAVE_VINES:
    case BLOCK_CAVE_VINES_PLANT: {
        // @TODO
        return 0;
    }
    default:
        // other blocks have no use action
        return 0;
    }
}

static void
register_block_property(int id, char * name, int value_count, char * * values) {
    block_property_spec prop_spec = {0};
    prop_spec.value_count = value_count;

    int name_size = strlen(name);

    unsigned char * tape = prop_spec.tape;
    *tape = name_size;
    tape++;
    memcpy(tape, name, name_size);
    tape += name_size;

    for (int i = 0; i < value_count; i++) {
        int value_size = strlen(values[i]);
        *tape = value_size;
        tape++;
        memcpy(tape, values[i], value_size);
        tape += value_size;
    }

    serv->block_property_specs[id] = prop_spec;
}

static void
register_property_v(int id, char * name, int value_count, ...) {
    va_list ap;
    va_start(ap, value_count);

    char * values[value_count];
    for (int i = 0; i < value_count; i++) {
        values[i] = va_arg(ap, char *);
    }

    va_end(ap);

    register_block_property(id, name, value_count, values);
}

static void
register_range_property(int id, char * name, int min, int max) {
    int value_count = max - min + 1;
    char * values[value_count];
    char buf[256];
    char * cursor = buf;
    char * end = buf + sizeof buf;

    for (int i = min; i <= max; i++) {
        int size = snprintf(cursor, end - cursor, "%d", i);
        values[i - min] = cursor;
        cursor += size;
        cursor++; // terminating null character
    }

    register_block_property(id, name, value_count, values);
}

static void
register_bool_property(int id, char * name) {
    register_property_v(id, name, 2, "true", "false");
}

static void
add_block_property(block_properties * props, int id, char * default_value) {
    // figure out index of default value
    block_property_spec * spec = serv->block_property_specs + id;
    int default_value_index = spec->value_count;
    int tape_index = 1 + spec->tape[0];

    String default_string = STR(default_value);

    for (int i = 0; i < spec->value_count; i++) {
        String value = {
            .size = spec->tape[tape_index],
            .data = spec->tape + tape_index + 1
        };
        if (net_string_equal(default_string, value)) {
            default_value_index = i;
            break;
        }

        tape_index += value.size + 1;
    }
    assert(default_value_index < spec->value_count);

    int prop_index = props->property_count;
    props->property_specs[prop_index] = id;
    props->default_value_indices[prop_index] = default_value_index;

    props->property_count++;
}

static int
count_block_states(block_properties * props) {
    int block_states = 1;
    for (int i = 0; i < props->property_count; i++) {
        block_states *= serv->block_property_specs[props->property_specs[i]].value_count;
    }
    return block_states;
}

static void
finalise_block_props(block_properties * props) {
    props->base_state = serv->actual_block_state_count;

    i32 block_type = props - serv->block_properties_table;
    int block_states = count_block_states(props);

    assert(serv->actual_block_state_count + block_states <= ARRAY_SIZE(serv->block_type_by_state));

    for (int i = 0; i < block_states; i++) {
        serv->block_type_by_state[serv->actual_block_state_count + i] = block_type;
    }

    serv->actual_block_state_count += block_states;
}

static i32
register_block_type(char * resource_loc) {
    String key = STR(resource_loc);
    resource_loc_table * table = &serv->block_resource_table;
    i32 block_type = serv->block_type_count;
    serv->block_type_count++;
    register_resource_loc(key, block_type, table);
    assert(net_string_equal(key, get_resource_loc(block_type, table)));
    assert(block_type == resolve_resource_loc_id(key, table));
    return block_type;
}

static void
set_collision_model_for_all_states(block_properties * props,
        int collision_model) {
    int block_states = count_block_states(props);
    for (int i = 0; i < block_states; i++) {
        int j = props->base_state + i;
        serv->collision_model_by_state[j] = collision_model;
    }
}

static void
init_simple_block(char * resource_loc, int collision_model) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    finalise_block_props(props);
    set_collision_model_for_all_states(props, collision_model);
}

static void
init_sapling(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_STAGE, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);
}

static void
init_pillar(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AXIS, "y");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);
}

static void
init_leaves(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_DISTANCE, "7");
    add_block_property(props, BLOCK_PROPERTY_PERSISTENT, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);
}

static void
init_bed(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_OCCUPIED, "false");
    add_block_property(props, BLOCK_PROPERTY_BED_PART, "foot");
    finalise_block_props(props);

    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int facing = info.horizontal_facing;
        if (info.bed_part == BED_PART_HEAD) {
            facing = get_opposite_direction(facing);
        }
        int model_index;
        switch (info.horizontal_facing) {
        case DIRECTION_POS_X: model_index = BLOCK_MODEL_BED_FOOT_POS_X; break;
        case DIRECTION_POS_Z: model_index = BLOCK_MODEL_BED_FOOT_POS_Z; break;
        case DIRECTION_NEG_X: model_index = BLOCK_MODEL_BED_FOOT_NEG_X; break;
        case DIRECTION_NEG_Z: model_index = BLOCK_MODEL_BED_FOOT_NEG_Z; break;
        }
        serv->collision_model_by_state[block_state] = model_index;
    }
}

static void
init_slab(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_SLAB_TYPE, "bottom");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int model_index;
        switch (info.slab_type) {
        case SLAB_TOP: model_index = BLOCK_MODEL_TOP_SLAB; break;
        case SLAB_BOTTOM: model_index = BLOCK_MODEL_Y_8; break;
        case SLAB_DOUBLE: model_index = BLOCK_MODEL_FULL; break;
        }
        serv->collision_model_by_state[block_state] = model_index;
    }
}

static void
init_sign(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ROTATION_16, "0");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);
}

static void
init_wall_sign(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);
}

static void
init_stair_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_HALF, "bottom");
    add_block_property(props, BLOCK_PROPERTY_STAIRS_SHAPE, "straight");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
}

static void
init_tall_plant(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);
}

static void
init_glazed_terracotta(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);
}

static void
init_shulker_box_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "up");
    finalise_block_props(props);
}

static void
init_wall_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WALL_POS_X, "none");
    add_block_property(props, BLOCK_PROPERTY_WALL_NEG_Z, "none");
    add_block_property(props, BLOCK_PROPERTY_WALL_POS_Z, "none");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "true");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_WALL_NEG_X, "none");
    finalise_block_props(props);
}

static void
init_pressure_plate(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);
}

static void
init_pane(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    finalise_block_props(props);
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int flags = (info.pos_x << 3) | (info.neg_x << 2) | (info.pos_z << 1) | info.neg_z;
        int model_index = BLOCK_MODEL_PANE_CENTRE + flags;
        serv->collision_model_by_state[block_state] = model_index;
    }
}

static void
init_fence(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    finalise_block_props(props);
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int flags = (info.pos_x << 3) | (info.neg_x << 2) | (info.pos_z << 1) | info.neg_z;
        int model_index = BLOCK_MODEL_FENCE_CENTRE + flags;
        serv->collision_model_by_state[block_state] = model_index;
    }
}

static void
init_door_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");
    add_block_property(props, BLOCK_PROPERTY_DOOR_HINGE, "left");
    add_block_property(props, BLOCK_PROPERTY_OPEN, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
}

static void
init_button(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ATTACH_FACE, "wall");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);
}

static void
init_trapdoor_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_HALF, "bottom");
    add_block_property(props, BLOCK_PROPERTY_OPEN, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
}

static void
init_fence_gate(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_IN_WALL, "false");
    add_block_property(props, BLOCK_PROPERTY_OPEN, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int model_index;
        if (info.open) {
            model_index = BLOCK_MODEL_EMPTY;
        } else if (get_direction_axis(info.horizontal_facing) == AXIS_X) {
            model_index = BLOCK_MODEL_FENCE_GATE_FACING_X;
        } else {
            model_index = BLOCK_MODEL_FENCE_GATE_FACING_Z;
        }
        serv->collision_model_by_state[block_state] = model_index;
    }
}

static void
init_mushroom_block(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_NEG_Y, "true");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "true");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "true");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "true");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "true");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "true");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);
}

static void
init_skull_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ROTATION_16, "0");
    finalise_block_props(props);
}

static void
init_wall_skull_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
}

static void
init_anvil_props(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
}

static void
init_banner(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ROTATION_16, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);
}

static void
init_wall_banner(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);
}

static void
init_coral(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);
}

static void
init_coral_fan(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);
}

static void
init_coral_wall_fan(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);
}

static void
init_snowy_grassy_block(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_SNOWY, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);
}

static void
init_redstone_ore(char * resource_loc) {
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);
}

static void
init_cauldron(char * resource_loc, int layered) {
    // @TODO(traks) collision models
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    if (layered) {
        add_block_property(props, BLOCK_PROPERTY_LEVEL_CAULDRON, "1");
    }
    finalise_block_props(props);
}

static void
init_candle(char * resource_loc) {
    // @TODO(traks) collision models
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_CANDLES, "1");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
}

static void
init_candle_cake(char * resource_loc) {
    // @TODO(traks) collision models
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    finalise_block_props(props);
}

static void
init_amethyst_cluster(char * resource_loc) {
    // @TODO(traks) collision models
    i32 block_type = register_block_type(resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "up");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
}

typedef struct {
    float min_a;
    float min_b;
    float max_a;
    float max_b;
} block_box_face;

typedef struct {
    float min_a;
    float min_b;
    float max_a;
    float max_b;
    float axis_min;
    float axis_max;
    float axis_cut;
} intersect_test;

static int
block_boxes_contain_face(int box_count, block_box * boxes,
        block_box slice, int direction) {
    // intersect boxes with 1x1x1 cube and get the faces
    int face_count = 0;
    block_box_face faces[box_count];
    // @NOTE(traks) All block models are currently aligned to the pixel grid, so
    // the epsilon can be fairly large. Bamboo is aligned to a quarter of a
    // pixel grid, but we currently don't use this algorithm for bamboo.
    float eps = 0.001;

    for (int i = 0; i < box_count; i++) {
        block_box box = boxes[i];
        intersect_test test;

        switch (direction) {
        case DIRECTION_NEG_Y: test = (intersect_test) {box.min_x, box.min_z, box.max_x, box.max_z, box.min_y, box.max_y, slice.min_y}; break;
        case DIRECTION_POS_Y: test = (intersect_test) {box.min_x, box.min_z, box.max_x, box.max_z, box.min_y, box.max_y, slice.max_y}; break;
        case DIRECTION_NEG_Z: test = (intersect_test) {box.min_x, box.min_y, box.max_x, box.max_y, box.min_z, box.max_z, slice.min_z}; break;
        case DIRECTION_POS_Z: test = (intersect_test) {box.min_x, box.min_y, box.max_x, box.max_y, box.min_z, box.max_z, slice.max_z}; break;
        case DIRECTION_NEG_X: test = (intersect_test) {box.min_y, box.min_z, box.max_y, box.max_z, box.min_x, box.max_x, slice.min_x}; break;
        case DIRECTION_POS_X: test = (intersect_test) {box.min_y, box.min_z, box.max_y, box.max_z, box.min_x, box.max_x, slice.max_x}; break;
        }

        if (test.axis_min <= test.axis_cut + eps && test.axis_cut <= test.axis_max + eps) {
            faces[face_count] = (block_box_face) {test.min_a, test.min_b, test.max_a, test.max_b};
            face_count++;
        }
    }

    block_box_face test;
    switch (direction) {
    case DIRECTION_NEG_Y: test = (block_box_face) {slice.min_x, slice.min_z, slice.max_x, slice.max_z}; break;
    case DIRECTION_POS_Y: test = (block_box_face) {slice.min_x, slice.min_z, slice.max_x, slice.max_z}; break;
    case DIRECTION_NEG_Z: test = (block_box_face) {slice.min_x, slice.min_y, slice.max_x, slice.max_y}; break;
    case DIRECTION_POS_Z: test = (block_box_face) {slice.min_x, slice.min_y, slice.max_x, slice.max_y}; break;
    case DIRECTION_NEG_X: test = (block_box_face) {slice.min_y, slice.min_z, slice.max_y, slice.max_z}; break;
    case DIRECTION_POS_X: test = (block_box_face) {slice.min_y, slice.min_z, slice.max_y, slice.max_z}; break;
    }

    // start at minimum a and b. First we try to move our best_b to the
    // maximum b by checking whether faces contain the coordinate
    // (best_a, best_b). After that we can move best_a forward by a bit. We then
    // reset best_b and repeat the whole process, until best_a doesn't move
    // anymore or hits the maximum a.
    //
    // Currently our algorithm assumes that each of the faces has positive area!
    // So no lines as input please. This is to ensure progress is made in the
    // loops below.

    float best_a = test.min_a;
    float best_b = test.min_b;

    for (int loop = 0; loop < 1000; loop++) {
        float old_best_a = best_a;

        // first try to move best_b forward
        float big_number = 1000;
        float min_found_a = big_number;

        for (;;) {
            float old_best_b = best_b;

            for (int i = 0; i < face_count; i++) {
                block_box_face * face = faces + i;

                if (face->min_a <= best_a + eps && best_a <= face->max_a + eps
                        && face->min_b <= best_b + eps && best_b <= face->max_b + eps) {
                    // face contains our coordinate, so move best_b forward
                    best_b = face->max_b;
                    min_found_a = MIN(face->max_a, min_found_a);
                }
            }

            if (old_best_b == best_b) {
                // best b didn't change, so test face not contained inside our
                // list of faces
                return 0;
            }

            if (best_b + eps >= test.max_b) {
                // reached maximum b, so move best_a forward and reset best_b
                best_b = test.min_b;
                best_a = min_found_a;
                break;
            }
        }

        if (best_a + eps >= test.max_a) {
            // reached maximum a, so done!
            return 1;
        }

        if (old_best_a == best_a) {
            // best_a didn't change, so test face not contained inside our
            // list of faces
            return 0;
        }
    }

    // maximum number of loops reached, very bad!
    assert(0);
    return 0;
}

static int
block_boxes_intersect_face(int box_count, block_box * boxes,
        block_box slice, int direction) {
    switch (direction) {
    case DIRECTION_NEG_Y: slice.max_y = slice.min_y; break;
    case DIRECTION_POS_Y: slice.min_y = slice.max_y; break;
    case DIRECTION_NEG_Z: slice.max_z = slice.min_z; break;
    case DIRECTION_POS_Z: slice.min_z = slice.max_z; break;
    case DIRECTION_NEG_X: slice.max_x = slice.min_x; break;
    case DIRECTION_POS_X: slice.min_x = slice.max_x; break;
    }

    // check if the selected face intersects any of the boxes
    for (int i = 0; i < box_count; i++) {
        block_box * box = boxes + i;
        if (box->min_x <= slice.max_x && box->max_x >= slice.min_x
                && box->min_y <= slice.max_y && box->max_y >= slice.min_y
                && box->min_z <= slice.max_z && box->max_z >= slice.min_z) {
            return 1;
        }
    }
    return 0;
}

static void
register_block_model(int index, int box_count, block_box * pixel_boxes) {
    block_box boxes[8];
    assert(box_count < ARRAY_SIZE(boxes));
    for (int i = 0; i < box_count; i++) {
        boxes[i] = pixel_boxes[i];
        boxes[i].min_x /= 16;
        boxes[i].min_y /= 16;
        boxes[i].min_z /= 16;
        boxes[i].max_x /= 16;
        boxes[i].max_y /= 16;
        boxes[i].max_z /= 16;
    }

    block_model * model = serv->block_models + index;
    model->box_count = box_count;
    for (int i = 0; i < box_count; i++) {
        model->boxes[i] = boxes[i];
    }
    if (index == BLOCK_MODEL_FULL) {
        model->flags |= BLOCK_MODEL_IS_FULL;
    }

    // compute support model
    support_model * support = serv->support_models + index;
    for (int dir = 0; dir < 6; dir++) {
        block_box full_box = {0, 0, 0, 1, 1, 1};
        if (block_boxes_contain_face(box_count, boxes, full_box, dir)) {
            support->full_face_flags |= 1 << dir;
        }

        block_box pole = {7.0f / 16, 0, 7.0f / 16, 9.0f / 16, 1, 9.0f / 16};
        if (block_boxes_contain_face(box_count, boxes, pole, dir)) {
            support->pole_face_flags |= 1 << dir;
        }

        if (block_boxes_intersect_face(box_count, boxes, full_box, dir)) {
            support->non_empty_face_flags |= 1 << dir;
        }
    }
}

// @NOTE(traks) all thes rotation functions assume the coordinates of the box
// are in pixel coordinates

static block_box
rotate_block_box_clockwise(block_box box) {
    block_box res = {16 - box.max_z , box.min_y, box.min_x, 16 - box.min_z, box.max_y, box.max_x};
    return res;
}

static block_box
rotate_block_box_180(block_box box) {
    return rotate_block_box_clockwise(rotate_block_box_clockwise(box));
}

static block_box
rotate_block_box_counter_clockwise(block_box box) {
    return rotate_block_box_180(rotate_block_box_clockwise(box));
}

static void
register_cross_block_models(int start_index, block_box centre_box,
        block_box neg_z_box, block_box z_box) {
    for (int i = 0; i < 16; i++) {
        int neg_z = (i & 0x1);
        int pos_z = (i & 0x2);
        int neg_x = (i & 0x4);
        int pos_x = (i & 0x8);

        int box_count = 0;
        block_box boxes[2];

        if (neg_z && pos_z) {
            boxes[box_count] = z_box;
            box_count++;
        } else if (neg_z) {
            boxes[box_count] = neg_z_box;
            box_count++;
        } else if (pos_z) {
            boxes[box_count] = rotate_block_box_180(neg_z_box);
            box_count++;
        }

        if (neg_x && pos_x) {
            boxes[box_count] = rotate_block_box_clockwise(z_box);
            box_count++;
        } else if (neg_x) {
            boxes[box_count] = rotate_block_box_counter_clockwise(neg_z_box);
            box_count++;
        } else if (pos_x) {
            boxes[box_count] = rotate_block_box_clockwise(neg_z_box);
            box_count++;
        }

        if (box_count == 0) {
            // not connected to any edges
            boxes[box_count] = centre_box;
            box_count++;
        }

        register_block_model(start_index + i, box_count, boxes);
    }
}

void
init_block_data(void) {
    register_block_model(BLOCK_MODEL_EMPTY, 0, NULL);
    // @NOTE(traks) this initialises the full block model
    for (int y = 1; y <= 16; y++) {
        block_box box = {0, 0, 0, 16, y, 16};
        register_block_model(BLOCK_MODEL_EMPTY + y, 1, &box);
    }
    block_box flower_pot_box = {5, 0, 5, 11, 6, 11};
    register_block_model(BLOCK_MODEL_FLOWER_POT, 1, &flower_pot_box);
    block_box cactus_box = {1, 0, 1, 15, 15, 15};
    register_block_model(BLOCK_MODEL_CACTUS, 1, &cactus_box);
    block_box composter_boxes[] = {
        {0, 0, 0, 16, 2, 16}, // bottom
        {0, 0, 0, 2, 16, 16}, // wall neg x
        {0, 0, 0, 16, 16, 2}, // wall neg z
        {14, 0, 0, 16, 16, 16}, // wall pos x
        {0, 0, 14, 16, 16, 16}, // wall pos z
    };
    register_block_model(BLOCK_MODEL_COMPOSTER, ARRAY_SIZE(composter_boxes), composter_boxes);
    block_box honey_box = {1, 0, 1, 15, 15, 15};
    register_block_model(BLOCK_MODEL_HONEY_BLOCK, 1, &honey_box);
    block_box fence_gate_facing_x_box = {6, 0, 0, 10, 24, 16};
    block_box fence_gate_facing_z_box = {0, 0, 6, 16, 24, 10};
    register_block_model(BLOCK_MODEL_FENCE_GATE_FACING_X, 1, &fence_gate_facing_x_box);
    register_block_model(BLOCK_MODEL_FENCE_GATE_FACING_Z, 1, &fence_gate_facing_z_box);
    block_box centred_bamboo_box = {6.5f, 0, 6.5f, 9.5f, 16, 9.5f};
    register_block_model(BLOCK_MODEL_CENTRED_BAMBOO, 1, &centred_bamboo_box);

    block_box pane_centre_box = {7, 0, 7, 9, 16, 9};
    block_box pane_neg_z_box = {7, 0, 0, 9, 16, 9};
    block_box pane_z_box = {7, 0, 0, 9, 16, 16};
    register_cross_block_models(BLOCK_MODEL_PANE_CENTRE, pane_centre_box,
            pane_neg_z_box, pane_z_box);

    block_box fence_centre_box = {6, 0, 6, 10, 24, 10};
    block_box fence_neg_z_box = {6, 0, 0, 10, 24, 10};
    block_box fence_z_box = {6, 0, 0, 10, 24, 16};
    register_cross_block_models(BLOCK_MODEL_FENCE_CENTRE, fence_centre_box,
            fence_neg_z_box, fence_z_box);

    block_box boxes_foot_pos_x[] = {
        {0, 3, 0, 16, 9, 16}, // horizontal part
        {0, 0, 0, 3, 3, 3}, // leg 1
        {0, 0, 13, 3, 3, 16}, // leg 2
    };
    for (int i = 0; i < 4; i++) {
        int model_index = BLOCK_MODEL_BED_FOOT_POS_X + i;
        register_block_model(model_index, ARRAY_SIZE(boxes_foot_pos_x), boxes_foot_pos_x);
        for (int j = 0; j < ARRAY_SIZE(boxes_foot_pos_x); j++) {
            boxes_foot_pos_x[i] = rotate_block_box_clockwise(boxes_foot_pos_x[i]);
        }
    }

    block_box lectern_boxes[] = {
        {0, 0, 0, 16, 2, 16}, // base
        {4, 2, 4, 12, 14, 12}, // post
    };
    register_block_model(BLOCK_MODEL_LECTERN, ARRAY_SIZE(lectern_boxes), lectern_boxes);
    block_box slab_top_box = {0, 8, 0, 16, 16, 16};
    register_block_model(BLOCK_MODEL_TOP_SLAB, 1, &slab_top_box);
    block_box lily_pad_box = {1, 0, 1, 15, 1.5f, 15};
    register_block_model(BLOCK_MODEL_LILY_PAD, 1, &lily_pad_box);

    register_bool_property(BLOCK_PROPERTY_ATTACHED, "attached");
    register_bool_property(BLOCK_PROPERTY_BOTTOM, "bottom");
    register_bool_property(BLOCK_PROPERTY_CONDITIONAL, "conditional");
    register_bool_property(BLOCK_PROPERTY_DISARMED, "disarmed");
    register_bool_property(BLOCK_PROPERTY_DRAG, "drag");
    register_bool_property(BLOCK_PROPERTY_ENABLED, "enabled");
    register_bool_property(BLOCK_PROPERTY_EXTENDED, "extended");
    register_bool_property(BLOCK_PROPERTY_EYE, "eye");
    register_bool_property(BLOCK_PROPERTY_FALLING, "falling");
    register_bool_property(BLOCK_PROPERTY_HANGING, "hanging");
    register_bool_property(BLOCK_PROPERTY_HAS_BOTTLE_0, "has_bottle_0");
    register_bool_property(BLOCK_PROPERTY_HAS_BOTTLE_1, "has_bottle_1");
    register_bool_property(BLOCK_PROPERTY_HAS_BOTTLE_2, "has_bottle_2");
    register_bool_property(BLOCK_PROPERTY_HAS_RECORD, "has_record");
    register_bool_property(BLOCK_PROPERTY_HAS_BOOK, "has_book");
    register_bool_property(BLOCK_PROPERTY_INVERTED, "inverted");
    register_bool_property(BLOCK_PROPERTY_IN_WALL, "in_wall");
    register_bool_property(BLOCK_PROPERTY_LIT, "lit");
    register_bool_property(BLOCK_PROPERTY_LOCKED, "locked");
    register_bool_property(BLOCK_PROPERTY_OCCUPIED, "occupied");
    register_bool_property(BLOCK_PROPERTY_OPEN, "open");
    register_bool_property(BLOCK_PROPERTY_PERSISTENT, "persistent");
    register_bool_property(BLOCK_PROPERTY_POWERED, "powered");
    register_bool_property(BLOCK_PROPERTY_SHORT_PISTON, "short");
    register_bool_property(BLOCK_PROPERTY_SIGNAL_FIRE, "signal_fire");
    register_bool_property(BLOCK_PROPERTY_SNOWY, "snowy");
    register_bool_property(BLOCK_PROPERTY_TRIGGERED, "triggered");
    register_bool_property(BLOCK_PROPERTY_UNSTABLE, "unstable");
    register_bool_property(BLOCK_PROPERTY_WATERLOGGED, "waterlogged");
    register_bool_property(BLOCK_PROPERTY_VINE_END, "vine_end");
    register_bool_property(BLOCK_PROPERTY_BERRIES, "berries");
    register_property_v(BLOCK_PROPERTY_HORIZONTAL_AXIS, "axis", 2, "x", "z");
    register_property_v(BLOCK_PROPERTY_AXIS, "axis", 3, "x", "y", "z");
    register_bool_property(BLOCK_PROPERTY_POS_Y, "up");
    register_bool_property(BLOCK_PROPERTY_NEG_Y, "down");
    register_bool_property(BLOCK_PROPERTY_NEG_Z, "north");
    register_bool_property(BLOCK_PROPERTY_POS_X, "east");
    register_bool_property(BLOCK_PROPERTY_POS_Z, "south");
    register_bool_property(BLOCK_PROPERTY_NEG_X, "west");
    register_property_v(BLOCK_PROPERTY_FACING, "facing", 6, "north", "east", "south", "west", "up", "down");
    register_property_v(BLOCK_PROPERTY_FACING_HOPPER, "facing", 5, "down", "north", "south", "west", "east");
    register_property_v(BLOCK_PROPERTY_HORIZONTAL_FACING, "facing", 4, "north", "south", "west", "east");
    register_property_v(BLOCK_PROPERTY_JIGSAW_ORIENTATION, "orientation", 12, "down_east", "down_north", "down_south", "down_west", "up_east", "up_north", "up_south", "up_west", "west_up", "east_up", "north_up", "south_up");
    register_property_v(BLOCK_PROPERTY_ATTACH_FACE, "face", 3, "floor", "wall", "ceiling");
    register_property_v(BLOCK_PROPERTY_BELL_ATTACHMENT, "attachment", 4, "floor", "ceiling", "single_wall", "double_wall");
    register_property_v(BLOCK_PROPERTY_WALL_POS_X, "east", 3, "none", "low", "tall");
    register_property_v(BLOCK_PROPERTY_WALL_NEG_Z, "north", 3, "none", "low", "tall");
    register_property_v(BLOCK_PROPERTY_WALL_POS_Z, "south", 3, "none", "low", "tall");
    register_property_v(BLOCK_PROPERTY_WALL_NEG_X, "west", 3, "none", "low", "tall");
    register_property_v(BLOCK_PROPERTY_REDSTONE_POS_X, "east", 3, "up", "side", "none");
    register_property_v(BLOCK_PROPERTY_REDSTONE_NEG_Z, "north", 3, "up", "side", "none");
    register_property_v(BLOCK_PROPERTY_REDSTONE_POS_Z, "south", 3, "up", "side", "none");
    register_property_v(BLOCK_PROPERTY_REDSTONE_NEG_X, "west", 3, "up", "side", "none");
    register_property_v(BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "half", 2, "upper", "lower");
    register_property_v(BLOCK_PROPERTY_HALF, "half", 2, "top", "bottom");
    register_property_v(BLOCK_PROPERTY_RAIL_SHAPE, "shape", 10, "north_south", "east_west", "ascending_east", "ascending_west", "ascending_north", "ascending_south", "south_east", "south_west", "north_west", "north_east");
    register_property_v(BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "shape", 6, "north_south", "east_west", "ascending_east", "ascending_west", "ascending_north", "ascending_south");
    register_range_property(BLOCK_PROPERTY_AGE_1, "age", 0, 1);
    register_range_property(BLOCK_PROPERTY_AGE_2, "age", 0, 2);
    register_range_property(BLOCK_PROPERTY_AGE_3, "age", 0, 3);
    register_range_property(BLOCK_PROPERTY_AGE_5, "age", 0, 5);
    register_range_property(BLOCK_PROPERTY_AGE_7, "age", 0, 7);
    register_range_property(BLOCK_PROPERTY_AGE_15, "age", 0, 15);
    register_range_property(BLOCK_PROPERTY_AGE_25, "age", 0, 25);
    register_range_property(BLOCK_PROPERTY_BITES, "bites", 0, 6);
    register_range_property(BLOCK_PROPERTY_CANDLES, "candles", 1, 4);
    register_range_property(BLOCK_PROPERTY_DELAY, "delay", 1, 4);
    register_range_property(BLOCK_PROPERTY_DISTANCE, "distance", 1, 7);
    register_range_property(BLOCK_PROPERTY_EGGS, "eggs", 1, 4);
    register_range_property(BLOCK_PROPERTY_HATCH, "hatch", 0, 2);
    register_range_property(BLOCK_PROPERTY_LAYERS, "layers", 1, 8);
    register_range_property(BLOCK_PROPERTY_LEVEL_CAULDRON, "level", 1, 3);
    register_range_property(BLOCK_PROPERTY_LEVEL_COMPOSTER, "level", 0, 8);
    register_range_property(BLOCK_PROPERTY_LEVEL_HONEY, "honey_level", 0, 5);
    register_range_property(BLOCK_PROPERTY_LEVEL, "level", 0, 15);
    register_range_property(BLOCK_PROPERTY_LEVEL_LIGHT, "level", 0, 15);
    register_range_property(BLOCK_PROPERTY_MOISTURE, "moisture", 0, 7);
    register_range_property(BLOCK_PROPERTY_NOTE, "note", 0, 24);
    register_range_property(BLOCK_PROPERTY_PICKLES, "pickles", 1, 4);
    register_range_property(BLOCK_PROPERTY_POWER, "power", 0, 15);
    register_range_property(BLOCK_PROPERTY_STAGE, "stage", 0, 1);
    register_range_property(BLOCK_PROPERTY_STABILITY_DISTANCE, "distance", 0, 7);
    register_range_property(BLOCK_PROPERTY_RESPAWN_ANCHOR_CHARGES, "charges", 0, 4);
    register_range_property(BLOCK_PROPERTY_ROTATION_16, "rotation", 0, 15);
    register_property_v(BLOCK_PROPERTY_BED_PART, "part", 2, "head", "foot");
    register_property_v(BLOCK_PROPERTY_CHEST_TYPE, "type", 3, "single", "left", "right");
    register_property_v(BLOCK_PROPERTY_MODE_COMPARATOR, "mode", 2, "compare", "subtract");
    register_property_v(BLOCK_PROPERTY_DOOR_HINGE, "hinge", 2, "left", "right");
    register_property_v(BLOCK_PROPERTY_NOTEBLOCK_INSTRUMENT, "instrument", 16, "harp", "basedrum", "snare", "hat", "bass", "flute", "bell", "guitar", "chime", "xylophone", "iron_xylophone", "cow_bell", "didgeridoo", "bit", "banjo", "pling");
    register_property_v(BLOCK_PROPERTY_PISTON_TYPE, "type", 2, "normal", "sticky");
    register_property_v(BLOCK_PROPERTY_SLAB_TYPE, "type", 3, "top", "bottom", "double");
    register_property_v(BLOCK_PROPERTY_STAIRS_SHAPE, "shape", 5, "straight", "inner_left", "inner_right", "outer_left", "outer_right");
    register_property_v(BLOCK_PROPERTY_STRUCTUREBLOCK_MODE, "mode", 4, "save", "load", "corner", "data");
    register_property_v(BLOCK_PROPERTY_BAMBOO_LEAVES, "leaves", 3, "none", "small", "large");
    register_property_v(BLOCK_PROPERTY_DRIPLEAF_TILT, "tilt", 4, "none", "unstable", "partial", "full");
    register_property_v(BLOCK_PROPERTY_VERTICAL_DIRECTION, "vertical_direction", 2, "up", "down");
    register_property_v(BLOCK_PROPERTY_DRIPSTONE_THICKNESS, "thickness", 5, "tip_merge", "tip", "frustum", "middle", "base");
    register_property_v(BLOCK_PROPERTY_SCULK_SENSOR_PHASE, "sculk_sensor_phase", 3, "inactive", "active", "cooldown");

    block_properties * props;
    i32 block_type;

    init_simple_block("minecraft:air", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:stone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:granite", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:polished_granite", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:diorite", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:polished_diorite", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:andesite", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:polished_andesite", BLOCK_MODEL_FULL);
    init_snowy_grassy_block("minecraft:grass_block");
    init_simple_block("minecraft:dirt", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:coarse_dirt", BLOCK_MODEL_FULL);
    init_snowy_grassy_block("minecraft:podzol");
    init_simple_block("minecraft:cobblestone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:oak_planks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:spruce_planks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:birch_planks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:jungle_planks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:acacia_planks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:dark_oak_planks", BLOCK_MODEL_FULL);
    init_sapling("minecraft:oak_sapling");
    init_sapling("minecraft:spruce_sapling");
    init_sapling("minecraft:birch_sapling");
    init_sapling("minecraft:jungle_sapling");
    init_sapling("minecraft:acacia_sapling");
    init_sapling("minecraft:dark_oak_sapling");
    init_simple_block("minecraft:bedrock", BLOCK_MODEL_FULL);

    // @TODO(traks) slower movement in fluids
    block_type = register_block_type("minecraft:water");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LEVEL, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    // @TODO(traks) slower movement in fluids
    block_type = register_block_type("minecraft:lava");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LEVEL, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_simple_block("minecraft:sand", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:red_sand", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:gravel", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:gold_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:deepslate_gold_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:iron_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:deepslate_iron_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:coal_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:deepslate_coal_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:nether_gold_ore", BLOCK_MODEL_FULL);
    init_pillar("minecraft:oak_log");
    init_pillar("minecraft:spruce_log");
    init_pillar("minecraft:birch_log");
    init_pillar("minecraft:jungle_log");
    init_pillar("minecraft:acacia_log");
    init_pillar("minecraft:dark_oak_log");
    init_pillar("minecraft:stripped_spruce_log");
    init_pillar("minecraft:stripped_birch_log");
    init_pillar("minecraft:stripped_jungle_log");
    init_pillar("minecraft:stripped_acacia_log");
    init_pillar("minecraft:stripped_dark_oak_log");
    init_pillar("minecraft:stripped_oak_log");
    init_pillar("minecraft:oak_wood");
    init_pillar("minecraft:spruce_wood");
    init_pillar("minecraft:birch_wood");
    init_pillar("minecraft:jungle_wood");
    init_pillar("minecraft:acacia_wood");
    init_pillar("minecraft:dark_oak_wood");
    init_pillar("minecraft:stripped_oak_wood");
    init_pillar("minecraft:stripped_spruce_wood");
    init_pillar("minecraft:stripped_birch_wood");
    init_pillar("minecraft:stripped_jungle_wood");
    init_pillar("minecraft:stripped_acacia_wood");
    init_pillar("minecraft:stripped_dark_oak_wood");
    init_leaves("minecraft:oak_leaves");
    init_leaves("minecraft:spruce_leaves");
    init_leaves("minecraft:birch_leaves");
    init_leaves("minecraft:jungle_leaves");
    init_leaves("minecraft:acacia_leaves");
    init_leaves("minecraft:dark_oak_leaves");
    init_leaves("minecraft:azalea_leaves");
    init_leaves("minecraft:flowering_azalea_leaves");
    init_simple_block("minecraft:sponge", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:wet_sponge", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:lapis_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:deepslate_lapis_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:lapis_block", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:dispenser");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_TRIGGERED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    init_simple_block("minecraft:sandstone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:chiseled_sandstone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cut_sandstone", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:note_block");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_NOTEBLOCK_INSTRUMENT, "harp");
    add_block_property(props, BLOCK_PROPERTY_NOTE, "0");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    init_bed("minecraft:white_bed");
    init_bed("minecraft:orange_bed");
    init_bed("minecraft:magenta_bed");
    init_bed("minecraft:light_blue_bed");
    init_bed("minecraft:yellow_bed");
    init_bed("minecraft:lime_bed");
    init_bed("minecraft:pink_bed");
    init_bed("minecraft:gray_bed");
    init_bed("minecraft:light_gray_bed");
    init_bed("minecraft:cyan_bed");
    init_bed("minecraft:purple_bed");
    init_bed("minecraft:blue_bed");
    init_bed("minecraft:brown_bed");
    init_bed("minecraft:green_bed");
    init_bed("minecraft:red_bed");
    init_bed("minecraft:black_bed");

    block_type = register_block_type("minecraft:powered_rail");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "north_south");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:detector_rail");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "north_south");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:sticky_piston");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_EXTENDED, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    finalise_block_props(props);

    // @TODO(traks) slow down entities in cobwebs
    init_simple_block("minecraft:cobweb", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:grass", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:fern", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:dead_bush", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:seagrass", BLOCK_MODEL_EMPTY);

    init_tall_plant("minecraft:tall_seagrass");

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:piston");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_EXTENDED, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    finalise_block_props(props);

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:piston_head");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_SHORT_PISTON, "false");
    add_block_property(props, BLOCK_PROPERTY_PISTON_TYPE, "normal");
    finalise_block_props(props);

    init_simple_block("minecraft:white_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:orange_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:magenta_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:light_blue_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:yellow_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:lime_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:pink_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:gray_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:light_gray_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cyan_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:purple_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:blue_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:brown_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:green_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:red_wool", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:black_wool", BLOCK_MODEL_FULL);

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:moving_piston");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_PISTON_TYPE, "normal");
    finalise_block_props(props);

    init_simple_block("minecraft:dandelion", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:poppy", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:blue_orchid", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:allium", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:azure_bluet", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:red_tulip", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:orange_tulip", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:white_tulip", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:pink_tulip", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:oxeye_daisy", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:cornflower", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:wither_rose", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:lily_of_the_valley", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:brown_mushroom", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:red_mushroom", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:gold_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:iron_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:bricks", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:tnt");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_UNSTABLE, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    init_simple_block("minecraft:bookshelf", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:mossy_cobblestone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:obsidian", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:torch", BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:wall_torch");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:fire");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_15, "0");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    // @TODO(traks) do damage in fire
    init_simple_block("minecraft:soul_fire", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:spawner", BLOCK_MODEL_FULL);

    // @TODO(traks) collision model
    init_stair_props("minecraft:oak_stairs");

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:chest");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_CHEST_TYPE, "single");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:redstone_wire");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_REDSTONE_POS_X, "none");
    add_block_property(props, BLOCK_PROPERTY_REDSTONE_NEG_Z, "none");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    add_block_property(props, BLOCK_PROPERTY_REDSTONE_POS_Z, "none");
    add_block_property(props, BLOCK_PROPERTY_REDSTONE_NEG_X, "none");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_simple_block("minecraft:diamond_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:deepslate_diamond_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:diamond_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:crafting_table", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:wheat");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:farmland");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_MOISTURE, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_Y_15);

    block_type = register_block_type("minecraft:furnace");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    init_sign("minecraft:oak_sign");
    init_sign("minecraft:spruce_sign");
    init_sign("minecraft:birch_sign");
    init_sign("minecraft:acacia_sign");
    init_sign("minecraft:jungle_sign");
    init_sign("minecraft:dark_oak_sign");

    // @TODO(traks) collision model
    init_door_props("minecraft:oak_door");

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:ladder");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:rail");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_RAIL_SHAPE, "north_south");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_stair_props("minecraft:cobblestone_stairs");

    init_wall_sign("minecraft:oak_wall_sign");
    init_wall_sign("minecraft:spruce_wall_sign");
    init_wall_sign("minecraft:birch_wall_sign");
    init_wall_sign("minecraft:acacia_wall_sign");
    init_wall_sign("minecraft:jungle_wall_sign");
    init_wall_sign("minecraft:dark_oak_wall_sign");

    block_type = register_block_type("minecraft:lever");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ATTACH_FACE, "wall");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_pressure_plate("minecraft:stone_pressure_plate");

    init_door_props("minecraft:iron_door");

    init_pressure_plate("minecraft:oak_pressure_plate");
    init_pressure_plate("minecraft:spruce_pressure_plate");
    init_pressure_plate("minecraft:birch_pressure_plate");
    init_pressure_plate("minecraft:jungle_pressure_plate");
    init_pressure_plate("minecraft:acacia_pressure_plate");
    init_pressure_plate("minecraft:dark_oak_pressure_plate");

    init_redstone_ore("minecraft:redstone_ore");
    init_redstone_ore("minecraft:deepslate_redstone_ore");

    block_type = register_block_type("minecraft:redstone_torch");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LIT, "true");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:redstone_wall_torch");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "true");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_button("minecraft:stone_button");

    block_type = register_block_type("minecraft:snow");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LAYERS, "1");
    finalise_block_props(props);
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int model_index = BLOCK_MODEL_EMPTY + (info.layers - 1) * 2;
        serv->collision_model_by_state[block_state] = model_index;
    }

    init_simple_block("minecraft:ice", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:snow_block", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:cactus");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_15, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_CACTUS);

    init_simple_block("minecraft:clay", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:sugar_cane");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_15, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:jukebox");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HAS_RECORD, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    init_fence("minecraft:oak_fence");

    init_simple_block("minecraft:pumpkin", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:netherrack", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:soul_sand", BLOCK_MODEL_Y_14);
    init_simple_block("minecraft:soul_soil", BLOCK_MODEL_FULL);
    init_pillar("minecraft:basalt");
    init_pillar("minecraft:polished_basalt");
    init_simple_block("minecraft:soul_torch", BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:soul_wall_torch");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_simple_block("minecraft:glowstone", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:nether_portal");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_AXIS, "x");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:carved_pumpkin");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:jack_o_lantern");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:cake");
    props = serv->block_properties_table + BLOCK_CAKE;
    add_block_property(props, BLOCK_PROPERTY_BITES, "0");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:repeater");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_DELAY, "1");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LOCKED, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_Y_2);

    init_simple_block("minecraft:white_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:orange_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:magenta_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:light_blue_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:yellow_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:lime_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:pink_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:gray_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:light_gray_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cyan_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:purple_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:blue_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:brown_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:green_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:red_stained_glass", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:black_stained_glass", BLOCK_MODEL_FULL);

    // @TODO(traks) collision models
    init_trapdoor_props("minecraft:oak_trapdoor");
    init_trapdoor_props("minecraft:spruce_trapdoor");
    init_trapdoor_props("minecraft:birch_trapdoor");
    init_trapdoor_props("minecraft:jungle_trapdoor");
    init_trapdoor_props("minecraft:acacia_trapdoor");
    init_trapdoor_props("minecraft:dark_oak_trapdoor");

    init_simple_block("minecraft:stone_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:mossy_stone_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cracked_stone_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:chiseled_stone_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:infested_stone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:infested_cobblestone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:infested_stone_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:infested_mossy_stone_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:infested_cracked_stone_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:infested_chiseled_stone_bricks", BLOCK_MODEL_FULL);
    init_mushroom_block("minecraft:brown_mushroom_block");
    init_mushroom_block("minecraft:red_mushroom_block");
    init_mushroom_block("minecraft:mushroom_stem");

    init_pane("minecraft:iron_bars");

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:chain");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AXIS, "y");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    init_pane("minecraft:glass_pane");

    init_simple_block("minecraft:melon", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:attached_pumpkin_stem");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:attached_melon_stem");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:pumpkin_stem");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:melon_stem");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:vine");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:glow_lichen");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_NEG_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_fence_gate("minecraft:oak_fence_gate");

    init_stair_props("minecraft:brick_stairs");
    init_stair_props("minecraft:stone_brick_stairs");

    init_snowy_grassy_block("minecraft:mycelium");

    init_simple_block("minecraft:lily_pad", BLOCK_MODEL_LILY_PAD);
    init_simple_block("minecraft:nether_bricks", BLOCK_MODEL_FULL);

    init_fence("minecraft:nether_brick_fence");

    init_stair_props("minecraft:nether_brick_stairs");

    block_type = register_block_type("minecraft:nether_wart");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_3, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_simple_block("minecraft:enchanting_table", BLOCK_MODEL_Y_12);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:brewing_stand");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HAS_BOTTLE_0, "false");
    add_block_property(props, BLOCK_PROPERTY_HAS_BOTTLE_1, "false");
    add_block_property(props, BLOCK_PROPERTY_HAS_BOTTLE_2, "false");
    finalise_block_props(props);

    init_cauldron("minecraft:cauldron", 0);
    init_cauldron("minecraft:water_cauldron", 1);
    init_cauldron("minecraft:lava_cauldron", 0);
    init_cauldron("minecraft:powder_snow_cauldron", 1);

    init_simple_block("minecraft:end_portal", BLOCK_MODEL_EMPTY);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:end_portal_frame");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_EYE, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);

    init_simple_block("minecraft:end_stone", BLOCK_MODEL_FULL);
    // @TODO(traks) correct block model
    init_simple_block("minecraft:dragon_egg", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:redstone_lamp");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:cocoa");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_2, "0");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);

    init_stair_props("minecraft:sandstone_stairs");

    init_simple_block("minecraft:emerald_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:deepslate_emerald_ore", BLOCK_MODEL_FULL);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:ender_chest");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:tripwire_hook");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ATTACHED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:tripwire");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ATTACHED, "false");
    add_block_property(props, BLOCK_PROPERTY_DISARMED, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_simple_block("minecraft:emerald_block", BLOCK_MODEL_FULL);

    init_stair_props("minecraft:spruce_stairs");
    init_stair_props("minecraft:birch_stairs");
    init_stair_props("minecraft:jungle_stairs");

    block_type = register_block_type("minecraft:command_block");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_CONDITIONAL, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    init_simple_block("minecraft:beacon", BLOCK_MODEL_FULL);

    // @TODO(traks) collision models
    init_wall_props("minecraft:cobblestone_wall");
    init_wall_props("minecraft:mossy_cobblestone_wall");

    init_simple_block("minecraft:flower_pot", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_oak_sapling", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_spruce_sapling", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_birch_sapling", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_jungle_sapling", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_acacia_sapling", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_dark_oak_sapling", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_fern", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_dandelion", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_poppy", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_blue_orchid", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_allium", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_azure_bluet", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_red_tulip", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_orange_tulip", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_white_tulip", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_pink_tulip", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_oxeye_daisy", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_cornflower", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_lily_of_the_valley", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_wither_rose", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_red_mushroom", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_brown_mushroom", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_dead_bush", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_cactus", BLOCK_MODEL_FLOWER_POT);

    block_type = register_block_type("minecraft:carrots");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:potatoes");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_button("minecraft:oak_button");
    init_button("minecraft:spruce_button");
    init_button("minecraft:birch_button");
    init_button("minecraft:jungle_button");
    init_button("minecraft:acacia_button");
    init_button("minecraft:dark_oak_button");

    // @TODO(traks) collision models
    init_skull_props("minecraft:skeleton_skull");
    init_wall_skull_props("minecraft:skeleton_wall_skull");
    init_skull_props("minecraft:wither_skeleton_skull");
    init_wall_skull_props("minecraft:wither_skeleton_wall_skull");
    init_skull_props("minecraft:zombie_head");
    init_wall_skull_props("minecraft:zombie_wall_head");
    init_skull_props("minecraft:player_head");
    init_wall_skull_props("minecraft:player_wall_head");
    init_skull_props("minecraft:creeper_head");
    init_wall_skull_props("minecraft:creeper_wall_head");
    init_skull_props("minecraft:dragon_head");
    init_wall_skull_props("minecraft:dragon_wall_head");

    // @TODO(traks) collision models
    init_anvil_props("minecraft:anvil");
    init_anvil_props("minecraft:chipped_anvil");
    init_anvil_props("minecraft:damaged_anvil");

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:trapped_chest");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_CHEST_TYPE, "single");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:light_weighted_pressure_plate");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:heavy_weighted_pressure_plate");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:comparator");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_MODE_COMPARATOR, "compare");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_Y_2);

    block_type = register_block_type("minecraft:daylight_detector");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_INVERTED, "false");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_Y_6);

    init_simple_block("minecraft:redstone_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:nether_quartz_ore", BLOCK_MODEL_FULL);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:hopper");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ENABLED, "true");
    add_block_property(props, BLOCK_PROPERTY_FACING_HOPPER, "down");
    finalise_block_props(props);

    init_simple_block("minecraft:quartz_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:chiseled_quartz_block", BLOCK_MODEL_FULL);

    init_pillar("minecraft:quartz_pillar");

    init_stair_props("minecraft:quartz_stairs");

    block_type = register_block_type("minecraft:activator_rail");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "north_south");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:dropper");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_TRIGGERED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    init_simple_block("minecraft:white_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:orange_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:magenta_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:light_blue_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:yellow_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:lime_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:pink_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:gray_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:light_gray_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cyan_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:purple_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:blue_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:brown_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:green_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:red_terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:black_terracotta", BLOCK_MODEL_FULL);

    init_pane("minecraft:white_stained_glass_pane");
    init_pane("minecraft:orange_stained_glass_pane");
    init_pane("minecraft:magenta_stained_glass_pane");
    init_pane("minecraft:light_blue_stained_glass_pane");
    init_pane("minecraft:yellow_stained_glass_pane");
    init_pane("minecraft:lime_stained_glass_pane");
    init_pane("minecraft:pink_stained_glass_pane");
    init_pane("minecraft:gray_stained_glass_pane");
    init_pane("minecraft:light_gray_stained_glass_pane");
    init_pane("minecraft:cyan_stained_glass_pane");
    init_pane("minecraft:purple_stained_glass_pane");
    init_pane("minecraft:blue_stained_glass_pane");
    init_pane("minecraft:brown_stained_glass_pane");
    init_pane("minecraft:green_stained_glass_pane");
    init_pane("minecraft:red_stained_glass_pane");
    init_pane("minecraft:black_stained_glass_pane");

    init_stair_props("minecraft:acacia_stairs");
    init_stair_props("minecraft:dark_oak_stairs");

    init_simple_block("minecraft:slime_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:barrier", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:light");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LEVEL_LIGHT, "15");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_trapdoor_props("minecraft:iron_trapdoor");

    init_simple_block("minecraft:prismarine", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:prismarine_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:dark_prismarine", BLOCK_MODEL_FULL);

    init_stair_props("minecraft:prismarine_stairs");
    init_stair_props("minecraft:prismarine_brick_stairs");
    init_stair_props("minecraft:dark_prismarine_stairs");

    init_slab("minecraft:prismarine_slab");
    init_slab("minecraft:prismarine_brick_slab");
    init_slab("minecraft:dark_prismarine_slab");

    init_simple_block("minecraft:sea_lantern", BLOCK_MODEL_FULL);

    init_pillar("minecraft:hay_block");

    init_simple_block("minecraft:white_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:orange_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:magenta_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:light_blue_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:yellow_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:lime_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:pink_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:gray_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:light_gray_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:cyan_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:purple_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:blue_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:brown_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:green_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:red_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:black_carpet", BLOCK_MODEL_Y_1);
    init_simple_block("minecraft:terracotta", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:coal_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:packed_ice", BLOCK_MODEL_FULL);

    init_tall_plant("minecraft:sunflower");
    init_tall_plant("minecraft:lilac");
    init_tall_plant("minecraft:rose_bush");
    init_tall_plant("minecraft:peony");
    init_tall_plant("minecraft:tall_grass");
    init_tall_plant("minecraft:large_fern");

    init_banner("minecraft:white_banner");
    init_banner("minecraft:orange_banner");
    init_banner("minecraft:magenta_banner");
    init_banner("minecraft:light_blue_banner");
    init_banner("minecraft:yellow_banner");
    init_banner("minecraft:lime_banner");
    init_banner("minecraft:pink_banner");
    init_banner("minecraft:gray_banner");
    init_banner("minecraft:light_gray_banner");
    init_banner("minecraft:cyan_banner");
    init_banner("minecraft:purple_banner");
    init_banner("minecraft:blue_banner");
    init_banner("minecraft:brown_banner");
    init_banner("minecraft:green_banner");
    init_banner("minecraft:red_banner");
    init_banner("minecraft:black_banner");

    init_wall_banner("minecraft:white_wall_banner");
    init_wall_banner("minecraft:orange_wall_banner");
    init_wall_banner("minecraft:magenta_wall_banner");
    init_wall_banner("minecraft:light_blue_wall_banner");
    init_wall_banner("minecraft:yellow_wall_banner");
    init_wall_banner("minecraft:lime_wall_banner");
    init_wall_banner("minecraft:pink_wall_banner");
    init_wall_banner("minecraft:gray_wall_banner");
    init_wall_banner("minecraft:light_gray_wall_banner");
    init_wall_banner("minecraft:cyan_wall_banner");
    init_wall_banner("minecraft:purple_wall_banner");
    init_wall_banner("minecraft:blue_wall_banner");
    init_wall_banner("minecraft:brown_wall_banner");
    init_wall_banner("minecraft:green_wall_banner");
    init_wall_banner("minecraft:red_wall_banner");
    init_wall_banner("minecraft:black_wall_banner");

    init_simple_block("minecraft:red_sandstone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:chiseled_red_sandstone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cut_red_sandstone", BLOCK_MODEL_FULL);

    init_stair_props("minecraft:red_sandstone_stairs");

    init_slab("minecraft:oak_slab");
    init_slab("minecraft:spruce_slab");
    init_slab("minecraft:birch_slab");
    init_slab("minecraft:jungle_slab");
    init_slab("minecraft:acacia_slab");
    init_slab("minecraft:dark_oak_slab");
    init_slab("minecraft:stone_slab");
    init_slab("minecraft:smooth_stone_slab");
    init_slab("minecraft:sandstone_slab");
    init_slab("minecraft:cut_sandstone_slab");
    init_slab("minecraft:petrified_oak_slab");
    init_slab("minecraft:cobblestone_slab");
    init_slab("minecraft:brick_slab");
    init_slab("minecraft:stone_brick_slab");
    init_slab("minecraft:nether_brick_slab");
    init_slab("minecraft:quartz_slab");
    init_slab("minecraft:red_sandstone_slab");
    init_slab("minecraft:cut_red_sandstone_slab");
    init_slab("minecraft:purpur_slab");

    init_simple_block("minecraft:smooth_stone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:smooth_sandstone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:smooth_quartz", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:smooth_red_sandstone", BLOCK_MODEL_FULL);

    init_fence_gate("minecraft:spruce_fence_gate");
    init_fence_gate("minecraft:birch_fence_gate");
    init_fence_gate("minecraft:jungle_fence_gate");
    init_fence_gate("minecraft:acacia_fence_gate");
    init_fence_gate("minecraft:dark_oak_fence_gate");

    init_fence("minecraft:spruce_fence");
    init_fence("minecraft:birch_fence");
    init_fence("minecraft:jungle_fence");
    init_fence("minecraft:acacia_fence");
    init_fence("minecraft:dark_oak_fence");

    init_door_props("minecraft:spruce_door");
    init_door_props("minecraft:birch_door");
    init_door_props("minecraft:jungle_door");
    init_door_props("minecraft:acacia_door");
    init_door_props("minecraft:dark_oak_door");

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:end_rod");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "up");
    finalise_block_props(props);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:chorus_plant");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_NEG_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    finalise_block_props(props);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:chorus_flower");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_5, "0");
    finalise_block_props(props);

    init_simple_block("minecraft:purpur_block", BLOCK_MODEL_FULL);

    init_pillar("minecraft:purpur_pillar");

    init_stair_props("minecraft:purpur_stairs");

    init_simple_block("minecraft:end_stone_bricks", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:beetroots");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_3, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_simple_block("minecraft:dirt_path", BLOCK_MODEL_Y_15);
    init_simple_block("minecraft:end_gateway", BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:repeating_command_block");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_CONDITIONAL, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:chain_command_block");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_CONDITIONAL, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:frosted_ice");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_3, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    init_simple_block("minecraft:magma_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:nether_wart_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:red_nether_bricks", BLOCK_MODEL_FULL);

    init_pillar("minecraft:bone_block");

    init_simple_block("minecraft:structure_void", BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:observer");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "south");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    // @TODO(traks) collision models
    init_shulker_box_props("minecraft:shulker_box");
    init_shulker_box_props("minecraft:white_shulker_box");
    init_shulker_box_props("minecraft:orange_shulker_box");
    init_shulker_box_props("minecraft:magenta_shulker_box");
    init_shulker_box_props("minecraft:light_blue_shulker_box");
    init_shulker_box_props("minecraft:yellow_shulker_box");
    init_shulker_box_props("minecraft:lime_shulker_box");
    init_shulker_box_props("minecraft:pink_shulker_box");
    init_shulker_box_props("minecraft:gray_shulker_box");
    init_shulker_box_props("minecraft:light_gray_shulker_box");
    init_shulker_box_props("minecraft:cyan_shulker_box");
    init_shulker_box_props("minecraft:purple_shulker_box");
    init_shulker_box_props("minecraft:blue_shulker_box");
    init_shulker_box_props("minecraft:brown_shulker_box");
    init_shulker_box_props("minecraft:green_shulker_box");
    init_shulker_box_props("minecraft:red_shulker_box");
    init_shulker_box_props("minecraft:black_shulker_box");

    init_glazed_terracotta("minecraft:white_glazed_terracotta");
    init_glazed_terracotta("minecraft:orange_glazed_terracotta");
    init_glazed_terracotta("minecraft:magenta_glazed_terracotta");
    init_glazed_terracotta("minecraft:light_blue_glazed_terracotta");
    init_glazed_terracotta("minecraft:yellow_glazed_terracotta");
    init_glazed_terracotta("minecraft:lime_glazed_terracotta");
    init_glazed_terracotta("minecraft:pink_glazed_terracotta");
    init_glazed_terracotta("minecraft:gray_glazed_terracotta");
    init_glazed_terracotta("minecraft:light_gray_glazed_terracotta");
    init_glazed_terracotta("minecraft:cyan_glazed_terracotta");
    init_glazed_terracotta("minecraft:purple_glazed_terracotta");
    init_glazed_terracotta("minecraft:blue_glazed_terracotta");
    init_glazed_terracotta("minecraft:brown_glazed_terracotta");
    init_glazed_terracotta("minecraft:green_glazed_terracotta");
    init_glazed_terracotta("minecraft:red_glazed_terracotta");
    init_glazed_terracotta("minecraft:black_glazed_terracotta");

    init_simple_block("minecraft:white_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:orange_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:magenta_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:light_blue_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:yellow_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:lime_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:pink_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:gray_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:light_gray_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cyan_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:purple_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:blue_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:brown_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:green_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:red_concrete", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:black_concrete", BLOCK_MODEL_FULL);

    init_simple_block("minecraft:white_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:orange_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:magenta_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:light_blue_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:yellow_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:lime_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:pink_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:gray_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:light_gray_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cyan_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:purple_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:blue_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:brown_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:green_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:red_concrete_powder", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:black_concrete_powder", BLOCK_MODEL_FULL);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:kelp");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_25, "0");
    finalise_block_props(props);

    init_simple_block("minecraft:kelp_plant", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:dried_kelp_block", BLOCK_MODEL_FULL);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:turtle_egg");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_EGGS, "1");
    add_block_property(props, BLOCK_PROPERTY_HATCH, "0");
    finalise_block_props(props);

    init_simple_block("minecraft:dead_tube_coral_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:dead_brain_coral_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:dead_bubble_coral_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:dead_fire_coral_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:dead_horn_coral_block", BLOCK_MODEL_FULL);

    init_simple_block("minecraft:tube_coral_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:brain_coral_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:bubble_coral_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:fire_coral_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:horn_coral_block", BLOCK_MODEL_FULL);

    init_coral("minecraft:dead_tube_coral");
    init_coral("minecraft:dead_brain_coral");
    init_coral("minecraft:dead_bubble_coral");
    init_coral("minecraft:dead_fire_coral");
    init_coral("minecraft:dead_horn_coral");
    init_coral("minecraft:tube_coral");
    init_coral("minecraft:brain_coral");
    init_coral("minecraft:bubble_coral");
    init_coral("minecraft:fire_coral");
    init_coral("minecraft:horn_coral");

    init_coral_fan("minecraft:dead_tube_coral_fan");
    init_coral_fan("minecraft:dead_brain_coral_fan");
    init_coral_fan("minecraft:dead_bubble_coral_fan");
    init_coral_fan("minecraft:dead_fire_coral_fan");
    init_coral_fan("minecraft:dead_horn_coral_fan");
    init_coral_fan("minecraft:tube_coral_fan");
    init_coral_fan("minecraft:brain_coral_fan");
    init_coral_fan("minecraft:bubble_coral_fan");
    init_coral_fan("minecraft:fire_coral_fan");
    init_coral_fan("minecraft:horn_coral_fan");

    init_coral_wall_fan("minecraft:dead_tube_coral_wall_fan");
    init_coral_wall_fan("minecraft:dead_brain_coral_wall_fan");
    init_coral_wall_fan("minecraft:dead_bubble_coral_wall_fan");
    init_coral_wall_fan("minecraft:dead_fire_coral_wall_fan");
    init_coral_wall_fan("minecraft:dead_horn_coral_wall_fan");
    init_coral_wall_fan("minecraft:tube_coral_wall_fan");
    init_coral_wall_fan("minecraft:brain_coral_wall_fan");
    init_coral_wall_fan("minecraft:bubble_coral_wall_fan");
    init_coral_wall_fan("minecraft:fire_coral_wall_fan");
    init_coral_wall_fan("minecraft:horn_coral_wall_fan");

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:sea_pickle");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_PICKLES, "1");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    finalise_block_props(props);

    init_simple_block("minecraft:blue_ice", BLOCK_MODEL_FULL);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:conduit");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    finalise_block_props(props);

    init_simple_block("minecraft:bamboo_sapling", BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:bamboo");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_1, "0");
    add_block_property(props, BLOCK_PROPERTY_BAMBOO_LEAVES, "none");
    add_block_property(props, BLOCK_PROPERTY_STAGE, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_CENTRED_BAMBOO);

    init_simple_block("minecraft:potted_bamboo", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:void_air", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:cave_air", BLOCK_MODEL_EMPTY);

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:bubble_column");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_DRAG, "true");
    finalise_block_props(props);

    init_stair_props("minecraft:polished_granite_stairs");
    init_stair_props("minecraft:smooth_red_sandstone_stairs");
    init_stair_props("minecraft:mossy_stone_brick_stairs");
    init_stair_props("minecraft:polished_diorite_stairs");
    init_stair_props("minecraft:mossy_cobblestone_stairs");
    init_stair_props("minecraft:end_stone_brick_stairs");
    init_stair_props("minecraft:stone_stairs");
    init_stair_props("minecraft:smooth_sandstone_stairs");
    init_stair_props("minecraft:smooth_quartz_stairs");
    init_stair_props("minecraft:granite_stairs");
    init_stair_props("minecraft:andesite_stairs");
    init_stair_props("minecraft:red_nether_brick_stairs");
    init_stair_props("minecraft:polished_andesite_stairs");
    init_stair_props("minecraft:diorite_stairs");

    init_slab("minecraft:polished_granite_slab");
    init_slab("minecraft:smooth_red_sandstone_slab");
    init_slab("minecraft:mossy_stone_brick_slab");
    init_slab("minecraft:polished_diorite_slab");
    init_slab("minecraft:mossy_cobblestone_slab");
    init_slab("minecraft:end_stone_brick_slab");
    init_slab("minecraft:smooth_sandstone_slab");
    init_slab("minecraft:smooth_quartz_slab");
    init_slab("minecraft:granite_slab");
    init_slab("minecraft:andesite_slab");
    init_slab("minecraft:red_nether_brick_slab");
    init_slab("minecraft:polished_andesite_slab");
    init_slab("minecraft:diorite_slab");

    init_wall_props("minecraft:brick_wall");
    init_wall_props("minecraft:prismarine_wall");
    init_wall_props("minecraft:red_sandstone_wall");
    init_wall_props("minecraft:mossy_stone_brick_wall");
    init_wall_props("minecraft:granite_wall");
    init_wall_props("minecraft:stone_brick_wall");
    init_wall_props("minecraft:nether_brick_wall");
    init_wall_props("minecraft:andesite_wall");
    init_wall_props("minecraft:red_nether_brick_wall");
    init_wall_props("minecraft:sandstone_wall");
    init_wall_props("minecraft:end_stone_brick_wall");
    init_wall_props("minecraft:diorite_wall");

    // @TODO(traks) collision models
    block_type = register_block_type("minecraft:scaffolding");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_BOTTOM, "false");
    add_block_property(props, BLOCK_PROPERTY_STABILITY_DISTANCE, "7");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:loom");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:barrel");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_OPEN, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:smoker");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:blast_furnace");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    init_simple_block("minecraft:cartography_table", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:fletching_table", BLOCK_MODEL_FULL);

    // @TODO(traks) collisions models
    block_type = register_block_type("minecraft:grindstone");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_ATTACH_FACE, "wall");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:lectern");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_HAS_BOOK, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_LECTERN);

    init_simple_block("minecraft:smithing_table", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:stonecutter");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_Y_9);

    // @TODO(traks) collisions models
    block_type = register_block_type("minecraft:bell");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_BELL_ATTACHMENT, "floor");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    finalise_block_props(props);

    // @TODO(traks) collisions models
    block_type = register_block_type("minecraft:lantern");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HANGING, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    // @TODO(traks) collisions models
    block_type = register_block_type("minecraft:soul_lantern");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HANGING, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:campfire");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "true");
    add_block_property(props, BLOCK_PROPERTY_SIGNAL_FIRE, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_Y_7);

    block_type = register_block_type("minecraft:soul_campfire");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "true");
    add_block_property(props, BLOCK_PROPERTY_SIGNAL_FIRE, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_Y_7);

    block_type = register_block_type("minecraft:sweet_berry_bush");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_3, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_pillar("minecraft:warped_stem");
    init_pillar("minecraft:stripped_warped_stem");
    init_pillar("minecraft:warped_hyphae");
    init_pillar("minecraft:stripped_warped_hyphae");

    init_simple_block("minecraft:warped_nylium", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:warped_fungus", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:warped_wart_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:warped_roots", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:nether_sprouts", BLOCK_MODEL_EMPTY);

    init_pillar("minecraft:crimson_stem");
    init_pillar("minecraft:stripped_crimson_stem");
    init_pillar("minecraft:crimson_hyphae");
    init_pillar("minecraft:stripped_crimson_hyphae");

    init_simple_block("minecraft:crimson_nylium", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:crimson_fungus", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:shroomlight", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:weeping_vines");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_25, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_simple_block("minecraft:weeping_vines_plant", BLOCK_MODEL_EMPTY);

    block_type = register_block_type("minecraft:twisting_vines");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_25, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_simple_block("minecraft:twisting_vines_plant", BLOCK_MODEL_EMPTY);

    init_simple_block("minecraft:crimson_roots", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:crimson_planks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:warped_planks", BLOCK_MODEL_FULL);

    init_slab("minecraft:crimson_slab");
    init_slab("minecraft:warped_slab");

    init_pressure_plate("minecraft:crimson_pressure_plate");
    init_pressure_plate("minecraft:warped_pressure_plate");

    init_fence("minecraft:crimson_fence");
    init_fence("minecraft:warped_fence");

    init_trapdoor_props("minecraft:crimson_trapdoor");
    init_trapdoor_props("minecraft:warped_trapdoor");

    init_fence_gate("minecraft:crimson_fence_gate");
    init_fence_gate("minecraft:warped_fence_gate");

    init_stair_props("minecraft:crimson_stairs");
    init_stair_props("minecraft:warped_stairs");

    init_button("minecraft:crimson_button");
    init_button("minecraft:warped_button");

    init_door_props("minecraft:crimson_door");
    init_door_props("minecraft:warped_door");

    init_sign("minecraft:crimson_sign");
    init_sign("minecraft:warped_sign");

    init_wall_sign("minecraft:crimson_wall_sign");
    init_wall_sign("minecraft:warped_wall_sign");

    block_type = register_block_type("minecraft:structure_block");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_STRUCTUREBLOCK_MODE, "save");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:jigsaw");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_JIGSAW_ORIENTATION, "north_up");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:composter");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_LEVEL_COMPOSTER, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_COMPOSTER);

    block_type = register_block_type("minecraft:target");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:bee_nest");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LEVEL_HONEY, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:beehive");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LEVEL_HONEY, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    init_simple_block("minecraft:honey_block", BLOCK_MODEL_HONEY_BLOCK);
    init_simple_block("minecraft:honeycomb_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:netherite_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:ancient_debris", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:crying_obsidian", BLOCK_MODEL_FULL);

    block_type = register_block_type("minecraft:respawn_anchor");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_RESPAWN_ANCHOR_CHARGES, "0");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_FULL);

    init_simple_block("minecraft:potted_crimson_fungus", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_warped_fungus", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_crimson_roots", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_warped_roots", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:lodestone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:blackstone", BLOCK_MODEL_FULL);

    init_stair_props("minecraft:blackstone_stairs");

    init_wall_props("minecraft:blackstone_wall");

    init_slab("minecraft:blackstone_slab");

    init_simple_block("minecraft:polished_blackstone", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:polished_blackstone_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cracked_polished_blackstone_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:chiseled_polished_blackstone", BLOCK_MODEL_FULL);

    init_slab("minecraft:polished_blackstone_brick_slab");

    init_stair_props("minecraft:polished_blackstone_brick_stairs");

    init_wall_props("minecraft:polished_blackstone_brick_wall");

    init_simple_block("minecraft:gilded_blackstone", BLOCK_MODEL_FULL);

    init_stair_props("minecraft:polished_blackstone_stairs");

    init_slab("minecraft:polished_blackstone_slab");

    init_pressure_plate("minecraft:polished_blackstone_pressure_plate");

    init_button("minecraft:polished_blackstone_button");

    init_wall_props("minecraft:polished_blackstone_wall");

    init_simple_block("minecraft:chiseled_nether_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cracked_nether_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:quartz_bricks", BLOCK_MODEL_FULL);

    init_candle("minecraft:candle");
    init_candle("minecraft:white_candle");
    init_candle("minecraft:orange_candle");
    init_candle("minecraft:magenta_candle");
    init_candle("minecraft:light_blue_candle");
    init_candle("minecraft:yellow_candle");
    init_candle("minecraft:lime_candle");
    init_candle("minecraft:pink_candle");
    init_candle("minecraft:gray_candle");
    init_candle("minecraft:light_gray_candle");
    init_candle("minecraft:cyan_candle");
    init_candle("minecraft:purple_candle");
    init_candle("minecraft:blue_candle");
    init_candle("minecraft:brown_candle");
    init_candle("minecraft:green_candle");
    init_candle("minecraft:red_candle");
    init_candle("minecraft:black_candle");

    init_candle_cake("minecraft:candle_cake");
    init_candle_cake("minecraft:white_candle_cake");
    init_candle_cake("minecraft:orange_candle_cake");
    init_candle_cake("minecraft:magenta_candle_cake");
    init_candle_cake("minecraft:light_blue_candle_cake");
    init_candle_cake("minecraft:yellow_candle_cake");
    init_candle_cake("minecraft:lime_candle_cake");
    init_candle_cake("minecraft:pink_candle_cake");
    init_candle_cake("minecraft:gray_candle_cake");
    init_candle_cake("minecraft:light_gray_candle_cake");
    init_candle_cake("minecraft:cyan_candle_cake");
    init_candle_cake("minecraft:purple_candle_cake");
    init_candle_cake("minecraft:blue_candle_cake");
    init_candle_cake("minecraft:brown_candle_cake");
    init_candle_cake("minecraft:green_candle_cake");
    init_candle_cake("minecraft:red_candle_cake");
    init_candle_cake("minecraft:black_candle_cake");

    init_simple_block("minecraft:amethyst_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:budding_amethyst", BLOCK_MODEL_FULL);

    init_amethyst_cluster("minecraft:amethyst_cluster");
    init_amethyst_cluster("minecraft:large_amethyst_bud");
    init_amethyst_cluster("minecraft:medium_amethyst_bud");
    init_amethyst_cluster("minecraft:small_amethyst_bud");

    init_simple_block("minecraft:tuff", BLOCK_MODEL_FULL);

    init_simple_block("minecraft:calcite", BLOCK_MODEL_FULL);

    init_simple_block("minecraft:tinted_glass", BLOCK_MODEL_FULL);

    // @TODO(traks) correct collision model
    init_simple_block("minecraft:powder_snow", BLOCK_MODEL_EMPTY);

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:sculk_sensor");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_SCULK_SENSOR_PHASE, "inactive");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    init_simple_block("minecraft:oxidized_copper", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:weathered_copper", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:exposed_copper", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:copper_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:copper_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:deepslate_copper_ore", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:oxidized_cut_copper", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:weathered_cut_copper", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:exposed_cut_copper", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cut_copper", BLOCK_MODEL_FULL);

    init_stair_props("minecraft:oxidized_cut_copper_stairs");
    init_stair_props("minecraft:weathered_cut_copper_stairs");
    init_stair_props("minecraft:exposed_cut_copper_stairs");
    init_stair_props("minecraft:cut_copper_stairs");

    init_slab("minecraft:oxidized_cut_copper_slab");
    init_slab("minecraft:weathered_cut_copper_slab");
    init_slab("minecraft:exposed_cut_copper_slab");
    init_slab("minecraft:cut_copper_slab");

    init_simple_block("minecraft:waxed_copper_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:waxed_weathered_copper", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:waxed_exposed_copper", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:waxed_oxidized_copper", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:waxed_oxidized_cut_copper", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:waxed_weathered_cut_copper", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:waxed_exposed_cut_copper", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:waxed_cut_copper", BLOCK_MODEL_FULL);

    init_stair_props("minecraft:waxed_oxidized_cut_copper_stairs");
    init_stair_props("minecraft:waxed_weathered_cut_copper_stairs");
    init_stair_props("minecraft:waxed_exposed_cut_copper_stairs");
    init_stair_props("minecraft:waxed_cut_copper_stairs");

    init_slab("minecraft:waxed_oxidized_cut_copper_slab");
    init_slab("minecraft:waxed_weathered_cut_copper_slab");
    init_slab("minecraft:waxed_exposed_cut_copper_slab");
    init_slab("minecraft:waxed_cut_copper_slab");

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:lightning_rod");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_FACING, "up");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:pointed_dripstone");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_VERTICAL_DIRECTION, "up");
    add_block_property(props, BLOCK_PROPERTY_DRIPSTONE_THICKNESS, "tip");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);

    init_simple_block("minecraft:dripstone_block", BLOCK_MODEL_FULL);

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:cave_vines");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_AGE_25, "0");
    add_block_property(props, BLOCK_PROPERTY_BERRIES, "false");
    finalise_block_props(props);

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:cave_vines_plant");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_BERRIES, "false");
    finalise_block_props(props);

    // @TODO(traks) collision model
    init_simple_block("minecraft:spore_blossom", BLOCK_MODEL_EMPTY);

    // @TODO(traks) collision models
    init_simple_block("minecraft:azalea", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:flowering_azalea", BLOCK_MODEL_EMPTY);

    init_simple_block("minecraft:moss_carpet", BLOCK_MODEL_EMPTY);
    init_simple_block("minecraft:moss_block", BLOCK_MODEL_Y_1);

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:big_dripleaf");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_DRIPLEAF_TILT, "none");
    finalise_block_props(props);

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:big_dripleaf_stem");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);

    // @TODO(traks) collision model
    block_type = register_block_type("minecraft:small_dripleaf");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    finalise_block_props(props);

    block_type = register_block_type("minecraft:hanging_roots");
    props = serv->block_properties_table + block_type;
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    finalise_block_props(props);
    set_collision_model_for_all_states(props, BLOCK_MODEL_EMPTY);

    init_simple_block("minecraft:rooted_dirt", BLOCK_MODEL_FULL);

    init_pillar("minecraft:deepslate");

    init_simple_block("minecraft:cobbled_deepslate", BLOCK_MODEL_FULL);
    init_stair_props("minecraft:cobbled_deepslate_stairs");
    init_slab("minecraft:cobbled_deepslate_slab");
    init_wall_props("minecraft:cobbled_deepslate_wall");

    init_simple_block("minecraft:polished_deepslate", BLOCK_MODEL_FULL);
    init_stair_props("minecraft:polished_deepslate_stairs");
    init_slab("minecraft:polished_deepslate_slab");
    init_wall_props("minecraft:polished_deepslate_wall");

    init_simple_block("minecraft:deepslate_tiles", BLOCK_MODEL_FULL);
    init_stair_props("minecraft:deepslate_tile_stairs");
    init_slab("minecraft:deepslate_tile_slab");
    init_wall_props("minecraft:deepslate_tile_wall");

    init_simple_block("minecraft:deepslate_bricks", BLOCK_MODEL_FULL);
    init_stair_props("minecraft:deepslate_brick_stairs");
    init_slab("minecraft:deepslate_brick_slab");
    init_wall_props("minecraft:deepslate_brick_wall");

    init_simple_block("minecraft:chiseled_deepslate", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cracked_deepslate_bricks", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:cracked_deepslate_tiles", BLOCK_MODEL_FULL);

    init_pillar("minecraft:infested_deepslate");

    init_simple_block("minecraft:smooth_basalt", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:raw_iron_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:raw_copper_block", BLOCK_MODEL_FULL);
    init_simple_block("minecraft:raw_gold_block", BLOCK_MODEL_FULL);

    init_simple_block("minecraft:potted_azalea_bush", BLOCK_MODEL_FLOWER_POT);
    init_simple_block("minecraft:potted_flowering_azalea_bush", BLOCK_MODEL_FLOWER_POT);

    serv->vanilla_block_state_count = serv->actual_block_state_count;

    init_simple_block("blaze:unknown", BLOCK_MODEL_FULL);
}
