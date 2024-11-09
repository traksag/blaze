#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "shared.h"
#include "chunk.h"

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
push_block_update(WorldBlockPos pos, int from_dir,
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
push_neighbour_block_update(WorldBlockPos pos, int dir,
        block_update_context * buc) {
    push_block_update(WorldBlockPosRel(pos, dir),
            get_opposite_direction(dir), buc);
}

void
push_direct_neighbour_block_updates(WorldBlockPos pos,
        block_update_context * buc) {
    if (buc->max_updates - buc->update_count < 6) {
        return;
    }

    for (int j = 0; j < 6; j++) {
        int to_direction = update_order[j];
        WorldBlockPos neighbour = WorldBlockPosRel(pos, to_direction);
        buc->blocks_to_update[buc->update_count] = (block_update) {
            .pos = neighbour,
            .from_direction = get_opposite_direction(to_direction),
        };
        buc->update_count++;
    }
}

static void
schedule_block_update(WorldBlockPos pos, int from_direction, int delay) {
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

block_state_info DescribeStateIndex(block_properties * props, i32 stateIndex) {
    block_state_info res = {0};
    res.typeTags = props->type_tags;
    memset(res.values, (u8) -1, BLOCK_PROPERTY_COUNT);
    for (i32 propIndex = props->property_count - 1; propIndex >= 0; propIndex--) {
        i32 propId = props->property_specs[propIndex];
        block_property_spec * spec = serv->block_property_specs + propId;
        i32 valueIndex = stateIndex % spec->value_count;
        i32 intValue = spec->intValues[valueIndex];
        res.values[propId] = intValue;
        stateIndex = stateIndex / spec->value_count;
    }
    return res;
}

block_state_info
describe_block_state(u16 block_state) {
    i32 blockType = serv->block_type_by_state[block_state];
    block_properties * props = serv->block_properties_table + blockType;
    u16 baseState = props->base_state;
    i32 stateIndex = block_state - baseState;
    block_state_info res = DescribeStateIndex(props, stateIndex);
    res.blockType = blockType;
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
    block_properties * props = serv->block_properties_table + info->blockType;
    i32 offset = 0;

    for (i32 propIndex = 0; propIndex < props->property_count; propIndex++) {
        i32 propId = props->property_specs[propIndex];
        i32 intValue = info->values[propId];
        block_property_spec * spec = serv->block_property_specs + propId;
        i32 valueCount = spec->value_count;
        i32 finalValue = props->default_value_indices[propIndex];
        // TODO(traks): This loop to find the value index is not ideal, but it
        // works for now. Once we start thinking harder about how we want to do
        // block state info, this might change a good bit.
        for (i32 valueIndex = 0; valueIndex < valueCount; valueIndex++) {
            if (spec->intValues[valueIndex] == intValue) {
                finalValue = valueIndex;
                break;
            }
        }
        offset = offset * valueCount + finalValue;
    }

    i32 res = props->base_state + offset;
    return res;
}

static SetBlockResult break_block(WorldBlockPos pos) {
    u16 cur_state = WorldGetBlockState(pos);
    i32 cur_type = serv->block_type_by_state[cur_state];

    // @TODO(traks) add a block property for this
    if (cur_type != BLOCK_FIRE && cur_type != BLOCK_SOUL_FIRE) {
        WorldChunkPos ch_pos = {
            .worldId = pos.worldId,
            .x = pos.x >> 4,
            .z = pos.z >> 4
        };
        Chunk * ch = GetChunkIfLoaded(ch_pos);
        if (ch != NULL) {
            if (ch->lastLocalEventTick != serv->current_tick) {
                ch->lastLocalEventTick = serv->current_tick;
                ch->localEventCount = 0;
            }
            if (ch->localEventCount < ARRAY_SIZE(ch->localEvents)) {
                ch->localEvents[ch->localEventCount] = (level_event) {
                    .type = LEVEL_EVENT_PARTICLES_DESTROY_BLOCK,
                    .pos = pos.xyz,
                    .data = cur_state,
                };
                ch->localEventCount++;
            }
        }
    }

    SetBlockResult res = WorldSetBlockState(pos, get_default_block_state(BLOCK_AIR));
    return res;
}

// used to check whether redstone power travels through a block state. Also used
// to check whether redstone wire connects diagonally through a block state;
// this function returns false for those states.
static int
conducts_redstone(u16 block_state, WorldBlockPos pos) {
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
    case BLOCK_MANGROVE_LEAVES:
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
        BlockModel model = BlockDetermineCollisionModel(block_state, pos);
        if (model.fullFaces == 0x3f) {
            return 1;
        }
        return 0;
    }
    }
}

static void
translate_model(BlockModel * model, float dx, float dy, float dz) {
    for (int i = 0; i < model->size; i++) {
        model->boxes[i].minX += dx;
        model->boxes[i].minY += dy;
        model->boxes[i].minZ += dz;
        model->boxes[i].maxX += dx;
        model->boxes[i].maxY += dy;
        model->boxes[i].maxZ += dz;
    }
}

BlockModel BlockDetermineCollisionModel(i32 blockState, WorldBlockPos pos) {
    i32 block_type = serv->block_type_by_state[blockState];
    BlockModel res;

    switch (block_type) {
    case BLOCK_BAMBOO: {
        u64 seed = ((u64) pos.x * 3129871) ^ ((u64) pos.z * 116129781);
        seed = seed * seed * 42317861 + seed * 11;
        seed >>= 16;
        res = serv->staticBlockModels[serv->collisionModelByState[blockState]];
        translate_model(&res, ((seed & 0xf) / 15.0f - 0.5f) * 0.5f, 0,
                (((seed >> 8) & 0xf) / 15.0f - 0.5f) * 0.5f);
        break;
    }
    case BLOCK_WATER:
    case BLOCK_LAVA: {
        // @TODO(traks) let striders walk on water/lava source blocks
        res = serv->staticBlockModels[serv->collisionModelByState[blockState]];
        break;
    }
    case BLOCK_MOVING_PISTON: {
        // @TODO(traks) use block entity to determine collision model
        res = serv->staticBlockModels[serv->collisionModelByState[blockState]];
        break;
    }
    case BLOCK_SCAFFOLDING: {
        // @TODO(traks) collision model should depend on whether entity is
        // shifting
        res = serv->staticBlockModels[serv->collisionModelByState[blockState]];
        break;
    }
    case BLOCK_POWDER_SNOW: {
        // @TODO(traks) collision model depends on what the entity is wearing
        // and some other stuff that depends on the entity and the surroundings
        // in the world
        res = serv->staticBlockModels[serv->collisionModelByState[blockState]];
        break;
    }
    default:
        res = serv->staticBlockModels[serv->collisionModelByState[blockState]];
    }
    return res;
}

BlockModel BlockDetermineSupportModel(i32 blockState) {
    i32 block_type = serv->block_type_by_state[blockState];
    BlockModel res;

    switch (block_type) {
    // some block types have special collision models and therefore also have
    // special support models
    case BLOCK_MOVING_PISTON: {
        // @TODO(traks) use block entity to determine support model
        res = serv->staticBlockModels[serv->supportModelByState[blockState]];
        break;
    }
    default:
        res = serv->staticBlockModels[serv->supportModelByState[blockState]];
    }
    return res;
}

int
get_water_level(u16 state) {
    block_state_info info = describe_block_state(state);

    switch (info.blockType) {
    // @TODO(traks) return this for lava as well?
    case BLOCK_WATER:
        return info.level_fluid;
    case BLOCK_BUBBLE_COLUMN:
    case BLOCK_KELP:
    case BLOCK_KELP_PLANT:
    case BLOCK_SEAGRASS:
    case BLOCK_TALL_SEAGRASS:
        return FLUID_LEVEL_SOURCE;
    default:
        if (info.waterlogged == 1) {
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

int can_propagule_survive_on(i32 type_below) {
    return can_plant_survive_on(type_below) || type_below == BLOCK_CLAY;
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
        BlockModel support = BlockDetermineSupportModel(state_below);
        if (support.fullFaces & (1 << DIRECTION_POS_Y)) {
            return 1;
        }
        return 0;
    }
    }
}

int
can_big_dripleaf_stem_survive_at(WorldBlockPos cur_pos) {
    u16 state_below = WorldGetBlockState(WorldBlockPosRel(cur_pos, DIRECTION_NEG_Y));
    i32 type_below = serv->block_type_by_state[state_below];
    BlockModel support_below = BlockDetermineSupportModel(state_below);
    u16 state_above = WorldGetBlockState(WorldBlockPosRel(cur_pos, DIRECTION_POS_Y));
    i32 type_above = serv->block_type_by_state[state_above];

    if ((type_below == BLOCK_BIG_DRIPLEAF_STEM || support_below.fullFaces & (1 << DIRECTION_POS_Y))
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
can_sea_pickle_survive_on(u16 state_below, WorldBlockPos posBelow) {
    BlockModel collision = BlockDetermineCollisionModel(state_below, posBelow);
    BlockModel support = BlockDetermineSupportModel(state_below);
    if ((collision.nonEmptyFaces & (1 << DIRECTION_POS_Y)) || (support.fullFaces & (1 << DIRECTION_POS_Y))) {
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
        BlockModel support = BlockDetermineSupportModel(state_below);
        if (support.fullFaces & (1 << DIRECTION_POS_Y)) {
            return 1;
        }
        return 0;
    }
    }
}

int
can_pressure_plate_survive_on(u16 state_below) {
    // @TODO(traks) can survive if top face is circle too (e.g. cauldron)
    BlockModel support = BlockDetermineSupportModel(state_below);
    if (support.poleFaces & (1 << DIRECTION_POS_Y)) {
        return 1;
    }
    return 0;
}

int
can_redstone_wire_survive_on(u16 state_below) {
    BlockModel support = BlockDetermineSupportModel(state_below);
    i32 type_below = serv->block_type_by_state[state_below];
    if (support.fullFaces & (1 << DIRECTION_POS_Y)) {
        return 1;
    } else if (type_below == BLOCK_HOPPER) {
        return 1;
    }
    return 0;
}

int
can_sugar_cane_survive_at(WorldBlockPos cur_pos) {
    u16 state_below = WorldGetBlockState(WorldBlockPosRel(cur_pos, DIRECTION_NEG_Y));
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
        WorldBlockPos neighbour_pos[4];
        for (int i = 0; i < 4; i++) {
            WorldBlockPos pos = cur_pos;
            pos.y--;
            neighbour_pos[i] = pos;
        }
        neighbour_pos[0].x--;
        neighbour_pos[1].x++;
        neighbour_pos[2].z--;
        neighbour_pos[3].z++;

        // check blocks next to ground block for water
        for (int i = 0; i < 4; i++) {
            WorldBlockPos pos = neighbour_pos[i];
            u16 neighbour_state = WorldGetBlockState(pos);
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
update_stairs_shape(WorldBlockPos pos, block_state_info * cur_info) {
    cur_info->stairs_shape = STAIRS_SHAPE_STRAIGHT;

    // first look on left and right of stairs block to see if there are other
    // stairs there it must connect to
    int force_connect_right = 0;
    int force_connect_left = 0;

    u16 state_right = WorldGetBlockState(WorldBlockPosRel(pos, rotate_direction_clockwise(cur_info->horizontal_facing)));
    block_state_info info_right = describe_block_state(state_right);
    if (BlockHasTag(&info_right, BLOCK_TAG_STAIRS)) {
        if (info_right.half == cur_info->half && info_right.horizontal_facing == cur_info->horizontal_facing) {
            force_connect_right = 1;
        }
    }

    u16 state_left = WorldGetBlockState(WorldBlockPosRel(pos, rotate_direction_counter_clockwise(cur_info->horizontal_facing)));
    block_state_info info_left = describe_block_state(state_left);
    if (BlockHasTag(&info_left, BLOCK_TAG_STAIRS)) {
        if (info_left.half == cur_info->half && info_left.horizontal_facing == cur_info->horizontal_facing) {
            force_connect_left = 1;
        }
    }

    // try to connect with stairs in front
    u16 state_front = WorldGetBlockState(WorldBlockPosRel(pos, get_opposite_direction(cur_info->horizontal_facing)));
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
    u16 state_behind = WorldGetBlockState(WorldBlockPosRel(pos, cur_info->horizontal_facing));
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
    BlockModel support = BlockDetermineSupportModel(neighbourState);
    u32 neighbourType = neighbourInfo->blockType;
    if (support.fullFaces & (1 << get_opposite_direction(fromDir))) {
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
update_pane_shape(WorldBlockPos pos,
        block_state_info * cur_info, int from_direction) {
    WorldBlockPos neighbour_pos = WorldBlockPosRel(pos, from_direction);
    u16 neighbour_state = WorldGetBlockState(neighbour_pos);
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
update_fence_shape(WorldBlockPos pos,
        block_state_info * cur_info, int from_direction) {
    WorldBlockPos neighbour_pos = WorldBlockPosRel(pos, from_direction);
    u16 neighbour_state = WorldGetBlockState(neighbour_pos);
    i32 neighbour_type = serv->block_type_by_state[neighbour_state];
    block_state_info neighbour_info = describe_block_state(neighbour_state);

    int connect = 0;

    if ((BlockHasTag(&neighbour_info, BLOCK_TAG_WOODEN_FENCE)
            && BlockHasTag(cur_info, BLOCK_TAG_WOODEN_FENCE))
            || neighbour_type == cur_info->blockType) {
        // @NOTE(traks) allow wooden fences to connect with each other and allow
        // nether brick fences to connect with each other
        connect = 1;
    } else if (CanCrossConnectToGeneric(cur_info, &neighbour_info, neighbour_state, from_direction)) {
        connect = 1;
    }

    *(&cur_info->neg_y + from_direction) = connect;
}

void
update_wall_shape(WorldBlockPos pos,
        block_state_info * cur_info, int from_direction) {
    WorldBlockPos neighbour_pos = WorldBlockPosRel(pos, from_direction);
    u16 neighbour_state = WorldGetBlockState(neighbour_pos);
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
    case BLOCK_MANGROVE_PRESSURE_PLATE:
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
    case BLOCK_MANGROVE_BUTTON:
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
    case BLOCK_MANGROVE_PRESSURE_PLATE:
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
    case BLOCK_MANGROVE_BUTTON:
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
    case BLOCK_MANGROVE_PRESSURE_PLATE:
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
    case BLOCK_MANGROVE_BUTTON:
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
get_redstone_side_power(WorldBlockPos pos, int dir, int to_wire,
        int ignore_wires) {
    WorldBlockPos side_pos = WorldBlockPosRel(pos, dir);
    int opp_dir = get_opposite_direction(dir);
    u16 side_state = WorldGetBlockState(side_pos);
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

            u16 state = WorldGetBlockState(WorldBlockPosRel(side_pos, dir_on_side));
            int power = get_conducted_redstone_power(
                    state, get_opposite_direction(dir_on_side),
                    to_wire, ignore_wires);
            res = MAX(res, power);
        }
    }
    return res;
}

static int
is_redstone_wire_connected(WorldBlockPos pos, block_state_info * info) {
    WorldBlockPos pos_above = WorldBlockPosRel(pos, DIRECTION_POS_Y);
    u16 state_above = WorldGetBlockState(pos_above);
    int conductor_above = conducts_redstone(state_above, pos_above);

    // order of redstone side entries in block state info struct
    int directions[] = {
        DIRECTION_POS_X, DIRECTION_NEG_Z, DIRECTION_POS_Z, DIRECTION_NEG_X,
    };

    for (int i = 0; i < 4; i++) {
        int dir = directions[i];
        int opp_dir = get_opposite_direction(dir);
        WorldBlockPos pos_side = WorldBlockPosRel(pos, dir);
        u16 state_side = WorldGetBlockState(pos_side);

        if (!conductor_above) {
            // try to connect diagonally up
            WorldBlockPos dest_pos = WorldBlockPosRel(pos_side, DIRECTION_POS_Y);
            u16 dest_state = WorldGetBlockState(dest_pos);
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
            WorldBlockPos dest_pos = WorldBlockPosRel(pos_side, DIRECTION_NEG_Y);
            u16 dest_state = WorldGetBlockState(dest_pos);
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
update_redstone_wire(WorldBlockPos pos, u16 in_world_state,
        block_state_info * base_info, block_update_context * buc) {
    WorldBlockPos pos_above = WorldBlockPosRel(pos, DIRECTION_POS_Y);
    u16 state_above = WorldGetBlockState(pos_above);
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
        WorldBlockPos pos_side = WorldBlockPosRel(pos, dir);
        u16 state_side = WorldGetBlockState(pos_side);
        i32 type_side = serv->block_type_by_state[state_side];
        block_state_info side_info = describe_block_state(state_side);
        int new_side = REDSTONE_SIDE_NONE;

        if (!conductor_above) {
            // try to connect diagonally up
            WorldBlockPos dest_pos = WorldBlockPosRel(pos_side, DIRECTION_POS_Y);
            u16 dest_state = WorldGetBlockState(dest_pos);
            i32 dest_type = serv->block_type_by_state[dest_state];

            // can only connect diagonally to redstone wire
            if (dest_type == BLOCK_REDSTONE_WIRE) {
                env.connected[i][0] = 1;
                BlockModel model = BlockDetermineSupportModel(state_side);
                if (model.fullFaces & (1 << opp_dir)) {
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
            WorldBlockPos dest_pos = WorldBlockPosRel(pos_side, DIRECTION_NEG_Y);
            u16 dest_state = WorldGetBlockState(dest_pos);
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

    WorldSetBlockState(pos, new_state);

    // @TODO(traks) update direct neighbours and diagonal neighbours in the
    // global update order
    for (int i = 0; i < 4; i++) {
        int dir = directions[i];
        int opp_dir = get_opposite_direction(dir);
        WorldBlockPos side_pos = WorldBlockPosRel(pos, dir);
        WorldBlockPos above_pos = WorldBlockPosRel(side_pos, DIRECTION_POS_Y);
        if (env.connected[i][0]) {
            push_block_update(above_pos, opp_dir, buc);
        }
        WorldBlockPos below_pos = WorldBlockPosRel(side_pos, DIRECTION_NEG_Y);
        if (env.connected[i][2]) {
            push_block_update(below_pos, opp_dir, buc);
        }
    }

    push_direct_neighbour_block_updates(pos, buc);
    return 1;
}

redstone_wire_env
calculate_redstone_wire_env(WorldBlockPos pos, u16 block_state,
        block_state_info * info, int ignore_same_line_power) {
    WorldBlockPos pos_above = WorldBlockPosRel(pos, DIRECTION_POS_Y);
    u16 state_above = WorldGetBlockState(pos_above);
    int conductor_above = conducts_redstone(state_above, pos_above);
    WorldBlockPos pos_below = WorldBlockPosRel(pos, DIRECTION_NEG_Y);
    u16 state_below = WorldGetBlockState(pos_below);
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
        WorldBlockPos pos_side = WorldBlockPosRel(pos, dir);
        u16 state_side = WorldGetBlockState(pos_side);
        i32 type_side = serv->block_type_by_state[state_side];
        block_state_info side_info = describe_block_state(state_side);
        int conductor_side = conducts_redstone(state_side, pos_side);
        int new_side = REDSTONE_SIDE_NONE;

        // first determine what this redstone wire should connect to, and what
        // the power is of diagonal redstone wires

        if (!conductor_above) {
            // try to connect diagonally up
            WorldBlockPos dest_pos = WorldBlockPosRel(pos_side, DIRECTION_POS_Y);
            u16 dest_state = WorldGetBlockState(dest_pos);
            i32 dest_type = serv->block_type_by_state[dest_state];

            // can only connect diagonally to redstone wire
            if (dest_type == BLOCK_REDSTONE_WIRE) {
                block_state_info dest_info = describe_block_state(dest_state);
                env.connected[i][0] = 1;
                BlockModel model = BlockDetermineSupportModel(state_side);
                if (model.fullFaces & (1 << opp_dir)) {
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
            WorldBlockPos dest_pos = WorldBlockPosRel(pos_side, DIRECTION_NEG_Y);
            u16 dest_state = WorldGetBlockState(dest_pos);
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
    WorldBlockPos pos;
    unsigned char distance;
} redstone_wire_pos;

static void
update_redstone_line(WorldBlockPos start_pos) {
    u16 start_state = WorldGetBlockState(start_pos);
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

        WorldBlockPos wires[500];
        int wire_count = 0;
        wires[0] = start_pos;
        wire_count++;

        start_info.power = start_env.power;
        u16 new_start_state = make_block_state(&start_info);
        WorldSetBlockState(start_pos, new_start_state);

        for (int wireIndex = 0; wireIndex < wire_count; wireIndex++) {
            WorldBlockPos wire_pos = wires[wireIndex];
            u16 state = WorldGetBlockState(wire_pos);
            block_state_info info = describe_block_state(state);
            redstone_wire_env env = calculate_redstone_wire_env(
                    wire_pos, state, &info, 0);

            for (int i = 0; i < 4; i++) {
                WorldBlockPos rel = WorldBlockPosRel(wire_pos, directions[i]);
                rel = WorldBlockPosRel(rel, DIRECTION_POS_Y);

                for (int j = 0; j < 3; j++) {
                    if (env.wire_out[i][j]) {
                        u16 out_state = WorldGetBlockState(rel);
                        block_state_info out_info = describe_block_state(out_state);
                        redstone_wire_env out_env = calculate_redstone_wire_env(
                                rel, out_state, &out_info, 0);
                        if (out_env.power > out_info.power) {
                            out_info.power = out_env.power;
                            WorldSetBlockState(rel, make_block_state(&out_info));
                            wires[wire_count] = rel;
                            wire_count++;
                        }
                    }
                    rel = WorldBlockPosRel(rel, DIRECTION_NEG_Y);
                }
            }
        }
    } else {
        // power went down

        redstone_wire_pos wires[500];
        int wire_count = 0;
        wires[0] = (redstone_wire_pos) {.pos = start_pos, .distance = 0};
        wire_count++;

        WorldBlockPos sources[50];
        int source_count = 0;

        redstone_wire_env lineless_env = calculate_redstone_wire_env(
                start_pos, start_state, &start_info, 1);
        if (lineless_env.power > 0) {
            sources[0] = start_pos;
            source_count++;
        }

        for (int wireIndex = 0; wireIndex < wire_count; wireIndex++) {
            WorldBlockPos wire_pos = wires[wireIndex].pos;
            int distance = wires[wireIndex].distance;
            u16 state = WorldGetBlockState(wire_pos);
            block_state_info info = describe_block_state(state);
            redstone_wire_env env = calculate_redstone_wire_env(
                    wire_pos, state, &info, 1);

            for (int i = 0; i < 4; i++) {
                WorldBlockPos rel = WorldBlockPosRel(wire_pos, directions[i]);
                rel = WorldBlockPosRel(rel, DIRECTION_POS_Y);

                for (int j = 0; j < 3; j++) {
                    if (env.wire_out[i][j]) {
                        block_state_info out_info = describe_block_state(WorldGetBlockState(rel));
                        if (out_info.power == start_info.power - distance - 1
                                && out_info.power > 0) {
                            redstone_wire_env out_env = calculate_redstone_wire_env(
                                    wire_pos, state, &info, 1);

                            if (out_env.power > 0) {
                                sources[source_count] = wire_pos;
                                source_count++;
                            }

                            out_info.power = 0;
                            WorldSetBlockState(rel, make_block_state(&out_info));

                            // this neighbour is exactly
                            wires[wire_count] = (redstone_wire_pos) {
                                .pos = rel,
                                .distance = distance + 1
                            };
                            wire_count++;
                        }
                    }
                    rel = WorldBlockPosRel(rel, DIRECTION_NEG_Y);
                }
            }
        }

        for (int wireIndex = 0; wireIndex < wire_count; wireIndex++) {
            WorldBlockPos wire_pos = wires[wireIndex].pos;
            int distance = wires[wireIndex].distance;
            u16 state = WorldGetBlockState(wire_pos);
            block_state_info info = describe_block_state(state);
            int cur_power = info.power;
            redstone_wire_env env = calculate_redstone_wire_env(
                    wire_pos, state, &info, 1);

            info.power = 0;
            WorldSetBlockState(wire_pos, make_block_state(&info));

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
                WorldBlockPos rel = WorldBlockPosRel(wire_pos, directions[i]);
                rel = WorldBlockPosRel(rel, DIRECTION_POS_Y);

                for (int j = 0; j < 3; j++) {
                    if (env.wire_out[i][j]) {
                        block_state_info out_info = describe_block_state(WorldGetBlockState(rel));
                        if (out_info.power > 0) {
                            wires[wire_count] = (redstone_wire_pos) {
                                .pos = rel,
                                .distance = distance + 1
                            };
                            wire_count++;
                        }
                    }
                    rel = WorldBlockPosRel(rel, DIRECTION_NEG_Y);
                }
            }
        }

        for (int i = 0; i < source_count; i++) {
            update_redstone_line(sources[i]);
        }
    }
}

static i32 DoBlockBehaviour(WorldBlockPos pos, int from_direction, int is_delayed, block_update_context * buc, i32 behaviour) {
    u16 cur_state = WorldGetBlockState(pos);
    block_state_info cur_info = describe_block_state(cur_state);
    i32 cur_type = cur_info.blockType;

    WorldBlockPos from_pos = WorldBlockPosRel(pos, from_direction);
    u16 from_state = WorldGetBlockState(from_pos);
    block_state_info from_info = describe_block_state(from_state);
    i32 from_type = from_info.blockType;

    switch (behaviour) {
    case BLOCK_BEHAVIOUR_SNOWY_TOP: {
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
        WorldSetBlockState(pos, new_state);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BEHAVIOUR_NEED_SOIL_BELOW: {
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
    case BLOCK_BEHAVIOUR_NEED_PROPAGULE_ENVIRONMENT: {
        if (from_direction == DIRECTION_NEG_Y) {
            i32 typeBelow = from_type;
            if (!can_propagule_survive_on(typeBelow)) {
                break_block(pos);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        } else if (from_direction == DIRECTION_POS_Y) {
            i32 typeAbove = from_type;
            if (typeAbove != BLOCK_MANGROVE_LEAVES) {
                break_block(pos);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_BEHAVIOUR_FLUID: {
        // TODO(traks): fluid update
        return 0;
    }
    case BLOCK_BEHAVIOUR_BED: {
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

                WorldSetBlockState(pos, new_state);
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

                WorldSetBlockState(pos, new_state);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_BEHAVIOUR_NEED_SOIL_OR_DRY_SOIL_BELOW: {
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
    case BLOCK_BEHAVIOUR_WITHER_ROSE: {
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
    case BLOCK_BEHAVIOUR_NEED_POLE_SUPPORT_BELOW: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        BlockModel support = BlockDetermineSupportModel(from_state);
        if (support.poleFaces & (1 << DIRECTION_POS_Y)) {
            return 0;
        }

        // block below cannot support us
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_BEHIND_HORIZONTAL: {
        if (from_direction != get_opposite_direction(cur_info.horizontal_facing)) {
            return 0;
        }

        BlockModel support = BlockDetermineSupportModel(from_state);
        if (support.fullFaces & (1 << cur_info.horizontal_facing)) {
            return 0;
        }

        // wall block cannot support us
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BEHAVIOUR_STAIRS: {
        if (from_direction == DIRECTION_NEG_Y
                || from_direction == DIRECTION_POS_Y) {
            return 0;
        }

        int cur_shape = cur_info.stairs_shape;
        update_stairs_shape(pos, &cur_info);
        if (cur_shape == cur_info.stairs_shape) {
            return 0;
        }
        WorldSetBlockState(pos, make_block_state(&cur_info));
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BEHAVIOUR_REDSTONE_WIRE: {
        if (from_direction == DIRECTION_NEG_Y) {
            if (!can_redstone_wire_survive_on(from_state)) {
                WorldSetBlockState(pos, 0);
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
    case BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW: {
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
    case BLOCK_BEHAVIOUR_DOOR_MATCH_OTHER_PART: {
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

                WorldSetBlockState(pos, new_state);
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

                WorldSetBlockState(pos, new_state);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            } else {
                BlockModel support = BlockDetermineSupportModel(from_state);
                if (support.fullFaces & (1 << DIRECTION_POS_Y)) {
                    return 0;
                }

                WorldSetBlockState(pos, 0);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_BEHAVIOUR_NEED_PLATE_SUPPORTING_SURFACE_BELOW: {
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
    case BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_ATTACHED: {
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

        BlockModel support = BlockDetermineSupportModel(from_state);
        if (support.fullFaces & (1 << from_direction)) {
            return 0;
        }

        // invalid wall block
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BEHAVIOUR_SNOW_LAYER: {
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
    case BLOCK_BEHAVIOUR_SUGAR_CANE: {
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
    case BLOCK_BEHAVIOUR_FENCE_CONNECT: {
        if (from_direction == DIRECTION_NEG_Y
                || from_direction == DIRECTION_POS_Y) {
            return 0;
        }
        update_fence_shape(pos, &cur_info, from_direction);
        u16 new_state = make_block_state(&cur_info);
        if (new_state == cur_state) {
            return 0;
        }
        WorldSetBlockState(pos, new_state);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BEHAVIOUR_MUSHROOM_BLOCK_CONNECT: {
        if (from_type != cur_type) {
            return 0;
        }

        // connect to neighbouring mushroom block of the same type
        cur_info.values[BLOCK_PROPERTY_NEG_Y + from_direction] = 0;
        u16 new_state = make_block_state(&cur_info);
        if (new_state == cur_state) {
            return 0;
        }
        WorldSetBlockState(pos, new_state);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BEHAVIOUR_PANE_CONNECT: {
        if (from_direction == DIRECTION_NEG_Y
                || from_direction == DIRECTION_POS_Y) {
            return 0;
        }
        update_pane_shape(pos, &cur_info, from_direction);
        u16 new_state = make_block_state(&cur_info);
        if (new_state == cur_state) {
            return 0;
        }
        WorldSetBlockState(pos, new_state);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BEHAVIOUR_FENCE_GATE_CONNECT: {
        int facing = cur_info.horizontal_facing;
        int rotated = rotate_direction_clockwise(facing);
        if (rotated != from_direction && rotated != get_opposite_direction(from_direction)) {
            return 0;
        }

        cur_info.in_wall = 0;
        if (facing == DIRECTION_POS_X || facing == DIRECTION_NEG_X) {
            int neighbour_state_pos = WorldGetBlockState(WorldBlockPosRel(pos, DIRECTION_POS_Z));
            block_state_info neighbour_info_pos = describe_block_state(neighbour_state_pos);
            int neighbour_state_neg = WorldGetBlockState(WorldBlockPosRel(pos, DIRECTION_NEG_Z));
            block_state_info neighbour_info_neg = describe_block_state(neighbour_state_neg);
            if (BlockHasTag(&neighbour_info_pos, BLOCK_TAG_WALL)
                    || BlockHasTag(&neighbour_info_neg, BLOCK_TAG_WALL)) {
                cur_info.in_wall = 1;
            }
        } else {
            // facing along z axis
            int neighbour_state_pos = WorldGetBlockState(WorldBlockPosRel(pos, DIRECTION_POS_X));
            block_state_info neighbour_info_pos = describe_block_state(neighbour_state_pos);
            int neighbour_state_neg = WorldGetBlockState(WorldBlockPosRel(pos, DIRECTION_NEG_X));
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
        WorldSetBlockState(pos, new_state);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BEHAVIOUR_LILY_PAD: {
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
    case BLOCK_BEHAVIOUR_NETHER_WART: {
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
    case BLOCK_BEHAVIOUR_WALL_CONNECT: {
        if (from_direction == DIRECTION_NEG_Y) {
            return 0;
        }
        update_wall_shape(pos, &cur_info, from_direction);
        u16 new_state = make_block_state(&cur_info);
        if (new_state == cur_state) {
            return 0;
        }
        WorldSetBlockState(pos, new_state);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BEHAVIOUR_NEED_NON_AIR_BELOW: {
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
    case BLOCK_BEHAVIOUR_TALL_PLANT: {
        i32 breakState = 0;
        if (get_water_level(cur_state) > 0) {
            block_state_info breakInfo = describe_default_block_state(BLOCK_WATER);
            breakInfo.level_fluid = FLUID_LEVEL_SOURCE;
            breakState = make_block_state(&breakInfo);
        }
        if (cur_info.double_block_half == DOUBLE_BLOCK_HALF_UPPER) {
            if (from_direction == DIRECTION_NEG_Y && (from_type != cur_type
                    || from_info.double_block_half != DOUBLE_BLOCK_HALF_LOWER)) {
                WorldSetBlockState(pos, 0);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        } else {
            if (from_direction == DIRECTION_NEG_Y) {
                // TODO(traks): use plant behaviour for this?
                if (!can_plant_survive_on(from_type)) {
                    WorldSetBlockState(pos, 0);
                    push_direct_neighbour_block_updates(pos, buc);
                    return 1;
                }
            } else if (from_direction == DIRECTION_POS_Y) {
                if (from_type != cur_type
                        || from_info.double_block_half != DOUBLE_BLOCK_HALF_UPPER) {
                    WorldSetBlockState(pos, 0);
                    push_direct_neighbour_block_updates(pos, buc);
                    return 1;
                }
            }
        }
        return 0;
    }
    case BLOCK_BEHAVIOUR_SEA_PICKLE: {
        if (from_direction == DIRECTION_NEG_Y) {
            if (!can_sea_pickle_survive_on(from_state, from_pos)) {
                break_block(pos);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_BEHAVIOUR_BAMBOO_SAPLING: {
        if (from_direction == DIRECTION_NEG_Y) {
            if (!is_bamboo_plantable_on(from_type)) {
                break_block(pos);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        } else if (from_direction == DIRECTION_POS_Y) {
            if (from_type == BLOCK_BAMBOO) {
                u16 new_state = get_default_block_state(BLOCK_BAMBOO);
                WorldSetBlockState(pos, new_state);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_BEHAVIOUR_BAMBOO: {
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
                WorldSetBlockState(pos, new_state);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_BEHAVIOUR_NEED_SOIL_OR_NETHER_SOIL_BELOW: {
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
    }
    case BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_BEHIND: {
        if (from_direction != get_opposite_direction(cur_info.facing)) {
            return 0;
        }

        BlockModel support = BlockDetermineSupportModel(from_state);
        if (support.fullFaces & (1 << cur_info.facing)) {
            return 0;
        }

        // wall block cannot support us
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BEHAVIOUR_NEED_POLE_SUPPORT_ABOVE: {
        if (from_direction != DIRECTION_POS_Y) {
            return 0;
        }

        BlockModel support = BlockDetermineSupportModel(from_state);
        if (support.poleFaces & (1 << DIRECTION_NEG_Y)) {
            return 0;
        }

        // block above cannot support us
        break_block(pos);
        push_direct_neighbour_block_updates(pos, buc);
        return 1;
    }
    case BLOCK_BEHAVIOUR_AZALEA: {
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
    case BLOCK_BEHAVIOUR_BIG_DRIPLEAF: {
        if (from_direction == DIRECTION_NEG_Y) {
            if (!can_azalea_survive_on(from_state)) {
                break_block(pos);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        } else if (from_direction == DIRECTION_POS_Y) {
            if (from_type == cur_type) {
                // TODO(traks): we transform into a stem block with same
                // properties. This is kind of dangerous, because the new block
                // type may not share the same properties as the previous block
                // type, but it should be OK for us here. This should really be
                // converted into a function that initialises new properties to
                // the defaults and gets rid of properties that the new block
                // doesn't have.
                cur_info.blockType = BLOCK_BIG_DRIPLEAF_STEM;
                u16 new_state = make_block_state(&cur_info);
                WorldSetBlockState(pos, new_state);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        }
        return 0;
    }
    case BLOCK_BEHAVIOUR_BIG_DRIPLEAF_STEM: {
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
    case BLOCK_BEHAVIOUR_SMALL_DRIPLEAF: {
        if (cur_info.double_block_half == DOUBLE_BLOCK_HALF_UPPER) {
            if (from_direction == DIRECTION_NEG_Y && (from_type != cur_type
                    || from_info.double_block_half != DOUBLE_BLOCK_HALF_LOWER)) {
                WorldSetBlockState(pos, 0);
                push_direct_neighbour_block_updates(pos, buc);
                return 1;
            }
        } else {
            if (from_direction == DIRECTION_NEG_Y) {
                if (!can_small_dripleaf_survive_at(from_type, cur_state)) {
                    WorldSetBlockState(pos, 0);
                    push_direct_neighbour_block_updates(pos, buc);
                    return 1;
                }
            } else if (from_direction == DIRECTION_POS_Y) {
                if (from_type != cur_type
                        || from_info.double_block_half != DOUBLE_BLOCK_HALF_UPPER) {
                    WorldSetBlockState(pos, 0);
                    push_direct_neighbour_block_updates(pos, buc);
                    return 1;
                }
            }
        }
        return 0;
    }
    default:
         // nothing
         break;
    }
    return 0;
}

// @TODO(traks) can we perhaps get rid of the from_direction for simplicity?
static int
update_block(WorldBlockPos pos, int from_direction, int is_delayed,
        block_update_context * buc) {
    // @TODO(traks) ideally all these chunk lookups and block lookups should be
    // cached to make a single block update as fast as possible. It is after all
    // incredibly easy to create tons of block updates in a single tick.

    u16 curState = WorldGetBlockState(pos);

    // @TODO(traks) drop items if the block is broken

    // @TODO(traks) remove block entity data

    BlockBehaviours behaviours = BlockGetBehaviours(curState);
    i32 res = 0;

    // TODO(traks): should we run all behaviours or stop once a behaviour has
    // changed a block? Problem is that updates will depend on the order in
    // which behaviours are registered.
    for (i32 i = 0; i < behaviours.size; i++) {
        i32 behaviour = behaviours.entries[i];
        i32 changed = DoBlockBehaviour(pos, from_direction, is_delayed, buc, behaviour);
        res += changed;
    }
    return res;
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
        WorldBlockPos pos = sbu.pos;
        update_block(pos, sbu.from_direction, 1, &buc);
    }

    serv->scheduled_block_update_count = sbu_count;

    for (int i = 0; i < buc.update_count; i++) {
        WorldBlockPos pos = buc.blocks_to_update[i].pos;
        int from_direction = buc.blocks_to_update[i].from_direction;
        update_block(pos, from_direction, 0, &buc);
    }
}

void
propagate_block_updates(block_update_context * buc) {
    for (int i = 0; i < buc->update_count; i++) {
        WorldBlockPos pos = buc->blocks_to_update[i].pos;
        int from_direction = buc->blocks_to_update[i].from_direction;
        update_block(pos, from_direction, 0, buc);
    }
}

int
use_block(Entity * player,
        i32 hand, WorldBlockPos clicked_pos, i32 clicked_face,
        float click_offset_x, float click_offset_y, float click_offset_z,
        u8 is_inside, block_update_context * buc) {
    u16 cur_state = WorldGetBlockState(clicked_pos);
    block_state_info cur_info = describe_block_state(cur_state);
    i32 cur_type = cur_info.blockType;

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
            WorldSetBlockState(clicked_pos, new_state);
            push_direct_neighbour_block_updates(clicked_pos, buc);
            return 1;
        } else if (!is_redstone_wire_connected(clicked_pos, &cur_info)) {
            cur_info.redstone_pos_x = REDSTONE_SIDE_NONE;
            cur_info.redstone_pos_z = REDSTONE_SIDE_NONE;
            cur_info.redstone_neg_x = REDSTONE_SIDE_NONE;
            cur_info.redstone_neg_z = REDSTONE_SIDE_NONE;
            u16 new_state = make_block_state(&cur_info);
            WorldSetBlockState(clicked_pos, new_state);
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
    case BLOCK_MANGROVE_SIGN:
    case BLOCK_CRIMSON_SIGN:
    case BLOCK_WARPED_SIGN:
        // @TODO
        return 0;
    case BLOCK_OAK_WALL_SIGN:
    case BLOCK_SPRUCE_WALL_SIGN:
    case BLOCK_BIRCH_WALL_SIGN:
    case BLOCK_ACACIA_WALL_SIGN:
    case BLOCK_JUNGLE_WALL_SIGN:
    case BLOCK_DARK_OAK_WALL_SIGN:
    case BLOCK_MANGROVE_WALL_SIGN:
    case BLOCK_CRIMSON_WALL_SIGN:
    case BLOCK_WARPED_WALL_SIGN:
        // @TODO
        return 0;
    case BLOCK_LEVER: {
        // @TODO play flip sound
        cur_info.powered = !cur_info.powered;
        u16 new_state = make_block_state(&cur_info);
        WorldSetBlockState(clicked_pos, new_state);
        push_direct_neighbour_block_updates(clicked_pos, buc);
        return 1;
    }
    case BLOCK_OAK_DOOR:
    case BLOCK_SPRUCE_DOOR:
    case BLOCK_BIRCH_DOOR:
    case BLOCK_JUNGLE_DOOR:
    case BLOCK_ACACIA_DOOR:
    case BLOCK_DARK_OAK_DOOR:
    case BLOCK_MANGROVE_DOOR:
    case BLOCK_CRIMSON_DOOR:
    case BLOCK_WARPED_DOOR: {
        // @TODO(traks) play opening/closing sound
        cur_info.open = !cur_info.open;
        u16 new_state = make_block_state(&cur_info);
        WorldSetBlockState(clicked_pos, new_state);
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
    case BLOCK_MANGROVE_BUTTON:
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
    case BLOCK_MANGROVE_FENCE:
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
        WorldSetBlockState(clicked_pos, new_state);
        push_direct_neighbour_block_updates(clicked_pos, buc);
        return 1;
    }
    case BLOCK_OAK_TRAPDOOR:
    case BLOCK_SPRUCE_TRAPDOOR:
    case BLOCK_BIRCH_TRAPDOOR:
    case BLOCK_JUNGLE_TRAPDOOR:
    case BLOCK_ACACIA_TRAPDOOR:
    case BLOCK_DARK_OAK_TRAPDOOR:
    case BLOCK_MANGROVE_TRAPDOOR:
    case BLOCK_CRIMSON_TRAPDOOR:
    case BLOCK_WARPED_TRAPDOOR: {
        // @TODO(traks) play opening/closing sound
        cur_info.open = !cur_info.open;
        u16 new_state = make_block_state(&cur_info);
        WorldSetBlockState(clicked_pos, new_state);
        push_direct_neighbour_block_updates(clicked_pos, buc);
        return 1;
    }
    case BLOCK_OAK_FENCE_GATE:
    case BLOCK_SPRUCE_FENCE_GATE:
    case BLOCK_BIRCH_FENCE_GATE:
    case BLOCK_JUNGLE_FENCE_GATE:
    case BLOCK_ACACIA_FENCE_GATE:
    case BLOCK_DARK_OAK_FENCE_GATE:
    case BLOCK_MANGROVE_FENCE_GATE:
    case BLOCK_CRIMSON_FENCE_GATE:
    case BLOCK_WARPED_FENCE_GATE: {
        if (cur_info.open) {
            cur_info.open = 0;
        } else {
            int player_facing = GetPlayerFacing(player);
            if (cur_info.horizontal_facing == get_opposite_direction(player_facing)) {
                cur_info.horizontal_facing = player_facing;
            }

            cur_info.open = 1;
        }

        u16 new_state = make_block_state(&cur_info);
        WorldSetBlockState(clicked_pos, new_state);
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
    case BLOCK_POTTED_MANGROVE_PROPAGULE:
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
        WorldSetBlockState(clicked_pos, new_state);
        push_direct_neighbour_block_updates(clicked_pos, buc);
        return 1;
    }
    case BLOCK_DAYLIGHT_DETECTOR: {
        // @TODO(traks) update output signal
        cur_info.inverted = !cur_info.inverted;
        u16 new_state = make_block_state(&cur_info);
        WorldSetBlockState(clicked_pos, new_state);
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
        item_stack * main = player->slots + player->selected_slot;
        item_stack * off = player->slots + PLAYER_OFF_HAND_SLOT;
        item_stack * used = hand == PLAYER_MAIN_HAND ? main : off;

        if ((player->flags & PLAYER_CAN_BUILD) && used->size == 0 && cur_info.lit) {
            cur_info.lit = 0;
            u16 new_state = make_block_state(&cur_info);
            WorldSetBlockState(clicked_pos, new_state);
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
    case BLOCK_FROGSPAWN: {
        // @TODO
        return 0;
    }
    default:
        // other blocks have no use action
        return 0;
    }
}
