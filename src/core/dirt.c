#include "core/dirt.h"

#include "core/util.h"

#include <stdlib.h>
#include <string.h>

void dirt_copy_tile(Dirt *dst, const Dirt *src, size_t index)
{
    dst->dirty_count += (src->level[index] != 0) - (dst->level[index] != 0);
    dst->level[index] = src->level[index];
}

bool dirt_init(Dirt *dirt, int width, int height, int depth)
{
    memset(dirt, 0, sizeof(*dirt));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    dirt->level = calloc((size_t)width * (size_t)height * (size_t)depth, sizeof(*dirt->level));
    if (dirt->level == NULL) {
        return false;
    }
    dirt->width = width;
    dirt->height = height;
    dirt->depth = depth;
    return true;
}

void dirt_free(Dirt *dirt)
{
    free(dirt->level);
    memset(dirt, 0, sizeof(*dirt));
}

static bool in_bounds(const Dirt *dirt, int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < dirt->width && y < dirt->height && z < dirt->depth;
}

static size_t index_of(const Dirt *dirt, int x, int y, int z)
{
    return ((size_t)z * (size_t)dirt->height + (size_t)y) * (size_t)dirt->width + (size_t)x;
}

int dirt_at(const Dirt *dirt, int x, int y, int z)
{
    return in_bounds(dirt, x, y, z) ? dirt->level[index_of(dirt, x, y, z)] : 0;
}

bool dirt_can_lie(const Map *map, int x, int y, int z)
{
    return map_is_passable(map, x, y, z) &&
           map->terrain[map_index(map, x, y, z)] != TERRAIN_SHALLOW_WATER;
}

int dirt_add(Dirt *dirt, const Map *map, int x, int y, int z, int amount)
{
    if (amount <= 0 || !in_bounds(dirt, x, y, z) || !dirt_can_lie(map, x, y, z)) {
        return 0;
    }
    uint8_t *level = &dirt->level[index_of(dirt, x, y, z)];
    int added = clampi(amount, 0, DIRT_MAX - *level);
    if (added > 0) {
        if (*level == 0) {
            dirt->dirty_count++;
        }
        *level = (uint8_t)(*level + added);
        dirt->revision++;
    }
    return added;
}

void dirt_spread(Dirt *dirt, const Map *map, int x, int y, int z, int amount)
{
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            dirt_add(dirt, map, x + dx, y + dy, z, dx == 0 && dy == 0 ? amount : amount / 2);
        }
    }
}

int dirt_clean(Dirt *dirt, int x, int y, int z, int amount)
{
    if (!in_bounds(dirt, x, y, z)) {
        return 0;
    }
    uint8_t *level = &dirt->level[index_of(dirt, x, y, z)];
    if (*level > 0 && amount > 0) {
        *level = (uint8_t)clampi(*level - amount, 0, DIRT_MAX);
        if (*level == 0) {
            dirt->dirty_count--;
        }
    }
    return *level;
}

uint64_t dirt_hash(const Dirt *dirt, uint64_t hash)
{
    return hash_fnv1a64(hash, dirt->level,
                        (size_t)dirt->width * (size_t)dirt->height * (size_t)dirt->depth);
}
