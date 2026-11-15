/* Debug cheats (GameDebug): instant free construction, free crafts, work without
 * tools, instant taking apart. */
#include "core/game.h"

#include "test_framework.h"

#include <string.h>

/* A small game on a flat, empty soil map with no starting colonists, at noon. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 7;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = 0; i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->cobbles[i] = MAP_STONES_MAX;
        map->flint[i] = 0;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map);
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

/* Rested colonist at level 75 in every knowledge. */
static Colonist *spawn_worker(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y));
    if (colonist != NULL) {
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
        for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
            colonist_set_knowledge_level(colonist, (KnowledgeType)k, 75.0);
        }
        colonist_set_strength(colonist, COLONIST_STRENGTH_STANDARD);
    }
    return colonist;
}

/* Keeps needs satisfied and the time at noon. */
static void run_ticks(Game *game, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
            if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
                for (int need = 0; need < NEED_COUNT; need++) {
                    game->colonists[slot].needs[need] = 1.0f;
                }
            }
        }
        game->clock.tick = 12 * TICKS_PER_HOUR;
        game_tick(game);
    }
}

static long items_total(const Game *game)
{
    long total = 0;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        total += game->items.totals[type];
    }
    return total;
}

static void test_instant_build(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Plant *bush = &game.map.plants[map_index(&game.map, 14, 10)];
    *bush = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 10, 10, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    REQUIRE(game_place_structure(&game, STRUCTURE_GRASS_BED, 10, 14, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    REQUIRE(game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 14, 10));
    REQUIRE(construction_at(&game.constructions, 14, 10) != CONSTRUCTION_NONE);
    /* A roof planned on a wall's site, then left with nothing to rest on. */
    int wall = game_place_structure(&game, STRUCTURE_WALL, 31, 30, DIRECTION_EAST);
    REQUIRE(wall != CONSTRUCTION_NONE);
    REQUIRE(game_place_structure(&game, STRUCTURE_ROOF, 30, 30, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    REQUIRE(game_remove_construction(&game, wall));

    /* Off: nothing happens without builders. */
    run_ticks(&game, 10);
    CHECK_EQ_INT(game.constructions.used_count, 4);

    uint64_t rng_state = game.rng.state;
    game_set_instant_build(&game, true);
    run_ticks(&game, 1);
    CHECK_EQ_INT(map_structure(&game.map, 10, 10), STRUCTURE_WALL);
    CHECK_EQ_INT(map_structure(&game.map, 10, 14), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map_structure_quality(&game.map, 10, 10), ITEM_QUALITY_STANDARD);
    CHECK(workstation_at(&game.workstations, 14, 10) != WORKSTATION_NONE);
    CHECK_EQ_INT(bush->type, PLANT_NONE); /* Cleared first. */
    /* The roof waits for support. */
    CHECK_EQ_INT(game.constructions.used_count, 1);
    CHECK_EQ_INT(map_roof(&game.map, 30, 30), STRUCTURE_NONE);
    CHECK(game.rng.state == rng_state); /* Grades are the average, not drawn. */

    /* A wall beside it: built, then the roof it holds up. */
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 31, 30, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    long items = items_total(&game);
    run_ticks(&game, 2);
    CHECK_EQ_INT(map_structure(&game.map, 31, 30), STRUCTURE_WALL);
    CHECK_EQ_INT(map_roof(&game.map, 30, 30), STRUCTURE_ROOF);
    CHECK_EQ_INT(game.constructions.used_count, 0);
    CHECK_EQ_INT((int)items_total(&game), (int)items); /* Nothing used, nothing dropped. */

    /* Off again: blueprints wait for builders. */
    game_set_instant_build(&game, false);
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 20, 20, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    run_ticks(&game, 10);
    CHECK_EQ_INT(map_structure(&game.map, 20, 20), STRUCTURE_NONE);
    game_shutdown(&game);
}

/* A wall built at once on a stockpile tile (drawn over its blueprint) takes
 * it out of the stockpile, as a builder's work does. */
static void test_instant_build_leaves_zones(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 11, 10, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    REQUIRE(game_add_stockpile_rect(&game, 10, 10, 12, 10, STOCKPILE_NONE) != STOCKPILE_NONE);
    REQUIRE(stockpile_at(&game.stockpiles, 11, 10) != STOCKPILE_NONE);
    game_set_instant_build(&game, true);
    run_ticks(&game, 1);
    CHECK_EQ_INT(map_structure(&game.map, 11, 10), STRUCTURE_WALL);
    CHECK(stockpile_at(&game.stockpiles, 11, 10) == STOCKPILE_NONE);
    CHECK(stockpile_at(&game.stockpiles, 10, 10) != STOCKPILE_NONE);
    game_shutdown(&game);
}

static void test_bill_free_input(void)
{
    Bill bill = {.recipe = RECIPE_HAND_AXE, .inputs = recipe_inputs(RECIPE_HAND_AXE)};
    const RecipeDef *def = recipe_def(RECIPE_HAND_AXE);
    ItemStack input = bill_free_input(&bill);
    CHECK_EQ_INT(input.type, def->inputs[0]);
    CHECK_EQ_INT(input.count, def->input_count);
    CHECK(input.quality >= ITEM_QUALITY_STANDARD);
    CHECK(recipe_accepts_input(RECIPE_HAND_AXE, input));
    bill.inputs = 0; /* Every input refused. */
    CHECK_EQ_INT(bill_free_input(&bill).count, 0);
}

/* A knapper with a hammer, a knapping spot with a hand axe bill, and no flint
 * anywhere. Returns the station. */
static int knapping_without_flint(Game *game, Colonist **knapper)
{
    *knapper = spawn_worker(game, 10, 10);
    colonist_set_knowledge_level(*knapper, KNOWLEDGE_STONE_KNAPPING, 95.0);
    colonist_add_tool(*knapper, item_stack_pristine(ITEM_COBBLE, 1));
    int index = workstations_place(&game->workstations, &game->map, WORKSTATION_KNAPPING_SPOT,
                                   14, 10);
    workstation_add_bill(&game->workstations, index, RECIPE_HAND_AXE);
    return index;
}

static void test_free_crafts(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *knapper;
    int index = knapping_without_flint(&game, &knapper);
    REQUIRE(knapper != NULL && index != WORKSTATION_NONE);
    const Workstation *station = workstation_get_const(&game.workstations, index);
    CHECK(!game_bill_has_input(&game, &station->bills[0]));
    run_ticks(&game, 300);
    CHECK(knapper->task != TASK_CRAFT);

    game_set_free_crafts(&game, true);
    CHECK(game_bill_has_input(&game, &station->bills[0]));
    for (int i = 0; i < 20 && game.items.totals[ITEM_HAND_AXE] == 0; i++) {
        run_ticks(&game, 300);
    }
    CHECK(game.items.totals[ITEM_HAND_AXE] > 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLINT], 0);
    CHECK_EQ_INT(station->bills[0].done, (int)game.items.totals[ITEM_HAND_AXE]);
    game_shutdown(&game);
}

/* An input made out of nothing is not given back when the craft is called
 * off: neither the station nor the bill leaves flint behind. */
static void test_free_input_is_not_given_back(void)
{
    for (int remove_station = 0; remove_station < 2; remove_station++) {
        Game game;
        REQUIRE(init_flat_game(&game));
        Colonist *knapper;
        int index = knapping_without_flint(&game, &knapper);
        game_set_free_crafts(&game, true);
        const Workstation *station = workstation_get_const(&game.workstations, index);
        for (int i = 0; i < 600 && station->work_input.count == 0; i++) {
            run_ticks(&game, 1);
        }
        REQUIRE(station->work_input.count > 0);
        CHECK(station->free_input);
        if (remove_station) {
            CHECK(game_remove_workstation(&game, index));
        } else {
            CHECK(game_remove_bill(&game, index, 0));
            CHECK_EQ_INT(station->work_input.count, 0);
            CHECK(!station->free_input);
        }
        CHECK_EQ_INT((int)game.items.totals[ITEM_FLINT], 0);
        run_ticks(&game, 10);
        CHECK(knapper->task != TASK_CRAFT);
        game_shutdown(&game);
    }

    /* A real input is still given back. */
    Workstations stations;
    REQUIRE(workstations_init(&stations, 16, 16));
    Map map;
    REQUIRE(map_init(&map, 16, 16));
    int index = workstations_place(&stations, &map, WORKSTATION_KNAPPING_SPOT, 2, 2);
    REQUIRE(workstation_add_bill(&stations, index, RECIPE_HAND_AXE) == 0);
    REQUIRE(workstation_start_free_craft(&stations, index, 0));
    CHECK(stations.list[index].free_input);
    CHECK(!workstation_start_free_craft(&stations, index, 0)); /* Already one. */
    ItemStack returned[1];
    CHECK_EQ_INT(workstations_remove(&stations, index, returned), 0);
    index = workstations_place(&stations, &map, WORKSTATION_KNAPPING_SPOT, 2, 2);
    workstation_add_bill(&stations, index, RECIPE_HAND_AXE);
    REQUIRE(workstation_start_craft(&stations, index, RECIPE_HAND_AXE,
                                    item_stack_pristine(ITEM_FLINT, 1)));
    CHECK(!stations.list[index].free_input);
    CHECK_EQ_INT(workstations_remove(&stations, index, returned), 1);
    CHECK_EQ_INT(returned[0].type, ITEM_FLINT);
    map_free(&map);
    workstations_free(&stations);
}

/* Without a hammer, nobody knaps; with the cheat, the knapper does, bare-handed. */
static void test_no_tools(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *knapper = spawn_worker(&game, 10, 10);
    REQUIRE(knapper != NULL);
    colonist_set_knowledge_level(knapper, KNOWLEDGE_STONE_KNAPPING, 95.0);
    int index = workstations_place(&game.workstations, &game.map, WORKSTATION_KNAPPING_SPOT,
                                   14, 10);
    workstation_add_bill(&game.workstations, index, RECIPE_HAND_AXE);
    items_add(&game.items, 16, 10, item_stack_pristine(ITEM_FLINT, 3));
    CHECK(game_recipe_missing_tools(&game, RECIPE_HAND_AXE) != 0);
    run_ticks(&game, 300);
    CHECK(knapper->task != TASK_CRAFT);

    game_set_no_tools(&game, true);
    CHECK_EQ_INT((int)game_recipe_missing_tools(&game, RECIPE_HAND_AXE), 0);
    for (int i = 0; i < 20 && game.items.totals[ITEM_HAND_AXE] == 0; i++) {
        run_ticks(&game, 300);
    }
    CHECK(game.items.totals[ITEM_HAND_AXE] > 0);
    CHECK(game.items.totals[ITEM_FLINT] < 3); /* Made of the flint lying there. */
    CHECK_EQ_INT(colonist_tool_index(knapper, ITEM_TYPE_BIT(ITEM_COBBLE)), -1);
    game_shutdown(&game);
}

/* Orders to take apart and to take roofs down are carried out at once; a
 * roof left without support falls in the same tick, and the refresh site of
 * a bed taken apart goes with it. */
static void test_instant_deconstruct(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    map_set_structure(&game.map, 10, 10, STRUCTURE_WALL);
    map_set_structure(&game.map, 14, 10, STRUCTURE_WALL);
    map_place_structure(&game.map, 10, 14, STRUCTURE_GRASS_BED, DIRECTION_EAST,
                        ITEM_QUALITY_STANDARD);
    map_set_roof(&game.map, 11, 10, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD); /* (10, 10)'s. */
    map_set_roof(&game.map, 13, 10, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD); /* (14, 10)'s. */
    int refresh = constructions_place_refresh(&game.constructions, &game.map, 10, 14);
    REQUIRE(refresh != CONSTRUCTION_NONE);
    const ConstructionSite *site = construction_get(&game.constructions, refresh);
    construction_deliver(&game.constructions, refresh,
                         item_stack_pristine(construction_material(site, 0), 1));
    CHECK_EQ_INT(game_designate_rect(&game, 10, 10, 10, 10, DESIGNATION_DECONSTRUCT), 1);
    CHECK_EQ_INT(game_designate_rect(&game, 11, 14, 11, 14, DESIGNATION_DECONSTRUCT), 1);
    CHECK_EQ_INT(game_designate_rect(&game, 13, 10, 13, 10, DESIGNATION_REMOVE_ROOF), 1);
    run_ticks(&game, 10);
    CHECK_EQ_INT(map_structure(&game.map, 10, 10), STRUCTURE_WALL); /* Nobody to do it. */

    uint32_t notices = game.notices.count;
    game_set_instant_deconstruct(&game, true);
    run_ticks(&game, 1);
    CHECK_EQ_INT(game.designations.count, 0);
    CHECK_EQ_INT(map_structure(&game.map, 10, 10), STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure(&game.map, 10, 14), STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure(&game.map, 11, 14), STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure(&game.map, 14, 10), STRUCTURE_WALL);
    CHECK_EQ_INT(map_roof(&game.map, 13, 10), STRUCTURE_NONE); /* Taken down. */
    CHECK_EQ_INT(map_roof(&game.map, 11, 10), STRUCTURE_NONE); /* Fell in. */
    CHECK_EQ_INT(game.notices.count, notices + 1);
    CHECK_EQ_INT(game.constructions.used_count, 0);
    CHECK(game.items.totals[ITEM_GRASS] > 0);
    game_shutdown(&game);
}

/* The cheats count in the hash only while on: a game where they were never
 * touched hashes as before. */
static void test_hash(void)
{
    Game a, b;
    REQUIRE(init_flat_game(&a));
    REQUIRE(init_flat_game(&b));
    CHECK(game_hash(&a) == game_hash(&b));
    game_set_free_crafts(&b, true);
    CHECK(game_hash(&a) != game_hash(&b));
    game_set_free_crafts(&b, false);
    game_set_instant_build(&b, true);
    CHECK(game_hash(&a) != game_hash(&b));
    game_set_instant_build(&b, false);
    CHECK(game_hash(&a) == game_hash(&b));
    game_set_no_tools(&b, true);
    CHECK(game_hash(&a) != game_hash(&b));
    game_set_no_tools(&b, false);
    game_set_instant_deconstruct(&b, true);
    CHECK(game_hash(&a) != game_hash(&b));
    game_set_instant_deconstruct(&b, false);
    CHECK(game_hash(&a) == game_hash(&b));
    game_shutdown(&a);
    game_shutdown(&b);
}

int main(void)
{
    RUN_TEST(test_instant_build);
    RUN_TEST(test_instant_build_leaves_zones);
    RUN_TEST(test_bill_free_input);
    RUN_TEST(test_free_crafts);
    RUN_TEST(test_free_input_is_not_given_back);
    RUN_TEST(test_no_tools);
    RUN_TEST(test_instant_deconstruct);
    RUN_TEST(test_hash);
    return test_summary();
}
