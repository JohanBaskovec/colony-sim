#include "core/noise.h"

#include "core/util.h"

#include <math.h>

static uint32_t hash_lattice(uint32_t seed, int32_t x, int32_t y)
{
    uint32_t h = seed;
    h ^= (uint32_t)x * 0x27d4eb2du;
    h = (h ^ (h >> 15)) * 0x85ebca6bu;
    h ^= (uint32_t)y * 0x165667b1u;
    h = (h ^ (h >> 13)) * 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

static float lattice_value(uint32_t seed, int32_t x, int32_t y)
{
    return (float)(hash_lattice(seed, x, y) >> 8) * (1.0f / 16777215.0f);
}

static float smootherstep(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float noise_value2d(uint32_t seed, float x, float y)
{
    float floor_x = floorf(x);
    float floor_y = floorf(y);
    int32_t ix = (int32_t)floor_x;
    int32_t iy = (int32_t)floor_y;
    float u = smootherstep(x - floor_x);
    float v = smootherstep(y - floor_y);

    float top = lerpf(lattice_value(seed, ix, iy), lattice_value(seed, ix + 1, iy), u);
    float bottom = lerpf(lattice_value(seed, ix, iy + 1), lattice_value(seed, ix + 1, iy + 1), u);
    return lerpf(top, bottom, v);
}

float noise_fbm2d(uint32_t seed, float x, float y, int octaves, float lacunarity, float gain)
{
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float total_amplitude = 0.0f;

    for (int octave = 0; octave < octaves; octave++) {
        uint32_t octave_seed = seed + (uint32_t)octave * 0x9e3779b9u;
        sum += amplitude * noise_value2d(octave_seed, x * frequency, y * frequency);
        total_amplitude += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return total_amplitude > 0.0f ? sum / total_amplitude : 0.0f;
}
