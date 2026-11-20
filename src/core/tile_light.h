/*
 * The light of each tile for the simulation (docs/SHADOWS.md §5.1), from the
 * real map: how much of the sky the tile sees (`open`, core/sky.h), how much
 * of its floor is in the sun now (`direct`) and the hours of sun it had
 * today and yesterday (`sun_hours`). A byte each, per tile of the levels
 * that may hold plants (Map.top_ground_level and below), for the tiles with
 * a surface: a floor (ground, water, a built roof, a cave floor) and no
 * block in the cell (rock, a trunk, a wall, a closed door). Other tiles get
 * 0.
 *
 * - `open` is kept per tile and worked out again where the map changed
 *   within SKY_REACH (Map.light_stamps): sweeps over the blocks of
 *   MAP_LIGHT_BLOCK columns work out the blocks out of date, the changes to
 *   what stops the light outright first (Map.light_solid_stamps), those to
 *   the foliage slowly (the crowns going bare stamp the whole map: about
 *   two days).
 * - `direct` is worked out every TILE_LIGHT_DIRECT_HOURS for the sun at
 *   the middle of those hours, in slices over their ticks: the mean of
 *   sun_lit() over TILE_LIGHT_SAMPLES × TILE_LIGHT_SAMPLES points spread on
 *   the floor, five of them first (the corners and the middle) and the
 *   others only when those five disagree. A tile whose `open` is 0 sees no
 *   sun.
 * - `sun_hours` adds each period's `direct` times the hours of it the sun
 *   is up, in tenths of an hour, and rolls over at midnight (when the
 *   tile's slice of the first period comes).
 *
 * Rays stop early: per block, the lowest level from which nothing stops the
 * light (its top), per cell of a few columns, and for each direction a ray takes (the sky's
 * sixteen, the sun of the hour) the highest top of the cells ahead of it within the ray's reach
 * (LightAhead, core/sun.h): a rising ray ends as soon as it is over everything still ahead of it,
 * with the same result as the whole ray.
 *
 * All of it is derived from the map and the clock on a schedule that
 * depends on the tick only: deterministic, built again at game_init(), not
 * part of the game hash (what it changes is).
 */
#ifndef COLONY_CORE_TILE_LIGHT_H
#define COLONY_CORE_TILE_LIGHT_H

#include "core/map.h"
#include "core/sky.h"
#include "core/sun.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Points a side of a tile for its share in the sun (nine: the five first,
 * its corners and its middle, among them). */
#define TILE_LIGHT_SAMPLES 3
/* The tops are kept per cell of 1 << TILE_LIGHT_CELL_SHIFT columns a side
 * (a block of MAP_LIGHT_BLOCK holds whole cells). Most cells ahead of a cell
 * along a direction. */
#define TILE_LIGHT_CELL_SHIFT 2
#define TILE_LIGHT_AHEAD_MAX 96
/* Work on `open` a tick: a tile worked out costs TILE_LIGHT_SURFACE_COST, a
 * cell looked at or a block found up to date 1 (tuning). A change to what
 * stops the light outright (a wall, a roof, a door kept open, a trunk) is
 * worked out within TILE_LIGHT_OPEN_BUDGET a tick (most within an hour of
 * game time), first; a change to the foliage alone (crowns growing, leaves
 * falling), weighing little on the light and coming all the time, within
 * TILE_LIGHT_FOLIAGE_BUDGET (the whole map in about two days). */
#define TILE_LIGHT_OPEN_BUDGET 128
#define TILE_LIGHT_FOLIAGE_BUDGET 8
#define TILE_LIGHT_SURFACE_COST 16
/* `direct` is worked out every TILE_LIGHT_DIRECT_HOURS (a whole part of a
 * day), for the sun at the middle of those hours. */
#define TILE_LIGHT_DIRECT_HOURS 2
/* sun_hours: tenths of an hour. */
#define TILE_LIGHT_SUN_HOUR_STEPS 10

/* A sweep over the blocks working out again the blocks out of date: the
 * block it is at, the next cell in it (z, then y, x in the block), the
 * light revision when it began the block. A round of the sweep that found
 * every block up to date marks it clean (the revision it follows + 1, 0:
 * not clean): nothing to do until that changes. */
typedef struct TileLightSweep {
    bool solid; /* Following Map.light_solid_stamps rather than Map.light_stamps. */
    int budget; /* Work a tick. */
    int block;
    size_t cell;
    uint32_t started;
    uint32_t round_started;
    bool round_worked;
    uint32_t clean;
} TileLightSweep;

typedef struct TileLight {
    int width, height, levels; /* levels: Map.top_ground_level + 1. */
    size_t count;              /* width × height × levels. */
    uint8_t *open;             /* Per tile: how much of the sky, 0 .. 255. */
    uint8_t *direct;           /* Per tile: its share in the sun now, 0 .. 255. */
    uint8_t *sun_today;        /* Per tile: tenths of an hour of sun today. */
    uint8_t *sun_yesterday;
    int blocks_x, blocks_y;
    int cells_x, cells_y;
    /* Per block and per cell: the lowest level from which nothing stops the
     * light. */
    uint8_t *block_top;
    uint8_t *cell_top;
    /* Per sky ray (sky_ray_direction()), per cell: the highest top ahead. */
    uint8_t *sky_tops;
    /* Per cell: the same for the sun towards (sun_east, sun_south). */
    uint8_t *sun_tops;
    float sun_east, sun_south;
    bool sun_ready;
    uint32_t tops_revision; /* The map's light revision the tops are for. */
    /* The cells ahead of a cell along each sky ray (offsets x, y). */
    int8_t sky_ahead[SKY_RAYS][TILE_LIGHT_AHEAD_MAX][2];
    int sky_ahead_count[SKY_RAYS];
    /* Per block: the light revision its `open` was worked out at, + 1 (0: never). */
    uint32_t *open_revisions;
    TileLightSweep fast;   /* Changes to what stops the light outright. */
    TileLightSweep slow;   /* Any change: the foliage growing, its leaves. */
    uint64_t first_period; /* The period of game_init(): its slices are already done. */
    uint64_t rays;         /* Cast so far (for the benchmarks). */
} TileLight;

/* Works everything out for the map at `tick`: `open` and the tops, `direct`
 * for the current period; no sun hours yet. False when memory ran out. */
bool tile_light_init(TileLight *light, const Map *map, uint64_t tick);
void tile_light_free(TileLight *light);

/* The work of the tick (game_tick(), after the tick count moved on). */
void tile_light_tick(TileLight *light, const Map *map, uint64_t tick);

/* Whether the light is worked out for a tile index (a surface, see above). */
bool tile_light_has_surface(const Map *map, size_t index);

/* 0 .. 1 (`direct`: in this period); 0 out of the stored levels, of the map or without a surface.
 */
float tile_light_open(const TileLight *light, int x, int y, int z);
float tile_light_direct(const TileLight *light, int x, int y, int z);
/* Hours of sun of the tile today so far, or yesterday. */
float tile_light_sun_hours(const TileLight *light, int x, int y, int z, bool yesterday);

/* sun_lit() of a point of the map, the rays cut short as for `direct` (the
 * tops brought up to date with the map first). */
float tile_light_sun_lit(TileLight *light, const Map *map, SunPosition sun, float x, float y,
                         float height);

/* Whether every block's `open` is up to date with the map; with what stops
 * the light outright in it (the foliage may still be out of date). */
bool tile_light_open_settled(const TileLight *light, const Map *map);
bool tile_light_open_firm(const TileLight *light, const Map *map);

#endif /* COLONY_CORE_TILE_LIGHT_H */
