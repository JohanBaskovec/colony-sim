/*
 * Who stands where: settlers and animals walk through one another (slowed
 * down while they cross) but never stop on the same tile.
 *
 * Every being holds a claim on the one tile it stops on: where it stands, or
 * where the path it walks ends. A being only ends its walk on a tile it
 * claimed, and a claim is refused while someone else holds the tile, unless
 * that one is yielding (idle or strolling: it steps aside, and the tile goes
 * to whoever needs it for something). The searches of a being skip the tiles
 * others hold (occupancy_stop_filter(), set on the pathfinder), so that two
 * beings never head for the same spot.
 *
 * Also counts the beings on each tile now, to slow down whoever crosses
 * someone. Beings are numbered: settlers by slot + 1, animals after them.
 */
#ifndef COLONY_CORE_OCCUPANCY_H
#define COLONY_CORE_OCCUPANCY_H

#include "core/map.h"

#include <stdbool.h>
#include <stdint.h>

#define OCCUPANCY_SETTLER_SLOTS 64 /* COLONIST_MAX */
#define OCCUPANCY_ANIMAL_SLOTS 256 /* ANIMAL_MAX */
#define OCCUPANCY_BEINGS (OCCUPANCY_SETTLER_SLOTS + OCCUPANCY_ANIMAL_SLOTS)
/* Speed while stepping onto a tile someone else is on. */
#define OCCUPANCY_CROSSING_SPEED 0.5f
/* How long one waits, a step short of a tile someone else holds, before
 * giving up what it went there for. */
#define OCCUPANCY_PATIENCE_TICKS 90

/* How far, in tiles, a being that falls on one already lying crawls to find
 * a tile of its own. */
#define OCCUPANCY_CRAWL_REACH 2

/* A being: 1..OCCUPANCY_BEINGS, OCCUPANCY_NOBODY for none. */
typedef uint16_t BeingId;
#define OCCUPANCY_NOBODY ((BeingId)0)

static inline BeingId occupancy_settler(uint32_t slot)
{
    return (BeingId)(slot + 1u);
}

static inline BeingId occupancy_animal(uint32_t slot)
{
    return (BeingId)(OCCUPANCY_SETTLER_SLOTS + slot + 1u);
}

typedef struct OccupancyBeing {
    TilePos claim; /* Valid when `claiming`. */
    TilePos at;    /* Valid when `placed`. */
    bool claiming;
    bool placed;
    bool yielding;
    bool forced; /* Its claim was forced: it lies where it fell. */
} OccupancyBeing;

typedef struct Occupancy {
    int width;
    int height;
    int depth;
    BeingId *claims;  /* Per tile: who stops there (OCCUPANCY_NOBODY: free). */
    uint8_t *present; /* Per tile: how many beings are on it now. */
    OccupancyBeing beings[OCCUPANCY_BEINGS + 1]; /* Indexed by BeingId. */
} Occupancy;

bool occupancy_init(Occupancy *occupancy, int width, int height, int depth);
void occupancy_free(Occupancy *occupancy);

/* Where a being is now (counted on that tile, no longer on the one before). */
void occupancy_place(Occupancy *occupancy, BeingId being, TilePos at);

/* A being gone (dead, removed): on no tile, holding none. */
void occupancy_remove(Occupancy *occupancy, BeingId being);

/* Whether a being yields its tile to whoever needs it (idle, strolling). */
void occupancy_set_yielding(Occupancy *occupancy, BeingId being, bool yielding);

/* Who holds a tile (OCCUPANCY_NOBODY: nobody, or out of the map). */
BeingId occupancy_holder(const Occupancy *occupancy, TilePos tile);

/* Whether the being holds the tile. */
bool occupancy_holds(const Occupancy *occupancy, BeingId being, TilePos tile);

/* Whether the being may stop on the tile: nobody holds it, it does, or (when
 * the being is not yielding itself) the holder yields. */
bool occupancy_can_stop(const Occupancy *occupancy, BeingId being, TilePos tile);

/* The being stops on the tile from now on: its claim moves there when
 * occupancy_can_stop() allows it (a yielding holder loses the tile). False,
 * and nothing changes, otherwise. */
bool occupancy_claim(Occupancy *occupancy, BeingId being, TilePos tile);

/* For one that cannot move (knocked down): the tile is its own, taken from
 * whoever held it, unless that one cannot move either (its claim was forced
 * too): then nothing changes and this returns false, and the being crawls
 * aside (it cannot lie on top of the other). */
bool occupancy_force(Occupancy *occupancy, BeingId being, TilePos tile);

/* The being stops nowhere (it holds no tile). */
void occupancy_release(Occupancy *occupancy, BeingId being);

/* How many beings other than `being` are on the tile now. */
int occupancy_others_on(const Occupancy *occupancy, BeingId being, TilePos tile);

/* A pathfinder stop filter (Pathfinder.stop_filter): searches for a being
 * accept only the tiles it may stop on (as when it does something: tiles of
 * yielding beings are free). `userdata` is an OccupancyStopper. */
typedef struct OccupancyStopper {
    const Occupancy *occupancy;
    BeingId being;
} OccupancyStopper;

bool occupancy_stop_filter(const Map *map, int x, int y, int z, void *userdata);

#endif /* COLONY_CORE_OCCUPANCY_H */
