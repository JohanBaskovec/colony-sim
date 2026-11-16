/*
 * Simulation benchmark: runs a generated colony for some in-game days and
 * reports the cost of a tick in processor time (steady on a busy machine):
 * average, percentiles, and the slowest ticks with their number and in-game
 * time (the simulation is deterministic: a slow tick can be replayed, e.g.
 * under perf). Not part of the test suite.
 *
 *     bench_simulation [colonists] [days] [scenario] [map_size] [seed] [flat]
 *
 * `flat`: a world without relief (one level), to compare with older versions.
 *
 * Profiling a slow tick: BENCH_TIMESTAMPS=1 perf record -g -k CLOCK_MONOTONIC
 * bench_simulation ..., then perf report --time with the times printed.
 *
 * Scenarios: see core/scenario.h (0 = none, 1 = gathering, 2 = unreachable
 * jobs, 3 = construction).
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
    printf("  state hash %016llx\n", (unsigned long long)game_hash(&game));

    game_shutdown(&game);
    return EXIT_SUCCESS;
}
