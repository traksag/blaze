#include "shared.h"

void TeleportEntity(Entity * entity, i32 worldId, f64 x, f64 y, f64 z, f32 rotX, f32 rotY) {
    // NOTE(traks): handles NaN/infinity properly, just to be safe
    x = (x >= MIN_ENTITY_XZ) ? ((x <= MAX_ENTITY_XZ) ? x : MAX_ENTITY_XZ) : MIN_ENTITY_XZ;
    y = (y >= MIN_ENTITY_Y) ? ((y <= MAX_ENTITY_Y) ? y : MAX_ENTITY_Y) : MIN_ENTITY_Y;
    z = (z >= MIN_ENTITY_XZ) ? ((z <= MAX_ENTITY_XZ) ? z : MAX_ENTITY_XZ) : MIN_ENTITY_XZ;
    entity->worldId = worldId;
    entity->x = x;
    entity->y = y;
    entity->z = z;
    entity->rot_x = rotX;
    entity->rot_y = rotY;
}
