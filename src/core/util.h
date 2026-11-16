/*
 * Small general-purpose helpers shared by all modules.
 */
#ifndef COLONY_CORE_UTIL_H
#define COLONY_CORE_UTIL_H

#include <stddef.h>
#include <stdint.h>

#define COLONY_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

/* Initial value for hash_fnv1a64(). */
#define HASH_FNV1A64_INIT 0xcbf29ce484222325ULL

static inline float clampf(float value, float min, float max)
{
    return value < min ? min : (value > max ? max : value);
}

static inline int clampi(int value, int min, int max)
{
    return value < min ? min : (value > max ? max : value);
}

static inline float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

/* Whether any of `layers` (tile layers of the same size, the list ended by
 * NULL; `layers` itself may be NULL) is non-zero at `index`. */
static inline int layers_any_set(const uint8_t *const *layers, size_t index)
{
    for (; layers != NULL && *layers != NULL; layers++) {
        if ((*layers)[index] != 0) {
            return 1;
        }
    }
    return 0;
}

/* Row `step` of a scan of rows 0..height-1 that starts at row `center` and
 * moves outwards (center, center + 1, center - 1, center + 2...), or -1 for
 * a step out of range (skip it). Steps go from 0 to 2 × height - 1. */
static inline int row_outwards(int center, int step, int height)
{
    int row = (step & 1) ? center + (step + 1) / 2 : center - step / 2;
    return row >= 0 && row < height ? row : -1;
}

/* Index of the lowest set bit of a non-zero value. */
static inline int lowest_bit64(uint64_t value)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(value);
#else
    int bit = 0;
    while ((value & 1u) == 0) {
        value >>= 1;
        bit++;
    }
    return bit;
#endif
}

/* Incremental 64-bit FNV-1a: feed the previous result (or HASH_FNV1A64_INIT) as `hash`. */
uint64_t hash_fnv1a64(uint64_t hash, const void *data, size_t size);

/* 32-bit FNV-1a of a NUL-terminated string. */
uint32_t hash_fnv1a32_str(const char *text);

#endif /* COLONY_CORE_UTIL_H */
