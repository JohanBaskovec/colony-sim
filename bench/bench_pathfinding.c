/*
 * Pathfinding benchmark: runs many A* searches between random passable tiles
 * of a generated map and reports timings. Not part of the test suite.
 *
 *     bench_pathfinding [map_size] [searches] [seed]
 */
#include "core/pathfinding.h"
#include "core/rng.h"
#include "core/worldgen.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static TilePos random_passable_tile(const Map *map, Rng *rng)
{
    for (;;) {
        int x = (int)rng_range_u32(rng, (uint32_t)map->width);
        int y = (int)rng_range_u32(rng, (uint32_t)map->height);
        if (map_is_passable(map, x, y)) {
            return (TilePos){(int16_t)x, (int16_t)y};
        }
    }
}

int main(int argc, char **argv)
{
    int size = argc > 1 ? atoi(argv[1]) : 256;
    int searches = argc > 2 ? atoi(argv[2]) : 2000;
    uint64_t seed = argc > 3 ? strtoull(argv[3], NULL, 10) : 1;

    WorldGenParams params;
    worldgen_default_params(&params);
    params.seed = seed;
    params.width = size;
    params.height = size;

    Map map;
    Pathfinder finder;
    Path path = {0};
    if (!worldgen_generate(&map, &params) || !pathfinder_init(&finder, size, size)) {
        fprintf(stderr, "initialization failed\n");
        return EXIT_FAILURE;
    }

    Rng rng;
    rng_seed(&rng, seed, 99);
    int found = 0;
    size_t expanded = 0;
    double total_length = 0.0;

    clock_t begin = clock();
    for (int i = 0; i < searches; i++) {
        TilePos start = random_passable_tile(&map, &rng);
        TilePos goal = random_passable_tile(&map, &rng);
        if (pathfinder_find(&finder, &map, start, goal, PATH_NO_COST_LIMIT, &path)) {
            found++;
            total_length += path.count;
        }
        expanded += finder.last_expanded;
    }
    double seconds = (double)(clock() - begin) / CLOCKS_PER_SEC;

    printf("map %dx%d, seed %llu: %d searches, %d found\n", size, size, (unsigned long long)seed,
           searches, found);
    printf("  total %.3f s, %.1f us/search, %.0f nodes expanded/search, avg path %.1f steps\n",
           seconds, seconds * 1e6 / searches, (double)expanded / searches,
           found > 0 ? total_length / found : 0.0);

    path_free(&path);
    pathfinder_free(&finder);
    map_free(&map);
    return EXIT_SUCCESS;
}
