/*
 * Dirt: how dirty each tile is, from 0 (clean) to DIRT_MAX percent.
 *
 * Some work makes tiles dirty (knapping chips, wood shavings: see
 * RecipeDef.dirt), and settlers clean them (the cleaning work type, the last
 * column of the priorities grid). Dirt will later lower the beauty of a place
 * and help diseases spread. It only lies on open ground: not on water, rock
 * walls or built walls.
 */
#ifndef COLONY_CORE_DIRT_H
#define COLONY_CORE_DIRT_H

#include "core/map.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIRT_MAX 100

typedef struct Dirt {
    int width;
    int height;
    int depth;
    uint8_t *level;  /* 0..DIRT_MAX per tile. */
    int dirty_count; /* Tiles with some dirt. */
    /* Incremented whenever a tile gets dirtier: settlers who found nothing to
     * clean look again. */
    uint32_t revision;
} Dirt;

bool dirt_init(Dirt *dirt, int width, int height, int depth);
void dirt_free(Dirt *dirt);

/* Dirt on a tile, 0 out of bounds. */
int dirt_at(const Dirt *dirt, int x, int y, int z);

/* Whether dirt may lie on a tile: open ground, not water. */
/* Copies the dirt of the tile at `index` from `src` to `dst` (same size), keeping
 * dst's count right: dst is a picture of src (see core/known.h). */
void dirt_copy_tile(Dirt *dst, const Dirt *src, size_t index);

bool dirt_can_lie(const Map *map, int x, int y, int z);

/* Makes a tile dirtier (capped at DIRT_MAX) where dirt_can_lie() allows.
 * Returns the dirt added. */
int dirt_add(Dirt *dirt, const Map *map, int x, int y, int z, int amount);

/* Dirt from work on a tile: `amount` on it, half on each of its 8 neighbours. */
void dirt_spread(Dirt *dirt, const Map *map, int x, int y, int z, int amount);

/* Removes up to `amount` of dirt from a tile; returns what is left. */
int dirt_clean(Dirt *dirt, int x, int y, int z, int amount);

uint64_t dirt_hash(const Dirt *dirt, uint64_t hash);

#endif /* COLONY_CORE_DIRT_H */
