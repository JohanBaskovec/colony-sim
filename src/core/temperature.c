#include "core/temperature.h"

#include "core/game_clock.h"
#include "core/structure.h"
#include "core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265f

static const float CONDUCTANCES[TEMPERATURE_MATERIAL_COUNT] = {
    [TEMPERATURE_MATERIAL_AIR] = TEMPERATURE_CONDUCTANCE_AIR,
    [TEMPERATURE_MATERIAL_WALL] = TEMPERATURE_CONDUCTANCE_WALL,
    [TEMPERATURE_MATERIAL_DOOR] = TEMPERATURE_CONDUCTANCE_DOOR,
    [TEMPERATURE_MATERIAL_ROCK] = TEMPERATURE_CONDUCTANCE_ROCK,
};

static const float EXPOSURES[TEMPERATURE_COVER_COUNT] = {
    [TEMPERATURE_COVER_OPEN] = TEMPERATURE_EXPOSURE_OPEN,
    [TEMPERATURE_COVER_ROOFED] = TEMPERATURE_EXPOSURE_ROOFED,
    [TEMPERATURE_COVER_NATURAL] = 0.0f,
};

static const float GROUND_RATES[TEMPERATURE_COVER_COUNT] = {
    [TEMPERATURE_COVER_NATURAL] = TEMPERATURE_GROUND_RATE_NATURAL_ROOF,
};

/* What a tile is, as the cache stores it. */
static uint8_t tile_kind(const Map *map, int x, int y, int z)
{
    TemperatureMaterial material = temperature_material(map, x, y, z);
    TemperatureCover cover = material == TEMPERATURE_MATERIAL_ROCK
                                 ? TEMPERATURE_COVER_NATURAL
                                 : temperature_cover(map, x, y, z);
    return (uint8_t)(material * TEMPERATURE_COVER_COUNT + cover);
}

static float kind_conductance(uint8_t kind)
{
    return CONDUCTANCES[kind / TEMPERATURE_COVER_COUNT];
}

/* Refreshes the caches over tiles x0..x1, y0..y1 of level z (clamped to the
 * map). */
static void refresh_area(Temperatures *temperatures, const Map *map, int x0, int y0, int x1,
                         int y1, int z)
{
    int width = temperatures->width, height = temperatures->height;
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 >= width ? width - 1 : x1;
    y1 = y1 >= height ? height - 1 : y1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            size_t i = map_index(map, x, y, z);
            temperatures->kind[i] = tile_kind(map, x, y, z);
            if (temperatures->kind[i] / TEMPERATURE_COVER_COUNT == TEMPERATURE_MATERIAL_ROCK) {
                temperatures->celsius[i] = TEMPERATURE_GROUND;
            }
        }
    }
    /* The edges of those tiles, those with their west and north neighbors
     * included. */
    for (int y = y0 > 0 ? y0 - 1 : 0; y <= y1; y++) {
        for (int x = x0 > 0 ? x0 - 1 : 0; x <= x1; x++) {
            size_t i = map_index(map, x, y, z);
            float here = kind_conductance(temperatures->kind[i]);
            float east = x + 1 < width ? kind_conductance(temperatures->kind[i + 1]) : 0.0f;
            float south = y + 1 < height
                              ? kind_conductance(temperatures->kind[i + (size_t)width])
                              : 0.0f;
            temperatures->edge_east[i] = fminf(here, east);
            temperatures->edge_south[i] = fminf(here, south);
        }
    }
}

/* Refreshes the caches over the whole map. */
static void refresh_all(Temperatures *temperatures, const Map *map)
{
    for (int z = 0; z < map->depth; z++) {
        refresh_area(temperatures, map, 0, 0, map->width - 1, map->height - 1, z);
    }
}

bool temperatures_init(Temperatures *temperatures, const Map *map, float outdoor)
{
    memset(temperatures, 0, sizeof(*temperatures));
    size_t count = map_tile_count(map);
    if (count == 0) {
        return false;
    }
    size_t width = (size_t)map->width;
    /* Padding: tiles on the edges read past them, at rates of 0. */
    temperatures->buffers[0] = calloc(count + 2 * width + 2, sizeof(float));
    temperatures->buffers[1] = calloc(count + 1, sizeof(float));
    temperatures->buffers[2] = calloc(count + width, sizeof(float));
    temperatures->kind = calloc(count, sizeof(*temperatures->kind));
    if (temperatures->buffers[0] == NULL || temperatures->buffers[1] == NULL ||
        temperatures->buffers[2] == NULL || temperatures->kind == NULL) {
        temperatures_free(temperatures);
        return false;
    }
    temperatures->celsius = temperatures->buffers[0] + width + 1;
    temperatures->edge_east = temperatures->buffers[1] + 1;
    temperatures->edge_south = temperatures->buffers[2] + width;
    temperatures->width = map->width;
    temperatures->height = map->height;
    temperatures->depth = map->depth;
    for (size_t i = 0; i < count; i++) {
        temperatures->celsius[i] = outdoor;
    }
    refresh_all(temperatures, map);
    temperatures->topology_revision = map->topology_revision;
    temperatures->build_revision = map->build_revision;
    return true;
}

void temperatures_free(Temperatures *temperatures)
{
    for (int i = 0; i < 3; i++) {
        free(temperatures->buffers[i]);
    }
    free(temperatures->kind);
    memset(temperatures, 0, sizeof(*temperatures));
}

float temperature_season_mean(uint64_t tick)
{
    static const float MEANS[SEASON_COUNT] = TEMPERATURE_SEASON_MEANS;
    /* Seasons: from the middle of one to the middle of the next. */
    float year = (float)(tick % TICKS_PER_YEAR) / (float)TICKS_PER_YEAR;
    float position = year * (float)SEASON_COUNT - 0.5f;
    float first = floorf(position);
    int from = ((int)first % SEASON_COUNT + SEASON_COUNT) % SEASON_COUNT;
    int to = (from + 1) % SEASON_COUNT;
    float blend = (1.0f - cosf(PI_F * (position - first))) * 0.5f;
    return lerpf(MEANS[from], MEANS[to], blend);
}

float temperature_daily_swing(uint64_t tick)
{
    /* Warming up from the coldest hour to the warmest, cooling down for the
     * rest of the day. */
    float hour = (float)(tick % TICKS_PER_DAY) / (float)TICKS_PER_HOUR;
    const float coldest = (float)TEMPERATURE_COLDEST_HOUR;
    const float warmest = (float)TEMPERATURE_WARMEST_HOUR;
    float swing;
    if (hour >= coldest && hour < warmest) {
        swing = -cosf(PI_F * (hour - coldest) / (warmest - coldest));
    } else {
        float since = fmodf(hour - warmest + (float)HOURS_PER_DAY, (float)HOURS_PER_DAY);
        swing = cosf(PI_F * since / ((float)HOURS_PER_DAY - (warmest - coldest)));
    }
    return swing;
}

float temperature_outdoor(uint64_t tick)
{
    return temperature_season_mean(tick) + TEMPERATURE_DAILY_SWING * temperature_daily_swing(tick);
}

static bool in_bounds(const Temperatures *temperatures, int x, int y, int z)
{
    return temperatures->celsius != NULL && x >= 0 && y >= 0 && z >= 0 &&
           x < temperatures->width && y < temperatures->height && z < temperatures->depth;
}

static size_t index_of(const Temperatures *temperatures, int x, int y, int z)
{
    return ((size_t)z * (size_t)temperatures->height + (size_t)y) * (size_t)temperatures->width +
           (size_t)x;
}

float temperature_at(const Temperatures *temperatures, int x, int y, int z)
{
    if (!in_bounds(temperatures, x, y, z)) {
        return TEMPERATURE_GROUND;
    }
    return temperatures->celsius[index_of(temperatures, x, y, z)];
}

void temperatures_add_heat(Temperatures *temperatures, int x, int y, int z, float degrees)
{
    if (in_bounds(temperatures, x, y, z)) {
        temperatures->celsius[index_of(temperatures, x, y, z)] += degrees;
    }
}

TemperatureMaterial temperature_material(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z) || map->rock[map_index(map, x, y, z)] != ROCK_NONE) {
        return TEMPERATURE_MATERIAL_ROCK;
    }
    StructureType structure = map_structure(map, x, y, z);
    if (structure == STRUCTURE_DOOR) {
        return TEMPERATURE_MATERIAL_DOOR;
    }
    return structure_blocks(structure) ? TEMPERATURE_MATERIAL_WALL : TEMPERATURE_MATERIAL_AIR;
}

TemperatureCover temperature_cover(const Map *map, int x, int y, int z)
{
    if (map_roof(map, x, y, z) != STRUCTURE_NONE) {
        return TEMPERATURE_COVER_ROOFED;
    }
    return map_natural_roof(map, x, y, z) ? TEMPERATURE_COVER_NATURAL : TEMPERATURE_COVER_OPEN;
}

float temperature_rot_rate(const Temperatures *temperatures, const Map *map, int x, int y, int z,
                           bool rains)
{
    if (!map_in_bounds(map, x, y, z)) {
        return 0.0f;
    }
    float celsius =
        temperatures != NULL ? temperature_at(temperatures, x, y, z) : ITEM_ROT_REFERENCE_CELSIUS;
    bool sheltered = temperature_cover(map, x, y, z) != TEMPERATURE_COVER_OPEN;
    float rate = item_rot_rate(celsius, sheltered);
    return rains && !sheltered ? rate * ITEM_ROT_RAINED_ON : rate;
}

/* Brings the caches up to date with the changes the map logged since. */
static void update_caches(Temperatures *temperatures, const Map *map)
{
    uint32_t topology = map->topology_revision - temperatures->topology_revision;
    uint32_t build = map->build_revision - temperatures->build_revision;
    if (topology == 0 && build == 0) {
        return;
    }
    bool everything = topology > MAP_TOPOLOGY_LOG_SIZE || build > MAP_BUILD_LOG_SIZE;
    for (uint32_t r = 1; r <= topology && !everything; r++) {
        uint32_t revision = temperatures->topology_revision + r;
        everything = map->topology_log[revision % MAP_TOPOLOGY_LOG_SIZE].anywhere;
    }
    if (everything) {
        refresh_all(temperatures, map);
    } else {
        /* Rock makes natural roofs around it; other changes stay on their tile. */
        const int reach = MAP_NATURAL_ROOF_REACH;
        for (uint32_t r = 1; r <= topology; r++) {
            uint32_t revision = temperatures->topology_revision + r;
            const MapTopologyChange *change = &map->topology_log[revision % MAP_TOPOLOGY_LOG_SIZE];
            refresh_area(temperatures, map, change->x - reach, change->y - reach,
                         change->x + reach, change->y + reach, change->z);
        }
        for (uint32_t r = 1; r <= build; r++) {
            uint32_t revision = temperatures->build_revision + r;
            const MapBuildChange *change = &map->build_log[revision % MAP_BUILD_LOG_SIZE];
            refresh_area(temperatures, map, change->x - reach, change->y - reach,
                         change->x + reach, change->y + reach, change->z);
        }
    }
    temperatures->topology_revision = map->topology_revision;
    temperatures->build_revision = map->build_revision;
}

void temperatures_tick(Temperatures *temperatures, const Map *map, uint64_t tick, float outdoor)
{
    if (temperatures->celsius == NULL || temperatures->width != map->width ||
        temperatures->height != map->height || temperatures->depth != map->depth) {
        return;
    }
    update_caches(temperatures, map);
    int band = (int)(tick % TEMPERATURE_SWEEP_TICKS);
    /* Rows of every level, one after the other. */
    int width = temperatures->width, rows = temperatures->height * temperatures->depth;
    int y0 = rows * band / TEMPERATURE_SWEEP_TICKS;
    int y1 = rows * (band + 1) / TEMPERATURE_SWEEP_TICKS;
    const float hours = (float)TEMPERATURE_SWEEP_TICKS / (float)TICKS_PER_HOUR;
    float *celsius = temperatures->celsius;
    const float *east = temperatures->edge_east, *south = temperatures->edge_south;
    const uint8_t *kind = temperatures->kind;
    size_t row = (size_t)width;
    for (size_t i = (size_t)y0 * row, end = (size_t)y1 * row; i < end; i++) {
        uint8_t here_kind = kind[i];
        if (here_kind / TEMPERATURE_COVER_COUNT == TEMPERATURE_MATERIAL_ROCK) {
            continue; /* Always the ground's. */
        }
        int cover = here_kind % TEMPERATURE_COVER_COUNT;
        float here = celsius[i];
        /* Edges past the map have a rate of 0 (and read the padding). The
         * west neighbor was updated just before: kept for last, so that
         * tiles wait for each other as little as possible. */
        float west = east[i - 1];
        float flow = EXPOSURES[cover] * (outdoor - here) +
                     GROUND_RATES[cover] * (TEMPERATURE_GROUND - here) +
                     east[i] * (celsius[i + 1] - here) + south[i] * (celsius[i + row] - here) +
                     south[i - row] * (celsius[i - row] - here) - west * here;
        celsius[i] = here + flow * hours + (west * hours) * celsius[i - 1];
    }
}

uint64_t temperatures_hash(const Temperatures *temperatures, uint64_t hash)
{
    if (temperatures->celsius != NULL) {
        hash = hash_fnv1a64(hash, temperatures->celsius,
                            (size_t)temperatures->width * (size_t)temperatures->height *
                                (size_t)temperatures->depth * sizeof(*temperatures->celsius));
    }
    return hash;
}
