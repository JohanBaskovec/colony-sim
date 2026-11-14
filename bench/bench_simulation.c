/*
 * Simulation benchmark: runs a generated colony for some in-game days and
 * reports the cost of a tick (average and worst). Not part of the test suite.
 *
 *     bench_simulation [colonists] [days] [scenario] [map_size] [seed]
 *
 * Scenarios: 0 = colonists left alone (needs and wandering only);
 * 1 = every plant of the map designated (grass cut, twigs, branches and
 * berries gathered), stones gathered next to the colony, two stockpiles (work,
 * hauling, and many orders that cannot be reached);
 * 2 = only orders that nobody can reach: branches of the trees growing where
 * the colonists cannot walk to (other islands).
 */
#include "core/game.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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

    NewGameParams params;
    game_default_params(&params);
    params.world.seed = seed;
    params.world.width = size;
    params.world.height = size;
    params.colonist_count = colonists;

    Game game;
    if (!game_init(&game, &params)) {
        fprintf(stderr, "initialization failed\n");
        return EXIT_FAILURE;
    }
    if (scenario == 1) {
        /* A tile holds one order: the later ones win where several apply. */
        game_designate_rect(&game, 0, 0, size - 1, size - 1, DESIGNATION_CUT_GRASS);
        game_designate_rect(&game, 0, 0, size - 1, size - 1, DESIGNATION_GATHER_TWIGS);
        game_designate_rect(&game, 0, 0, size - 1, size - 1, DESIGNATION_GATHER_BRANCHES);
        game_designate_rect(&game, 0, 0, size - 1, size - 1, DESIGNATION_HARVEST);
        const Colonist *first = game_colonist_const(&game, game_colonist_id_at(&game, 0));
        if (first != NULL) {
            int x = first->x, y = first->y;
            /* Repeating orders: they keep some colonists busy to the end. */
            game_designate_rect(&game, x - 14, y + 8, x - 10, y + 12,
                                DESIGNATION_GATHER_STONES);
            stockpile_create(&game.stockpiles, &game.map, x - 6, y - 6, x + 6, y + 6);
            int better =
                stockpile_create(&game.stockpiles, &game.map, x + 10, y - 4, x + 16, y + 4);
            stockpile_set_priority(&game.stockpiles, better, 2);
        }
    }
    if (scenario == 2) {
        const Colonist *first = game_colonist_const(&game, game_colonist_id_at(&game, 0));
        regions_update(&game.regions, &game.map);
        uint32_t home = first != NULL
                            ? regions_at(&game.regions, &game.map, first->x, first->y)
                            : REGION_NONE;
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                uint32_t region = regions_at(&game.regions, &game.map, x, y);
                if (region != REGION_NONE && region != home) {
                    game_designate_rect(&game, x, y, x, y, DESIGNATION_GATHER_BRANCHES);
                }
            }
        }
    }
    int orders = game.designations.count;

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

    printf("map %dx%d, seed %llu, scenario %d: %u colonists, %d days (%ld ticks)\n", size, size,
           (unsigned long long)seed, scenario, game.colonist_pool.count, days, ticks);
    printf("  total %.2f s, %.2f us/tick, worst tick %.2f ms, %ld ticks over 1 ms\n", seconds,
           seconds * 1e6 / (double)ticks, worst * 1e3, slow_ticks);
    printf("  orders %d -> %d, items:", orders, game.designations.count);
    for (int type = 1; type < ITEM_TYPE_COUNT; type++) {
        if (game.items.totals[type] > 0) {
            printf(" %ld %s", game.items.totals[type], item_def((ItemType)type)->id);
        }
    }
    printf("\n");
    printf("  state hash %016llx\n", (unsigned long long)game_hash(&game));

    game_shutdown(&game);
    return EXIT_SUCCESS;
}
