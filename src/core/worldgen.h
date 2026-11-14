/*
 * Procedural map generation. Fully deterministic: the same parameters always
 * produce the same map (see map_hash()).
 */
#ifndef COLONY_CORE_WORLDGEN_H
#define COLONY_CORE_WORLDGEN_H

#include "core/map.h"

#include <stdint.h>

#define WORLDGEN_DEFAULT_SIZE 200

typedef struct WorldGenParams {
    uint64_t seed;
    int width;
    int height;
    float water_level; /* Elevation (0..1) below which tiles are water. */
    float mountain_level; /* Elevation (0..1) above which tiles are solid rock. */
    float tree_density; /* 0 (no trees) .. 1 (dense forests). */
    float feature_scale; /* Typical size of terrain features, in tiles. */
} WorldGenParams;

void worldgen_default_params(WorldGenParams *params);

/* Initializes `map` (it must not hold allocated memory) and fills it. */
bool worldgen_generate(Map *map, const WorldGenParams *params);

#endif /* COLONY_CORE_WORLDGEN_H */
