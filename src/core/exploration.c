#include "core/exploration.h"

#include "core/rng.h"

#include <stdlib.h>
#include <string.h>

/* Walked around the arrival point: every tile within a number of steps is
 * explored, with its neighbors. */
static void explore_camp(Vision *vision, const Map *map, TilePos center)
{
    size_t tiles = map_tile_count(map);
    uint8_t *steps = malloc(tiles);
    size_t *queue = malloc(tiles * sizeof(*queue));
    if (steps == NULL || queue == NULL) {
        free(steps);
        free(queue);
        return;
    }
    memset(steps, 255, tiles);
    size_t head = 0, tail = 0;
    size_t start = map_index(map, center.x, center.y, center.z);
    steps[start] = 0;
    queue[tail++] = start;
    while (head < tail) {
        size_t index = queue[head++];
        int x, y, z;
        map_tile_of(map, index, &x, &y, &z);
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (vision_tracked(map, x + dx, y + dy, z)) {
                    vision_explore_tile(vision, map_index(map, x + dx, y + dy, z));
                }
            }
        }
        if (steps[index] >= EXPLORATION_CAMP_STEPS) {
            continue;
        }
        TilePos next[4 + MAP_MAX_LEVEL_LINKS];
        int count = 0;
        static const int SIDES[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
        for (int side = 0; side < 4; side++) {
            int nx = x + SIDES[side][0], ny = y + SIDES[side][1];
            if (map_is_passable(map, nx, ny, z)) {
                next[count++] = (TilePos){(int16_t)nx, (int16_t)ny, (int16_t)z};
            }
        }
        count += map_level_links(map, x, y, z, next + count);
        for (int i = 0; i < count; i++) {
            size_t other = map_index(map, next[i].x, next[i].y, next[i].z);
            if (steps[other] == 255) {
                steps[other] = (uint8_t)(steps[index] + 1);
                queue[tail++] = other;
            }
        }
    }
    free(steps);
    free(queue);
}

/* What is seen from a tile of the way in: the whole field or the near one. */
static void explore_view(Vision *vision, const Map *map, TilePos tile, int radius)
{
    memset(vision->scratch, 0, vision->words * sizeof(*vision->scratch));
    vision_cast(vision, map, tile.x, tile.y, tile.z, false, radius, vision->scratch);
    vision_explore_field(vision, vision->scratch);
}

int exploration_start(Vision *vision, const Map *map, Pathfinder *finder, TilePos center,
                      uint64_t seed)
{
    if (!map_in_bounds(map, center.x, center.y, center.z)) {
        return 0;
    }
    explore_camp(vision, map, center);
    explore_view(vision, map, center, 0);

    /* The way in: from an edge tile that can be walked from, drawn at random. */
    Rng rng;
    rng_seed(&rng, seed, 0x666f67ULL /* "fog" */);
    Path path = {0};
    bool found = false;
    for (int attempt = 0; attempt < 64 && !found; attempt++) {
        int side = (int)rng_range_u32(&rng, 4);
        int along = (int)rng_range_u32(&rng, (uint32_t)(side % 2 == 0 ? map->width : map->height));
        int x = side == 1 ? map->width - 1 : side == 3 ? 0 : along;
        int y = side == 0 ? 0 : side == 2 ? map->height - 1 : along;
        int z = map_ground_level(map, x, y);
        if (!map_is_passable(map, x, y, z)) {
            continue;
        }
        TilePos start = {(int16_t)x, (int16_t)y, (int16_t)z};
        found = pathfinder_find(finder, map, start, center, PATH_NO_COST_LIMIT, &path);
    }
    int length = 0;
    if (found) {
        length = path.count;
        for (int i = 0; i < path.count; i++) {
            bool far = i % EXPLORATION_WAY_FAR_EVERY == 0;
            explore_view(vision, map, path.steps[i], far ? 0 : VISION_NEAR_RADIUS);
        }
    }
    path_free(&path);
    return length;
}
