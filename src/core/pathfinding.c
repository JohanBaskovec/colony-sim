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
    finder->cost_so_far = malloc(count * sizeof(*finder->cost_so_far));
    finder->came_from = malloc(count * sizeof(*finder->came_from));
    finder->seen_stamp = calloc(count, sizeof(*finder->seen_stamp));
    finder->closed_stamp = calloc(count, sizeof(*finder->closed_stamp));
    if (finder->cost_so_far == NULL || finder->came_from == NULL ||
        finder->seen_stamp == NULL || finder->closed_stamp == NULL) {
        pathfinder_free(finder);
        return false;
    }
    finder->width = width;
    finder->height = height;
    finder->depth = depth;
    return true;
}

void pathfinder_free(Pathfinder *finder)
{
    free(finder->cost_so_far);
    free(finder->came_from);
    free(finder->seen_stamp);
    free(finder->closed_stamp);
    free(finder->heap);
    memset(finder, 0, sizeof(*finder));
}

static void begin_search(Pathfinder *finder)
{
    finder->stamp++;
    if (finder->stamp == 0) { /* Wrapped around: stale stamps could match again. */
        size_t count = (size_t)finder->width * (size_t)finder->height * (size_t)finder->depth;
        memset(finder->seen_stamp, 0, count * sizeof(*finder->seen_stamp));
        memset(finder->closed_stamp, 0, count * sizeof(*finder->closed_stamp));
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

static bool build_path(const Pathfinder *finder, uint32_t start, uint32_t end, Path *out)
{
    int count = 0;
    for (uint32_t i = end; i != start; i = (uint32_t)finder->came_from[i]) {
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
    for (uint32_t i = end; i != start; i = (uint32_t)finder->came_from[i]) {
        out->steps[slot].x = (int16_t)(i % width);
        out->steps[slot].y = (int16_t)(i % level / width);
        out->steps[slot].z = (int16_t)(i / level);
        slot--;
    }
    return true;
}

typedef struct SearchGoal {
    const TilePos *goal; /* A* when set... */
    TilePredicate predicate; /* ...Dijkstra towards a predicate otherwise. */
    void *userdata;
} SearchGoal;

static const int DIRECTIONS[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1},
                                     {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

/* Records a step from `current` to `next` costing `cost` in all, if it is
 * the cheapest way to `next` found so far. False when out of memory. */
static bool relax(Pathfinder *finder, const SearchGoal *target, uint32_t current, uint32_t next,
                  float cost, int nx, int ny, int nz)
{
    if (finder->closed_stamp[next] == finder->stamp ||
        (finder->seen_stamp[next] == finder->stamp && cost >= finder->cost_so_far[next])) {
        return true;
    }
    finder->seen_stamp[next] = finder->stamp;
    finder->cost_so_far[next] = cost;
    finder->came_from[next] = (int32_t)current;
    float priority = cost + (target->goal ? heuristic(nx, ny, nz, target->goal) : 0.0f);
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
    if (target->goal != NULL &&
        !map_is_passable(map, target->goal->x, target->goal->y, target->goal->z)) {
        return -1; /* Would explore the whole reachable area for nothing. */
    }

    const uint32_t width = (uint32_t)map->width;
    const uint32_t level = width * (uint32_t)map->height; /* Tiles of a level. */
    const uint32_t start_index = (uint32_t)map_index(map, start.x, start.y, start.z);
    finder->cost_so_far[start_index] = 0.0f;
    finder->came_from[start_index] = -1;
    finder->seen_stamp[start_index] = finder->stamp;
    float start_h = target->goal ? heuristic(start.x, start.y, start.z, target->goal) : 0.0f;
    if (!heap_push(finder, start_h, start_index)) {
        return -1;
    }

    while (finder->heap_count > 0) {
        uint32_t current = heap_pop(finder);
        if (finder->closed_stamp[current] == finder->stamp) {
            continue; /* Outdated duplicate entry. */
        }
        finder->closed_stamp[current] = finder->stamp;
        finder->last_expanded++;

        int z = (int)(current / level);
        int x = (int)(current % width);
        int y = (int)(current % level / width);
        bool reached = target->goal != NULL
                           ? (x == target->goal->x && y == target->goal->y && z == target->goal->z)
                           : (target->predicate != NULL &&
                              target->predicate(map, x, y, z, target->userdata));
        if (reached) {
            finder->last_cost = finder->cost_so_far[current];
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
            side[d] = inside[d] ? map_move_cost_at(map, (size_t)((int64_t)current +
                                                                 DIRECTIONS[d][1] * (int64_t)width +
                                                                 DIRECTIONS[d][0]))
                                : 0.0f;
        }
        const float base_cost = finder->cost_so_far[current];

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
                    finder->closed_stamp[next] == finder->stamp) {
                    continue;
                }
                step = map_move_cost_at(map, next) * SQRT2;
            }
            if (step <= 0.0f || base_cost + step > max_cost) {
                continue;
            }
            if (!relax(finder, target, current, next, base_cost + step, x + dx, y + dy, z)) {
                return -1;
            }
        }

        /* Up a slope, to the orthogonal neighbors of the level above. */
        if (map->slope[current] != 0 && z + 1 < map->depth &&
            map_is_open_at(map, current + level)) {
            for (int d = 0; d < 4; d++) {
                if (!inside[d]) {
                    continue;
                }
                int dx = DIRECTIONS[d][0], dy = DIRECTIONS[d][1];
                uint32_t next = (uint32_t)((int64_t)current + level + dy * (int64_t)width + dx);
                float step = map_move_cost_at(map, next) * MAP_CLIMB_COST;
                if (step > 0.0f && base_cost + step <= max_cost &&
                    !relax(finder, target, current, next, base_cost + step, x + dx, y + dy,
                           z + 1)) {
                    return -1;
                }
            }
        }
        /* Down a slope: over the open space next to this tile, onto the slope
         * below it. */
        if (z > 0) {
            for (int d = 0; d < 4; d++) {
                if (side[d] > 0.0f || !inside[d]) {
                    continue;
                }
                int dx = DIRECTIONS[d][0], dy = DIRECTIONS[d][1];
                uint32_t beside = (uint32_t)((int64_t)current + dy * (int64_t)width + dx);
                uint32_t next = beside - level;
                if (!map_is_open_at(map, beside) || map->slope[next] == 0) {
                    continue;
                }
                float step = map_move_cost_at(map, next) * MAP_CLIMB_COST;
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
    SearchGoal target = {&goal, NULL, NULL};
    int64_t end = search(finder, map, start, &target, max_cost);
    if (end < 0) {
        return false;
    }
    return build_path(finder, (uint32_t)map_index(map, start.x, start.y, start.z), (uint32_t)end,
                      out);
}

bool pathfinder_find_nearest(Pathfinder *finder, const Map *map, TilePos start,
                             TilePredicate predicate, void *userdata, float max_cost, Path *out,
                             TilePos *found)
{
    SearchGoal target = {NULL, predicate, userdata};
    int64_t end = search(finder, map, start, &target, max_cost);
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

size_t pathfinder_reachable_count(Pathfinder *finder, const Map *map, TilePos start,
                                  float max_cost)
{
    SearchGoal target = {NULL, NULL, NULL}; /* Never reached: floods the whole area. */
    search(finder, map, start, &target, max_cost);
    return finder->last_expanded;
}
