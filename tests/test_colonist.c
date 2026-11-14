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
    map_topology_changed(map); /* The layers were written directly. */
    /* Noon: nobody wants to sleep because of the night. */
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

/* Spawns a colonist with every need satisfied and standard skills (work at
 * the normal speed, so that timings are predictable). */
static Colonist *spawn_rested(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y));
    if (colonist != NULL) {
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
        for (int skill = 0; skill < SKILL_COUNT; skill++) {
            colonist_set_skill_level(colonist, (SkillType)skill, SKILL_LEVEL_STANDARD);
        }
    }
    return colonist;
}

/* Ticks until the order on (x, y) is gone (at most `max_ticks`); returns the
 * number of ticks during which work was done on it. */
static int run_until_done(Game *game, int x, int y, int max_ticks)
{
    int working = 0;
    for (int i = 0; i < max_ticks && designation_get(&game->designations, x, y) != DESIGNATION_NONE;
         i++) {
        float before = game->designations.progress[map_index(&game->map, x, y)];
        game_tick(game);
        if (designation_get(&game->designations, x, y) == DESIGNATION_NONE ||
            game->designations.progress[map_index(&game->map, x, y)] > before) {
            working++;
        }
    }
    return working;
}

/* Turns every column from `x0` to the right edge into shallow water. */
static void add_water_band(Map *map, int x0)
{
    for (int y = 0; y < map->height; y++) {
        for (int x = x0; x < map->width; x++) {
            map_set_terrain(map, x, y, TERRAIN_SHALLOW_WATER);
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
    map_set_terrain(&game.map, 3, 3, TERRAIN_DEEP_WATER);
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
                map_set_rock(map, cx + dx, cy + dy, rock);
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
        map_set_rock(&game.map, 25, y, ROCK_SANDSTONE);
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
    map_set_rock(&game.map, 23, 20, ROCK_SANDSTONE);
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

static void test_set_work_priority_validates(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId id = game_spawn_colonist(&game, 20, 20);
    Colonist *colonist = game_colonist(&game, id);
    REQUIRE(colonist != NULL);
    uint64_t before = game_hash(&game);

    CHECK(game_set_work_priority(&game, id, WORK_MINING, WORK_PRIORITY_HIGHEST));
    CHECK_EQ_INT(colonist->work_priorities[WORK_MINING], WORK_PRIORITY_HIGHEST);
    CHECK(game_hash(&game) != before); /* Priorities are part of the simulation state. */
    CHECK(game_set_work_priority(&game, id, WORK_HAULING, WORK_PRIORITY_DISABLED));
    CHECK_EQ_INT(colonist->work_priorities[WORK_HAULING], WORK_PRIORITY_DISABLED);

    CHECK(!game_set_work_priority(&game, id, WORK_TYPE_COUNT, 1));
    CHECK(!game_set_work_priority(&game, id, WORK_MINING, WORK_PRIORITY_LOWEST + 1));
    CHECK(!game_set_work_priority(&game, id, WORK_MINING, -1));
    CHECK_EQ_INT(colonist->work_priorities[WORK_MINING], WORK_PRIORITY_HIGHEST);

    game_remove_colonist(&game, id);
    CHECK(!game_set_work_priority(&game, id, WORK_MINING, 2)); /* Stale handle. */
    game_shutdown(&game);
}

static void test_enabling_work_type_is_noticed_right_away(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    map_set_rock(&game.map, 23, 20, ROCK_SANDSTONE);
    EntityId id = game_spawn_colonist(&game, 20, 20);
    Colonist *colonist = game_colonist(&game, id);
    REQUIRE(colonist != NULL);
    for (int need = 0; need < NEED_COUNT; need++) {
        colonist->needs[need] = 1.0f;
    }
    set_rock_ring(&game.map, 20, 20, ROCK_GRANITE); /* Stays put: decides every tick. */
    map_set_rock(&game.map, 21, 20, ROCK_NONE);
    map_set_rock(&game.map, 22, 20, ROCK_NONE);
    REQUIRE(game_set_work_priority(&game, id, WORK_MINING, WORK_PRIORITY_DISABLED));
    game_designate_rect(&game, 23, 20, 23, 20, DESIGNATION_MINE);
    run_ticks(&game, 200);
    REQUIRE(colonist->task != TASK_WORK);

    /* No need to wait for the search backoff. */
    REQUIRE(game_set_work_priority(&game, id, WORK_MINING, WORK_PRIORITY_LOWEST));
    for (int i = 0; i < 200 && colonist->task != TASK_WORK; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    game_shutdown(&game);
}

static void test_disabling_work_type_stops_the_current_job(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_tree(&game.map, 22, 20);
    EntityId id = game_spawn_colonist(&game, 20, 20);
    Colonist *colonist = game_colonist(&game, id);
    REQUIRE(colonist != NULL);
    for (int need = 0; need < NEED_COUNT; need++) {
        colonist->needs[need] = 1.0f;
    }
    game_designate_rect(&game, 22, 20, 22, 20, DESIGNATION_CHOP);
    run_ticks(&game, walk_ticks(1) + 100);
    REQUIRE(colonist->task == TASK_WORK);
    float progress = designation_progress_fraction(&game.designations, &game.map, 22, 20);
    REQUIRE(progress > 0.0f);

    REQUIRE(game_set_work_priority(&game, id, WORK_WOODCUTTING, WORK_PRIORITY_DISABLED));
    run_ticks(&game, 5);
    CHECK(colonist->task != TASK_WORK);
    CHECK_EQ_INT(work_owner(&game, 22, 20), RESERVATION_NO_OWNER);
    /* The order and the work done stay for someone else. */
    CHECK_EQ_INT(designation_get(&game.designations, 22, 20), DESIGNATION_CHOP);
    CHECK(designation_progress_fraction(&game.designations, &game.map, 22, 20) >= progress);
    game_shutdown(&game);
}

static void test_priority_beats_distance(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_tree(&game.map, 22, 20);
    map_set_rock(&game.map, 32, 20, ROCK_SANDSTONE);
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
    items_add(&game.items, 24, 20, item_stack_pristine(ITEM_BERRIES, 20));
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
 * that tile (work: TASK_WORK, food: TASK_EAT, haul source and storage:
 * TASK_HAUL), each colonist holds at most one per kind, and the per-kind
 * counters match. */
static bool reservations_consistent(const Game *game)
{
    const Reservations *r = &game->reservations;
    int by_kind[RESERVATION_KIND_COUNT] = {0};
    int held[COLONIST_MAX + 1][RESERVATION_KIND_COUNT] = {{0}};
    for (int i = 0; i < r->count; i++) {
        const Reservation *entry = &r->entries[i];
        by_kind[entry->kind]++;
        uint32_t slot = entry->owner - 1u;
        if (entry->owner == RESERVATION_NO_OWNER || slot >= COLONIST_MAX ||
            entity_id_is_none(game_colonist_id_at(game, slot)) ||
            ++held[entry->owner][entry->kind] > 1) {
            return false;
        }
        const Colonist *c = &game->colonists[slot];
        ColonistTask expected = TASK_HAUL;
        TilePos tile = c->target;
        switch (entry->kind) {
        case RESERVATION_WORK: expected = TASK_WORK; break;
        case RESERVATION_FOOD: expected = TASK_EAT; break;
        case RESERVATION_STORAGE: tile = c->haul_destination; break;
        default: break;
        }
        if (c->task != expected || tile.x != entry->tile.x || tile.y != entry->tile.y) {
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
        map_set_rock(&game.map, 38, y, ROCK_SANDSTONE);
        Plant *bush = &game.map.plants[map_index(&game.map, 20, y)];
        bush->type = PLANT_BUSH;
        bush->growth = 1.0f;
    }
    for (int i = 0; i < 4; i++) {
        REQUIRE(spawn_rested(&game, 18 + i, 20) != NULL);
    }
    game_designate_rect(&game, 0, 0, 47, 47, DESIGNATION_CHOP);
    game_designate_rect(&game, 0, 0, 47, 47, DESIGNATION_MINE);
    int pile = stockpile_create(&game.stockpiles, &game.map, 14, 10, 16, 30);
    REQUIRE(pile != STOCKPILE_NONE);
    int initial = game.designations.count;
    REQUIRE(initial > 30);

    bool consistent = true;
    for (int i = 0; i < TICKS_PER_DAY && consistent; i++) {
        game_tick(&game);
        if (i == TICKS_PER_DAY / 4) {
            game_designate_rect(&game, 30, 0, 30, 47, DESIGNATION_NONE); /* Cancel some. */
        }
        if (i == TICKS_PER_DAY / 3) {
            stockpiles_remove_rect(&game.stockpiles, 14, 10, 16, 15); /* Shrink the storage. */
        }
        if (i == TICKS_PER_DAY / 2) {
            stockpile_set_allowed(&game.stockpiles, pile, ITEM_TYPE_BIT(ITEM_WOOD));
        }
        if (i % 50 == 0) {
            consistent = reservations_consistent(&game);
        }
    }
    CHECK(consistent);
    CHECK(game.designations.count < initial); /* Work got done. */
    int stored_wood = 0;
    for (int y = 16; y <= 30; y++) {
        for (int x = 14; x <= 16; x++) {
            ItemStack stack = items_get(&game.items, x, y);
            stored_wood += stack.type == ITEM_WOOD ? stack.count : 0;
        }
    }
    CHECK(stored_wood > 0); /* And hauled. */

    /* The regions, updated in place as rock was dug out, still group tiles
     * like regions built from scratch do. */
    Regions fresh;
    REQUIRE(regions_init(&fresh, game.map.width, game.map.height));
    regions_update(&fresh, &game.map);
    regions_update(&game.regions, &game.map);
    bool same = true;
    for (int y = 0; y < game.map.height && same; y++) {
        for (int x = 0; x < game.map.width && same; x++) {
            uint32_t a = regions_at(&game.regions, &game.map, x, y);
            uint32_t b = regions_at(&fresh, &game.map, x, y);
            same = (a == REGION_NONE) == (b == REGION_NONE);
            if (x + 1 < game.map.width) {
                same = same && (a == regions_at(&game.regions, &game.map, x + 1, y)) ==
                                   (b == regions_at(&fresh, &game.map, x + 1, y));
            }
            if (y + 1 < game.map.height) {
                same = same && (a == regions_at(&game.regions, &game.map, x, y + 1)) ==
                                   (b == regions_at(&fresh, &game.map, x, y + 1));
            }
        }
    }
    CHECK(same);
    CHECK(game.items.totals[ITEM_STONE] > 0 || stored_wood > 0);
    regions_free(&fresh);
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

static void test_carry_capacity(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Test", 30, 5, 5);
    CHECK_NEAR(colonist.max_carry_weight, COLONIST_CARRY_WEIGHT, 1e-6);
    CHECK_NEAR(colonist.max_carry_bulk, COLONIST_CARRY_BULK, 1e-6);
    /* Logs: 3 by bulk (60 / 20) before 5 by weight (40 kg / 7 kg). */
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_WOOD), 3);
    /* Stone: 2 by weight (40 kg / 20 kg). */
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_STONE), 2);
    /* Berries: 60 portions by bulk (18 kg). */
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_BERRIES), 60);
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_NONE), 0);

    CHECK_NEAR(colonist_carried_weight(&colonist), 0.0, 1e-6);
    CHECK_NEAR(colonist_load_speed_factor(&colonist), 1.0, 1e-6);
    colonist.carried = item_stack_pristine(ITEM_STONE, 2);
    CHECK_NEAR(colonist_carried_weight(&colonist), 40.0, 1e-4);
    CHECK_NEAR(colonist_load_speed_factor(&colonist), 1.0, 1e-6); /* At the limit: fine. */

    /* A weak colonist still drags one item, slowly. */
    colonist.max_carry_weight = 5.0f;
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_STONE), 1);
    colonist.carried = item_stack_pristine(ITEM_STONE, 1);
    CHECK_NEAR(colonist_load_speed_factor(&colonist), 0.25, 1e-6);
    colonist.max_carry_weight = 0.1f;
    CHECK_NEAR(colonist_load_speed_factor(&colonist), COLONIST_MIN_LOAD_SPEED, 1e-6);
    colonist_free(&colonist);
}

static void test_night_hours(void)
{
    CHECK(colonist_is_night(0));
    CHECK(colonist_is_night(6 * TICKS_PER_HOUR - 1));
    CHECK(!colonist_is_night(6 * TICKS_PER_HOUR));
    CHECK(!colonist_is_night(22 * TICKS_PER_HOUR - 1));
    CHECK(colonist_is_night(22 * TICKS_PER_HOUR));
    CHECK(colonist_is_night((uint64_t)TICKS_PER_DAY * 7 + 23 * TICKS_PER_HOUR));
}

/* A colonist walled in on a map without water: the regions tell that neither
 * water nor any wandering goal can be reached, so no path search runs at all. */
static void test_no_search_when_region_has_no_water(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    set_rock_ring(&game.map, 20, 20, ROCK_GRANITE);
    colonist->needs[NEED_WATER] = 0.2f;

    uint32_t searches = game.pathfinder.stamp;
    run_ticks(&game, 300);
    CHECK_EQ_UINT(game.pathfinder.stamp, searches);
    CHECK(colonist->task != TASK_DRINK);
    CHECK(colonist->search_cooldown[SEARCH_WATER] > 0); /* Backs off as after a failed search. */
    CHECK_EQ_INT(colonist->x, 20);

    /* Water appears within reach: found at the next attempt. */
    map_set_rock(&game.map, 21, 20, ROCK_NONE);
    map_set_terrain(&game.map, 21, 20, TERRAIN_SHALLOW_WATER);
    colonist->search_cooldown[SEARCH_WATER] = 0;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_DRINK);
    CHECK(game.pathfinder.stamp != searches);
    game_shutdown(&game);
}

/* Orders that cannot be reached do not make idle colonists search. */
static void test_no_search_when_orders_are_out_of_reach(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    set_rock_ring(&game.map, 20, 20, ROCK_GRANITE);
    /* A block of rock elsewhere, its center designated: nothing to stand on next to it. */
    for (int y = 30; y <= 34; y++) {
        for (int x = 30; x <= 34; x++) {
            map_set_rock(&game.map, x, y, ROCK_SANDSTONE);
        }
    }
    CHECK_EQ_INT(game_designate_rect(&game, 32, 32, 32, 32, DESIGNATION_MINE), 1);
    add_tree(&game.map, 40, 40); /* Reachable only from outside the ring. */
    CHECK_EQ_INT(game_designate_rect(&game, 40, 40, 40, 40, DESIGNATION_CHOP), 1);

    uint32_t searches = game.pathfinder.stamp;
    run_ticks(&game, 300);
    CHECK_EQ_UINT(game.pathfinder.stamp, searches);
    CHECK(colonist->task != TASK_WORK);
    CHECK_EQ_INT(game.reservations.count, 0);

    /* An order on the ring itself can be worked on from inside. */
    CHECK_EQ_INT(game_designate_rect(&game, 21, 20, 21, 20, DESIGNATION_MINE), 1);
    for (int i = 0; i < 10 && colonist->task != TASK_WORK; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->target.x, 21);

    /* Once dug out, the way is open: the tree outside gets chopped too. */
    run_ticks(&game, (int)rock_def(ROCK_GRANITE)->mine_work + 50);
    CHECK(map_is_passable(&game.map, 21, 20));
    CHECK(regions_connected(&game.regions, &game.map, (TilePos){20, 20}, (TilePos){40, 40}));
    for (int i = 0; i < 200 && colonist->work_type != DESIGNATION_CHOP; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->work_type, DESIGNATION_CHOP);
    game_shutdown(&game);
}

static void test_starting_skills_are_random_and_valid(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    bool varied = false;
    int first = -1;
    for (int i = 0; i < 12; i++) {
        Colonist *colonist = game_colonist(&game, game_spawn_colonist(&game, 10 + i, 10));
        REQUIRE(colonist != NULL);
        for (int skill = 0; skill < SKILL_COUNT; skill++) {
            int level = colonist_skill_level(colonist, (SkillType)skill);
            CHECK(level >= SKILL_LEVEL_MIN && level <= SKILL_LEVEL_MAX);
            if (first < 0) {
                first = level;
            } else if (level != first) {
                varied = true;
            }
        }
    }
    CHECK(varied);
    game_shutdown(&game);
}

static void test_skill_levels_validate(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0);
    CHECK_EQ_INT(colonist_skill_level(&colonist, SKILL_MINING), SKILL_LEVEL_MIN);
    CHECK(colonist_set_skill_level(&colonist, SKILL_MINING, 12));
    CHECK_EQ_INT(colonist_skill_level(&colonist, SKILL_MINING), 12);
    CHECK(!colonist_set_skill_level(&colonist, SKILL_MINING, SKILL_LEVEL_MAX + 1));
    CHECK(!colonist_set_skill_level(&colonist, SKILL_MINING, -1));
    CHECK(!colonist_set_skill_level(&colonist, SKILL_NONE, 3));
    CHECK_EQ_INT(colonist_skill_level(&colonist, SKILL_MINING), 12);
    CHECK_EQ_INT(colonist_skill_level(&colonist, SKILL_NONE), SKILL_LEVEL_MIN);
    colonist_free(&colonist);
}

static void test_experience_raises_levels(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0);
    REQUIRE(colonist_set_skill_level(&colonist, SKILL_MINING, 2));
    float to_three = skill_xp_to_next_level(2);
    CHECK_EQ_INT(colonist_gain_skill_xp(&colonist, SKILL_MINING, to_three * 0.25f), 0);
    CHECK_NEAR(colonist_skill_progress(&colonist, SKILL_MINING), 0.25, 1e-4);
    CHECK_EQ_INT(colonist_gain_skill_xp(&colonist, SKILL_MINING, to_three * 0.75f), 1);
    CHECK_EQ_INT(colonist_skill_level(&colonist, SKILL_MINING), 3);
    CHECK_NEAR(colonist_skill_progress(&colonist, SKILL_MINING), 0.0, 1e-4);

    /* A big gain can raise several levels; the leftover is kept. */
    float lots = skill_xp_to_next_level(3) + skill_xp_to_next_level(4) + 100.0f;
    CHECK_EQ_INT(colonist_gain_skill_xp(&colonist, SKILL_MINING, lots), 2);
    CHECK_EQ_INT(colonist_skill_level(&colonist, SKILL_MINING), 5);
    CHECK_NEAR(colonist.skill_xp[SKILL_MINING], 100.0, 1e-2);

    /* Other skills are untouched; invalid gains are ignored. */
    CHECK_EQ_INT(colonist_skill_level(&colonist, SKILL_WOODCUTTING), SKILL_LEVEL_MIN);
    CHECK_EQ_INT(colonist_gain_skill_xp(&colonist, SKILL_NONE, 1e9f), 0);
    CHECK_EQ_INT(colonist_gain_skill_xp(&colonist, SKILL_MINING, -5.0f), 0);
    CHECK_EQ_INT(colonist_gain_skill_xp(&colonist, SKILL_MINING, 0.0f), 0);
    CHECK_NEAR(colonist.skill_xp[SKILL_MINING], 100.0, 1e-2);
    CHECK_NEAR(colonist_skill_progress(&colonist, SKILL_NONE), 0.0, 1e-6);

    /* Setting a level starts the experience over. */
    REQUIRE(colonist_set_skill_level(&colonist, SKILL_MINING, 7));
    CHECK_NEAR(colonist.skill_xp[SKILL_MINING], 0.0, 1e-6);

    /* Masters stop gathering experience. */
    REQUIRE(colonist_set_skill_level(&colonist, SKILL_MINING, SKILL_LEVEL_MAX - 1));
    CHECK_EQ_INT(colonist_gain_skill_xp(&colonist, SKILL_MINING, 1e9f), 1);
    CHECK_EQ_INT(colonist_skill_level(&colonist, SKILL_MINING), SKILL_LEVEL_MAX);
    CHECK_NEAR(colonist.skill_xp[SKILL_MINING], 0.0, 1e-6);
    CHECK_NEAR(colonist_skill_progress(&colonist, SKILL_MINING), 0.0, 1e-6);
    CHECK_EQ_INT(colonist_gain_skill_xp(&colonist, SKILL_MINING, 50.0f), 0);
    CHECK_EQ_INT(colonist_skill_level(&colonist, SKILL_MINING), SKILL_LEVEL_MAX);
    colonist_free(&colonist);
}

static void test_working_teaches_the_order_skill(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_tree(&game.map, 26, 20);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        REQUIRE(colonist_set_skill_level(colonist, (SkillType)skill, 0));
    }
    game_designate_rect(&game, 26, 20, 26, 20, DESIGNATION_CHOP);
    int working = run_until_done(&game, 26, 20, 20000);
    REQUIRE(designation_get(&game.designations, 26, 20) == DESIGNATION_NONE);

    /* Every tick spent working gave one point of woodcutting experience. */
    float total = colonist->skill_xp[SKILL_WOODCUTTING];
    for (int level = 0; level < colonist_skill_level(colonist, SKILL_WOODCUTTING); level++) {
        total += skill_xp_to_next_level(level);
    }
    CHECK_NEAR(total, (float)working * COLONIST_WORK_XP_PER_TICK, 1.0);
    /* A novice works at half speed: a tree (2,000 work) takes about 4,000
     * ticks, enough for level 1 (2,500). */
    CHECK(working > 3500);
    CHECK_EQ_INT(colonist_skill_level(colonist, SKILL_WOODCUTTING), 1);
    CHECK_EQ_INT(colonist_skill_level(colonist, SKILL_MINING), 0);
    CHECK_NEAR(colonist->skill_xp[SKILL_MINING], 0.0, 1e-6);
    CHECK_NEAR(colonist->skill_xp[SKILL_GATHERING], 0.0, 1e-6);
    game_shutdown(&game);
}

static void test_woodcutter_skill_sets_wood_condition(void)
{
    /* The same tree, cut by a novice and by an expert. */
    ItemStack results[2];
    int working[2];
    int levels[2] = {SKILL_LEVEL_MIN, SKILL_LEVEL_MAX};
    for (int run = 0; run < 2; run++) {
        Game game;
        REQUIRE(init_flat_game(&game));
        add_tree(&game.map, 26, 20);
        Colonist *colonist = spawn_rested(&game, 20, 20);
        REQUIRE(colonist != NULL);
        REQUIRE(colonist_set_skill_level(colonist, SKILL_WOODCUTTING, levels[run]));
        game_designate_rect(&game, 26, 20, 26, 20, DESIGNATION_CHOP);
        working[run] = run_until_done(&game, 26, 20, 20000);
        results[run] = items_get(&game.items, 26, 20);
        REQUIRE(results[run].type == ITEM_WOOD);
        game_shutdown(&game);
    }
    CHECK(results[0].quality <= ITEM_QUALITY_ROUGH);
    CHECK(results[1].quality >= ITEM_QUALITY_FINE);
    CHECK(results[0].hit_points < results[1].hit_points);
    CHECK_EQ_INT(results[1].hit_points, item_def(ITEM_WOOD)->max_hit_points);
    /* The expert is also much faster (×1.5 against ×0.5, the novice a bit
     * faster once it reaches level 1). */
    CHECK(working[1] * 2 < working[0]);
}

static void test_work_speed_follows_skill(void)
{
    CHECK_NEAR(skill_work_speed(SKILL_LEVEL_MIN), SKILL_SPEED_AT_MIN, 1e-6);
    CHECK_NEAR(skill_work_speed(SKILL_LEVEL_STANDARD), 1.0, 1e-6);
    CHECK_NEAR(skill_work_speed(SKILL_LEVEL_MAX), SKILL_SPEED_AT_MAX, 1e-6);
    CHECK_NEAR(skill_work_speed(-3), SKILL_SPEED_AT_MIN, 1e-6);
    CHECK_NEAR(skill_work_speed(99), SKILL_SPEED_AT_MAX, 1e-6);

    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0);
    REQUIRE(colonist_set_skill_level(&colonist, SKILL_MINING, SKILL_LEVEL_MAX));
    CHECK_NEAR(colonist_work_speed(&colonist, SKILL_MINING), COLONIST_WORK_SPEED * 1.5, 1e-6);
    CHECK_NEAR(colonist_work_speed(&colonist, SKILL_GATHERING), COLONIST_WORK_SPEED * 0.5, 1e-6);
    CHECK_NEAR(colonist_work_speed(&colonist, SKILL_NONE), COLONIST_WORK_SPEED, 1e-6);
    colonist_free(&colonist);
}

static void test_translation_keys(void)
{
    for (int task = 0; task < TASK_COUNT; task++) {
        CHECK(i18n_is_valid_key(colonist_task_name_key((ColonistTask)task)));
    }
    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        CHECK(i18n_is_valid_key(skill_def((SkillType)skill)->name_key));
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
    RUN_TEST(test_set_work_priority_validates);
    RUN_TEST(test_enabling_work_type_is_noticed_right_away);
    RUN_TEST(test_disabling_work_type_stops_the_current_job);
    RUN_TEST(test_priority_beats_distance);
    RUN_TEST(test_hungry_colonist_eats_berries_from_the_ground);
    RUN_TEST(test_colonist_harvests_berries);
    RUN_TEST(test_starting_skills_are_random_and_valid);
    RUN_TEST(test_skill_levels_validate);
    RUN_TEST(test_woodcutter_skill_sets_wood_condition);
    RUN_TEST(test_experience_raises_levels);
    RUN_TEST(test_working_teaches_the_order_skill);
    RUN_TEST(test_work_speed_follows_skill);
    RUN_TEST(test_reservations_stay_consistent_over_a_long_run);
    RUN_TEST(test_position_is_interpolated_while_walking);
    RUN_TEST(test_picking);
    RUN_TEST(test_carry_capacity);
    RUN_TEST(test_night_hours);
    RUN_TEST(test_no_search_when_region_has_no_water);
    RUN_TEST(test_no_search_when_orders_are_out_of_reach);
    RUN_TEST(test_translation_keys);
    return test_summary();
}
