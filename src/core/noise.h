/*
 * Deterministic 2D value noise, used by world generation.
 */
#ifndef COLONY_CORE_NOISE_H
#define COLONY_CORE_NOISE_H

#include <stdint.h>

/* Smooth value noise in [0, 1]. Same (seed, x, y) always gives the same value. */
float noise_value2d(uint32_t seed, float x, float y);

/*
 * Fractal Brownian motion: sum of `octaves` layers of value noise, each one
 * `lacunarity` times finer and `gain` times weaker than the previous one.
 * Result is normalized to [0, 1].
 */
float noise_fbm2d(uint32_t seed, float x, float y, int octaves, float lacunarity, float gain);

#endif /* COLONY_CORE_NOISE_H */
