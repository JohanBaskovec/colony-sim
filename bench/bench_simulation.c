/*
 * Simulation benchmark: runs a generated colony for some in-game days and
 * reports the cost of a tick (average and worst). Not part of the test suite.
 *
 *     bench_simulation [colonists] [days] [scenario] [map_size] [seed]
 *
 * Scenarios: 0 = colonists left alone (needs and wandering only);
 * 1 = a gathering zone over the whole map (grass cut, twigs, branches and
 * berries gathered, forever), a zone gathering stones forever next to the
 * colony, two stockpiles (work, hauling, and many jobs that cannot be reached);
 * 2 = only jobs that nobody can reach: a zone gathering the branches of the
 * trees growing where the colonists cannot walk to (other islands);
 * 3 = construction: an enclosure of grass walls with a branch screen and
 * beds inside, planned next to the colony, with a gathering zone around it
 * (grass, branches, twigs) and a stockpile: gathering, drying, hauling to the
 * sites, building, sleeping in the beds and refreshing them.
 */
#include "core/game.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void gather_forever(Game *game, int zone, GatherResource resource)
{
    gather_zone_set_enabled(&game->gather_zones, zone, resource, true);
    gather_zone_set_mode(&game->gather_zones, zone, resource, GATHER_FOREVER);
}

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
        const Colonist *first = game_colonist_const(&game, game_colonist_id_at(&game, 0));
        if (first != NULL) {
            int x = first->x, y = first->y;
            /* Stones forever: keeps some colonists busy to the end. */
            int stones = gather_zone_create(&game.gather_zones, &game.map, x - 14, y + 8, x - 10,
                                            y + 12);
            gather_forever(&game, stones, GATHER_COBBLES);
            gather_forever(&game, stones, GATHER_FLINT);
            stockpile_create(&game.stockpiles, &game.map, x - 6, y - 6, x + 6, y + 6);
            int better =
                stockpile_create(&game.stockpiles, &game.map, x + 10, y - 4, x + 16, y + 4);
            stockpile_set_priority(&game.stockpiles, better, 2);
        }
        /* Everything else the land offers, on the whole map. */
        int land = gather_zone_create(&game.gather_zones, &game.map, 0, 0, size - 1, size - 1);
        gather_forever(&game, land, GATHER_GRASS);
        gather_forever(&game, land, GATHER_TWIGS);
        gather_forever(&game, land, GATHER_BRANCHES);
        gather_forever(&game, land, GATHER_BERRIES);
        gather_zone_set_ripe_only(&game.gather_zones, land, GATHER_BERRIES, false);
    }
    if (scenario == 2) {
        const Colonist *first = game_colonist_const(&game, game_colonist_id_at(&game, 0));
        regions_update(&game.regions, &game.map);
        int land = gather_zone_create(&game.gather_zones, &game.map, 0, 0, size - 1, size - 1);
        gather_forever(&game, land, GATHER_BRANCHES);
        uint32_t home = first != NULL
                            ? regions_at(&game.regions, &game.map, first->x, first->y)
                            : REGION_NONE;
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                uint32_t region = regions_at(&game.regions, &game.map, x, y);
                if (region == home) {
                    gather_zones_remove_rect(&game.gather_zones, x, y, x, y);
                }
            }
        }
    }
    if (scenario == 3) {
        const Colonist *first = game_colonist_const(&game, game_colonist_id_at(&game, 0));
        if (first != NULL) {
            int x = first->x, y = first->y;
            stockpile_create(&game.stockpiles, &game.map, x - 8, y - 3, x - 3, y + 3);
            int land = gather_zone_create(&game.gather_zones, &game.map, x - 30, y - 30, x + 30,
                                          y + 30);
            gather_forever(&game, land, GATHER_GRASS);
            gather_forever(&game, land, GATHER_BRANCHES);
            gather_forever(&game, land, GATHER_TWIGS);
            /* The screen first, then the walls around it (7 × 6 tiles), then
             * two rows of beds inside. */
            game_place_structure(&game, STRUCTURE_DOOR, x + 7, y + 3, DIRECTION_SOUTH);
            game_place_structure_rect(&game, STRUCTURE_WALL, DIRECTION_SOUTH, x + 4, y - 2,
                                      x + 10, y + 3);
            game_place_structure_rect(&game, STRUCTURE_GRASS_BED, DIRECTION_EAST, x + 5, y - 1,
                                      x + 8, y + 1);
        }
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

    printf("map %dx%d, seed %llu, scenario %d: %u colonists, %d days (%ld ticks)\n", size, size,
           (unsigned long long)seed, scenario, game.colonist_pool.count, days, ticks);
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
