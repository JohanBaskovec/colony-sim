/*
 * Procedural map generation. Fully deterministic: the same parameters always
 * produce the same map (see map_hash()). The relief decides the map's depth:
 * plains on Map.surface_level, with `underground_levels` below; lakes and
 * rivers in hollows (shallows and beaches one level down, deep water two);
 * hills rising level by level with natural slopes, rock outcrops one level
 * higher behind cliffs. The inside of the ground is earth near the surface
 * and rock deeper down (rock all the way under stony ground).
 */
#ifndef COLONY_CORE_WORLDGEN_H
#define COLONY_CORE_WORLDGEN_H

#include "core/map.h"

#include <stdbool.h>
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
    float water_level; /* Elevation (0..1) below which tiles are water. */
    float mountain_level; /* Elevation (0..1) above which the ground is rock outcrops. */
    float hill_level; /* Elevation (0..1) above which the ground rises (hills). */
    float level_step; /* Elevation for each level the ground rises. */
    int underground_levels; /* Levels under the plains (2 to 16). */
    /* No relief: a single level, rock outcrops as rock walls on it (tests and
     * benchmarks that need flat ground). */
    bool flat;
    float tree_density; /* 0 (no trees) .. 1 (dense forests). */
    float feature_scale; /* Typical size of terrain features, in tiles. */
} WorldGenParams;

void worldgen_default_params(WorldGenParams *params);

/* Initializes `map` (it must not hold allocated memory) and fills it. */
bool worldgen_generate(Map *map, const WorldGenParams *params);

#endif /* COLONY_CORE_WORLDGEN_H */
