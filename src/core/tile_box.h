/*
 * A box of tiles (inclusive bounds on the three axes) holding every tile of a
 * sparse layer (designations, gathering zones), so that scans can skip the
 * rest of the map. It may hold more than those tiles: it grows as tiles are
 * added but only shrinks, to nothing, when the last one goes, which keeps it
 * cheap to maintain and always safe to scan.
 */
#ifndef COLONY_CORE_TILE_BOX_H
#define COLONY_CORE_TILE_BOX_H

#include <stdbool.h>

typedef struct TileBox {
    int x0, y0, z0; /* Lowest corner. */
    int x1, y1, z1; /* Highest corner, included. x1 < x0 when empty. */
} TileBox;

static inline TileBox tile_box_empty(void)
{
    return (TileBox){0, 0, 0, -1, -1, -1};
}

static inline bool tile_box_is_empty(const TileBox *box)
{
    return box->x1 < box->x0;
}

static inline void tile_box_add(TileBox *box, int x, int y, int z)
{
    if (tile_box_is_empty(box)) {
        *box = (TileBox){x, y, z, x, y, z};
        return;
    }
    box->x0 = x < box->x0 ? x : box->x0;
    box->y0 = y < box->y0 ? y : box->y0;
    box->z0 = z < box->z0 ? z : box->z0;
    box->x1 = x > box->x1 ? x : box->x1;
    box->y1 = y > box->y1 ? y : box->y1;
    box->z1 = z > box->z1 ? z : box->z1;
}

static inline bool tile_box_contains(const TileBox *box, int x, int y, int z)
{
    return x >= box->x0 && x <= box->x1 && y >= box->y0 && y <= box->y1 && z >= box->z0 &&
           z <= box->z1;
}

#endif /* COLONY_CORE_TILE_BOX_H */
