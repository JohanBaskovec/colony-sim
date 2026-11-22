/*
 * Allowed areas: where each settler may go of its own accord.
 *
 * The player draws areas (boxes of tiles on any levels, added and taken away)
 * and gives each settler one (Colonist.area). A settler looks for its jobs,
 * food, water, beds and walks only where its area lets it stop: a search for
 * the nearest tile skips the tiles outside, a walk toward a goal outside is
 * refused (Pathfinder.area_masks). The way there may cross outside it (round
 * a lake). The player's orders (move there, carry this), fights and flights
 * ignore the area.
 *
 * A game always has at least one area; the first of the list is the default
 * one: settlers who arrive get it, and those of an area deleted go back to it.
 * Each tile holds a bit per area (dense layer the size of the map).
 */
#ifndef COLONY_CORE_ALLOWED_AREA_H
#define COLONY_CORE_ALLOWED_AREA_H

#include "core/serialize.h"

#include <stdbool.h>
#include <stdint.h>

#define ALLOWED_AREA_MAX 16
#define ALLOWED_AREA_NAME_SIZE 32
#define ALLOWED_AREA_NONE (-1)

_Static_assert(ALLOWED_AREA_MAX <= 16, "an area is a bit of a uint16_t");

typedef struct AllowedArea {
    /* As the player typed it (UTF-8). Empty: the default name the frontend
     * shows (the first area of a game is born so). */
    char name[ALLOWED_AREA_NAME_SIZE];
    long tiles; /* Tiles it holds, on every level. */
} AllowedArea;

typedef struct AllowedAreas {
    int width;
    int height;
    int depth;
    uint16_t *masks; /* Per tile: bit i set when area i holds it. */
    AllowedArea list[ALLOWED_AREA_MAX];
    int count;         /* Areas in use: list[0 .. count - 1], the default one first. */
    uint32_t revision; /* Bumped by every change. */
} AllowedAreas;

/* One area holding the whole map, with no name. */
bool allowed_areas_init(AllowedAreas *areas, int width, int height, int depth);
void allowed_areas_free(AllowedAreas *areas);

static inline uint16_t allowed_area_bit(int index)
{
    return (uint16_t)(1u << (unsigned)index);
}

bool allowed_areas_valid(const AllowedAreas *areas, int index);
/* NULL for an index out of the list. */
const AllowedArea *allowed_areas_get(const AllowedAreas *areas, int index);

/* Whether the area holds the tile (false out of the map or for no area). */
bool allowed_areas_holds(const AllowedAreas *areas, int index, int x, int y, int z);

/* A new area holding no tile, at the end of the list. Its index, or
 * ALLOWED_AREA_NONE when there are ALLOWED_AREA_MAX already. */
int allowed_areas_add(AllowedAreas *areas, const char *name);

/* Deletes an area: those after it move up one place. Refused (false) for the
 * last area left or an index out of the list. */
bool allowed_areas_remove(AllowedAreas *areas, int index);

/* The name is cut to fit (never in the middle of a character). */
bool allowed_areas_rename(AllowedAreas *areas, int index, const char *name);

/* The area holds every tile of the map it did not hold, and none of the
 * others. */
bool allowed_areas_invert(AllowedAreas *areas, int index);

/* Adds (`held`) or takes away the tiles of a box (corners in any order, cut to
 * the map) to an area. Returns how many tiles changed. */
long allowed_areas_set_box(AllowedAreas *areas, int index, int x0, int y0, int z0, int x1, int y1,
                           int z1, bool held);

uint64_t allowed_areas_hash(const AllowedAreas *areas, uint64_t hash);

/* Saves or loads (docs/SAVE.md) the areas, already allocated at the size of
 * the map. The tile counts are worked out again when reading. */
void allowed_areas_serialize(Serializer *s, AllowedAreas *areas);

#endif /* COLONY_CORE_ALLOWED_AREA_H */
