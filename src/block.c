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

    res.type_tags = props->type_tags;

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
        Chunk * ch = GetChunkIfLoaded(ch_pos);
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
    if (BlockHasTag(&info_right, BLOCK_TAG_STAIRS)) {
        if (info_right.half == cur_info->half && info_right.horizontal_facing == cur_info->horizontal_facing) {
            force_connect_right = 1;
        }
    }

    u16 state_left = try_get_block_state(get_relative_block_pos(pos, rotate_direction_counter_clockwise(cur_info->horizontal_facing)));
    block_state_info info_left = describe_block_state(state_left);
    if (BlockHasTag(&info_left, BLOCK_TAG_STAIRS)) {
        if (info_left.half == cur_info->half && info_left.horizontal_facing == cur_info->horizontal_facing) {
            force_connect_left = 1;
        }
    }

    // try to connect with stairs in front
    u16 state_front = try_get_block_state(get_relative_block_pos(pos,
            get_opposite_direction(cur_info->horizontal_facing)));
    block_state_info info_front = describe_block_state(state_front);
    if (BlockHasTag(&info_front, BLOCK_TAG_STAIRS)) {
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
    if (BlockHasTag(&info_behind, BLOCK_TAG_STAIRS)) {
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

int CanCrossConnectToGeneric(block_state_info * curInfo,
        block_state_info * neighbourInfo, u16 neighbourState,
        int fromDir) {
    // @NOTE(traks) can connect to sturdy faces with some exceptions
    support_model support = get_support_model(neighbourState);
    u32 neighbourType = neighbourInfo->block_type;
    if (support.full_face_flags & (1 << get_opposite_direction(fromDir))) {
        if (BlockHasTag(neighbourInfo, BLOCK_TAG_LEAVES)
                || BlockHasTag(neighbourInfo, BLOCK_TAG_SHULKER_BOX)
                || neighbourType == BLOCK_BARRIER
                || neighbourType == BLOCK_PUMPKIN
                || neighbourType == BLOCK_CARVED_PUMPKIN
                || neighbourType == BLOCK_JACK_O_LANTERN
                || neighbourType == BLOCK_MELON) {
            return 0;
        }
        return 1;
    }
    return 0;
}

void
update_pane_shape(BlockPos pos,
        block_state_info * cur_info, int from_direction) {
    BlockPos neighbour_pos = get_relative_block_pos(pos, from_direction);
    u16 neighbour_state = try_get_block_state(neighbour_pos);
    i32 neighbour_type = serv->block_type_by_state[neighbour_state];
    block_state_info neighbour_info = describe_block_state(neighbour_state);

    int connect = 0;

    if (BlockHasTag(&neighbour_info, BLOCK_TAG_PANE_LIKE)
            || BlockHasTag(&neighbour_info, BLOCK_TAG_WALL)) {
        connect = 1;
    } else if (CanCrossConnectToGeneric(cur_info, &neighbour_info, neighbour_state, from_direction)) {
        connect = 1;
    }

    *(&cur_info->neg_y + from_direction) = connect;
}

void
update_fence_shape(BlockPos pos,
        block_state_info * cur_info, int from_direction) {
    BlockPos neighbour_pos = get_relative_block_pos(pos, from_direction);
    u16 neighbour_state = try_get_block_state(neighbour_pos);
    i32 neighbour_type = serv->block_type_by_state[neighbour_state];
    block_state_info neighbour_info = describe_block_state(neighbour_state);

    int connect = 0;

    if ((BlockHasTag(&neighbour_info, BLOCK_TAG_WOODEN_FENCE)
            && BlockHasTag(cur_info, BLOCK_TAG_WOODEN_FENCE))
            || neighbour_type == cur_info->block_type) {
        // @NOTE(traks) allow wooden fences to connect with each other and allow
        // nether brick fences to connect with each other
        connect = 1;
    } else if (CanCrossConnectToGeneric(cur_info, &neighbour_info, neighbour_state, from_direction)) {
        connect = 1;
    }

    *(&cur_info->neg_y + from_direction) = connect;
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

    if (BlockHasTag(&neighbour_info, BLOCK_TAG_FENCE_GATE)) {
        // @NOTE(traks) try connect to fence gate in wall if oriented properly
        int facing = neighbour_info.horizontal_facing;
        int rotated = rotate_direction_clockwise(facing);
        if (rotated == from_direction || rotated == get_opposite_direction(from_direction)) {
            connect = 1;
        }
    } else if (BlockHasTag(&neighbour_info, BLOCK_TAG_WALL)) {
        connect = 1;
    } else if (CanCrossConnectToGeneric(cur_info, &neighbour_info, neighbour_state, from_direction)) {
        connect = 1;
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
            block_state_info neighbour_info_pos = describe_block_state(neighbour_state_pos);
            int neighbour_state_neg = try_get_block_state(
                    get_relative_block_pos(pos, DIRECTION_NEG_Z));
            block_state_info neighbour_info_neg = describe_block_state(neighbour_state_neg);
            if (BlockHasTag(&neighbour_info_pos, BLOCK_TAG_WALL)
                    || BlockHasTag(&neighbour_info_neg, BLOCK_TAG_WALL)) {
                cur_info.in_wall = 1;
            }
        } else {
            // facing along z axis
            int neighbour_state_pos = try_get_block_state(
                    get_relative_block_pos(pos, DIRECTION_POS_X));
            block_state_info neighbour_info_pos = describe_block_state(neighbour_state_pos);
            int neighbour_state_neg = try_get_block_state(
                    get_relative_block_pos(pos, DIRECTION_NEG_X));
            block_state_info neighbour_info_neg = describe_block_state(neighbour_state_neg);
            if (BlockHasTag(&neighbour_info_pos, BLOCK_TAG_WALL)
                    || BlockHasTag(&neighbour_info_neg, BLOCK_TAG_WALL)) {
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
        .blocks_to_update = MallocInArena(&temp_arena,
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
