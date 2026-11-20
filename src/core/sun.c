#include "core/sun.h"

#include "core/game_clock.h"

#include <math.h>
#include <stddef.h>

#define PI 3.14159265358979323846
#define DEGREES (PI / 180.0)

float sun_declination(int day_of_year)
{
    /* At latitude φ the sun stays up for 2·h0 / 15° hours, with
     * cos h0 = -tan φ tan δ: the declination that gives the game's day. */
    double half_day = 0.5 * 15.0 * DEGREES * (double)game_day_length(day_of_year);
    double declination = atan(-cos(half_day) / tan((double)SUN_LATITUDE_DEFAULT * DEGREES));
    return (float)(declination / DEGREES);
}

SunPosition sun_position(float latitude, int day_of_year, float hour)
{
    double phi = (double)latitude * DEGREES;
    double delta = (double)sun_declination(day_of_year) * DEGREES;
    double h = 15.0 * DEGREES * ((double)hour - 12.0); /* The hour angle: 0 at noon. */

    /* The direction of the sun in the local frame (east, north, up), worked
     * out as vectors so that nothing breaks at the poles or at the zenith. */
    double east = -cos(delta) * sin(h);
    double north = cos(phi) * sin(delta) - sin(phi) * cos(delta) * cos(h);
    double up = sin(phi) * sin(delta) + cos(phi) * cos(delta) * cos(h);

    SunPosition sun;
    sun.elevation = (float)(asin(fmax(-1.0, fmin(1.0, up))) / DEGREES);
    double azimuth = atan2(east, north) / DEGREES;
    sun.azimuth = (float)(azimuth < 0.0 ? azimuth + 360.0 : azimuth);
    sun.east = (float)east;
    sun.south = (float)-north;
    sun.up = (float)up;
    return sun;
}

SunPosition sun_at_tick(uint64_t tick, float latitude)
{
    float hour = (float)(tick % TICKS_PER_DAY) / (float)TICKS_PER_HOUR;
    return sun_position(latitude, game_day_of_year(tick), hour);
}

/* ------------------------------------------------------------------------ */
/* The sun's rays                                                           */
/* ------------------------------------------------------------------------ */

/* Metres in a level. */
#define LEVEL_HEIGHT 2.0f
/* Under this, what is left of a ray counts as nothing. */
#define LIT_EPSILON 1.0e-3f
#define FAR 1.0e30f

/* Whether the floor of a cell (the plane at its bottom) stops the light. */
static bool floor_blocks_light(uint8_t terrain)
{
    return terrain != TERRAIN_OPEN && !terrain_is_foliage(terrain);
}

/* Distance along the ray to the first boundary of the cell on one axis, and
 * between two boundaries (`size`: the cell's size on that axis). */
static void axis_start(float position, float direction, float size, int cell, float *next,
                       float *delta)
{
    if (direction > 0.0f) {
        *delta = size / direction;
        *next = ((float)(cell + 1) * size - position) / direction;
    } else if (direction < 0.0f) {
        *delta = -size / direction;
        *next = ((float)cell * size - position) / direction;
    } else {
        *delta = FAR;
        *next = FAR;
    }
}

float light_ray(const Map *map, float dx, float dy, float dz, float x, float y, float height,
                float distance)
{
    return light_ray_ahead(map, NULL, dx, dy, dz, x, y, height, distance);
}

/* The level from which nothing is ahead of the cell (cx, cy). */
static int top_ahead(const LightAhead *ahead, int cx, int cy)
{
    return ahead->tops[(cy >> ahead->shift) * ahead->cells_x + (cx >> ahead->shift)];
}

float light_ray_ahead(const Map *map, const LightAhead *ahead, float dx, float dy, float dz,
                      float x, float y, float height, float distance)
{
    int cx = (int)floorf(x), cy = (int)floorf(y), cz = (int)floorf(height / LEVEL_HEIGHT);
    if (cx < 0 || cy < 0 || cx >= map->width || cy >= map->height || cz < 0) {
        return 0.0f;
    }
    if (cz >= map->depth) {
        return dz >= 0.0f ? 1.0f : 0.0f;
    }
    if (dz <= 0.0f) {
        ahead = NULL; /* Only known for rising rays. */
    }
    int top = ahead != NULL ? top_ahead(ahead, cx, cy) : map->depth;
    if (cz >= top) {
        return 1.0f; /* Rising over everything ahead. */
    }
    size_t index = map_index(map, cx, cy, cz);
    if (map_stops_light_at(map, index)) {
        return 0.0f;
    }
    const ptrdiff_t level = (ptrdiff_t)map_level_tile_count(map);
    const ptrdiff_t step_x = dx > 0.0f ? 1 : -1;
    const ptrdiff_t step_y = dy > 0.0f ? map->width : -map->width;
    float next_x, next_y, next_z, delta_x, delta_y, delta_z;
    axis_start(x, dx, 1.0f, cx, &next_x, &delta_x);
    axis_start(y, dy, 1.0f, cy, &next_y, &delta_y);
    axis_start(height, dz, LEVEL_HEIGHT, cz, &next_z, &delta_z);
    float bare = map_bare_fraction(map);

    float light = 1.0f, from = 0.0f;
    for (;;) {
        /* Not fminf(): a call into the maths library where NaNs are kept. */
        float to = next_x < next_y ? next_x : next_y;
        to = next_z < to ? next_z : to;
        uint8_t terrain = map->terrain[index];
        if (terrain_is_foliage(terrain)) {
            float share = map_foliage_light((Terrain)terrain, bare);
            light *= powf(share, ((to < distance ? to : distance) - from) / LEVEL_HEIGHT);
            if (light < LIT_EPSILON) {
                return 0.0f;
            }
        }
        if (to >= distance) {
            return light;
        }
        from = to;
        if (to == next_z) {
            next_z += delta_z;
            if (dz > 0.0f) { /* Up into the cell above, through its floor. */
                if (++cz >= top || cz >= map->depth) {
                    return light;
                }
                index += (size_t)level;
                if (floor_blocks_light(map->terrain[index])) {
                    return 0.0f;
                }
            } else { /* Down through the floor of this cell. */
                if (floor_blocks_light(map->terrain[index]) || --cz < 0) {
                    return 0.0f;
                }
                index -= (size_t)level;
            }
        } else if (to == next_x) {
            cx += (int)step_x;
            if (cx < 0 || cx >= map->width) {
                return light;
            }
            index = (size_t)((ptrdiff_t)index + step_x);
            next_x += delta_x;
            if (ahead != NULL && (top = top_ahead(ahead, cx, cy)) <= cz) {
                return light;
            }
        } else {
            cy += step_y > 0 ? 1 : -1;
            if (cy < 0 || cy >= map->height) {
                return light;
            }
            index = (size_t)((ptrdiff_t)index + step_y);
            next_y += delta_y;
            if (ahead != NULL && (top = top_ahead(ahead, cx, cy)) <= cz) {
                return light;
            }
        }
        if (map_stops_light_at(map, index)) {
            return 0.0f;
        }
    }
}

float sun_lit(const Map *map, SunPosition sun, float x, float y, float height)
{
    return sun_lit_ahead(map, NULL, sun, x, y, height);
}

float sun_lit_ahead(const Map *map, const LightAhead *ahead, SunPosition sun, float x, float y,
                    float height)
{
    if (sun.up <= 0.0f) {
        return 0.0f;
    }
    float flat = sqrtf(sun.east * sun.east + sun.south * sun.south);
    float reach = flat > 0.0f ? (float)SUN_SHADOW_REACH / flat : FAR;
    return light_ray_ahead(map, ahead, sun.east, sun.south, sun.up, x, y, height, reach);
}

float sun_lit_floor(const Map *map, SunPosition sun, float x, float y, int z)
{
    return sun_lit(map, sun, x, y, (float)z * LEVEL_HEIGHT + SUN_RAY_LIFT);
}
