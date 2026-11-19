#include "core/fire_light.h"

#include "core/hearth.h"
#include "core/shadow.h"
#include "core/sun.h"
#include "core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LEVEL_HEIGHT 2.0f
/* Levels under the one shown a fire may light from. */
#define LEVELS_BELOW ((int)(HEARTH_LIGHT_RADIUS / LEVEL_HEIGHT))

_Static_assert(FIRE_LIGHT_REACH >= (int)HEARTH_LIGHT_RADIUS, "a fire lights beyond its area");

/* The light of a fire at (fx, fy, flame) on the point (px, py, height). */
static float light_from(const Map *map, float fx, float fy, float flame, float px, float py,
                        float height)
{
    float dx = fx - px, dy = fy - py, dz = flame - height;
    float distance = sqrtf(dx * dx + dy * dy + dz * dz);
    float falloff = 1.0f - distance / HEARTH_LIGHT_RADIUS;
    if (falloff <= 0.0f) {
        return 0.0f;
    }
    if (distance < 1.0e-4f) {
        return falloff;
    }
    return falloff *
           light_ray(map, dx / distance, dy / distance, dz / distance, px, py, height, distance);
}

/* Just outside a face of a tile. */
#define FACE_GAP 1.0e-3f

float fire_light_seen(const Workstation *station, const Map *map, int x, int y, int z, float u,
                      float v)
{
    float height;
    if (!shadow_surface_height(map, x, y, z, &height)) {
        return 0.0f;
    }
    float fx = (float)station->x + 0.5f, fy = (float)station->y + 0.5f;
    float flame = (float)station->z * LEVEL_HEIGHT + FIRE_LIGHT_FLAME_HEIGHT;
    float px = (float)x + u, py = (float)y + v;
    int level = map_view_level(map, x, y, z);
    if (level != z || !map_stops_light_at(map, map_index(map, x, y, level))) {
        return light_from(map, fx, fy, flame, px, py, height);
    }
    /* A block standing on the level shown (under it, it is the ground): the
     * best lit of its faces, at the point facing this one, at the flame's
     * height within the block. */
    static const int SIDES[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    height = clampf(flame, (float)level * LEVEL_HEIGHT + SUN_RAY_LIFT,
                    (float)(level + 1) * LEVEL_HEIGHT - SUN_RAY_LIFT);
    float best = 0.0f;
    for (int side = 0; side < 4; side++) {
        int nx = x + SIDES[side][0], ny = y + SIDES[side][1];
        if (!map_in_bounds(map, nx, ny, level) ||
            map_stops_light_at(map, map_index(map, nx, ny, level))) {
            continue; /* No face there. */
        }
        float sx = SIDES[side][0] < 0   ? (float)x - FACE_GAP
                   : SIDES[side][0] > 0 ? (float)x + 1.0f + FACE_GAP
                                        : px;
        float sy = SIDES[side][1] < 0   ? (float)y - FACE_GAP
                   : SIDES[side][1] > 0 ? (float)y + 1.0f + FACE_GAP
                                        : py;
        best = fmaxf(best, light_from(map, fx, fy, flame, sx, sy, height));
    }
    return best;
}

/* ------------------------------------------------------------------------ */
/* Kept per fire                                                            */
/* ------------------------------------------------------------------------ */

void fire_light_init(FireLight *light)
{
    memset(light, 0, sizeof(*light));
}

void fire_light_free(FireLight *light)
{
    for (int i = 0; i < WORKSTATION_MAX; i++) {
        free(light->slots[i].values);
    }
    fire_light_init(light);
}

/* Whether a station is a fire that may light the level shown. */
static bool lights_level(const Workstation *station, int z)
{
    return station->used && station->burning && station->z <= z && z - station->z <= LEVELS_BELOW;
}

/* Whether the area of a fire meets the tiles (x0, y0) .. (x1, y1). */
static bool reaches(const Workstation *station, int x0, int y0, int x1, int y1)
{
    return station->x + FIRE_LIGHT_REACH >= x0 && station->x - FIRE_LIGHT_REACH < x1 &&
           station->y + FIRE_LIGHT_REACH >= y0 && station->y - FIRE_LIGHT_REACH < y1;
}

static bool slot_matches(const FireLightSlot *slot, const Workstation *station, int z, int samples)
{
    return slot->ready && slot->x == station->x && slot->y == station->y && slot->z == station->z &&
           slot->level == z && slot->samples == samples;
}

static bool work_out(FireLight *light, FireLightSlot *slot, const Workstation *station,
                     const Map *map, int z, int samples)
{
    int side = FIRE_LIGHT_SPAN * samples;
    if (side * side > slot->capacity) {
        uint8_t *grown = realloc(slot->values, (size_t)side * (size_t)side);
        if (grown == NULL) {
            return false;
        }
        slot->values = grown;
        slot->capacity = side * side;
    }
    const float inverse = 1.0f / (float)samples;
    int left = station->x - FIRE_LIGHT_REACH, top = station->y - FIRE_LIGHT_REACH;
    for (int ty = 0; ty < FIRE_LIGHT_SPAN; ty++) {
        for (int tx = 0; tx < FIRE_LIGHT_SPAN; tx++) {
            int x = left + tx, y = top + ty;
            bool inside = x >= 0 && y >= 0 && x < map->width && y < map->height;
            for (int sj = 0; sj < samples; sj++) {
                uint8_t *row = slot->values + (size_t)(ty * samples + sj) * (size_t)side +
                               (size_t)(tx * samples);
                for (int si = 0; si < samples; si++) {
                    float value = 0.0f;
                    if (inside) {
                        value = fire_light_seen(station, map, x, y, z, ((float)si + 0.5f) * inverse,
                                                ((float)sj + 0.5f) * inverse);
                        light->rays++;
                    }
                    row[si] = (uint8_t)lroundf(value * 255.0f);
                }
            }
        }
    }
    slot->ready = true;
    slot->x = station->x;
    slot->y = station->y;
    slot->z = station->z;
    slot->level = z;
    slot->samples = samples;
    slot->revision = map->light_revision;
    return true;
}

bool fire_light_update(FireLight *light, const Workstations *workstations, const Map *map, int z,
                       int samples, int x0, int y0, int x1, int y1, int64_t *budget)
{
    if (map != light->map) {
        for (int i = 0; i < WORKSTATION_MAX; i++) {
            light->slots[i].ready = false;
        }
        light->map = map;
    }
    for (int i = 0; i < WORKSTATION_MAX; i++) {
        const Workstation *station = &workstations->list[i];
        FireLightSlot *slot = &light->slots[i];
        if (!lights_level(station, z) || !reaches(station, x0, y0, x1, y1)) {
            continue;
        }
        bool matches = slot_matches(slot, station, z, samples);
        if (matches && map_light_stamp(map, station->x - FIRE_LIGHT_REACH,
                                       station->y - FIRE_LIGHT_REACH, station->x + FIRE_LIGHT_REACH,
                                       station->y + FIRE_LIGHT_REACH) <= slot->revision) {
            continue; /* Up to date. */
        }
        if (*budget <= 0) {
            continue;
        }
        uint64_t rays = light->rays;
        if (!work_out(light, slot, station, map, z, samples)) {
            slot->ready = false;
            return false;
        }
        *budget -= (int64_t)(light->rays - rays);
    }
    return true;
}

bool fire_light_fill(const FireLight *light, const Workstations *workstations, int z, int x0,
                     int y0, int columns, int rows, int samples, float *out)
{
    int width = columns * samples, height = rows * samples;
    bool any = false;
    for (int i = 0; i < WORKSTATION_MAX; i++) {
        const Workstation *station = &workstations->list[i];
        const FireLightSlot *slot = &light->slots[i];
        if (!lights_level(station, z) || !reaches(station, x0, y0, x0 + columns, y0 + rows) ||
            !slot_matches(slot, station, z, samples)) {
            continue;
        }
        if (!any) {
            memset(out, 0, (size_t)width * (size_t)height * sizeof(*out));
            any = true;
        }
        int side = FIRE_LIGHT_SPAN * samples;
        /* The fire's area in points of `out`. */
        int left = (station->x - FIRE_LIGHT_REACH - x0) * samples;
        int top = (station->y - FIRE_LIGHT_REACH - y0) * samples;
        int from_x = left < 0 ? -left : 0, from_y = top < 0 ? -top : 0;
        int to_x = left + side > width ? width - left : side;
        int to_y = top + side > height ? height - top : side;
        for (int j = from_y; j < to_y; j++) {
            const uint8_t *source = slot->values + (size_t)j * (size_t)side;
            float *target = out + (size_t)(top + j) * (size_t)width;
            for (int p = from_x; p < to_x; p++) {
                float value = (float)source[p] * (1.0f / 255.0f);
                if (value > target[left + p]) {
                    target[left + p] = value;
                }
            }
        }
    }
    return any;
}
