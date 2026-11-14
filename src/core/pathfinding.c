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

bool pathfinder_init(Pathfinder *finder, int width, int height)
{
    memset(finder, 0, sizeof(*finder));
    if (width <= 0 || height <= 0) {
        return false;
    }
    size_t count = (size_t)width * (size_t)height;
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
        size_t count = (size_t)finder->width * (size_t)finder->height;
        memset(finder->seen_stamp, 0, count * sizeof(*finder->seen_stamp));
        memset(finder->closed_stamp, 0, count * sizeof(*finder->closed_stamp));
        finder->stamp = 1;
    }
    finder->heap_count = 0;
    finder->last_expanded = 0;
    finder->last_cost = 0.0f;
}

/* Octile distance: exact cost of the shortest path on uniform cheapest terrain. */
static float heuristic(int x, int y, int goal_x, int goal_y)
{
    int dx = abs(x - goal_x);
    int dy = abs(y - goal_y);
    int diagonal = dx < dy ? dx : dy;
    int straight = (dx > dy ? dx : dy) - diagonal;
    return PATH_MIN_TILE_COST * ((float)straight + SQRT2 * (float)diagonal);
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
    for (uint32_t i = end; i != start; i = (uint32_t)finder->came_from[i]) {
        out->steps[slot].x = (int16_t)(i % (uint32_t)finder->width);
        out->steps[slot].y = (int16_t)(i / (uint32_t)finder->width);
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

/* Returns the tile index reached, or -1. */
static int64_t search(Pathfinder *finder, const Map *map, TilePos start, const SearchGoal *target,
                      float max_cost)
{
    begin_search(finder);
    if (!map_in_bounds(map, start.x, start.y) || map->width != finder->width ||
        map->height != finder->height) {
        return -1;
    }
    if (target->goal != NULL && !map_is_passable(map, target->goal->x, target->goal->y)) {
        return -1; /* Would explore the whole reachable area for nothing. */
    }

    const uint32_t width = (uint32_t)map->width;
    const uint32_t start_index = (uint32_t)map_index(map, start.x, start.y);
    finder->cost_so_far[start_index] = 0.0f;
    finder->came_from[start_index] = -1;
    finder->seen_stamp[start_index] = finder->stamp;
    float start_h = target->goal ? heuristic(start.x, start.y, target->goal->x, target->goal->y)
                                 : 0.0f;
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

        int x = (int)(current % width);
        int y = (int)(current / width);
        bool reached = target->goal != NULL
                           ? (x == target->goal->x && y == target->goal->y)
                           : (target->predicate != NULL &&
                              target->predicate(map, x, y, target->userdata));
        if (reached) {
            finder->last_cost = finder->cost_so_far[current];
            return current;
        }

        for (int d = 0; d < 8; d++) {
            int dx = DIRECTIONS[d][0];
            int dy = DIRECTIONS[d][1];
            int nx = x + dx;
            int ny = y + dy;
            float step = map_move_cost(map, nx, ny); /* 0 when out of bounds or blocked. */
            if (step <= 0.0f) {
                continue;
            }
            if (dx != 0 && dy != 0) {
                if (!map_is_passable(map, x + dx, y) || !map_is_passable(map, x, y + dy)) {
                    continue; /* No corner cutting. */
                }
                step *= SQRT2;
            }
            float cost = finder->cost_so_far[current] + step;
            if (cost > max_cost) {
                continue;
            }
            uint32_t next = (uint32_t)ny * width + (uint32_t)nx;
            if (finder->closed_stamp[next] == finder->stamp) {
                continue;
            }
            if (finder->seen_stamp[next] == finder->stamp && cost >= finder->cost_so_far[next]) {
                continue;
            }
            finder->seen_stamp[next] = finder->stamp;
            finder->cost_so_far[next] = cost;
            finder->came_from[next] = (int32_t)current;
            float priority =
                cost + (target->goal ? heuristic(nx, ny, target->goal->x, target->goal->y) : 0.0f);
            if (!heap_push(finder, priority, next)) {
                return -1;
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
    return build_path(finder, (uint32_t)map_index(map, start.x, start.y), (uint32_t)end, out);
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
        found->x = (int16_t)(end % map->width);
        found->y = (int16_t)(end / map->width);
    }
    return build_path(finder, (uint32_t)map_index(map, start.x, start.y), (uint32_t)end, out);
}

size_t pathfinder_reachable_count(Pathfinder *finder, const Map *map, TilePos start,
                                  float max_cost)
{
    SearchGoal target = {NULL, NULL, NULL}; /* Never reached: floods the whole area. */
    search(finder, map, start, &target, max_cost);
    return finder->last_expanded;
}
