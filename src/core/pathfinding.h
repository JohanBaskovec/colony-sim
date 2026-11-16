/*
 * Grid pathfinding: A* towards a goal tile, or Dijkstra towards the nearest
 * tile matching a predicate (e.g. "nearest edible plant").
 *
 * Movement is 8-directional on a level. Entering a tile costs map_move_cost()
 * (times sqrt(2) diagonally); diagonal moves may not cut corners next to
 * impassable tiles. Slopes lead to the levels above and below (orthogonal
 * steps only, MAP_CLIMB_COST times the tile reached, see map_can_climb()).
 * A Pathfinder owns reusable scratch memory sized for one map, so searches
 * do not allocate (except when the open list grows).
 */
#ifndef COLONY_CORE_PATHFINDING_H
#define COLONY_CORE_PATHFINDING_H

#include "core/map.h"
#include "core/tile_box.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Cheapest possible tile cost: keeps the A* heuristic admissible. */
#define PATH_MIN_TILE_COST 1.0f
#define PATH_NO_COST_LIMIT 1e30f

/* Sequence of tiles to walk through: excludes the start, ends at the destination. */
typedef struct Path {
    TilePos *steps;
    int count;
    int capacity;
} Path;

void path_clear(Path *path);
void path_free(Path *path);

/* What a search knows of a tile; the fields of a tile side by side, so that
 * looking at one costs a single cache miss. */
typedef struct PathNode {
    uint32_t seen_stamp;   /* The search that reached the tile... */
    uint32_t closed_stamp; /* ...and the one that expanded it. */
    float cost_so_far;     /* g cost, valid when seen_stamp matches. */
    int32_t came_from;
} PathNode;

typedef struct PathHeapItem {
    float priority;
    uint32_t index;
} PathHeapItem;

typedef struct Pathfinder {
    int width;
    int height;
    int depth;
    PathNode *nodes; /* One per tile. */
    uint32_t stamp;  /* Incremented per search instead of clearing the nodes. */
    PathHeapItem *heap;
    size_t heap_count;
    size_t heap_capacity;
    /* Statistics of the last search. */
    size_t last_expanded;
    float last_cost;
} Pathfinder;

typedef bool (*TilePredicate)(const Map *map, int x, int y, int z, void *userdata);

bool pathfinder_init(Pathfinder *finder, int width, int height, int depth);
void pathfinder_free(Pathfinder *finder);

/*
 * Finds the cheapest path from `start` to `goal`. The start tile itself may be
 * impassable (a colonist standing there); the goal must be passable. Gives up
 * beyond `max_cost`. On success, `out` holds the path and last_cost its cost.
 */
bool pathfinder_find(Pathfinder *finder, const Map *map, TilePos start, TilePos goal,
                     float max_cost, Path *out);

/*
 * Finds the cheapest reachable tile for which `predicate` returns true (the
 * start tile included) and the path to it. `found` receives the tile.
 */
bool pathfinder_find_nearest(Pathfinder *finder, const Map *map, TilePos start,
                             TilePredicate predicate, void *userdata, float max_cost, Path *out,
                             TilePos *found);

/*
 * As pathfinder_find_nearest(), when every tile the predicate accepts lies in
 * `box`: an A* search with the distance to the box as heuristic, which heads
 * for it instead of spreading evenly (a far zone costs a narrow strip, not a
 * disk). The tile found is as cheap to reach as the nearest one, but where
 * several are, it may be another one than pathfinder_find_nearest() picks.
 */
bool pathfinder_find_nearest_in_box(Pathfinder *finder, const Map *map, TilePos start,
                                    const TileBox *box, TilePredicate predicate, void *userdata,
                                    float max_cost, Path *out, TilePos *found);

/* Number of tiles reachable from `start` within `max_cost`. */
size_t pathfinder_reachable_count(Pathfinder *finder, const Map *map, TilePos start,
                                  float max_cost);

#endif /* COLONY_CORE_PATHFINDING_H */
