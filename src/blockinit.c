#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "shared.h"

typedef struct {
    f32 edge;
    f32 minA;
    f32 minB;
    f32 maxA;
    f32 maxB;
} PropagationTest;

static PropagationTest BoxToPropagationTest(BoundingBox box, i32 dir) {
    PropagationTest res = {0};
    switch (dir) {
    case DIRECTION_NEG_Y: res = (PropagationTest) {1 - box.minY, box.minX, box.minZ, box.maxX, box.maxZ};
    case DIRECTION_POS_Y: res = (PropagationTest) {box.maxY, box.minX, box.minZ, box.maxX, box.maxZ};
    case DIRECTION_NEG_Z: res = (PropagationTest) {1 - box.minZ, box.minX, box.minY, box.maxX, box.maxY};
    case DIRECTION_POS_Z: res = (PropagationTest) {box.maxZ, box.minX, box.minY, box.maxX, box.maxY};
    case DIRECTION_NEG_X: res = (PropagationTest) {1 - box.minX, box.minY, box.minZ, box.maxY, box.maxZ};
    case DIRECTION_POS_X: res = (PropagationTest) {box.maxX, box.minY, box.minZ, box.maxY, box.maxZ};
    }
    return res;
}

// TODO(traks): most block models are not used for light propagation, because
// most blocks have an empty light blocking model
static i32 BlockLightCanPropagate(i32 fromModelIndex, i32 toModelIndex, i32 dir) {
    BlockModel fromModel = serv->staticBlockModels[fromModelIndex];
    BlockModel toModel = serv->staticBlockModels[toModelIndex];

    i32 testCount = 0;
    PropagationTest tests[16];

    for (i32 i = 0; i < fromModel.size; i++) {
        PropagationTest test = BoxToPropagationTest(fromModel.boxes[i], dir);
        if (test.edge >= 1) {
            tests[testCount++] = test;
        }
    }
    for (i32 i = 0; i < toModel.size; i++) {
        PropagationTest test = BoxToPropagationTest(fromModel.boxes[i], get_opposite_direction(dir));
        if (test.edge >= 1) {
            tests[testCount++] = test;
        }
    }

    f32 curA = 0;
    for (;;) {
        f32 curB = 0;
        f32 nextA = curA;
        for (i32 testIndex = 0; testIndex < testCount; testIndex++) {
            PropagationTest test = tests[testIndex];
            if (test.minA <= curA && test.maxA >= curA && test.minB <= curB && test.maxB >= curB) {
                curB = test.maxB;
                nextA = MIN(nextA, test.maxA);
            }
        }
        if (nextA == curA) {
            break;
        }
    }

    return (curA != 1);
}

static void CalculateBlockLightPropagation() {
    for (i32 fromModelIndex = 0; fromModelIndex < BLOCK_MODEL_COUNT; fromModelIndex++) {
        for (i32 toModelIndex = 0; toModelIndex < BLOCK_MODEL_COUNT; toModelIndex++) {
            u32 entry = 0;
            for (i32 dir = 0; dir < 6; dir++) {
                i32 canPropagate = BlockLightCanPropagate(fromModelIndex, toModelIndex, dir);
                entry |= (canPropagate << dir);
            }
            serv->lightCanPropagate[fromModelIndex * BLOCK_MODEL_COUNT + toModelIndex] = entry;
        }
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

    // TODO(traks): get rid of VLA?
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
    // TODO(traks): get rid of VLA?
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

static i32 count_block_states(block_properties * props) {
    i32 res = 1;
    for (int i = 0; i < props->property_count; i++) {
        res *= serv->block_property_specs[props->property_specs[i]].value_count;
    }
    return res;
}

static void UpdateGlobalBlockReferences(block_properties * props) {
    i32 blockType = props - serv->block_properties_table;
    i32 blockStates = count_block_states(props);
    assert(props->base_state + blockStates <= (i32) ARRAY_SIZE(serv->block_type_by_state));

    for (int i = 0; i < blockStates; i++) {
        serv->block_type_by_state[props->base_state + i] = blockType;
    }
}

// NOTE(traks): returns < 0 or == 0 or > 0 depending on whether a < b, a == b or
// a > b respectively.
static i32 CompareBlockProperty(String a, String b) {
    i32 minSize = MIN(a.size, b.size);
    for (i32 charIndex = 0; charIndex < minSize; charIndex++) {
        if (a.data[charIndex] != b.data[charIndex]) {
            return (i32) a.data[charIndex] - (i32) b.data[charIndex];
        }
    }
    return a.size - b.size;
}

static void
add_block_property(block_properties * props, int propertyId, char * defaultValue) {
    block_property_spec * spec = serv->block_property_specs + propertyId;
    String propName = {
        .size = spec->tape[0],
        .data = spec->tape + 1,
    };
    String defaultValueString = STR(defaultValue);
    i32 oldStateCount = count_block_states(props);

    // NOTE(traks): figure out index of default value
    // NOTE(traks): tape starts with name size, then name string, and then
    // repeated value size + value string. Skip the name part
    int curTapeIndex = 1 + spec->tape[0];
    int defaultValueIndex = spec->value_count;

    for (i32 valueIndex = 0; valueIndex < spec->value_count; valueIndex++) {
        String value = {
            .size = spec->tape[curTapeIndex],
            .data = spec->tape + curTapeIndex + 1
        };
        if (net_string_equal(defaultValueString, value)) {
            defaultValueIndex = valueIndex;
            break;
        }

        curTapeIndex += value.size + 1;
    }

    assert(defaultValueIndex < spec->value_count);

    // NOTE(traks): Minecraft sorts block properties using the natural sorting
    // order for Java strings. Since all properties have ascii characters, this
    // comes down to ASCII sorting based on character values

    assert(props->property_count < ARRAY_SIZE(props->property_specs));

    // NOTE(traks): grab the index we should register the new property at
    i32 newPropIndex;
    for (newPropIndex = 0; newPropIndex < props->property_count; newPropIndex++) {
        block_property_spec * otherSpec = serv->block_property_specs + props->property_specs[newPropIndex];
        String otherPropName = {
            .size = otherSpec->tape[0],
            .data = otherSpec->tape + 1,
        };
        if (CompareBlockProperty(propName, otherPropName) < 0) {
            break;
        }
    }

    // NOTE(traks): make space for the new property
    for (i32 propIndex = props->property_count; propIndex > newPropIndex; propIndex--) {
        props->property_specs[propIndex] = props->property_specs[propIndex - 1];
        props->default_value_indices[propIndex] = props->default_value_indices[propIndex - 1];
    }

    // NOTE(traks): register the new property for the block
    props->property_specs[newPropIndex] = propertyId;
    props->default_value_indices[newPropIndex] = defaultValueIndex;
    props->property_count++;

    // NOTE(traks): update global info
    i32 newStateCount = count_block_states(props);
    assert(serv->actual_block_state_count == props->base_state + oldStateCount);
    serv->actual_block_state_count = props->base_state + newStateCount;
    UpdateGlobalBlockReferences(props);
}

static block_properties * BeginNextBlock(char * resourceLoc) {
    // NOTE(traks): register the new resource location
    i32 blockType = AddRegistryEntry(&serv->blockRegistry, resourceLoc);

    // NOTE(traks): initialise the properties
    block_properties * res = serv->block_properties_table + blockType;
    res->base_state = serv->actual_block_state_count;
    serv->actual_block_state_count++;
    UpdateGlobalBlockReferences(res);
    return res;
}

static void SetParticularModelForAllStates(block_properties * props, u8 * modelByState, i32 modelId) {
    int block_states = count_block_states(props);
    for (int i = 0; i < block_states; i++) {
        int j = props->base_state + i;
        modelByState[j] = modelId;
    }
}

static void SetCollisionModelForAllStates(block_properties * props, i32 modelId) {
    SetParticularModelForAllStates(props, serv->collisionModelByState, modelId);
}

static void SetSupportModelForAllStates(block_properties * props, i32 modelId) {
    SetParticularModelForAllStates(props, serv->supportModelByState, modelId);
}

// NOTE(traks): for this, some relevant vanilla code is:
// - useShapeForLightOcclusion: false = can just use empty model here
// - getOcclusionShape: shape for occlusion if above = true
static void SetLightBlockingModelForAllStates(block_properties * props, i32 modelId) {
    SetParticularModelForAllStates(props, serv->lightBlockingModelByState, modelId);
}

static void SetAllModelsForAllStatesIndividually(block_properties * props, i32 collisionModelId, i32 supportModelId, i32 lightModelId) {
    SetCollisionModelForAllStates(props, collisionModelId);
    SetSupportModelForAllStates(props, supportModelId);
    SetLightBlockingModelForAllStates(props, lightModelId);
}

static void SetAllModelsForAllStates(block_properties * props, i32 modelId) {
    SetAllModelsForAllStatesIndividually(props, modelId, modelId, modelId);
}

// NOTE(traks): for full blocks it doesn't really matter what the light
// reduction is, because the light will get blocked anyway.
// Relevant vanilla code is:
// - getLightBlock: how much light is blocked
// - propagatesSkylightDown: if max sky light can pass through unchanged
static void SetLightReductionForAllStates(block_properties * props, i32 lightReduction) {
    i32 stateCount = count_block_states(props);
    for (i32 i = 0; i < stateCount; i++) {
        i32 j = props->base_state + i;
        serv->lightReductionByState[j] = lightReduction & 0xf;
    }
}

static void SetLightReductionWhenWaterlogged(block_properties * props) {
    i32 stateCount = count_block_states(props);
    for (i32 i = 0; i < stateCount; i++) {
        i32 blockState = props->base_state + i;
        block_state_info info = describe_block_state(blockState);
        i32 lightReduction = (info.waterlogged ? 1 : 0);
        serv->lightReductionByState[blockState] = lightReduction;
    }
}

// NOTE(traks): Relevant vanilla code is:
// - .lightLevel in Blocks: sets how much light is emitted
static void SetEmittedLightForAllStates(block_properties * props, i32 emittedLight) {
    i32 stateCount = count_block_states(props);
    for (i32 i = 0; i < stateCount; i++) {
        i32 j = props->base_state + i;
        serv->emittedLightByState[j] = emittedLight & 0xf;
    }
}

static void SetEmittedLightWhenLit(block_properties * props, i32 emittedLight) {
    i32 stateCount = count_block_states(props);
    for (i32 i = 0; i < stateCount; i++) {
        i32 blockState = props->base_state + i;
        block_state_info info = describe_block_state(blockState);
        if (info.lit) {
            serv->emittedLightByState[blockState] = emittedLight & 0xf;
        }
    }
}

static void SetEmittedLightWhenBerries(block_properties * props, i32 emittedLight) {
    i32 stateCount = count_block_states(props);
    for (i32 i = 0; i < stateCount; i++) {
        i32 blockState = props->base_state + i;
        block_state_info info = describe_block_state(blockState);
        if (info.berries) {
            serv->emittedLightByState[blockState] = emittedLight & 0xf;
        }
    }
}

static void AddBlockBehaviour(block_properties * props, i32 behaviour) {
    i32 blockType = props - serv->block_properties_table;
    BlockBehaviours * behaviours = serv->blockBehavioursByType + blockType;
    assert(behaviours->size < (i32) ARRAY_SIZE(behaviours->entries));
    behaviours->entries[behaviours->size] = behaviour;
    behaviours->size++;
}

BlockBehaviours BlockGetBehaviours(i32 blockState) {
    i32 blockType = serv->block_type_by_state[blockState];
    BlockBehaviours * behaviours = serv->blockBehavioursByType + blockType;
    return *behaviours;
}

static void
InitSimpleBlockWithModels(char * resource_loc,
        i32 collisionModelId, i32 supportModelId, i32 lightModelId,
        i32 lightReduction, i32 emittedLight) {
    block_properties * props = BeginNextBlock(resource_loc);
    SetCollisionModelForAllStates(props, collisionModelId);
    SetSupportModelForAllStates(props, supportModelId);
    SetLightBlockingModelForAllStates(props, lightModelId);
    SetLightReductionForAllStates(props, lightReduction);
    SetEmittedLightForAllStates(props, emittedLight);
}

static void
init_simple_block(char * resource_loc, int modelId, i32 lightReduction, i32 emittedLight) {
    InitSimpleBlockWithModels(resource_loc, modelId, modelId, modelId, lightReduction, emittedLight);
}

static void InitSimpleEmptyBlock(char * resourceLoc) {
    InitSimpleBlockWithModels(resourceLoc, BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, 0, 0);
}

static void InitSimpleFullBlock(char * resourceLoc) {
    InitSimpleBlockWithModels(resourceLoc, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 15, 0);
}

static void InitSimpleFullEmittingBlock(char * resourceLoc, i32 emittedLight) {
    InitSimpleBlockWithModels(resourceLoc, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 15, emittedLight);
}

static void
init_sapling(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_STAGE, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_SOIL_BELOW);
}

static void
init_propagule(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_STAGE, "0");
    add_block_property(props, BLOCK_PROPERTY_AGE_4, "0");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_HANGING, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_PROPAGULE_ENVIRONMENT);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_pillar(char * resource_loc, i32 emittedLight) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_AXIS, "y");
    SetAllModelsForAllStates(props, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(props, 15);
    SetEmittedLightForAllStates(props, emittedLight);
}

static void
init_leaves(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    props->type_tags |= (u32) 1 << BLOCK_TAG_LEAVES;
    add_block_property(props, BLOCK_PROPERTY_DISTANCE, "7");
    add_block_property(props, BLOCK_PROPERTY_PERSISTENT, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetCollisionModelForAllStates(props, BLOCK_MODEL_FULL);
    SetSupportModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 1);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_bed(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_OCCUPIED, "false");
    add_block_property(props, BLOCK_PROPERTY_BED_PART, "foot");

    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int facing = info.horizontal_facing;
        if (info.bed_part == BED_PART_HEAD) {
            facing = get_opposite_direction(facing);
        }
        int model_index = 0;
        switch (info.horizontal_facing) {
        case DIRECTION_POS_X: model_index = BLOCK_MODEL_BED_FOOT_POS_X; break;
        case DIRECTION_POS_Z: model_index = BLOCK_MODEL_BED_FOOT_POS_Z; break;
        case DIRECTION_NEG_X: model_index = BLOCK_MODEL_BED_FOOT_NEG_X; break;
        case DIRECTION_NEG_Z: model_index = BLOCK_MODEL_BED_FOOT_NEG_Z; break;
        }
        serv->collisionModelByState[block_state] = model_index;
        serv->supportModelByState[block_state] = model_index;
    }

    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_BED);
}

static void
init_slab(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_SLAB_TYPE, "bottom");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int model_index = 0;
        switch (info.slab_type) {
        case SLAB_TOP: model_index = BLOCK_MODEL_TOP_SLAB; break;
        case SLAB_BOTTOM: model_index = BLOCK_MODEL_Y_8; break;
        case SLAB_DOUBLE: model_index = BLOCK_MODEL_FULL; break;
        }
        serv->collisionModelByState[block_state] = model_index;
        serv->supportModelByState[block_state] = model_index;
        serv->lightBlockingModelByState[block_state] = model_index;
    }
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_sign(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_ROTATION_16, "0");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_wall_sign(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
}

static void
InitHangingSign(char * resource_loc) {
    // TODO(traks): models, light reduction, block behaviour, etc.
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_ROTATION_16, "0");
    add_block_property(props, BLOCK_PROPERTY_ATTACHED, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
}

static void
InitWallHangingSign(char * resource_loc) {
    // TODO(traks): models, light reduction, block behaviour, etc.
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
}

static void
init_stair_props(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    props->type_tags |= (u32) 1 << BLOCK_TAG_STAIRS;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_HALF, "bottom");
    add_block_property(props, BLOCK_PROPERTY_STAIRS_SHAPE, "straight");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    // TODO(traks): block models
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_STAIRS);
}

static void
init_tall_plant(char * resource_loc, i32 hasWater) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, hasWater ? 1 : 0);
    if (hasWater) {
        AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
    }
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_TALL_PLANT);
}

static void
init_glazed_terracotta(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStates(props, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(props, 15);
}

static void
init_shulker_box_props(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    props->type_tags |= (u32) 1 << BLOCK_TAG_SHULKER_BOX;
    add_block_property(props, BLOCK_PROPERTY_FACING, "up");
    // TODO(traks): block models
    SetLightReductionForAllStates(props, 1);
}

static void
init_wall_props(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    props->type_tags |= ((u32) 1) << BLOCK_TAG_WALL;
    add_block_property(props, BLOCK_PROPERTY_WALL_POS_X, "none");
    add_block_property(props, BLOCK_PROPERTY_WALL_NEG_Z, "none");
    add_block_property(props, BLOCK_PROPERTY_WALL_POS_Z, "none");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "true");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_WALL_NEG_X, "none");
    // TODO(traks): block models
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_WALL_CONNECT);
}

static void
init_pressure_plate(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_PLATE_SUPPORTING_SURFACE_BELOW);
}

static void
init_pane(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    props->type_tags |= (u32) 1 << BLOCK_TAG_PANE_LIKE;
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int flags = (info.pos_x << 3) | (info.neg_x << 2) | (info.pos_z << 1) | info.neg_z;
        int model_index = BLOCK_MODEL_PANE_CENTRE + flags;
        serv->collisionModelByState[block_state] = model_index;
        serv->supportModelByState[block_state] = model_index;
    }
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_PANE_CONNECT);
}

static void
init_fence(char * resource_loc, int wooden) {
    block_properties * props = BeginNextBlock(resource_loc);
    if (wooden) {
        props->type_tags |= (u32) 1 << BLOCK_TAG_WOODEN_FENCE;
    }
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        int flags = (info.pos_x << 3) | (info.neg_x << 2) | (info.pos_z << 1) | info.neg_z;
        int model_index = BLOCK_MODEL_FENCE_CENTRE + flags;
        serv->collisionModelByState[block_state] = model_index;
        serv->supportModelByState[block_state] = model_index;
    }
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FENCE_CONNECT);
}

static void
init_door_props(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");
    add_block_property(props, BLOCK_PROPERTY_DOOR_HINGE, "left");
    add_block_property(props, BLOCK_PROPERTY_OPEN, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    // TODO(traks): block models
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_DOOR_MATCH_OTHER_PART);
}

static void
init_button(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_ATTACH_FACE, "wall");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_ATTACHED);
}

static void
init_trapdoor_props(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_HALF, "bottom");
    add_block_property(props, BLOCK_PROPERTY_OPEN, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_fence_gate(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    props->type_tags |= (u32) 1 << BLOCK_TAG_FENCE_GATE;
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_IN_WALL, "false");
    add_block_property(props, BLOCK_PROPERTY_OPEN, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
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
        serv->collisionModelByState[block_state] = model_index;
        serv->supportModelByState[block_state] = model_index;
    }
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FENCE_GATE_CONNECT);
}

static void
init_mushroom_block(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_NEG_Y, "true");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "true");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "true");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "true");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "true");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "true");
    SetAllModelsForAllStates(props, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(props, 15);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_MUSHROOM_BLOCK_CONNECT);
}

static void
init_skull_props(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_ROTATION_16, "0");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
}

static void
init_wall_skull_props(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
}

static void
init_anvil_props(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
}

static void
init_banner(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_ROTATION_16, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
}

static void
init_wall_banner(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
}

static void
init_coral(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_coral_fan(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_coral_wall_fan(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_snowy_grassy_block(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_SNOWY, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(props, 15);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_SNOWY_TOP);
}

static void
init_redstone_ore(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(props, 15);
    SetEmittedLightWhenLit(props, 9);
}

static void
init_cauldron(char * resource_loc, int layered, i32 emittedLight) {
    block_properties * props = BeginNextBlock(resource_loc);
    if (layered) {
        add_block_property(props, BLOCK_PROPERTY_LEVEL_CAULDRON, "1");
    }
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    SetEmittedLightForAllStates(props, emittedLight);
}

static void
init_candle(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_CANDLES, "1");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    for (i32 i = 0; i < count_block_states(props); i++) {
        i32 blockState = props->base_state + i;
        block_state_info info = describe_block_state(blockState);
        if (info.lit) {
            serv->emittedLightByState[blockState] = 3 * info.candles;
        }
    }
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_POLE_SUPPORT_BELOW);
}

static void
init_candle_cake(char * resource_loc) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    SetEmittedLightWhenLit(props, 3);
}

static void
init_amethyst_cluster(char * resource_loc, i32 emittedLight) {
    block_properties * props = BeginNextBlock(resource_loc);
    add_block_property(props, BLOCK_PROPERTY_FACING, "up");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    SetEmittedLightForAllStates(props, emittedLight);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_BEHIND);
}

static void InitMultiFaceBlock(char * resourceLoc, i32 emittedLight) {
    block_properties * props = BeginNextBlock(resourceLoc);
    add_block_property(props, BLOCK_PROPERTY_NEG_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);

    for (int i = 0; i < count_block_states(props); i++) {
        i32 blockState = props->base_state + i;
        block_state_info info = describe_block_state(blockState);
        if (info.neg_y || info.pos_y || info.neg_z || info.pos_z || info.neg_x || info.pos_x) {
            serv->emittedLightByState[blockState] = emittedLight;
        }
    }

    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
}

static void InitFlowerPot(char * resourceLoc) {
    InitSimpleBlockWithModels(resourceLoc, BLOCK_MODEL_FLOWER_POT, BLOCK_MODEL_FLOWER_POT, BLOCK_MODEL_EMPTY, 0, 0);
}

static void InitCarpet(char * resourceLoc) {
    block_properties * props = BeginNextBlock(resourceLoc);
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_Y_1, BLOCK_MODEL_Y_1, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_NON_AIR_BELOW);
}

static void InitSimplePlant(char * resourceLoc) {
    block_properties * props = BeginNextBlock(resourceLoc);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_SOIL_BELOW);
}

static void InitSimpleNetherPlant(char * resourceLoc) {
    block_properties * props = BeginNextBlock(resourceLoc);
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_SOIL_OR_NETHER_SOIL_BELOW);
}

static void InitTorch(char * resourceLoc, i32 onWall, i32 emittedLight) {
    block_properties * props = BeginNextBlock(resourceLoc);
    if (onWall) {
        add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    }
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetEmittedLightForAllStates(props, emittedLight);
    if (onWall) {
        AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_BEHIND_HORIZONTAL);
    } else {
        AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_POLE_SUPPORT_BELOW);
    }
}

static void InitAzalea(char * resourceLoc) {
    // TODO(traks): block models
    block_properties * props = BeginNextBlock(resourceLoc);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_AZALEA);
}

static void InitSuspiciousBlock(char * resourceLoc) {
    // TODO(traks): block models, light reduction, etc. This is a block entity
    block_properties * props = BeginNextBlock(resourceLoc);
    add_block_property(props, BLOCK_PROPERTY_DUSTED, "0");
}

static void InitGrate(char * resourceLoc) {
    block_properties * props = BeginNextBlock(resourceLoc);
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
}

static void InitBulb(char * resourceLoc) {
    // TODO(traks): block models, light, behaviours, etc.
    block_properties * props = BeginNextBlock(resourceLoc);
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
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
block_boxes_contain_face(int box_count, BoundingBox * boxes,
        BoundingBox slice, int direction) {
    // intersect boxes with 1x1x1 cube and get the faces
    int face_count = 0;
    // TODO(traks): get rid of VLA?
    block_box_face faces[box_count];
    // @NOTE(traks) All block models are currently aligned to the pixel grid, so
    // the epsilon can be fairly large. Bamboo is aligned to a quarter of a
    // pixel grid, but we currently don't use this algorithm for bamboo.
    float eps = 0.001;

    for (int i = 0; i < box_count; i++) {
        BoundingBox box = boxes[i];
        intersect_test test;

        switch (direction) {
        case DIRECTION_NEG_Y: test = (intersect_test) {box.minX, box.minZ, box.maxX, box.maxZ, box.minY, box.maxY, slice.minY}; break;
        case DIRECTION_POS_Y: test = (intersect_test) {box.minX, box.minZ, box.maxX, box.maxZ, box.minY, box.maxY, slice.maxY}; break;
        case DIRECTION_NEG_Z: test = (intersect_test) {box.minX, box.minY, box.maxX, box.maxY, box.minZ, box.maxZ, slice.minZ}; break;
        case DIRECTION_POS_Z: test = (intersect_test) {box.minX, box.minY, box.maxX, box.maxY, box.minZ, box.maxZ, slice.maxZ}; break;
        case DIRECTION_NEG_X: test = (intersect_test) {box.minY, box.minZ, box.maxY, box.maxZ, box.minX, box.maxX, slice.minX}; break;
        case DIRECTION_POS_X: test = (intersect_test) {box.minY, box.minZ, box.maxY, box.maxZ, box.minX, box.maxX, slice.maxX}; break;
        }

        if (test.axis_min <= test.axis_cut + eps && test.axis_cut <= test.axis_max + eps) {
            faces[face_count] = (block_box_face) {test.min_a, test.min_b, test.max_a, test.max_b};
            face_count++;
        }
    }

    block_box_face test;
    switch (direction) {
    case DIRECTION_NEG_Y: test = (block_box_face) {slice.minX, slice.minZ, slice.maxX, slice.maxZ}; break;
    case DIRECTION_POS_Y: test = (block_box_face) {slice.minX, slice.minZ, slice.maxX, slice.maxZ}; break;
    case DIRECTION_NEG_Z: test = (block_box_face) {slice.minX, slice.minY, slice.maxX, slice.maxY}; break;
    case DIRECTION_POS_Z: test = (block_box_face) {slice.minX, slice.minY, slice.maxX, slice.maxY}; break;
    case DIRECTION_NEG_X: test = (block_box_face) {slice.minY, slice.minZ, slice.maxY, slice.maxZ}; break;
    case DIRECTION_POS_X: test = (block_box_face) {slice.minY, slice.minZ, slice.maxY, slice.maxZ}; break;
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
block_boxes_intersect_face(int box_count, BoundingBox * boxes,
        BoundingBox slice, int direction) {
    switch (direction) {
    case DIRECTION_NEG_Y: slice.maxY = slice.minY; break;
    case DIRECTION_POS_Y: slice.minY = slice.maxY; break;
    case DIRECTION_NEG_Z: slice.maxZ = slice.minZ; break;
    case DIRECTION_POS_Z: slice.minZ = slice.maxZ; break;
    case DIRECTION_NEG_X: slice.maxX = slice.minX; break;
    case DIRECTION_POS_X: slice.minX = slice.maxX; break;
    }

    // check if the selected face intersects any of the boxes
    for (int i = 0; i < box_count; i++) {
        BoundingBox * box = boxes + i;
        if (box->minX <= slice.maxX && box->maxX >= slice.minX
                && box->minY <= slice.maxY && box->maxY >= slice.minY
                && box->minZ <= slice.maxZ && box->maxZ >= slice.minZ) {
            return 1;
        }
    }
    return 0;
}

static void
register_block_model(int index, int box_count, BoundingBox * pixel_boxes) {
    BlockModel * model = serv->staticBlockModels + index;
    assert(box_count < (i32) ARRAY_SIZE(model->boxes));
    model->size = box_count;
    for (int i = 0; i < box_count; i++) {
        model->boxes[i] = pixel_boxes[i];
        model->boxes[i].minX /= 16;
        model->boxes[i].minY /= 16;
        model->boxes[i].minZ /= 16;
        model->boxes[i].maxX /= 16;
        model->boxes[i].maxY /= 16;
        model->boxes[i].maxZ /= 16;
    }

    for (int dir = 0; dir < 6; dir++) {
        BoundingBox full_box = {0, 0, 0, 1, 1, 1};
        if (block_boxes_contain_face(box_count, model->boxes, full_box, dir)) {
            model->fullFaces |= 1 << dir;
        }

        BoundingBox pole = {7.0f / 16, 0, 7.0f / 16, 9.0f / 16, 1, 9.0f / 16};
        if (block_boxes_contain_face(box_count, model->boxes, pole, dir)) {
            model->poleFaces |= 1 << dir;
        }

        if (block_boxes_intersect_face(box_count, model->boxes, full_box, dir)) {
            model->nonEmptyFaces |= 1 << dir;
        }
    }
}

static void RegisterBlockModel(i32 id, BlockModel model) {
    serv->staticBlockModels[id] = model;
}

// @NOTE(traks) all thes rotation functions assume the coordinates of the box
// are in pixel coordinates

static BoundingBox
rotate_block_box_clockwise(BoundingBox box) {
    BoundingBox res = {16 - box.maxZ , box.minY, box.minX, 16 - box.minZ, box.maxY, box.maxX};
    return res;
}

static BoundingBox
rotate_block_box_180(BoundingBox box) {
    return rotate_block_box_clockwise(rotate_block_box_clockwise(box));
}

static BoundingBox
rotate_block_box_counter_clockwise(BoundingBox box) {
    return rotate_block_box_180(rotate_block_box_clockwise(box));
}

static void
register_cross_block_models(int start_index, BoundingBox centre_box,
        BoundingBox neg_z_box, BoundingBox z_box) {
    for (int i = 0; i < 16; i++) {
        int neg_z = (i & 0x1);
        int pos_z = (i & 0x2);
        int neg_x = (i & 0x4);
        int pos_x = (i & 0x8);

        int box_count = 0;
        BoundingBox boxes[2];

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

static void DivideOutCollisionModelPixels(BlockModel * model) {
    for (i32 i = 0; i < model->size; i++) {
        model->boxes[i].minX /= 16;
        model->boxes[i].minY /= 16;
        model->boxes[i].minZ /= 16;
        model->boxes[i].maxX /= 16;
        model->boxes[i].maxY /= 16;
        model->boxes[i].maxZ /= 16;
    }
}

void
// NOTE(traks): This function takes 6 seconds to compile under Clang's O3 for
// me, disable optimisations for now. This function only runs once anyway
#if defined(__clang__)
__attribute__ ((optnone))
#endif
init_block_data(void) {
    Registry * registry = &serv->blockRegistry;
    SetRegistryName(registry, "minecraft:block");

    register_block_model(BLOCK_MODEL_EMPTY, 0, NULL);
    // @NOTE(traks) this initialises the full block model
    for (int y = 1; y <= 16; y++) {
        BoundingBox box = {0, 0, 0, 16, y, 16};
        register_block_model(BLOCK_MODEL_EMPTY + y, 1, &box);
    }
    BoundingBox flower_pot_box = {5, 0, 5, 11, 6, 11};
    register_block_model(BLOCK_MODEL_FLOWER_POT, 1, &flower_pot_box);
    BoundingBox cactus_box = {1, 0, 1, 15, 15, 15};
    register_block_model(BLOCK_MODEL_CACTUS, 1, &cactus_box);
    BoundingBox composter_boxes[] = {
        {0, 0, 0, 16, 2, 16}, // bottom
        {0, 0, 0, 2, 16, 16}, // wall neg x
        {0, 0, 0, 16, 16, 2}, // wall neg z
        {14, 0, 0, 16, 16, 16}, // wall pos x
        {0, 0, 14, 16, 16, 16}, // wall pos z
    };
    register_block_model(BLOCK_MODEL_COMPOSTER, ARRAY_SIZE(composter_boxes), composter_boxes);
    BoundingBox honey_box = {1, 0, 1, 15, 15, 15};
    register_block_model(BLOCK_MODEL_HONEY_BLOCK, 1, &honey_box);
    BoundingBox fence_gate_facing_x_box = {6, 0, 0, 10, 24, 16};
    BoundingBox fence_gate_facing_z_box = {0, 0, 6, 16, 24, 10};
    register_block_model(BLOCK_MODEL_FENCE_GATE_FACING_X, 1, &fence_gate_facing_x_box);
    register_block_model(BLOCK_MODEL_FENCE_GATE_FACING_Z, 1, &fence_gate_facing_z_box);

    BlockModel centredBambooModel = {
        .size = 1,
        .nonEmptyFaces = (1 << DIRECTION_NEG_Y) | (1 << DIRECTION_POS_Y),
        .boxes = {{6.5f, 0, 6.5f, 9.5f, 16, 9.5f}}
    };
    DivideOutCollisionModelPixels(&centredBambooModel);
    RegisterBlockModel(BLOCK_MODEL_CENTRED_BAMBOO, centredBambooModel);

    BoundingBox pane_centre_box = {7, 0, 7, 9, 16, 9};
    BoundingBox pane_neg_z_box = {7, 0, 0, 9, 16, 9};
    BoundingBox pane_z_box = {7, 0, 0, 9, 16, 16};
    register_cross_block_models(BLOCK_MODEL_PANE_CENTRE, pane_centre_box,
            pane_neg_z_box, pane_z_box);

    BoundingBox fence_centre_box = {6, 0, 6, 10, 24, 10};
    BoundingBox fence_neg_z_box = {6, 0, 0, 10, 24, 10};
    BoundingBox fence_z_box = {6, 0, 0, 10, 24, 16};
    register_cross_block_models(BLOCK_MODEL_FENCE_CENTRE, fence_centre_box,
            fence_neg_z_box, fence_z_box);

    BoundingBox boxes_foot_pos_x[] = {
        {0, 3, 0, 16, 9, 16}, // horizontal part
        {0, 0, 0, 3, 3, 3}, // leg 1
        {0, 0, 13, 3, 3, 16}, // leg 2
    };
    for (int rot = 0; rot < 4; rot++) {
        int model_index = BLOCK_MODEL_BED_FOOT_POS_X + rot;
        register_block_model(model_index, ARRAY_SIZE(boxes_foot_pos_x), boxes_foot_pos_x);
        for (int boxIndex = 0; boxIndex < (i32) ARRAY_SIZE(boxes_foot_pos_x); boxIndex++) {
            boxes_foot_pos_x[boxIndex] = rotate_block_box_clockwise(boxes_foot_pos_x[boxIndex]);
        }
    }

    BoundingBox lectern_boxes[] = {
        {0, 0, 0, 16, 2, 16}, // base
        {4, 2, 4, 12, 14, 12}, // post
    };
    register_block_model(BLOCK_MODEL_LECTERN, ARRAY_SIZE(lectern_boxes), lectern_boxes);
    BoundingBox slab_top_box = {0, 8, 0, 16, 16, 16};
    register_block_model(BLOCK_MODEL_TOP_SLAB, 1, &slab_top_box);
    BoundingBox lily_pad_box = {1, 0, 1, 15, 1.5f, 15};
    register_block_model(BLOCK_MODEL_LILY_PAD, 1, &lily_pad_box);
    BoundingBox scaffoldingBoxes[] = {
        {0, 14, 0, 16, 16, 16}, // top part
        {0, 0, 0, 2, 16, 2}, // leg 1
        {14, 0, 0, 16, 16, 2}, // leg 2
        {0, 0, 14, 2, 16, 16}, // leg 3
        {14, 0, 14, 16, 16, 16}, // leg 4
    };
    register_block_model(BLOCK_MODEL_SCAFFOLDING, 5, scaffoldingBoxes);

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
    register_bool_property(BLOCK_PROPERTY_TIP, "tip");
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
    register_bool_property(BLOCK_PROPERTY_BERRIES, "berries");
    register_bool_property(BLOCK_PROPERTY_BLOOM, "bloom");
    register_bool_property(BLOCK_PROPERTY_SHRIEKING, "shrieking");
    register_bool_property(BLOCK_PROPERTY_CAN_SUMMON, "can_summon");
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
    register_range_property(BLOCK_PROPERTY_FLOWER_AMOUNT, "flower_amount", 1, 4);
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
    register_range_property(BLOCK_PROPERTY_AGE_4, "age", 0, 4);
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
    register_property_v(BLOCK_PROPERTY_NOTEBLOCK_INSTRUMENT, "instrument", 23, "harp", "basedrum", "snare", "hat", "bass", "flute", "bell", "guitar", "chime", "xylophone", "iron_xylophone", "cow_bell", "didgeridoo", "bit", "banjo", "pling", "zombie", "skeleton", "creeper", "dragon", "wither_skeleton", "piglin", "custom_head");
    register_property_v(BLOCK_PROPERTY_PISTON_TYPE, "type", 2, "normal", "sticky");
    register_property_v(BLOCK_PROPERTY_SLAB_TYPE, "type", 3, "top", "bottom", "double");
    register_property_v(BLOCK_PROPERTY_STAIRS_SHAPE, "shape", 5, "straight", "inner_left", "inner_right", "outer_left", "outer_right");
    register_property_v(BLOCK_PROPERTY_STRUCTUREBLOCK_MODE, "mode", 4, "save", "load", "corner", "data");
    register_property_v(BLOCK_PROPERTY_BAMBOO_LEAVES, "leaves", 3, "none", "small", "large");
    register_property_v(BLOCK_PROPERTY_DRIPLEAF_TILT, "tilt", 4, "none", "unstable", "partial", "full");
    register_property_v(BLOCK_PROPERTY_VERTICAL_DIRECTION, "vertical_direction", 2, "up", "down");
    register_property_v(BLOCK_PROPERTY_DRIPSTONE_THICKNESS, "thickness", 5, "tip_merge", "tip", "frustum", "middle", "base");
    register_property_v(BLOCK_PROPERTY_SCULK_SENSOR_PHASE, "sculk_sensor_phase", 3, "inactive", "active", "cooldown");
    register_bool_property(BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_0_OCCUPIED, "slot_0_occupied");
    register_bool_property(BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_1_OCCUPIED, "slot_1_occupied");
    register_bool_property(BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_2_OCCUPIED, "slot_2_occupied");
    register_bool_property(BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_3_OCCUPIED, "slot_3_occupied");
    register_bool_property(BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_4_OCCUPIED, "slot_4_occupied");
    register_bool_property(BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_5_OCCUPIED, "slot_5_occupied");
    register_range_property(BLOCK_PROPERTY_DUSTED, "dusted", 0, 3);
    register_bool_property(BLOCK_PROPERTY_CRACKED, "cracked");
    register_bool_property(BLOCK_PROPERTY_CRAFTING, "crafting");
    register_property_v(BLOCK_PROPERTY_TRIAL_SPAWNER_STATE, "trial_spawner_state", 6, "inactive", "waiting_for_players", "active", "waiting_for_reward_ejection", "ejecting_reward", "cooldown");
    register_property_v(BLOCK_PROPERTY_VAULT_STATE, "vault_state", 4, "inactive", "active", "unlocking", "ejecting");
    register_property_v(BLOCK_PROPERTY_CREAKING, "creaking", 3, "disabled", "dormant", "active");
    register_bool_property(BLOCK_PROPERTY_OMINOUS, "ominous");

    block_properties * props;

    InitSimpleEmptyBlock("minecraft:air");
    InitSimpleFullBlock("minecraft:stone");
    InitSimpleFullBlock("minecraft:granite");
    InitSimpleFullBlock("minecraft:polished_granite");
    InitSimpleFullBlock("minecraft:diorite");
    InitSimpleFullBlock("minecraft:polished_diorite");
    InitSimpleFullBlock("minecraft:andesite");
    InitSimpleFullBlock("minecraft:polished_andesite");
    init_snowy_grassy_block("minecraft:grass_block");
    InitSimpleFullBlock("minecraft:dirt");
    InitSimpleFullBlock("minecraft:coarse_dirt");
    init_snowy_grassy_block("minecraft:podzol");
    InitSimpleFullBlock("minecraft:cobblestone");
    InitSimpleFullBlock("minecraft:oak_planks");
    InitSimpleFullBlock("minecraft:spruce_planks");
    InitSimpleFullBlock("minecraft:birch_planks");
    InitSimpleFullBlock("minecraft:jungle_planks");
    InitSimpleFullBlock("minecraft:acacia_planks");
    InitSimpleFullBlock("minecraft:cherry_planks");
    InitSimpleFullBlock("minecraft:dark_oak_planks");
    init_pillar("minecraft:pale_oak_wood", 0);
    InitSimpleFullBlock("minecraft:pale_oak_planks");
    InitSimpleFullBlock("minecraft:mangrove_planks");
    InitSimpleFullBlock("minecraft:bamboo_planks");
    InitSimpleFullBlock("minecraft:bamboo_mosaic");
    init_sapling("minecraft:oak_sapling");
    init_sapling("minecraft:spruce_sapling");
    init_sapling("minecraft:birch_sapling");
    init_sapling("minecraft:jungle_sapling");
    init_sapling("minecraft:acacia_sapling");
    init_sapling("minecraft:cherry_sapling");
    init_sapling("minecraft:dark_oak_sapling");
    init_sapling("minecraft:pale_oak_sapling");
    init_propagule("minecraft:mangrove_propagule");
    InitSimpleFullBlock("minecraft:bedrock");

    // @TODO(traks) slower movement in fluids
    props = BeginNextBlock("minecraft:water");
    add_block_property(props, BLOCK_PROPERTY_LEVEL, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 1);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    // @TODO(traks) slower movement in fluids
    props = BeginNextBlock("minecraft:lava");
    add_block_property(props, BLOCK_PROPERTY_LEVEL, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 1);
    SetEmittedLightForAllStates(props, 15);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    InitSimpleFullBlock("minecraft:sand");

    // TODO(traks): block models, light reduction, etc. This is a block entity
    InitSuspiciousBlock("minecraft:suspicious_sand");
    InitSimpleFullBlock("minecraft:red_sand");
    InitSimpleFullBlock("minecraft:gravel");
    InitSuspiciousBlock("minecraft:suspicious_gravel");
    InitSimpleFullBlock("minecraft:gold_ore");
    InitSimpleFullBlock("minecraft:deepslate_gold_ore");
    InitSimpleFullBlock("minecraft:iron_ore");
    InitSimpleFullBlock("minecraft:deepslate_iron_ore");
    InitSimpleFullBlock("minecraft:coal_ore");
    InitSimpleFullBlock("minecraft:deepslate_coal_ore");
    InitSimpleFullBlock("minecraft:nether_gold_ore");
    init_pillar("minecraft:oak_log", 0);
    init_pillar("minecraft:spruce_log", 0);
    init_pillar("minecraft:birch_log", 0);
    init_pillar("minecraft:jungle_log", 0);
    init_pillar("minecraft:acacia_log", 0);
    init_pillar("minecraft:cherry_log", 0);
    init_pillar("minecraft:dark_oak_log", 0);
    init_pillar("minecraft:pale_oak_log", 0);
    init_pillar("minecraft:mangrove_log", 0);

    props = BeginNextBlock("minecraft:mangrove_roots");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    init_pillar("minecraft:muddy_mangrove_roots", 0);
    init_pillar("minecraft:bamboo_block", 0);
    init_pillar("minecraft:stripped_spruce_log", 0);
    init_pillar("minecraft:stripped_birch_log", 0);
    init_pillar("minecraft:stripped_jungle_log", 0);
    init_pillar("minecraft:stripped_acacia_log", 0);
    init_pillar("minecraft:stripped_cherry_log", 0);
    init_pillar("minecraft:stripped_dark_oak_log", 0);
    init_pillar("minecraft:stripped_pale_oak_log", 0);
    init_pillar("minecraft:stripped_oak_log", 0);
    init_pillar("minecraft:stripped_mangrove_log", 0);
    init_pillar("minecraft:stripped_bamboo_block", 0);
    init_pillar("minecraft:oak_wood", 0);
    init_pillar("minecraft:spruce_wood", 0);
    init_pillar("minecraft:birch_wood", 0);
    init_pillar("minecraft:jungle_wood", 0);
    init_pillar("minecraft:acacia_wood", 0);
    init_pillar("minecraft:cherry_wood", 0);
    init_pillar("minecraft:dark_oak_wood", 0);
    init_pillar("minecraft:mangrove_wood", 0);
    init_pillar("minecraft:stripped_oak_wood", 0);
    init_pillar("minecraft:stripped_spruce_wood", 0);
    init_pillar("minecraft:stripped_birch_wood", 0);
    init_pillar("minecraft:stripped_jungle_wood", 0);
    init_pillar("minecraft:stripped_acacia_wood", 0);
    init_pillar("minecraft:stripped_cherry_wood", 0);
    init_pillar("minecraft:stripped_dark_oak_wood", 0);
    init_pillar("minecraft:stripped_pale_oak_wood", 0);
    init_pillar("minecraft:stripped_mangrove_wood", 0);
    init_leaves("minecraft:oak_leaves");
    init_leaves("minecraft:spruce_leaves");
    init_leaves("minecraft:birch_leaves");
    init_leaves("minecraft:jungle_leaves");
    init_leaves("minecraft:acacia_leaves");
    init_leaves("minecraft:cherry_leaves");
    init_leaves("minecraft:dark_oak_leaves");
    init_leaves("minecraft:pale_oak_leaves");
    init_leaves("minecraft:mangrove_leaves");
    init_leaves("minecraft:azalea_leaves");
    init_leaves("minecraft:flowering_azalea_leaves");
    InitSimpleFullBlock("minecraft:sponge");
    InitSimpleFullBlock("minecraft:wet_sponge");
    InitSimpleFullBlock("minecraft:glass");
    InitSimpleFullBlock("minecraft:lapis_ore");
    InitSimpleFullBlock("minecraft:deepslate_lapis_ore");
    InitSimpleFullBlock("minecraft:lapis_block");

    props = BeginNextBlock("minecraft:dispenser");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_TRIGGERED, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleFullBlock("minecraft:sandstone");
    InitSimpleFullBlock("minecraft:chiseled_sandstone");
    InitSimpleFullBlock("minecraft:cut_sandstone");

    props = BeginNextBlock("minecraft:note_block");
    add_block_property(props, BLOCK_PROPERTY_NOTEBLOCK_INSTRUMENT, "harp");
    add_block_property(props, BLOCK_PROPERTY_NOTE, "0");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

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

    props = BeginNextBlock("minecraft:powered_rail");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "north_south");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    props = BeginNextBlock("minecraft:detector_rail");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "north_south");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:sticky_piston");
    add_block_property(props, BLOCK_PROPERTY_EXTENDED, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");

    // @TODO(traks) slow down entities in cobwebs
    init_simple_block("minecraft:cobweb", BLOCK_MODEL_EMPTY, 1, 0);
    InitSimplePlant("minecraft:short_grass");
    InitSimplePlant("minecraft:fern");

    props = BeginNextBlock("minecraft:dead_bush");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_SOIL_OR_DRY_SOIL_BELOW);

    props = BeginNextBlock("minecraft:seagrass");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 1);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    init_tall_plant("minecraft:tall_seagrass", 1);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:piston");
    add_block_property(props, BLOCK_PROPERTY_EXTENDED, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:piston_head");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_SHORT_PISTON, "false");
    add_block_property(props, BLOCK_PROPERTY_PISTON_TYPE, "normal");

    InitSimpleFullBlock("minecraft:white_wool");
    InitSimpleFullBlock("minecraft:orange_wool");
    InitSimpleFullBlock("minecraft:magenta_wool");
    InitSimpleFullBlock("minecraft:light_blue_wool");
    InitSimpleFullBlock("minecraft:yellow_wool");
    InitSimpleFullBlock("minecraft:lime_wool");
    InitSimpleFullBlock("minecraft:pink_wool");
    InitSimpleFullBlock("minecraft:gray_wool");
    InitSimpleFullBlock("minecraft:light_gray_wool");
    InitSimpleFullBlock("minecraft:cyan_wool");
    InitSimpleFullBlock("minecraft:purple_wool");
    InitSimpleFullBlock("minecraft:blue_wool");
    InitSimpleFullBlock("minecraft:brown_wool");
    InitSimpleFullBlock("minecraft:green_wool");
    InitSimpleFullBlock("minecraft:red_wool");
    InitSimpleFullBlock("minecraft:black_wool");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:moving_piston");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_PISTON_TYPE, "normal");

    InitSimplePlant("minecraft:dandelion");
    InitSimplePlant("minecraft:torchflower");
    InitSimplePlant("minecraft:poppy");
    InitSimplePlant("minecraft:blue_orchid");
    InitSimplePlant("minecraft:allium");
    InitSimplePlant("minecraft:azure_bluet");
    InitSimplePlant("minecraft:red_tulip");
    InitSimplePlant("minecraft:orange_tulip");
    InitSimplePlant("minecraft:white_tulip");
    InitSimplePlant("minecraft:pink_tulip");
    InitSimplePlant("minecraft:oxeye_daisy");
    InitSimplePlant("minecraft:cornflower");

    props = BeginNextBlock("minecraft:wither_rose");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_WITHER_ROSE);

    InitSimplePlant("minecraft:lily_of_the_valley");
    init_simple_block("minecraft:brown_mushroom", BLOCK_MODEL_EMPTY, 0, 1);
    InitSimpleEmptyBlock("minecraft:red_mushroom");
    InitSimpleFullBlock("minecraft:gold_block");
    InitSimpleFullBlock("minecraft:iron_block");
    InitSimpleFullBlock("minecraft:bricks");

    props = BeginNextBlock("minecraft:tnt");
    add_block_property(props, BLOCK_PROPERTY_UNSTABLE, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleFullBlock("minecraft:bookshelf");

    // TODO(traks): block model, light reduction, etc. This is a block entity
    props = BeginNextBlock("minecraft:chiseled_bookshelf");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_0_OCCUPIED, "false");
    add_block_property(props, BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_1_OCCUPIED, "false");
    add_block_property(props, BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_2_OCCUPIED, "false");
    add_block_property(props, BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_3_OCCUPIED, "false");
    add_block_property(props, BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_4_OCCUPIED, "false");
    add_block_property(props, BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_5_OCCUPIED, "false");

    InitSimpleFullBlock("minecraft:mossy_cobblestone");
    InitSimpleFullBlock("minecraft:obsidian");

    InitTorch("minecraft:torch", 0, 14);
    InitTorch("minecraft:wall_torch", 1, 14);

    props = BeginNextBlock("minecraft:fire");
    add_block_property(props, BLOCK_PROPERTY_AGE_15, "0");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    SetEmittedLightForAllStates(props, 15);

    // @TODO(traks) do damage in fire
    init_simple_block("minecraft:soul_fire", BLOCK_MODEL_EMPTY, 0, 10);
    InitSimpleBlockWithModels("minecraft:spawner", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 1, 0);

    // TODO(traks): block models + light reduction, etc.
    props = BeginNextBlock("minecraft:creaking_heart");
    add_block_property(props, BLOCK_PROPERTY_AXIS, "y");
    add_block_property(props, BLOCK_PROPERTY_CREAKING, "disabled");

    // TODO(traks): block models + light reduction
    init_stair_props("minecraft:oak_stairs");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:chest");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_CHEST_TYPE, "single");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    props = BeginNextBlock("minecraft:redstone_wire");
    add_block_property(props, BLOCK_PROPERTY_REDSTONE_POS_X, "none");
    add_block_property(props, BLOCK_PROPERTY_REDSTONE_NEG_Z, "none");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    add_block_property(props, BLOCK_PROPERTY_REDSTONE_POS_Z, "none");
    add_block_property(props, BLOCK_PROPERTY_REDSTONE_NEG_X, "none");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_REDSTONE_WIRE);

    InitSimpleFullBlock("minecraft:diamond_ore");
    InitSimpleFullBlock("minecraft:deepslate_diamond_ore");
    InitSimpleFullBlock("minecraft:diamond_block");
    InitSimpleFullBlock("minecraft:crafting_table");

    props = BeginNextBlock("minecraft:wheat");
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    props = BeginNextBlock("minecraft:farmland");
    add_block_property(props, BLOCK_PROPERTY_MOISTURE, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_Y_15);
    SetLightReductionForAllStates(props, 0);

    props = BeginNextBlock("minecraft:furnace");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);
    SetEmittedLightWhenLit(props, 13);

    init_sign("minecraft:oak_sign");
    init_sign("minecraft:spruce_sign");
    init_sign("minecraft:birch_sign");
    init_sign("minecraft:acacia_sign");
    init_sign("minecraft:cherry_sign");
    init_sign("minecraft:jungle_sign");
    init_sign("minecraft:dark_oak_sign");
    init_sign("minecraft:pale_oak_sign");
    init_sign("minecraft:mangrove_sign");
    init_sign("minecraft:bamboo_sign");

    // TODO(traks): block models + light reduction
    init_door_props("minecraft:oak_door");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:ladder");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_BEHIND_HORIZONTAL);

    props = BeginNextBlock("minecraft:rail");
    add_block_property(props, BLOCK_PROPERTY_RAIL_SHAPE, "north_south");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    init_stair_props("minecraft:cobblestone_stairs");

    init_wall_sign("minecraft:oak_wall_sign");
    init_wall_sign("minecraft:spruce_wall_sign");
    init_wall_sign("minecraft:birch_wall_sign");
    init_wall_sign("minecraft:acacia_wall_sign");
    init_wall_sign("minecraft:cherry_wall_sign");
    init_wall_sign("minecraft:jungle_wall_sign");
    init_wall_sign("minecraft:dark_oak_wall_sign");
    init_wall_sign("minecraft:pale_oak_wall_sign");
    init_wall_sign("minecraft:mangrove_wall_sign");
    init_wall_sign("minecraft:bamboo_wall_sign");

    InitHangingSign("minecraft:oak_hanging_sign");
    InitHangingSign("minecraft:spruce_hanging_sign");
    InitHangingSign("minecraft:birch_hanging_sign");
    InitHangingSign("minecraft:acacia_hanging_sign");
    InitHangingSign("minecraft:cherry_hanging_sign");
    InitHangingSign("minecraft:jungle_hanging_sign");
    InitHangingSign("minecraft:dark_oak_hanging_sign");
    InitHangingSign("minecraft:pale_oak_hanging_sign");
    InitHangingSign("minecraft:crimson_hanging_sign");
    InitHangingSign("minecraft:warped_hanging_sign");
    InitHangingSign("minecraft:mangrove_hanging_sign");
    InitHangingSign("minecraft:bamboo_hanging_sign");

    InitWallHangingSign("minecraft:oak_wall_hanging_sign");
    InitWallHangingSign("minecraft:spruce_wall_hanging_sign");
    InitWallHangingSign("minecraft:birch_wall_hanging_sign");
    InitWallHangingSign("minecraft:acacia_wall_hanging_sign");
    InitWallHangingSign("minecraft:cherry_wall_hanging_sign");
    InitWallHangingSign("minecraft:jungle_wall_hanging_sign");
    InitWallHangingSign("minecraft:dark_oak_wall_hanging_sign");
    InitWallHangingSign("minecraft:pale_oak_wall_hanging_sign");
    InitWallHangingSign("minecraft:mangrove_wall_hanging_sign");
    InitWallHangingSign("minecraft:crimson_wall_hanging_sign");
    InitWallHangingSign("minecraft:warped_wall_hanging_sign");
    InitWallHangingSign("minecraft:bamboo_wall_hanging_sign");

    props = BeginNextBlock("minecraft:lever");
    add_block_property(props, BLOCK_PROPERTY_ATTACH_FACE, "wall");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_ATTACHED);

    init_pressure_plate("minecraft:stone_pressure_plate");

    init_door_props("minecraft:iron_door");

    init_pressure_plate("minecraft:oak_pressure_plate");
    init_pressure_plate("minecraft:spruce_pressure_plate");
    init_pressure_plate("minecraft:birch_pressure_plate");
    init_pressure_plate("minecraft:jungle_pressure_plate");
    init_pressure_plate("minecraft:acacia_pressure_plate");
    init_pressure_plate("minecraft:cherry_pressure_plate");
    init_pressure_plate("minecraft:dark_oak_pressure_plate");
    init_pressure_plate("minecraft:pale_oak_pressure_plate");
    init_pressure_plate("minecraft:mangrove_pressure_plate");
    init_pressure_plate("minecraft:bamboo_pressure_plate");

    init_redstone_ore("minecraft:redstone_ore");
    init_redstone_ore("minecraft:deepslate_redstone_ore");

    props = BeginNextBlock("minecraft:redstone_torch");
    add_block_property(props, BLOCK_PROPERTY_LIT, "true");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    SetEmittedLightWhenLit(props, 7);

    props = BeginNextBlock("minecraft:redstone_wall_torch");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "true");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    SetEmittedLightWhenLit(props, 7);

    init_button("minecraft:stone_button");

    props = BeginNextBlock("minecraft:snow");
    add_block_property(props, BLOCK_PROPERTY_LAYERS, "1");
    for (int i = 0; i < count_block_states(props); i++) {
        u16 block_state = props->base_state + i;
        block_state_info info = describe_block_state(block_state);
        i32 collisionModelId = BLOCK_MODEL_EMPTY + (info.layers - 1) * 2;
        i32 supportModelId = BLOCK_MODEL_EMPTY + info.layers * 2;
        serv->collisionModelByState[block_state] = collisionModelId;
        serv->supportModelByState[block_state] = supportModelId;
        serv->lightBlockingModelByState[block_state] = supportModelId;
    }
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_SNOW_LAYER);

    InitSimpleBlockWithModels("minecraft:ice", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 1, 0);
    InitSimpleFullBlock("minecraft:snow_block");

    props = BeginNextBlock("minecraft:cactus");
    add_block_property(props, BLOCK_PROPERTY_AGE_15, "0");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_CACTUS, BLOCK_MODEL_CACTUS, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleFullBlock("minecraft:clay");

    props = BeginNextBlock("minecraft:sugar_cane");
    add_block_property(props, BLOCK_PROPERTY_AGE_15, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_SUGAR_CANE);

    props = BeginNextBlock("minecraft:jukebox");
    add_block_property(props, BLOCK_PROPERTY_HAS_RECORD, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    init_fence("minecraft:oak_fence", 1);

    InitSimpleFullBlock("minecraft:netherrack");
    InitSimpleBlockWithModels("minecraft:soul_sand", BLOCK_MODEL_Y_14, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 15, 0);
    InitSimpleFullBlock("minecraft:soul_soil");
    init_pillar("minecraft:basalt", 0);
    init_pillar("minecraft:polished_basalt", 0);

    InitTorch("minecraft:soul_torch", 0, 10);
    InitTorch("minecraft:soul_wall_torch", 1, 10);

    InitSimpleFullEmittingBlock("minecraft:glowstone", 15);

    props = BeginNextBlock("minecraft:nether_portal");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_AXIS, "x");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    SetEmittedLightForAllStates(props, 11);

    props = BeginNextBlock("minecraft:carved_pumpkin");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    props = BeginNextBlock("minecraft:jack_o_lantern");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);
    SetEmittedLightForAllStates(props, 15);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:cake");
    add_block_property(props, BLOCK_PROPERTY_BITES, "0");

    props = BeginNextBlock("minecraft:repeater");
    add_block_property(props, BLOCK_PROPERTY_DELAY, "1");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LOCKED, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_Y_2, BLOCK_MODEL_Y_2, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleBlockWithModels("minecraft:white_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:orange_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:magenta_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:light_blue_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:yellow_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:lime_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:pink_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:gray_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:light_gray_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:cyan_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:purple_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:blue_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:brown_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:green_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:red_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
    InitSimpleBlockWithModels("minecraft:black_stained_glass", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);

    // @TODO(traks) collision models
    init_trapdoor_props("minecraft:oak_trapdoor");
    init_trapdoor_props("minecraft:spruce_trapdoor");
    init_trapdoor_props("minecraft:birch_trapdoor");
    init_trapdoor_props("minecraft:jungle_trapdoor");
    init_trapdoor_props("minecraft:acacia_trapdoor");
    init_trapdoor_props("minecraft:cherry_trapdoor");
    init_trapdoor_props("minecraft:dark_oak_trapdoor");
    init_trapdoor_props("minecraft:pale_oak_trapdoor");
    init_trapdoor_props("minecraft:mangrove_trapdoor");
    init_trapdoor_props("minecraft:bamboo_trapdoor");

    InitSimpleFullBlock("minecraft:stone_bricks");
    InitSimpleFullBlock("minecraft:mossy_stone_bricks");
    InitSimpleFullBlock("minecraft:cracked_stone_bricks");
    InitSimpleFullBlock("minecraft:chiseled_stone_bricks");

    InitSimpleFullBlock("minecraft:packed_mud");
    InitSimpleFullBlock("minecraft:mud_bricks");

    InitSimpleFullBlock("minecraft:infested_stone");
    InitSimpleFullBlock("minecraft:infested_cobblestone");
    InitSimpleFullBlock("minecraft:infested_stone_bricks");
    InitSimpleFullBlock("minecraft:infested_mossy_stone_bricks");
    InitSimpleFullBlock("minecraft:infested_cracked_stone_bricks");
    InitSimpleFullBlock("minecraft:infested_chiseled_stone_bricks");
    init_mushroom_block("minecraft:brown_mushroom_block");
    init_mushroom_block("minecraft:red_mushroom_block");
    init_mushroom_block("minecraft:mushroom_stem");

    init_pane("minecraft:iron_bars");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:chain");
    add_block_property(props, BLOCK_PROPERTY_AXIS, "y");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    init_pane("minecraft:glass_pane");

    InitSimpleFullBlock("minecraft:pumpkin");
    InitSimpleFullBlock("minecraft:melon");

    props = BeginNextBlock("minecraft:attached_pumpkin_stem");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    props = BeginNextBlock("minecraft:attached_melon_stem");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    props = BeginNextBlock("minecraft:pumpkin_stem");
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    props = BeginNextBlock("minecraft:melon_stem");
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    props = BeginNextBlock("minecraft:vine");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitMultiFaceBlock("minecraft:glow_lichen", 7);

    init_fence_gate("minecraft:oak_fence_gate");

    init_stair_props("minecraft:brick_stairs");
    init_stair_props("minecraft:stone_brick_stairs");
    init_stair_props("minecraft:mud_brick_stairs");

    init_snowy_grassy_block("minecraft:mycelium");

    props = BeginNextBlock("minecraft:lily_pad");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_LILY_PAD, BLOCK_MODEL_LILY_PAD, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_LILY_PAD);

    InitSimpleFullBlock("minecraft:nether_bricks");

    init_fence("minecraft:nether_brick_fence", 0);

    init_stair_props("minecraft:nether_brick_stairs");

    props = BeginNextBlock("minecraft:nether_wart");
    add_block_property(props, BLOCK_PROPERTY_AGE_3, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NETHER_WART);

    InitSimpleBlockWithModels("minecraft:enchanting_table", BLOCK_MODEL_Y_12, BLOCK_MODEL_Y_12, BLOCK_MODEL_Y_12, 1, 7);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:brewing_stand");
    add_block_property(props, BLOCK_PROPERTY_HAS_BOTTLE_0, "false");
    add_block_property(props, BLOCK_PROPERTY_HAS_BOTTLE_1, "false");
    add_block_property(props, BLOCK_PROPERTY_HAS_BOTTLE_2, "false");
    SetEmittedLightForAllStates(props, 1);

    init_cauldron("minecraft:cauldron", 0, 0);
    init_cauldron("minecraft:water_cauldron", 1, 0);
    init_cauldron("minecraft:lava_cauldron", 0, 15);
    init_cauldron("minecraft:powder_snow_cauldron", 1, 0);

    init_simple_block("minecraft:end_portal", BLOCK_MODEL_EMPTY, 0, 15);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:end_portal_frame");
    add_block_property(props, BLOCK_PROPERTY_EYE, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetEmittedLightForAllStates(props, 1);

    InitSimpleFullBlock("minecraft:end_stone");
    // @TODO(traks) correct block model
    InitSimpleBlockWithModels("minecraft:dragon_egg", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 1);

    props = BeginNextBlock("minecraft:redstone_lamp");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:cocoa");
    add_block_property(props, BLOCK_PROPERTY_AGE_2, "0");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");

    init_stair_props("minecraft:sandstone_stairs");

    InitSimpleFullBlock("minecraft:emerald_ore");
    InitSimpleFullBlock("minecraft:deepslate_emerald_ore");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:ender_chest");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetEmittedLightForAllStates(props, 7);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:tripwire_hook");
    add_block_property(props, BLOCK_PROPERTY_ATTACHED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");

    props = BeginNextBlock("minecraft:tripwire");
    add_block_property(props, BLOCK_PROPERTY_ATTACHED, "false");
    add_block_property(props, BLOCK_PROPERTY_DISARMED, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleFullBlock("minecraft:emerald_block");

    init_stair_props("minecraft:spruce_stairs");
    init_stair_props("minecraft:birch_stairs");
    init_stair_props("minecraft:jungle_stairs");

    props = BeginNextBlock("minecraft:command_block");
    add_block_property(props, BLOCK_PROPERTY_CONDITIONAL, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleBlockWithModels("minecraft:beacon", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 1, 15);

    // @TODO(traks) collision models
    init_wall_props("minecraft:cobblestone_wall");
    init_wall_props("minecraft:mossy_cobblestone_wall");

    InitFlowerPot("minecraft:flower_pot");
    InitFlowerPot("minecraft:potted_torchflower");
    InitFlowerPot("minecraft:potted_oak_sapling");
    InitFlowerPot("minecraft:potted_spruce_sapling");
    InitFlowerPot("minecraft:potted_birch_sapling");
    InitFlowerPot("minecraft:potted_jungle_sapling");
    InitFlowerPot("minecraft:potted_acacia_sapling");
    InitFlowerPot("minecraft:potted_cherry_sapling");
    InitFlowerPot("minecraft:potted_dark_oak_sapling");
    InitFlowerPot("minecraft:potted_pale_oak_sapling");
    InitFlowerPot("minecraft:potted_mangrove_propagule");
    InitFlowerPot("minecraft:potted_fern");
    InitFlowerPot("minecraft:potted_dandelion");
    InitFlowerPot("minecraft:potted_poppy");
    InitFlowerPot("minecraft:potted_blue_orchid");
    InitFlowerPot("minecraft:potted_allium");
    InitFlowerPot("minecraft:potted_azure_bluet");
    InitFlowerPot("minecraft:potted_red_tulip");
    InitFlowerPot("minecraft:potted_orange_tulip");
    InitFlowerPot("minecraft:potted_white_tulip");
    InitFlowerPot("minecraft:potted_pink_tulip");
    InitFlowerPot("minecraft:potted_oxeye_daisy");
    InitFlowerPot("minecraft:potted_cornflower");
    InitFlowerPot("minecraft:potted_lily_of_the_valley");
    InitFlowerPot("minecraft:potted_wither_rose");
    InitFlowerPot("minecraft:potted_red_mushroom");
    InitFlowerPot("minecraft:potted_brown_mushroom");
    InitFlowerPot("minecraft:potted_dead_bush");
    InitFlowerPot("minecraft:potted_cactus");

    props = BeginNextBlock("minecraft:carrots");
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    props = BeginNextBlock("minecraft:potatoes");
    add_block_property(props, BLOCK_PROPERTY_AGE_7, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    init_button("minecraft:oak_button");
    init_button("minecraft:spruce_button");
    init_button("minecraft:birch_button");
    init_button("minecraft:jungle_button");
    init_button("minecraft:acacia_button");
    init_button("minecraft:cherry_button");
    init_button("minecraft:dark_oak_button");
    init_button("minecraft:pale_oak_button");
    init_button("minecraft:mangrove_button");
    init_button("minecraft:bamboo_button");

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
    init_skull_props("minecraft:piglin_head");
    init_wall_skull_props("minecraft:piglin_wall_head");

    // @TODO(traks) collision models
    init_anvil_props("minecraft:anvil");
    init_anvil_props("minecraft:chipped_anvil");
    init_anvil_props("minecraft:damaged_anvil");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:trapped_chest");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_CHEST_TYPE, "single");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    props = BeginNextBlock("minecraft:light_weighted_pressure_plate");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_PLATE_SUPPORTING_SURFACE_BELOW);

    props = BeginNextBlock("minecraft:heavy_weighted_pressure_plate");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_PLATE_SUPPORTING_SURFACE_BELOW);

    props = BeginNextBlock("minecraft:comparator");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_MODE_COMPARATOR, "compare");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_Y_2, BLOCK_MODEL_Y_2, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    props = BeginNextBlock("minecraft:daylight_detector");
    add_block_property(props, BLOCK_PROPERTY_INVERTED, "false");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_Y_6);
    SetLightReductionForAllStates(props, 0);

    InitSimpleFullBlock("minecraft:redstone_block");
    InitSimpleFullBlock("minecraft:nether_quartz_ore");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:hopper");
    add_block_property(props, BLOCK_PROPERTY_ENABLED, "true");
    add_block_property(props, BLOCK_PROPERTY_FACING_HOPPER, "down");

    InitSimpleFullBlock("minecraft:quartz_block");
    InitSimpleFullBlock("minecraft:chiseled_quartz_block");

    init_pillar("minecraft:quartz_pillar", 0);

    init_stair_props("minecraft:quartz_stairs");

    props = BeginNextBlock("minecraft:activator_rail");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "north_south");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    props = BeginNextBlock("minecraft:dropper");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_TRIGGERED, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleFullBlock("minecraft:white_terracotta");
    InitSimpleFullBlock("minecraft:orange_terracotta");
    InitSimpleFullBlock("minecraft:magenta_terracotta");
    InitSimpleFullBlock("minecraft:light_blue_terracotta");
    InitSimpleFullBlock("minecraft:yellow_terracotta");
    InitSimpleFullBlock("minecraft:lime_terracotta");
    InitSimpleFullBlock("minecraft:pink_terracotta");
    InitSimpleFullBlock("minecraft:gray_terracotta");
    InitSimpleFullBlock("minecraft:light_gray_terracotta");
    InitSimpleFullBlock("minecraft:cyan_terracotta");
    InitSimpleFullBlock("minecraft:purple_terracotta");
    InitSimpleFullBlock("minecraft:blue_terracotta");
    InitSimpleFullBlock("minecraft:brown_terracotta");
    InitSimpleFullBlock("minecraft:green_terracotta");
    InitSimpleFullBlock("minecraft:red_terracotta");
    InitSimpleFullBlock("minecraft:black_terracotta");

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
    init_stair_props("minecraft:cherry_stairs");
    init_stair_props("minecraft:dark_oak_stairs");
    init_stair_props("minecraft:pale_oak_stairs");
    init_stair_props("minecraft:mangrove_stairs");
    init_stair_props("minecraft:bamboo_stairs");
    init_stair_props("minecraft:bamboo_mosaic_stairs");

    InitSimpleBlockWithModels("minecraft:slime_block", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 1, 0);

    props = BeginNextBlock("minecraft:barrier");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(props);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    props = BeginNextBlock("minecraft:light");
    add_block_property(props, BLOCK_PROPERTY_LEVEL_LIGHT, "15");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    for (i32 i = 0; i < count_block_states(props); i++) {
        i32 blockState = props->base_state + i;
        block_state_info info = describe_block_state(blockState);
        serv->emittedLightByState[blockState] = info.level_light & 0xf;
    }
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    init_trapdoor_props("minecraft:iron_trapdoor");

    InitSimpleFullBlock("minecraft:prismarine");
    InitSimpleFullBlock("minecraft:prismarine_bricks");
    InitSimpleFullBlock("minecraft:dark_prismarine");

    init_stair_props("minecraft:prismarine_stairs");
    init_stair_props("minecraft:prismarine_brick_stairs");
    init_stair_props("minecraft:dark_prismarine_stairs");

    init_slab("minecraft:prismarine_slab");
    init_slab("minecraft:prismarine_brick_slab");
    init_slab("minecraft:dark_prismarine_slab");

    InitSimpleFullEmittingBlock("minecraft:sea_lantern", 15);

    init_pillar("minecraft:hay_block", 0);

    InitCarpet("minecraft:white_carpet");
    InitCarpet("minecraft:orange_carpet");
    InitCarpet("minecraft:magenta_carpet");
    InitCarpet("minecraft:light_blue_carpet");
    InitCarpet("minecraft:yellow_carpet");
    InitCarpet("minecraft:lime_carpet");
    InitCarpet("minecraft:pink_carpet");
    InitCarpet("minecraft:gray_carpet");
    InitCarpet("minecraft:light_gray_carpet");
    InitCarpet("minecraft:cyan_carpet");
    InitCarpet("minecraft:purple_carpet");
    InitCarpet("minecraft:blue_carpet");
    InitCarpet("minecraft:brown_carpet");
    InitCarpet("minecraft:green_carpet");
    InitCarpet("minecraft:red_carpet");
    InitCarpet("minecraft:black_carpet");
    InitSimpleFullBlock("minecraft:terracotta");
    InitSimpleFullBlock("minecraft:coal_block");
    InitSimpleFullBlock("minecraft:packed_ice");

    init_tall_plant("minecraft:sunflower", 0);
    init_tall_plant("minecraft:lilac", 0);
    init_tall_plant("minecraft:rose_bush", 0);
    init_tall_plant("minecraft:peony", 0);
    init_tall_plant("minecraft:tall_grass", 0);
    init_tall_plant("minecraft:large_fern", 0);

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

    InitSimpleFullBlock("minecraft:red_sandstone");
    InitSimpleFullBlock("minecraft:chiseled_red_sandstone");
    InitSimpleFullBlock("minecraft:cut_red_sandstone");

    init_stair_props("minecraft:red_sandstone_stairs");

    init_slab("minecraft:oak_slab");
    init_slab("minecraft:spruce_slab");
    init_slab("minecraft:birch_slab");
    init_slab("minecraft:jungle_slab");
    init_slab("minecraft:acacia_slab");
    init_slab("minecraft:cherry_slab");
    init_slab("minecraft:dark_oak_slab");
    init_slab("minecraft:pale_oak_slab");
    init_slab("minecraft:mangrove_slab");
    init_slab("minecraft:bamboo_slab");
    init_slab("minecraft:bamboo_mosaic_slab");
    init_slab("minecraft:stone_slab");
    init_slab("minecraft:smooth_stone_slab");
    init_slab("minecraft:sandstone_slab");
    init_slab("minecraft:cut_sandstone_slab");
    init_slab("minecraft:petrified_oak_slab");
    init_slab("minecraft:cobblestone_slab");
    init_slab("minecraft:brick_slab");
    init_slab("minecraft:stone_brick_slab");
    init_slab("minecraft:mud_brick_slab");
    init_slab("minecraft:nether_brick_slab");
    init_slab("minecraft:quartz_slab");
    init_slab("minecraft:red_sandstone_slab");
    init_slab("minecraft:cut_red_sandstone_slab");
    init_slab("minecraft:purpur_slab");

    InitSimpleFullBlock("minecraft:smooth_stone");
    InitSimpleFullBlock("minecraft:smooth_sandstone");
    InitSimpleFullBlock("minecraft:smooth_quartz");
    InitSimpleFullBlock("minecraft:smooth_red_sandstone");

    init_fence_gate("minecraft:spruce_fence_gate");
    init_fence_gate("minecraft:birch_fence_gate");
    init_fence_gate("minecraft:jungle_fence_gate");
    init_fence_gate("minecraft:acacia_fence_gate");
    init_fence_gate("minecraft:cherry_fence_gate");
    init_fence_gate("minecraft:dark_oak_fence_gate");
    init_fence_gate("minecraft:pale_oak_fence_gate");
    init_fence_gate("minecraft:mangrove_fence_gate");
    init_fence_gate("minecraft:bamboo_fence_gate");

    init_fence("minecraft:spruce_fence", 1);
    init_fence("minecraft:birch_fence", 1);
    init_fence("minecraft:jungle_fence", 1);
    init_fence("minecraft:acacia_fence", 1);
    init_fence("minecraft:cherry_fence", 1);
    init_fence("minecraft:dark_oak_fence", 1);
    init_fence("minecraft:pale_oak_fence", 1);
    init_fence("minecraft:mangrove_fence", 1);
    init_fence("minecraft:bamboo_fence", 1);

    init_door_props("minecraft:spruce_door");
    init_door_props("minecraft:birch_door");
    init_door_props("minecraft:jungle_door");
    init_door_props("minecraft:acacia_door");
    init_door_props("minecraft:cherry_door");
    init_door_props("minecraft:dark_oak_door");
    init_door_props("minecraft:pale_oak_door");
    init_door_props("minecraft:mangrove_door");
    init_door_props("minecraft:bamboo_door");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:end_rod");
    add_block_property(props, BLOCK_PROPERTY_FACING, "up");
    SetEmittedLightForAllStates(props, 14);

    // TODO(traks): block modesl + light reduction
    props = BeginNextBlock("minecraft:chorus_plant");
    add_block_property(props, BLOCK_PROPERTY_NEG_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_X, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Z, "false");
    add_block_property(props, BLOCK_PROPERTY_POS_Y, "false");
    add_block_property(props, BLOCK_PROPERTY_NEG_X, "false");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:chorus_flower");
    add_block_property(props, BLOCK_PROPERTY_AGE_5, "0");

    InitSimpleFullBlock("minecraft:purpur_block");

    init_pillar("minecraft:purpur_pillar", 0);

    init_stair_props("minecraft:purpur_stairs");

    InitSimpleFullBlock("minecraft:end_stone_bricks");

    props = BeginNextBlock("minecraft:torchflower_crop");
    add_block_property(props, BLOCK_PROPERTY_AGE_1, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    // TODO(traks): behaviours, models, etc.
    props = BeginNextBlock("minecraft:pitcher_crop");
    add_block_property(props, BLOCK_PROPERTY_AGE_4, "0");
    add_block_property(props, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");

    init_tall_plant("minecraft:pitcher_plant", 0);

    props = BeginNextBlock("minecraft:beetroots");
    add_block_property(props, BLOCK_PROPERTY_AGE_3, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    init_simple_block("minecraft:dirt_path", BLOCK_MODEL_Y_15, 0, 0);
    init_simple_block("minecraft:end_gateway", BLOCK_MODEL_EMPTY, 0, 15);

    props = BeginNextBlock("minecraft:repeating_command_block");
    add_block_property(props, BLOCK_PROPERTY_CONDITIONAL, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    props = BeginNextBlock("minecraft:chain_command_block");
    add_block_property(props, BLOCK_PROPERTY_CONDITIONAL, "false");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    props = BeginNextBlock("minecraft:frosted_ice");
    add_block_property(props, BLOCK_PROPERTY_AGE_3, "0");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleFullEmittingBlock("minecraft:magma_block", 3);
    InitSimpleFullBlock("minecraft:nether_wart_block");
    InitSimpleFullBlock("minecraft:red_nether_bricks");

    init_pillar("minecraft:bone_block", 0);

    InitSimpleEmptyBlock("minecraft:structure_void");

    props = BeginNextBlock("minecraft:observer");
    add_block_property(props, BLOCK_PROPERTY_FACING, "south");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

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

    InitSimpleFullBlock("minecraft:white_concrete");
    InitSimpleFullBlock("minecraft:orange_concrete");
    InitSimpleFullBlock("minecraft:magenta_concrete");
    InitSimpleFullBlock("minecraft:light_blue_concrete");
    InitSimpleFullBlock("minecraft:yellow_concrete");
    InitSimpleFullBlock("minecraft:lime_concrete");
    InitSimpleFullBlock("minecraft:pink_concrete");
    InitSimpleFullBlock("minecraft:gray_concrete");
    InitSimpleFullBlock("minecraft:light_gray_concrete");
    InitSimpleFullBlock("minecraft:cyan_concrete");
    InitSimpleFullBlock("minecraft:purple_concrete");
    InitSimpleFullBlock("minecraft:blue_concrete");
    InitSimpleFullBlock("minecraft:brown_concrete");
    InitSimpleFullBlock("minecraft:green_concrete");
    InitSimpleFullBlock("minecraft:red_concrete");
    InitSimpleFullBlock("minecraft:black_concrete");

    InitSimpleFullBlock("minecraft:white_concrete_powder");
    InitSimpleFullBlock("minecraft:orange_concrete_powder");
    InitSimpleFullBlock("minecraft:magenta_concrete_powder");
    InitSimpleFullBlock("minecraft:light_blue_concrete_powder");
    InitSimpleFullBlock("minecraft:yellow_concrete_powder");
    InitSimpleFullBlock("minecraft:lime_concrete_powder");
    InitSimpleFullBlock("minecraft:pink_concrete_powder");
    InitSimpleFullBlock("minecraft:gray_concrete_powder");
    InitSimpleFullBlock("minecraft:light_gray_concrete_powder");
    InitSimpleFullBlock("minecraft:cyan_concrete_powder");
    InitSimpleFullBlock("minecraft:purple_concrete_powder");
    InitSimpleFullBlock("minecraft:blue_concrete_powder");
    InitSimpleFullBlock("minecraft:brown_concrete_powder");
    InitSimpleFullBlock("minecraft:green_concrete_powder");
    InitSimpleFullBlock("minecraft:red_concrete_powder");
    InitSimpleFullBlock("minecraft:black_concrete_powder");

    props = BeginNextBlock("minecraft:kelp");
    add_block_property(props, BLOCK_PROPERTY_AGE_25, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 1);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    props = BeginNextBlock("minecraft:kelp_plant");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 1);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    InitSimpleFullBlock("minecraft:dried_kelp_block");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:turtle_egg");
    add_block_property(props, BLOCK_PROPERTY_EGGS, "1");
    add_block_property(props, BLOCK_PROPERTY_HATCH, "0");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:sniffer_egg");
    add_block_property(props, BLOCK_PROPERTY_HATCH, "0");

    InitSimpleFullBlock("minecraft:dead_tube_coral_block");
    InitSimpleFullBlock("minecraft:dead_brain_coral_block");
    InitSimpleFullBlock("minecraft:dead_bubble_coral_block");
    InitSimpleFullBlock("minecraft:dead_fire_coral_block");
    InitSimpleFullBlock("minecraft:dead_horn_coral_block");

    InitSimpleFullBlock("minecraft:tube_coral_block");
    InitSimpleFullBlock("minecraft:brain_coral_block");
    InitSimpleFullBlock("minecraft:bubble_coral_block");
    InitSimpleFullBlock("minecraft:fire_coral_block");
    InitSimpleFullBlock("minecraft:horn_coral_block");

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
    props = BeginNextBlock("minecraft:sea_pickle");
    add_block_property(props, BLOCK_PROPERTY_PICKLES, "1");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    for (i32 i = 0; i < count_block_states(props); i++) {
        i32 blockState = props->base_state + i;
        block_state_info info = describe_block_state(blockState);
        if (info.waterlogged) {
            serv->emittedLightByState[blockState] = (info.pickles + 1) * 3;
        }
    }
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_SEA_PICKLE);

    InitSimpleFullBlock("minecraft:blue_ice");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:conduit");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "true");
    SetEmittedLightForAllStates(props, 15);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    props = BeginNextBlock("minecraft:bamboo_sapling");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_BAMBOO_SAPLING);

    props = BeginNextBlock("minecraft:bamboo");
    add_block_property(props, BLOCK_PROPERTY_AGE_1, "0");
    add_block_property(props, BLOCK_PROPERTY_BAMBOO_LEAVES, "none");
    add_block_property(props, BLOCK_PROPERTY_STAGE, "0");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_CENTRED_BAMBOO, BLOCK_MODEL_CENTRED_BAMBOO, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_BAMBOO);

    InitFlowerPot("minecraft:potted_bamboo");
    InitSimpleEmptyBlock("minecraft:void_air");
    InitSimpleEmptyBlock("minecraft:cave_air");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:bubble_column");
    add_block_property(props, BLOCK_PROPERTY_DRAG, "true");
    SetLightReductionForAllStates(props, 1);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

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
    init_wall_props("minecraft:mud_brick_wall");
    init_wall_props("minecraft:nether_brick_wall");
    init_wall_props("minecraft:andesite_wall");
    init_wall_props("minecraft:red_nether_brick_wall");
    init_wall_props("minecraft:sandstone_wall");
    init_wall_props("minecraft:end_stone_brick_wall");
    init_wall_props("minecraft:diorite_wall");

    // TODO(traks): block models (collision models & support model
    // (THE scaffolding model)) + light reduction
    props = BeginNextBlock("minecraft:scaffolding");
    add_block_property(props, BLOCK_PROPERTY_BOTTOM, "false");
    add_block_property(props, BLOCK_PROPERTY_STABILITY_DISTANCE, "7");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    props = BeginNextBlock("minecraft:loom");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    props = BeginNextBlock("minecraft:barrel");
    add_block_property(props, BLOCK_PROPERTY_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_OPEN, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    props = BeginNextBlock("minecraft:smoker");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);
    SetEmittedLightWhenLit(props, 13);

    props = BeginNextBlock("minecraft:blast_furnace");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);
    SetEmittedLightWhenLit(props, 13);

    InitSimpleFullBlock("minecraft:cartography_table");
    InitSimpleFullBlock("minecraft:fletching_table");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:grindstone");
    add_block_property(props, BLOCK_PROPERTY_ATTACH_FACE, "wall");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");

    props = BeginNextBlock("minecraft:lectern");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_HAS_BOOK, "false");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_LECTERN);
    SetLightReductionForAllStates(props, 0);

    InitSimpleFullBlock("minecraft:smithing_table");

    props = BeginNextBlock("minecraft:stonecutter");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_Y_9, BLOCK_MODEL_Y_9, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    // TODO(traks): collisions models + light reduction
    props = BeginNextBlock("minecraft:bell");
    add_block_property(props, BLOCK_PROPERTY_BELL_ATTACHMENT, "floor");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:lantern");
    add_block_property(props, BLOCK_PROPERTY_HANGING, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetEmittedLightForAllStates(props, 15);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:soul_lantern");
    add_block_property(props, BLOCK_PROPERTY_HANGING, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetEmittedLightForAllStates(props, 10);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    props = BeginNextBlock("minecraft:campfire");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "true");
    add_block_property(props, BLOCK_PROPERTY_SIGNAL_FIRE, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_Y_7, BLOCK_MODEL_Y_7, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    SetEmittedLightWhenLit(props, 15);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    props = BeginNextBlock("minecraft:soul_campfire");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LIT, "true");
    add_block_property(props, BLOCK_PROPERTY_SIGNAL_FIRE, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_Y_7, BLOCK_MODEL_Y_7, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    SetEmittedLightWhenLit(props, 10);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    props = BeginNextBlock("minecraft:sweet_berry_bush");
    add_block_property(props, BLOCK_PROPERTY_AGE_3, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_SOIL_BELOW);

    init_pillar("minecraft:warped_stem", 0);
    init_pillar("minecraft:stripped_warped_stem", 0);
    init_pillar("minecraft:warped_hyphae", 0);
    init_pillar("minecraft:stripped_warped_hyphae", 0);

    InitSimpleFullBlock("minecraft:warped_nylium");
    InitSimpleNetherPlant("minecraft:warped_fungus");
    InitSimpleFullBlock("minecraft:warped_wart_block");
    InitSimpleNetherPlant("minecraft:warped_roots");
    InitSimpleNetherPlant("minecraft:nether_sprouts");

    init_pillar("minecraft:crimson_stem", 0);
    init_pillar("minecraft:stripped_crimson_stem", 0);
    init_pillar("minecraft:crimson_hyphae", 0);
    init_pillar("minecraft:stripped_crimson_hyphae", 0);

    InitSimpleFullBlock("minecraft:crimson_nylium");
    InitSimpleNetherPlant("minecraft:crimson_fungus");
    InitSimpleFullEmittingBlock("minecraft:shroomlight", 15);

    props = BeginNextBlock("minecraft:weeping_vines");
    add_block_property(props, BLOCK_PROPERTY_AGE_25, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleEmptyBlock("minecraft:weeping_vines_plant");

    props = BeginNextBlock("minecraft:twisting_vines");
    add_block_property(props, BLOCK_PROPERTY_AGE_25, "0");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    InitSimpleEmptyBlock("minecraft:twisting_vines_plant");

    InitSimpleNetherPlant("minecraft:crimson_roots");
    InitSimpleFullBlock("minecraft:crimson_planks");
    InitSimpleFullBlock("minecraft:warped_planks");

    init_slab("minecraft:crimson_slab");
    init_slab("minecraft:warped_slab");

    init_pressure_plate("minecraft:crimson_pressure_plate");
    init_pressure_plate("minecraft:warped_pressure_plate");

    init_fence("minecraft:crimson_fence", 1);
    init_fence("minecraft:warped_fence", 1);

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

    props = BeginNextBlock("minecraft:structure_block");
    add_block_property(props, BLOCK_PROPERTY_STRUCTUREBLOCK_MODE, "save");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    props = BeginNextBlock("minecraft:jigsaw");
    add_block_property(props, BLOCK_PROPERTY_JIGSAW_ORIENTATION, "north_up");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    props = BeginNextBlock("minecraft:composter");
    add_block_property(props, BLOCK_PROPERTY_LEVEL_COMPOSTER, "0");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_COMPOSTER, BLOCK_MODEL_COMPOSTER, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 0);

    props = BeginNextBlock("minecraft:target");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    props = BeginNextBlock("minecraft:bee_nest");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LEVEL_HONEY, "0");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    props = BeginNextBlock("minecraft:beehive");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_LEVEL_HONEY, "0");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);

    InitSimpleBlockWithModels("minecraft:honey_block", BLOCK_MODEL_HONEY_BLOCK, BLOCK_MODEL_HONEY_BLOCK, BLOCK_MODEL_EMPTY, 1, 0);
    InitSimpleFullBlock("minecraft:honeycomb_block");
    InitSimpleFullBlock("minecraft:netherite_block");
    InitSimpleFullBlock("minecraft:ancient_debris");
    InitSimpleFullEmittingBlock("minecraft:crying_obsidian", 10);

    props = BeginNextBlock("minecraft:respawn_anchor");
    add_block_property(props, BLOCK_PROPERTY_RESPAWN_ANCHOR_CHARGES, "0");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);
    for (i32 i = 0; i < count_block_states(props); i++) {
        i32 blockState = props->base_state + i;
        block_state_info info = describe_block_state(blockState);
        serv->emittedLightByState[blockState] = 15 * info.respawn_anchor_charges / 4;
    }

    InitFlowerPot("minecraft:potted_crimson_fungus");
    InitFlowerPot("minecraft:potted_warped_fungus");
    InitFlowerPot("minecraft:potted_crimson_roots");
    InitFlowerPot("minecraft:potted_warped_roots");
    InitSimpleFullBlock("minecraft:lodestone");
    InitSimpleFullBlock("minecraft:blackstone");

    init_stair_props("minecraft:blackstone_stairs");

    init_wall_props("minecraft:blackstone_wall");

    init_slab("minecraft:blackstone_slab");

    InitSimpleFullBlock("minecraft:polished_blackstone");
    InitSimpleFullBlock("minecraft:polished_blackstone_bricks");
    InitSimpleFullBlock("minecraft:cracked_polished_blackstone_bricks");
    InitSimpleFullBlock("minecraft:chiseled_polished_blackstone");

    init_slab("minecraft:polished_blackstone_brick_slab");

    init_stair_props("minecraft:polished_blackstone_brick_stairs");

    init_wall_props("minecraft:polished_blackstone_brick_wall");

    InitSimpleFullBlock("minecraft:gilded_blackstone");

    init_stair_props("minecraft:polished_blackstone_stairs");

    init_slab("minecraft:polished_blackstone_slab");

    init_pressure_plate("minecraft:polished_blackstone_pressure_plate");

    init_button("minecraft:polished_blackstone_button");

    init_wall_props("minecraft:polished_blackstone_wall");

    InitSimpleFullBlock("minecraft:chiseled_nether_bricks");
    InitSimpleFullBlock("minecraft:cracked_nether_bricks");
    InitSimpleFullBlock("minecraft:quartz_bricks");

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

    InitSimpleFullBlock("minecraft:amethyst_block");
    InitSimpleFullBlock("minecraft:budding_amethyst");

    init_amethyst_cluster("minecraft:amethyst_cluster", 5);
    init_amethyst_cluster("minecraft:large_amethyst_bud", 4);
    init_amethyst_cluster("minecraft:medium_amethyst_bud", 2);
    init_amethyst_cluster("minecraft:small_amethyst_bud", 1);

    InitSimpleFullBlock("minecraft:tuff");
    init_slab("minecraft:tuff_slab");
    init_stair_props("minecraft:tuff_stairs");
    init_wall_props("minecraft:tuff_wall");
    InitSimpleFullBlock("minecraft:polished_tuff");
    init_slab("minecraft:polished_tuff_slab");
    init_stair_props("minecraft:polished_tuff_stairs");
    init_wall_props("minecraft:polished_tuff_wall");
    InitSimpleFullBlock("minecraft:chiseled_tuff");
    InitSimpleFullBlock("minecraft:tuff_bricks");
    init_slab("minecraft:tuff_brick_slab");
    init_stair_props("minecraft:tuff_brick_stairs");
    init_wall_props("minecraft:tuff_brick_wall");
    InitSimpleFullBlock("minecraft:chiseled_tuff_bricks");

    InitSimpleFullBlock("minecraft:calcite");

    InitSimpleFullBlock("minecraft:tinted_glass");

    // @TODO(traks) correct collision model, support model is correct though!
    InitSimpleBlockWithModels("minecraft:powder_snow", BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, 1, 0);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:sculk_sensor");
    add_block_property(props, BLOCK_PROPERTY_SCULK_SENSOR_PHASE, "inactive");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetEmittedLightForAllStates(props, 1);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    // TODO(traks): block models, light reduction, light emission, behaviours, etc.
    props = BeginNextBlock("minecraft:calibrated_sculk_sensor");
    add_block_property(props, BLOCK_PROPERTY_SCULK_SENSOR_PHASE, "inactive");
    add_block_property(props, BLOCK_PROPERTY_POWER, "0");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");

    InitSimpleFullBlock("minecraft:sculk");
    InitMultiFaceBlock("minecraft:sculk_vein", 0);

    props = BeginNextBlock("minecraft:sculk_catalyst");
    add_block_property(props, BLOCK_PROPERTY_BLOOM, "false");
    SetAllModelsForAllStatesIndividually(props, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(props, 15);
    SetEmittedLightForAllStates(props, 6);

    props = BeginNextBlock("minecraft:sculk_shrieker");
    add_block_property(props, BLOCK_PROPERTY_SHRIEKING, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_CAN_SUMMON, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_Y_8);
    SetLightReductionForAllStates(props, 0);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    InitSimpleFullBlock("minecraft:copper_block");
    InitSimpleFullBlock("minecraft:exposed_copper");
    InitSimpleFullBlock("minecraft:weathered_copper");
    InitSimpleFullBlock("minecraft:oxidized_copper");
    InitSimpleFullBlock("minecraft:copper_ore");
    InitSimpleFullBlock("minecraft:deepslate_copper_ore");
    InitSimpleFullBlock("minecraft:oxidized_cut_copper");
    InitSimpleFullBlock("minecraft:weathered_cut_copper");
    InitSimpleFullBlock("minecraft:exposed_cut_copper");
    InitSimpleFullBlock("minecraft:cut_copper");
    InitSimpleFullBlock("minecraft:oxidized_chiseled_copper");
    InitSimpleFullBlock("minecraft:weathered_chiseled_copper");
    InitSimpleFullBlock("minecraft:exposed_chiseled_copper");
    InitSimpleFullBlock("minecraft:chiseled_copper");
    InitSimpleFullBlock("minecraft:waxed_oxidized_chiseled_copper");
    InitSimpleFullBlock("minecraft:waxed_weathered_chiseled_copper");
    InitSimpleFullBlock("minecraft:waxed_exposed_chiseled_copper");
    InitSimpleFullBlock("minecraft:waxed_chiseled_copper");

    init_stair_props("minecraft:oxidized_cut_copper_stairs");
    init_stair_props("minecraft:weathered_cut_copper_stairs");
    init_stair_props("minecraft:exposed_cut_copper_stairs");
    init_stair_props("minecraft:cut_copper_stairs");

    init_slab("minecraft:oxidized_cut_copper_slab");
    init_slab("minecraft:weathered_cut_copper_slab");
    init_slab("minecraft:exposed_cut_copper_slab");
    init_slab("minecraft:cut_copper_slab");

    InitSimpleFullBlock("minecraft:waxed_copper_block");
    InitSimpleFullBlock("minecraft:waxed_weathered_copper");
    InitSimpleFullBlock("minecraft:waxed_exposed_copper");
    InitSimpleFullBlock("minecraft:waxed_oxidized_copper");
    InitSimpleFullBlock("minecraft:waxed_oxidized_cut_copper");
    InitSimpleFullBlock("minecraft:waxed_weathered_cut_copper");
    InitSimpleFullBlock("minecraft:waxed_exposed_cut_copper");
    InitSimpleFullBlock("minecraft:waxed_cut_copper");

    init_stair_props("minecraft:waxed_oxidized_cut_copper_stairs");
    init_stair_props("minecraft:waxed_weathered_cut_copper_stairs");
    init_stair_props("minecraft:waxed_exposed_cut_copper_stairs");
    init_stair_props("minecraft:waxed_cut_copper_stairs");

    init_slab("minecraft:waxed_oxidized_cut_copper_slab");
    init_slab("minecraft:waxed_weathered_cut_copper_slab");
    init_slab("minecraft:waxed_exposed_cut_copper_slab");
    init_slab("minecraft:waxed_cut_copper_slab");

    init_door_props("minecraft:copper_door");
    init_door_props("minecraft:exposed_copper_door");
    init_door_props("minecraft:oxidized_copper_door");
    init_door_props("minecraft:weathered_copper_door");
    init_door_props("minecraft:waxed_copper_door");
    init_door_props("minecraft:waxed_exposed_copper_door");
    init_door_props("minecraft:waxed_oxidized_copper_door");
    init_door_props("minecraft:waxed_weathered_copper_door");

    init_trapdoor_props("minecraft:copper_trapdoor");
    init_trapdoor_props("minecraft:exposed_copper_trapdoor");
    init_trapdoor_props("minecraft:oxidized_copper_trapdoor");
    init_trapdoor_props("minecraft:weathered_copper_trapdoor");
    init_trapdoor_props("minecraft:waxed_copper_trapdoor");
    init_trapdoor_props("minecraft:waxed_exposed_copper_trapdoor");
    init_trapdoor_props("minecraft:waxed_oxidized_copper_trapdoor");
    init_trapdoor_props("minecraft:waxed_weathered_copper_trapdoor");

    InitGrate("minecraft:copper_grate");
    InitGrate("minecraft:exposed_copper_grate");
    InitGrate("minecraft:oxidized_copper_grate");
    InitGrate("minecraft:weathered_copper_grate");
    InitGrate("minecraft:waxed_copper_grate");
    InitGrate("minecraft:waxed_exposed_copper_grate");
    InitGrate("minecraft:waxed_oxidized_copper_grate");
    InitGrate("minecraft:waxed_weathered_copper_grate");

    InitBulb("minecraft:copper_bulb");
    InitBulb("minecraft:exposed_copper_bulb");
    InitBulb("minecraft:oxidized_copper_bulb");
    InitBulb("minecraft:weathered_copper_bulb");
    InitBulb("minecraft:waxed_copper_bulb");
    InitBulb("minecraft:waxed_exposed_copper_bulb");
    InitBulb("minecraft:waxed_oxidized_copper_bulb");
    InitBulb("minecraft:waxed_weathered_copper_bulb");

    // TODO(traks) block models + light reduction
    props = BeginNextBlock("minecraft:lightning_rod");
    add_block_property(props, BLOCK_PROPERTY_FACING, "up");
    add_block_property(props, BLOCK_PROPERTY_POWERED, "false");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:pointed_dripstone");
    add_block_property(props, BLOCK_PROPERTY_VERTICAL_DIRECTION, "up");
    add_block_property(props, BLOCK_PROPERTY_DRIPSTONE_THICKNESS, "tip");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    InitSimpleFullBlock("minecraft:dripstone_block");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:cave_vines");
    add_block_property(props, BLOCK_PROPERTY_AGE_25, "0");
    add_block_property(props, BLOCK_PROPERTY_BERRIES, "false");
    SetEmittedLightWhenBerries(props, 14);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:cave_vines_plant");
    add_block_property(props, BLOCK_PROPERTY_BERRIES, "false");
    SetEmittedLightWhenBerries(props, 14);

    props = BeginNextBlock("minecraft:spore_blossom");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_NEED_POLE_SUPPORT_ABOVE);

    InitAzalea("minecraft:azalea");
    InitAzalea("minecraft:flowering_azalea");

    InitCarpet("minecraft:moss_carpet");

    // TODO(traks): block models, light reduction, behaviour, etc.
    props = BeginNextBlock("minecraft:pink_petals");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_FLOWER_AMOUNT, "1");

    InitSimpleFullBlock("minecraft:moss_block");

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:big_dripleaf");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_DRIPLEAF_TILT, "none");
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_BIG_DRIPLEAF);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:big_dripleaf_stem");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_BIG_DRIPLEAF_STEM);

    // TODO(traks): block models + light reduction
    props = BeginNextBlock("minecraft:small_dripleaf");
    add_block_property(props, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_SMALL_DRIPLEAF);

    props = BeginNextBlock("minecraft:hanging_roots");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(props, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(props, BLOCK_BEHAVIOUR_FLUID);

    InitSimpleFullBlock("minecraft:rooted_dirt");
    InitSimpleFullBlock("minecraft:mud");

    init_pillar("minecraft:deepslate", 0);

    InitSimpleFullBlock("minecraft:cobbled_deepslate");
    init_stair_props("minecraft:cobbled_deepslate_stairs");
    init_slab("minecraft:cobbled_deepslate_slab");
    init_wall_props("minecraft:cobbled_deepslate_wall");

    InitSimpleFullBlock("minecraft:polished_deepslate");
    init_stair_props("minecraft:polished_deepslate_stairs");
    init_slab("minecraft:polished_deepslate_slab");
    init_wall_props("minecraft:polished_deepslate_wall");

    InitSimpleFullBlock("minecraft:deepslate_tiles");
    init_stair_props("minecraft:deepslate_tile_stairs");
    init_slab("minecraft:deepslate_tile_slab");
    init_wall_props("minecraft:deepslate_tile_wall");

    InitSimpleFullBlock("minecraft:deepslate_bricks");
    init_stair_props("minecraft:deepslate_brick_stairs");
    init_slab("minecraft:deepslate_brick_slab");
    init_wall_props("minecraft:deepslate_brick_wall");

    InitSimpleFullBlock("minecraft:chiseled_deepslate");
    InitSimpleFullBlock("minecraft:cracked_deepslate_bricks");
    InitSimpleFullBlock("minecraft:cracked_deepslate_tiles");

    init_pillar("minecraft:infested_deepslate", 0);

    InitSimpleFullBlock("minecraft:smooth_basalt");
    InitSimpleFullBlock("minecraft:raw_iron_block");
    InitSimpleFullBlock("minecraft:raw_copper_block");
    InitSimpleFullBlock("minecraft:raw_gold_block");

    InitFlowerPot("minecraft:potted_azalea_bush");
    InitFlowerPot("minecraft:potted_flowering_azalea_bush");

    init_pillar("minecraft:ochre_froglight", 15);
    init_pillar("minecraft:verdant_froglight", 15);
    init_pillar("minecraft:pearlescent_froglight", 15);
    InitSimpleEmptyBlock("minecraft:frogspawn");
    InitSimpleFullBlock("minecraft:reinforced_deepslate");

    // TODO(traks): block models, behaviours, light behaviour, etc. This is a
    // block entity
    props = BeginNextBlock("minecraft:decorated_pot");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");

    // TODO(traks): block models, behaviours, light behaviour, etc. This is a
    // block entity
    props = BeginNextBlock("minecraft:crafter");
    add_block_property(props, BLOCK_PROPERTY_JIGSAW_ORIENTATION, "north_up");
    add_block_property(props, BLOCK_PROPERTY_TRIGGERED, "false");
    add_block_property(props, BLOCK_PROPERTY_CRAFTING, "false");

    // TODO(traks): block models, behaviours, light behaviour, etc. This is a
    // block entity
    props = BeginNextBlock("minecraft:trial_spawner");
    add_block_property(props, BLOCK_PROPERTY_TRIAL_SPAWNER_STATE, "inactive");
    add_block_property(props, BLOCK_PROPERTY_OMINOUS, "false");

    // TODO(traks): block models, behaviours, light behaviour, etc. This is a
    // block entity
    props = BeginNextBlock("minecraft:vault");
    add_block_property(props, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    add_block_property(props, BLOCK_PROPERTY_VAULT_STATE, "inactive");
    add_block_property(props, BLOCK_PROPERTY_OMINOUS, "false");

    // TODO(traks): block models, behaviours, light behaviour, etc.
    props = BeginNextBlock("minecraft:heavy_core");
    add_block_property(props, BLOCK_PROPERTY_WATERLOGGED, "false");

    // TODO(traks): behaviours
    InitSimpleFullBlock("minecraft:pale_moss_block");
    InitCarpet("minecraft:pale_moss_carpet");
    // TODO(traks): block models, behaviours, light, etc.
    props = BeginNextBlock("minecraft:pale_hanging_moss");
    add_block_property(props, BLOCK_PROPERTY_TIP, "true");

    serv->vanilla_block_state_count = serv->actual_block_state_count;

    InitSimpleFullBlock("blaze:unknown");

    AddRegistryTag(registry, "minecraft:frog_prefer_jump_to", "minecraft:lily_pad", "minecraft:big_dripleaf", NULL);
    AddRegistryTag(registry, "minecraft:completes_find_tree_tutorial", "minecraft:dark_oak_log", "minecraft:dark_oak_wood", "minecraft:stripped_dark_oak_log", "minecraft:stripped_dark_oak_wood", "minecraft:oak_log", "minecraft:oak_wood", "minecraft:stripped_oak_log", "minecraft:stripped_oak_wood", "minecraft:acacia_log", "minecraft:acacia_wood", "minecraft:stripped_acacia_log", "minecraft:stripped_acacia_wood", "minecraft:birch_log", "minecraft:birch_wood", "minecraft:stripped_birch_log", "minecraft:stripped_birch_wood", "minecraft:jungle_log", "minecraft:jungle_wood", "minecraft:stripped_jungle_log", "minecraft:stripped_jungle_wood", "minecraft:spruce_log", "minecraft:spruce_wood", "minecraft:stripped_spruce_log", "minecraft:stripped_spruce_wood", "minecraft:mangrove_log", "minecraft:mangrove_wood", "minecraft:stripped_mangrove_log", "minecraft:stripped_mangrove_wood", "minecraft:cherry_log", "minecraft:cherry_wood", "minecraft:stripped_cherry_log", "minecraft:stripped_cherry_wood", "minecraft:crimson_stem", "minecraft:stripped_crimson_stem", "minecraft:crimson_hyphae", "minecraft:stripped_crimson_hyphae", "minecraft:warped_stem", "minecraft:stripped_warped_stem", "minecraft:warped_hyphae", "minecraft:stripped_warped_hyphae", "minecraft:jungle_leaves", "minecraft:oak_leaves", "minecraft:spruce_leaves", "minecraft:dark_oak_leaves", "minecraft:acacia_leaves", "minecraft:birch_leaves", "minecraft:azalea_leaves", "minecraft:flowering_azalea_leaves", "minecraft:mangrove_leaves", "minecraft:cherry_leaves", "minecraft:nether_wart_block", "minecraft:warped_wart_block", NULL);
    AddRegistryTag(registry, "minecraft:base_stone_overworld", "minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate", NULL);
    AddRegistryTag(registry, "minecraft:convertable_to_mud", "minecraft:dirt", "minecraft:coarse_dirt", "minecraft:rooted_dirt", NULL);
    AddRegistryTag(registry, "minecraft:geode_invalid_blocks", "minecraft:bedrock", "minecraft:water", "minecraft:lava", "minecraft:ice", "minecraft:packed_ice", "minecraft:blue_ice", NULL);
    AddRegistryTag(registry, "minecraft:pressure_plates", "minecraft:light_weighted_pressure_plate", "minecraft:heavy_weighted_pressure_plate", "minecraft:oak_pressure_plate", "minecraft:spruce_pressure_plate", "minecraft:birch_pressure_plate", "minecraft:jungle_pressure_plate", "minecraft:acacia_pressure_plate", "minecraft:dark_oak_pressure_plate", "minecraft:crimson_pressure_plate", "minecraft:warped_pressure_plate", "minecraft:mangrove_pressure_plate", "minecraft:bamboo_pressure_plate", "minecraft:cherry_pressure_plate", "minecraft:stone_pressure_plate", "minecraft:polished_blackstone_pressure_plate", NULL);
    AddRegistryTag(registry, "minecraft:badlands_terracotta", "minecraft:terracotta", "minecraft:white_terracotta", "minecraft:yellow_terracotta", "minecraft:orange_terracotta", "minecraft:red_terracotta", "minecraft:brown_terracotta", "minecraft:light_gray_terracotta", NULL);
    AddRegistryTag(registry, "minecraft:wooden_fences", "minecraft:oak_fence", "minecraft:acacia_fence", "minecraft:dark_oak_fence", "minecraft:spruce_fence", "minecraft:birch_fence", "minecraft:jungle_fence", "minecraft:crimson_fence", "minecraft:warped_fence", "minecraft:mangrove_fence", "minecraft:bamboo_fence", "minecraft:cherry_fence", NULL);
    AddRegistryTag(registry, "minecraft:cave_vines", "minecraft:cave_vines_plant", "minecraft:cave_vines", NULL);
    AddRegistryTag(registry, "minecraft:campfires", "minecraft:campfire", "minecraft:soul_campfire", NULL);
    AddRegistryTag(registry, "minecraft:hoglin_repellents", "minecraft:warped_fungus", "minecraft:potted_warped_fungus", "minecraft:nether_portal", "minecraft:respawn_anchor", NULL);
    AddRegistryTag(registry, "minecraft:smelts_to_glass", "minecraft:sand", "minecraft:red_sand", NULL);
    AddRegistryTag(registry, "minecraft:sand", "minecraft:sand", "minecraft:red_sand", "minecraft:suspicious_sand", "minecraft:suspicious_sand", NULL);
    AddRegistryTag(registry, "minecraft:incorrect_for_stone_tool", "minecraft:obsidian", "minecraft:crying_obsidian", "minecraft:netherite_block", "minecraft:respawn_anchor", "minecraft:ancient_debris", "minecraft:diamond_block", "minecraft:diamond_ore", "minecraft:deepslate_diamond_ore", "minecraft:emerald_ore", "minecraft:deepslate_emerald_ore", "minecraft:emerald_block", "minecraft:gold_block", "minecraft:raw_gold_block", "minecraft:gold_ore", "minecraft:deepslate_gold_ore", "minecraft:redstone_ore", "minecraft:deepslate_redstone_ore", NULL);
    AddRegistryTag(registry, "minecraft:frogs_spawnable_on", "minecraft:grass_block", "minecraft:mud", "minecraft:mangrove_roots", "minecraft:muddy_mangrove_roots", NULL);
    AddRegistryTag(registry, "minecraft:warped_stems", "minecraft:warped_stem", "minecraft:stripped_warped_stem", "minecraft:warped_hyphae", "minecraft:stripped_warped_hyphae", NULL);
    AddRegistryTag(registry, "minecraft:mangrove_roots_can_grow_through", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:mangrove_roots", "minecraft:moss_carpet", "minecraft:vine", "minecraft:mangrove_propagule", "minecraft:snow", NULL);
    AddRegistryTag(registry, "minecraft:all_hanging_signs", "minecraft:oak_hanging_sign", "minecraft:spruce_hanging_sign", "minecraft:birch_hanging_sign", "minecraft:acacia_hanging_sign", "minecraft:cherry_hanging_sign", "minecraft:jungle_hanging_sign", "minecraft:dark_oak_hanging_sign", "minecraft:crimson_hanging_sign", "minecraft:warped_hanging_sign", "minecraft:mangrove_hanging_sign", "minecraft:bamboo_hanging_sign", "minecraft:oak_wall_hanging_sign", "minecraft:spruce_wall_hanging_sign", "minecraft:birch_wall_hanging_sign", "minecraft:acacia_wall_hanging_sign", "minecraft:cherry_wall_hanging_sign", "minecraft:jungle_wall_hanging_sign", "minecraft:dark_oak_wall_hanging_sign", "minecraft:crimson_wall_hanging_sign", "minecraft:warped_wall_hanging_sign", "minecraft:mangrove_wall_hanging_sign", "minecraft:bamboo_wall_hanging_sign", NULL);
    AddRegistryTag(registry, "minecraft:trapdoors", "minecraft:acacia_trapdoor", "minecraft:birch_trapdoor", "minecraft:dark_oak_trapdoor", "minecraft:jungle_trapdoor", "minecraft:oak_trapdoor", "minecraft:spruce_trapdoor", "minecraft:crimson_trapdoor", "minecraft:warped_trapdoor", "minecraft:mangrove_trapdoor", "minecraft:bamboo_trapdoor", "minecraft:cherry_trapdoor", "minecraft:iron_trapdoor", "minecraft:copper_trapdoor", "minecraft:exposed_copper_trapdoor", "minecraft:weathered_copper_trapdoor", "minecraft:oxidized_copper_trapdoor", "minecraft:waxed_copper_trapdoor", "minecraft:waxed_exposed_copper_trapdoor", "minecraft:waxed_weathered_copper_trapdoor", "minecraft:waxed_oxidized_copper_trapdoor", NULL);
    AddRegistryTag(registry, "minecraft:inside_step_sound_blocks", "minecraft:powder_snow", "minecraft:sculk_vein", "minecraft:glow_lichen", "minecraft:lily_pad", "minecraft:small_amethyst_bud", "minecraft:pink_petals", NULL);
    AddRegistryTag(registry, "minecraft:climbable", "minecraft:ladder", "minecraft:vine", "minecraft:scaffolding", "minecraft:weeping_vines", "minecraft:weeping_vines_plant", "minecraft:twisting_vines", "minecraft:twisting_vines_plant", "minecraft:cave_vines", "minecraft:cave_vines_plant", NULL);
    AddRegistryTag(registry, "minecraft:wall_corals", "minecraft:tube_coral_wall_fan", "minecraft:brain_coral_wall_fan", "minecraft:bubble_coral_wall_fan", "minecraft:fire_coral_wall_fan", "minecraft:horn_coral_wall_fan", NULL);
    AddRegistryTag(registry, "minecraft:coral_plants", "minecraft:tube_coral", "minecraft:brain_coral", "minecraft:bubble_coral", "minecraft:fire_coral", "minecraft:horn_coral", NULL);
    AddRegistryTag(registry, "minecraft:incorrect_for_wooden_tool", "minecraft:obsidian", "minecraft:crying_obsidian", "minecraft:netherite_block", "minecraft:respawn_anchor", "minecraft:ancient_debris", "minecraft:diamond_block", "minecraft:diamond_ore", "minecraft:deepslate_diamond_ore", "minecraft:emerald_ore", "minecraft:deepslate_emerald_ore", "minecraft:emerald_block", "minecraft:gold_block", "minecraft:raw_gold_block", "minecraft:gold_ore", "minecraft:deepslate_gold_ore", "minecraft:redstone_ore", "minecraft:deepslate_redstone_ore", "minecraft:iron_block", "minecraft:raw_iron_block", "minecraft:iron_ore", "minecraft:deepslate_iron_ore", "minecraft:lapis_block", "minecraft:lapis_ore", "minecraft:deepslate_lapis_ore", "minecraft:copper_block", "minecraft:raw_copper_block", "minecraft:copper_ore", "minecraft:deepslate_copper_ore", "minecraft:cut_copper_slab", "minecraft:cut_copper_stairs", "minecraft:cut_copper", "minecraft:weathered_copper", "minecraft:weathered_cut_copper_slab", "minecraft:weathered_cut_copper_stairs", "minecraft:weathered_cut_copper", "minecraft:oxidized_copper", "minecraft:oxidized_cut_copper_slab", "minecraft:oxidized_cut_copper_stairs", "minecraft:oxidized_cut_copper", "minecraft:exposed_copper", "minecraft:exposed_cut_copper_slab", "minecraft:exposed_cut_copper_stairs", "minecraft:exposed_cut_copper", "minecraft:waxed_copper_block", "minecraft:waxed_cut_copper_slab", "minecraft:waxed_cut_copper_stairs", "minecraft:waxed_cut_copper", "minecraft:waxed_weathered_copper", "minecraft:waxed_weathered_cut_copper_slab", "minecraft:waxed_weathered_cut_copper_stairs", "minecraft:waxed_weathered_cut_copper", "minecraft:waxed_exposed_copper", "minecraft:waxed_exposed_cut_copper_slab", "minecraft:waxed_exposed_cut_copper_stairs", "minecraft:waxed_exposed_cut_copper", "minecraft:waxed_oxidized_copper", "minecraft:waxed_oxidized_cut_copper_slab", "minecraft:waxed_oxidized_cut_copper_stairs", "minecraft:waxed_oxidized_cut_copper", "minecraft:lightning_rod", "minecraft:crafter", "minecraft:chiseled_copper", "minecraft:exposed_chiseled_copper", "minecraft:weathered_chiseled_copper", "minecraft:oxidized_chiseled_copper", "minecraft:waxed_chiseled_copper", "minecraft:waxed_exposed_chiseled_copper", "minecraft:waxed_weathered_chiseled_copper", "minecraft:waxed_oxidized_chiseled_copper", "minecraft:copper_grate", "minecraft:exposed_copper_grate", "minecraft:weathered_copper_grate", "minecraft:oxidized_copper_grate", "minecraft:waxed_copper_grate", "minecraft:waxed_exposed_copper_grate", "minecraft:waxed_weathered_copper_grate", "minecraft:waxed_oxidized_copper_grate", "minecraft:copper_bulb", "minecraft:exposed_copper_bulb", "minecraft:weathered_copper_bulb", "minecraft:oxidized_copper_bulb", "minecraft:waxed_copper_bulb", "minecraft:waxed_exposed_copper_bulb", "minecraft:waxed_weathered_copper_bulb", "minecraft:waxed_oxidized_copper_bulb", "minecraft:copper_trapdoor", "minecraft:exposed_copper_trapdoor", "minecraft:weathered_copper_trapdoor", "minecraft:oxidized_copper_trapdoor", "minecraft:waxed_copper_trapdoor", "minecraft:waxed_exposed_copper_trapdoor", "minecraft:waxed_weathered_copper_trapdoor", "minecraft:waxed_oxidized_copper_trapdoor", "minecraft:copper_door", "minecraft:exposed_copper_door", "minecraft:weathered_copper_door", "minecraft:oxidized_copper_door", "minecraft:waxed_copper_door", "minecraft:waxed_exposed_copper_door", "minecraft:waxed_weathered_copper_door", "minecraft:waxed_oxidized_copper_door", NULL);
    AddRegistryTag(registry, "minecraft:planks", "minecraft:oak_planks", "minecraft:spruce_planks", "minecraft:birch_planks", "minecraft:jungle_planks", "minecraft:acacia_planks", "minecraft:dark_oak_planks", "minecraft:crimson_planks", "minecraft:warped_planks", "minecraft:mangrove_planks", "minecraft:bamboo_planks", "minecraft:cherry_planks", NULL);
    AddRegistryTag(registry, "minecraft:incorrect_for_gold_tool", "minecraft:obsidian", "minecraft:crying_obsidian", "minecraft:netherite_block", "minecraft:respawn_anchor", "minecraft:ancient_debris", "minecraft:diamond_block", "minecraft:diamond_ore", "minecraft:deepslate_diamond_ore", "minecraft:emerald_ore", "minecraft:deepslate_emerald_ore", "minecraft:emerald_block", "minecraft:gold_block", "minecraft:raw_gold_block", "minecraft:gold_ore", "minecraft:deepslate_gold_ore", "minecraft:redstone_ore", "minecraft:deepslate_redstone_ore", "minecraft:iron_block", "minecraft:raw_iron_block", "minecraft:iron_ore", "minecraft:deepslate_iron_ore", "minecraft:lapis_block", "minecraft:lapis_ore", "minecraft:deepslate_lapis_ore", "minecraft:copper_block", "minecraft:raw_copper_block", "minecraft:copper_ore", "minecraft:deepslate_copper_ore", "minecraft:cut_copper_slab", "minecraft:cut_copper_stairs", "minecraft:cut_copper", "minecraft:weathered_copper", "minecraft:weathered_cut_copper_slab", "minecraft:weathered_cut_copper_stairs", "minecraft:weathered_cut_copper", "minecraft:oxidized_copper", "minecraft:oxidized_cut_copper_slab", "minecraft:oxidized_cut_copper_stairs", "minecraft:oxidized_cut_copper", "minecraft:exposed_copper", "minecraft:exposed_cut_copper_slab", "minecraft:exposed_cut_copper_stairs", "minecraft:exposed_cut_copper", "minecraft:waxed_copper_block", "minecraft:waxed_cut_copper_slab", "minecraft:waxed_cut_copper_stairs", "minecraft:waxed_cut_copper", "minecraft:waxed_weathered_copper", "minecraft:waxed_weathered_cut_copper_slab", "minecraft:waxed_weathered_cut_copper_stairs", "minecraft:waxed_weathered_cut_copper", "minecraft:waxed_exposed_copper", "minecraft:waxed_exposed_cut_copper_slab", "minecraft:waxed_exposed_cut_copper_stairs", "minecraft:waxed_exposed_cut_copper", "minecraft:waxed_oxidized_copper", "minecraft:waxed_oxidized_cut_copper_slab", "minecraft:waxed_oxidized_cut_copper_stairs", "minecraft:waxed_oxidized_cut_copper", "minecraft:lightning_rod", "minecraft:crafter", "minecraft:chiseled_copper", "minecraft:exposed_chiseled_copper", "minecraft:weathered_chiseled_copper", "minecraft:oxidized_chiseled_copper", "minecraft:waxed_chiseled_copper", "minecraft:waxed_exposed_chiseled_copper", "minecraft:waxed_weathered_chiseled_copper", "minecraft:waxed_oxidized_chiseled_copper", "minecraft:copper_grate", "minecraft:exposed_copper_grate", "minecraft:weathered_copper_grate", "minecraft:oxidized_copper_grate", "minecraft:waxed_copper_grate", "minecraft:waxed_exposed_copper_grate", "minecraft:waxed_weathered_copper_grate", "minecraft:waxed_oxidized_copper_grate", "minecraft:copper_bulb", "minecraft:exposed_copper_bulb", "minecraft:weathered_copper_bulb", "minecraft:oxidized_copper_bulb", "minecraft:waxed_copper_bulb", "minecraft:waxed_exposed_copper_bulb", "minecraft:waxed_weathered_copper_bulb", "minecraft:waxed_oxidized_copper_bulb", "minecraft:copper_trapdoor", "minecraft:exposed_copper_trapdoor", "minecraft:weathered_copper_trapdoor", "minecraft:oxidized_copper_trapdoor", "minecraft:waxed_copper_trapdoor", "minecraft:waxed_exposed_copper_trapdoor", "minecraft:waxed_weathered_copper_trapdoor", "minecraft:waxed_oxidized_copper_trapdoor", "minecraft:copper_door", "minecraft:exposed_copper_door", "minecraft:weathered_copper_door", "minecraft:oxidized_copper_door", "minecraft:waxed_copper_door", "minecraft:waxed_exposed_copper_door", "minecraft:waxed_weathered_copper_door", "minecraft:waxed_oxidized_copper_door", NULL);
    AddRegistryTag(registry, "minecraft:beacon_base_blocks", "minecraft:netherite_block", "minecraft:emerald_block", "minecraft:diamond_block", "minecraft:gold_block", "minecraft:iron_block", NULL);
    AddRegistryTag(registry, "minecraft:replaceable", "minecraft:air", "minecraft:water", "minecraft:lava", "minecraft:short_grass", "minecraft:fern", "minecraft:dead_bush", "minecraft:seagrass", "minecraft:tall_seagrass", "minecraft:fire", "minecraft:soul_fire", "minecraft:snow", "minecraft:vine", "minecraft:glow_lichen", "minecraft:light", "minecraft:tall_grass", "minecraft:large_fern", "minecraft:structure_void", "minecraft:void_air", "minecraft:cave_air", "minecraft:bubble_column", "minecraft:warped_roots", "minecraft:nether_sprouts", "minecraft:crimson_roots", "minecraft:hanging_roots", NULL);
    AddRegistryTag(registry, "minecraft:emerald_ores", "minecraft:emerald_ore", "minecraft:deepslate_emerald_ore", NULL);
    AddRegistryTag(registry, "minecraft:tall_flowers", "minecraft:sunflower", "minecraft:lilac", "minecraft:peony", "minecraft:rose_bush", "minecraft:pitcher_plant", NULL);
    AddRegistryTag(registry, "minecraft:mob_interactable_doors", "minecraft:oak_door", "minecraft:spruce_door", "minecraft:birch_door", "minecraft:jungle_door", "minecraft:acacia_door", "minecraft:dark_oak_door", "minecraft:crimson_door", "minecraft:warped_door", "minecraft:mangrove_door", "minecraft:bamboo_door", "minecraft:cherry_door", "minecraft:copper_door", "minecraft:exposed_copper_door", "minecraft:weathered_copper_door", "minecraft:oxidized_copper_door", "minecraft:waxed_copper_door", "minecraft:waxed_exposed_copper_door", "minecraft:waxed_weathered_copper_door", "minecraft:waxed_oxidized_copper_door", NULL);
    AddRegistryTag(registry, "minecraft:logs", "minecraft:dark_oak_log", "minecraft:dark_oak_wood", "minecraft:stripped_dark_oak_log", "minecraft:stripped_dark_oak_wood", "minecraft:oak_log", "minecraft:oak_wood", "minecraft:stripped_oak_log", "minecraft:stripped_oak_wood", "minecraft:acacia_log", "minecraft:acacia_wood", "minecraft:stripped_acacia_log", "minecraft:stripped_acacia_wood", "minecraft:birch_log", "minecraft:birch_wood", "minecraft:stripped_birch_log", "minecraft:stripped_birch_wood", "minecraft:jungle_log", "minecraft:jungle_wood", "minecraft:stripped_jungle_log", "minecraft:stripped_jungle_wood", "minecraft:spruce_log", "minecraft:spruce_wood", "minecraft:stripped_spruce_log", "minecraft:stripped_spruce_wood", "minecraft:mangrove_log", "minecraft:mangrove_wood", "minecraft:stripped_mangrove_log", "minecraft:stripped_mangrove_wood", "minecraft:cherry_log", "minecraft:cherry_wood", "minecraft:stripped_cherry_log", "minecraft:stripped_cherry_wood", "minecraft:crimson_stem", "minecraft:stripped_crimson_stem", "minecraft:crimson_hyphae", "minecraft:stripped_crimson_hyphae", "minecraft:warped_stem", "minecraft:stripped_warped_stem", "minecraft:warped_hyphae", "minecraft:stripped_warped_hyphae", NULL);
    AddRegistryTag(registry, "minecraft:jungle_logs", "minecraft:jungle_log", "minecraft:jungle_wood", "minecraft:stripped_jungle_log", "minecraft:stripped_jungle_wood", NULL);
    AddRegistryTag(registry, "minecraft:sniffer_diggable_block", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", NULL);
    AddRegistryTag(registry, "minecraft:fire", "minecraft:fire", "minecraft:soul_fire", NULL);
    AddRegistryTag(registry, "minecraft:wall_hanging_signs", "minecraft:oak_wall_hanging_sign", "minecraft:spruce_wall_hanging_sign", "minecraft:birch_wall_hanging_sign", "minecraft:acacia_wall_hanging_sign", "minecraft:cherry_wall_hanging_sign", "minecraft:jungle_wall_hanging_sign", "minecraft:dark_oak_wall_hanging_sign", "minecraft:crimson_wall_hanging_sign", "minecraft:warped_wall_hanging_sign", "minecraft:mangrove_wall_hanging_sign", "minecraft:bamboo_wall_hanging_sign", NULL);
    AddRegistryTag(registry, "minecraft:wool", "minecraft:white_wool", "minecraft:orange_wool", "minecraft:magenta_wool", "minecraft:light_blue_wool", "minecraft:yellow_wool", "minecraft:lime_wool", "minecraft:pink_wool", "minecraft:gray_wool", "minecraft:light_gray_wool", "minecraft:cyan_wool", "minecraft:purple_wool", "minecraft:blue_wool", "minecraft:brown_wool", "minecraft:green_wool", "minecraft:red_wool", "minecraft:black_wool", NULL);
    AddRegistryTag(registry, "minecraft:cauldrons", "minecraft:cauldron", "minecraft:water_cauldron", "minecraft:lava_cauldron", "minecraft:powder_snow_cauldron", NULL);
    AddRegistryTag(registry, "minecraft:candles", "minecraft:candle", "minecraft:white_candle", "minecraft:orange_candle", "minecraft:magenta_candle", "minecraft:light_blue_candle", "minecraft:yellow_candle", "minecraft:lime_candle", "minecraft:pink_candle", "minecraft:gray_candle", "minecraft:light_gray_candle", "minecraft:cyan_candle", "minecraft:purple_candle", "minecraft:blue_candle", "minecraft:brown_candle", "minecraft:green_candle", "minecraft:red_candle", "minecraft:black_candle", NULL);
    AddRegistryTag(registry, "minecraft:wolves_spawnable_on", "minecraft:grass_block", "minecraft:snow", "minecraft:snow_block", "minecraft:coarse_dirt", "minecraft:podzol", NULL);
    AddRegistryTag(registry, "minecraft:wooden_buttons", "minecraft:oak_button", "minecraft:spruce_button", "minecraft:birch_button", "minecraft:jungle_button", "minecraft:acacia_button", "minecraft:dark_oak_button", "minecraft:crimson_button", "minecraft:warped_button", "minecraft:mangrove_button", "minecraft:bamboo_button", "minecraft:cherry_button", NULL);
    AddRegistryTag(registry, "minecraft:dripstone_replaceable_blocks", "minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate", NULL);
    AddRegistryTag(registry, "minecraft:cherry_logs", "minecraft:cherry_log", "minecraft:cherry_wood", "minecraft:stripped_cherry_log", "minecraft:stripped_cherry_wood", NULL);
    AddRegistryTag(registry, "minecraft:standing_signs", "minecraft:oak_sign", "minecraft:spruce_sign", "minecraft:birch_sign", "minecraft:acacia_sign", "minecraft:jungle_sign", "minecraft:dark_oak_sign", "minecraft:crimson_sign", "minecraft:warped_sign", "minecraft:mangrove_sign", "minecraft:bamboo_sign", "minecraft:cherry_sign", NULL);
    AddRegistryTag(registry, "minecraft:moss_replaceable", "minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate", "minecraft:cave_vines_plant", "minecraft:cave_vines", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", NULL);
    AddRegistryTag(registry, "minecraft:shulker_boxes", "minecraft:shulker_box", "minecraft:black_shulker_box", "minecraft:blue_shulker_box", "minecraft:brown_shulker_box", "minecraft:cyan_shulker_box", "minecraft:gray_shulker_box", "minecraft:green_shulker_box", "minecraft:light_blue_shulker_box", "minecraft:light_gray_shulker_box", "minecraft:lime_shulker_box", "minecraft:magenta_shulker_box", "minecraft:orange_shulker_box", "minecraft:pink_shulker_box", "minecraft:purple_shulker_box", "minecraft:red_shulker_box", "minecraft:white_shulker_box", "minecraft:yellow_shulker_box", NULL);
    AddRegistryTag(registry, "minecraft:nylium", "minecraft:crimson_nylium", "minecraft:warped_nylium", NULL);
    AddRegistryTag(registry, "minecraft:wither_summon_base_blocks", "minecraft:soul_sand", "minecraft:soul_soil", NULL);
    AddRegistryTag(registry, "minecraft:foxes_spawnable_on", "minecraft:grass_block", "minecraft:snow", "minecraft:snow_block", "minecraft:podzol", "minecraft:coarse_dirt", NULL);
    AddRegistryTag(registry, "minecraft:stone_buttons", "minecraft:stone_button", "minecraft:polished_blackstone_button", NULL);
    AddRegistryTag(registry, "minecraft:coal_ores", "minecraft:coal_ore", "minecraft:deepslate_coal_ore", NULL);
    AddRegistryTag(registry, "minecraft:camel_sand_step_sound_blocks", "minecraft:sand", "minecraft:red_sand", "minecraft:suspicious_sand", "minecraft:suspicious_sand", "minecraft:white_concrete_powder", "minecraft:orange_concrete_powder", "minecraft:magenta_concrete_powder", "minecraft:light_blue_concrete_powder", "minecraft:yellow_concrete_powder", "minecraft:lime_concrete_powder", "minecraft:pink_concrete_powder", "minecraft:gray_concrete_powder", "minecraft:light_gray_concrete_powder", "minecraft:cyan_concrete_powder", "minecraft:purple_concrete_powder", "minecraft:blue_concrete_powder", "minecraft:brown_concrete_powder", "minecraft:green_concrete_powder", "minecraft:red_concrete_powder", "minecraft:black_concrete_powder", NULL);
    AddRegistryTag(registry, "minecraft:valid_spawn", "minecraft:grass_block", "minecraft:podzol", NULL);
    AddRegistryTag(registry, "minecraft:saplings", "minecraft:oak_sapling", "minecraft:spruce_sapling", "minecraft:birch_sapling", "minecraft:jungle_sapling", "minecraft:acacia_sapling", "minecraft:dark_oak_sapling", "minecraft:azalea", "minecraft:flowering_azalea", "minecraft:mangrove_propagule", "minecraft:cherry_sapling", NULL);
    AddRegistryTag(registry, "minecraft:animals_spawnable_on", "minecraft:grass_block", NULL);
    AddRegistryTag(registry, "minecraft:mangrove_logs_can_grow_through", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:mangrove_roots", "minecraft:mangrove_leaves", "minecraft:mangrove_log", "minecraft:mangrove_propagule", "minecraft:moss_carpet", "minecraft:vine", NULL);
    AddRegistryTag(registry, "minecraft:sculk_replaceable_world_gen", "minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:terracotta", "minecraft:white_terracotta", "minecraft:orange_terracotta", "minecraft:magenta_terracotta", "minecraft:light_blue_terracotta", "minecraft:yellow_terracotta", "minecraft:lime_terracotta", "minecraft:pink_terracotta", "minecraft:gray_terracotta", "minecraft:light_gray_terracotta", "minecraft:cyan_terracotta", "minecraft:purple_terracotta", "minecraft:blue_terracotta", "minecraft:brown_terracotta", "minecraft:green_terracotta", "minecraft:red_terracotta", "minecraft:black_terracotta", "minecraft:crimson_nylium", "minecraft:warped_nylium", "minecraft:netherrack", "minecraft:basalt", "minecraft:blackstone", "minecraft:sand", "minecraft:red_sand", "minecraft:gravel", "minecraft:soul_sand", "minecraft:soul_soil", "minecraft:calcite", "minecraft:smooth_basalt", "minecraft:clay", "minecraft:dripstone_block", "minecraft:end_stone", "minecraft:red_sandstone", "minecraft:sandstone", "minecraft:deepslate_bricks", "minecraft:deepslate_tiles", "minecraft:cobbled_deepslate", "minecraft:cracked_deepslate_bricks", "minecraft:cracked_deepslate_tiles", "minecraft:polished_deepslate", NULL);
    AddRegistryTag(registry, "minecraft:snaps_goat_horn", "minecraft:acacia_log", "minecraft:birch_log", "minecraft:oak_log", "minecraft:jungle_log", "minecraft:spruce_log", "minecraft:dark_oak_log", "minecraft:mangrove_log", "minecraft:cherry_log", "minecraft:stone", "minecraft:packed_ice", "minecraft:iron_ore", "minecraft:coal_ore", "minecraft:copper_ore", "minecraft:emerald_ore", NULL);
    AddRegistryTag(registry, "minecraft:dragon_transparent", "minecraft:light", "minecraft:fire", "minecraft:soul_fire", NULL);
    AddRegistryTag(registry, "minecraft:needs_stone_tool", "minecraft:iron_block", "minecraft:raw_iron_block", "minecraft:iron_ore", "minecraft:deepslate_iron_ore", "minecraft:lapis_block", "minecraft:lapis_ore", "minecraft:deepslate_lapis_ore", "minecraft:copper_block", "minecraft:raw_copper_block", "minecraft:copper_ore", "minecraft:deepslate_copper_ore", "minecraft:cut_copper_slab", "minecraft:cut_copper_stairs", "minecraft:cut_copper", "minecraft:weathered_copper", "minecraft:weathered_cut_copper_slab", "minecraft:weathered_cut_copper_stairs", "minecraft:weathered_cut_copper", "minecraft:oxidized_copper", "minecraft:oxidized_cut_copper_slab", "minecraft:oxidized_cut_copper_stairs", "minecraft:oxidized_cut_copper", "minecraft:exposed_copper", "minecraft:exposed_cut_copper_slab", "minecraft:exposed_cut_copper_stairs", "minecraft:exposed_cut_copper", "minecraft:waxed_copper_block", "minecraft:waxed_cut_copper_slab", "minecraft:waxed_cut_copper_stairs", "minecraft:waxed_cut_copper", "minecraft:waxed_weathered_copper", "minecraft:waxed_weathered_cut_copper_slab", "minecraft:waxed_weathered_cut_copper_stairs", "minecraft:waxed_weathered_cut_copper", "minecraft:waxed_exposed_copper", "minecraft:waxed_exposed_cut_copper_slab", "minecraft:waxed_exposed_cut_copper_stairs", "minecraft:waxed_exposed_cut_copper", "minecraft:waxed_oxidized_copper", "minecraft:waxed_oxidized_cut_copper_slab", "minecraft:waxed_oxidized_cut_copper_stairs", "minecraft:waxed_oxidized_cut_copper", "minecraft:lightning_rod", "minecraft:crafter", "minecraft:chiseled_copper", "minecraft:exposed_chiseled_copper", "minecraft:weathered_chiseled_copper", "minecraft:oxidized_chiseled_copper", "minecraft:waxed_chiseled_copper", "minecraft:waxed_exposed_chiseled_copper", "minecraft:waxed_weathered_chiseled_copper", "minecraft:waxed_oxidized_chiseled_copper", "minecraft:copper_grate", "minecraft:exposed_copper_grate", "minecraft:weathered_copper_grate", "minecraft:oxidized_copper_grate", "minecraft:waxed_copper_grate", "minecraft:waxed_exposed_copper_grate", "minecraft:waxed_weathered_copper_grate", "minecraft:waxed_oxidized_copper_grate", "minecraft:copper_bulb", "minecraft:exposed_copper_bulb", "minecraft:weathered_copper_bulb", "minecraft:oxidized_copper_bulb", "minecraft:waxed_copper_bulb", "minecraft:waxed_exposed_copper_bulb", "minecraft:waxed_weathered_copper_bulb", "minecraft:waxed_oxidized_copper_bulb", "minecraft:copper_trapdoor", "minecraft:exposed_copper_trapdoor", "minecraft:weathered_copper_trapdoor", "minecraft:oxidized_copper_trapdoor", "minecraft:waxed_copper_trapdoor", "minecraft:waxed_exposed_copper_trapdoor", "minecraft:waxed_weathered_copper_trapdoor", "minecraft:waxed_oxidized_copper_trapdoor", "minecraft:copper_door", "minecraft:exposed_copper_door", "minecraft:weathered_copper_door", "minecraft:oxidized_copper_door", "minecraft:waxed_copper_door", "minecraft:waxed_exposed_copper_door", "minecraft:waxed_weathered_copper_door", "minecraft:waxed_oxidized_copper_door", NULL);
    AddRegistryTag(registry, "minecraft:wither_immune", "minecraft:barrier", "minecraft:bedrock", "minecraft:end_portal", "minecraft:end_portal_frame", "minecraft:end_gateway", "minecraft:command_block", "minecraft:repeating_command_block", "minecraft:chain_command_block", "minecraft:structure_block", "minecraft:jigsaw", "minecraft:moving_piston", "minecraft:light", "minecraft:reinforced_deepslate", NULL);
    AddRegistryTag(registry, "minecraft:bee_growables", "minecraft:beetroots", "minecraft:carrots", "minecraft:potatoes", "minecraft:wheat", "minecraft:melon_stem", "minecraft:pumpkin_stem", "minecraft:torchflower_crop", "minecraft:pitcher_crop", "minecraft:sweet_berry_bush", "minecraft:cave_vines", "minecraft:cave_vines_plant", NULL);
    AddRegistryTag(registry, "minecraft:logs_that_burn", "minecraft:dark_oak_log", "minecraft:dark_oak_wood", "minecraft:stripped_dark_oak_log", "minecraft:stripped_dark_oak_wood", "minecraft:oak_log", "minecraft:oak_wood", "minecraft:stripped_oak_log", "minecraft:stripped_oak_wood", "minecraft:acacia_log", "minecraft:acacia_wood", "minecraft:stripped_acacia_log", "minecraft:stripped_acacia_wood", "minecraft:birch_log", "minecraft:birch_wood", "minecraft:stripped_birch_log", "minecraft:stripped_birch_wood", "minecraft:jungle_log", "minecraft:jungle_wood", "minecraft:stripped_jungle_log", "minecraft:stripped_jungle_wood", "minecraft:spruce_log", "minecraft:spruce_wood", "minecraft:stripped_spruce_log", "minecraft:stripped_spruce_wood", "minecraft:mangrove_log", "minecraft:mangrove_wood", "minecraft:stripped_mangrove_log", "minecraft:stripped_mangrove_wood", "minecraft:cherry_log", "minecraft:cherry_wood", "minecraft:stripped_cherry_log", "minecraft:stripped_cherry_wood", NULL);
    AddRegistryTag(registry, "minecraft:spruce_logs", "minecraft:spruce_log", "minecraft:spruce_wood", "minecraft:stripped_spruce_log", "minecraft:stripped_spruce_wood", NULL);
    AddRegistryTag(registry, "minecraft:sword_efficient", "minecraft:jungle_leaves", "minecraft:oak_leaves", "minecraft:spruce_leaves", "minecraft:dark_oak_leaves", "minecraft:acacia_leaves", "minecraft:birch_leaves", "minecraft:azalea_leaves", "minecraft:flowering_azalea_leaves", "minecraft:mangrove_leaves", "minecraft:cherry_leaves", "minecraft:oak_sapling", "minecraft:spruce_sapling", "minecraft:birch_sapling", "minecraft:jungle_sapling", "minecraft:acacia_sapling", "minecraft:dark_oak_sapling", "minecraft:azalea", "minecraft:flowering_azalea", "minecraft:mangrove_propagule", "minecraft:cherry_sapling", "minecraft:dandelion", "minecraft:poppy", "minecraft:blue_orchid", "minecraft:allium", "minecraft:azure_bluet", "minecraft:red_tulip", "minecraft:orange_tulip", "minecraft:white_tulip", "minecraft:pink_tulip", "minecraft:oxeye_daisy", "minecraft:cornflower", "minecraft:lily_of_the_valley", "minecraft:wither_rose", "minecraft:torchflower", "minecraft:beetroots", "minecraft:carrots", "minecraft:potatoes", "minecraft:wheat", "minecraft:melon_stem", "minecraft:pumpkin_stem", "minecraft:torchflower_crop", "minecraft:pitcher_crop", "minecraft:short_grass", "minecraft:fern", "minecraft:dead_bush", "minecraft:vine", "minecraft:glow_lichen", "minecraft:sunflower", "minecraft:lilac", "minecraft:rose_bush", "minecraft:peony", "minecraft:tall_grass", "minecraft:large_fern", "minecraft:hanging_roots", "minecraft:pitcher_plant", "minecraft:brown_mushroom", "minecraft:red_mushroom", "minecraft:sugar_cane", "minecraft:pumpkin", "minecraft:carved_pumpkin", "minecraft:jack_o_lantern", "minecraft:melon", "minecraft:attached_pumpkin_stem", "minecraft:attached_melon_stem", "minecraft:lily_pad", "minecraft:cocoa", "minecraft:pitcher_crop", "minecraft:sweet_berry_bush", "minecraft:cave_vines", "minecraft:cave_vines_plant", "minecraft:spore_blossom", "minecraft:moss_carpet", "minecraft:pink_petals", "minecraft:big_dripleaf", "minecraft:big_dripleaf_stem", "minecraft:small_dripleaf", "minecraft:nether_wart", "minecraft:warped_fungus", "minecraft:warped_roots", "minecraft:nether_sprouts", "minecraft:crimson_fungus", "minecraft:weeping_vines", "minecraft:weeping_vines_plant", "minecraft:twisting_vines", "minecraft:twisting_vines_plant", "minecraft:crimson_roots", "minecraft:chorus_plant", "minecraft:chorus_flower", NULL);
    AddRegistryTag(registry, "minecraft:sculk_replaceable", "minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:terracotta", "minecraft:white_terracotta", "minecraft:orange_terracotta", "minecraft:magenta_terracotta", "minecraft:light_blue_terracotta", "minecraft:yellow_terracotta", "minecraft:lime_terracotta", "minecraft:pink_terracotta", "minecraft:gray_terracotta", "minecraft:light_gray_terracotta", "minecraft:cyan_terracotta", "minecraft:purple_terracotta", "minecraft:blue_terracotta", "minecraft:brown_terracotta", "minecraft:green_terracotta", "minecraft:red_terracotta", "minecraft:black_terracotta", "minecraft:crimson_nylium", "minecraft:warped_nylium", "minecraft:netherrack", "minecraft:basalt", "minecraft:blackstone", "minecraft:sand", "minecraft:red_sand", "minecraft:gravel", "minecraft:soul_sand", "minecraft:soul_soil", "minecraft:calcite", "minecraft:smooth_basalt", "minecraft:clay", "minecraft:dripstone_block", "minecraft:end_stone", "minecraft:red_sandstone", "minecraft:sandstone", NULL);
    AddRegistryTag(registry, "minecraft:nether_carver_replaceables", "minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate", "minecraft:netherrack", "minecraft:basalt", "minecraft:blackstone", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:crimson_nylium", "minecraft:warped_nylium", "minecraft:nether_wart_block", "minecraft:warped_wart_block", "minecraft:soul_sand", "minecraft:soul_soil", NULL);
    AddRegistryTag(registry, "minecraft:bamboo_plantable_on", "minecraft:sand", "minecraft:red_sand", "minecraft:suspicious_sand", "minecraft:suspicious_sand", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:bamboo", "minecraft:bamboo_sapling", "minecraft:gravel", "minecraft:suspicious_gravel", NULL);
    AddRegistryTag(registry, "minecraft:infiniburn_nether", "minecraft:netherrack", "minecraft:magma_block", NULL);
    AddRegistryTag(registry, "minecraft:small_dripleaf_placeable", "minecraft:clay", "minecraft:moss_block", NULL);
    AddRegistryTag(registry, "minecraft:fall_damage_resetting", "minecraft:ladder", "minecraft:vine", "minecraft:scaffolding", "minecraft:weeping_vines", "minecraft:weeping_vines_plant", "minecraft:twisting_vines", "minecraft:twisting_vines_plant", "minecraft:cave_vines", "minecraft:cave_vines_plant", "minecraft:sweet_berry_bush", "minecraft:cobweb", NULL);
    AddRegistryTag(registry, "minecraft:mushroom_grow_block", "minecraft:mycelium", "minecraft:podzol", "minecraft:crimson_nylium", "minecraft:warped_nylium", NULL);
    AddRegistryTag(registry, "minecraft:gold_ores", "minecraft:gold_ore", "minecraft:nether_gold_ore", "minecraft:deepslate_gold_ore", NULL);
    AddRegistryTag(registry, "minecraft:invalid_spawn_inside", "minecraft:end_portal", "minecraft:end_gateway", NULL);
    AddRegistryTag(registry, "minecraft:coral_blocks", "minecraft:tube_coral_block", "minecraft:brain_coral_block", "minecraft:bubble_coral_block", "minecraft:fire_coral_block", "minecraft:horn_coral_block", NULL);
    AddRegistryTag(registry, "minecraft:stairs", "minecraft:oak_stairs", "minecraft:spruce_stairs", "minecraft:birch_stairs", "minecraft:jungle_stairs", "minecraft:acacia_stairs", "minecraft:dark_oak_stairs", "minecraft:crimson_stairs", "minecraft:warped_stairs", "minecraft:mangrove_stairs", "minecraft:bamboo_stairs", "minecraft:cherry_stairs", "minecraft:bamboo_mosaic_stairs", "minecraft:cobblestone_stairs", "minecraft:sandstone_stairs", "minecraft:nether_brick_stairs", "minecraft:stone_brick_stairs", "minecraft:brick_stairs", "minecraft:purpur_stairs", "minecraft:quartz_stairs", "minecraft:red_sandstone_stairs", "minecraft:prismarine_brick_stairs", "minecraft:prismarine_stairs", "minecraft:dark_prismarine_stairs", "minecraft:polished_granite_stairs", "minecraft:smooth_red_sandstone_stairs", "minecraft:mossy_stone_brick_stairs", "minecraft:polished_diorite_stairs", "minecraft:mossy_cobblestone_stairs", "minecraft:end_stone_brick_stairs", "minecraft:stone_stairs", "minecraft:smooth_sandstone_stairs", "minecraft:smooth_quartz_stairs", "minecraft:granite_stairs", "minecraft:andesite_stairs", "minecraft:red_nether_brick_stairs", "minecraft:polished_andesite_stairs", "minecraft:diorite_stairs", "minecraft:blackstone_stairs", "minecraft:polished_blackstone_brick_stairs", "minecraft:polished_blackstone_stairs", "minecraft:cobbled_deepslate_stairs", "minecraft:polished_deepslate_stairs", "minecraft:deepslate_tile_stairs", "minecraft:deepslate_brick_stairs", "minecraft:oxidized_cut_copper_stairs", "minecraft:weathered_cut_copper_stairs", "minecraft:exposed_cut_copper_stairs", "minecraft:cut_copper_stairs", "minecraft:waxed_weathered_cut_copper_stairs", "minecraft:waxed_exposed_cut_copper_stairs", "minecraft:waxed_cut_copper_stairs", "minecraft:waxed_oxidized_cut_copper_stairs", "minecraft:mud_brick_stairs", "minecraft:tuff_stairs", "minecraft:polished_tuff_stairs", "minecraft:tuff_brick_stairs", NULL);
    AddRegistryTag(registry, "minecraft:mooshrooms_spawnable_on", "minecraft:mycelium", NULL);
    AddRegistryTag(registry, "minecraft:crimson_stems", "minecraft:crimson_stem", "minecraft:stripped_crimson_stem", "minecraft:crimson_hyphae", "minecraft:stripped_crimson_hyphae", NULL);
    AddRegistryTag(registry, "minecraft:wart_blocks", "minecraft:nether_wart_block", "minecraft:warped_wart_block", NULL);
    AddRegistryTag(registry, "minecraft:unstable_bottom_center", "minecraft:acacia_fence_gate", "minecraft:birch_fence_gate", "minecraft:dark_oak_fence_gate", "minecraft:jungle_fence_gate", "minecraft:oak_fence_gate", "minecraft:spruce_fence_gate", "minecraft:crimson_fence_gate", "minecraft:warped_fence_gate", "minecraft:mangrove_fence_gate", "minecraft:bamboo_fence_gate", "minecraft:cherry_fence_gate", NULL);
    AddRegistryTag(registry, "minecraft:guarded_by_piglins", "minecraft:gold_block", "minecraft:barrel", "minecraft:chest", "minecraft:ender_chest", "minecraft:gilded_blackstone", "minecraft:trapped_chest", "minecraft:raw_gold_block", "minecraft:shulker_box", "minecraft:black_shulker_box", "minecraft:blue_shulker_box", "minecraft:brown_shulker_box", "minecraft:cyan_shulker_box", "minecraft:gray_shulker_box", "minecraft:green_shulker_box", "minecraft:light_blue_shulker_box", "minecraft:light_gray_shulker_box", "minecraft:lime_shulker_box", "minecraft:magenta_shulker_box", "minecraft:orange_shulker_box", "minecraft:pink_shulker_box", "minecraft:purple_shulker_box", "minecraft:red_shulker_box", "minecraft:white_shulker_box", "minecraft:yellow_shulker_box", "minecraft:gold_ore", "minecraft:nether_gold_ore", "minecraft:deepslate_gold_ore", NULL);
    AddRegistryTag(registry, "minecraft:enderman_holdable", "minecraft:dandelion", "minecraft:poppy", "minecraft:blue_orchid", "minecraft:allium", "minecraft:azure_bluet", "minecraft:red_tulip", "minecraft:orange_tulip", "minecraft:white_tulip", "minecraft:pink_tulip", "minecraft:oxeye_daisy", "minecraft:cornflower", "minecraft:lily_of_the_valley", "minecraft:wither_rose", "minecraft:torchflower", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:sand", "minecraft:red_sand", "minecraft:gravel", "minecraft:brown_mushroom", "minecraft:red_mushroom", "minecraft:tnt", "minecraft:cactus", "minecraft:clay", "minecraft:pumpkin", "minecraft:carved_pumpkin", "minecraft:melon", "minecraft:crimson_fungus", "minecraft:crimson_nylium", "minecraft:crimson_roots", "minecraft:warped_fungus", "minecraft:warped_nylium", "minecraft:warped_roots", NULL);
    AddRegistryTag(registry, "minecraft:concrete_powder", "minecraft:white_concrete_powder", "minecraft:orange_concrete_powder", "minecraft:magenta_concrete_powder", "minecraft:light_blue_concrete_powder", "minecraft:yellow_concrete_powder", "minecraft:lime_concrete_powder", "minecraft:pink_concrete_powder", "minecraft:gray_concrete_powder", "minecraft:light_gray_concrete_powder", "minecraft:cyan_concrete_powder", "minecraft:purple_concrete_powder", "minecraft:blue_concrete_powder", "minecraft:brown_concrete_powder", "minecraft:green_concrete_powder", "minecraft:red_concrete_powder", "minecraft:black_concrete_powder", NULL);
    AddRegistryTag(registry, "minecraft:flower_pots", "minecraft:flower_pot", "minecraft:potted_poppy", "minecraft:potted_blue_orchid", "minecraft:potted_allium", "minecraft:potted_azure_bluet", "minecraft:potted_red_tulip", "minecraft:potted_orange_tulip", "minecraft:potted_white_tulip", "minecraft:potted_pink_tulip", "minecraft:potted_oxeye_daisy", "minecraft:potted_dandelion", "minecraft:potted_oak_sapling", "minecraft:potted_spruce_sapling", "minecraft:potted_birch_sapling", "minecraft:potted_jungle_sapling", "minecraft:potted_acacia_sapling", "minecraft:potted_dark_oak_sapling", "minecraft:potted_red_mushroom", "minecraft:potted_brown_mushroom", "minecraft:potted_dead_bush", "minecraft:potted_fern", "minecraft:potted_cactus", "minecraft:potted_cornflower", "minecraft:potted_lily_of_the_valley", "minecraft:potted_wither_rose", "minecraft:potted_bamboo", "minecraft:potted_crimson_fungus", "minecraft:potted_warped_fungus", "minecraft:potted_crimson_roots", "minecraft:potted_warped_roots", "minecraft:potted_azalea_bush", "minecraft:potted_flowering_azalea_bush", "minecraft:potted_mangrove_propagule", "minecraft:potted_cherry_sapling", "minecraft:potted_torchflower", NULL);
    AddRegistryTag(registry, "minecraft:ceiling_hanging_signs", "minecraft:oak_hanging_sign", "minecraft:spruce_hanging_sign", "minecraft:birch_hanging_sign", "minecraft:acacia_hanging_sign", "minecraft:cherry_hanging_sign", "minecraft:jungle_hanging_sign", "minecraft:dark_oak_hanging_sign", "minecraft:crimson_hanging_sign", "minecraft:warped_hanging_sign", "minecraft:mangrove_hanging_sign", "minecraft:bamboo_hanging_sign", NULL);
    AddRegistryTag(registry, "minecraft:overworld_natural_logs", "minecraft:acacia_log", "minecraft:birch_log", "minecraft:oak_log", "minecraft:jungle_log", "minecraft:spruce_log", "minecraft:dark_oak_log", "minecraft:mangrove_log", "minecraft:cherry_log", NULL);
    AddRegistryTag(registry, "minecraft:crystal_sound_blocks", "minecraft:amethyst_block", "minecraft:budding_amethyst", NULL);
    AddRegistryTag(registry, "minecraft:fences", "minecraft:oak_fence", "minecraft:acacia_fence", "minecraft:dark_oak_fence", "minecraft:spruce_fence", "minecraft:birch_fence", "minecraft:jungle_fence", "minecraft:crimson_fence", "minecraft:warped_fence", "minecraft:mangrove_fence", "minecraft:bamboo_fence", "minecraft:cherry_fence", "minecraft:nether_brick_fence", NULL);
    AddRegistryTag(registry, "minecraft:occludes_vibration_signals", "minecraft:white_wool", "minecraft:orange_wool", "minecraft:magenta_wool", "minecraft:light_blue_wool", "minecraft:yellow_wool", "minecraft:lime_wool", "minecraft:pink_wool", "minecraft:gray_wool", "minecraft:light_gray_wool", "minecraft:cyan_wool", "minecraft:purple_wool", "minecraft:blue_wool", "minecraft:brown_wool", "minecraft:green_wool", "minecraft:red_wool", "minecraft:black_wool", NULL);
    AddRegistryTag(registry, "minecraft:oak_logs", "minecraft:oak_log", "minecraft:oak_wood", "minecraft:stripped_oak_log", "minecraft:stripped_oak_wood", NULL);
    AddRegistryTag(registry, "minecraft:stone_ore_replaceables", "minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", NULL);
    AddRegistryTag(registry, "minecraft:iron_ores", "minecraft:iron_ore", "minecraft:deepslate_iron_ore", NULL);
    AddRegistryTag(registry, "minecraft:crops", "minecraft:beetroots", "minecraft:carrots", "minecraft:potatoes", "minecraft:wheat", "minecraft:melon_stem", "minecraft:pumpkin_stem", "minecraft:torchflower_crop", "minecraft:pitcher_crop", NULL);
    AddRegistryTag(registry, "minecraft:wall_post_override", "minecraft:torch", "minecraft:soul_torch", "minecraft:redstone_torch", "minecraft:tripwire", "minecraft:oak_sign", "minecraft:spruce_sign", "minecraft:birch_sign", "minecraft:acacia_sign", "minecraft:jungle_sign", "minecraft:dark_oak_sign", "minecraft:crimson_sign", "minecraft:warped_sign", "minecraft:mangrove_sign", "minecraft:bamboo_sign", "minecraft:cherry_sign", "minecraft:oak_wall_sign", "minecraft:spruce_wall_sign", "minecraft:birch_wall_sign", "minecraft:acacia_wall_sign", "minecraft:jungle_wall_sign", "minecraft:dark_oak_wall_sign", "minecraft:crimson_wall_sign", "minecraft:warped_wall_sign", "minecraft:mangrove_wall_sign", "minecraft:bamboo_wall_sign", "minecraft:cherry_wall_sign", "minecraft:white_banner", "minecraft:orange_banner", "minecraft:magenta_banner", "minecraft:light_blue_banner", "minecraft:yellow_banner", "minecraft:lime_banner", "minecraft:pink_banner", "minecraft:gray_banner", "minecraft:light_gray_banner", "minecraft:cyan_banner", "minecraft:purple_banner", "minecraft:blue_banner", "minecraft:brown_banner", "minecraft:green_banner", "minecraft:red_banner", "minecraft:black_banner", "minecraft:white_wall_banner", "minecraft:orange_wall_banner", "minecraft:magenta_wall_banner", "minecraft:light_blue_wall_banner", "minecraft:yellow_wall_banner", "minecraft:lime_wall_banner", "minecraft:pink_wall_banner", "minecraft:gray_wall_banner", "minecraft:light_gray_wall_banner", "minecraft:cyan_wall_banner", "minecraft:purple_wall_banner", "minecraft:blue_wall_banner", "minecraft:brown_wall_banner", "minecraft:green_wall_banner", "minecraft:red_wall_banner", "minecraft:black_wall_banner", "minecraft:light_weighted_pressure_plate", "minecraft:heavy_weighted_pressure_plate", "minecraft:oak_pressure_plate", "minecraft:spruce_pressure_plate", "minecraft:birch_pressure_plate", "minecraft:jungle_pressure_plate", "minecraft:acacia_pressure_plate", "minecraft:dark_oak_pressure_plate", "minecraft:crimson_pressure_plate", "minecraft:warped_pressure_plate", "minecraft:mangrove_pressure_plate", "minecraft:bamboo_pressure_plate", "minecraft:cherry_pressure_plate", "minecraft:stone_pressure_plate", "minecraft:polished_blackstone_pressure_plate", NULL);
    AddRegistryTag(registry, "minecraft:stone_pressure_plates", "minecraft:stone_pressure_plate", "minecraft:polished_blackstone_pressure_plate", NULL);
    AddRegistryTag(registry, "minecraft:wool_carpets", "minecraft:white_carpet", "minecraft:orange_carpet", "minecraft:magenta_carpet", "minecraft:light_blue_carpet", "minecraft:yellow_carpet", "minecraft:lime_carpet", "minecraft:pink_carpet", "minecraft:gray_carpet", "minecraft:light_gray_carpet", "minecraft:cyan_carpet", "minecraft:purple_carpet", "minecraft:blue_carpet", "minecraft:brown_carpet", "minecraft:green_carpet", "minecraft:red_carpet", "minecraft:black_carpet", NULL);
    AddRegistryTag(registry, "minecraft:combination_step_sound_blocks", "minecraft:white_carpet", "minecraft:orange_carpet", "minecraft:magenta_carpet", "minecraft:light_blue_carpet", "minecraft:yellow_carpet", "minecraft:lime_carpet", "minecraft:pink_carpet", "minecraft:gray_carpet", "minecraft:light_gray_carpet", "minecraft:cyan_carpet", "minecraft:purple_carpet", "minecraft:blue_carpet", "minecraft:brown_carpet", "minecraft:green_carpet", "minecraft:red_carpet", "minecraft:black_carpet", "minecraft:moss_carpet", "minecraft:snow", "minecraft:nether_sprouts", "minecraft:warped_roots", "minecraft:crimson_roots", NULL);
    AddRegistryTag(registry, "minecraft:lava_pool_stone_cannot_replace", "minecraft:bedrock", "minecraft:spawner", "minecraft:chest", "minecraft:end_portal_frame", "minecraft:reinforced_deepslate", "minecraft:trial_spawner", "minecraft:vault", "minecraft:jungle_leaves", "minecraft:oak_leaves", "minecraft:spruce_leaves", "minecraft:dark_oak_leaves", "minecraft:acacia_leaves", "minecraft:birch_leaves", "minecraft:azalea_leaves", "minecraft:flowering_azalea_leaves", "minecraft:mangrove_leaves", "minecraft:cherry_leaves", "minecraft:dark_oak_log", "minecraft:dark_oak_wood", "minecraft:stripped_dark_oak_log", "minecraft:stripped_dark_oak_wood", "minecraft:oak_log", "minecraft:oak_wood", "minecraft:stripped_oak_log", "minecraft:stripped_oak_wood", "minecraft:acacia_log", "minecraft:acacia_wood", "minecraft:stripped_acacia_log", "minecraft:stripped_acacia_wood", "minecraft:birch_log", "minecraft:birch_wood", "minecraft:stripped_birch_log", "minecraft:stripped_birch_wood", "minecraft:jungle_log", "minecraft:jungle_wood", "minecraft:stripped_jungle_log", "minecraft:stripped_jungle_wood", "minecraft:spruce_log", "minecraft:spruce_wood", "minecraft:stripped_spruce_log", "minecraft:stripped_spruce_wood", "minecraft:mangrove_log", "minecraft:mangrove_wood", "minecraft:stripped_mangrove_log", "minecraft:stripped_mangrove_wood", "minecraft:cherry_log", "minecraft:cherry_wood", "minecraft:stripped_cherry_log", "minecraft:stripped_cherry_wood", "minecraft:crimson_stem", "minecraft:stripped_crimson_stem", "minecraft:crimson_hyphae", "minecraft:stripped_crimson_hyphae", "minecraft:warped_stem", "minecraft:stripped_warped_stem", "minecraft:warped_hyphae", "minecraft:stripped_warped_hyphae", NULL);
    AddRegistryTag(registry, "minecraft:bats_spawnable_on", "minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate", NULL);
    AddRegistryTag(registry, "minecraft:maintains_farmland", "minecraft:pumpkin_stem", "minecraft:attached_pumpkin_stem", "minecraft:melon_stem", "minecraft:attached_melon_stem", "minecraft:beetroots", "minecraft:carrots", "minecraft:potatoes", "minecraft:torchflower_crop", "minecraft:torchflower", "minecraft:pitcher_crop", "minecraft:wheat", NULL);
    AddRegistryTag(registry, "minecraft:portals", "minecraft:nether_portal", "minecraft:end_portal", "minecraft:end_gateway", NULL);
    AddRegistryTag(registry, "minecraft:goats_spawnable_on", "minecraft:grass_block", "minecraft:stone", "minecraft:snow", "minecraft:snow_block", "minecraft:packed_ice", "minecraft:gravel", NULL);
    AddRegistryTag(registry, "minecraft:incorrect_for_diamond_tool", NULL);
    AddRegistryTag(registry, "minecraft:features_cannot_replace", "minecraft:bedrock", "minecraft:spawner", "minecraft:chest", "minecraft:end_portal_frame", "minecraft:reinforced_deepslate", "minecraft:trial_spawner", "minecraft:vault", NULL);
    AddRegistryTag(registry, "minecraft:bamboo_blocks", "minecraft:bamboo_block", "minecraft:stripped_bamboo_block", NULL);
    AddRegistryTag(registry, "minecraft:ice", "minecraft:ice", "minecraft:packed_ice", "minecraft:blue_ice", "minecraft:frosted_ice", NULL);
    AddRegistryTag(registry, "minecraft:ancient_city_replaceable", "minecraft:deepslate", "minecraft:deepslate_bricks", "minecraft:deepslate_tiles", "minecraft:deepslate_brick_slab", "minecraft:deepslate_tile_slab", "minecraft:deepslate_brick_stairs", "minecraft:deepslate_tile_wall", "minecraft:deepslate_brick_wall", "minecraft:cobbled_deepslate", "minecraft:cracked_deepslate_bricks", "minecraft:cracked_deepslate_tiles", "minecraft:gray_wool", NULL);
    AddRegistryTag(registry, "minecraft:small_flowers", "minecraft:dandelion", "minecraft:poppy", "minecraft:blue_orchid", "minecraft:allium", "minecraft:azure_bluet", "minecraft:red_tulip", "minecraft:orange_tulip", "minecraft:white_tulip", "minecraft:pink_tulip", "minecraft:oxeye_daisy", "minecraft:cornflower", "minecraft:lily_of_the_valley", "minecraft:wither_rose", "minecraft:torchflower", NULL);
    AddRegistryTag(registry, "minecraft:azalea_root_replaceable", "minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:terracotta", "minecraft:white_terracotta", "minecraft:orange_terracotta", "minecraft:magenta_terracotta", "minecraft:light_blue_terracotta", "minecraft:yellow_terracotta", "minecraft:lime_terracotta", "minecraft:pink_terracotta", "minecraft:gray_terracotta", "minecraft:light_gray_terracotta", "minecraft:cyan_terracotta", "minecraft:purple_terracotta", "minecraft:blue_terracotta", "minecraft:brown_terracotta", "minecraft:green_terracotta", "minecraft:red_terracotta", "minecraft:black_terracotta", "minecraft:red_sand", "minecraft:clay", "minecraft:gravel", "minecraft:sand", "minecraft:snow_block", "minecraft:powder_snow", NULL);
    AddRegistryTag(registry, "minecraft:replaceable_by_trees", "minecraft:jungle_leaves", "minecraft:oak_leaves", "minecraft:spruce_leaves", "minecraft:dark_oak_leaves", "minecraft:acacia_leaves", "minecraft:birch_leaves", "minecraft:azalea_leaves", "minecraft:flowering_azalea_leaves", "minecraft:mangrove_leaves", "minecraft:cherry_leaves", "minecraft:short_grass", "minecraft:fern", "minecraft:dead_bush", "minecraft:vine", "minecraft:glow_lichen", "minecraft:sunflower", "minecraft:lilac", "minecraft:rose_bush", "minecraft:peony", "minecraft:tall_grass", "minecraft:large_fern", "minecraft:hanging_roots", "minecraft:pitcher_plant", "minecraft:water", "minecraft:seagrass", "minecraft:tall_seagrass", "minecraft:warped_roots", "minecraft:nether_sprouts", "minecraft:crimson_roots", NULL);
    AddRegistryTag(registry, "minecraft:dragon_immune", "minecraft:barrier", "minecraft:bedrock", "minecraft:end_portal", "minecraft:end_portal_frame", "minecraft:end_gateway", "minecraft:command_block", "minecraft:repeating_command_block", "minecraft:chain_command_block", "minecraft:structure_block", "minecraft:jigsaw", "minecraft:moving_piston", "minecraft:obsidian", "minecraft:crying_obsidian", "minecraft:end_stone", "minecraft:iron_bars", "minecraft:respawn_anchor", "minecraft:reinforced_deepslate", NULL);
    AddRegistryTag(registry, "minecraft:diamond_ores", "minecraft:diamond_ore", "minecraft:deepslate_diamond_ore", NULL);
    AddRegistryTag(registry, "minecraft:snow_layer_can_survive_on", "minecraft:honey_block", "minecraft:soul_sand", "minecraft:mud", NULL);
    AddRegistryTag(registry, "minecraft:slabs", "minecraft:oak_slab", "minecraft:spruce_slab", "minecraft:birch_slab", "minecraft:jungle_slab", "minecraft:acacia_slab", "minecraft:dark_oak_slab", "minecraft:crimson_slab", "minecraft:warped_slab", "minecraft:mangrove_slab", "minecraft:bamboo_slab", "minecraft:cherry_slab", "minecraft:bamboo_mosaic_slab", "minecraft:stone_slab", "minecraft:smooth_stone_slab", "minecraft:stone_brick_slab", "minecraft:sandstone_slab", "minecraft:purpur_slab", "minecraft:quartz_slab", "minecraft:red_sandstone_slab", "minecraft:brick_slab", "minecraft:cobblestone_slab", "minecraft:nether_brick_slab", "minecraft:petrified_oak_slab", "minecraft:prismarine_slab", "minecraft:prismarine_brick_slab", "minecraft:dark_prismarine_slab", "minecraft:polished_granite_slab", "minecraft:smooth_red_sandstone_slab", "minecraft:mossy_stone_brick_slab", "minecraft:polished_diorite_slab", "minecraft:mossy_cobblestone_slab", "minecraft:end_stone_brick_slab", "minecraft:smooth_sandstone_slab", "minecraft:smooth_quartz_slab", "minecraft:granite_slab", "minecraft:andesite_slab", "minecraft:red_nether_brick_slab", "minecraft:polished_andesite_slab", "minecraft:diorite_slab", "minecraft:cut_sandstone_slab", "minecraft:cut_red_sandstone_slab", "minecraft:blackstone_slab", "minecraft:polished_blackstone_brick_slab", "minecraft:polished_blackstone_slab", "minecraft:cobbled_deepslate_slab", "minecraft:polished_deepslate_slab", "minecraft:deepslate_tile_slab", "minecraft:deepslate_brick_slab", "minecraft:waxed_weathered_cut_copper_slab", "minecraft:waxed_exposed_cut_copper_slab", "minecraft:waxed_cut_copper_slab", "minecraft:oxidized_cut_copper_slab", "minecraft:weathered_cut_copper_slab", "minecraft:exposed_cut_copper_slab", "minecraft:cut_copper_slab", "minecraft:waxed_oxidized_cut_copper_slab", "minecraft:mud_brick_slab", "minecraft:tuff_slab", "minecraft:polished_tuff_slab", "minecraft:tuff_brick_slab", NULL);
    AddRegistryTag(registry, "minecraft:copper_ores", "minecraft:copper_ore", "minecraft:deepslate_copper_ore", NULL);
    AddRegistryTag(registry, "minecraft:axolotls_spawnable_on", "minecraft:clay", NULL);
    AddRegistryTag(registry, "minecraft:beds", "minecraft:red_bed", "minecraft:black_bed", "minecraft:blue_bed", "minecraft:brown_bed", "minecraft:cyan_bed", "minecraft:gray_bed", "minecraft:green_bed", "minecraft:light_blue_bed", "minecraft:light_gray_bed", "minecraft:lime_bed", "minecraft:magenta_bed", "minecraft:orange_bed", "minecraft:pink_bed", "minecraft:purple_bed", "minecraft:white_bed", "minecraft:yellow_bed", NULL);
    AddRegistryTag(registry, "minecraft:fence_gates", "minecraft:acacia_fence_gate", "minecraft:birch_fence_gate", "minecraft:dark_oak_fence_gate", "minecraft:jungle_fence_gate", "minecraft:oak_fence_gate", "minecraft:spruce_fence_gate", "minecraft:crimson_fence_gate", "minecraft:warped_fence_gate", "minecraft:mangrove_fence_gate", "minecraft:bamboo_fence_gate", "minecraft:cherry_fence_gate", NULL);
    AddRegistryTag(registry, "minecraft:wooden_slabs", "minecraft:oak_slab", "minecraft:spruce_slab", "minecraft:birch_slab", "minecraft:jungle_slab", "minecraft:acacia_slab", "minecraft:dark_oak_slab", "minecraft:crimson_slab", "minecraft:warped_slab", "minecraft:mangrove_slab", "minecraft:bamboo_slab", "minecraft:cherry_slab", NULL);
    AddRegistryTag(registry, "minecraft:dark_oak_logs", "minecraft:dark_oak_log", "minecraft:dark_oak_wood", "minecraft:stripped_dark_oak_log", "minecraft:stripped_dark_oak_wood", NULL);
    AddRegistryTag(registry, "minecraft:infiniburn_end", "minecraft:netherrack", "minecraft:magma_block", "minecraft:bedrock", NULL);
    AddRegistryTag(registry, "minecraft:azalea_grows_on", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:sand", "minecraft:red_sand", "minecraft:suspicious_sand", "minecraft:suspicious_sand", "minecraft:terracotta", "minecraft:white_terracotta", "minecraft:orange_terracotta", "minecraft:magenta_terracotta", "minecraft:light_blue_terracotta", "minecraft:yellow_terracotta", "minecraft:lime_terracotta", "minecraft:pink_terracotta", "minecraft:gray_terracotta", "minecraft:light_gray_terracotta", "minecraft:cyan_terracotta", "minecraft:purple_terracotta", "minecraft:blue_terracotta", "minecraft:brown_terracotta", "minecraft:green_terracotta", "minecraft:red_terracotta", "minecraft:black_terracotta", "minecraft:snow_block", "minecraft:powder_snow", NULL);
    AddRegistryTag(registry, "minecraft:soul_speed_blocks", "minecraft:soul_sand", "minecraft:soul_soil", NULL);
    AddRegistryTag(registry, "minecraft:overworld_carver_replaceables", "minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:sand", "minecraft:red_sand", "minecraft:suspicious_sand", "minecraft:suspicious_sand", "minecraft:terracotta", "minecraft:white_terracotta", "minecraft:orange_terracotta", "minecraft:magenta_terracotta", "minecraft:light_blue_terracotta", "minecraft:yellow_terracotta", "minecraft:lime_terracotta", "minecraft:pink_terracotta", "minecraft:gray_terracotta", "minecraft:light_gray_terracotta", "minecraft:cyan_terracotta", "minecraft:purple_terracotta", "minecraft:blue_terracotta", "minecraft:brown_terracotta", "minecraft:green_terracotta", "minecraft:red_terracotta", "minecraft:black_terracotta", "minecraft:iron_ore", "minecraft:deepslate_iron_ore", "minecraft:copper_ore", "minecraft:deepslate_copper_ore", "minecraft:water", "minecraft:gravel", "minecraft:suspicious_gravel", "minecraft:sandstone", "minecraft:red_sandstone", "minecraft:calcite", "minecraft:snow", "minecraft:packed_ice", "minecraft:raw_iron_block", "minecraft:raw_copper_block", NULL);
    AddRegistryTag(registry, "minecraft:flowers", "minecraft:dandelion", "minecraft:poppy", "minecraft:blue_orchid", "minecraft:allium", "minecraft:azure_bluet", "minecraft:red_tulip", "minecraft:orange_tulip", "minecraft:white_tulip", "minecraft:pink_tulip", "minecraft:oxeye_daisy", "minecraft:cornflower", "minecraft:lily_of_the_valley", "minecraft:wither_rose", "minecraft:torchflower", "minecraft:sunflower", "minecraft:lilac", "minecraft:peony", "minecraft:rose_bush", "minecraft:pitcher_plant", "minecraft:flowering_azalea_leaves", "minecraft:flowering_azalea", "minecraft:mangrove_propagule", "minecraft:cherry_leaves", "minecraft:pink_petals", "minecraft:chorus_flower", "minecraft:spore_blossom", NULL);
    AddRegistryTag(registry, "minecraft:impermeable", "minecraft:glass", "minecraft:white_stained_glass", "minecraft:orange_stained_glass", "minecraft:magenta_stained_glass", "minecraft:light_blue_stained_glass", "minecraft:yellow_stained_glass", "minecraft:lime_stained_glass", "minecraft:pink_stained_glass", "minecraft:gray_stained_glass", "minecraft:light_gray_stained_glass", "minecraft:cyan_stained_glass", "minecraft:purple_stained_glass", "minecraft:blue_stained_glass", "minecraft:brown_stained_glass", "minecraft:green_stained_glass", "minecraft:red_stained_glass", "minecraft:black_stained_glass", "minecraft:tinted_glass", NULL);
    AddRegistryTag(registry, "minecraft:snow", "minecraft:snow", "minecraft:snow_block", "minecraft:powder_snow", NULL);
    AddRegistryTag(registry, "minecraft:redstone_ores", "minecraft:redstone_ore", "minecraft:deepslate_redstone_ore", NULL);
    AddRegistryTag(registry, "minecraft:all_signs", "minecraft:oak_sign", "minecraft:spruce_sign", "minecraft:birch_sign", "minecraft:acacia_sign", "minecraft:jungle_sign", "minecraft:dark_oak_sign", "minecraft:crimson_sign", "minecraft:warped_sign", "minecraft:mangrove_sign", "minecraft:bamboo_sign", "minecraft:cherry_sign", "minecraft:oak_wall_sign", "minecraft:spruce_wall_sign", "minecraft:birch_wall_sign", "minecraft:acacia_wall_sign", "minecraft:jungle_wall_sign", "minecraft:dark_oak_wall_sign", "minecraft:crimson_wall_sign", "minecraft:warped_wall_sign", "minecraft:mangrove_wall_sign", "minecraft:bamboo_wall_sign", "minecraft:cherry_wall_sign", "minecraft:oak_hanging_sign", "minecraft:spruce_hanging_sign", "minecraft:birch_hanging_sign", "minecraft:acacia_hanging_sign", "minecraft:cherry_hanging_sign", "minecraft:jungle_hanging_sign", "minecraft:dark_oak_hanging_sign", "minecraft:crimson_hanging_sign", "minecraft:warped_hanging_sign", "minecraft:mangrove_hanging_sign", "minecraft:bamboo_hanging_sign", "minecraft:oak_wall_hanging_sign", "minecraft:spruce_wall_hanging_sign", "minecraft:birch_wall_hanging_sign", "minecraft:acacia_wall_hanging_sign", "minecraft:cherry_wall_hanging_sign", "minecraft:jungle_wall_hanging_sign", "minecraft:dark_oak_wall_hanging_sign", "minecraft:crimson_wall_hanging_sign", "minecraft:warped_wall_hanging_sign", "minecraft:mangrove_wall_hanging_sign", "minecraft:bamboo_wall_hanging_sign", NULL);
    AddRegistryTag(registry, "minecraft:does_not_block_hoppers", "minecraft:bee_nest", "minecraft:beehive", NULL);
    AddRegistryTag(registry, "minecraft:incorrect_for_iron_tool", "minecraft:obsidian", "minecraft:crying_obsidian", "minecraft:netherite_block", "minecraft:respawn_anchor", "minecraft:ancient_debris", NULL);
    AddRegistryTag(registry, "minecraft:dirt", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", NULL);
    AddRegistryTag(registry, "minecraft:underwater_bonemeals", "minecraft:seagrass", "minecraft:tube_coral", "minecraft:brain_coral", "minecraft:bubble_coral", "minecraft:fire_coral", "minecraft:horn_coral", "minecraft:tube_coral_fan", "minecraft:brain_coral_fan", "minecraft:bubble_coral_fan", "minecraft:fire_coral_fan", "minecraft:horn_coral_fan", "minecraft:tube_coral_wall_fan", "minecraft:brain_coral_wall_fan", "minecraft:bubble_coral_wall_fan", "minecraft:fire_coral_wall_fan", "minecraft:horn_coral_wall_fan", NULL);
    AddRegistryTag(registry, "minecraft:polar_bears_spawnable_on_alternate", "minecraft:ice", NULL);
    AddRegistryTag(registry, "minecraft:needs_iron_tool", "minecraft:diamond_block", "minecraft:diamond_ore", "minecraft:deepslate_diamond_ore", "minecraft:emerald_ore", "minecraft:deepslate_emerald_ore", "minecraft:emerald_block", "minecraft:gold_block", "minecraft:raw_gold_block", "minecraft:gold_ore", "minecraft:deepslate_gold_ore", "minecraft:redstone_ore", "minecraft:deepslate_redstone_ore", NULL);
    AddRegistryTag(registry, "minecraft:corals", "minecraft:tube_coral", "minecraft:brain_coral", "minecraft:bubble_coral", "minecraft:fire_coral", "minecraft:horn_coral", "minecraft:tube_coral_fan", "minecraft:brain_coral_fan", "minecraft:bubble_coral_fan", "minecraft:fire_coral_fan", "minecraft:horn_coral_fan", NULL);
    AddRegistryTag(registry, "minecraft:infiniburn_overworld", "minecraft:netherrack", "minecraft:magma_block", NULL);
    AddRegistryTag(registry, "minecraft:blocks_wind_charge_explosions", "minecraft:barrier", "minecraft:bedrock", NULL);
    AddRegistryTag(registry, "minecraft:needs_diamond_tool", "minecraft:obsidian", "minecraft:crying_obsidian", "minecraft:netherite_block", "minecraft:respawn_anchor", "minecraft:ancient_debris", NULL);
    AddRegistryTag(registry, "minecraft:incorrect_for_netherite_tool", NULL);
    AddRegistryTag(registry, "minecraft:leaves", "minecraft:jungle_leaves", "minecraft:oak_leaves", "minecraft:spruce_leaves", "minecraft:dark_oak_leaves", "minecraft:acacia_leaves", "minecraft:birch_leaves", "minecraft:azalea_leaves", "minecraft:flowering_azalea_leaves", "minecraft:mangrove_leaves", "minecraft:cherry_leaves", NULL);
    AddRegistryTag(registry, "minecraft:walls", "minecraft:cobblestone_wall", "minecraft:mossy_cobblestone_wall", "minecraft:brick_wall", "minecraft:prismarine_wall", "minecraft:red_sandstone_wall", "minecraft:mossy_stone_brick_wall", "minecraft:granite_wall", "minecraft:stone_brick_wall", "minecraft:nether_brick_wall", "minecraft:andesite_wall", "minecraft:red_nether_brick_wall", "minecraft:sandstone_wall", "minecraft:end_stone_brick_wall", "minecraft:diorite_wall", "minecraft:blackstone_wall", "minecraft:polished_blackstone_brick_wall", "minecraft:polished_blackstone_wall", "minecraft:cobbled_deepslate_wall", "minecraft:polished_deepslate_wall", "minecraft:deepslate_tile_wall", "minecraft:deepslate_brick_wall", "minecraft:mud_brick_wall", "minecraft:tuff_wall", "minecraft:polished_tuff_wall", "minecraft:tuff_brick_wall", NULL);
    AddRegistryTag(registry, "minecraft:base_stone_nether", "minecraft:netherrack", "minecraft:basalt", "minecraft:blackstone", NULL);
    AddRegistryTag(registry, "minecraft:wooden_doors", "minecraft:oak_door", "minecraft:spruce_door", "minecraft:birch_door", "minecraft:jungle_door", "minecraft:acacia_door", "minecraft:dark_oak_door", "minecraft:crimson_door", "minecraft:warped_door", "minecraft:mangrove_door", "minecraft:bamboo_door", "minecraft:cherry_door", NULL);
    AddRegistryTag(registry, "minecraft:lapis_ores", "minecraft:lapis_ore", "minecraft:deepslate_lapis_ore", NULL);
    AddRegistryTag(registry, "minecraft:vibration_resonators", "minecraft:amethyst_block", NULL);
    AddRegistryTag(registry, "minecraft:banners", "minecraft:white_banner", "minecraft:orange_banner", "minecraft:magenta_banner", "minecraft:light_blue_banner", "minecraft:yellow_banner", "minecraft:lime_banner", "minecraft:pink_banner", "minecraft:gray_banner", "minecraft:light_gray_banner", "minecraft:cyan_banner", "minecraft:purple_banner", "minecraft:blue_banner", "minecraft:brown_banner", "minecraft:green_banner", "minecraft:red_banner", "minecraft:black_banner", "minecraft:white_wall_banner", "minecraft:orange_wall_banner", "minecraft:magenta_wall_banner", "minecraft:light_blue_wall_banner", "minecraft:yellow_wall_banner", "minecraft:lime_wall_banner", "minecraft:pink_wall_banner", "minecraft:gray_wall_banner", "minecraft:light_gray_wall_banner", "minecraft:cyan_wall_banner", "minecraft:purple_wall_banner", "minecraft:blue_wall_banner", "minecraft:brown_wall_banner", "minecraft:green_wall_banner", "minecraft:red_wall_banner", "minecraft:black_wall_banner", NULL);
    AddRegistryTag(registry, "minecraft:stone_bricks", "minecraft:stone_bricks", "minecraft:mossy_stone_bricks", "minecraft:cracked_stone_bricks", "minecraft:chiseled_stone_bricks", NULL);
    AddRegistryTag(registry, "minecraft:candle_cakes", "minecraft:candle_cake", "minecraft:white_candle_cake", "minecraft:orange_candle_cake", "minecraft:magenta_candle_cake", "minecraft:light_blue_candle_cake", "minecraft:yellow_candle_cake", "minecraft:lime_candle_cake", "minecraft:pink_candle_cake", "minecraft:gray_candle_cake", "minecraft:light_gray_candle_cake", "minecraft:cyan_candle_cake", "minecraft:purple_candle_cake", "minecraft:blue_candle_cake", "minecraft:brown_candle_cake", "minecraft:green_candle_cake", "minecraft:red_candle_cake", "minecraft:black_candle_cake", NULL);
    AddRegistryTag(registry, "minecraft:acacia_logs", "minecraft:acacia_log", "minecraft:acacia_wood", "minecraft:stripped_acacia_log", "minecraft:stripped_acacia_wood", NULL);
    AddRegistryTag(registry, "minecraft:doors", "minecraft:oak_door", "minecraft:spruce_door", "minecraft:birch_door", "minecraft:jungle_door", "minecraft:acacia_door", "minecraft:dark_oak_door", "minecraft:crimson_door", "minecraft:warped_door", "minecraft:mangrove_door", "minecraft:bamboo_door", "minecraft:cherry_door", "minecraft:copper_door", "minecraft:exposed_copper_door", "minecraft:weathered_copper_door", "minecraft:oxidized_copper_door", "minecraft:waxed_copper_door", "minecraft:waxed_exposed_copper_door", "minecraft:waxed_weathered_copper_door", "minecraft:waxed_oxidized_copper_door", "minecraft:iron_door", NULL);
    AddRegistryTag(registry, "minecraft:piglin_repellents", "minecraft:soul_fire", "minecraft:soul_torch", "minecraft:soul_lantern", "minecraft:soul_wall_torch", "minecraft:soul_campfire", NULL);
    AddRegistryTag(registry, "minecraft:wooden_trapdoors", "minecraft:acacia_trapdoor", "minecraft:birch_trapdoor", "minecraft:dark_oak_trapdoor", "minecraft:jungle_trapdoor", "minecraft:oak_trapdoor", "minecraft:spruce_trapdoor", "minecraft:crimson_trapdoor", "minecraft:warped_trapdoor", "minecraft:mangrove_trapdoor", "minecraft:bamboo_trapdoor", "minecraft:cherry_trapdoor", NULL);
    AddRegistryTag(registry, "minecraft:air", "minecraft:air", "minecraft:void_air", "minecraft:cave_air", NULL);
    AddRegistryTag(registry, "minecraft:snow_layer_cannot_survive_on", "minecraft:ice", "minecraft:packed_ice", "minecraft:barrier", NULL);
    AddRegistryTag(registry, "minecraft:armadillo_spawnable_on", "minecraft:grass_block", "minecraft:terracotta", "minecraft:white_terracotta", "minecraft:yellow_terracotta", "minecraft:orange_terracotta", "minecraft:red_terracotta", "minecraft:brown_terracotta", "minecraft:light_gray_terracotta", "minecraft:red_sand", "minecraft:coarse_dirt", NULL);
    AddRegistryTag(registry, "minecraft:anvil", "minecraft:anvil", "minecraft:chipped_anvil", "minecraft:damaged_anvil", NULL);
    AddRegistryTag(registry, "minecraft:buttons", "minecraft:oak_button", "minecraft:spruce_button", "minecraft:birch_button", "minecraft:jungle_button", "minecraft:acacia_button", "minecraft:dark_oak_button", "minecraft:crimson_button", "minecraft:warped_button", "minecraft:mangrove_button", "minecraft:bamboo_button", "minecraft:cherry_button", "minecraft:stone_button", "minecraft:polished_blackstone_button", NULL);
    AddRegistryTag(registry, "minecraft:parrots_spawnable_on", "minecraft:grass_block", "minecraft:air", "minecraft:jungle_leaves", "minecraft:oak_leaves", "minecraft:spruce_leaves", "minecraft:dark_oak_leaves", "minecraft:acacia_leaves", "minecraft:birch_leaves", "minecraft:azalea_leaves", "minecraft:flowering_azalea_leaves", "minecraft:mangrove_leaves", "minecraft:cherry_leaves", "minecraft:dark_oak_log", "minecraft:dark_oak_wood", "minecraft:stripped_dark_oak_log", "minecraft:stripped_dark_oak_wood", "minecraft:oak_log", "minecraft:oak_wood", "minecraft:stripped_oak_log", "minecraft:stripped_oak_wood", "minecraft:acacia_log", "minecraft:acacia_wood", "minecraft:stripped_acacia_log", "minecraft:stripped_acacia_wood", "minecraft:birch_log", "minecraft:birch_wood", "minecraft:stripped_birch_log", "minecraft:stripped_birch_wood", "minecraft:jungle_log", "minecraft:jungle_wood", "minecraft:stripped_jungle_log", "minecraft:stripped_jungle_wood", "minecraft:spruce_log", "minecraft:spruce_wood", "minecraft:stripped_spruce_log", "minecraft:stripped_spruce_wood", "minecraft:mangrove_log", "minecraft:mangrove_wood", "minecraft:stripped_mangrove_log", "minecraft:stripped_mangrove_wood", "minecraft:cherry_log", "minecraft:cherry_wood", "minecraft:stripped_cherry_log", "minecraft:stripped_cherry_wood", "minecraft:crimson_stem", "minecraft:stripped_crimson_stem", "minecraft:crimson_hyphae", "minecraft:stripped_crimson_hyphae", "minecraft:warped_stem", "minecraft:stripped_warped_stem", "minecraft:warped_hyphae", "minecraft:stripped_warped_hyphae", NULL);
    AddRegistryTag(registry, "minecraft:wall_signs", "minecraft:oak_wall_sign", "minecraft:spruce_wall_sign", "minecraft:birch_wall_sign", "minecraft:acacia_wall_sign", "minecraft:jungle_wall_sign", "minecraft:dark_oak_wall_sign", "minecraft:crimson_wall_sign", "minecraft:warped_wall_sign", "minecraft:mangrove_wall_sign", "minecraft:bamboo_wall_sign", "minecraft:cherry_wall_sign", NULL);
    AddRegistryTag(registry, "minecraft:rabbits_spawnable_on", "minecraft:grass_block", "minecraft:snow", "minecraft:snow_block", "minecraft:sand", NULL);
    AddRegistryTag(registry, "minecraft:trail_ruins_replaceable", "minecraft:gravel", NULL);
    AddRegistryTag(registry, "minecraft:deepslate_ore_replaceables", "minecraft:deepslate", "minecraft:tuff", NULL);
    AddRegistryTag(registry, "minecraft:wooden_pressure_plates", "minecraft:oak_pressure_plate", "minecraft:spruce_pressure_plate", "minecraft:birch_pressure_plate", "minecraft:jungle_pressure_plate", "minecraft:acacia_pressure_plate", "minecraft:dark_oak_pressure_plate", "minecraft:crimson_pressure_plate", "minecraft:warped_pressure_plate", "minecraft:mangrove_pressure_plate", "minecraft:bamboo_pressure_plate", "minecraft:cherry_pressure_plate", NULL);
    AddRegistryTag(registry, "minecraft:beehives", "minecraft:bee_nest", "minecraft:beehive", NULL);
    AddRegistryTag(registry, "minecraft:strider_warm_blocks", "minecraft:lava", NULL);
    AddRegistryTag(registry, "minecraft:enchantment_power_provider", "minecraft:bookshelf", NULL);
    AddRegistryTag(registry, "minecraft:signs", "minecraft:oak_sign", "minecraft:spruce_sign", "minecraft:birch_sign", "minecraft:acacia_sign", "minecraft:jungle_sign", "minecraft:dark_oak_sign", "minecraft:crimson_sign", "minecraft:warped_sign", "minecraft:mangrove_sign", "minecraft:bamboo_sign", "minecraft:cherry_sign", "minecraft:oak_wall_sign", "minecraft:spruce_wall_sign", "minecraft:birch_wall_sign", "minecraft:acacia_wall_sign", "minecraft:jungle_wall_sign", "minecraft:dark_oak_wall_sign", "minecraft:crimson_wall_sign", "minecraft:warped_wall_sign", "minecraft:mangrove_wall_sign", "minecraft:bamboo_wall_sign", "minecraft:cherry_wall_sign", NULL);
    AddRegistryTag(registry, "minecraft:terracotta", "minecraft:terracotta", "minecraft:white_terracotta", "minecraft:orange_terracotta", "minecraft:magenta_terracotta", "minecraft:light_blue_terracotta", "minecraft:yellow_terracotta", "minecraft:lime_terracotta", "minecraft:pink_terracotta", "minecraft:gray_terracotta", "minecraft:light_gray_terracotta", "minecraft:cyan_terracotta", "minecraft:purple_terracotta", "minecraft:blue_terracotta", "minecraft:brown_terracotta", "minecraft:green_terracotta", "minecraft:red_terracotta", "minecraft:black_terracotta", NULL);
    AddRegistryTag(registry, "minecraft:sniffer_egg_hatch_boost", "minecraft:moss_block", NULL);
    AddRegistryTag(registry, "minecraft:mangrove_logs", "minecraft:mangrove_log", "minecraft:mangrove_wood", "minecraft:stripped_mangrove_log", "minecraft:stripped_mangrove_wood", NULL);
    AddRegistryTag(registry, "minecraft:soul_fire_base_blocks", "minecraft:soul_sand", "minecraft:soul_soil", NULL);
    AddRegistryTag(registry, "minecraft:dead_bush_may_place_on", "minecraft:sand", "minecraft:red_sand", "minecraft:suspicious_sand", "minecraft:suspicious_sand", "minecraft:terracotta", "minecraft:white_terracotta", "minecraft:orange_terracotta", "minecraft:magenta_terracotta", "minecraft:light_blue_terracotta", "minecraft:yellow_terracotta", "minecraft:lime_terracotta", "minecraft:pink_terracotta", "minecraft:gray_terracotta", "minecraft:light_gray_terracotta", "minecraft:cyan_terracotta", "minecraft:purple_terracotta", "minecraft:blue_terracotta", "minecraft:brown_terracotta", "minecraft:green_terracotta", "minecraft:red_terracotta", "minecraft:black_terracotta", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", NULL);
    AddRegistryTag(registry, "minecraft:dampens_vibrations", "minecraft:white_wool", "minecraft:orange_wool", "minecraft:magenta_wool", "minecraft:light_blue_wool", "minecraft:yellow_wool", "minecraft:lime_wool", "minecraft:pink_wool", "minecraft:gray_wool", "minecraft:light_gray_wool", "minecraft:cyan_wool", "minecraft:purple_wool", "minecraft:blue_wool", "minecraft:brown_wool", "minecraft:green_wool", "minecraft:red_wool", "minecraft:black_wool", "minecraft:white_carpet", "minecraft:orange_carpet", "minecraft:magenta_carpet", "minecraft:light_blue_carpet", "minecraft:yellow_carpet", "minecraft:lime_carpet", "minecraft:pink_carpet", "minecraft:gray_carpet", "minecraft:light_gray_carpet", "minecraft:cyan_carpet", "minecraft:purple_carpet", "minecraft:blue_carpet", "minecraft:brown_carpet", "minecraft:green_carpet", "minecraft:red_carpet", "minecraft:black_carpet", NULL);
    AddRegistryTag(registry, "minecraft:lush_ground_replaceable", "minecraft:stone", "minecraft:granite", "minecraft:diorite", "minecraft:andesite", "minecraft:tuff", "minecraft:deepslate", "minecraft:cave_vines_plant", "minecraft:cave_vines", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:clay", "minecraft:gravel", "minecraft:sand", NULL);
    AddRegistryTag(registry, "minecraft:birch_logs", "minecraft:birch_log", "minecraft:birch_wood", "minecraft:stripped_birch_log", "minecraft:stripped_birch_wood", NULL);
    AddRegistryTag(registry, "minecraft:prevent_mob_spawning_inside", "minecraft:rail", "minecraft:powered_rail", "minecraft:detector_rail", "minecraft:activator_rail", NULL);
    AddRegistryTag(registry, "minecraft:rails", "minecraft:rail", "minecraft:powered_rail", "minecraft:detector_rail", "minecraft:activator_rail", NULL);
    AddRegistryTag(registry, "minecraft:big_dripleaf_placeable", "minecraft:clay", "minecraft:moss_block", "minecraft:dirt", "minecraft:grass_block", "minecraft:podzol", "minecraft:coarse_dirt", "minecraft:mycelium", "minecraft:rooted_dirt", "minecraft:moss_block", "minecraft:mud", "minecraft:muddy_mangrove_roots", "minecraft:farmland", NULL);
    AddRegistryTag(registry, "minecraft:enchantment_power_transmitter", "minecraft:air", "minecraft:water", "minecraft:lava", "minecraft:short_grass", "minecraft:fern", "minecraft:dead_bush", "minecraft:seagrass", "minecraft:tall_seagrass", "minecraft:fire", "minecraft:soul_fire", "minecraft:snow", "minecraft:vine", "minecraft:glow_lichen", "minecraft:light", "minecraft:tall_grass", "minecraft:large_fern", "minecraft:structure_void", "minecraft:void_air", "minecraft:cave_air", "minecraft:bubble_column", "minecraft:warped_roots", "minecraft:nether_sprouts", "minecraft:crimson_roots", "minecraft:hanging_roots", NULL);
    AddRegistryTag(registry, "minecraft:wooden_stairs", "minecraft:oak_stairs", "minecraft:spruce_stairs", "minecraft:birch_stairs", "minecraft:jungle_stairs", "minecraft:acacia_stairs", "minecraft:dark_oak_stairs", "minecraft:crimson_stairs", "minecraft:warped_stairs", "minecraft:mangrove_stairs", "minecraft:bamboo_stairs", "minecraft:cherry_stairs", NULL);
    AddRegistryTag(registry, "minecraft:mineable/shovel", "minecraft:clay", "minecraft:dirt", "minecraft:coarse_dirt", "minecraft:podzol", "minecraft:farmland", "minecraft:grass_block", "minecraft:gravel", "minecraft:mycelium", "minecraft:sand", "minecraft:red_sand", "minecraft:snow_block", "minecraft:snow", "minecraft:soul_sand", "minecraft:dirt_path", "minecraft:soul_soil", "minecraft:rooted_dirt", "minecraft:muddy_mangrove_roots", "minecraft:mud", "minecraft:suspicious_sand", "minecraft:suspicious_gravel", "minecraft:white_concrete_powder", "minecraft:orange_concrete_powder", "minecraft:magenta_concrete_powder", "minecraft:light_blue_concrete_powder", "minecraft:yellow_concrete_powder", "minecraft:lime_concrete_powder", "minecraft:pink_concrete_powder", "minecraft:gray_concrete_powder", "minecraft:light_gray_concrete_powder", "minecraft:cyan_concrete_powder", "minecraft:purple_concrete_powder", "minecraft:blue_concrete_powder", "minecraft:brown_concrete_powder", "minecraft:green_concrete_powder", "minecraft:red_concrete_powder", "minecraft:black_concrete_powder", NULL);
    AddRegistryTag(registry, "minecraft:mineable/pickaxe", "minecraft:stone", "minecraft:granite", "minecraft:polished_granite", "minecraft:diorite", "minecraft:polished_diorite", "minecraft:andesite", "minecraft:polished_andesite", "minecraft:cobblestone", "minecraft:gold_ore", "minecraft:deepslate_gold_ore", "minecraft:iron_ore", "minecraft:deepslate_iron_ore", "minecraft:coal_ore", "minecraft:deepslate_coal_ore", "minecraft:nether_gold_ore", "minecraft:lapis_ore", "minecraft:deepslate_lapis_ore", "minecraft:lapis_block", "minecraft:dispenser", "minecraft:sandstone", "minecraft:chiseled_sandstone", "minecraft:cut_sandstone", "minecraft:gold_block", "minecraft:iron_block", "minecraft:bricks", "minecraft:mossy_cobblestone", "minecraft:obsidian", "minecraft:spawner", "minecraft:diamond_ore", "minecraft:deepslate_diamond_ore", "minecraft:diamond_block", "minecraft:furnace", "minecraft:cobblestone_stairs", "minecraft:stone_pressure_plate", "minecraft:iron_door", "minecraft:redstone_ore", "minecraft:deepslate_redstone_ore", "minecraft:netherrack", "minecraft:basalt", "minecraft:polished_basalt", "minecraft:stone_bricks", "minecraft:mossy_stone_bricks", "minecraft:cracked_stone_bricks", "minecraft:chiseled_stone_bricks", "minecraft:iron_bars", "minecraft:chain", "minecraft:brick_stairs", "minecraft:stone_brick_stairs", "minecraft:nether_bricks", "minecraft:nether_brick_fence", "minecraft:nether_brick_stairs", "minecraft:enchanting_table", "minecraft:brewing_stand", "minecraft:end_stone", "minecraft:sandstone_stairs", "minecraft:emerald_ore", "minecraft:deepslate_emerald_ore", "minecraft:ender_chest", "minecraft:emerald_block", "minecraft:light_weighted_pressure_plate", "minecraft:heavy_weighted_pressure_plate", "minecraft:redstone_block", "minecraft:nether_quartz_ore", "minecraft:hopper", "minecraft:quartz_block", "minecraft:chiseled_quartz_block", "minecraft:quartz_pillar", "minecraft:quartz_stairs", "minecraft:dropper", "minecraft:white_terracotta", "minecraft:orange_terracotta", "minecraft:magenta_terracotta", "minecraft:light_blue_terracotta", "minecraft:yellow_terracotta", "minecraft:lime_terracotta", "minecraft:pink_terracotta", "minecraft:gray_terracotta", "minecraft:light_gray_terracotta", "minecraft:cyan_terracotta", "minecraft:purple_terracotta", "minecraft:blue_terracotta", "minecraft:brown_terracotta", "minecraft:green_terracotta", "minecraft:red_terracotta", "minecraft:black_terracotta", "minecraft:iron_trapdoor", "minecraft:prismarine", "minecraft:prismarine_bricks", "minecraft:dark_prismarine", "minecraft:prismarine_stairs", "minecraft:prismarine_brick_stairs", "minecraft:dark_prismarine_stairs", "minecraft:prismarine_slab", "minecraft:prismarine_brick_slab", "minecraft:dark_prismarine_slab", "minecraft:terracotta", "minecraft:coal_block", "minecraft:red_sandstone", "minecraft:chiseled_red_sandstone", "minecraft:cut_red_sandstone", "minecraft:red_sandstone_stairs", "minecraft:stone_slab", "minecraft:smooth_stone_slab", "minecraft:sandstone_slab", "minecraft:cut_sandstone_slab", "minecraft:petrified_oak_slab", "minecraft:cobblestone_slab", "minecraft:brick_slab", "minecraft:stone_brick_slab", "minecraft:nether_brick_slab", "minecraft:quartz_slab", "minecraft:red_sandstone_slab", "minecraft:cut_red_sandstone_slab", "minecraft:purpur_slab", "minecraft:smooth_stone", "minecraft:smooth_sandstone", "minecraft:smooth_quartz", "minecraft:smooth_red_sandstone", "minecraft:purpur_block", "minecraft:purpur_pillar", "minecraft:purpur_stairs", "minecraft:end_stone_bricks", "minecraft:magma_block", "minecraft:red_nether_bricks", "minecraft:bone_block", "minecraft:observer", "minecraft:white_glazed_terracotta", "minecraft:orange_glazed_terracotta", "minecraft:magenta_glazed_terracotta", "minecraft:light_blue_glazed_terracotta", "minecraft:yellow_glazed_terracotta", "minecraft:lime_glazed_terracotta", "minecraft:pink_glazed_terracotta", "minecraft:gray_glazed_terracotta", "minecraft:light_gray_glazed_terracotta", "minecraft:cyan_glazed_terracotta", "minecraft:purple_glazed_terracotta", "minecraft:blue_glazed_terracotta", "minecraft:brown_glazed_terracotta", "minecraft:green_glazed_terracotta", "minecraft:red_glazed_terracotta", "minecraft:black_glazed_terracotta", "minecraft:white_concrete", "minecraft:orange_concrete", "minecraft:magenta_concrete", "minecraft:light_blue_concrete", "minecraft:yellow_concrete", "minecraft:lime_concrete", "minecraft:pink_concrete", "minecraft:gray_concrete", "minecraft:light_gray_concrete", "minecraft:cyan_concrete", "minecraft:purple_concrete", "minecraft:blue_concrete", "minecraft:brown_concrete", "minecraft:green_concrete", "minecraft:red_concrete", "minecraft:black_concrete", "minecraft:dead_tube_coral_block", "minecraft:dead_brain_coral_block", "minecraft:dead_bubble_coral_block", "minecraft:dead_fire_coral_block", "minecraft:dead_horn_coral_block", "minecraft:tube_coral_block", "minecraft:brain_coral_block", "minecraft:bubble_coral_block", "minecraft:fire_coral_block", "minecraft:horn_coral_block", "minecraft:dead_tube_coral", "minecraft:dead_brain_coral", "minecraft:dead_bubble_coral", "minecraft:dead_fire_coral", "minecraft:dead_horn_coral", "minecraft:dead_tube_coral_fan", "minecraft:dead_brain_coral_fan", "minecraft:dead_bubble_coral_fan", "minecraft:dead_fire_coral_fan", "minecraft:dead_horn_coral_fan", "minecraft:dead_tube_coral_wall_fan", "minecraft:dead_brain_coral_wall_fan", "minecraft:dead_bubble_coral_wall_fan", "minecraft:dead_fire_coral_wall_fan", "minecraft:dead_horn_coral_wall_fan", "minecraft:polished_granite_stairs", "minecraft:smooth_red_sandstone_stairs", "minecraft:mossy_stone_brick_stairs", "minecraft:polished_diorite_stairs", "minecraft:mossy_cobblestone_stairs", "minecraft:end_stone_brick_stairs", "minecraft:stone_stairs", "minecraft:smooth_sandstone_stairs", "minecraft:smooth_quartz_stairs", "minecraft:granite_stairs", "minecraft:andesite_stairs", "minecraft:red_nether_brick_stairs", "minecraft:polished_andesite_stairs", "minecraft:diorite_stairs", "minecraft:polished_granite_slab", "minecraft:smooth_red_sandstone_slab", "minecraft:mossy_stone_brick_slab", "minecraft:polished_diorite_slab", "minecraft:mossy_cobblestone_slab", "minecraft:end_stone_brick_slab", "minecraft:smooth_sandstone_slab", "minecraft:smooth_quartz_slab", "minecraft:granite_slab", "minecraft:andesite_slab", "minecraft:red_nether_brick_slab", "minecraft:polished_andesite_slab", "minecraft:diorite_slab", "minecraft:smoker", "minecraft:blast_furnace", "minecraft:grindstone", "minecraft:stonecutter", "minecraft:bell", "minecraft:lantern", "minecraft:soul_lantern", "minecraft:warped_nylium", "minecraft:crimson_nylium", "minecraft:netherite_block", "minecraft:ancient_debris", "minecraft:crying_obsidian", "minecraft:respawn_anchor", "minecraft:lodestone", "minecraft:blackstone", "minecraft:blackstone_stairs", "minecraft:blackstone_slab", "minecraft:polished_blackstone", "minecraft:polished_blackstone_bricks", "minecraft:cracked_polished_blackstone_bricks", "minecraft:chiseled_polished_blackstone", "minecraft:polished_blackstone_brick_slab", "minecraft:polished_blackstone_brick_stairs", "minecraft:gilded_blackstone", "minecraft:polished_blackstone_stairs", "minecraft:polished_blackstone_slab", "minecraft:polished_blackstone_pressure_plate", "minecraft:chiseled_nether_bricks", "minecraft:cracked_nether_bricks", "minecraft:quartz_bricks", "minecraft:tuff", "minecraft:calcite", "minecraft:oxidized_copper", "minecraft:weathered_copper", "minecraft:exposed_copper", "minecraft:copper_block", "minecraft:copper_ore", "minecraft:deepslate_copper_ore", "minecraft:oxidized_cut_copper", "minecraft:weathered_cut_copper", "minecraft:exposed_cut_copper", "minecraft:cut_copper", "minecraft:oxidized_cut_copper_stairs", "minecraft:weathered_cut_copper_stairs", "minecraft:exposed_cut_copper_stairs", "minecraft:cut_copper_stairs", "minecraft:oxidized_cut_copper_slab", "minecraft:weathered_cut_copper_slab", "minecraft:exposed_cut_copper_slab", "minecraft:cut_copper_slab", "minecraft:waxed_copper_block", "minecraft:waxed_weathered_copper", "minecraft:waxed_exposed_copper", "minecraft:waxed_oxidized_copper", "minecraft:waxed_oxidized_cut_copper", "minecraft:waxed_weathered_cut_copper", "minecraft:waxed_exposed_cut_copper", "minecraft:waxed_cut_copper", "minecraft:waxed_oxidized_cut_copper_stairs", "minecraft:waxed_weathered_cut_copper_stairs", "minecraft:waxed_exposed_cut_copper_stairs", "minecraft:waxed_cut_copper_stairs", "minecraft:waxed_oxidized_cut_copper_slab", "minecraft:waxed_weathered_cut_copper_slab", "minecraft:waxed_exposed_cut_copper_slab", "minecraft:waxed_cut_copper_slab", "minecraft:lightning_rod", "minecraft:pointed_dripstone", "minecraft:dripstone_block", "minecraft:deepslate", "minecraft:cobbled_deepslate", "minecraft:cobbled_deepslate_stairs", "minecraft:cobbled_deepslate_slab", "minecraft:polished_deepslate", "minecraft:polished_deepslate_stairs", "minecraft:polished_deepslate_slab", "minecraft:deepslate_tiles", "minecraft:deepslate_tile_stairs", "minecraft:deepslate_tile_slab", "minecraft:deepslate_bricks", "minecraft:deepslate_brick_stairs", "minecraft:deepslate_brick_slab", "minecraft:chiseled_deepslate", "minecraft:cracked_deepslate_bricks", "minecraft:cracked_deepslate_tiles", "minecraft:smooth_basalt", "minecraft:raw_iron_block", "minecraft:raw_copper_block", "minecraft:raw_gold_block", "minecraft:ice", "minecraft:packed_ice", "minecraft:blue_ice", "minecraft:piston", "minecraft:sticky_piston", "minecraft:piston_head", "minecraft:amethyst_cluster", "minecraft:small_amethyst_bud", "minecraft:medium_amethyst_bud", "minecraft:large_amethyst_bud", "minecraft:amethyst_block", "minecraft:budding_amethyst", "minecraft:infested_cobblestone", "minecraft:infested_chiseled_stone_bricks", "minecraft:infested_cracked_stone_bricks", "minecraft:infested_deepslate", "minecraft:infested_stone", "minecraft:infested_mossy_stone_bricks", "minecraft:infested_stone_bricks", "minecraft:stone_button", "minecraft:polished_blackstone_button", "minecraft:cobblestone_wall", "minecraft:mossy_cobblestone_wall", "minecraft:brick_wall", "minecraft:prismarine_wall", "minecraft:red_sandstone_wall", "minecraft:mossy_stone_brick_wall", "minecraft:granite_wall", "minecraft:stone_brick_wall", "minecraft:nether_brick_wall", "minecraft:andesite_wall", "minecraft:red_nether_brick_wall", "minecraft:sandstone_wall", "minecraft:end_stone_brick_wall", "minecraft:diorite_wall", "minecraft:blackstone_wall", "minecraft:polished_blackstone_brick_wall", "minecraft:polished_blackstone_wall", "minecraft:cobbled_deepslate_wall", "minecraft:polished_deepslate_wall", "minecraft:deepslate_tile_wall", "minecraft:deepslate_brick_wall", "minecraft:mud_brick_wall", "minecraft:tuff_wall", "minecraft:polished_tuff_wall", "minecraft:tuff_brick_wall", "minecraft:shulker_box", "minecraft:black_shulker_box", "minecraft:blue_shulker_box", "minecraft:brown_shulker_box", "minecraft:cyan_shulker_box", "minecraft:gray_shulker_box", "minecraft:green_shulker_box", "minecraft:light_blue_shulker_box", "minecraft:light_gray_shulker_box", "minecraft:lime_shulker_box", "minecraft:magenta_shulker_box", "minecraft:orange_shulker_box", "minecraft:pink_shulker_box", "minecraft:purple_shulker_box", "minecraft:red_shulker_box", "minecraft:white_shulker_box", "minecraft:yellow_shulker_box", "minecraft:anvil", "minecraft:chipped_anvil", "minecraft:damaged_anvil", "minecraft:cauldron", "minecraft:water_cauldron", "minecraft:lava_cauldron", "minecraft:powder_snow_cauldron", "minecraft:rail", "minecraft:powered_rail", "minecraft:detector_rail", "minecraft:activator_rail", "minecraft:conduit", "minecraft:mud_bricks", "minecraft:mud_brick_stairs", "minecraft:mud_brick_slab", "minecraft:packed_mud", "minecraft:crafter", "minecraft:tuff_slab", "minecraft:tuff_stairs", "minecraft:tuff_wall", "minecraft:chiseled_tuff", "minecraft:polished_tuff", "minecraft:polished_tuff_slab", "minecraft:polished_tuff_stairs", "minecraft:polished_tuff_wall", "minecraft:tuff_bricks", "minecraft:tuff_brick_slab", "minecraft:tuff_brick_stairs", "minecraft:tuff_brick_wall", "minecraft:chiseled_tuff_bricks", "minecraft:chiseled_copper", "minecraft:exposed_chiseled_copper", "minecraft:weathered_chiseled_copper", "minecraft:oxidized_chiseled_copper", "minecraft:waxed_chiseled_copper", "minecraft:waxed_exposed_chiseled_copper", "minecraft:waxed_weathered_chiseled_copper", "minecraft:waxed_oxidized_chiseled_copper", "minecraft:copper_grate", "minecraft:exposed_copper_grate", "minecraft:weathered_copper_grate", "minecraft:oxidized_copper_grate", "minecraft:waxed_copper_grate", "minecraft:waxed_exposed_copper_grate", "minecraft:waxed_weathered_copper_grate", "minecraft:waxed_oxidized_copper_grate", "minecraft:copper_bulb", "minecraft:exposed_copper_bulb", "minecraft:weathered_copper_bulb", "minecraft:oxidized_copper_bulb", "minecraft:waxed_copper_bulb", "minecraft:waxed_exposed_copper_bulb", "minecraft:waxed_weathered_copper_bulb", "minecraft:waxed_oxidized_copper_bulb", "minecraft:copper_door", "minecraft:exposed_copper_door", "minecraft:weathered_copper_door", "minecraft:oxidized_copper_door", "minecraft:waxed_copper_door", "minecraft:waxed_exposed_copper_door", "minecraft:waxed_weathered_copper_door", "minecraft:waxed_oxidized_copper_door", "minecraft:copper_trapdoor", "minecraft:exposed_copper_trapdoor", "minecraft:weathered_copper_trapdoor", "minecraft:oxidized_copper_trapdoor", "minecraft:waxed_copper_trapdoor", "minecraft:waxed_exposed_copper_trapdoor", "minecraft:waxed_weathered_copper_trapdoor", "minecraft:waxed_oxidized_copper_trapdoor", "minecraft:heavy_core", NULL);
    AddRegistryTag(registry, "minecraft:mineable/hoe", "minecraft:nether_wart_block", "minecraft:warped_wart_block", "minecraft:hay_block", "minecraft:dried_kelp_block", "minecraft:target", "minecraft:shroomlight", "minecraft:sponge", "minecraft:wet_sponge", "minecraft:jungle_leaves", "minecraft:oak_leaves", "minecraft:spruce_leaves", "minecraft:dark_oak_leaves", "minecraft:acacia_leaves", "minecraft:birch_leaves", "minecraft:azalea_leaves", "minecraft:flowering_azalea_leaves", "minecraft:mangrove_leaves", "minecraft:sculk_sensor", "minecraft:calibrated_sculk_sensor", "minecraft:moss_block", "minecraft:moss_carpet", "minecraft:sculk", "minecraft:sculk_catalyst", "minecraft:sculk_vein", "minecraft:sculk_shrieker", "minecraft:pink_petals", "minecraft:cherry_leaves", NULL);
    AddRegistryTag(registry, "minecraft:mineable/axe", "minecraft:note_block", "minecraft:attached_melon_stem", "minecraft:attached_pumpkin_stem", "minecraft:azalea", "minecraft:bamboo", "minecraft:barrel", "minecraft:bee_nest", "minecraft:beehive", "minecraft:beetroots", "minecraft:big_dripleaf_stem", "minecraft:big_dripleaf", "minecraft:bookshelf", "minecraft:brown_mushroom_block", "minecraft:brown_mushroom", "minecraft:campfire", "minecraft:carrots", "minecraft:cartography_table", "minecraft:carved_pumpkin", "minecraft:cave_vines_plant", "minecraft:cave_vines", "minecraft:chest", "minecraft:chorus_flower", "minecraft:chorus_plant", "minecraft:cocoa", "minecraft:composter", "minecraft:crafting_table", "minecraft:crimson_fungus", "minecraft:daylight_detector", "minecraft:dead_bush", "minecraft:fern", "minecraft:fletching_table", "minecraft:glow_lichen", "minecraft:short_grass", "minecraft:hanging_roots", "minecraft:jack_o_lantern", "minecraft:jukebox", "minecraft:ladder", "minecraft:large_fern", "minecraft:lectern", "minecraft:lily_pad", "minecraft:loom", "minecraft:melon_stem", "minecraft:melon", "minecraft:mushroom_stem", "minecraft:nether_wart", "minecraft:potatoes", "minecraft:pumpkin_stem", "minecraft:pumpkin", "minecraft:red_mushroom_block", "minecraft:red_mushroom", "minecraft:scaffolding", "minecraft:small_dripleaf", "minecraft:smithing_table", "minecraft:soul_campfire", "minecraft:spore_blossom", "minecraft:sugar_cane", "minecraft:sweet_berry_bush", "minecraft:tall_grass", "minecraft:trapped_chest", "minecraft:twisting_vines_plant", "minecraft:twisting_vines", "minecraft:vine", "minecraft:warped_fungus", "minecraft:weeping_vines_plant", "minecraft:weeping_vines", "minecraft:wheat", "minecraft:white_banner", "minecraft:orange_banner", "minecraft:magenta_banner", "minecraft:light_blue_banner", "minecraft:yellow_banner", "minecraft:lime_banner", "minecraft:pink_banner", "minecraft:gray_banner", "minecraft:light_gray_banner", "minecraft:cyan_banner", "minecraft:purple_banner", "minecraft:blue_banner", "minecraft:brown_banner", "minecraft:green_banner", "minecraft:red_banner", "minecraft:black_banner", "minecraft:white_wall_banner", "minecraft:orange_wall_banner", "minecraft:magenta_wall_banner", "minecraft:light_blue_wall_banner", "minecraft:yellow_wall_banner", "minecraft:lime_wall_banner", "minecraft:pink_wall_banner", "minecraft:gray_wall_banner", "minecraft:light_gray_wall_banner", "minecraft:cyan_wall_banner", "minecraft:purple_wall_banner", "minecraft:blue_wall_banner", "minecraft:brown_wall_banner", "minecraft:green_wall_banner", "minecraft:red_wall_banner", "minecraft:black_wall_banner", "minecraft:acacia_fence_gate", "minecraft:birch_fence_gate", "minecraft:dark_oak_fence_gate", "minecraft:jungle_fence_gate", "minecraft:oak_fence_gate", "minecraft:spruce_fence_gate", "minecraft:crimson_fence_gate", "minecraft:warped_fence_gate", "minecraft:mangrove_fence_gate", "minecraft:bamboo_fence_gate", "minecraft:cherry_fence_gate", "minecraft:dark_oak_log", "minecraft:dark_oak_wood", "minecraft:stripped_dark_oak_log", "minecraft:stripped_dark_oak_wood", "minecraft:oak_log", "minecraft:oak_wood", "minecraft:stripped_oak_log", "minecraft:stripped_oak_wood", "minecraft:acacia_log", "minecraft:acacia_wood", "minecraft:stripped_acacia_log", "minecraft:stripped_acacia_wood", "minecraft:birch_log", "minecraft:birch_wood", "minecraft:stripped_birch_log", "minecraft:stripped_birch_wood", "minecraft:jungle_log", "minecraft:jungle_wood", "minecraft:stripped_jungle_log", "minecraft:stripped_jungle_wood", "minecraft:spruce_log", "minecraft:spruce_wood", "minecraft:stripped_spruce_log", "minecraft:stripped_spruce_wood", "minecraft:mangrove_log", "minecraft:mangrove_wood", "minecraft:stripped_mangrove_log", "minecraft:stripped_mangrove_wood", "minecraft:cherry_log", "minecraft:cherry_wood", "minecraft:stripped_cherry_log", "minecraft:stripped_cherry_wood", "minecraft:crimson_stem", "minecraft:stripped_crimson_stem", "minecraft:crimson_hyphae", "minecraft:stripped_crimson_hyphae", "minecraft:warped_stem", "minecraft:stripped_warped_stem", "minecraft:warped_hyphae", "minecraft:stripped_warped_hyphae", "minecraft:oak_planks", "minecraft:spruce_planks", "minecraft:birch_planks", "minecraft:jungle_planks", "minecraft:acacia_planks", "minecraft:dark_oak_planks", "minecraft:crimson_planks", "minecraft:warped_planks", "minecraft:mangrove_planks", "minecraft:bamboo_planks", "minecraft:cherry_planks", "minecraft:oak_sapling", "minecraft:spruce_sapling", "minecraft:birch_sapling", "minecraft:jungle_sapling", "minecraft:acacia_sapling", "minecraft:dark_oak_sapling", "minecraft:azalea", "minecraft:flowering_azalea", "minecraft:mangrove_propagule", "minecraft:cherry_sapling", "minecraft:oak_sign", "minecraft:spruce_sign", "minecraft:birch_sign", "minecraft:acacia_sign", "minecraft:jungle_sign", "minecraft:dark_oak_sign", "minecraft:crimson_sign", "minecraft:warped_sign", "minecraft:mangrove_sign", "minecraft:bamboo_sign", "minecraft:cherry_sign", "minecraft:oak_wall_sign", "minecraft:spruce_wall_sign", "minecraft:birch_wall_sign", "minecraft:acacia_wall_sign", "minecraft:jungle_wall_sign", "minecraft:dark_oak_wall_sign", "minecraft:crimson_wall_sign", "minecraft:warped_wall_sign", "minecraft:mangrove_wall_sign", "minecraft:bamboo_wall_sign", "minecraft:cherry_wall_sign", "minecraft:oak_button", "minecraft:spruce_button", "minecraft:birch_button", "minecraft:jungle_button", "minecraft:acacia_button", "minecraft:dark_oak_button", "minecraft:crimson_button", "minecraft:warped_button", "minecraft:mangrove_button", "minecraft:bamboo_button", "minecraft:cherry_button", "minecraft:oak_door", "minecraft:spruce_door", "minecraft:birch_door", "minecraft:jungle_door", "minecraft:acacia_door", "minecraft:dark_oak_door", "minecraft:crimson_door", "minecraft:warped_door", "minecraft:mangrove_door", "minecraft:bamboo_door", "minecraft:cherry_door", "minecraft:oak_fence", "minecraft:acacia_fence", "minecraft:dark_oak_fence", "minecraft:spruce_fence", "minecraft:birch_fence", "minecraft:jungle_fence", "minecraft:crimson_fence", "minecraft:warped_fence", "minecraft:mangrove_fence", "minecraft:bamboo_fence", "minecraft:cherry_fence", "minecraft:oak_pressure_plate", "minecraft:spruce_pressure_plate", "minecraft:birch_pressure_plate", "minecraft:jungle_pressure_plate", "minecraft:acacia_pressure_plate", "minecraft:dark_oak_pressure_plate", "minecraft:crimson_pressure_plate", "minecraft:warped_pressure_plate", "minecraft:mangrove_pressure_plate", "minecraft:bamboo_pressure_plate", "minecraft:cherry_pressure_plate", "minecraft:oak_slab", "minecraft:spruce_slab", "minecraft:birch_slab", "minecraft:jungle_slab", "minecraft:acacia_slab", "minecraft:dark_oak_slab", "minecraft:crimson_slab", "minecraft:warped_slab", "minecraft:mangrove_slab", "minecraft:bamboo_slab", "minecraft:cherry_slab", "minecraft:oak_stairs", "minecraft:spruce_stairs", "minecraft:birch_stairs", "minecraft:jungle_stairs", "minecraft:acacia_stairs", "minecraft:dark_oak_stairs", "minecraft:crimson_stairs", "minecraft:warped_stairs", "minecraft:mangrove_stairs", "minecraft:bamboo_stairs", "minecraft:cherry_stairs", "minecraft:acacia_trapdoor", "minecraft:birch_trapdoor", "minecraft:dark_oak_trapdoor", "minecraft:jungle_trapdoor", "minecraft:oak_trapdoor", "minecraft:spruce_trapdoor", "minecraft:crimson_trapdoor", "minecraft:warped_trapdoor", "minecraft:mangrove_trapdoor", "minecraft:bamboo_trapdoor", "minecraft:cherry_trapdoor", "minecraft:mangrove_roots", "minecraft:oak_hanging_sign", "minecraft:spruce_hanging_sign", "minecraft:birch_hanging_sign", "minecraft:acacia_hanging_sign", "minecraft:cherry_hanging_sign", "minecraft:jungle_hanging_sign", "minecraft:dark_oak_hanging_sign", "minecraft:crimson_hanging_sign", "minecraft:warped_hanging_sign", "minecraft:mangrove_hanging_sign", "minecraft:bamboo_hanging_sign", "minecraft:oak_wall_hanging_sign", "minecraft:spruce_wall_hanging_sign", "minecraft:birch_wall_hanging_sign", "minecraft:acacia_wall_hanging_sign", "minecraft:cherry_wall_hanging_sign", "minecraft:jungle_wall_hanging_sign", "minecraft:dark_oak_wall_hanging_sign", "minecraft:crimson_wall_hanging_sign", "minecraft:warped_wall_hanging_sign", "minecraft:mangrove_wall_hanging_sign", "minecraft:bamboo_wall_hanging_sign", "minecraft:bamboo_mosaic", "minecraft:bamboo_mosaic_slab", "minecraft:bamboo_mosaic_stairs", "minecraft:bamboo_block", "minecraft:stripped_bamboo_block", "minecraft:chiseled_bookshelf", NULL);

    assert(MAX_BLOCK_STATES >= serv->actual_block_state_count);
    assert(CeilLog2U32(serv->vanilla_block_state_count) == BITS_PER_BLOCK_STATE);
    assert(ACTUAL_BLOCK_TYPE_COUNT == serv->blockRegistry.entryCount);

    LogInfo("Block state count: %d (ceillog2 = %d)", serv->vanilla_block_state_count, CeilLog2U32(serv->vanilla_block_state_count));

    CalculateBlockLightPropagation();
}
