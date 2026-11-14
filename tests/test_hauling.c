#include "core/game.h"
#include "core/hauling.h"

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
    game->clock.tick = 12 * TICKS_PER_HOUR; /* Noon. */
    return true;
}

/* Spawns a colonist with every need satisfied that only hauls. */
static Colonist *spawn_hauler(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y));
    if (colonist != NULL) {
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
        for (int work = 0; work < WORK_TYPE_COUNT; work++) {
            colonist->work_priorities[work] = WORK_PRIORITY_DISABLED;
        }
        colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_DEFAULT;
    }
    return colonist;
}

static void run_ticks(Game *game, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        game_tick(game);
    }
}

static HaulWorld world_of(Game *game)
{
    HaulWorld world = {&game->map,          &game->items,      &game->stockpiles,
                       &game->reservations, &game->pathfinder, 1};
    return world;
}

static int count_at(const Game *game, int x, int y, ItemType type)
{
    ItemStack stack = items_get(&game->items, x, y);
    return stack.type == type ? stack.count : 0;
}

/* Items of `type` inside a rectangle. */
static int count_in(const Game *game, int x0, int y0, int x1, int y1, ItemType type)
{
    int total = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            total += count_at(game, x, y, type);
        }
    }
    return total;
}

static void test_storage_room(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    CHECK_EQ_INT(haul_storage_room(&game.items, 3, 3, ITEM_WOOD), item_stack_limit(ITEM_WOOD));
    items_add(&game.items, 3, 3, ITEM_WOOD, 20);
    CHECK_EQ_INT(haul_storage_room(&game.items, 3, 3, ITEM_WOOD),
                 item_stack_limit(ITEM_WOOD) - 20);
    CHECK_EQ_INT(haul_storage_room(&game.items, 3, 3, ITEM_STONE), 0); /* Other type. */
    CHECK_EQ_INT(haul_storage_room(&game.items, 4, 4, ITEM_NONE), 0);
    game_shutdown(&game);
}

static void test_no_job_without_stockpile(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 5, 5, ITEM_WOOD, 10);
    HaulWorld world = world_of(&game);
    HaulJob job;
    CHECK(!haul_any_job_possible(&world));
    CHECK(!haul_find_job(&world, (TilePos){5, 6}, PATH_NO_COST_LIMIT, &job));

    /* A stockpile that refuses wood does not help either. */
    int pile = stockpile_create(&game.stockpiles, &game.map, 10, 10, 12, 12);
    stockpile_set_allowed(&game.stockpiles, pile, ITEM_TYPE_BIT(ITEM_STONE));
    CHECK(!haul_any_job_possible(&world));
    CHECK(!haul_find_job(&world, (TilePos){5, 6}, PATH_NO_COST_LIMIT, &job));
    game_shutdown(&game);
}

static void test_find_job_picks_nearest_stack_and_storage(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 5, 5, ITEM_WOOD, 10);
    items_add(&game.items, 20, 5, ITEM_STONE, 7);
    stockpile_create(&game.stockpiles, &game.map, 8, 4, 9, 6);
    HaulWorld world = world_of(&game);
    HaulJob job;
    CHECK(haul_any_job_possible(&world));
    REQUIRE(haul_find_job(&world, (TilePos){4, 5}, PATH_NO_COST_LIMIT, &job));
    CHECK_EQ_INT(job.source.x, 5);
    CHECK_EQ_INT(job.source.y, 5);
    CHECK_EQ_INT(job.type, ITEM_WOOD);
    CHECK_EQ_INT(job.count, 10);
    CHECK_EQ_INT(job.destination.x, 8); /* Nearest stockpile tile to the stack. */
    CHECK_EQ_INT(job.destination.y, 5);
    game_shutdown(&game);
}

static void test_count_is_limited_by_room(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int limit = item_stack_limit(ITEM_WOOD);
    items_add(&game.items, 5, 5, ITEM_WOOD, 30);
    stockpile_create(&game.stockpiles, &game.map, 8, 5, 8, 5);
    items_add(&game.items, 8, 5, ITEM_WOOD, limit - 4);
    HaulWorld world = world_of(&game);
    HaulJob job;
    REQUIRE(haul_find_job(&world, (TilePos){5, 5}, PATH_NO_COST_LIMIT, &job));
    CHECK_EQ_INT(job.count, 4);
    game_shutdown(&game);
}

static void test_stored_items_move_only_to_better_storage(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int low = stockpile_create(&game.stockpiles, &game.map, 5, 5, 5, 5);
    items_add(&game.items, 5, 5, ITEM_WOOD, 10);
    HaulWorld world = world_of(&game);
    HaulJob job;
    CHECK(!haul_find_job(&world, (TilePos){5, 5}, PATH_NO_COST_LIMIT, &job));

    /* Same priority elsewhere: still nothing to do. */
    int other = stockpile_create(&game.stockpiles, &game.map, 15, 5, 15, 5);
    CHECK(!haul_find_job(&world, (TilePos){5, 5}, PATH_NO_COST_LIMIT, &job));

    /* Lower priority (higher number) elsewhere: nothing either. */
    stockpile_set_priority(&game.stockpiles, other, STOCKPILE_PRIORITY_LOWEST);
    CHECK(!haul_find_job(&world, (TilePos){5, 5}, PATH_NO_COST_LIMIT, &job));

    /* Better priority: move. */
    stockpile_set_priority(&game.stockpiles, other, STOCKPILE_PRIORITY_HIGHEST);
    REQUIRE(haul_find_job(&world, (TilePos){5, 5}, PATH_NO_COST_LIMIT, &job));
    CHECK_EQ_INT(job.destination.x, 15);

    /* A stockpile that stops accepting its items makes them haulable to any storage. */
    stockpile_set_priority(&game.stockpiles, other, STOCKPILE_PRIORITY_LOWEST);
    stockpile_set_allowed(&game.stockpiles, low, ITEM_TYPE_BIT(ITEM_STONE));
    REQUIRE(haul_find_job(&world, (TilePos){5, 5}, PATH_NO_COST_LIMIT, &job));
    CHECK_EQ_INT(job.destination.x, 15);
    game_shutdown(&game);
}

static void test_priority_beats_distance(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 5, 5, ITEM_WOOD, 10);
    stockpile_create(&game.stockpiles, &game.map, 7, 5, 7, 5);
    int far = stockpile_create(&game.stockpiles, &game.map, 30, 5, 30, 5);
    stockpile_set_priority(&game.stockpiles, far, 2);
    HaulWorld world = world_of(&game);
    HaulJob job;
    REQUIRE(haul_find_job(&world, (TilePos){5, 5}, PATH_NO_COST_LIMIT, &job));
    CHECK_EQ_INT(job.destination.x, 30);
    game_shutdown(&game);
}

static void test_reserved_tiles_are_skipped(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 5, 5, ITEM_WOOD, 10);
    items_add(&game.items, 5, 9, ITEM_WOOD, 10);
    stockpile_create(&game.stockpiles, &game.map, 8, 5, 8, 6);
    reservations_add(&game.reservations, RESERVATION_HAUL_SOURCE, (TilePos){5, 5}, 2);
    reservations_add(&game.reservations, RESERVATION_STORAGE, (TilePos){8, 5}, 2);
    HaulWorld world = world_of(&game);
    HaulJob job;
    REQUIRE(haul_find_job(&world, (TilePos){5, 5}, PATH_NO_COST_LIMIT, &job));
    CHECK_EQ_INT(job.source.y, 9);
    CHECK_EQ_INT(job.destination.y, 6);

    /* Every storage tile reserved by someone else: nothing possible. */
    reservations_add(&game.reservations, RESERVATION_STORAGE, (TilePos){8, 6}, 3);
    CHECK(!haul_any_job_possible(&world));
    CHECK(!haul_find_job(&world, (TilePos){5, 5}, PATH_NO_COST_LIMIT, &job));
    game_shutdown(&game);
}

static void test_colonist_hauls_items_into_stockpile(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, ITEM_BERRIES, 30);
    int pile = stockpile_create(&game.stockpiles, &game.map, 20, 10, 21, 11);
    REQUIRE(pile != STOCKPILE_NONE);
    Colonist *colonist = spawn_hauler(&game, 8, 10);
    REQUIRE(colonist != NULL);

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_HAUL);
    CHECK_STR_EQ(colonist_activity_key(colonist), "colonist.task.haul");
    CHECK_EQ_INT(reservations_owner(&game.reservations, RESERVATION_HAUL_SOURCE,
                                    (TilePos){10, 10}),
                 1);
    run_ticks(&game, 200);
    CHECK_EQ_INT(colonist->carried.type, ITEM_BERRIES); /* Picked up on the way. */
    CHECK_EQ_INT(colonist->carried.count, 30);
    CHECK_EQ_INT(count_at(&game, 10, 10, ITEM_BERRIES), 0);
    CHECK_EQ_INT(game.items.totals[ITEM_BERRIES], 0); /* Carried items are not on the ground. */

    run_ticks(&game, 600);
    CHECK_EQ_INT(count_in(&game, 20, 10, 21, 11, ITEM_BERRIES), 30);
    CHECK_EQ_INT(colonist->carried.count, 0);
    CHECK_EQ_INT(game.reservations.count, 0);
    game_shutdown(&game);
}

static void test_hauler_carries_what_it_can_and_comes_back(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, ITEM_WOOD, 10);
    stockpile_create(&game.stockpiles, &game.map, 15, 10, 15, 10);
    Colonist *colonist = spawn_hauler(&game, 10, 10);
    REQUIRE(colonist != NULL);
    game_tick(&game);
    int capacity = colonist_carry_capacity(colonist, ITEM_WOOD);
    CHECK_EQ_INT(colonist->carried.count, capacity);
    CHECK_EQ_INT(count_at(&game, 10, 10, ITEM_WOOD), 10 - capacity);

    run_ticks(&game, 2500); /* Four round trips of 5 tiles. */
    CHECK_EQ_INT(count_at(&game, 15, 10, ITEM_WOOD), 10);
    CHECK_EQ_INT(colonist->carried.count, 0);
    game_shutdown(&game);
}

/* Ticks of walking for a hauler with this weight limit to bring one log 20
 * tiles away. */
static int delivery_ticks(float max_carry_weight)
{
    Game game;
    if (!init_flat_game(&game)) {
        return -1;
    }
    items_add(&game.items, 10, 10, ITEM_WOOD, 1);
    stockpile_create(&game.stockpiles, &game.map, 30, 10, 30, 10);
    Colonist *colonist = spawn_hauler(&game, 10, 10);
    int ticks = -1;
    if (colonist != NULL) {
        colonist->max_carry_weight = max_carry_weight;
        for (int i = 1; i <= 10000 && ticks < 0; i++) {
            game_tick(&game);
            if (count_at(&game, 30, 10, ITEM_WOOD) == 1) {
                ticks = i;
            }
        }
    }
    game_shutdown(&game);
    return ticks;
}

static void test_overloaded_hauler_drags_slowly(void)
{
    float log = item_def(ITEM_WOOD)->weight;
    int normal = delivery_ticks(COLONIST_CARRY_WEIGHT);
    int dragging = delivery_ticks(log * 0.5f); /* Half the weight of a log: half speed. */
    REQUIRE(normal > 0);
    REQUIRE(dragging > 0);
    CHECK(dragging > normal * 19 / 10 && dragging < normal * 21 / 10);
}

static void test_interrupted_hauler_drops_its_load(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, ITEM_STONE, 20);
    stockpile_create(&game.stockpiles, &game.map, 30, 10, 30, 10);
    Colonist *colonist = spawn_hauler(&game, 10, 10);
    REQUIRE(colonist != NULL);
    run_ticks(&game, 120);
    REQUIRE(colonist->carried.count == colonist_carry_capacity(colonist, ITEM_STONE));
    REQUIRE(colonist->x > 10 && colonist->x < 30);

    /* Exhausted: the hauler falls asleep and puts its load down. */
    colonist->needs[NEED_REST] = 0.1f;
    run_ticks(&game, 60);
    CHECK_EQ_INT(colonist->carried.count, 0);
    CHECK_EQ_INT(game.items.totals[ITEM_STONE], 20); /* Nothing lost. */
    CHECK_EQ_INT(count_at(&game, 10, 10, ITEM_STONE), 20 - colonist_carry_capacity(colonist,
                                                                                  ITEM_STONE));
    game_shutdown(&game);
}

static void test_removed_destination_reroutes_the_hauler(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, ITEM_BERRIES, 15);
    int near = stockpile_create(&game.stockpiles, &game.map, 20, 10, 20, 10);
    stockpile_create(&game.stockpiles, &game.map, 20, 20, 20, 20);
    Colonist *colonist = spawn_hauler(&game, 10, 10);
    REQUIRE(colonist != NULL);
    run_ticks(&game, 60);
    REQUIRE(colonist->carried.count == 15);
    CHECK_EQ_INT(colonist->haul_destination.y, 10);

    stockpile_delete(&game.stockpiles, near);
    run_ticks(&game, 1500);
    CHECK_EQ_INT(count_at(&game, 20, 20, ITEM_BERRIES), 15);
    CHECK_EQ_INT(game.items.totals[ITEM_BERRIES], 15);
    CHECK_EQ_INT(game.reservations.count, 0);
    game_shutdown(&game);
}

static void test_destination_gone_without_alternative_drops_items(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, ITEM_BERRIES, 15);
    int pile = stockpile_create(&game.stockpiles, &game.map, 25, 10, 25, 10);
    Colonist *colonist = spawn_hauler(&game, 10, 10);
    REQUIRE(colonist != NULL);
    run_ticks(&game, 60);
    REQUIRE(colonist->carried.count == 15);

    stockpile_set_allowed(&game.stockpiles, pile, 0);
    run_ticks(&game, 120);
    CHECK_EQ_INT(colonist->carried.count, 0);
    CHECK(colonist->task != TASK_HAUL);
    CHECK_EQ_INT(game.items.totals[ITEM_BERRIES], 15);
    CHECK_EQ_INT(count_at(&game, 25, 10, ITEM_BERRIES), 0);
    game_shutdown(&game);
}

static void test_two_haulers_take_different_stacks(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, ITEM_BERRIES, 10);
    items_add(&game.items, 10, 12, ITEM_BERRIES, 10);
    stockpile_create(&game.stockpiles, &game.map, 20, 10, 20, 13);
    Colonist *a = spawn_hauler(&game, 9, 11);
    Colonist *b = spawn_hauler(&game, 8, 11);
    REQUIRE(a != NULL && b != NULL);
    game_tick(&game);
    REQUIRE(a->task == TASK_HAUL && b->task == TASK_HAUL);
    CHECK(a->target.y != b->target.y);
    CHECK(a->haul_destination.y != b->haul_destination.y);
    run_ticks(&game, 1200);
    CHECK_EQ_INT(count_in(&game, 20, 10, 20, 13, ITEM_BERRIES), 20);
    game_shutdown(&game);
}

static void test_disabled_hauling_is_never_done(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, ITEM_WOOD, 10);
    stockpile_create(&game.stockpiles, &game.map, 12, 10, 12, 10);
    Colonist *colonist = spawn_hauler(&game, 9, 10);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_DISABLED;
    run_ticks(&game, 1200);
    CHECK_EQ_INT(count_at(&game, 10, 10, ITEM_WOOD), 10);
    CHECK_EQ_INT(count_at(&game, 12, 10, ITEM_WOOD), 0);
    game_shutdown(&game);
}

static void test_disabling_hauling_stops_before_pickup_only(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 14, 10, ITEM_BERRIES, 10);
    stockpile_create(&game.stockpiles, &game.map, 24, 10, 24, 10);
    EntityId id = game_spawn_colonist(&game, 10, 10);
    Colonist *colonist = game_colonist(&game, id);
    REQUIRE(colonist != NULL);
    for (int need = 0; need < NEED_COUNT; need++) {
        colonist->needs[need] = 1.0f;
    }
    game_tick(&game);
    REQUIRE(colonist->task == TASK_HAUL);

    /* On the way to the stack: gives up. */
    REQUIRE(game_set_work_priority(&game, id, WORK_HAULING, WORK_PRIORITY_DISABLED));
    run_ticks(&game, 60);
    CHECK(colonist->task != TASK_HAUL);
    CHECK_EQ_INT(game.reservations.count, 0);
    CHECK_EQ_INT(count_at(&game, 14, 10, ITEM_BERRIES), 10);

    /* Enabled again, then disabled once the load is in hand: it is delivered. */
    REQUIRE(game_set_work_priority(&game, id, WORK_HAULING, WORK_PRIORITY_DEFAULT));
    for (int i = 0; i < 2000 && colonist->carried.count == 0; i++) {
        game_tick(&game);
    }
    REQUIRE(colonist->carried.count == 10);
    REQUIRE(game_set_work_priority(&game, id, WORK_HAULING, WORK_PRIORITY_DISABLED));
    run_ticks(&game, 800);
    CHECK_EQ_INT(count_at(&game, 24, 10, ITEM_BERRIES), 10);
    CHECK(colonist->task != TASK_HAUL);
    game_shutdown(&game);
}

static void test_designations_come_before_hauling_at_same_priority(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, ITEM_WOOD, 10);
    stockpile_create(&game.stockpiles, &game.map, 12, 10, 12, 10);
    Plant *tree = &game.map.plants[map_index(&game.map, 30, 30)];
    tree->type = PLANT_TREE;
    tree->growth = 1.0f;
    game_designate_rect(&game, 30, 30, 30, 30, DESIGNATION_CHOP);
    Colonist *colonist = spawn_hauler(&game, 9, 10);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_WOODCUTTING] = WORK_PRIORITY_DEFAULT;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_WORK);

    /* Hauling at a higher priority wins over woodcutting. */
    Colonist *other = spawn_hauler(&game, 9, 10);
    REQUIRE(other != NULL);
    other->work_priorities[WORK_WOODCUTTING] = WORK_PRIORITY_DEFAULT;
    other->work_priorities[WORK_HAULING] = WORK_PRIORITY_HIGHEST;
    game_tick(&game);
    CHECK_EQ_INT(other->task, TASK_HAUL);
    game_shutdown(&game);
}

static void test_new_stockpile_is_noticed_despite_backoff(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, ITEM_WOOD, 10);
    Colonist *colonist = spawn_hauler(&game, 9, 10);
    REQUIRE(colonist != NULL);
    run_ticks(&game, 10);
    stockpile_create(&game.stockpiles, &game.map, 14, 10, 14, 10);
    run_ticks(&game, 900); /* Far less than the search backoff. */
    CHECK_EQ_INT(count_at(&game, 14, 10, ITEM_WOOD), 10);
    game_shutdown(&game);
}

static void test_hauling_is_deterministic(void)
{
    uint64_t hashes[2];
    for (int run = 0; run < 2; run++) {
        Game game;
        REQUIRE(init_flat_game(&game));
        for (int i = 0; i < 6; i++) {
            items_add(&game.items, 5 + i * 3, 8 + i, i % 2 ? ITEM_WOOD : ITEM_STONE, 25);
        }
        int pile = stockpile_create(&game.stockpiles, &game.map, 30, 5, 33, 8);
        stockpile_create(&game.stockpiles, &game.map, 30, 20, 33, 22);
        stockpile_set_priority(&game.stockpiles, pile, 2);
        for (int i = 0; i < 3; i++) {
            spawn_hauler(&game, 10 + i, 20);
        }
        run_ticks(&game, 6000);
        hashes[run] = game_hash(&game);
        CHECK(count_in(&game, 30, 5, 33, 8, ITEM_WOOD) +
                  count_in(&game, 30, 5, 33, 8, ITEM_STONE) >
              20);
        game_shutdown(&game);
    }
    CHECK_EQ_UINT(hashes[0], hashes[1]);
}

int main(void)
{
    RUN_TEST(test_storage_room);
    RUN_TEST(test_no_job_without_stockpile);
    RUN_TEST(test_find_job_picks_nearest_stack_and_storage);
    RUN_TEST(test_count_is_limited_by_room);
    RUN_TEST(test_stored_items_move_only_to_better_storage);
    RUN_TEST(test_priority_beats_distance);
    RUN_TEST(test_reserved_tiles_are_skipped);
    RUN_TEST(test_colonist_hauls_items_into_stockpile);
    RUN_TEST(test_hauler_carries_what_it_can_and_comes_back);
    RUN_TEST(test_overloaded_hauler_drags_slowly);
    RUN_TEST(test_interrupted_hauler_drops_its_load);
    RUN_TEST(test_removed_destination_reroutes_the_hauler);
    RUN_TEST(test_destination_gone_without_alternative_drops_items);
    RUN_TEST(test_two_haulers_take_different_stacks);
    RUN_TEST(test_disabled_hauling_is_never_done);
    RUN_TEST(test_disabling_hauling_stops_before_pickup_only);
    RUN_TEST(test_designations_come_before_hauling_at_same_priority);
    RUN_TEST(test_new_stockpile_is_noticed_despite_backoff);
    RUN_TEST(test_hauling_is_deterministic);
    return test_summary();
}
