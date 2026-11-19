/*
 * The light of fires on screen (docs/SHADOWS.md §3.3): a burning hearth
 * lights what a straight line from its flame reaches, fading over
 * HEARTH_LIGHT_RADIUS (3D distance), through the blocks, floors and foliage
 * as the sun's rays (light_ray()). A fire lights its room and nothing behind
 * the walls, falls out of an open door as a wedge, and gives a wall or a
 * trunk near it a shadow pointing away. A block (a wall, a rock) standing on
 * the level shown takes the light of its best lit face (one not against
 * another block).
 *
 * A FireLight keeps, for each burning hearth, its light at the points of the
 * screen's fine texture (several a side per tile) within its reach: fires
 * do not move, so it is worked out again only when the fire, the level shown
 * or the map within its reach changes (Map.light_stamps), within a budget.
 *
 * Only derived from a map and its hearths: nothing here is part of the game
 * hash.
 */
#ifndef COLONY_CORE_FIRE_LIGHT_H
#define COLONY_CORE_FIRE_LIGHT_H

#include "core/map.h"
#include "core/workstation.h"

#include <stdbool.h>
#include <stdint.h>

/* How high over its floor a fire's light comes from (metres). */
#define FIRE_LIGHT_FLAME_HEIGHT 0.5f
/* Tiles from a fire to the farthest it lights, and the side of its area. */
#define FIRE_LIGHT_REACH 6
#define FIRE_LIGHT_SPAN (2 * FIRE_LIGHT_REACH + 1)

/* The light (0 .. 1) of a burning hearth on the surface drawn at the column
 * (x, y) when level z is shown (shadow_surface_height()), at the point
 * (x + u, y + v) of it; 0 where nothing is drawn. Whether the hearth burns
 * is not looked at. */
float fire_light_seen(const Workstation *station, const Map *map, int x, int y, int z, float u,
                      float v);

typedef struct FireLightSlot {
    bool ready;        /* `values` hold the light of the fire described here. */
    int16_t x, y, z;   /* Where the fire was. */
    int level;         /* The level shown. */
    int samples;       /* Points a side of a tile. */
    uint32_t revision; /* The map's light revision then. */
    uint8_t *values;   /* FIRE_LIGHT_SPAN * samples points a side, rows first. */
    int capacity;      /* Points `values` has room for. */
} FireLightSlot;

typedef struct FireLight {
    const Map *map;                       /* What it was worked out from (not owned). */
    FireLightSlot slots[WORKSTATION_MAX]; /* By station index. */
    uint64_t rays;                        /* Cast so far. */
} FireLight;

void fire_light_init(FireLight *light);
void fire_light_free(FireLight *light);

/* Works out again the light of the burning hearths that reach the tiles
 * (x0, y0) .. (x1, y1) (excluded) with level z shown and `samples` points a
 * side per tile, of those whose light is out of date, while `budget` (rays)
 * lasts: one fire is always finished once begun. False when memory ran
 * out. */
bool fire_light_update(FireLight *light, const Workstations *workstations, const Map *map, int z,
                       int samples, int x0, int y0, int x1, int y1, int64_t *budget);

/* The light of the fires (the brightest at each point, 0 .. 1) at the
 * points of `columns` x `rows` tiles from (x0, y0) with `samples` points a
 * side, rows of points first (columns * samples a row), into `out`; fires
 * whose light was never worked out for this level and these points are left
 * out (an out-of-date one is used). False when no fire lights any point. */
bool fire_light_fill(const FireLight *light, const Workstations *workstations, int z, int x0,
                     int y0, int columns, int rows, int samples, float *out);

#endif /* COLONY_CORE_FIRE_LIGHT_H */
