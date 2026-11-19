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
 * Pure functions of their arguments: nothing here is part of the game hash.
 */
#ifndef COLONY_CORE_SUN_H
#define COLONY_CORE_SUN_H

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

/* Whether the sun is over the horizon. */
static inline bool sun_is_up(SunPosition sun)
{
    return sun.elevation > 0.0f;
}

#endif /* COLONY_CORE_SUN_H */
