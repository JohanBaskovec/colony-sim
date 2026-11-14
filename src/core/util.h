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

/* Incremental 64-bit FNV-1a: feed the previous result (or HASH_FNV1A64_INIT) as `hash`. */
uint64_t hash_fnv1a64(uint64_t hash, const void *data, size_t size);

/* 32-bit FNV-1a of a NUL-terminated string. */
uint32_t hash_fnv1a32_str(const char *text);

#endif /* COLONY_CORE_UTIL_H */
