/*
 * The course of a river across the map (docs/WATER.md §4.2): it enters by
 * one edge and leaves by another, at given points along them, and meanders
 * between them. These are the parameters the world map will set for the
 * map of its cell one day (docs/WORLD.md §4.1, step G0c); until then the
 * edges and the points come from the seed, the width from the New game
 * window. World generation (core/worldgen.h) lays its channel and valley
 * from the distance of each column to the middle of its course.
 *
 * Its discharge over time (RiverFlow, W3c-3): how high it stands beyond the
 * edges it comes in and goes out by, after the seasons and the rain over its
 * catchment, out of the map.
 */
#ifndef COLONY_CORE_RIVER_H
#define COLONY_CORE_RIVER_H

#include "core/serialize.h"

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
    /* How much it rises and falls with the seasons and the rain, against a
     * usual river (1; 0: it keeps the lakes' level). The world map will set
     * it from the catchment of its cell (docs/WORLD.md G0c). */
    float discharge;
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

/*
 * The river's stage over time (docs/WATER.md §4.2): how far above the
 * lakes' level it stands beyond its edges, in mm. A seasonal part, high
 * after the snowmelt of spring (RIVER_SEASON_PEAK_DAY), low in early autumn,
 * RIVER_SEASON_MM either side; and the rain over its catchment (the map's
 * rain, the same weather): it fills a quick store (the ground and the
 * brooks, emptying in about RIVER_QUICK_HOURS), which fills a slow one (the
 * river, about RIVER_SLOW_HOURS), RIVER_RAIN_RISE mm of stage for each mm in
 * the slow store. The river so rises for hours after a storm, peaks about a
 * day later, and falls over days; a rainy week raises it by tens of
 * centimetres, a storm's cloudburst by a metre, a wet spell of storms by
 * one to two: a flood over its banks. Both times the RiverParams.discharge.
 */
#define RIVER_SEASON_MM 200.0f
#define RIVER_SEASON_PEAK_DAY 8
#define RIVER_QUICK_HOURS 12.0f
#define RIVER_SLOW_HOURS 60.0f
#define RIVER_RAIN_RISE 15.0f
#define RIVER_STAGE_MIN_MM (-300)
#define RIVER_STAGE_MAX_MM 2500
/* Its ends are set again when the stage moved this far (mm) since they
 * were: each time the water near the edges wakes. */
#define RIVER_STAGE_STEP_MM 20

typedef struct RiverFlow {
    bool flowing;      /* A river with its ends on the edges of the map. */
    float discharge;   /* RiverParams.discharge. */
    int64_t rest;      /* The lakes' level: z × WATER_CELL_FULL + water, 1/WATER_FINE mm. */
    float quick, slow; /* The catchment's stores, mm of rain. */
    int32_t stage_set; /* The stage its ends hold, mm. */
} RiverFlow;

/* An hour of rain (`rain_mm` this hour) over the catchment. */
void river_flow_hour(RiverFlow *flow, float rain_mm);

/* The stage on day `day_of_year` (0 .. DAYS_PER_YEAR - 1) with the stores as
 * they are, mm, within RIVER_STAGE_MIN_MM .. RIVER_STAGE_MAX_MM. */
int32_t river_stage(const RiverFlow *flow, int day_of_year);

/* How much higher it stands beyond the edge it comes in by than beyond the
 * one it leaves by at `stage` (its slope): RIVER_HEAD_MM, more as it rises
 * (twice as much a metre up), less when low. */
int32_t river_head(int32_t stage);

uint64_t river_flow_hash(const RiverFlow *flow, uint64_t hash);

/* Saves or loads (docs/SAVE.md) the river's flow. */
void river_flow_serialize(Serializer *s, RiverFlow *flow);

#endif /* COLONY_CORE_RIVER_H */
