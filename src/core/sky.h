/*
 * The light of the sky (docs/SHADOWS.md §3.2): how much of the sky a surface
 * sees, 0 .. 1. From the point, SKY_RAYS rays go in fixed directions spread
 * over the dome, none lower than SKY_LOWEST_ELEVATION, each weighted by the
 * light a flat ground takes from its part of the sky (more from above than
 * from the sides), through the blocks, floors and foliage as the sun's rays
 * (light_ray()), over SKY_REACH tiles at most. Open ground sees all of it; a
 * closed hut none (its door shut), so it is dark at noon; a roof on posts
 * hides only the sky above; a cave darkens from its mouth inwards; the foot
 * of a cliff sees half the sky.
 *
 * It does not depend on the sun: it changes when the map does. A field of
 * it (SkyField) keeps the value of the surface drawn on each column, worked
 * out again within a budget once the map changed within SKY_REACH
 * (Map.light_stamps), for the screen. It keeps the surface it was worked out
 * for (shadow_surface_code()), not the level shown: showing another level
 * only works out again the columns whose surface differs.
 *
 * Only derived from a map: nothing here is part of the game hash.
 */
#ifndef COLONY_CORE_SKY_H
#define COLONY_CORE_SKY_H

#include "core/map.h"
#include "core/sun.h"

#include <stdbool.h>
#include <stdint.h>

#define SKY_RAYS 16
/* Tiles a ray towards the sky is followed, along the ground. */
#define SKY_REACH 24
/* The lowest rays (degrees over the horizon): what lies lower lights a flat
 * ground little. */
#define SKY_LOWEST_ELEVATION 20.0f

/* How much of the sky the point (x, y, height) sees (as for sun_lit()). */
float sky_open(const Map *map, float x, float y, float height);
/* The same cut short by what is ahead of each ray (`aheads`: SKY_RAYS of
 * them, in the order of sky_ray_direction(), for SKY_REACH; NULL: whole
 * rays). */
float sky_open_ahead(const Map *map, const LightAhead *aheads, float x, float y, float height);
/* The unit direction of a ray (0 .. SKY_RAYS - 1): east, south, up. */
void sky_ray_direction(int ray, float *east, float *south, float *up);

/* sky_open() of the surface drawn at the column (x, y) when level z is shown
 * (shadow_surface_height()); 1 where nothing is drawn. */
float sky_open_drawn(const Map *map, int x, int y, int z);

typedef struct SkyField {
    const Map *map; /* What it was worked out from (not owned). */
    int width, height;
    uint8_t *values;     /* Per column: how much of the sky, 0 .. 255. */
    uint8_t *surfaces;   /* Per column: the surface it was worked out for. */
    uint32_t *revisions; /* Per column: the light revision then, + 1 (0: never). */
    /* Per block of MAP_LIGHT_BLOCK columns in view: the latest light change
     * within SKY_REACH of it (sky_field_begin()). */
    uint32_t *reach_stamps;
    int blocks_x, blocks_y;
    uint64_t rays; /* Cast so far. */
} SkyField;

void sky_field_init(SkyField *field);
void sky_field_free(SkyField *field);

/* Gets ready for a frame showing the tiles (x0, y0) .. (x1, y1) (excluded)
 * of `map`: starts again for another map (or one of another size), and
 * looks at what changed within reach of the blocks in view. False when
 * memory ran out. */
bool sky_field_begin(SkyField *field, const Map *map, int x0, int y0, int x1, int y1);

/* How much of the sky the surface drawn at the column (x, y) sees, `level`
 * being the level drawn there (map_view_level() of the level shown, not
 * -1): as remembered when up to date, else worked out while `budget`
 * (columns) lasts, else as remembered even out of date, else what lies
 * straight above (map_light_cover()). The column must be in the view given
 * to sky_field_begin(). */
float sky_field_open(SkyField *field, int x, int y, int level, int *budget);

#endif /* COLONY_CORE_SKY_H */
