#include "core/game.h"

#include "test_framework.h"

#include <stdlib.h>

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
    map_set_rock(map, 1, 1, ROCK_NONE);
    map_set_rock(map, 2, 1, ROCK_NONE);
    map_set_terrain(map, 1, 1, TERRAIN_SOIL);
    map_set_terrain(map, 2, 1, TERRAIN_SAND);
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

/* Growth is spread over the ticks of an interval: a tick only touches a
 * slice of the map, and after a whole interval every plant grew once. */
static void test_plant_growth_is_spread_over_ticks(void)
{
    Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 7;
    params.world.width = 64;
    params.world.height = 64;
    params.colonist_count = 0; /* Nobody eats the plants meanwhile. */
    REQUIRE(game_init(&game, &params));
    Map *map = &game.map;
    size_t count = map_tile_count(map);
    float *before = malloc(count * sizeof(*before));
    REQUIRE(before != NULL);
    size_t growing = 0;
    for (size_t i = 0; i < count; i++) {
        const Plant *plant = &map->plants[i];
        before[i] = plant->growth;
        if (plant->type != PLANT_NONE && plant->growth < 1.0f &&
            terrain_def((Terrain)map->terrain[i])->fertility >=
                plant_def((PlantType)plant->type)->min_fertility) {
            growing++;
        }
    }
    REQUIRE(growing > 100);

    size_t grown = 0, most_in_one_tick = 0;
    for (int tick = 0; tick < GAME_PLANT_UPDATE_INTERVAL; tick++) {
        game_tick(&game);
        size_t now = 0;
        for (size_t i = 0; i < count; i++) {
            if (map->plants[i].growth != before[i]) {
                now++;
            }
        }
        most_in_one_tick = now - grown > most_in_one_tick ? now - grown : most_in_one_tick;
        grown = now;
    }
    CHECK_EQ_UINT(grown, growing);
    CHECK(most_in_one_tick <= count / GAME_PLANT_UPDATE_INTERVAL + 1);

    /* Not twice: one more interval gives each plant one more step. */
    size_t index = 0;
    while (index < count && (map->plants[index].type == PLANT_NONE ||
                             map->plants[index].growth == before[index] ||
                             map->plants[index].growth >= 0.9f)) {
        index++;
    }
    REQUIRE(index < count);
    float step = map->plants[index].growth - before[index];
    float after_one = map->plants[index].growth;
    for (int tick = 0; tick < GAME_PLANT_UPDATE_INTERVAL; tick++) {
        game_tick(&game);
    }
    CHECK_NEAR(map->plants[index].growth - after_one, step, 1e-6);
    free(before);
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

static void test_hash_covers_skills_and_carried_condition(void)
{
    Game game;
    REQUIRE(init_small_game(&game, 8));
    Colonist *colonist = game_colonist(&game, game_colonist_id_at(&game, 0));
    REQUIRE(colonist != NULL);
    uint64_t before = game_hash(&game);
    int level = colonist_skill_level(colonist, SKILL_GATHERING);
    colonist_set_skill_level(colonist, SKILL_GATHERING, level == 0 ? 1 : level - 1);
    uint64_t skilled = game_hash(&game);
    CHECK(skilled != before);
    colonist_gain_skill_xp(colonist, SKILL_GATHERING, 10.0f);
    uint64_t learned = game_hash(&game);
    CHECK(learned != skilled);
    colonist_set_strength(colonist, colonist->strength == 5 ? 6 : 5);
    CHECK(game_hash(&game) != learned);

    colonist->carried = item_stack_make(ITEM_WOOD, 2, ITEM_QUALITY_FINE, 60);
    uint64_t carrying = game_hash(&game);
    colonist->carried.quality = ITEM_QUALITY_ROUGH;
    CHECK(game_hash(&game) != carrying);
    colonist->carried.quality = ITEM_QUALITY_FINE;
    colonist->carried.hit_points = 30;
    CHECK(game_hash(&game) != carrying);
    colonist->carried = (ItemStack){.type = ITEM_NONE};
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_init_and_shutdown);
    RUN_TEST(test_init_failure_leaves_game_safe_to_shut_down);
    RUN_TEST(test_update_advances_ticks);
    RUN_TEST(test_plants_grow_over_time);
    RUN_TEST(test_plant_growth_is_spread_over_ticks);
    RUN_TEST(test_simulation_is_deterministic);
    RUN_TEST(test_hash_changes_as_simulation_runs);
    RUN_TEST(test_hash_covers_skills_and_carried_condition);
    return test_summary();
}
