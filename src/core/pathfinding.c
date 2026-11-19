#include "core/pathfinding.h"

#include <stdlib.h>
#include <string.h>

#define SQRT2 1.41421356f

/* ---- Path ---------------------------------------------------------------- */

void path_clear(Path *path)
{
    path->count = 0;
}

void path_free(Path *path)
{
    free(path->steps);
    memset(path, 0, sizeof(*path));
}

Heading path_step_heading(TilePos from, TilePos to, Heading fallback)
{
    if (to.x != from.x) {
        return to.x < from.x ? HEADING_WEST : HEADING_EAST;
    }
    if (to.y != from.y) {
        return to.y < from.y ? HEADING_NORTH : HEADING_SOUTH;
    }
    return fallback;
}

static bool path_reserve(Path *path, int count)
{
    if (count <= path->capacity) {
        return true;
    }
    int capacity = path->capacity > 0 ? path->capacity : 32;
    while (capacity < count) {
        capacity *= 2;
    }
    TilePos *steps = realloc(path->steps, (size_t)capacity * sizeof(*steps));
    if (steps == NULL) {
        return false;
    }
    path->steps = steps;
    path->capacity = capacity;
    return true;
}

/* ---- Binary min-heap ----------------------------------------------------- */

static bool heap_push(Pathfinder *finder, float priority, uint32_t index)
{
    if (finder->heap_count == finder->heap_capacity) {
        size_t capacity = finder->heap_capacity > 0 ? finder->heap_capacity * 2 : 1024;
        PathHeapItem *heap = realloc(finder->heap, capacity * sizeof(*heap));
        if (heap == NULL) {
            return false;
        }
        finder->heap = heap;
        finder->heap_capacity = capacity;
    }
    size_t i = finder->heap_count++;
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (finder->heap[parent].priority <= priority) {
            break;
        }
        finder->heap[i] = finder->heap[parent];
        i = parent;
    }
    finder->heap[i] = (PathHeapItem){priority, index};
    return true;
}

static uint32_t heap_pop(Pathfinder *finder)
{
    PathHeapItem *heap = finder->heap;
    uint32_t top = heap[0].index;
    PathHeapItem last = heap[--finder->heap_count];
    size_t count = finder->heap_count;
    size_t i = 0;
    for (;;) {
        size_t child = i * 2 + 1;
        if (child >= count) {
            break;
        }
        if (child + 1 < count && heap[child + 1].priority < heap[child].priority) {
            child++;
        }
        if (last.priority <= heap[child].priority) {
            break;
        }
        heap[i] = heap[child];
        i = child;
    }
    if (count > 0) {
        heap[i] = last;
    }
    return top;
}

/* ---- Pathfinder ---------------------------------------------------------- */

bool pathfinder_init(Pathfinder *finder, int width, int height, int depth)
{
    memset(finder, 0, sizeof(*finder));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    size_t count = (size_t)width * (size_t)height * (size_t)depth;
    finder->nodes = calloc(count, sizeof(*finder->nodes));
    if (finder->nodes == NULL) {
        return false;
    }
    finder->width = width;
    finder->height = height;
    finder->depth = depth;
    return true;
}

void pathfinder_free(Pathfinder *finder)
{
    free(finder->nodes);
    free(finder->heap);
    memset(finder, 0, sizeof(*finder));
}

static void begin_search(Pathfinder *finder)
{
    finder->stamp++;
    if (finder->stamp == 0) { /* Wrapped around: stale stamps could match again. */
        size_t count = (size_t)finder->width * (size_t)finder->height * (size_t)finder->depth;
        memset(finder->nodes, 0, count * sizeof(*finder->nodes));
        finder->stamp = 1;
    }
    finder->heap_count = 0;
    finder->last_expanded = 0;
    finder->last_cost = 0.0f;
}

/* Octile distance on the level (the exact cost of the shortest path on uniform
 * cheapest terrain), plus what climbing to the goal's level costs at least:
 * each step up or down a slope moves one tile across too, at MAP_CLIMB_COST
 * times the tile instead of once. */
static float heuristic(int x, int y, int z, const TilePos *goal)
{
    int dx = abs(x - goal->x);
    int dy = abs(y - goal->y);
    int dz = abs(z - goal->z);
    int diagonal = dx < dy ? dx : dy;
    int straight = (dx > dy ? dx : dy) - diagonal;
    return PATH_MIN_TILE_COST *
           ((float)straight + SQRT2 * (float)diagonal + (MAP_CLIMB_COST - 1.0f) * (float)dz);
}

/* As heuristic(), towards the nearest tile of a box: the distance on each
 * axis is to the box's span (0 within it). Consistent as heuristic() is: a
 * step changes it by at most what the step costs at least. */
static float box_heuristic(int x, int y, int z, const TileBox *box)
{
    int dx = x < box->x0 ? box->x0 - x : (x > box->x1 ? x - box->x1 : 0);
    int dy = y < box->y0 ? box->y0 - y : (y > box->y1 ? y - box->y1 : 0);
    int dz = z < box->z0 ? box->z0 - z : (z > box->z1 ? z - box->z1 : 0);
    int diagonal = dx < dy ? dx : dy;
    int straight = (dx > dy ? dx : dy) - diagonal;
    return PATH_MIN_TILE_COST *
           ((float)straight + SQRT2 * (float)diagonal + (MAP_CLIMB_COST - 1.0f) * (float)dz);
}

static bool build_path(const Pathfinder *finder, uint32_t start, uint32_t end, Path *out)
{
    int count = 0;
    for (uint32_t i = end; i != start; i = (uint32_t)finder->nodes[i].came_from) {
        count++;
    }
    if (out == NULL) {
        return true;
    }
    if (!path_reserve(out, count)) {
        return false;
    }
    out->count = count;
    int slot = count - 1;
    const uint32_t width = (uint32_t)finder->width;
    const uint32_t level = width * (uint32_t)finder->height;
    for (uint32_t i = end; i != start; i = (uint32_t)finder->nodes[i].came_from) {
        out->steps[slot].x = (int16_t)(i % width);
        out->steps[slot].y = (int16_t)(i % level / width);
        out->steps[slot].z = (int16_t)(i / level);
        slot--;
    }
    return true;
}

/* What entering a tile (an index in bounds) costs a walker, 0 when it cannot:
 * animals open no door, but walk through one kept open. */
static inline float walk_cost(const Map *map, bool animal, size_t index)
{
    return animal && map->structure[index] == STRUCTURE_DOOR && !map_door_kept_open_at(map, index)
               ? 0.0f
               : map_move_cost_at(map, index);
}

/* What a step up from a tile, or down onto it, costs a walker (see
 * map_climb_cost_at()): animals climb no ladder. */
static inline float climb_cost(const Map *map, bool animal, size_t index)
{
    if (animal) {
        return map->slope[index] != 0 ? MAP_CLIMB_COST : 0.0f;
    }
    return map_climb_cost_at(map, index);
}

bool path_walker_can_stand(PathWalker walker, const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) &&
           walk_cost(map, walker == PATH_WALKER_ANIMAL, map_index(map, x, y, z)) > 0.0f;
}

bool path_walker_can_step(PathWalker walker, const Map *map, TilePos from, TilePos to)
{
    const bool animal = walker == PATH_WALKER_ANIMAL;
    int dx = to.x - from.x, dy = to.y - from.y;
    if (!map_in_bounds(map, from.x, from.y, from.z) ||
        !path_walker_can_stand(walker, map, to.x, to.y, to.z) || dx < -1 || dx > 1 || dy < -1 ||
        dy > 1) {
        return false;
    }
    if (to.z == from.z) {
        if (dx == 0 && dy == 0) {
            return false;
        }
        /* No corner cutting. */
        return dx == 0 || dy == 0 ||
               (path_walker_can_stand(walker, map, from.x + dx, from.y, from.z) &&
                path_walker_can_stand(walker, map, from.x, from.y + dy, from.z));
    }
    TilePos low = to.z > from.z ? from : to;
    TilePos high = to.z > from.z ? to : from;
    if (high.z != low.z + 1) {
        return false;
    }
    if (dx == 0 && dy == 0) {
        return !animal && map_can_climb_tree(map, low.x, low.y, low.z);
    }
    return (dx == 0 || dy == 0) && map_can_climb(map, low.x, low.y, low.z, high.x, high.y) &&
           climb_cost(map, animal, map_index(map, low.x, low.y, low.z)) > 0.0f;
}

typedef struct SearchGoal {
    const TilePos *goal;     /* A* when set... */
    TilePredicate predicate; /* ...Dijkstra towards a predicate otherwise... */
    void *userdata;
    const TileBox *box; /* ...or A* towards a predicate true only in this box. */
} SearchGoal;

/* The heuristic of a search: 0 for Dijkstra. */
static float goal_heuristic(const SearchGoal *target, int x, int y, int z)
{
    if (target->goal != NULL) {
        return heuristic(x, y, z, target->goal);
    }
    return target->box != NULL ? box_heuristic(x, y, z, target->box) : 0.0f;
}

static const int DIRECTIONS[8][2] = {{1, 0}, {-1, 0}, {0, 1},  {0, -1},
                                     {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

/* Records a step from `current` to `next` costing `cost` in all, if it is
 * the cheapest way to `next` found so far. False when out of memory. */
static bool relax(Pathfinder *finder, const SearchGoal *target, uint32_t current, uint32_t next,
                  float cost, int nx, int ny, int nz)
{
    PathNode *node = &finder->nodes[next];
    if (node->closed_stamp == finder->stamp ||
        (node->seen_stamp == finder->stamp && cost >= node->cost_so_far)) {
        return true;
    }
    node->seen_stamp = finder->stamp;
    node->cost_so_far = cost;
    node->came_from = (int32_t)current;
    float priority = cost + goal_heuristic(target, nx, ny, nz);
    return heap_push(finder, priority, next);
}

/* Returns the tile index reached, or -1. */
static int64_t search(Pathfinder *finder, const Map *map, TilePos start, const SearchGoal *target,
                      float max_cost)
{
    begin_search(finder);
    if (!map_in_bounds(map, start.x, start.y, start.z) || map->width != finder->width ||
        map->height != finder->height || map->depth != finder->depth) {
        return -1;
    }
    const bool animal = finder->walker == PATH_WALKER_ANIMAL;
    if (target->goal != NULL && !path_walker_can_stand(finder->walker, map, target->goal->x,
                                                       target->goal->y, target->goal->z)) {
        return -1; /* Would explore the whole reachable area for nothing. */
    }

    const uint32_t width = (uint32_t)map->width;
    const uint32_t level = width * (uint32_t)map->height; /* Tiles of a level. */
    const uint32_t start_index = (uint32_t)map_index(map, start.x, start.y, start.z);
    finder->nodes[start_index] = (PathNode){finder->stamp, 0, 0.0f, -1};
    float start_h = goal_heuristic(target, start.x, start.y, start.z);
    if (!heap_push(finder, start_h, start_index)) {
        return -1;
    }

    while (finder->heap_count > 0) {
        uint32_t current = heap_pop(finder);
        PathNode *node = &finder->nodes[current];
        if (node->closed_stamp == finder->stamp) {
            continue; /* Outdated duplicate entry. */
        }
        node->closed_stamp = finder->stamp;
        finder->last_expanded++;

        int z = (int)(current / level);
        int x = (int)(current % width);
        int y = (int)(current % level / width);
        bool reached =
            target->goal != NULL
                ? (x == target->goal->x && y == target->goal->y && z == target->goal->z)
                : (target->predicate != NULL && target->predicate(map, x, y, z, target->userdata) &&
                   (finder->stop_filter == NULL ||
                    finder->stop_filter(map, x, y, z, finder->stop_userdata)));
        if (reached) {
            finder->last_cost = node->cost_so_far;
            return current;
        }

        /* Costs of the four orthogonal neighbors (0 when out of bounds or
         * blocked), in DIRECTIONS order. They are computed once: they also
         * decide which diagonal moves are allowed. */
        float side[4];
        bool inside[4];
        for (int d = 0; d < 4; d++) {
            int nx = x + DIRECTIONS[d][0];
            int ny = y + DIRECTIONS[d][1];
            inside[d] = nx >= 0 && ny >= 0 && nx < map->width && ny < map->height;
            side[d] =
                inside[d]
                    ? walk_cost(map, animal,
                                (size_t)((int64_t)current + DIRECTIONS[d][1] * (int64_t)width +
                                         DIRECTIONS[d][0]))
                    : 0.0f;
        }
        const float base_cost = node->cost_so_far;

        for (int d = 0; d < 8; d++) {
            int dx = DIRECTIONS[d][0];
            int dy = DIRECTIONS[d][1];
            /* Checked below if d >= 4. */
            uint32_t next = (uint32_t)((int64_t)current + dy * (int64_t)width + dx);
            float step;
            if (d < 4) {
                step = side[d];
            } else {
                /* No corner cutting: both orthogonal neighbors must be passable
                 * (which also means the diagonal one is in bounds). */
                if (side[dx > 0 ? 0 : 1] <= 0.0f || side[dy > 0 ? 2 : 3] <= 0.0f ||
                    finder->nodes[next].closed_stamp == finder->stamp) {
                    continue;
                }
                step = walk_cost(map, animal, next) * SQRT2;
            }
            if (step <= 0.0f || base_cost + step > max_cost) {
                continue;
            }
            if (!relax(finder, target, current, next, base_cost + step, x + dx, y + dy, z)) {
                return -1;
            }
        }

        /* Up a slope or a ladder, to the orthogonal neighbors of the level
         * above. */
        float climb = climb_cost(map, animal, current);
        if (climb > 0.0f && z + 1 < map->depth && map_is_open_at(map, current + level)) {
            for (int d = 0; d < 4; d++) {
                if (!inside[d]) {
                    continue;
                }
                int dx = DIRECTIONS[d][0], dy = DIRECTIONS[d][1];
                uint32_t next = (uint32_t)((int64_t)current + level + dy * (int64_t)width + dx);
                float step = walk_cost(map, animal, next) * climb;
                if (step > 0.0f && base_cost + step <= max_cost &&
                    !relax(finder, target, current, next, base_cost + step, x + dx, y + dy,
                           z + 1)) {
                    return -1;
                }
            }
        }
        /* Up or down a tree's trunk: straight to the foliage over this tile,
         * or from the foliage down to the ground beside the trunk. */
        if (!animal && z + 1 < map->depth && map->terrain[current + level] == TERRAIN_FOLIAGE &&
            map_can_climb_tree(map, x, y, z)) {
            uint32_t next = current + level;
            float step = map_move_cost_at(map, next) * MAP_TREE_CLIMB_COST;
            if (base_cost + step <= max_cost &&
                !relax(finder, target, current, next, base_cost + step, x, y, z + 1)) {
                return -1;
            }
        }
        if (!animal && z > 0 && map->terrain[current] == TERRAIN_FOLIAGE &&
            map_can_climb_tree(map, x, y, z - 1)) {
            uint32_t next = current - level;
            float step = map_move_cost_at(map, next) * MAP_TREE_CLIMB_COST;
            if (base_cost + step <= max_cost &&
                !relax(finder, target, current, next, base_cost + step, x, y, z - 1)) {
                return -1;
            }
        }
        /* Down a slope or a ladder: over the open space next to this tile,
         * onto the slope or ladder below it. */
        if (z > 0) {
            for (int d = 0; d < 4; d++) {
                if (side[d] > 0.0f || !inside[d]) {
                    continue;
                }
                int dx = DIRECTIONS[d][0], dy = DIRECTIONS[d][1];
                uint32_t beside = (uint32_t)((int64_t)current + dy * (int64_t)width + dx);
                uint32_t next = beside - level;
                float down = climb_cost(map, animal, next);
                if (!map_is_open_at(map, beside) || down <= 0.0f) {
                    continue;
                }
                float step = walk_cost(map, animal, next) * down;
                if (step > 0.0f && base_cost + step <= max_cost &&
                    !relax(finder, target, current, next, base_cost + step, x + dx, y + dy,
                           z - 1)) {
                    return -1;
                }
            }
        }
    }
    return -1;
}

bool pathfinder_find(Pathfinder *finder, const Map *map, TilePos start, TilePos goal,
                     float max_cost, Path *out)
{
    SearchGoal target = {&goal, NULL, NULL, NULL};
    int64_t end = search(finder, map, start, &target, max_cost);
    if (end < 0) {
        return false;
    }
    return build_path(finder, (uint32_t)map_index(map, start.x, start.y, start.z), (uint32_t)end,
                      out);
}

static bool find_nearest(Pathfinder *finder, const Map *map, TilePos start,
                         const SearchGoal *target, float max_cost, Path *out, TilePos *found)
{
    int64_t end = search(finder, map, start, target, max_cost);
    if (end < 0) {
        return false;
    }
    if (found != NULL) {
        int x, y, z;
        map_tile_of(map, (size_t)end, &x, &y, &z);
        *found = (TilePos){(int16_t)x, (int16_t)y, (int16_t)z};
    }
    return build_path(finder, (uint32_t)map_index(map, start.x, start.y, start.z), (uint32_t)end,
                      out);
}

bool pathfinder_find_nearest(Pathfinder *finder, const Map *map, TilePos start,
                             TilePredicate predicate, void *userdata, float max_cost, Path *out,
                             TilePos *found)
{
    SearchGoal target = {NULL, predicate, userdata, NULL};
    return find_nearest(finder, map, start, &target, max_cost, out, found);
}

bool pathfinder_find_nearest_in_box(Pathfinder *finder, const Map *map, TilePos start,
                                    const TileBox *box, TilePredicate predicate, void *userdata,
                                    float max_cost, Path *out, TilePos *found)
{
    if (tile_box_is_empty(box)) {
        begin_search(finder);
        return false; /* Nothing to find. */
    }
    SearchGoal target = {NULL, predicate, userdata, box};
    return find_nearest(finder, map, start, &target, max_cost, out, found);
}

size_t pathfinder_reachable_count(Pathfinder *finder, const Map *map, TilePos start, float max_cost)
{
    SearchGoal target = {NULL, NULL, NULL, NULL}; /* Never reached: floods the whole area. */
    search(finder, map, start, &target, max_cost);
    return finder->last_expanded;
}
