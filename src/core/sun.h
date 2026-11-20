/*
 * Where the sun stands in the sky (docs/SHADOWS.md §2): its elevation over
 * the horizon and its azimuth, from the hour, the day of the year and the
 * latitude of the map, as for a real sun. It rises in the east, stands in the
 * south at noon (north of the tropic of Cancer) and sets in the west, high in
 * summer and low in winter.
 *
 * The map's axes: x grows to the east, y to the south (north is the top of
 * the screen), z upwards (levels). The direction towards the sun is given in
 * metres along those axes (a tile is 1 m wide, a level 2 m high): whoever
 * walks the grid scales the height.
 *
 * The declination of the sun over the year is the one that gives, at
 * SUN_LATITUDE_DEFAULT, the day lengths of core/game_clock.h: there, the sun
 * rises and sets exactly at game_sunrise() and a day length later. Other
 * latitudes get the sun of that latitude for the same declination (polar day
 * and night included); the day lengths still come from game_clock.h until
 * they are worked out from the latitude (docs/SHADOWS.md §6, step 7).
 *
 * The sun's rays (§3.1): how much of the direct sun reaches a point of the
 * map's volume is what is left of a straight line from it towards the sun,
 * walked cell by cell through the grid as the lines of sight of
 * core/vision.h. Only the blocks of the map stop it: rock (trunks included),
 * walls and doors fill their cell; a floor (the ground of a level, a built
 * roof) is a plane at the bottom of its cell; foliage fills its cell and lets
 * through its share (map_foliage_light()) for a ray that crosses one level
 * straight down, more or less by the length of the ray in it. Settlers,
 * animals, items, plants and furniture are not blocks: they cast nothing.
 *
 * Pure functions of their arguments: nothing here is part of the game hash.
 */
#ifndef COLONY_CORE_SUN_H
#define COLONY_CORE_SUN_H

#include "core/map.h"

#include <stdbool.h>
#include <stdint.h>

/* Latitude of the map (degrees, north positive): the one at which the days
 * last what core/game_clock.h says (8.5 h at midwinter, 15.5 h at midsummer).
 * A value of the world later (docs/SHADOWS.md §2.1). */
#define SUN_LATITUDE_DEFAULT 45.6f

typedef struct SunPosition {
    float elevation; /* Degrees over the horizon, negative under it. */
    float azimuth;   /* Degrees clockwise from north: 90 east, 180 south, 270 west. */
    /* Unit vector from the ground towards the sun, in metres along the map's
     * axes: east (+x), south (+y), up (+z). */
    float east, south, up;
} SunPosition;

/* The sun's declination on a day of the year (degrees): about +23.4 at
 * midsummer, -23.4 at midwinter, 0 at the equinoxes. */
float sun_declination(int day_of_year);

/* The sun at a fractional hour of a day of the year (0 .. 24, noon at 12),
 * seen from a latitude (degrees, -90 .. 90). */
SunPosition sun_position(float latitude, int day_of_year, float hour);

/* The sun at a tick of the game clock, seen from a latitude. */
SunPosition sun_at_tick(uint64_t tick, float latitude);

/* How far a ray towards the sun is followed (tiles, along the ground): past
 * it, nothing more is in the way. A ray also stops when it leaves the map,
 * through its top or its sides. */
#define SUN_SHADOW_REACH 40
/* Under this elevation (degrees) the cast shadows fade out with the direct
 * light: at dawn and dusk the sky alone lights the world (applied by who
 * puts the light together, §2.3). */
#define SUN_LOW_ELEVATION 8.0f
/* How far over the surface it lights a ray starts (metres). */
#define SUN_RAY_LIFT 0.01f

/* How much of the direct sun reaches the point (x, y, height) of the map's
 * volume, 0 .. 1: x and y in tiles (the tile (x, y) spans x .. x + 1),
 * `height` in metres over the bottom of level 0 (a level is 2 m high). 0
 * when the sun is down, inside a block or out of the map's sides; 1 above
 * the map. The crowns are as bare as the map says (map_bare_fraction()). */
float sun_lit(const Map *map, SunPosition sun, float x, float y, float height);

/* What is left of a ray of light from the point (x, y, height) along the
 * unit direction (dx east, dy south, dz up; metres) over `distance` metres,
 * 0 .. 1, through the blocks, floors and foliage as for the sun: 0 from
 * inside a block, out of the map's sides or under it; a ray that leaves the
 * map through its sides or its top before `distance` keeps what is left. */
float light_ray(const Map *map, float dx, float dy, float dz, float x, float y, float height,
                float distance);

/* What a rising ray along one direction may still meet, per cell of
 * 1 << `shift` columns a side (`cells_x` a row): the level from which
 * nothing stops the light ahead of any point of the cell, along that
 * direction and within the ray's reach (core/tile_light.h works it out). A
 * ray ends as soon as it rises there, with the same result as the whole
 * ray. */
typedef struct LightAhead {
    const uint8_t *tops;
    int cells_x;
    int shift;
} LightAhead;

/* light_ray() and sun_lit() cut short by what is ahead (NULL: the whole
 * ray). */
float light_ray_ahead(const Map *map, const LightAhead *ahead, float dx, float dy, float dz,
                      float x, float y, float height, float distance);
float sun_lit_ahead(const Map *map, const LightAhead *ahead, SunPosition sun, float x, float y,
                    float height);

/* sun_lit() of a point SUN_RAY_LIFT over the floor of level `z`: the ground
 * of a tile, or the top of a block of level z - 1 (a wall, a rock). */
float sun_lit_floor(const Map *map, SunPosition sun, float x, float y, int z);

/* Whether the sun is over the horizon. */
static inline bool sun_is_up(SunPosition sun)
{
    return sun.elevation > 0.0f;
}

#endif /* COLONY_CORE_SUN_H */
