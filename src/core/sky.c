#include "core/sky.h"

#include "core/shadow.h"
#include "core/sun.h"
#include "core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265f
#define FAR 1.0e30f

typedef struct SkyRay {
    float east, south, up; /* Unit direction. */
    float distance;        /* How far it is followed (metres along it). */
    float weight;
} SkyRay;

/* The rays: one straight up, then rings of five at 67.5°, 45° and 25°, each
 * turned from the one below. A ring stands for a band of the dome, and its
 * weight is the light a flat ground takes from that band of a uniform sky
 * (sin² of the band's top minus that of its bottom), shared by its rays. */
static const struct {
    float elevation, bottom, top, turn;
    int count;
} RINGS[] = {
    {90.0f, 80.0f, 90.0f, 0.0f, 1},
    {67.5f, 57.5f, 80.0f, 0.0f, 5},
    {45.0f, 35.0f, 57.5f, 36.0f, 5},
    {25.0f, SKY_LOWEST_ELEVATION, 35.0f, 18.0f, 5},
};

static SkyRay sky_rays[SKY_RAYS];
static bool sky_rays_ready;

static void make_rays(void)
{
    const float degrees = PI_F / 180.0f;
    float total = 0.0f;
    int n = 0;
    for (size_t r = 0; r < sizeof(RINGS) / sizeof(RINGS[0]); r++) {
        float top = sinf(RINGS[r].top * degrees), bottom = sinf(RINGS[r].bottom * degrees);
        float weight = (top * top - bottom * bottom) / (float)RINGS[r].count;
        float elevation = RINGS[r].elevation * degrees;
        for (int k = 0; k < RINGS[r].count && n < SKY_RAYS; k++, n++) {
            float azimuth = (RINGS[r].turn + 360.0f * (float)k / (float)RINGS[r].count) * degrees;
            float flat = cosf(elevation);
            sky_rays[n].east = flat * sinf(azimuth);
            sky_rays[n].south = -flat * cosf(azimuth);
            sky_rays[n].up = sinf(elevation);
            sky_rays[n].distance = flat > 1.0e-4f ? (float)SKY_REACH / flat : FAR;
            sky_rays[n].weight = weight;
            total += weight;
        }
    }
    for (int i = 0; i < SKY_RAYS; i++) {
        sky_rays[i].weight /= total; /* Open ground sees 1. */
    }
    sky_rays_ready = true;
}

float sky_open(const Map *map, float x, float y, float height)
{
    if (!sky_rays_ready) {
        make_rays();
    }
    float open = 0.0f;
    for (int i = 0; i < SKY_RAYS; i++) {
        const SkyRay *ray = &sky_rays[i];
        open += ray->weight *
                light_ray(map, ray->east, ray->south, ray->up, x, y, height, ray->distance);
    }
    return clampf(open, 0.0f, 1.0f);
}

float sky_open_drawn(const Map *map, int x, int y, int z)
{
    float height;
    if (!shadow_surface_height(map, x, y, z, &height)) {
        return 1.0f;
    }
    return sky_open(map, (float)x + 0.5f, (float)y + 0.5f, height);
}

/* ------------------------------------------------------------------------ */
/* The field                                                                */
/* ------------------------------------------------------------------------ */

void sky_field_init(SkyField *field)
{
    memset(field, 0, sizeof(*field));
}

void sky_field_free(SkyField *field)
{
    free(field->values);
    free(field->levels);
    free(field->revisions);
    free(field->reach_stamps);
    sky_field_init(field);
}

static bool reset_for_map(SkyField *field, const Map *map)
{
    size_t columns = map_level_tile_count(map);
    if (map->width != field->width || map->height != field->height || field->values == NULL) {
        sky_field_free(field);
        field->values = malloc(columns);
        field->levels = malloc(columns);
        field->revisions = malloc(columns * sizeof(*field->revisions));
        field->blocks_x = (map->width + MAP_LIGHT_BLOCK - 1) / MAP_LIGHT_BLOCK;
        field->blocks_y = (map->height + MAP_LIGHT_BLOCK - 1) / MAP_LIGHT_BLOCK;
        field->reach_stamps =
            malloc((size_t)field->blocks_x * (size_t)field->blocks_y * sizeof(uint32_t));
        if (field->values == NULL || field->levels == NULL || field->revisions == NULL ||
            field->reach_stamps == NULL) {
            sky_field_free(field);
            return false;
        }
        field->width = map->width;
        field->height = map->height;
    }
    memset(field->revisions, 0, columns * sizeof(*field->revisions));
    field->map = map;
    return true;
}

bool sky_field_begin(SkyField *field, const Map *map, int x0, int y0, int x1, int y1)
{
    if ((map != field->map || map->width != field->width || map->height != field->height) &&
        !reset_for_map(field, map)) {
        return false;
    }
    int bx0 = clampi(x0, 0, map->width - 1) / MAP_LIGHT_BLOCK;
    int by0 = clampi(y0, 0, map->height - 1) / MAP_LIGHT_BLOCK;
    int bx1 = clampi(x1 - 1, 0, map->width - 1) / MAP_LIGHT_BLOCK;
    int by1 = clampi(y1 - 1, 0, map->height - 1) / MAP_LIGHT_BLOCK;
    for (int by = by0; by <= by1; by++) {
        for (int bx = bx0; bx <= bx1; bx++) {
            int tx = bx * MAP_LIGHT_BLOCK, ty = by * MAP_LIGHT_BLOCK;
            field->reach_stamps[by * field->blocks_x + bx] = map_light_stamp(
                map, tx - SKY_REACH, ty - SKY_REACH, tx + MAP_LIGHT_BLOCK - 1 + SKY_REACH,
                ty + MAP_LIGHT_BLOCK - 1 + SKY_REACH);
        }
    }
    return true;
}

float sky_field_open(SkyField *field, int x, int y, int z, int *budget)
{
    const Map *map = field->map;
    size_t column = (size_t)y * (size_t)field->width + (size_t)x;
    uint32_t stamp =
        field->reach_stamps[(y / MAP_LIGHT_BLOCK) * field->blocks_x + x / MAP_LIGHT_BLOCK];
    bool known = field->revisions[column] != 0 && field->levels[column] == (uint8_t)z;
    if (known && field->revisions[column] - 1 >= stamp) {
        return (float)field->values[column] / 255.0f;
    }
    if (*budget > 0) {
        --*budget;
        float open = sky_open_drawn(map, x, y, z);
        field->rays += SKY_RAYS;
        field->values[column] = (uint8_t)lroundf(open * 255.0f);
        field->levels[column] = (uint8_t)z;
        field->revisions[column] = map->light_revision + 1;
        return open;
    }
    if (known) {
        return (float)field->values[column] / 255.0f;
    }
    int level = map_view_level(map, x, y, z);
    return level < 0 ? 1.0f : map_light_cover(map, x, y, level);
}
