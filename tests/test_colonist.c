#include "core/game.h"
#include "core/i18n.h"

#include "test_framework.h"

#include <math.h>
#include <string.h>

/* A small game on a flat, empty soil map with no starting colonists. */
/* The start of the first day of a game (the end of summer: brambles bear fruit). */
#define TODAY ((uint64_t)GAME_START_DAY * TICKS_PER_DAY)

static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true; /* One level of ground, open air over it. */
    params.world.seed = 5;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = map_level_tile_count(map); i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_OPEN; /* Open air over it: no trees rising there. */
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) { /* The ground level. */
        map->terrain[i] = TERRAIN_SOIL;
        map->cobbles[i] = MAP_STONES_MAX; /* Cobbles everywhere, no flint. */
        map->flint[i] = 0;
        map->branches[i] = map->twigs[i] = 0;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map); /* The layers were written directly. */
    /* Noon: nobody wants to sleep because of the night. */
    game->clock.tick = TODAY + 12 * TICKS_PER_HOUR;
    return true;
}

/* Level at which work goes at the normal speed. */
#define STANDARD_LEVEL 75.0

/* Spawns a colonist with every need satisfied, the standard level in every
 * knowledge (work at the normal speed, so that timings are predictable) and
 * standard strength. */
static Colonist *spawn_rested(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, 0));
    if (colonist != NULL) {
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
        for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
            colonist_set_knowledge_level(colonist, (KnowledgeType)k, STANDARD_LEVEL);
        }
        colonist_set_strength(colonist, COLONIST_STRENGTH_STANDARD);
    }
    return colonist;
}

/* Ticks until the order on (x, y) is gone (at most `max_ticks`); returns the
 * number of ticks during which work was done on it. */
/* Runs until the branches of the hazel at (x, y), in a zone, are taken;
 * returns the ticks spent working on them. */
static int run_until_done(Game *game, int x, int y, int max_ticks)
{
    int working = 0;
    const float *progress = &game->gather_zones.progress[map_index(&game->map, x, y, 0)];
    for (int i = 0;
         i < max_ticks && plant_stock_available(&game->map.plants[map_index(&game->map, x, y, 0)],
                                                PLANT_STOCK_BRANCHES) > 0;
         i++) {
        float before = *progress;
        game_tick(game);
        if (*progress != before) {
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
            map_set_terrain(map, x, y, 0, TERRAIN_SHALLOW_WATER);
        }
    }
}

/* Owner of the work reservation on a tile (RESERVATION_NO_OWNER if free). */
static uint8_t work_owner(const Game *game, int x, int y)
{
    return reservations_owner(&game->reservations, RESERVATION_WORK,
                              (TilePos){(int16_t)x, (int16_t)y, 0});
}

static void run_ticks(Game *game, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        game_tick(game);
    }
}

/* A grown bramble bearing all its blackberries. */
static Plant *add_bramble(Map *map, int x, int y)
{
    Plant *bramble = &map->plants[map_index(map, x, y, 0)];
    *bramble = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
    plant_settle(bramble, GAME_START_DAY);
    return bramble;
}

/* Handfuls of blackberries a grown bramble bears at most. */
static int bramble_fruit(void)
{
    return plant_def(PLANT_BRAMBLE)->stock_capacity[PLANT_STOCK_FOOD];
}

static void test_spawn_requires_passable_tile(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    map_set_terrain(&game.map, 3, 3, 0, TERRAIN_DEEP_WATER);
    CHECK(entity_id_is_none(game_spawn_colonist(&game, 3, 3, 0)));
    CHECK(entity_id_is_none(game_spawn_colonist(&game, -1, 3, 0)));

    EntityId id = game_spawn_colonist(&game, 4, 4, 0);
    const Colonist *colonist = game_colonist_const(&game, id);
    REQUIRE(colonist != NULL);
    CHECK_EQ_INT(colonist->x, 4);
    CHECK(colonist->name[0] != '\0');
    CHECK(colonist->age >= 18 && colonist->age <= 60);
    CHECK(game_tile_has_colonist(&game, 4, 4, 0));
    game_shutdown(&game);
}

static void test_names_are_unique(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId ids[20];
    for (int i = 0; i < 20; i++) {
        ids[i] = game_spawn_colonist(&game, i, 0, 0);
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
    EntityId id = game_spawn_colonist(&game, 2, 2, 0);
    CHECK(game_remove_colonist(&game, id));
    CHECK(game_colonist(&game, id) == NULL);
    CHECK(!game_remove_colonist(&game, id));
    CHECK(!game_tile_has_colonist(&game, 2, 2, 0));
    game_shutdown(&game);
}

static bool is_drinking_spot(const Map *map, int x, int y, int z, void *userdata)
{
    (void)userdata;
    return map_is_drinking_spot(map, x, y, z);
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
            CHECK(map_is_passable(&game.map, ca->x, ca->y, ca->z));
            CHECK(pathfinder_find_nearest(&game.pathfinder, &game.map,
                                          (TilePos){(int16_t)ca->x, (int16_t)ca->y, (int16_t)ca->z},
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
    CHECK_NEAR(colonist->needs[NEED_WATER], 1.0 - NEED_WATER_DECAY_PER_TICK * TICKS_PER_HOUR, 1e-4);
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
    Plant *bramble = add_bramble(map, 30, 20);

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
    /* Ate some of the fruit; the bramble stays. */
    CHECK(plant_stock_available(bramble, PLANT_STOCK_FOOD) < bramble_fruit());
    CHECK_NEAR(bramble->growth, 1.0f, 1e-6);
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
                map_set_rock(map, cx + dx, cy + dy, 0, rock);
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
        add_bramble(&game.map, 10, 20);
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
    add_bramble(&game.map, 25, 20);
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
    game.clock.tick = TODAY + 23 * TICKS_PER_HOUR;
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
    game.clock.tick = TODAY + 23 * TICKS_PER_HOUR;
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

/* A grown hazel: its branches are taken from beside it (a tree's are taken
 * on its foliage, see core/tree.h). */
static void add_hazel(Map *map, int x, int y)
{
    Plant *hazel = &map->plants[map_index(map, x, y, 0)];
    hazel->type = PLANT_HAZEL;
    hazel->growth = 1.0f;
    plant_settle(hazel, GAME_START_DAY);
}

/* Branches a grown hazel holds. */
static int hazel_branches(void)
{
    return plant_def(PLANT_HAZEL)->stock_capacity[PLANT_STOCK_BRANCHES];
}

/* Ticks needed to walk `tiles` tiles on cheap terrain, plus some margin. */
static int walk_ticks(int tiles)
{
    return tiles * TICKS_PER_SECOND * 2 / 3 + 60;
}

/* A gathering zone over a rectangle that gathers `resource` forever. Returns
 * the number of tiles it covers. */
static int order_zone(Game *game, int x0, int y0, int x1, int y1, GatherResource resource)
{
    int zone = gather_zone_create(&game->gather_zones, &game->map, x0, y0, x1, y1, 0);
    if (zone == GATHER_ZONE_NONE) {
        return 0;
    }
    gather_zone_set_enabled(&game->gather_zones, zone, resource, true);
    gather_zone_set_mode(&game->gather_zones, zone, resource, GATHER_FOREVER);
    return gather_zone_get(&game->gather_zones, zone)->cell_count;
}

static bool branches_open(const Game *game, int x, int y)
{
    return gather_job_available(&game->gather_zones, &game->map, x, y, 0, GATHER_JOB_BRANCHES);
}

static void test_idle_colonist_gathers_branches(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_hazel(&game.map, 26, 20);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    CHECK_EQ_INT(order_zone(&game, 26, 20, 26, 20, GATHER_BRANCHES), 1);

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->target.x, 26);
    CHECK_EQ_INT(work_owner(&game, 26, 20), 1);
    CHECK_STR_EQ(colonist_activity_key(colonist), "colonist.task.gather_branches");

    float work = gather_job_work_needed(&game.map, 26, 20, 0, GATHER_JOB_BRANCHES);
    run_ticks(&game, walk_ticks(6) + (int)(work * 0.5f));
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->x, 25); /* Works from the neighboring tile. */
    CHECK_EQ_INT(colonist->y, 20);
    CHECK(gather_zone_progress_fraction(&game.gather_zones, &game.map, 26, 20, 0) > 0.0f);

    run_ticks(&game, (int)(work * 0.5f));
    const Plant *hazel = &game.map.plants[map_index(&game.map, 26, 20, 0)];
    CHECK_EQ_INT(hazel->type, PLANT_HAZEL);                              /* The hazel stays... */
    CHECK_EQ_INT(plant_stock_available(hazel, PLANT_STOCK_BRANCHES), 0); /* ...bare. */
    CHECK_EQ_INT(items_get(&game.items, 26, 20, 0).type, ITEM_BRANCHES);
    CHECK_EQ_INT((int)game.items.totals[ITEM_BRANCHES], hazel_branches());
    CHECK(!branches_open(&game, 26, 20));
    CHECK(colonist->task != TASK_WORK);
    game_shutdown(&game);
}

static void test_gather_work_under_way(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_hazel(&game.map, 26, 20);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    EntityId id = game_colonist_id_at(&game, 0);
    GatherWorkInfo info;
    CHECK(!game_gather_work_at(&game, 26, 20, 0, &info));
    CHECK_EQ_INT(info.job, GATHER_JOB_NONE);
    CHECK(!game_gather_work_at(&game, -1, 20, 0, &info));
    CHECK_EQ_INT(order_zone(&game, 26, 20, 26, 20, GATHER_BRANCHES), 1);
    CHECK(!game_gather_work_at(&game, 26, 20, 0, &info)); /* Nobody on it yet. */

    /* Taken: walking there. */
    game_tick(&game);
    REQUIRE(game_gather_work_at(&game, 26, 20, 0, &info));
    CHECK_EQ_INT(info.job, GATHER_JOB_BRANCHES);
    CHECK(entity_id_equal(info.worker, id));
    CHECK_NEAR(info.fraction, 0.0, 1e-6);

    /* Half done, once there. */
    float work = gather_job_work_needed(&game.map, 26, 20, 0, GATHER_JOB_BRANCHES);
    for (int i = 0; i < walk_ticks(6) && colonist->x != 25; i++) {
        game_tick(&game);
    }
    run_ticks(&game, (int)(work * 0.5f));
    REQUIRE(game_gather_work_at(&game, 26, 20, 0, &info));
    CHECK(info.fraction > 0.3f && info.fraction < 0.7f);
    CHECK(entity_id_equal(info.worker, id));

    /* Left off: the work done stays, nobody on it. */
    game_set_work_priority(&game, id, WORK_GATHERING, WORK_PRIORITY_DISABLED);
    game_tick(&game);
    REQUIRE(game_gather_work_at(&game, 26, 20, 0, &info));
    CHECK_EQ_INT(info.job, GATHER_JOB_BRANCHES);
    CHECK(entity_id_is_none(info.worker));
    CHECK(info.fraction > 0.3f);
    CHECK(!game_gather_work_at(&game, 25, 20, 0, &info)); /* Another tile. */
    game_shutdown(&game);
}

/* Where a settler at (20, 20) first goes to take branches, with a hazel in a
 * zone of its own at x = 23, 32 and 40 (zone priorities in that order); the
 * hazel at x = 40 is bare when `far_bare`. */
static int first_branches_target(int near, int middle, int far, bool far_bare)
{
    Game game;
    if (!init_flat_game(&game)) {
        return -1;
    }
    const int xs[3] = {23, 32, 40};
    const int priorities[3] = {near, middle, far};
    for (int i = 0; i < 3; i++) {
        add_hazel(&game.map, xs[i], 20);
        order_zone(&game, xs[i], 20, xs[i], 20, GATHER_BRANCHES);
        gather_zone_set_priority(&game.gather_zones,
                                 gather_zone_at(&game.gather_zones, xs[i], 20, 0), priorities[i]);
    }
    if (far_bare) {
        Plant *hazel = &game.map.plants[map_index(&game.map, 40, 20, 0)];
        plant_take_stock(hazel, PLANT_STOCK_BRANCHES, 1000);
    }
    Colonist *colonist = spawn_rested(&game, 20, 20);
    int target = -1;
    if (colonist != NULL) {
        game_tick(&game);
        target = colonist->task == TASK_WORK ? colonist->target.x : -1;
    }
    game_shutdown(&game);
    return target;
}

static void test_zone_priority_before_distance(void)
{
    int normal = GATHER_ZONE_PRIORITY_DEFAULT;
    /* Same priority: the nearest. */
    CHECK_EQ_INT(first_branches_target(normal, normal, normal, false), 23);
    /* The zone of the highest priority, however far. */
    CHECK_EQ_INT(first_branches_target(normal, 3, 2, false), 40);
    CHECK_EQ_INT(first_branches_target(4, 3, normal, false), 32);
    /* A zone of a higher priority with nothing to take is passed over. */
    CHECK_EQ_INT(first_branches_target(normal, 3, 2, true), 32);
}

/* Work done cutting a tile of grass 3 tiles away in `ticks` from the first
 * tick of work, by a settler carrying `tool` (count 0: bare hands). */
static float grass_progress(ItemStack tool, int ticks)
{
    Game game;
    if (!init_flat_game(&game)) {
        return -1.0f;
    }
    Plant *grass = &game.map.plants[map_index(&game.map, 23, 20, 0)];
    *grass = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    Colonist *colonist = spawn_rested(&game, 20, 20);
    if (tool.count > 0) {
        colonist_add_tool(colonist, tool);
    }
    order_zone(&game, 23, 20, 23, 20, GATHER_GRASS);
    const float *progress_at = &game.gather_zones.progress[map_index(&game.map, 23, 20, 0)];
    for (int i = 0; i < 500 && *progress_at <= 0.0f; i++) {
        game_tick(&game);
    }
    float first = *progress_at;
    for (int i = 0; i < ticks; i++) {
        game_tick(&game);
    }
    float progress = *progress_at - first;
    game_shutdown(&game);
    return progress;
}

static void test_tools_speed_up_work(void)
{
    ItemStack flake = item_stack_make(ITEM_FLAKE, 1, ITEM_QUALITY_STANDARD, 0);
    ItemStack cobble = item_stack_make(ITEM_COBBLE, 1, ITEM_QUALITY_STANDARD, 0);
    float bare = grass_progress((ItemStack){.type = ITEM_NONE}, 40);
    float with_flake = grass_progress(flake, 40);
    float with_cobble = grass_progress(cobble, 40);
    REQUIRE(bare > 0.0f);
    /* Bare hands cut grass at ×0.6, a flake at ×1; a cobble does not cut. */
    CHECK_NEAR(with_flake / bare, 1.0 / 0.6, 1e-3);
    CHECK_NEAR(with_cobble, bare, 1e-3);

    /* Factors of what a settler carries. */
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    const ItemTypeMask any = ~(ItemTypeMask)0;
    CHECK_NEAR(colonist_tool_factor(colonist, TOOL_USE_BUILDING, any), 0.8, 1e-6);
    CHECK_NEAR(colonist_tool_factor(colonist, TOOL_USE_NONE, any), 1.0, 1e-6);
    colonist_add_tool(colonist, cobble);
    CHECK_NEAR(colonist_tool_factor(colonist, TOOL_USE_BUILDING, any), 0.9, 1e-6);
    colonist_add_tool(colonist, item_stack_make(ITEM_HAND_AXE, 1, ITEM_QUALITY_FINE, 0));
    CHECK_NEAR(colonist_tool_factor(colonist, TOOL_USE_BUILDING, any), 1.1, 1e-5);
    CHECK_NEAR(colonist_tool_factor(colonist, TOOL_USE_BUILDING, ITEM_TYPE_BIT(ITEM_COBBLE)), 0.9,
               1e-6);
    /* Swapping for another of the same type gives the old one back. */
    ItemStack replaced;
    CHECK(colonist_swap_tool(colonist, item_stack_make(ITEM_COBBLE, 1, ITEM_QUALITY_SUPERB, 0),
                             &replaced));
    CHECK_EQ_INT(replaced.type, ITEM_COBBLE);
    CHECK_EQ_INT(replaced.quality, ITEM_QUALITY_STANDARD);
    CHECK(colonist_swap_tool(colonist, flake, &replaced)); /* None carried: added. */
    CHECK_EQ_INT(replaced.type, ITEM_NONE);
    CHECK(colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_FLAKE)) >= 0);
    game_shutdown(&game);
}

static void test_nobody_fells_or_mines_yet(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    map_set_rock(&game.map, 25, 20, 0, ROCK_SANDSTONE);
    add_hazel(&game.map, 20, 25);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
        colonist_set_knowledge_level(colonist, (KnowledgeType)k, KNOWLEDGE_LEVEL_MAX);
    }
    /* Orders can exist, but nobody can carry them out: no axe, no pick. */
    CHECK_EQ_INT(game_designate_rect(&game, 25, 20, 25, 20, 0, DESIGNATION_MINE), 1);
    CHECK_EQ_INT(order_zone(&game, 20, 25, 20, 25, GATHER_WOOD), 1);
    CHECK(game_designation_available(&game, DESIGNATION_MINE)); /* Earth can be dug. */
    CHECK(!colonist_can_do(colonist, DESIGNATION_MINE));        /* Rock cannot. */
    CHECK(!game_gather_job_known(&game, GATHER_JOB_CHOP));
    CHECK(game_gather_job_known(&game, GATHER_JOB_HARVEST));

    run_ticks(&game, walk_ticks(5) + 4000);
    CHECK(!map_is_passable(&game.map, 25, 20, 0));
    CHECK_EQ_INT(game.map.plants[map_index(&game.map, 20, 25, 0)].type, PLANT_HAZEL);
    CHECK_EQ_INT(game.designations.count, 1);
    CHECK(colonist->task != TASK_WORK);
    CHECK_EQ_INT(game.reservations.count, 0);
    game_shutdown(&game);
}

static void test_one_worker_per_designation(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_hazel(&game.map, 26, 20);
    add_hazel(&game.map, 26, 30);
    Colonist *a = spawn_rested(&game, 20, 20);
    Colonist *b = spawn_rested(&game, 20, 22);
    REQUIRE(a != NULL && b != NULL);
    order_zone(&game, 26, 20, 26, 20, GATHER_BRANCHES);

    game_tick(&game);
    CHECK_EQ_INT(a->task, TASK_WORK);
    CHECK(b->task != TASK_WORK); /* The only job is taken. */

    /* A second job goes to the free colonist, even though it is farther. */
    order_zone(&game, 26, 30, 26, 30, GATHER_BRANCHES);
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
    add_hazel(&game.map, 22, 20);
    add_bramble(&game.map, 15, 20);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    order_zone(&game, 22, 20, 22, 20, GATHER_BRANCHES);
    run_ticks(&game, walk_ticks(1) + 300);
    REQUIRE(colonist->task == TASK_WORK);
    float progress = gather_zone_progress_fraction(&game.gather_zones, &game.map, 22, 20, 0);
    CHECK(progress > 0.0f);

    colonist->needs[NEED_FOOD] = 0.2f;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_EAT);
    CHECK_EQ_INT(work_owner(&game, 22, 20), RESERVATION_NO_OWNER); /* Released. */
    CHECK(gather_zone_progress_fraction(&game.gather_zones, &game.map, 22, 20, 0) >= progress);

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
    add_hazel(&game.map, 22, 20);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    order_zone(&game, 22, 20, 22, 20, GATHER_BRANCHES);
    run_ticks(&game, walk_ticks(1) + 100);
    REQUIRE(colonist->task == TASK_WORK);

    CHECK_EQ_INT(gather_zones_remove_rect(&game.gather_zones, 22, 20, 22, 20, 0), 1);
    game_tick(&game);
    CHECK(colonist->task != TASK_WORK);
    const Plant *hazel = &game.map.plants[map_index(&game.map, 22, 20, 0)];
    CHECK_EQ_INT(plant_stock_available(hazel, PLANT_STOCK_BRANCHES), hazel_branches());
    game_shutdown(&game);
}

static void test_removed_worker_releases_its_job(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_hazel(&game.map, 22, 20);
    EntityId id = game_spawn_colonist(&game, 20, 20, 0);
    REQUIRE(game_colonist(&game, id) != NULL);
    order_zone(&game, 22, 20, 22, 20, GATHER_BRANCHES);
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
    /* A hazel enclosed in rock: designated but unreachable. */
    add_hazel(&game.map, 10, 10);
    set_rock_ring(&game.map, 10, 10, ROCK_GRANITE);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    order_zone(&game, 10, 10, 10, 10, GATHER_BRANCHES);
    game_tick(&game);
    CHECK(colonist->task != TASK_WORK);
    CHECK(colonist->search_cooldown[SEARCH_GATHER] > 0);

    CHECK(colonist->search_cooldown[SEARCH_GATHER] > COLONIST_SEARCH_MIN_RETRY_TICKS);

    /* A reachable order is picked up without waiting for the whole cooldown, but not
     * before the minimum delay: a busy colony changes orders all the time. */
    add_hazel(&game.map, 24, 20);
    order_zone(&game, 24, 20, 24, 20, GATHER_BRANCHES);
    run_ticks(&game, COLONIST_SEARCH_MIN_RETRY_TICKS - 2);
    CHECK(colonist->task != TASK_WORK);
    for (int i = 0; i < walk_ticks(1) && colonist->task != TASK_WORK; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->target.x, 24);
    game_shutdown(&game);
}

/* In a wide zone where only a far tile holds something, the search still
 * finds it (it heads for the open jobs, not for the zone). */
static void test_far_job_in_a_wide_zone(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_hazel(&game.map, 40, 40);
    Colonist *colonist = spawn_rested(&game, 4, 4);
    REQUIRE(colonist != NULL);
    order_zone(&game, 2, 2, 40, 40, GATHER_BRANCHES);
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->target.x, 40);
    CHECK_EQ_INT(colonist->target.y, 40);
    game_shutdown(&game);
}

/* Settlers whose searches failed together do not all search again on the
 * same tick when something changes: each has its own tick. */
static void test_retries_after_a_change_are_staggered(void)
{
    enum { COUNT = COLONIST_SEARCH_STAGGER_TICKS };
    Game game;
    REQUIRE(init_flat_game(&game));
    add_hazel(&game.map, 10, 10);
    set_rock_ring(&game.map, 10, 10, ROCK_GRANITE);
    Colonist *colonists[COUNT];
    for (int i = 0; i < COUNT; i++) {
        colonists[i] = spawn_rested(&game, 20, 20 + i);
        REQUIRE(colonists[i] != NULL);
    }
    order_zone(&game, 10, 10, 10, 10, GATHER_BRANCHES);
    game_tick(&game);
    for (int i = 0; i < COUNT; i++) {
        CHECK(colonists[i]->task != TASK_WORK);
    }

    for (int i = 0; i < COUNT; i++) {
        add_hazel(&game.map, 30, 16 + 2 * i);
    }
    order_zone(&game, 30, 16, 30, 16 + 2 * (COUNT - 1), GATHER_BRANCHES);
    uint64_t started[COUNT] = {0};
    for (int t = 0; t < COLONIST_SEARCH_MIN_RETRY_TICKS + 2 * COUNT; t++) {
        game_tick(&game);
        for (int i = 0; i < COUNT; i++) {
            if (started[i] == 0 && colonists[i]->task == TASK_WORK) {
                started[i] = game.clock.tick;
            }
        }
    }
    for (int i = 0; i < COUNT; i++) {
        CHECK(started[i] != 0);
        for (int j = 0; j < i; j++) {
            CHECK(started[i] != started[j]);
        }
    }
    game_shutdown(&game);
}

static void test_hungry_colonists_go_to_different_brambles(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    for (int i = 0; i < 2; i++) {
        add_bramble(&game.map, 25, 20 + 10 * i);
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
    CHECK_EQ_INT(b->target.y, 30); /* The closer bramble is already taken. */
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
    add_hazel(&game.map, 23, 20);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_GATHERING] = WORK_PRIORITY_DISABLED;
    order_zone(&game, 23, 20, 23, 20, GATHER_BRANCHES);
    run_ticks(&game, 200);
    CHECK(colonist->task != TASK_WORK);
    CHECK_EQ_INT(game.reservations.count, 0);

    /* Enabling the work type is noticed on the next retry. */
    colonist->work_priorities[WORK_GATHERING] = WORK_PRIORITY_LOWEST;
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
    EntityId id = game_spawn_colonist(&game, 20, 20, 0);
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
    EntityId id = game_spawn_colonist(&game, 20, 20, 0);
    Colonist *colonist = game_colonist(&game, id);
    REQUIRE(colonist != NULL);
    for (int need = 0; need < NEED_COUNT; need++) {
        colonist->needs[need] = 1.0f;
    }
    set_rock_ring(&game.map, 20, 20, ROCK_GRANITE); /* Stays put: decides every tick. */
    map_set_rock(&game.map, 21, 20, 0, ROCK_NONE);
    REQUIRE(game_set_work_priority(&game, id, WORK_GATHERING, WORK_PRIORITY_DISABLED));
    REQUIRE(order_zone(&game, 22, 20, 22, 20, GATHER_COBBLES) == 1);
    run_ticks(&game, 200);
    REQUIRE(colonist->task != TASK_WORK);

    /* No need to wait for the search backoff. */
    REQUIRE(game_set_work_priority(&game, id, WORK_GATHERING, WORK_PRIORITY_LOWEST));
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
    add_hazel(&game.map, 22, 20);
    EntityId id = game_spawn_colonist(&game, 20, 20, 0);
    Colonist *colonist = game_colonist(&game, id);
    REQUIRE(colonist != NULL);
    for (int need = 0; need < NEED_COUNT; need++) {
        colonist->needs[need] = 1.0f;
    }
    order_zone(&game, 22, 20, 22, 20, GATHER_BRANCHES);
    run_ticks(&game, walk_ticks(1) + 100);
    REQUIRE(colonist->task == TASK_WORK);
    float progress = gather_zone_progress_fraction(&game.gather_zones, &game.map, 22, 20, 0);
    REQUIRE(progress > 0.0f);

    REQUIRE(game_set_work_priority(&game, id, WORK_GATHERING, WORK_PRIORITY_DISABLED));
    run_ticks(&game, 5);
    CHECK(colonist->task != TASK_WORK);
    CHECK_EQ_INT(work_owner(&game, 22, 20), RESERVATION_NO_OWNER);
    /* The order and the work done stay for someone else. */
    CHECK(branches_open(&game, 22, 20));
    CHECK(gather_zone_progress_fraction(&game.gather_zones, &game.map, 22, 20, 0) >= progress);
    game_shutdown(&game);
}

static void test_priority_beats_distance(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_hazel(&game.map, 32, 20);
    /* Something to haul right next to the colonist. */
    items_add(&game.items, 22, 20, 0, item_stack_pristine(ITEM_COBBLE, 5));
    REQUIRE(stockpile_create(&game.stockpiles, &game.map, 18, 24, 20, 26, 0) != STOCKPILE_NONE);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_GATHERING] = WORK_PRIORITY_HIGHEST;
    colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_LOWEST;
    order_zone(&game, 0, 0, 47, 47, GATHER_BRANCHES);

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->target.x, 32); /* Farther, but gathering comes first. */

    /* With gathering done (cancelled here), the lower priority work is picked up. */
    gather_zones_remove_rect(&game.gather_zones, 32, 20, 32, 20, 0);
    for (int i = 0; i < walk_ticks(2) && colonist->task != TASK_HAUL; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_HAUL);
    CHECK_EQ_INT(colonist->target.x, 22);
    game_shutdown(&game);
}

static void test_hungry_colonist_eats_fruit_from_the_ground(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 24, 20, 0, item_stack_pristine(ITEM_BLACKBERRIES, 20));
    Plant *bramble = add_bramble(&game.map, 30, 20); /* Farther away. */
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = 0.2f;

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_EAT);
    CHECK_EQ_INT(colonist->target.x, 24); /* The nearest food. */
    run_ticks(&game, walk_ticks(4) + COLONIST_EAT_TICKS);

    /* Ate just what it takes to be sated (hunger grew a little on the way), and
     * left the rest. */
    float nutrition = item_def(ITEM_BLACKBERRIES)->nutrition;
    int eaten = 20 - items_get(&game.items, 24, 20, 0).count;
    int expected = (int)ceilf((COLONIST_SATED_FOOD - 0.2f) / nutrition);
    CHECK(colonist->needs[NEED_FOOD] >= COLONIST_SATED_FOOD - 0.01f);
    CHECK(eaten >= expected && eaten <= expected + 1);
    /* The bramble was not touched. */
    CHECK_EQ_INT(plant_stock_available(bramble, PLANT_STOCK_FOOD), bramble_fruit());
    game_shutdown(&game);
}

/* A game on two levels: plains of soil on level 0, a hill of earth over
 * x >= 26 with its top on level 1, a slope up its side at (25, 20). */
static bool init_hill_game(Game *game)
{
    for (uint64_t seed = 1; seed < 50; seed++) {
        NewGameParams params;
        game_default_params(&params);
        params.world.seed = seed;
        params.world.width = 48;
        params.world.height = 48;
        params.colonist_count = 0;
        if (!game_init(game, &params)) {
            return false;
        }
        if (game->map.depth >= 2) {
            break;
        }
        game_shutdown(game);
    }
    Map *map = &game->map;
    if (map->depth < 2) {
        return false;
    }
    for (size_t i = 0; i < map_tile_count(map); i++) {
        int x, y, z;
        map_tile_of(map, i, &x, &y, &z);
        bool hill = x >= 26;
        map->terrain[i] = (uint8_t)(z == 0 || (z == 1 && hill) ? TERRAIN_SOIL : TERRAIN_OPEN);
        map->rock[i] = (uint8_t)(z == 0 && hill ? ROCK_EARTH : ROCK_NONE);
        map->slope[i] = 0;
        map->cobbles[i] = 0;
        map->flint[i] = 0;
        map->branches[i] = map->twigs[i] = 0;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map->slope[map_index(map, 25, 20, 0)] = 1;
    map->slope_count = 1;
    map_topology_changed(map); /* The layers were written directly. */
    game->clock.tick = TODAY + 12 * TICKS_PER_HOUR;
    return true;
}

/* Settlers walk up and down slopes to what they need on other levels. */
static void test_colonist_climbs_to_food_on_a_hill(void)
{
    Game game;
    REQUIRE(init_hill_game(&game));
    items_add(&game.items, 30, 20, 1, item_stack_pristine(ITEM_BLACKBERRIES, 20));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = 0.2f;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_EAT);
    CHECK(colonist->target.x == 30 && colonist->target.z == 1);
    bool climbed = false;
    for (int i = 0; i < walk_ticks(20) && colonist->z == 0; i++) {
        int x = colonist->x, y = colonist->y;
        game_tick(&game);
        if (colonist->z == 1) {
            climbed = x == 25 && y == 20 && colonist->x == 26 && colonist->y == 20;
        }
    }
    CHECK(climbed); /* Up the slope. */
    run_ticks(&game, walk_ticks(6) + COLONIST_EAT_TICKS);
    CHECK(colonist->needs[NEED_FOOD] >= COLONIST_SATED_FOOD - 0.01f);
    CHECK(items_get(&game.items, 30, 20, 1).count < 20);

    /* Food back down on the plains: down the slope again. */
    colonist->needs[NEED_FOOD] = 0.2f;
    items_add(&game.items, 18, 20, 0, item_stack_pristine(ITEM_HAZELNUTS, 20));
    items_remove(&game.items, 30, 20, 1, 20);
    run_ticks(&game, walk_ticks(20) + COLONIST_EAT_TICKS);
    CHECK_EQ_INT(colonist->z, 0);
    CHECK(items_get(&game.items, 18, 20, 0).count < 20);
    game_shutdown(&game);
}

/* Cutting a ramp into a bank of earth: the digger fetches a digging stick on
 * the way, cuts the ramp, and settlers then walk up it. */
static void test_colonist_cuts_a_ramp(void)
{
    Game game;
    REQUIRE(init_hill_game(&game));
    map_set_slope(&game.map, 25, 20, 0, false); /* The hill's only way up. */
    items_add(&game.items, 20, 26, 0, item_stack_pristine(ITEM_DIGGING_STICK, 1));
    Colonist *colonist = spawn_rested(&game, 20, 24);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_DISABLED;
    CHECK(game_designation_available(&game, DESIGNATION_DIG_RAMP));
    REQUIRE(game_designate_rect(&game, 26, 28, 26, 28, 0, DESIGNATION_DIG_RAMP) == 1);
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_STR_EQ(colonist_activity_key(colonist), "colonist.task.dig_ramp");
    for (int i = 0; i < 2 * TICKS_PER_HOUR && !map_is_slope(&game.map, 26, 28, 0); i++) {
        game_tick(&game);
    }
    CHECK(map_is_slope(&game.map, 26, 28, 0));
    CHECK(map_is_open(&game.map, 26, 28, 1));
    CHECK(colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_DIGGING_STICK)) >= 0);
    CHECK_EQ_INT(designation_get(&game.designations, 26, 28, 0), DESIGNATION_NONE);

    /* Food on the hill: up the new ramp. */
    items_add(&game.items, 30, 28, 1, item_stack_pristine(ITEM_BLACKBERRIES, 20));
    colonist->needs[NEED_FOOD] = 0.2f;
    run_ticks(&game, walk_ticks(20) + COLONIST_EAT_TICKS);
    CHECK_EQ_INT(colonist->z, 1);
    CHECK(items_get(&game.items, 30, 28, 1).count < 20);
    game_shutdown(&game);
}

/* No ramp under a stockpile: the order is refused, or dropped when the
 * stockpile comes after it. */
static void test_ramps_spare_stockpiles(void)
{
    Game game;
    REQUIRE(init_hill_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 24);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_DISABLED;
    REQUIRE(stockpile_create(&game.stockpiles, &game.map, 26, 10, 27, 10, 1) != STOCKPILE_NONE);
    CHECK_EQ_INT(game_designate_rect(&game, 26, 10, 26, 10, 0, DESIGNATION_DIG_RAMP), 0);
    REQUIRE(game_designate_rect(&game, 26, 28, 26, 28, 0, DESIGNATION_DIG_RAMP) == 1);
    REQUIRE(stockpile_create(&game.stockpiles, &game.map, 26, 28, 26, 28, 1) != STOCKPILE_NONE);
    run_ticks(&game, walk_ticks(12) + TICKS_PER_SECOND);
    CHECK_EQ_INT(designation_get(&game.designations, 26, 28, 0), DESIGNATION_NONE);
    CHECK(!map_is_slope(&game.map, 26, 28, 0));
    CHECK(map_has_floor(&game.map, 26, 28, 1));
    game_shutdown(&game);
}

/* A 5 x 5 room dug into the hill of init_hill_game() (x 27 to 31, y 20 to
 * 24), reached from the plains through (26, 22); its middle (29, 22) is out
 * of reach of the rock around, held up by a prop when `propped`. */
static void dig_room(Game *game, bool propped)
{
    for (int y = 20; y <= 24; y++) {
        for (int x = 27; x <= 31; x++) {
            map_set_rock(&game->map, x, y, 0, ROCK_NONE);
        }
    }
    map_set_rock(&game->map, 26, 22, 0, ROCK_NONE);
    if (propped) {
        map_set_structure(&game->map, 29, 22, 0, STRUCTURE_PROP);
    }
}

/* Diggers leave alone what would bring a ceiling down, until a prop holds
 * it up. */
static void test_diggers_wait_for_props(void)
{
    Game game;
    REQUIRE(init_hill_game(&game));
    dig_room(&game, false);
    map_set_rock(&game.map, 29, 22, 0, ROCK_EARTH); /* A pillar in the middle. */
    Colonist *colonist = spawn_rested(&game, 20, 22);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_DISABLED;
    REQUIRE(game_designate_rect(&game, 29, 22, 29, 22, 0, DESIGNATION_DIG) == 1);
    run_ticks(&game, walk_ticks(12));
    CHECK(colonist->task != TASK_WORK);
    CHECK(game.map.rock[map_index(&game.map, 29, 22, 0)] == ROCK_EARTH);

    /* A prop beside it: the pillar can go. */
    map_set_structure(&game.map, 28, 22, 0, STRUCTURE_PROP);
    uint32_t notices = game.notices.count;
    for (int i = 0; i < 3 * TICKS_PER_HOUR && game.map.rock[map_index(&game.map, 29, 22, 0)];
         i++) { /* Bare-handed: slow. */
        game_tick(&game);
    }
    CHECK(game.map.rock[map_index(&game.map, 29, 22, 0)] == ROCK_NONE);
    CHECK_EQ_INT(game.notices.count, notices); /* Nothing caved in. */
    game_shutdown(&game);
}

/* Settlers walking over ground that is taken away fall to the ground
 * below. */
static void test_colonist_falls(void)
{
    Game game;
    REQUIRE(init_hill_game(&game));
    Colonist *colonist = game_colonist(&game, game_spawn_colonist(&game, 30, 10, 1));
    REQUIRE(colonist != NULL);
    REQUIRE(colonist->z == 1);
    map_set_rock(&game.map, 30, 10, 0, ROCK_NONE);
    map_set_terrain(&game.map, 30, 10, 1, TERRAIN_OPEN);
    game_tick(&game);
    CHECK_EQ_INT(colonist->z, 0);
    CHECK(colonist->x == 30 && colonist->y == 10);
    game_shutdown(&game);
}

/* A prop taken away in a game: the ceiling caves in, the stockpile there
 * goes, the settler there steps out, and the player is told. */
static void test_cave_in_in_game(void)
{
    Game game;
    REQUIRE(init_hill_game(&game));
    dig_room(&game, true);
    game_tick(&game);
    map_set_structure(&game.map, 29, 22, 0, STRUCTURE_NONE);
    REQUIRE(stockpile_create(&game.stockpiles, &game.map, 29, 22, 29, 22, 0) != STOCKPILE_NONE);
    Colonist *colonist = spawn_rested(&game, 29, 22);
    REQUIRE(colonist != NULL);
    uint32_t notices = game.notices.count;
    game_tick(&game);
    CHECK(game.map.rock[map_index(&game.map, 29, 22, 0)] == ROCK_EARTH);
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 29, 22, 0), STOCKPILE_NONE);
    REQUIRE(game.notices.count == notices + 1);
    CHECK_EQ_INT(notices_get(&game.notices, notices)->type, NOTICE_CAVE_IN);
    game_tick(&game);
    CHECK(map_is_passable(&game.map, colonist->x, colonist->y, colonist->z));
    game_shutdown(&game);
}

/* Digging down into the hill's ground: the hole slopes back up to where the
 * digger stood, and settlers walk down into it. */
static void test_colonist_digs_down(void)
{
    Game game;
    REQUIRE(init_hill_game(&game));
    Colonist *colonist = game_colonist(&game, game_spawn_colonist(&game, 30, 10, 1));
    REQUIRE(colonist != NULL);
    for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
        colonist_set_knowledge_level(colonist, (KnowledgeType)k, STANDARD_LEVEL);
    }
    colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_DISABLED;
    REQUIRE(game_designate_rect(&game, 32, 10, 32, 10, 1, DESIGNATION_DIG_DOWN) == 1);
    for (int i = 0; i < 3 * TICKS_PER_HOUR && !map_is_open(&game.map, 32, 10, 1); i++) {
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
        game_tick(&game);
    }
    CHECK(map_is_open(&game.map, 32, 10, 1));
    CHECK(map_is_slope(&game.map, 32, 10, 0));
    CHECK_EQ_INT(colonist->z, 1);

    /* Food at the bottom of the hole: down it goes. */
    items_add(&game.items, 32, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 20));
    colonist->needs[NEED_FOOD] = 0.2f;
    run_ticks(&game, walk_ticks(6) + COLONIST_EAT_TICKS);
    CHECK(items_get(&game.items, 32, 10, 0).count < 20);
    game_shutdown(&game);
}

/* A ladder up the hill's cliff, where no slope leads: settlers climb it. */
static void test_colonist_climbs_a_ladder(void)
{
    Game game;
    REQUIRE(init_hill_game(&game));
    map_set_slope(&game.map, 25, 20, 0, false);
    REQUIRE(game_place_structure(&game, STRUCTURE_LADDER, 25, 14, 0, DIRECTION_SOUTH) !=
            CONSTRUCTION_NONE);
    CHECK_EQ_INT(game_place_structure(&game, STRUCTURE_LADDER, 18, 14, 0, DIRECTION_SOUTH),
                 CONSTRUCTION_NONE); /* Leads nowhere. */
    game.debug.instant_build = true;
    game_tick(&game);
    REQUIRE(map_structure(&game.map, 25, 14, 0) == STRUCTURE_LADDER);
    items_add(&game.items, 30, 14, 1, item_stack_pristine(ITEM_BLACKBERRIES, 20));
    Colonist *colonist = spawn_rested(&game, 20, 14);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = 0.2f;
    run_ticks(&game, walk_ticks(20) + COLONIST_EAT_TICKS);
    CHECK_EQ_INT(colonist->z, 1);
    CHECK(items_get(&game.items, 30, 14, 1).count < 20);
    game_shutdown(&game);
}

static void test_hungry_colonist_eats_spoiling_food_first(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 23, 20, 0, item_stack_pristine(ITEM_HAZELNUTS, 20)); /* Keeps. */
    /* Bruised mushrooms, a few hours from rotting away, farther. */
    items_add(&game.items, 32, 20, 0,
              item_stack_make(ITEM_MUSHROOMS, 30, ITEM_QUALITY_STANDARD, 1));
    /* Carrots keep for weeks: not worth a longer walk. */
    items_add(&game.items, 26, 20, 0, item_stack_pristine(ITEM_WILD_CARROTS, 20));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = 0.2f;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_EAT);
    CHECK_EQ_INT(colonist->target.x, 32);

    /* Nothing about to rot: the nearest food. */
    Colonist *other = spawn_rested(&game, 20, 24);
    REQUIRE(other != NULL);
    items_remove(&game.items, 32, 20, 0, 30);
    other->needs[NEED_FOOD] = 0.2f;
    game_tick(&game);
    CHECK_EQ_INT(other->task, TASK_EAT);
    CHECK_EQ_INT(other->target.x, 23);
    game_shutdown(&game);
}

static void test_colonist_harvests_blackberries(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Plant *bramble = add_bramble(&game.map, 24, 20);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    CHECK_EQ_INT(order_zone(&game, 24, 20, 24, 20, GATHER_BLACKBERRIES), 1);

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_STR_EQ(colonist_activity_key(colonist), "colonist.task.harvest");
    run_ticks(&game, walk_ticks(4) +
                         (int)gather_job_work_needed(&game.map, 24, 20, 0, GATHER_JOB_HARVEST));
    CHECK_EQ_INT(items_get(&game.items, 24, 20, 0).type, ITEM_BLACKBERRIES);
    CHECK_EQ_INT(items_get(&game.items, 24, 20, 0).count, bramble_fruit());
    CHECK_EQ_INT(plant_stock_available(bramble, PLANT_STOCK_FOOD), 0);
    CHECK_NEAR(bramble->growth, 1.0f, 1e-6);
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
        add_hazel(&game.map, 10, y);
        add_hazel(&game.map, 30, y);
        map_set_rock(&game.map, 38, y, 0, ROCK_SANDSTONE);
        add_bramble(&game.map, 20, y);
    }
    for (int i = 0; i < 4; i++) {
        REQUIRE(spawn_rested(&game, 18 + i, 20) != NULL);
    }
    order_zone(&game, 0, 0, 47, 47, GATHER_BRANCHES);
    game_designate_rect(&game, 0, 0, 47, 47, 0, DESIGNATION_MINE); /* Nobody can, for now. */
    int pile = stockpile_create(&game.stockpiles, &game.map, 14, 10, 16, 30, 0);
    REQUIRE(pile != STOCKPILE_NONE);
    int mines = game.designations.count;
    REQUIRE(mines > 10);

    bool consistent = true;
    for (int i = 0; i < TICKS_PER_DAY && consistent; i++) {
        game_tick(&game);
        if (i == TICKS_PER_DAY / 4) {
            gather_zones_remove_rect(&game.gather_zones, 30, 0, 30, 47, 0); /* Cancel some. */
        }
        if (i == TICKS_PER_DAY / 3) {
            stockpiles_remove_rect(&game.stockpiles, 14, 10, 16, 15, 0); /* Shrink the storage. */
        }
        if (i == TICKS_PER_DAY / 2) {
            stockpile_set_allowed(&game.stockpiles, pile, ITEM_TYPE_BIT(ITEM_BRANCHES));
        }
        if (i % 50 == 0) {
            consistent = reservations_consistent(&game);
        }
    }
    CHECK(consistent);
    CHECK_EQ_INT(game.designations.count, mines); /* Nobody can mine. */
    int stored_branches = 0;
    for (int y = 16; y <= 30; y++) {
        for (int x = 14; x <= 16; x++) {
            ItemStack stack = items_get(&game.items, x, y, 0);
            stored_branches += stack.type == ITEM_BRANCHES ? stack.count : 0;
        }
    }
    CHECK(stored_branches > 0); /* And hauled. */

    /* The regions, updated in place, still group tiles like regions built
     * from scratch do. */
    Regions fresh;
    REQUIRE(regions_init(&fresh, game.map.width, game.map.height, game.map.depth));
    regions_update(&fresh, &game.map);
    regions_update(&game.regions, &game.map);
    bool same = true;
    for (int y = 0; y < game.map.height && same; y++) {
        for (int x = 0; x < game.map.width && same; x++) {
            uint32_t a = regions_at(&game.regions, &game.map, x, y, 0);
            uint32_t b = regions_at(&fresh, &game.map, x, y, 0);
            same = (a == REGION_NONE) == (b == REGION_NONE);
            if (x + 1 < game.map.width) {
                same = same && (a == regions_at(&game.regions, &game.map, x + 1, y, 0)) ==
                                   (b == regions_at(&fresh, &game.map, x + 1, y, 0));
            }
            if (y + 1 < game.map.height) {
                same = same && (a == regions_at(&game.regions, &game.map, x, y + 1, 0)) ==
                                   (b == regions_at(&fresh, &game.map, x, y + 1, 0));
            }
        }
    }
    CHECK(same);
    regions_free(&fresh);
    game_shutdown(&game);
}

static void test_position_is_interpolated_while_walking(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Test", 30, 5, 5, 0);
    float x, y;
    colonist_position(&colonist, &x, &y);
    CHECK_NEAR(x, 5.5, 1e-6);
    CHECK_NEAR(y, 5.5, 1e-6);

    colonist.path.steps = malloc(sizeof(TilePos));
    REQUIRE(colonist.path.steps != NULL);
    colonist.path.capacity = 1;
    colonist.path.count = 1;
    colonist.path.steps[0] = (TilePos){6, 5, 0};
    colonist.move_progress = 0.25f;
    CHECK(colonist_is_moving(&colonist));
    colonist_position(&colonist, &x, &y);
    CHECK_NEAR(x, 5.75, 1e-6);
    CHECK_NEAR(y, 5.5, 1e-6);
    colonist_free(&colonist);
}

static void test_action_follows_the_job(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Test", 30, 5, 5, 0);
    ItemType tool = ITEM_FLINT;
    CHECK_EQ_INT(colonist_action(&colonist, &tool), COLONIST_ACTION_NONE); /* Idle. */
    CHECK_EQ_INT(tool, ITEM_NONE);

    colonist.task = TASK_EAT;
    CHECK_EQ_INT(colonist_action(&colonist, NULL), COLONIST_ACTION_EAT);
    colonist.task = TASK_DRINK;
    CHECK_EQ_INT(colonist_action(&colonist, NULL), COLONIST_ACTION_EAT);

    /* Work only next to the target. */
    colonist.task = TASK_WORK;
    colonist.work_type = DESIGNATION_MINE;
    colonist.target = (TilePos){9, 5, 0};
    CHECK_EQ_INT(colonist_action(&colonist, NULL), COLONIST_ACTION_NONE);
    colonist.target = (TilePos){6, 5, 0};
    CHECK_EQ_INT(colonist_action(&colonist, NULL), COLONIST_ACTION_STRIKE);
    colonist.gathering = true;
    colonist.work_type = GATHER_JOB_HARVEST;
    CHECK_EQ_INT(colonist_action(&colonist, NULL), COLONIST_ACTION_GATHER);
    colonist.work_type = GATHER_JOB_CHOP;
    CHECK_EQ_INT(colonist_action(&colonist, NULL), COLONIST_ACTION_STRIKE);

    /* Crafting: only once at work, with the tool of the recipe. */
    colonist.gathering = false;
    colonist.task = TASK_CRAFT;
    colonist.recipe = RECIPE_HAND_AXE;
    colonist.station_phase = STATION_JOB_FETCH;
    CHECK_EQ_INT(colonist_action(&colonist, NULL), COLONIST_ACTION_NONE);
    colonist.station_phase = STATION_JOB_WORK;
    REQUIRE(colonist_add_tool(&colonist, (ItemStack){.type = ITEM_COBBLE,
                                                     .quality = ITEM_QUALITY_STANDARD,
                                                     .count = 1,
                                                     .hit_points = 100}));
    REQUIRE(colonist_add_tool(&colonist, (ItemStack){.type = ITEM_FLAKE,
                                                     .quality = ITEM_QUALITY_STANDARD,
                                                     .count = 1,
                                                     .hit_points = 100}));
    CHECK_EQ_INT(colonist_action(&colonist, &tool), COLONIST_ACTION_STRIKE);
    CHECK_EQ_INT(tool, ITEM_COBBLE);
    colonist.recipe = RECIPE_THROWING_SPEAR;
    CHECK_EQ_INT(colonist_action(&colonist, &tool), COLONIST_ACTION_CUT);
    CHECK_EQ_INT(tool, ITEM_FLAKE);
    colonist.task = TASK_CONSTRUCT;
    CHECK_EQ_INT(colonist_action(&colonist, &tool), COLONIST_ACTION_STRIKE);
    CHECK_EQ_INT(tool, ITEM_NONE); /* Built by hand. */

    /* Nobody acts while walking. */
    colonist.path.steps = malloc(sizeof(TilePos));
    REQUIRE(colonist.path.steps != NULL);
    colonist.path.capacity = 1;
    colonist.path.count = 1;
    colonist.path.steps[0] = (TilePos){6, 6, 0};
    CHECK_EQ_INT(colonist_action(&colonist, NULL), COLONIST_ACTION_NONE);
    colonist_free(&colonist);
}

static void test_picking(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId a = game_spawn_colonist(&game, 10, 10, 0);
    EntityId b = game_spawn_colonist(&game, 12, 10, 0);
    game_spawn_colonist(&game, 30, 30, 0);

    CHECK(entity_id_equal(game_colonist_at(&game, 10.5f, 10.5f, 0), a));
    CHECK(entity_id_equal(game_colonist_at(&game, 12.3f, 10.6f, 0), b));
    CHECK(entity_id_is_none(game_colonist_at(&game, 11.5f, 10.5f, 0)));
    /* Seen from the level above (open air over the ground), not from below it. */
    CHECK(entity_id_equal(game_colonist_at(&game, 10.5f, 10.5f, 1), a));

    EntityId found[8];
    CHECK_EQ_INT(game_colonists_in_rect(&game, 13.0f, 9.0f, 9.0f, 12.0f, 0, found, 8), 2);
    CHECK_EQ_INT(game_colonists_in_rect(&game, 0.0f, 0.0f, 48.0f, 48.0f, 0, found, 8), 3);
    CHECK_EQ_INT(game_colonists_in_rect(&game, 0.0f, 0.0f, 48.0f, 48.0f, 0, found, 1), 1);
    CHECK_EQ_INT(game_colonists_in_rect(&game, 0.0f, 0.0f, 48.0f, 48.0f, 1, found, 8), 3);
    game_shutdown(&game);
}

static void test_carry_capacity(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Test", 30, 5, 5, 0);
    CHECK_NEAR(colonist.max_carry_weight, COLONIST_CARRY_WEIGHT, 1e-6);
    CHECK_NEAR(colonist.max_carry_bulk, COLONIST_CARRY_BULK, 1e-6);
    /* Logs: 3 by bulk (60 / 20) before 5 by weight (40 kg / 7 kg). */
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_WOOD), 3);
    /* Stone: 2 by weight (40 kg / 20 kg). */
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_STONE_BLOCK), 2);
    /* Berries: 60 portions by bulk (18 kg). */
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_BLACKBERRIES), 60);
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_NONE), 0);

    CHECK_NEAR(colonist_carried_weight(&colonist), 0.0, 1e-6);
    CHECK_NEAR(colonist_load_speed_factor(&colonist), 1.0, 1e-6);
    colonist.carried = item_stack_pristine(ITEM_STONE_BLOCK, 2);
    CHECK_NEAR(colonist_carried_weight(&colonist), 40.0, 1e-4);
    CHECK_NEAR(colonist_load_speed_factor(&colonist), 1.0, 1e-6); /* At the limit: fine. */

    /* A weak colonist still drags one item, slowly. */
    colonist.max_carry_weight = 5.0f;
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_STONE_BLOCK), 1);
    colonist.carried = item_stack_pristine(ITEM_STONE_BLOCK, 1);
    CHECK_NEAR(colonist_load_speed_factor(&colonist), 0.25, 1e-6);
    colonist.max_carry_weight = 0.1f;
    CHECK_NEAR(colonist_load_speed_factor(&colonist), COLONIST_MIN_LOAD_SPEED, 1e-6);
    colonist_free(&colonist);
}

static void test_strength_sets_carry_weight(void)
{
    CHECK_NEAR(colonist_carry_weight_for_strength(COLONIST_STRENGTH_STANDARD),
               COLONIST_CARRY_WEIGHT, 1e-6);
    CHECK_NEAR(colonist_carry_weight_for_strength(COLONIST_STRENGTH_MIN), 22.0, 1e-4);
    CHECK_NEAR(colonist_carry_weight_for_strength(COLONIST_STRENGTH_MAX), 60.0, 1e-4);
    CHECK_NEAR(colonist_carry_weight_for_strength(-5), 22.0, 1e-4);
    CHECK_NEAR(colonist_carry_weight_for_strength(99), 60.0, 1e-4);

    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0, 0);
    CHECK_EQ_INT(colonist.strength, COLONIST_STRENGTH_STANDARD);
    /* 40 kg: two 20 kg stone blocks. */
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_STONE_BLOCK), 2);
    REQUIRE(colonist_set_strength(&colonist, COLONIST_STRENGTH_MAX));
    CHECK_NEAR(colonist.max_carry_weight, 60.0, 1e-4);
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_STONE_BLOCK), 3);
    REQUIRE(colonist_set_strength(&colonist, COLONIST_STRENGTH_MIN));
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_STONE_BLOCK), 1);
    /* Bulk does not depend on strength: still 60 portions of berries. */
    CHECK_EQ_INT(colonist_carry_capacity(&colonist, ITEM_BLACKBERRIES), 60);
    CHECK(!colonist_set_strength(&colonist, 0));
    CHECK(!colonist_set_strength(&colonist, COLONIST_STRENGTH_MAX + 1));
    CHECK_EQ_INT(colonist.strength, COLONIST_STRENGTH_MIN);
    colonist_free(&colonist);
}

static void test_random_strength(void)
{
    Rng rng;
    rng_seed(&rng, 21, 4);
    long young_total = 0, old_total = 0;
    int lowest = COLONIST_STRENGTH_MAX, highest = COLONIST_STRENGTH_MIN;
    for (int i = 0; i < 2000; i++) {
        int young = colonist_random_strength(&rng, 25);
        int old = colonist_random_strength(&rng, 60);
        REQUIRE(young >= COLONIST_STRENGTH_MIN && young <= COLONIST_STRENGTH_MAX);
        REQUIRE(old >= COLONIST_STRENGTH_MIN && old <= COLONIST_STRENGTH_MAX);
        young_total += young;
        old_total += old;
        lowest = young < lowest ? young : lowest;
        highest = young > highest ? young : highest;
    }
    /* Average 10 before 40; three points less at 60. */
    CHECK_NEAR((double)young_total / 2000.0, 10.0, 0.3);
    CHECK_NEAR((double)old_total / 2000.0, 7.0, 0.3);
    CHECK_EQ_INT(lowest, COLONIST_STRENGTH_BASE);
    CHECK_EQ_INT(highest, COLONIST_STRENGTH_BASE + 2 * COLONIST_STRENGTH_DIE);

    /* Spawned colonists get a strength and the matching weight limit. */
    Game game;
    REQUIRE(init_flat_game(&game));
    bool varied = false;
    int first = -1;
    for (int i = 0; i < 12; i++) {
        Colonist *colonist = game_colonist(&game, game_spawn_colonist(&game, 10 + i, 12, 0));
        REQUIRE(colonist != NULL);
        CHECK_NEAR(colonist->max_carry_weight,
                   colonist_carry_weight_for_strength(colonist->strength), 1e-6);
        first = first < 0 ? colonist->strength : first;
        varied = varied || colonist->strength != first;
    }
    CHECK(varied);
    game_shutdown(&game);
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

/* Each settler's night starts and ends a little later, on its own tick. */
static void test_bedtimes_are_spread(void)
{
    const uint64_t evening = (uint64_t)TICKS_PER_DAY * 3 + 22 * TICKS_PER_HOUR;
    const uint64_t morning = (uint64_t)TICKS_PER_DAY * 4 + 6 * TICKS_PER_HOUR;
    uint64_t bedtimes[64];
    for (int slot = 0; slot < 64; slot++) {
        uint8_t worker = (uint8_t)(slot + 1);
        CHECK(!colonist_is_night_for(evening - 1, worker));
        CHECK(colonist_is_night_for(evening + COLONIST_BEDTIME_SPREAD_TICKS, worker));
        CHECK(colonist_is_night_for(morning - 1, worker));
        CHECK(!colonist_is_night_for(morning + COLONIST_BEDTIME_SPREAD_TICKS, worker));
        uint64_t tick = evening;
        while (!colonist_is_night_for(tick, worker)) {
            tick++;
        }
        bedtimes[slot] = tick;
        for (int other = 0; other < slot; other++) {
            CHECK(bedtimes[other] != tick);
        }
        /* The same hours on the first day of a game. */
        CHECK(colonist_is_night_for(0, worker));
    }
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
    map_set_rock(&game.map, 21, 20, 0, ROCK_NONE);
    map_set_terrain(&game.map, 21, 20, 0, TERRAIN_SHALLOW_WATER);
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
    /* A hazel walled in elsewhere: nothing to stand on next to it. */
    add_hazel(&game.map, 32, 32);
    set_rock_ring(&game.map, 32, 32, ROCK_SANDSTONE);
    CHECK_EQ_INT(order_zone(&game, 32, 32, 32, 32, GATHER_BRANCHES), 1);
    add_hazel(&game.map, 40, 40); /* Reachable only from outside the ring. */
    CHECK_EQ_INT(order_zone(&game, 40, 40, 40, 40, GATHER_BRANCHES), 1);

    uint32_t searches = game.pathfinder.stamp;
    run_ticks(&game, 300);
    CHECK_EQ_UINT(game.pathfinder.stamp, searches);
    CHECK(colonist->task != TASK_WORK);
    CHECK_EQ_INT(game.reservations.count, 0);

    /* Once the ring opens, the hazel outside is reached (after the search
     * backoff: no order changed). */
    map_set_rock(&game.map, 21, 20, 0, ROCK_NONE);
    int wait = (COLONIST_SEARCH_RETRY_TICKS << COLONIST_SEARCH_MAX_BACKOFF) + walk_ticks(1);
    for (int i = 0; i < wait && colonist->task != TASK_WORK; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK_EQ_INT(colonist->target.x, 40);
    game_shutdown(&game);
}

static void test_starting_knowledge_is_random_and_valid(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    bool varied = false;
    double first = -1.0;
    for (int i = 0; i < 12; i++) {
        Colonist *colonist = game_colonist(&game, game_spawn_colonist(&game, 10 + i, 10, 0));
        REQUIRE(colonist != NULL);
        for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
            double level = colonist_knowledge_level(colonist, (KnowledgeType)k);
            /* Every first settler masters the starting knowledges. */
            REQUIRE(knowledge_def((KnowledgeType)k)->starting);
            CHECK(level >= KNOWLEDGE_START_MIN &&
                  level <= KNOWLEDGE_START_MIN + KNOWLEDGE_START_SPAN);
            if (first < 0.0) {
                first = level;
            } else if (level != first) {
                varied = true;
            }
        }
    }
    CHECK(varied);
    game_shutdown(&game);
}

static void test_knowledge_levels_validate(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0, 0);
    CHECK_NEAR(colonist_knowledge_level(&colonist, KNOWLEDGE_FIRE), KNOWLEDGE_LEVEL_MIN, 1e-9);
    CHECK(colonist_set_knowledge_level(&colonist, KNOWLEDGE_FIRE, 62.5));
    CHECK_NEAR(colonist_knowledge_level(&colonist, KNOWLEDGE_FIRE), 62.5, 1e-9);
    CHECK(!colonist_set_knowledge_level(&colonist, KNOWLEDGE_FIRE, KNOWLEDGE_LEVEL_MAX + 1.0));
    CHECK(!colonist_set_knowledge_level(&colonist, KNOWLEDGE_FIRE, -1.0));
    CHECK(!colonist_set_knowledge_level(&colonist, KNOWLEDGE_NONE, 60.0));
    CHECK_NEAR(colonist_knowledge_level(&colonist, KNOWLEDGE_FIRE), 62.5, 1e-9);
    CHECK_NEAR(colonist_knowledge_level(&colonist, KNOWLEDGE_NONE), KNOWLEDGE_LEVEL_MIN, 1e-9);

    /* Orders need the mastery level of their knowledge. */
    CHECK(!colonist_can_gather(&colonist, GATHER_JOB_TWIGS));
    REQUIRE(colonist_set_knowledge_level(&colonist, KNOWLEDGE_GATHERING, 49.9));
    CHECK(!colonist_can_gather(&colonist, GATHER_JOB_TWIGS));
    REQUIRE(colonist_set_knowledge_level(&colonist, KNOWLEDGE_GATHERING, 50.0));
    CHECK(colonist_can_gather(&colonist, GATHER_JOB_TWIGS));
    CHECK(colonist_can_gather(&colonist, GATHER_JOB_HARVEST));
    CHECK(!colonist_can_gather(&colonist, GATHER_JOB_CHOP));
    CHECK(!colonist_can_do(&colonist, DESIGNATION_MINE));
    colonist_free(&colonist);
}

static void test_learning_by_doing(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0, 0);
    REQUIRE(colonist_set_knowledge_level(&colonist, KNOWLEDGE_GATHERING, 50.0));
    /* An hour of work at 50: one point. */
    colonist_learn_by_doing(&colonist, KNOWLEDGE_GATHERING, TICKS_PER_HOUR, 0);
    CHECK_NEAR(colonist_knowledge_level(&colonist, KNOWLEDGE_GATHERING), 51.0, 1e-6);

    /* Other knowledges are untouched; invalid practice is ignored. */
    CHECK_NEAR(colonist_knowledge_level(&colonist, KNOWLEDGE_FIRE), 0.0, 1e-9);
    colonist_learn_by_doing(&colonist, KNOWLEDGE_NONE, TICKS_PER_HOUR, 0);
    colonist_learn_by_doing(&colonist, KNOWLEDGE_GATHERING, 0, 0);
    colonist_learn_by_doing(&colonist, KNOWLEDGE_GATHERING, -50, 0);
    CHECK_NEAR(colonist_knowledge_level(&colonist, KNOWLEDGE_GATHERING), 51.0, 1e-6);

    /* Elders learn half as fast; nobody goes past the maximum. */
    Colonist elder;
    colonist_init(&elder, "Oren", 70, 0, 0, 0);
    REQUIRE(colonist_set_knowledge_level(&elder, KNOWLEDGE_FIRE, 50.0));
    colonist_learn_by_doing(&elder, KNOWLEDGE_FIRE, TICKS_PER_HOUR, 0);
    CHECK_NEAR(colonist_knowledge_level(&elder, KNOWLEDGE_FIRE), 50.5, 1e-6);
    REQUIRE(colonist_set_knowledge_level(&elder, KNOWLEDGE_FIRE, 99.99));
    colonist_learn_by_doing(&elder, KNOWLEDGE_FIRE, 100 * TICKS_PER_HOUR, 0);
    CHECK_NEAR(colonist_knowledge_level(&elder, KNOWLEDGE_FIRE), KNOWLEDGE_LEVEL_MAX, 1e-9);
    colonist_free(&elder);
    colonist_free(&colonist);
}

static void test_working_teaches_the_order_knowledge(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_hazel(&game.map, 26, 20);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
        REQUIRE(colonist_set_knowledge_level(colonist, (KnowledgeType)k, 50.0));
    }
    order_zone(&game, 26, 20, 26, 20, GATHER_BRANCHES);
    int working = run_until_done(&game, 26, 20, 20000);
    REQUIRE(!branches_open(&game, 26, 20));

    /* A beginner works at half speed: the branches (900 work) take about
     * 1,800 ticks, worth about 0.7 points of gathering at this level. */
    CHECK(working > 1700);
    double gained = colonist_knowledge_level(colonist, KNOWLEDGE_GATHERING) - 50.0;
    double hours = (double)working / TICKS_PER_HOUR;
    CHECK(gained > hours * 0.97 && gained <= hours);
    for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
        if (k != KNOWLEDGE_GATHERING) {
            CHECK_NEAR(colonist_knowledge_level(colonist, (KnowledgeType)k), 50.0, 1e-9);
        }
    }
    game_shutdown(&game);
}

static void test_gatherer_level_sets_branch_condition(void)
{
    /* The same hazel, gathered from by a beginner and by a master. */
    ItemStack results[2];
    int working[2];
    double levels[2] = {KNOWLEDGE_MASTERY_LEVEL, KNOWLEDGE_LEVEL_MAX};
    for (int run = 0; run < 2; run++) {
        Game game;
        REQUIRE(init_flat_game(&game));
        add_hazel(&game.map, 26, 20);
        Colonist *colonist = spawn_rested(&game, 20, 20);
        REQUIRE(colonist != NULL);
        REQUIRE(colonist_set_knowledge_level(colonist, KNOWLEDGE_GATHERING, levels[run]));
        order_zone(&game, 26, 20, 26, 20, GATHER_BRANCHES);
        working[run] = run_until_done(&game, 26, 20, 20000);
        results[run] = items_get(&game.items, 26, 20, 0);
        REQUIRE(results[run].type == ITEM_BRANCHES);
        game_shutdown(&game);
    }
    CHECK(results[0].quality <= ITEM_QUALITY_ROUGH);
    CHECK(results[1].quality >= ITEM_QUALITY_FINE);
    CHECK(results[0].hit_points < results[1].hit_points);
    CHECK_EQ_INT(results[1].hit_points, item_def(ITEM_BRANCHES)->max_hit_points);
    /* The master is also much faster (×1.5 against ×0.5). */
    CHECK(working[1] * 2 < working[0]);
}

static void test_gathering_zone_keeps_the_worker_busy(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_DISABLED;
    REQUIRE(order_zone(&game, 23, 20, 23, 20, GATHER_COBBLES) == 1);
    int zone = gather_zone_at(&game.gather_zones, 23, 20, 0);
    float stone_work = gather_job_work_needed(&game.map, 23, 20, 0, GATHER_JOB_STONES);

    run_ticks(&game, walk_ticks(2) + (int)(stone_work * 3.5f));
    /* Stone after stone, without stopping. */
    CHECK((int)game.items.totals[ITEM_COBBLE] >= 3);
    CHECK_EQ_INT(items_get(&game.items, 23, 20, 0).type, ITEM_COBBLE);
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    CHECK(colonist->gathering);
    CHECK_EQ_INT(colonist->target.x, 23);
    CHECK_EQ_INT(work_owner(&game, 23, 20), 1);
    CHECK_STR_EQ(colonist_activity_key(colonist), "colonist.task.gather_stones");

    /* Until the player removes the zone. */
    gather_zone_delete(&game.gather_zones, zone);
    game_tick(&game);
    CHECK(colonist->task != TASK_WORK);
    game_shutdown(&game);
}

static void test_gathering_zone_stops_at_its_targets(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_DISABLED;
    int zone = gather_zone_create(&game.gather_zones, &game.map, 23, 20, 24, 20, 0);
    REQUIRE(zone != GATHER_ZONE_NONE);
    gather_zone_set_enabled(&game.gather_zones, zone, GATHER_COBBLES, true);
    gather_zone_set_target(&game.gather_zones, zone, GATHER_COBBLES, 3);
    float stone_work = gather_job_work_needed(&game.map, 23, 20, 0, GATHER_JOB_STONES);
    run_ticks(&game, walk_ticks(4) + (int)(stone_work * 8.0f));
    /* Three cobbles in the colony: done. */
    CHECK_EQ_INT((int)game.items.totals[ITEM_COBBLE], 3);
    CHECK(colonist->task != TASK_WORK);
    CHECK_EQ_INT(game.gather_zones.wanting_cells, 0);
    /* One taken away (used up): the zone wants one more. */
    items_remove(&game.items, 23, 20, 0, 1);
    run_ticks(&game, walk_ticks(4) + (int)(stone_work * 4.0f));
    CHECK_EQ_INT((int)game.items.totals[ITEM_COBBLE], 3);
    game_shutdown(&game);
}

static void test_gatherer_waits_for_the_fruit_to_ripen(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Plant *bramble = add_bramble(&game.map, 24, 20);
    plant_take_stock(bramble, PLANT_STOCK_FOOD, 1000); /* Picked clean. */
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_DISABLED;
    REQUIRE(order_zone(&game, 22, 18, 26, 22, GATHER_BLACKBERRIES) == 25);
    run_ticks(&game, 300);
    CHECK(colonist->task != TASK_WORK);
    CHECK_EQ_INT((int)game.items.totals[ITEM_BLACKBERRIES], 0);

    /* Once a handful has ripened, the idle colonist notices and picks it. */
    int regrow = plant_def(PLANT_BRAMBLE)->stock_regrow_hours[PLANT_STOCK_FOOD];
    plant_regrow_stocks(bramble, regrow, GAME_START_DAY);
    REQUIRE(plant_stock_available(bramble, PLANT_STOCK_FOOD) == 1);
    run_ticks(&game, TICKS_PER_HOUR * 2);
    CHECK_EQ_INT((int)game.items.totals[ITEM_BLACKBERRIES], 1);
    CHECK_EQ_INT(plant_stock_available(bramble, PLANT_STOCK_FOOD), 0);
    game_shutdown(&game);
}

/* Digging roots up: with a digging stick lying close by, the gatherer
 * fetches it on the way and digs faster; it keeps it. */
static void test_gatherer_fetches_a_digging_stick(void)
{
    double ticks[2];
    for (int stick = 0; stick <= 1; stick++) {
        Game game;
        REQUIRE(init_flat_game(&game));
        Plant *cattail = &game.map.plants[map_index(&game.map, 26, 20, 0)];
        *cattail = (Plant){.type = PLANT_CATTAIL, .growth = 1.0f};
        plant_settle(cattail, GAME_START_DAY);
        int rhizomes = plant_stock_available(cattail, PLANT_STOCK_FOOD);
        REQUIRE(rhizomes > 0);
        if (stick) {
            items_add(&game.items, 20, 23, 0, item_stack_pristine(ITEM_DIGGING_STICK, 1));
        }
        Colonist *colonist = spawn_rested(&game, 20, 20);
        REQUIRE(colonist != NULL);
        colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_DISABLED;
        REQUIRE(order_zone(&game, 26, 20, 26, 20, GATHER_CATTAIL_RHIZOMES) == 1);
        game_tick(&game);
        CHECK_EQ_INT(colonist->task, TASK_WORK);
        CHECK_STR_EQ(colonist_activity_key(colonist), "colonist.task.dig");
        int i = 0;
        for (; i < 6 * TICKS_PER_HOUR && game.items.totals[ITEM_CATTAIL_RHIZOMES] == 0; i++) {
            game_tick(&game);
        }
        CHECK_EQ_INT((int)game.items.totals[ITEM_CATTAIL_RHIZOMES], rhizomes);
        CHECK_EQ_INT(colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_DIGGING_STICK)) >= 0, stick);
        CHECK_EQ_INT((int)game.items.totals[ITEM_DIGGING_STICK], 0);
        ticks[stick] = i;
        game_shutdown(&game);
    }
    /* Three times faster with the stick, a detour included. */
    CHECK(ticks[1] * 2.0 < ticks[0]);
}

/* Food that looks like a poisonous plant: only a skilled gatherer takes it. */
static void test_only_skilled_gatherers_pick_mushrooms(void)
{
    for (int skilled = 0; skilled <= 1; skilled++) {
        Game game;
        REQUIRE(init_flat_game(&game));
        game.clock.tick =
            (uint64_t)SEASON_DAY(SEASON_AUTUMN, 2) * TICKS_PER_DAY + 12 * TICKS_PER_HOUR;
        Plant *patch = &game.map.plants[map_index(&game.map, 24, 20, 0)];
        *patch = (Plant){.type = PLANT_MUSHROOMS, .growth = 1.0f};
        plant_settle(patch, game_day_of_year(game.clock.tick));
        Colonist *colonist = spawn_rested(&game, 20, 20);
        REQUIRE(colonist != NULL);
        colonist_set_knowledge_level(colonist, KNOWLEDGE_GATHERING,
                                     skilled ? GATHER_CAREFUL_MIN_LEVEL
                                             : GATHER_CAREFUL_MIN_LEVEL - 1.0f);
        REQUIRE(order_zone(&game, 24, 20, 24, 20, GATHER_MUSHROOMS) == 1);
        game_tick(&game);
        CHECK_EQ_INT(colonist->task == TASK_WORK, skilled);
        game_shutdown(&game);
    }
}

static void test_orders_need_the_knowledge(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    add_hazel(&game.map, 24, 20);
    Colonist *colonist = spawn_rested(&game, 20, 20);
    REQUIRE(colonist != NULL);
    REQUIRE(colonist_set_knowledge_level(colonist, KNOWLEDGE_GATHERING, 49.0));
    CHECK(!game_gather_job_known(&game, GATHER_JOB_BRANCHES));
    order_zone(&game, 24, 20, 24, 20, GATHER_BRANCHES);
    run_ticks(&game, 300);
    CHECK(colonist->task != TASK_WORK);
    CHECK_EQ_INT(game.reservations.count, 0);

    /* Once at the mastery level, the order is taken (after the search backoff). */
    REQUIRE(colonist_set_knowledge_level(colonist, KNOWLEDGE_GATHERING, 50.0));
    CHECK(game_gather_job_known(&game, GATHER_JOB_BRANCHES));
    int wait = (COLONIST_SEARCH_RETRY_TICKS << COLONIST_SEARCH_MAX_BACKOFF) + walk_ticks(1);
    for (int i = 0; i < wait && colonist->task != TASK_WORK; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    game_shutdown(&game);
}

static void test_work_speed_follows_knowledge(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0, 0);
    REQUIRE(colonist_set_knowledge_level(&colonist, KNOWLEDGE_GATHERING, KNOWLEDGE_LEVEL_MAX));
    REQUIRE(colonist_set_knowledge_level(&colonist, KNOWLEDGE_FIRE, KNOWLEDGE_MASTERY_LEVEL));
    CHECK_NEAR(colonist_work_speed(&colonist, KNOWLEDGE_GATHERING), COLONIST_WORK_SPEED * 1.5,
               1e-6);
    CHECK_NEAR(colonist_work_speed(&colonist, KNOWLEDGE_FIRE), COLONIST_WORK_SPEED * 0.5, 1e-6);
    CHECK_NEAR(colonist_work_speed(&colonist, KNOWLEDGE_NONE), COLONIST_WORK_SPEED, 1e-6);
    colonist_free(&colonist);
}

static void test_colonist_cleans_dirty_tiles(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    CHECK_EQ_INT(colonist->work_priorities[WORK_CLEANING], WORK_PRIORITY_DEFAULT_CLEANING);
    dirt_add(&game.dirt, &game.map, 14, 10, 0, 50);
    dirt_add(&game.dirt, &game.map, 15, 10, 0, 20);
    dirt_add(&game.dirt, &game.map, 15, 11, 0, 30);
    bool seen_cleaning = false;
    for (int i = 0; i < 1200 && game.dirt.dirty_count > 0; i++) {
        game_tick(&game);
        if (colonist->task == TASK_CLEAN &&
            colonist_action(colonist, NULL) == COLONIST_ACTION_GATHER) {
            seen_cleaning = true;
            CHECK(colonist->x == colonist->target.x && colonist->y == colonist->target.y);
        }
    }
    CHECK(seen_cleaning);
    CHECK_EQ_INT(game.dirt.dirty_count, 0);
    /* 100 % of dirt at COLONIST_CLEAN_TICKS_PER_PERCENT ticks each, and the walk. */
    CHECK(game.clock.tick - TODAY - 12 * TICKS_PER_HOUR >= 100 * COLONIST_CLEAN_TICKS_PER_PERCENT);
    CHECK_EQ_INT(game.reservations.count_by_kind[RESERVATION_CLEAN], 0);
    game_shutdown(&game);
}

static void test_cleaning_can_be_disabled(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    EntityId id = game_colonist_id_at(&game, 0);
    dirt_add(&game.dirt, &game.map, 12, 10, 0, 100);
    run_ticks(&game, 120);
    CHECK_EQ_INT(colonist->task, TASK_CLEAN);
    /* Disabled on the way or while cleaning: the colonist stops. */
    CHECK(game_set_work_priority(&game, id, WORK_CLEANING, WORK_PRIORITY_DISABLED));
    run_ticks(&game, 60);
    CHECK(colonist->task != TASK_CLEAN);
    int left = dirt_at(&game.dirt, 12, 10, 0);
    CHECK(left > 0);
    run_ticks(&game, 2000);
    CHECK_EQ_INT(dirt_at(&game.dirt, 12, 10, 0), left);
    game_shutdown(&game);
}

/* First task of a colonist with a dirty tile next to it and an item to haul
 * farther away, at the given priorities. */
static ColonistTask first_job(int hauling, int cleaning)
{
    Game game;
    if (!init_flat_game(&game)) {
        return TASK_COUNT;
    }
    Colonist *colonist = spawn_rested(&game, 10, 10);
    EntityId id = game_colonist_id_at(&game, 0);
    stockpile_create(&game.stockpiles, &game.map, 20, 20, 21, 21, 0);
    dirt_add(&game.dirt, &game.map, 11, 10, 0, 100);
    items_add(&game.items, 16, 10, 0, item_stack_pristine(ITEM_COBBLE, 1));
    game_set_work_priority(&game, id, WORK_HAULING, hauling);
    game_set_work_priority(&game, id, WORK_CLEANING, cleaning);
    run_ticks(&game, 30);
    ColonistTask task = colonist != NULL ? colonist->task : TASK_COUNT;
    game_shutdown(&game);
    return task;
}

static void test_cleaning_comes_after_hauling(void)
{
    /* Default priorities: cleaning is the lowest. */
    CHECK_EQ_INT(first_job(WORK_PRIORITY_DEFAULT, WORK_PRIORITY_DEFAULT_CLEANING), TASK_HAUL);
    /* Same priority: hauling, the column on the left, comes first. */
    CHECK_EQ_INT(first_job(3, 3), TASK_HAUL);
    /* A higher priority wins over the column order. */
    CHECK_EQ_INT(first_job(3, WORK_PRIORITY_HIGHEST), TASK_CLEAN);
}

static void test_translation_keys(void)
{
    for (int task = 0; task < TASK_COUNT; task++) {
        CHECK(i18n_is_valid_key(colonist_task_name_key((ColonistTask)task)));
    }
    for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
        CHECK(i18n_is_valid_key(knowledge_def((KnowledgeType)k)->name_key));
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
    RUN_TEST(test_idle_colonist_gathers_branches);
    RUN_TEST(test_nobody_fells_or_mines_yet);
    RUN_TEST(test_one_worker_per_designation);
    RUN_TEST(test_hungry_worker_leaves_work_and_resumes_later);
    RUN_TEST(test_cancelled_order_stops_the_worker);
    RUN_TEST(test_removed_worker_releases_its_job);
    RUN_TEST(test_new_order_is_noticed_despite_backoff);
    RUN_TEST(test_far_job_in_a_wide_zone);
    RUN_TEST(test_retries_after_a_change_are_staggered);
    RUN_TEST(test_hungry_colonists_go_to_different_brambles);
    RUN_TEST(test_disabled_work_type_is_never_done);
    RUN_TEST(test_set_work_priority_validates);
    RUN_TEST(test_enabling_work_type_is_noticed_right_away);
    RUN_TEST(test_disabling_work_type_stops_the_current_job);
    RUN_TEST(test_priority_beats_distance);
    RUN_TEST(test_hungry_colonist_eats_fruit_from_the_ground);
    RUN_TEST(test_colonist_climbs_to_food_on_a_hill);
    RUN_TEST(test_colonist_cuts_a_ramp);
    RUN_TEST(test_ramps_spare_stockpiles);
    RUN_TEST(test_diggers_wait_for_props);
    RUN_TEST(test_colonist_falls);
    RUN_TEST(test_cave_in_in_game);
    RUN_TEST(test_colonist_digs_down);
    RUN_TEST(test_colonist_climbs_a_ladder);
    RUN_TEST(test_hungry_colonist_eats_spoiling_food_first);
    RUN_TEST(test_colonist_harvests_blackberries);
    RUN_TEST(test_starting_knowledge_is_random_and_valid);
    RUN_TEST(test_knowledge_levels_validate);
    RUN_TEST(test_gatherer_level_sets_branch_condition);
    RUN_TEST(test_gathering_zone_keeps_the_worker_busy);
    RUN_TEST(test_gathering_zone_stops_at_its_targets);
    RUN_TEST(test_gatherer_waits_for_the_fruit_to_ripen);
    RUN_TEST(test_gatherer_fetches_a_digging_stick);
    RUN_TEST(test_only_skilled_gatherers_pick_mushrooms);
    RUN_TEST(test_orders_need_the_knowledge);
    RUN_TEST(test_learning_by_doing);
    RUN_TEST(test_working_teaches_the_order_knowledge);
    RUN_TEST(test_work_speed_follows_knowledge);
    RUN_TEST(test_reservations_stay_consistent_over_a_long_run);
    RUN_TEST(test_position_is_interpolated_while_walking);
    RUN_TEST(test_action_follows_the_job);
    RUN_TEST(test_picking);
    RUN_TEST(test_carry_capacity);
    RUN_TEST(test_strength_sets_carry_weight);
    RUN_TEST(test_random_strength);
    RUN_TEST(test_night_hours);
    RUN_TEST(test_bedtimes_are_spread);
    RUN_TEST(test_no_search_when_region_has_no_water);
    RUN_TEST(test_no_search_when_orders_are_out_of_reach);
    RUN_TEST(test_colonist_cleans_dirty_tiles);
    RUN_TEST(test_cleaning_can_be_disabled);
    RUN_TEST(test_cleaning_comes_after_hauling);
    RUN_TEST(test_zone_priority_before_distance);
    RUN_TEST(test_gather_work_under_way);
    RUN_TEST(test_tools_speed_up_work);
    RUN_TEST(test_translation_keys);
    return test_summary();
}
