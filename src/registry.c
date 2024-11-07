#include <stdlib.h>
#include <stdarg.h>
#include "shared.h"

static i32 HashEntryName(String name) {
    u32 res = 0;
    for (i32 i = 0; i < name.size; i++) {
        res = res * 31 + (u32) name.data[i];
    }
    return res;
}

void SetRegistryName(Registry * registry, char * name) {
    String nameString = STR(name);
    assert(nameString.size <= (i32) ARRAY_SIZE(registry->name));
    memcpy(registry->name, name, nameString.size);
    registry->nameSize = nameString.size;
}

static i32 PushString(Registry * registry, String string) {
    assert(string.size <= REGISTRY_MAX_STRING_SIZE);
    i32 neededSize = string.size + 1;
    if (registry->stringPoolSize - registry->stringPoolCount < neededSize) {
        i32 newSize = MAX(2 * registry->stringPoolSize, 1024);
        u8 * newPool = malloc(newSize);
        memcpy(newPool, registry->stringPool, registry->stringPoolCount);
        free(registry->stringPool);
        registry->stringPoolSize = newSize;
        registry->stringPool = newPool;

        if (registry->stringPoolCount == 0) {
            // NOTE(traks): reserve string at index 0 as a null pointer
            registry->stringPoolCount++;
        }
        assert(newSize - registry->stringPoolCount >= neededSize);
    }

    i32 baseIndex = registry->stringPoolCount;
    u8 * next = &registry->stringPool[baseIndex];
    next[0] = string.size;
    memcpy(&next[1], string.data, string.size);
    registry->stringPoolCount += neededSize;
    return baseIndex;
}

String GetRegistryString(Registry * registry, i32 index) {
    // TODO(traks): return empty/null string instead of asserting?
    assert(index < registry->stringPoolCount);
    i32 size = registry->stringPool[index];
    assert(registry->stringPoolCount - size >= index + 1);
    String res = {
        .size = size,
        .data = &registry->stringPool[index + 1],
    };
    return res;
}

static RegistryHashEntry * FindEntryOrEmpty(Registry * registry, String name) {
    u32 hash = HashEntryName(name);
    for (i32 offset = 0; offset < registry->entryHashTableSize; offset++) {
        i32 entryIndex = (hash + (u32) offset) & registry->entryHashTableMask;
        RegistryHashEntry * hashEntry = registry->entryHashTable + entryIndex;
        if (hashEntry->nameIndex == 0 || StringEquals(name, GetRegistryString(registry, hashEntry->nameIndex))) {
            return hashEntry;
        }
    }
    assert(0);
    return NULL;
}

i32 AddRegistryEntry(Registry * registry, char * name) {
    if (registry->idToNameBufferSize <= registry->entryCount) {
        i32 oldSize = registry->idToNameBufferSize;
        i32 * oldBuffer = registry->idToNameBuffer;
        i32 newSize = MAX(2 * oldSize, 32);
        i32 * newBuffer = malloc(newSize * sizeof *newBuffer);
        memcpy(newBuffer, oldBuffer, oldSize * sizeof *newBuffer);
        free(oldBuffer);
        registry->idToNameBuffer = newBuffer;
        registry->idToNameBufferSize = newSize;
    }

    if (registry->entryHashTableSize / 2 <= registry->entryCount) {
        i32 oldSize = registry->entryHashTableSize;
        i32 oldMask = registry->entryHashTableMask;
        RegistryHashEntry * oldTable = registry->entryHashTable;
        i32 newSize = MAX(2 * oldSize, 32);
        RegistryHashEntry * newTable = calloc(1, newSize * sizeof *newTable);
        registry->entryHashTableSize = newSize;
        registry->entryHashTable = newTable;
        registry->entryHashTableMask = newSize - 1;
        for (i32 entryIndex = 0; entryIndex < oldSize; entryIndex++) {
            RegistryHashEntry * oldEntry = oldTable + entryIndex;
            if (oldEntry->nameIndex != 0) {
                RegistryHashEntry * newEntry = FindEntryOrEmpty(registry, GetRegistryString(registry, oldEntry->nameIndex));
                *newEntry = *oldEntry;
            }
        }
        free(oldTable);
    }

    String nameString = STR(name);
    i32 nameIndex = PushString(registry, nameString);
    i32 id = registry->entryCount;
    registry->entryCount++;
    registry->idToNameBuffer[id] = nameIndex;
    RegistryHashEntry * hashEntry = FindEntryOrEmpty(registry, nameString);
    assert(hashEntry->nameIndex == 0);
    *hashEntry = (RegistryHashEntry) {.nameIndex = nameIndex, .id = id};
    assert(ResolveRegistryEntryId(registry, nameString) == id);
    assert(StringEquals(ResolveRegistryEntryName(registry, id), nameString));
    return id;
}

void AddRegistryTag(Registry * registry, char * tagName, ...) {
    va_list ap;
    va_start(ap, tagName);

    i32 valueIdBuffer[512];
    i32 valueCount = 0;

    for (;;) {
        char * valueName = va_arg(ap, char *);
        if (valueName == NULL) {
            break;
        }
        assert(valueCount < (i32) ARRAY_SIZE(valueIdBuffer));
        i32 valueId = ResolveRegistryEntryId(registry, STR(valueName));
        assert(valueId != -1);
        valueIdBuffer[valueCount++] = valueId;
    }

    va_end(ap);

    if (registry->tagCount >= registry->tagBufferSize) {
        i32 oldSize = registry->tagBufferSize;
        RegistryTagInfo * oldBuffer = registry->tagBuffer;
        i32 newSize = MAX(2 * oldSize, 64);
        RegistryTagInfo * newBuffer = malloc(newSize * sizeof *newBuffer);
        memcpy(newBuffer, oldBuffer, oldSize * sizeof *newBuffer);
        free(oldBuffer);
        registry->tagBuffer = newBuffer;
        registry->tagBufferSize = newSize;
    }

    i32 neededSize = valueCount + 1;
    if (registry->tagValueBufferSize - registry->tagValueBufferCount < neededSize) {
        i32 newSize = MAX(2 * registry->tagValueBufferSize, 512);
        i32 * newBuffer = malloc(newSize * sizeof *newBuffer);
        memcpy(newBuffer, registry->tagValueBuffer, registry->tagValueBufferCount * sizeof *newBuffer);
        free(registry->tagValueBuffer);
        registry->tagValueBuffer = newBuffer;
        registry->tagValueBufferSize = newSize;
        assert(newSize - registry->tagValueBufferCount >= neededSize);
    }

    i32 valueIndex = registry->tagValueBufferCount;
    i32 * targetValueBuffer = registry->tagValueBuffer + valueIndex;
    targetValueBuffer[0] = valueCount;
    memcpy(&targetValueBuffer[1], valueIdBuffer, valueCount * sizeof *valueIdBuffer);
    registry->tagValueBufferCount += neededSize;
    i32 nameIndex = PushString(registry, STR(tagName));
    registry->tagBuffer[registry->tagCount++] = (RegistryTagInfo) {.nameIndex = nameIndex, .valueIndex = valueIndex};
}

static void InitEntityTypeRegistry(void) {
    Registry * registry = &serv->entityTypeRegistry;
    SetRegistryName(registry, "minecraft:entity_type");

    AddRegistryEntry(registry, "minecraft:acacia_boat");
    AddRegistryEntry(registry, "minecraft:acacia_chest_boat");
    AddRegistryEntry(registry, "minecraft:allay");
    AddRegistryEntry(registry, "minecraft:area_effect_cloud");
    AddRegistryEntry(registry, "minecraft:armadillo");
    AddRegistryEntry(registry, "minecraft:armor_stand");
    AddRegistryEntry(registry, "minecraft:arrow");
    AddRegistryEntry(registry, "minecraft:axolotl");
    AddRegistryEntry(registry, "minecraft:bamboo_chest_raft");
    AddRegistryEntry(registry, "minecraft:bamboo_raft");
    AddRegistryEntry(registry, "minecraft:bat");
    AddRegistryEntry(registry, "minecraft:bee");
    AddRegistryEntry(registry, "minecraft:birch_boat");
    AddRegistryEntry(registry, "minecraft:birch_chest_boat");
    AddRegistryEntry(registry, "minecraft:blaze");
    AddRegistryEntry(registry, "minecraft:block_display");
    AddRegistryEntry(registry, "minecraft:bogged");
    AddRegistryEntry(registry, "minecraft:breeze");
    AddRegistryEntry(registry, "minecraft:breeze_wind_charge");
    AddRegistryEntry(registry, "minecraft:camel");
    AddRegistryEntry(registry, "minecraft:cat");
    AddRegistryEntry(registry, "minecraft:cave_spider");
    AddRegistryEntry(registry, "minecraft:cherry_boat");
    AddRegistryEntry(registry, "minecraft:cherry_chest_boat");
    AddRegistryEntry(registry, "minecraft:chest_minecart");
    AddRegistryEntry(registry, "minecraft:chicken");
    AddRegistryEntry(registry, "minecraft:cod");
    AddRegistryEntry(registry, "minecraft:command_block_minecart");
    AddRegistryEntry(registry, "minecraft:cow");
    AddRegistryEntry(registry, "minecraft:creaking");
    AddRegistryEntry(registry, "minecraft:creaking_transient");
    AddRegistryEntry(registry, "minecraft:creeper");
    AddRegistryEntry(registry, "minecraft:dark_oak_boat");
    AddRegistryEntry(registry, "minecraft:dark_oak_chest_boat");
    AddRegistryEntry(registry, "minecraft:dolphin");
    AddRegistryEntry(registry, "minecraft:donkey");
    AddRegistryEntry(registry, "minecraft:dragon_fireball");
    AddRegistryEntry(registry, "minecraft:drowned");
    AddRegistryEntry(registry, "minecraft:egg");
    AddRegistryEntry(registry, "minecraft:elder_guardian");
    AddRegistryEntry(registry, "minecraft:enderman");
    AddRegistryEntry(registry, "minecraft:endermite");
    AddRegistryEntry(registry, "minecraft:ender_dragon");
    AddRegistryEntry(registry, "minecraft:ender_pearl");
    AddRegistryEntry(registry, "minecraft:end_crystal");
    AddRegistryEntry(registry, "minecraft:evoker");
    AddRegistryEntry(registry, "minecraft:evoker_fangs");
    AddRegistryEntry(registry, "minecraft:experience_bottle");
    AddRegistryEntry(registry, "minecraft:experience_orb");
    AddRegistryEntry(registry, "minecraft:eye_of_ender");
    AddRegistryEntry(registry, "minecraft:falling_block");
    AddRegistryEntry(registry, "minecraft:fireball");
    AddRegistryEntry(registry, "minecraft:firework_rocket");
    AddRegistryEntry(registry, "minecraft:fox");
    AddRegistryEntry(registry, "minecraft:frog");
    AddRegistryEntry(registry, "minecraft:furnace_minecart");
    AddRegistryEntry(registry, "minecraft:ghast");
    AddRegistryEntry(registry, "minecraft:giant");
    AddRegistryEntry(registry, "minecraft:glow_item_frame");
    AddRegistryEntry(registry, "minecraft:glow_squid");
    AddRegistryEntry(registry, "minecraft:goat");
    AddRegistryEntry(registry, "minecraft:guardian");
    AddRegistryEntry(registry, "minecraft:hoglin");
    AddRegistryEntry(registry, "minecraft:hopper_minecart");
    AddRegistryEntry(registry, "minecraft:horse");
    AddRegistryEntry(registry, "minecraft:husk");
    AddRegistryEntry(registry, "minecraft:illusioner");
    AddRegistryEntry(registry, "minecraft:interaction");
    AddRegistryEntry(registry, "minecraft:iron_golem");
    AddRegistryEntry(registry, "minecraft:item");
    AddRegistryEntry(registry, "minecraft:item_display");
    AddRegistryEntry(registry, "minecraft:item_frame");
    AddRegistryEntry(registry, "minecraft:jungle_boat");
    AddRegistryEntry(registry, "minecraft:jungle_chest_boat");
    AddRegistryEntry(registry, "minecraft:leash_knot");
    AddRegistryEntry(registry, "minecraft:lightning_bolt");
    AddRegistryEntry(registry, "minecraft:llama");
    AddRegistryEntry(registry, "minecraft:llama_spit");
    AddRegistryEntry(registry, "minecraft:magma_cube");
    AddRegistryEntry(registry, "minecraft:mangrove_boat");
    AddRegistryEntry(registry, "minecraft:mangrove_chest_boat");
    AddRegistryEntry(registry, "minecraft:marker");
    AddRegistryEntry(registry, "minecraft:minecart");
    AddRegistryEntry(registry, "minecraft:mooshroom");
    AddRegistryEntry(registry, "minecraft:mule");
    AddRegistryEntry(registry, "minecraft:oak_boat");
    AddRegistryEntry(registry, "minecraft:oak_chest_boat");
    AddRegistryEntry(registry, "minecraft:ocelot");
    AddRegistryEntry(registry, "minecraft:ominous_item_spawner");
    AddRegistryEntry(registry, "minecraft:painting");
    AddRegistryEntry(registry, "minecraft:pale_oak_boat");
    AddRegistryEntry(registry, "minecraft:pale_oak_chest_boat");
    AddRegistryEntry(registry, "minecraft:panda");
    AddRegistryEntry(registry, "minecraft:parrot");
    AddRegistryEntry(registry, "minecraft:phantom");
    AddRegistryEntry(registry, "minecraft:pig");
    AddRegistryEntry(registry, "minecraft:piglin");
    AddRegistryEntry(registry, "minecraft:piglin_brute");
    AddRegistryEntry(registry, "minecraft:pillager");
    AddRegistryEntry(registry, "minecraft:polar_bear");
    AddRegistryEntry(registry, "minecraft:potion");
    AddRegistryEntry(registry, "minecraft:pufferfish");
    AddRegistryEntry(registry, "minecraft:rabbit");
    AddRegistryEntry(registry, "minecraft:ravager");
    AddRegistryEntry(registry, "minecraft:salmon");
    AddRegistryEntry(registry, "minecraft:sheep");
    AddRegistryEntry(registry, "minecraft:shulker");
    AddRegistryEntry(registry, "minecraft:shulker_bullet");
    AddRegistryEntry(registry, "minecraft:silverfish");
    AddRegistryEntry(registry, "minecraft:skeleton");
    AddRegistryEntry(registry, "minecraft:skeleton_horse");
    AddRegistryEntry(registry, "minecraft:slime");
    AddRegistryEntry(registry, "minecraft:small_fireball");
    AddRegistryEntry(registry, "minecraft:sniffer");
    AddRegistryEntry(registry, "minecraft:snowball");
    AddRegistryEntry(registry, "minecraft:snow_golem");
    AddRegistryEntry(registry, "minecraft:spawner_minecart");
    AddRegistryEntry(registry, "minecraft:spectral_arrow");
    AddRegistryEntry(registry, "minecraft:spider");
    AddRegistryEntry(registry, "minecraft:spruce_boat");
    AddRegistryEntry(registry, "minecraft:spruce_chest_boat");
    AddRegistryEntry(registry, "minecraft:squid");
    AddRegistryEntry(registry, "minecraft:stray");
    AddRegistryEntry(registry, "minecraft:strider");
    AddRegistryEntry(registry, "minecraft:tadpole");
    AddRegistryEntry(registry, "minecraft:text_display");
    AddRegistryEntry(registry, "minecraft:tnt");
    AddRegistryEntry(registry, "minecraft:tnt_minecart");
    AddRegistryEntry(registry, "minecraft:trader_llama");
    AddRegistryEntry(registry, "minecraft:trident");
    AddRegistryEntry(registry, "minecraft:tropical_fish");
    AddRegistryEntry(registry, "minecraft:turtle");
    AddRegistryEntry(registry, "minecraft:vex");
    AddRegistryEntry(registry, "minecraft:villager");
    AddRegistryEntry(registry, "minecraft:vindicator");
    AddRegistryEntry(registry, "minecraft:wandering_trader");
    AddRegistryEntry(registry, "minecraft:warden");
    AddRegistryEntry(registry, "minecraft:wind_charge");
    AddRegistryEntry(registry, "minecraft:witch");
    AddRegistryEntry(registry, "minecraft:wither");
    AddRegistryEntry(registry, "minecraft:wither_skeleton");
    AddRegistryEntry(registry, "minecraft:wither_skull");
    AddRegistryEntry(registry, "minecraft:wolf");
    AddRegistryEntry(registry, "minecraft:zoglin");
    AddRegistryEntry(registry, "minecraft:zombie");
    AddRegistryEntry(registry, "minecraft:zombie_horse");
    AddRegistryEntry(registry, "minecraft:zombie_villager");
    AddRegistryEntry(registry, "minecraft:zombified_piglin");
    AddRegistryEntry(registry, "minecraft:player");
    AddRegistryEntry(registry, "minecraft:fishing_bobber");
    AddRegistryEntry(registry, "blaze:null");

    AddRegistryTag(registry, "minecraft:raiders", "minecraft:evoker", "minecraft:pillager", "minecraft:ravager", "minecraft:vindicator", "minecraft:illusioner", "minecraft:witch", NULL);
    AddRegistryTag(registry, "minecraft:inverted_healing_and_harm", "minecraft:skeleton", "minecraft:stray", "minecraft:wither_skeleton", "minecraft:skeleton_horse", "minecraft:bogged", "minecraft:zombie_horse", "minecraft:zombie", "minecraft:zombie_villager", "minecraft:zombified_piglin", "minecraft:zoglin", "minecraft:drowned", "minecraft:husk", "minecraft:wither", "minecraft:phantom", NULL);
    AddRegistryTag(registry, "minecraft:arthropod", "minecraft:bee", "minecraft:endermite", "minecraft:silverfish", "minecraft:spider", "minecraft:cave_spider", NULL);
    AddRegistryTag(registry, "minecraft:sensitive_to_impaling", "minecraft:turtle", "minecraft:axolotl", "minecraft:guardian", "minecraft:elder_guardian", "minecraft:cod", "minecraft:pufferfish", "minecraft:salmon", "minecraft:tropical_fish", "minecraft:dolphin", "minecraft:squid", "minecraft:glow_squid", "minecraft:tadpole", NULL);
    AddRegistryTag(registry, "minecraft:immune_to_infested", "minecraft:silverfish", NULL);
    AddRegistryTag(registry, "minecraft:can_turn_in_boats", "minecraft:breeze", NULL);
    AddRegistryTag(registry, "minecraft:freeze_hurts_extra_types", "minecraft:strider", "minecraft:blaze", "minecraft:magma_cube", NULL);
    AddRegistryTag(registry, "minecraft:can_breathe_under_water", "minecraft:skeleton", "minecraft:stray", "minecraft:wither_skeleton", "minecraft:skeleton_horse", "minecraft:bogged", "minecraft:zombie_horse", "minecraft:zombie", "minecraft:zombie_villager", "minecraft:zombified_piglin", "minecraft:zoglin", "minecraft:drowned", "minecraft:husk", "minecraft:wither", "minecraft:phantom", "minecraft:axolotl", "minecraft:frog", "minecraft:guardian", "minecraft:elder_guardian", "minecraft:turtle", "minecraft:glow_squid", "minecraft:cod", "minecraft:pufferfish", "minecraft:salmon", "minecraft:squid", "minecraft:tropical_fish", "minecraft:tadpole", "minecraft:armor_stand", NULL);
    AddRegistryTag(registry, "minecraft:ignores_poison_and_regen", "minecraft:skeleton", "minecraft:stray", "minecraft:wither_skeleton", "minecraft:skeleton_horse", "minecraft:bogged", "minecraft:zombie_horse", "minecraft:zombie", "minecraft:zombie_villager", "minecraft:zombified_piglin", "minecraft:zoglin", "minecraft:drowned", "minecraft:husk", "minecraft:wither", "minecraft:phantom", NULL);
    AddRegistryTag(registry, "minecraft:freeze_immune_entity_types", "minecraft:stray", "minecraft:polar_bear", "minecraft:snow_golem", "minecraft:wither", NULL);
    AddRegistryTag(registry, "minecraft:redirectable_projectile", "minecraft:fireball", "minecraft:wind_charge", "minecraft:breeze_wind_charge", NULL);
    AddRegistryTag(registry, "minecraft:immune_to_oozing", "minecraft:slime", NULL);
    AddRegistryTag(registry, "minecraft:undead", "minecraft:skeleton", "minecraft:stray", "minecraft:wither_skeleton", "minecraft:skeleton_horse", "minecraft:bogged", "minecraft:zombie_horse", "minecraft:zombie", "minecraft:zombie_villager", "minecraft:zombified_piglin", "minecraft:zoglin", "minecraft:drowned", "minecraft:husk", "minecraft:wither", "minecraft:phantom", NULL);
    AddRegistryTag(registry, "minecraft:frog_food", "minecraft:slime", "minecraft:magma_cube", NULL);
    AddRegistryTag(registry, "minecraft:sensitive_to_bane_of_arthropods", "minecraft:bee", "minecraft:endermite", "minecraft:silverfish", "minecraft:spider", "minecraft:cave_spider", NULL);
    AddRegistryTag(registry, "minecraft:not_scary_for_pufferfish", "minecraft:turtle", "minecraft:guardian", "minecraft:elder_guardian", "minecraft:cod", "minecraft:pufferfish", "minecraft:salmon", "minecraft:tropical_fish", "minecraft:dolphin", "minecraft:squid", "minecraft:glow_squid", "minecraft:tadpole", NULL);
    AddRegistryTag(registry, "minecraft:zombies", "minecraft:zombie_horse", "minecraft:zombie", "minecraft:zombie_villager", "minecraft:zombified_piglin", "minecraft:zoglin", "minecraft:drowned", "minecraft:husk", NULL);
    AddRegistryTag(registry, "minecraft:powder_snow_walkable_mobs", "minecraft:rabbit", "minecraft:endermite", "minecraft:silverfish", "minecraft:fox", NULL);
    AddRegistryTag(registry, "minecraft:skeletons", "minecraft:skeleton", "minecraft:stray", "minecraft:wither_skeleton", "minecraft:skeleton_horse", "minecraft:bogged", NULL);
    AddRegistryTag(registry, "minecraft:sensitive_to_smite", "minecraft:skeleton", "minecraft:stray", "minecraft:wither_skeleton", "minecraft:skeleton_horse", "minecraft:bogged", "minecraft:zombie_horse", "minecraft:zombie", "minecraft:zombie_villager", "minecraft:zombified_piglin", "minecraft:zoglin", "minecraft:drowned", "minecraft:husk", "minecraft:wither", "minecraft:phantom", NULL);
    AddRegistryTag(registry, "minecraft:non_controlling_rider", "minecraft:slime", "minecraft:magma_cube", NULL);
    AddRegistryTag(registry, "minecraft:axolotl_hunt_targets", "minecraft:tropical_fish", "minecraft:pufferfish", "minecraft:salmon", "minecraft:cod", "minecraft:squid", "minecraft:glow_squid", "minecraft:tadpole", NULL);
    AddRegistryTag(registry, "minecraft:wither_friends", "minecraft:skeleton", "minecraft:stray", "minecraft:wither_skeleton", "minecraft:skeleton_horse", "minecraft:bogged", "minecraft:zombie_horse", "minecraft:zombie", "minecraft:zombie_villager", "minecraft:zombified_piglin", "minecraft:zoglin", "minecraft:drowned", "minecraft:husk", "minecraft:wither", "minecraft:phantom", NULL);
    AddRegistryTag(registry, "minecraft:deflects_projectiles", "minecraft:breeze", NULL);
    AddRegistryTag(registry, "minecraft:illager_friends", "minecraft:evoker", "minecraft:illusioner", "minecraft:pillager", "minecraft:vindicator", NULL);
    AddRegistryTag(registry, "minecraft:impact_projectiles", "minecraft:arrow", "minecraft:spectral_arrow", "minecraft:firework_rocket", "minecraft:snowball", "minecraft:fireball", "minecraft:small_fireball", "minecraft:egg", "minecraft:trident", "minecraft:dragon_fireball", "minecraft:wither_skull", "minecraft:wind_charge", "minecraft:breeze_wind_charge", NULL);
    AddRegistryTag(registry, "minecraft:aquatic", "minecraft:turtle", "minecraft:axolotl", "minecraft:guardian", "minecraft:elder_guardian", "minecraft:cod", "minecraft:pufferfish", "minecraft:salmon", "minecraft:tropical_fish", "minecraft:dolphin", "minecraft:squid", "minecraft:glow_squid", "minecraft:tadpole", NULL);
    AddRegistryTag(registry, "minecraft:dismounts_underwater", "minecraft:camel", "minecraft:chicken", "minecraft:donkey", "minecraft:horse", "minecraft:llama", "minecraft:mule", "minecraft:pig", "minecraft:ravager", "minecraft:spider", "minecraft:strider", "minecraft:trader_llama", "minecraft:zombie_horse", NULL);
    AddRegistryTag(registry, "minecraft:axolotl_always_hostiles", "minecraft:drowned", "minecraft:guardian", "minecraft:elder_guardian", NULL);
    AddRegistryTag(registry, "minecraft:beehive_inhabitors", "minecraft:bee", NULL);
    AddRegistryTag(registry, "minecraft:fall_damage_immune", "minecraft:iron_golem", "minecraft:snow_golem", "minecraft:shulker", "minecraft:allay", "minecraft:bat", "minecraft:bee", "minecraft:blaze", "minecraft:cat", "minecraft:chicken", "minecraft:ghast", "minecraft:phantom", "minecraft:magma_cube", "minecraft:ocelot", "minecraft:parrot", "minecraft:wither", "minecraft:breeze", NULL);
    AddRegistryTag(registry, "minecraft:boat", "minecraft:oak_boat", "minecraft:spruce_boat", "minecraft:birch_boat", "minecraft:jungle_boat", "minecraft:acacia_boat", "minecraft:cherry_boat", "minecraft:dark_oak_boat", "minecraft:mangrove_boat", "minecraft:bamboo_raft", NULL);
    AddRegistryTag(registry, "minecraft:arrows", "minecraft:arrow", "minecraft:spectral_arrow", NULL);
    AddRegistryTag(registry, "minecraft:no_anger_from_wind_charge", "minecraft:breeze", "minecraft:skeleton", "minecraft:bogged", "minecraft:stray", "minecraft:zombie", "minecraft:husk", "minecraft:spider", "minecraft:cave_spider", "minecraft:slime", NULL);
    AddRegistryTag(registry, "minecraft:illager", "minecraft:evoker", "minecraft:illusioner", "minecraft:pillager", "minecraft:vindicator", NULL);
}

static void InitFluidRegistry(void) {
    Registry * registry = &serv->fluidRegistry;
    SetRegistryName(registry, "minecraft:fluid");

    AddRegistryEntry(registry, "minecraft:empty");
    AddRegistryEntry(registry, "minecraft:flowing_water");
    AddRegistryEntry(registry, "minecraft:water");
    AddRegistryEntry(registry, "minecraft:flowing_lava");
    AddRegistryEntry(registry, "minecraft:lava");

    AddRegistryTag(registry, "minecraft:water", "minecraft:water", "minecraft:flowing_water", NULL);
    AddRegistryTag(registry, "minecraft:lava", "minecraft:lava", "minecraft:flowing_lava", NULL);
}

static void InitGameEventRegistry(void) {
    Registry * registry = &serv->gameEventRegistry;
    SetRegistryName(registry, "minecraft:game_event");

    AddRegistryEntry(registry, "minecraft:block_activate");
    AddRegistryEntry(registry, "minecraft:block_attach");
    AddRegistryEntry(registry, "minecraft:block_change");
    AddRegistryEntry(registry, "minecraft:block_close");
    AddRegistryEntry(registry, "minecraft:block_deactivate");
    AddRegistryEntry(registry, "minecraft:block_destroy");
    AddRegistryEntry(registry, "minecraft:block_detach");
    AddRegistryEntry(registry, "minecraft:block_open");
    AddRegistryEntry(registry, "minecraft:block_place");
    AddRegistryEntry(registry, "minecraft:container_close");
    AddRegistryEntry(registry, "minecraft:container_open");
    AddRegistryEntry(registry, "minecraft:drink");
    AddRegistryEntry(registry, "minecraft:eat");
    AddRegistryEntry(registry, "minecraft:elytra_glide");
    AddRegistryEntry(registry, "minecraft:entity_damage");
    AddRegistryEntry(registry, "minecraft:entity_die");
    AddRegistryEntry(registry, "minecraft:entity_dismount");
    AddRegistryEntry(registry, "minecraft:entity_interact");
    AddRegistryEntry(registry, "minecraft:entity_mount");
    AddRegistryEntry(registry, "minecraft:entity_place");
    AddRegistryEntry(registry, "minecraft:entity_action");
    AddRegistryEntry(registry, "minecraft:equip");
    AddRegistryEntry(registry, "minecraft:explode");
    AddRegistryEntry(registry, "minecraft:flap");
    AddRegistryEntry(registry, "minecraft:fluid_pickup");
    AddRegistryEntry(registry, "minecraft:fluid_place");
    AddRegistryEntry(registry, "minecraft:hit_ground");
    AddRegistryEntry(registry, "minecraft:instrument_play");
    AddRegistryEntry(registry, "minecraft:item_interact_finish");
    AddRegistryEntry(registry, "minecraft:item_interact_start");
    AddRegistryEntry(registry, "minecraft:jukebox_play");
    AddRegistryEntry(registry, "minecraft:jukebox_stop_play");
    AddRegistryEntry(registry, "minecraft:lightning_strike");
    AddRegistryEntry(registry, "minecraft:note_block_play");
    AddRegistryEntry(registry, "minecraft:prime_fuse");
    AddRegistryEntry(registry, "minecraft:projectile_land");
    AddRegistryEntry(registry, "minecraft:projectile_shoot");
    AddRegistryEntry(registry, "minecraft:sculk_sensor_tendrils_clicking");
    AddRegistryEntry(registry, "minecraft:shear");
    AddRegistryEntry(registry, "minecraft:shriek");
    AddRegistryEntry(registry, "minecraft:splash");
    AddRegistryEntry(registry, "minecraft:step");
    AddRegistryEntry(registry, "minecraft:swim");
    AddRegistryEntry(registry, "minecraft:teleport");
    AddRegistryEntry(registry, "minecraft:unequip");
    AddRegistryEntry(registry, "minecraft:resonate_1");
    AddRegistryEntry(registry, "minecraft:resonate_2");
    AddRegistryEntry(registry, "minecraft:resonate_3");
    AddRegistryEntry(registry, "minecraft:resonate_4");
    AddRegistryEntry(registry, "minecraft:resonate_5");
    AddRegistryEntry(registry, "minecraft:resonate_6");
    AddRegistryEntry(registry, "minecraft:resonate_7");
    AddRegistryEntry(registry, "minecraft:resonate_8");
    AddRegistryEntry(registry, "minecraft:resonate_9");
    AddRegistryEntry(registry, "minecraft:resonate_10");
    AddRegistryEntry(registry, "minecraft:resonate_11");
    AddRegistryEntry(registry, "minecraft:resonate_12");
    AddRegistryEntry(registry, "minecraft:resonate_13");
    AddRegistryEntry(registry, "minecraft:resonate_14");
    AddRegistryEntry(registry, "minecraft:resonate_15");

    AddRegistryTag(registry, "minecraft:shrieker_can_listen", "minecraft:sculk_sensor_tendrils_clicking", NULL);
    AddRegistryTag(registry, "minecraft:ignore_vibrations_sneaking", "minecraft:hit_ground", "minecraft:projectile_shoot", "minecraft:step", "minecraft:swim", "minecraft:item_interact_start", "minecraft:item_interact_finish", NULL);
    AddRegistryTag(registry, "minecraft:vibrations", "minecraft:block_attach", "minecraft:block_change", "minecraft:block_close", "minecraft:block_destroy", "minecraft:block_detach", "minecraft:block_open", "minecraft:block_place", "minecraft:block_activate", "minecraft:block_deactivate", "minecraft:container_close", "minecraft:container_open", "minecraft:drink", "minecraft:eat", "minecraft:elytra_glide", "minecraft:entity_damage", "minecraft:entity_die", "minecraft:entity_dismount", "minecraft:entity_interact", "minecraft:entity_mount", "minecraft:entity_place", "minecraft:entity_action", "minecraft:equip", "minecraft:explode", "minecraft:fluid_pickup", "minecraft:fluid_place", "minecraft:hit_ground", "minecraft:instrument_play", "minecraft:item_interact_finish", "minecraft:lightning_strike", "minecraft:note_block_play", "minecraft:prime_fuse", "minecraft:projectile_land", "minecraft:projectile_shoot", "minecraft:shear", "minecraft:splash", "minecraft:step", "minecraft:swim", "minecraft:teleport", "minecraft:unequip", "minecraft:resonate_1", "minecraft:resonate_2", "minecraft:resonate_3", "minecraft:resonate_4", "minecraft:resonate_5", "minecraft:resonate_6", "minecraft:resonate_7", "minecraft:resonate_8", "minecraft:resonate_9", "minecraft:resonate_10", "minecraft:resonate_11", "minecraft:resonate_12", "minecraft:resonate_13", "minecraft:resonate_14", "minecraft:resonate_15", "minecraft:flap", NULL);
    AddRegistryTag(registry, "minecraft:allay_can_listen", "minecraft:note_block_play", NULL);
    AddRegistryTag(registry, "minecraft:warden_can_listen", "minecraft:block_attach", "minecraft:block_change", "minecraft:block_close", "minecraft:block_destroy", "minecraft:block_detach", "minecraft:block_open", "minecraft:block_place", "minecraft:block_activate", "minecraft:block_deactivate", "minecraft:container_close", "minecraft:container_open", "minecraft:drink", "minecraft:eat", "minecraft:elytra_glide", "minecraft:entity_damage", "minecraft:entity_die", "minecraft:entity_dismount", "minecraft:entity_interact", "minecraft:entity_mount", "minecraft:entity_place", "minecraft:entity_action", "minecraft:equip", "minecraft:explode", "minecraft:fluid_pickup", "minecraft:fluid_place", "minecraft:hit_ground", "minecraft:instrument_play", "minecraft:item_interact_finish", "minecraft:lightning_strike", "minecraft:note_block_play", "minecraft:prime_fuse", "minecraft:projectile_land", "minecraft:projectile_shoot", "minecraft:shear", "minecraft:splash", "minecraft:step", "minecraft:swim", "minecraft:teleport", "minecraft:unequip", "minecraft:resonate_1", "minecraft:resonate_2", "minecraft:resonate_3", "minecraft:resonate_4", "minecraft:resonate_5", "minecraft:resonate_6", "minecraft:resonate_7", "minecraft:resonate_8", "minecraft:resonate_9", "minecraft:resonate_10", "minecraft:resonate_11", "minecraft:resonate_12", "minecraft:resonate_13", "minecraft:resonate_14", "minecraft:resonate_15", "minecraft:shriek", "minecraft:sculk_sensor_tendrils_clicking", NULL);
}

static void InitBiomeRegistry(void) {
    Registry * registry = &serv->biomeRegistry;
    SetRegistryName(registry, "minecraft:worldgen/biome");
    registry->sendEntriesToClients = 1;

    AddRegistryEntry(registry, "minecraft:badlands");
    AddRegistryEntry(registry, "minecraft:bamboo_jungle");
    AddRegistryEntry(registry, "minecraft:basalt_deltas");
    AddRegistryEntry(registry, "minecraft:beach");
    AddRegistryEntry(registry, "minecraft:birch_forest");
    AddRegistryEntry(registry, "minecraft:cherry_grove");
    AddRegistryEntry(registry, "minecraft:cold_ocean");
    AddRegistryEntry(registry, "minecraft:crimson_forest");
    AddRegistryEntry(registry, "minecraft:dark_forest");
    AddRegistryEntry(registry, "minecraft:deep_cold_ocean");
    AddRegistryEntry(registry, "minecraft:deep_dark");
    AddRegistryEntry(registry, "minecraft:deep_frozen_ocean");
    AddRegistryEntry(registry, "minecraft:deep_lukewarm_ocean");
    AddRegistryEntry(registry, "minecraft:deep_ocean");
    AddRegistryEntry(registry, "minecraft:desert");
    AddRegistryEntry(registry, "minecraft:dripstone_caves");
    AddRegistryEntry(registry, "minecraft:end_barrens");
    AddRegistryEntry(registry, "minecraft:end_highlands");
    AddRegistryEntry(registry, "minecraft:end_midlands");
    AddRegistryEntry(registry, "minecraft:eroded_badlands");
    AddRegistryEntry(registry, "minecraft:flower_forest");
    AddRegistryEntry(registry, "minecraft:forest");
    AddRegistryEntry(registry, "minecraft:frozen_ocean");
    AddRegistryEntry(registry, "minecraft:frozen_peaks");
    AddRegistryEntry(registry, "minecraft:frozen_river");
    AddRegistryEntry(registry, "minecraft:grove");
    AddRegistryEntry(registry, "minecraft:ice_spikes");
    AddRegistryEntry(registry, "minecraft:jagged_peaks");
    AddRegistryEntry(registry, "minecraft:jungle");
    AddRegistryEntry(registry, "minecraft:lukewarm_ocean");
    AddRegistryEntry(registry, "minecraft:lush_caves");
    AddRegistryEntry(registry, "minecraft:mangrove_swamp");
    AddRegistryEntry(registry, "minecraft:meadow");
    AddRegistryEntry(registry, "minecraft:mushroom_fields");
    AddRegistryEntry(registry, "minecraft:nether_wastes");
    AddRegistryEntry(registry, "minecraft:ocean");
    AddRegistryEntry(registry, "minecraft:old_growth_birch_forest");
    AddRegistryEntry(registry, "minecraft:old_growth_pine_taiga");
    AddRegistryEntry(registry, "minecraft:old_growth_spruce_taiga");
    AddRegistryEntry(registry, "minecraft:plains");
    AddRegistryEntry(registry, "minecraft:river");
    AddRegistryEntry(registry, "minecraft:savanna");
    AddRegistryEntry(registry, "minecraft:savanna_plateau");
    AddRegistryEntry(registry, "minecraft:small_end_islands");
    AddRegistryEntry(registry, "minecraft:snowy_beach");
    AddRegistryEntry(registry, "minecraft:snowy_plains");
    AddRegistryEntry(registry, "minecraft:snowy_slopes");
    AddRegistryEntry(registry, "minecraft:snowy_taiga");
    AddRegistryEntry(registry, "minecraft:soul_sand_valley");
    AddRegistryEntry(registry, "minecraft:sparse_jungle");
    AddRegistryEntry(registry, "minecraft:stony_peaks");
    AddRegistryEntry(registry, "minecraft:stony_shore");
    AddRegistryEntry(registry, "minecraft:sunflower_plains");
    AddRegistryEntry(registry, "minecraft:swamp");
    AddRegistryEntry(registry, "minecraft:taiga");
    AddRegistryEntry(registry, "minecraft:the_end");
    AddRegistryEntry(registry, "minecraft:the_void");
    AddRegistryEntry(registry, "minecraft:warm_ocean");
    AddRegistryEntry(registry, "minecraft:warped_forest");
    AddRegistryEntry(registry, "minecraft:windswept_forest");
    AddRegistryEntry(registry, "minecraft:windswept_gravelly_hills");
    AddRegistryEntry(registry, "minecraft:windswept_hills");
    AddRegistryEntry(registry, "minecraft:windswept_savanna");
    AddRegistryEntry(registry, "minecraft:wooded_badlands");

    AddRegistryTag(registry, "minecraft:water_on_map_outlines", "minecraft:deep_frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:deep_ocean", "minecraft:deep_lukewarm_ocean", "minecraft:frozen_ocean", "minecraft:ocean", "minecraft:cold_ocean", "minecraft:lukewarm_ocean", "minecraft:warm_ocean", "minecraft:river", "minecraft:frozen_river", "minecraft:swamp", "minecraft:mangrove_swamp", NULL);
    AddRegistryTag(registry, "minecraft:spawns_warm_variant_frogs", "minecraft:desert", "minecraft:warm_ocean", "minecraft:bamboo_jungle", "minecraft:jungle", "minecraft:sparse_jungle", "minecraft:savanna", "minecraft:savanna_plateau", "minecraft:windswept_savanna", "minecraft:nether_wastes", "minecraft:soul_sand_valley", "minecraft:crimson_forest", "minecraft:warped_forest", "minecraft:basalt_deltas", "minecraft:badlands", "minecraft:eroded_badlands", "minecraft:wooded_badlands", "minecraft:mangrove_swamp", NULL);
    AddRegistryTag(registry, "minecraft:is_ocean", "minecraft:deep_frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:deep_ocean", "minecraft:deep_lukewarm_ocean", "minecraft:frozen_ocean", "minecraft:ocean", "minecraft:cold_ocean", "minecraft:lukewarm_ocean", "minecraft:warm_ocean", NULL);
    AddRegistryTag(registry, "minecraft:mineshaft_blocking", "minecraft:deep_dark", NULL);
    AddRegistryTag(registry, "minecraft:required_ocean_monument_surrounding", "minecraft:deep_frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:deep_ocean", "minecraft:deep_lukewarm_ocean", "minecraft:frozen_ocean", "minecraft:ocean", "minecraft:cold_ocean", "minecraft:lukewarm_ocean", "minecraft:warm_ocean", "minecraft:river", "minecraft:frozen_river", NULL);
    AddRegistryTag(registry, "minecraft:stronghold_biased_to", "minecraft:plains", "minecraft:sunflower_plains", "minecraft:snowy_plains", "minecraft:ice_spikes", "minecraft:desert", "minecraft:forest", "minecraft:flower_forest", "minecraft:birch_forest", "minecraft:dark_forest", "minecraft:old_growth_birch_forest", "minecraft:old_growth_pine_taiga", "minecraft:old_growth_spruce_taiga", "minecraft:taiga", "minecraft:snowy_taiga", "minecraft:savanna", "minecraft:savanna_plateau", "minecraft:windswept_hills", "minecraft:windswept_gravelly_hills", "minecraft:windswept_forest", "minecraft:windswept_savanna", "minecraft:jungle", "minecraft:sparse_jungle", "minecraft:bamboo_jungle", "minecraft:badlands", "minecraft:eroded_badlands", "minecraft:wooded_badlands", "minecraft:meadow", "minecraft:grove", "minecraft:snowy_slopes", "minecraft:frozen_peaks", "minecraft:jagged_peaks", "minecraft:stony_peaks", "minecraft:mushroom_fields", "minecraft:dripstone_caves", "minecraft:lush_caves", NULL);
    AddRegistryTag(registry, "minecraft:spawns_cold_variant_frogs", "minecraft:snowy_plains", "minecraft:ice_spikes", "minecraft:frozen_peaks", "minecraft:jagged_peaks", "minecraft:snowy_slopes", "minecraft:frozen_ocean", "minecraft:deep_frozen_ocean", "minecraft:grove", "minecraft:deep_dark", "minecraft:frozen_river", "minecraft:snowy_taiga", "minecraft:snowy_beach", "minecraft:the_end", "minecraft:end_highlands", "minecraft:end_midlands", "minecraft:small_end_islands", "minecraft:end_barrens", NULL);
    AddRegistryTag(registry, "minecraft:is_hill", "minecraft:windswept_hills", "minecraft:windswept_forest", "minecraft:windswept_gravelly_hills", NULL);
    AddRegistryTag(registry, "minecraft:without_wandering_trader_spawns", "minecraft:the_void", NULL);
    AddRegistryTag(registry, "minecraft:spawns_white_rabbits", "minecraft:snowy_plains", "minecraft:ice_spikes", "minecraft:frozen_ocean", "minecraft:snowy_taiga", "minecraft:frozen_river", "minecraft:snowy_beach", "minecraft:frozen_peaks", "minecraft:jagged_peaks", "minecraft:snowy_slopes", "minecraft:grove", NULL);
    AddRegistryTag(registry, "minecraft:is_taiga", "minecraft:taiga", "minecraft:snowy_taiga", "minecraft:old_growth_pine_taiga", "minecraft:old_growth_spruce_taiga", NULL);
    AddRegistryTag(registry, "minecraft:spawns_gold_rabbits", "minecraft:desert", NULL);
    AddRegistryTag(registry, "minecraft:increased_fire_burnout", "minecraft:bamboo_jungle", "minecraft:mushroom_fields", "minecraft:mangrove_swamp", "minecraft:snowy_slopes", "minecraft:frozen_peaks", "minecraft:jagged_peaks", "minecraft:swamp", "minecraft:jungle", NULL);
    AddRegistryTag(registry, "minecraft:has_closer_water_fog", "minecraft:swamp", "minecraft:mangrove_swamp", NULL);
    AddRegistryTag(registry, "minecraft:snow_golem_melts", "minecraft:badlands", "minecraft:basalt_deltas", "minecraft:crimson_forest", "minecraft:desert", "minecraft:eroded_badlands", "minecraft:nether_wastes", "minecraft:savanna", "minecraft:savanna_plateau", "minecraft:soul_sand_valley", "minecraft:warped_forest", "minecraft:windswept_savanna", "minecraft:wooded_badlands", NULL);
    AddRegistryTag(registry, "minecraft:is_deep_ocean", "minecraft:deep_frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:deep_ocean", "minecraft:deep_lukewarm_ocean", NULL);
    AddRegistryTag(registry, "minecraft:allows_surface_slime_spawns", "minecraft:swamp", "minecraft:mangrove_swamp", NULL);
    AddRegistryTag(registry, "minecraft:is_forest", "minecraft:forest", "minecraft:flower_forest", "minecraft:birch_forest", "minecraft:old_growth_birch_forest", "minecraft:dark_forest", "minecraft:grove", NULL);
    AddRegistryTag(registry, "minecraft:is_river", "minecraft:river", "minecraft:frozen_river", NULL);
    AddRegistryTag(registry, "minecraft:produces_corals_from_bonemeal", "minecraft:warm_ocean", NULL);
    AddRegistryTag(registry, "minecraft:plays_underwater_music", "minecraft:deep_frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:deep_ocean", "minecraft:deep_lukewarm_ocean", "minecraft:frozen_ocean", "minecraft:ocean", "minecraft:cold_ocean", "minecraft:lukewarm_ocean", "minecraft:warm_ocean", "minecraft:river", "minecraft:frozen_river", NULL);
    AddRegistryTag(registry, "minecraft:is_jungle", "minecraft:bamboo_jungle", "minecraft:jungle", "minecraft:sparse_jungle", NULL);
    AddRegistryTag(registry, "minecraft:is_end", "minecraft:the_end", "minecraft:end_highlands", "minecraft:end_midlands", "minecraft:small_end_islands", "minecraft:end_barrens", NULL);
    AddRegistryTag(registry, "minecraft:spawns_snow_foxes", "minecraft:snowy_plains", "minecraft:ice_spikes", "minecraft:frozen_ocean", "minecraft:snowy_taiga", "minecraft:frozen_river", "minecraft:snowy_beach", "minecraft:frozen_peaks", "minecraft:jagged_peaks", "minecraft:snowy_slopes", "minecraft:grove", NULL);
    AddRegistryTag(registry, "minecraft:is_savanna", "minecraft:savanna", "minecraft:savanna_plateau", "minecraft:windswept_savanna", NULL);
    AddRegistryTag(registry, "minecraft:is_nether", "minecraft:nether_wastes", "minecraft:soul_sand_valley", "minecraft:crimson_forest", "minecraft:warped_forest", "minecraft:basalt_deltas", NULL);
    AddRegistryTag(registry, "minecraft:reduce_water_ambient_spawns", "minecraft:river", "minecraft:frozen_river", NULL);
    AddRegistryTag(registry, "minecraft:is_overworld", "minecraft:mushroom_fields", "minecraft:deep_frozen_ocean", "minecraft:frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:cold_ocean", "minecraft:deep_ocean", "minecraft:ocean", "minecraft:deep_lukewarm_ocean", "minecraft:lukewarm_ocean", "minecraft:warm_ocean", "minecraft:stony_shore", "minecraft:swamp", "minecraft:mangrove_swamp", "minecraft:snowy_slopes", "minecraft:snowy_plains", "minecraft:snowy_beach", "minecraft:windswept_gravelly_hills", "minecraft:grove", "minecraft:windswept_hills", "minecraft:snowy_taiga", "minecraft:windswept_forest", "minecraft:taiga", "minecraft:plains", "minecraft:meadow", "minecraft:beach", "minecraft:forest", "minecraft:old_growth_spruce_taiga", "minecraft:flower_forest", "minecraft:birch_forest", "minecraft:dark_forest", "minecraft:savanna_plateau", "minecraft:savanna", "minecraft:jungle", "minecraft:badlands", "minecraft:desert", "minecraft:wooded_badlands", "minecraft:jagged_peaks", "minecraft:stony_peaks", "minecraft:frozen_river", "minecraft:river", "minecraft:ice_spikes", "minecraft:old_growth_pine_taiga", "minecraft:sunflower_plains", "minecraft:old_growth_birch_forest", "minecraft:sparse_jungle", "minecraft:bamboo_jungle", "minecraft:eroded_badlands", "minecraft:windswept_savanna", "minecraft:cherry_grove", "minecraft:frozen_peaks", "minecraft:dripstone_caves", "minecraft:lush_caves", "minecraft:deep_dark", NULL);
    AddRegistryTag(registry, "minecraft:is_badlands", "minecraft:badlands", "minecraft:eroded_badlands", "minecraft:wooded_badlands", NULL);
    AddRegistryTag(registry, "minecraft:without_zombie_sieges", "minecraft:mushroom_fields", NULL);
    AddRegistryTag(registry, "minecraft:without_patrol_spawns", "minecraft:mushroom_fields", NULL);
    AddRegistryTag(registry, "minecraft:polar_bears_spawn_on_alternate_blocks", "minecraft:frozen_ocean", "minecraft:deep_frozen_ocean", NULL);
    AddRegistryTag(registry, "minecraft:more_frequent_drowned_spawns", "minecraft:river", "minecraft:frozen_river", NULL);
    AddRegistryTag(registry, "minecraft:is_beach", "minecraft:beach", "minecraft:snowy_beach", NULL);
    AddRegistryTag(registry, "minecraft:is_mountain", "minecraft:meadow", "minecraft:frozen_peaks", "minecraft:jagged_peaks", "minecraft:stony_peaks", "minecraft:snowy_slopes", "minecraft:cherry_grove", NULL);
    AddRegistryTag(registry, "minecraft:allows_tropical_fish_spawns_at_any_height", "minecraft:lush_caves", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/woodland_mansion", "minecraft:dark_forest", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/ancient_city", "minecraft:deep_dark", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/village_desert", "minecraft:desert", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/village_taiga", "minecraft:taiga", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/stronghold", "minecraft:mushroom_fields", "minecraft:deep_frozen_ocean", "minecraft:frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:cold_ocean", "minecraft:deep_ocean", "minecraft:ocean", "minecraft:deep_lukewarm_ocean", "minecraft:lukewarm_ocean", "minecraft:warm_ocean", "minecraft:stony_shore", "minecraft:swamp", "minecraft:mangrove_swamp", "minecraft:snowy_slopes", "minecraft:snowy_plains", "minecraft:snowy_beach", "minecraft:windswept_gravelly_hills", "minecraft:grove", "minecraft:windswept_hills", "minecraft:snowy_taiga", "minecraft:windswept_forest", "minecraft:taiga", "minecraft:plains", "minecraft:meadow", "minecraft:beach", "minecraft:forest", "minecraft:old_growth_spruce_taiga", "minecraft:flower_forest", "minecraft:birch_forest", "minecraft:dark_forest", "minecraft:savanna_plateau", "minecraft:savanna", "minecraft:jungle", "minecraft:badlands", "minecraft:desert", "minecraft:wooded_badlands", "minecraft:jagged_peaks", "minecraft:stony_peaks", "minecraft:frozen_river", "minecraft:river", "minecraft:ice_spikes", "minecraft:old_growth_pine_taiga", "minecraft:sunflower_plains", "minecraft:old_growth_birch_forest", "minecraft:sparse_jungle", "minecraft:bamboo_jungle", "minecraft:eroded_badlands", "minecraft:windswept_savanna", "minecraft:cherry_grove", "minecraft:frozen_peaks", "minecraft:dripstone_caves", "minecraft:lush_caves", "minecraft:deep_dark", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/ruined_portal_mountain", "minecraft:badlands", "minecraft:eroded_badlands", "minecraft:wooded_badlands", "minecraft:windswept_hills", "minecraft:windswept_forest", "minecraft:windswept_gravelly_hills", "minecraft:savanna_plateau", "minecraft:windswept_savanna", "minecraft:stony_shore", "minecraft:meadow", "minecraft:frozen_peaks", "minecraft:jagged_peaks", "minecraft:stony_peaks", "minecraft:snowy_slopes", "minecraft:cherry_grove", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/mineshaft", "minecraft:deep_frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:deep_ocean", "minecraft:deep_lukewarm_ocean", "minecraft:frozen_ocean", "minecraft:ocean", "minecraft:cold_ocean", "minecraft:lukewarm_ocean", "minecraft:warm_ocean", "minecraft:river", "minecraft:frozen_river", "minecraft:beach", "minecraft:snowy_beach", "minecraft:meadow", "minecraft:frozen_peaks", "minecraft:jagged_peaks", "minecraft:stony_peaks", "minecraft:snowy_slopes", "minecraft:cherry_grove", "minecraft:windswept_hills", "minecraft:windswept_forest", "minecraft:windswept_gravelly_hills", "minecraft:taiga", "minecraft:snowy_taiga", "minecraft:old_growth_pine_taiga", "minecraft:old_growth_spruce_taiga", "minecraft:bamboo_jungle", "minecraft:jungle", "minecraft:sparse_jungle", "minecraft:forest", "minecraft:flower_forest", "minecraft:birch_forest", "minecraft:old_growth_birch_forest", "minecraft:dark_forest", "minecraft:grove", "minecraft:stony_shore", "minecraft:mushroom_fields", "minecraft:ice_spikes", "minecraft:windswept_savanna", "minecraft:desert", "minecraft:savanna", "minecraft:snowy_plains", "minecraft:plains", "minecraft:sunflower_plains", "minecraft:swamp", "minecraft:mangrove_swamp", "minecraft:savanna_plateau", "minecraft:dripstone_caves", "minecraft:lush_caves", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/swamp_hut", "minecraft:swamp", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/ocean_ruin_cold", "minecraft:frozen_ocean", "minecraft:cold_ocean", "minecraft:ocean", "minecraft:deep_frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:deep_ocean", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/village_snowy", "minecraft:snowy_plains", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/buried_treasure", "minecraft:beach", "minecraft:snowy_beach", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/ruined_portal_desert", "minecraft:desert", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/end_city", "minecraft:end_highlands", "minecraft:end_midlands", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/pillager_outpost", "minecraft:desert", "minecraft:plains", "minecraft:savanna", "minecraft:snowy_plains", "minecraft:taiga", "minecraft:meadow", "minecraft:frozen_peaks", "minecraft:jagged_peaks", "minecraft:stony_peaks", "minecraft:snowy_slopes", "minecraft:cherry_grove", "minecraft:grove", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/village_plains", "minecraft:plains", "minecraft:meadow", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/ruined_portal_standard", "minecraft:beach", "minecraft:snowy_beach", "minecraft:river", "minecraft:frozen_river", "minecraft:taiga", "minecraft:snowy_taiga", "minecraft:old_growth_pine_taiga", "minecraft:old_growth_spruce_taiga", "minecraft:forest", "minecraft:flower_forest", "minecraft:birch_forest", "minecraft:old_growth_birch_forest", "minecraft:dark_forest", "minecraft:grove", "minecraft:mushroom_fields", "minecraft:ice_spikes", "minecraft:dripstone_caves", "minecraft:lush_caves", "minecraft:savanna", "minecraft:snowy_plains", "minecraft:plains", "minecraft:sunflower_plains", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/jungle_temple", "minecraft:bamboo_jungle", "minecraft:jungle", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/trial_chambers", "minecraft:mushroom_fields", "minecraft:deep_frozen_ocean", "minecraft:frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:cold_ocean", "minecraft:deep_ocean", "minecraft:ocean", "minecraft:deep_lukewarm_ocean", "minecraft:lukewarm_ocean", "minecraft:warm_ocean", "minecraft:stony_shore", "minecraft:swamp", "minecraft:mangrove_swamp", "minecraft:snowy_slopes", "minecraft:snowy_plains", "minecraft:snowy_beach", "minecraft:windswept_gravelly_hills", "minecraft:grove", "minecraft:windswept_hills", "minecraft:snowy_taiga", "minecraft:windswept_forest", "minecraft:taiga", "minecraft:plains", "minecraft:meadow", "minecraft:beach", "minecraft:forest", "minecraft:old_growth_spruce_taiga", "minecraft:flower_forest", "minecraft:birch_forest", "minecraft:dark_forest", "minecraft:savanna_plateau", "minecraft:savanna", "minecraft:jungle", "minecraft:badlands", "minecraft:desert", "minecraft:wooded_badlands", "minecraft:jagged_peaks", "minecraft:stony_peaks", "minecraft:frozen_river", "minecraft:river", "minecraft:ice_spikes", "minecraft:old_growth_pine_taiga", "minecraft:sunflower_plains", "minecraft:old_growth_birch_forest", "minecraft:sparse_jungle", "minecraft:bamboo_jungle", "minecraft:eroded_badlands", "minecraft:windswept_savanna", "minecraft:cherry_grove", "minecraft:frozen_peaks", "minecraft:dripstone_caves", "minecraft:lush_caves", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/ruined_portal_jungle", "minecraft:bamboo_jungle", "minecraft:jungle", "minecraft:sparse_jungle", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/ocean_monument", "minecraft:deep_frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:deep_ocean", "minecraft:deep_lukewarm_ocean", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/mineshaft_mesa", "minecraft:badlands", "minecraft:eroded_badlands", "minecraft:wooded_badlands", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/ruined_portal_nether", "minecraft:nether_wastes", "minecraft:soul_sand_valley", "minecraft:crimson_forest", "minecraft:warped_forest", "minecraft:basalt_deltas", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/village_savanna", "minecraft:savanna", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/nether_fossil", "minecraft:soul_sand_valley", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/trail_ruins", "minecraft:taiga", "minecraft:snowy_taiga", "minecraft:old_growth_pine_taiga", "minecraft:old_growth_spruce_taiga", "minecraft:old_growth_birch_forest", "minecraft:jungle", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/ruined_portal_ocean", "minecraft:deep_frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:deep_ocean", "minecraft:deep_lukewarm_ocean", "minecraft:frozen_ocean", "minecraft:ocean", "minecraft:cold_ocean", "minecraft:lukewarm_ocean", "minecraft:warm_ocean", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/bastion_remnant", "minecraft:crimson_forest", "minecraft:nether_wastes", "minecraft:soul_sand_valley", "minecraft:warped_forest", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/igloo", "minecraft:snowy_taiga", "minecraft:snowy_plains", "minecraft:snowy_slopes", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/nether_fortress", "minecraft:nether_wastes", "minecraft:soul_sand_valley", "minecraft:crimson_forest", "minecraft:warped_forest", "minecraft:basalt_deltas", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/shipwreck", "minecraft:deep_frozen_ocean", "minecraft:deep_cold_ocean", "minecraft:deep_ocean", "minecraft:deep_lukewarm_ocean", "minecraft:frozen_ocean", "minecraft:ocean", "minecraft:cold_ocean", "minecraft:lukewarm_ocean", "minecraft:warm_ocean", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/desert_pyramid", "minecraft:desert", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/ocean_ruin_warm", "minecraft:lukewarm_ocean", "minecraft:warm_ocean", "minecraft:deep_lukewarm_ocean", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/ruined_portal_swamp", "minecraft:swamp", "minecraft:mangrove_swamp", NULL);
    AddRegistryTag(registry, "minecraft:has_structure/shipwreck_beached", "minecraft:beach", "minecraft:snowy_beach", NULL);
}

static void InitChatTypeRegistry(void) {
    Registry * registry = &serv->chatTypeRegistry;
    SetRegistryName(registry, "minecraft:chat_type");
    registry->sendEntriesToClients = 1;

    AddRegistryEntry(registry, "minecraft:chat");
    AddRegistryEntry(registry, "minecraft:emote_command");
    AddRegistryEntry(registry, "minecraft:msg_command_incoming");
    AddRegistryEntry(registry, "minecraft:msg_command_outgoing");
    AddRegistryEntry(registry, "minecraft:say_command");
    AddRegistryEntry(registry, "minecraft:team_msg_command_incoming");
    AddRegistryEntry(registry, "minecraft:team_msg_command_outgoing");
}

static void InitTrimPatternRegistry(void) {
    Registry * registry = &serv->trimPatternRegistry;
    SetRegistryName(registry, "minecraft:trim_pattern");
    registry->sendEntriesToClients = 1;

    AddRegistryEntry(registry, "minecraft:bolt");
    AddRegistryEntry(registry, "minecraft:coast");
    AddRegistryEntry(registry, "minecraft:dune");
    AddRegistryEntry(registry, "minecraft:eye");
    AddRegistryEntry(registry, "minecraft:flow");
    AddRegistryEntry(registry, "minecraft:host");
    AddRegistryEntry(registry, "minecraft:raiser");
    AddRegistryEntry(registry, "minecraft:rib");
    AddRegistryEntry(registry, "minecraft:sentry");
    AddRegistryEntry(registry, "minecraft:shaper");
    AddRegistryEntry(registry, "minecraft:silence");
    AddRegistryEntry(registry, "minecraft:snout");
    AddRegistryEntry(registry, "minecraft:spire");
    AddRegistryEntry(registry, "minecraft:tide");
    AddRegistryEntry(registry, "minecraft:vex");
    AddRegistryEntry(registry, "minecraft:ward");
    AddRegistryEntry(registry, "minecraft:wayfinder");
    AddRegistryEntry(registry, "minecraft:wild");
}

static void InitTrimMaterialRegistry(void) {
    Registry * registry = &serv->trimMaterialRegistry;
    SetRegistryName(registry, "minecraft:trim_material");
    registry->sendEntriesToClients = 1;

    AddRegistryEntry(registry, "minecraft:amethyst");
    AddRegistryEntry(registry, "minecraft:copper");
    AddRegistryEntry(registry, "minecraft:diamond");
    AddRegistryEntry(registry, "minecraft:emerald");
    AddRegistryEntry(registry, "minecraft:gold");
    AddRegistryEntry(registry, "minecraft:iron");
    AddRegistryEntry(registry, "minecraft:lapis");
    AddRegistryEntry(registry, "minecraft:netherite");
    AddRegistryEntry(registry, "minecraft:quartz");
    AddRegistryEntry(registry, "minecraft:redstone");
}

static void InitWolfVariantRegistry(void) {
    Registry * registry = &serv->wolfVariantRegistry;
    SetRegistryName(registry, "minecraft:wolf_variant");
    registry->sendEntriesToClients = 1;

    AddRegistryEntry(registry, "minecraft:ashen");
    AddRegistryEntry(registry, "minecraft:black");
    AddRegistryEntry(registry, "minecraft:chestnut");
    AddRegistryEntry(registry, "minecraft:pale");
    AddRegistryEntry(registry, "minecraft:rusty");
    AddRegistryEntry(registry, "minecraft:snowy");
    AddRegistryEntry(registry, "minecraft:spotted");
    AddRegistryEntry(registry, "minecraft:striped");
    AddRegistryEntry(registry, "minecraft:woods");
}

static void InitPaintingVariantRegistry(void) {
    Registry * registry = &serv->paintingVariantRegistry;
    SetRegistryName(registry, "minecraft:painting_variant");
    registry->sendEntriesToClients = 1;

    AddRegistryEntry(registry, "minecraft:alban");
    AddRegistryEntry(registry, "minecraft:aztec");
    AddRegistryEntry(registry, "minecraft:aztec2");
    AddRegistryEntry(registry, "minecraft:backyard");
    AddRegistryEntry(registry, "minecraft:baroque");
    AddRegistryEntry(registry, "minecraft:bomb");
    AddRegistryEntry(registry, "minecraft:bouquet");
    AddRegistryEntry(registry, "minecraft:burning_skull");
    AddRegistryEntry(registry, "minecraft:bust");
    AddRegistryEntry(registry, "minecraft:cavebird");
    AddRegistryEntry(registry, "minecraft:changing");
    AddRegistryEntry(registry, "minecraft:cotan");
    AddRegistryEntry(registry, "minecraft:courbet");
    AddRegistryEntry(registry, "minecraft:creebet");
    AddRegistryEntry(registry, "minecraft:donkey_kong");
    AddRegistryEntry(registry, "minecraft:earth");
    AddRegistryEntry(registry, "minecraft:endboss");
    AddRegistryEntry(registry, "minecraft:fern");
    AddRegistryEntry(registry, "minecraft:fighters");
    AddRegistryEntry(registry, "minecraft:finding");
    AddRegistryEntry(registry, "minecraft:fire");
    AddRegistryEntry(registry, "minecraft:graham");
    AddRegistryEntry(registry, "minecraft:humble");
    AddRegistryEntry(registry, "minecraft:kebab");
    AddRegistryEntry(registry, "minecraft:lowmist");
    AddRegistryEntry(registry, "minecraft:match");
    AddRegistryEntry(registry, "minecraft:meditative");
    AddRegistryEntry(registry, "minecraft:orb");
    AddRegistryEntry(registry, "minecraft:owlemons");
    AddRegistryEntry(registry, "minecraft:passage");
    AddRegistryEntry(registry, "minecraft:pigscene");
    AddRegistryEntry(registry, "minecraft:plant");
    AddRegistryEntry(registry, "minecraft:pointer");
    AddRegistryEntry(registry, "minecraft:pond");
    AddRegistryEntry(registry, "minecraft:pool");
    AddRegistryEntry(registry, "minecraft:prairie_ride");
    AddRegistryEntry(registry, "minecraft:sea");
    AddRegistryEntry(registry, "minecraft:skeleton");
    AddRegistryEntry(registry, "minecraft:skull_and_roses");
    AddRegistryEntry(registry, "minecraft:stage");
    AddRegistryEntry(registry, "minecraft:sunflowers");
    AddRegistryEntry(registry, "minecraft:sunset");
    AddRegistryEntry(registry, "minecraft:tides");
    AddRegistryEntry(registry, "minecraft:unpacked");
    AddRegistryEntry(registry, "minecraft:void");
    AddRegistryEntry(registry, "minecraft:wanderer");
    AddRegistryEntry(registry, "minecraft:wasteland");
    AddRegistryEntry(registry, "minecraft:water");
    AddRegistryEntry(registry, "minecraft:wind");
    AddRegistryEntry(registry, "minecraft:wither");

    AddRegistryTag(registry, "minecraft:placeable", "minecraft:kebab", "minecraft:aztec", "minecraft:alban", "minecraft:aztec2", "minecraft:bomb", "minecraft:plant", "minecraft:wasteland", "minecraft:pool", "minecraft:courbet", "minecraft:sea", "minecraft:sunset", "minecraft:creebet", "minecraft:wanderer", "minecraft:graham", "minecraft:match", "minecraft:bust", "minecraft:stage", "minecraft:void", "minecraft:skull_and_roses", "minecraft:wither", "minecraft:fighters", "minecraft:pointer", "minecraft:pigscene", "minecraft:burning_skull", "minecraft:skeleton", "minecraft:donkey_kong", "minecraft:baroque", "minecraft:humble", "minecraft:meditative", "minecraft:prairie_ride", "minecraft:unpacked", "minecraft:backyard", "minecraft:bouquet", "minecraft:cavebird", "minecraft:changing", "minecraft:cotan", "minecraft:endboss", "minecraft:fern", "minecraft:finding", "minecraft:lowmist", "minecraft:orb", "minecraft:owlemons", "minecraft:passage", "minecraft:pond", "minecraft:sunflowers", "minecraft:tides", NULL);
}

static void InitDimensionTypeRegistry(void) {
    Registry * registry = &serv->dimensionTypeRegistry;
    SetRegistryName(registry, "minecraft:dimension_type");
    registry->sendEntriesToClients = 1;

    AddRegistryEntry(registry, "minecraft:overworld");
    AddRegistryEntry(registry, "minecraft:overworld_caves");
    AddRegistryEntry(registry, "minecraft:the_end");
    AddRegistryEntry(registry, "minecraft:the_nether");
}

static void InitDamageTypeRegistry(void) {
    Registry * registry = &serv->damageTypeRegistry;
    SetRegistryName(registry, "minecraft:damage_type");
    registry->sendEntriesToClients = 1;

    AddRegistryEntry(registry, "minecraft:arrow");
    AddRegistryEntry(registry, "minecraft:bad_respawn_point");
    AddRegistryEntry(registry, "minecraft:cactus");
    AddRegistryEntry(registry, "minecraft:campfire");
    AddRegistryEntry(registry, "minecraft:cramming");
    AddRegistryEntry(registry, "minecraft:dragon_breath");
    AddRegistryEntry(registry, "minecraft:drown");
    AddRegistryEntry(registry, "minecraft:dry_out");
    AddRegistryEntry(registry, "minecraft:ender_pearl");
    AddRegistryEntry(registry, "minecraft:explosion");
    AddRegistryEntry(registry, "minecraft:fall");
    AddRegistryEntry(registry, "minecraft:falling_anvil");
    AddRegistryEntry(registry, "minecraft:falling_block");
    AddRegistryEntry(registry, "minecraft:falling_stalactite");
    AddRegistryEntry(registry, "minecraft:fireball");
    AddRegistryEntry(registry, "minecraft:fireworks");
    AddRegistryEntry(registry, "minecraft:fly_into_wall");
    AddRegistryEntry(registry, "minecraft:freeze");
    AddRegistryEntry(registry, "minecraft:generic");
    AddRegistryEntry(registry, "minecraft:generic_kill");
    AddRegistryEntry(registry, "minecraft:hot_floor");
    AddRegistryEntry(registry, "minecraft:in_fire");
    AddRegistryEntry(registry, "minecraft:in_wall");
    AddRegistryEntry(registry, "minecraft:indirect_magic");
    AddRegistryEntry(registry, "minecraft:lava");
    AddRegistryEntry(registry, "minecraft:lightning_bolt");
    AddRegistryEntry(registry, "minecraft:mace_smash");
    AddRegistryEntry(registry, "minecraft:magic");
    AddRegistryEntry(registry, "minecraft:mob_attack");
    AddRegistryEntry(registry, "minecraft:mob_attack_no_aggro");
    AddRegistryEntry(registry, "minecraft:mob_projectile");
    AddRegistryEntry(registry, "minecraft:on_fire");
    AddRegistryEntry(registry, "minecraft:out_of_world");
    AddRegistryEntry(registry, "minecraft:outside_border");
    AddRegistryEntry(registry, "minecraft:player_attack");
    AddRegistryEntry(registry, "minecraft:player_explosion");
    AddRegistryEntry(registry, "minecraft:sonic_boom");
    AddRegistryEntry(registry, "minecraft:spit");
    AddRegistryEntry(registry, "minecraft:stalagmite");
    AddRegistryEntry(registry, "minecraft:starve");
    AddRegistryEntry(registry, "minecraft:sting");
    AddRegistryEntry(registry, "minecraft:sweet_berry_bush");
    AddRegistryEntry(registry, "minecraft:thorns");
    AddRegistryEntry(registry, "minecraft:thrown");
    AddRegistryEntry(registry, "minecraft:trident");
    AddRegistryEntry(registry, "minecraft:unattributed_fireball");
    AddRegistryEntry(registry, "minecraft:wind_charge");
    AddRegistryEntry(registry, "minecraft:wither");
    AddRegistryEntry(registry, "minecraft:wither_skull");

    AddRegistryTag(registry, "minecraft:always_most_significant_fall", "minecraft:out_of_world", NULL);
    AddRegistryTag(registry, "minecraft:no_anger", "minecraft:mob_attack_no_aggro", NULL);
    AddRegistryTag(registry, "minecraft:is_lightning", "minecraft:lightning_bolt", NULL);
    AddRegistryTag(registry, "minecraft:always_hurts_ender_dragons", "minecraft:fireworks", "minecraft:explosion", "minecraft:player_explosion", "minecraft:bad_respawn_point", NULL);
    AddRegistryTag(registry, "minecraft:is_freezing", "minecraft:freeze", NULL);
    AddRegistryTag(registry, "minecraft:always_triggers_silverfish", "minecraft:magic", NULL);
    AddRegistryTag(registry, "minecraft:is_fire", "minecraft:in_fire", "minecraft:campfire", "minecraft:on_fire", "minecraft:lava", "minecraft:hot_floor", "minecraft:unattributed_fireball", "minecraft:fireball", NULL);
    AddRegistryTag(registry, "minecraft:is_player_attack", "minecraft:player_attack", "minecraft:mace_smash", NULL);
    AddRegistryTag(registry, "minecraft:no_knockback", "minecraft:explosion", "minecraft:player_explosion", "minecraft:bad_respawn_point", "minecraft:in_fire", "minecraft:lightning_bolt", "minecraft:on_fire", "minecraft:lava", "minecraft:hot_floor", "minecraft:in_wall", "minecraft:cramming", "minecraft:drown", "minecraft:starve", "minecraft:cactus", "minecraft:fall", "minecraft:ender_pearl", "minecraft:fly_into_wall", "minecraft:out_of_world", "minecraft:generic", "minecraft:magic", "minecraft:wither", "minecraft:dragon_breath", "minecraft:dry_out", "minecraft:sweet_berry_bush", "minecraft:freeze", "minecraft:stalagmite", "minecraft:outside_border", "minecraft:generic_kill", "minecraft:campfire", NULL);
    AddRegistryTag(registry, "minecraft:burn_from_stepping", "minecraft:campfire", "minecraft:hot_floor", NULL);
    AddRegistryTag(registry, "minecraft:no_impact", "minecraft:drown", NULL);
    AddRegistryTag(registry, "minecraft:is_explosion", "minecraft:fireworks", "minecraft:explosion", "minecraft:player_explosion", "minecraft:bad_respawn_point", NULL);
    AddRegistryTag(registry, "minecraft:can_break_armor_stand", "minecraft:player_explosion", "minecraft:player_attack", "minecraft:mace_smash", NULL);
    AddRegistryTag(registry, "minecraft:bypasses_shield", "minecraft:on_fire", "minecraft:in_wall", "minecraft:cramming", "minecraft:drown", "minecraft:fly_into_wall", "minecraft:generic", "minecraft:wither", "minecraft:dragon_breath", "minecraft:starve", "minecraft:fall", "minecraft:ender_pearl", "minecraft:freeze", "minecraft:stalagmite", "minecraft:magic", "minecraft:indirect_magic", "minecraft:out_of_world", "minecraft:generic_kill", "minecraft:sonic_boom", "minecraft:outside_border", "minecraft:falling_anvil", "minecraft:falling_stalactite", NULL);
    AddRegistryTag(registry, "minecraft:burns_armor_stands", "minecraft:on_fire", NULL);
    AddRegistryTag(registry, "minecraft:ignites_armor_stands", "minecraft:in_fire", "minecraft:campfire", NULL);
    AddRegistryTag(registry, "minecraft:panic_causes", "minecraft:cactus", "minecraft:freeze", "minecraft:hot_floor", "minecraft:in_fire", "minecraft:lava", "minecraft:lightning_bolt", "minecraft:on_fire", "minecraft:arrow", "minecraft:dragon_breath", "minecraft:explosion", "minecraft:fireball", "minecraft:fireworks", "minecraft:indirect_magic", "minecraft:magic", "minecraft:mob_attack", "minecraft:mob_projectile", "minecraft:player_explosion", "minecraft:sonic_boom", "minecraft:sting", "minecraft:thrown", "minecraft:trident", "minecraft:unattributed_fireball", "minecraft:wind_charge", "minecraft:wither", "minecraft:wither_skull", "minecraft:player_attack", "minecraft:mace_smash", NULL);
    AddRegistryTag(registry, "minecraft:damages_helmet", "minecraft:falling_anvil", "minecraft:falling_block", "minecraft:falling_stalactite", NULL);
    AddRegistryTag(registry, "minecraft:is_projectile", "minecraft:arrow", "minecraft:trident", "minecraft:mob_projectile", "minecraft:unattributed_fireball", "minecraft:fireball", "minecraft:wither_skull", "minecraft:thrown", "minecraft:wind_charge", NULL);
    AddRegistryTag(registry, "minecraft:bypasses_wolf_armor", "minecraft:out_of_world", "minecraft:generic_kill", "minecraft:cramming", "minecraft:drown", "minecraft:dry_out", "minecraft:freeze", "minecraft:in_wall", "minecraft:indirect_magic", "minecraft:magic", "minecraft:outside_border", "minecraft:starve", "minecraft:thorns", "minecraft:wither", NULL);
    AddRegistryTag(registry, "minecraft:bypasses_enchantments", "minecraft:sonic_boom", NULL);
    AddRegistryTag(registry, "minecraft:always_kills_armor_stands", "minecraft:arrow", "minecraft:trident", "minecraft:fireball", "minecraft:wither_skull", "minecraft:wind_charge", NULL);
    AddRegistryTag(registry, "minecraft:is_fall", "minecraft:fall", "minecraft:ender_pearl", "minecraft:stalagmite", NULL);
    AddRegistryTag(registry, "minecraft:panic_environmental_causes", "minecraft:cactus", "minecraft:freeze", "minecraft:hot_floor", "minecraft:in_fire", "minecraft:lava", "minecraft:lightning_bolt", "minecraft:on_fire", NULL);
    AddRegistryTag(registry, "minecraft:bypasses_resistance", "minecraft:out_of_world", "minecraft:generic_kill", NULL);
    AddRegistryTag(registry, "minecraft:mace_smash", "minecraft:mace_smash", NULL);
    AddRegistryTag(registry, "minecraft:bypasses_invulnerability", "minecraft:out_of_world", "minecraft:generic_kill", NULL);
    AddRegistryTag(registry, "minecraft:bypasses_effects", "minecraft:starve", NULL);
    AddRegistryTag(registry, "minecraft:avoids_guardian_thorns", "minecraft:magic", "minecraft:thorns", "minecraft:fireworks", "minecraft:explosion", "minecraft:player_explosion", "minecraft:bad_respawn_point", NULL);
    AddRegistryTag(registry, "minecraft:is_drowning", "minecraft:drown", NULL);
    AddRegistryTag(registry, "minecraft:witch_resistant_to", "minecraft:magic", "minecraft:indirect_magic", "minecraft:sonic_boom", "minecraft:thorns", NULL);
    AddRegistryTag(registry, "minecraft:bypasses_armor", "minecraft:on_fire", "minecraft:in_wall", "minecraft:cramming", "minecraft:drown", "minecraft:fly_into_wall", "minecraft:generic", "minecraft:wither", "minecraft:dragon_breath", "minecraft:starve", "minecraft:fall", "minecraft:ender_pearl", "minecraft:freeze", "minecraft:stalagmite", "minecraft:magic", "minecraft:indirect_magic", "minecraft:out_of_world", "minecraft:generic_kill", "minecraft:sonic_boom", "minecraft:outside_border", NULL);
    AddRegistryTag(registry, "minecraft:wither_immune_to", "minecraft:drown", NULL);

}

static void InitBannerPatternRegistry(void) {
    Registry * registry = &serv->bannerPatternRegistry;
    SetRegistryName(registry, "minecraft:banner_pattern");
    registry->sendEntriesToClients = 1;

    AddRegistryEntry(registry, "minecraft:base");
    AddRegistryEntry(registry, "minecraft:border");
    AddRegistryEntry(registry, "minecraft:bricks");
    AddRegistryEntry(registry, "minecraft:circle");
    AddRegistryEntry(registry, "minecraft:creeper");
    AddRegistryEntry(registry, "minecraft:cross");
    AddRegistryEntry(registry, "minecraft:curly_border");
    AddRegistryEntry(registry, "minecraft:diagonal_left");
    AddRegistryEntry(registry, "minecraft:diagonal_right");
    AddRegistryEntry(registry, "minecraft:diagonal_up_left");
    AddRegistryEntry(registry, "minecraft:diagonal_up_right");
    AddRegistryEntry(registry, "minecraft:flow");
    AddRegistryEntry(registry, "minecraft:flower");
    AddRegistryEntry(registry, "minecraft:globe");
    AddRegistryEntry(registry, "minecraft:gradient");
    AddRegistryEntry(registry, "minecraft:gradient_up");
    AddRegistryEntry(registry, "minecraft:guster");
    AddRegistryEntry(registry, "minecraft:half_horizontal");
    AddRegistryEntry(registry, "minecraft:half_horizontal_bottom");
    AddRegistryEntry(registry, "minecraft:half_vertical");
    AddRegistryEntry(registry, "minecraft:half_vertical_right");
    AddRegistryEntry(registry, "minecraft:mojang");
    AddRegistryEntry(registry, "minecraft:piglin");
    AddRegistryEntry(registry, "minecraft:rhombus");
    AddRegistryEntry(registry, "minecraft:skull");
    AddRegistryEntry(registry, "minecraft:small_stripes");
    AddRegistryEntry(registry, "minecraft:square_bottom_left");
    AddRegistryEntry(registry, "minecraft:square_bottom_right");
    AddRegistryEntry(registry, "minecraft:square_top_left");
    AddRegistryEntry(registry, "minecraft:square_top_right");
    AddRegistryEntry(registry, "minecraft:straight_cross");
    AddRegistryEntry(registry, "minecraft:stripe_bottom");
    AddRegistryEntry(registry, "minecraft:stripe_center");
    AddRegistryEntry(registry, "minecraft:stripe_downleft");
    AddRegistryEntry(registry, "minecraft:stripe_downright");
    AddRegistryEntry(registry, "minecraft:stripe_left");
    AddRegistryEntry(registry, "minecraft:stripe_middle");
    AddRegistryEntry(registry, "minecraft:stripe_right");
    AddRegistryEntry(registry, "minecraft:stripe_top");
    AddRegistryEntry(registry, "minecraft:triangle_bottom");
    AddRegistryEntry(registry, "minecraft:triangle_top");
    AddRegistryEntry(registry, "minecraft:triangles_bottom");
    AddRegistryEntry(registry, "minecraft:triangles_top");

    AddRegistryTag(registry, "minecraft:no_item_required", "minecraft:square_bottom_left", "minecraft:square_bottom_right", "minecraft:square_top_left", "minecraft:square_top_right", "minecraft:stripe_bottom", "minecraft:stripe_top", "minecraft:stripe_left", "minecraft:stripe_right", "minecraft:stripe_center", "minecraft:stripe_middle", "minecraft:stripe_downright", "minecraft:stripe_downleft", "minecraft:small_stripes", "minecraft:cross", "minecraft:straight_cross", "minecraft:triangle_bottom", "minecraft:triangle_top", "minecraft:triangles_bottom", "minecraft:triangles_top", "minecraft:diagonal_left", "minecraft:diagonal_up_right", "minecraft:diagonal_up_left", "minecraft:diagonal_right", "minecraft:circle", "minecraft:rhombus", "minecraft:half_vertical", "minecraft:half_horizontal", "minecraft:half_vertical_right", "minecraft:half_horizontal_bottom", "minecraft:border", "minecraft:gradient", "minecraft:gradient_up", NULL);
    AddRegistryTag(registry, "minecraft:pattern_item/bordure_indented", "minecraft:curly_border", NULL);
    AddRegistryTag(registry, "minecraft:pattern_item/flower", "minecraft:flower", NULL);
    AddRegistryTag(registry, "minecraft:pattern_item/globe", "minecraft:globe", NULL);
    AddRegistryTag(registry, "minecraft:pattern_item/creeper", "minecraft:creeper", NULL);
    AddRegistryTag(registry, "minecraft:pattern_item/guster", "minecraft:guster", NULL);
    AddRegistryTag(registry, "minecraft:pattern_item/flow", "minecraft:flow", NULL);
    AddRegistryTag(registry, "minecraft:pattern_item/skull", "minecraft:skull", NULL);
    AddRegistryTag(registry, "minecraft:pattern_item/mojang", "minecraft:mojang", NULL);
    AddRegistryTag(registry, "minecraft:pattern_item/piglin", "minecraft:piglin", NULL);
    AddRegistryTag(registry, "minecraft:pattern_item/field_masoned", "minecraft:bricks", NULL);
}

static void InitEnchantmentRegistry(void) {
    Registry * registry = &serv->enchantmentRegistry;
    SetRegistryName(registry, "minecraft:enchantment");
    registry->sendEntriesToClients = 1;

    AddRegistryEntry(registry, "minecraft:aqua_affinity");
    AddRegistryEntry(registry, "minecraft:bane_of_arthropods");
    AddRegistryEntry(registry, "minecraft:binding_curse");
    AddRegistryEntry(registry, "minecraft:blast_protection");
    AddRegistryEntry(registry, "minecraft:breach");
    AddRegistryEntry(registry, "minecraft:channeling");
    AddRegistryEntry(registry, "minecraft:density");
    AddRegistryEntry(registry, "minecraft:depth_strider");
    AddRegistryEntry(registry, "minecraft:efficiency");
    AddRegistryEntry(registry, "minecraft:feather_falling");
    AddRegistryEntry(registry, "minecraft:fire_aspect");
    AddRegistryEntry(registry, "minecraft:fire_protection");
    AddRegistryEntry(registry, "minecraft:flame");
    AddRegistryEntry(registry, "minecraft:fortune");
    AddRegistryEntry(registry, "minecraft:frost_walker");
    AddRegistryEntry(registry, "minecraft:impaling");
    AddRegistryEntry(registry, "minecraft:infinity");
    AddRegistryEntry(registry, "minecraft:knockback");
    AddRegistryEntry(registry, "minecraft:looting");
    AddRegistryEntry(registry, "minecraft:loyalty");
    AddRegistryEntry(registry, "minecraft:luck_of_the_sea");
    AddRegistryEntry(registry, "minecraft:lure");
    AddRegistryEntry(registry, "minecraft:mending");
    AddRegistryEntry(registry, "minecraft:multishot");
    AddRegistryEntry(registry, "minecraft:piercing");
    AddRegistryEntry(registry, "minecraft:power");
    AddRegistryEntry(registry, "minecraft:projectile_protection");
    AddRegistryEntry(registry, "minecraft:protection");
    AddRegistryEntry(registry, "minecraft:punch");
    AddRegistryEntry(registry, "minecraft:quick_charge");
    AddRegistryEntry(registry, "minecraft:respiration");
    AddRegistryEntry(registry, "minecraft:riptide");
    AddRegistryEntry(registry, "minecraft:sharpness");
    AddRegistryEntry(registry, "minecraft:silk_touch");
    AddRegistryEntry(registry, "minecraft:smite");
    AddRegistryEntry(registry, "minecraft:soul_speed");
    AddRegistryEntry(registry, "minecraft:sweeping_edge");
    AddRegistryEntry(registry, "minecraft:swift_sneak");
    AddRegistryEntry(registry, "minecraft:thorns");
    AddRegistryEntry(registry, "minecraft:unbreaking");
    AddRegistryEntry(registry, "minecraft:vanishing_curse");
    AddRegistryEntry(registry, "minecraft:wind_burst");

    AddRegistryTag(registry, "minecraft:non_treasure", "minecraft:protection", "minecraft:fire_protection", "minecraft:feather_falling", "minecraft:blast_protection", "minecraft:projectile_protection", "minecraft:respiration", "minecraft:aqua_affinity", "minecraft:thorns", "minecraft:depth_strider", "minecraft:sharpness", "minecraft:smite", "minecraft:bane_of_arthropods", "minecraft:knockback", "minecraft:fire_aspect", "minecraft:looting", "minecraft:sweeping_edge", "minecraft:efficiency", "minecraft:silk_touch", "minecraft:unbreaking", "minecraft:fortune", "minecraft:power", "minecraft:punch", "minecraft:flame", "minecraft:infinity", "minecraft:luck_of_the_sea", "minecraft:lure", "minecraft:loyalty", "minecraft:impaling", "minecraft:riptide", "minecraft:channeling", "minecraft:multishot", "minecraft:quick_charge", "minecraft:piercing", "minecraft:density", "minecraft:breach", NULL);
    AddRegistryTag(registry, "minecraft:double_trade_price", "minecraft:binding_curse", "minecraft:vanishing_curse", "minecraft:swift_sneak", "minecraft:soul_speed", "minecraft:frost_walker", "minecraft:mending", "minecraft:wind_burst", NULL);
    AddRegistryTag(registry, "minecraft:tooltip_order", "minecraft:binding_curse", "minecraft:vanishing_curse", "minecraft:riptide", "minecraft:channeling", "minecraft:wind_burst", "minecraft:frost_walker", "minecraft:sharpness", "minecraft:smite", "minecraft:bane_of_arthropods", "minecraft:impaling", "minecraft:power", "minecraft:density", "minecraft:breach", "minecraft:piercing", "minecraft:sweeping_edge", "minecraft:multishot", "minecraft:fire_aspect", "minecraft:flame", "minecraft:knockback", "minecraft:punch", "minecraft:protection", "minecraft:blast_protection", "minecraft:fire_protection", "minecraft:projectile_protection", "minecraft:feather_falling", "minecraft:fortune", "minecraft:looting", "minecraft:silk_touch", "minecraft:luck_of_the_sea", "minecraft:efficiency", "minecraft:quick_charge", "minecraft:lure", "minecraft:respiration", "minecraft:aqua_affinity", "minecraft:soul_speed", "minecraft:swift_sneak", "minecraft:depth_strider", "minecraft:thorns", "minecraft:loyalty", "minecraft:unbreaking", "minecraft:infinity", "minecraft:mending", NULL);
    AddRegistryTag(registry, "minecraft:on_traded_equipment", "minecraft:protection", "minecraft:fire_protection", "minecraft:feather_falling", "minecraft:blast_protection", "minecraft:projectile_protection", "minecraft:respiration", "minecraft:aqua_affinity", "minecraft:thorns", "minecraft:depth_strider", "minecraft:sharpness", "minecraft:smite", "minecraft:bane_of_arthropods", "minecraft:knockback", "minecraft:fire_aspect", "minecraft:looting", "minecraft:sweeping_edge", "minecraft:efficiency", "minecraft:silk_touch", "minecraft:unbreaking", "minecraft:fortune", "minecraft:power", "minecraft:punch", "minecraft:flame", "minecraft:infinity", "minecraft:luck_of_the_sea", "minecraft:lure", "minecraft:loyalty", "minecraft:impaling", "minecraft:riptide", "minecraft:channeling", "minecraft:multishot", "minecraft:quick_charge", "minecraft:piercing", "minecraft:density", "minecraft:breach", NULL);
    AddRegistryTag(registry, "minecraft:on_mob_spawn_equipment", "minecraft:protection", "minecraft:fire_protection", "minecraft:feather_falling", "minecraft:blast_protection", "minecraft:projectile_protection", "minecraft:respiration", "minecraft:aqua_affinity", "minecraft:thorns", "minecraft:depth_strider", "minecraft:sharpness", "minecraft:smite", "minecraft:bane_of_arthropods", "minecraft:knockback", "minecraft:fire_aspect", "minecraft:looting", "minecraft:sweeping_edge", "minecraft:efficiency", "minecraft:silk_touch", "minecraft:unbreaking", "minecraft:fortune", "minecraft:power", "minecraft:punch", "minecraft:flame", "minecraft:infinity", "minecraft:luck_of_the_sea", "minecraft:lure", "minecraft:loyalty", "minecraft:impaling", "minecraft:riptide", "minecraft:channeling", "minecraft:multishot", "minecraft:quick_charge", "minecraft:piercing", "minecraft:density", "minecraft:breach", NULL);
    AddRegistryTag(registry, "minecraft:curse", "minecraft:binding_curse", "minecraft:vanishing_curse", NULL);
    AddRegistryTag(registry, "minecraft:prevents_ice_melting", "minecraft:silk_touch", NULL);
    AddRegistryTag(registry, "minecraft:prevents_decorated_pot_shattering", "minecraft:silk_touch", NULL);
    AddRegistryTag(registry, "minecraft:on_random_loot", "minecraft:protection", "minecraft:fire_protection", "minecraft:feather_falling", "minecraft:blast_protection", "minecraft:projectile_protection", "minecraft:respiration", "minecraft:aqua_affinity", "minecraft:thorns", "minecraft:depth_strider", "minecraft:sharpness", "minecraft:smite", "minecraft:bane_of_arthropods", "minecraft:knockback", "minecraft:fire_aspect", "minecraft:looting", "minecraft:sweeping_edge", "minecraft:efficiency", "minecraft:silk_touch", "minecraft:unbreaking", "minecraft:fortune", "minecraft:power", "minecraft:punch", "minecraft:flame", "minecraft:infinity", "minecraft:luck_of_the_sea", "minecraft:lure", "minecraft:loyalty", "minecraft:impaling", "minecraft:riptide", "minecraft:channeling", "minecraft:multishot", "minecraft:quick_charge", "minecraft:piercing", "minecraft:density", "minecraft:breach", "minecraft:binding_curse", "minecraft:vanishing_curse", "minecraft:frost_walker", "minecraft:mending", NULL);
    AddRegistryTag(registry, "minecraft:prevents_infested_spawns", "minecraft:silk_touch", NULL);
    AddRegistryTag(registry, "minecraft:prevents_bee_spawns_when_mining", "minecraft:silk_touch", NULL);
    AddRegistryTag(registry, "minecraft:smelts_loot", "minecraft:fire_aspect", NULL);
    AddRegistryTag(registry, "minecraft:treasure", "minecraft:binding_curse", "minecraft:vanishing_curse", "minecraft:swift_sneak", "minecraft:soul_speed", "minecraft:frost_walker", "minecraft:mending", "minecraft:wind_burst", NULL);
    AddRegistryTag(registry, "minecraft:tradeable", "minecraft:protection", "minecraft:fire_protection", "minecraft:feather_falling", "minecraft:blast_protection", "minecraft:projectile_protection", "minecraft:respiration", "minecraft:aqua_affinity", "minecraft:thorns", "minecraft:depth_strider", "minecraft:sharpness", "minecraft:smite", "minecraft:bane_of_arthropods", "minecraft:knockback", "minecraft:fire_aspect", "minecraft:looting", "minecraft:sweeping_edge", "minecraft:efficiency", "minecraft:silk_touch", "minecraft:unbreaking", "minecraft:fortune", "minecraft:power", "minecraft:punch", "minecraft:flame", "minecraft:infinity", "minecraft:luck_of_the_sea", "minecraft:lure", "minecraft:loyalty", "minecraft:impaling", "minecraft:riptide", "minecraft:channeling", "minecraft:multishot", "minecraft:quick_charge", "minecraft:piercing", "minecraft:density", "minecraft:breach", "minecraft:binding_curse", "minecraft:vanishing_curse", "minecraft:frost_walker", "minecraft:mending", NULL);
    AddRegistryTag(registry, "minecraft:in_enchanting_table", "minecraft:protection", "minecraft:fire_protection", "minecraft:feather_falling", "minecraft:blast_protection", "minecraft:projectile_protection", "minecraft:respiration", "minecraft:aqua_affinity", "minecraft:thorns", "minecraft:depth_strider", "minecraft:sharpness", "minecraft:smite", "minecraft:bane_of_arthropods", "minecraft:knockback", "minecraft:fire_aspect", "minecraft:looting", "minecraft:sweeping_edge", "minecraft:efficiency", "minecraft:silk_touch", "minecraft:unbreaking", "minecraft:fortune", "minecraft:power", "minecraft:punch", "minecraft:flame", "minecraft:infinity", "minecraft:luck_of_the_sea", "minecraft:lure", "minecraft:loyalty", "minecraft:impaling", "minecraft:riptide", "minecraft:channeling", "minecraft:multishot", "minecraft:quick_charge", "minecraft:piercing", "minecraft:density", "minecraft:breach", NULL);
    AddRegistryTag(registry, "minecraft:exclusive_set/armor", "minecraft:protection", "minecraft:blast_protection", "minecraft:fire_protection", "minecraft:projectile_protection", NULL);
    AddRegistryTag(registry, "minecraft:exclusive_set/riptide", "minecraft:loyalty", "minecraft:channeling", NULL);
    AddRegistryTag(registry, "minecraft:exclusive_set/mining", "minecraft:fortune", "minecraft:silk_touch", NULL);
    AddRegistryTag(registry, "minecraft:exclusive_set/damage", "minecraft:sharpness", "minecraft:smite", "minecraft:bane_of_arthropods", "minecraft:impaling", "minecraft:density", "minecraft:breach", NULL);
    AddRegistryTag(registry, "minecraft:exclusive_set/boots", "minecraft:frost_walker", "minecraft:depth_strider", NULL);
    AddRegistryTag(registry, "minecraft:exclusive_set/crossbow", "minecraft:multishot", "minecraft:piercing", NULL);
    AddRegistryTag(registry, "minecraft:exclusive_set/bow", "minecraft:infinity", "minecraft:mending", NULL);
}

static void InitJukeboxSongRegistry(void) {
    Registry * registry = &serv->jukeboxSongRegistry;
    SetRegistryName(registry, "minecraft:jukebox_song");
    registry->sendEntriesToClients = 1;

    AddRegistryEntry(registry, "minecraft:11");
    AddRegistryEntry(registry, "minecraft:13");
    AddRegistryEntry(registry, "minecraft:5");
    AddRegistryEntry(registry, "minecraft:blocks");
    AddRegistryEntry(registry, "minecraft:cat");
    AddRegistryEntry(registry, "minecraft:chirp");
    AddRegistryEntry(registry, "minecraft:creator");
    AddRegistryEntry(registry, "minecraft:creator_music_box");
    AddRegistryEntry(registry, "minecraft:far");
    AddRegistryEntry(registry, "minecraft:mall");
    AddRegistryEntry(registry, "minecraft:mellohi");
    AddRegistryEntry(registry, "minecraft:otherside");
    AddRegistryEntry(registry, "minecraft:pigstep");
    AddRegistryEntry(registry, "minecraft:precipice");
    AddRegistryEntry(registry, "minecraft:relic");
    AddRegistryEntry(registry, "minecraft:stal");
    AddRegistryEntry(registry, "minecraft:strad");
    AddRegistryEntry(registry, "minecraft:wait");
    AddRegistryEntry(registry, "minecraft:ward");
}

static void InitInstrumentRegistry(void) {
    Registry * registry = &serv->instrumentRegistry;
    SetRegistryName(registry, "minecraft:instrument");
    registry->sendEntriesToClients = 1;

    AddRegistryEntry(registry, "minecraft:admire_goat_horn");
    AddRegistryEntry(registry, "minecraft:call_goat_horn");
    AddRegistryEntry(registry, "minecraft:dream_goat_horn");
    AddRegistryEntry(registry, "minecraft:feel_goat_horn");
    AddRegistryEntry(registry, "minecraft:ponder_goat_horn");
    AddRegistryEntry(registry, "minecraft:seek_goat_horn");
    AddRegistryEntry(registry, "minecraft:sing_goat_horn");
    AddRegistryEntry(registry, "minecraft:yearn_goat_horn");

    AddRegistryTag(registry, "minecraft:goat_horns", "minecraft:ponder_goat_horn", "minecraft:sing_goat_horn", "minecraft:seek_goat_horn", "minecraft:feel_goat_horn", "minecraft:admire_goat_horn", "minecraft:call_goat_horn", "minecraft:yearn_goat_horn", "minecraft:dream_goat_horn", NULL);
    AddRegistryTag(registry, "minecraft:screaming_goat_horns", "minecraft:admire_goat_horn", "minecraft:call_goat_horn", "minecraft:yearn_goat_horn", "minecraft:dream_goat_horn", NULL);
    AddRegistryTag(registry, "minecraft:regular_goat_horns", "minecraft:ponder_goat_horn", "minecraft:sing_goat_horn", "minecraft:seek_goat_horn", "minecraft:feel_goat_horn", NULL);
}

void InitRegistries(void) {
    InitEntityTypeRegistry();
    InitFluidRegistry();
    InitGameEventRegistry();
    InitBiomeRegistry();
    InitChatTypeRegistry();
    InitTrimPatternRegistry();
    InitTrimMaterialRegistry();
    InitWolfVariantRegistry();
    InitPaintingVariantRegistry();
    InitDimensionTypeRegistry();
    InitDamageTypeRegistry();
    InitBannerPatternRegistry();
    InitEnchantmentRegistry();
    InitJukeboxSongRegistry();
    InitInstrumentRegistry();
    // TODO(traks): additional tags to send:
    // - cat variant
    // - point of interest type
    // - future stuff?
    //
    // Not required to be sent. I think vanilla just sends all tags for all
    // registries (even if the registry entries are not network synced). When we
    // implement more registries, the tags will be sent automatically too.
}

i32 ResolveRegistryEntryId(Registry * registry, String entryName) {
    RegistryHashEntry * entry = FindEntryOrEmpty(registry, entryName);
    if (entry->nameIndex == 0) {
        // TODO(traks): instead of returning an error, perhaps we should return
        // some fallback entry id? Probably better to just error though,
        // especially for things like chunk loading where we shouldn't really be
        // loading chunks with bad data, because saving them will potentially
        // overwrite data that shouldn't be wiped?
        return -1;
    }
    return entry->id;
}

String ResolveRegistryEntryName(Registry * registry, i32 entryId) {
    // TODO(traks): return empty/null string instead of asserting?
    assert(entryId >= 0 && entryId < registry->entryCount);
    i32 nameIndex = registry->idToNameBuffer[entryId];
    return GetRegistryString(registry, nameIndex);
}
