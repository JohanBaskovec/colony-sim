/*
 * The course of a river across the map (docs/WATER.md §4.2): it enters by
 * one edge and leaves by another, at given points along them, and meanders
 * between them. These are the parameters the world map will set for the
 * map of its cell one day (docs/WORLD.md §4.1, step G0c); until then the
 * edges and the points come from the seed, the width from the New game
 * window. World generation (core/worldgen.h) lays its channel and valley
 * from the distance of each column to the middle of its course.
 */
#ifndef COLONY_CORE_RIVER_H
#define COLONY_CORE_RIVER_H

#include <stdbool.h>
#include <stdint.h>

/* The edges of the map. */
typedef enum MapSide {
    MAP_SIDE_NORTH, /* y = 0 */
    MAP_SIDE_EAST,  /* x = width - 1 */
    MAP_SIDE_SOUTH, /* y = height - 1 */
    MAP_SIDE_WEST,  /* x = 0 */
    MAP_SIDE_COUNT  /* RiverParams: chosen from the seed. */
} MapSide;

/* Widths of a river's channel, in tiles: a stream is all shallows, a wider
 * river deep in its middle. */
#define RIVER_STREAM_WIDTH 3
#define RIVER_MAX_WIDTH 12
/* Tiles across each level that the walls of its valley rise. */
#define RIVER_VALLEY_TILES 3
/* How much higher than the lakes the river stands beyond the edge it comes
 * in by (mm): its slope down to the outlet, held at the lakes' level beyond
 * the edge it leaves by, makes it flow (docs/WATER.md §4.2). */
#define RIVER_HEAD_MM 100

typedef struct RiverParams {
    int width;            /* Tiles across its channel; 0: no river. */
    MapSide from, to;     /* The edge it comes in by and leaves by (MAP_SIDE_COUNT: the seed's). */
    float from_at, to_at; /* Where along them, 0 .. 1 (west to east, north to south). */
} RiverParams;

/* No river, its course from the seed should it get a width. */
void river_default_params(RiverParams *river);

/* The course to lay on a map of `width` × `height` from the seed `seed`: the
 * edges left to the seed chosen (another edge than the one it comes in by:
 * the opposite one more often than not), the points brought within the
 * edges' middle part. The width is kept as it is (within 0 ..
 * RIVER_MAX_WIDTH). */
void river_resolve(const RiverParams *river, uint64_t seed, RiverParams *out);

/*
 * The distance of each column of a map of `width` × `height` from the middle
 * of the river's course (`river` resolved), in tiles, written in `out`
 * (width × height, row by row): exact near the course, approximate further
 * out (for the valley). The course meanders from `seed`, enters and leaves
 * square to its edges at its points, and never strays out of the map. False
 * when there is no river to lay (`out` then untouched).
 */
bool river_distances(const RiverParams *river, uint64_t seed, int width, int height, float *out);

#endif /* COLONY_CORE_RIVER_H */
