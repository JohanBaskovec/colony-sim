/*
 * Stockpiles: zones drawn by the player where settlers store items.
 *
 * Each tile belongs to at most one stockpile (dense layer of zone ids, the
 * size of the map). A stockpile has a filter (which item types it accepts)
 * and a priority: settlers haul items to the best stockpile that accepts them,
 * and move items from a stockpile to a better one.
 */
#ifndef COLONY_CORE_STOCKPILE_H
#define COLONY_CORE_STOCKPILE_H

#include "core/item.h"
#include "core/map.h"

#include <stdbool.h>
#include <stdint.h>

#define STOCKPILE_MAX 64
#define STOCKPILE_NONE (-1)

/* 1 is the highest priority, like work priorities. */
#define STOCKPILE_PRIORITY_HIGHEST 1
#define STOCKPILE_PRIORITY_LOWEST 10
#define STOCKPILE_PRIORITY_DEFAULT 5

typedef struct Stockpile {
    bool used;
    int number; /* Shown to the player ("Stockpile 3"); unique, never reused. */
    ItemTypeMask allowed;
    int priority;
    int cell_count;
} Stockpile;

typedef struct Stockpiles {
    int width;
    int height;
    uint8_t *zone; /* Stockpile index + 1 per tile, 0 when outside any stockpile. */
    Stockpile list[STOCKPILE_MAX];
    int next_number;
    /* Incremented whenever zones or their rules change (tiles added or
     * removed, filter, priority): haulers look again. */
    uint32_t revision;
} Stockpiles;

bool stockpiles_init(Stockpiles *stockpiles, int width, int height);
void stockpiles_free(Stockpiles *stockpiles);

/* Whether items may be stored on this tile: passable and buildable terrain. */
bool stockpile_can_cover(const Map *map, int x, int y);

/*
 * Creates a stockpile from the tiles of a rectangle (corners in any order)
 * that can be covered and are not in another stockpile. Accepts every item
 * type, default priority. Returns its index, or STOCKPILE_NONE when no tile
 * qualifies or the stockpile list is full.
 */
int stockpile_create(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1, int y1);

/* Adds the free, coverable tiles of a rectangle to an existing stockpile.
 * Returns the number of tiles added. */
int stockpile_extend(Stockpiles *stockpiles, const Map *map, int index, int x0, int y0, int x1,
                     int y1);

/* Removes every tile of a rectangle from whatever stockpile it belongs to;
 * stockpiles left without tiles are deleted. Returns the number of tiles removed. */
int stockpiles_remove_rect(Stockpiles *stockpiles, int x0, int y0, int x1, int y1);

void stockpile_delete(Stockpiles *stockpiles, int index);

/* NULL when the index is out of range or the slot is unused. */
const Stockpile *stockpile_get(const Stockpiles *stockpiles, int index);

/* Index of the stockpile covering a tile, or STOCKPILE_NONE. */
int stockpile_at(const Stockpiles *stockpiles, int x, int y);

void stockpile_set_allowed(Stockpiles *stockpiles, int index, ItemTypeMask allowed);

/* Clamped to STOCKPILE_PRIORITY_HIGHEST..STOCKPILE_PRIORITY_LOWEST. */
void stockpile_set_priority(Stockpiles *stockpiles, int index, int priority);

/* Priority at which an item of this type is stored on a tile: the stockpile's
 * priority if the tile is in a stockpile accepting the type, 0 otherwise
 * (not stored). */
int stockpile_storage_priority(const Stockpiles *stockpiles, int x, int y, ItemType type);

uint64_t stockpiles_hash(const Stockpiles *stockpiles, uint64_t hash);

#endif /* COLONY_CORE_STOCKPILE_H */
