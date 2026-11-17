#include "core/worldgen.h"

#include "core/noise.h"
#include "core/rng.h"
#include "core/tree.h"
#include "core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void worldgen_default_params(WorldGenParams *params)
{
    params->seed = 1;
    params->width = WORLDGEN_DEFAULT_SIZE;
    params->height = WORLDGEN_DEFAULT_SIZE;
    params->water_level = 0.24f;
    params->mountain_level = 0.75f;
    params->hill_level = 0.55f;
    params->level_step = 0.06f;
    params->underground_levels = WORLDGEN_UNDERGROUND_LEVELS;
    params->flat = false;
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

/* The sand along the water, up to this elevation above the water level, lies
 * in the hollow of the water, one level down (see ground_height()). */
#define BEACH_ELEVATION 0.03f

static Terrain pick_terrain(const WorldGenParams *params, float elevation, float moisture)
{
    if (elevation < params->water_level - 0.08f) {
        return TERRAIN_DEEP_WATER;
    }
    if (elevation < params->water_level) {
        return TERRAIN_SHALLOW_WATER;
    }
    if (elevation < params->water_level + BEACH_ELEVATION) {
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

/* Chances that a grown tree is old (a 2 x 2 trunk) or ancient (3 x 3), where
 * it has room. */
#define OLD_TREE_CHANCE 0.04f
#define ANCIENT_TREE_CHANCE 0.01f

/* Whether a slope leads up to (x, y, z) from beside it. */
static bool slope_leads_to(const Map *map, int x, int y, int z)
{
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    for (int d = 0; d < 4; d++) {
        if (map_can_climb(map, x + DX[d], y + DY[d], z - 1, x, y)) {
            return true;
        }
    }
    return false;
}

/* Whether a tree stands on (x, y, z). */
static bool tree_on(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) && plant_is_tree(map->plants[map_index(map, x, y, z)].type);
}

/* Whether a trunk of `size` with its foot on (x, y, z) has room: flat,
 * fertile ground under it, no other tree on it nor around it. */
static bool wide_trunk_fits(const Map *map, int x, int y, int z, int size)
{
    for (int dy = -1; dy <= size; dy++) {
        for (int dx = -1; dx <= size; dx++) {
            int tx = x + dx, ty = y + dy;
            if ((dx != 0 || dy != 0) && tree_on(map, tx, ty, z)) {
                return false;
            }
            bool under = dx >= 0 && dy >= 0 && dx < size && dy < size;
            if (under && (!map_in_bounds(map, tx, ty, z) || !map_is_buildable(map, tx, ty, z) ||
                          slope_leads_to(map, tx, ty, z) ||
                          terrain_def((Terrain)map->terrain[map_index(map, tx, ty, z)])->fertility <
                              plant_def(PLANT_TREE)->min_fertility)) {
                return false;
            }
        }
    }
    return true;
}

/*
 * Trees take room (core/tree.h). No tree grows on a tile touching another
 * one's (the 8 neighbors), so that trunks never touch and woods stay
 * passable: of two, the first in reading order stays, the other gives way
 * to grass; nor on a slope or where a slope leads up. A few grown trees
 * are old or ancient, where they have room (the plants under their trunk
 * go). Then every tree is written into the map;
 * one whose trunk has no room (under an overhang) stays a sapling.
 */
static void place_tree_volumes(Map *map, Rng *rng)
{
    for (int z = 0; z <= map->top_ground_level; z++) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                if (!tree_on(map, x, y, z)) {
                    continue;
                }
                Plant *plant = &map->plants[map_index(map, x, y, z)];
                /* Nor on a slope or at its top, where it would bar the way. */
                if (tree_on(map, x - 1, y - 1, z) || tree_on(map, x, y - 1, z) ||
                    tree_on(map, x + 1, y - 1, z) || tree_on(map, x - 1, y, z) ||
                    map_is_slope(map, x, y, z) || slope_leads_to(map, x, y, z)) {
                    *plant = (Plant){.type = PLANT_GRASS, .growth = plant->growth};
                    plant_settle(plant, GAME_START_DAY);
                    continue;
                }
                float roll = rng_next_float(rng);
                int size = roll < ANCIENT_TREE_CHANCE                     ? 3
                           : roll < ANCIENT_TREE_CHANCE + OLD_TREE_CHANCE ? 2
                                                                          : 1;
                if (size == 1 || plant->type != PLANT_TREE ||
                    plant_growth_stage(plant) < PLANT_GROWTH_STAGES - 1 ||
                    !wide_trunk_fits(map, x, y, z, size)) {
                    continue;
                }
                for (int dy = 0; dy < size; dy++) {
                    for (int dx = 0; dx < size; dx++) {
                        memset(&map->plants[map_index(map, x + dx, y + dy, z)], 0, sizeof(Plant));
                    }
                }
                *plant = (Plant){.type = PLANT_TREE, .size = (uint8_t)size, .growth = 1.0f};
                plant_settle(plant, GAME_START_DAY);
            }
        }
    }
    for (int z = 0; z <= map->top_ground_level; z++) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                Plant *plant = &map->plants[map_index(map, x, y, z)];
                TreeShape shape;
                if (!tree_shape(plant, x, y, z, &shape)) {
                    continue;
                }
                if (tree_room_to_grow(map, x, y, z, &shape)) {
                    tree_build(map, x, y, z);
                } else {
                    plant->size = 0; /* A sapling, the last stage before a trunk. */
                    plant->growth =
                        ((float)TREE_VOLUME_MIN_STAGE - 0.5f) / (float)PLANT_GROWTH_STAGES;
                    plant_settle(plant, GAME_START_DAY);
                }
            }
        }
    }
}

/* The twigs and branches the trees shed lie under them and on their foliage. */
static void place_litter(Map *map, Rng *rng)
{
    for (int z = 0; z <= map->top_ground_level; z++) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                if (plant_is_tree(map->plants[map_index(map, x, y, z)].type)) {
                    tree_scatter_litter(map, x, y, z, rng);
                }
            }
        }
    }
}

/* Level of the ground of a column, from the plains' (0): lakes and rivers
 * lie in hollows, their shallows and beaches one level down, deep water two;
 * the ground rises one level every `level_step` of elevation above
 * `hill_level`, half as fast above `mountain_level` (hills, not mountains);
 * rock outcrops stand one level above the ground around them, behind
 * cliffs. */
static int ground_height(const WorldGenParams *params, float elevation, Terrain surface)
{
    if (params->flat) {
        return 0;
    }
    if (surface == TERRAIN_DEEP_WATER) {
        return -2;
    }
    if (elevation < params->water_level + BEACH_ELEVATION) { /* Shallows and beaches. */
        return -1;
    }
    if (elevation < params->hill_level || params->level_step <= 0.0f) {
        return surface == TERRAIN_ROUGH_STONE ? 1 : 0;
    }
    float rise = fminf(elevation, params->mountain_level) - params->hill_level;
    if (elevation > params->mountain_level) {
        rise += (elevation - params->mountain_level) * 0.5f;
    }
    int height = 1 + (int)(rise / params->level_step);
    if (surface == TERRAIN_ROUGH_STONE) {
        height++;
    }
    return height;
}

/* Whether the ground of a column is stony: its inside is rock, not earth, and
 * no slope climbs it. */
static bool stony_column(Terrain surface)
{
    return surface == TERRAIN_ROUGH_STONE || surface == TERRAIN_GRAVEL;
}

/* Natural slopes: where soft ground or shallow water rises one level to an
 * orthogonal neighbor (not a rock outcrop), the lower tile slopes up to it.
 * Rock outcrops, deep water and steeper rises stay cliffs. */
static void place_slopes(Map *map, const uint8_t *heights, const uint8_t *surfaces)
{
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            size_t column = (size_t)y * (size_t)map->width + (size_t)x;
            Terrain surface = (Terrain)surfaces[column];
            if (surface == TERRAIN_ROUGH_STONE || surface == TERRAIN_DEEP_WATER) {
                continue; /* Shallows slope up their banks: one wades out. */
            }
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d], ny = y + DY[d];
                if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) {
                    continue;
                }
                size_t neighbor = (size_t)ny * (size_t)map->width + (size_t)nx;
                if (heights[neighbor] == heights[column] + 1 &&
                    surfaces[neighbor] != TERRAIN_ROUGH_STONE) {
                    map->slope[map_index(map, x, y, heights[column])] = 1;
                    map->slope_count++;
                    break;
                }
            }
        }
    }
}

bool worldgen_generate(Map *map, const WorldGenParams *params)
{
    memset(map, 0, sizeof(*map));
    if (params->width < MAP_MIN_SIZE || params->height < MAP_MIN_SIZE ||
        params->width > MAP_MAX_SIZE || params->height > MAP_MAX_SIZE) {
        return false;
    }
    const uint32_t elevation_seed = derive_seed(params->seed, 1);
    const uint32_t moisture_seed = derive_seed(params->seed, 2);
    const uint32_t rock_seed = derive_seed(params->seed, 3);
    const uint32_t forest_seed = derive_seed(params->seed, 4);
    const uint32_t flint_seed = derive_seed(params->seed, 5);
    const float frequency = 1.0f / (params->feature_scale > 1.0f ? params->feature_scale : 1.0f);

    /* First the relief, which decides how many levels the map needs. */
    size_t columns = (size_t)params->width * (size_t)params->height;
    uint8_t *heights = malloc(columns);
    uint8_t *surfaces = malloc(columns);
    float *moistures = malloc(columns * sizeof(*moistures));
    if (heights == NULL || surfaces == NULL || moistures == NULL) {
        free(heights);
        free(surfaces);
        free(moistures);
        return false;
    }
    /* The plains' level: the levels below are underground. */
    const int surface_level = params->flat ? 0 : clampi(params->underground_levels, 2, 16);
    int highest = 0;
    for (int y = 0; y < params->height; y++) {
        for (int x = 0; x < params->width; x++) {
            size_t column = (size_t)y * (size_t)params->width + (size_t)x;
            float nx = (float)x * frequency;
            float ny = (float)y * frequency;
            float elevation = stretch(noise_fbm2d(elevation_seed, nx, ny, 5, 2.0f, 0.5f));
            float moisture =
                stretch(noise_fbm2d(moisture_seed, nx * 1.3f, ny * 1.3f, 4, 2.0f, 0.5f));
            Terrain surface = pick_terrain(params, elevation, moisture);
            int height = clampi(surface_level + ground_height(params, elevation, surface), 0,
                                MAP_MAX_LEVELS - 1);
            heights[column] = (uint8_t)height;
            surfaces[column] = (uint8_t)surface;
            moistures[column] = moisture;
            highest = height > highest ? height : highest;
        }
    }
    /* Open levels over the highest ground: room for the tallest trees, and
     * for roofs everywhere. */
    int depth =
        highest + TREE_MAX_LEVELS < MAP_MAX_LEVELS ? highest + TREE_MAX_LEVELS : MAP_MAX_LEVELS;
    if (!map_init(map, params->width, params->height, depth)) {
        free(heights);
        free(surfaces);
        free(moistures);
        return false;
    }
    map->surface_level = surface_level;
    map->top_ground_level = highest;

    Rng rng;
    rng_seed(&rng, params->seed, 0x776f726c64ULL /* "world" */);
    Rng stone_rng; /* A stream of its own: the plants do not depend on the stones. */
    rng_seed(&stone_rng, params->seed, 0x73746f6e6573ULL /* "stones" */);

    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            size_t column = (size_t)y * (size_t)map->width + (size_t)x;
            float nx = (float)x * frequency;
            float ny = (float)y * frequency;
            Terrain terrain = (Terrain)surfaces[column];
            int z = heights[column];

            /* The inside of the ground: earth near the surface (rock under
             * stony ground), rock deeper down, each with a floor under it
             * should it be dug out. */
            float rock_noise = noise_value2d(rock_seed, nx * 2.0f, ny * 2.0f);
            RockType stone = rock_noise < 0.5f ? ROCK_GRANITE : ROCK_SANDSTONE;
            for (int below = 0; below < z; below++) {
                RockType fill =
                    stony_column(terrain) || z - below > WORLDGEN_EARTH_LEVELS ? stone : ROCK_EARTH;
                size_t inside = map_index(map, x, y, below);
                map->rock[inside] = (uint8_t)fill;
                map->terrain[inside] =
                    (uint8_t)(fill == ROCK_EARTH ? TERRAIN_SOIL : TERRAIN_ROUGH_STONE);
            }

            size_t index = map_index(map, x, y, z);
            map->terrain[index] = (uint8_t)terrain;
            if (terrain_def(terrain)->stony) {
                float flint = stretch(noise_fbm2d(flint_seed, nx * 3.0f, ny * 3.0f, 3, 2.0f, 0.5f));
                place_stones(map, x, y, z, &stone_rng, flint_chance(terrain, flint));
            }
            if (terrain == TERRAIN_ROUGH_STONE) {
                if (params->flat) { /* Rock walls on the only level. */
                    map->rock[index] = (uint8_t)stone;
                }
                continue; /* Bare rock on the top of outcrops. */
            }
            float forest = stretch(noise_fbm2d(forest_seed, nx * 2.0f, ny * 2.0f, 3, 2.0f, 0.5f));
            place_plant(map, index, &rng, params, moistures[column], forest);
        }
    }
    if (!params->flat) {
        place_slopes(map, heights, surfaces);
    }
    free(heights);
    free(surfaces);
    free(moistures);

    Rng shore_rng; /* A stream of its own: the inland plants do not depend on the shores. */
    rng_seed(&shore_rng, params->seed, 0x73686f7265ULL /* "shore" */);
    place_cattails(map, &shore_rng);
    Rng tree_rng; /* A stream of its own: the other plants do not depend on the trees. */
    rng_seed(&tree_rng, params->seed, 0x7472656573ULL /* "trees" */);
    place_tree_volumes(map, &tree_rng);
    Rng litter_rng; /* A stream of its own: nothing else depends on the litter. */
    rng_seed(&litter_rng, params->seed, 0x6c697474657200ULL /* "litter" */);
    place_litter(map, &litter_rng);
    return true;
}
