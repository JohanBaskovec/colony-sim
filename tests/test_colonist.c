#include "core/game.h"
#include "core/i18n.h"

#include "test_framework.h"

/* A small game on a flat, empty soil map with no starting colonists. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 5;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = 0; i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->rock[i] = ROCK_NONE;
        map->plants[i].type = PLANT_NONE;
        map->plants[i].growth = 0.0f;
    }
    /* Noon: nobody wants to sleep because of the night. */
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

static void run_ticks(Game *game, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        game_tick(game);
    }
}

static void test_spawn_requires_passable_tile(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    game.map.terrain[map_index(&game.map, 3, 3)] = TERRAIN_DEEP_WATER;
    CHECK(entity_id_is_none(game_spawn_colonist(&game, 3, 3)));
    CHECK(entity_id_is_none(game_spawn_colonist(&game, -1, 3)));

    EntityId id = game_spawn_colonist(&game, 4, 4);
    const Colonist *colonist = game_colonist_const(&game, id);
    REQUIRE(colonist != NULL);
    CHECK_EQ_INT(colonist->x, 4);
    CHECK(colonist->name[0] != '\0');
    CHECK(colonist->age >= 18 && colonist->age <= 60);
    CHECK(game_tile_has_colonist(&game, 4, 4));
    game_shutdown(&game);
}

static void test_names_are_unique(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId ids[20];
    for (int i = 0; i < 20; i++) {
        ids[i] = game_spawn_colonist(&game, i, 0);
        REQUIRE(!entity_id_is_none(ids[i]));
    }
    for (int i = 0; i < 20; i++) {
        for (int j = i + 1; j < 20; j++) {
            CHECK(strcmp(game.colonists[ids[i].index].name, game.colonists[ids[j].index].name) != 0);
        }
    }
    game_shutdown(&game);
}

static void test_remove_invalidates_handle(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId id = game_spawn_colonist(&game, 2, 2);
    CHECK(game_remove_colonist(&game, id));
    CHECK(game_colonist(&game, id) == NULL);
    CHECK(!game_remove_colonist(&game, id));
    CHECK(!game_tile_has_colonist(&game, 2, 2));
    game_shutdown(&game);
}

static void test_starting_colonists_on_distinct_passable_tiles(void)
{
    for (uint64_t seed = 1; seed <= 6; seed++) {
        Game game;
        NewGameParams params;
        game_default_params(&params);
        params.world.seed = seed;
        params.world.width = 96;
        params.world.height = 96;
        REQUIRE(game_init(&game, &params));
        CHECK_EQ_UINT(game.colonist_pool.count, GAME_DEFAULT_COLONIST_COUNT);

        for (uint32_t a = 0; a < game_colonist_slot_count(&game); a++) {
            const Colonist *ca = &game.colonists[a];
            CHECK(map_is_passable(&game.map, ca->x, ca->y));
            for (uint32_t b = a + 1; b < game_colonist_slot_count(&game); b++) {
                const Colonist *cb = &game.colonists[b];
                CHECK(ca->x != cb->x || ca->y != cb->y);
            }
        }
        game_shutdown(&game);
    }
}

static void test_needs_decay_over_time(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = game_colonist(&game, game_spawn_colonist(&game, 10, 10));
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = 1.0f;
    colonist->needs[NEED_REST] = 1.0f;

    run_ticks(&game, TICKS_PER_HOUR);
    CHECK_NEAR(colonist->needs[NEED_FOOD], 1.0 - NEED_FOOD_DECAY_PER_TICK * TICKS_PER_HOUR, 1e-4);
    CHECK_NEAR(colonist->needs[NEED_REST], 1.0 - NEED_REST_DECAY_PER_TICK * TICKS_PER_HOUR, 1e-4);
    game_shutdown(&game);
}

static void test_idle_colonist_wanders(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId id = game_spawn_colonist(&game, 20, 20);
    Colonist *colonist = game_colonist(&game, id);
    colonist->needs[NEED_FOOD] = 1.0f;
    colonist->needs[NEED_REST] = 1.0f;

    bool moved = false;
    for (int i = 0; i < 2000 && !moved; i++) {
        game_tick(&game);
        moved = colonist->x != 20 || colonist->y != 20;
    }
    CHECK(moved);
    CHECK(abs(colonist->x - 20) <= COLONIST_WANDER_RADIUS);
    CHECK(abs(colonist->y - 20) <= COLONIST_WANDER_RADIUS);
    game_shutdown(&game);
}

static void test_hungry_colonist_walks_to_food_and_eats(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    size_t bush = map_index(map, 30, 20);
    map->plants[bush].type = PLANT_BUSH;
    map->plants[bush].growth = 1.0f;

    Colonist *colonist = game_colonist(&game, game_spawn_colonist(&game, 20, 20));
    colonist->needs[NEED_FOOD] = 0.2f;
    colonist->needs[NEED_REST] = 1.0f;

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_EAT);
    CHECK_EQ_INT(colonist->target.x, 30);

    /* ~10 tiles at 3.5 tiles/s plus eating time. */
    run_ticks(&game, 10 * TICKS_PER_SECOND / 3 + COLONIST_EAT_TICKS + 60);
    CHECK_EQ_INT(colonist->x, 30);
    CHECK_EQ_INT(colonist->y, 20);
    CHECK(colonist->needs[NEED_FOOD] > 0.55f);
    CHECK_NEAR(map->plants[bush].growth, PLANT_GROWTH_AFTER_EATEN, 1e-6);
    CHECK(colonist->task != TASK_EAT); /* No other food around: gives up and idles. */
    game_shutdown(&game);
}

static void test_hungry_colonist_without_food_retries_later(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = game_colonist(&game, game_spawn_colonist(&game, 20, 20));
    colonist->needs[NEED_FOOD] = 0.2f;
    game_tick(&game);
    CHECK(colonist->task != TASK_EAT);
    CHECK(colonist->food_search_cooldown > 0);
    game_shutdown(&game);
}

static void test_tired_colonist_sleeps_and_recovers(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = game_colonist(&game, game_spawn_colonist(&game, 20, 20));
    colonist->needs[NEED_FOOD] = 1.0f;
    colonist->needs[NEED_REST] = 0.2f;

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_SLEEP);
    run_ticks(&game, TICKS_PER_HOUR);
    CHECK(colonist->needs[NEED_REST] > 0.2f);
    CHECK_EQ_INT(colonist->x, 20); /* Sleeps in place. */

    /* Wakes up once fully rested (8 hours from 0). */
    run_ticks(&game, 8 * TICKS_PER_HOUR);
    CHECK(colonist->task != TASK_SLEEP);
    game_shutdown(&game);
}

static void test_colonists_sleep_at_night(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    game.clock.tick = 23 * TICKS_PER_HOUR;
    Colonist *colonist = game_colonist(&game, game_spawn_colonist(&game, 20, 20));
    colonist->needs[NEED_FOOD] = 1.0f;
    colonist->needs[NEED_REST] = 0.7f;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_SLEEP);
    game_shutdown(&game);
}

static void test_position_is_interpolated_while_walking(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Test", 30, 5, 5);
    float x, y;
    colonist_position(&colonist, &x, &y);
    CHECK_NEAR(x, 5.5, 1e-6);
    CHECK_NEAR(y, 5.5, 1e-6);

    colonist.path.steps = malloc(sizeof(TilePos));
    REQUIRE(colonist.path.steps != NULL);
    colonist.path.capacity = 1;
    colonist.path.count = 1;
    colonist.path.steps[0] = (TilePos){6, 5};
    colonist.move_progress = 0.25f;
    CHECK(colonist_is_moving(&colonist));
    colonist_position(&colonist, &x, &y);
    CHECK_NEAR(x, 5.75, 1e-6);
    CHECK_NEAR(y, 5.5, 1e-6);
    colonist_free(&colonist);
}

static void test_picking(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId a = game_spawn_colonist(&game, 10, 10);
    EntityId b = game_spawn_colonist(&game, 12, 10);
    game_spawn_colonist(&game, 30, 30);

    CHECK(entity_id_equal(game_colonist_at(&game, 10.5f, 10.5f), a));
    CHECK(entity_id_equal(game_colonist_at(&game, 12.3f, 10.6f), b));
    CHECK(entity_id_is_none(game_colonist_at(&game, 11.5f, 10.5f)));

    EntityId found[8];
    CHECK_EQ_INT(game_colonists_in_rect(&game, 13.0f, 9.0f, 9.0f, 12.0f, found, 8), 2);
    CHECK_EQ_INT(game_colonists_in_rect(&game, 0.0f, 0.0f, 48.0f, 48.0f, found, 8), 3);
    CHECK_EQ_INT(game_colonists_in_rect(&game, 0.0f, 0.0f, 48.0f, 48.0f, found, 1), 1);
    game_shutdown(&game);
}

static void test_translation_keys(void)
{
    for (int task = 0; task < TASK_COUNT; task++) {
        CHECK(i18n_is_valid_key(colonist_task_name_key((ColonistTask)task)));
    }
    for (int need = 0; need < NEED_COUNT; need++) {
        CHECK(i18n_is_valid_key(need_name_key((NeedType)need)));
        CHECK(need_level_key((NeedType)need, NEED_LEVEL_OK) == NULL);
        CHECK(i18n_is_valid_key(need_level_key((NeedType)need, NEED_LEVEL_LOW)));
        CHECK(i18n_is_valid_key(need_level_key((NeedType)need, NEED_LEVEL_CRITICAL)));
    }
    CHECK_EQ_INT(need_level(0.5f), NEED_LEVEL_OK);
    CHECK_EQ_INT(need_level(0.2f), NEED_LEVEL_LOW);
    CHECK_EQ_INT(need_level(0.05f), NEED_LEVEL_CRITICAL);
    CHECK(colonist_name_count() >= 32);
}

int main(void)
{
    RUN_TEST(test_spawn_requires_passable_tile);
    RUN_TEST(test_names_are_unique);
    RUN_TEST(test_remove_invalidates_handle);
    RUN_TEST(test_starting_colonists_on_distinct_passable_tiles);
    RUN_TEST(test_needs_decay_over_time);
    RUN_TEST(test_idle_colonist_wanders);
    RUN_TEST(test_hungry_colonist_walks_to_food_and_eats);
    RUN_TEST(test_hungry_colonist_without_food_retries_later);
    RUN_TEST(test_tired_colonist_sleeps_and_recovers);
    RUN_TEST(test_colonists_sleep_at_night);
    RUN_TEST(test_position_is_interpolated_while_walking);
    RUN_TEST(test_picking);
    RUN_TEST(test_translation_keys);
    return test_summary();
}
