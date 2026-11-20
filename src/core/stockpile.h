/*
 * Stockpiles: zones drawn by the player where settlers store items.
 *
 * Each tile belongs to at most one stockpile (dense layer of zone ids, the
 * size of the map). A stockpile has a filter (which item types it accepts,
 * within which ranges of quality and hit points) and a priority: settlers haul
 * items to the best stockpile that accepts them, and move items from a
 * stockpile to a better one, or out of a stockpile that no longer accepts them.
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

/* How far a stockpile takes items from (Stockpile.radius): tiles in a
 * straight line from the item to the zone's bounding box, a level counting
 * as a tile. STOCKPILE_RADIUS_UNLIMITED: from anywhere; 0: from nowhere (the zone
 * takes nothing, the items already in it stay). */
#define STOCKPILE_RADIUS_UNLIMITED (-1)
#define STOCKPILE_RADIUS_MIN 0
#define STOCKPILE_RADIUS_MAX 160
#define STOCKPILE_RADIUS_DEFAULT 32

/* Inclusive range of values (quality qualities, hit point percentages). */
typedef struct IntRange {
    int min;
    int max;
} IntRange;

typedef struct Stockpile {
    bool used;
    int number; /* Shown to the player ("Stockpile 3"); unique, never reused. */
    ItemTypeMask allowed;
    IntRange quality;    /* Accepted qualities (ItemQuality). */
    IntRange hit_points; /* Accepted hit points, in percent of the maximum (0..100). */
    int priority;
    int radius; /* Items further away are left alone (STOCKPILE_RADIUS_*). */
    /* Forbidden by the player: nothing is hauled onto its tiles nor off them
     * (what lies there still counts as stored). */
    bool forbidden;
    int cell_count;
    /* Bounding box of the zone's tiles (inclusive), kept tight: lets searches
     * visit the zone without scanning the whole map. */
    int min_x, min_y, min_z, max_x, max_y, max_z;
} Stockpile;

typedef struct Stockpiles {
    int width;
    int height;
    int depth;
    uint8_t *zone; /* Stockpile index + 1 per tile, 0 when outside any stockpile. */
    Stockpile list[STOCKPILE_MAX];
    int used_count; /* Stockpiles in use. */
    int next_number;
    /* Incremented whenever zones or their rules change (tiles added or
     * removed, filter, priority, radius): haulers look again. */
    uint32_t revision;
} Stockpiles;

bool stockpiles_init(Stockpiles *stockpiles, int width, int height, int depth);
void stockpiles_free(Stockpiles *stockpiles);

/* Whether items may be stored on this tile: passable and buildable terrain. */
bool stockpile_can_cover(const Map *map, int x, int y, int z);

/*
 * Creates a stockpile from the tiles of a rectangle (corners in any order)
 * that can be covered and are not in another stockpile. Accepts every item
 * type in any condition, default priority and radius. Returns its index, or STOCKPILE_NONE when no
 * tile qualifies or the stockpile list is full.
 */
int stockpile_create(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1, int y1, int z);

/* Adds the free, coverable tiles of a rectangle to an existing stockpile.
 * Returns the number of tiles added. */
int stockpile_extend(Stockpiles *stockpiles, const Map *map, int index, int x0, int y0, int x1,
                     int y1, int z);

/*
 * Adds a rectangle to the stockpiles, as the zone tool does: `preferred` (the
 * selected stockpile, or STOCKPILE_NONE) and every stockpile the rectangle
 * overlaps become one stockpile, which gets the free coverable tiles of the
 * rectangle. The merged stockpile keeps the settings of `preferred`, else of
 * the oldest one. With no stockpile involved, a new one is created. Tiles
 * where a layer of `blocked` (layers the size of the map, the list ended by
 * NULL; or NULL) is not 0 are left out: the game passes the other kinds of
 * zones, which may not overlap, and the workstations. Returns the stockpile's
 * index, or STOCKPILE_NONE when nothing could be added.
 */
int stockpiles_add_rect(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1, int y1,
                        int z, int preferred, const uint8_t *const *blocked);

/*
 * As stockpiles_add_rect(), with `z` the level shown: each tile of the
 * rectangle stands for what is seen there (map_view_level()), the level
 * below when it is open space. A stockpile may thus cover several levels.
 */
int stockpiles_add_view_rect(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1, int y1,
                             int z, int preferred, const uint8_t *const *blocked);

/* stockpiles_remove_rect() on what is seen (map_view_level()) with level z shown. */
int stockpiles_remove_view_rect(Stockpiles *stockpiles, const Map *map, int x0, int y0, int x1,
                                int y1, int z);

/* Removes every tile of a rectangle from whatever stockpile it belongs to;
 * stockpiles left without tiles are deleted. Returns the number of tiles removed. */
int stockpiles_remove_rect(Stockpiles *stockpiles, int x0, int y0, int x1, int y1, int z);

/* What is seen in a rectangle (level z shown, map_view_level()) added to
 * stockpile `index` alone: the free coverable tiles (no merging with the
 * stockpiles it touches; `blocked` as in stockpiles_add_rect()), or taken out
 * of it alone (the others keep theirs; it is deleted once empty). Return the
 * number of tiles added or removed. */
int stockpile_extend_view(Stockpiles *stockpiles, const Map *map, int index, int x0, int y0, int x1,
                          int y1, int z, const uint8_t *const *blocked);
int stockpile_shrink_view(Stockpiles *stockpiles, const Map *map, int index, int x0, int y0, int x1,
                          int y1, int z);

void stockpile_delete(Stockpiles *stockpiles, int index);

/* NULL when the index is out of range or the slot is unused. */
const Stockpile *stockpile_get(const Stockpiles *stockpiles, int index);

/* Index of the stockpile covering a tile, or STOCKPILE_NONE. */
int stockpile_at(const Stockpiles *stockpiles, int x, int y, int z);

void stockpile_set_allowed(Stockpiles *stockpiles, int index, ItemTypeMask allowed);

/* Bounds are clamped (to the qualities, to 0..100) and swapped if min > max. */
void stockpile_set_quality_range(Stockpiles *stockpiles, int index, IntRange range);
void stockpile_set_hit_points_range(Stockpiles *stockpiles, int index, IntRange range);

/* Whether the filter of a stockpile accepts these items: allowed type, quality
 * and hit points (item_stack_hit_points_percent()) within the ranges. Items
 * that are always perfect (stone) are of the best quality and at 100 %. */
bool stockpile_accepts(const Stockpile *stockpile, ItemStack stack);

/* Clamped to STOCKPILE_PRIORITY_HIGHEST..STOCKPILE_PRIORITY_LOWEST. */
void stockpile_set_priority(Stockpiles *stockpiles, int index, int priority);

/* Clamped to STOCKPILE_RADIUS_MIN..STOCKPILE_RADIUS_MAX; a negative one, or
 * more than the maximum: STOCKPILE_RADIUS_UNLIMITED. */
void stockpile_set_radius(Stockpiles *stockpiles, int index, int radius);

/* Whether items on this tile are within the stockpile's radius. */
bool stockpile_in_radius(const Stockpile *stockpile, int x, int y, int z);

/* The radii a slider offers, finer near the zone: steps 0 ..
 * stockpile_radius_steps() - 1 from STOCKPILE_RADIUS_MIN to STOCKPILE_RADIUS_MAX, the last
 * one unlimited (a bill's ingredient range uses the same without the last). The step of a
 * radius is the nearest one offered. */
int stockpile_radius_steps(void);
int stockpile_radius_of_step(int step);
int stockpile_radius_step(int radius);

/* Forbids or allows the whole zone (the settlers neither bring items to it nor
 * take any away). */
void stockpile_set_forbidden(Stockpiles *stockpiles, int index, bool forbidden);

/* Whether the tile is in a forbidden stockpile. */
bool stockpile_forbidden_at(const Stockpiles *stockpiles, int x, int y, int z);

/* Priority at which these items are stored on a tile: the stockpile's
 * priority if the tile is in a stockpile accepting them, 0 otherwise
 * (not stored). */
int stockpile_storage_priority(const Stockpiles *stockpiles, int x, int y, int z, ItemStack stack);

/* Adds to `stored` (per item type) the items lying in stockpiles that accept
 * them. */
void stockpiles_count_stored(const Stockpiles *stockpiles, const Items *items,
                             long stored[ITEM_TYPE_COUNT]);

uint64_t stockpiles_hash(const Stockpiles *stockpiles, uint64_t hash);

#endif /* COLONY_CORE_STOCKPILE_H */
