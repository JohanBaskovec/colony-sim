#include "core/util.h"

uint64_t hash_fnv1a64(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

uint32_t hash_fnv1a32_str(const char *text)
{
    uint32_t hash = 0x811c9dc5u;
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        hash ^= *p;
        hash *= 0x01000193u;
    }
    return hash;
}
