/*
 * Simulation benchmark: runs a generated colony for some in-game days and
 * reports the cost of a tick in processor time (steady on a busy machine):
 * average, percentiles, and the slowest ticks with their number and in-game
 * time (the simulation is deterministic: a slow tick can be replayed, e.g.
 * under perf). Not part of the test suite.
 *
 *     bench_simulation [colonists] [days] [scenario] [map_size] [seed] [flat] [fog] [wildlife]
 *
 * `flat`: a world without relief (one level), to compare with older versions.
 * `fog`, `fog-revealed`: with the fog of war. `wildlife`: with wild animals
 * (core/animal.h), or a number: their density (1 as played). `-` skips an
 * argument.
 *
 * Profiling a slow tick: BENCH_TIMESTAMPS=1 perf record -g -k CLOCK_MONOTONIC
 * bench_simulation ..., then perf report --time with the times printed.
 *
 * Scenarios: see core/scenario.h (0 = none, 1 = gathering, 2 = unreachable
 * jobs, 3 = construction, 4 = feuds).
 */
#define _POSIX_C_SOURCE 199309L /* clock_gettime(). */

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

/* How the animals fare: how many of each species, what they are doing, how
 * their needs are on average and at worst. */
static void print_animals(const Game *game)
{
    const Animals *animals = &game->animals;
    int species[ANIMAL_SPECIES_COUNT] = {0};
    int tasks[ANIMAL_TASK_COUNT] = {0};
    double sum[ANIMAL_NEED_COUNT] = {0.0};
    float lowest[ANIMAL_NEED_COUNT] = {1.0f, 1.0f, 1.0f};
    int starving = 0, parched = 0, count = 0;
    for (uint32_t slot = 0; slot < animals_slot_count(animals); slot++) {
        if (entity_id_is_none(animals_id_at(animals, slot))) {
            continue;
        }
        const Animal *animal = &animals->list[slot];
        count++;
        species[animal->species]++;
        tasks[animal->task]++;
        for (int need = 0; need < ANIMAL_NEED_COUNT; need++) {
            sum[need] += animal->needs[need];
            lowest[need] = animal->needs[need] < lowest[need] ? animal->needs[need] : lowest[need];
        }
        starving += animal->needs[ANIMAL_NEED_FOOD] < ANIMAL_URGENT_NEED ? 1 : 0;
        parched += animal->needs[ANIMAL_NEED_WATER] < ANIMAL_URGENT_NEED ? 1 : 0;
    }
    printf("  animals %d:", count);
    for (int i = 0; i < ANIMAL_SPECIES_COUNT; i++) {
        printf(" %d %s", species[i], animal_def((AnimalSpecies)i)->id);
    }
    printf("\n    idle %d, wandering %d, eating %d, drinking %d, asleep %d, fleeing %d\n",
           tasks[ANIMAL_TASK_IDLE], tasks[ANIMAL_TASK_WANDER], tasks[ANIMAL_TASK_EAT],
           tasks[ANIMAL_TASK_DRINK], tasks[ANIMAL_TASK_SLEEP], tasks[ANIMAL_TASK_FLEE]);
    printf("    food %.2f (lowest %.2f, %d starving), water %.2f (lowest %.2f, %d parched), rest "
           "%.2f (lowest %.2f)\n",
           sum[ANIMAL_NEED_FOOD] / count, lowest[ANIMAL_NEED_FOOD], starving,
           sum[ANIMAL_NEED_WATER] / count, lowest[ANIMAL_NEED_WATER], parched,
           sum[ANIMAL_NEED_REST] / count, lowest[ANIMAL_NEED_REST]);
}

/* The slowest ticks listed. */
#define SLOWEST_SHOWN 10

typedef struct TickTime {
    long tick;
    double seconds;
    double start, end; /* CLOCK_MONOTONIC around the tick (see BENCH_TIMESTAMPS). */
} TickTime;

static int compare_slowest_first(const void *a, const void *b)
{
    const TickTime *x = a, *y = b;
    if (x->seconds != y->seconds) {
        return x->seconds > y->seconds ? -1 : 1;
    }
    return x->tick < y->tick ? -1 : (x->tick > y->tick);
}

/* Duration of the tick at a share of the ticks sorted slowest first: 0.01 is
 * the 99th percentile. */
static double percentile(const TickTime *sorted, long count, double share)
{
    long index = (long)((double)count * share);
    return sorted[index < count ? index : count - 1].seconds;
}

static double now_seconds(void)
{
    struct timespec time;
    timespec_get(&time, TIME_UTC);
    return (double)time.tv_sec + (double)time.tv_nsec * 1e-9;
}

/* The clock perf can timestamp its samples with (perf record -k CLOCK_MONOTONIC). */
static double monotonic_seconds(void)
{
#ifdef CLOCK_MONOTONIC
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) == 0) {
        return (double)time.tv_sec + (double)time.tv_nsec * 1e-9;
    }
#endif
    return 0.0;
}

/* Processor time used by this thread: unlike the wall clock, it does not count
 * the time the system ran other programs, so ticks are measured as well on a
 * busy machine (the wall clock where this clock is missing). */
static double cpu_seconds(void)
{
#ifdef CLOCK_THREAD_CPUTIME_ID
    struct timespec time;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) == 0) {
        return (double)time.tv_sec + (double)time.tv_nsec * 1e-9;
    }
#endif
    return now_seconds();
}

int main(int argc, char **argv)
{
    int colonists = argc > 1 ? atoi(argv[1]) : 32;
    int days = argc > 2 ? atoi(argv[2]) : 5;
    int scenario = argc > 3 ? atoi(argv[3]) : 1;
    int size = argc > 4 ? atoi(argv[4]) : 200;
    uint64_t seed = argc > 5 ? strtoull(argv[5], NULL, 10) : 42;
    bool flat = argc > 6 && strcmp(argv[6], "flat") == 0;
    /* 7th argument "fog": with the fog of war (settlers plan on what they know); "fog-revealed":
     * the same with the whole map explored from the start (the cost of the fog itself, without
     * settlers searching in vain for what they have not found yet). */
    bool fog_revealed = argc > 7 && strcmp(argv[7], "fog-revealed") == 0;
    bool fog = fog_revealed || (argc > 7 && strcmp(argv[7], "fog") == 0);
    /* 8th argument "wildlife": with wild animals, as many as in a game as played; a number:
     * that many times more. */
    float wildlife = 0.0f;
    if (argc > 8 && strcmp(argv[8], "wildlife") == 0) {
        wildlife = GAME_WILDLIFE_DENSITY;
    } else if (argc > 8) {
        wildlife = (float)atof(argv[8]);
    }

    NewGameParams params;
    game_default_params(&params);
    params.world.seed = seed;
    params.world.width = size;
    params.world.height = size;
    params.world.flat = flat;
    params.colonist_count = colonists;
    params.wildlife = wildlife;

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
    if (fog && !game_set_fog(&game, true)) {
        fprintf(stderr, "cannot start the fog of war\n");
        game_shutdown(&game);
        return EXIT_FAILURE;
    }
    if (fog_revealed) {
        game_reveal_map(&game);
        game_tick(&game); /* The known world catches up. */
    }
    int orders = zone_tiles(&game);
    int sites = game.constructions.used_count;

    long ticks = (long)days * TICKS_PER_DAY;
    if (ticks <= 0) {
        fprintf(stderr, "nothing to run\n");
        game_shutdown(&game);
        return EXIT_FAILURE;
    }
    TickTime *times = malloc((size_t)ticks * sizeof(*times));
    if (times == NULL) {
        fprintf(stderr, "out of memory\n");
        game_shutdown(&game);
        return EXIT_FAILURE;
    }
    /* BENCH_TIMESTAMPS set: the slowest ticks are listed with when they began
     * and ended, so that a perf record taken with -k CLOCK_MONOTONIC can be
     * cut to them (perf report --time begin,end). */
    bool timestamps = getenv("BENCH_TIMESTAMPS") != NULL;
    long slow_ticks = 0; /* Ticks over one millisecond. */
    double begin = now_seconds();
    double cpu_begin = cpu_seconds();
    for (long i = 0; i < ticks; i++) {
        double start = timestamps ? monotonic_seconds() : 0.0;
        double tick_begin = cpu_seconds();
        game_tick(&game);
        double elapsed = cpu_seconds() - tick_begin;
        double end = timestamps ? monotonic_seconds() : 0.0;
        times[i] = (TickTime){(long)game.clock.tick, elapsed, start, end};
        if (elapsed > 1e-3) {
            slow_ticks++;
        }
    }
    double wall = now_seconds() - begin;
    double seconds = cpu_seconds() - cpu_begin;
    qsort(times, (size_t)ticks, sizeof(*times), compare_slowest_first);

    printf("map %dx%dx%d, seed %llu, scenario %d: %u colonists, %d days (%ld ticks)\n", size, size,
           game.map.depth, (unsigned long long)seed, scenario, game.colonist_pool.count, days,
           ticks);
    printf("  processor time %.2f s (wall clock %.2f s), %.2f us/tick, worst tick %.2f ms, %ld "
           "ticks over 1 ms\n",
           seconds, wall, seconds * 1e6 / (double)ticks, times[0].seconds * 1e3, slow_ticks);
    printf("  tick percentiles: p50 %.1f us, p99 %.1f us, p99.9 %.2f ms, p99.99 %.2f ms\n",
           percentile(times, ticks, 0.5) * 1e6, percentile(times, ticks, 0.01) * 1e6,
           percentile(times, ticks, 0.001) * 1e3, percentile(times, ticks, 0.0001) * 1e3);
    printf("  slowest ticks:");
    for (long i = 0; i < SLOWEST_SHOWN && i < ticks; i++) {
        long tick = times[i].tick;
        long minutes = tick % TICKS_PER_DAY * 60 / TICKS_PER_HOUR;
        printf("%s %ld (day %ld %02ld:%02ld) %.1f ms", i % 5 == 0 ? "\n   " : ",", tick,
               tick / TICKS_PER_DAY, minutes / 60, minutes % 60, times[i].seconds * 1e3);
    }
    printf("\n");
    if (timestamps) {
        printf("  slowest ticks, CLOCK_MONOTONIC seconds (perf report --time begin,end):\n");
        for (long i = 0; i < SLOWEST_SHOWN && i < ticks; i++) {
            printf("    %ld: --time %.6f,%.6f\n", times[i].tick, times[i].start, times[i].end);
        }
    }
    free(times);
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
    printf("  connectivity regions rebuilt %u times\n", game.regions.rebuilds);
    if (animals_slot_count(&game.animals) > 0) {
        print_animals(&game);
    }
    printf("  state hash %016llx\n", (unsigned long long)game_hash(&game));

    game_shutdown(&game);
    return EXIT_SUCCESS;
}
