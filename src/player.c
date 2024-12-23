#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "shared.h"
#include "nbt.h"
#include "chunk.h"
#include "packet.h"
#include "player.h"

// TODO(traks): apprioriate size
#define MAX_JOINS_PER_TICK (16)

typedef struct {
    PlayerController * players[MAX_PLAYERS];
    i32 playerCount;

    PlayerListEntry entryArray[MAX_PLAYERS];
    i32 entryCount;
    pthread_mutex_t entryMutex;

    JoinRequest joinQueue[MAX_JOINS_PER_TICK];
    i32 joinQueueCount;
    pthread_mutex_t joinQueueMutex;
} PlayerList;

static PlayerList playerList;

#define DEFAULT_PACKET_MAX_STRING_SIZE (0x7fff)

#define PACKET_CHAT_SIGNATURE_SIZE (256)

// NOTE(traks): Implicit packet IDs for ease of updating. Updating packet IDs
// manually is a pain because packet types are ordered alphabetically and Mojang
// doesn't provide an explicit list of packet IDs.
enum serverbound_packet_type {
    SBP_ACCEPT_TELEPORTATION,
    SBP_BLOCK_ENTITY_TAG_QUERY,
    SBP_BUNDLE_ITEM_SELECTED,
    SBP_CHANGE_DIFFICULTY,
    SBP_CHAT_ACK,
    SBP_CHAT_COMMAND,
    SBP_CHAT_COMMAND_SIGNED,
    SBP_CHAT,
    SBP_CHAT_SESSION_UPDATE,
    SBP_CHUNK_BATCH_RECEIVED,
    SBP_CLIENT_COMMAND,
    SBP_CLIENT_TICK_END,
    SBP_CLIENT_INFORMATION,
    SBP_COMMAND_SUGGESTION,
    SBP_CONFIGURATION_ACKNOWLEDGED,
    SBP_CONTAINER_BUTTON_CLICK,
    SBP_CONTAINER_CLICK,
    SBP_CONTAINER_CLOSE,
    SBP_CONTAINER_SLOT_STATE_CHANGED,
    SBP_COOKIE_RESPONSE,
    SBP_CUSTOM_PAYLOAD,
    SBP_DEBUG_SAMPLE_SUBSCRIPTION,
    SBP_EDIT_BOOK,
    SBP_ENTITY_TAG_QUERY,
    SBP_INTERACT,
    SBP_JIGSAW_GENERATE,
    SBP_KEEP_ALIVE,
    SBP_LOCK_DIFFICULTY,
    SBP_MOVE_PLAYER_POS,
    SBP_MOVE_PLAYER_POS_ROT,
    SBP_MOVE_PLAYER_ROT,
    SBP_MOVE_PLAYER_STATUS_ONLY,
    SBP_MOVE_VEHICLE,
    SBP_PADDLE_BOAT,
    SBP_PICK_ITEM,
    SBP_PING_REQUEST,
    SBP_PLACE_RECIPE,
    SBP_PLAYER_ABILITIES,
    SBP_PLAYER_ACTION,
    SBP_PLAYER_COMMAND,
    SBP_PLAYER_INPUT,
    SBP_PONG,
    SBP_RECIPE_BOOK_CHANGE_SETTINGS,
    SBP_RECIPE_BOOK_SEEN_RECIPE,
    SBP_RENAME_ITEM,
    SBP_RESOURCE_PACK,
    SBP_SEEN_ADVANCEMENTS,
    SBP_SELECT_TRADE,
    SBP_SET_BEACON,
    SBP_SET_CARRIED_ITEM,
    SBP_SET_COMMAND_BLOCK,
    SBP_SET_COMMAND_MINECART,
    SBP_SET_CREATIVE_MODE_SLOT,
    SBP_SET_JIGSAW_BLOCK,
    SBP_SET_STRUCTURE_BLOCK,
    SBP_SIGN_UPDATE,
    SBP_SWING,
    SBP_TELEPORT_TO_ENTITY,
    SBP_USE_ITEM_ON,
    SBP_USE_ITEM,
    SERVERBOUND_PACKET_COUNT,
};

enum clientbound_packet_type {
    CBP_BUNDLE,
    CBP_ADD_ENTITY,
    CBP_ADD_EXPERIENCE_ORB,
    CBP_ANIMATE,
    CBP_AWARD_STATS,
    CBP_BLOCK_CHANGED_ACK,
    CBP_BLOCK_DESTRUCTION,
    CBP_BLOCK_ENTITY_DATA,
    CBP_BLOCK_EVENT,
    CBP_BLOCK_UPDATE,
    CBP_BOSS_EVENT,
    CBP_CHANGE_DIFFICULTY,
    CBP_CHUNK_BATCH_FINISHED,
    CBP_CHUNK_BATCH_START,
    CBP_CHUNKS_BIOMES,
    CBP_CLEAR_TITLES,
    CBP_COMMAND_SUGGESTIONS,
    CBP_COMMANDS,
    CBP_CONTAINER_CLOSE,
    CBP_CONTAINER_SET_CONTENT,
    CBP_CONTAINER_SET_DATA,
    CBP_CONTAINER_SET_SLOT,
    CBP_COOKIE_REQUEST,
    CBP_COOLDOWN,
    CBP_CUSTOM_CHAT_COMPLETIONS,
    CBP_CUSTOM_PAYLOAD,
    CBP_DAMAGE_EVENT,
    CBP_DEBUG_SAMPLE,
    CBP_DELETE_CHAT,
    CBP_DISCONNECT,
    CBP_DISGUISED_CHAT,
    CBP_ENTITY_EVENT,
    CBP_ENTITY_POSITION_SYNC,
    CBP_EXPLODE,
    CBP_FORGET_LEVEL_CHUNK,
    CBP_GAME_EVENT,
    CBP_HORSE_SCREEN_OPEN,
    CBP_HURT_ANIMATION,
    CBP_INITIALIZE_BORDER,
    CBP_KEEP_ALIVE,
    CBP_LEVEL_CHUNK_WITH_LIGHT,
    CBP_LEVEL_EVENT,
    CBP_LEVEL_PARTICLES,
    CBP_LIGHT_UPDATE,
    CBP_LOGIN,
    CBP_MAP_ITEM_DATA,
    CBP_MERCHANT_OFFERS,
    CBP_MOVE_ENTITY_POS,
    CBP_MOVE_ENTITY_POS_ROT,
    CBP_MOVE_MINECART_ALONG_TRACK,
    CBP_MOVE_ENTITY_ROT,
    CBP_MOVE_VEHICLE,
    CBP_OPEN_BOOK,
    CBP_OPEN_SCREEN,
    CBP_OPEN_SIGN_EDITOR,
    CBP_PING,
    CBP_PONG_RESPONSE,
    CBP_PLACE_GHOST_RECIPE,
    CBP_PLAYER_ABILITIES,
    CBP_PLAYER_CHAT,
    CBP_PLAYER_COMBAT_END,
    CBP_PLAYER_COMBAT_ENTER,
    CBP_PLAYER_COMBAT_KILL,
    CBP_PLAYER_INFO_REMOVE,
    CBP_PLAYER_INFO_UPDATE,
    CBP_PLAYER_LOOK_AT,
    CBP_PLAYER_POSITION,
    CBP_PLAYER_ROTATION,
    CBP_RECIPE_BOOK_ADD,
    CBP_RECIPE_BOOK_REMOVE,
    CBP_RECIPE_BOOK_SETTINGS,
    CBP_REMOVE_ENTITIES,
    CBP_REMOVE_MOB_EFFECT,
    CBP_RESET_SCORE,
    CBP_RESOURCE_PACK_POP,
    CBP_RESOURCE_PACK_PUSH,
    CBP_RESPAWN,
    CBP_ROTATE_HEAD,
    CBP_SECTION_BLOCKS_UPDATE,
    CBP_SELECT_ADVANCEMENTS_TAB,
    CBP_SERVER_DATA,
    CBP_SET_ACTION_BAR_TEXT,
    CBP_SET_BORDER_CENTER,
    CBP_SET_BORDER_LERP_SIZE,
    CBP_SET_BORDER_SIZE,
    CBP_SET_BORDER_WARNING_DELAY,
    CBP_SET_BORDER_WARNING_DISTANCE,
    CBP_SET_CAMERA,
    CBP_SET_CHUNK_CACHE_CENTER,
    CBP_SET_CHUNK_CACHE_RADIUS,
    CBP_SET_CURSOR_ITEM,
    CBP_SET_DEFAULT_SPAWN_POSITION,
    CBP_SET_DISPLAY_OBJECTIVE,
    CBP_SET_ENTITY_DATA,
    CBP_SET_ENTITY_LINK,
    CBP_SET_ENTITY_MOTION,
    CBP_SET_EQUIPMENT,
    CBP_SET_EXPERIENCE,
    CBP_SET_HEALTH,
    CBP_SET_HELD_SLOT,
    CBP_SET_OBJECTIVE,
    CBP_SET_PASSENGERS,
    CBP_SET_PLAYER_INVENTORY,
    CBP_SET_PLAYER_TEAM,
    CBP_SET_SCORE,
    CBP_SET_SIMULATION_DISTANCE,
    CBP_SET_SUBTITLE_TEXT,
    CBP_SET_TIME,
    CBP_SET_TITLE_TEXT,
    CBP_SET_TITLES_ANIMATION,
    CBP_SOUND_ENTITY,
    CBP_SOUND,
    CBP_START_CONFIGURATION,
    CBP_STOP_SOUND,
    CBP_STORE_COOKIE,
    CBP_SYSTEM_CHAT,
    CBP_TAB_LIST,
    CBP_TAG_QUERY,
    CBP_TAKE_ITEM_ENTITY,
    CBP_TELEPORT_ENTITY,
    CBP_TICKING_STATE,
    CBP_TICKING_STEP,
    CBP_TRANSFER,
    CBP_UPDATE_ADVANCEMENTS,
    CBP_UPDATE_ATTRIBUTES,
    CBP_UPDATE_MOB_EFFECT,
    CBP_UPDATE_RECIPES,
    CBP_UPDATE_TAGS,
    CBP_PROJECTILE_POWER,
    CBP_CUSTOM_REPORT_DETAILS,
    CBP_SERVER_LINKS,
    CLIENTBOUND_PACKET_COUNT,
};

static void
nbt_write_key(Cursor * cursor, u8 tag, String key) {
    WriteU8(cursor, tag);
    WriteU16(cursor, key.size);
    WriteData(cursor, key.data, key.size);
}

static void
nbt_write_string(Cursor * cursor, String val) {
    WriteU16(cursor, val.size);
    WriteData(cursor, val.data, val.size);
}

enum ItemComponentType {
    ITEM_COMPONENT_CUSTOM_DATA,
    ITEM_COMPONENT_MAX_STACK_SIZE,
    ITEM_COMPONENT_MAX_DAMAGE,
    ITEM_COMPONENT_DAMAGE,
    ITEM_COMPONENT_UNBREAKABLE,
    ITEM_COMPONENT_CUSTOM_NAME,
    ITEM_COMPONENT_ITEM_NAME,
    ITEM_COMPONENT_LORE,
    ITEM_COMPONENT_RARITY,
    ITEM_COMPONENT_ENCHANTMENTS,
    ITEM_COMPONENT_CAN_PLACE_ON,
    ITEM_COMPONENT_CAN_BREAK,
    ITEM_COMPONENT_ATTRIBUTE_MODIFIERS,
    ITEM_COMPONENT_CUSTOM_MODEL_DATA,
    ITEM_COMPONENT_HIDE_ADDITIONAL_TOOLTIP,
    ITEM_COMPONENT_HIDE_TOOLTIP,
    ITEM_COMPONENT_REPAIR_COST,
    ITEM_COMPONENT_CREATIVE_SLOT_LOCK,
    ITEM_COMPONENT_ENCHANTMENT_GLINT_OVERRIDE,
    ITEM_COMPONENT_INTANGIBLE_PROJECTILE,
    ITEM_COMPONENT_FOOD,
    ITEM_COMPONENT_FIRE_RESISTANT,
    ITEM_COMPONENT_TOOL,
    ITEM_COMPONENT_STORED_ENCHANTMENTS,
    ITEM_COMPONENT_DYED_COLOR,
    ITEM_COMPONENT_MAP_COLOR,
    ITEM_COMPONENT_MAP_ID,
    ITEM_COMPONENT_MAP_DECORATIONS,
    ITEM_COMPONENT_MAP_POST_PROCESSING,
    ITEM_COMPONENT_CHARGED_PROJECTILES,
    ITEM_COMPONENT_BUNDLE_CONTENTS,
    ITEM_COMPONENT_POTION_CONTENTS,
    ITEM_COMPONENT_SUSPICIOUS_STEW_EFFECTS,
    ITEM_COMPONENT_WRITABLE_BOOK_CONTENT,
    ITEM_COMPONENT_WRITTEN_BOOK_CONTENT,
    ITEM_COMPONENT_TRIM,
    ITEM_COMPONENT_DEBUG_STICK_STATE,
    ITEM_COMPONENT_ENTITY_DATA,
    ITEM_COMPONENT_BUCKET_ENTITY_DATA,
    ITEM_COMPONENT_BLOCK_ENTITY_DATA,
    ITEM_COMPONENT_INSTRUMENT,
    ITEM_COMPONENT_OMINOUS_BOTTLE_AMPLIFIER,
    ITEM_COMPONENT_JUKEBOX_PLAYABLE,
    ITEM_COMPONENT_RECIPES,
    ITEM_COMPONENT_LODESTONE_TRACKER,
    ITEM_COMPONENT_FIREWORK_EXPLOSION,
    ITEM_COMPONENT_FIREWORKS,
    ITEM_COMPONENT_PROFILE,
    ITEM_COMPONENT_NOTE_BLOCK_SOUND,
    ITEM_COMPONENT_BANNER_PATTERNS,
    ITEM_COMPONENT_BASE_COLOR,
    ITEM_COMPONENT_POT_DECORATIONS,
    ITEM_COMPONENT_CONTAINER,
    ITEM_COMPONENT_BLOCK_STATE,
    ITEM_COMPONENT_BEES,
    ITEM_COMPONENT_LOCK,
    ITEM_COMPONENT_CONTAINER_LOOT,
    ITEM_COMPONENT_COUNT,
};

#define PACKET_ITEM_COMPONENT_DEFAULT (0)
#define PACKET_ITEM_COMPONENT_OVERWRITE (1)
#define PACKET_ITEM_COMPONENT_CLEAR (2)

typedef struct {
    u8 toggle;
} PacketItemComponent;

typedef struct {
    i32 size;
    i32 typeId;
    PacketItemComponent components[ITEM_COMPONENT_COUNT];
} PacketItemStack;

static PacketItemStack ReadItemStack(Cursor * cursor) {
    PacketItemStack res = {0};
    res.size = ReadVarU32(cursor);
    if (res.size < 0) {
        cursor->error = 1;
        return (PacketItemStack) {0};
    }
    if (res.size > 0) {
        res.typeId = ReadVarU32(cursor);
        if (res.typeId <= 0) {
            cursor->error = 1;
            return (PacketItemStack) {0};
        }

        i32 overwriteComponentCount = ReadVarU32(cursor);
        if (overwriteComponentCount < 0 || overwriteComponentCount > ITEM_COMPONENT_COUNT) {
            cursor->error = 1;
            return (PacketItemStack) {0};
        }
        i32 clearComponentCount = ReadVarU32(cursor);
        if (clearComponentCount < 0 || clearComponentCount > ITEM_COMPONENT_COUNT) {
            cursor->error = 1;
            return (PacketItemStack) {0};
        }

        for (i32 componentIndex = 0; componentIndex < overwriteComponentCount; componentIndex++) {
            i32 componentType = ReadVarU32(cursor);
            if (componentType < 0 || componentType >= ITEM_COMPONENT_COUNT) {
                cursor->error = 1;
                return (PacketItemStack) {0};
            }
            PacketItemComponent * component = &res.components[componentType];
            if (component->toggle != PACKET_ITEM_COMPONENT_DEFAULT) {
                cursor->error = 1;
                return (PacketItemStack) {0};
            }
            component->toggle = PACKET_ITEM_COMPONENT_OVERWRITE;
            // TODO(traks): read item component instead of erroring
            cursor->error = 1;
            return (PacketItemStack) {0};
        }
        for (i32 componentIndex = 0; componentIndex < clearComponentCount; componentIndex++) {
            i32 componentType = ReadVarU32(cursor);
            if (componentType < 0 || componentType >= ITEM_COMPONENT_COUNT) {
                cursor->error = 1;
                return (PacketItemStack) {0};
            }
            PacketItemComponent * component = &res.components[componentType];
            if (component->toggle != PACKET_ITEM_COMPONENT_DEFAULT) {
                cursor->error = 1;
                return (PacketItemStack) {0};
            }
            component->toggle = PACKET_ITEM_COMPONENT_CLEAR;
        }
    }
    return res;
}

void SetPlayerGamemode(Entity * player, i32 newGamemode) {
    if (player->gamemode != newGamemode) {
        player->changed_data |= PLAYER_GAMEMODE_CHANGED;
    }

    player->gamemode = newGamemode;
    unsigned old_flags = player->flags;

    switch (newGamemode) {
    case GAMEMODE_SURVIVAL:
        player->flags &= ~PLAYER_FLYING;
        player->flags &= ~PLAYER_CAN_FLY;
        player->flags &= ~PLAYER_INSTABUILD;
        player->flags &= ~ENTITY_INVULNERABLE;
        player->flags |= PLAYER_CAN_BUILD;
        player->changed_data |= PLAYER_ABILITIES_CHANGED;

        // @TODO(traks) should be based on active effects
        player->flags &= ~ENTITY_INVISIBLE;
        player->changed_data |= 1 << ENTITY_DATA_FLAGS;

        // @TODO(traks) show effect particles and set ambience
        break;
    case GAMEMODE_CREATIVE:
        // @NOTE(traks) actually need abilities to make creative mode players
        // able to fly
        player->flags |= PLAYER_CAN_FLY;
        player->flags |= PLAYER_INSTABUILD;
        player->flags |= ENTITY_INVULNERABLE;
        player->flags |= PLAYER_CAN_BUILD;
        player->changed_data |= PLAYER_ABILITIES_CHANGED;

        // @TODO(traks) should be based on active effects
        player->flags &= ~ENTITY_INVISIBLE;
        player->changed_data |= 1 << ENTITY_DATA_FLAGS;

        // @TODO(traks) show effect particles and set ambience
        break;
    case GAMEMODE_ADVENTURE:
        player->flags &= ~PLAYER_FLYING;
        player->flags &= ~PLAYER_CAN_FLY;
        player->flags &= ~PLAYER_INSTABUILD;
        player->flags &= ~ENTITY_INVULNERABLE;
        player->flags &= ~PLAYER_CAN_BUILD;
        player->changed_data |= PLAYER_ABILITIES_CHANGED;

        // @TODO(traks) should be based on active effects
        player->flags &= ~ENTITY_INVISIBLE;
        player->changed_data |= 1 << ENTITY_DATA_FLAGS;

        // @TODO(traks) show effect particles and set ambience
        break;
    case GAMEMODE_SPECTATOR:
        player->flags |= PLAYER_FLYING;
        player->flags |= PLAYER_CAN_FLY;
        player->flags &= ~PLAYER_INSTABUILD;
        player->flags |= ENTITY_INVULNERABLE;
        player->flags &= ~PLAYER_CAN_BUILD;
        player->changed_data |= PLAYER_ABILITIES_CHANGED;

        // @NOTE(traks) if we don't make the player invisible, their head will
        // be fully opaque for both themselves (the model rendered in the
        // inventory GUI) and to other players.
        player->flags |= ENTITY_INVISIBLE;
        player->changed_data |= 1 << ENTITY_DATA_FLAGS;

        player->flags &= ~LIVING_EFFECT_AMBIENCE;
        player->changed_data |= 1 << ENTITY_DATA_EFFECT_AMBIENCE;

        player->effect_colour = 0;
        player->changed_data |= 1 << ENTITY_DATA_EFFECT_COLOUR;
        break;
    }
}

static void ProcessMovePlayerPacket(PlayerController * control, Entity * player, f64 newX, f64 newY, f64 newZ, f32 newHeadRotX, f32 newHeadRotY, i32 onGround, i32 horizontalCollision) {
    if ((control->flags & PLAYER_CONTROL_AWAITING_TELEPORT) != 0) {
        return;
    }

    // @TODO(traks) if new x, y, z out of certain bounds, don't update player
    // x, y, z to prevent NaN errors and extreme precision loss, etc.

    player->x = newX;
    player->y = newY;
    player->z = newZ;
    player->rot_x = newHeadRotX;
    player->rot_y = newHeadRotY;
    if (onGround) {
        player->flags |= ENTITY_ON_GROUND;
    } else {
        player->flags &= ~ENTITY_ON_GROUND;
    }
    // TODO(traks): what is the horizontal collision used for?
}

static int
drop_item(Entity * player, item_stack * is, unsigned char drop_size) {
    Entity * item = TryReserveEntity(ENTITY_ITEM);
    if (item->type == ENTITY_NULL) {
        return 0;
    }

    // @TODO(traks) this has to depend on player's current pose
    double eye_y = player->y + 1.62;

    item->worldId = player->worldId;
    item->x = player->x;
    item->y = eye_y - 0.3;
    item->z = player->z;

    item->collision_width = 0.25;
    item->collision_height = 0.25;

    item->contents = *is;
    item->contents.size = drop_size;
    item->pickup_timeout = 40;

    float sin_rot_y = sinf(player->rot_y * RADIANS_PER_DEGREE);
    float cos_rot_y = cosf(player->rot_y * RADIANS_PER_DEGREE);
    float sin_rot_x = sinf(player->rot_x * RADIANS_PER_DEGREE);
    float cos_rot_x = cosf(player->rot_x * RADIANS_PER_DEGREE);

    // @TODO(traks) random offset
    item->vx = 0.3f * -sin_rot_y * cos_rot_x;
    item->vy = 0.3f * -sin_rot_x + 0.1;
    item->vz = 0.3f * cos_rot_y * cos_rot_x;
    return 1;
}

static void
AckBlockChange(PlayerController * control, u32 sequenceNumber) {
    // TODO(traks): disconnect player if sequence number negative?
    i32 signedNumber = sequenceNumber;
    control->lastAckedBlockChange = MAX(control->lastAckedBlockChange, signedNumber);
}

static void ProcessPacket(PlayerController * control, Cursor * recCursor, MemoryArena * processArena) {
    // NOTE(traks): we need to handle packets in the order in which they arive,
    // so e.g. the client can move the player to a position, perform some
    // action, and then move the player a bit further, all in the same tick.
    //
    // This seems the most natural way to do it, and gives us e.g. as much
    // information about the player's position when they perform a certain
    // action.

    Entity * player = ResolveEntity(control->entityId);
    i32 packetId = ReadVarU32(recCursor);

    switch (packetId) {
    case SBP_ACCEPT_TELEPORTATION: {
        LogInfo("Packet accept teleportation");
        u32 teleportId = ReadVarU32(recCursor);

        if ((control->flags & PLAYER_CONTROL_AWAITING_TELEPORT) && teleportId == control->lastSentTeleportId) {
            LogInfo("The teleport ID is correct!");
            control->flags &= ~PLAYER_CONTROL_AWAITING_TELEPORT;
        }
        break;
    }
    case SBP_BLOCK_ENTITY_TAG_QUERY: {
        LogInfo("Packet block entity tag query");
        i32 transactionId = ReadVarU32(recCursor);
        BlockPos blockPos = ReadBlockPos(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_BUNDLE_ITEM_SELECTED: {
        LogInfo("Packet bundle item selected");
        i32 slotId = ReadVarU32(recCursor);
        i32 selectedIndex = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CHANGE_DIFFICULTY: {
        LogInfo("Packet change difficulty");
        u8 difficulty = ReadU8(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CHAT_ACK: {
        LogInfo("Chat ack");
        u32 offset = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CHAT_COMMAND: {
        LogInfo("Packet chat command");
        String command = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CHAT_COMMAND_SIGNED: {
        LogInfo("Packet chat command signed");
        String command = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        i64 timeEpoch = ReadU64(recCursor);
        i64 salt = ReadU64(recCursor);
        i32 signedArgumentCount = ReadVarU32(recCursor);
        if (signedArgumentCount < 0 || signedArgumentCount >= 8) {
            recCursor->error = 1;
            break;
        }
        for (i32 argIndex = 0; argIndex < signedArgumentCount; argIndex++) {
            String argumentName = ReadVarString(recCursor, 16);
            u8 * signature = ReadData(recCursor, PACKET_CHAT_SIGNATURE_SIZE);
        }
        i32 seenMessageCount = ReadVarU32(recCursor);
        u8 * seenMessageAcks = ReadData(recCursor, 3);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CHAT: {
        String chat = ReadVarString(recCursor, 256);
        i64 timeEpoch = ReadU64(recCursor);
        i64 salt = ReadU64(recCursor);
        if (ReadBool(recCursor)) {
            u8 * signature = ReadData(recCursor, PACKET_CHAT_SIGNATURE_SIZE);
        }
        i32 seenMessageCount = ReadVarU32(recCursor);
        u8 * seenMessageAcks = ReadData(recCursor, 3);

        // TODO(traks): filter out bad characters from the message, handle
        // signature, seen messages, etc.

        if (serv->global_msg_count < (i32) ARRAY_SIZE(serv->global_msgs)) {
            global_msg * msg = serv->global_msgs + serv->global_msg_count;
            serv->global_msg_count++;
            int text_size = snprintf(
                    (void *) msg->text, sizeof msg->text,
                    "<%.*s> %.*s",
                    (int) control->username_size,
                    control->username,
                    (int) chat.size, chat.data);
            msg->size = text_size;
            // LogInfo("%.*s", msg->size, msg->text);
        }
        break;
    }
    case SBP_CHAT_SESSION_UPDATE: {
        LogInfo("Packet chat session update");
        UUID uuid = ReadUUID(recCursor);
        i64 expirationEpoch = ReadU64(recCursor);
        i32 publicKeySize = ReadVarU32(recCursor);
        if (publicKeySize < 0 || publicKeySize > 512) {
            recCursor->error = 1;
            break;
        }
        u8 * publicKey = ReadData(recCursor, publicKeySize);
        i32 keySignatureSize = ReadVarU32(recCursor);
        if (keySignatureSize < 0 || keySignatureSize > 4096) {
            recCursor->error = 1;
            break;
        }
        u8 * keySignature = ReadData(recCursor, keySignatureSize);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CHUNK_BATCH_RECEIVED: {
        LogInfo("Packet chunk batch received");
        f32 clientDesiredChunksPerTick = ReadF32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CLIENT_COMMAND: {
        LogInfo("Packet client command");
        i32 action = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CLIENT_TICK_END: {
        // TODO(traks): handle packet
        break;
    }
    case SBP_CLIENT_INFORMATION: {
        LogInfo("Packet client information");
        String locale = ReadVarString(recCursor, MAX_PLAYER_LOCALE_SIZE);
        memcpy(control->locale, locale.data, locale.size);
        control->localeSize = locale.size;
        // NOTE(traks): View distance is without the extra border of chunks,
        // while chunk cache radius is with the extra border of
        // chunks. This clamps the view distance between the minimum
        // of 2 and the server maximum.
        i32 viewDistance = ReadU8(recCursor);
        control->nextChunkCacheRadius = MIN(MAX(viewDistance, 2), MAX_RENDER_DISTANCE) + 1;
        control->chatMode = ReadVarU32(recCursor);
        control->seesChatColours = ReadBool(recCursor);
        control->skinCustomisation = ReadU8(recCursor);
        control->mainHand = ReadVarU32(recCursor);
        control->textFiltering = ReadBool(recCursor);
        control->showInStatusList = ReadBool(recCursor);
        control->particleStatus = ReadVarU32(recCursor);
        break;
    }
    case SBP_COMMAND_SUGGESTION: {
        LogInfo("Packet command suggestion");
        i32 transactionId = ReadVarU32(recCursor);
        String command = ReadVarString(recCursor, 32500);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CONFIGURATION_ACKNOWLEDGED: {
        LogInfo("Packet configuration acknowledged");
        // TODO(traks): handle packet
        break;
    }
    case SBP_CONTAINER_BUTTON_CLICK: {
        LogInfo("Packet container button click");
        i32 containerId = ReadVarU32(recCursor);
        i32 buttonId = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CONTAINER_CLICK: {
        LogInfo("Packet container click");
        u8 containerId = ReadVarU32(recCursor);
        i32 statId = ReadVarU32(recCursor);
        i32 slot = ReadU16(recCursor);
        i32 button = ReadU8(recCursor);
        i32 clickType = ReadVarU32(recCursor);
        i32 changedSlotCount = ReadVarU32(recCursor);
        if (changedSlotCount > 128 || changedSlotCount < 0) {
            recCursor->error = 1;
            break;
        }
        for (int changeIndex = 0; changeIndex < changedSlotCount; changeIndex++) {
            i32 changedSlot = ReadU16(recCursor);
            PacketItemStack slotItem = ReadItemStack(recCursor);
        }
        PacketItemStack cursor = ReadItemStack(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CONTAINER_CLOSE: {
        LogInfo("Packet container close");
        i32 containerId = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_CONTAINER_SLOT_STATE_CHANGED: {
        LogInfo("Packet contained slot state changed");
        i32 slotId = ReadVarU32(recCursor);
        i32 containerId = ReadVarU32(recCursor);
        i32 newState = ReadBool(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_COOKIE_RESPONSE: {
        LogInfo("Packet cookie response");
        String id = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        if (ReadBool(recCursor)) {
            i32 payloadSize = ReadVarU32(recCursor);
            if (payloadSize < 0 || payloadSize > 5120) {
                recCursor->error = 1;
                break;
            }
            u8 * payload = ReadData(recCursor, payloadSize);
        }
        // TODO(traks): handle packet
        break;
    }
    case SBP_CUSTOM_PAYLOAD: {
        LogInfo("Packet custom payload");
        String id = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        i32 payloadSize = CursorRemaining(recCursor);
        if (payloadSize > 32767) {
            recCursor->error = 1;
            break;
        }
        u8 * payload = ReadData(recCursor, payloadSize);
        // TODO(traks): handle packet
        break;
    }
    case SBP_DEBUG_SAMPLE_SUBSCRIPTION: {
        LogInfo("Packet debug sample subscription");
        i32 type = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_EDIT_BOOK: {
        LogInfo("Packet edit book");
        i32 slot = ReadVarU32(recCursor);
        i32 pageCount = ReadVarU32(recCursor);
        if (pageCount < 0 || pageCount > 100) {
            recCursor->error = 1;
            break;
        }
        for (i32 pageIndex = 0; pageIndex < pageCount; pageIndex++) {
            String page = ReadVarString(recCursor, 1024);
        }
        if (ReadU8(recCursor)) {
            String title = ReadVarString(recCursor, 32);
        }
        // TODO(traks): handle packet
        break;
    }
    case SBP_ENTITY_TAG_QUERY: {
        LogInfo("Packet entity tag query");
        i32 transactionId = ReadVarU32(recCursor);
        i32 entityId = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_INTERACT: {
        LogInfo("Packet interact");
        i32 entityId = ReadVarU32(recCursor);
        i32 action = ReadVarU32(recCursor);
        switch (action) {
        case 0: { // interact
            i32 hand = ReadVarU32(recCursor);
            u8 isSecondaryAction = ReadU8(recCursor);
            break;
        }
        case 1: { // attack
            u8 isSecondaryAction = ReadU8(recCursor);
            break;
        }
        case 2: { // interact at
            float x = ReadF32(recCursor);
            float y = ReadF32(recCursor);
            float z = ReadF32(recCursor);
            i32 hand = ReadVarU32(recCursor);
            u8 isSecondaryAction = ReadU8(recCursor);
            break;
        }
        default:
            recCursor->error = 1;
        }
        // TODO(traks): handle packet
        break;
    }
    case SBP_JIGSAW_GENERATE: {
        LogInfo("Packet jigsaw generate");
        BlockPos blockPos = ReadBlockPos(recCursor);
        i32 levels = ReadVarU32(recCursor);
        i32 keepJigsaws = ReadBool(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_KEEP_ALIVE: {
        i64 transactionId = ReadU64(recCursor);
        if (control->last_keep_alive_sent_tick == transactionId) {
            control->flags |= PLAYER_CONTROL_GOT_ALIVE_RESPONSE;
        }
        break;
    }
    case SBP_LOCK_DIFFICULTY: {
        LogInfo("Packet lock difficulty");
        i32 locked = ReadBool(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_MOVE_PLAYER_POS: {
        double x = ReadF64(recCursor);
        double y = ReadF64(recCursor);
        double z = ReadF64(recCursor);
        u8 flags = ReadU8(recCursor);
        i32 onGround = !!(flags & 0x1);
        i32 horizontalCollision = !!(flags & 0x2);
        ProcessMovePlayerPacket(control, player, x, y, z, player->rot_x, player->rot_y, onGround, horizontalCollision);
        break;
    }
    case SBP_MOVE_PLAYER_POS_ROT: {
        double x = ReadF64(recCursor);
        double y = ReadF64(recCursor);
        double z = ReadF64(recCursor);
        float headRotY = ReadF32(recCursor);
        float headRotX = ReadF32(recCursor);
        u8 flags = ReadU8(recCursor);
        i32 onGround = !!(flags & 0x1);
        i32 horizontalCollision = !!(flags & 0x2);
        ProcessMovePlayerPacket(control, player, x, y, z, headRotX, headRotY, onGround, horizontalCollision);
        break;
    }
    case SBP_MOVE_PLAYER_ROT: {
        float headRotY = ReadF32(recCursor);
        float headRotX = ReadF32(recCursor);
        u8 flags = ReadU8(recCursor);
        i32 onGround = !!(flags & 0x1);
        i32 horizontalCollision = !!(flags & 0x2);
        ProcessMovePlayerPacket(control, player, player->x, player->y, player->z, headRotX, headRotY, onGround, horizontalCollision);
        break;
    }
    case SBP_MOVE_PLAYER_STATUS_ONLY: {
        u8 flags = ReadU8(recCursor);
        i32 onGround = !!(flags & 0x1);
        i32 horizontalCollision = !!(flags & 0x2);
        ProcessMovePlayerPacket(control, player, player->x, player->y, player->z, player->rot_x, player->rot_y, onGround, horizontalCollision);
        break;
    }
    case SBP_MOVE_VEHICLE: {
        LogInfo("Packet move vehicle");
        double x = ReadF64(recCursor);
        double y = ReadF64(recCursor);
        double z = ReadF64(recCursor);
        float rotY = ReadF32(recCursor);
        float rotX = ReadF32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_PADDLE_BOAT: {
        LogInfo("Packet paddle boat");
        i32 leftPaddleTurning = ReadBool(recCursor);
        i32 rightPaddleTurning = ReadBool(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_PICK_ITEM: {
        LogInfo("Packet pick item");
        i32 slot = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_PING_REQUEST: {
        LogInfo("Packet ping request");
        i64 payload = ReadU64(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_PLACE_RECIPE: {
        LogInfo("Packet place recipe");
        i32 containerId = ReadVarU32(recCursor);
        String recipe = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        i32 shiftDown = ReadBool(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_PLAYER_ABILITIES: {
        LogInfo("Packet player abilities");
        u8 flags = ReadU8(recCursor);
        i32 flying = !!(flags & 0x2);
        // TODO(traks): validate whether the player can toggle fly mode
        if (flying) {
            if (player->flags & PLAYER_CAN_FLY) {
                player->flags |= PLAYER_FLYING;
            } else {
                player->changed_data |= PLAYER_ABILITIES_CHANGED;
            }
        } else {
            player->flags &= ~PLAYER_FLYING;
        }
        break;
    }
    case SBP_PLAYER_ACTION: {
        i32 action = ReadVarU32(recCursor);
        // TODO(traks): validate block pos inside world
        BlockPos blockPos = ReadBlockPos(recCursor);
        i32 direction = ReadU8(recCursor);
        i32 sequenceNumber = ReadVarU32(recCursor);

        // NOTE(traks): destroying blocks in survival works as follows:
        //
        //  1. first client sends start packet
        //  2. if player stops mining before block is broken, client sends
        //     abort packet
        //  3. if player breaks block on client side, then stop packet is sent
        //
        // However, if abort packet is sent, and the player starts mining the
        // same block again (without mining another block in between), no start
        // packet is sent, but a stop packet is sent and the server is supposed
        // to break the block!
        //
        // It seems that stopping mining and continuing mining with saved mining
        // progress, is handled client side and not server side.

        // TODO(traks): does the above still hold in the latest version of the
        // game? That note was written a while back

        switch (action) {
        case 0: { // start destroy block
            // NOTE(traks): The player started mining the block. If the player
            // is in creative mode, the stop and abort packets are not sent.
            // TODO(traks): implementation for other gamemodes
            if (player->gamemode == GAMEMODE_CREATIVE) {
                // TODO(traks): ensure block pos is close to the
                // player and the chunk is sent to the player. Ensure not in
                // teleport, etc. Might get handled automatically if we deal
                // with the world ID properly (e.g. it's an invalid world while
                // the player is teleporting).

                WorldBlockPos destroyPos = {.worldId = player->worldId, .xyz = blockPos};
                SetBlockResult destroy = WorldSetBlockState(destroyPos, get_default_block_state(BLOCK_AIR));
                if (destroy.failed) {
                    break;
                }

                // TODO(traks): better block breaking logic. E.g. new state
                // should be water source block if waterlogged block is broken.
                // Should move this stuff to some generic block breaking
                // function, so we can do proper block updating of redstone dust
                // and stuff.
                i32 maxUpdates = 512;
                block_update_context buc = {
                    .blocks_to_update = MallocInArena(processArena, maxUpdates * sizeof (block_update)),
                    .update_count = 0,
                    .max_updates = maxUpdates
                };
                push_direct_neighbour_block_updates(destroyPos, &buc);
                propagate_block_updates(&buc);
                AckBlockChange(control, sequenceNumber);
            }
            break;
        }
        case 1: { // abort destroy block
            // NOTE(traks): player stopped mining the block before it breaks
            // TODO(traks): handle
            AckBlockChange(control, sequenceNumber);
            break;
        }
        case 2: { // stop destroy block
            // NOTE(traks): player stopped mining the block because it broke
            // TODO(traks): handle
            AckBlockChange(control, sequenceNumber);
            break;
        }
        case 3: { // drop all items
            i32 selectedSlot = player->selected_slot;
            item_stack * is = player->slots + selectedSlot;
            // NOTE(traks): client updates its view of the item stack size
            // itself, so no need to send updates for the slot if nothing
            // special happens
            if (is->size > 0) {
                if (drop_item(player, is, is->size)) {
                    is->size = 0;
                    is->type = ITEM_AIR;
                } else {
                    player->slots_needing_update |= (u64) 1 << selectedSlot;
                }
            }
            break;
        }
        case 4: { // drop item
            i32 selectedSlot = player->selected_slot;
            item_stack * is = player->slots + selectedSlot;
            // NOTE(traks): client updates its view of the item stack size
            // itself, so no need to send updates for the slot if nothing
            // special happens
            if (is->size > 0) {
                if (drop_item(player, is, 1)) {
                    is->size -= 1;
                    if (is->size == 0) {
                        is->type = ITEM_AIR;
                    }
                } else {
                    player->slots_needing_update |= (u64) 1 << selectedSlot;
                }
            }
            break;
        }
        case 5: { // release use item
            // TODO(traks): handle
            break;
        }
        case 6: { // swap held items
            i32 selectedSlot = player->selected_slot;
            item_stack * selectedStack = player->slots + selectedSlot;
            item_stack * offHandStack = player->slots + PLAYER_OFF_HAND_SLOT;
            item_stack selectedCopy = *selectedStack;
            *selectedStack = *offHandStack;
            *offHandStack = selectedCopy;
            // NOTE(traks): client doesn't update its view of the inventory for
            // this packet, so send updates to the client
            player->slots_needing_update |= (u64) 1 << selectedSlot;
            player->slots_needing_update |= (u64) 1 << PLAYER_OFF_HAND_SLOT;
            break;
        }
        default:
            recCursor->error = 1;
        }
        break;
    }
    case SBP_PLAYER_COMMAND: {
        i32 id = ReadVarU32(recCursor);
        i32 action = ReadVarU32(recCursor);
        i32 data = ReadVarU32(recCursor);

        switch (action) {
        case 0: // press shift key
            player->flags |= ENTITY_SHIFTING;
            player->changed_data |= 1 << ENTITY_DATA_FLAGS;
            if (!(player->flags & PLAYER_FLYING)) {
                player->pose = ENTITY_POSE_CROUCHING;
                player->changed_data |= 1 << ENTITY_DATA_POSE;
            }
            break;
        case 1: // release shift key
            player->flags &= ~ENTITY_SHIFTING;
            player->changed_data |= 1 << ENTITY_DATA_FLAGS;
            player->pose = ENTITY_POSE_STANDING;
            player->changed_data |= 1 << ENTITY_DATA_POSE;
            break;
        case 2: // stop sleeping
            // TODO(traks): handle
            break;
        case 3: // start sprinting
            player->flags |= ENTITY_SPRINTING;
            player->changed_data |= 1 << ENTITY_DATA_FLAGS;
            break;
        case 4: // stop sprinting
            player->flags &= ~ENTITY_SPRINTING;
            player->changed_data |= 1 << ENTITY_DATA_FLAGS;
            break;
        case 5: // start riding jump
            // TODO(traks): handle
            break;
        case 6: // stop riding jump
            // TODO(traks): handle
            break;
        case 7: // open inventory
            // TODO(traks): handle
            break;
        case 8: // start fall flying
            // TODO(traks): handle
            break;
        default:
            recCursor->error = 1;
        }
        break;
    }
    case SBP_PLAYER_INPUT: {
        u8 flags = ReadU8(recCursor);
        i32 forward = !!(flags & (1 << 0));
        i32 backward = !!(flags & (1 << 1));
        i32 left = !!(flags & (1 << 2));
        i32 right = !!(flags & (1 << 3));
        i32 jump = !!(flags & (1 << 4));
        i32 shift = !!(flags & (1 << 5));
        i32 sprint = !!(flags & (1 << 6));
        // TODO(traks): handle packet
        break;
    }
    case SBP_PONG: {
        LogInfo("Packet pong");
        i32 transactionId = ReadU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_RECIPE_BOOK_CHANGE_SETTINGS: {
        LogInfo("Packet recipe book change settings");
        i32 bookType = ReadVarU32(recCursor);
        i32 open = ReadBool(recCursor);
        i32 filtering = ReadBool(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_RECIPE_BOOK_SEEN_RECIPE: {
        LogInfo("Packet recipe book seen recipe");
        i32 recipeId = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_RENAME_ITEM: {
        LogInfo("Packet rename item");
        String name = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        // TODO(traks): handle packet
        break;
    }
    case SBP_RESOURCE_PACK: {
        LogInfo("Packet resource pack");
        UUID packUuid = ReadUUID(recCursor);
        i32 resultType = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_SEEN_ADVANCEMENTS: {
        LogInfo("Packet seen advancements");
        i32 action = ReadVarU32(recCursor);
        switch (action) {
        case 0: { // open tab
            String tabName = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
            break;
        }
        case 1: { // close
            break;
        }
        default: {
            recCursor->error = 1;
        }
        }
        // TODO(traks): handle packet
        break;
    }
    case SBP_SELECT_TRADE: {
        LogInfo("Packet select trade");
        i32 slot = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_SET_BEACON: {
        LogInfo("Packet set beacon");
        if (ReadBool(recCursor)) {
            i32 primaryEffectId = ReadVarU32(recCursor);
        }
        if (ReadBool(recCursor)) {
            i32 secondaryEffectId = ReadVarU32(recCursor);
        }
        // TODO(traks): handle packet
        break;
    }
    case SBP_SET_CARRIED_ITEM: {
        LogInfo("Set carried item");
        i32 slot = ReadU16(recCursor);
        if (slot < 0 || slot > PLAYER_LAST_HOTBAR_SLOT - PLAYER_FIRST_HOTBAR_SLOT) {
            recCursor->error = 1;
            break;
        }
        player->selected_slot = PLAYER_FIRST_HOTBAR_SLOT + slot;
        break;
    }
    case SBP_SET_COMMAND_BLOCK: {
        LogInfo("Packet set command block");
        BlockPos blockPos = ReadBlockPos(recCursor);
        String command = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        i32 mode = ReadVarU32(recCursor);
        u8 flags = ReadU8(recCursor);
        i32 trackOutput = !!(flags & 0x1);
        i32 conditional = !!(flags & 0x2);
        i32 automatic = !!(flags & 0x4);
        // TODO(traks): handle packet
        break;
    }
    case SBP_SET_COMMAND_MINECART: {
        LogInfo("Packet set command minecart");
        i32 entityId = ReadVarU32(recCursor);
        String command = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        i32 trackOutput = ReadBool(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_SET_CREATIVE_MODE_SLOT: {
        LogInfo("Set creative mode slot");
        i32 slot = ReadU16(recCursor);

        if (slot < 0 || slot >= PLAYER_SLOTS) {
            recCursor->error = 1;
            break;
        }

        PacketItemStack stack = ReadItemStack(recCursor);
        item_stack * is = player->slots + slot;
        *is = (item_stack) {0};
        // TODO(traks): item components
        is->size = stack.size;
        is->type = stack.typeId;

        String itemTypeName = ResolveRegistryEntryName(&serv->itemRegistry, is->type);
        LogInfo("Set creative slot: %.*s", (int) itemTypeName.size, itemTypeName.data);
        break;
    }
    case SBP_SET_JIGSAW_BLOCK: {
        LogInfo("Packet set jigsaw block");
        BlockPos blockPos = ReadBlockPos(recCursor);
        String name = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        String target = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        String pool = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        String finalState = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        String joint = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        i32 selectionPriority = ReadVarU32(recCursor);
        i32 placementPriority = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_SET_STRUCTURE_BLOCK: {
        LogInfo("Packet set structure block");
        BlockPos blockPos = ReadBlockPos(recCursor);
        i32 updateType = ReadVarU32(recCursor);
        i32 mode = ReadVarU32(recCursor);
        String name = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        i32 offsetX = (i8) ReadU8(recCursor);
        i32 offsetY = (i8) ReadU8(recCursor);
        i32 offsetZ = (i8) ReadU8(recCursor);
        i32 sizeX = ReadU8(recCursor);
        i32 sizeY = ReadU8(recCursor);
        i32 sizeZ = ReadU8(recCursor);
        i32 mirror = ReadVarU32(recCursor);
        i32 rotation = ReadVarU32(recCursor);
        String data = ReadVarString(recCursor, DEFAULT_PACKET_MAX_STRING_SIZE);
        f32 integrity = ReadF32(recCursor);
        i64 seed = ReadVarU64(recCursor);
        u8 flags = ReadU8(recCursor);
        i32 ignoreEntities = !!(flags & 0x1);
        i32 showAir = !!(flags & 0x2);
        i32 showBoundingBox = !!(flags & 0x4);
        // TODO(traks): handle packet
        break;
    }
    case SBP_SIGN_UPDATE: {
        LogInfo("Packet sign update");
        BlockPos blockPos = ReadBlockPos(recCursor);
        i32 isFront = ReadBool(recCursor);
        String lines[4];
        for (i32 lineIndex = 0; lineIndex < (i32) ARRAY_SIZE(lines); lineIndex++) {
            lines[lineIndex] = ReadVarString(recCursor, 384);
        }
        // TODO(traks): handle packet
        break;
    }
    case SBP_SWING: {
        // LogInfo("Packet swing");
        i32 hand = ReadVarU32(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_TELEPORT_TO_ENTITY: {
        LogInfo("Packet teleport to entity");
        UUID targetUuid = ReadUUID(recCursor);
        // TODO(traks): handle packet
        break;
    }
    case SBP_USE_ITEM_ON: {
        LogInfo("Packet use item on");
        i32 hand = ReadVarU32(recCursor);
        BlockPos clickedPos = ReadBlockPos(recCursor);
        i32 clickedFace = ReadVarU32(recCursor);
        float clickOffsetX = ReadF32(recCursor);
        float clickOffsetY = ReadF32(recCursor);
        float clickOffsetZ = ReadF32(recCursor);
        // TODO(traks): figure out what this is used for
        u8 isInside = ReadU8(recCursor);
        u8 worldBorderHit = ReadU8(recCursor);
        i32 sequenceNumber = ReadVarU32(recCursor);

        // TODO(traks): if we cancel at any point and don't kick the
        // client, send some packets to the client to make the
        // original blocks reappear, otherwise we'll get a desync

        if (hand != 0 && hand != 1) {
            recCursor->error = 1;
            break;
        }
        if (clickedFace < 0 || clickedFace >= 6) {
            recCursor->error = 1;
            break;
        }
        if (clickOffsetX < 0 || clickOffsetX > 1
                || clickOffsetY < 0 || clickOffsetY > 1
                || clickOffsetZ < 0 || clickOffsetZ > 1) {
            recCursor->error = 1;
            break;
        }

        // TODO(traks): ensure clicked pos is inside the world and the eventual
        // target position as well, so no assertions fire when setting the block
        // in the chunk (e.g. because of negative y)

        process_use_item_on_packet(control, hand, clickedPos, clickedFace, clickOffsetX, clickOffsetY, clickOffsetZ, isInside, processArena);
        AckBlockChange(control, sequenceNumber);
        break;
    }
    case SBP_USE_ITEM: {
        LogInfo("Packet use item");
        i32 hand = ReadVarU32(recCursor);
        i32 sequenceNumber = ReadVarU32(recCursor);
        f32 rotY = ReadF32(recCursor);
        f32 rotX = ReadF32(recCursor);

        AckBlockChange(control, sequenceNumber);
        break;
    }
    default: {
        LogInfo("Unknown player packet id %jd", (intmax_t) packetId);
        recCursor->error = 1;
    }
    }
}

static int
chunk_cache_index(ChunkPos pos) {
    // Do some remainder operations first so we don't integer overflow. Note
    // that the remainder operator can produce negative numbers.
    int n = MAX_CHUNK_CACHE_DIAM * MAX_CHUNK_CACHE_DIAM;
    long x = (pos.x % n) + n;
    long z = (pos.z % n) + n;
    return (x * MAX_CHUNK_CACHE_DIAM + z) % n;
}

static void FinishPlayerPacket(Cursor * cursor, PlayerController * control) {
    FinishPacket(cursor, !!(control->flags & PLAYER_CONTROL_PACKET_COMPRESSION));
}

static void PackLightSection(Cursor * targetCursor, u8 * source) {
    WriteVarU32(targetCursor, 2048);
    u8 * target = targetCursor->data + targetCursor->index;
    if (CursorSkip(targetCursor, 2048)) {
        for (i32 i = 0; i < 2048; i++) {
            target[i] = GetSectionLight(source, 2 * i) | (GetSectionLight(source, 2 * i + 1) << 4);
        }
    }
}

void
send_chunk_fully(Cursor * send_cursor, Chunk * ch,
        PlayerController * control, MemoryArena * tick_arena) {
    BeginTimings(SendChunkFully);

    // TODO(traks): make uncompressed data as compact as possible, so the
    // compressor doesn't choke on these packets. As of writing this, these
    // packets are ~170KiB, and take ~2ms to compress. If we send a chunk to
    // 1000 players every tick, that's 2 seconds in a tick of 50ms, so we need
    // 40 CPU cores for that.
    BeginPacket(send_cursor, CBP_LEVEL_CHUNK_WITH_LIGHT);
    WriteU32(send_cursor, ch->pos.x);
    WriteU32(send_cursor, ch->pos.z);

    BeginTimings(WriteHeightMap);

    // @NOTE(traks) height map NBT
    {
        // NOTE(traks): the protocol doesn't include the name of the root
        // compound
        WriteU8(send_cursor, NBT_TAG_COMPOUND);

        nbt_write_key(send_cursor, NBT_TAG_LONG_ARRAY, STR("MOTION_BLOCKING"));
        // number of elements in long array
        i32 bitsPerValue = CeilLog2U32(WORLD_HEIGHT + 1);
        i32 valuesPerLong = 64 / bitsPerValue;
        i32 longs = (16 * 16  + valuesPerLong - 1) / valuesPerLong;
        WriteU32(send_cursor, longs);
        u64 val = 0;
        int offset = 0;

        for (int j = 0; j < 16 * 16; j++) {
            u64 height = ch->motion_blocking_height_map[j];
            val |= height << offset;
            offset += bitsPerValue;

            if (offset > 64 - bitsPerValue) {
                WriteU64(send_cursor, val);
                val = 0;
                offset = 0;
            }
        }

        if (offset != 0) {
            WriteU64(send_cursor, val);
        }

        WriteU8(send_cursor, NBT_TAG_END);
    }

    EndTimings(WriteHeightMap);

    // @NOTE(traks) calculate size of block data

    BeginTimings(WriteBlocks);

    // calculate total size of chunk section data
    i32 section_data_size = 0;
    // @TODO(traks) compute bits per block using block type table
    int bits_per_block = CeilLog2U32(serv->vanilla_block_state_count);
    int blocks_per_long = 64 / bits_per_block;

    for (int i = 0; i < SECTIONS_PER_CHUNK; i++) {
        ChunkSection * section = ch->sections + i;
        if (section->nonAirCount == 0) {
            section_data_size += 2 + 1 + 1 + 1;
        } else {
            // size of non-air count + bits per block
            section_data_size += 2 + 1;
            // size of block state data in longs
            int longs = (16 * 16 * 16 + blocks_per_long - 1) / blocks_per_long;
            section_data_size += VarU32Size(longs);
            // number of bytes used to store block state data
            section_data_size += longs * 8;
        }

        // @NOTE(traks) size of biome data
        section_data_size += 1 + 1 + 1;
    }

    // @NOTE(traks) write block data

    WriteVarU32(send_cursor, section_data_size);

    for (i32 i = 0; i < SECTIONS_PER_CHUNK; i++) {
        ChunkSection * section = ch->sections + i;
        WriteU16(send_cursor, section->nonAirCount); // # of non-air blocks
        if (section->nonAirCount == 0) {
            WriteU8(send_cursor, 0);
            WriteVarU32(send_cursor, 0);
            WriteVarU32(send_cursor, 0);
        } else {
            WriteU8(send_cursor, bits_per_block);

            // number of longs used for the block states
            int longs = (16 * 16 * 16 + blocks_per_long - 1) / blocks_per_long;
            WriteVarU32(send_cursor, longs);
            u64 val = 0;
            int offset = 0;
            assert(blocks_per_long == 4);

            u8 * cursorData = send_cursor->data + send_cursor->index;
            if (CursorSkip(send_cursor, longs * 8)) {
                for (i32 longIndex = 0; longIndex < longs; longIndex++) {
                    u16 blockStates[4];
                    blockStates[0] = SectionGetBlockState(&section->blocks, 4 * longIndex + 0);
                    blockStates[1] = SectionGetBlockState(&section->blocks, 4 * longIndex + 1);
                    blockStates[2] = SectionGetBlockState(&section->blocks, 4 * longIndex + 2);
                    blockStates[3] = SectionGetBlockState(&section->blocks, 4 * longIndex + 3);
                    u64 longValue = ((u64) blockStates[0])
                            | ((u64) blockStates[1] << 15)
                            | ((u64) blockStates[2] << 30)
                            | ((u64) blockStates[3] << 45);
                    WriteDirectU64(cursorData + (8 * longIndex), longValue);
                }
            }
        }

        // @NOTE(traks) write biome data. Currently we just write all plains
        // biome -> 0 bit palette
        WriteU8(send_cursor, 0);
        WriteVarU32(send_cursor, 1);
        WriteVarU32(send_cursor, 0);
    }

    // number of block entities
    WriteVarU32(send_cursor, 0);

    EndTimings(WriteBlocks);

    // @NOTE(traks) now write lighting data

    // NOTE(traks): not sure why the server is even sending light values to the
    // client. The client can calculate it itself. It almost takes longer to
    // compress these packets than to compute the light. Also is a waste of
    // bandwidth.

    BeginTimings(WriteLight);

    i32 lightSections = LIGHT_SECTIONS_PER_CHUNK;
    // @NOTE(traks) light sections present as arrays in this packet
    u64 sky_light_mask = (0x1 << lightSections) - 1;
    u64 block_light_mask = (0x1 << lightSections) - 1;
    // @NOTE(traks) sections with all light values equal to 0
    u64 zero_sky_light_mask = 0;
    u64 zero_block_light_mask = 0;

    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, sky_light_mask);
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, block_light_mask);
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, zero_sky_light_mask);
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, zero_block_light_mask);

    WriteVarU32(send_cursor, lightSections);
    for (int sectionIndex = 0; sectionIndex < lightSections; sectionIndex++) {
        LightSection * section = ch->lightSections + sectionIndex;
        PackLightSection(send_cursor, section->skyLight);
    }

    WriteVarU32(send_cursor, lightSections);
    for (int sectionIndex = 0; sectionIndex < lightSections; sectionIndex++) {
        LightSection * section = ch->lightSections + sectionIndex;
        PackLightSection(send_cursor, section->blockLight);
    }

    EndTimings(WriteLight);

    FinishPlayerPacket(send_cursor, control);

    EndTimings(SendChunkFully);
}

static void
send_light_update(Cursor * send_cursor, ChunkPos pos, Chunk * ch,
        PlayerController * control, MemoryArena * tick_arena) {
    BeginTimings(SendLightUpdate);

    // @TODO(traks) send the real lighting data

    BeginPacket(send_cursor, CBP_LIGHT_UPDATE);
    WriteVarU32(send_cursor, pos.x);
    WriteVarU32(send_cursor, pos.z);

    // light sections present as arrays in this packet
    // @NOTE(traks) add 2 for light below the world and light above the world
    i32 lightSections = SECTIONS_PER_CHUNK + 2;
    u64 sky_light_mask = (0x1 << lightSections) - 1;
    u64 block_light_mask = (0x1 << lightSections) - 1;
    // sections with all light values equal to 0
    u64 zero_sky_light_mask = 0;
    u64 zero_block_light_mask = 0;

    WriteU8(send_cursor, 1); // trust edges
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, sky_light_mask);
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, block_light_mask);
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, zero_sky_light_mask);
    WriteVarU32(send_cursor, 1);
    WriteU64(send_cursor, zero_block_light_mask);

    WriteVarU32(send_cursor, lightSections);
    for (int i = 0; i < lightSections; i++) {
        WriteVarU32(send_cursor, 2048);
        for (int j = 0; j < 4096; j += 2) {
            u8 light = 0xff;
            WriteU8(send_cursor, light);
        }
    }

    WriteVarU32(send_cursor, lightSections);
    for (int i = 0; i < lightSections; i++) {
        WriteVarU32(send_cursor, 2048);
        for (int j = 0; j < 4096; j += 2) {
            u8 light = 0;
            WriteU8(send_cursor, light);
        }
    }

    FinishPlayerPacket(send_cursor, control);

    EndTimings(SendLightUpdate);
}

static void SendPlayerTeleport(PlayerController * control, Entity * player, Cursor * send_cursor) {
    BeginPacket(send_cursor, CBP_PLAYER_POSITION);
    WriteVarU32(send_cursor, player->currentTeleportId);
    WriteF64(send_cursor, player->x);
    WriteF64(send_cursor, player->y);
    WriteF64(send_cursor, player->z);
    WriteF64(send_cursor, 0);
    WriteF64(send_cursor, 0);
    WriteF64(send_cursor, 0);
    WriteF32(send_cursor, player->rot_y);
    WriteF32(send_cursor, player->rot_x);
    WriteU32(send_cursor, 0); // relative arguments
    FinishPlayerPacket(send_cursor, control);
    control->lastSentTeleportId = player->currentTeleportId;
    control->flags |= PLAYER_CONTROL_AWAITING_TELEPORT;
}

static void DisconnectPlayer(PlayerController * control, Entity * player) {
    control->flags |= PLAYER_CONTROL_SHOULD_DISCONNECT;
    // NOTE(traks): detach player from world
    player->worldId = 0;
}

static float
degree_diff(float to, float from) {
    float div = (to - from) / 360 + 0.5f;
    float mod = div - floor(div);
    float res = mod * 360 - 180;
    return res;
}

static void
merge_stack_to_player_slot(Entity * player, int slot, item_stack * to_add) {
    // @TODO(traks) also ensure item components are similar
    item_stack * is = player->slots + slot;

    if (is->type == to_add->type) {
        int max_stack_size = get_max_stack_size(is->type);
        int add = MIN(max_stack_size - is->size, to_add->size);
        is->size += add;
        to_add->size -= add;
        if (to_add->size == 0) {
            to_add->type = ITEM_AIR;
        }

        if (add != 0) {
            player->slots_needing_update |= (u64) 1 << slot;
        }
    }
}

void
add_stack_to_player_inventory(Entity * player, item_stack * to_add) {
    merge_stack_to_player_slot(player, player->selected_slot, to_add);
    merge_stack_to_player_slot(player, PLAYER_OFF_HAND_SLOT, to_add);

    for (int i = PLAYER_FIRST_HOTBAR_SLOT; i <= PLAYER_LAST_HOTBAR_SLOT; i++) {
        merge_stack_to_player_slot(player, i, to_add);
    }
    for (int i = PLAYER_FIRST_MAIN_INV_SLOT; i <= PLAYER_LAST_MAIN_INV_SLOT; i++) {
        merge_stack_to_player_slot(player, i, to_add);
    }

    if (to_add->size != 0) {
        // try to put remaining stack in empty spot of inventory
        for (int i = PLAYER_FIRST_HOTBAR_SLOT; i <= PLAYER_LAST_HOTBAR_SLOT; i++) {
            item_stack * is = player->slots + i;
            if (is->type == ITEM_AIR) {
                *is = *to_add;
                *to_add = (item_stack) {0};
                player->slots_needing_update |= (u64) 1 << i;
                return;
            }
        }
        for (int i = PLAYER_FIRST_MAIN_INV_SLOT; i <= PLAYER_LAST_MAIN_INV_SLOT; i++) {
            item_stack * is = player->slots + i;
            if (is->type == ITEM_AIR) {
                *is = *to_add;
                *to_add = (item_stack) {0};
                player->slots_needing_update |= (u64) 1 << i;
                return;
            }
        }
    }
}

void
tick_player(PlayerController * control, Entity * player, MemoryArena * tick_arena) {
    BeginTimings(TickPlayer);

    // TODO(traks): should we wait with actually creating the player and ticking
    // the player until the login packet has been sent?

    // @TODO(traks) remove
    if (0) {
        i32 x = floor(player->x);
        i32 y = floor(player->y);
        i32 z = floor(player->z);
        WorldChunkPos chunkPos = {.worldId = player->worldId, .x = x >> 4, .z = z >> 4};
        Chunk * ch = GetChunkIfLoaded(chunkPos);
        if (ch != NULL) {
            i32 basedY = y - MIN_WORLD_Y + 16;
            i32 sectionIndex = basedY >> 4;
            i32 posIndex = ((basedY & 0xf) << 8) | ((z & 0xf) << 4) | (x & 0xf);
            i32 skyLight = GetSectionLight(ch->lightSections[basedY >> 4].skyLight, posIndex);
            i32 blockLight = GetSectionLight(ch->lightSections[basedY >> 4].blockLight, posIndex);
            i32 maxHeight = ch->motion_blocking_height_map[((z & 0xf) << 4) | (x & 0xf)];

            if (serv->global_msg_count < (i32) ARRAY_SIZE(serv->global_msgs)) {
                global_msg * msg = serv->global_msgs + serv->global_msg_count;
                serv->global_msg_count++;
                int text_size = snprintf(
                        (void *) msg->text, sizeof msg->text,
                        "sky light %d, block light %d, max height: %d",
                        skyLight, blockLight, maxHeight);
                msg->size = text_size;
            }
        }
    }

    assert(player->type == ENTITY_PLAYER);
    int sock = control->sock;

    // TODO(traks): We might want to move this to an event-based system (such as
    // epoll/kqueue/etc.). Not sure if offloading some of the work to a
    // different thread is really worth it (haven't tested). However, it seems
    // like TCP ACKs aren't sent back to the client until our program actually
    // reads from the OS buffers (at least on macOS). I'm thinking this might
    // give the client's TCP stack a wrong impression about the server latency.
    //
    // In the future we might also want to respond to certain packets
    // immediately (such as tab completes, statistics requests and chat
    // previewing), instead of waiting for the next tick. Since waiting could
    // delay the handling of the packet upward of 50ms.
    //
    // We also should actually also respond to server list pings as soon as
    // possible, so the client's ping counter is accurate.
    BeginTimings(SystemRecv);
    ssize_t recSize = recv(sock, control->recBuffer + control->recWriteCursor, control->recBufferSize - control->recWriteCursor, 0);
    EndTimings(SystemRecv);

    if (recSize == 0) {
        DisconnectPlayer(control, player);
    } else if (recSize == -1) {
        // EAGAIN means no data received
        if (errno != EAGAIN) {
            LogErrno("Couldn't receive protocol data from player: %s");
            DisconnectPlayer(control, player);
        }
    } else {
        BeginTimings(ReadAndProcessPackets);
        control->recWriteCursor += recSize;

        Cursor * rec_cursor = &(Cursor) {
            .data = control->recBuffer,
            .size = control->recWriteCursor
        };

        // @TODO(traks) rate limit incoming packets per player

        for (;;) {
            MemoryArena * loopArena = &(MemoryArena) {0};
            *loopArena = *tick_arena;

            Cursor * packetCursor = &(Cursor) {0};
            *packetCursor = TryReadPacket(rec_cursor, loopArena, !!(control->flags & PLAYER_CONTROL_PACKET_COMPRESSION), control->recBufferSize);

            if (rec_cursor->error) {
                LogInfo("Player incoming packet error");
                DisconnectPlayer(control, player);
                break;
            }
            if (packetCursor->size == 0) {
                // NOTE(traks): packet not ready yet
                break;
            }

            ProcessPacket(control, packetCursor, loopArena);

            if (packetCursor->error) {
                LogInfo("Player protocol error occurred");
                DisconnectPlayer(control, player);
                break;
            }

            if (packetCursor->index != packetCursor->size) {
                LogInfo("Player protocol packet not fully read");
                DisconnectPlayer(control, player);
                break;
            }
        }

        memmove(rec_cursor->data, rec_cursor->data + rec_cursor->index, rec_cursor->size - rec_cursor->index);
        control->recWriteCursor = rec_cursor->size - rec_cursor->index;
        EndTimings(ReadAndProcessPackets);
    }

    // try to pick up nearby items
    BeginTimings(PickupItems);
    for (int i = 0; i < (i32) ARRAY_SIZE(serv->entities); i++) {
        Entity * entity = serv->entities + i;
        if ((entity->flags & ENTITY_IN_USE) == 0) {
            continue;
        }
        if (entity->type != ENTITY_ITEM) {
            continue;
        }
        if (entity->worldId != player->worldId) {
            continue;
        }
        if (entity->pickup_timeout != 0) {
            continue;
        }

        double test_min_x = player->x - player->collision_width / 2 - 1;
        double test_min_y = player->y - 0.5;
        double test_min_z = player->z - player->collision_width / 2 - 1;
        double test_max_x = player->x + player->collision_width / 2 + 1;
        double test_max_y = player->y + player->collision_height + 0.5;
        double test_max_z = player->z + player->collision_width / 2 + 1;

        double bb_min_x = entity->x - entity->collision_width / 2;
        double bb_min_y = entity->y;
        double bb_min_z = entity->z - entity->collision_width / 2;
        double bb_max_x = entity->x + entity->collision_width / 2;
        double bb_max_y = entity->y + entity->collision_height;
        double bb_max_z = entity->z + entity->collision_width / 2;

        if (test_min_x <= bb_max_x && test_max_x >= bb_min_x
                && test_min_y <= bb_max_y && test_max_y >= bb_min_y
                && test_min_z <= bb_max_z && test_max_z >= bb_min_z) {
            // boxes intersect

            item_stack * contents = &entity->contents;
            int initial_size = contents->size;
            add_stack_to_player_inventory(player, contents);

            int picked_up_size = initial_size - contents->size;

            if (picked_up_size != 0) {
                // prepare to send a packet for the pickup animation
                player->picked_up_item_id = entity->id;
                player->picked_up_item_size = picked_up_size;
                player->picked_up_tick = serv->current_tick;

                // @TODO(traks) we currently restrict to at most one pickup per
                // tick. Should this be increased? 1 stack per tick is probably
                // fast enough.
                break;
            }
        }
    }
    EndTimings(PickupItems);

    EndTimings(TickPlayer);
}

static void
send_changed_entity_data(Cursor * send_cursor, PlayerController * control,
        Entity * entity, u32 changed_data) {
    if (changed_data == 0) {
        return;
    }

    BeginPacket(send_cursor, CBP_SET_ENTITY_DATA);
    WriteVarU32(send_cursor, entity->id);

    if (changed_data & (1 << ENTITY_DATA_FLAGS)) {
        WriteU8(send_cursor, ENTITY_DATA_FLAGS);
        WriteVarU32(send_cursor, ENTITY_DATA_TYPE_BYTE);
        u8 flags = 0;
        // @TODO(traks) shared flags
        flags |= !!(entity->flags & ENTITY_VISUAL_FIRE) << 0;
        flags |= !!(entity->flags & ENTITY_SHIFTING) << 1;
        flags |= !!(entity->flags & ENTITY_SPRINTING) << 3;
        flags |= !!(entity->flags & ENTITY_SWIMMING) << 4;
        flags |= !!(entity->flags & ENTITY_INVISIBLE) << 5;
        flags |= !!(entity->flags & ENTITY_GLOWING) << 6;
        // flags |= !!(entity->flags & ENTITY_FALL_FLYING) << 7;
        WriteU8(send_cursor, flags);
    }

    if (changed_data & (1 << ENTITY_DATA_AIR_SUPPLY)) {
        WriteU8(send_cursor, ENTITY_DATA_AIR_SUPPLY);
        WriteVarU32(send_cursor, ENTITY_DATA_TYPE_INT);
        WriteVarU32(send_cursor, entity->air_supply);
    }

    // @TODO(traks) custom names
    // WriteU8(send_cursor, ENTITY_BASE_DATA_CUSTOM_NAME);

    if (changed_data & (1 << ENTITY_DATA_CUSTOM_NAME_VISIBLE)) {
        WriteU8(send_cursor, ENTITY_DATA_CUSTOM_NAME_VISIBLE);
        WriteVarU32(send_cursor, ENTITY_DATA_TYPE_BOOL);
        WriteU8(send_cursor, !!(entity->flags & ENTITY_CUSTOM_NAME_VISIBLE));
    }

    if (changed_data & (1 << ENTITY_DATA_SILENT)) {
        WriteU8(send_cursor, ENTITY_DATA_SILENT);
        WriteVarU32(send_cursor, ENTITY_DATA_TYPE_BOOL);
        WriteU8(send_cursor, !!(entity->flags & ENTITY_SILENT));
    }

    if (changed_data & (1 << ENTITY_DATA_NO_GRAVITY)) {
        WriteU8(send_cursor, ENTITY_DATA_NO_GRAVITY);
        WriteVarU32(send_cursor, ENTITY_DATA_TYPE_BOOL);
        WriteU8(send_cursor, !!(entity->flags & ENTITY_NO_GRAVITY));
    }

    if (changed_data & (1 << ENTITY_DATA_POSE)) {
        WriteU8(send_cursor, ENTITY_DATA_POSE);
        WriteVarU32(send_cursor, ENTITY_DATA_TYPE_POSE);
        WriteVarU32(send_cursor, entity->pose);
    }

    switch (entity->type) {
    case ENTITY_PLAYER: {
        // @TODO(traks)
        break;
    }
    case ENTITY_ITEM: {
        if (changed_data & (1 << ENTITY_DATA_ITEM)) {
            WriteU8(send_cursor, ENTITY_DATA_ITEM);
            WriteVarU32(send_cursor, ENTITY_DATA_TYPE_ITEM_STACK);
            item_stack * is = &entity->contents;
            WriteVarU32(send_cursor, is->size);
            if (is->size > 0) {
                WriteVarU32(send_cursor, is->type);
                // TODO(traks): item components added & removed
                WriteVarU32(send_cursor, 0);
                WriteVarU32(send_cursor, 0);
            }
        }
        break;
    }
    }

    WriteU8(send_cursor, 0xff); // end of entity data
    FinishPlayerPacket(send_cursor, control);
}

static void
send_take_item_entity_packet(PlayerController * control, Cursor * send_cursor,
        EntityId taker_id, EntityId pickup_id, u8 pickup_size) {
    BeginPacket(send_cursor, CBP_TAKE_ITEM_ENTITY);
    WriteVarU32(send_cursor, pickup_id);
    WriteVarU32(send_cursor, taker_id);
    WriteVarU32(send_cursor, pickup_size);
    FinishPlayerPacket(send_cursor, control);
}

static void
try_update_tracked_entity(PlayerController * control,
        Cursor * send_cursor, MemoryArena * tick_arena,
        tracked_entity * tracked, Entity * entity) {
    // TODO(traks): There are a bunch of timers in play here. The update
    // interval timer, the TP packet timer, the position packet timer, etc. If
    // these happen to line up at the same tick for a bunch of players (e.g. if
    // a bunch of players join during the same tick), that'll be bad times.
    //
    // We should spread out these timers among multiple ticks based on the
    // player count. This spreading out should be done dynamically. There
    // shouldn't be any fixed interval timers.
    //
    // Ideally the packets for the tracked entities for a single player would
    // also be spread out among multiple ticks. It's better to spread out
    // network bandwidth than to flood it every so often. Flooding will result
    // in temporary network lag, spreading out won't.
    if (serv->current_tick - tracked->last_update_tick < tracked->update_interval
            && entity->changed_data == 0) {
        return;
    }

    tracked->last_update_tick = serv->current_tick;

    double newX = entity->x;
    double newY = entity->y;
    double newZ = entity->z;
    double dx = newX - tracked->last_sent_x;
    double dy = newY - tracked->last_sent_y;
    double dz = newZ - tracked->last_sent_z;
    // @TODO(traks) better epsilons
    double dmax = 32767.0 / 4096.0 - 0.001;
    double dmin = -32768.0 / 4096.0 + 0.001;

    unsigned char encoded_rot_x = (int) (entity->rot_x * 256.0f / 360.0f) & 0xff;
    unsigned char encoded_rot_y = (int) (entity->rot_y * 256.0f / 360.0f) & 0xff;

    if (dx > dmin && dx < dmax
            && dy > dmin && dy < dmax
            && dz > dmin && dz < dmax
            && serv->current_tick - tracked->last_tp_packet_tick < 200) {
        // resend position every once in a while in case of floating point
        // rounding errors and discrepancies between our view of the position
        // and the encoded position
        int sent_pos = (dx * dx + dy * dy + dz * dz > 0)
                || (serv->current_tick - tracked->last_send_pos_tick >= 100);
        int sent_rot = (encoded_rot_x - tracked->last_sent_rot_x) != 0
                || (encoded_rot_y - tracked->last_sent_rot_y) != 0;
        // TODO(traks): ensure no overflow
        i64 encodedLastX = round(tracked->last_sent_x * 4096.0);
        i64 encodedLastY = round(tracked->last_sent_y * 4096.0);
        i64 encodedLastZ = round(tracked->last_sent_z * 4096.0);
        i64 encodedNewX = round(newX * 4096.0);
        i64 encodedNewY = round(newY * 4096.0);
        i64 encodedNewZ = round(newZ * 4096.0);
        i16 encoded_dx = encodedNewX - encodedLastX;
        i16 encoded_dy = encodedNewY - encodedLastY;
        i16 encoded_dz = encodedNewZ - encodedLastZ;

        if (sent_pos && sent_rot) {
            BeginPacket(send_cursor, CBP_MOVE_ENTITY_POS_ROT);
            WriteVarU32(send_cursor, entity->id);
            WriteU16(send_cursor, encoded_dx);
            WriteU16(send_cursor, encoded_dy);
            WriteU16(send_cursor, encoded_dz);
            WriteU8(send_cursor, encoded_rot_y);
            WriteU8(send_cursor, encoded_rot_x);
            WriteU8(send_cursor, !!(entity->flags & ENTITY_ON_GROUND));
            FinishPlayerPacket(send_cursor, control);
        } else if (sent_pos) {
            BeginPacket(send_cursor, CBP_MOVE_ENTITY_POS);
            WriteVarU32(send_cursor, entity->id);
            WriteU16(send_cursor, encoded_dx);
            WriteU16(send_cursor, encoded_dy);
            WriteU16(send_cursor, encoded_dz);
            WriteU8(send_cursor, !!(entity->flags & ENTITY_ON_GROUND));
            FinishPlayerPacket(send_cursor, control);
        } else if (sent_rot) {
            BeginPacket(send_cursor, CBP_MOVE_ENTITY_ROT);
            WriteVarU32(send_cursor, entity->id);
            WriteU8(send_cursor, encoded_rot_y);
            WriteU8(send_cursor, encoded_rot_x);
            WriteU8(send_cursor, !!(entity->flags & ENTITY_ON_GROUND));
            FinishPlayerPacket(send_cursor, control);
        }

        if (sent_pos) {
            // @NOTE(traks) this is the way the Minecraft client calculates the
            // new position. We emulate this to reduce accumulated precision
            // loss across many move packets.
            tracked->last_sent_x = encoded_dx == 0 ? tracked->last_sent_x : (encodedLastX + encoded_dx) / 4096.0;
            tracked->last_sent_y = encoded_dy == 0 ? tracked->last_sent_y : (encodedLastY + encoded_dy) / 4096.0;
            tracked->last_sent_z = encoded_dz == 0 ? tracked->last_sent_z : (encodedLastZ + encoded_dz) / 4096.0;

            tracked->last_send_pos_tick = serv->current_tick;
        }
        if (sent_rot) {
            tracked->last_sent_rot_x = encoded_rot_x;
            tracked->last_sent_rot_y = encoded_rot_y;
        }
    } else {
        BeginPacket(send_cursor, CBP_ENTITY_POSITION_SYNC);
        WriteVarU32(send_cursor, entity->id);
        WriteF64(send_cursor, entity->x);
        WriteF64(send_cursor, entity->y);
        WriteF64(send_cursor, entity->z);
        // TODO(traks): send movement here (what unit?)
        WriteF64(send_cursor, 0);
        WriteF64(send_cursor, 0);
        WriteF64(send_cursor, 0);
        WriteF32(send_cursor, entity->rot_y);
        WriteF32(send_cursor, entity->rot_x);
        WriteU8(send_cursor, !!(entity->flags & ENTITY_ON_GROUND));
        FinishPlayerPacket(send_cursor, control);

        tracked->last_tp_packet_tick = serv->current_tick;

        tracked->last_sent_x = entity->x;
        tracked->last_sent_y = entity->y;
        tracked->last_sent_z = entity->z;
        tracked->last_sent_rot_x = encoded_rot_x;
        tracked->last_sent_rot_y = encoded_rot_y;
        tracked->last_send_pos_tick = serv->current_tick;
    }

    if (entity->type != ENTITY_PLAYER) {
        BeginPacket(send_cursor, CBP_SET_ENTITY_MOTION);
        WriteVarU32(send_cursor, entity->id);
        WriteU16(send_cursor, CLAMP(entity->vx, -3.9, 3.9) * 8000);
        WriteU16(send_cursor, CLAMP(entity->vy, -3.9, 3.9) * 8000);
        WriteU16(send_cursor, CLAMP(entity->vz, -3.9, 3.9) * 8000);
        FinishPlayerPacket(send_cursor, control);
    }

    switch (entity->type) {
    case ENTITY_PLAYER: {
        if (encoded_rot_y != tracked->last_sent_head_rot_y) {
            tracked->last_sent_head_rot_y = encoded_rot_y;

            BeginPacket(send_cursor, CBP_ROTATE_HEAD);
            WriteVarU32(send_cursor, entity->id);
            WriteU8(send_cursor, encoded_rot_y);
            FinishPlayerPacket(send_cursor, control);
        }

        if (entity->picked_up_tick == serv->current_tick) {
            send_take_item_entity_packet(control, send_cursor,
                    entity->id, entity->picked_up_item_id,
                    entity->picked_up_item_size);
        }
        break;
    }
    }

    send_changed_entity_data(send_cursor, control, entity, entity->changed_data);
}

static void
start_tracking_entity(PlayerController * control,
        Cursor * send_cursor, MemoryArena * tick_arena,
        tracked_entity * tracked, Entity * entity) {
    *tracked = (tracked_entity) {0};
    tracked->entityId = entity->id;

    tracked->last_sent_x = entity->x;
    tracked->last_sent_y = entity->y;
    tracked->last_sent_z = entity->z;
    unsigned char encoded_rot_x = (int) (entity->rot_x * 256.0f / 360.0f) & 0xff;
    unsigned char encoded_rot_y = (int) (entity->rot_y * 256.0f / 360.0f) & 0xff;
    tracked->last_sent_rot_x = encoded_rot_x;
    tracked->last_sent_rot_y = encoded_rot_y;

    tracked->last_tp_packet_tick = serv->current_tick;
    tracked->last_send_pos_tick = serv->current_tick;
    tracked->last_update_tick = serv->current_tick;

    switch (entity->type) {
    case ENTITY_PLAYER: {
        tracked->update_interval = 2;

        BeginPacket(send_cursor, CBP_ADD_ENTITY);
        WriteVarU32(send_cursor, entity->id);
        WriteUUID(send_cursor, entity->uuid);
        WriteVarU32(send_cursor, entity->type);
        WriteF64(send_cursor, entity->x);
        WriteF64(send_cursor, entity->y);
        WriteF64(send_cursor, entity->z);
        WriteU8(send_cursor, encoded_rot_x);
        WriteU8(send_cursor, encoded_rot_y);
        WriteU8(send_cursor, encoded_rot_y); // TODO(traks): y head rot?
        // NOTE(traks): entity data not used for players
        WriteVarU32(send_cursor, 0);
        // TODO(traks): velocity?
        WriteU16(send_cursor, 0);
        WriteU16(send_cursor, 0);
        WriteU16(send_cursor, 0);
        FinishPlayerPacket(send_cursor, control);

        tracked->last_sent_head_rot_y = encoded_rot_y;
        break;
    }
    case ENTITY_ITEM: {
        tracked->update_interval = 20;

        // BeginPacket(send_cursor, CBP_ADD_MOB);
        // WriteVarU32(send_cursor, entity->eid);
        // // @TODO(traks) appropriate UUID
        // WriteU64(send_cursor, 0);
        // WriteU64(send_cursor, 0);
        // WriteVarU32(send_cursor, ENTITY_SQUID);
        // WriteF64(send_cursor, entity->x);
        // WriteF64(send_cursor, entity->y);
        // WriteF64(send_cursor, entity->z);
        // WriteU8(send_cursor, 0);
        // WriteU8(send_cursor, 0);
        // // @TODO(traks) y head rotation (what is that?)
        // WriteU8(send_cursor, 0);
        // // @TODO(traks) entity velocity
        // WriteU16(send_cursor, 0);
        // WriteU16(send_cursor, 0);
        // WriteU16(send_cursor, 0);
        // FinishPlayerPacket(send_cursor, player);
        BeginPacket(send_cursor, CBP_ADD_ENTITY);
        WriteVarU32(send_cursor, entity->id);
        WriteUUID(send_cursor, entity->uuid);
        WriteVarU32(send_cursor, entity->type);
        WriteF64(send_cursor, entity->x);
        WriteF64(send_cursor, entity->y);
        WriteF64(send_cursor, entity->z);
        // rotation of items is ignored
        WriteU8(send_cursor, 0); // x rot
        WriteU8(send_cursor, 0); // y rot
        WriteU8(send_cursor, 0); // y head rot
        // this kind of entity data not used for items
        WriteVarU32(send_cursor, 0);
        // @NOTE(traks) for some reason Minecraft ignores the initial velocity
        // and initialises it to random values. To solve this, we send the
        // velocity in a separate packet below.
        WriteU16(send_cursor, 0);
        WriteU16(send_cursor, 0);
        WriteU16(send_cursor, 0);
        FinishPlayerPacket(send_cursor, control);

        BeginPacket(send_cursor, CBP_SET_ENTITY_MOTION);
        WriteVarU32(send_cursor, entity->id);
        WriteU16(send_cursor, CLAMP(entity->vx, -3.9, 3.9) * 8000);
        WriteU16(send_cursor, CLAMP(entity->vy, -3.9, 3.9) * 8000);
        WriteU16(send_cursor, CLAMP(entity->vz, -3.9, 3.9) * 8000);
        FinishPlayerPacket(send_cursor, control);
        break;
    }
    }

    send_changed_entity_data(send_cursor, control, entity, 0xffffffff);
}

static void
send_player_abilities(Cursor * send_cursor, PlayerController * control, Entity * player) {
    BeginPacket(send_cursor, CBP_PLAYER_ABILITIES);
    u8 ability_flags = 0;

    if (player->flags & ENTITY_INVULNERABLE) {
        ability_flags |= 0x1;
    }
    if (player->flags & PLAYER_FLYING) {
        ability_flags |= 0x2;
    }
    if (player->flags & PLAYER_CAN_FLY) {
        ability_flags |= 0x4;
    }
    if (player->flags & PLAYER_INSTABUILD) {
        ability_flags |= 0x8;
    }

    WriteU8(send_cursor, ability_flags);
    WriteF32(send_cursor, 0.05); // flying speed
    WriteF32(send_cursor, 0.1); // walking speed
    FinishPlayerPacket(send_cursor, control);
}

static void UpdateChunkCache(PlayerController * control, Entity * player, Cursor * sendCursor) {
    i32 chunkCacheMinX = control->chunkCacheCentreX - control->chunkCacheRadius;
    i32 chunkCacheMinZ = control->chunkCacheCentreZ - control->chunkCacheRadius;
    i32 chunkCacheMaxX = control->chunkCacheCentreX + control->chunkCacheRadius;
    i32 chunkCacheMaxZ = control->chunkCacheCentreZ + control->chunkCacheRadius;

    i32 nextChunkCacheCentreX = (i32) floor(player->x) >> 4;
    i32 nextChunkCacheCentreZ = (i32) floor(player->z) >> 4;
    i32 nextChunkCacheWorldId = player->worldId;
    assert(control->nextChunkCacheRadius <= MAX_CHUNK_CACHE_RADIUS);
    i32 nextChunkCacheMinX = nextChunkCacheCentreX - control->nextChunkCacheRadius;
    i32 nextChunkCacheMinZ = nextChunkCacheCentreZ - control->nextChunkCacheRadius;
    i32 nextChunkCacheMaxX = nextChunkCacheCentreX + control->nextChunkCacheRadius;
    i32 nextChunkCacheMaxZ = nextChunkCacheCentreZ + control->nextChunkCacheRadius;

    if (control->chunkCacheCentreX != nextChunkCacheCentreX
            || control->chunkCacheCentreZ != nextChunkCacheCentreZ
            // NOTE(traks): is it really necessary to check this?
            || control->chunkCacheWorldId != nextChunkCacheWorldId) {
        BeginPacket(sendCursor, CBP_SET_CHUNK_CACHE_CENTER);
        WriteVarU32(sendCursor, nextChunkCacheCentreX);
        WriteVarU32(sendCursor, nextChunkCacheCentreZ);
        FinishPlayerPacket(sendCursor, control);
    }

    if (control->chunkCacheRadius != control->nextChunkCacheRadius) {
        // TODO(traks): also send set simulation distance packet?
        // NOTE(traks): this sets the render/view distance of the client, NOT
        // the chunk cache radius as we define it
        BeginPacket(sendCursor, CBP_SET_CHUNK_CACHE_RADIUS);
        WriteVarU32(sendCursor, control->nextChunkCacheRadius - 1);
        FinishPlayerPacket(sendCursor, control);
    }

    // TODO(traks): technically we don't need to send forget packets
    // NOTE(traks): untrack old chunks
    for (i32 x = chunkCacheMinX; x <= chunkCacheMaxX; x++) {
        for (i32 z = chunkCacheMinZ; z <= chunkCacheMaxZ; z++) {
            WorldChunkPos pos = {.worldId = control->chunkCacheWorldId, .x = x, .z = z};
            i32 index = chunk_cache_index(pos.xz);
            PlayerChunkCacheEntry * cacheEntry = control->chunkCache + index;

            if (x >= nextChunkCacheMinX && x <= nextChunkCacheMaxX
                    && z >= nextChunkCacheMinZ && z <= nextChunkCacheMaxZ
                    && nextChunkCacheWorldId == control->chunkCacheWorldId) {
                // NOTE(traks): old chunk still in new region
                continue;
            }

            if (cacheEntry->flags & PLAYER_CHUNK_ADDED_INTEREST) {
                AddChunkInterest(pos, -1);
            }

            if (cacheEntry->flags & PLAYER_CHUNK_SENT) {
                BeginPacket(sendCursor, CBP_FORGET_LEVEL_CHUNK);
                WriteU32(sendCursor, z);
                WriteU32(sendCursor, x);
                FinishPlayerPacket(sendCursor, control);
            }

            *cacheEntry = (PlayerChunkCacheEntry) {0};
        }
    }

    control->chunkCacheRadius = control->nextChunkCacheRadius;
    control->chunkCacheCentreX = nextChunkCacheCentreX;
    control->chunkCacheCentreZ = nextChunkCacheCentreZ;
    control->chunkCacheWorldId = nextChunkCacheWorldId;
}

static void SendTrackedBlockChanges(PlayerController * control, Cursor * sendCursor, MemoryArena * tickArena) {
    i32 chunkCacheDiam = 2 * control->chunkCacheRadius + 1;
    i32 chunkCacheMinX = control->chunkCacheCentreX - control->chunkCacheRadius;
    i32 chunkCacheMaxX = control->chunkCacheCentreX + control->chunkCacheRadius;
    i32 chunkCacheMinZ = control->chunkCacheCentreZ - control->chunkCacheRadius;
    i32 chunkCacheMaxZ = control->chunkCacheCentreZ + control->chunkCacheRadius;
    Chunk * * changedChunks = CallocInArena(tickArena, MAX_CHUNK_CACHE_DIAM * MAX_CHUNK_CACHE_DIAM * sizeof (Chunk *));
    BeginTimings(CollectLoadedChunks);
    i32 changedChunkCount = CollectChangedChunks(
            (WorldChunkPos) {.worldId = control->chunkCacheWorldId, .x = chunkCacheMinX, .z = chunkCacheMinZ},
            (WorldChunkPos) {.worldId = control->chunkCacheWorldId, .x = chunkCacheMaxX, .z = chunkCacheMaxZ},
            changedChunks);
    EndTimings(CollectLoadedChunks);

    for (i32 chunkIndex = 0; chunkIndex < changedChunkCount; chunkIndex++) {
        Chunk * chunk = changedChunks[chunkIndex];
        WorldChunkPos pos = chunk->pos;
        i32 index = chunk_cache_index(pos.xz);
        PlayerChunkCacheEntry * cacheEntry = control->chunkCache + index;

        if (!(cacheEntry->flags & PLAYER_CHUNK_SENT)) {
            continue;
        }

        Chunk * ch = changedChunks[chunkIndex];
        assert(ch != NULL);

        for (i32 sectionIndex = 0; sectionIndex < SECTIONS_PER_CHUNK; sectionIndex++) {
            i32 sectionY = sectionIndex + MIN_SECTION;
            ChunkSection * section = ch->sections + sectionIndex;
            if (!(chunk->changedBlockSections & ((u32) 1 << sectionIndex))) {
                continue;
            }

            assert(section->changedBlockCount > 0);

            // @TODO(traks) in case of tons of block changes in all
            // sections combined, we shouldn't spam clients with section
            // change packets. We should limit the maximum number of
            // packet data for this packet type and if the limit is
            // exceeded, reload chunks for clients.

            BeginPacket(sendCursor, CBP_SECTION_BLOCKS_UPDATE);
            u64 section_pos = ((u64) (pos.x & 0x3fffff) << 42)
                    | ((u64) (pos.z & 0x3fffff) << 20)
                    | (u64) (sectionY & 0xfffff);
            WriteU64(sendCursor, section_pos);
            WriteVarU32(sendCursor, section->changedBlockCount);

            for (i32 i = 0; i < section->changedBlockSetMask + 1; i++) {
                if (section->changedBlockSet[i] != 0) {
                    BlockPos posInSection = SectionIndexToPos(section->changedBlockSet[i] & 0xfff);
                    i64 block_state = ChunkGetBlockState(ch, (BlockPos) {posInSection.x, posInSection.y + sectionY * 16, posInSection.z});
                    i64 encoded = (block_state << 12) | (posInSection.x << 8) | (posInSection.z << 4) | (posInSection.y & 0xf);
                    WriteVarU64(sendCursor, encoded);
                }
            }

            FinishPlayerPacket(sendCursor, control);
        }

        if (ch->lastLocalEventTick == serv->current_tick) {
            for (i32 i = 0; i < ch->localEventCount; i++) {
                level_event * event = ch->localEvents + i;

                BeginPacket(sendCursor, CBP_LEVEL_EVENT);
                WriteU32(sendCursor, event->type);
                WriteBlockPos(sendCursor, event->pos);
                WriteU32(sendCursor, event->data);
                WriteU8(sendCursor, 0); // is global event
                FinishPlayerPacket(sendCursor, control);
            }
        }
    }
}

// @TODO(traks) I wonder if this function should be sending packets to all
// players at once instead of to only a single player. That would allow us to
// cache compressed chunk packets, copy packets that get sent to all players,
// use the CPU cache better, etc.
void
send_packets_to_player(PlayerController * control, Entity * player, MemoryArena * tick_arena) {
    BeginTimings(SendPackets);

    size_t max_uncompressed_packet_size = 1 << 20;
    Cursor send_cursor_ = {
        .data = MallocInArena(tick_arena, max_uncompressed_packet_size),
        .size = max_uncompressed_packet_size
    };
    Cursor * send_cursor = &send_cursor_;

    if (!(control->flags & PLAYER_CONTROL_DID_INIT_PACKETS)) {
        control->flags |= PLAYER_CONTROL_DID_INIT_PACKETS;

        String levelName = STR("blaze:main");

        BeginPacket(send_cursor, CBP_LOGIN);
        WriteU32(send_cursor, control->entityId);
        WriteU8(send_cursor, 0); // hardcore

        // list of levels on the server
        // TODO(traks): what do we do if we want to update this list?
        WriteVarU32(send_cursor, 1);
        WriteVarString(send_cursor, levelName);

        WriteVarU32(send_cursor, 0); // max players (ignored by client)
        WriteVarU32(send_cursor, control->nextChunkCacheRadius - 1); // chunk radius
        // @TODO(traks) figure out why the client needs to know the simulation
        // distance and what it uses it for
        WriteVarU32(send_cursor, control->nextChunkCacheRadius - 1); // simulation distance
        WriteU8(send_cursor, 0); // reduced debug info
        WriteU8(send_cursor, 1); // show death screen on death
        WriteU8(send_cursor, 0); // limited crafting
        WriteVarU32(send_cursor, 0); // id of dimension type, minecraft:overworld
        WriteVarString(send_cursor, levelName); // level being spawned into
        WriteU64(send_cursor, 0); // hashed seed
        WriteU8(send_cursor, player->gamemode); // current gamemode
        WriteU8(send_cursor, -1); // previous gamemode
        WriteU8(send_cursor, 0); // is debug world
        WriteU8(send_cursor, 0); // is flat
        WriteU8(send_cursor, 0); // has death location, world + pos after if true
        WriteVarU32(send_cursor, 0); // portal cooldown
        WriteVarU32(send_cursor, 63); // sea level (of the dimension?)
        WriteU8(send_cursor, ENFORCE_SECURE_CHAT); // enforces secure chat
        FinishPlayerPacket(send_cursor, control);

        BeginPacket(send_cursor, CBP_SET_HELD_SLOT);
        WriteU8(send_cursor, player->selected_slot - PLAYER_FIRST_HOTBAR_SLOT);
        FinishPlayerPacket(send_cursor, control);

        BeginPacket(send_cursor, CBP_CUSTOM_PAYLOAD);
        String brand_str = STR("minecraft:brand");
        String brand = STR("Blaze");
        WriteVarString(send_cursor, brand_str);
        WriteVarString(send_cursor, brand);
        FinishPlayerPacket(send_cursor, control);

        BeginPacket(send_cursor, CBP_CHANGE_DIFFICULTY);
        WriteU8(send_cursor, 2); // difficulty normal
        WriteU8(send_cursor, 0); // locked
        FinishPlayerPacket(send_cursor, control);

        send_player_abilities(send_cursor, control, player);

        send_changed_entity_data(send_cursor, control, player, player->changed_data);

        // TODO(traks): Only do this after we have sent the player a couple of
        // chunks around the spawn chunk, so they can move around immediately
        // after the loading screen closes?
        //
        // Currently, it looks like the client will simulate falling into the
        // void after being teleported into unsent chunks. The game event packet
        // will eventually cause the world to be shown, because the player
        // leaves the world boundaries when falling into the void (I think
        // that's why at least). This happens even if the player's current chunk
        // is still unsent.

        SendPlayerTeleport(control, player, send_cursor);

        BeginPacket(send_cursor, CBP_SET_DEFAULT_SPAWN_POSITION);
        // TODO(traks): specific value not important for now
        WriteBlockPos(send_cursor, (BlockPos) {0, 0, 0});
        WriteF32(send_cursor, 0); // yaw
        FinishPlayerPacket(send_cursor, control);

        // NOTE(traks): seems pointless, but is required to get the client
        // eventually off the world loading screen
        BeginPacket(send_cursor, CBP_GAME_EVENT);
        WriteU8(send_cursor, PACKET_GAME_EVENT_LEVEL_CHUNKS_LOAD_START);
        WriteF32(send_cursor, 0);
        FinishPlayerPacket(send_cursor, control);

        // reset changed data, because all data is sent already and we don't
        // want to send the same data twice
        player->changed_data = 0;

        control->lastAckedBlockChange = -1;
    }

    // send keep alive packet every so often
    if (serv->current_tick - control->last_keep_alive_sent_tick >= KEEP_ALIVE_SPACING
            && (control->flags & PLAYER_CONTROL_GOT_ALIVE_RESPONSE)) {
        BeginPacket(send_cursor, CBP_KEEP_ALIVE);
        WriteU64(send_cursor, serv->current_tick);
        FinishPlayerPacket(send_cursor, control);

        control->last_keep_alive_sent_tick = serv->current_tick;
        control->flags &= ~PLAYER_CONTROL_GOT_ALIVE_RESPONSE;
    }

    if (player->currentTeleportId != control->lastSentTeleportId) {
        LogInfo("Sending teleport to player");
        SendPlayerTeleport(control, player, send_cursor);
    }

    if (player->changed_data & PLAYER_GAMEMODE_CHANGED) {
        BeginPacket(send_cursor, CBP_GAME_EVENT);
        WriteU8(send_cursor, PACKET_GAME_EVENT_CHANGE_GAME_MODE);
        WriteF32(send_cursor, player->gamemode);
        FinishPlayerPacket(send_cursor, control);
    }

    if (player->changed_data & PLAYER_ABILITIES_CHANGED) {
        send_player_abilities(send_cursor, control, player);
    }

    send_changed_entity_data(send_cursor, control, player, player->changed_data);

    if (player->picked_up_tick == serv->current_tick) {
        send_take_item_entity_packet(control, send_cursor,
                player->id, player->picked_up_item_id,
                player->picked_up_item_size);
    }

    // NOTE(traks): send block change ack
    if (control->lastAckedBlockChange >= 0) {
        BeginPacket(send_cursor, CBP_BLOCK_CHANGED_ACK);
        WriteVarU32(send_cursor, control->lastAckedBlockChange);
        FinishPlayerPacket(send_cursor, control);
        control->lastAckedBlockChange = -1;
    }

    // send block changes for this player only
    for (int i = 0; i < control->changed_block_count; i++) {
        WorldBlockPos pos = control->changed_blocks[i];
        if (pos.worldId != player->worldId) {
            continue;
        }
        u16 block_state = WorldGetBlockState(pos);
        if (block_state >= serv->vanilla_block_state_count) {
            // catches unknown blocks
            continue;
        }

        BeginPacket(send_cursor, CBP_BLOCK_UPDATE);
        WriteBlockPos(send_cursor, pos.xyz);
        WriteVarU32(send_cursor, block_state);
        FinishPlayerPacket(send_cursor, control);
    }
    control->changed_block_count = 0;

    BeginTimings(UpdateChunkCache);
    UpdateChunkCache(control, player, send_cursor);
    EndTimings(UpdateChunkCache);

    BeginTimings(SendTrackedBlockChanges);
    SendTrackedBlockChanges(control, send_cursor, tick_arena);
    EndTimings(SendTrackedBlockChanges);

    // load and send tracked chunks
    BeginTimings(LoadAndSendChunks);

    // @TODO(traks) Don't send chunks if there are still chunks being sent to
    // the client. Don't want to overflow the connection with chunks and lag
    // everything else. Is there any way to figure out if a chunk has been
    // received by the client? Or is there no way to tell if a chunk is still
    // buffered by the OS or TCP?

    // TODO(traks): would also be really cool if we could dynamically adjust the
    // chunk send rate based on the client's bandwidth. For example, send ping
    // packet then chunk packet, see how long it takes until we get the pong
    // packet. In case of low bandwidth, we should send chunks less frequently.
    // In case of high bandwidth, we can send chunks more rapidly.

    // We iterate in a spiral around the player, so chunks near the player
    // are processed first. This shortens server join times (since players
    // don't need to wait for the chunk they are in to load) and allows
    // players to move around much earlier.
    int newly_sent_chunks = 0;
    int newInterestAdded = 0;
    int chunk_cache_diam = 2 * control->chunkCacheRadius + 1;
    int chunk_cache_area = chunk_cache_diam * chunk_cache_diam;
    int off_x = 0;
    int off_z = 0;
    int step_x = 1;
    int step_z = 0;
    for (int i = 0; i < chunk_cache_area; i++) {
        int x = control->chunkCacheCentreX + off_x;
        int z = control->chunkCacheCentreZ + off_z;
        int cache_index = chunk_cache_index((ChunkPos) {.x = x, .z = z});
        PlayerChunkCacheEntry * cacheEntry = control->chunkCache + cache_index;
        WorldChunkPos pos = {.worldId = player->worldId, .x = x, .z = z};

        if (!(cacheEntry->flags & PLAYER_CHUNK_ADDED_INTEREST)
                && newInterestAdded < MAX_CHUNK_LOADS_PER_TICK) {
            AddChunkInterest(pos, 1);
            cacheEntry->flags |= PLAYER_CHUNK_ADDED_INTEREST;
            newInterestAdded++;
        }

        if (!(cacheEntry->flags & PLAYER_CHUNK_SENT)
                && newly_sent_chunks < MAX_CHUNK_SENDS_PER_TICK) {
            Chunk * ch = GetChunkIfLoaded(pos);
            if (ch != NULL) {
                // send chunk blocks and lighting
                send_chunk_fully(send_cursor, ch, control, tick_arena);
                cacheEntry->flags |= PLAYER_CHUNK_SENT;
                newly_sent_chunks++;
            }
        }

        off_x += step_x;
        off_z += step_z;
        // change direction of spiral when we hit a corner
        if (off_x == off_z || (off_x == -off_z && off_x < 0)
                || (off_x == -off_z + 1 && off_x > 0)) {
            int prev_step_x = step_x;
            step_x = -step_z;
            step_z = prev_step_x;
        }
    }

    EndTimings(LoadAndSendChunks);

    // send updates in player's own inventory
    BeginTimings(SendInventory);

    for (int i = 0; i < PLAYER_SLOTS; i++) {
        if (!(player->slots_needing_update & ((u64) 1 << i))) {
            continue;
        }

        LogInfo("Sending slot update for %d", i);
        item_stack * is = player->slots + i;

        // @TODO(traks) under certain conditions, the client may not modify its
        // local version of the inventory menu when we send this packet. Work
        // around this?

        BeginPacket(send_cursor, CBP_CONTAINER_SET_SLOT);
        WriteU8(send_cursor, 0); // own container id
        // @TODO(traks) figure out what this is used for
        WriteVarU32(send_cursor, 0); // state id
        WriteU16(send_cursor, i);
        WriteVarU32(send_cursor, is->size);

        if (is->size > 0) {
            assert(is->type != 0);
            WriteVarU32(send_cursor, is->type);
            // TODO(traks): write item components added & removed
            WriteVarU32(send_cursor, 0);
            WriteVarU32(send_cursor, 0);
        }
        FinishPlayerPacket(send_cursor, control);
    }

    player->slots_needing_update = 0;
    memcpy(player->slots_prev_tick, player->slots, sizeof player->slots);

    EndTimings(SendInventory);

    // tab list updates
    BeginTimings(SendTabList);

    if (!(control->flags & PLAYER_CONTROL_INITIALISED_TAB_LIST)) {
        control->flags |= PLAYER_CONTROL_INITIALISED_TAB_LIST;
        if (serv->tab_list_size > 0) {
            BeginPacket(send_cursor, CBP_PLAYER_INFO_UPDATE);
            u8 actionBits = 0b1111111; // everything
            WriteU8(send_cursor, actionBits);
            WriteVarU32(send_cursor, serv->tab_list_size);

            for (int i = 0; i < serv->tab_list_size; i++) {
                UUID uuid = serv->tab_list[i];
                PlayerController * tabListPlayer = ResolvePlayer(uuid);
                // TODO(traks): If a player disconnects mid tick (e.g. due to a
                // networking error), this assert can fail, because the player
                // entity will be gone. Fix this. For example by storing all
                // required data in the tab list array, so we don't need to
                // resolve entities.
                // assert(tabListPlayer->type == ENTITY_PLAYER);
                WriteUUID(send_cursor, tabListPlayer->uuid);
                String username = {
                    .data = tabListPlayer->username,
                    .size = tabListPlayer->username_size
                };
                WriteVarString(send_cursor, username);
                WriteVarU32(send_cursor, 0); // num properties
                WriteU8(send_cursor, 0); // has message signing key
                Entity * tabListEntity = ResolveEntity(tabListPlayer->entityId);
                WriteVarU32(send_cursor, tabListEntity->gamemode);
                WriteU8(send_cursor, 1); // listed
                WriteVarU32(send_cursor, 0); // latency
                WriteU8(send_cursor, 0); // has display name
                WriteVarU32(send_cursor, 0); // list order
            }
            FinishPlayerPacket(send_cursor, control);
        }
    } else {
        if (serv->tab_list_removed_count > 0) {
            BeginPacket(send_cursor, CBP_PLAYER_INFO_REMOVE);
            WriteVarU32(send_cursor, serv->tab_list_removed_count);

            for (int i = 0; i < serv->tab_list_removed_count; i++) {
                UUID uuid = serv->tab_list_removed[i];
                WriteUUID(send_cursor, uuid);
            }
            FinishPlayerPacket(send_cursor, control);
        }
        if (serv->tab_list_added_count > 0) {
            BeginPacket(send_cursor, CBP_PLAYER_INFO_UPDATE);

            // NOTE(traks): actions:
            // 0 = add player
            // 1 = init chat
            // 2 = update game mode
            // 3 = update listed
            // 4 = update latency
            // 5 = update display name
            // 6 = update list order

            // TODO(traks): init chat?

            u8 actionBits = 0b1111111; // everything
            WriteU8(send_cursor, actionBits);
            WriteVarU32(send_cursor, serv->tab_list_added_count);

            for (int i = 0; i < serv->tab_list_added_count; i++) {
                UUID uuid = serv->tab_list_added[i];
                PlayerController * tabListPlayer = ResolvePlayer(uuid);
                // TODO(traks): If a player disconnects mid tick (e.g. due to a
                // networking error), this assert can fail, because the player
                // entity will be gone. Fix this. For example by storing all
                // required data in the tab list array, so we don't need to
                // resolve entities.
                // assert(tabListPlayer->type == ENTITY_PLAYER);
                WriteUUID(send_cursor, tabListPlayer->uuid);

                // NOTE(traks): after the UUID, write all the data of the player
                // per action, in order

                String username = {
                    .data = tabListPlayer->username,
                    .size = tabListPlayer->username_size
                };
                WriteVarString(send_cursor, username);
                WriteVarU32(send_cursor, 0); // num properties
                WriteU8(send_cursor, 0); // has message signing key
                Entity * tabListEntity = ResolveEntity(tabListPlayer->entityId);
                WriteVarU32(send_cursor, tabListEntity->gamemode);
                WriteU8(send_cursor, 1); // listed
                WriteVarU32(send_cursor, 0); // latency
                WriteU8(send_cursor, 0); // has display name
                WriteVarU32(send_cursor, 0); // list order
            }
            FinishPlayerPacket(send_cursor, control);
        }

        // TODO(traks): pull this out and run this once per tick
        for (int i = 0; i < MAX_ENTITIES; i++) {
            Entity * entity = serv->entities + i;
            if (!(entity->flags & ENTITY_IN_USE)) {
                continue;
            }
            if (entity->type != ENTITY_PLAYER) {
                continue;
            }

            if (entity->changed_data & PLAYER_GAMEMODE_CHANGED) {
                BeginPacket(send_cursor, CBP_PLAYER_INFO_UPDATE);
                WriteVarU32(send_cursor, 0b0000100); // action: update gamemode
                WriteVarU32(send_cursor, 1); // changed entries
                WriteUUID(send_cursor, entity->uuid);
                WriteVarU32(send_cursor, entity->gamemode);
                FinishPlayerPacket(send_cursor, control);
            }
        }
    }

    EndTimings(SendTabList);

    // entity tracking
    BeginTimings(TrackEntities);

    EntityId removed_entities[64];
    int removed_entity_count = 0;

    for (int j = 1; j < MAX_ENTITIES; j++) {
        Entity * candidate = serv->entities + j;
        tracked_entity * tracked = control->tracked_entities + j;
        int is_tracking_candidate = (tracked->entityId == candidate->id);

        if ((candidate->flags & ENTITY_IN_USE)
                && is_tracking_candidate) {
            // Candidate is a valid entity and we're already tracking it
            double dx = candidate->x - player->x;
            double dy = candidate->y - player->y;
            double dz = candidate->z - player->z;
            if (candidate->worldId == player->worldId && dx * dx + dy * dy + dz * dz < 45 * 45) {
                try_update_tracked_entity(control,
                        send_cursor, tick_arena, tracked, candidate);
                continue;
            }
        }

        // several states are possible: the candidate isn't a valid entity, or
        // we aren't tracking it yet, or the currently tracked entity was
        // removed, or the tracked entity is out of range

        if (tracked->entityId != 0) {
            // remove tracked entity if we were tracking an entity
            if (removed_entity_count == ARRAY_SIZE(removed_entities)) {
                // no more space to untrack, try again next tick
                continue;
            }

            removed_entities[removed_entity_count] = tracked->entityId;
            tracked->entityId = 0;
            removed_entity_count++;
        }

        if ((candidate->flags & ENTITY_IN_USE) && candidate->id != player->id
                && !is_tracking_candidate) {
            // candidate is a valid entity and wasn't tracked already
            double dx = candidate->x - player->x;
            double dy = candidate->y - player->y;
            double dz = candidate->z - player->z;

            if (candidate->worldId != player->worldId || dx * dx + dy * dy + dz * dz > 40 * 40) {
                // @NOTE(traks) Don't start tracking until close enough. This
                // radius should be lower than the untrack radius: if the track
                // radius is larger than the untrack radius, then there's a zone
                // in which we continuously track and untrack every tick. This
                // is a waste of bandwidth.
                continue;
            }

            start_tracking_entity(control,
                    send_cursor, tick_arena, tracked, candidate);
        }
    }

    if (removed_entity_count > 0) {
        BeginPacket(send_cursor, CBP_REMOVE_ENTITIES);
        WriteVarU32(send_cursor, removed_entity_count);
        for (int i = 0; i < removed_entity_count; i++) {
            WriteVarU32(send_cursor, removed_entities[i]);
        }
        FinishPlayerPacket(send_cursor, control);
    }

    EndTimings(TrackEntities);

    // send chat messages
    BeginTimings(SendChat);

    for (int msgIndex = 0; msgIndex < serv->global_msg_count; msgIndex++) {
        global_msg * msg = serv->global_msgs + msgIndex;
        // TODO(traks): use player chat packet for this with annoying signing.
        // Also will make chat narration work properly as "sender says message"
        BeginPacket(send_cursor, CBP_SYSTEM_CHAT);
        WriteU8(send_cursor, NBT_TAG_STRING);
        nbt_write_string(send_cursor, (String) {.data = msg->text, .size = msg->size});
        WriteU8(send_cursor, 0); // action bar or chat log
        FinishPlayerPacket(send_cursor, control);
    }

    EndTimings(SendChat);

    // try to write everything to the socket buffer

    if (send_cursor->error != 0) {
        // just disconnect the player
        LogInfo("Failed to create packets");
        DisconnectPlayer(control, player);
        goto bail;
    }

    Cursor final_cursor_ = {
        .data = control->sendBuffer,
        .size = control->sendBufferSize,
        .index = control->sendWriteCursor
    };
    Cursor * final_cursor = &final_cursor_;

    FinalisePackets(final_cursor, send_cursor);

    if (final_cursor->error != 0) {
        // just disconnect the player
        LogInfo("Failed to finalise packets");
        DisconnectPlayer(control, player);
        goto bail;
    }

    BeginTimings(SystemSend);
    ssize_t send_size = send(control->sock, final_cursor->data,
            final_cursor->index, 0);
    EndTimings(SystemSend);

    if (send_size == -1) {
        // EAGAIN means no data sent
        if (errno != EAGAIN) {
            LogErrno("Couldn't send protocol data to player: %s");
            DisconnectPlayer(control, player);
        }
    } else {
        memmove(final_cursor->data, final_cursor->data + send_size,
                final_cursor->index - send_size);
        control->sendWriteCursor = final_cursor->index - send_size;
    }

bail:
    EndTimings(SendPackets);
}

i32 GetPlayerFacing(Entity * player) {
    float rot_y = player->rot_y;
    int int_rot = (int) floor(rot_y / 90.0f + 0.5f) & 0x3;
    switch (int_rot) {
    case 0: return DIRECTION_POS_Z;
    case 1: return DIRECTION_NEG_X;
    case 2: return DIRECTION_NEG_Z;
    case 3: return DIRECTION_POS_X;
    default:
        assert(0);
        return -1;
    }
}

PlayerController * ResolvePlayer(UUID uuid) {
    for (i32 playerIndex = 0; playerIndex < playerList.playerCount; playerIndex++) {
        PlayerController * control = playerList.players[playerIndex];
        if (memcmp(&control->uuid, &uuid, sizeof uuid) == 0) {
            return control;
        }
    }
    return NULL;
}

static void DestroyPlayer(PlayerController * control) {
    // TODO(traks): send disconnect message and wait a bit before closing the
    // socket, so the disconnect message has a chance of reaching the client
    BeginTimings(SystemClose);
    close(control->sock);
    EndTimings(SystemClose);

    i32 chunk_cache_min_x = control->chunkCacheCentreX - control->chunkCacheRadius;
    i32 chunk_cache_max_x = control->chunkCacheCentreX + control->chunkCacheRadius;
    i32 chunk_cache_min_z = control->chunkCacheCentreZ - control->chunkCacheRadius;
    i32 chunk_cache_max_z = control->chunkCacheCentreZ + control->chunkCacheRadius;

    for (i32 x = chunk_cache_min_x; x <= chunk_cache_max_x; x++) {
        for (i32 z = chunk_cache_min_z; z <= chunk_cache_max_z; z++) {
            WorldChunkPos pos = {.worldId = control->chunkCacheWorldId, .x = x, .z = z};
            PlayerChunkCacheEntry * cacheEntry = control->chunkCache + chunk_cache_index(pos.xz);
            if (cacheEntry->flags & PLAYER_CHUNK_ADDED_INTEREST) {
                AddChunkInterest(pos, -1);
            }
        }
    }

    free(control->recBuffer);
    free(control->sendBuffer);
    EvictEntity(control->entityId);
    free(control);
}

static void JoinPlayer(JoinRequest * request) {
    Entity * player = TryReserveEntity(ENTITY_PLAYER);

    // TODO(traks): don't malloc this much when a player joins. AAA
    // games send a lot less than 1MB/tick. For example, according
    // to some website, Fortnite sends about 1.5KB/tick. Although we
    // sometimes have to send a bunch of chunk data, which can be
    // tens of KB. Minecraft even allows up to 2MB of chunk data.

    // TODO(traks): There shouldn't be anything left in the receive
    // buffer/send buffer when we're done here with the initial processing.
    // However, perhaps we should make sure and copy over anything to the
    // player's receive and send buffer?
    i32 recBufferSize = 1 << 16;
    u8 * recBuffer = malloc(recBufferSize);

    i32 sendBufferSize = 1 << 20;
    u8 * sendBuffer = malloc(sendBufferSize);

    PlayerController * control = calloc(1, sizeof *control);

    if (player->type != ENTITY_PLAYER || recBuffer == NULL || sendBuffer == NULL || playerList.playerCount >= (i32) ARRAY_SIZE(playerList.players) || control == NULL) {
        // TODO(traks): send some message on disconnect
        LogInfo("Failed to join player");
        free(sendBuffer);
        free(recBuffer);
        EvictEntity(player->id);
        free(control);
        close(request->socket);
        return;
    }

    playerList.players[playerList.playerCount] = control;
    playerList.playerCount++;

    control->entityId = player->id;
    control->sock = request->socket;
    control->recBufferSize = recBufferSize;
    control->recBuffer = recBuffer;
    control->sendBufferSize = sendBufferSize;
    control->sendBuffer = sendBuffer;
    memcpy(control->username, request->username, request->usernameSize);
    control->username_size = request->usernameSize;
    control->uuid = request->uuid;
    player->uuid = request->uuid;

    memcpy(control->locale, request->locale, request->localeSize);
    control->localeSize = request->localeSize;
    control->chatMode = request->chatMode;
    control->seesChatColours = request->seesChatColours;
    control->skinCustomisation = request->skinCustomisation;
    control->mainHand = request->mainHand;
    control->textFiltering = request->textFiltering;
    control->showInStatusList = request->showInStatusList;
    control->nextChunkCacheRadius = request->chunkCacheRadius;
    control->particleStatus = request->particleStatus;

    control->last_keep_alive_sent_tick = serv->current_tick;
    control->flags |= PLAYER_CONTROL_GOT_ALIVE_RESPONSE;
    if (request->packetCompression) {
        control->flags |= PLAYER_CONTROL_PACKET_COMPRESSION;
    }
    player->selected_slot = PLAYER_FIRST_HOTBAR_SLOT;
    // TODO(traks): collision width and height of player depending
    // on player pose
    player->collision_width = 0.6;
    player->collision_height = 1.8;
    SetPlayerGamemode(player, GAMEMODE_CREATIVE);

    // TeleportEntity(player, 1, 88, 70, 73, 0, 0);
    TeleportEntity(player, 1, 0.5, 140, 0.5, 0, 0);

    // TODO(traks): ensure this can never happen instead of asserting
    // it never will hopefully happen
    assert(serv->tab_list_added_count < (i32) ARRAY_SIZE(serv->tab_list_added));
    serv->tab_list_added[serv->tab_list_added_count] = control->uuid;
    serv->tab_list_added_count++;

    LogInfo("Player '%.*s' joined", (int) control->username_size, control->username);
    if (serv->global_msg_count < (i32) ARRAY_SIZE(serv->global_msgs)) {
        global_msg * msg = serv->global_msgs + serv->global_msg_count;
        serv->global_msg_count++;
        int textSize = snprintf(
                (void *) msg->text, sizeof msg->text,
                "%.*s joined the game",
                (int) control->username_size, control->username);
        msg->size = textSize;
    }
}

void TickPlayers(MemoryArena * arena) {
    if (pthread_mutex_lock(&playerList.joinQueueMutex) == 0) {
        for (i32 entryIndex = 0; entryIndex < playerList.joinQueueCount; entryIndex++) {
            JoinRequest * request = &playerList.joinQueue[entryIndex];
            JoinPlayer(request);
        }
        playerList.joinQueueCount = 0;
        pthread_mutex_unlock(&playerList.joinQueueMutex);
    }

    for (i32 playerIndex = 0; playerIndex < playerList.playerCount; playerIndex++) {
        MemoryArena * singleTickArena = &(MemoryArena) {0};
        *singleTickArena = *arena;
        PlayerController * control = playerList.players[playerIndex];
        Entity * player = ResolveEntity(control->entityId);
        tick_player(control, player, singleTickArena);
    }
}

void SendPacketsToPlayers(MemoryArena * arena) {
    for (i32 playerIndex = 0; playerIndex < playerList.playerCount; playerIndex++) {
        MemoryArena * singleTickArena = &(MemoryArena) {0};
        *singleTickArena = *arena;
        PlayerController * control = playerList.players[playerIndex];
        Entity * player = ResolveEntity(control->entityId);
        send_packets_to_player(control, player, singleTickArena);
    }

    for (i32 playerIndex = 0; playerIndex < playerList.playerCount; playerIndex++) {
        PlayerController * control = playerList.players[playerIndex];
        if (control->flags & PLAYER_CONTROL_SHOULD_DISCONNECT) {
            DestroyPlayer(control);
            playerList.players[playerIndex] = playerList.players[playerList.playerCount - 1];
            playerIndex--;
            playerList.playerCount--;
        }
    }

    // NOTE(traks): update player list entries
    if (pthread_mutex_lock(&playerList.entryMutex) == 0) {
        for (i32 playerIndex = 0; playerIndex < playerList.playerCount; playerIndex++) {
            PlayerListEntry * entry = &playerList.entryArray[playerIndex];
            PlayerController * control = playerList.players[playerIndex];
            entry->uuid = control->uuid;
            memcpy(entry->username, control->username, control->username_size);
            entry->usernameSize = control->username_size;
        }
        playerList.entryCount = playerList.playerCount;
        pthread_mutex_unlock(&playerList.entryMutex);
    }
}

void InitPlayerControl(void) {
    if (pthread_mutex_init(&playerList.entryMutex, NULL)) {
        LogErrno("Failed to create entry mutex: %s");
        exit(1);
    }
    if (pthread_mutex_init(&playerList.joinQueueMutex, NULL)) {
        LogErrno("Failed to create join queue mutex: %s");
        exit(1);
    }
}

i32 CopyPlayerList(PlayerListEntry * entryArray, i32 arraySize) {
    i32 res = 0;
    if (pthread_mutex_lock(&playerList.entryMutex) == 0) {
        res = MIN(arraySize, playerList.entryCount);
        memcpy(entryArray, playerList.entryArray, res * sizeof *entryArray);
        pthread_mutex_unlock(&playerList.entryMutex);
    }
    return res;
}

i32 QueuePlayerJoin(JoinRequest request) {
    i32 res = 0;
    if (pthread_mutex_lock(&playerList.joinQueueMutex) == 0) {
        if (playerList.joinQueueCount < (i32) ARRAY_SIZE(playerList.joinQueue)) {
            playerList.joinQueue[playerList.joinQueueCount] = request;
            playerList.joinQueueCount++;
            res = 1;
        }
        pthread_mutex_unlock(&playerList.joinQueueMutex);
    }
    return res;
}
