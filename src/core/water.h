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
 * Deterministic: blocks are worked out in the order of their numbers. The
 * total of water only changes through the map's edges (none yet).
 */
#ifndef COLONY_CORE_WATER_H
#define COLONY_CORE_WATER_H

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

/* The state of an active block. */
typedef struct WaterBlock {
    /* 1/WATER_FINE mm a step from each tile to the one east of it, and to the
     * one south of it. */
    int32_t east[WATER_BLOCK_TILES];
    int32_t south[WATER_BLOCK_TILES];
    /* What the map was last told of each tile: passable, open water. */
    uint64_t passable[WATER_BLOCK_TILES / 64];
    uint64_t open_water[WATER_BLOCK_TILES / 64];
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
 * map's logs as they are. */
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
 * it: every block falls asleep (the map told of what changed in them) and
 * what the map logged so far wakes nothing. For tests and scenarios that lay
 * water by hand (a pond on a plain without banks would spread otherwise). */
void water_settle(Water *water, Map *map);

/* One step of the flow (a tick). False when out of memory (the step is then
 * not taken whole). */
bool water_step(Water *water, Map *map);

/* The total of the free water on the map, 1/WATER_FINE mm (tests: it is
 * kept). */
uint64_t water_total(const Water *water, const Map *map);

uint64_t water_hash(const Water *water, uint64_t hash);

#endif /* COLONY_CORE_WATER_H */
