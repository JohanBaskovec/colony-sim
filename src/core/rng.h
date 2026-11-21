/*
 * Deterministic pseudo-random number generator (PCG32, O'Neill 2014).
 *
 * The simulation must never use rand(): every random decision goes through an
 * explicitly seeded Rng so that a given seed always produces the same world
 * and the same sequence of events (replays, saves, bug reports, tests).
 */
#ifndef COLONY_CORE_RNG_H
#define COLONY_CORE_RNG_H

#include "core/serialize.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct Rng {
    uint64_t state;
    uint64_t increment; /* Selects the stream; always odd. */
} Rng;

/* Seeds the generator. Different `stream` values give independent sequences. */
void rng_seed(Rng *rng, uint64_t seed, uint64_t stream);

uint32_t rng_next_u32(Rng *rng);

/* Uniform integer in [0, bound). Returns 0 when bound is 0. Unbiased. */
uint32_t rng_range_u32(Rng *rng, uint32_t bound);

/* Uniform integer in [min, max] (both inclusive). */
int32_t rng_range_i32(Rng *rng, int32_t min, int32_t max);

/* Uniform float in [0, 1). */
float rng_next_float(Rng *rng);

/* Returns true with the given probability (clamped to [0, 1]). */
bool rng_chance(Rng *rng, float probability);

/* Saves or loads (docs/SAVE.md) the state of a stream. */
void rng_serialize(Serializer *s, Rng *rng);

#endif /* COLONY_CORE_RNG_H */
