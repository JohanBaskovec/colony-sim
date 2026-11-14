#include "core/game.h"

#include "test_framework.h"

static bool init_small_game(Game *game, uint64_t seed)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = seed;
    params.world.width = 64;
    params.world.height = 64;
    return game_init(game, &params);
}

static void test_init_and_shutdown(void)
{
    Game game;
    REQUIRE(init_small_game(&game, 9));
    CHECK_EQ_UINT(game.seed, 9);
    CHECK_EQ_INT(game.map.width, 64);
    CHECK_EQ_INT(game.clock.speed, GAME_SPEED_NORMAL);
    CHECK_EQ_UINT(game.colonist_pool.count, GAME_DEFAULT_COLONIST_COUNT);
    game_shutdown(&game);
    CHECK(game.map.terrain == NULL);
}

static void test_init_failure_leaves_game_safe_to_shut_down(void)
{
    Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.width = 1;
    CHECK(!game_init(&game, &params));
    game_shutdown(&game);
}

static void test_update_advances_ticks(void)
{
    Game game;
    REQUIRE(init_small_game(&game, 1));
    uint64_t start = game.clock.tick;
    int ran = game_update(&game, 0.25);
    CHECK_EQ_INT(ran, TICKS_PER_SECOND / 4);
    CHECK_EQ_UINT(game.clock.tick, start + (uint64_t)ran);

    game_clock_toggle_pause(&game.clock);
    CHECK_EQ_INT(game_update(&game, 1.0), 0);
    CHECK_EQ_UINT(game.clock.tick, start + (uint64_t)ran);
    game_shutdown(&game);
}

static void test_plants_grow_over_time(void)
{
    Game game;
    REQUIRE(init_small_game(&game, 3));
    Map *map = &game.map;

    /* Controlled setup on known terrain. */
    size_t fertile = map_index(map, 1, 1);
    size_t barren = map_index(map, 2, 1);
    map->rock[fertile] = ROCK_NONE;
    map->rock[barren] = ROCK_NONE;
    map->terrain[fertile] = TERRAIN_SOIL;
    map->terrain[barren] = TERRAIN_SAND;
    map->plants[fertile].type = PLANT_GRASS;
    map->plants[fertile].growth = 0.1f;
    map->plants[barren].type = PLANT_GRASS;
    map->plants[barren].growth = 0.1f;

    for (int i = 0; i < TICKS_PER_DAY; i++) {
        game_tick(&game);
    }

    float expected = 0.1f + plant_def(PLANT_GRASS)->growth_per_day *
                                terrain_def(TERRAIN_SOIL)->fertility;
    CHECK_NEAR(map->plants[fertile].growth, expected, 1e-4);
    /* Sand is below the minimum fertility of grass: no growth. */
    CHECK_NEAR(map->plants[barren].growth, 0.1, 1e-6);

    /* Growth saturates at 1. */
    for (int i = 0; i < TICKS_PER_DAY * 10; i++) {
        game_tick(&game);
    }
    CHECK_NEAR(map->plants[fertile].growth, 1.0, 1e-6);
    game_shutdown(&game);
}

static void test_simulation_is_deterministic(void)
{
    Game a, b;
    REQUIRE(init_small_game(&a, 42));
    REQUIRE(init_small_game(&b, 42));
    for (int i = 0; i < TICKS_PER_DAY; i++) {
        game_tick(&a);
        game_tick(&b);
    }
    CHECK_EQ_UINT(map_hash(&a.map), map_hash(&b.map));
    CHECK_EQ_UINT(game_hash(&a), game_hash(&b));
    CHECK_EQ_UINT(a.clock.tick, b.clock.tick);
    game_shutdown(&a);
    game_shutdown(&b);
}

static void test_hash_changes_as_simulation_runs(void)
{
    Game game;
    REQUIRE(init_small_game(&game, 8));
    uint64_t before = game_hash(&game);
    for (int i = 0; i < 600; i++) {
        game_tick(&game);
    }
    CHECK(game_hash(&game) != before);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_init_and_shutdown);
    RUN_TEST(test_init_failure_leaves_game_safe_to_shut_down);
    RUN_TEST(test_update_advances_ticks);
    RUN_TEST(test_plants_grow_over_time);
    RUN_TEST(test_simulation_is_deterministic);
    RUN_TEST(test_hash_changes_as_simulation_runs);
    return test_summary();
}
