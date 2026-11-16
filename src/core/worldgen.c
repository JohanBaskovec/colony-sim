#include "core/worldgen.h"

#include "core/noise.h"
#include "core/rng.h"
#include "core/util.h"

#include <math.h>

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

/*
 * Stones: every stony tile (see TerrainDef.stony) gets 0 to MAP_STONES_MAX
 * stones. On flint patches, some of them are flint: where the flint noise is
 * above FLINT_NOISE_THRESHOLD, the share of flint rises to FLINT_MAX_CHANCE
 * percent at the top of the noise, scaled by the ground (flint weathers out
 * of rock: plenty on gravel and under rock walls, less on soil, little under
 * the deep rich soil). Shares below FLINT_MIN_CHANCE are dropped, so that
 * patches have clear edges; even a rich patch gives cobbles too.
 */
#define FLINT_NOISE_THRESHOLD 0.85f
#define FLINT_MIN_CHANCE 5
#define FLINT_MAX_CHANCE 60

static float flint_ground_factor(Terrain terrain)
{
    switch (terrain) {
    case TERRAIN_GRAVEL:
    case TERRAIN_ROUGH_STONE: return 1.0f;
    case TERRAIN_SOIL: return 0.7f;
    case TERRAIN_RICH_SOIL: return 0.3f;
    default: return 0.0f;
    }
}

static int flint_chance(Terrain terrain, float noise)
{
    float strength = (noise - FLINT_NOISE_THRESHOLD) / (1.0f - FLINT_NOISE_THRESHOLD);
    if (strength <= 0.0f) {
        return 0;
    }
    int chance = (int)((float)FLINT_MAX_CHANCE * clampf(strength * 1.5f, 0.0f, 1.0f) *
                       flint_ground_factor(terrain));
    return chance >= FLINT_MIN_CHANCE ? chance : 0;
}

/* Stones of a stony tile, each one flint with `chance` percent. */
static void place_stones(Map *map, int x, int y, int z, Rng *rng, int chance)
{
    int stones = rng_range_i32(rng, 0, MAP_STONES_MAX);
    int flint = 0;
    for (int i = 0; i < stones && chance > 0; i++) {
        flint += rng_range_i32(rng, 0, 99) < chance;
    }
    map_set_stones(map, x, y, z, stones - flint, flint);
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

/* A plant of a type, if the tile is fertile enough for it, with a chance. */
typedef struct PlantChance {
    PlantType type;
    float chance;
} PlantChance;

static void place_plant(Map *map, size_t index, Rng *rng, const WorldGenParams *params,
                        float moisture, float forest)
{
    Terrain terrain = (Terrain)map->terrain[index];
    float fertility = terrain_def(terrain)->fertility;
    float roll = rng_next_float(rng);
    float growth_roll = rng_next_float(rng);

    /* Mostly scattered trees, with dense forests where the forest noise is
     * high; shrubs like their edges, low plants the open meadows or the damp
     * woods. */
    float forest_factor = clampf((forest - 0.5f) * 3.0f, 0.0f, 1.0f);
    float edge = forest_factor > 0.0f ? 1.0f - fabsf(2.0f * forest_factor - 1.0f) : 0.0f;
    float open = 1.0f - forest_factor;
    const PlantChance chances[] = {
        {PLANT_TREE,
         params->tree_density * (0.04f + 0.9f * forest_factor) * (0.5f + 0.5f * moisture)},
        {PLANT_HAZEL, 0.006f + 0.05f * edge},
        {PLANT_BRAMBLE, 0.015f + 0.025f * edge},
        {PLANT_WILD_GARLIC, 0.12f * forest_factor},
        {PLANT_NETTLE, 0.04f * moisture},
        {PLANT_PLANTAIN, 0.04f * open},
        {PLANT_WILD_CARROT, terrain == TERRAIN_SOIL ? 0.015f * open : 0.0f},
        {PLANT_GRASS, 0.35f + 0.6f * moisture},
    };
    Plant *plant = &map->plants[index];
    float sum = 0.0f;
    for (size_t i = 0; i < sizeof(chances) / sizeof(chances[0]); i++) {
        const PlantDef *def = plant_def(chances[i].type);
        if (fertility < def->min_fertility) {
            continue;
        }
        sum += chances[i].chance;
        if (roll < sum) {
            plant->type = (uint8_t)chances[i].type;
            /* Trees are older; grass and low plants anything. */
            plant->growth = chances[i].type == PLANT_TREE ? 0.4f + 0.6f * growth_roll
                                                          : 0.2f + 0.8f * growth_roll;
            break;
        }
    }
    plant_settle(plant, GAME_START_DAY);
}

/* Whether a tile is water, or next to it (8 neighbors). */
static bool by_water(const Map *map, int x, int y, int z)
{
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (map_is_water(map, x + dx, y + dy, z)) {
                return true;
            }
        }
    }
    return false;
}

/* Cattails along the shores: in the shallows next to the land, and on the
 * land next to the water, in place of grass. */
static void place_cattails(Map *map, Rng *rng)
{
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            int z = map_ground_level(map, x, y);
            size_t index = map_index(map, x, y, z);
            Terrain terrain = (Terrain)map->terrain[index];
            Plant *plant = &map->plants[index];
            if (terrain == TERRAIN_DEEP_WATER || map->rock[index] != ROCK_NONE ||
                (plant->type != PLANT_NONE && plant->type != PLANT_GRASS) ||
                !by_water(map, x, y, z)) {
                continue;
            }
            bool shallows = terrain == TERRAIN_SHALLOW_WATER;
            if (shallows) { /* Only at the edge: next to the land. */
                bool shore = false;
                for (int d = 0; d < 4 && !shore; d++) {
                    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
                    int nx = x + DX[d], ny = y + DY[d];
                    shore = map_in_bounds(map, nx, ny, z) && !map_is_water(map, nx, ny, z);
                }
                if (!shore) {
                    continue;
                }
            }
            float roll = rng_next_float(rng);
            float growth_roll = rng_next_float(rng);
            if (roll < (shallows ? 0.3f : 0.15f)) {
                *plant = (Plant){.type = PLANT_CATTAIL, .growth = 0.4f + 0.6f * growth_roll};
                plant_settle(plant, GAME_START_DAY);
            }
        }
    }
}

bool worldgen_generate(Map *map, const WorldGenParams *params)
{
    if (!map_init(map, params->width, params->height, 1)) { /* One level for now. */
        return false;
    }

    const uint32_t elevation_seed = derive_seed(params->seed, 1);
    const uint32_t moisture_seed = derive_seed(params->seed, 2);
    const uint32_t rock_seed = derive_seed(params->seed, 3);
    const uint32_t forest_seed = derive_seed(params->seed, 4);
    const uint32_t flint_seed = derive_seed(params->seed, 5);
    const float frequency = 1.0f / (params->feature_scale > 1.0f ? params->feature_scale : 1.0f);

    Rng rng;
    rng_seed(&rng, params->seed, 0x776f726c64ULL /* "world" */);
    Rng stone_rng; /* A stream of its own: the plants do not depend on the stones. */
    rng_seed(&stone_rng, params->seed, 0x73746f6e6573ULL /* "stones" */);

    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            float nx = (float)x * frequency;
            float ny = (float)y * frequency;
            float elevation = stretch(noise_fbm2d(elevation_seed, nx, ny, 5, 2.0f, 0.5f));
            float moisture =
                stretch(noise_fbm2d(moisture_seed, nx * 1.3f, ny * 1.3f, 4, 2.0f, 0.5f));

            int z = map_ground_level(map, x, y);
            size_t index = map_index(map, x, y, z);
            Terrain terrain = pick_terrain(params, elevation, moisture);
            map->terrain[index] = (uint8_t)terrain;
            if (terrain_def(terrain)->stony) {
                float flint =
                    stretch(noise_fbm2d(flint_seed, nx * 3.0f, ny * 3.0f, 3, 2.0f, 0.5f));
                place_stones(map, x, y, z, &stone_rng, flint_chance(terrain, flint));
            }

            if (terrain == TERRAIN_ROUGH_STONE) {
                float rock_noise = noise_value2d(rock_seed, nx * 2.0f, ny * 2.0f);
                map->rock[index] = (uint8_t)(rock_noise < 0.5f ? ROCK_GRANITE : ROCK_SANDSTONE);
                continue;
            }

            float forest = stretch(noise_fbm2d(forest_seed, nx * 2.0f, ny * 2.0f, 3, 2.0f, 0.5f));
            place_plant(map, index, &rng, params, moisture, forest);
        }
    }
    Rng shore_rng; /* A stream of its own: the inland plants do not depend on the shores. */
    rng_seed(&shore_rng, params->seed, 0x73686f7265ULL /* "shore" */);
    place_cattails(map, &shore_rng);
    return true;
}
