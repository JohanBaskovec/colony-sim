/*
 * Surface flow of water (docs/WATER.md §3): the free water of Map.water
 * falling between levels and flowing between neighbouring tiles of a level,
 * in "virtual pipes" worked out with integers.
 *
 * Only active blocks (WATER_BLOCK_SIZE × WATER_BLOCK_SIZE tiles of one level)
 * are worked out: a block wakes when water is added or taken around it from
 * outside the flow (Map.water_log), when the map changes around water
 * (Map.topology_log, Map.build_log: dug, built, a door opened or shut), or
 * when an active neighbour sends it water, and falls asleep once nothing has
 * moved in it (more than ripples) for WATER_SLEEP_STEPS steps. A lake at rest costs nothing.
 *
 * A step:
 * - Falling: the water of an open tile without a floor goes into the tile
 *   below as far as it has room (a pit fills from the bottom up, water pours
 *   off a cliff).
 * - Flowing: each pipe between two tiles of a level (east and south of each
 *   tile; none across rock, a wall or a closed door) keeps part of its flow
 *   and gains in proportion to the difference of their surfaces; what would
 *   leave a tile is scaled down to what it holds; all flows are worked out
 *   first, then applied, so that the order of the tiles does not matter.
 *   Water less than WATER_FILM_MM deep does not leave its tile. A tile full
 *   past WATER_CELL_FULL passes the rest to the open tile above it.
 * - Classes: the changes of where one can walk and where open water is are
 *   told to the map (map_water_class_changed()) at most once every
 *   WATER_CLASS_MIN_TICKS steps, so that a wave at the edge of deep water
 *   does not change the paths every tick; the walking cost of a tile follows
 *   its depth at once.
 *
 * The edges (docs/WATER.md §4.1): the world goes on beyond the map. Beyond
 * each tile of the edge lies a tile of the land outside, as it was when the
 * flow began (water_init(), water_settle()): solid under the ground of the
 * edge's column then, and above it water up to that column's surface then
 * (none where it was dry). The edge tile and the one beyond share a pipe like
 * any other, for a film's difference at least; the one beyond is the edge of
 * a basin of WATER_EDGE_BASIN tiles, its level hardly moved by what crosses:
 * a lake cut by the edge keeps its level, a flood reaching a dry edge flows
 * out of the map. The land beyond lives the same weather as the edge (rain,
 * the sun: water_exchange()). Sealed edges (water_seal_edges()) let nothing
 * through.
 *
 * Deterministic: blocks are worked out in the order of their numbers. The
 * total of water only changes through the map's edges (Water.edge_in).
 */
#ifndef COLONY_CORE_WATER_H
#define COLONY_CORE_WATER_H

#include "core/serialize.h"

#include "core/map.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WATER_BLOCK_SIZE 16
#define WATER_BLOCK_TILES (WATER_BLOCK_SIZE * WATER_BLOCK_SIZE)
/* Water this shallow (mm) does not flow: it soaks in or dries where it is. */
#define WATER_FILM_MM 3
/* Steps without anything moving in a block before it falls asleep. */
#define WATER_SLEEP_STEPS 60
/* Steps between two reports of the class changes to the map. */
#define WATER_CLASS_MIN_TICKS 60
/* The flow of a pipe each step: it keeps WATER_FLOW_KEEP / 16 of itself and
 * gains the difference of the surfaces divided by WATER_FLOW_DIVISOR
 * (tuning). */
#define WATER_FLOW_KEEP 15
#define WATER_FLOW_DIVISOR 4
/* The flow works in 1/WATER_FINE mm: in whole millimetres, water would stop
 * on a slope of a few millimetres a tile (the difference too small to move
 * any), and a long channel would end well below its lake. The fraction of
 * each tile is kept here, the millimetres in Map.water. */
#define WATER_FINE 256
/* Flows this small (1/WATER_FINE mm a step) are the last ripples of water
 * settling: a block where none is larger falls asleep all the same. */
#define WATER_QUIET_FLOW 16
/* Tiles of the land beyond the edge behind each edge tile: a basin that the
 * water flowing out fills, and the water flowing in drains (docs/WATER.md
 * §4.1). Wide enough to keep a lake cut by the edge at its level, and finite,
 * so that two basins a little apart (the sun takes more here than there)
 * even out instead of keeping a current running through the map. */
#define WATER_EDGE_BASIN 256

/* The state of an active block. */
typedef struct WaterBlock {
    /* 1/WATER_FINE mm a step from each tile to the one east of it, and to the
     * one south of it. */
    int32_t east[WATER_BLOCK_TILES];
    int32_t south[WATER_BLOCK_TILES];
    /* What the map was last told of each tile: passable, open water. */
    uint64_t passable[WATER_BLOCK_TILES / 64];
    uint64_t open_water[WATER_BLOCK_TILES / 64];
    /* On the west and north edges of the map: 1/WATER_FINE mm a step from the
     * tile beyond the edge into each tile of the block's west column, and of
     * its north row (the east and south edges use `east` and `south`). */
    int32_t west_edge[WATER_BLOCK_SIZE];
    int32_t north_edge[WATER_BLOCK_SIZE];
    uint32_t number; /* The block's number (water_block_of()). */
    uint16_t quiet;  /* Steps in a row without anything moving. */
    bool moved;      /* Something moved in it this step. */
} WaterBlock;

typedef struct Water {
    int width, height, depth;
    int blocks_x, blocks_y;
    uint32_t block_count;
    /* Per block: 1 + its entry in `pool` while active, 0 asleep. */
    uint32_t *slot;
    WaterBlock *pool;
    uint32_t pool_capacity;
    uint32_t pool_used;
    uint32_t *free_entries; /* Entries of the pool given back. */
    uint32_t free_count;
    /* The numbers of the active blocks, ascending, and those woken during
     * the step (merged at its end). */
    uint32_t *active;
    uint32_t active_count;
    uint32_t *woken;
    uint32_t woken_count;
    uint32_t list_capacity;
    /* Tiles that went past full this step (indices into the map). */
    size_t *overfull;
    uint32_t overfull_count, overfull_capacity;
    /* Per tile of the map: its water past its millimetres, 1/WATER_FINE mm. */
    uint8_t *fine;
    bool fine_used; /* Some fraction was ever set (hashed from then on). */
    /* The land beyond the edges, per column of the edge (north row, south
     * row, west column, east column: 2 × width + 2 × height): the level of its
     * ground (solid under it) and the surface of its water, z × WATER_CELL_FULL
     * + water, in 1/(WATER_FINE × WATER_EDGE_BASIN) mm (the ground's level ×
     * full when dry): the flow through the edge moves it by 1/WATER_EDGE_BASIN
     * of what it moves on the edge tile, and nothing is lost to rounding. */
    uint8_t *edge_ground;
    int64_t *edge_surface;
    /* Water that came in through the edges so far, less what went out, in
     * 1/WATER_FINE mm. */
    int64_t edge_in;
    bool sealed; /* No water crosses the edges (water_seal_edges()). */
    /* The map's logs read so far. */
    uint32_t topology_seen, build_seen, water_seen;
    uint64_t steps;
    /* Statistics, not in the hash: blocks woken, the times every block with
     * water was woken (a log overflowed), water moved last step (mm). */
    uint64_t wakes;
    uint64_t wake_alls;
    uint64_t moved_last_step;
} Water;

/* Starts with every block asleep (the water at rest as laid), having read the
 * map's logs as they are; the land beyond the edges as the map is now. */
bool water_init(Water *water, const Map *map);
void water_free(Water *water);

/* The number of the block holding a tile. */
uint32_t water_block_of(const Water *water, int x, int y, int z);

/* Whether the block holding a tile is active. */
bool water_block_active(const Water *water, int x, int y, int z);

/* Wakes the block holding a tile (nothing when out of bounds). False when out
 * of memory. */
bool water_wake(Water *water, const Map *map, int x, int y, int z);

/* Wakes every block holding some water (after the map was changed in ways
 * its logs no longer tell). */
bool water_wake_all(Water *water, const Map *map);

/* Takes the water as it lies now for water at rest, as world generation lays
 * it: every block falls asleep (the map told of what changed in them), what
 * the map logged so far wakes nothing, and the land beyond the edges is taken
 * again from the map as it is. For tests and scenarios that lay water by hand
 * (a pond on a plain without banks would spread otherwise). */
void water_settle(Water *water, Map *map);

/* Seals the map's edges: the land beyond is solid all the way up, now and
 * after water_settle(). For the tests' flat world (game_init()), whose lakes
 * lie on flat ground: fed from beyond, they would flood it. */
void water_seal_edges(Water *water);

/* The tile holding the surface of the water standing on a floor (an index
 * in bounds): the floor's tile, or the open tile above a full one, up the
 * column. Rain falls there, and water evaporates from there. */
size_t water_surface_index(const Map *map, size_t index);

/* Adds water to a tile (an index in bounds; takes it when `fine` is
 * negative, never more than there is) from outside the flow: rain, soaking
 * into the ground, evaporation (docs/WATER.md §3.5), in 1/WATER_FINE mm.
 * Nothing on rock. A sleeping block wakes only when the tile's water would
 * then move (flow to or from a neighbour, or fall): rain falling alike on a
 * lake at rest or on flat ground, or a puddle drying, moves nothing. The map is told at once of a
 * change of class in a sleeping block (an active one tells it as the flow does). Returns the change
 * made. False never: a block that cannot wake (out of memory) wakes at the next change. */
int32_t water_exchange(Water *water, Map *map, size_t index, int32_t fine);

/* The water of a tile in mm, with its fraction. */
float water_mm_at(const Water *water, const Map *map, size_t index);

/* One step of the flow (a tick). False when out of memory (the step is then
 * not taken whole). */
bool water_step(Water *water, Map *map);

/* The water beyond the edge next to the edge tile (x, y, z), on the side
 * (dx, dy) (one step out of the map), in 1/WATER_FINE mm; -1 where the land
 * beyond is solid, or the tile is not on that edge. */
int32_t water_beyond_edge(const Water *water, int x, int y, int z, int dx, int dy);

/* The total of the free water on the map, 1/WATER_FINE mm (tests: it only
 * changes by Water.edge_in). */
uint64_t water_total(const Water *water, const Map *map);

uint64_t water_hash(const Water *water, uint64_t hash);

/* Saves or loads (docs/SAVE.md) the flow: the fractions of millimetres, the
 * active blocks and their pipes, the blocks woken, the land beyond the edges,
 * the logs read; when reading, into a flow initialized for the map saved. */
void water_serialize(Serializer *s, Water *water);

#endif /* COLONY_CORE_WATER_H */
