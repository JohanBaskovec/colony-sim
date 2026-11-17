/*
 * Vision benchmark: casts the field of view of settlers standing on random
 * tiles of a generated map, whole and near, and reports the cost, the tiles
 * seen and the rays' steps. Not part of the test suite.
 *
 *     bench_vision [map_size] [casts] [seed] [flat]
 */
#include "core/rng.h"
#include "core/vision.h"
#include "core/worldgen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double seconds(clock_t from, clock_t to)
{
    return (double)(to - from) / (double)CLOCKS_PER_SEC;
}

static size_t popcount_all(const uint64_t *bits, size_t words)
{
    size_t count = 0;
    for (size_t i = 0; i < words; i++) {
        count += (size_t)__builtin_popcountll(bits[i]);
    }
    return count;
}

int main(int argc, char **argv)
{
    int size = argc > 1 ? atoi(argv[1]) : 200;
    int casts = argc > 2 ? atoi(argv[2]) : 200;
    uint64_t seed = argc > 3 ? strtoull(argv[3], NULL, 10) : 1;

    WorldGenParams params;
    worldgen_default_params(&params);
    params.seed = seed;
    params.width = size;
    params.height = size;
    params.flat = argc > 4 && strcmp(argv[4], "flat") == 0;

    Map map;
    Vision vision;
    if (!worldgen_generate(&map, &params) || !vision_init(&vision, &map)) {
        fprintf(stderr, "initialization failed\n");
        return EXIT_FAILURE;
    }
    uint64_t *bits = malloc(vision.words * sizeof(*bits));
    if (bits == NULL) {
        return EXIT_FAILURE;
    }
    printf("map %d x %d x %d, %d casts\n", map.width, map.height, map.depth, casts);

    static const int RADII[] = {0, VISION_NEAR_RADIUS};
    for (int mode = 0; mode < 2; mode++) {
        Rng rng;
        rng_seed(&rng, seed, 7);
        vision.rays = vision.steps = 0;
        double worst = 0.0, total = 0.0;
        size_t seen = 0;
        for (int i = 0; i < casts; i++) {
            int x = (int)rng_range_u32(&rng, (uint32_t)map.width);
            int y = (int)rng_range_u32(&rng, (uint32_t)map.height);
            int z = map_ground_level(&map, x, y);
            if (!map_is_passable(&map, x, y, z)) {
                i--;
                continue;
            }
            memset(bits, 0, vision.words * sizeof(*bits));
            clock_t begin = clock();
            vision_cast(&vision, &map, x, y, z, false, RADII[mode], bits);
            double took = seconds(begin, clock());
            total += took;
            worst = took > worst ? took : worst;
            seen += popcount_all(bits, vision.words);
        }
        printf("%-5s: %8.1f us average, %8.1f us worst, %8.0f tiles seen, %8.0f steps, %6.0f rays "
               "per cast\n",
               mode == 0 ? "whole" : "near", total / casts * 1e6, worst * 1e6, (double)seen / casts,
               (double)vision.steps / casts, (double)vision.rays / casts);
    }
    free(bits);
    vision_free(&vision);
    map_free(&map);
    return EXIT_SUCCESS;
}
