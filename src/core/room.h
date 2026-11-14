/*
 * Rooms: spaces closed by walls, doors and rock, and whether a roof covers
 * them. A cache derived from the map, like the connectivity regions
 * (core/region.h): rooms_update() rebuilds it when a structure, a roof or
 * the topology changed (Map.build_revision, Map.topology_revision).
 *
 * Every tile that neither holds rock, a wall nor a door belongs to an area:
 * the tiles reached from it without crossing those (water included, so an
 * island is not a room). An area is a room when it is closed: it does not
 * reach the edge of the map and has at most ROOM_MAX_TILES tiles. A room is
 * roofed when every one of its tiles is under a roof, built or natural
 * (map_is_roofed()). Nothing depends on rooms yet; warmth, rain and moods
 * will (docs/KNOWLEDGE.md §10).
 */
#ifndef COLONY_CORE_ROOM_H
#define COLONY_CORE_ROOM_H

#include "core/map.h"

#include <stdbool.h>
#include <stdint.h>

#define ROOM_NONE 0u /* Rock, a wall or a door; out of bounds; unknown. */

/* Larger closed areas are outdoors (a valley ringed by cliffs) (tuning). */
#define ROOM_MAX_TILES 400

typedef struct Room {
    int tiles;
    int roofed; /* Tiles under a roof (counted for rooms only). */
    bool closed; /* A room; false: outdoors. */
} Room;

typedef struct Rooms {
    int width;
    int height;
    uint32_t *id; /* Area of each tile, ROOM_NONE on rock, walls and doors. */
    uint32_t count; /* Areas are numbered 1..count. */
    Room *list; /* Per area id (index 0 unused). */
    uint32_t *stack; /* Scratch for the flood fill. */
    uint32_t topology_revision; /* The map revisions the areas were built for. */
    uint32_t build_revision;
    bool built;
} Rooms;

bool rooms_init(Rooms *rooms, int width, int height);
void rooms_free(Rooms *rooms);

/* Rebuilds the areas if the map changed since the last call. */
void rooms_update(Rooms *rooms, const Map *map);

/* Whether the areas describe the map as it is now. */
bool rooms_current(const Rooms *rooms, const Map *map);

/* The area of a tile: a room or outdoors. NULL on rock, walls and doors, out
 * of bounds, or when the areas are not current. */
const Room *rooms_at(const Rooms *rooms, const Map *map, int x, int y);

/* Whether a room is closed and every one of its tiles roofed. */
bool room_is_roofed(const Room *room);

#endif /* COLONY_CORE_ROOM_H */
