/*
 * Procedural map generation. Fully deterministic: the same parameters always
 * produce the same map (see map_hash()). The relief decides the map's depth:
 * plains on Map.surface_level, with `underground_levels` below; lakes and
 * rivers in hollows (shallows and beaches one level down, deep water two);
 * hills rising level by level with natural slopes, rock outcrops one level
 * higher behind cliffs. The inside of the ground is earth near the surface
 * and rock deeper down (rock all the way under stony ground). Open levels
 * top the highest ground, where trees rise and roofs go (TREE_MAX_LEVELS - 1
 * of them: a flat world has TREE_MAX_LEVELS levels). Trees stand apart (no
 * two on touching tiles), a few of the grown ones old or ancient (wider
 * trunks), and are written into the map (core/tree.h).
 *
 * The plants under the trees are set as if years had gone by: by the real
 * light of each tile (core/tile_light.h: the sky it sees, its sun, the
 * shade of the relief and the crowns), and only where a plant gets enough
 * of it all year round: on the darkest stretch of its awake days
 * (worldgen_plant_dark_day()), under the skies of that season, not only on
 * the first day of the game, in the summer. Nothing that needs light grows
 * where it would starve in the years to come: at the bottom of a deep pit,
 * in the deep shade of a wood.
 */
#ifndef COLONY_CORE_WORLDGEN_H
#define COLONY_CORE_WORLDGEN_H

#include "core/map.h"
#include "core/tile_light.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WORLDGEN_DEFAULT_SIZE 200
/* Levels under the plains (Map.surface_level): the hollows of the water,
 * then solid ground. */
#define WORLDGEN_UNDERGROUND_LEVELS 4
/* Under the ground's surface, earth for this many levels, then rock (under
 * soft ground; stony ground is rock all the way down). */
#define WORLDGEN_EARTH_LEVELS 2

typedef struct WorldGenParams {
    uint64_t seed;
    int width;
    int height;
    float water_level;      /* Elevation (0..1) below which tiles are water. */
    float mountain_level;   /* Elevation (0..1) above which the ground is rock outcrops. */
    float hill_level;       /* Elevation (0..1) above which the ground rises (hills). */
    float level_step;       /* Elevation for each level the ground rises. */
    int underground_levels; /* Levels under the plains (2 to 16). */
    /* No relief: a single level, rock outcrops as rock walls on it (tests and
     * benchmarks that need flat ground). */
    bool flat;
    float tree_density;  /* 0 (no trees) .. 1 (dense forests). */
    float feature_scale; /* Typical size of terrain features, in tiles. */
} WorldGenParams;

void worldgen_default_params(WorldGenParams *params);

/* Initializes `map` (it must not hold allocated memory) and fills it. */
bool worldgen_generate(Map *map, const WorldGenParams *params);

/* The same, leaving `light` (it must not hold allocated memory) worked out
 * for the map made, at GAME_START_TICK (tile_light_init()): game_init()
 * keeps it. On failure, neither holds memory. */
bool worldgen_generate_lit(Map *map, const WorldGenParams *params, TileLight *light);

/*
 * The day of the year a plant of `type` is judged by for its light. Its
 * awake days are those its climate (Plant.climate, the running mean of the
 * seasons' temperatures, a day behind them near the ground) keeps it awake,
 * as advance_hour() rests and wakes it. Its last `dark_days` awake days
 * under the leaves (the crowns are counted in leaf from midsummer until the
 * cold: a warm autumn keeps them on) are the darkest stretch of its year: the
 * sun sinks, and what it bears then is mended in the spring, under bare
 * crowns. It starves if all of them are too dark: the day is the brightest
 * of them (the highest noon sun). -1 for a plant that needs no light
 * (dark_days 0), a tree (its light is at the top of its crown), or one never
 * awake under the leaves.
 */
int worldgen_plant_dark_day(PlantType type);

/* How many plants of the map (trees left out) would starve in the dark
 * where they stand: on their dark day, the share of the hours their best
 * light (game_light_peak_at(): the sky and the sun on a clear day, the sky
 * dimmed by the clouds otherwise) falls short of their need, with the skies
 * of the season (weather_season_shares()), is more than they mend from
 * (plant_dark_share_borne()). `light` worked out for the map, the crowns in
 * leaf. SIZE_MAX when memory ran out. */
size_t worldgen_starving_plants(const Map *map, TileLight *light);

#endif /* COLONY_CORE_WORLDGEN_H */
