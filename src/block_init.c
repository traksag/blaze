#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "shared.h"

typedef struct {
    block_properties props;
    i32 stateCount;
    u8 * collisionModelByState;
    u8 * supportModelByState;
    u8 * lightBlockingModelByState;
    u8 * lightReductionByState;
    u8 * emittedLightByState;
    BlockBehaviours behaviours;
} BlockConfig;

typedef struct {
    BlockConfig * configs;
    MemoryArena * arena;
} BlockSetup;

static BlockSetup blockSetup;

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

static void ReallocateStateArrays(BlockConfig * config) {
    config->collisionModelByState = CallocInArena(blockSetup.arena, config->stateCount);
    config->supportModelByState = CallocInArena(blockSetup.arena, config->stateCount);
    config->lightBlockingModelByState = CallocInArena(blockSetup.arena, config->stateCount);
    config->lightReductionByState = CallocInArena(blockSetup.arena, config->stateCount);
    config->emittedLightByState = CallocInArena(blockSetup.arena, config->stateCount);
}

static void AddProperty(BlockConfig * config, i32 propertyId, char * defaultValue) {
    block_property_spec * spec = serv->block_property_specs + propertyId;
    String propName = {
        .size = spec->tape[0],
        .data = spec->tape + 1,
    };
    String defaultValueString = STR(defaultValue);

    // NOTE(traks): compute index of default value
    // NOTE(traks): tape starts with name size, then name string, and then
    // repeated value size + value string. Skip the name part
    i32 curTapeIndex = 1 + spec->tape[0];
    i32 defaultValueIndex = -1;
    for (i32 valueIndex = 0; valueIndex < spec->value_count; valueIndex++) {
        String value = {
            .size = spec->tape[curTapeIndex],
            .data = spec->tape + curTapeIndex + 1
        };
        if (StringEquals(defaultValueString, value)) {
            defaultValueIndex = valueIndex;
            break;
        }
        curTapeIndex += value.size + 1;
    }
    assert(defaultValueIndex >= 0);

    // NOTE(traks): Minecraft sorts block properties using the natural sorting
    // order for Java strings. Since all properties have ascii characters, this
    // comes down to ASCII sorting based on character values

    assert(config->props.property_count < ARRAY_SIZE(config->props.property_specs));

    // NOTE(traks): grab the index we should register the new property at
    i32 newPropIndex;
    for (newPropIndex = 0; newPropIndex < config->props.property_count; newPropIndex++) {
        block_property_spec * otherSpec = serv->block_property_specs + config->props.property_specs[newPropIndex];
        String otherPropName = {
            .size = otherSpec->tape[0],
            .data = otherSpec->tape + 1,
        };
        if (CompareBlockProperty(propName, otherPropName) < 0) {
            break;
        }
    }

    // NOTE(traks): make space for the new property
    for (i32 propIndex = config->props.property_count; propIndex > newPropIndex; propIndex--) {
        config->props.property_specs[propIndex] = config->props.property_specs[propIndex - 1];
        config->props.default_value_indices[propIndex] = config->props.default_value_indices[propIndex - 1];
    }

    // NOTE(traks): register the new property for the block
    config->props.property_specs[newPropIndex] = propertyId;
    config->props.default_value_indices[newPropIndex] = defaultValueIndex;
    config->props.property_count++;

    // NOTE(traks): update other fields
    config->stateCount *= spec->value_count;
    ReallocateStateArrays(config);
}

static BlockConfig * BeginNextBlock(char * resourceLoc) {
    i32 blockType = ResolveRegistryEntryId(&serv->blockRegistry, STR(resourceLoc));
    assert(blockType >= 0);
    BlockConfig * res = &blockSetup.configs[blockType];
    LogInfo("%s", resourceLoc);
    assert(res->stateCount == 0);
    res->stateCount = 1;
    ReallocateStateArrays(res);
    return res;
}

static void FinaliseBlocks(void) {
    // NOTE(traks): compute block states and set up global arrays
    for (i32 blockType = 0; blockType < serv->blockRegistry.entryCount; blockType++) {
        BlockConfig * config = blockSetup.configs + blockType;
        i32 baseState = serv->actual_block_state_count;
        config->props.base_state = baseState;

        assert(baseState + config->stateCount <= (i32) ARRAY_SIZE(serv->block_type_by_state));
        assert(config->stateCount > 0);

        memcpy(serv->block_properties_table + blockType, &config->props, sizeof config->props);
        for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
            serv->block_type_by_state[baseState + stateIndex] = blockType;
        }
        memcpy(serv->collisionModelByState + baseState, config->collisionModelByState, config->stateCount);
        memcpy(serv->supportModelByState + baseState, config->supportModelByState, config->stateCount);
        memcpy(serv->lightBlockingModelByState + baseState, config->lightBlockingModelByState, config->stateCount);
        memcpy(serv->lightReductionByState + baseState, config->lightReductionByState, config->stateCount);
        memcpy(serv->emittedLightByState + baseState, config->emittedLightByState, config->stateCount);
        serv->blockBehavioursByType[blockType] = config->behaviours;

        serv->actual_block_state_count += config->stateCount;
        if (blockType < VANILLA_BLOCK_TYPE_COUNT) {
            serv->vanilla_block_state_count += config->stateCount;
        }
    }

    // NOTE(traks): compute light propagation data
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

static void SetParticularModelForAllStates(BlockConfig * config, u8 * modelByState, i32 modelId) {
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        modelByState[stateIndex] = modelId;
    }
}

static void SetCollisionModelForAllStates(BlockConfig * config, i32 modelId) {
    SetParticularModelForAllStates(config, config->collisionModelByState, modelId);
}

static void SetSupportModelForAllStates(BlockConfig * config, i32 modelId) {
    SetParticularModelForAllStates(config, config->supportModelByState, modelId);
}

// NOTE(traks): for this, some relevant vanilla code is:
// - useShapeForLightOcclusion: false = can just use empty model here
// - getOcclusionShape: shape for occlusion if above = true
static void SetLightBlockingModelForAllStates(BlockConfig * config, i32 modelId) {
    SetParticularModelForAllStates(config, config->lightBlockingModelByState, modelId);
}

static void SetAllModelsForAllStatesIndividually(BlockConfig * config, i32 collisionModelId, i32 supportModelId, i32 lightModelId) {
    SetCollisionModelForAllStates(config, collisionModelId);
    SetSupportModelForAllStates(config, supportModelId);
    SetLightBlockingModelForAllStates(config, lightModelId);
}

static void SetAllModelsForAllStates(BlockConfig * config, i32 modelId) {
    SetAllModelsForAllStatesIndividually(config, modelId, modelId, modelId);
}

// NOTE(traks): for full blocks it doesn't really matter what the light
// reduction is, because the light will get blocked anyway.
// Relevant vanilla code is:
// - getLightBlock: how much light is blocked
// - propagatesSkylightDown: if max sky light can pass through unchanged
static void SetLightReductionForAllStates(BlockConfig * config, i32 lightReduction) {
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        config->lightReductionByState[stateIndex] = lightReduction & 0xf;
    }
}

static void SetLightReductionWhenWaterlogged(BlockConfig * config) {
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        i32 lightReduction = (info.waterlogged ? 1 : 0);
        config->lightReductionByState[stateIndex] = lightReduction;
    }
}

// NOTE(traks): Relevant vanilla code is:
// - .lightLevel in Blocks: sets how much light is emitted
static void SetEmittedLightForAllStates(BlockConfig * config, i32 emittedLight) {
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        config->emittedLightByState[stateIndex] = emittedLight & 0xf;
    }
}

static void SetEmittedLightWhenLit(BlockConfig * config, i32 emittedLight) {
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        if (info.lit) {
            config->emittedLightByState[stateIndex] = emittedLight & 0xf;
        }
    }
}

static void SetEmittedLightWhenBerries(BlockConfig * config, i32 emittedLight) {
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        if (info.berries) {
            config->emittedLightByState[stateIndex] = emittedLight & 0xf;
        }
    }
}

static void AddBlockBehaviour(BlockConfig * config, i32 behaviour) {
    BlockBehaviours * behaviours = &config->behaviours;
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
    BlockConfig * config = BeginNextBlock(resource_loc);
    SetCollisionModelForAllStates(config, collisionModelId);
    SetSupportModelForAllStates(config, supportModelId);
    SetLightBlockingModelForAllStates(config, lightModelId);
    SetLightReductionForAllStates(config, lightReduction);
    SetEmittedLightForAllStates(config, emittedLight);
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
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_STAGE, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_SOIL_BELOW);
}

static void
init_propagule(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_STAGE, "0");
    AddProperty(config, BLOCK_PROPERTY_AGE_4, "0");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddProperty(config, BLOCK_PROPERTY_HANGING, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_PROPAGULE_ENVIRONMENT);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_pillar(char * resource_loc, i32 emittedLight) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_AXIS, "y");
    SetAllModelsForAllStates(config, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(config, 15);
    SetEmittedLightForAllStates(config, emittedLight);
}

static void
init_leaves(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    config->props.type_tags |= (u32) 1 << BLOCK_TAG_LEAVES;
    AddProperty(config, BLOCK_PROPERTY_DISTANCE, "7");
    AddProperty(config, BLOCK_PROPERTY_PERSISTENT, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetCollisionModelForAllStates(config, BLOCK_MODEL_FULL);
    SetSupportModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 1);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_bed(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_OCCUPIED, "false");
    AddProperty(config, BLOCK_PROPERTY_BED_PART, "foot");

    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
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
        config->collisionModelByState[stateIndex] = model_index;
        config->supportModelByState[stateIndex] = model_index;
    }

    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_BED);
}

static void
init_slab(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_SLAB_TYPE, "bottom");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        int model_index = 0;
        switch (info.slab_type) {
        case SLAB_TOP: model_index = BLOCK_MODEL_TOP_SLAB; break;
        case SLAB_BOTTOM: model_index = BLOCK_MODEL_Y_8; break;
        case SLAB_DOUBLE: model_index = BLOCK_MODEL_FULL; break;
        }
        config->collisionModelByState[stateIndex] = model_index;
        config->supportModelByState[stateIndex] = model_index;
        config->lightBlockingModelByState[stateIndex] = model_index;
    }
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_sign(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_ROTATION_16, "0");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_wall_sign(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
}

static void
InitHangingSign(char * resource_loc) {
    // TODO(traks): models, light reduction, block behaviour, etc.
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_ROTATION_16, "0");
    AddProperty(config, BLOCK_PROPERTY_ATTACHED, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
}

static void
InitWallHangingSign(char * resource_loc) {
    // TODO(traks): models, light reduction, block behaviour, etc.
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
}

static void
init_stair_props(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    config->props.type_tags |= (u32) 1 << BLOCK_TAG_STAIRS;
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_HALF, "bottom");
    AddProperty(config, BLOCK_PROPERTY_STAIRS_SHAPE, "straight");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    // TODO(traks): block models
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_STAIRS);
}

static void
init_tall_plant(char * resource_loc, i32 hasWater) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, hasWater ? 1 : 0);
    if (hasWater) {
        AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
    }
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_TALL_PLANT);
}

static void
init_glazed_terracotta(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStates(config, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(config, 15);
}

static void
init_shulker_box_props(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    config->props.type_tags |= (u32) 1 << BLOCK_TAG_SHULKER_BOX;
    AddProperty(config, BLOCK_PROPERTY_FACING, "up");
    // TODO(traks): block models
    SetLightReductionForAllStates(config, 1);
}

static void
init_wall_props(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    config->props.type_tags |= ((u32) 1) << BLOCK_TAG_WALL;
    AddProperty(config, BLOCK_PROPERTY_WALL_POS_X, "none");
    AddProperty(config, BLOCK_PROPERTY_WALL_NEG_Z, "none");
    AddProperty(config, BLOCK_PROPERTY_WALL_POS_Z, "none");
    AddProperty(config, BLOCK_PROPERTY_POS_Y, "true");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddProperty(config, BLOCK_PROPERTY_WALL_NEG_X, "none");
    // TODO(traks): block models
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_WALL_CONNECT);
}

static void
init_pressure_plate(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_PLATE_SUPPORTING_SURFACE_BELOW);
}

static void
init_pane(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    config->props.type_tags |= (u32) 1 << BLOCK_TAG_PANE_LIKE;
    AddProperty(config, BLOCK_PROPERTY_POS_X, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_X, "false");
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        int flags = (info.pos_x << 3) | (info.neg_x << 2) | (info.pos_z << 1) | info.neg_z;
        int model_index = BLOCK_MODEL_PANE_CENTRE + flags;
        config->collisionModelByState[stateIndex] = model_index;
        config->supportModelByState[stateIndex] = model_index;
    }
    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_PANE_CONNECT);
}

static void
init_fence(char * resource_loc, int wooden) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    if (wooden) {
        config->props.type_tags |= (u32) 1 << BLOCK_TAG_WOODEN_FENCE;
    }
    AddProperty(config, BLOCK_PROPERTY_POS_X, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_X, "false");
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        int flags = (info.pos_x << 3) | (info.neg_x << 2) | (info.pos_z << 1) | info.neg_z;
        int model_index = BLOCK_MODEL_FENCE_CENTRE + flags;
        config->collisionModelByState[stateIndex] = model_index;
        config->supportModelByState[stateIndex] = model_index;
    }
    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FENCE_CONNECT);
}

static void
init_door_props(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");
    AddProperty(config, BLOCK_PROPERTY_DOOR_HINGE, "left");
    AddProperty(config, BLOCK_PROPERTY_OPEN, "false");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    // TODO(traks): block models
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_DOOR_MATCH_OTHER_PART);
}

static void
init_button(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_ATTACH_FACE, "wall");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_ATTACHED);
}

static void
init_trapdoor_props(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_HALF, "bottom");
    AddProperty(config, BLOCK_PROPERTY_OPEN, "false");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_fence_gate(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    config->props.type_tags |= (u32) 1 << BLOCK_TAG_FENCE_GATE;
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_IN_WALL, "false");
    AddProperty(config, BLOCK_PROPERTY_OPEN, "false");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        int model_index;
        if (info.open) {
            model_index = BLOCK_MODEL_EMPTY;
        } else if (get_direction_axis(info.horizontal_facing) == AXIS_X) {
            model_index = BLOCK_MODEL_FENCE_GATE_FACING_X;
        } else {
            model_index = BLOCK_MODEL_FENCE_GATE_FACING_Z;
        }
        config->collisionModelByState[stateIndex] = model_index;
        config->supportModelByState[stateIndex] = model_index;
    }
    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FENCE_GATE_CONNECT);
}

static void
init_mushroom_block(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_NEG_Y, "true");
    AddProperty(config, BLOCK_PROPERTY_POS_X, "true");
    AddProperty(config, BLOCK_PROPERTY_NEG_Z, "true");
    AddProperty(config, BLOCK_PROPERTY_POS_Z, "true");
    AddProperty(config, BLOCK_PROPERTY_POS_Y, "true");
    AddProperty(config, BLOCK_PROPERTY_NEG_X, "true");
    SetAllModelsForAllStates(config, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(config, 15);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_MUSHROOM_BLOCK_CONNECT);
}

static void
init_skull_props(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    AddProperty(config, BLOCK_PROPERTY_ROTATION_16, "0");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
}

static void
init_wall_skull_props(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
}

static void
init_anvil_props(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
}

static void
init_banner(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_ROTATION_16, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
}

static void
init_wall_banner(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
}

static void
init_coral(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "true");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_coral_fan(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "true");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_coral_wall_fan(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "true");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
}

static void
init_snowy_grassy_block(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_SNOWY, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(config, 15);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_SNOWY_TOP);
}

static void
init_redstone_ore(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_LIT, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_FULL);
    SetLightReductionForAllStates(config, 15);
    SetEmittedLightWhenLit(config, 9);
}

static void
init_cauldron(char * resource_loc, int layered, i32 emittedLight) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    if (layered) {
        AddProperty(config, BLOCK_PROPERTY_LEVEL_CAULDRON, "1");
    }
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    SetEmittedLightForAllStates(config, emittedLight);
}

static void
init_candle(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_CANDLES, "1");
    AddProperty(config, BLOCK_PROPERTY_LIT, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        if (info.lit) {
            config->emittedLightByState[stateIndex] = 3 * info.candles;
        }
    }
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_POLE_SUPPORT_BELOW);
}

static void
init_candle_cake(char * resource_loc) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_LIT, "false");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    SetEmittedLightWhenLit(config, 3);
}

static void
init_amethyst_cluster(char * resource_loc, i32 emittedLight) {
    BlockConfig * config = BeginNextBlock(resource_loc);
    AddProperty(config, BLOCK_PROPERTY_FACING, "up");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    // TODO(traks): block models
    SetLightBlockingModelForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    SetEmittedLightForAllStates(config, emittedLight);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_BEHIND);
}

static void InitMultiFaceBlock(char * resourceLoc, i32 emittedLight) {
    BlockConfig * config = BeginNextBlock(resourceLoc);
    AddProperty(config, BLOCK_PROPERTY_NEG_Y, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_Y, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_X, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_X, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);

    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        if (info.neg_y || info.pos_y || info.neg_z || info.pos_z || info.neg_x || info.pos_x) {
            config->emittedLightByState[stateIndex] = emittedLight;
        }
    }

    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
}

static void InitFlowerPot(char * resourceLoc) {
    InitSimpleBlockWithModels(resourceLoc, BLOCK_MODEL_FLOWER_POT, BLOCK_MODEL_FLOWER_POT, BLOCK_MODEL_EMPTY, 0, 0);
}

static void InitCarpet(char * resourceLoc) {
    BlockConfig * config = BeginNextBlock(resourceLoc);
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_Y_1, BLOCK_MODEL_Y_1, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_NON_AIR_BELOW);
}

static void InitSimplePlant(char * resourceLoc) {
    BlockConfig * config = BeginNextBlock(resourceLoc);
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_SOIL_BELOW);
}

static void InitSimpleNetherPlant(char * resourceLoc) {
    BlockConfig * config = BeginNextBlock(resourceLoc);
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_SOIL_OR_NETHER_SOIL_BELOW);
}

static void InitTorch(char * resourceLoc, i32 onWall, i32 emittedLight) {
    BlockConfig * config = BeginNextBlock(resourceLoc);
    if (onWall) {
        AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    }
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetEmittedLightForAllStates(config, emittedLight);
    if (onWall) {
        AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_BEHIND_HORIZONTAL);
    } else {
        AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_POLE_SUPPORT_BELOW);
    }
}

static void InitAzalea(char * resourceLoc) {
    // TODO(traks): block models
    BlockConfig * config = BeginNextBlock(resourceLoc);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_AZALEA);
}

static void InitSuspiciousBlock(char * resourceLoc) {
    // TODO(traks): block models, light reduction, etc. This is a block entity
    BlockConfig * config = BeginNextBlock(resourceLoc);
    AddProperty(config, BLOCK_PROPERTY_DUSTED, "0");
}

static void InitGrate(char * resourceLoc) {
    BlockConfig * config = BeginNextBlock(resourceLoc);
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
}

static void InitBulb(char * resourceLoc) {
    // TODO(traks): block models, light, behaviours, etc.
    BlockConfig * config = BeginNextBlock(resourceLoc);
    AddProperty(config, BLOCK_PROPERTY_LIT, "false");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
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

static char * FormatString(char * format, ...) {
    va_list ap;
    va_start(ap, format);
    i32 outSize = 256;
    char * res = MallocInArena(blockSetup.arena, outSize);
    vsnprintf(res, outSize, format, ap);
    va_end(ap);
    return res;
}

static i32 EndsWith(char * string, char * suffix) {
    if (strlen(string) < strlen(suffix)) {
        return 0;
    }
    if (memcmp(string + (strlen(string) - strlen(suffix)), suffix, strlen(suffix)) == 0) {
        return 1;
    }
    return 0;
}

void
// NOTE(traks): This function takes 6 seconds to compile under Clang's O3 for
// me, disable optimisations for now. This function only runs once anyway
#if defined(__clang__)
__attribute__ ((optnone))
#endif
init_block_data(MemoryArena * scratchArena) {
    blockSetup.arena = scratchArena;
    blockSetup.configs = CallocInArena(blockSetup.arena, serv->blockRegistry.entryCount * sizeof *blockSetup.configs);

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

    BlockConfig * config;

    InitSimpleEmptyBlock("minecraft:air");
    init_snowy_grassy_block("minecraft:grass_block");
    InitSimpleFullBlock("minecraft:dirt");
    InitSimpleFullBlock("minecraft:coarse_dirt");
    init_snowy_grassy_block("minecraft:podzol");

    // NOTE(traks): initialise all the wood blocks
    typedef struct {
        char * name;
        char * logName;
        char * woodName;
        i32 hasSapling;
        i32 hasLeaves;
    } WoodType;

    WoodType woodTypes[] = {
        {"oak", "log", "wood", 1, 1},
        {"spruce", "log", "wood", 1, 1},
        {"birch", "log", "wood", 1, 1},
        {"jungle", "log", "wood", 1, 1},
        {"acacia", "log", "wood", 1, 1},
        {"cherry", "log", "wood", 1, 1},
        {"dark_oak", "log", "wood", 1, 1},
        {"pale_oak", "log", "wood", 1, 1},
        {"mangrove", "log", "wood", 0, 1},
        {"bamboo", "block", NULL, 0, 0},
        {"crimson", "stem", "hyphae", 0, 0},
        {"warped", "stem", "hyphae", 0, 0},
    };

    for (i32 woodIndex = 0; woodIndex < (i32) ARRAY_SIZE(woodTypes); woodIndex++) {
        WoodType * type = &woodTypes[woodIndex];
        char * name = type->name;

        InitSimpleFullBlock(FormatString("minecraft:%s_planks", name));
        init_stair_props(FormatString("minecraft:%s_stairs", name));
        init_slab(FormatString("minecraft:%s_slab", name));
        init_sign(FormatString("minecraft:%s_sign", name));
        init_wall_sign(FormatString("minecraft:%s_wall_sign", name));
        InitHangingSign(FormatString("minecraft:%s_hanging_sign", name));
        InitWallHangingSign(FormatString("minecraft:%s_wall_hanging_sign", name));
        init_pressure_plate(FormatString("minecraft:%s_pressure_plate", name));
        init_trapdoor_props(FormatString("minecraft:%s_trapdoor", name));
        init_button(FormatString("minecraft:%s_button", name));
        init_fence_gate(FormatString("minecraft:%s_fence_gate", name));
        init_fence(FormatString("minecraft:%s_fence", name), 1);
        init_door_props(FormatString("minecraft:%s_door", name));

        if (type->logName) {
            init_pillar(FormatString("minecraft:%s_%s", name, type->logName), 0);
            init_pillar(FormatString("minecraft:stripped_%s_%s", name, type->logName), 0);
        }
        if (type->woodName) {
            init_pillar(FormatString("minecraft:%s_%s", name, type->woodName), 0);
            init_pillar(FormatString("minecraft:stripped_%s_%s", name, type->woodName), 0);
        }
        if (type->hasSapling) {
            init_sapling(FormatString("minecraft:%s_sapling", name));
            InitFlowerPot(FormatString("minecraft:potted_%s_sapling", name));
        }
        if (type->hasLeaves) {
            init_leaves(FormatString("minecraft:%s_leaves", name));
        }
    }

    // NOTE(traks): now the special wood stuff
    init_leaves("minecraft:azalea_leaves");
    init_leaves("minecraft:flowering_azalea_leaves");

    InitSimpleFullBlock("minecraft:bamboo_mosaic");
    init_stair_props("minecraft:bamboo_mosaic_stairs");
    init_slab("minecraft:bamboo_mosaic_slab");
    config = BeginNextBlock("minecraft:bamboo_sapling");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_BAMBOO_SAPLING);
    config = BeginNextBlock("minecraft:bamboo");
    AddProperty(config, BLOCK_PROPERTY_AGE_1, "0");
    AddProperty(config, BLOCK_PROPERTY_BAMBOO_LEAVES, "none");
    AddProperty(config, BLOCK_PROPERTY_STAGE, "0");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_CENTRED_BAMBOO, BLOCK_MODEL_CENTRED_BAMBOO, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_BAMBOO);
    InitFlowerPot("minecraft:potted_bamboo");

    init_propagule("minecraft:mangrove_propagule");
    InitFlowerPot("minecraft:potted_mangrove_propagule");

    InitSimpleNetherPlant("minecraft:crimson_fungus");
    InitFlowerPot("minecraft:potted_crimson_fungus");

    InitSimpleNetherPlant("minecraft:warped_fungus");
    InitFlowerPot("minecraft:potted_warped_fungus");

    // NOTE(traks): initialise all the coloured blocks
    char * basicColours[] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray", "light_gray", "cyan", "purple", "blue", "brown", "green", "red", "black"};

    for (i32 colourIndex = 0; colourIndex < (i32) ARRAY_SIZE(basicColours); colourIndex++) {
        char * colour = basicColours[colourIndex];
        init_bed(FormatString("minecraft:%s_bed", colour));
        InitSimpleFullBlock(FormatString("minecraft:%s_wool", colour));
        InitSimpleBlockWithModels(FormatString("minecraft:%s_stained_glass", colour), BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 0);
        init_pane(FormatString("minecraft:%s_stained_glass_pane", colour));
        InitSimpleFullBlock(FormatString("minecraft:%s_terracotta", colour));
        InitCarpet(FormatString("minecraft:%s_carpet", colour));
        init_banner(FormatString("minecraft:%s_banner", colour));
        init_wall_banner(FormatString("minecraft:%s_wall_banner", colour));
        init_shulker_box_props(FormatString("minecraft:%s_shulker_box", colour));
        init_glazed_terracotta(FormatString("minecraft:%s_glazed_terracotta", colour));
        InitSimpleFullBlock(FormatString("minecraft:%s_concrete", colour));
        InitSimpleFullBlock(FormatString("minecraft:%s_concrete_powder", colour));
        init_candle(FormatString("minecraft:%s_candle", colour));
        init_candle_cake(FormatString("minecraft:%s_candle_cake", colour));
    }

    // NOTE(traks): blank versions of coloured blocks
    InitSimpleFullBlock("minecraft:terracotta");
    init_shulker_box_props("minecraft:shulker_box");
    init_candle("minecraft:candle");
    init_candle_cake("minecraft:candle_cake");

    // NOTE(traks): ore blocks
    InitSimpleFullBlock("minecraft:copper_ore");
    InitSimpleFullBlock("minecraft:deepslate_copper_ore");
    InitSimpleFullBlock("minecraft:raw_copper_block");

    InitSimpleFullBlock("minecraft:gold_ore");
    InitSimpleFullBlock("minecraft:deepslate_gold_ore");
    InitSimpleFullBlock("minecraft:gold_block");
    InitSimpleFullBlock("minecraft:raw_gold_block");

    InitSimpleFullBlock("minecraft:iron_ore");
    InitSimpleFullBlock("minecraft:deepslate_iron_ore");
    InitSimpleFullBlock("minecraft:iron_block");
    InitSimpleFullBlock("minecraft:raw_iron_block");

    InitSimpleFullBlock("minecraft:coal_ore");
    InitSimpleFullBlock("minecraft:deepslate_coal_ore");
    InitSimpleFullBlock("minecraft:coal_block");

    InitSimpleFullBlock("minecraft:lapis_ore");
    InitSimpleFullBlock("minecraft:deepslate_lapis_ore");
    InitSimpleFullBlock("minecraft:lapis_block");

    InitSimpleFullBlock("minecraft:diamond_ore");
    InitSimpleFullBlock("minecraft:deepslate_diamond_ore");
    InitSimpleFullBlock("minecraft:diamond_block");

    init_redstone_ore("minecraft:redstone_ore");
    init_redstone_ore("minecraft:deepslate_redstone_ore");
    InitSimpleFullBlock("minecraft:redstone_block");

    InitSimpleFullBlock("minecraft:emerald_ore");
    InitSimpleFullBlock("minecraft:deepslate_emerald_ore");
    InitSimpleFullBlock("minecraft:emerald_block");

    // NOTE(traks): copper
    char * copperTypes[] = {"", "exposed_", "oxidized_", "weathered_"};
    for (i32 copperIndex = 0; copperIndex < (i32) ARRAY_SIZE(copperTypes); copperIndex++) {
        char * name = copperTypes[copperIndex];
        char * appendBlock = (strlen(name) == 0 ? "_block" : "");
        InitSimpleFullBlock(FormatString("minecraft:%scopper%s", name, appendBlock));
        InitSimpleFullBlock(FormatString("minecraft:waxed_%scopper%s", name, appendBlock));
        InitSimpleFullBlock(FormatString("minecraft:%scut_copper", name));
        InitSimpleFullBlock(FormatString("minecraft:waxed_%scut_copper", name));
        InitSimpleFullBlock(FormatString("minecraft:%schiseled_copper", name));
        InitSimpleFullBlock(FormatString("minecraft:waxed_%schiseled_copper", name));
        init_stair_props(FormatString("minecraft:%scut_copper_stairs", name));
        init_stair_props(FormatString("minecraft:waxed_%scut_copper_stairs", name));
        init_slab(FormatString("minecraft:%scut_copper_slab", name));
        init_slab(FormatString("minecraft:waxed_%scut_copper_slab", name));
        init_door_props(FormatString("minecraft:%scopper_door", name));
        init_door_props(FormatString("minecraft:waxed_%scopper_door", name));
        init_trapdoor_props(FormatString("minecraft:%scopper_trapdoor", name));
        init_trapdoor_props(FormatString("minecraft:waxed_%scopper_trapdoor", name));
        InitGrate(FormatString("minecraft:%scopper_grate", name));
        InitGrate(FormatString("minecraft:waxed_%scopper_grate", name));
        InitBulb(FormatString("minecraft:%scopper_bulb", name));
        InitBulb(FormatString("minecraft:waxed_%scopper_bulb", name));
    }

    // NOTE(traks): bunch of structured blocks with slab, stairs, etc. variants.
    // These are blocks that are stone/brick-like
    typedef struct {
        char * name;
        i32 hasSlab;
        i32 hasStairs;
        i32 hasWall;
    } StructuredType;

    StructuredType structuredTypes[] = {
        {"smooth_quartz", 1, 1, 0},
        {"quartz_brick", 0, 0, 0},
        {"brick", 1, 1, 1},
        {"mud_brick", 1, 1, 1},

        {"prismarine", 1, 1, 1},
        {"prismarine_brick", 1, 1, 0},
        {"dark_prismarine", 1, 1, 0},

        {"nether_brick", 1, 1, 1},
        {"red_nether_brick", 1, 1, 1},
        {"chiseled_nether_brick", 0, 0, 0},
        {"cracked_nether_brick", 0, 0, 0},

        {"sandstone", 1, 1, 1},
        {"smooth_sandstone", 1, 1, 0},
        {"cut_sandstone", 1, 0, 0},
        {"chiseled_sandstone", 0, 0, 0},
        {"red_sandstone", 1, 1, 1},
        {"smooth_red_sandstone", 1, 1, 0},
        {"cut_red_sandstone", 1, 0, 0},
        {"chiseled_red_sandstone", 0, 0, 0},

        {"end_stone_brick", 1, 1, 1},

        {"andesite", 1, 1, 1},
        {"polished_andesite", 1, 1, 0},
        {"diorite", 1, 1, 1},
        {"polished_diorite", 1, 1, 0},
        {"granite", 1, 1, 1},
        {"polished_granite", 1, 1, 0},
        {"calcite", 0, 0, 0},

        {"stone", 1, 1, 0},
        {"smooth_stone", 1, 0, 0},
        {"cobblestone", 1, 1, 1},
        {"mossy_cobblestone", 1, 1, 1},
        {"stone_brick", 1, 1, 1},
        {"mossy_stone_brick", 1, 1, 1},
        {"cracked_stone_brick", 0, 0, 0},
        {"chiseled_stone_brick", 0, 0, 0},

        {"deepslate_brick", 1, 1, 1},
        {"deepslate_tile", 1, 1, 1},
        {"polished_deepslate", 1, 1, 1},
        {"cobbled_deepslate", 1, 1, 1},
        {"chiseled_deepslate", 0, 0, 0},
        {"cracked_deepslate_tile", 0, 0, 0},
        {"cracked_deepslate_brick", 0, 0, 0},
        {"reinforced_deepslate", 0, 0, 0},

        {"tuff", 1, 1, 1},
        {"polished_tuff", 1, 1, 1},
        {"tuff_brick", 1, 1, 1},
        {"chiseled_tuff", 0, 0, 0},
        {"chiseled_tuff_brick", 0, 0, 0},

        {"blackstone", 1, 1, 1},
        {"polished_blackstone", 1, 1, 1},
        {"polished_blackstone_brick", 1, 1, 1},
        {"gilded_blackstone", 0, 0, 0},
        {"chiseled_polished_blackstone", 0, 0, 0},
        {"cracked_polished_blackstone_brick", 0, 0, 0},
    };

    for (i32 structuredIndex = 0; structuredIndex < (i32) ARRAY_SIZE(structuredTypes); structuredIndex++) {
        StructuredType * type = &structuredTypes[structuredIndex];
        char * name = type->name;
        char * suffix = (EndsWith(name, "brick") || EndsWith(name, "tile") ? "s" : "");
        InitSimpleFullBlock(FormatString("minecraft:%s%s", name, suffix));
        if (type->hasSlab) {
            init_slab(FormatString("minecraft:%s_slab", name));
        }
        if (type->hasStairs) {
            init_stair_props(FormatString("minecraft:%s_stairs", name));
        }
        if (type->hasWall) {
            init_wall_props(FormatString("minecraft:%s_wall", name));
        }
    }

    // NOTE(traks): special versions of structured types
    init_pillar("minecraft:deepslate", 0);
    init_pressure_plate("minecraft:polished_blackstone_pressure_plate");
    init_button("minecraft:polished_blackstone_button");
    init_fence("minecraft:nether_brick_fence", 0);
    init_button("minecraft:stone_button");
    init_pressure_plate("minecraft:stone_pressure_plate");
    init_slab("minecraft:petrified_oak_slab");

    InitSimpleFullBlock("minecraft:quartz_block");
    init_slab("minecraft:quartz_slab");
    init_stair_props("minecraft:quartz_stairs");
    init_pillar("minecraft:quartz_pillar", 0);
    InitSimpleFullBlock("minecraft:chiseled_quartz_block");

    InitSimpleFullBlock("minecraft:purpur_block");
    init_slab("minecraft:purpur_slab");
    init_stair_props("minecraft:purpur_stairs");
    init_pillar("minecraft:purpur_pillar", 0);

    InitSimpleFullBlock("minecraft:bedrock");

    // @TODO(traks) slower movement in fluids
    config = BeginNextBlock("minecraft:water");
    AddProperty(config, BLOCK_PROPERTY_LEVEL, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 1);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    // @TODO(traks) slower movement in fluids
    config = BeginNextBlock("minecraft:lava");
    AddProperty(config, BLOCK_PROPERTY_LEVEL, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 1);
    SetEmittedLightForAllStates(config, 15);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    InitSimpleFullBlock("minecraft:sand");

    // TODO(traks): block models, light reduction, etc. This is a block entity
    InitSuspiciousBlock("minecraft:suspicious_sand");
    InitSimpleFullBlock("minecraft:red_sand");
    InitSimpleFullBlock("minecraft:gravel");
    InitSuspiciousBlock("minecraft:suspicious_gravel");
    InitSimpleFullBlock("minecraft:nether_gold_ore");

    config = BeginNextBlock("minecraft:mangrove_roots");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    init_pillar("minecraft:muddy_mangrove_roots", 0);

    InitSimpleFullBlock("minecraft:sponge");
    InitSimpleFullBlock("minecraft:wet_sponge");
    InitSimpleFullBlock("minecraft:glass");

    config = BeginNextBlock("minecraft:dispenser");
    AddProperty(config, BLOCK_PROPERTY_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_TRIGGERED, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    config = BeginNextBlock("minecraft:note_block");
    AddProperty(config, BLOCK_PROPERTY_NOTEBLOCK_INSTRUMENT, "harp");
    AddProperty(config, BLOCK_PROPERTY_NOTE, "0");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    config = BeginNextBlock("minecraft:powered_rail");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    AddProperty(config, BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "north_south");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    config = BeginNextBlock("minecraft:detector_rail");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    AddProperty(config, BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "north_south");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:sticky_piston");
    AddProperty(config, BLOCK_PROPERTY_EXTENDED, "false");
    AddProperty(config, BLOCK_PROPERTY_FACING, "north");

    // @TODO(traks) slow down entities in cobwebs
    init_simple_block("minecraft:cobweb", BLOCK_MODEL_EMPTY, 1, 0);
    InitSimplePlant("minecraft:short_grass");
    InitSimplePlant("minecraft:fern");

    config = BeginNextBlock("minecraft:dead_bush");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_SOIL_OR_DRY_SOIL_BELOW);

    config = BeginNextBlock("minecraft:seagrass");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 1);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    init_tall_plant("minecraft:tall_seagrass", 1);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:piston");
    AddProperty(config, BLOCK_PROPERTY_EXTENDED, "false");
    AddProperty(config, BLOCK_PROPERTY_FACING, "north");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:piston_head");
    AddProperty(config, BLOCK_PROPERTY_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_SHORT_PISTON, "false");
    AddProperty(config, BLOCK_PROPERTY_PISTON_TYPE, "normal");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:moving_piston");
    AddProperty(config, BLOCK_PROPERTY_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_PISTON_TYPE, "normal");

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

    config = BeginNextBlock("minecraft:wither_rose");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_WITHER_ROSE);

    InitSimplePlant("minecraft:lily_of_the_valley");
    init_simple_block("minecraft:brown_mushroom", BLOCK_MODEL_EMPTY, 0, 1);
    InitSimpleEmptyBlock("minecraft:red_mushroom");

    config = BeginNextBlock("minecraft:tnt");
    AddProperty(config, BLOCK_PROPERTY_UNSTABLE, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    InitSimpleFullBlock("minecraft:bookshelf");

    // TODO(traks): block model, light reduction, etc. This is a block entity
    config = BeginNextBlock("minecraft:chiseled_bookshelf");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_0_OCCUPIED, "false");
    AddProperty(config, BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_1_OCCUPIED, "false");
    AddProperty(config, BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_2_OCCUPIED, "false");
    AddProperty(config, BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_3_OCCUPIED, "false");
    AddProperty(config, BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_4_OCCUPIED, "false");
    AddProperty(config, BLOCK_PROPERTY_CHISELED_BOOKSHELF_SLOT_5_OCCUPIED, "false");

    InitSimpleFullBlock("minecraft:obsidian");

    InitTorch("minecraft:torch", 0, 14);
    InitTorch("minecraft:wall_torch", 1, 14);

    config = BeginNextBlock("minecraft:fire");
    AddProperty(config, BLOCK_PROPERTY_AGE_15, "0");
    AddProperty(config, BLOCK_PROPERTY_POS_X, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_Y, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_X, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    SetEmittedLightForAllStates(config, 15);

    // @TODO(traks) do damage in fire
    init_simple_block("minecraft:soul_fire", BLOCK_MODEL_EMPTY, 0, 10);
    InitSimpleBlockWithModels("minecraft:spawner", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 1, 0);

    // TODO(traks): block models + light reduction, etc.
    config = BeginNextBlock("minecraft:creaking_heart");
    AddProperty(config, BLOCK_PROPERTY_AXIS, "y");
    AddProperty(config, BLOCK_PROPERTY_CREAKING, "disabled");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:chest");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_CHEST_TYPE, "single");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    config = BeginNextBlock("minecraft:redstone_wire");
    AddProperty(config, BLOCK_PROPERTY_REDSTONE_POS_X, "none");
    AddProperty(config, BLOCK_PROPERTY_REDSTONE_NEG_Z, "none");
    AddProperty(config, BLOCK_PROPERTY_POWER, "0");
    AddProperty(config, BLOCK_PROPERTY_REDSTONE_POS_Z, "none");
    AddProperty(config, BLOCK_PROPERTY_REDSTONE_NEG_X, "none");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_REDSTONE_WIRE);

    InitSimpleFullBlock("minecraft:crafting_table");

    config = BeginNextBlock("minecraft:wheat");
    AddProperty(config, BLOCK_PROPERTY_AGE_7, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    config = BeginNextBlock("minecraft:farmland");
    AddProperty(config, BLOCK_PROPERTY_MOISTURE, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_Y_15);
    SetLightReductionForAllStates(config, 0);

    config = BeginNextBlock("minecraft:furnace");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_LIT, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);
    SetEmittedLightWhenLit(config, 13);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:ladder");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_BEHIND_HORIZONTAL);

    config = BeginNextBlock("minecraft:rail");
    AddProperty(config, BLOCK_PROPERTY_RAIL_SHAPE, "north_south");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    config = BeginNextBlock("minecraft:lever");
    AddProperty(config, BLOCK_PROPERTY_ATTACH_FACE, "wall");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_FULL_SUPPORT_ATTACHED);

    init_door_props("minecraft:iron_door");

    config = BeginNextBlock("minecraft:redstone_torch");
    AddProperty(config, BLOCK_PROPERTY_LIT, "true");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    SetEmittedLightWhenLit(config, 7);

    config = BeginNextBlock("minecraft:redstone_wall_torch");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_LIT, "true");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    SetEmittedLightWhenLit(config, 7);

    config = BeginNextBlock("minecraft:snow");
    AddProperty(config, BLOCK_PROPERTY_LAYERS, "1");
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        i32 collisionModelId = BLOCK_MODEL_EMPTY + (info.layers - 1) * 2;
        i32 supportModelId = BLOCK_MODEL_EMPTY + info.layers * 2;
        config->collisionModelByState[stateIndex] = collisionModelId;
        config->supportModelByState[stateIndex] = supportModelId;
        config->lightBlockingModelByState[stateIndex] = supportModelId;
    }
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_SNOW_LAYER);

    InitSimpleBlockWithModels("minecraft:ice", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 1, 0);
    InitSimpleFullBlock("minecraft:snow_block");

    config = BeginNextBlock("minecraft:cactus");
    AddProperty(config, BLOCK_PROPERTY_AGE_15, "0");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_CACTUS, BLOCK_MODEL_CACTUS, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);

    InitSimpleFullBlock("minecraft:clay");

    config = BeginNextBlock("minecraft:sugar_cane");
    AddProperty(config, BLOCK_PROPERTY_AGE_15, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_SUGAR_CANE);

    config = BeginNextBlock("minecraft:jukebox");
    AddProperty(config, BLOCK_PROPERTY_HAS_RECORD, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    InitSimpleFullBlock("minecraft:netherrack");
    InitSimpleBlockWithModels("minecraft:soul_sand", BLOCK_MODEL_Y_14, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 15, 0);
    InitSimpleFullBlock("minecraft:soul_soil");
    init_pillar("minecraft:basalt", 0);
    init_pillar("minecraft:polished_basalt", 0);

    InitTorch("minecraft:soul_torch", 0, 10);
    InitTorch("minecraft:soul_wall_torch", 1, 10);

    InitSimpleFullEmittingBlock("minecraft:glowstone", 15);

    config = BeginNextBlock("minecraft:nether_portal");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_AXIS, "x");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    SetEmittedLightForAllStates(config, 11);

    config = BeginNextBlock("minecraft:carved_pumpkin");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    config = BeginNextBlock("minecraft:jack_o_lantern");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);
    SetEmittedLightForAllStates(config, 15);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:cake");
    AddProperty(config, BLOCK_PROPERTY_BITES, "0");

    config = BeginNextBlock("minecraft:repeater");
    AddProperty(config, BLOCK_PROPERTY_DELAY, "1");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_LOCKED, "false");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_Y_2, BLOCK_MODEL_Y_2, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);

    InitSimpleFullBlock("minecraft:packed_mud");

    init_pillar("minecraft:infested_deepslate", 0);
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
    config = BeginNextBlock("minecraft:chain");
    AddProperty(config, BLOCK_PROPERTY_AXIS, "y");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    init_pane("minecraft:glass_pane");

    InitSimpleFullBlock("minecraft:pumpkin");
    InitSimpleFullBlock("minecraft:melon");

    config = BeginNextBlock("minecraft:attached_pumpkin_stem");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);

    config = BeginNextBlock("minecraft:attached_melon_stem");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);

    config = BeginNextBlock("minecraft:pumpkin_stem");
    AddProperty(config, BLOCK_PROPERTY_AGE_7, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    config = BeginNextBlock("minecraft:melon_stem");
    AddProperty(config, BLOCK_PROPERTY_AGE_7, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    config = BeginNextBlock("minecraft:vine");
    AddProperty(config, BLOCK_PROPERTY_POS_X, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_Y, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_X, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);

    InitMultiFaceBlock("minecraft:glow_lichen", 7);

    init_snowy_grassy_block("minecraft:mycelium");

    config = BeginNextBlock("minecraft:lily_pad");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_LILY_PAD, BLOCK_MODEL_LILY_PAD, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_LILY_PAD);

    config = BeginNextBlock("minecraft:nether_wart");
    AddProperty(config, BLOCK_PROPERTY_AGE_3, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NETHER_WART);

    InitSimpleBlockWithModels("minecraft:enchanting_table", BLOCK_MODEL_Y_12, BLOCK_MODEL_Y_12, BLOCK_MODEL_Y_12, 1, 7);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:brewing_stand");
    AddProperty(config, BLOCK_PROPERTY_HAS_BOTTLE_0, "false");
    AddProperty(config, BLOCK_PROPERTY_HAS_BOTTLE_1, "false");
    AddProperty(config, BLOCK_PROPERTY_HAS_BOTTLE_2, "false");
    SetEmittedLightForAllStates(config, 1);

    init_cauldron("minecraft:cauldron", 0, 0);
    init_cauldron("minecraft:water_cauldron", 1, 0);
    init_cauldron("minecraft:lava_cauldron", 0, 15);
    init_cauldron("minecraft:powder_snow_cauldron", 1, 0);

    init_simple_block("minecraft:end_portal", BLOCK_MODEL_EMPTY, 0, 15);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:end_portal_frame");
    AddProperty(config, BLOCK_PROPERTY_EYE, "false");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetEmittedLightForAllStates(config, 1);

    InitSimpleFullBlock("minecraft:end_stone");
    // @TODO(traks) correct block model
    InitSimpleBlockWithModels("minecraft:dragon_egg", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 0, 1);

    config = BeginNextBlock("minecraft:redstone_lamp");
    AddProperty(config, BLOCK_PROPERTY_LIT, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:cocoa");
    AddProperty(config, BLOCK_PROPERTY_AGE_2, "0");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:ender_chest");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetEmittedLightForAllStates(config, 7);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:tripwire_hook");
    AddProperty(config, BLOCK_PROPERTY_ATTACHED, "false");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");

    config = BeginNextBlock("minecraft:tripwire");
    AddProperty(config, BLOCK_PROPERTY_ATTACHED, "false");
    AddProperty(config, BLOCK_PROPERTY_DISARMED, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_X, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_X, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);

    config = BeginNextBlock("minecraft:command_block");
    AddProperty(config, BLOCK_PROPERTY_CONDITIONAL, "false");
    AddProperty(config, BLOCK_PROPERTY_FACING, "north");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    InitSimpleBlockWithModels("minecraft:beacon", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 1, 15);

    InitFlowerPot("minecraft:flower_pot");
    InitFlowerPot("minecraft:potted_torchflower");
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

    config = BeginNextBlock("minecraft:carrots");
    AddProperty(config, BLOCK_PROPERTY_AGE_7, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    config = BeginNextBlock("minecraft:potatoes");
    AddProperty(config, BLOCK_PROPERTY_AGE_7, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

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
    config = BeginNextBlock("minecraft:trapped_chest");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_CHEST_TYPE, "single");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    config = BeginNextBlock("minecraft:light_weighted_pressure_plate");
    AddProperty(config, BLOCK_PROPERTY_POWER, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_PLATE_SUPPORTING_SURFACE_BELOW);

    config = BeginNextBlock("minecraft:heavy_weighted_pressure_plate");
    AddProperty(config, BLOCK_PROPERTY_POWER, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_PLATE_SUPPORTING_SURFACE_BELOW);

    config = BeginNextBlock("minecraft:comparator");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_MODE_COMPARATOR, "compare");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_Y_2, BLOCK_MODEL_Y_2, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);

    config = BeginNextBlock("minecraft:daylight_detector");
    AddProperty(config, BLOCK_PROPERTY_INVERTED, "false");
    AddProperty(config, BLOCK_PROPERTY_POWER, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_Y_6);
    SetLightReductionForAllStates(config, 0);

    InitSimpleFullBlock("minecraft:nether_quartz_ore");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:hopper");
    AddProperty(config, BLOCK_PROPERTY_ENABLED, "true");
    AddProperty(config, BLOCK_PROPERTY_FACING_HOPPER, "down");

    config = BeginNextBlock("minecraft:activator_rail");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    AddProperty(config, BLOCK_PROPERTY_RAIL_SHAPE_STRAIGHT, "north_south");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    config = BeginNextBlock("minecraft:dropper");
    AddProperty(config, BLOCK_PROPERTY_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_TRIGGERED, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    InitSimpleBlockWithModels("minecraft:slime_block", BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY, 1, 0);

    config = BeginNextBlock("minecraft:barrier");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionWhenWaterlogged(config);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    config = BeginNextBlock("minecraft:light");
    AddProperty(config, BLOCK_PROPERTY_LEVEL_LIGHT, "15");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        config->emittedLightByState[stateIndex] = info.level_light & 0xf;
    }
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    init_trapdoor_props("minecraft:iron_trapdoor");

    InitSimpleFullEmittingBlock("minecraft:sea_lantern", 15);

    init_pillar("minecraft:hay_block", 0);

    InitSimpleFullBlock("minecraft:packed_ice");

    init_tall_plant("minecraft:sunflower", 0);
    init_tall_plant("minecraft:lilac", 0);
    init_tall_plant("minecraft:rose_bush", 0);
    init_tall_plant("minecraft:peony", 0);
    init_tall_plant("minecraft:tall_grass", 0);
    init_tall_plant("minecraft:large_fern", 0);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:end_rod");
    AddProperty(config, BLOCK_PROPERTY_FACING, "up");
    SetEmittedLightForAllStates(config, 14);

    // TODO(traks): block modesl + light reduction
    config = BeginNextBlock("minecraft:chorus_plant");
    AddProperty(config, BLOCK_PROPERTY_NEG_Y, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_X, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_Z, "false");
    AddProperty(config, BLOCK_PROPERTY_POS_Y, "false");
    AddProperty(config, BLOCK_PROPERTY_NEG_X, "false");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:chorus_flower");
    AddProperty(config, BLOCK_PROPERTY_AGE_5, "0");

    config = BeginNextBlock("minecraft:torchflower_crop");
    AddProperty(config, BLOCK_PROPERTY_AGE_1, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    // TODO(traks): behaviours, models, etc.
    config = BeginNextBlock("minecraft:pitcher_crop");
    AddProperty(config, BLOCK_PROPERTY_AGE_4, "0");
    AddProperty(config, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");

    init_tall_plant("minecraft:pitcher_plant", 0);

    config = BeginNextBlock("minecraft:beetroots");
    AddProperty(config, BLOCK_PROPERTY_AGE_3, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_FARMLAND_BELOW);

    init_simple_block("minecraft:dirt_path", BLOCK_MODEL_Y_15, 0, 0);
    init_simple_block("minecraft:end_gateway", BLOCK_MODEL_EMPTY, 0, 15);

    config = BeginNextBlock("minecraft:repeating_command_block");
    AddProperty(config, BLOCK_PROPERTY_CONDITIONAL, "false");
    AddProperty(config, BLOCK_PROPERTY_FACING, "north");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    config = BeginNextBlock("minecraft:chain_command_block");
    AddProperty(config, BLOCK_PROPERTY_CONDITIONAL, "false");
    AddProperty(config, BLOCK_PROPERTY_FACING, "north");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    config = BeginNextBlock("minecraft:frosted_ice");
    AddProperty(config, BLOCK_PROPERTY_AGE_3, "0");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    InitSimpleFullEmittingBlock("minecraft:magma_block", 3);
    InitSimpleFullBlock("minecraft:nether_wart_block");

    init_pillar("minecraft:bone_block", 0);

    InitSimpleEmptyBlock("minecraft:structure_void");

    config = BeginNextBlock("minecraft:observer");
    AddProperty(config, BLOCK_PROPERTY_FACING, "south");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    config = BeginNextBlock("minecraft:kelp");
    AddProperty(config, BLOCK_PROPERTY_AGE_25, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 1);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    config = BeginNextBlock("minecraft:kelp_plant");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 1);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    InitSimpleFullBlock("minecraft:dried_kelp_block");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:turtle_egg");
    AddProperty(config, BLOCK_PROPERTY_EGGS, "1");
    AddProperty(config, BLOCK_PROPERTY_HATCH, "0");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:sniffer_egg");
    AddProperty(config, BLOCK_PROPERTY_HATCH, "0");

    char * coralTypes[] = {"tube", "brain", "bubble", "fire", "horn"};

    for (i32 coralIndex = 0; coralIndex < (i32) ARRAY_SIZE(coralTypes); coralIndex++) {
        char * coral = coralTypes[coralIndex];
        InitSimpleFullBlock(FormatString("minecraft:%s_coral_block", coral));
        InitSimpleFullBlock(FormatString("minecraft:dead_%s_coral_block", coral));
        init_coral(FormatString("minecraft:%s_coral", coral));
        init_coral(FormatString("minecraft:dead_%s_coral", coral));
        init_coral_fan(FormatString("minecraft:%s_coral_fan", coral));
        init_coral_fan(FormatString("minecraft:dead_%s_coral_fan", coral));
        init_coral_wall_fan(FormatString("minecraft:%s_coral_wall_fan", coral));
        init_coral_wall_fan(FormatString("minecraft:dead_%s_coral_wall_fan", coral));
    }

    // @TODO(traks) collision models
    config = BeginNextBlock("minecraft:sea_pickle");
    AddProperty(config, BLOCK_PROPERTY_PICKLES, "1");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "true");
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        if (info.waterlogged) {
            config->emittedLightByState[stateIndex] = (info.pickles + 1) * 3;
        }
    }
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_SEA_PICKLE);

    InitSimpleFullBlock("minecraft:blue_ice");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:conduit");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "true");
    SetEmittedLightForAllStates(config, 15);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    InitSimpleEmptyBlock("minecraft:void_air");
    InitSimpleEmptyBlock("minecraft:cave_air");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:bubble_column");
    AddProperty(config, BLOCK_PROPERTY_DRAG, "true");
    SetLightReductionForAllStates(config, 1);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    // TODO(traks): block models (collision models & support model
    // (THE scaffolding model)) + light reduction
    config = BeginNextBlock("minecraft:scaffolding");
    AddProperty(config, BLOCK_PROPERTY_BOTTOM, "false");
    AddProperty(config, BLOCK_PROPERTY_STABILITY_DISTANCE, "7");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    config = BeginNextBlock("minecraft:loom");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    config = BeginNextBlock("minecraft:barrel");
    AddProperty(config, BLOCK_PROPERTY_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_OPEN, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    config = BeginNextBlock("minecraft:smoker");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_LIT, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);
    SetEmittedLightWhenLit(config, 13);

    config = BeginNextBlock("minecraft:blast_furnace");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_LIT, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);
    SetEmittedLightWhenLit(config, 13);

    InitSimpleFullBlock("minecraft:cartography_table");
    InitSimpleFullBlock("minecraft:fletching_table");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:grindstone");
    AddProperty(config, BLOCK_PROPERTY_ATTACH_FACE, "wall");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");

    config = BeginNextBlock("minecraft:lectern");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_HAS_BOOK, "false");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_LECTERN);
    SetLightReductionForAllStates(config, 0);

    InitSimpleFullBlock("minecraft:smithing_table");

    config = BeginNextBlock("minecraft:stonecutter");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_Y_9, BLOCK_MODEL_Y_9, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);

    // TODO(traks): collisions models + light reduction
    config = BeginNextBlock("minecraft:bell");
    AddProperty(config, BLOCK_PROPERTY_BELL_ATTACHMENT, "floor");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:lantern");
    AddProperty(config, BLOCK_PROPERTY_HANGING, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetEmittedLightForAllStates(config, 15);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:soul_lantern");
    AddProperty(config, BLOCK_PROPERTY_HANGING, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetEmittedLightForAllStates(config, 10);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    config = BeginNextBlock("minecraft:campfire");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_LIT, "true");
    AddProperty(config, BLOCK_PROPERTY_SIGNAL_FIRE, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_Y_7, BLOCK_MODEL_Y_7, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    SetEmittedLightWhenLit(config, 15);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    config = BeginNextBlock("minecraft:soul_campfire");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_LIT, "true");
    AddProperty(config, BLOCK_PROPERTY_SIGNAL_FIRE, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_Y_7, BLOCK_MODEL_Y_7, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    SetEmittedLightWhenLit(config, 10);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    config = BeginNextBlock("minecraft:sweet_berry_bush");
    AddProperty(config, BLOCK_PROPERTY_AGE_3, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_SOIL_BELOW);

    InitSimpleFullBlock("minecraft:warped_nylium");
    InitSimpleFullBlock("minecraft:warped_wart_block");
    InitSimpleNetherPlant("minecraft:warped_roots");
    InitSimpleNetherPlant("minecraft:nether_sprouts");

    InitSimpleFullBlock("minecraft:crimson_nylium");
    InitSimpleFullEmittingBlock("minecraft:shroomlight", 15);

    config = BeginNextBlock("minecraft:weeping_vines");
    AddProperty(config, BLOCK_PROPERTY_AGE_25, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);

    InitSimpleEmptyBlock("minecraft:weeping_vines_plant");

    config = BeginNextBlock("minecraft:twisting_vines");
    AddProperty(config, BLOCK_PROPERTY_AGE_25, "0");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);

    InitSimpleEmptyBlock("minecraft:twisting_vines_plant");

    InitSimpleNetherPlant("minecraft:crimson_roots");

    config = BeginNextBlock("minecraft:structure_block");
    AddProperty(config, BLOCK_PROPERTY_STRUCTUREBLOCK_MODE, "save");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    config = BeginNextBlock("minecraft:jigsaw");
    AddProperty(config, BLOCK_PROPERTY_JIGSAW_ORIENTATION, "north_up");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    config = BeginNextBlock("minecraft:composter");
    AddProperty(config, BLOCK_PROPERTY_LEVEL_COMPOSTER, "0");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_COMPOSTER, BLOCK_MODEL_COMPOSTER, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 0);

    config = BeginNextBlock("minecraft:target");
    AddProperty(config, BLOCK_PROPERTY_POWER, "0");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    config = BeginNextBlock("minecraft:bee_nest");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_LEVEL_HONEY, "0");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    config = BeginNextBlock("minecraft:beehive");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_LEVEL_HONEY, "0");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);

    InitSimpleBlockWithModels("minecraft:honey_block", BLOCK_MODEL_HONEY_BLOCK, BLOCK_MODEL_HONEY_BLOCK, BLOCK_MODEL_EMPTY, 1, 0);
    InitSimpleFullBlock("minecraft:honeycomb_block");
    InitSimpleFullBlock("minecraft:netherite_block");
    InitSimpleFullBlock("minecraft:ancient_debris");
    InitSimpleFullEmittingBlock("minecraft:crying_obsidian", 10);

    config = BeginNextBlock("minecraft:respawn_anchor");
    AddProperty(config, BLOCK_PROPERTY_RESPAWN_ANCHOR_CHARGES, "0");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);
    for (i32 stateIndex = 0; stateIndex < config->stateCount; stateIndex++) {
        block_state_info info = DescribeStateIndex(&config->props, stateIndex);
        config->emittedLightByState[stateIndex] = 15 * info.respawn_anchor_charges / 4;
    }

    InitFlowerPot("minecraft:potted_crimson_roots");
    InitFlowerPot("minecraft:potted_warped_roots");
    InitSimpleFullBlock("minecraft:lodestone");

    InitSimpleFullBlock("minecraft:amethyst_block");
    InitSimpleFullBlock("minecraft:budding_amethyst");

    init_amethyst_cluster("minecraft:amethyst_cluster", 5);
    init_amethyst_cluster("minecraft:large_amethyst_bud", 4);
    init_amethyst_cluster("minecraft:medium_amethyst_bud", 2);
    init_amethyst_cluster("minecraft:small_amethyst_bud", 1);

    InitSimpleFullBlock("minecraft:tinted_glass");

    // @TODO(traks) correct collision model, support model is correct though!
    InitSimpleBlockWithModels("minecraft:powder_snow", BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, BLOCK_MODEL_EMPTY, 1, 0);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:sculk_sensor");
    AddProperty(config, BLOCK_PROPERTY_SCULK_SENSOR_PHASE, "inactive");
    AddProperty(config, BLOCK_PROPERTY_POWER, "0");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetEmittedLightForAllStates(config, 1);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    // TODO(traks): block models, light reduction, light emission, behaviours, etc.
    config = BeginNextBlock("minecraft:calibrated_sculk_sensor");
    AddProperty(config, BLOCK_PROPERTY_SCULK_SENSOR_PHASE, "inactive");
    AddProperty(config, BLOCK_PROPERTY_POWER, "0");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");

    InitSimpleFullBlock("minecraft:sculk");
    InitMultiFaceBlock("minecraft:sculk_vein", 0);

    config = BeginNextBlock("minecraft:sculk_catalyst");
    AddProperty(config, BLOCK_PROPERTY_BLOOM, "false");
    SetAllModelsForAllStatesIndividually(config, BLOCK_MODEL_FULL, BLOCK_MODEL_FULL, BLOCK_MODEL_EMPTY);
    SetLightReductionForAllStates(config, 15);
    SetEmittedLightForAllStates(config, 6);

    config = BeginNextBlock("minecraft:sculk_shrieker");
    AddProperty(config, BLOCK_PROPERTY_SHRIEKING, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddProperty(config, BLOCK_PROPERTY_CAN_SUMMON, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_Y_8);
    SetLightReductionForAllStates(config, 0);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    // TODO(traks) block models + light reduction
    config = BeginNextBlock("minecraft:lightning_rod");
    AddProperty(config, BLOCK_PROPERTY_FACING, "up");
    AddProperty(config, BLOCK_PROPERTY_POWERED, "false");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:pointed_dripstone");
    AddProperty(config, BLOCK_PROPERTY_VERTICAL_DIRECTION, "up");
    AddProperty(config, BLOCK_PROPERTY_DRIPSTONE_THICKNESS, "tip");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    InitSimpleFullBlock("minecraft:dripstone_block");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:cave_vines");
    AddProperty(config, BLOCK_PROPERTY_AGE_25, "0");
    AddProperty(config, BLOCK_PROPERTY_BERRIES, "false");
    SetEmittedLightWhenBerries(config, 14);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:cave_vines_plant");
    AddProperty(config, BLOCK_PROPERTY_BERRIES, "false");
    SetEmittedLightWhenBerries(config, 14);

    config = BeginNextBlock("minecraft:spore_blossom");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_NEED_POLE_SUPPORT_ABOVE);

    InitAzalea("minecraft:azalea");
    InitAzalea("minecraft:flowering_azalea");

    InitCarpet("minecraft:moss_carpet");

    // TODO(traks): block models, light reduction, behaviour, etc.
    config = BeginNextBlock("minecraft:pink_petals");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_FLOWER_AMOUNT, "1");

    InitSimpleFullBlock("minecraft:moss_block");

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:big_dripleaf");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_DRIPLEAF_TILT, "none");
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_BIG_DRIPLEAF);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:big_dripleaf_stem");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_BIG_DRIPLEAF_STEM);

    // TODO(traks): block models + light reduction
    config = BeginNextBlock("minecraft:small_dripleaf");
    AddProperty(config, BLOCK_PROPERTY_DOUBLE_BLOCK_HALF, "lower");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_SMALL_DRIPLEAF);

    config = BeginNextBlock("minecraft:hanging_roots");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");
    SetAllModelsForAllStates(config, BLOCK_MODEL_EMPTY);
    AddBlockBehaviour(config, BLOCK_BEHAVIOUR_FLUID);

    InitSimpleFullBlock("minecraft:rooted_dirt");
    InitSimpleFullBlock("minecraft:mud");

    InitSimpleFullBlock("minecraft:smooth_basalt");

    InitFlowerPot("minecraft:potted_azalea_bush");
    InitFlowerPot("minecraft:potted_flowering_azalea_bush");

    init_pillar("minecraft:ochre_froglight", 15);
    init_pillar("minecraft:verdant_froglight", 15);
    init_pillar("minecraft:pearlescent_froglight", 15);
    InitSimpleEmptyBlock("minecraft:frogspawn");

    // TODO(traks): block models, behaviours, light behaviour, etc. This is a
    // block entity
    config = BeginNextBlock("minecraft:decorated_pot");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");

    // TODO(traks): block models, behaviours, light behaviour, etc. This is a
    // block entity
    config = BeginNextBlock("minecraft:crafter");
    AddProperty(config, BLOCK_PROPERTY_JIGSAW_ORIENTATION, "north_up");
    AddProperty(config, BLOCK_PROPERTY_TRIGGERED, "false");
    AddProperty(config, BLOCK_PROPERTY_CRAFTING, "false");

    // TODO(traks): block models, behaviours, light behaviour, etc. This is a
    // block entity
    config = BeginNextBlock("minecraft:trial_spawner");
    AddProperty(config, BLOCK_PROPERTY_TRIAL_SPAWNER_STATE, "inactive");
    AddProperty(config, BLOCK_PROPERTY_OMINOUS, "false");

    // TODO(traks): block models, behaviours, light behaviour, etc. This is a
    // block entity
    config = BeginNextBlock("minecraft:vault");
    AddProperty(config, BLOCK_PROPERTY_HORIZONTAL_FACING, "north");
    AddProperty(config, BLOCK_PROPERTY_VAULT_STATE, "inactive");
    AddProperty(config, BLOCK_PROPERTY_OMINOUS, "false");

    // TODO(traks): block models, behaviours, light behaviour, etc.
    config = BeginNextBlock("minecraft:heavy_core");
    AddProperty(config, BLOCK_PROPERTY_WATERLOGGED, "false");

    // TODO(traks): behaviours
    InitSimpleFullBlock("minecraft:pale_moss_block");
    InitCarpet("minecraft:pale_moss_carpet");
    // TODO(traks): block models, behaviours, light, etc.
    config = BeginNextBlock("minecraft:pale_hanging_moss");
    AddProperty(config, BLOCK_PROPERTY_TIP, "true");

    InitSimpleFullBlock("blaze:unknown");

    FinaliseBlocks();

    assert(MAX_BLOCK_STATES >= serv->actual_block_state_count);
    assert(CeilLog2U32(serv->vanilla_block_state_count) == BITS_PER_BLOCK_STATE);
    assert(ACTUAL_BLOCK_TYPE_COUNT == serv->blockRegistry.entryCount);

    LogInfo("Block state count: %d (ceillog2 = %d)", serv->vanilla_block_state_count, CeilLog2U32(serv->vanilla_block_state_count));
}
