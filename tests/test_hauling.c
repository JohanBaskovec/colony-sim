#include "core/game.h"
#include "core/hauling.h"

#include "test_framework.h"

/* A small game on a flat, empty soil map with no starting colonists. */
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
        map->plants[i].type = PLANT_NONE;
        map->plants[i].growth = 0.0f;
    }
    map_topology_changed(map); /* The layers were written directly. */
    game->clock.tick = 12 * TICKS_PER_HOUR; /* Noon. */
    return true;
}

/* Spawns a colonist with every need satisfied and standard strength that
 * only hauls. */
static Colonist *spawn_hauler(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, 0));
    if (colonist != NULL) {
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
        for (int work = 0; work < WORK_TYPE_COUNT; work++) {
            colonist->work_priorities[work] = WORK_PRIORITY_DISABLED;
        }
        colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_DEFAULT;
        colonist_set_strength(colonist, COLONIST_STRENGTH_STANDARD);
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
                       &game->reservations, &game->pathfinder, 1,
                       NULL,                REGION_NONE,       0,
                       0};
    return world;
}

static int count_at(const Game *game, int x, int y, ItemType type)
{
    ItemStack stack = items_get(&game->items, x, y, 0);
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
    ItemStack wood = item_stack_pristine(ITEM_WOOD, 1);
    CHECK_EQ_INT(haul_storage_room(&game.items, 3, 3, 0, wood), item_stack_limit(ITEM_WOOD));
    items_add(&game.items, 3, 3, 0, item_stack_pristine(ITEM_WOOD, 20));
    CHECK_EQ_INT(haul_storage_room(&game.items, 3, 3, 0, item_stack_pristine(ITEM_WOOD, 1)),
                 item_stack_limit(ITEM_WOOD) - 20);
    ItemStack stone = item_stack_pristine(ITEM_STONE_BLOCK, 1);
    CHECK_EQ_INT(haul_storage_room(&game.items, 3, 3, 0, stone), 0); /* Other type. */
    CHECK_EQ_INT(haul_storage_room(&game.items, 4, 4, 0, item_stack_pristine(ITEM_NONE, 1)), 0);
    game_shutdown(&game);
}

static void test_no_job_without_stockpile(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 5, 5, 0, item_stack_pristine(ITEM_WOOD, 10));
    HaulWorld world = world_of(&game);
    HaulJob job;
    CHECK(!haul_any_job_possible(&world));
    CHECK(!haul_find_job(&world, (TilePos){5, 6, 0}, PATH_NO_COST_LIMIT, &job));

    /* A stockpile that refuses wood does not help either. */
    int pile = stockpile_create(&game.stockpiles, &game.map, 10, 10, 12, 12, 0);
    stockpile_set_allowed(&game.stockpiles, pile, ITEM_TYPE_BIT(ITEM_STONE_BLOCK));
    CHECK(!haul_any_job_possible(&world));
    CHECK(!haul_find_job(&world, (TilePos){5, 6, 0}, PATH_NO_COST_LIMIT, &job));
    game_shutdown(&game);
}

static void test_find_job_picks_nearest_stack_and_storage(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 5, 5, 0, item_stack_pristine(ITEM_WOOD, 10));
    items_add(&game.items, 20, 5, 0, item_stack_pristine(ITEM_STONE_BLOCK, 7));
    stockpile_create(&game.stockpiles, &game.map, 8, 4, 9, 6, 0);
    HaulWorld world = world_of(&game);
    HaulJob job;
    CHECK(haul_any_job_possible(&world));
    REQUIRE(haul_find_job(&world, (TilePos){4, 5, 0}, PATH_NO_COST_LIMIT, &job));
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
    items_add(&game.items, 5, 5, 0, item_stack_pristine(ITEM_WOOD, 30));
    stockpile_create(&game.stockpiles, &game.map, 8, 5, 8, 5, 0);
    items_add(&game.items, 8, 5, 0, item_stack_pristine(ITEM_WOOD, limit - 4));
    HaulWorld world = world_of(&game);
    HaulJob job;
    REQUIRE(haul_find_job(&world, (TilePos){5, 5, 0}, PATH_NO_COST_LIMIT, &job));
    CHECK_EQ_INT(job.count, 4);
    game_shutdown(&game);
}

static void test_stored_items_move_only_to_better_storage(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int low = stockpile_create(&game.stockpiles, &game.map, 5, 5, 5, 5, 0);
    items_add(&game.items, 5, 5, 0, item_stack_pristine(ITEM_WOOD, 10));
    HaulWorld world = world_of(&game);
    HaulJob job;
    CHECK(!haul_find_job(&world, (TilePos){5, 5, 0}, PATH_NO_COST_LIMIT, &job));

    /* Same priority elsewhere: still nothing to do. */
    int other = stockpile_create(&game.stockpiles, &game.map, 15, 5, 15, 5, 0);
    CHECK(!haul_find_job(&world, (TilePos){5, 5, 0}, PATH_NO_COST_LIMIT, &job));

    /* Lower priority (higher number) elsewhere: nothing either. */
    stockpile_set_priority(&game.stockpiles, other, STOCKPILE_PRIORITY_LOWEST);
    CHECK(!haul_find_job(&world, (TilePos){5, 5, 0}, PATH_NO_COST_LIMIT, &job));

    /* Better priority: move. */
    stockpile_set_priority(&game.stockpiles, other, STOCKPILE_PRIORITY_HIGHEST);
    REQUIRE(haul_find_job(&world, (TilePos){5, 5, 0}, PATH_NO_COST_LIMIT, &job));
    CHECK_EQ_INT(job.destination.x, 15);

    /* A stockpile that stops accepting its items makes them haulable to any storage. */
    stockpile_set_priority(&game.stockpiles, other, STOCKPILE_PRIORITY_LOWEST);
    stockpile_set_allowed(&game.stockpiles, low, ITEM_TYPE_BIT(ITEM_STONE_BLOCK));
    REQUIRE(haul_find_job(&world, (TilePos){5, 5, 0}, PATH_NO_COST_LIMIT, &job));
    CHECK_EQ_INT(job.destination.x, 15);
    game_shutdown(&game);
}

static void test_priority_beats_distance(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 5, 5, 0, item_stack_pristine(ITEM_WOOD, 10));
    stockpile_create(&game.stockpiles, &game.map, 7, 5, 7, 5, 0);
    int far = stockpile_create(&game.stockpiles, &game.map, 30, 5, 30, 5, 0);
    stockpile_set_priority(&game.stockpiles, far, 2);
    HaulWorld world = world_of(&game);
    HaulJob job;
    REQUIRE(haul_find_job(&world, (TilePos){5, 5, 0}, PATH_NO_COST_LIMIT, &job));
    CHECK_EQ_INT(job.destination.x, 30);
    game_shutdown(&game);
}

static void test_reserved_tiles_are_skipped(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 5, 5, 0, item_stack_pristine(ITEM_WOOD, 10));
    items_add(&game.items, 5, 9, 0, item_stack_pristine(ITEM_WOOD, 10));
    stockpile_create(&game.stockpiles, &game.map, 8, 5, 8, 6, 0);
    reservations_add(&game.reservations, RESERVATION_HAUL_SOURCE, (TilePos){5, 5, 0}, 2);
    reservations_add(&game.reservations, RESERVATION_STORAGE, (TilePos){8, 5, 0}, 2);
    HaulWorld world = world_of(&game);
    HaulJob job;
    REQUIRE(haul_find_job(&world, (TilePos){5, 5, 0}, PATH_NO_COST_LIMIT, &job));
    CHECK_EQ_INT(job.source.y, 9);
    CHECK_EQ_INT(job.destination.y, 6);

    /* Every storage tile reserved by someone else: nothing possible. */
    reservations_add(&game.reservations, RESERVATION_STORAGE, (TilePos){8, 6, 0}, 3);
    CHECK(!haul_any_job_possible(&world));
    CHECK(!haul_find_job(&world, (TilePos){5, 5, 0}, PATH_NO_COST_LIMIT, &job));
    game_shutdown(&game);
}

static void test_colonist_hauls_items_into_stockpile(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 30));
    int pile = stockpile_create(&game.stockpiles, &game.map, 20, 10, 21, 11, 0);
    REQUIRE(pile != STOCKPILE_NONE);
    Colonist *colonist = spawn_hauler(&game, 8, 10);
    REQUIRE(colonist != NULL);

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_HAUL);
    CHECK_STR_EQ(colonist_activity_key(colonist), "colonist.task.haul");
    CHECK_EQ_INT(reservations_owner(&game.reservations, RESERVATION_HAUL_SOURCE,
                                    (TilePos){10, 10, 0}),
                 1);
    run_ticks(&game, 200);
    CHECK_EQ_INT(colonist->carried.type, ITEM_BLACKBERRIES); /* Picked up on the way. */
    CHECK_EQ_INT(colonist->carried.count, 30);
    CHECK_EQ_INT(count_at(&game, 10, 10, ITEM_BLACKBERRIES), 0);
    /* Carried items are not on the ground. */
    CHECK_EQ_INT(game.items.totals[ITEM_BLACKBERRIES], 0);

    run_ticks(&game, 600);
    CHECK_EQ_INT(count_in(&game, 20, 10, 21, 11, ITEM_BLACKBERRIES), 30);
    CHECK_EQ_INT(colonist->carried.count, 0);
    CHECK_EQ_INT(game.reservations.count, 0);
    game_shutdown(&game);
}

static void test_hauler_carries_what_it_can_and_comes_back(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_WOOD, 10));
    stockpile_create(&game.stockpiles, &game.map, 15, 10, 15, 10, 0);
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
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_WOOD, 1));
    stockpile_create(&game.stockpiles, &game.map, 30, 10, 30, 10, 0);
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
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_STONE_BLOCK, 20));
    stockpile_create(&game.stockpiles, &game.map, 30, 10, 30, 10, 0);
    Colonist *colonist = spawn_hauler(&game, 10, 10);
    REQUIRE(colonist != NULL);
    run_ticks(&game, 120);
    REQUIRE(colonist->carried.count == colonist_carry_capacity(colonist, ITEM_STONE_BLOCK));
    REQUIRE(colonist->x > 10 && colonist->x < 30);

    /* Exhausted: the hauler falls asleep and puts its load down. */
    colonist->needs[NEED_REST] = 0.1f;
    run_ticks(&game, 60);
    CHECK_EQ_INT(colonist->carried.count, 0);
    CHECK_EQ_INT(game.items.totals[ITEM_STONE_BLOCK], 20); /* Nothing lost. */
    int carried = colonist_carry_capacity(colonist, ITEM_STONE_BLOCK);
    CHECK_EQ_INT(count_at(&game, 10, 10, ITEM_STONE_BLOCK), 20 - carried);
    game_shutdown(&game);
}

static void test_removed_destination_reroutes_the_hauler(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 15));
    int near = stockpile_create(&game.stockpiles, &game.map, 20, 10, 20, 10, 0);
    stockpile_create(&game.stockpiles, &game.map, 20, 20, 20, 20, 0);
    Colonist *colonist = spawn_hauler(&game, 10, 10);
    REQUIRE(colonist != NULL);
    run_ticks(&game, 60);
    REQUIRE(colonist->carried.count == 15);
    CHECK_EQ_INT(colonist->haul_destination.y, 10);

    stockpile_delete(&game.stockpiles, near);
    run_ticks(&game, 1500);
    CHECK_EQ_INT(count_at(&game, 20, 20, ITEM_BLACKBERRIES), 15);
    CHECK_EQ_INT(game.items.totals[ITEM_BLACKBERRIES], 15);
    CHECK_EQ_INT(game.reservations.count, 0);
    game_shutdown(&game);
}

static void test_destination_gone_without_alternative_drops_items(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 15));
    int pile = stockpile_create(&game.stockpiles, &game.map, 25, 10, 25, 10, 0);
    Colonist *colonist = spawn_hauler(&game, 10, 10);
    REQUIRE(colonist != NULL);
    run_ticks(&game, 60);
    REQUIRE(colonist->carried.count == 15);

    stockpile_set_allowed(&game.stockpiles, pile, 0);
    run_ticks(&game, 120);
    CHECK_EQ_INT(colonist->carried.count, 0);
    CHECK(colonist->task != TASK_HAUL);
    CHECK_EQ_INT(game.items.totals[ITEM_BLACKBERRIES], 15);
    CHECK_EQ_INT(count_at(&game, 25, 10, ITEM_BLACKBERRIES), 0);
    game_shutdown(&game);
}

static void test_two_haulers_take_different_stacks(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    items_add(&game.items, 10, 12, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    stockpile_create(&game.stockpiles, &game.map, 20, 10, 20, 13, 0);
    Colonist *a = spawn_hauler(&game, 9, 11);
    Colonist *b = spawn_hauler(&game, 8, 11);
    REQUIRE(a != NULL && b != NULL);
    game_tick(&game);
    REQUIRE(a->task == TASK_HAUL && b->task == TASK_HAUL);
    CHECK(a->target.y != b->target.y);
    CHECK(a->haul_destination.y != b->haul_destination.y);
    run_ticks(&game, 1200);
    CHECK_EQ_INT(count_in(&game, 20, 10, 20, 13, ITEM_BLACKBERRIES), 20);
    game_shutdown(&game);
}

static void test_disabled_hauling_is_never_done(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_WOOD, 10));
    stockpile_create(&game.stockpiles, &game.map, 12, 10, 12, 10, 0);
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
    items_add(&game.items, 14, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    stockpile_create(&game.stockpiles, &game.map, 24, 10, 24, 10, 0);
    EntityId id = game_spawn_colonist(&game, 10, 10, 0);
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
    CHECK_EQ_INT(count_at(&game, 14, 10, ITEM_BLACKBERRIES), 10);

    /* Enabled again, then disabled once the load is in hand: it is delivered. */
    REQUIRE(game_set_work_priority(&game, id, WORK_HAULING, WORK_PRIORITY_DEFAULT));
    for (int i = 0; i < 2000 && colonist->carried.count == 0; i++) {
        game_tick(&game);
    }
    REQUIRE(colonist->carried.count == 10);
    REQUIRE(game_set_work_priority(&game, id, WORK_HAULING, WORK_PRIORITY_DISABLED));
    run_ticks(&game, 800);
    CHECK_EQ_INT(count_at(&game, 24, 10, ITEM_BLACKBERRIES), 10);
    CHECK(colonist->task != TASK_HAUL);
    game_shutdown(&game);
}

static void test_gathering_comes_before_hauling_at_same_priority(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_WOOD, 10));
    stockpile_create(&game.stockpiles, &game.map, 12, 10, 12, 10, 0);
    Plant *hazel = &game.map.plants[map_index(&game.map, 30, 30, 0)];
    hazel->type = PLANT_HAZEL;
    hazel->growth = 1.0f;
    plant_settle(hazel, GAME_START_DAY);
    int zone = gather_zone_create(&game.gather_zones, &game.map, 30, 30, 30, 30, 0);
    gather_zone_set_enabled(&game.gather_zones, zone, GATHER_BRANCHES, true);
    gather_zone_set_mode(&game.gather_zones, zone, GATHER_BRANCHES, GATHER_FOREVER);
    Colonist *colonist = spawn_hauler(&game, 9, 10);
    REQUIRE(colonist != NULL);
    colonist->work_priorities[WORK_GATHERING] = WORK_PRIORITY_DEFAULT;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_WORK);

    /* Hauling at a higher priority wins over gathering. */
    Colonist *other = spawn_hauler(&game, 9, 10);
    REQUIRE(other != NULL);
    other->work_priorities[WORK_GATHERING] = WORK_PRIORITY_DEFAULT;
    other->work_priorities[WORK_HAULING] = WORK_PRIORITY_HIGHEST;
    game_tick(&game);
    CHECK_EQ_INT(other->task, TASK_HAUL);
    game_shutdown(&game);
}

static void test_new_stockpile_is_noticed_despite_backoff(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_WOOD, 10));
    Colonist *colonist = spawn_hauler(&game, 9, 10);
    REQUIRE(colonist != NULL);
    run_ticks(&game, 10);
    stockpile_create(&game.stockpiles, &game.map, 14, 10, 14, 10, 0);
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
            ItemType type = i % 2 ? ITEM_WOOD : ITEM_STONE_BLOCK;
            items_add(&game.items, 5 + i * 3, 8 + i, 0, item_stack_pristine(type, 25));
        }
        int pile = stockpile_create(&game.stockpiles, &game.map, 30, 5, 33, 8, 0);
        stockpile_create(&game.stockpiles, &game.map, 30, 20, 33, 22, 0);
        stockpile_set_priority(&game.stockpiles, pile, 2);
        for (int i = 0; i < 3; i++) {
            spawn_hauler(&game, 10 + i, 20);
        }
        run_ticks(&game, 6000);
        hashes[run] = game_hash(&game);
        CHECK(count_in(&game, 30, 5, 33, 8, ITEM_WOOD) +
                  count_in(&game, 30, 5, 33, 8, ITEM_STONE_BLOCK) >
              20);
        game_shutdown(&game);
    }
    CHECK_EQ_UINT(hashes[0], hashes[1]);
}

static void test_destination_room_follows_the_filter(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int pile = stockpile_create(&game.stockpiles, &game.map, 3, 3, 5, 3, 0);
    stockpile_set_quality_range(&game.stockpiles, pile,
                                (IntRange){ITEM_QUALITY_STANDARD, ITEM_QUALITY_BEST});
    ItemStack good = item_stack_make(ITEM_WOOD, 5, ITEM_QUALITY_FINE, 60);
    ItemStack poor = item_stack_make(ITEM_WOOD, 5, ITEM_QUALITY_CRUDE, 60);
    int limit = item_stack_limit(ITEM_WOOD);
    CHECK_EQ_INT(haul_destination_room(&game.stockpiles, &game.items, 3, 3, 0, good), limit);
    CHECK_EQ_INT(haul_destination_room(&game.stockpiles, &game.items, 3, 3, 0, poor), 0);
    CHECK_EQ_INT(haul_destination_room(&game.stockpiles, &game.items, 9, 9, 0, good), 0);

    /* A tile holding wood the stockpile no longer accepts is not added to. */
    items_add(&game.items, 4, 3, 0, poor);
    CHECK_EQ_INT(haul_destination_room(&game.stockpiles, &game.items, 4, 3, 0, good), 0);
    items_add(&game.items, 5, 3, 0, good);
    CHECK_EQ_INT(haul_destination_room(&game.stockpiles, &game.items, 5, 3, 0, good), limit - 5);
    /* Accepted, but another grade lies there. */
    ItemStack standard = item_stack_make(ITEM_WOOD, 5, ITEM_QUALITY_STANDARD, 60);
    CHECK_EQ_INT(haul_destination_room(&game.stockpiles, &game.items, 5, 3, 0, standard), 0);
    CHECK_EQ_INT(haul_storage_room(&game.items, 5, 3, 0, standard), 0);
    CHECK_EQ_INT(haul_storage_room(&game.items, 5, 3, 0, good), limit - 5);
    game_shutdown(&game);
}

static void test_find_job_respects_condition_filters(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int pile = stockpile_create(&game.stockpiles, &game.map, 20, 5, 20, 5, 0);
    stockpile_set_quality_range(&game.stockpiles, pile,
                                (IntRange){ITEM_QUALITY_STANDARD, ITEM_QUALITY_BEST});
    items_add(&game.items, 5, 5, 0, item_stack_make(ITEM_WOOD, 10, ITEM_QUALITY_ROUGH, 60));
    HaulWorld world = world_of(&game);
    HaulJob job;
    CHECK(!haul_find_job(&world, (TilePos){4, 5, 0}, 100.0f, &job));

    items_add(&game.items, 8, 5, 0, item_stack_make(ITEM_WOOD, 10, ITEM_QUALITY_FINE, 60));
    REQUIRE(haul_find_job(&world, (TilePos){4, 5, 0}, 100.0f, &job));
    CHECK_EQ_INT(job.source.x, 8); /* The nearer stack is skipped. */
    CHECK_EQ_INT(job.destination.x, 20);

    /* Hit points too: only damaged items wanted here. */
    stockpile_set_quality_range(&game.stockpiles, pile,
                                (IntRange){ITEM_QUALITY_CRUDE, ITEM_QUALITY_BEST});
    stockpile_set_hit_points_range(&game.stockpiles, pile, (IntRange){0, 80});
    CHECK(!haul_find_job(&world, (TilePos){4, 5, 0}, 100.0f, &job));
    items_add(&game.items, 10, 5, 0, item_stack_make(ITEM_WOOD, 3, ITEM_QUALITY_FINE, 12));
    REQUIRE(haul_find_job(&world, (TilePos){4, 5, 0}, 100.0f, &job));
    CHECK_EQ_INT(job.source.x, 10);
    game_shutdown(&game);
}

static void test_items_rejected_by_a_new_filter_move_out(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int pile = stockpile_create(&game.stockpiles, &game.map, 10, 10, 10, 10, 0);
    items_add(&game.items, 10, 10, 0, item_stack_make(ITEM_WOOD, 10, ITEM_QUALITY_ROUGH, 60));
    HaulWorld world = world_of(&game);
    HaulJob job;
    CHECK(!haul_find_job(&world, (TilePos){10, 10, 0}, 100.0f, &job)); /* Stored. */

    stockpile_set_quality_range(&game.stockpiles, pile,
                                (IntRange){ITEM_QUALITY_STANDARD, ITEM_QUALITY_BEST});
    CHECK(!haul_find_job(&world, (TilePos){10, 10, 0}, 100.0f, &job)); /* Nowhere to go. */

    /* A lower priority stockpile that takes it: it is no longer stored, so
     * any accepting stockpile is better. */
    int dump = stockpile_create(&game.stockpiles, &game.map, 15, 10, 15, 10, 0);
    stockpile_set_priority(&game.stockpiles, dump, STOCKPILE_PRIORITY_LOWEST);
    REQUIRE(haul_find_job(&world, (TilePos){10, 10, 0}, 100.0f, &job));
    CHECK_EQ_INT(job.source.x, 10);
    CHECK_EQ_INT(job.destination.x, 15);

    spawn_hauler(&game, 12, 12);
    run_ticks(&game, 1500);
    CHECK_EQ_INT(count_at(&game, 10, 10, ITEM_WOOD), 0);
    CHECK_EQ_INT(count_at(&game, 15, 10, ITEM_WOOD), 10);
    CHECK_EQ_INT(items_get(&game.items, 15, 10, 0).quality, ITEM_QUALITY_ROUGH);
    game_shutdown(&game);
}

static void test_haulers_sort_items_by_condition(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    /* Good wood goes to a high priority stockpile, the rest to a low one. */
    int good = stockpile_create(&game.stockpiles, &game.map, 20, 10, 21, 10, 0);
    stockpile_set_priority(&game.stockpiles, good, 2);
    stockpile_set_quality_range(&game.stockpiles, good,
                                (IntRange){ITEM_QUALITY_FINE, ITEM_QUALITY_BEST});
    stockpile_set_hit_points_range(&game.stockpiles, good, (IntRange){50, 100});
    stockpile_create(&game.stockpiles, &game.map, 20, 14, 21, 14, 0);
    items_add(&game.items, 10, 10, 0, item_stack_make(ITEM_WOOD, 8, ITEM_QUALITY_FINE, 60));
    items_add(&game.items, 10, 12, 0, item_stack_make(ITEM_WOOD, 8, ITEM_QUALITY_CRUDE, 60));
    items_add(&game.items, 10, 14, 0, item_stack_make(ITEM_WOOD, 8, ITEM_QUALITY_FINE, 10));
    items_add(&game.items, 10, 16, 0, item_stack_make(ITEM_WOOD, 8, ITEM_QUALITY_FINE, 40));
    spawn_hauler(&game, 12, 12);
    run_ticks(&game, 6000);

    CHECK_EQ_INT(count_in(&game, 20, 10, 21, 10, ITEM_WOOD), 16);
    CHECK_EQ_INT(count_in(&game, 20, 14, 21, 14, ITEM_WOOD), 16);
    for (int x = 20; x <= 21; x++) {
        ItemStack stack = items_get(&game.items, x, 10, 0);
        if (stack.count > 0) {
            CHECK(stockpile_accepts(stockpile_get(&game.stockpiles, good), stack));
        }
    }
    /* The two fine stacks share a tile, with their average hit points
     * (8 × 60 + 8 × 40) / 16; crude and broken wood never mix into it. */
    ItemStack a = items_get(&game.items, 20, 10, 0), b = items_get(&game.items, 21, 10, 0);
    ItemStack merged = a.count == 16 ? a : b;
    CHECK_EQ_INT(merged.count, 16);
    CHECK_EQ_INT(merged.quality, ITEM_QUALITY_FINE);
    /* Loads merged in hand, then on the tile: rounding may differ by one. */
    CHECK(merged.hit_points >= 49 && merged.hit_points <= 51);
    game_shutdown(&game);
}

static void test_carried_items_keep_their_condition(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0,
              item_stack_make(ITEM_BLACKBERRIES, 30, ITEM_QUALITY_ROUGH, 7));
    stockpile_create(&game.stockpiles, &game.map, 20, 10, 20, 10, 0);
    Colonist *colonist = spawn_hauler(&game, 9, 10);
    REQUIRE(colonist != NULL);
    for (int i = 0; i < 2000 && colonist->carried.count == 0; i++) {
        game_tick(&game);
    }
    REQUIRE(colonist->carried.count == 30);
    CHECK_EQ_INT(colonist->carried.quality, ITEM_QUALITY_ROUGH);
    CHECK_EQ_INT(colonist->carried.hit_points, 7);
    run_ticks(&game, 1000);
    ItemStack stored = items_get(&game.items, 20, 10, 0);
    CHECK_EQ_INT(stored.count, 30);
    CHECK_EQ_INT(stored.quality, ITEM_QUALITY_ROUGH);
    CHECK_EQ_INT(stored.hit_points, 7);
    game_shutdown(&game);
}

static void test_find_extra_source(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int pile = stockpile_create(&game.stockpiles, &game.map, 20, 10, 20, 10, 0);
    stockpile_set_hit_points_range(&game.stockpiles, pile, (IntRange){50, 100});
    TilePos dest = {20, 10, 0};
    ItemStack load = item_stack_make(ITEM_WOOD, 2, ITEM_QUALITY_FINE, 60);
    items_add(&game.items, 11, 10, 0, item_stack_make(ITEM_WOOD, 3, ITEM_QUALITY_ROUGH, 60));
    items_add(&game.items, 10, 12, 0, item_stack_pristine(ITEM_STONE_BLOCK, 3));
    items_add(&game.items, 9, 10, 0, item_stack_make(ITEM_WOOD, 3, ITEM_QUALITY_FINE, 10));
    HaulWorld world = world_of(&game);
    TilePos out;
    /* Other grade, other type, too damaged for the destination: none fits. */
    CHECK(!haul_find_extra_source(&world, (TilePos){10, 10, 0}, load, dest, 12.0f, &out, NULL));

    items_add(&game.items, 10, 14, 0, item_stack_make(ITEM_WOOD, 3, ITEM_QUALITY_FINE, 40));
    REQUIRE(haul_find_extra_source(&world, (TilePos){10, 10, 0}, load, dest, 12.0f, &out, NULL));
    CHECK_EQ_INT(out.x, 10);
    CHECK_EQ_INT(out.y, 14);
    /* Out of reach of a short detour. */
    CHECK(!haul_find_extra_source(&world, (TilePos){10, 10, 0}, load, dest, 3.0f, &out, NULL));
    /* Reserved by another hauler. */
    reservations_add(&game.reservations, RESERVATION_HAUL_SOURCE, (TilePos){10, 14, 0}, 7);
    CHECK(!haul_find_extra_source(&world, (TilePos){10, 10, 0}, load, dest, 12.0f, &out, NULL));
    reservations_release_all(&game.reservations, 7);

    /* Already in a stockpile as good as the destination: stays there. */
    int other = stockpile_create(&game.stockpiles, &game.map, 10, 14, 10, 14, 0);
    CHECK(!haul_find_extra_source(&world, (TilePos){10, 10, 0}, load, dest, 12.0f, &out, NULL));
    stockpile_set_priority(&game.stockpiles, other, STOCKPILE_PRIORITY_LOWEST);
    CHECK(haul_find_extra_source(&world, (TilePos){10, 10, 0}, load, dest, 12.0f, &out, NULL));

    /* A better stockpile with room wants it: not taken along. */
    int better = stockpile_create(&game.stockpiles, &game.map, 30, 30, 30, 30, 0);
    stockpile_set_priority(&game.stockpiles, better, 1);
    CHECK(!haul_find_extra_source(&world, (TilePos){10, 10, 0}, load, dest, 12.0f, &out, NULL));
    /* No destination stockpile: nothing. */
    CHECK(!haul_find_extra_source(&world, (TilePos){10, 10, 0}, load, (TilePos){2, 2, 0}, 12.0f,
                                  &out, NULL));
    game_shutdown(&game);
}

/* Runs until the first items reach (x, y); returns how many arrived at once. */
static int first_delivery(Game *game, int x, int y, int max_ticks)
{
    for (int i = 0; i < max_ticks; i++) {
        game_tick(game);
        int count = items_get(&game->items, x, y, 0).count;
        if (count > 0) {
            return count;
        }
    }
    return 0;
}

static void test_hauler_collects_several_stacks_per_trip(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0,
              item_stack_make(ITEM_BLACKBERRIES, 10, ITEM_QUALITY_FINE, 10));
    items_add(&game.items, 12, 11, 0, item_stack_make(ITEM_BLACKBERRIES, 10, ITEM_QUALITY_FINE, 6));
    items_add(&game.items, 11, 13, 0, item_stack_make(ITEM_BLACKBERRIES, 10, ITEM_QUALITY_FINE, 8));
    stockpile_create(&game.stockpiles, &game.map, 25, 10, 25, 10, 0);
    Colonist *colonist = spawn_hauler(&game, 9, 10);
    REQUIRE(colonist != NULL);
    CHECK_EQ_INT(first_delivery(&game, 25, 10, 4000), 30); /* All three in one trip. */
    ItemStack stored = items_get(&game.items, 25, 10, 0);
    CHECK_EQ_INT(stored.quality, ITEM_QUALITY_FINE);
    CHECK_EQ_INT(stored.hit_points, 8); /* (10 × 10 + 10 × 6 + 10 × 8) / 30 */
    CHECK_EQ_INT(count_in(&game, 0, 0, 20, 20, ITEM_BLACKBERRIES), 0);
    CHECK_EQ_INT(game.reservations.count, 0);
    game_shutdown(&game);
}

static void test_collecting_stops_at_carrying_capacity(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    stockpile_create(&game.stockpiles, &game.map, 25, 10, 25, 10, 0);
    Colonist *colonist = spawn_hauler(&game, 9, 10);
    REQUIRE(colonist != NULL);
    /* Two stacks that together are more than one load. */
    int capacity = colonist_carry_capacity(colonist, ITEM_WOOD);
    REQUIRE(capacity >= 2);
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_WOOD, capacity - 1));
    items_add(&game.items, 11, 11, 0, item_stack_pristine(ITEM_WOOD, capacity - 1));
    int most = 0;
    for (int i = 0; i < 4000; i++) {
        game_tick(&game);
        most = colonist->carried.count > most ? colonist->carried.count : most;
    }
    CHECK_EQ_INT(most, capacity);
    /* The rest came on a second trip. */
    CHECK_EQ_INT(count_at(&game, 25, 10, ITEM_WOOD), 2 * (capacity - 1));
    game_shutdown(&game);
}

static void test_far_or_vanished_stacks_are_not_waited_for(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    items_add(&game.items, 12, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    items_add(&game.items, 10, 40, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10)); /* Far away. */
    stockpile_create(&game.stockpiles, &game.map, 25, 10, 25, 10, 0);
    Colonist *colonist = spawn_hauler(&game, 9, 10);
    REQUIRE(colonist != NULL);
    /* Picks up the first stack and heads for the second... */
    for (int i = 0; i < 2000 && colonist->carried.count == 0; i++) {
        game_tick(&game);
    }
    REQUIRE(colonist->carried.count == 10);
    CHECK(!colonist->haul_delivering);
    CHECK_EQ_INT(colonist->target.x, 12);
    /* ...which someone else takes meanwhile: it delivers what it holds. */
    items_remove(&game.items, 12, 10, 0, 10);
    CHECK_EQ_INT(first_delivery(&game, 25, 10, 2000), 10);
    CHECK_EQ_INT(count_at(&game, 10, 40, ITEM_BLACKBERRIES), 10); /* Not collected on the way. */
    game_shutdown(&game);
}

/* The check before a search is exact about the stacks: none left that a
 * stockpile with room accepts, nobody else took, in the hauler's region. */
static void test_any_job_possible_is_exact(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    stockpile_create(&game.stockpiles, &game.map, 20, 5, 20, 5, 0);
    /* A stored stack of fine wood with room left: rough wood lies about. */
    items_add(&game.items, 20, 5, 0, item_stack_make(ITEM_WOOD, 5, ITEM_QUALITY_FINE, 100));
    items_add(&game.items, 5, 5, 0, item_stack_make(ITEM_WOOD, 10, ITEM_QUALITY_ROUGH, 100));
    HaulWorld world = world_of(&game);
    CHECK(!haul_any_job_possible(&world)); /* Wood is allowed, but not that grade. */

    /* Fine wood, taken by another hauler. */
    items_add(&game.items, 30, 30, 0, item_stack_make(ITEM_WOOD, 3, ITEM_QUALITY_FINE, 100));
    CHECK(haul_any_job_possible(&world));
    reservations_add(&game.reservations, RESERVATION_HAUL_SOURCE, (TilePos){30, 30, 0}, 2);
    CHECK(!haul_any_job_possible(&world));
    reservations_release_all(&game.reservations, 2);
    CHECK(haul_any_job_possible(&world));

    /* Walled off from the hauler: another region. */
    for (int y = 28; y <= 32; y++) {
        for (int x = 28; x <= 32; x++) {
            bool edge = x == 28 || x == 32 || y == 28 || y == 32;
            if (edge) {
                map_set_rock(&game.map, x, y, 0, ROCK_GRANITE);
            }
        }
    }
    regions_update(&game.regions, &game.map);
    world.regions = &game.regions;
    world.region = regions_at(&game.regions, &game.map, 4, 5, 0);
    world.from_y = 5;
    CHECK(!haul_any_job_possible(&world));
    world.region = regions_at(&game.regions, &game.map, 30, 30, 0); /* A hauler in there. */
    CHECK(haul_any_job_possible(&world));
    game_shutdown(&game);
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
    RUN_TEST(test_gathering_comes_before_hauling_at_same_priority);
    RUN_TEST(test_new_stockpile_is_noticed_despite_backoff);
    RUN_TEST(test_destination_room_follows_the_filter);
    RUN_TEST(test_find_job_respects_condition_filters);
    RUN_TEST(test_items_rejected_by_a_new_filter_move_out);
    RUN_TEST(test_haulers_sort_items_by_condition);
    RUN_TEST(test_carried_items_keep_their_condition);
    RUN_TEST(test_find_extra_source);
    RUN_TEST(test_any_job_possible_is_exact);
    RUN_TEST(test_hauler_collects_several_stacks_per_trip);
    RUN_TEST(test_collecting_stops_at_carrying_capacity);
    RUN_TEST(test_far_or_vanished_stacks_are_not_waited_for);
    RUN_TEST(test_hauling_is_deterministic);
    return test_summary();
}
