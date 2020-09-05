#include <string.h>
#include <stdarg.h>
#include <stdio.h>
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

static void
add_block_prop_direct(server * serv, block_properties * props,
        char * key, char * def, int value_count, char * * values) {
    block_property_spec prop_spec = {0};
    prop_spec.value_count = value_count;

    int key_size = strlen(key);

    unsigned char * tape = prop_spec.tape;
    *tape = key_size;
    tape++;
    memcpy(tape, key, key_size);
    tape += key_size;

    for (int i = 0; i < value_count; i++) {
        int value_size = strlen(values[i]);
        *tape = value_size;
        tape++;
        memcpy(tape, values[i], value_size);
        tape += value_size;
    }

    // check if property spec already exists, else add it to the list
    int spec_index;
    for (spec_index = 0; spec_index < serv->block_property_spec_count; spec_index++) {
        block_property_spec * existing = serv->block_property_specs + spec_index;
        if (memcmp(existing, &prop_spec, sizeof prop_spec) == 0) {
            break;
        }
    }

    if (spec_index == serv->block_property_spec_count) {
        serv->block_property_specs[spec_index] = prop_spec;
        serv->block_property_spec_count++;
    }

    // figure out index of default value
    int def_index;
    for (def_index = 0; def_index < value_count; def_index++) {
        net_string value = {
            .size = strlen(values[def_index]),
            .ptr = values[def_index]
        };
        net_string def_str = {
            .size = strlen(def),
            .ptr = def
        };
        if (net_string_equal(def_str, value)) {
            break;
        }
    }
    assert(def_index < value_count);

    int prop_index = props->property_count;
    props->property_specs[prop_index] = spec_index;
    props->default_value_indices[prop_index] = def_index;

    props->property_count++;
}

static void
add_block_prop(server * serv, block_properties * props,
        char * key, char * def, int value_count, ...) {
    va_list ap;
    va_start(ap, value_count);

    char * values[value_count];
    for (int i = 0; i < value_count; i++) {
        values[i] = va_arg(ap, char *);
    }

    va_end(ap);

    add_block_prop_direct(serv, props, key, def, value_count, values);
}

static void
add_block_range_prop(server * serv, block_properties * props,
        char * key, int def, int min, int max) {
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

    // default is min
    add_block_prop_direct(serv, props, key, values[def - min], value_count, values);
}

static void
finalise_block_props(server * serv, block_properties * props) {
    mc_int block_type = props - serv->block_properties_table;
    props->base_state = serv->block_state_count;

    int block_states = 1;
    for (int i = 0; i < props->property_count; i++) {
        block_states *= serv->block_property_specs[props->property_specs[i]].value_count;
    }

    for (int i = 0; i < block_states; i++) {
        serv->block_type_by_state[serv->block_state_count + i] = block_type;
    }

    serv->block_state_count += block_states;
}

static void
register_block_type(server * serv, mc_int block_type, char * resource_loc) {
    net_string key = {
        .size = strlen(resource_loc),
        .ptr = resource_loc
    };
    resource_loc_table * table = &serv->block_resource_table;
    register_resource_loc(key, block_type, table);
    assert(net_string_equal(key, get_resource_loc(block_type, table)));
    assert(block_type == resolve_resource_loc_id(key, table));
}

static void
init_no_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    finalise_block_props(serv, props);
}

static void
init_sapling_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_range_prop(serv, props, "stage", 0, 0, 1);
    finalise_block_props(serv, props);
}

static void
init_pillar_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "axis", "y", 3, "x", "y", "z");
    finalise_block_props(serv, props);
}

static void
init_leaves_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_range_prop(serv, props, "distance", 7, 1, 7);
    add_block_prop(serv, props, "persistent", "false", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_bed_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "occupied", "false", 2, "true", "false");
    add_block_prop(serv, props, "part", "foot", 2, "head", "foot");
    finalise_block_props(serv, props);
}

static void
init_slab_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "type", "bottom", 3, "top", "bottom", "double");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_sign_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_range_prop(serv, props, "rotation", 0, 0, 15);
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_wall_sign_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_stair_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "half", "bottom", 2, "top", "bottom");
    add_block_prop(serv, props, "shape", "straight", 5, "straight", "inner_left", "inner_right", "outer_left", "outer_right");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_tall_plant_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "half", "lower", 2, "upper", "lower");
    finalise_block_props(serv, props);
}

static void
init_glazed_terracotta_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);
}

static void
init_shulker_box_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "facing", "up", 6, "north", "east", "south", "west", "up", "down");
    finalise_block_props(serv, props);
}

static void
init_wall_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "east", "none", 3, "none", "low", "tall");
    add_block_prop(serv, props, "north", "none", 3, "none", "low", "tall");
    add_block_prop(serv, props, "south", "none", 3, "none", "low", "tall");
    add_block_prop(serv, props, "up", "true", 2, "true", "false");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    add_block_prop(serv, props, "west", "none", 3, "none", "low", "tall");
    finalise_block_props(serv, props);
}

static void
init_pressure_plate_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_cross_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "east", "false", 2, "true", "false");
    add_block_prop(serv, props, "north", "false", 2, "true", "false");
    add_block_prop(serv, props, "south", "false", 2, "true", "false");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    add_block_prop(serv, props, "west", "false", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_door_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "half", "lower", 2, "upper", "lower");
    add_block_prop(serv, props, "hinge", "left", 2, "left", "right");
    add_block_prop(serv, props, "open", "false", 2, "true", "false");
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_button_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "face", "wall", 3, "floor", "wall", "ceiling");
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_trapdoor_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "half", "bottom", 2, "top", "bottom");
    add_block_prop(serv, props, "open", "false", 2, "true", "false");
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_fence_gate_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "in_wall", "false", 2, "true", "false");
    add_block_prop(serv, props, "open", "false", 2, "true", "false");
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_mushroom_block_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "down", "true", 2, "true", "false");
    add_block_prop(serv, props, "east", "true", 2, "true", "false");
    add_block_prop(serv, props, "north", "true", 2, "true", "false");
    add_block_prop(serv, props, "south", "true", 2, "true", "false");
    add_block_prop(serv, props, "up", "true", 2, "true", "false");
    add_block_prop(serv, props, "west", "true", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_skull_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_range_prop(serv, props, "rotation", 0, 0, 15);
    finalise_block_props(serv, props);
}

static void
init_wall_skull_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);
}

static void
init_anvil_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);
}

static void
init_banner_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_range_prop(serv, props, "rotation", 0, 0, 15);
    finalise_block_props(serv, props);
}

static void
init_wall_banner_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);
}

static void
init_coral_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "waterlogged", "true", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_coral_fan_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "waterlogged", "true", 2, "true", "false");
    finalise_block_props(serv, props);
}

static void
init_coral_wall_fan_props(server * serv, mc_int block_type, char * resource_loc) {
    register_block_type(serv, block_type, resource_loc);
    block_properties * props = serv->block_properties_table + block_type;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "waterlogged", "true", 2, "true", "false");
    finalise_block_props(serv, props);
}

void
init_block_data(server * serv) {
    // @TODO(traks) all these resource locations were very annoying to type out.
    // Perhaps we could write a program that converts all the block type enum
    // entries into resource locations and writes them to the resource location
    // table. We could even perform some optimisations of the hash function
    // there to reduce collisions.
    block_properties * props;

    init_no_props(serv, BLOCK_AIR, "minecraft:air");
    init_no_props(serv, BLOCK_STONE, "minecraft:stone");
    init_no_props(serv, BLOCK_GRANITE, "minecraft:granite");
    init_no_props(serv, BLOCK_POLISHED_GRANITE, "minecraft:polished_granite");
    init_no_props(serv, BLOCK_DIORITE, "minecraft:diorite");
    init_no_props(serv, BLOCK_POLISHED_DIORITE, "minecraft:polished_diorite");
    init_no_props(serv, BLOCK_ANDESITE, "minecraft:andesite");
    init_no_props(serv, BLOCK_POLISHED_ANDESITE, "minecraft:polished_andesite");

    register_block_type(serv, BLOCK_GRASS_BLOCK, "minecraft:grass_block");
    props = serv->block_properties_table + BLOCK_GRASS_BLOCK;
    add_block_prop(serv, props, "snowy", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_DIRT, "minecraft:dirt");
    init_no_props(serv, BLOCK_COARSE_DIRT, "minecraft:coarse_dirt");

    register_block_type(serv, BLOCK_PODZOL, "minecraft:podzol");
    props = serv->block_properties_table + BLOCK_PODZOL;
    add_block_prop(serv, props, "snowy", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_COBBLESTONE, "minecraft:cobblestone");
    init_no_props(serv, BLOCK_OAK_PLANKS, "minecraft:oak_planks");
    init_no_props(serv, BLOCK_SPRUCE_PLANKS, "minecraft:spruce_planks");
    init_no_props(serv, BLOCK_BIRCH_PLANKS, "minecraft:birch_planks");
    init_no_props(serv, BLOCK_JUNGLE_PLANKS, "minecraft:jungle_planks");
    init_no_props(serv, BLOCK_ACACIA_PLANKS, "minecraft:acacia_planks");
    init_no_props(serv, BLOCK_DARK_OAK_PLANKS, "minecraft:dark_oak_planks");

    init_sapling_props(serv, BLOCK_OAK_SAPLING, "minecraft:oak_sapling");
    init_sapling_props(serv, BLOCK_SPRUCE_SAPLING, "minecraft:spruce_sapling");
    init_sapling_props(serv, BLOCK_BIRCH_SAPLING, "minecraft:birch_sapling");
    init_sapling_props(serv, BLOCK_JUNGLE_SAPLING, "minecraft:jungle_sapling");
    init_sapling_props(serv, BLOCK_ACACIA_SAPLING, "minecraft:acacia_sapling");
    init_sapling_props(serv, BLOCK_DARK_OAK_SAPLING, "minecraft:dark_oak_sapling");

    init_no_props(serv, BLOCK_BEDROCK, "minecraft:bedrock");

    register_block_type(serv, BLOCK_WATER, "minecraft:water");
    props = serv->block_properties_table + BLOCK_WATER;
    add_block_range_prop(serv, props, "level", 0, 0, 15);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_LAVA, "minecraft:lava");
    props = serv->block_properties_table + BLOCK_LAVA;
    add_block_range_prop(serv, props, "level", 0, 0, 15);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_SAND, "minecraft:sand");
    init_no_props(serv, BLOCK_RED_SAND, "minecraft:red_sand");
    init_no_props(serv, BLOCK_GRAVEL, "minecraft:gravel");
    init_no_props(serv, BLOCK_GOLD_ORE, "minecraft:gold_ore");
    init_no_props(serv, BLOCK_IRON_ORE, "minecraft:iron_ore");
    init_no_props(serv, BLOCK_COAL_ORE, "minecraft:coal_ore");
    init_no_props(serv, BLOCK_NETHER_GOLD_ORE, "minecraft:nether_gold_ore");

    init_pillar_props(serv, BLOCK_OAK_LOG, "minecraft:oak_log");
    init_pillar_props(serv, BLOCK_SPRUCE_LOG, "minecraft:spruce_log");
    init_pillar_props(serv, BLOCK_BIRCH_LOG, "minecraft:birch_log");
    init_pillar_props(serv, BLOCK_JUNGLE_LOG, "minecraft:jungle_log");
    init_pillar_props(serv, BLOCK_ACACIA_LOG, "minecraft:acacia_log");
    init_pillar_props(serv, BLOCK_DARK_OAK_LOG, "minecraft:dark_oak_log");
    init_pillar_props(serv, BLOCK_STRIPPED_SPRUCE_LOG, "minecraft:stripped_spruce_log");
    init_pillar_props(serv, BLOCK_STRIPPED_BIRCH_LOG, "minecraft:stripped_birch_log");
    init_pillar_props(serv, BLOCK_STRIPPED_JUNGLE_LOG, "minecraft:stripped_jungle_log");
    init_pillar_props(serv, BLOCK_STRIPPED_ACACIA_LOG, "minecraft:stripped_acacia_log");
    init_pillar_props(serv, BLOCK_STRIPPED_DARK_OAK_LOG, "minecraft:stripped_dark_oak_log");
    init_pillar_props(serv, BLOCK_STRIPPED_OAK_LOG, "minecraft:stripped_oak_log");
    init_pillar_props(serv, BLOCK_OAK_WOOD, "minecraft:oak_wood");
    init_pillar_props(serv, BLOCK_SPRUCE_WOOD, "minecraft:spruce_wood");
    init_pillar_props(serv, BLOCK_BIRCH_WOOD, "minecraft:birch_wood");
    init_pillar_props(serv, BLOCK_JUNGLE_WOOD, "minecraft:jungle_wood");
    init_pillar_props(serv, BLOCK_ACACIA_WOOD, "minecraft:acacia_wood");
    init_pillar_props(serv, BLOCK_DARK_OAK_WOOD, "minecraft:dark_oak_wood");
    init_pillar_props(serv, BLOCK_STRIPPED_OAK_WOOD, "minecraft:stripped_oak_wood");
    init_pillar_props(serv, BLOCK_STRIPPED_SPRUCE_WOOD, "minecraft:stripped_spruce_wood");
    init_pillar_props(serv, BLOCK_STRIPPED_BIRCH_WOOD, "minecraft:stripped_birch_wood");
    init_pillar_props(serv, BLOCK_STRIPPED_JUNGLE_WOOD, "minecraft:stripped_jungle_wood");
    init_pillar_props(serv, BLOCK_STRIPPED_ACACIA_WOOD, "minecraft:stripped_acacia_wood");
    init_pillar_props(serv, BLOCK_STRIPPED_DARK_OAK_WOOD, "minecraft:stripped_dark_oak_wood");

    init_leaves_props(serv, BLOCK_OAK_LEAVES, "minecraft:oak_leaves");
    init_leaves_props(serv, BLOCK_SPRUCE_LEAVES, "minecraft:spruce_leaves");
    init_leaves_props(serv, BLOCK_BIRCH_LEAVES, "minecraft:birch_leaves");
    init_leaves_props(serv, BLOCK_JUNGLE_LEAVES, "minecraft:jungle_leaves");
    init_leaves_props(serv, BLOCK_ACACIA_LEAVES, "minecraft:acacia_leaves");
    init_leaves_props(serv, BLOCK_DARK_OAK_LEAVES, "minecraft:dark_oak_leaves");

    init_no_props(serv, BLOCK_SPONGE, "minecraft:sponge");
    init_no_props(serv, BLOCK_WET_SPONGE, "minecraft:wet_sponge");
    init_no_props(serv, BLOCK_GLASS, "minecraft:glass");
    init_no_props(serv, BLOCK_LAPIS_ORE, "minecraft:lapis_ore");
    init_no_props(serv, BLOCK_LAPIS_BLOCK, "minecraft:lapis_block");

    register_block_type(serv, BLOCK_DISPENSER, "minecraft:dispenser");
    props = serv->block_properties_table + BLOCK_DISPENSER;
    add_block_prop(serv, props, "facing", "north", 6, "north", "east", "south", "west", "up", "down");
    add_block_prop(serv, props, "triggered", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_SANDSTONE, "minecraft:sandstone");
    init_no_props(serv, BLOCK_CHISELED_SANDSTONE, "minecraft:chiseled_sandstone");
    init_no_props(serv, BLOCK_CUT_SANDSTONE, "minecraft:cut_sandstone");

    register_block_type(serv, BLOCK_NOTE_BLOCK, "minecraft:note_block");
    props = serv->block_properties_table + BLOCK_NOTE_BLOCK;
    add_block_prop(serv, props, "instrument", "harp", 16, "harp", "basedrum", "snare", "hat", "bass", "flute", "bell", "guitar", "chime", "xylophone", "iron_xylophone", "cow_bell", "didgeridoo", "bit", "banjo", "pling");
    add_block_range_prop(serv, props, "note", 0, 0, 24);
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_bed_props(serv, BLOCK_WHITE_BED, "minecraft:white_bed");
    init_bed_props(serv, BLOCK_ORANGE_BED, "minecraft:orange_bed");
    init_bed_props(serv, BLOCK_MAGENTA_BED, "minecraft:magenta_bed");
    init_bed_props(serv, BLOCK_LIGHT_BLUE_BED, "minecraft:light_blue_bed");
    init_bed_props(serv, BLOCK_YELLOW_BED, "minecraft:yellow_bed");
    init_bed_props(serv, BLOCK_LIME_BED, "minecraft:lime_bed");
    init_bed_props(serv, BLOCK_PINK_BED, "minecraft:pink_bed");
    init_bed_props(serv, BLOCK_GRAY_BED, "minecraft:gray_bed");
    init_bed_props(serv, BLOCK_LIGHT_GRAY_BED, "minecraft:light_gray_bed");
    init_bed_props(serv, BLOCK_CYAN_BED, "minecraft:cyan_bed");
    init_bed_props(serv, BLOCK_PURPLE_BED, "minecraft:purple_bed");
    init_bed_props(serv, BLOCK_BLUE_BED, "minecraft:blue_bed");
    init_bed_props(serv, BLOCK_BROWN_BED, "minecraft:brown_bed");
    init_bed_props(serv, BLOCK_GREEN_BED, "minecraft:green_bed");
    init_bed_props(serv, BLOCK_RED_BED, "minecraft:red_bed");
    init_bed_props(serv, BLOCK_BLACK_BED, "minecraft:black_bed");

    register_block_type(serv, BLOCK_POWERED_RAIL, "minecraft:powered_rail");
    props = serv->block_properties_table + BLOCK_POWERED_RAIL;
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    add_block_prop(serv, props, "shape", "north_south", 6, "north_south", "east_west", "ascending_east", "ascending_west", "ascending_north", "ascending_south");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_DETECTOR_RAIL, "minecraft:detector_rail");
    props = serv->block_properties_table + BLOCK_DETECTOR_RAIL;
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    add_block_prop(serv, props, "shape", "north_south", 6, "north_south", "east_west", "ascending_east", "ascending_west", "ascending_north", "ascending_south");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_STICKY_PISTON, "minecraft:sticky_piston");
    props = serv->block_properties_table + BLOCK_STICKY_PISTON;
    add_block_prop(serv, props, "extended", "false", 2, "true", "false");
    add_block_prop(serv, props, "facing", "north", 6, "north", "east", "south", "west", "up", "down");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_COBWEB, "minecraft:cobweb");
    init_no_props(serv, BLOCK_GRASS, "minecraft:grass");
    init_no_props(serv, BLOCK_FERN, "minecraft:fern");
    init_no_props(serv, BLOCK_DEAD_BUSH, "minecraft:dead_bush");
    init_no_props(serv, BLOCK_SEAGRASS, "minecraft:seagrass");

    init_tall_plant_props(serv, BLOCK_TALL_SEAGRASS, "minecraft:tall_seagrass");

    register_block_type(serv, BLOCK_PISTON, "minecraft:piston");
    props = serv->block_properties_table + BLOCK_PISTON;
    add_block_prop(serv, props, "extended", "false", 2, "true", "false");
    add_block_prop(serv, props, "facing", "north", 6, "north", "east", "south", "west", "up", "down");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_PISTON_HEAD, "minecraft:piston_head");
    props = serv->block_properties_table + BLOCK_PISTON_HEAD;
    add_block_prop(serv, props, "facing", "north", 6, "north", "east", "south", "west", "up", "down");
    add_block_prop(serv, props, "short", "false", 2, "true", "false");
    add_block_prop(serv, props, "type", "normal", 2, "normal", "sticky");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_WHITE_WOOL, "minecraft:white_wool");
    init_no_props(serv, BLOCK_ORANGE_WOOL, "minecraft:orange_wool");
    init_no_props(serv, BLOCK_MAGENTA_WOOL, "minecraft:magenta_wool");
    init_no_props(serv, BLOCK_LIGHT_BLUE_WOOL, "minecraft:light_blue_wool");
    init_no_props(serv, BLOCK_YELLOW_WOOL, "minecraft:yellow_wool");
    init_no_props(serv, BLOCK_LIME_WOOL, "minecraft:lime_wool");
    init_no_props(serv, BLOCK_PINK_WOOL, "minecraft:pink_wool");
    init_no_props(serv, BLOCK_GRAY_WOOL, "minecraft:gray_wool");
    init_no_props(serv, BLOCK_LIGHT_GRAY_WOOL, "minecraft:light_gray_wool");
    init_no_props(serv, BLOCK_CYAN_WOOL, "minecraft:cyan_wool");
    init_no_props(serv, BLOCK_PURPLE_WOOL, "minecraft:purple_wool");
    init_no_props(serv, BLOCK_BLUE_WOOL, "minecraft:blue_wool");
    init_no_props(serv, BLOCK_BROWN_WOOL, "minecraft:brown_wool");
    init_no_props(serv, BLOCK_GREEN_WOOL, "minecraft:green_wool");
    init_no_props(serv, BLOCK_RED_WOOL, "minecraft:red_wool");
    init_no_props(serv, BLOCK_BLACK_WOOL, "minecraft:black_wool");

    register_block_type(serv, BLOCK_MOVING_PISTON, "minecraft:moving_piston");
    props = serv->block_properties_table + BLOCK_MOVING_PISTON;
    add_block_prop(serv, props, "facing", "north", 6, "north", "east", "south", "west", "up", "down");
    add_block_prop(serv, props, "type", "normal", 2, "normal", "sticky");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_DANDELION, "minecraft:dandelion");
    init_no_props(serv, BLOCK_POPPY, "minecraft:poppy");
    init_no_props(serv, BLOCK_BLUE_ORCHID, "minecraft:blue_orchid");
    init_no_props(serv, BLOCK_ALLIUM, "minecraft:allium");
    init_no_props(serv, BLOCK_AZURE_BLUET, "minecraft:azure_bluet");
    init_no_props(serv, BLOCK_RED_TULIP, "minecraft:red_tulip");
    init_no_props(serv, BLOCK_ORANGE_TULIP, "minecraft:orange_tulip");
    init_no_props(serv, BLOCK_WHITE_TULIP, "minecraft:white_tulip");
    init_no_props(serv, BLOCK_PINK_TULIP, "minecraft:pink_tulip");
    init_no_props(serv, BLOCK_OXEYE_DAISY, "minecraft:oxeye_daisy");
    init_no_props(serv, BLOCK_CORNFLOWER, "minecraft:cornflower");
    init_no_props(serv, BLOCK_WITHER_ROSE, "minecraft:wither_rose");
    init_no_props(serv, BLOCK_LILY_OF_THE_VALLEY, "minecraft:lily_of_the_valley");
    init_no_props(serv, BLOCK_BROWN_MUSHROOM, "minecraft:brown_mushroom");
    init_no_props(serv, BLOCK_RED_MUSHROOM, "minecraft:red_mushroom");
    init_no_props(serv, BLOCK_GOLD_BLOCK, "minecraft:gold_block");
    init_no_props(serv, BLOCK_IRON_BLOCK, "minecraft:iron_block");
    init_no_props(serv, BLOCK_BRICKS, "minecraft:bricks");

    register_block_type(serv, BLOCK_TNT, "minecraft:tnt");
    props = serv->block_properties_table + BLOCK_TNT;
    add_block_prop(serv, props, "unstable", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_BOOKSHELF, "minecraft:bookshelf");
    init_no_props(serv, BLOCK_MOSSY_COBBLESTONE, "minecraft:mossy_cobblestone");
    init_no_props(serv, BLOCK_OBSIDIAN, "minecraft:obsidian");
    init_no_props(serv, BLOCK_TORCH, "minecraft:torch");

    register_block_type(serv, BLOCK_WALL_TORCH, "minecraft:wall_torch");
    props = serv->block_properties_table + BLOCK_WALL_TORCH;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_FIRE, "minecraft:fire");
    props = serv->block_properties_table + BLOCK_FIRE;
    add_block_range_prop(serv, props, "age", 0, 0, 15);
    add_block_prop(serv, props, "east", "false", 2, "true", "false");
    add_block_prop(serv, props, "north", "false", 2, "true", "false");
    add_block_prop(serv, props, "south", "false", 2, "true", "false");
    add_block_prop(serv, props, "up", "false", 2, "true", "false");
    add_block_prop(serv, props, "west", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_SOUL_FIRE, "minecraft:soul_fire");
    init_no_props(serv, BLOCK_SPAWNER, "minecraft:spawner");

    init_stair_props(serv, BLOCK_OAK_STAIRS, "minecraft:oak_stairs");

    register_block_type(serv, BLOCK_CHEST, "minecraft:chest");
    props = serv->block_properties_table + BLOCK_CHEST;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "type", "single", 3, "single", "left", "right");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_REDSTONE_WIRE, "minecraft:redstone_wire");
    props = serv->block_properties_table + BLOCK_REDSTONE_WIRE;
    add_block_prop(serv, props, "east", "none", 3, "up", "side", "none");
    add_block_prop(serv, props, "north", "none", 3, "up", "side", "none");
    add_block_range_prop(serv, props, "power", 0, 0, 15);
    add_block_prop(serv, props, "south", "none", 3, "up", "side", "none");
    add_block_prop(serv, props, "west", "none", 3, "up", "side", "none");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_DIAMOND_ORE, "minecraft:diamond_ore");
    init_no_props(serv, BLOCK_DIAMOND_BLOCK, "minecraft:diamond_block");
    init_no_props(serv, BLOCK_CRAFTING_TABLE, "minecraft:crafting_table");

    register_block_type(serv, BLOCK_WHEAT, "minecraft:wheat");
    props = serv->block_properties_table + BLOCK_WHEAT;
    add_block_range_prop(serv, props, "age", 0, 0, 7);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_FARMLAND, "minecraft:farmland");
    props = serv->block_properties_table + BLOCK_FARMLAND;
    add_block_range_prop(serv, props, "moisture", 0, 0, 7);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_FURNACE, "minecraft:furnace");
    props = serv->block_properties_table + BLOCK_FURNACE;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "lit", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_sign_props(serv, BLOCK_OAK_SIGN, "minecraft:oak_sign");
    init_sign_props(serv, BLOCK_SPRUCE_SIGN, "minecraft:spruce_sign");
    init_sign_props(serv, BLOCK_BIRCH_SIGN, "minecraft:birch_sign");
    init_sign_props(serv, BLOCK_ACACIA_SIGN, "minecraft:acacia_sign");
    init_sign_props(serv, BLOCK_JUNGLE_SIGN, "minecraft:jungle_sign");
    init_sign_props(serv, BLOCK_DARK_OAK_SIGN, "minecraft:dark_oak_sign");

    init_door_props(serv, BLOCK_OAK_DOOR, "minecraft:oak_door");

    register_block_type(serv, BLOCK_LADDER, "minecraft:ladder");
    props = serv->block_properties_table + BLOCK_LADDER;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_RAIL, "minecraft:rail");
    props = serv->block_properties_table + BLOCK_RAIL;
    add_block_prop(serv, props, "shape", "north_south", 10, "north_south", "east_west", "ascending_east", "ascending_west", "ascending_north", "ascending_south", "south_east", "south_west", "north_west", "north_east");
    finalise_block_props(serv, props);

    init_stair_props(serv, BLOCK_COBBLESTONE_STAIRS, "minecraft:cobblestone_stairs");

    init_wall_sign_props(serv, BLOCK_OAK_WALL_SIGN, "minecraft:oak_wall_sign");
    init_wall_sign_props(serv, BLOCK_SPRUCE_WALL_SIGN, "minecraft:spruce_wall_sign");
    init_wall_sign_props(serv, BLOCK_BIRCH_WALL_SIGN, "minecraft:birch_wall_sign");
    init_wall_sign_props(serv, BLOCK_ACACIA_WALL_SIGN, "minecraft:acacia_wall_sign");
    init_wall_sign_props(serv, BLOCK_JUNGLE_WALL_SIGN, "minecraft:jungle_wall_sign");
    init_wall_sign_props(serv, BLOCK_DARK_OAK_WALL_SIGN, "minecraft:dark_oak_wall_sign");

    register_block_type(serv, BLOCK_LEVER, "minecraft:lever");
    props = serv->block_properties_table + BLOCK_LEVER;
    add_block_prop(serv, props, "face", "wall", 3, "floor", "wall", "ceiling");
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_pressure_plate_props(serv, BLOCK_STONE_PRESSURE_PLATE, "minecraft:stone_pressure_plate");

    init_door_props(serv, BLOCK_IRON_DOOR, "minecraft:iron_door");

    init_pressure_plate_props(serv, BLOCK_OAK_PRESSURE_PLATE, "minecraft:oak_pressure_plate");
    init_pressure_plate_props(serv, BLOCK_SPRUCE_PRESSURE_PLATE, "minecraft:spruce_pressure_plate");
    init_pressure_plate_props(serv, BLOCK_BIRCH_PRESSURE_PLATE, "minecraft:birch_pressure_plate");
    init_pressure_plate_props(serv, BLOCK_JUNGLE_PRESSURE_PLATE, "minecraft:jungle_pressure_plate");
    init_pressure_plate_props(serv, BLOCK_ACACIA_PRESSURE_PLATE, "minecraft:acacia_pressure_plate");
    init_pressure_plate_props(serv, BLOCK_DARK_OAK_PRESSURE_PLATE, "minecraft:dark_oak_pressure_plate");

    register_block_type(serv, BLOCK_REDSTONE_ORE, "minecraft:redstone_ore");
    props = serv->block_properties_table + BLOCK_REDSTONE_ORE;
    add_block_prop(serv, props, "lit", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_REDSTONE_TORCH, "minecraft:redstone_torch");
    props = serv->block_properties_table + BLOCK_REDSTONE_TORCH;
    add_block_prop(serv, props, "lit", "true", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_REDSTONE_WALL_TORCH, "minecraft:redstone_wall_torch");
    props = serv->block_properties_table + BLOCK_REDSTONE_WALL_TORCH;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "lit", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_button_props(serv, BLOCK_STONE_BUTTON, "minecraft:stone_button");

    register_block_type(serv, BLOCK_SNOW, "minecraft:snow");
    props = serv->block_properties_table + BLOCK_SNOW;
    add_block_range_prop(serv, props, "layers", 1, 1, 8);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_ICE, "minecraft:ice");
    init_no_props(serv, BLOCK_SNOW_BLOCK, "minecraft:snow_block");

    register_block_type(serv, BLOCK_CACTUS, "minecraft:cactus");
    props = serv->block_properties_table + BLOCK_CACTUS;
    add_block_range_prop(serv, props, "age", 0, 0, 15);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_CLAY, "minecraft:clay");

    register_block_type(serv, BLOCK_SUGAR_CANE, "minecraft:sugar_cane");
    props = serv->block_properties_table + BLOCK_SUGAR_CANE;
    add_block_range_prop(serv, props, "age", 0, 0, 15);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_JUKEBOX, "minecraft:jukebox");
    props = serv->block_properties_table + BLOCK_JUKEBOX;
    add_block_prop(serv, props, "has_record", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_cross_props(serv, BLOCK_OAK_FENCE, "minecraft:oak_fence");

    init_no_props(serv, BLOCK_PUMPKIN, "minecraft:pumpkin");
    init_no_props(serv, BLOCK_NETHERRACK, "minecraft:netherrack");
    init_no_props(serv, BLOCK_SOUL_SAND, "minecraft:soul_sand");
    init_no_props(serv, BLOCK_SOUL_SOIL, "minecraft:soul_soil");

    init_pillar_props(serv, BLOCK_BASALT, "minecraft:basalt");
    init_pillar_props(serv, BLOCK_POLISHED_BASALT, "minecraft:polished_basalt");

    init_no_props(serv, BLOCK_SOUL_TORCH, "minecraft:soul_torch");

    register_block_type(serv, BLOCK_SOUL_WALL_TORCH, "minecraft:soul_wall_torch");
    props = serv->block_properties_table + BLOCK_SOUL_WALL_TORCH;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_GLOWSTONE, "minecraft:glowsonte");

    register_block_type(serv, BLOCK_NETHER_PORTAL, "minecraft:nether_portal");
    props = serv->block_properties_table + BLOCK_NETHER_PORTAL;
    add_block_prop(serv, props, "axis", "x", 2, "x", "z");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_CARVED_PUMPKIN, "minecraft:carved_pumpkin");
    props = serv->block_properties_table + BLOCK_CARVED_PUMPKIN;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_JACK_O_LANTERN, "minecraft:jack_o_lantern");
    props = serv->block_properties_table + BLOCK_JACK_O_LANTERN;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_CAKE, "minecraft:cake");
    props = serv->block_properties_table + BLOCK_CAKE;
    add_block_range_prop(serv, props, "facing", 0, 0, 6);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_REPEATER, "minecraft:repeater");
    props = serv->block_properties_table + BLOCK_REPEATER;
    add_block_range_prop(serv, props, "delay", 1, 1, 4);
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "locked", "false", 2, "true", "false");
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_WHITE_STAINED_GLASS, "minecraft:white_stained_glass");
    init_no_props(serv, BLOCK_ORANGE_STAINED_GLASS, "minecraft:orange_stained_glass");
    init_no_props(serv, BLOCK_MAGENTA_STAINED_GLASS, "minecraft:magenta_stained_glass");
    init_no_props(serv, BLOCK_LIGHT_BLUE_STAINED_GLASS, "minecraft:light_blue_stained_glass");
    init_no_props(serv, BLOCK_YELLOW_STAINED_GLASS, "minecraft:yellow_stained_glass");
    init_no_props(serv, BLOCK_LIME_STAINED_GLASS, "minecraft:lime_stained_glass");
    init_no_props(serv, BLOCK_PINK_STAINED_GLASS, "minecraft:pink_stained_glass");
    init_no_props(serv, BLOCK_GRAY_STAINED_GLASS, "minecraft:gray_stained_glass");
    init_no_props(serv, BLOCK_LIGHT_GRAY_STAINED_GLASS, "minecraft:light_gray_stained_glass");
    init_no_props(serv, BLOCK_CYAN_STAINED_GLASS, "minecraft:cyan_stained_glass");
    init_no_props(serv, BLOCK_PURPLE_STAINED_GLASS, "minecraft:purple_stained_glass");
    init_no_props(serv, BLOCK_BLUE_STAINED_GLASS, "minecraft:blue_stained_glass");
    init_no_props(serv, BLOCK_BROWN_STAINED_GLASS, "minecraft:brown_stained_glass");
    init_no_props(serv, BLOCK_GREEN_STAINED_GLASS, "minecraft:green_stained_glass");
    init_no_props(serv, BLOCK_RED_STAINED_GLASS, "minecraft:red_stained_glass");
    init_no_props(serv, BLOCK_BLACK_STAINED_GLASS, "minecraft:black_stained_glass");

    init_trapdoor_props(serv, BLOCK_OAK_TRAPDOOR, "minecraft:oak_trapdoor");
    init_trapdoor_props(serv, BLOCK_SPRUCE_TRAPDOOR, "minecraft:spruce_trapdoor");
    init_trapdoor_props(serv, BLOCK_BIRCH_TRAPDOOR, "minecraft:birch_trapdoor");
    init_trapdoor_props(serv, BLOCK_JUNGLE_TRAPDOOR, "minecraft:jungle_trapdoor");
    init_trapdoor_props(serv, BLOCK_ACACIA_TRAPDOOR, "minecraft:acacia_trapdoor");
    init_trapdoor_props(serv, BLOCK_DARK_OAK_TRAPDOOR, "minecraft:dark_oak_trapdoor");

    init_no_props(serv, BLOCK_STONE_BRICKS, "minecraft:stone_bricks");
    init_no_props(serv, BLOCK_MOSSY_STONE_BRICKS, "minecraft:mossy_stone_bricks");
    init_no_props(serv, BLOCK_CRACKED_STONE_BRICKS, "minecraft:cracked_stone_bricks");
    init_no_props(serv, BLOCK_CHISELED_STONE_BRICKS, "minecraft:chiseled_stone_bricks");
    init_no_props(serv, BLOCK_INFESTED_STONE, "minecraft:infested_stone");
    init_no_props(serv, BLOCK_INFESTED_COBBLESTONE, "minecraft:infested_cobblestone");
    init_no_props(serv, BLOCK_INFESTED_STONE_BRICKS, "minecraft:infested_stone_bricks");
    init_no_props(serv, BLOCK_INFESTED_MOSSY_STONE_BRICKS, "minecraft:infested_mossy_stone_bricks");
    init_no_props(serv, BLOCK_INFESTED_CRACKED_STONE_BRICKS, "minecraft:infested_cracked_stone_bricks");
    init_no_props(serv, BLOCK_INFESTED_CHISELED_STONE_BRICKS, "minecraft:infested_chiseled_stone_bricks");

    init_mushroom_block_props(serv, BLOCK_BROWN_MUSHROOM_BLOCK, "minecraft:brown_mushroom_block");
    init_mushroom_block_props(serv, BLOCK_RED_MUSHROOM_BLOCK, "minecraft:red_mushroom_block");
    init_mushroom_block_props(serv, BLOCK_MUSHROOM_STEM, "minecraft:mushroom_stem");

    init_cross_props(serv, BLOCK_IRON_BARS, "minecraft:iron_bars");

    register_block_type(serv, BLOCK_CHAIN, "minecraft:chain");
    props = serv->block_properties_table + BLOCK_CHAIN;
    add_block_prop(serv, props, "axis", "y", 3, "x", "y", "z");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_cross_props(serv, BLOCK_GLASS_PANE, "minecraft:glass_pane");

    init_no_props(serv, BLOCK_MELON, "minecraft:melon");

    register_block_type(serv, BLOCK_ATTACHED_PUMPKIN_STEM, "minecraft:attached_pumpkin_stem");
    props = serv->block_properties_table + BLOCK_ATTACHED_PUMPKIN_STEM;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_ATTACHED_MELON_STEM, "minecraft:attached_melon_stem");
    props = serv->block_properties_table + BLOCK_ATTACHED_MELON_STEM;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_PUMPKIN_STEM, "minecraft:pumpkin_stem");
    props = serv->block_properties_table + BLOCK_PUMPKIN_STEM;
    add_block_range_prop(serv, props, "age", 0, 0, 7);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_MELON_STEM, "minecraft:melon_stem");
    props = serv->block_properties_table + BLOCK_MELON_STEM;
    add_block_range_prop(serv, props, "age", 0, 0, 7);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_VINE, "minecraft:vine");
    props = serv->block_properties_table + BLOCK_VINE;
    add_block_prop(serv, props, "east", "false", 2, "true", "false");
    add_block_prop(serv, props, "north", "false", 2, "true", "false");
    add_block_prop(serv, props, "south", "false", 2, "true", "false");
    add_block_prop(serv, props, "up", "false", 2, "true", "false");
    add_block_prop(serv, props, "west", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_fence_gate_props(serv, BLOCK_OAK_FENCE_GATE, "minecraft:oak_fence_gate");

    init_stair_props(serv, BLOCK_BRICK_STAIRS, "minecraft:brick_stairs");
    init_stair_props(serv, BLOCK_STONE_BRICK_STAIRS, "minecraft:stone_brick_stairs");

    register_block_type(serv, BLOCK_MYCELIUM, "minecraft:mycelium");
    props = serv->block_properties_table + BLOCK_MYCELIUM;
    add_block_prop(serv, props, "snowy", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_LILY_PAD, "minecraft:lily_pad");
    init_no_props(serv, BLOCK_NETHER_BRICKS, "minecraft:nether_bricks");

    init_cross_props(serv, BLOCK_NETHER_BRICK_FENCE, "minecraft:nether_brick_fence");

    init_stair_props(serv, BLOCK_NETHER_BRICK_STAIRS, "minecraft:nether_brick_stairs");

    register_block_type(serv, BLOCK_NETHER_WART, "minecraft:nether_wart");
    props = serv->block_properties_table + BLOCK_NETHER_WART;
    add_block_range_prop(serv, props, "age", 0, 0, 3);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_ENCHANTING_TABLE, "minecraft:enchanting_table");

    register_block_type(serv, BLOCK_BREWING_STAND, "minecraft:brewing_stand");
    props = serv->block_properties_table + BLOCK_BREWING_STAND;
    add_block_prop(serv, props, "has_bottle_0", "false", 2, "true", "false");
    add_block_prop(serv, props, "has_bottle_1", "false", 2, "true", "false");
    add_block_prop(serv, props, "has_bottle_2", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_CAULDRON, "minecraft:cauldron");
    props = serv->block_properties_table + BLOCK_CAULDRON;
    add_block_range_prop(serv, props, "level", 0, 0, 3);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_END_PORTAL, "minecraft:end_portal");

    register_block_type(serv, BLOCK_END_PORTAL_FRAME, "minecraft:end_portal_frame");
    props = serv->block_properties_table + BLOCK_END_PORTAL_FRAME;
    add_block_prop(serv, props, "eye", "false", 2, "true", "false");
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_END_STONE, "minecraft:end_stone");
    init_no_props(serv, BLOCK_DRAGON_EGG, "minecraft:dragon_egg");

    register_block_type(serv, BLOCK_REDSTONE_LAMP, "minecraft:redstone_lamp");
    props = serv->block_properties_table + BLOCK_REDSTONE_LAMP;
    add_block_prop(serv, props, "lit", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_COCOA, "minecraft:cocoa");
    props = serv->block_properties_table + BLOCK_COCOA;
    add_block_range_prop(serv, props, "age", 0, 0, 2);
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);

    init_stair_props(serv, BLOCK_SANDSTONE_STAIRS, "minecraft:sandstone_stairs");

    init_no_props(serv, BLOCK_EMERALD_ORE, "minecraft:emerald_ore");

    register_block_type(serv, BLOCK_ENDER_CHEST, "minecraft:ender_chest");
    props = serv->block_properties_table + BLOCK_ENDER_CHEST;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_TRIPWIRE_HOOK, "minecraft:tripwire_hook");
    props = serv->block_properties_table + BLOCK_TRIPWIRE_HOOK;
    add_block_prop(serv, props, "attached", "false", 2, "true", "false");
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_TRIPWIRE, "minecraft:tripwire");
    props = serv->block_properties_table + BLOCK_TRIPWIRE;
    add_block_prop(serv, props, "attached", "false", 2, "true", "false");
    add_block_prop(serv, props, "disarmed", "false", 2, "true", "false");
    add_block_prop(serv, props, "east", "false", 2, "true", "false");
    add_block_prop(serv, props, "north", "false", 2, "true", "false");
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    add_block_prop(serv, props, "south", "false", 2, "true", "false");
    add_block_prop(serv, props, "west", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_EMERALD_BLOCK, "minecraft:emerald_block");

    init_stair_props(serv, BLOCK_SPRUCE_STAIRS, "minecraft:spruce_stairs");
    init_stair_props(serv, BLOCK_BIRCH_STAIRS, "minecraft:birch_stairs");
    init_stair_props(serv, BLOCK_JUNGLE_STAIRS, "minecraft:jungle_stairs");

    register_block_type(serv, BLOCK_COMMAND_BLOCK, "minecraft:command_block");
    props = serv->block_properties_table + BLOCK_COMMAND_BLOCK;
    add_block_prop(serv, props, "conditional", "false", 2, "true", "false");
    add_block_prop(serv, props, "facing", "north", 6, "north", "east", "south", "west", "up", "down");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_BEACON, "minecraft:beacon");

    init_wall_props(serv, BLOCK_COBBLESTONE_WALL, "minecraft:cobblestone_wall");
    init_wall_props(serv, BLOCK_MOSSY_COBBLESTONE_WALL, "minecraft:mossy_cobblestone_wall");

    init_no_props(serv, BLOCK_FLOWER_POT, "minecraft:flower_pot");
    init_no_props(serv, BLOCK_POTTED_OAK_SAPLING, "minecraft:potted_oak_sapling");
    init_no_props(serv, BLOCK_POTTED_SPRUCE_SAPLING, "minecraft:potted_spruce_sapling");
    init_no_props(serv, BLOCK_POTTED_BIRCH_SAPLING, "minecraft:potted_birch_sapling");
    init_no_props(serv, BLOCK_POTTED_JUNGLE_SAPLING, "minecraft:potted_jungle_sapling");
    init_no_props(serv, BLOCK_POTTED_ACACIA_SAPLING, "minecraft:potted_acacia_sapling");
    init_no_props(serv, BLOCK_POTTED_DARK_OAK_SAPLING, "minecraft:potted_dark_oak_sapling");
    init_no_props(serv, BLOCK_POTTED_FERN, "minecraft:potted_fern");
    init_no_props(serv, BLOCK_POTTED_DANDELION, "minecraft:potted_dandelion");
    init_no_props(serv, BLOCK_POTTED_POPPY, "minecraft:potted_poppy");
    init_no_props(serv, BLOCK_POTTED_BLUE_ORCHID, "minecraft:potted_blue_orchid");
    init_no_props(serv, BLOCK_POTTED_ALLIUM, "minecraft:potted_allium");
    init_no_props(serv, BLOCK_POTTED_AZURE_BLUET, "minecraft:potted_azure_bluet");
    init_no_props(serv, BLOCK_POTTED_RED_TULIP, "minecraft:potted_red_tulip");
    init_no_props(serv, BLOCK_POTTED_ORANGE_TULIP, "minecraft:potted_orange_tulip");
    init_no_props(serv, BLOCK_POTTED_WHITE_TULIP, "minecraft:potted_white_tulip");
    init_no_props(serv, BLOCK_POTTED_PINK_TULIP, "minecraft:potted_pink_tulip");
    init_no_props(serv, BLOCK_POTTED_OXEYE_DAISY, "minecraft:potted_oxeye_daisy");
    init_no_props(serv, BLOCK_POTTED_CORNFLOWER, "minecraft:potted_cornflower");
    init_no_props(serv, BLOCK_POTTED_LILY_OF_THE_VALLEY, "minecraft:potted_lily_of_the_valley");
    init_no_props(serv, BLOCK_POTTED_WITHER_ROSE, "minecraft:potted_wither_rose");
    init_no_props(serv, BLOCK_POTTED_RED_MUSHROOM, "minecraft:potted_red_mushroom");
    init_no_props(serv, BLOCK_POTTED_BROWN_MUSHROOM, "minecraft:potted_brown_mushroom");
    init_no_props(serv, BLOCK_POTTED_DEAD_BUSH, "minecraft:potted_dead_bush");
    init_no_props(serv, BLOCK_POTTED_CACTUS, "minecraft:potted_cactus");

    register_block_type(serv, BLOCK_CARROTS, "minecraft:carrots");
    props = serv->block_properties_table + BLOCK_CARROTS;
    add_block_range_prop(serv, props, "age", 0, 0, 7);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_POTATOES, "minecraft:potatoes");
    props = serv->block_properties_table + BLOCK_POTATOES;
    add_block_range_prop(serv, props, "age", 0, 0, 7);
    finalise_block_props(serv, props);

    init_button_props(serv, BLOCK_OAK_BUTTON, "minecraft:oak_button");
    init_button_props(serv, BLOCK_SPRUCE_BUTTON, "minecraft:spruce_button");
    init_button_props(serv, BLOCK_BIRCH_BUTTON, "minecraft:birch_button");
    init_button_props(serv, BLOCK_JUNGLE_BUTTON, "minecraft:jungle_button");
    init_button_props(serv, BLOCK_ACACIA_BUTTON, "minecraft:acacia_button");
    init_button_props(serv, BLOCK_DARK_OAK_BUTTON, "minecraft:dark_oak_button");

    init_skull_props(serv, BLOCK_SKELETON_SKULL, "minecraft:skeleton_skull");
    init_wall_skull_props(serv, BLOCK_SKELETON_WALL_SKULL, "minecraft:skeleton_wall_skull");
    init_skull_props(serv, BLOCK_WITHER_SKELETON_SKULL, "minecraft:wither_skeleton_skull");
    init_wall_skull_props(serv, BLOCK_WITHER_SKELETON_WALL_SKULL, "minecraft:wither_skeleton_wall_skull");
    init_skull_props(serv, BLOCK_ZOMBIE_HEAD, "minecraft:zombie_head");
    init_wall_skull_props(serv, BLOCK_ZOMBIE_WALL_HEAD, "minecraft:zombie_wall_head");
    init_skull_props(serv, BLOCK_PLAYER_HEAD, "minecraft:player_head");
    init_wall_skull_props(serv, BLOCK_PLAYER_WALL_HEAD, "minecraft:player_wall_head");
    init_skull_props(serv, BLOCK_CREEPER_HEAD, "minecraft:creeper_head");
    init_wall_skull_props(serv, BLOCK_CREEPER_WALL_HEAD, "minecraft:creeper_wall_head");
    init_skull_props(serv, BLOCK_DRAGON_HEAD, "minecraft:dragon_head");
    init_wall_skull_props(serv, BLOCK_DRAGON_WALL_HEAD, "minecraft:dragon_wall_head");

    init_anvil_props(serv, BLOCK_ANVIL, "minecraft:anvil");
    init_anvil_props(serv, BLOCK_CHIPPED_ANVIL, "minecraft:chipped_anvil");
    init_anvil_props(serv, BLOCK_DAMAGED_ANVIL, "minecraft:damaged_anvil");

    register_block_type(serv, BLOCK_TRAPPED_CHEST, "minecraft:trapped_chest");
    props = serv->block_properties_table + BLOCK_TRAPPED_CHEST;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "type", "single", 3, "single", "left", "right");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_LIGHT_WEIGHTED_PRESSURE_PLATE, "minecraft:light_weighted_pressure_plate");
    props = serv->block_properties_table + BLOCK_LIGHT_WEIGHTED_PRESSURE_PLATE;
    add_block_range_prop(serv, props, "power", 0, 0, 15);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_HEAVY_WEIGHTED_PRESSURE_PLATE, "minecraft:heavy_weighted_pressure_plate");
    props = serv->block_properties_table + BLOCK_HEAVY_WEIGHTED_PRESSURE_PLATE;
    add_block_range_prop(serv, props, "power", 0, 0, 15);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_COMPARATOR, "minecraft:comparator");
    props = serv->block_properties_table + BLOCK_COMPARATOR;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "mode", "compare", 2, "compare", "subtract");
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_DAYLIGHT_DETECTOR, "minecraft:daylight_detector");
    props = serv->block_properties_table + BLOCK_DAYLIGHT_DETECTOR;
    add_block_prop(serv, props, "inverted", "false", 2, "true", "false");
    add_block_range_prop(serv, props, "power", 0, 0, 15);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_REDSTONE_BLOCK, "minecraft:redstone_block");
    init_no_props(serv, BLOCK_NETHER_QUARTZ_ORE, "minecraft:nether_quartz_ore");

    register_block_type(serv, BLOCK_HOPPER, "minecraft:hopper");
    props = serv->block_properties_table + BLOCK_HOPPER;
    add_block_prop(serv, props, "enabled", "true", 2, "true", "false");
    add_block_prop(serv, props, "facing", "down", 5, "down", "north", "south", "west", "east");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_QUARTZ_BLOCK, "minecraft:quartz_ore");
    init_no_props(serv, BLOCK_CHISELED_QUARTZ_BLOCK, "minecraft:chiseled_quartz_block");

    init_pillar_props(serv, BLOCK_QUARTZ_PILLAR, "minecraft:quartz_piller");

    init_stair_props(serv, BLOCK_QUARTZ_STAIRS, "minecraft:quartz_stairs");

    register_block_type(serv, BLOCK_ACTIVATOR_RAIL, "minecraft:activator_rail");
    props = serv->block_properties_table + BLOCK_ACTIVATOR_RAIL;
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    add_block_prop(serv, props, "shape", "north_south", 6, "north_south", "east_west", "ascending_east", "ascending_west", "ascending_north", "ascending_south");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_DROPPER, "minecraft:dropper");
    props = serv->block_properties_table + BLOCK_DROPPER;
    add_block_prop(serv, props, "facing", "north", 6, "north", "east", "south", "west", "up", "down");
    add_block_prop(serv, props, "triggered", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_WHITE_TERRACOTTA, "minecraft:white_terracotta");
    init_no_props(serv, BLOCK_ORANGE_TERRACOTTA, "minecraft:orange_terracotta");
    init_no_props(serv, BLOCK_MAGENTA_TERRACOTTA, "minecraft:magenta_terracotta");
    init_no_props(serv, BLOCK_LIGHT_BLUE_TERRACOTTA, "minecraft:light_blue_terracotta");
    init_no_props(serv, BLOCK_YELLOW_TERRACOTTA, "minecraft:yellow_terracotta");
    init_no_props(serv, BLOCK_LIME_TERRACOTTA, "minecraft:lime_terracotta");
    init_no_props(serv, BLOCK_PINK_TERRACOTTA, "minecraft:pink_terracotta");
    init_no_props(serv, BLOCK_GRAY_TERRACOTTA, "minecraft:gray_terrcotta");
    init_no_props(serv, BLOCK_LIGHT_GRAY_TERRACOTTA, "minecraft:light_gray_terracotta");
    init_no_props(serv, BLOCK_CYAN_TERRACOTTA, "minecraft:cyan_terracotta");
    init_no_props(serv, BLOCK_PURPLE_TERRACOTTA, "minecraft:purple_terracotta");
    init_no_props(serv, BLOCK_BLUE_TERRACOTTA, "minecraft:blue_terracotta");
    init_no_props(serv, BLOCK_BROWN_TERRACOTTA, "minecraft:brown_terracotta");
    init_no_props(serv, BLOCK_GREEN_TERRACOTTA, "minecraft:green_terracotta");
    init_no_props(serv, BLOCK_RED_TERRACOTTA, "minecraft:red_terracotta");
    init_no_props(serv, BLOCK_BLACK_TERRACOTTA, "minecraft:black_terracotta");

    init_cross_props(serv, BLOCK_WHITE_STAINED_GLASS_PANE, "minecraft:white_stained_glass_pane");
    init_cross_props(serv, BLOCK_ORANGE_STAINED_GLASS_PANE, "minecraft:orange_stained_glass_pane");
    init_cross_props(serv, BLOCK_MAGENTA_STAINED_GLASS_PANE, "minecraft:magenta_stained_glass_pane");
    init_cross_props(serv, BLOCK_LIGHT_BLUE_STAINED_GLASS_PANE, "minecraft:light_blue_stained_glass_pane");
    init_cross_props(serv, BLOCK_YELLOW_STAINED_GLASS_PANE, "minecraft:yellow_stained_glass_pane");
    init_cross_props(serv, BLOCK_LIME_STAINED_GLASS_PANE, "minecraft:lime_stained_glass_pane");
    init_cross_props(serv, BLOCK_PINK_STAINED_GLASS_PANE, "minecraft:pink_stained_glass_pane");
    init_cross_props(serv, BLOCK_GRAY_STAINED_GLASS_PANE, "minecraft:gray_stained_glass_pane");
    init_cross_props(serv, BLOCK_LIGHT_GRAY_STAINED_GLASS_PANE, "minecraft:light_gray_stained_glass_pane");
    init_cross_props(serv, BLOCK_CYAN_STAINED_GLASS_PANE, "minecraft:cyan_stained_glass_pane");
    init_cross_props(serv, BLOCK_PURPLE_STAINED_GLASS_PANE, "minecraft:purple_stained_glass_pane");
    init_cross_props(serv, BLOCK_BLUE_STAINED_GLASS_PANE, "minecraft:blue_stained_glass_pane");
    init_cross_props(serv, BLOCK_BROWN_STAINED_GLASS_PANE, "minecraft:brown_stained_glass_pane");
    init_cross_props(serv, BLOCK_GREEN_STAINED_GLASS_PANE, "minecraft:green_stained_glass_pane");
    init_cross_props(serv, BLOCK_RED_STAINED_GLASS_PANE, "minecraft:red_stained_glass_pane");
    init_cross_props(serv, BLOCK_BLACK_STAINED_GLASS_PANE, "minecraft:black_stained_glass_pane");

    init_stair_props(serv, BLOCK_ACACIA_STAIRS, "minecraft:acacia_stairs");
    init_stair_props(serv, BLOCK_DARK_OAK_STAIRS, "minecraft:dark_oak_stairs");

    init_no_props(serv, BLOCK_SLIME_BLOCK, "minecraft:slime_block");
    init_no_props(serv, BLOCK_BARRIER, "minecraft:barrier");

    init_trapdoor_props(serv, BLOCK_IRON_TRAPDOOR, "minecraft:iron_trapdoor");

    init_no_props(serv, BLOCK_PRISMARINE, "minecraft:prismarine");
    init_no_props(serv, BLOCK_PRISMARINE_BRICKS, "minecraft:prismarine_bricks");
    init_no_props(serv, BLOCK_DARK_PRISMARINE, "minecraft:dark_prismarine");

    init_stair_props(serv, BLOCK_PRISMARINE_STAIRS, "minecraft:prismarine_stairs");
    init_stair_props(serv, BLOCK_PRISMARINE_BRICK_STAIRS, "minecraft:prismarine_brick_stairs");
    init_stair_props(serv, BLOCK_DARK_PRISMARINE_STAIRS, "minecraft:dark_prismarine_stairs");

    init_slab_props(serv, BLOCK_PRISMARINE_SLAB, "minecraft:prismarine_slab");
    init_slab_props(serv, BLOCK_PRISMARINE_BRICK_SLAB, "minecraft:prismarine_brick_slab");
    init_slab_props(serv, BLOCK_DARK_PRISMARINE_SLAB, "minecraft:dark_prismarine_slab");

    init_no_props(serv, BLOCK_SEA_LANTERN, "minecraft:sea_lantern");

    init_pillar_props(serv, BLOCK_HAY_BLOCK, "minecraft:hay_block");

    init_no_props(serv, BLOCK_WHITE_CARPET, "minecraft:white_carpet");
    init_no_props(serv, BLOCK_ORANGE_CARPET, "minecraft:orange_carpet");
    init_no_props(serv, BLOCK_MAGENTA_CARPET, "minecraft:magenta_carpet");
    init_no_props(serv, BLOCK_LIGHT_BLUE_CARPET, "minecraft:light_blue_carpet");
    init_no_props(serv, BLOCK_YELLOW_CARPET, "minecraft:yellow_carpet");
    init_no_props(serv, BLOCK_LIME_CARPET, "minecraft:lime_carpet");
    init_no_props(serv, BLOCK_PINK_CARPET, "minecraft:pink_carpet");
    init_no_props(serv, BLOCK_GRAY_CARPET, "minecraft:gray_carpet");
    init_no_props(serv, BLOCK_LIGHT_GRAY_CARPET, "minecraft:light_gray_carpet");
    init_no_props(serv, BLOCK_CYAN_CARPET, "minecraft:cyan_carpet");
    init_no_props(serv, BLOCK_PURPLE_CARPET, "minecraft:purple_carpet");
    init_no_props(serv, BLOCK_BLUE_CARPET, "minecraft:blue_carpet");
    init_no_props(serv, BLOCK_BROWN_CARPET, "minecraft:brown_carpet");
    init_no_props(serv, BLOCK_GREEN_CARPET, "minecraft:green_carpet");
    init_no_props(serv, BLOCK_RED_CARPET, "minecraft:red_carpet");
    init_no_props(serv, BLOCK_BLACK_CARPET, "minecraft:black_carpet");
    init_no_props(serv, BLOCK_TERRACOTTA, "minecraft:terracotta");
    init_no_props(serv, BLOCK_COAL_BLOCK, "minecraft:coal_block");
    init_no_props(serv, BLOCK_PACKED_ICE, "minecraft:packed_ice");

    init_tall_plant_props(serv, BLOCK_SUNFLOWER, "minecraft:sunflower");
    init_tall_plant_props(serv, BLOCK_LILAC, "minecraft:lilac");
    init_tall_plant_props(serv, BLOCK_ROSE_BUSH, "minecraft:rose_bush");
    init_tall_plant_props(serv, BLOCK_PEONY, "minecraft:peony");
    init_tall_plant_props(serv, BLOCK_TALL_GRASS, "minecraft:tall_grass");
    init_tall_plant_props(serv, BLOCK_LARGE_FERN, "minecraft:large_fern");

    init_banner_props(serv, BLOCK_WHITE_BANNER, "minecraft:white_banner");
    init_banner_props(serv, BLOCK_ORANGE_BANNER, "minecraft:orange_banner");
    init_banner_props(serv, BLOCK_MAGENTA_BANNER, "minecraft:magenta_banner");
    init_banner_props(serv, BLOCK_LIGHT_BLUE_BANNER, "minecraft:light_blue_banner");
    init_banner_props(serv, BLOCK_YELLOW_BANNER, "minecraft:yellow_banner");
    init_banner_props(serv, BLOCK_LIME_BANNER, "minecraft:lime_banner");
    init_banner_props(serv, BLOCK_PINK_BANNER, "minecraft:pink_banner");
    init_banner_props(serv, BLOCK_GRAY_BANNER, "minecraft:gray_banner");
    init_banner_props(serv, BLOCK_LIGHT_GRAY_BANNER, "minecraft:light_gray_banner");
    init_banner_props(serv, BLOCK_CYAN_BANNER, "minecraft:cyan_banner");
    init_banner_props(serv, BLOCK_PURPLE_BANNER, "minecraft:purple_banner");
    init_banner_props(serv, BLOCK_BLUE_BANNER, "minecraft:blue_banner");
    init_banner_props(serv, BLOCK_BROWN_BANNER, "minecraft:brown_banner");
    init_banner_props(serv, BLOCK_GREEN_BANNER, "minecraft:green_banner");
    init_banner_props(serv, BLOCK_RED_BANNER, "minecraft:red_banner");
    init_banner_props(serv, BLOCK_BLACK_BANNER, "minecraft:black_banner");

    init_wall_banner_props(serv, BLOCK_WHITE_WALL_BANNER, "minecraft:white_wall_banner");
    init_wall_banner_props(serv, BLOCK_ORANGE_WALL_BANNER, "minecraft:orange_wall_banner");
    init_wall_banner_props(serv, BLOCK_MAGENTA_WALL_BANNER, "minecraft:magenta_wall_banner");
    init_wall_banner_props(serv, BLOCK_LIGHT_BLUE_WALL_BANNER, "minecraft:light_blue_wall_banner");
    init_wall_banner_props(serv, BLOCK_YELLOW_WALL_BANNER, "minecraft:yellow_wall_banner");
    init_wall_banner_props(serv, BLOCK_LIME_WALL_BANNER, "minecraft:lime_wall_banner");
    init_wall_banner_props(serv, BLOCK_PINK_WALL_BANNER, "minecraft:pink_wall_banner");
    init_wall_banner_props(serv, BLOCK_GRAY_WALL_BANNER, "minecraft:gray_wall_banner");
    init_wall_banner_props(serv, BLOCK_LIGHT_GRAY_WALL_BANNER, "minecraft:light_gray_wall_banner");
    init_wall_banner_props(serv, BLOCK_CYAN_WALL_BANNER, "minecraft:cyan_wall_banner");
    init_wall_banner_props(serv, BLOCK_PURPLE_WALL_BANNER, "minecraft:purple_wall_banner");
    init_wall_banner_props(serv, BLOCK_BLUE_WALL_BANNER, "minecraft:blue_wall_banner");
    init_wall_banner_props(serv, BLOCK_BROWN_WALL_BANNER, "minecraft:brown_wall_banner");
    init_wall_banner_props(serv, BLOCK_GREEN_WALL_BANNER, "minecraft:green_wall_banner");
    init_wall_banner_props(serv, BLOCK_RED_WALL_BANNER, "minecraft:red_wall_banner");
    init_wall_banner_props(serv, BLOCK_BLACK_WALL_BANNER, "minecraft:black_wall_banner");

    init_no_props(serv, BLOCK_RED_SANDSTONE, "minecraft:red_sandstone");
    init_no_props(serv, BLOCK_CHISELED_RED_SANDSTONE, "minecraft:chiseled_red_sandstone");
    init_no_props(serv, BLOCK_CUT_RED_SANDSTONE, "minecraft:cut_red_sandstone");

    init_stair_props(serv, BLOCK_RED_SANDSTONE_STAIRS, "minecraft:red_sandstone_stairs");

    init_slab_props(serv, BLOCK_OAK_SLAB, "minecraft:oak_slab");
    init_slab_props(serv, BLOCK_SPRUCE_SLAB, "minecraft:spruce_slab");
    init_slab_props(serv, BLOCK_BIRCH_SLAB, "minecraft:birch_slab");
    init_slab_props(serv, BLOCK_JUNGLE_SLAB, "minecraft:jungle_slab");
    init_slab_props(serv, BLOCK_ACACIA_SLAB, "minecraft:acacia_slab");
    init_slab_props(serv, BLOCK_DARK_OAK_SLAB, "minecraft:dark_oak_slab");
    init_slab_props(serv, BLOCK_STONE_SLAB, "minecraft:stone_slab");
    init_slab_props(serv, BLOCK_SMOOTH_STONE_SLAB, "minecraft:smooth_stone_slab");
    init_slab_props(serv, BLOCK_SANDSTONE_SLAB, "minecraft:sandstone_slab");
    init_slab_props(serv, BLOCK_CUT_SANDSTONE_SLAB, "minecraft:cut_sandstone_slab");
    init_slab_props(serv, BLOCK_PETRIFIED_OAK_SLAB, "minecraft:petrified_oak_slab");
    init_slab_props(serv, BLOCK_COBBLESTONE_SLAB, "minecraft:cobblestone_slab");
    init_slab_props(serv, BLOCK_BRICK_SLAB, "minecraft:brick_slab");
    init_slab_props(serv, BLOCK_STONE_BRICK_SLAB, "minecraft:stone_brick_slab");
    init_slab_props(serv, BLOCK_NETHER_BRICK_SLAB, "minecraft:nether_brick_slab");
    init_slab_props(serv, BLOCK_QUARTZ_SLAB, "minecraft:quartz_slab");
    init_slab_props(serv, BLOCK_RED_SANDSTONE_SLAB, "minecraft:red_sandstone_slab");
    init_slab_props(serv, BLOCK_CUT_RED_SANDSTONE_SLAB, "minecraft:cut_red_sandstone_slab");
    init_slab_props(serv, BLOCK_PURPUR_SLAB, "minecraft:purpur_slab");

    init_no_props(serv, BLOCK_SMOOTH_STONE, "minecraft:smooth_stone");
    init_no_props(serv, BLOCK_SMOOTH_SANDSTONE, "minecraft:smooth_sandstone");
    init_no_props(serv, BLOCK_SMOOTH_QUARTZ, "minecraft:smooth_quartz");
    init_no_props(serv, BLOCK_SMOOTH_RED_SANDSTONE, "minecraft:smooth_red_sandstone");

    init_fence_gate_props(serv, BLOCK_SPRUCE_FENCE_GATE, "minecraft:spruce_fence_gate");
    init_fence_gate_props(serv, BLOCK_BIRCH_FENCE_GATE, "minecraft:birch_fence_gate");
    init_fence_gate_props(serv, BLOCK_JUNGLE_FENCE_GATE, "minecraft:jungle_fence_gate");
    init_fence_gate_props(serv, BLOCK_ACACIA_FENCE_GATE, "minecraft:acacia_fence_gate");
    init_fence_gate_props(serv, BLOCK_DARK_OAK_FENCE_GATE, "minecraft:dark_oak_fence_gate");

    init_cross_props(serv, BLOCK_SPRUCE_FENCE, "minecraft:spruce_fence");
    init_cross_props(serv, BLOCK_BIRCH_FENCE, "minecraft:birch_fence");
    init_cross_props(serv, BLOCK_JUNGLE_FENCE, "minecraft:jungle_fence");
    init_cross_props(serv, BLOCK_ACACIA_FENCE, "minecraft:acacia_fence");
    init_cross_props(serv, BLOCK_DARK_OAK_FENCE, "minecraft:dark_oak_fence");

    init_door_props(serv, BLOCK_SPRUCE_DOOR, "minecraft:spruce_door");
    init_door_props(serv, BLOCK_BIRCH_DOOR, "minecraft:birch_door");
    init_door_props(serv, BLOCK_JUNGLE_DOOR, "minecraft:jungle_door");
    init_door_props(serv, BLOCK_ACACIA_DOOR, "minecraft:acacia_door");
    init_door_props(serv, BLOCK_DARK_OAK_DOOR, "minecraft:dark_oak_door");

    register_block_type(serv, BLOCK_END_ROD, "minecraft:end_rod");
    props = serv->block_properties_table + BLOCK_END_ROD;
    add_block_prop(serv, props, "facing", "up", 6, "north", "east", "south", "west", "up", "down");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_CHORUS_PLANT, "minecraft:chorus_plant");
    props = serv->block_properties_table + BLOCK_CHORUS_PLANT;
    add_block_prop(serv, props, "down", "false", 2, "true", "false");
    add_block_prop(serv, props, "east", "false", 2, "true", "false");
    add_block_prop(serv, props, "north", "false", 2, "true", "false");
    add_block_prop(serv, props, "south", "false", 2, "true", "false");
    add_block_prop(serv, props, "up", "false", 2, "true", "false");
    add_block_prop(serv, props, "west", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_CHORUS_FLOWER, "minecraft:chorus_flower");
    props = serv->block_properties_table + BLOCK_CHORUS_FLOWER;
    add_block_range_prop(serv, props, "age", 0, 0, 5);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_PURPUR_BLOCK, "minecraft:purpur_block");

    init_pillar_props(serv, BLOCK_PURPUR_PILLAR, "minecraft:purpur_pillar");

    init_stair_props(serv, BLOCK_PURPUR_STAIRS, "minecraft:purpur_stairs");

    init_no_props(serv, BLOCK_END_STONE_BRICKS, "minecraft:end_stone_bricks");

    register_block_type(serv, BLOCK_BEETROOTS, "minecraft:beetroots");
    props = serv->block_properties_table + BLOCK_BEETROOTS;
    add_block_range_prop(serv, props, "age", 0, 0, 3);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_GRASS_PATH, "minecraft:grass_path");
    init_no_props(serv, BLOCK_END_GATEWAY, "minecraft:end_gateway");

    register_block_type(serv, BLOCK_REPEATING_COMMAND_BLOCK, "minecraft:repeating_command_block");
    props = serv->block_properties_table + BLOCK_REPEATING_COMMAND_BLOCK;
    add_block_prop(serv, props, "conditional", "false", 2, "true", "false");
    add_block_prop(serv, props, "facing", "north", 6, "north", "east", "south", "west", "up", "down");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_CHAIN_COMMAND_BLOCK, "minecraft:chain_command_block");
    props = serv->block_properties_table + BLOCK_CHAIN_COMMAND_BLOCK;
    add_block_prop(serv, props, "conditional", "false", 2, "true", "false");
    add_block_prop(serv, props, "facing", "north", 6, "north", "east", "south", "west", "up", "down");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_FROSTED_ICE, "minecraft:frosted_ice");
    props = serv->block_properties_table + BLOCK_FROSTED_ICE;
    add_block_range_prop(serv, props, "age", 0, 0, 3);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_MAGMA_BLOCK, "minecraft:magma_block");
    init_no_props(serv, BLOCK_NETHER_WART_BLOCK, "minecraft:nether_wart_block");
    init_no_props(serv, BLOCK_RED_NETHER_BRICKS, "minecraft:red_nether_bricks");

    init_pillar_props(serv, BLOCK_BONE_BLOCK, "minecraft:bone_block");

    init_no_props(serv, BLOCK_STRUCTURE_VOID, "minecraft:structure_void");

    register_block_type(serv, BLOCK_OBSERVER, "minecraft:observer");
    props = serv->block_properties_table + BLOCK_OBSERVER;
    add_block_prop(serv, props, "facing", "south", 6, "north", "east", "south", "west", "up", "down");
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_shulker_box_props(serv, BLOCK_SHULKER_BOX, "minecraft:shulker_box");
    init_shulker_box_props(serv, BLOCK_WHITE_SHULKER_BOX, "minecraft:white_shulker_box");
    init_shulker_box_props(serv, BLOCK_ORANGE_SHULKER_BOX, "minecraft:orange_shulker_box");
    init_shulker_box_props(serv, BLOCK_MAGENTA_SHULKER_BOX, "minecraft:magenta_shulker_box");
    init_shulker_box_props(serv, BLOCK_LIGHT_BLUE_SHULKER_BOX, "minecraft:light_blue_shulker_box");
    init_shulker_box_props(serv, BLOCK_YELLOW_SHULKER_BOX, "minecraft:yellow_shulker_box");
    init_shulker_box_props(serv, BLOCK_LIME_SHULKER_BOX, "minecraft:lime_shulker_box");
    init_shulker_box_props(serv, BLOCK_PINK_SHULKER_BOX, "minecraft:pink_shulker_box");
    init_shulker_box_props(serv, BLOCK_GRAY_SHULKER_BOX, "minecraft:gray_shulker_box");
    init_shulker_box_props(serv, BLOCK_LIGHT_GRAY_SHULKER_BOX, "minecraft:light_gray_shulker_box");
    init_shulker_box_props(serv, BLOCK_CYAN_SHULKER_BOX, "minecraft:cyan_shulker_box");
    init_shulker_box_props(serv, BLOCK_PURPLE_SHULKER_BOX, "minecraft:purple_shulker_box");
    init_shulker_box_props(serv, BLOCK_BLUE_SHULKER_BOX, "minecraft:blue_shulker_box");
    init_shulker_box_props(serv, BLOCK_BROWN_SHULKER_BOX, "minecraft:brown_shulker_box");
    init_shulker_box_props(serv, BLOCK_GREEN_SHULKER_BOX, "minecraft:green_shulker_box");
    init_shulker_box_props(serv, BLOCK_RED_SHULKER_BOX, "minecraft:red_shulker_box");
    init_shulker_box_props(serv, BLOCK_BLACK_SHULKER_BOX, "minecraft:black_shulker_box");

    init_glazed_terracotta_props(serv, BLOCK_WHITE_GLAZED_TERRACOTTA, "minecraft:white_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_ORANGE_GLAZED_TERRACOTTA, "minecraft:orange_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_MAGENTA_GLAZED_TERRACOTTA, "minecraft:magenta_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_LIGHT_BLUE_GLAZED_TERRACOTTA, "minecraft:light_blue_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_YELLOW_GLAZED_TERRACOTTA, "minecraft:yellow_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_LIME_GLAZED_TERRACOTTA, "minecraft:lime_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_PINK_GLAZED_TERRACOTTA, "minecraft:pink_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_GRAY_GLAZED_TERRACOTTA, "minecraft:gray_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_LIGHT_GRAY_GLAZED_TERRACOTTA, "minecraft:light_gray_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_CYAN_GLAZED_TERRACOTTA, "minecraft:cyan_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_PURPLE_GLAZED_TERRACOTTA, "minecraft:purple_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_BLUE_GLAZED_TERRACOTTA, "minecraft:blue_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_BROWN_GLAZED_TERRACOTTA, "minecraft:brown_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_GREEN_GLAZED_TERRACOTTA, "minecraft:green_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_RED_GLAZED_TERRACOTTA, "minecraft:red_glazed_terracotta");
    init_glazed_terracotta_props(serv, BLOCK_BLACK_GLAZED_TERRACOTTA, "minecraft:black_glazed_terracotta");

    init_no_props(serv, BLOCK_WHITE_CONCRETE, "minecraft:white_concrete");
    init_no_props(serv, BLOCK_ORANGE_CONCRETE, "minecraft:orange_concrete");
    init_no_props(serv, BLOCK_MAGENTA_CONCRETE, "minecraft:magenta_concrete");
    init_no_props(serv, BLOCK_LIGHT_BLUE_CONCRETE, "minecraft:light_blue_concrete");
    init_no_props(serv, BLOCK_YELLOW_CONCRETE, "minecraft:yellow_concrete");
    init_no_props(serv, BLOCK_LIME_CONCRETE, "minecraft:lime_concrete");
    init_no_props(serv, BLOCK_PINK_CONCRETE, "minecraft:pink_concrete");
    init_no_props(serv, BLOCK_GRAY_CONCRETE, "minecraft:gray_concrete");
    init_no_props(serv, BLOCK_LIGHT_GRAY_CONCRETE, "minecraft:light_gray_concrete");
    init_no_props(serv, BLOCK_CYAN_CONCRETE, "minecraft:cyan_concrete");
    init_no_props(serv, BLOCK_PURPLE_CONCRETE, "minecraft:purple_concrete");
    init_no_props(serv, BLOCK_BLUE_CONCRETE, "minecraft:blue_concrete");
    init_no_props(serv, BLOCK_BROWN_CONCRETE, "minecraft:brown_concrete");
    init_no_props(serv, BLOCK_GREEN_CONCRETE, "minecraft:green_concrete");
    init_no_props(serv, BLOCK_RED_CONCRETE, "minecraft:red_concrete");
    init_no_props(serv, BLOCK_BLACK_CONCRETE, "minecraft:black_concrete");

    init_no_props(serv, BLOCK_WHITE_CONCRETE_POWDER, "minecraft:white_concrete_powder");
    init_no_props(serv, BLOCK_ORANGE_CONCRETE_POWDER, "minecraft:orange_concrete_powder");
    init_no_props(serv, BLOCK_MAGENTA_CONCRETE_POWDER, "minecraft:magenta_concrete_powder");
    init_no_props(serv, BLOCK_LIGHT_BLUE_CONCRETE_POWDER, "minecraft:light_blue_concrete_powder");
    init_no_props(serv, BLOCK_YELLOW_CONCRETE_POWDER, "minecraft:yellow_concrete_powder");
    init_no_props(serv, BLOCK_LIME_CONCRETE_POWDER, "minecraft:lime_concrete_powder");
    init_no_props(serv, BLOCK_PINK_CONCRETE_POWDER, "minecraft:pink_concrete_powder");
    init_no_props(serv, BLOCK_GRAY_CONCRETE_POWDER, "minecraft:gray_concrete_powder");
    init_no_props(serv, BLOCK_LIGHT_GRAY_CONCRETE_POWDER, "minecraft:light_gray_concrete_powder");
    init_no_props(serv, BLOCK_CYAN_CONCRETE_POWDER, "minecraft:cyan_concrete_powder");
    init_no_props(serv, BLOCK_PURPLE_CONCRETE_POWDER, "minecraft:purple_concrete_powder");
    init_no_props(serv, BLOCK_BLUE_CONCRETE_POWDER, "minecraft:blue_concrete_powder");
    init_no_props(serv, BLOCK_BROWN_CONCRETE_POWDER, "minecraft:brown_concrete_powder");
    init_no_props(serv, BLOCK_GREEN_CONCRETE_POWDER, "minecraft:green_concrete_powder");
    init_no_props(serv, BLOCK_RED_CONCRETE_POWDER, "minecraft:red_concrete_powder");
    init_no_props(serv, BLOCK_BLACK_CONCRETE_POWDER, "minecraft:black_concrete_powder");

    register_block_type(serv, BLOCK_KELP, "minecraft:kelp");
    props = serv->block_properties_table + BLOCK_KELP;
    add_block_range_prop(serv, props, "age", 0, 0, 25);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_KELP_PLANT, "minecraft:kelp_plant");
    init_no_props(serv, BLOCK_DRIED_KELP_BLOCK, "minecraft:dried_kelp_block");

    register_block_type(serv, BLOCK_TURTLE_EGG, "minecraft:turtle_egg");
    props = serv->block_properties_table + BLOCK_TURTLE_EGG;
    add_block_range_prop(serv, props, "eggs", 1, 1, 4);
    add_block_range_prop(serv, props, "hatch", 0, 0, 2);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_DEAD_TUBE_CORAL_BLOCK, "minecraft:dead_tube_coral_block");
    init_no_props(serv, BLOCK_DEAD_BRAIN_CORAL_BLOCK, "minecraft:dead_brain_coral_block");
    init_no_props(serv, BLOCK_DEAD_BUBBLE_CORAL_BLOCK, "minecraft:dead_bubble_coral_block");
    init_no_props(serv, BLOCK_DEAD_FIRE_CORAL_BLOCK, "minecraft:dead_fire_coral_block");
    init_no_props(serv, BLOCK_DEAD_HORN_CORAL_BLOCK, "minecraft:dead_horn_coral_block");

    init_no_props(serv, BLOCK_TUBE_CORAL_BLOCK, "minecraft:tube_coral_block");
    init_no_props(serv, BLOCK_BRAIN_CORAL_BLOCK, "minecraft:brain_coral_block");
    init_no_props(serv, BLOCK_BUBBLE_CORAL_BLOCK, "minecraft:bubble_coral_block");
    init_no_props(serv, BLOCK_FIRE_CORAL_BLOCK, "minecraft:fire_coral_block");
    init_no_props(serv, BLOCK_HORN_CORAL_BLOCK, "minecraft:horn_coral_block");

    init_coral_props(serv, BLOCK_DEAD_TUBE_CORAL, "minecraft:dead_tube_coral");
    init_coral_props(serv, BLOCK_DEAD_BRAIN_CORAL, "minecraft:dead_brain_coral");
    init_coral_props(serv, BLOCK_DEAD_BUBBLE_CORAL, "minecraft:dead_bubble_coral");
    init_coral_props(serv, BLOCK_DEAD_FIRE_CORAL, "minecraft:dead_fire_coral");
    init_coral_props(serv, BLOCK_DEAD_HORN_CORAL, "minecraft:dead_horn_coral");

    init_coral_props(serv, BLOCK_TUBE_CORAL, "minecraft:tube_coral");
    init_coral_props(serv, BLOCK_BRAIN_CORAL, "minecraft:brain_coral");
    init_coral_props(serv, BLOCK_BUBBLE_CORAL, "minecraft:bubble_coral");
    init_coral_props(serv, BLOCK_FIRE_CORAL, "minecraft:fire_coral");
    init_coral_props(serv, BLOCK_HORN_CORAL, "minecraft:horn_coral");

    init_coral_fan_props(serv, BLOCK_DEAD_TUBE_CORAL_FAN, "minecraft:dead_tube_coral_fan");
    init_coral_fan_props(serv, BLOCK_DEAD_BRAIN_CORAL_FAN, "minecraft:dead_brain_coral_fan");
    init_coral_fan_props(serv, BLOCK_DEAD_BUBBLE_CORAL_FAN, "minecraft:dead_bubble_coral_fan");
    init_coral_fan_props(serv, BLOCK_DEAD_FIRE_CORAL_FAN, "minecraft:dead_fire_coral_fan");
    init_coral_fan_props(serv, BLOCK_DEAD_HORN_CORAL_FAN, "minecraft:dead_horn_coral_fan");

    init_coral_fan_props(serv, BLOCK_TUBE_CORAL_FAN, "minecraft:tube_coral_fan");
    init_coral_fan_props(serv, BLOCK_BRAIN_CORAL_FAN, "minecraft:brain_coral_fan");
    init_coral_fan_props(serv, BLOCK_BUBBLE_CORAL_FAN, "minecraft:bubble_coral_fan");
    init_coral_fan_props(serv, BLOCK_FIRE_CORAL_FAN, "minecraft:fire_coral_fan");
    init_coral_fan_props(serv, BLOCK_HORN_CORAL_FAN, "minecraft:horn_coral_fan");

    init_coral_wall_fan_props(serv, BLOCK_DEAD_TUBE_CORAL_WALL_FAN, "minecraft:dead_tube_coral_wall_fan");
    init_coral_wall_fan_props(serv, BLOCK_DEAD_BRAIN_CORAL_WALL_FAN, "minecraft:dead_brain_coral_wall_fan");
    init_coral_wall_fan_props(serv, BLOCK_DEAD_BUBBLE_CORAL_WALL_FAN, "minecraft:dead_bubble_coral_wall_fan");
    init_coral_wall_fan_props(serv, BLOCK_DEAD_FIRE_CORAL_WALL_FAN, "minecraft:dead_fire_coral_wall_fan");
    init_coral_wall_fan_props(serv, BLOCK_DEAD_HORN_CORAL_WALL_FAN, "minecraft:dead_horn_coral_wall_fan");

    init_coral_wall_fan_props(serv, BLOCK_TUBE_CORAL_WALL_FAN, "minecraft:tube_coral_wall_fan");
    init_coral_wall_fan_props(serv, BLOCK_BRAIN_CORAL_WALL_FAN, "minecraft:brain_coral_wall_fan");
    init_coral_wall_fan_props(serv, BLOCK_BUBBLE_CORAL_WALL_FAN, "minecraft:bubble_coral_wall_fan");
    init_coral_wall_fan_props(serv, BLOCK_FIRE_CORAL_WALL_FAN, "minecraft:fire_coral_wall_fan");
    init_coral_wall_fan_props(serv, BLOCK_HORN_CORAL_WALL_FAN, "minecraft:horn_coral_wall_fan");

    register_block_type(serv, BLOCK_SEA_PICKLE, "minecraft:sea_pickle");
    props = serv->block_properties_table + BLOCK_SEA_PICKLE;
    add_block_range_prop(serv, props, "pickles", 1, 1, 4);
    add_block_prop(serv, props, "waterlogged", "true", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_BLUE_ICE, "minecraft:blue_ice");

    register_block_type(serv, BLOCK_CONDUIT, "minecraft:conduit");
    props = serv->block_properties_table + BLOCK_CONDUIT;
    add_block_prop(serv, props, "waterlogged", "true", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_BAMBOO_SAPLING, "minecraft:bamboo_sapling");

    register_block_type(serv, BLOCK_BAMBOO, "minecraft:bamboo");
    props = serv->block_properties_table + BLOCK_BAMBOO;
    add_block_range_prop(serv, props, "age", 0, 0, 1);
    add_block_prop(serv, props, "leaves", "none", 3, "none", "small", "large");
    add_block_range_prop(serv, props, "stage", 0, 0, 1);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_POTTED_BAMBOO, "minecraft:potted_bamboo");
    init_no_props(serv, BLOCK_VOID_AIR, "minecraft:void_air");
    init_no_props(serv, BLOCK_CAVE_AIR, "minecraft:cave_air");

    register_block_type(serv, BLOCK_BUBBLE_COLUMN, "minecraft:bubble_column");
    props = serv->block_properties_table + BLOCK_BUBBLE_COLUMN;
    add_block_prop(serv, props, "drag", "true", 2, "true", "false");
    finalise_block_props(serv, props);

    init_stair_props(serv, BLOCK_POLISHED_GRANITE_STAIRS, "minecraft:polished_granite_stairs");
    init_stair_props(serv, BLOCK_SMOOTH_RED_SANDSTONE_STAIRS, "minecraft:smooth_red_sandstone_stairs");
    init_stair_props(serv, BLOCK_MOSSY_STONE_BRICK_STAIRS, "minecraft:mossy_stone_brick_stairs");
    init_stair_props(serv, BLOCK_POLISHED_DIORITE_STAIRS, "minecraft:polished_diorite_stairs");
    init_stair_props(serv, BLOCK_MOSSY_COBBLESTONE_STAIRS, "minecraft:mossy_cobblestone_stairs");
    init_stair_props(serv, BLOCK_END_STONE_BRICK_STAIRS, "minecraft:end_stone_brick_stairs");
    init_stair_props(serv, BLOCK_STONE_STAIRS, "minecraft:stone_stairs");
    init_stair_props(serv, BLOCK_SMOOTH_SANDSTONE_STAIRS, "minecraft:smooth_sandstone_stairs");
    init_stair_props(serv, BLOCK_SMOOTH_QUARTZ_STAIRS, "minecraft:smooth_quartz_stairs");
    init_stair_props(serv, BLOCK_GRANITE_STAIRS, "minecraft:granite_stairs");
    init_stair_props(serv, BLOCK_ANDESITE_STAIRS, "minecraft:andesite_stairs");
    init_stair_props(serv, BLOCK_RED_NETHER_BRICK_STAIRS, "minecraft:red_nether_brick_stairs");
    init_stair_props(serv, BLOCK_POLISHED_ANDESITE_STAIRS, "minecraft:polished_andesite_stairs");
    init_stair_props(serv, BLOCK_DIORITE_STAIRS, "minecraft:diorite_stairs");

    init_slab_props(serv, BLOCK_POLISHED_GRANITE_SLAB, "minecraft:polished_granite_slab");
    init_slab_props(serv, BLOCK_SMOOTH_RED_SANDSTONE_SLAB, "minecraft:smooth_red_sandstone_slab");
    init_slab_props(serv, BLOCK_MOSSY_STONE_BRICK_SLAB, "minecraft:mossy_stone_brick_slab");
    init_slab_props(serv, BLOCK_POLISHED_DIORITE_SLAB, "minecraft:polished_diorite_slab");
    init_slab_props(serv, BLOCK_MOSSY_COBBLESTONE_SLAB, "minecraft:mossy_cobblestone_slab");
    init_slab_props(serv, BLOCK_END_STONE_BRICK_SLAB, "minecraft:end_stone_brick_slab");
    init_slab_props(serv, BLOCK_SMOOTH_SANDSTONE_SLAB, "minecraft:smooth_sandstone_slab");
    init_slab_props(serv, BLOCK_SMOOTH_QUARTZ_SLAB, "minecraft:smooth_quartz_slab");
    init_slab_props(serv, BLOCK_GRANITE_SLAB, "minecraft:granite_slab");
    init_slab_props(serv, BLOCK_ANDESITE_SLAB, "minecraft:andesite_slab");
    init_slab_props(serv, BLOCK_RED_NETHER_BRICK_SLAB, "minecraft:red_nether_brick_slab");
    init_slab_props(serv, BLOCK_POLISHED_ANDESITE_SLAB, "minecraft:polished_andesite_slab");
    init_slab_props(serv, BLOCK_DIORITE_SLAB, "minecraft:diorite_slab");

    init_wall_props(serv, BLOCK_BRICK_WALL, "minecraft:brick_wall");
    init_wall_props(serv, BLOCK_PRISMARINE_WALL, "minecraft:prismarine_wall");
    init_wall_props(serv, BLOCK_RED_SANDSTONE_WALL, "minecraft:red_sandstone_wall");
    init_wall_props(serv, BLOCK_MOSSY_STONE_BRICK_WALL, "minecraft:mossy_stone_brick_wall");
    init_wall_props(serv, BLOCK_GRANITE_WALL, "minecraft:granite_wall");
    init_wall_props(serv, BLOCK_STONE_BRICK_WALL, "minecraft:stone_brick_wall");
    init_wall_props(serv, BLOCK_NETHER_BRICK_WALL, "minecraft:nether_brick_wall");
    init_wall_props(serv, BLOCK_ANDESITE_WALL, "minecraft:andesite_wall");
    init_wall_props(serv, BLOCK_RED_NETHER_BRICK_WALL, "minecraft:red_nether_brick_wall");
    init_wall_props(serv, BLOCK_SANDSTONE_WALL, "minecraft:sandstone_wall");
    init_wall_props(serv, BLOCK_END_STONE_BRICK_WALL, "minecraft:end_stone_brick_wall");
    init_wall_props(serv, BLOCK_DIORITE_WALL, "minecraft:diorite_wall");

    register_block_type(serv, BLOCK_SCAFFOLDING, "minecraft:scaffolding");
    props = serv->block_properties_table + BLOCK_SCAFFOLDING;
    add_block_prop(serv, props, "bottom", "false", 2, "true", "false");
    add_block_range_prop(serv, props, "distance", 7, 0, 7);
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_LOOM, "minecraft:loom");
    props = serv->block_properties_table + BLOCK_LOOM;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_BARREL, "minecraft:barrel");
    props = serv->block_properties_table + BLOCK_BARREL;
    add_block_prop(serv, props, "facing", "north", 6, "north", "east", "south", "west", "up", "down");
    add_block_prop(serv, props, "open", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_SMOKER, "minecraft:smoker");
    props = serv->block_properties_table + BLOCK_SMOKER;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "lit", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_FURNACE, "minecraft:furnace");
    props = serv->block_properties_table + BLOCK_BLAST_FURNACE;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "lit", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_CARTOGRAPHY_TABLE, "minecraft:cartography_table");
    init_no_props(serv, BLOCK_FLETCHING_TABLE, "minecraft:fletching_table");

    register_block_type(serv, BLOCK_GRINDSTONE, "minecraft:grindstone");
    props = serv->block_properties_table + BLOCK_GRINDSTONE;
    add_block_prop(serv, props, "floor", "wall", 3, "floor", "wall", "ceiling");
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_LECTERN, "minecraft:lectern");
    props = serv->block_properties_table + BLOCK_LECTERN;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "has_book", "false", 2, "true", "false");
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_SMITHING_TABLE, "minecraft:smithing_table");

    register_block_type(serv, BLOCK_STONECUTTER, "minecraft:stonecutter");
    props = serv->block_properties_table + BLOCK_STONECUTTER;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_BELL, "minecraft:bell");
    props = serv->block_properties_table + BLOCK_BELL;
    add_block_prop(serv, props, "attachment", "floor", 4, "floor", "ceiling", "single_wall", "double_wall");
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "powered", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_LANTERN, "minecraft:lantern");
    props = serv->block_properties_table + BLOCK_LANTERN;
    add_block_prop(serv, props, "hanging", "false", 2, "true", "false");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_SOUL_LANTERN, "minecraft:soul_lantern");
    props = serv->block_properties_table + BLOCK_SOUL_LANTERN;
    add_block_prop(serv, props, "hanging", "false", 2, "true", "false");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_CAMPFIRE, "minecraft:campfire");
    props = serv->block_properties_table + BLOCK_CAMPFIRE;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "lit", "true", 2, "true", "false");
    add_block_prop(serv, props, "signal_fire", "false", 2, "true", "false");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_SOUL_CAMPFIRE, "minecraft:soul_campfire");
    props = serv->block_properties_table + BLOCK_SOUL_CAMPFIRE;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_prop(serv, props, "lit", "true", 2, "true", "false");
    add_block_prop(serv, props, "signal_fire", "false", 2, "true", "false");
    add_block_prop(serv, props, "waterlogged", "false", 2, "true", "false");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_SWEET_BERRY_BUSH, "minecraft:sweet_berry_bush");
    props = serv->block_properties_table + BLOCK_SWEET_BERRY_BUSH;
    add_block_range_prop(serv, props, "age", 0, 0, 3);
    finalise_block_props(serv, props);

    init_pillar_props(serv, BLOCK_WARPED_STEM, "minecraft:warped_stem");
    init_pillar_props(serv, BLOCK_STRIPPED_WARPED_STEM, "minecraft:stripped_warped_stem");
    init_pillar_props(serv, BLOCK_WARPED_HYPHAE, "minecraft:warped_hyphae");
    init_pillar_props(serv, BLOCK_STRIPPED_WARPED_HYPHAE, "minecraft:stripped_warped_hyphae");

    init_no_props(serv, BLOCK_WARPED_NYLIUM, "minecraft:warped_nylium");
    init_no_props(serv, BLOCK_WARPED_FUNGUS, "minecraft:warped_fungus");
    init_no_props(serv, BLOCK_WARPED_WART_BLOCK, "minecraft:warped_wart_block");
    init_no_props(serv, BLOCK_WARPED_ROOTS, "minecraft:warped_roots");
    init_no_props(serv, BLOCK_NETHER_SPROUTS, "minecraft:nether_sprouts");

    init_pillar_props(serv, BLOCK_CRIMSON_STEM, "minecraft:crimson_stem");
    init_pillar_props(serv, BLOCK_STRIPPED_CRIMSON_STEM, "minecraft:stripped_crimson_stem");
    init_pillar_props(serv, BLOCK_CRIMSON_HYPHAE, "minecraft:crimson_hyphae");
    init_pillar_props(serv, BLOCK_STRIPPED_CRIMSON_HYPHAE, "minecraft:stripped_crimson_hyphae");

    init_no_props(serv, BLOCK_CRIMSON_NYLIUM, "minecraft:crimson_nylium");
    init_no_props(serv, BLOCK_CRIMSON_FUNGUS, "minecraft:crimson_fungus");
    init_no_props(serv, BLOCK_SHROOMLIGHT, "minecraft:shroomlight");

    register_block_type(serv, BLOCK_WEEPING_VINES, "minecraft:weeping_vines");
    props = serv->block_properties_table + BLOCK_WEEPING_VINES;
    add_block_range_prop(serv, props, "age", 0, 0, 25);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_WEEPING_VINES_PLANT, "minecraft:weeping_vines_plant");

    register_block_type(serv, BLOCK_TWISTING_VINES, "minecraft:twisting_vines");
    props = serv->block_properties_table + BLOCK_TWISTING_VINES;
    add_block_range_prop(serv, props, "age", 0, 0, 25);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_TWISTING_VINES_PLANT, "minecraft:twisting_vines_plant");

    init_no_props(serv, BLOCK_CRIMSON_ROOTS, "minecraft:crimson_roots");
    init_no_props(serv, BLOCK_CRIMSON_PLANKS, "minecraft:crimson_planks");
    init_no_props(serv, BLOCK_WARPED_PLANKS, "minecraft:warped_planks");

    init_slab_props(serv, BLOCK_CRIMSON_SLAB, "minecraft:crimson_slab");
    init_slab_props(serv, BLOCK_WARPED_SLAB, "minecraft:warped_slab");

    init_pressure_plate_props(serv, BLOCK_CRIMSON_PRESSURE_PLATE, "minecraft:crimson_pressure_plate");
    init_pressure_plate_props(serv, BLOCK_WARPED_PRESSURE_PLATE, "minecraft:warped_pressure_plate");

    init_cross_props(serv, BLOCK_CRIMSON_FENCE, "minecraft:crimson_fence");
    init_cross_props(serv, BLOCK_WARPED_FENCE, "minecraft:warped_fence");

    init_trapdoor_props(serv, BLOCK_CRIMSON_TRAPDOOR, "minecraft:crimson_trapdoor");
    init_trapdoor_props(serv, BLOCK_WARPED_TRAPDOOR, "minecraft:warped_trapdoor");

    init_fence_gate_props(serv, BLOCK_CRIMSON_FENCE_GATE, "minecraft:crimson_fence_gate");
    init_fence_gate_props(serv, BLOCK_WARPED_FENCE_GATE, "minecraft:warped_fence_gate");

    init_stair_props(serv, BLOCK_CRIMSON_STAIRS, "minecraft:crimson_stairs");
    init_stair_props(serv, BLOCK_WARPED_STAIRS, "minecraft:warped_stairs");

    init_button_props(serv, BLOCK_CRIMSON_BUTTON, "minecraft:crimson_button");
    init_button_props(serv, BLOCK_WARPED_BUTTON, "minecraft:warped_button");

    init_door_props(serv, BLOCK_CRIMSON_DOOR, "minecraft:crimson_door");
    init_door_props(serv, BLOCK_WARPED_DOOR, "minecraft:warped_door");

    init_sign_props(serv, BLOCK_CRIMSON_SIGN, "minecraft:crimson_sign");
    init_sign_props(serv, BLOCK_WARPED_SIGN, "minecraft:warped_sign");

    init_wall_sign_props(serv, BLOCK_CRIMSON_WALL_SIGN, "minecraft:crimson_wall_sign");
    init_wall_sign_props(serv, BLOCK_WARPED_WALL_SIGN, "minecraft:warped_wall_sign");

    register_block_type(serv, BLOCK_STRUCTURE_BLOCK, "minecraft:structure_block");
    props = serv->block_properties_table + BLOCK_STRUCTURE_BLOCK;
    add_block_prop(serv, props, "mode", "save", 4, "save", "load", "corner", "data");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_JIGSAW, "minecraft:jigsaw");
    props = serv->block_properties_table + BLOCK_JIGSAW;
    add_block_prop(serv, props, "orientation", "north_up", 12, "down_east", "down_north", "down_south", "down_west", "up_east", "up_north", "up_south", "up_west", "west_up", "east_up", "north_up", "south_up");
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_COMPOSTER, "minecraft:composter");
    props = serv->block_properties_table + BLOCK_COMPOSTER;
    add_block_range_prop(serv, props, "level", 0, 0, 8);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_TARGET, "minecraft:target");
    props = serv->block_properties_table + BLOCK_TARGET;
    add_block_range_prop(serv, props, "power", 0, 0, 15);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_BEE_NEST, "minecraft:bee_nest");
    props = serv->block_properties_table + BLOCK_BEE_NEST;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_range_prop(serv, props, "honey_level", 0, 0, 5);
    finalise_block_props(serv, props);

    register_block_type(serv, BLOCK_BEEHIVE, "minecraft:beehive");
    props = serv->block_properties_table + BLOCK_BEEHIVE;
    add_block_prop(serv, props, "facing", "north", 4, "north", "south", "west", "east");
    add_block_range_prop(serv, props, "honey_level", 0, 0, 5);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_HONEY_BLOCK, "minecraft:honey_block");
    init_no_props(serv, BLOCK_HONEYCOMB_BLOCK, "minecraft:honeycomb_block");
    init_no_props(serv, BLOCK_NETHERITE_BLOCK, "minecraft:netherite_block");
    init_no_props(serv, BLOCK_ANCIENT_DEBRIS, "minecraft:ancient_debris");
    init_no_props(serv, BLOCK_CRYING_OBSIDIAN, "minecraft:crying_obsidian");

    register_block_type(serv, BLOCK_RESPAWN_ANCHOR, "minecraft:respawn_anchor");
    props = serv->block_properties_table + BLOCK_RESPAWN_ANCHOR;
    add_block_range_prop(serv, props, "charges", 0, 0, 4);
    finalise_block_props(serv, props);

    init_no_props(serv, BLOCK_POTTED_CRIMSON_FUNGUS, "minecraft:potted_crimson_fungus");
    init_no_props(serv, BLOCK_POTTED_WARPED_FUNGUS, "minecraft:potted_warped_fungus");
    init_no_props(serv, BLOCK_POTTED_CRIMSON_ROOTS, "minecraft:potted_crimson_roots");
    init_no_props(serv, BLOCK_POTTED_WARPED_ROOTS, "minecraft:potted_warped_roots");
    init_no_props(serv, BLOCK_LODESTONE, "minecraft:lodestone");
    init_no_props(serv, BLOCK_BLACKSTONE, "minecraft:blackstone");

    init_stair_props(serv, BLOCK_BLACKSTONE_STAIRS, "minecraft:blackstone_stairs");

    init_wall_props(serv, BLOCK_BLACKSTONE_WALL, "minecraft:blackstone_wall");

    init_slab_props(serv, BLOCK_BLACKSTONE_SLAB, "minecraft:blackstone_slab");

    init_no_props(serv, BLOCK_POLISHED_BLACKSTONE, "minecraft:polished_blackstone");
    init_no_props(serv, BLOCK_POLISHED_BLACKSTONE_BRICKS, "minecraft:polished_blackstone_bricks");
    init_no_props(serv, BLOCK_CRACKED_POLISHED_BLACKSTONE_BRICKS, "minecraft:cracked_polished_blackstone_bricks");
    init_no_props(serv, BLOCK_CHISELED_POLISHED_BLACKSTONE, "minecraft:chiseled_polished_blackstone");

    init_slab_props(serv, BLOCK_POLISHED_BLACKSTONE_BRICK_SLAB, "minecraft:polished_blackstone_brick_slab");

    init_stair_props(serv, BLOCK_POLISHED_BLACKSTONE_BRICK_STAIRS, "minecraft:polished_blackstone_brick_stairs");

    init_wall_props(serv, BLOCK_POLISHED_BLACKSTONE_BRICK_WALL, "minecraft:polished_blackstone_brick_wall");

    init_no_props(serv, BLOCK_GILDED_BLACKSTONE, "minecraft:gilded_blackstone");

    init_stair_props(serv, BLOCK_POLISHED_BLACKSTONE_STAIRS, "minecraft:polished_blackstone_stairs");

    init_slab_props(serv, BLOCK_POLISHED_BLACKSTONE_SLAB, "minecraft:polished_blackstone_slab");

    init_pressure_plate_props(serv, BLOCK_POLISHED_BLACKSTONE_PRESSURE_PLATE, "minecraft:polished_blackstone_pressure_plate");

    init_button_props(serv, BLOCK_POLISHED_BLACKSTONE_BUTTON, "minecraft:polished_blackstone_button");

    init_wall_props(serv, BLOCK_POLISHED_BLACKSTONE_WALL, "minecraft:polished_blackstone_wall");

    init_no_props(serv, BLOCK_CHISELED_NETHER_BRICKS, "minecraft:chiseled_nether_bricks");
    init_no_props(serv, BLOCK_CRACKED_NETHER_BRICKS, "minecraft:crakced_nether_bricks");
    init_no_props(serv, BLOCK_QUARTZ_BRICKS, "minecraft:quartz_bricks");
}
