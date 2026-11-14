#include "core/dirt.h"

#include "core/util.h"

#include <stdlib.h>
#include <string.h>

bool dirt_init(Dirt *dirt, int width, int height)
{
    memset(dirt, 0, sizeof(*dirt));
    if (width <= 0 || height <= 0) {
        return false;
    }
    dirt->level = calloc((size_t)width * (size_t)height, sizeof(*dirt->level));
    if (dirt->level == NULL) {
        return false;
    }
    dirt->width = width;
    dirt->height = height;
    return true;
}

void dirt_free(Dirt *dirt)
{
    free(dirt->level);
    memset(dirt, 0, sizeof(*dirt));
}

static bool in_bounds(const Dirt *dirt, int x, int y)
{
    return x >= 0 && y >= 0 && x < dirt->width && y < dirt->height;
}

static size_t index_of(const Dirt *dirt, int x, int y)
{
    return (size_t)y * (size_t)dirt->width + (size_t)x;
}

int dirt_at(const Dirt *dirt, int x, int y)
{
    return in_bounds(dirt, x, y) ? dirt->level[index_of(dirt, x, y)] : 0;
}

bool dirt_can_lie(const Map *map, int x, int y)
{
    return map_is_passable(map, x, y) &&
           map->terrain[map_index(map, x, y)] != TERRAIN_SHALLOW_WATER;
}

int dirt_add(Dirt *dirt, const Map *map, int x, int y, int amount)
{
    if (amount <= 0 || !in_bounds(dirt, x, y) || !dirt_can_lie(map, x, y)) {
        return 0;
    }
    uint8_t *level = &dirt->level[index_of(dirt, x, y)];
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

void dirt_spread(Dirt *dirt, const Map *map, int x, int y, int amount)
{
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            dirt_add(dirt, map, x + dx, y + dy, dx == 0 && dy == 0 ? amount : amount / 2);
        }
    }
}

int dirt_clean(Dirt *dirt, int x, int y, int amount)
{
    if (!in_bounds(dirt, x, y)) {
        return 0;
    }
    uint8_t *level = &dirt->level[index_of(dirt, x, y)];
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
    return hash_fnv1a64(hash, dirt->level, (size_t)dirt->width * (size_t)dirt->height);
}
