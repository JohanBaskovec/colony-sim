#include "core/game.h"
#include "core/i18n.h"

#include "test_framework.h"

#include <math.h>

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

/* Spawns a colonist with every need satisfied. */
static Colonist *spawn_rested(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y));
    if (colonist != NULL) {
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
    }
    return colonist;
}

/* Turns every column from `x0` to the right edge into shallow water. */
static void add_water_band(Map *map, int x0)
{
    for (int y = 0; y < map->height; y++) {
        for (int x = x0; x < map->width; x++) {
            map->terrain[map_index(map, x, y)] = TERRAIN_SHALLOW_WATER;
        }
    }
}

/* Owner of the work reservation on a tile (RESERVATION_NO_OWNER if free). */
static uint8_t work_owner(const Game *game, int x, int y)
{
    return reservations_owner(&game->reservations, RESERVATION_WORK,
                              (TilePos){(int16_t)x, (int16_t)y});
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
            const char *a = game.colonists[ids[i].index].name;
            const char *b = game.colonists[ids[j].index].name;
            CHECK(strcmp(a, b) != 0);
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

static bool is_drinking_spot(const Map *map, int x, int y, void *userdata)
{
    (void)userdata;
    return map_is_drinking_spot(map, x, y);
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
            CHECK(pathfinder_find_nearest(&game.pathfinder, &game.map,
                                          (TilePos){(int16_t)ca->x, (int16_t)ca->y},
                                          is_drinking_spot, NULL, COLONIST_WATER_SEARCH_MAX_COST,
                                          NULL, NULL));
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
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);

    run_ticks(&game, TICKS_PER_HOUR);
    CHECK_NEAR(colonist->needs[NEED_FOOD], 1.0 - NEED_FOOD_DECAY_PER_TICK * TICKS_PER_HOUR, 1e-4);
    CHECK_NEAR(colonist->needs[NEED_WATER], 1.0 - NEED_WATER_DECAY_PER_TICK * TICKS_PER_HOUR,
               1e-4);
    CHECK_NEAR(colonist->needs[NEED_REST], 1.0 - NEED_REST_DECAY_PER_TICK * TICKS_PER_HOUR, 1e-4);
    game_shutdown(&game);
}

static void test_idle_colonist_wanders(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);

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

    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = 0.2f;

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_EAT);
    CHECK_EQ_INT(colonist->target.x, 30);

    /* ~10 tiles at 3.5 tiles/s plus eating time. */
    run_ticks(&game, 10 * TICKS_PER_SECOND / 3 + COLONIST_EAT_TICKS + 60);
    CHECK_EQ_INT(colonist->x, 30);
    CHECK_EQ_INT(colonist->y, 20);
    CHECK(colonist->needs[NEED_FOOD] > 0.55f);
    CHECK_NEAR(map->plants[bush].growth, PLANT_GROWTH_AFTER_PICKING, 1e-6);
    CHECK(colonist->task != TASK_EAT); /* No other food around: gives up and idles. */
    game_shutdown(&game);
}

static void test_hungry_colonist_without_food_retries_later(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = 0.2f;
    game_tick(&game);
    CHECK(colonist->task != TASK_EAT);
    CHECK(colonist->search_cooldown[SEARCH_FOOD] > 0);
    game_shutdown(&game);
}

static void test_thirsty_colonist_walks_to_shore_and_drinks(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_water_band(&game.map, 30);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_WATER] = 0.2f;

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_DRINK);
    /* Stops on the shore instead of wading into the water. */
    CHECK_EQ_INT(colonist->target.x, 29);
    CHECK_EQ_INT(colonist->target.y, 20);

    run_ticks(&game, 9 * TICKS_PER_SECOND / 3 + COLONIST_DRINK_TICKS + 60);
    CHECK_EQ_INT(colonist->x, 29);
    CHECK_NEAR(colonist->needs[NEED_WATER], 1.0, 0.01);
    CHECK(colonist->task != TASK_DRINK);
    game_shutdown(&game);
}

/* Surrounds a tile with rock walls (or removes them), so that a colonist
 * standing on it cannot move and decides on every tick. */
static void set_rock_ring(Map *map, int cx, int cy, RockType rock)
{
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx != 0 || dy != 0) {
                map->rock[map_index(map, cx + dx, cy + dy)] = (uint8_t)rock;
            }
        }
    }
}

static void test_thirsty_colonist_without_water_retries_later(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    set_rock_ring(&game.map, 20, 20, ROCK_GRANITE);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_WATER] = 0.2f;
    game_tick(&game);
    CHECK(colonist->task != TASK_DRINK);
    CHECK_EQ_INT(colonist->search_cooldown[SEARCH_WATER], COLONIST_SEARCH_RETRY_TICKS);
    CHECK_EQ_INT(colonist->search_cooldown[SEARCH_FOOD], 0);

    /* Each further failure waits twice as long, up to a cap. */
    int expected = COLONIST_SEARCH_RETRY_TICKS;
    for (int failure = 1; failure <= COLONIST_SEARCH_MAX_BACKOFF + 1; failure++) {
        run_ticks(&game, colonist->search_cooldown[SEARCH_WATER]);
        CHECK(colonist->task != TASK_DRINK);
        if (failure <= COLONIST_SEARCH_MAX_BACKOFF) {
            expected *= 2;
        }
        CHECK_EQ_INT(colonist->search_cooldown[SEARCH_WATER], expected);
    }

    /* Water becomes reachable: the next search succeeds and resets the backoff. */
    set_rock_ring(&game.map, 20, 20, ROCK_NONE);
    add_water_band(&game.map, 40);
    colonist->needs[NEED_WATER] = 0.2f;
    run_ticks(&game, colonist->search_cooldown[SEARCH_WATER]);
    CHECK_EQ_INT(colonist->task, TASK_DRINK);
    CHECK_EQ_INT(colonist->search_failures[SEARCH_WATER], 0);
    game_shutdown(&game);
}

static void test_most_urgent_need_first(void)
{
    for (int thirstier = 0; thirstier <= 1; thirstier++) {
        Game game;
        REQUIRE(init_flat_game(&game));
        add_water_band(&game.map, 30);
        Plant *bush = &game.map.plants[map_index(&game.map, 10, 20)];
        bush->type = PLANT_BUSH;
        bush->growth = 1.0f;
        Colonist *colonist = spawn_rested(&game, 20, 20);
        REQUIRE(colonist != NULL);
        colonist->needs[NEED_FOOD] = thirstier ? 0.2f : 0.1f;
        colonist->needs[NEED_WATER] = thirstier ? 0.1f : 0.2f;

        game_tick(&game);
        CHECK_EQ_INT(colonist->task, thirstier ? TASK_DRINK : TASK_EAT);
        game_shutdown(&game);
    }
}

static void test_thirst_falls_back_to_food_when_no_water(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Plant *bush = &game.map.plants[map_index(&game.map, 25, 20)];
    bush->type = PLANT_BUSH;
    bush->growth = 1.0f;
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_WATER] = 0.1f;
    colonist->needs[NEED_FOOD] = 0.2f;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_EAT);
    game_shutdown(&game);
}

static void test_severe_thirst_wakes_sleeping_colonist(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_water_band(&game.map, 30);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_REST] = 0.2f;
    colonist->needs[NEED_WATER] = 0.6f;

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_SLEEP);

    colonist->needs[NEED_WATER] = NEED_CRITICAL_THRESHOLD - 0.01f;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_DRINK);
    game_shutdown(&game);
}

static void test_tired_colonist_sleeps_and_recovers(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
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
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_REST] = 0.7f;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_SLEEP);
    game_shutdown(&game);
}

static void test_colonists_drink_before_sleeping(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_water_band(&game.map, 30);
    game.clock.tick = 23 * TICKS_PER_HOUR;
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_REST] = 0.7f;
    colonist->needs[NEED_WATER] = COLONIST_BEDTIME_NEED - 0.05f;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_DRINK);

    /* Once refreshed, goes to bed. */
    run_ticks(&game, 10 * TICKS_PER_SECOND / 3 + COLONIST_DRINK_TICKS + COLONIST_IDLE_MAX_TICKS);
    CHECK_EQ_INT(colonist->task, TASK_SLEEP);
    game_shutdown(&game);
}

/* ---- Work on designations ---------------------------------------------- */

static void add_tree(Map *map, int x, int y)
{
    Plant *tree = &map->plants[map_index(map, x, y)];
    tree->type = PLANT_TREE;
    tree->growth = 1.0f;
}

/* Ticks needed to walk `tiles` tiles on cheap terrain, plus some margin. */
static int walk_ticks(int tiles)
{
    return tiles * TICKS_PER_SECOND * 2 / 3 + 60;
}

static void test_idle_colonist_chops_designated_tree(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_tree(&game.map, 26, 20);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    CHECK_EQ_INT(game_designate_rect(&game, 26, 20, 26, 20, DESIGNATION_CHOP), 1);

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->target.x, 26);
    CHECK_EQ_INT(work_owner(&game, 26, 20), 1);
    CHECK_STR_EQ(colonist_activity_key(colonist), "colonist.task.chop");

    float work = designation_work_needed(&game.designations, &game.map, 26, 20);
    run_ticks(&game, walk_ticks(6) + (int)(work * 0.5f));
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->x, 25); /* Works from the neighboring tile. */
    CHECK_EQ_INT(colonist->y, 20);
    CHECK(designation_progress_fraction(&game.designations, &game.map, 26, 20) > 0.0f);

    run_ticks(&game, (int)(work * 0.5f));
    CHECK_EQ_INT(game.map.plants[map_index(&game.map, 26, 20)].type, PLANT_NONE);
    CHECK_EQ_INT(items_get(&game.items, 26, 20).type, ITEM_WOOD); /* The wood stays there. */
    CHECK_EQ_INT((int)game.items.totals[ITEM_WOOD], plant_def(PLANT_TREE)->cut_yield);
    CHECK_EQ_INT(designation_get(&game.designations, 26, 20), DESIGNATION_NONE);
    CHECK(colonist->task != TASK_WORK);
    game_shutdown(&game);
}

static void test_mining_opens_a_passage(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    for (int y = 0; y < game.map.height; y++) {
        game.map.rock[map_index(&game.map, 25, y)] = ROCK_SANDSTONE;
    }
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    CHECK(!pathfinder_find(&game.pathfinder, &game.map, (TilePos){20, 20}, (TilePos){30, 20},
                           PATH_NO_COST_LIMIT, NULL));
    game_designate_rect(&game, 25, 20, 25, 20, DESIGNATION_MINE);

    run_ticks(&game, walk_ticks(5) + (int)rock_def(ROCK_SANDSTONE)->mine_work);
    CHECK(map_is_passable(&game.map, 25, 20));
    CHECK(pathfinder_find(&game.pathfinder, &game.map, (TilePos){20, 20}, (TilePos){30, 20},
                          PATH_NO_COST_LIMIT, NULL));
    game_shutdown(&game);
}

static void test_one_worker_per_designation(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_tree(&game.map, 26, 20);
    add_tree(&game.map, 26, 30);
    Colonist *a = spawn_rested(&game, 20, 20);
    Colonist *b = spawn_rested(&game, 20, 22);
    REQUIRE(a != NULL && b != NULL);
    game_designate_rect(&game, 26, 20, 26, 20, DESIGNATION_CHOP);

    game_tick(&game);
    CHECK_EQ_INT(a->task, TASK_WORK);
    CHECK(b->task != TASK_WORK); /* The only job is taken. */

    /* A second job goes to the free colonist, even though it is farther. */
    game_designate_rect(&game, 26, 30, 26, 30, DESIGNATION_CHOP);
    for (int i = 0; i < walk_ticks(2) && b->task != TASK_WORK; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(b->task, TASK_WORK);
    CHECK_EQ_INT(b->target.y, 30);
    CHECK_EQ_INT(a->target.y, 20);
    game_shutdown(&game);
}

static void test_hungry_worker_leaves_work_and_resumes_later(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_tree(&game.map, 22, 20);
    Plant *bush = &game.map.plants[map_index(&game.map, 15, 20)];
    bush->type = PLANT_BUSH;
    bush->growth = 1.0f;
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    game_designate_rect(&game, 22, 20, 22, 20, DESIGNATION_CHOP);
    run_ticks(&game, walk_ticks(1) + 300);
    REQUIRE(colonist->task == TASK_WORK);
    float progress = designation_progress_fraction(&game.designations, &game.map, 22, 20);
    CHECK(progress > 0.0f);

    colonist->needs[NEED_FOOD] = 0.2f;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_EAT);
    CHECK_EQ_INT(work_owner(&game, 22, 20), RESERVATION_NO_OWNER); /* Released. */
    CHECK(designation_progress_fraction(&game.designations, &game.map, 22, 20) >= progress);

    /* Back to work after eating, from where it stopped. */
    for (int i = 0; i < walk_ticks(8) + COLONIST_EAT_TICKS && colonist->task != TASK_WORK; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    game_shutdown(&game);
}

static void test_cancelled_order_stops_the_worker(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_tree(&game.map, 22, 20);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    game_designate_rect(&game, 22, 20, 22, 20, DESIGNATION_CHOP);
    run_ticks(&game, walk_ticks(1) + 100);
    REQUIRE(colonist->task == TASK_WORK);

    CHECK_EQ_INT(game_designate_rect(&game, 22, 20, 22, 20, DESIGNATION_NONE), 1);
    game_tick(&game);
    CHECK(colonist->task != TASK_WORK);
    CHECK_EQ_INT(game.map.plants[map_index(&game.map, 22, 20)].type, PLANT_TREE);
    game_shutdown(&game);
}

static void test_removed_worker_releases_its_job(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_tree(&game.map, 22, 20);
    EntityId id = game_spawn_colonist(&game, 20, 20);
    REQUIRE(game_colonist(&game, id) != NULL);
    game_designate_rect(&game, 22, 20, 22, 20, DESIGNATION_CHOP);
    game_tick(&game);
    REQUIRE(game_colonist(&game, id)->task == TASK_WORK);
    CHECK(work_owner(&game, 22, 20) != RESERVATION_NO_OWNER);

    game_remove_colonist(&game, id);
    CHECK_EQ_INT(work_owner(&game, 22, 20), RESERVATION_NO_OWNER);
    CHECK_EQ_INT(game.reservations.count, 0);
    game_shutdown(&game);
}

static void test_new_order_is_noticed_despite_backoff(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    /* A tree enclosed in rock: designated but unreachable. */
    add_tree(&game.map, 10, 10);
    set_rock_ring(&game.map, 10, 10, ROCK_GRANITE);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    game_designate_rect(&game, 10, 10, 10, 10, DESIGNATION_CHOP);
    game_tick(&game);
    CHECK(colonist->task != TASK_WORK);
    CHECK(colonist->search_cooldown[SEARCH_WORK] > 0);

    /* A reachable order is picked up right away, without waiting for the cooldown. */
    add_tree(&game.map, 24, 20);
    game_designate_rect(&game, 24, 20, 24, 20, DESIGNATION_CHOP);
    for (int i = 0; i < walk_ticks(1) && colonist->task != TASK_WORK; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->target.x, 24);
    game_shutdown(&game);
}

static void test_hungry_colonists_go_to_different_bushes(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    for (int i = 0; i < 2; i++) {
        Plant *bush = &game.map.plants[map_index(&game.map, 25, 20 + 10 * i)];
        bush->type = PLANT_BUSH;
        bush->growth = 1.0f;
    }
    Colonist *a = spawn_rested(&game, 20, 20);
    Colonist *b = spawn_rested(&game, 20, 21);
    REQUIRE(a != NULL && b != NULL);
    a->needs[NEED_FOOD] = 0.2f;
    b->needs[NEED_FOOD] = 0.2f;

    game_tick(&game);
    CHECK_EQ_INT(a->task, TASK_EAT);
    CHECK_EQ_INT(b->task, TASK_EAT);
    CHECK_EQ_INT(a->target.y, 20);
    CHECK_EQ_INT(b->target.y, 30); /* The closer bush is already taken. */
    CHECK_EQ_INT(game.reservations.count_by_kind[RESERVATION_FOOD], 2);

    /* Once both have eaten, nothing stays reserved. */
    run_ticks(&game, walk_ticks(12) + COLONIST_EAT_TICKS);
    CHECK(a->needs[NEED_FOOD] > 0.5f && b->needs[NEED_FOOD] > 0.5f);
    CHECK_EQ_INT(game.reservations.count_by_kind[RESERVATION_FOOD], 0);
    game_shutdown(&game);
}

static void test_disabled_work_type_is_never_done(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    game.map.rock[map_index(&game.map, 23, 20)] = ROCK_SANDSTONE;
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_MINING] = WORK_PRIORITY_DISABLED;
    game_designate_rect(&game, 23, 20, 23, 20, DESIGNATION_MINE);
    run_ticks(&game, 200);
    CHECK(colonist->task != TASK_WORK);
    CHECK_EQ_INT(game.reservations.count, 0);

    /* Enabling the work type is noticed on the next retry. */
    colonist->work_priorities[WORK_MINING] = WORK_PRIORITY_LOWEST;
    for (int i = 0; i < COLONIST_SEARCH_RETRY_TICKS + 100 && colonist->task != TASK_WORK; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    game_shutdown(&game);
}

static void test_priority_beats_distance(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_tree(&game.map, 22, 20);
    game.map.rock[map_index(&game.map, 32, 20)] = ROCK_SANDSTONE;
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_MINING] = WORK_PRIORITY_HIGHEST;
    colonist->work_priorities[WORK_WOODCUTTING] = WORK_PRIORITY_LOWEST;
    game_designate_rect(&game, 0, 0, 47, 47, DESIGNATION_CHOP);
    game_designate_rect(&game, 0, 0, 47, 47, DESIGNATION_MINE);

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->target.x, 32); /* Farther, but mining comes first. */
    CHECK_EQ_INT(colonist->work_type, DESIGNATION_MINE);

    /* With mining done (cancelled here), the lower priority work is picked up. */
    game_designate_rect(&game, 32, 20, 32, 20, DESIGNATION_NONE);
    for (int i = 0; i < walk_ticks(2) && colonist->work_type != DESIGNATION_CHOP; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->target.x, 22);
    game_shutdown(&game);
}

static void test_hungry_colonist_eats_berries_from_the_ground(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 24, 20, ITEM_BERRIES, 20);
    Plant *bush = &game.map.plants[map_index(&game.map, 30, 20)]; /* Farther away. */
    bush->type = PLANT_BUSH;
    bush->growth = 1.0f;
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = 0.2f;

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_EAT);
    CHECK_EQ_INT(colonist->target.x, 24); /* The nearest food. */
    run_ticks(&game, walk_ticks(4) + COLONIST_EAT_TICKS);

    /* Ate just what it takes to be sated (hunger grew a little on the way), and
     * left the rest. */
    float nutrition = item_def(ITEM_BERRIES)->nutrition;
    int eaten = 20 - items_get(&game.items, 24, 20).count;
    int expected = (int)ceilf((COLONIST_SATED_FOOD - 0.2f) / nutrition);
    CHECK(colonist->needs[NEED_FOOD] >= COLONIST_SATED_FOOD - 0.01f);
    CHECK(eaten >= expected && eaten <= expected + 1);
    CHECK_NEAR(bush->growth, 1.0, 1e-6); /* The bush was not touched. */
    game_shutdown(&game);
}

static void test_colonist_harvests_berries(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Plant *bush = &game.map.plants[map_index(&game.map, 24, 20)];
    bush->type = PLANT_BUSH;
    bush->growth = 1.0f;
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    CHECK_EQ_INT(game_designate_rect(&game, 24, 20, 24, 20, DESIGNATION_HARVEST), 1);

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_STR_EQ(colonist_activity_key(colonist), "colonist.task.harvest");
    run_ticks(&game, walk_ticks(4) + (int)designation_work_needed(&game.designations,
                                                                   &game.map, 24, 20));
    CHECK_EQ_INT(items_get(&game.items, 24, 20).type, ITEM_BERRIES);
    CHECK_EQ_INT(items_get(&game.items, 24, 20).count, plant_def(PLANT_BUSH)->harvest_yield);
    CHECK_NEAR(bush->growth, PLANT_GROWTH_AFTER_PICKING, 0.01);
    game_shutdown(&game);
}

/* Every reservation belongs to a live colonist whose current task targets
 * that tile (work: TASK_WORK, food: TASK_EAT), each colonist holds at most
 * one, and the per-kind counters match. */
static bool reservations_consistent(const Game *game)
{
    const Reservations *r = &game->reservations;
    int by_kind[RESERVATION_KIND_COUNT] = {0};
    int held[COLONIST_MAX + 1] = {0};
    for (int i = 0; i < r->count; i++) {
        const Reservation *entry = &r->entries[i];
        by_kind[entry->kind]++;
        uint32_t slot = entry->owner - 1u;
        if (entry->owner == RESERVATION_NO_OWNER || slot >= COLONIST_MAX ||
            entity_id_is_none(game_colonist_id_at(game, slot)) || ++held[entry->owner] > 1) {
            return false;
        }
        const Colonist *c = &game->colonists[slot];
        ColonistTask expected = entry->kind == RESERVATION_WORK ? TASK_WORK : TASK_EAT;
        if (c->task != expected || c->target.x != entry->tile.x || c->target.y != entry->tile.y) {
            return false;
        }
    }
    for (int kind = 0; kind < RESERVATION_KIND_COUNT; kind++) {
        if (by_kind[kind] != r->count_by_kind[kind]) {
            return false;
        }
    }
    return true;
}

static void test_reservations_stay_consistent_over_a_long_run(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_water_band(&game.map, 44);
    for (int y = 4; y < 44; y += 3) {
        add_tree(&game.map, 10, y);
        add_tree(&game.map, 30, y);
        game.map.rock[map_index(&game.map, 38, y)] = ROCK_SANDSTONE;
        Plant *bush = &game.map.plants[map_index(&game.map, 20, y)];
        bush->type = PLANT_BUSH;
        bush->growth = 1.0f;
    }
    for (int i = 0; i < 4; i++) {
        REQUIRE(spawn_rested(&game, 18 + i, 20) != NULL);
    }
    game_designate_rect(&game, 0, 0, 47, 47, DESIGNATION_CHOP);
    game_designate_rect(&game, 0, 0, 47, 47, DESIGNATION_MINE);
    int initial = game.designations.count;
    REQUIRE(initial > 30);

    bool consistent = true;
    for (int i = 0; i < TICKS_PER_DAY && consistent; i++) {
        game_tick(&game);
        if (i == TICKS_PER_DAY / 4) {
            game_designate_rect(&game, 30, 0, 30, 47, DESIGNATION_NONE); /* Cancel some. */
        }
        if (i % 50 == 0) {
            consistent = reservations_consistent(&game);
        }
    }
    CHECK(consistent);
    CHECK(game.designations.count < initial); /* Work got done. */
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
    RUN_TEST(test_thirsty_colonist_walks_to_shore_and_drinks);
    RUN_TEST(test_thirsty_colonist_without_water_retries_later);
    RUN_TEST(test_most_urgent_need_first);
    RUN_TEST(test_thirst_falls_back_to_food_when_no_water);
    RUN_TEST(test_severe_thirst_wakes_sleeping_colonist);
    RUN_TEST(test_tired_colonist_sleeps_and_recovers);
    RUN_TEST(test_colonists_sleep_at_night);
    RUN_TEST(test_colonists_drink_before_sleeping);
    RUN_TEST(test_idle_colonist_chops_designated_tree);
    RUN_TEST(test_mining_opens_a_passage);
    RUN_TEST(test_one_worker_per_designation);
    RUN_TEST(test_hungry_worker_leaves_work_and_resumes_later);
    RUN_TEST(test_cancelled_order_stops_the_worker);
    RUN_TEST(test_removed_worker_releases_its_job);
    RUN_TEST(test_new_order_is_noticed_despite_backoff);
    RUN_TEST(test_hungry_colonists_go_to_different_bushes);
    RUN_TEST(test_disabled_work_type_is_never_done);
    RUN_TEST(test_priority_beats_distance);
    RUN_TEST(test_hungry_colonist_eats_berries_from_the_ground);
    RUN_TEST(test_colonist_harvests_berries);
    RUN_TEST(test_reservations_stay_consistent_over_a_long_run);
    RUN_TEST(test_position_is_interpolated_while_walking);
    RUN_TEST(test_picking);
    RUN_TEST(test_translation_keys);
    return test_summary();
}
