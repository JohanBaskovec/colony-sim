/*
 * Water benchmark (docs/WATER.md §3): the surface flow in three cases, run
 * until the water rests or for a number of steps, with the cost of a step
 * (processor time), the blocks awake and whether the water was kept. Not
 * part of the test suite.
 *
 *     bench_water [map_size] [steps] [open]
 *
 * The map's edges are sealed (a closed box, as the cases were made), unless
 * `open`: water then flows out over them (docs/WATER.md §4.1), and what left
 * is told.
 *
 * - dam: half the map a lake 1500 mm deep behind a wall of rock, which is
 *   dug through over 16 tiles: the lake pours into the other half.
 * - plain: a pond 48 tiles a side, 1000 mm deep, its banks taken away at
 *   once: it spreads over the whole plain.
 * - channel: a channel one tile wide dug from a lake across the map.
 */
#include "core/map.h"
#include "core/water.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double cpu_seconds(void)
{
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

typedef enum Case { CASE_DAM, CASE_PLAIN, CASE_CHANNEL, CASE_COUNT } Case;

static const char *const CASE_NAMES[CASE_COUNT] = {"dam", "plain", "channel"};

/* Lays the case's water at rest, then opens it (the map logs the change). */
static void lay(Map *map, Case which)
{
    int size = map->width;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            size_t i = map_index(map, x, y, 0);
            switch (which) {
            case CASE_DAM:
                map->rock[i] = x == size / 2 ? ROCK_GRANITE : ROCK_NONE;
                map->water[i] = x < size / 2 ? 1500 : 0;
                break;
            case CASE_PLAIN: {
                bool pond = abs(x - size / 2) < 24 && abs(y - size / 2) < 24;
                bool bank = abs(x - size / 2) == 24 || abs(y - size / 2) == 24;
                map->rock[i] = bank && abs(x - size / 2) <= 24 && abs(y - size / 2) <= 24
                                   ? ROCK_GRANITE
                                   : ROCK_NONE;
                map->water[i] = pond ? 1000 : 0;
                break;
            }
            case CASE_CHANNEL:
            case CASE_COUNT:
                map->rock[i] = x < 32 || y == size / 2 ? ROCK_NONE : ROCK_GRANITE;
                if (x == 32 && y == size / 2) {
                    map->rock[i] = ROCK_GRANITE; /* The lake shut from the channel. */
                }
                map->water[i] = x < 32 ? 800 : 0;
                break;
            }
        }
    }
    map_topology_changed(map);
}

static void open_case(Map *map, Case which)
{
    int size = map->width;
    switch (which) {
    case CASE_DAM:
        for (int y = size / 2 - 8; y < size / 2 + 8; y++) {
            map_set_rock(map, size / 2, y, 0, ROCK_NONE);
        }
        break;
    case CASE_PLAIN:
        for (int d = -24; d <= 24; d++) {
            map_set_rock(map, size / 2 + d, size / 2 - 24, 0, ROCK_NONE);
            map_set_rock(map, size / 2 + d, size / 2 + 24, 0, ROCK_NONE);
            map_set_rock(map, size / 2 - 24, size / 2 + d, 0, ROCK_NONE);
            map_set_rock(map, size / 2 + 24, size / 2 + d, 0, ROCK_NONE);
        }
        break;
    case CASE_CHANNEL:
    case CASE_COUNT: map_set_rock(map, 32, size / 2, 0, ROCK_NONE); break;
    }
}

static int run_case(Case which, int size, int steps, bool open)
{
    Map map;
    Water water;
    if (!map_init(&map, size, size, 2)) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    lay(&map, which);
    if (!water_init(&water, &map)) {
        fprintf(stderr, "out of memory\n");
        map_free(&map);
        return 1;
    }
    if (!open) {
        water_seal_edges(&water);
    }
    open_case(&map, which);
    uint64_t total = water_total(&water, &map);
    double begin = cpu_seconds(), worst = 0.0;
    uint32_t most = 0;
    uint64_t block_steps = 0;
    int step = 0;
    bool ok = true;
    for (; step < steps && ok; step++) {
        double start = cpu_seconds();
        ok = water_step(&water, &map);
        double elapsed = cpu_seconds() - start;
        worst = elapsed > worst ? elapsed : worst;
        most = water.active_count > most ? water.active_count : most;
        block_steps += water.active_count;
        if (water.active_count == 0) {
            step++;
            break;
        }
    }
    double seconds = cpu_seconds() - begin;
    /* Kept: changed only by what crossed the edges. */
    bool kept = (int64_t)water_total(&water, &map) == (int64_t)total + water.edge_in;
    char out[48] = "";
    if (water.edge_in != 0) {
        snprintf(out, sizeof(out), ", %.0f mm %s", fabs((double)water.edge_in) / WATER_FINE,
                 water.edge_in > 0 ? "in" : "out");
    }
    printf("%-8s %d steps%s, %.1f us/step (worst %.2f ms), %.2f us per block step, "
           "%u blocks awake at most, %llu woken, water %s%s%s\n",
           CASE_NAMES[which], step, water.active_count == 0 ? " (at rest)" : "",
           seconds * 1e6 / (step > 0 ? step : 1), worst * 1e3,
           block_steps > 0 ? seconds * 1e6 / (double)block_steps : 0.0, most,
           (unsigned long long)water.wakes, kept ? "kept" : "LOST", out,
           ok ? "" : " (out of memory)");
    water_free(&water);
    map_free(&map);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    int size = argc > 1 ? atoi(argv[1]) : 256;
    int steps = argc > 2 ? atoi(argv[2]) : 5000;
    bool open = argc > 3 && strcmp(argv[3], "open") == 0;
    if (size < MAP_MIN_SIZE || size > MAP_MAX_SIZE || steps <= 0) {
        fprintf(stderr, "usage: bench_water [map_size %d..%d] [steps] [open]\n", MAP_MIN_SIZE,
                MAP_MAX_SIZE);
        return EXIT_FAILURE;
    }
    printf("map %dx%d, at most %d steps, edges %s\n", size, size, steps, open ? "open" : "sealed");
    int failures = 0;
    for (int which = 0; which < CASE_COUNT; which++) {
        failures += run_case((Case)which, size, steps, open);
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
