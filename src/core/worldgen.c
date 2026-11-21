#include "core/worldgen.h"

#include "core/noise.h"
#include "core/rng.h"
#include "core/shadow.h"
#include "core/soil.h"
#include "core/sun.h"
#include "core/temperature.h"
#include "core/tree.h"
#include "core/util.h"
#include "core/weather.h"

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
    params->hill_height = WORLDGEN_HILL_HEIGHT;
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

/* Glades: the noise above which the woods open (glade_share()), and the share of
 * trees lost in the middle of one. */
#define GLADE_NOISE_FROM 0.62f
#define GLADE_TREE_LOSS 0.92f
/* Share of the trees and hazels that start young (the rest start grown). */
#define WOODY_YOUNG_SHARE 0.15f

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

/* Of the stones a stony tile would hold, one in COBBLE_RARITY is a cobble
 * that lies there (every one is an item of its own: docs/ITEMS.md). */
#define COBBLE_RARITY 50

/* Stones of a stony tile, each one flint with `chance` percent; of the
 * others, few cobbles. */
static void place_stones(Map *map, int x, int y, int z, Rng *rng, int chance)
{
    int stones = rng_range_i32(rng, 0, MAP_STONES_MAX);
    int flint = 0;
    for (int i = 0; i < stones && chance > 0; i++) {
        flint += rng_range_i32(rng, 0, 99) < chance;
    }
    int cobbles = 0;
    for (int i = flint; i < stones; i++) {
        cobbles += rng_range_i32(rng, 1, COBBLE_RARITY) == 1;
    }
    map_set_stones(map, x, y, z, cobbles, flint);
}

/* The sand along the water, up to this elevation above the water level, lies
 * on the plains around the hollow of the water (see ground_height()). */
#define BEACH_ELEVATION 0.03f
/* Deep water below this elevation under the water level, shallows above. */
#define DEEP_WATER_BELOW 0.08f

/* The water of a column: none, shallows (a hollow one level down) or deep
 * water (two levels down). */
typedef enum Lake { LAKE_NONE, LAKE_SHALLOWS, LAKE_DEEP } Lake;

static Lake pick_lake(const WorldGenParams *params, float elevation)
{
    return elevation < params->water_level - DEEP_WATER_BELOW ? LAKE_DEEP
           : elevation < params->water_level                  ? LAKE_SHALLOWS
                                                              : LAKE_NONE;
}

static Terrain pick_terrain(const WorldGenParams *params, float elevation, float moisture)
{
    if (pick_lake(params, elevation) != LAKE_NONE) {
        return TERRAIN_SILT; /* The bed of the water. */
    }
    if (params->water_level > 0.0f && elevation < params->water_level + BEACH_ELEVATION) {
        return TERRAIN_SAND; /* Beaches, where there is water. */
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

/* How forested a place is: 0 in the open, 1 in the heart of a wood (the
 * forest noise above 0.5 builds up to it). */
static float forest_share(float forest)
{
    return clampf((forest - 0.5f) * 3.0f, 0.0f, 1.0f);
}

/* Glades: small clearings in the woods, where the noise is high. */
static float glade_share(float glade)
{
    return clampf((glade - GLADE_NOISE_FROM) * 5.0f, 0.0f, 1.0f);
}

/* How far each column is from open water (Chebyshev, capped at
 * SOIL_WET_FAR), from the water of the columns (Lake). */
static void water_distances(const uint8_t *lakes, int width, int height, uint8_t *out)
{
    size_t columns = (size_t)width * (size_t)height;
    for (size_t c = 0; c < columns; c++) {
        out[c] = lakes[c] != LAKE_NONE ? 0 : SOIL_WET_FAR;
    }
    /* Two sweeps over the eight neighbors: forward, then backward. */
    for (int pass = 0; pass < 2; pass++) {
        for (int step = 0; step < height; step++) {
            int y = pass == 0 ? step : height - 1 - step;
            for (int across = 0; across < width; across++) {
                int x = pass == 0 ? across : width - 1 - across;
                size_t c = (size_t)y * (size_t)width + (size_t)x;
                int best = out[c];
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < width && ny < height) {
                            int d = out[(size_t)ny * (size_t)width + (size_t)nx] + 1;
                            best = d < best ? d : best;
                        }
                    }
                }
                out[c] = (uint8_t)best;
            }
        }
    }
}

/* The moisture a column's ground is expected to hold (docs/CLIMATE.md §3.6):
 * from the moisture of the region (0 .. 1), or the water table near open
 * water when wetter. */
static float expected_moisture(float region, int water_distance)
{
    return fmaxf(0.45f + 0.6f * region, soil_bank_moisture(water_distance));
}

/* How much of the trees of a site are of each species (docs/TREES.md §4,
 * tuning), from the forest share, the edge and clearing shares and the
 * ground: weights, about oak 30, beech 25, birch 15, ash 12, chestnut 8 and
 * 5 for each wild fruit tree on soil in an ordinary wood. The species that
 * the ground cannot hold still count in the sum: poor ground has fewer
 * trees. */
static void tree_species_weights(Terrain terrain, float forest, float edge, float clearing,
                                 float weights[PLANT_TREE_LAST - PLANT_TREE_FIRST])
{
    bool poor = terrain == TERRAIN_SAND || terrain == TERRAIN_GRAVEL;
    bool rich = terrain == TERRAIN_RICH_SOIL;
    float open = 1.0f - forest;
    float rim = edge + clearing; /* Edges and clearings. */
    float *w = weights - PLANT_TREE_FIRST;
    /* Everywhere, most in open woods and at edges; the lone trees of the
     * meadows. */
    w[PLANT_OAK] = 30.0f * (0.7f + 0.6f * open + 0.4f * edge) * (poor ? 0.4f : 1.0f);
    /* The heart of woods, on soil that is not wet (its water response). */
    w[PLANT_BEECH] = 25.0f * (0.2f + 1.6f * forest * forest) * (1.0f - 0.7f * clearing);
    /* Pioneer of poor and open ground. */
    w[PLANT_BIRCH] = 15.0f * (0.4f + 0.3f * open + 1.0f * rim) * (poor ? 4.0f : 1.0f);
    /* Rich and moist ground (its water response). */
    w[PLANT_ASH] = 12.0f * (rich ? 2.0f : 0.8f);
    /* Drained, lighter ground. */
    w[PLANT_CHESTNUT] = 8.0f * (poor ? 2.0f : 1.0f) * (0.6f + 0.6f * forest);
    /* Rare and alone, at edges and in clearings. */
    w[PLANT_WILD_APPLE] = 5.0f * (0.3f + 0.4f * open + 2.0f * rim) * (poor ? 0.5f : 1.0f);
    w[PLANT_WILD_PEAR] = 5.0f * (0.3f + 0.4f * open + 2.0f * rim) * (poor ? 0.5f : 1.0f);
}

/* The first pass: trees (mostly scattered, with dense forests where the
 * forest noise is high and glades where it is not), of a species the site
 * suits (tree_species_weights()), and hazels (at the edges of the woods).
 * Everything that grows under them is placed afterwards, once their crowns
 * are there to cast their shade (place_understory()). */
static void place_woody(Map *map, size_t index, Rng *rng, const WorldGenParams *params,
                        float moisture, float ground_water, float forest, float glade)
{
    Terrain terrain = (Terrain)map->terrain[index];
    float fertility = terrain_def(terrain)->fertility;
    float roll = rng_next_float(rng);
    float growth_roll = rng_next_float(rng);

    float forest_factor = forest_share(forest);
    float edge = forest_factor > 0.0f ? 1.0f - fabsf(2.0f * forest_factor - 1.0f) : 0.0f;
    float clearing = glade_share(glade) * forest_factor;
    float trees = params->tree_density * (0.04f + 0.9f * forest_factor) * (0.5f + 0.5f * moisture) *
                  (1.0f - GLADE_TREE_LOSS * clearing);
    enum { SPECIES = PLANT_TREE_LAST - PLANT_TREE_FIRST }; /* The hazel apart. */
    float weights[SPECIES];
    tree_species_weights(terrain, forest_factor, edge, clearing, weights);
    float total = 0.0f;
    for (int i = 0; i < SPECIES; i++) {
        total += weights[i];
    }
    PlantChance chances[SPECIES + 1];
    for (int i = 0; i < SPECIES; i++) {
        chances[i] = (PlantChance){(PlantType)(PLANT_TREE_FIRST + i), trees * weights[i] / total};
    }
    chances[SPECIES] = (PlantChance){PLANT_HAZEL, 0.006f + 0.05f * edge + 0.03f * clearing};
    Plant *plant = &map->plants[index];
    float sum = 0.0f;
    for (size_t i = 0; i < sizeof(chances) / sizeof(chances[0]); i++) {
        const PlantDef *def = plant_def(chances[i].type);
        if (fertility < def->min_fertility) {
            continue;
        }
        sum += chances[i].chance * plant_water_response(chances[i].type, ground_water);
        if (roll < sum) {
            plant->type = (uint8_t)chances[i].type;
            /* A wood is mostly grown: the shade it casts, which decides what grows under
             * it, would change within days otherwise. A few trees are young. */
            plant->growth =
                growth_roll < WOODY_YOUNG_SHARE
                    ? 0.2f + 0.7f * growth_roll / WOODY_YOUNG_SHARE
                    : 0.9f + 0.1f * (growth_roll - WOODY_YOUNG_SHARE) / (1.0f - WOODY_YOUNG_SHARE);
            break;
        }
    }
    plant_settle(plant, GAME_START_DAY);
}

/* ---- Light over the year ---------------------------------------------------- */

/* The temperature a plant's climate follows (Plant.climate) is that of the
 * air over its ground, which follows the outdoor air a day or so behind (the
 * ground's warmth, core/temperature.h). */
#define WORLDGEN_GROUND_LAG_HOURS 24

static float noon_elevation(int day)
{
    return sun_position(SUN_LATITUDE_DEFAULT, day, 12.0f).elevation;
}

/* A plant's climate at noon on each day of the year, on open ground in an
 * ordinary year: the running mean of advance_hour() over the seasons' mean
 * temperature (temperature_season_mean()), the years gone by settling it. */
static void year_climate(float climate[DAYS_PER_YEAR])
{
    const int hours = DAYS_PER_YEAR * HOURS_PER_DAY;
    uint64_t lag = (uint64_t)WORLDGEN_GROUND_LAG_HOURS * TICKS_PER_HOUR;
    uint64_t year = (uint64_t)hours * TICKS_PER_HOUR;
    float mean = temperature_season_mean(year - lag);
    for (int pass = 0; pass < 2; pass++) {
        for (int hour = 0; hour < hours; hour++) {
            uint64_t tick = year + (uint64_t)hour * TICKS_PER_HOUR - lag;
            mean += (temperature_season_mean(tick) - mean) / PLANT_CLIMATE_HOURS;
            if (hour % HOURS_PER_DAY == HOURS_PER_DAY / 2) {
                climate[hour / HOURS_PER_DAY] = mean;
            }
        }
    }
}

int worldgen_plant_dark_day(PlantType type)
{
    if ((unsigned)type >= PLANT_COUNT || type == PLANT_NONE || plant_is_tree(type)) {
        return -1;
    }
    const PlantDef *def = plant_def(type);
    if (def->dark_days <= 0.0f) {
        return -1;
    }
    float climate[DAYS_PER_YEAR];
    year_climate(climate);
    /* The year counted from its coldest day: the awake time is all of a piece. */
    int coldest = 0;
    for (int day = 1; day < DAYS_PER_YEAR; day++) {
        if (climate[day] < climate[coldest]) {
            coldest = day;
        }
    }
    int warmest = 0;
    for (int i = 1; i < DAYS_PER_YEAR; i++) {
        if (climate[(coldest + i) % DAYS_PER_YEAR] > climate[(coldest + warmest) % DAYS_PER_YEAR]) {
            warmest = i;
        }
    }
    /* Its awake days, as advance_hour() rests and wakes it, and those the
     * crowns are in leaf (the trees follow the same climate). In the spring
     * they leaf as the warmth comes; after midsummer they are counted in leaf
     * until the cold: a warm spell in the autumn (WEATHER_SPELL_MAX) keeps
     * the leaves on until it ends and the cold comes all at once, and what
     * starves in such a year is gone after a few of them. */
    bool awake[DAYS_PER_YEAR], leafy[DAYS_PER_YEAR];
    bool up = false, resting_from_heat = false; /* Until the next winter. */
    for (int i = 0; i < DAYS_PER_YEAR; i++) {
        float c = climate[(coldest + i) % DAYS_PER_YEAR];
        resting_from_heat |= def->rests_in_heat && c > def->t_max;
        up = !resting_from_heat && (up ? c >= def->t_min : c >= def->t_min + PLANT_REST_HYSTERESIS);
        awake[i] = up;
        leafy[i] = i >= warmest || plant_bare_target(c) <= 0.0f;
    }
    /* It starves when its last `dark_days` awake days under the leaves
     * (counted back from its last one: what it bore then is mended in the
     * spring, the crowns still bare) are too dark. Whether they are is down
     * to the brightest of them, the one with the highest noon sun. A plant
     * whose time under the leaves is shorter is judged on its brightest awake
     * day under them. */
    int last = DAYS_PER_YEAR - 1;
    while (last >= 0 && !(awake[last] && leafy[last])) {
        last--;
    }
    if (last < 0) {
        return -1; /* Never awake under the leaves. */
    }
    int best = -1, days = 0;
    for (int i = last; i >= 0 && awake[i] && leafy[i] && (float)days < def->dark_days; i--) {
        int day = (coldest + i) % DAYS_PER_YEAR;
        if (best < 0 || noon_elevation(day) > noon_elevation(best)) {
            best = day;
        }
        days++;
    }
    return best;
}

/* The light plants live by over the year, for a list of tiles: per dark day
 * of a plant (worldgen_plant_dark_day()), each tile's floor in the sun around
 * noon, and the skies of its season. The noon sun stands due south every
 * day, only higher or lower: days whose noon sun is within
 * SEASON_LIGHT_MERGE_DEGREES of a darker one take that one's light (a little
 * stricter, and far fewer suns to work out). */
#define SEASON_LIGHT_MERGE_DEGREES 3.0f

typedef struct SeasonLight {
    TileLight *light;
    const size_t *tiles;
    size_t count;
    int day_of[PLANT_COUNT]; /* An index in the days below, -1: the plant needs no light. */
    int day_count;
    /* Per day: the share of the light straight from the sun at noon under a
     * clear sky, the sun's strength at noon (game_sun_noon()), and the share
     * of the hours of each sky in its season (weather_season_shares()). */
    float share[PLANT_COUNT];
    float sun[PLANT_COUNT];
    float skies[PLANT_COUNT][WEATHER_TYPE_COUNT];
    float *noon; /* Per day, per tile: its floor's share in the sun around noon. */
} SeasonLight;

static bool season_light_init(SeasonLight *season, TileLight *light, const Map *map,
                              const size_t *tiles, size_t count)
{
    memset(season, 0, sizeof(*season));
    season->light = light;
    season->tiles = tiles;
    season->count = count;
    /* The plants' dark days, by their noon sun, lowest first; each takes the
     * first day it is close enough to. */
    int days[PLANT_COUNT], types[PLANT_COUNT], typed = 0;
    for (int type = 0; type < PLANT_COUNT; type++) {
        season->day_of[type] = -1;
        int day = worldgen_plant_dark_day((PlantType)type);
        if (day < 0) {
            continue;
        }
        int at = typed++;
        while (at > 0 && noon_elevation(days[at - 1]) > noon_elevation(day)) {
            days[at] = days[at - 1];
            types[at] = types[at - 1];
            at--;
        }
        days[at] = day;
        types[at] = type;
    }
    for (int i = 0; i < typed; i++) {
        int k = season->day_count - 1;
        if (k < 0 ||
            noon_elevation(days[i]) - noon_elevation(days[k]) >= SEASON_LIGHT_MERGE_DEGREES) {
            days[++k] = days[i]; /* A day of its own (k <= i: already read). */
            season->day_count = k + 1;
        }
        season->day_of[types[i]] = k;
    }
    size_t values = (size_t)season->day_count * count;
    season->noon = malloc((values > 0 ? values : 1) * sizeof(*season->noon));
    if (season->noon == NULL) {
        return false;
    }
    for (int k = 0; k < season->day_count; k++) {
        season->share[k] = shadow_direct_share(noon_elevation(days[k]), 1.0f);
        season->sun[k] = game_sun_noon(days[k]);
        weather_season_shares(game_date_from_tick((uint64_t)days[k] * TICKS_PER_DAY).season,
                              season->skies[k]);
        tile_light_noon_on(light, map, days[k], tiles, count, season->noon + (size_t)k * count);
    }
    return true;
}

static void season_light_free(SeasonLight *season)
{
    free(season->noon);
    season->noon = NULL;
}

/* Whether a plant of `type` on tiles[i] gets light enough all year round:
 * whether the hours its best light of the day is too low for it, on its dark
 * day, are few enough for it to mend from them (plant_dark_share_borne()).
 * That best light is game_light_peak_at()'s: under a clear sky, the sky it
 * sees and the sun on its floor; under clouds, only the sky it sees, dimmed
 * by them. In the shade of a crown, a plant may starve in the sun and live
 * under the clouds. */
static bool season_light_lasts(const SeasonLight *season, const Map *map, size_t i, PlantType type)
{
    int k = (unsigned)type < PLANT_COUNT ? season->day_of[type] : -1;
    if (k < 0) {
        return true;
    }
    int x, y, z;
    map_tile_of(map, season->tiles[i], &x, &y, &z);
    float open = tile_light_open(season->light, x, y, z);
    float noon = season->noon[(size_t)k * season->count + i];
    float need = plant_def(type)->light_min;
    float dark = 0.0f;
    for (int sky = 0; sky < WEATHER_TYPE_COUNT; sky++) {
        float share = sky == WEATHER_CLEAR ? season->share[k] : 0.0f;
        float light = season->sun[k] * weather_def((WeatherType)sky)->light *
                      ((1.0f - share) * open + share * noon);
        dark += light < need ? season->skies[k][sky] : 0.0f;
    }
    return dark <= plant_dark_share_borne(type);
}

size_t worldgen_starving_plants(const Map *map, TileLight *light)
{
    size_t count = 0;
    for (int pass = 0; pass < 2; pass++) {
        size_t *tiles = NULL;
        if (pass == 1) {
            tiles = malloc((count > 0 ? count : 1) * sizeof(*tiles));
            if (tiles == NULL) {
                return SIZE_MAX;
            }
        }
        size_t found = 0;
        for (int z = 0; z < light->levels; z++) {
            for (int y = 0; y < map->height; y++) {
                for (int x = 0; x < map->width; x++) {
                    size_t index = map_index(map, x, y, z);
                    PlantType type = (PlantType)map->plants[index].type;
                    if (type != PLANT_NONE && !plant_is_tree(type)) {
                        if (tiles != NULL) {
                            tiles[found] = index;
                        }
                        found++;
                    }
                }
            }
        }
        if (pass == 0) {
            count = found;
            continue;
        }
        SeasonLight season;
        if (!season_light_init(&season, light, map, tiles, count)) {
            free(tiles);
            return SIZE_MAX;
        }
        size_t starving = 0;
        for (size_t i = 0; i < count; i++) {
            starving += !season_light_lasts(&season, map, i, (PlantType)map->plants[tiles[i]].type);
        }
        season_light_free(&season);
        free(tiles);
        return starving;
    }
    return 0;
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

/* Cattails and reeds along the shores: in the shallows next to the land, and on
 * the land next to the water, in place of the low plants (trees stay), where
 * they get light enough all year. Reeds make beds of their own where the
 * `reed_seed` noise is high, cattails stand in the rest. */
static void place_shore_plants(Map *map, Rng *rng, uint32_t reed_seed, float frequency,
                               const SeasonLight *season)
{
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            int z = map_ground_level(map, x, y);
            size_t index = map_index(map, x, y, z);
            Plant *plant = &map->plants[index];
            if (map_water_class(map, x, y, z) > WATER_SHALLOWS || map->rock[index] != ROCK_NONE ||
                plant_is_tree(plant->type) || !by_water(map, x, y, z)) {
                continue;
            }
            bool shallows = map_is_water_at(map, index);
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
                bool reeds =
                    stretch(noise_fbm2d(reed_seed, (float)x * frequency * 3.0f,
                                        (float)y * frequency * 3.0f, 2, 2.0f, 0.5f)) > 0.55f;
                PlantType type = reeds ? PLANT_REED : PLANT_CATTAIL;
                size_t column = (size_t)y * (size_t)map->width + (size_t)x;
                if (!season_light_lasts(season, map, column, type)) {
                    continue; /* A shore in the shade keeps what grows there. */
                }
                *plant = (Plant){.type = (uint8_t)type, .growth = 0.4f + 0.6f * growth_roll};
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

/* Whether (x, y, z) is under the trunk of an old or ancient tree, or its
 * foot. */
static bool wide_trunk_on(const Map *map, int x, int y, int z)
{
    for (int dy = 0; dy < TREE_MAX_SIZE; dy++) {
        for (int dx = 0; dx < TREE_MAX_SIZE; dx++) {
            int size = map_in_bounds(map, x - dx, y - dy, z)
                           ? tree_size(&map->plants[map_index(map, x - dx, y - dy, z)])
                           : 0;
            if (size > 1 && size > dx && size > dy) {
                return true;
            }
        }
    }
    return false;
}

/* Whether a trunk of `size` with its foot on (x, y, z) has room: flat,
 * fertile ground under it, no other old or ancient tree on it nor around it
 * (the younger trees there give way, see place_tree_volumes()). */
static bool wide_trunk_fits(const Map *map, int x, int y, int z, int size, PlantType type)
{
    for (int dy = -1; dy <= size; dy++) {
        for (int dx = -1; dx <= size; dx++) {
            int tx = x + dx, ty = y + dy;
            if ((dx != 0 || dy != 0) && wide_trunk_on(map, tx, ty, z)) {
                return false;
            }
            bool under = dx >= 0 && dy >= 0 && dx < size && dy < size;
            if (under && (!map_in_bounds(map, tx, ty, z) || !map_is_buildable(map, tx, ty, z) ||
                          slope_leads_to(map, tx, ty, z) ||
                          terrain_def((Terrain)map->terrain[map_index(map, tx, ty, z)])->fertility <
                              plant_def(type)->min_fertility)) {
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
 * of the species that can be are old or ancient, where they have room: the
 * plants under their trunk go, and the trees around it give way. Then every
 * tree is written into the map, the old and ancient ones first (the crowns
 * of their neighbors do not take their room); one whose trunk has no room
 * (under an overhang) stays a sapling.
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
                    memset(plant, 0, sizeof(*plant)); /* The understory takes its place. */
                    continue;
                }
                float roll = rng_next_float(rng);
                int size = roll < ANCIENT_TREE_CHANCE                     ? 3
                           : roll < ANCIENT_TREE_CHANCE + OLD_TREE_CHANCE ? 2
                                                                          : 1;
                /* Only some species grow old (docs/TREES.md §3). */
                const TreeDef *def = tree_def((PlantType)plant->type);
                if (size == 3 && !def->can_be_ancient) {
                    size = def->can_be_old ? 2 : 1;
                } else if (size == 2 && !def->can_be_old) {
                    size = 1;
                }
                if (size == 1 || plant_growth_stage(plant) < PLANT_GROWTH_STAGES - 1 ||
                    !wide_trunk_fits(map, x, y, z, size, (PlantType)plant->type)) {
                    continue;
                }
                PlantType type = (PlantType)plant->type;
                for (int dy = -1; dy <= size; dy++) {
                    for (int dx = -1; dx <= size; dx++) {
                        bool under = dx >= 0 && dy >= 0 && dx < size && dy < size;
                        if (under || tree_on(map, x + dx, y + dy, z)) {
                            memset(&map->plants[map_index(map, x + dx, y + dy, z)], 0,
                                   sizeof(Plant));
                        }
                    }
                }
                *plant = (Plant){.type = (uint8_t)type, .size = (uint8_t)size, .growth = 1.0f};
                plant_settle(plant, GAME_START_DAY);
            }
        }
    }
    for (int pass = 0; pass < 2; pass++) { /* The old and ancient trees first. */
        for (int z = 0; z <= map->top_ground_level; z++) {
            for (int y = 0; y < map->height; y++) {
                for (int x = 0; x < map->width; x++) {
                    Plant *plant = &map->plants[map_index(map, x, y, z)];
                    TreeShape shape;
                    if ((tree_size(plant) > 1) != (pass == 0) ||
                        !tree_shape(plant, x, y, z, &shape)) {
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
}

/* The twigs and branches the trees shed lie on the ground under them. */
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

/* How far up the ground rises at an elevation (the hills' climb, in elevation
 * units): from `hill_level`, half as fast above `mountain_level` (hills, not
 * mountains). 0 below the hills. */
static float hill_rise(const WorldGenParams *params, float elevation)
{
    if (elevation <= params->hill_level) {
        return 0.0f;
    }
    float rise = fminf(elevation, params->mountain_level) - params->hill_level;
    if (elevation > params->mountain_level) {
        rise += (elevation - params->mountain_level) * 0.5f;
    }
    return rise;
}

/* The elevation each level up takes, so that the highest ground of the map
 * (at the elevation `top`) stands `params->hill_height` levels over the plains:
 * the hills are as tall as asked on every map, steeper or gentler as its relief
 * needs. 0 when the map has no hills. */
static float level_step(const WorldGenParams *params, float top)
{
    float rise = hill_rise(params, top);
    if (params->flat || rise <= 0.0f) {
        return 0.0f;
    }
    /* The ground there is 1 + floor(rise / step) levels up, one more when it is a
     * rock outcrop (above `mountain_level`). */
    int levels = clampi(params->hill_height, 1, WORLDGEN_MAX_HILL_HEIGHT) - 1 -
                 (top > params->mountain_level ? 1 : 0);
    return rise / ((float)(levels > 0 ? levels : 0) + 0.5f);
}

/* Level of the ground of a column, from the plains' (0): lakes and rivers
 * lie in hollows, their shallows one level down, deep water two (beaches stay
 * on the plains: water at rest has one surface over a level);
 * the ground rises one level every `step` of elevation above `hill_level`
 * (see level_step()); rock outcrops stand one level above the ground around
 * them, behind cliffs. */
static int ground_height(const WorldGenParams *params, float step, float elevation, Terrain surface,
                         Lake lake)
{
    if (params->flat) {
        return 0;
    }
    if (lake != LAKE_NONE) {
        return lake == LAKE_DEEP ? -2 : -1;
    }
    if (elevation < params->hill_level || step <= 0.0f) {
        return surface == TERRAIN_ROUGH_STONE ? 1 : 0;
    }
    int height = 1 + (int)(hill_rise(params, elevation) / step);
    if (surface == TERRAIN_ROUGH_STONE) {
        height++;
    }
    return height;
}

/* The water of a lake over its bed at `index`, at rest: every lake's surface
 * WORLDGEN_LAKE_MM over the floor of its shallows, so deep water fills the
 * level of its bed and that much of the level above (docs/WATER.md W1). A
 * flat world has a single level: its deep water is WORLDGEN_FLAT_DEEP_MM. */
static void fill_lake(Map *map, const WorldGenParams *params, size_t index, Lake lake)
{
    if (lake == LAKE_SHALLOWS) {
        map->water[index] = WORLDGEN_LAKE_MM;
    } else if (params->flat) {
        map->water[index] = WORLDGEN_FLAT_DEEP_MM;
    } else {
        map->water[index] = WATER_CELL_FULL;
        size_t above = index + map_level_tile_count(map);
        if (above < map_tile_count(map)) {
            map->water[above] = WORLDGEN_LAKE_MM;
        }
    }
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
static void place_slopes(Map *map, const uint8_t *heights, const uint8_t *surfaces,
                         const uint8_t *lakes)
{
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            size_t column = (size_t)y * (size_t)map->width + (size_t)x;
            Terrain surface = (Terrain)surfaces[column];
            if (surface == TERRAIN_ROUGH_STONE || lakes[column] == LAKE_DEEP) {
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

/*
 * The understory: what grows on the ground once the trees stand, from the
 * light that reaches each tile (its tile light on the first day: the sky it
 * sees and its sun, so that a dense crown lets little through, a glade or a
 * meadow all of it, and the foot of a cliff facing north stays in the shade;
 * each plant only where it gets light enough all year, SeasonLight), the
 * ground (its fertility, the moisture of the region) and how forested the
 * place is, in patches (a noise of its own for each, so that bracken, nettles
 * or heather stand together as they do in the wild). Under the deepest shade
 * only moss, wood sorrel and wild garlic live, and a lot of ground stays
 * bare, under its litter; the meadows hold grass, plantain and carrots, the
 * edges brambles, roses and strawberries, the poor sandy and stony ground
 * heather, bilberries and bracken (docs/PLANTS.md §10).
 */
typedef enum Patch {
    PATCH_MOSS,
    PATCH_SORREL,
    PATCH_BRACKEN,
    PATCH_NETTLE,
    PATCH_PLANTAIN,
    PATCH_STRAWBERRY,
    PATCH_BILBERRY,
    PATCH_HEATHER,
    PATCH_COUNT
} Patch;

/* The light a tile gets in a good day of the year, from its cover: the sun
 * stands lower than at midsummer's noon most of the year. */
#define UNDERSTORY_SUN 0.85f
#define UNDERSTORY_MAX 16

typedef struct Site {
    Terrain terrain;
    float fertility;
    float moisture; /* Of the region, 0 .. 1. */
    float water;    /* The moisture its ground is expected to hold (expected_moisture()). */
    float forest;   /* 0 in the open .. 1 in a wood. */
    float edge;     /* 1 at the edge of a wood. */
    float open;
    float light;
    float patch[PATCH_COUNT]; /* 0 .. 1: how thick the plant stands here. */
    /* Its light over the year: the plants that would starve are left out. */
    const SeasonLight *season;
    const Map *map;
    size_t tile; /* In the season's tiles. */
} Site;

/* 1 where the light is between `from` and `to` (its limits ramp a little). */
static float light_window(float light, float from, float to)
{
    return clampf((light - from) / 0.08f, 0.0f, 1.0f) *
           clampf((to + 0.12f - light) / 0.12f, 0.0f, 1.0f);
}

/* How well a ground suits plants of poor soil, by its terrain. */
static float poor_ground(Terrain terrain, float sand, float gravel, float soil)
{
    switch (terrain) {
    case TERRAIN_SAND: return sand;
    case TERRAIN_GRAVEL: return gravel;
    case TERRAIN_SOIL: return soil;
    default: return 0.0f;
    }
}

/* The chances of each plant on a site, in order of precedence (grass last:
 * it fills what the others leave). Returns how many. */
static int understory_chances(const Site *site, PlantChance out[UNDERSTORY_MAX])
{
    int n = 0;
    const float light = site->light, fertility = site->fertility, moisture = site->moisture;
#define ADD(plant, chance)                                                                         \
    do {                                                                                           \
        float c_ = (chance) * plant_water_response((plant), site->water);                          \
        if (c_ > 0.0f && fertility >= plant_def(plant)->min_fertility &&                           \
            season_light_lasts(site->season, site->map, site->tile, (plant))) {                    \
            out[n++] = (PlantChance){(plant), c_};                                                 \
        }                                                                                          \
    } while (0)
    float shade = clampf(1.0f - light / 0.5f, 0.0f, 1.0f);
    if (moisture >= 0.3f) {
        ADD(PLANT_MOSS,
            light_window(light, 0.03f, 9.0f) * (0.03f + 0.4f * shade * (0.4f + 0.6f * moisture) *
                                                            (0.5f + site->patch[PATCH_MOSS])));
    }
    if (moisture >= 0.35f) {
        ADD(PLANT_WOOD_SORREL, 0.2f * light_window(light, 0.05f, 0.35f) * (0.4f + 0.6f * moisture) *
                                   (0.4f + site->patch[PATCH_SORREL]));
    }
    ADD(PLANT_WILD_GARLIC, 0.12f * site->forest * light_window(light, 0.05f, 0.5f));
    ADD(PLANT_BRACKEN, 0.26f * site->patch[PATCH_BRACKEN] * light_window(light, 0.2f, 0.85f) *
                           (moisture < 0.75f ? 1.0f : 0.4f) *
                           (site->terrain == TERRAIN_RICH_SOIL ? 0.3f
                            : site->terrain == TERRAIN_SOIL    ? 0.7f
                                                               : 1.0f));
    ADD(PLANT_NETTLE,
        0.3f * moisture * site->patch[PATCH_NETTLE] * light_window(light, 0.15f, 0.9f));
    ADD(PLANT_PLANTAIN, 0.08f * site->open * (0.4f + site->patch[PATCH_PLANTAIN]) *
                            light_window(light, 0.4f, 9.0f));
    ADD(PLANT_WILD_CARROT, site->terrain == TERRAIN_SOIL
                               ? 0.03f * site->open * light_window(light, 0.4f, 9.0f)
                               : 0.0f);
    ADD(PLANT_WILD_STRAWBERRY, 0.5f * site->patch[PATCH_STRAWBERRY] * (0.3f + site->edge) *
                                   light_window(light, 0.25f, 0.8f));
    ADD(PLANT_BILBERRY, moisture < 0.7f
                            ? 0.5f * site->patch[PATCH_BILBERRY] *
                                  poor_ground(site->terrain, 0.6f, 0.8f, 0.12f) *
                                  (0.4f + site->forest) * light_window(light, 0.15f, 0.6f)
                            : 0.0f);
    ADD(PLANT_HEATHER, moisture < 0.55f ? 0.45f * site->patch[PATCH_HEATHER] *
                                              poor_ground(site->terrain, 0.9f, 0.7f, 0.1f) *
                                              light_window(light, 0.6f, 9.0f)
                                        : 0.0f);
    ADD(PLANT_DOG_ROSE, (0.008f + 0.02f * site->edge) * light_window(light, 0.5f, 9.0f));
    ADD(PLANT_BRAMBLE, (0.015f + 0.025f * site->edge) * light_window(light, 0.15f, 9.0f));
    ADD(PLANT_GRASS, (0.35f + 0.6f * moisture) * light_window(light, 0.3f, 9.0f));
#undef ADD
    return n;
}

/* Whether a tile of the ground can bear understory plants: bare of plants
 * and rock, not water. */
static bool understory_ground(const Map *map, size_t index)
{
    switch ((Terrain)map->terrain[index]) {
    case TERRAIN_SAND:
    case TERRAIN_SOIL:
    case TERRAIN_RICH_SOIL:
    case TERRAIN_GRAVEL: break;
    default: return false;
    }
    return map->rock[index] == ROCK_NONE && map->plants[index].type == PLANT_NONE &&
           map->water[index] == 0;
}

static void place_understory(Map *map, const WorldGenParams *params, const float *moistures,
                             const uint8_t *shores, const float *forests, float frequency,
                             const SeasonLight *season, Rng *rng)
{
    const float share = shadow_direct_share(noon_elevation(GAME_START_DAY), 1.0f);
    uint32_t seeds[PATCH_COUNT];
    for (int i = 0; i < PATCH_COUNT; i++) {
        seeds[i] = derive_seed(params->seed, 20u + (uint32_t)i);
    }
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            int z = map_ground_level(map, x, y);
            size_t index = map_index(map, x, y, z);
            size_t column = (size_t)y * (size_t)map->width + (size_t)x;
            float roll = rng_next_float(rng);
            float growth_roll = rng_next_float(rng);
            if (!understory_ground(map, index)) {
                continue;
            }
            Terrain terrain = (Terrain)map->terrain[index];
            Site site = {.terrain = terrain,
                         .fertility = terrain_def(terrain)->fertility,
                         .moisture = moistures[column],
                         .water = expected_moisture(moistures[column], shores[column]),
                         .forest = forest_share(forests[column]),
                         .light = ((1.0f - share) * tile_light_open(season->light, x, y, z) +
                                   share * tile_light_noon(season->light, x, y, z)) *
                                  UNDERSTORY_SUN,
                         .season = season,
                         .map = map,
                         .tile = column};
            site.edge = site.forest > 0.0f ? 1.0f - fabsf(2.0f * site.forest - 1.0f) : 0.0f;
            site.open = 1.0f - site.forest;
            for (int i = 0; i < PATCH_COUNT; i++) {
                float n = stretch(noise_fbm2d(seeds[i], (float)x * frequency * 4.0f,
                                              (float)y * frequency * 4.0f, 2, 2.0f, 0.5f));
                site.patch[i] = clampf((n - 0.5f) * 3.0f, 0.0f, 1.0f);
            }
            PlantChance chances[UNDERSTORY_MAX];
            int count = understory_chances(&site, chances);
            float sum = 0.0f;
            for (int i = 0; i < count; i++) {
                sum += chances[i].chance;
                if (roll < sum) {
                    Plant *plant = &map->plants[index];
                    plant->type = (uint8_t)chances[i].type;
                    plant->growth = 0.2f + 0.8f * growth_roll;
                    plant_settle(plant, GAME_START_DAY);
                    break;
                }
            }
        }
    }
}

bool worldgen_generate(Map *map, const WorldGenParams *params)
{
    TileLight light;
    if (!worldgen_generate_lit(map, params, &light)) {
        return false;
    }
    tile_light_free(&light);
    return true;
}

bool worldgen_generate_lit(Map *map, const WorldGenParams *params, TileLight *light)
{
    memset(map, 0, sizeof(*map));
    memset(light, 0, sizeof(*light));
    if (params->width < MAP_MIN_SIZE || params->height < MAP_MIN_SIZE ||
        params->width > MAP_MAX_SIZE || params->height > MAP_MAX_SIZE) {
        return false;
    }
    const uint32_t elevation_seed = derive_seed(params->seed, 1);
    const uint32_t moisture_seed = derive_seed(params->seed, 2);
    const uint32_t rock_seed = derive_seed(params->seed, 3);
    const uint32_t forest_seed = derive_seed(params->seed, 4);
    const uint32_t flint_seed = derive_seed(params->seed, 5);
    const uint32_t reed_seed = derive_seed(params->seed, 6);
    const uint32_t glade_seed = derive_seed(params->seed, 7);
    const float frequency = 1.0f / (params->feature_scale > 1.0f ? params->feature_scale : 1.0f);

    /* First the relief, which decides how many levels the map needs. */
    size_t columns = (size_t)params->width * (size_t)params->height;
    uint8_t *heights = malloc(columns);
    uint8_t *surfaces = malloc(columns);
    float *moistures = malloc(columns * sizeof(*moistures));
    float *forests = malloc(columns * sizeof(*forests));
    uint8_t *shores = malloc(columns);
    uint8_t *lakes = malloc(columns);
    if (heights == NULL || surfaces == NULL || moistures == NULL || forests == NULL ||
        shores == NULL || lakes == NULL) {
        free(heights);
        free(surfaces);
        free(moistures);
        free(forests);
        free(shores);
        free(lakes);
        return false;
    }
    /* The plains' level: the levels below are underground. */
    const int surface_level =
        params->flat ? 0 : clampi(params->underground_levels, 2, WORLDGEN_MAX_UNDERGROUND);
    /* The highest elevation of the map decides how steep the hills are. */
    float top_elevation = 0.0f;
    for (int y = 0; y < params->height; y++) {
        for (int x = 0; x < params->width; x++) {
            float elevation = stretch(noise_fbm2d(elevation_seed, (float)x * frequency,
                                                  (float)y * frequency, 5, 2.0f, 0.5f));
            top_elevation = elevation > top_elevation ? elevation : top_elevation;
        }
    }
    const float step = level_step(params, top_elevation);
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
            Lake lake = pick_lake(params, elevation);
            int height =
                clampi(surface_level + ground_height(params, step, elevation, surface, lake), 0,
                       MAP_MAX_LEVELS - 1);
            heights[column] = (uint8_t)height;
            surfaces[column] = (uint8_t)surface;
            lakes[column] = (uint8_t)lake;
            moistures[column] = moisture;
            highest = height > highest ? height : highest;
        }
    }
    water_distances(lakes, params->width, params->height, shores);
    /* Open levels over the highest ground: room for the tallest trees, and
     * for roofs everywhere. A world with relief has WORLDGEN_LEVELS_ABOVE levels
     * over the plains whatever its hills; a flat one only what it needs. */
    int depth =
        params->flat ? highest + TREE_MAX_LEVELS : surface_level + 1 + WORLDGEN_LEVELS_ABOVE;
    depth = depth < MAP_MAX_LEVELS ? depth : MAP_MAX_LEVELS;
    if (!map_init(map, params->width, params->height, depth)) {
        free(heights);
        free(surfaces);
        free(moistures);
        free(forests);
        free(shores);
        free(lakes);
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
            float forest = stretch(noise_fbm2d(forest_seed, nx * 2.0f, ny * 2.0f, 3, 2.0f, 0.5f));
            forests[column] = forest;
            if (lakes[column] != LAKE_NONE) {
                fill_lake(map, params, index, (Lake)lakes[column]);
                /* Nothing grows nor lies on the bed (shore plants come later); the
                 * draws of a site are made all the same, so that the plants of
                 * the land do not depend on where the water is. */
                rng_next_float(&rng);
                rng_next_float(&rng);
                continue;
            }
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
            float glade = stretch(noise_fbm2d(glade_seed, nx * 6.0f, ny * 6.0f, 2, 2.0f, 0.5f));
            place_woody(map, index, &rng, params, moistures[column],
                        expected_moisture(moistures[column], shores[column]), forest, glade);
        }
    }
    if (!params->flat) {
        place_slopes(map, heights, surfaces, lakes);
    }
    free(heights);
    free(surfaces);
    free(lakes);
    Rng tree_rng; /* A stream of its own: the other plants do not depend on the trees. */
    rng_seed(&tree_rng, params->seed, 0x7472656573ULL /* "trees" */);
    place_tree_volumes(map, &tree_rng);
    Rng litter_rng; /* A stream of its own: nothing else depends on the litter. */
    rng_seed(&litter_rng, params->seed, 0x6c697474657200ULL /* "litter" */);
    place_litter(map, &litter_rng);
    /* The crowns are up: the light of every tile, and over the year on the
     * ground (the low plants do not stop it), then what grows there follows it. */
    size_t *grounds = malloc(columns * sizeof(*grounds));
    SeasonLight season;
    bool lit = grounds != NULL && tile_light_init(light, map, GAME_START_TICK);
    for (int y = 0; lit && y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            grounds[(size_t)y * (size_t)map->width + (size_t)x] =
                map_index(map, x, y, map_ground_level(map, x, y));
        }
    }
    if (!lit || !season_light_init(&season, light, map, grounds, columns)) {
        if (lit) {
            season_light_free(&season);
        }
        free(grounds);
        free(moistures);
        free(forests);
        free(shores);
        tile_light_free(light);
        map_free(map);
        return false;
    }
    Rng understory_rng;
    rng_seed(&understory_rng, params->seed, 0x756e6465727374ULL /* "underst" */);
    place_understory(map, params, moistures, shores, forests, frequency, &season, &understory_rng);
    free(moistures);
    free(forests);
    free(shores);
    Rng shore_rng; /* A stream of its own: the inland plants do not depend on the shores. */
    rng_seed(&shore_rng, params->seed, 0x73686f7265ULL /* "shore" */);
    place_shore_plants(map, &shore_rng, reed_seed, frequency, &season);
    season_light_free(&season);
    free(grounds);
    return true;
}
