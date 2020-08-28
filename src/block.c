#include "shared.h"

typedef struct {
    net_block_pos pos;
    unsigned char from_direction;
} block_update;

int
can_plant_survive_on(mc_int type_below) {
    switch (type_below) {
    case BLOCK_GRASS_BLOCK:
    case BLOCK_DIRT:
    case BLOCK_COARSE_DIRT:
    case BLOCK_PODZOL:
    case BLOCK_FARMLAND:
        return 1;
    default:
        return 0;
    }
}

int
can_carpet_survive_on(mc_int type_below) {
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
can_dead_bush_survive_on(mc_int type_below) {
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
    case BLOCK_DIRT:
    case BLOCK_COARSE_DIRT:
    case BLOCK_PODZOL:
        return 1;
    default:
        return 0;
    }
}

int
can_wither_rose_survive_on(mc_int type_below) {
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
can_nether_plant_survive_on(mc_int type_below) {
    // @TODO(traks) allow if type below has nylium block tag
    switch (type_below) {
    case BLOCK_SOUL_SOIL:
        return 1;
    default:
        return can_plant_survive_on(type_below);
    }
}

static int
update_block(net_block_pos pos, int from_direction, server * serv) {
    // @TODO(traks) ideally all these chunk lookups and block lookups should be
    // cached to make a single block update as fast as possible. It is after all
    // incredibly easy to create tons of block updates in a single tick.
    chunk_pos cur_ch_pos = {
        .x = pos.x >> 4,
        .z = pos.z >> 4,
    };
    chunk * cur_ch = get_chunk_if_loaded(cur_ch_pos);
    if (cur_ch == NULL) {
        return 0;
    }

    mc_ushort cur_state = chunk_get_block_state(cur_ch,
            pos.x & 0xf, pos.y, pos.z & 0xf);
    mc_int cur_type = serv->block_type_by_state[cur_state];

    net_block_pos from_pos = get_relative_block_pos(pos, from_direction);
    chunk_pos from_ch_pos = {
        .x = from_pos.x >> 4,
        .z = from_pos.z >> 4,
    };
    chunk * from_ch = get_chunk_if_loaded(from_ch_pos);
    if (from_ch == NULL) {
        return 0;
    }

    mc_ushort from_state = chunk_get_block_state(from_ch,
            from_pos.x & 0xf, from_pos.y, from_pos.z & 0xf);
    mc_int from_type = serv->block_type_by_state[from_state];

    // @TODO(traks) drop items if the block is broken

    // @TODO(traks) remove block entity data

    switch (cur_type) {
    case BLOCK_GRASS_BLOCK:
    case BLOCK_PODZOL:
    case BLOCK_MYCELIUM: {
        if (from_direction != DIRECTION_POS_Y) {
            return 0;
        }

        mc_ushort new_state = serv->block_properties_table[cur_type].base_state;
        mc_int type_above = from_type;
        if (type_above == BLOCK_SNOW_BLOCK || type_above == BLOCK_SNOW) {
            new_state += 0;
        } else {
            new_state += 1;
        }

        if (new_state != cur_state) {
            chunk_set_block_state(cur_ch, pos.x & 0xf, pos.y, pos.z & 0xf, new_state);
            return 1;
        }
        return 0;
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

        mc_int type_below = from_type;
        if (can_plant_survive_on(type_below)) {
            return 0;
        }

        chunk_set_block_state(cur_ch, pos.x & 0xf, pos.y, pos.z & 0xf, 0);
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
        break;
    case BLOCK_SPRUCE_LEAVES:
        break;
    case BLOCK_BIRCH_LEAVES:
        break;
    case BLOCK_JUNGLE_LEAVES:
        break;
    case BLOCK_ACACIA_LEAVES:
        break;
    case BLOCK_DARK_OAK_LEAVES:
        break;
    case BLOCK_SPONGE:
        break;
    case BLOCK_DISPENSER:
        break;
    case BLOCK_NOTE_BLOCK:
        break;
    case BLOCK_WHITE_BED:
        break;
    case BLOCK_ORANGE_BED:
        break;
    case BLOCK_MAGENTA_BED:
        break;
    case BLOCK_LIGHT_BLUE_BED:
        break;
    case BLOCK_YELLOW_BED:
        break;
    case BLOCK_LIME_BED:
        break;
    case BLOCK_PINK_BED:
        break;
    case BLOCK_GRAY_BED:
        break;
    case BLOCK_LIGHT_GRAY_BED:
        break;
    case BLOCK_CYAN_BED:
        break;
    case BLOCK_PURPLE_BED:
        break;
    case BLOCK_BLUE_BED:
        break;
    case BLOCK_BROWN_BED:
        break;
    case BLOCK_GREEN_BED:
        break;
    case BLOCK_RED_BED:
        break;
    case BLOCK_BLACK_BED:
        break;
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

        mc_int type_below = from_type;
        if (can_dead_bush_survive_on(type_below)) {
            return 0;
        }

        chunk_set_block_state(cur_ch, pos.x & 0xf, pos.y, pos.z & 0xf, 0);
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

        mc_int type_below = from_type;
        if (can_wither_rose_survive_on(type_below)) {
            return 0;
        }

        chunk_set_block_state(cur_ch, pos.x & 0xf, pos.y, pos.z & 0xf, 0);
        return 1;
    }
    case BLOCK_BROWN_MUSHROOM:
        break;
    case BLOCK_RED_MUSHROOM:
        break;
    case BLOCK_TNT:
        break;
    case BLOCK_TORCH:
        break;
    case BLOCK_WALL_TORCH:
        break;
    case BLOCK_FIRE:
        break;
    case BLOCK_SOUL_FIRE:
        break;
    case BLOCK_SPAWNER:
        break;
    case BLOCK_OAK_STAIRS:
        break;
    case BLOCK_CHEST:
        break;
    case BLOCK_REDSTONE_WIRE:
        break;
    case BLOCK_WHEAT:
    case BLOCK_BEETROOTS:
    case BLOCK_CARROTS:
    case BLOCK_POTATOES: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        // @TODO(traks) light level also needs to be sufficient
        mc_int type_below = from_type;
        if (type_below == BLOCK_FARMLAND) {
            return 0;
        }

        chunk_set_block_state(cur_ch, pos.x & 0xf, pos.y, pos.z & 0xf, 0);
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
        break;
    case BLOCK_LADDER:
        break;
    case BLOCK_RAIL:
        break;
    case BLOCK_COBBLESTONE_STAIRS:
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
    case BLOCK_LEVER:
        break;
    case BLOCK_STONE_PRESSURE_PLATE:
        break;
    case BLOCK_IRON_DOOR:
        break;
    case BLOCK_OAK_PRESSURE_PLATE:
        break;
    case BLOCK_SPRUCE_PRESSURE_PLATE:
        break;
    case BLOCK_BIRCH_PRESSURE_PLATE:
        break;
    case BLOCK_JUNGLE_PRESSURE_PLATE:
        break;
    case BLOCK_ACACIA_PRESSURE_PLATE:
        break;
    case BLOCK_DARK_OAK_PRESSURE_PLATE:
        break;
    case BLOCK_REDSTONE_TORCH:
        break;
    case BLOCK_REDSTONE_WALL_TORCH:
        break;
    case BLOCK_STONE_BUTTON:
        break;
    case BLOCK_SNOW:
        break;
    case BLOCK_CACTUS:
        break;
    case BLOCK_SUGAR_CANE:
        break;
    case BLOCK_JUKEBOX:
        break;
    case BLOCK_OAK_FENCE:
        break;
    case BLOCK_NETHERRACK:
        break;
    case BLOCK_SOUL_SAND:
        break;
    case BLOCK_SOUL_TORCH:
        break;
    case BLOCK_SOUL_WALL_TORCH:
        break;
    case BLOCK_NETHER_PORTAL:
        break;
    case BLOCK_CAKE:
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
        break;
    case BLOCK_RED_MUSHROOM_BLOCK:
        break;
    case BLOCK_MUSHROOM_STEM:
        break;
    case BLOCK_IRON_BARS:
        break;
    case BLOCK_CHAIN:
        break;
    case BLOCK_GLASS_PANE:
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

        mc_int type_below = from_type;
        if (type_below == BLOCK_FARMLAND) {
            return 0;
        }

        chunk_set_block_state(cur_ch, pos.x & 0xf, pos.y, pos.z & 0xf, 0);
        return 1;
    }
    case BLOCK_VINE:
        break;
    case BLOCK_OAK_FENCE_GATE:
        break;
    case BLOCK_BRICK_STAIRS:
        break;
    case BLOCK_STONE_BRICK_STAIRS:
        break;
    case BLOCK_LILY_PAD:
        break;
    case BLOCK_NETHER_BRICK_FENCE:
        break;
    case BLOCK_NETHER_BRICK_STAIRS:
        break;
    case BLOCK_NETHER_WART: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        mc_int type_below = from_type;
        if (type_below == BLOCK_SOUL_SAND) {
            return 0;
        }

        chunk_set_block_state(cur_ch, pos.x & 0xf, pos.y, pos.z & 0xf, 0);
        return 1;
    }
    case BLOCK_ENCHANTING_TABLE:
        break;
    case BLOCK_BREWING_STAND:
        break;
    case BLOCK_CAULDRON:
        break;
    case BLOCK_END_PORTAL:
        break;
    case BLOCK_END_PORTAL_FRAME:
        break;
    case BLOCK_DRAGON_EGG:
        break;
    case BLOCK_REDSTONE_LAMP:
        break;
    case BLOCK_COCOA:
        break;
    case BLOCK_SANDSTONE_STAIRS:
        break;
    case BLOCK_ENDER_CHEST:
        break;
    case BLOCK_TRIPWIRE_HOOK:
        break;
    case BLOCK_TRIPWIRE:
        break;
    case BLOCK_SPRUCE_STAIRS:
        break;
    case BLOCK_BIRCH_STAIRS:
        break;
    case BLOCK_JUNGLE_STAIRS:
        break;
    case BLOCK_COMMAND_BLOCK:
        break;
    case BLOCK_BEACON:
        break;
    case BLOCK_COBBLESTONE_WALL:
        break;
    case BLOCK_MOSSY_COBBLESTONE_WALL:
        break;
    case BLOCK_FLOWER_POT:
        break;
    case BLOCK_POTTED_OAK_SAPLING:
        break;
    case BLOCK_POTTED_SPRUCE_SAPLING:
        break;
    case BLOCK_POTTED_BIRCH_SAPLING:
        break;
    case BLOCK_POTTED_JUNGLE_SAPLING:
        break;
    case BLOCK_POTTED_ACACIA_SAPLING:
        break;
    case BLOCK_POTTED_DARK_OAK_SAPLING:
        break;
    case BLOCK_POTTED_FERN:
        break;
    case BLOCK_POTTED_DANDELION:
        break;
    case BLOCK_POTTED_POPPY:
        break;
    case BLOCK_POTTED_BLUE_ORCHID:
        break;
    case BLOCK_POTTED_ALLIUM:
        break;
    case BLOCK_POTTED_AZURE_BLUET:
        break;
    case BLOCK_POTTED_RED_TULIP:
        break;
    case BLOCK_POTTED_ORANGE_TULIP:
        break;
    case BLOCK_POTTED_WHITE_TULIP:
        break;
    case BLOCK_POTTED_PINK_TULIP:
        break;
    case BLOCK_POTTED_OXEYE_DAISY:
        break;
    case BLOCK_POTTED_CORNFLOWER:
        break;
    case BLOCK_POTTED_LILY_OF_THE_VALLEY:
        break;
    case BLOCK_POTTED_WITHER_ROSE:
        break;
    case BLOCK_POTTED_RED_MUSHROOM:
        break;
    case BLOCK_POTTED_BROWN_MUSHROOM:
        break;
    case BLOCK_POTTED_DEAD_BUSH:
        break;
    case BLOCK_POTTED_CACTUS:
        break;
    case BLOCK_OAK_BUTTON:
        break;
    case BLOCK_SPRUCE_BUTTON:
        break;
    case BLOCK_BIRCH_BUTTON:
        break;
    case BLOCK_JUNGLE_BUTTON:
        break;
    case BLOCK_ACACIA_BUTTON:
        break;
    case BLOCK_DARK_OAK_BUTTON:
        break;
    case BLOCK_ANVIL:
        break;
    case BLOCK_CHIPPED_ANVIL:
        break;
    case BLOCK_DAMAGED_ANVIL:
        break;
    case BLOCK_TRAPPED_CHEST:
        break;
    case BLOCK_LIGHT_WEIGHTED_PRESSURE_PLATE:
        break;
    case BLOCK_HEAVY_WEIGHTED_PRESSURE_PLATE:
        break;
    case BLOCK_COMPARATOR:
        break;
    case BLOCK_DAYLIGHT_DETECTOR:
        break;
    case BLOCK_REDSTONE_BLOCK:
        break;
    case BLOCK_HOPPER:
        break;
    case BLOCK_QUARTZ_STAIRS:
        break;
    case BLOCK_ACTIVATOR_RAIL:
        break;
    case BLOCK_DROPPER:
        break;
    case BLOCK_WHITE_STAINED_GLASS_PANE:
        break;
    case BLOCK_ORANGE_STAINED_GLASS_PANE:
        break;
    case BLOCK_MAGENTA_STAINED_GLASS_PANE:
        break;
    case BLOCK_LIGHT_BLUE_STAINED_GLASS_PANE:
        break;
    case BLOCK_YELLOW_STAINED_GLASS_PANE:
        break;
    case BLOCK_LIME_STAINED_GLASS_PANE:
        break;
    case BLOCK_PINK_STAINED_GLASS_PANE:
        break;
    case BLOCK_GRAY_STAINED_GLASS_PANE:
        break;
    case BLOCK_LIGHT_GRAY_STAINED_GLASS_PANE:
        break;
    case BLOCK_CYAN_STAINED_GLASS_PANE:
        break;
    case BLOCK_PURPLE_STAINED_GLASS_PANE:
        break;
    case BLOCK_BLUE_STAINED_GLASS_PANE:
        break;
    case BLOCK_BROWN_STAINED_GLASS_PANE:
        break;
    case BLOCK_GREEN_STAINED_GLASS_PANE:
        break;
    case BLOCK_RED_STAINED_GLASS_PANE:
        break;
    case BLOCK_BLACK_STAINED_GLASS_PANE:
        break;
    case BLOCK_ACACIA_STAIRS:
        break;
    case BLOCK_DARK_OAK_STAIRS:
        break;
    case BLOCK_SLIME_BLOCK:
        break;
    case BLOCK_IRON_TRAPDOOR:
        break;
    case BLOCK_PRISMARINE_STAIRS:
        break;
    case BLOCK_PRISMARINE_BRICK_STAIRS:
        break;
    case BLOCK_DARK_PRISMARINE_STAIRS:
        break;
    case BLOCK_PRISMARINE_SLAB:
        break;
    case BLOCK_PRISMARINE_BRICK_SLAB:
        break;
    case BLOCK_DARK_PRISMARINE_SLAB:
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
    case BLOCK_BLACK_CARPET: {
        if (from_direction != DIRECTION_NEG_Y) {
            return 0;
        }

        mc_int type_below = from_type;
        if (can_carpet_survive_on(type_below)) {
            return 0;
        }

        chunk_set_block_state(cur_ch, pos.x & 0xf, pos.y, pos.z & 0xf, 0);
        return 1;
    }
    case BLOCK_SUNFLOWER:
    case BLOCK_LILAC:
    case BLOCK_ROSE_BUSH:
    case BLOCK_PEONY:
    case BLOCK_TALL_GRASS:
    case BLOCK_LARGE_FERN: {
        mc_ushort base_state = serv->block_properties_table[cur_type].base_state;
        mc_ushort upper_state = base_state;
        mc_ushort lower_state = base_state + 1;
        if (cur_state == upper_state) {
            if (from_direction == DIRECTION_NEG_Y && from_type != lower_state) {
                chunk_set_block_state(cur_ch, pos.x & 0xf, pos.y, pos.z & 0xf, 0);
                return 1;
            }
        } else {
            if (from_direction == DIRECTION_NEG_Y) {
                if (!can_plant_survive_on(from_type)) {
                    chunk_set_block_state(cur_ch, pos.x & 0xf, pos.y, pos.z & 0xf, 0);
                    return 1;
                }
            } else if (from_direction == DIRECTION_POS_Y) {
                if (from_state != upper_state) {
                    chunk_set_block_state(cur_ch, pos.x & 0xf, pos.y, pos.z & 0xf, 0);
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
    case BLOCK_RED_SANDSTONE_STAIRS:
        break;
    case BLOCK_OAK_SLAB:
        break;
    case BLOCK_SPRUCE_SLAB:
        break;
    case BLOCK_BIRCH_SLAB:
        break;
    case BLOCK_JUNGLE_SLAB:
        break;
    case BLOCK_ACACIA_SLAB:
        break;
    case BLOCK_DARK_OAK_SLAB:
        break;
    case BLOCK_STONE_SLAB:
        break;
    case BLOCK_SMOOTH_STONE_SLAB:
        break;
    case BLOCK_SANDSTONE_SLAB:
        break;
    case BLOCK_CUT_SANDSTONE_SLAB:
        break;
    case BLOCK_PETRIFIED_OAK_SLAB:
        break;
    case BLOCK_COBBLESTONE_SLAB:
        break;
    case BLOCK_BRICK_SLAB:
        break;
    case BLOCK_STONE_BRICK_SLAB:
        break;
    case BLOCK_NETHER_BRICK_SLAB:
        break;
    case BLOCK_QUARTZ_SLAB:
        break;
    case BLOCK_RED_SANDSTONE_SLAB:
        break;
    case BLOCK_CUT_RED_SANDSTONE_SLAB:
        break;
    case BLOCK_PURPUR_SLAB:
        break;
    case BLOCK_SPRUCE_FENCE_GATE:
        break;
    case BLOCK_BIRCH_FENCE_GATE:
        break;
    case BLOCK_JUNGLE_FENCE_GATE:
        break;
    case BLOCK_ACACIA_FENCE_GATE:
        break;
    case BLOCK_DARK_OAK_FENCE_GATE:
        break;
    case BLOCK_SPRUCE_FENCE:
        break;
    case BLOCK_BIRCH_FENCE:
        break;
    case BLOCK_JUNGLE_FENCE:
        break;
    case BLOCK_ACACIA_FENCE:
        break;
    case BLOCK_DARK_OAK_FENCE:
        break;
    case BLOCK_SPRUCE_DOOR:
        break;
    case BLOCK_BIRCH_DOOR:
        break;
    case BLOCK_JUNGLE_DOOR:
        break;
    case BLOCK_ACACIA_DOOR:
        break;
    case BLOCK_DARK_OAK_DOOR:
        break;
    case BLOCK_CHORUS_PLANT:
        break;
    case BLOCK_CHORUS_FLOWER:
        break;
    case BLOCK_PURPUR_STAIRS:
        break;
    case BLOCK_GRASS_PATH:
        break;
    case BLOCK_END_GATEWAY:
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
    case BLOCK_TURTLE_EGG:
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
    case BLOCK_SEA_PICKLE:
        break;
    case BLOCK_CONDUIT:
        break;
    case BLOCK_BAMBOO_SAPLING:
        break;
    case BLOCK_BAMBOO:
        break;
    case BLOCK_POTTED_BAMBOO:
        break;
    case BLOCK_BUBBLE_COLUMN:
        break;
    case BLOCK_POLISHED_GRANITE_STAIRS:
        break;
    case BLOCK_SMOOTH_RED_SANDSTONE_STAIRS:
        break;
    case BLOCK_MOSSY_STONE_BRICK_STAIRS:
        break;
    case BLOCK_POLISHED_DIORITE_STAIRS:
        break;
    case BLOCK_MOSSY_COBBLESTONE_STAIRS:
        break;
    case BLOCK_END_STONE_BRICK_STAIRS:
        break;
    case BLOCK_STONE_STAIRS:
        break;
    case BLOCK_SMOOTH_SANDSTONE_STAIRS:
        break;
    case BLOCK_SMOOTH_QUARTZ_STAIRS:
        break;
    case BLOCK_GRANITE_STAIRS:
        break;
    case BLOCK_ANDESITE_STAIRS:
        break;
    case BLOCK_RED_NETHER_BRICK_STAIRS:
        break;
    case BLOCK_POLISHED_ANDESITE_STAIRS:
        break;
    case BLOCK_DIORITE_STAIRS:
        break;
    case BLOCK_POLISHED_GRANITE_SLAB:
        break;
    case BLOCK_SMOOTH_RED_SANDSTONE_SLAB:
        break;
    case BLOCK_MOSSY_STONE_BRICK_SLAB:
        break;
    case BLOCK_POLISHED_DIORITE_SLAB:
        break;
    case BLOCK_MOSSY_COBBLESTONE_SLAB:
        break;
    case BLOCK_END_STONE_BRICK_SLAB:
        break;
    case BLOCK_SMOOTH_SANDSTONE_SLAB:
        break;
    case BLOCK_SMOOTH_QUARTZ_SLAB:
        break;
    case BLOCK_GRANITE_SLAB:
        break;
    case BLOCK_ANDESITE_SLAB:
        break;
    case BLOCK_RED_NETHER_BRICK_SLAB:
        break;
    case BLOCK_POLISHED_ANDESITE_SLAB:
        break;
    case BLOCK_DIORITE_SLAB:
        break;
    case BLOCK_BRICK_WALL:
        break;
    case BLOCK_PRISMARINE_WALL:
        break;
    case BLOCK_RED_SANDSTONE_WALL:
        break;
    case BLOCK_MOSSY_STONE_BRICK_WALL:
        break;
    case BLOCK_GRANITE_WALL:
        break;
    case BLOCK_STONE_BRICK_WALL:
        break;
    case BLOCK_NETHER_BRICK_WALL:
        break;
    case BLOCK_ANDESITE_WALL:
        break;
    case BLOCK_RED_NETHER_BRICK_WALL:
        break;
    case BLOCK_SANDSTONE_WALL:
        break;
    case BLOCK_END_STONE_BRICK_WALL:
        break;
    case BLOCK_DIORITE_WALL:
        break;
    case BLOCK_SCAFFOLDING:
        break;
    case BLOCK_GRINDSTONE:
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

        mc_int type_below = from_type;
        if (can_nether_plant_survive_on(type_below)) {
            return 0;
        }

        chunk_set_block_state(cur_ch, pos.x & 0xf, pos.y, pos.z & 0xf, 0);
        return 1;
    case BLOCK_WEEPING_VINES:
        break;
    case BLOCK_WEEPING_VINES_PLANT:
        break;
    case BLOCK_TWISTING_VINES:
        break;
    case BLOCK_TWISTING_VINES_PLANT:
        break;
    case BLOCK_CRIMSON_SLAB:
        break;
    case BLOCK_WARPED_SLAB:
        break;
    case BLOCK_CRIMSON_PRESSURE_PLATE:
        break;
    case BLOCK_WARPED_PRESSURE_PLATE:
        break;
    case BLOCK_CRIMSON_FENCE:
        break;
    case BLOCK_WARPED_FENCE:
        break;
    case BLOCK_CRIMSON_TRAPDOOR:
        break;
    case BLOCK_WARPED_TRAPDOOR:
        break;
    case BLOCK_CRIMSON_FENCE_GATE:
        break;
    case BLOCK_WARPED_FENCE_GATE:
        break;
    case BLOCK_CRIMSON_STAIRS:
        break;
    case BLOCK_WARPED_STAIRS:
        break;
    case BLOCK_CRIMSON_BUTTON:
        break;
    case BLOCK_WARPED_BUTTON:
        break;
    case BLOCK_CRIMSON_DOOR:
        break;
    case BLOCK_WARPED_DOOR:
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
    case BLOCK_JIGSAW:
        break;
    case BLOCK_COMPOSTER:
        break;
    case BLOCK_TARGET:
        break;
    case BLOCK_BEE_NEST:
        break;
    case BLOCK_BEEHIVE:
        break;
    case BLOCK_RESPAWN_ANCHOR:
        break;
    case BLOCK_POTTED_CRIMSON_FUNGUS:
        break;
    case BLOCK_POTTED_WARPED_FUNGUS:
        break;
    case BLOCK_POTTED_CRIMSON_ROOTS:
        break;
    case BLOCK_POTTED_WARPED_ROOTS:
        break;
    case BLOCK_BLACKSTONE_STAIRS:
        break;
    case BLOCK_BLACKSTONE_WALL:
        break;
    case BLOCK_BLACKSTONE_SLAB:
        break;
    case BLOCK_POLISHED_BLACKSTONE_BRICK_SLAB:
        break;
    case BLOCK_POLISHED_BLACKSTONE_BRICK_STAIRS:
        break;
    case BLOCK_POLISHED_BLACKSTONE_BRICK_WALL:
        break;
    case BLOCK_POLISHED_BLACKSTONE_STAIRS:
        break;
    case BLOCK_POLISHED_BLACKSTONE_SLAB:
        break;
    case BLOCK_POLISHED_BLACKSTONE_PRESSURE_PLATE:
        break;
    case BLOCK_POLISHED_BLACKSTONE_BUTTON:
        break;
    case BLOCK_POLISHED_BLACKSTONE_WALL:
        break;
    default:
         // nothing
         break;
    }
    return 0;
}

void
propagate_block_updates_after_change(net_block_pos change_pos,
        server * serv, memory_arena * scratch_arena) {
    unsigned char update_order[] = {
        DIRECTION_NEG_X, DIRECTION_POS_X,
        DIRECTION_NEG_Z, DIRECTION_POS_Z,
        DIRECTION_NEG_Y, DIRECTION_POS_Y,
    };

    memory_arena temp_arena = *scratch_arena;
    int max_updates = 512;
    block_update * blocks_to_update = alloc_in_arena(&temp_arena,
            max_updates * sizeof (*blocks_to_update));
    int update_count = 0;

    for (int j = 0; j < 6; j++) {
        int to_direction = update_order[j];
        net_block_pos neighbour = get_relative_block_pos(change_pos, to_direction);
        if (neighbour.y > MAX_WORLD_Y || neighbour.y < 0) {
            continue;
        }

        blocks_to_update[update_count] = (block_update) {
            .pos = neighbour,
            .from_direction = get_opposite_direction(to_direction),
        };
        update_count++;
    }

    update_block(change_pos, DIRECTION_ZERO, serv);

    for (int i = 0; i < update_count; i++) {
        net_block_pos pos = blocks_to_update[i].pos;
        int from_direction = blocks_to_update[i].from_direction;

        if (!update_block(pos, from_direction, serv)) {
            continue;
        }

        if (max_updates - update_count < 6) {
            continue;
        }

        for (int j = 0; j < 6; j++) {
            int to_direction = update_order[j];
            net_block_pos neighbour = get_relative_block_pos(pos, to_direction);
            if (neighbour.y > MAX_WORLD_Y || neighbour.y < 0) {
                continue;
            }

            blocks_to_update[update_count] = (block_update) {
                .pos = neighbour,
                .from_direction = get_opposite_direction(to_direction),
            };
            update_count++;
        }
    }
}
