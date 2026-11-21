#include "core/rng.h"

void rng_seed(Rng *rng, uint64_t seed, uint64_t stream)
{
    rng->state = 0u;
    rng->increment = (stream << 1u) | 1u;
    rng_next_u32(rng);
    rng->state += seed;
    rng_next_u32(rng);
}

uint32_t rng_next_u32(Rng *rng)
{
    uint64_t old_state = rng->state;
    rng->state = old_state * 6364136223846793005ULL + rng->increment;
    uint32_t xorshifted = (uint32_t)(((old_state >> 18u) ^ old_state) >> 27u);
    uint32_t rotation = (uint32_t)(old_state >> 59u);
    return (xorshifted >> rotation) | (xorshifted << ((0u - rotation) & 31u));
}

uint32_t rng_range_u32(Rng *rng, uint32_t bound)
{
    if (bound == 0) {
        return 0;
    }
    /* Reject the low values that would make the modulo biased. */
    uint32_t threshold = (0u - bound) % bound;
    for (;;) {
        uint32_t value = rng_next_u32(rng);
        if (value >= threshold) {
            return value % bound;
        }
    }
}

int32_t rng_range_i32(Rng *rng, int32_t min, int32_t max)
{
    if (max <= min) {
        return min;
    }
    uint32_t span = (uint32_t)((int64_t)max - (int64_t)min) + 1u;
    if (span == 0) { /* Full 32-bit range. */
        return (int32_t)rng_next_u32(rng);
    }
    return (int32_t)((int64_t)min + (int64_t)rng_range_u32(rng, span));
}

float rng_next_float(Rng *rng)
{
    /* 24 random bits: every value is exactly representable as a float. */
    return (float)(rng_next_u32(rng) >> 8) * (1.0f / 16777216.0f);
}

bool rng_chance(Rng *rng, float probability)
{
    if (probability <= 0.0f) {
        return false;
    }
    if (probability >= 1.0f) {
        return true;
    }
    return rng_next_float(rng) < probability;
}

void rng_serialize(Serializer *s, Rng *rng)
{
    ser_u64(s, &rng->state);
    ser_u64(s, &rng->increment);
}
