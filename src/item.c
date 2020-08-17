#include "shared.h"

static void
place_simple_block(net_block_pos clicked_pos, mc_int clicked_face,
        mc_ushort place_block_state) {
    int can_replace = 0;

    net_block_pos target_pos = clicked_pos;
    chunk_pos target_chunk_pos = {
        .x = target_pos.x >> 4,
        .z = target_pos.z >> 4
    };
    chunk * ch = get_chunk_if_loaded(target_chunk_pos);
    if (ch == NULL) {
        return;
    }

    mc_ushort cur_block_state = chunk_get_block_state(ch,
            target_pos.x & 0xf, target_pos.y, target_pos.z & 0xf);

    if (cur_block_state == 0) {
        // @TODO(traks) this allows players to place blocks in mid-air. Is
        // there a reason one might want to allow this?
        can_replace = 1;
    } else {
        // @TODO(traks) check for all the other things
    }

    if (!can_replace) {
        switch (clicked_face) {
        case DIRECTION_NEG_Y: target_pos.y--; break;
        case DIRECTION_POS_Y: target_pos.y++; break;
        case DIRECTION_NEG_Z: target_pos.z--; break;
        case DIRECTION_POS_Z: target_pos.z++; break;
        case DIRECTION_NEG_X: target_pos.x--; break;
        case DIRECTION_POS_X: target_pos.x++; break;
        }

        target_chunk_pos = (chunk_pos) {
            .x = target_pos.x >> 4,
            .z = target_pos.z >> 4
        };
        ch = get_chunk_if_loaded(target_chunk_pos);
        if (ch == NULL) {
            return;
        }
    }

    chunk_set_block_state(ch, target_pos.x & 0xf, target_pos.y,
            target_pos.z & 0xf, place_block_state);
}

void
process_use_item_on_packet(entity_data * entity, player_brain * brain,
        mc_int hand, net_block_pos clicked_pos, mc_int clicked_face,
        float click_offset_x, float click_offset_y, float click_offset_z,
        mc_ubyte is_inside) {
    if (entity->flags & ENTITY_TELEPORTING) {
        // ignore
        return;
    }

    int sel_slot = entity->player.selected_slot;
    item_stack * main = entity->player.slots + sel_slot;
    item_stack * off = entity->player.slots + PLAYER_OFF_HAND_SLOT;
    item_stack * used = hand == PLAYER_MAIN_HAND ? main : off;

    // @TODO(traks) special handling depending on gamemode. Currently we assume
    // gamemode creative

    // @TODO(traks) ensure clicked block is in one of the sent
    // chunks inside the player's chunk cache

    // @TODO(traks) check for cooldowns (ender pearls,
    // chorus fruits)

    // if the player is not crouching with an item in their hands, try to use
    // the clicked block
    if (!((brain->flags & PLAYER_BRAIN_SHIFTING)
            && (main->type != ITEM_AIR || off->type != ITEM_AIR))) {
        // @TODO(traks) use clicked block (button, door, etc.)
    }

    // try to use the held item

    // @TODO(traks) swing arm if necessary

    // @TODO(traks) send "built too high" action bar message

    // @TODO(traks) if placement cancelled unexpectedly, send block update
    // packet back to the player to fix the possible desync

    // @TODO(traks) adventure mode place allow/deny

    // @TODO(traks) implement all items

    switch (used->type) {
    case ITEM_AIR:
        // nothing to do
        break;
    case ITEM_STONE:
        place_simple_block(clicked_pos, clicked_face, 1);
        break;
    case ITEM_GRANITE:
        place_simple_block(clicked_pos, clicked_face, 2);
        break;
    case ITEM_POLISHED_GRANITE:
        place_simple_block(clicked_pos, clicked_face, 3);
        break;
    case ITEM_DIORITE:
        place_simple_block(clicked_pos, clicked_face, 4);
        break;
    case ITEM_POLISHED_DIORITE:
        place_simple_block(clicked_pos, clicked_face, 5);
        break;
    case ITEM_ANDESITE:
        place_simple_block(clicked_pos, clicked_face, 6);
        break;
    case ITEM_POLISHED_ANDESITE:
        place_simple_block(clicked_pos, clicked_face, 7);
        break;
    case ITEM_GRASS_BLOCK:
        break;
    case ITEM_DIRT:
        place_simple_block(clicked_pos, clicked_face, 10);
        break;
    case ITEM_COARSE_DIRT:
        place_simple_block(clicked_pos, clicked_face, 11);
        break;
    case ITEM_PODZOL:
        break;
    case ITEM_CRIMSON_NYLIUM:
        break;
    case ITEM_WARPED_NYLIUM:
        break;
    case ITEM_COBBLESTONE:
        break;
    case ITEM_OAK_PLANKS:
        break;
    case ITEM_SPRUCE_PLANKS:
        break;
    case ITEM_BIRCH_PLANKS:
        break;
    case ITEM_JUNGLE_PLANKS:
        break;
    case ITEM_ACACIA_PLANKS:
        break;
    case ITEM_DARK_OAK_PLANKS:
        break;
    case ITEM_CRIMSON_PLANKS:
        break;
    case ITEM_WARPED_PLANKS:
        break;
    case ITEM_OAK_SAPLING:
        break;
    case ITEM_SPRUCE_SAPLING:
        break;
    case ITEM_BIRCH_SAPLING:
        break;
    case ITEM_JUNGLE_SAPLING:
        break;
    case ITEM_ACACIA_SAPLING:
        break;
    case ITEM_DARK_OAK_SAPLING:
        break;
    case ITEM_BEDROCK:
        break;
    case ITEM_SAND:
        break;
    case ITEM_RED_SAND:
        break;
    case ITEM_GRAVEL:
        break;
    case ITEM_GOLD_ORE:
        break;
    case ITEM_IRON_ORE:
        break;
    case ITEM_COAL_ORE:
        break;
    case ITEM_NETHER_GOLD_ORE:
        break;
    case ITEM_OAK_LOG:
        break;
    case ITEM_SPRUCE_LOG:
        break;
    case ITEM_BIRCH_LOG:
        break;
    case ITEM_JUNGLE_LOG:
        break;
    case ITEM_ACACIA_LOG:
        break;
    case ITEM_DARK_OAK_LOG:
        break;
    case ITEM_CRIMSON_STEM:
        break;
    case ITEM_WARPED_STEM:
        break;
    case ITEM_STRIPPED_OAK_LOG:
        break;
    case ITEM_STRIPPED_SPRUCE_LOG:
        break;
    case ITEM_STRIPPED_BIRCH_LOG:
        break;
    case ITEM_STRIPPED_JUNGLE_LOG:
        break;
    case ITEM_STRIPPED_ACACIA_LOG:
        break;
    case ITEM_STRIPPED_DARK_OAK_LOG:
        break;
    case ITEM_STRIPPED_CRIMSON_STEM:
        break;
    case ITEM_STRIPPED_WARPED_STEM:
        break;
    case ITEM_STRIPPED_OAK_WOOD:
        break;
    case ITEM_STRIPPED_SPRUCE_WOOD:
        break;
    case ITEM_STRIPPED_BIRCH_WOOD:
        break;
    case ITEM_STRIPPED_JUNGLE_WOOD:
        break;
    case ITEM_STRIPPED_ACACIA_WOOD:
        break;
    case ITEM_STRIPPED_DARK_OAK_WOOD:
        break;
    case ITEM_STRIPPED_CRIMSON_HYPHAE:
        break;
    case ITEM_STRIPPED_WARPED_HYPHAE:
        break;
    case ITEM_OAK_WOOD:
        break;
    case ITEM_SPRUCE_WOOD:
        break;
    case ITEM_BIRCH_WOOD:
        break;
    case ITEM_JUNGLE_WOOD:
        break;
    case ITEM_ACACIA_WOOD:
        break;
    case ITEM_DARK_OAK_WOOD:
        break;
    case ITEM_CRIMSON_HYPHAE:
        break;
    case ITEM_WARPED_HYPHAE:
        break;
    case ITEM_OAK_LEAVES:
        break;
    case ITEM_SPRUCE_LEAVES:
        break;
    case ITEM_BIRCH_LEAVES:
        break;
    case ITEM_JUNGLE_LEAVES:
        break;
    case ITEM_ACACIA_LEAVES:
        break;
    case ITEM_DARK_OAK_LEAVES:
        break;
    case ITEM_SPONGE:
        break;
    case ITEM_WET_SPONGE:
        break;
    case ITEM_GLASS:
        break;
    case ITEM_LAPIS_ORE:
        break;
    case ITEM_LAPIS_BLOCK:
        break;
    case ITEM_DISPENSER:
        break;
    case ITEM_SANDSTONE:
        break;
    case ITEM_CHISELED_SANDSTONE:
        break;
    case ITEM_CUT_SANDSTONE:
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
        break;
    case ITEM_GRASS:
        break;
    case ITEM_FERN:
        break;
    case ITEM_DEAD_BUSH:
        break;
    case ITEM_SEAGRASS:
        break;
    case ITEM_SEA_PICKLE:
        break;
    case ITEM_PISTON:
        break;
    case ITEM_WHITE_WOOL:
        break;
    case ITEM_ORANGE_WOOL:
        break;
    case ITEM_MAGENTA_WOOL:
        break;
    case ITEM_LIGHT_BLUE_WOOL:
        break;
    case ITEM_YELLOW_WOOL:
        break;
    case ITEM_LIME_WOOL:
        break;
    case ITEM_PINK_WOOL:
        break;
    case ITEM_GRAY_WOOL:
        break;
    case ITEM_LIGHT_GRAY_WOOL:
        break;
    case ITEM_CYAN_WOOL:
        break;
    case ITEM_PURPLE_WOOL:
        break;
    case ITEM_BLUE_WOOL:
        break;
    case ITEM_BROWN_WOOL:
        break;
    case ITEM_GREEN_WOOL:
        break;
    case ITEM_RED_WOOL:
        break;
    case ITEM_BLACK_WOOL:
        break;
    case ITEM_DANDELION:
        break;
    case ITEM_POPPY:
        break;
    case ITEM_BLUE_ORCHID:
        break;
    case ITEM_ALLIUM:
        break;
    case ITEM_AZURE_BLUET:
        break;
    case ITEM_RED_TULIP:
        break;
    case ITEM_ORANGE_TULIP:
        break;
    case ITEM_WHITE_TULIP:
        break;
    case ITEM_PINK_TULIP:
        break;
    case ITEM_OXEYE_DAISY:
        break;
    case ITEM_CORNFLOWER:
        break;
    case ITEM_LILY_OF_THE_VALLEY:
        break;
    case ITEM_WITHER_ROSE:
        break;
    case ITEM_BROWN_MUSHROOM:
        break;
    case ITEM_RED_MUSHROOM:
        break;
    case ITEM_CRIMSON_FUNGUS:
        break;
    case ITEM_WARPED_FUNGUS:
        break;
    case ITEM_CRIMSON_ROOTS:
        break;
    case ITEM_WARPED_ROOTS:
        break;
    case ITEM_NETHER_SPROUTS:
        break;
    case ITEM_WEEPING_VINES:
        break;
    case ITEM_TWISTING_VINES:
        break;
    case ITEM_SUGAR_CANE:
        break;
    case ITEM_KELP:
        break;
    case ITEM_BAMBOO:
        break;
    case ITEM_GOLD_BLOCK:
        break;
    case ITEM_IRON_BLOCK:
        break;
    case ITEM_OAK_SLAB:
        break;
    case ITEM_SPRUCE_SLAB:
        break;
    case ITEM_BIRCH_SLAB:
        break;
    case ITEM_JUNGLE_SLAB:
        break;
    case ITEM_ACACIA_SLAB:
        break;
    case ITEM_DARK_OAK_SLAB:
        break;
    case ITEM_CRIMSON_SLAB:
        break;
    case ITEM_WARPED_SLAB:
        break;
    case ITEM_STONE_SLAB:
        break;
    case ITEM_SMOOTH_STONE_SLAB:
        break;
    case ITEM_SANDSTONE_SLAB:
        break;
    case ITEM_CUT_SANDSTONE_SLAB:
        break;
    case ITEM_PETRIFIED_OAK_SLAB:
        break;
    case ITEM_COBBLESTONE_SLAB:
        break;
    case ITEM_BRICK_SLAB:
        break;
    case ITEM_STONE_BRICK_SLAB:
        break;
    case ITEM_NETHER_BRICK_SLAB:
        break;
    case ITEM_QUARTZ_SLAB:
        break;
    case ITEM_RED_SANDSTONE_SLAB:
        break;
    case ITEM_CUT_RED_SANDSTONE_SLAB:
        break;
    case ITEM_PURPUR_SLAB:
        break;
    case ITEM_PRISMARINE_SLAB:
        break;
    case ITEM_PRISMARINE_BRICK_SLAB:
        break;
    case ITEM_DARK_PRISMARINE_SLAB:
        break;
    case ITEM_SMOOTH_QUARTZ:
        break;
    case ITEM_SMOOTH_RED_SANDSTONE:
        break;
    case ITEM_SMOOTH_SANDSTONE:
        break;
    case ITEM_SMOOTH_STONE:
        break;
    case ITEM_BRICKS:
        break;
    case ITEM_TNT:
        break;
    case ITEM_BOOKSHELF:
        break;
    case ITEM_MOSSY_COBBLESTONE:
        break;
    case ITEM_OBSIDIAN:
        break;
    case ITEM_TORCH:
        break;
    case ITEM_END_ROD:
        break;
    case ITEM_CHORUS_PLANT:
        break;
    case ITEM_CHORUS_FLOWER:
        break;
    case ITEM_PURPUR_BLOCK:
        break;
    case ITEM_PURPUR_PILLAR:
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
        break;
    case ITEM_DIAMOND_BLOCK:
        break;
    case ITEM_CRAFTING_TABLE:
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
        break;
    case ITEM_REDSTONE_TORCH:
        break;
    case ITEM_SNOW:
        break;
    case ITEM_ICE:
        break;
    case ITEM_SNOW_BLOCK:
        break;
    case ITEM_CACTUS:
        break;
    case ITEM_CLAY:
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
        break;
    case ITEM_CARVED_PUMPKIN:
        break;
    case ITEM_NETHERRACK:
        break;
    case ITEM_SOUL_SAND:
        break;
    case ITEM_SOUL_SOIL:
        break;
    case ITEM_BASALT:
        break;
    case ITEM_POLISHED_BASALT:
        break;
    case ITEM_SOUL_TORCH:
        break;
    case ITEM_GLOWSTONE:
        break;
    case ITEM_JACK_O_LANTERN:
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
        break;
    case ITEM_INFESTED_COBBLESTONE:
        break;
    case ITEM_INFESTED_STONE_BRICKS:
        break;
    case ITEM_INFESTED_MOSSY_STONE_BRICKS:
        break;
    case ITEM_INFESTED_CRACKED_STONE_BRICKS:
        break;
    case ITEM_INFESTED_CHISELED_STONE_BRICKS:
        break;
    case ITEM_STONE_BRICKS:
        break;
    case ITEM_MOSSY_STONE_BRICKS:
        break;
    case ITEM_CRACKED_STONE_BRICKS:
        break;
    case ITEM_CHISELED_STONE_BRICKS:
        break;
    case ITEM_BROWN_MUSHROOM_BLOCK:
        break;
    case ITEM_RED_MUSHROOM_BLOCK:
        break;
    case ITEM_MUSHROOM_STEM:
        break;
    case ITEM_IRON_BARS:
        break;
    case ITEM_CHAIN:
        break;
    case ITEM_GLASS_PANE:
        break;
    case ITEM_MELON:
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
        break;
    case ITEM_LILY_PAD:
        break;
    case ITEM_NETHER_BRICKS:
        break;
    case ITEM_CRACKED_NETHER_BRICKS:
        break;
    case ITEM_CHISELED_NETHER_BRICKS:
        break;
    case ITEM_NETHER_BRICK_FENCE:
        break;
    case ITEM_NETHER_BRICK_STAIRS:
        break;
    case ITEM_ENCHANTING_TABLE:
        break;
    case ITEM_END_PORTAL_FRAME:
        break;
    case ITEM_END_STONE:
        break;
    case ITEM_END_STONE_BRICKS:
        break;
    case ITEM_DRAGON_EGG:
        break;
    case ITEM_REDSTONE_LAMP:
        break;
    case ITEM_SANDSTONE_STAIRS:
        break;
    case ITEM_EMERALD_ORE:
        break;
    case ITEM_ENDER_CHEST:
        break;
    case ITEM_TRIPWIRE_HOOK:
        break;
    case ITEM_EMERALD_BLOCK:
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
        break;
    case ITEM_NETHER_QUARTZ_ORE:
        break;
    case ITEM_HOPPER:
        break;
    case ITEM_CHISELED_QUARTZ_BLOCK:
        break;
    case ITEM_QUARTZ_BLOCK:
        break;
    case ITEM_QUARTZ_BRICKS:
        break;
    case ITEM_QUARTZ_PILLAR:
        break;
    case ITEM_QUARTZ_STAIRS:
        break;
    case ITEM_ACTIVATOR_RAIL:
        break;
    case ITEM_DROPPER:
        break;
    case ITEM_WHITE_TERRACOTTA:
        break;
    case ITEM_ORANGE_TERRACOTTA:
        break;
    case ITEM_MAGENTA_TERRACOTTA:
        break;
    case ITEM_LIGHT_BLUE_TERRACOTTA:
        break;
    case ITEM_YELLOW_TERRACOTTA:
        break;
    case ITEM_LIME_TERRACOTTA:
        break;
    case ITEM_PINK_TERRACOTTA:
        break;
    case ITEM_GRAY_TERRACOTTA:
        break;
    case ITEM_LIGHT_GRAY_TERRACOTTA:
        break;
    case ITEM_CYAN_TERRACOTTA:
        break;
    case ITEM_PURPLE_TERRACOTTA:
        break;
    case ITEM_BLUE_TERRACOTTA:
        break;
    case ITEM_BROWN_TERRACOTTA:
        break;
    case ITEM_GREEN_TERRACOTTA:
        break;
    case ITEM_RED_TERRACOTTA:
        break;
    case ITEM_BLACK_TERRACOTTA:
        break;
    case ITEM_BARRIER:
        break;
    case ITEM_IRON_TRAPDOOR:
        break;
    case ITEM_HAY_BLOCK:
        break;
    case ITEM_WHITE_CARPET:
        break;
    case ITEM_ORANGE_CARPET:
        break;
    case ITEM_MAGENTA_CARPET:
        break;
    case ITEM_LIGHT_BLUE_CARPET:
        break;
    case ITEM_YELLOW_CARPET:
        break;
    case ITEM_LIME_CARPET:
        break;
    case ITEM_PINK_CARPET:
        break;
    case ITEM_GRAY_CARPET:
        break;
    case ITEM_LIGHT_GRAY_CARPET:
        break;
    case ITEM_CYAN_CARPET:
        break;
    case ITEM_PURPLE_CARPET:
        break;
    case ITEM_BLUE_CARPET:
        break;
    case ITEM_BROWN_CARPET:
        break;
    case ITEM_GREEN_CARPET:
        break;
    case ITEM_RED_CARPET:
        break;
    case ITEM_BLACK_CARPET:
        break;
    case ITEM_TERRACOTTA:
        break;
    case ITEM_COAL_BLOCK:
        break;
    case ITEM_PACKED_ICE:
        break;
    case ITEM_ACACIA_STAIRS:
        break;
    case ITEM_DARK_OAK_STAIRS:
        break;
    case ITEM_SLIME_BLOCK:
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
        break;
    case ITEM_ORANGE_STAINED_GLASS:
        break;
    case ITEM_MAGENTA_STAINED_GLASS:
        break;
    case ITEM_LIGHT_BLUE_STAINED_GLASS:
        break;
    case ITEM_YELLOW_STAINED_GLASS:
        break;
    case ITEM_LIME_STAINED_GLASS:
        break;
    case ITEM_PINK_STAINED_GLASS:
        break;
    case ITEM_GRAY_STAINED_GLASS:
        break;
    case ITEM_LIGHT_GRAY_STAINED_GLASS:
        break;
    case ITEM_CYAN_STAINED_GLASS:
        break;
    case ITEM_PURPLE_STAINED_GLASS:
        break;
    case ITEM_BLUE_STAINED_GLASS:
        break;
    case ITEM_BROWN_STAINED_GLASS:
        break;
    case ITEM_GREEN_STAINED_GLASS:
        break;
    case ITEM_RED_STAINED_GLASS:
        break;
    case ITEM_BLACK_STAINED_GLASS:
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
        break;
    case ITEM_PRISMARINE_BRICKS:
        break;
    case ITEM_DARK_PRISMARINE:
        break;
    case ITEM_PRISMARINE_STAIRS:
        break;
    case ITEM_PRISMARINE_BRICK_STAIRS:
        break;
    case ITEM_DARK_PRISMARINE_STAIRS:
        break;
    case ITEM_SEA_LANTERN:
        break;
    case ITEM_RED_SANDSTONE:
        break;
    case ITEM_CHISELED_RED_SANDSTONE:
        break;
    case ITEM_CUT_RED_SANDSTONE:
        break;
    case ITEM_RED_SANDSTONE_STAIRS:
        break;
    case ITEM_REPEATING_COMMAND_BLOCK:
        break;
    case ITEM_CHAIN_COMMAND_BLOCK:
        break;
    case ITEM_MAGMA_BLOCK:
        break;
    case ITEM_NETHER_WART_BLOCK:
        break;
    case ITEM_WARPED_WART_BLOCK:
        break;
    case ITEM_RED_NETHER_BRICKS:
        break;
    case ITEM_BONE_BLOCK:
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
        break;
    case ITEM_ORANGE_CONCRETE:
        break;
    case ITEM_MAGENTA_CONCRETE:
        break;
    case ITEM_LIGHT_BLUE_CONCRETE:
        break;
    case ITEM_YELLOW_CONCRETE:
        break;
    case ITEM_LIME_CONCRETE:
        break;
    case ITEM_PINK_CONCRETE:
        break;
    case ITEM_GRAY_CONCRETE:
        break;
    case ITEM_LIGHT_GRAY_CONCRETE:
        break;
    case ITEM_CYAN_CONCRETE:
        break;
    case ITEM_PURPLE_CONCRETE:
        break;
    case ITEM_BLUE_CONCRETE:
        break;
    case ITEM_BROWN_CONCRETE:
        break;
    case ITEM_GREEN_CONCRETE:
        break;
    case ITEM_RED_CONCRETE:
        break;
    case ITEM_BLACK_CONCRETE:
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
        break;
    case ITEM_DEAD_BRAIN_CORAL_BLOCK:
        break;
    case ITEM_DEAD_BUBBLE_CORAL_BLOCK:
        break;
    case ITEM_DEAD_FIRE_CORAL_BLOCK:
        break;
    case ITEM_DEAD_HORN_CORAL_BLOCK:
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
        break;
    case ITEM_DEAD_BUBBLE_CORAL:
        break;
    case ITEM_DEAD_FIRE_CORAL:
        break;
    case ITEM_DEAD_HORN_CORAL:
        break;
    case ITEM_DEAD_TUBE_CORAL:
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
        break;
    case ITEM_DEAD_BRAIN_CORAL_FAN:
        break;
    case ITEM_DEAD_BUBBLE_CORAL_FAN:
        break;
    case ITEM_DEAD_FIRE_CORAL_FAN:
        break;
    case ITEM_DEAD_HORN_CORAL_FAN:
        break;
    case ITEM_BLUE_ICE:
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
        break;
    case ITEM_SMOOTH_RED_SANDSTONE_SLAB:
        break;
    case ITEM_MOSSY_STONE_BRICK_SLAB:
        break;
    case ITEM_POLISHED_DIORITE_SLAB:
        break;
    case ITEM_MOSSY_COBBLESTONE_SLAB:
        break;
    case ITEM_END_STONE_BRICK_SLAB:
        break;
    case ITEM_SMOOTH_SANDSTONE_SLAB:
        break;
    case ITEM_SMOOTH_QUARTZ_SLAB:
        break;
    case ITEM_GRANITE_SLAB:
        break;
    case ITEM_ANDESITE_SLAB:
        break;
    case ITEM_RED_NETHER_BRICK_SLAB:
        break;
    case ITEM_POLISHED_ANDESITE_SLAB:
        break;
    case ITEM_DIORITE_SLAB:
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
    case ITEM_TURTLE_HELMET:
        break;
    case ITEM_SCUTE:
        break;
    case ITEM_IRON_SHOVEL:
        break;
    case ITEM_IRON_PICKAXE:
        break;
    case ITEM_IRON_AXE:
        break;
    case ITEM_FLINT_AND_STEEL:
        break;
    case ITEM_APPLE:
        break;
    case ITEM_BOW:
        break;
    case ITEM_ARROW:
        break;
    case ITEM_COAL:
        break;
    case ITEM_CHARCOAL:
        break;
    case ITEM_DIAMOND:
        break;
    case ITEM_IRON_INGOT:
        break;
    case ITEM_GOLD_INGOT:
        break;
    case ITEM_NETHERITE_INGOT:
        break;
    case ITEM_NETHERITE_SCRAP:
        break;
    case ITEM_IRON_SWORD:
        break;
    case ITEM_WOODEN_SWORD:
        break;
    case ITEM_WOODEN_SHOVEL:
        break;
    case ITEM_WOODEN_PICKAXE:
        break;
    case ITEM_WOODEN_AXE:
        break;
    case ITEM_STONE_SWORD:
        break;
    case ITEM_STONE_SHOVEL:
        break;
    case ITEM_STONE_PICKAXE:
        break;
    case ITEM_STONE_AXE:
        break;
    case ITEM_DIAMOND_SWORD:
        break;
    case ITEM_DIAMOND_SHOVEL:
        break;
    case ITEM_DIAMOND_PICKAXE:
        break;
    case ITEM_DIAMOND_AXE:
        break;
    case ITEM_STICK:
        break;
    case ITEM_BOWL:
        break;
    case ITEM_MUSHROOM_STEW:
        break;
    case ITEM_GOLDEN_SWORD:
        break;
    case ITEM_GOLDEN_SHOVEL:
        break;
    case ITEM_GOLDEN_PICKAXE:
        break;
    case ITEM_GOLDEN_AXE:
        break;
    case ITEM_NETHERITE_SWORD:
        break;
    case ITEM_NETHERITE_SHOVEL:
        break;
    case ITEM_NETHERITE_PICKAXE:
        break;
    case ITEM_NETHERITE_AXE:
        break;
    case ITEM_STRING:
        break;
    case ITEM_FEATHER:
        break;
    case ITEM_GUNPOWDER:
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
        break;
    case ITEM_WHEAT:
        break;
    case ITEM_BREAD:
        break;
    case ITEM_LEATHER_HELMET:
        break;
    case ITEM_LEATHER_CHESTPLATE:
        break;
    case ITEM_LEATHER_LEGGINGS:
        break;
    case ITEM_LEATHER_BOOTS:
        break;
    case ITEM_CHAINMAIL_HELMET:
        break;
    case ITEM_CHAINMAIL_CHESTPLATE:
        break;
    case ITEM_CHAINMAIL_LEGGINGS:
        break;
    case ITEM_CHAINMAIL_BOOTS:
        break;
    case ITEM_IRON_HELMET:
        break;
    case ITEM_IRON_CHESTPLATE:
        break;
    case ITEM_IRON_LEGGINGS:
        break;
    case ITEM_IRON_BOOTS:
        break;
    case ITEM_DIAMOND_HELMET:
        break;
    case ITEM_DIAMOND_CHESTPLATE:
        break;
    case ITEM_DIAMOND_LEGGINGS:
        break;
    case ITEM_DIAMOND_BOOTS:
        break;
    case ITEM_GOLDEN_HELMET:
        break;
    case ITEM_GOLDEN_CHESTPLATE:
        break;
    case ITEM_GOLDEN_LEGGINGS:
        break;
    case ITEM_GOLDEN_BOOTS:
        break;
    case ITEM_NETHERITE_HELMET:
        break;
    case ITEM_NETHERITE_CHESTPLATE:
        break;
    case ITEM_NETHERITE_LEGGINGS:
        break;
    case ITEM_NETHERITE_BOOTS:
        break;
    case ITEM_FLINT:
        break;
    case ITEM_PORKCHOP:
        break;
    case ITEM_COOKED_PORKCHOP:
        break;
    case ITEM_PAINTING:
        break;
    case ITEM_GOLDEN_APPLE:
        break;
    case ITEM_ENCHANTED_GOLDEN_APPLE:
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
    case ITEM_BUCKET:
        break;
    case ITEM_WATER_BUCKET:
        break;
    case ITEM_LAVA_BUCKET:
        break;
    case ITEM_MINECART:
        break;
    case ITEM_SADDLE:
        break;
    case ITEM_REDSTONE:
        break;
    case ITEM_SNOWBALL:
        break;
    case ITEM_OAK_BOAT:
        break;
    case ITEM_LEATHER:
        break;
    case ITEM_MILK_BUCKET:
        break;
    case ITEM_PUFFERFISH_BUCKET:
        break;
    case ITEM_SALMON_BUCKET:
        break;
    case ITEM_COD_BUCKET:
        break;
    case ITEM_TROPICAL_FISH_BUCKET:
        break;
    case ITEM_BRICK:
        break;
    case ITEM_CLAY_BALL:
        break;
    case ITEM_DRIED_KELP_BLOCK:
        break;
    case ITEM_PAPER:
        break;
    case ITEM_BOOK:
        break;
    case ITEM_SLIME_BALL:
        break;
    case ITEM_CHEST_MINECART:
        break;
    case ITEM_FURNACE_MINECART:
        break;
    case ITEM_EGG:
        break;
    case ITEM_COMPASS:
        break;
    case ITEM_FISHING_ROD:
        break;
    case ITEM_CLOCK:
        break;
    case ITEM_GLOWSTONE_DUST:
        break;
    case ITEM_COD:
        break;
    case ITEM_SALMON:
        break;
    case ITEM_TROPICAL_FISH:
        break;
    case ITEM_PUFFERFISH:
        break;
    case ITEM_COOKED_COD:
        break;
    case ITEM_COOKED_SALMON:
        break;
    case ITEM_INK_SAC:
        break;
    case ITEM_RED_DYE:
        break;
    case ITEM_GREEN_DYE:
        break;
    case ITEM_COCOA_BEANS:
        break;
    case ITEM_LAPIS_LAZULI:
        break;
    case ITEM_PURPLE_DYE:
        break;
    case ITEM_CYAN_DYE:
        break;
    case ITEM_LIGHT_GRAY_DYE:
        break;
    case ITEM_GRAY_DYE:
        break;
    case ITEM_PINK_DYE:
        break;
    case ITEM_LIME_DYE:
        break;
    case ITEM_YELLOW_DYE:
        break;
    case ITEM_LIGHT_BLUE_DYE:
        break;
    case ITEM_MAGENTA_DYE:
        break;
    case ITEM_ORANGE_DYE:
        break;
    case ITEM_BONE_MEAL:
        break;
    case ITEM_BLUE_DYE:
        break;
    case ITEM_BROWN_DYE:
        break;
    case ITEM_BLACK_DYE:
        break;
    case ITEM_WHITE_DYE:
        break;
    case ITEM_BONE:
        break;
    case ITEM_SUGAR:
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
    case ITEM_COOKIE:
        break;
    case ITEM_FILLED_MAP:
        break;
    case ITEM_SHEARS:
        break;
    case ITEM_MELON_SLICE:
        break;
    case ITEM_DRIED_KELP:
        break;
    case ITEM_PUMPKIN_SEEDS:
        break;
    case ITEM_MELON_SEEDS:
        break;
    case ITEM_BEEF:
        break;
    case ITEM_COOKED_BEEF:
        break;
    case ITEM_CHICKEN:
        break;
    case ITEM_COOKED_CHICKEN:
        break;
    case ITEM_ROTTEN_FLESH:
        break;
    case ITEM_ENDER_PEARL:
        break;
    case ITEM_BLAZE_ROD:
        break;
    case ITEM_GHAST_TEAR:
        break;
    case ITEM_GOLD_NUGGET:
        break;
    case ITEM_NETHER_WART:
        break;
    case ITEM_POTION:
        break;
    case ITEM_GLASS_BOTTLE:
        break;
    case ITEM_SPIDER_EYE:
        break;
    case ITEM_FERMENTED_SPIDER_EYE:
        break;
    case ITEM_BLAZE_POWDER:
        break;
    case ITEM_MAGMA_CREAM:
        break;
    case ITEM_BREWING_STAND:
        break;
    case ITEM_CAULDRON:
        break;
    case ITEM_ENDER_EYE:
        break;
    case ITEM_GLISTERING_MELON_SLICE:
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
    case ITEM_EXPERIENCE_BOTTLE:
        break;
    case ITEM_FIRE_CHARGE:
        break;
    case ITEM_WRITABLE_BOOK:
        break;
    case ITEM_WRITTEN_BOOK:
        break;
    case ITEM_EMERALD:
        break;
    case ITEM_ITEM_FRAME:
        break;
    case ITEM_FLOWER_POT:
        break;
    case ITEM_CARROT:
        break;
    case ITEM_POTATO:
        break;
    case ITEM_BAKED_POTATO:
        break;
    case ITEM_POISONOUS_POTATO:
        break;
    case ITEM_MAP:
        break;
    case ITEM_GOLDEN_CARROT:
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
    case ITEM_CARROT_ON_A_STICK:
        break;
    case ITEM_WARPED_FUNGUS_ON_A_STICK:
        break;
    case ITEM_NETHER_STAR:
        break;
    case ITEM_PUMPKIN_PIE:
        break;
    case ITEM_FIREWORK_ROCKET:
        break;
    case ITEM_FIREWORK_STAR:
        break;
    case ITEM_ENCHANTED_BOOK:
        break;
    case ITEM_NETHER_BRICK:
        break;
    case ITEM_QUARTZ:
        break;
    case ITEM_TNT_MINECART:
        break;
    case ITEM_HOPPER_MINECART:
        break;
    case ITEM_PRISMARINE_SHARD:
        break;
    case ITEM_PRISMARINE_CRYSTALS:
        break;
    case ITEM_RABBIT:
        break;
    case ITEM_COOKED_RABBIT:
        break;
    case ITEM_RABBIT_STEW:
        break;
    case ITEM_RABBIT_FOOT:
        break;
    case ITEM_RABBIT_HIDE:
        break;
    case ITEM_ARMOR_STAND:
        break;
    case ITEM_IRON_HORSE_ARMOR:
        break;
    case ITEM_GOLDEN_HORSE_ARMOR:
        break;
    case ITEM_DIAMOND_HORSE_ARMOR:
        break;
    case ITEM_LEATHER_HORSE_ARMOR:
        break;
    case ITEM_LEAD:
        break;
    case ITEM_NAME_TAG:
        break;
    case ITEM_COMMAND_BLOCK_MINECART:
        break;
    case ITEM_MUTTON:
        break;
    case ITEM_COOKED_MUTTON:
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
    case ITEM_CHORUS_FRUIT:
        break;
    case ITEM_POPPED_CHORUS_FRUIT:
        break;
    case ITEM_BEETROOT:
        break;
    case ITEM_BEETROOT_SEEDS:
        break;
    case ITEM_BEETROOT_SOUP:
        break;
    case ITEM_DRAGON_BREATH:
        break;
    case ITEM_SPLASH_POTION:
        break;
    case ITEM_SPECTRAL_ARROW:
        break;
    case ITEM_TIPPED_ARROW:
        break;
    case ITEM_LINGERING_POTION:
        break;
    case ITEM_SHIELD:
        break;
    case ITEM_ELYTRA:
        break;
    case ITEM_SPRUCE_BOAT:
        break;
    case ITEM_BIRCH_BOAT:
        break;
    case ITEM_JUNGLE_BOAT:
        break;
    case ITEM_ACACIA_BOAT:
        break;
    case ITEM_DARK_OAK_BOAT:
        break;
    case ITEM_TOTEM_OF_UNDYING:
        break;
    case ITEM_SHULKER_SHELL:
        break;
    case ITEM_IRON_NUGGET:
        break;
    case ITEM_KNOWLEDGE_BOOK:
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
    case ITEM_TRIDENT:
        break;
    case ITEM_PHANTOM_MEMBRANE:
        break;
    case ITEM_NAUTILUS_SHELL:
        break;
    case ITEM_HEART_OF_THE_SEA:
        break;
    case ITEM_CROSSBOW:
        break;
    case ITEM_SUSPICIOUS_STEW:
        break;
    case ITEM_LOOM:
        break;
    case ITEM_FLOWER_BANNER_PATTERN:
        break;
    case ITEM_CREEPER_BANNER_PATTERN:
        break;
    case ITEM_SKULL_BANNER_PATTERN:
        break;
    case ITEM_MOJANG_BANNER_PATTERN:
        break;
    case ITEM_GLOBE_BANNER_PATTERN:
        break;
    case ITEM_PIGLIN_BANNER_PATTERN:
        break;
    case ITEM_COMPOSTER:
        break;
    case ITEM_BARREL:
        break;
    case ITEM_SMOKER:
        break;
    case ITEM_BLAST_FURNACE:
        break;
    case ITEM_CARTOGRAPHY_TABLE:
        break;
    case ITEM_FLETCHING_TABLE:
        break;
    case ITEM_GRINDSTONE:
        break;
    case ITEM_LECTERN:
        break;
    case ITEM_SMITHING_TABLE:
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
        break;
    case ITEM_CAMPFIRE:
        break;
    case ITEM_SOUL_CAMPFIRE:
        break;
    case ITEM_SHROOMLIGHT:
        break;
    case ITEM_HONEYCOMB:
        break;
    case ITEM_BEE_NEST:
        break;
    case ITEM_BEEHIVE:
        break;
    case ITEM_HONEY_BOTTLE:
        break;
    case ITEM_HONEY_BLOCK:
        break;
    case ITEM_HONEYCOMB_BLOCK:
        break;
    case ITEM_LODESTONE:
        break;
    case ITEM_NETHERITE_BLOCK:
        break;
    case ITEM_ANCIENT_DEBRIS:
        break;
    case ITEM_TARGET:
        break;
    case ITEM_CRYING_OBSIDIAN:
        break;
    case ITEM_BLACKSTONE:
        break;
    case ITEM_BLACKSTONE_SLAB:
        break;
    case ITEM_BLACKSTONE_STAIRS:
        break;
    case ITEM_GILDED_BLACKSTONE:
        break;
    case ITEM_POLISHED_BLACKSTONE:
        break;
    case ITEM_POLISHED_BLACKSTONE_SLAB:
        break;
    case ITEM_POLISHED_BLACKSTONE_STAIRS:
        break;
    case ITEM_CHISELED_POLISHED_BLACKSTONE:
        break;
    case ITEM_POLISHED_BLACKSTONE_BRICKS:
        break;
    case ITEM_POLISHED_BLACKSTONE_BRICK_SLAB:
        break;
    case ITEM_POLISHED_BLACKSTONE_BRICK_STAIRS:
        break;
    case ITEM_CRACKED_POLISHED_BLACKSTONE_BRICKS:
        break;
    case ITEM_RESPAWN_ANCHOR:
        break;
    default:
        assert(0);
    }
}

mc_ubyte
get_max_stack_size(mc_int item_type) {
    switch (item_type) {
    case ITEM_AIR:
        return 0;
    case ITEM_STONE:
    case ITEM_GRANITE:
    case ITEM_POLISHED_GRANITE:
    case ITEM_DIORITE:
    case ITEM_POLISHED_DIORITE:
    case ITEM_ANDESITE:
    case ITEM_POLISHED_ANDESITE:
    case ITEM_GRASS_BLOCK:
    case ITEM_DIRT:
    case ITEM_COARSE_DIRT:
    case ITEM_PODZOL:
    case ITEM_CRIMSON_NYLIUM:
    case ITEM_WARPED_NYLIUM:
    case ITEM_COBBLESTONE:
    case ITEM_OAK_PLANKS:
    case ITEM_SPRUCE_PLANKS:
    case ITEM_BIRCH_PLANKS:
    case ITEM_JUNGLE_PLANKS:
    case ITEM_ACACIA_PLANKS:
    case ITEM_DARK_OAK_PLANKS:
    case ITEM_CRIMSON_PLANKS:
    case ITEM_WARPED_PLANKS:
    case ITEM_OAK_SAPLING:
    case ITEM_SPRUCE_SAPLING:
    case ITEM_BIRCH_SAPLING:
    case ITEM_JUNGLE_SAPLING:
    case ITEM_ACACIA_SAPLING:
    case ITEM_DARK_OAK_SAPLING:
    case ITEM_BEDROCK:
    case ITEM_SAND:
    case ITEM_RED_SAND:
    case ITEM_GRAVEL:
        return 64;
    default:
        // @TODO(traks) implement other block types
        return 0;
    }
}