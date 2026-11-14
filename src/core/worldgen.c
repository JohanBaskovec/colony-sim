#include "core/worldgen.h"

#include "core/noise.h"
#include "core/rng.h"
#include "core/util.h"

void worldgen_default_params(WorldGenParams *params)
{
    params->seed = 1;
    params->width = WORLDGEN_DEFAULT_SIZE;
    params->height = WORLDGEN_DEFAULT_SIZE;
    params->water_level = 0.24f;
    params->mountain_level = 0.75f;
    params->tree_density = 0.5f;
    params->feature_scale = 48.0f;
}

/* Derives independent 32-bit noise seeds from the 64-bit world seed. */
static uint32_t derive_seed(uint64_t seed, uint32_t salt)
{
    uint64_t mixed = seed ^ ((uint64_t)salt * 0x9e3779b97f4a7c15ULL);
    mixed ^= mixed >> 33;
    mixed *= 0xff51afd7ed558ccdULL;
    mixed ^= mixed >> 33;
    return (uint32_t)mixed;
}

/* fBm values cluster around 0.5: spread them out so thresholds are intuitive. */
static float stretch(float value)
{
    return clampf((value - 0.5f) * 1.9f + 0.5f, 0.0f, 1.0f);
}

static Terrain pick_terrain(const WorldGenParams *params, float elevation, float moisture)
{
    if (elevation < params->water_level - 0.08f) {
        return TERRAIN_DEEP_WATER;
    }
    if (elevation < params->water_level) {
        return TERRAIN_SHALLOW_WATER;
    }
    if (elevation < params->water_level + 0.03f) {
        return TERRAIN_SAND;
    }
    if (elevation > params->mountain_level) {
        return TERRAIN_ROUGH_STONE;
    }
    if (elevation > params->mountain_level - 0.04f) {
        return TERRAIN_GRAVEL;
    }
    if (moisture < 0.18f) {
        return TERRAIN_SAND;
    }
    return moisture > 0.6f ? TERRAIN_RICH_SOIL : TERRAIN_SOIL;
}

static void place_plant(Map *map, size_t index, Rng *rng, const WorldGenParams *params,
                        float moisture, float forest)
{
    float fertility = terrain_def((Terrain)map->terrain[index])->fertility;
    float roll = rng_next_float(rng);
    float growth_roll = rng_next_float(rng);

    float tree_chance = 0.0f;
    if (fertility >= plant_def(PLANT_TREE)->min_fertility) {
        /* Mostly scattered trees, with dense forests where the forest noise is high. */
        float forest_factor = clampf((forest - 0.5f) * 3.0f, 0.0f, 1.0f);
        tree_chance =
            params->tree_density * (0.04f + 0.9f * forest_factor) * (0.5f + 0.5f * moisture);
    }
    float bush_chance = fertility >= plant_def(PLANT_BUSH)->min_fertility ? 0.03f : 0.0f;
    float grass_chance =
        fertility >= plant_def(PLANT_GRASS)->min_fertility ? 0.35f + 0.6f * moisture : 0.0f;

    Plant *plant = &map->plants[index];
    if (roll < tree_chance) {
        plant->type = PLANT_TREE;
        plant->growth = 0.4f + 0.6f * growth_roll;
    } else if (roll < tree_chance + bush_chance) {
        plant->type = PLANT_BUSH;
        plant->growth = 0.2f + 0.8f * growth_roll;
    } else if (roll < tree_chance + bush_chance + grass_chance) {
        plant->type = PLANT_GRASS;
        plant->growth = 0.2f + 0.8f * growth_roll;
    }
}

bool worldgen_generate(Map *map, const WorldGenParams *params)
{
    if (!map_init(map, params->width, params->height)) {
        return false;
    }

    const uint32_t elevation_seed = derive_seed(params->seed, 1);
    const uint32_t moisture_seed = derive_seed(params->seed, 2);
    const uint32_t rock_seed = derive_seed(params->seed, 3);
    const uint32_t forest_seed = derive_seed(params->seed, 4);
    const float frequency = 1.0f / (params->feature_scale > 1.0f ? params->feature_scale : 1.0f);

    Rng rng;
    rng_seed(&rng, params->seed, 0x776f726c64ULL /* "world" */);

    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            float nx = (float)x * frequency;
            float ny = (float)y * frequency;
            float elevation = stretch(noise_fbm2d(elevation_seed, nx, ny, 5, 2.0f, 0.5f));
            float moisture =
                stretch(noise_fbm2d(moisture_seed, nx * 1.3f, ny * 1.3f, 4, 2.0f, 0.5f));

            size_t index = map_index(map, x, y);
            Terrain terrain = pick_terrain(params, elevation, moisture);
            map->terrain[index] = (uint8_t)terrain;

            if (terrain == TERRAIN_ROUGH_STONE) {
                float rock_noise = noise_value2d(rock_seed, nx * 2.0f, ny * 2.0f);
                map->rock[index] = (uint8_t)(rock_noise < 0.5f ? ROCK_GRANITE : ROCK_SANDSTONE);
                continue;
            }

            float forest = stretch(noise_fbm2d(forest_seed, nx * 2.0f, ny * 2.0f, 3, 2.0f, 0.5f));
            place_plant(map, index, &rng, params, moisture, forest);
        }
    }
    return true;
}
