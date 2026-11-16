/*
 * Connectivity regions: two passable tiles have the same region id exactly
 * when one can walk from one to the other. Lets searches answer "there is no
 * way" or "there is none in reach" without flooding the whole area.
 *
 * Regions are a cache derived from the map: regions_update() rebuilds them
 * when the map topology changed (Map.topology_revision). Queries take the map
 * to check that the data is current; when it is not (the map changed since
 * the last update), they answer as if everything were connected, so a stale
 * cache can only cost time, never give a wrong "no".
 */
#ifndef COLONY_CORE_REGION_H
#define COLONY_CORE_REGION_H

#include "core/map.h"
#include "core/pathfinding.h"
#include "core/tile_box.h"

#include <stdbool.h>
#include <stdint.h>

#define REGION_NONE 0u /* Impassable tile, out of bounds, or unknown. */

typedef struct Regions {
    int width;
    int height;
    int depth;
    uint32_t *id;       /* Region of each tile, REGION_NONE when impassable. */
    uint32_t count;     /* Regions are numbered 1..count. */
    uint8_t *drinkable; /* Per region id: whether it contains a drinking spot. */
    /* Per region id: a box holding its tiles, for scans of a region. Given up
     * (boxes_lost) until the next full rebuild if memory ran out. */
    TileBox *boxes;
    uint32_t box_capacity;
    bool boxes_lost;
    uint32_t *stack;       /* Scratch for the flood fill. */
    uint32_t map_revision; /* Map.topology_revision the regions were built for. */
    bool built;
    uint32_t rebuilds; /* Full rebuilds so far (statistics, tests). */
} Regions;

bool regions_init(Regions *regions, int width, int height, int depth);
void regions_free(Regions *regions);

/* Brings the regions up to date if the map topology changed since the last
 * call. Tiles that merely opened (a rock wall dug out) are added in place;
 * anything else rebuilds everything. */
void regions_update(Regions *regions, const Map *map);

/* Whether the regions describe the map as it is now. */
bool regions_current(const Regions *regions, const Map *map);

/* Region of a tile; REGION_NONE if impassable or if the regions are not current. */
uint32_t regions_at(const Regions *regions, const Map *map, int x, int y, int z);

/* False only when both tiles are known to be in different regions. */
bool regions_connected(const Regions *regions, const Map *map, TilePos a, TilePos b);

/* A box holding every tile of the region (core/tile_box.h): scans of a
 * region can skip the rest of the map. The whole map when unknown (no
 * region, regions not current). */
TileBox regions_box(const Regions *regions, const Map *map, uint32_t region);

/* False only when the region is known to have no drinking spot
 * (map_is_drinking_spot()). */
bool regions_has_drinking_spot(const Regions *regions, const Map *map, uint32_t region);

#endif /* COLONY_CORE_REGION_H */
