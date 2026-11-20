#include "core/soil.h"

#include "core/i18n.h"
#include "core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int imin(int a, int b)
{
    return a < b ? a : b;
}

/* docs/CLIMATE.md §3 (tuning): mm of water in the root zone. */
static const SoilDef SOIL_DEFS[TERRAIN_COUNT] = {
    [TERRAIN_SAND] = {8.0f, 40.0f, 90.0f, 0.25f, 40.0f},
    [TERRAIN_GRAVEL] = {12.0f, 55.0f, 100.0f, 0.18f, 25.0f},
    [TERRAIN_SOIL] = {40.0f, 140.0f, 200.0f, 0.06f, 12.0f},
    [TERRAIN_RICH_SOIL] = {55.0f, 180.0f, 250.0f, 0.05f, 15.0f},
};

const SoilDef *soil_def(Terrain terrain)
{
    if ((unsigned)terrain >= TERRAIN_COUNT || SOIL_DEFS[terrain].capacity <= 0.0f) {
        return NULL;
    }
    return &SOIL_DEFS[terrain];
}

/* The bucket of a tile, NULL when it is not rooting ground. */
static const SoilDef *tile_def(const Map *map, size_t index)
{
    if (map->rock[index] != ROCK_NONE) {
        return NULL;
    }
    return soil_def((Terrain)map->terrain[index]);
}

float soil_moisture_of(const SoilDef *def, float water)
{
    if (water <= def->capacity) {
        return (water - def->wilting) / (def->capacity - def->wilting);
    }
    return 1.0f + (water - def->capacity) / (def->saturation - def->capacity);
}

float soil_water_for(const SoilDef *def, float moisture)
{
    if (moisture <= 1.0f) {
        return def->wilting + moisture * (def->capacity - def->wilting);
    }
    return def->capacity + (moisture - 1.0f) * (def->saturation - def->capacity);
}

float soil_bank_moisture(int distance)
{
    /* Saturated next to the water, half-way at 2, field capacity at 3 and 4,
     * fading to nothing at SOIL_WET_FAR. */
    static const float BY_DISTANCE[SOIL_WET_FAR] = {2.0f, 2.0f, 1.5f, 1.0f, 1.0f, 0.7f, 0.4f};
    return distance >= 0 && distance < SOIL_WET_FAR ? BY_DISTANCE[distance] : 0.0f;
}

float soil_bank_target(const SoilDef *def, int distance)
{
    float moisture = soil_bank_moisture(distance);
    return moisture > 0.0f ? soil_water_for(def, moisture) : 0.0f;
}

/* Whether a tile counts as open water for the banks of level z: water on it,
 * or on the tile under it (a lake in a hollow, seen from its banks). */
static bool water_for(const Map *map, int x, int y, int z)
{
    return map_is_water(map, x, y, z) || (z > 0 && map_is_water(map, x, y, z - 1));
}

void soil_refresh_banks(Soil *soil, const Map *map)
{
    if (soil->wet_revision == map->water_revision) {
        return;
    }
    soil->wet_revision = map->water_revision;
    int width = map->width, height = map->height;
    int levels = map->top_ground_level + 1 < map->depth ? map->top_ground_level + 1 : map->depth;
    for (int z = 0; z < levels; z++) {
        uint8_t *wet = soil->wet + map_index(map, 0, 0, z);
        /* Chebyshev distance to water: a pass forward, a pass back. */
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int d = water_for(map, x, y, z) ? 0 : SOIL_WET_FAR;
                if (d > 0) {
                    if (x > 0) {
                        d = imin(d, wet[y * width + x - 1] + 1);
                    }
                    if (y > 0) {
                        d = imin(d, wet[(y - 1) * width + x] + 1);
                        if (x > 0) {
                            d = imin(d, wet[(y - 1) * width + x - 1] + 1);
                        }
                        if (x + 1 < width) {
                            d = imin(d, wet[(y - 1) * width + x + 1] + 1);
                        }
                    }
                }
                wet[y * width + x] = (uint8_t)d;
            }
        }
        for (int y = height - 1; y >= 0; y--) {
            for (int x = width - 1; x >= 0; x--) {
                int d = wet[y * width + x];
                if (x + 1 < width) {
                    d = imin(d, wet[y * width + x + 1] + 1);
                }
                if (y + 1 < height) {
                    d = imin(d, wet[(y + 1) * width + x] + 1);
                    if (x + 1 < width) {
                        d = imin(d, wet[(y + 1) * width + x + 1] + 1);
                    }
                    if (x > 0) {
                        d = imin(d, wet[(y + 1) * width + x - 1] + 1);
                    }
                }
                wet[y * width + x] = (uint8_t)d;
            }
        }
    }
}

bool soil_init(Soil *soil, const Map *map, float share)
{
    memset(soil, 0, sizeof(*soil));
    size_t count = map_tile_count(map);
    if (count == 0) {
        return false;
    }
    soil->water = calloc(count, sizeof(*soil->water));
    soil->evap = calloc(count, sizeof(*soil->evap));
    soil->canopy = calloc(count, sizeof(*soil->canopy));
    soil->sun = calloc(count, sizeof(*soil->sun));
    soil->wet = malloc(count);
    if (soil->water == NULL || soil->evap == NULL || soil->canopy == NULL || soil->sun == NULL ||
        soil->wet == NULL) {
        soil_free(soil);
        return false;
    }
    memset(soil->wet, SOIL_WET_FAR, count);
    soil->width = map->width;
    soil->height = map->height;
    soil->depth = map->depth;
    soil->wet_revision = map->water_revision - 1; /* Not worked out yet. */
    soil_refresh_banks(soil, map);
    for (size_t i = 0; i < count; i++) {
        const SoilDef *def = tile_def(map, i);
        if (def != NULL) {
            float water = def->wilting + share * (def->capacity - def->wilting);
            soil->water[i] = fmaxf(water, soil_bank_target(def, soil->wet[i]));
        }
    }
    soil_look_up(soil, map, 0, count);
    memcpy(soil->sun, soil->canopy, count); /* Until the first hour. */
    return true;
}

/* The tile's own share of the sun (Soil.sun). */
static uint8_t tile_sun(const Soil *soil, const Map *map, size_t index, const SoilHour *hour)
{
    const TileLight *light = hour->light;
    if (light == NULL || index >= light->count || !tile_light_has_surface(map, index)) {
        return soil->canopy[index];
    }
    float share = hour->direct_share;
    float sun = (1.0f - share) * (float)light->open[index] + share * (float)light->direct[index];
    return (uint8_t)lroundf(clampf(sun, 0.0f, 255.0f));
}

void soil_free(Soil *soil)
{
    free(soil->water);
    free(soil->evap);
    free(soil->canopy);
    free(soil->sun);
    free(soil->wet);
    memset(soil, 0, sizeof(*soil));
}

float soil_potential_evaporation(float sunlight, float sky, float celsius, float humidity)
{
    float warmth = clampf((celsius + 5.0f) / 25.0f, 0.0f, 1.2f);
    return 0.55f * sunlight * sky * warmth + 0.03f * clampf(1.0f - humidity, 0.0f, 1.0f);
}

float soil_snow_hour(Soil *soil, float snowfall, float celsius, bool raining)
{
    soil->snowpack += fmaxf(snowfall, 0.0f);
    float melt = celsius > 0.0f ? celsius * SOIL_MELT_PER_DEGREE * (raining ? 2.0f : 1.0f) : 0.0f;
    melt = fminf(melt, soil->snowpack);
    soil->snowpack -= melt;
    return melt;
}

/* The leaf cover of the plant on a tile, for what it transpires: its growth
 * times its kind's transpiration, nothing while it rests. */
static float leaf_cover(const Plant *plant)
{
    const PlantDef *def = plant_def((PlantType)plant->type);
    if (plant->type == PLANT_NONE || def == NULL ||
        (plant->state & ~PLANT_CLIMATE_KNOWN) != PLANT_STATE_AWAKE) {
        return 0.0f;
    }
    return clampf(plant->growth, 0.0f, 1.0f) * def->transpiration;
}

/* What covers a tile, as `canopy` stores it. */
static uint8_t look_up(const Map *map, int x, int y, int z)
{
    if (map_cover_level(map, x, y, z) >= 0) {
        return 0;
    }
    long light = lroundf(clampf(map_light_cover(map, x, y, z), 0.0f, 1.0f) * 255.0f);
    return (uint8_t)(light < 1 ? 1 : light);
}

/* Whether the soil keeps anything about a tile: rooting ground, water, any
 * floor (stone, roofs, foliage) the microclimate warms. */
static bool has_surface(const Map *map, size_t index)
{
    return map->rock[index] == ROCK_NONE && map->terrain[index] != TERRAIN_OPEN;
}

void soil_look_up(Soil *soil, const Map *map, size_t first, size_t last)
{
    size_t count = map_tile_count(map);
    last = last < count ? last : count;
    for (size_t i = first; i < last; i++) {
        if (has_surface(map, i)) {
            int x, y, z;
            map_tile_of(map, i, &x, &y, &z);
            soil->canopy[i] = look_up(map, x, y, z);
        }
    }
}

/* The rooting ground nearest the foot of a trunk (`index`, ROCK_WOOD), where
 * its roots spread: two tiles at most, a trunk can be two wide. SIZE_MAX if
 * none. */
static size_t beside_trunk(const Map *map, size_t index)
{
    int x, y, z;
    map_tile_of(map, index, &x, &y, &z);
    while (z > 0 && map->rock[map_index(map, x, y, z - 1)] == ROCK_WOOD) {
        z--; /* Down to its foot. */
    }
    for (int ring = 1; ring <= 2; ring++) {
        for (int dy = -ring; dy <= ring; dy++) {
            for (int dx = -ring; dx <= ring; dx++) {
                if ((abs(dx) == ring || abs(dy) == ring) && map_in_bounds(map, x + dx, y + dy, z)) {
                    size_t side = map_index(map, x + dx, y + dy, z);
                    if (tile_def(map, side) != NULL) {
                        return side;
                    }
                }
            }
        }
    }
    return SIZE_MAX;
}

/* The ground a crown's roots draw from: the first tile under foliage that is
 * neither open space, foliage nor trunk; under the trunk, the ground beside
 * its foot. SIZE_MAX if none. */
static size_t ground_under(const Map *map, size_t index)
{
    size_t level = map_level_tile_count(map);
    while (index >= level) {
        index -= level;
        uint8_t rock = map->rock[index];
        uint8_t terrain = map->terrain[index];
        if (terrain == TERRAIN_OPEN || (rock == ROCK_NONE && terrain_is_foliage(terrain))) {
            continue; /* Open space, foliage, the trunk over its foot. */
        }
        return rock == ROCK_WOOD ? beside_trunk(map, index) : index;
    }
    return SIZE_MAX;
}

/* How freely plants rooted in a tile draw water, 0 (pores closed) .. 1. */
static float open_to_dry(const SoilDef *def, float water)
{
    float moisture = (water - def->wilting) / (def->capacity - def->wilting);
    return clampf(moisture / SOIL_DRY_BELOW, 0.0f, 1.0f);
}

/* The share of the sun a crown's foliage turns into vapour: its leaves
 * transpire while the ground under it is moist; bare, it is as that ground
 * (its sun goes through to it). */
static uint8_t foliage_evaporation(const Soil *soil, const Map *map, size_t index)
{
    size_t ground = ground_under(map, index);
    if (ground == SIZE_MAX) {
        return 0;
    }
    const SoilDef *def = tile_def(map, ground);
    float drawn = def != NULL ? open_to_dry(def, soil->water[ground]) : 1.0f;
    float bare = map_bare_fraction(map);
    float share = (1.0f - bare) * drawn + bare * (float)soil->evap[ground] * (1.0f / 255.0f);
    return (uint8_t)lroundf(clampf(share, 0.0f, 1.0f) * 255.0f);
}

void soil_update(Soil *soil, const Map *map, size_t first, size_t last, const SoilHour *hour,
                 const float *temperature)
{
    if (soil->water == NULL || soil->width != map->width || soil->height != map->height ||
        soil->depth != map->depth) {
        return;
    }
    size_t count = map_tile_count(map);
    last = last < count ? last : count;
    for (size_t i = first; i < last; i++) {
        if (!has_surface(map, i)) {
            continue;
        }
        if (hour->look_up) {
            int x, y, z;
            map_tile_of(map, i, &x, &y, &z);
            soil->canopy[i] = look_up(map, x, y, z);
        }
        bool covered = soil->canopy[i] == 0;
        float canopy = (float)soil->canopy[i] * (1.0f / 255.0f);
        soil->sun[i] = tile_sun(soil, map, i, hour);
        float sun = (float)soil->sun[i] * (1.0f / 255.0f);
        const SoilDef *def = tile_def(map, i);
        Terrain terrain = (Terrain)map->terrain[i];
        if (def == NULL) {
            /* Water turns all its sun into vapour, foliage what its roots
             * draw; stone and roofs none. */
            bool water = terrain == TERRAIN_SHALLOW_WATER || terrain == TERRAIN_DEEP_WATER;
            if (terrain_is_foliage(terrain)) {
                soil->evap[i] = foliage_evaporation(soil, map, i);
            } else {
                soil->evap[i] = water ? 255 : 0;
            }
            continue;
        }
        float water = soil->water[i];
        if (water == 0.0f) {
            water = def->capacity; /* Ground opened since: as damp as fresh earth. */
        }

        /* Rain through the crowns, and snowmelt. */
        if (!covered) {
            float input =
                hour->rain * (SOIL_THROUGHFALL_DENSE + (1.0f - SOIL_THROUGHFALL_DENSE) * canopy) +
                hour->melt;
            input = fminf(input, def->intake);
            water += fminf(input, fmaxf(def->saturation - water, 0.0f));
        }
        /* Drainage above field capacity, down to the water table near open
         * water, which draws the ground up towards it. */
        float target = soil_bank_target(def, soil->wet[i]);
        float drained = fmaxf(def->capacity, target);
        if (water > drained) {
            water -= (water - drained) * def->drainage;
        }
        if (water < target) {
            water += (target - water) * SOIL_BANK_RATE;
        }
        /* Evaporation and transpiration. */
        float drawn = open_to_dry(def, water);
        float top = fminf(1.0f, water / def->capacity);
        float lost;
        if (covered) {
            lost = hour->pet * SOIL_COVERED_EVAPORATION * top * top;
        } else {
            float leaf = fminf(leaf_cover(&map->plants[i]), 1.0f);
            /* The crowns over it take what they stop, the ground what
             * reaches it of the sun. */
            float crowns = (1.0f - canopy) * drawn;
            float plant = sun * leaf * drawn;
            float bare = sun * (1.0f - leaf) * SOIL_BARE_EVAPORATION * top * top;
            lost = hour->pet * (crowns + plant + bare);
        }
        lost = fminf(lost, 0.9f * water); /* Never quite dry: 0 means not followed. */
        water -= lost;
        /* Dew on cold open ground at night. */
        if (!covered && hour->night && temperature != NULL && temperature[i] < hour->dew_point) {
            water += SOIL_DEW_MM;
        }
        soil->water[i] = water;
        float share = hour->pet > 0.0f ? lost / hour->pet : 0.0f;
        soil->evap[i] = (uint8_t)lroundf(clampf(share, 0.0f, 1.0f) * 255.0f);
    }
}

float soil_moisture(const Soil *soil, const Map *map, int x, int y, int z)
{
    if (soil->water == NULL || !map_in_bounds(map, x, y, z)) {
        return -1.0f;
    }
    size_t index = map_index(map, x, y, z);
    if (map->rock[index] == ROCK_WOOD) {
        index = beside_trunk(map, index);
        if (index == SIZE_MAX) {
            return -1.0f;
        }
    } else if (map_is_water(map, x, y, z)) {
        return PLANT_MOISTURE_STANDING;
    }
    const SoilDef *def = tile_def(map, index);
    if (def == NULL || soil->water[index] == 0.0f) {
        return -1.0f;
    }
    return soil_moisture_of(def, soil->water[index]);
}

const char *soil_moisture_key(const Soil *soil, const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z) || tile_def(map, map_index(map, x, y, z)) == NULL) {
        return NULL; /* Not rooting ground (water, stone, a trunk...). */
    }
    float moisture = soil_moisture(soil, map, x, y, z);
    if (moisture < -0.5f) {
        return NULL;
    }
    if (moisture < SOIL_DRY_UNDER) {
        return N_("soil.dry");
    }
    if (moisture < SOIL_FAIRLY_DRY_UNDER) {
        return N_("soil.fairly_dry");
    }
    if (moisture <= SOIL_MOIST_UP_TO) {
        return N_("soil.moist");
    }
    return moisture < SOIL_SOAKED_FROM ? N_("soil.wet") : N_("soil.soaked");
}

uint64_t soil_hash(const Soil *soil, uint64_t hash)
{
    if (soil->water != NULL) {
        size_t count = (size_t)soil->width * (size_t)soil->height * (size_t)soil->depth;
        hash = hash_fnv1a64(hash, soil->water, count * sizeof(*soil->water));
        hash = hash_fnv1a64(hash, &soil->snowpack, sizeof(soil->snowpack));
    }
    return hash;
}
