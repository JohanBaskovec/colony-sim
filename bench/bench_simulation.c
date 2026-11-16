/*
 * Simulation benchmark: runs a generated colony for some in-game days and
 * reports the cost of a tick (average and worst). Not part of the test suite.
 *
 *     bench_simulation [colonists] [days] [scenario] [map_size] [seed] [flat]
 *
 * `flat`: a world without relief (one level), to compare with older versions.
 *
 * Scenarios: see core/scenario.h (0 = none, 1 = gathering, 2 = unreachable
 * jobs, 3 = construction).
 */
#include "core/game.h"
#include "core/scenario.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Tiles of all the gathering zones. */
static int zone_tiles(const Game *game)
{
    int tiles = 0;
    for (int i = 0; i < GATHER_ZONE_MAX; i++) {
        const GatherZone *zone = gather_zone_get(&game->gather_zones, i);
        tiles += zone != NULL ? zone->cell_count : 0;
    }
    return tiles;
}

static double now_seconds(void)
{
    struct timespec time;
    timespec_get(&time, TIME_UTC);
    return (double)time.tv_sec + (double)time.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    int colonists = argc > 1 ? atoi(argv[1]) : 32;
    int days = argc > 2 ? atoi(argv[2]) : 5;
    int scenario = argc > 3 ? atoi(argv[3]) : 1;
    int size = argc > 4 ? atoi(argv[4]) : 200;
    uint64_t seed = argc > 5 ? strtoull(argv[5], NULL, 10) : 42;
    bool flat = argc > 6 && strcmp(argv[6], "flat") == 0;

    NewGameParams params;
    game_default_params(&params);
    params.world.seed = seed;
    params.world.width = size;
    params.world.height = size;
    params.world.flat = flat;
    params.colonist_count = colonists;

    Game game;
    if (!game_init(&game, &params)) {
        fprintf(stderr, "initialization failed\n");
        return EXIT_FAILURE;
    }
    if (!scenario_apply(&game, (Scenario)scenario)) {
        fprintf(stderr, "unknown scenario %d\n", scenario);
        game_shutdown(&game);
        return EXIT_FAILURE;
    }
    int orders = zone_tiles(&game);
    int sites = game.constructions.used_count;

    long ticks = (long)days * TICKS_PER_DAY;
    long slow_ticks = 0; /* Ticks over one millisecond. */
    double worst = 0.0;
    double begin = now_seconds();
    for (long i = 0; i < ticks; i++) {
        double tick_begin = now_seconds();
        game_tick(&game);
        double elapsed = now_seconds() - tick_begin;
        if (elapsed > worst) {
            worst = elapsed;
        }
        if (elapsed > 1e-3) {
            slow_ticks++;
        }
    }
    double seconds = now_seconds() - begin;

    printf("map %dx%dx%d, seed %llu, scenario %d: %u colonists, %d days (%ld ticks)\n", size,
           size, game.map.depth, (unsigned long long)seed, scenario, game.colonist_pool.count,
           days, ticks);
    printf("  total %.2f s, %.2f us/tick, worst tick %.2f ms, %ld ticks over 1 ms\n", seconds,
           seconds * 1e6 / (double)ticks, worst * 1e3, slow_ticks);
    printf("  zone tiles %d, items:", orders);
    for (int type = 1; type < ITEM_TYPE_COUNT; type++) {
        if (game.items.totals[type] > 0) {
            printf(" %ld %s", game.items.totals[type], item_def((ItemType)type)->id);
        }
    }
    printf("\n");
    printf("  construction sites %d planned, %d left; structure tiles %d\n", sites,
           game.constructions.used_count,
           (int)map_tile_count(&game.map) - game.map.structure_counts[STRUCTURE_NONE]);
    printf("  state hash %016llx\n", (unsigned long long)game_hash(&game));

    game_shutdown(&game);
    return EXIT_SUCCESS;
}
