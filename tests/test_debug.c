/* Debug cheats (GameDebug): instant free construction, free crafts, work without
 * tools, instant taking apart, instant felling and mining; and the one-shot
 * debug commands (knowledge, time, items, teleport). */
#include "core/game.h"
#include "core/tree.h"

#include "test_framework.h"

#include <stdlib.h>
#include <string.h>

/* A small game on a flat, empty soil map with no starting colonists, at noon. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true; /* One level of ground, open air over it. */
    params.world.seed = 7;
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
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_COBBLE));
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_FLINT));
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_BRANCHES) | ITEM_TYPE_BIT(ITEM_TWIGS));
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
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, 0));
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
    Plant *bush = &game.map.plants[map_index(&game.map, 14, 10, 0)];
    *bush = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 10, 10, 0, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    REQUIRE(game_place_structure(&game, STRUCTURE_GRASS_BED, 10, 14, 0, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    REQUIRE(game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 14, 10, 0));
    REQUIRE(construction_at(&game.constructions, 14, 10, 0) != CONSTRUCTION_NONE);
    /* A roof planned over a wall's site, then left with nothing to rest on. */
    int wall = game_place_structure(&game, STRUCTURE_WALL, 31, 30, 0, DIRECTION_EAST);
    REQUIRE(wall != CONSTRUCTION_NONE);
    REQUIRE(game_place_structure(&game, STRUCTURE_ROOF, 30, 30, 1, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    REQUIRE(game_remove_construction(&game, wall));

    /* Off: nothing happens without builders. */
    run_ticks(&game, 10);
    CHECK_EQ_INT(game.constructions.used_count, 4);

    uint64_t rng_state = game.rng.state;
    game_set_instant_build(&game, true);
    run_ticks(&game, 1);
    CHECK_EQ_INT(map_structure(&game.map, 10, 10, 0), STRUCTURE_WALL);
    CHECK_EQ_INT(map_structure(&game.map, 10, 14, 0), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map_structure_quality(&game.map, 10, 10, 0), ITEM_QUALITY_STANDARD);
    CHECK(workstation_at(&game.workstations, 14, 10, 0) != WORKSTATION_NONE);
    CHECK_EQ_INT(bush->type, PLANT_NONE); /* Cleared first. */
    /* The roof waits for support. */
    CHECK_EQ_INT(game.constructions.used_count, 1);
    CHECK_EQ_INT(map_roof(&game.map, 30, 30, 1), STRUCTURE_NONE);
    CHECK(game.rng.state == rng_state); /* Grades are the average, not drawn. */

    /* A wall beside it: built, then the roof it holds up. */
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 31, 30, 0, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    long items = items_total(&game);
    run_ticks(&game, 2);
    CHECK_EQ_INT(map_structure(&game.map, 31, 30, 0), STRUCTURE_WALL);
    CHECK_EQ_INT(map_roof(&game.map, 30, 30, 1), STRUCTURE_ROOF);
    CHECK_EQ_INT(game.constructions.used_count, 0);
    CHECK_EQ_INT((int)items_total(&game), (int)items); /* Nothing used, nothing dropped. */

    /* Off again: blueprints wait for builders. */
    game_set_instant_build(&game, false);
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 20, 20, 0, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    run_ticks(&game, 10);
    CHECK_EQ_INT(map_structure(&game.map, 20, 20, 0), STRUCTURE_NONE);
    game_shutdown(&game);
}

/* A wall built at once on a stockpile tile (drawn over its blueprint) takes
 * it out of the stockpile, as a builder's work does. */
static void test_instant_build_leaves_zones(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 11, 10, 0, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    REQUIRE(game_add_stockpile_rect(&game, 10, 10, 12, 10, 0, STOCKPILE_NONE) != STOCKPILE_NONE);
    REQUIRE(stockpile_at(&game.stockpiles, 11, 10, 0) != STOCKPILE_NONE);
    game_set_instant_build(&game, true);
    run_ticks(&game, 1);
    CHECK_EQ_INT(map_structure(&game.map, 11, 10, 0), STRUCTURE_WALL);
    CHECK(stockpile_at(&game.stockpiles, 11, 10, 0) == STOCKPILE_NONE);
    CHECK(stockpile_at(&game.stockpiles, 10, 10, 0) != STOCKPILE_NONE);
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
    int index =
        workstations_place(&game->workstations, &game->map, WORKSTATION_KNAPPING_SPOT, 14, 10, 0);
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
    REQUIRE(workstations_init(&stations, 16, 16, 1));
    Map map;
    REQUIRE(map_init(&map, 16, 16, 1));
    int index = workstations_place(&stations, &map, WORKSTATION_KNAPPING_SPOT, 2, 2, 0);
    REQUIRE(workstation_add_bill(&stations, index, RECIPE_HAND_AXE) == 0);
    REQUIRE(workstation_start_free_craft(&stations, index, 0));
    CHECK(stations.list[index].free_input);
    CHECK(!workstation_start_free_craft(&stations, index, 0)); /* Already one. */
    ItemStack returned[1];
    CHECK_EQ_INT(workstations_remove(&stations, index, returned), 0);
    index = workstations_place(&stations, &map, WORKSTATION_KNAPPING_SPOT, 2, 2, 0);
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
    int index =
        workstations_place(&game.workstations, &game.map, WORKSTATION_KNAPPING_SPOT, 14, 10, 0);
    workstation_add_bill(&game.workstations, index, RECIPE_HAND_AXE);
    items_add(&game.items, 16, 10, 0, item_stack_pristine(ITEM_FLINT, 3));
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
    map_set_structure(&game.map, 10, 10, 0, STRUCTURE_WALL);
    map_set_structure(&game.map, 14, 10, 0, STRUCTURE_WALL);
    map_place_structure(&game.map, 10, 14, 0, STRUCTURE_GRASS_BED, DIRECTION_EAST,
                        ITEM_QUALITY_STANDARD);
    map_set_roof(&game.map, 11, 10, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD); /* (10, 10)'s. */
    map_set_roof(&game.map, 13, 10, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD); /* (14, 10)'s. */
    int refresh = constructions_place_refresh(&game.constructions, &game.map, 10, 14, 0);
    REQUIRE(refresh != CONSTRUCTION_NONE);
    const ConstructionSite *site = construction_get(&game.constructions, refresh);
    construction_deliver(&game.constructions, refresh,
                         item_stack_pristine(construction_material(site, 0), 1));
    CHECK_EQ_INT(game_designate_rect(&game, 10, 10, 10, 10, 0, DESIGNATION_DECONSTRUCT), 1);
    CHECK_EQ_INT(game_designate_rect(&game, 11, 14, 11, 14, 0, DESIGNATION_DECONSTRUCT), 1);
    CHECK_EQ_INT(game_designate_rect(&game, 13, 10, 13, 10, 1, DESIGNATION_DECONSTRUCT), 1);
    run_ticks(&game, 10);
    CHECK_EQ_INT(map_structure(&game.map, 10, 10, 0), STRUCTURE_WALL); /* Nobody to do it. */

    uint32_t notices = game.notices.count;
    game_set_instant_deconstruct(&game, true);
    run_ticks(&game, 1);
    CHECK_EQ_INT(game.designations.count, 0);
    CHECK_EQ_INT(map_structure(&game.map, 10, 10, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure(&game.map, 10, 14, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure(&game.map, 11, 14, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure(&game.map, 14, 10, 0), STRUCTURE_WALL);
    CHECK_EQ_INT(map_roof(&game.map, 13, 10, 1), STRUCTURE_NONE); /* Taken down. */
    CHECK_EQ_INT(map_roof(&game.map, 11, 10, 1), STRUCTURE_NONE); /* Fell in. */
    CHECK(map_is_open(&game.map, 11, 10, 1));
    CHECK_EQ_INT(game.notices.count, notices + 1);
    CHECK_EQ_INT(game.constructions.used_count, 0);
    CHECK(game.items.totals[ITEM_GRASS] > 0);
    game_shutdown(&game);
}

/* Orders to fell trees and to mine rock, which nobody can carry out yet, can
 * be given with the cheat on, and are carried out at once. */
static void test_instant_fell_and_mine(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    REQUIRE(spawn_worker(&game, 30, 30) != NULL);
    Map *map = &game.map;
    map->plants[map_index(map, 10, 10, 0)] = (Plant){.type = PLANT_TREE, .growth = 1.0f};
    tree_build(map, 10, 10, 0);
    map_set_rock(map, 20, 20, 0, ROCK_GRANITE);
    map_set_rock(map, 22, 20, 0, ROCK_GRANITE);
    CHECK(!game_designation_available(&game, DESIGNATION_CHOP));
    CHECK(game_designation_available(&game, DESIGNATION_MINE)); /* A shelter builder digs earth. */
    CHECK(game_designation_available(&game, DESIGNATION_DIG));
    /* Given from a tile of the trunk: the order goes on its foot. */
    CHECK_EQ_INT(game_designate_rect(&game, 10, 10, 10, 10, 1, DESIGNATION_CHOP), 1);
    CHECK_EQ_INT(designation_get(&game.designations, 10, 10, 0), DESIGNATION_CHOP);
    CHECK_EQ_INT(game_designate_rect(&game, 20, 20, 20, 20, 0, DESIGNATION_MINE), 1);
    run_ticks(&game, 10);
    CHECK(map->rock[map_index(map, 10, 10, 0)] == ROCK_WOOD); /* Nobody to do it. */
    CHECK(map->rock[map_index(map, 20, 20, 0)] == ROCK_GRANITE);

    game_set_instant_fell_and_mine(&game, true);
    CHECK(game_designation_available(&game, DESIGNATION_CHOP));
    CHECK(game_designation_available(&game, DESIGNATION_MINE));
    CHECK_EQ_INT(game_designate_rect(&game, 22, 20, 22, 20, 0, DESIGNATION_MINE), 1);
    long before = items_total(&game);
    run_ticks(&game, 1);
    CHECK_EQ_INT(game.designations.count, 0);
    CHECK_EQ_INT(map->plants[map_index(map, 10, 10, 0)].type, PLANT_NONE);
    CHECK(map->rock[map_index(map, 10, 10, 0)] == ROCK_NONE);
    CHECK(map->rock[map_index(map, 10, 10, 1)] == ROCK_NONE);
    CHECK(map->rock[map_index(map, 20, 20, 0)] == ROCK_NONE);
    CHECK(map->rock[map_index(map, 22, 20, 0)] == ROCK_NONE);
    CHECK_EQ_INT((int)game.items.totals[ITEM_WOOD], plant_def(PLANT_TREE)->cut_yield);
    CHECK(items_total(&game) > before + plant_def(PLANT_TREE)->cut_yield); /* And stone. */

    /* An order whose tree is gone goes. */
    map->plants[map_index(map, 14, 14, 0)] = (Plant){.type = PLANT_TREE, .growth = 0.3f};
    game_set_instant_fell_and_mine(&game, false);
    REQUIRE(game_designate_rect(&game, 14, 14, 14, 14, 0, DESIGNATION_CHOP) == 1);
    map->plants[map_index(map, 14, 14, 0)] = (Plant){0};
    game_set_instant_fell_and_mine(&game, true);
    run_ticks(&game, 1);
    CHECK_EQ_INT(game.designations.count, 0);
    game_shutdown(&game);
}

static void test_weather_cheats(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    CHECK_EQ_INT(game.weather.type, WEATHER_CLEAR);
    WeatherType order[] = {WEATHER_CLOUDY, WEATHER_FOG, WEATHER_RAIN, WEATHER_STORM, WEATHER_CLEAR};
    for (int i = 0; i < 5; i++) {
        game_debug_next_weather(&game);
        CHECK_EQ_INT(game.weather.type, order[i]);
    }
    float before = game_outdoor_temperature(&game);
    game_debug_cold_snap(&game);
    CHECK_NEAR(game.weather.spell, -WEATHER_SPELL_MAX, 1e-6f);
    CHECK(game_outdoor_temperature(&game) < before);
    game_shutdown(&game);
}

/* The cheats count in the hash only while on: a game where they were never
 * touched hashes as before. */
static void test_hash(void)
{
    static Game a, b;
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
    game_set_instant_fell_and_mine(&b, true);
    CHECK(game_hash(&a) != game_hash(&b));
    game_set_instant_fell_and_mine(&b, false);
    CHECK(game_hash(&a) == game_hash(&b));
    game_shutdown(&a);
    game_shutdown(&b);
}

/* The character editor's steps: needs, attributes, knowledge; limits hold. */
static void test_character_editor(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId id = game_spawn_colonist(&game, 10, 10, 0);
    Colonist *colonist = game_colonist(&game, id);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = 0.5f;
    CHECK(game_debug_colonist_need(&game, id, NEED_FOOD, 1));
    CHECK_NEAR(colonist->needs[NEED_FOOD], 0.6, 1e-5);
    CHECK(game_debug_colonist_need(&game, id, NEED_FOOD, -1));
    CHECK(game_debug_colonist_need(&game, id, NEED_FOOD, -1));
    CHECK_NEAR(colonist->needs[NEED_FOOD], 0.4, 1e-5);
    for (int i = 0; i < 12; i++) {
        game_debug_colonist_need(&game, id, NEED_FOOD, 1);
    }
    CHECK_NEAR(colonist->needs[NEED_FOOD], 1.0, 1e-6);
    for (int i = 0; i < 12; i++) {
        game_debug_colonist_need(&game, id, NEED_FOOD, -1);
    }
    CHECK_NEAR(colonist->needs[NEED_FOOD], 0.0, 1e-6);
    CHECK(!game_debug_colonist_need(&game, id, NEED_COUNT, 1));
    CHECK(!game_debug_colonist_need(&game, ENTITY_NONE, NEED_FOOD, 1));

    /* An attribute moves by 5 percent, up to either end; the mood character follows. */
    attributes_set(&colonist->attributes, ATTRIBUTE_PAIN_TOLERANCE, 0);
    CHECK(game_debug_colonist_attribute(&game, id, ATTRIBUTE_PAIN_TOLERANCE, 1));
    CHECK_EQ_INT(colonist->attributes.position[ATTRIBUTE_PAIN_TOLERANCE],
                 GAME_DEBUG_ATTRIBUTE_STEP);
    CHECK(game_debug_colonist_attribute(&game, id, ATTRIBUTE_PAIN_TOLERANCE, -1));
    CHECK(game_debug_colonist_attribute(&game, id, ATTRIBUTE_PAIN_TOLERANCE, -1));
    CHECK_EQ_INT(colonist->attributes.position[ATTRIBUTE_PAIN_TOLERANCE],
                 -GAME_DEBUG_ATTRIBUTE_STEP);
    for (int i = 0; i < 30; i++) {
        game_debug_colonist_attribute(&game, id, ATTRIBUTE_PAIN_TOLERANCE, 1);
    }
    CHECK_EQ_INT(colonist->attributes.position[ATTRIBUTE_PAIN_TOLERANCE], ATTRIBUTE_POSITION_MAX);
    float tolerant = colonist->health.pain_factor;
    for (int i = 0; i < 50; i++) {
        game_debug_colonist_attribute(&game, id, ATTRIBUTE_PAIN_TOLERANCE, -1);
    }
    CHECK_EQ_INT(colonist->attributes.position[ATTRIBUTE_PAIN_TOLERANCE], -ATTRIBUTE_POSITION_MAX);
    CHECK(colonist->health.pain_factor > tolerant); /* The factors follow. */
    CHECK(!game_debug_colonist_attribute(&game, id, ATTRIBUTE_COUNT, 1));

    /* A knowledge moves by 5 levels, its peak with it (nothing to relearn). */
    colonist_set_knowledge_level(colonist, KNOWLEDGE_FIRE, 50.0);
    colonist->knowledge_peak[KNOWLEDGE_FIRE] = 80.0;
    CHECK(game_debug_colonist_knowledge(&game, id, KNOWLEDGE_FIRE, 1));
    CHECK_NEAR(colonist->knowledge[KNOWLEDGE_FIRE], 55.0, 1e-9);
    CHECK_NEAR(colonist->knowledge_peak[KNOWLEDGE_FIRE], 55.0, 1e-9);
    for (int i = 0; i < 30; i++) {
        game_debug_colonist_knowledge(&game, id, KNOWLEDGE_FIRE, 1);
    }
    CHECK_NEAR(colonist->knowledge[KNOWLEDGE_FIRE], KNOWLEDGE_LEVEL_MAX, 1e-9);
    for (int i = 0; i < 30; i++) {
        game_debug_colonist_knowledge(&game, id, KNOWLEDGE_FIRE, -1);
    }
    CHECK_NEAR(colonist->knowledge[KNOWLEDGE_FIRE], KNOWLEDGE_LEVEL_MIN, 1e-9);
    CHECK(!game_debug_colonist_knowledge(&game, id, KNOWLEDGE_NONE, 1));

    /* An animal's needs. */
    EntityId deer = game_debug_spawn_animal(&game, ANIMAL_ROE_DEER, ANIMAL_FEMALE, 20, 20, 0);
    REQUIRE(!entity_id_is_none(deer));
    Animal *animal = animals_get(&game.animals, deer);
    animal->needs[ANIMAL_NEED_WATER] = 0.5f;
    CHECK(game_debug_animal_need(&game, deer, ANIMAL_NEED_WATER, -1));
    CHECK_NEAR(animal->needs[ANIMAL_NEED_WATER], 0.4, 1e-5);
    CHECK(!game_debug_animal_need(&game, deer, ANIMAL_NEED_COUNT, 1));
    CHECK(!game_debug_animal_need(&game, ENTITY_NONE, ANIMAL_NEED_WATER, 1));
    game_shutdown(&game);
}

/* The air's temperature goes up and down by steps, within limits, and the
 * tiles under the sky follow; the hash tells it apart, and only when it is set. */
static void test_temperature_steps(void)
{
    static Game a, b;
    REQUIRE(init_flat_game(&a));
    REQUIRE(init_flat_game(&b));
    float normal = game_outdoor_temperature(&a);
    game_debug_temperature(&b, 1);
    CHECK_NEAR(game_outdoor_temperature(&b), normal + GAME_DEBUG_TEMPERATURE_STEP, 1e-4);
    CHECK(game_hash(&a) != game_hash(&b));
    game_debug_temperature(&b, -1);
    CHECK_NEAR(game_outdoor_temperature(&b), normal, 1e-4);
    CHECK(game_hash(&a) == game_hash(&b));
    game_debug_temperature(&b, -1);
    CHECK_NEAR(game_outdoor_temperature(&b), normal - GAME_DEBUG_TEMPERATURE_STEP, 1e-4);
    for (int i = 0; i < 40; i++) {
        game_debug_temperature(&b, -1);
    }
    CHECK_NEAR(game_outdoor_temperature(&b), normal - GAME_DEBUG_TEMPERATURE_LIMIT, 1e-4);
    for (int i = 0; i < 40; i++) {
        game_debug_temperature(&b, 1);
    }
    CHECK_NEAR(game_outdoor_temperature(&b), normal + GAME_DEBUG_TEMPERATURE_LIMIT, 1e-4);
    /* The ground level, open to the sky, follows the air. */
    for (int i = 0; i < 10 * TICKS_PER_HOUR; i++) {
        game_tick(&b);
    }
    CHECK(temperature_at(&b.temperatures, 5, 5, 0) > normal + 20.0f);
    game_shutdown(&a);
    game_shutdown(&b);
}

static void test_learn_all(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *novice = game_colonist(&game, game_spawn_colonist(&game, 10, 10, 0));
    Colonist *expert = spawn_worker(&game, 12, 10);
    REQUIRE(novice != NULL && expert != NULL);
    for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
        colonist_set_knowledge_level(novice, (KnowledgeType)k, 0.0);
    }
    colonist_set_knowledge_level(expert, KNOWLEDGE_BASIC_SHELTERS, 90.0);
    CHECK(!game_structure_buildable(&game, STRUCTURE_ROOF) ||
          colonist_knowledge_level(expert, KNOWLEDGE_BASIC_SHELTERS) >= 50.0);
    game.clock.tick += 5 * TICKS_PER_DAY;
    game_debug_learn_all(&game);
    for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
        CHECK_NEAR(colonist_knowledge_level(novice, (KnowledgeType)k), GAME_DEBUG_KNOWLEDGE_LEVEL,
                   1e-9);
        CHECK(novice->knowledge_peak[k] >= GAME_DEBUG_KNOWLEDGE_LEVEL);
        CHECK(!colonist_knowledge_fading(novice, (KnowledgeType)k, game.clock.tick));
    }
    /* Nobody loses what they knew better. */
    CHECK_NEAR(colonist_knowledge_level(expert, KNOWLEDGE_BASIC_SHELTERS), 90.0, 1e-9);
    for (int s = STRUCTURE_NONE + 1; s < STRUCTURE_TYPE_COUNT; s++) {
        CHECK(game_structure_buildable(&game, (StructureType)s));
    }
    game_shutdown(&game);
}

static void test_skip_and_next_season(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    game.clock.tick = GAME_START_TICK + 1234;
    game_debug_skip(&game, 100);
    CHECK_EQ_UINT(game.clock.tick, GAME_START_TICK + 1334);
    GameDate before = game_date_from_tick(game.clock.tick);
    game_debug_next_season(&game);
    GameDate after = game_date_from_tick(game.clock.tick);
    CHECK_EQ_INT(after.season, SEASON_AUTUMN);
    CHECK_EQ_INT(after.day, 1);
    CHECK_EQ_INT(after.year, before.year);
    CHECK_EQ_INT(after.hour, before.hour);
    CHECK_EQ_INT(after.minute, before.minute);
    /* Winter turns into the spring of the next year. */
    game_debug_next_season(&game);
    game_debug_next_season(&game);
    after = game_date_from_tick(game.clock.tick);
    CHECK_EQ_INT(after.season, SEASON_SPRING);
    CHECK_EQ_INT(after.year, before.year + 1);
    game_debug_skip(&game, 10); /* The simulation goes on from there. */
    CHECK_EQ_INT(game_date_from_tick(game.clock.tick).season, SEASON_SPRING);
    game_shutdown(&game);
}

static void test_spawn_items(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    CHECK_EQ_INT(game_debug_spawn_items(&game, 10, 10, 0, ITEM_BRANCHES, 3), 3);
    ItemStack stack = items_get(&game.items, 10, 10, 0);
    CHECK_EQ_INT(stack.type, ITEM_BRANCHES);
    CHECK_EQ_INT(stack.count, 3);
    CHECK_EQ_INT(stack.quality, ITEM_QUALITY_STANDARD);
    CHECK_EQ_INT(item_stack_hit_points_percent(stack), 100);
    CHECK_EQ_INT(game.items.totals[ITEM_BRANCHES], 3);
    /* More than a tile holds spreads around it. */
    int many = 3 * item_stack_limit(ITEM_BRANCHES);
    CHECK_EQ_INT(game_debug_spawn_items(&game, 20, 20, 0, ITEM_BRANCHES, many), many);
    CHECK_EQ_INT(game.items.totals[ITEM_BRANCHES], 3 + many);
    CHECK_EQ_INT(items_get(&game.items, 20, 20, 0).count, item_stack_limit(ITEM_BRANCHES));
    /* Nothing on open air, out of the map, nor of no type. */
    CHECK_EQ_INT(game_debug_spawn_items(&game, 10, 10, 1, ITEM_BRANCHES, 3), 0);
    CHECK_EQ_INT(game_debug_spawn_items(&game, -1, 10, 0, ITEM_BRANCHES, 3), 0);
    CHECK_EQ_INT(game_debug_spawn_items(&game, 11, 10, 0, ITEM_NONE, 3), 0);
    CHECK_EQ_INT(game_debug_spawn_items(&game, 11, 10, 0, ITEM_TYPE_COUNT, 3), 0);
    CHECK_EQ_INT(game_debug_spawn_items(&game, 11, 10, 0, ITEM_BRANCHES, 0), 0);
    game_shutdown(&game);
}

/* Items dragged by hand land at the very spot, not on open air. */
static void test_move_items_by_hand(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    REQUIRE(game_debug_spawn_items(&game, 10, 10, 0, ITEM_FLINT, 1) == 1);
    const Item *flint = items_first(&game.items, map_index(&game.map, 10, 10, 0));
    REQUIRE(flint != NULL);
    float x = 10.0f + ((float)flint->spot_x + 0.5f) / 256.0f;
    float y = 10.0f + ((float)flint->spot_y + 0.5f) / 256.0f;
    ItemId id = game_item_at_point(&game, x, y, 0);
    REQUIRE(id.slot != UINT32_MAX);
    CHECK(!game_debug_move_item(&game, id, 12.5f, 12.5f, 1)); /* Open air. */
    CHECK(game_debug_move_item(&game, id, 12.25f, 13.75f, 0));
    CHECK_EQ_INT(items_get(&game.items, 12, 13, 0).type, ITEM_FLINT);
    CHECK_EQ_INT(items_by_id(&game.items, id)->spot_x, 64);
    CHECK_EQ_INT(items_by_id(&game.items, id)->spot_y, 192);
    CHECK(game_debug_turn_item(&game, id, -1));
    CHECK_EQ_UINT(game_item_at_point(&game, 12.25f, 13.75f, 0).slot, id.slot);
    game_shutdown(&game);
}

/* Items picked one by one under a click: the topmost first, followed as
 * they move, gone once taken. */
static void test_items_under_a_point(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    for (int y = 9; y <= 13; y++) { /* Nothing lying about there. */
        for (int x = 9; x <= 14; x++) {
            items_clear(&game.items, x, y, 0);
        }
    }
    REQUIRE(game_debug_spawn_items(&game, 10, 10, 0, ITEM_FLINT, 1) == 1);
    REQUIRE(game_debug_spawn_items(&game, 10, 10, 0, ITEM_COBBLE, 1) == 1);
    REQUIRE(game_debug_spawn_items(&game, 11, 10, 0, ITEM_BRANCHES, 1) == 1);
    size_t index = map_index(&game.map, 10, 10, 0);
    ItemId flint = items_id(&game.items, items_first(&game.items, index));
    ItemId cobble = ITEM_ID_NONE;
    for (const Item *item = items_first(&game.items, index); item != NULL;
         item = items_next(&game.items, item)) {
        if (item->type == ITEM_COBBLE) {
            cobble = items_id(&game.items, item);
        } else {
            flint = items_id(&game.items, item);
        }
    }
    ItemId branch =
        items_id(&game.items, items_first(&game.items, map_index(&game.map, 11, 10, 0)));
    /* All three at the same point (the branch on the next tile, at its edge):
     * the branch's tile is drawn after, then the newest of the tile. */
    REQUIRE(game_debug_move_item(&game, flint, 10.9f, 10.5f, 0));
    REQUIRE(game_debug_move_item(&game, cobble, 10.9f, 10.5f, 0));
    REQUIRE(game_debug_move_item(&game, branch, 11.05f, 10.5f, 0));
    ItemPick picks[8];
    int count = game_items_under_point(&game, 10.92f, 10.5f, 0, picks, 8);
    REQUIRE(count == 3);
    CHECK_EQ_UINT(picks[0].id.slot, branch.slot);
    CHECK_EQ_INT(picks[0].x, 11);
    CHECK_EQ_UINT(picks[1].id.slot, cobble.slot); /* Moved last: the newest. */
    CHECK_EQ_UINT(picks[2].id.slot, flint.slot);
    CHECK_EQ_INT(picks[2].x, 10);
    CHECK_EQ_INT(game_items_under_point(&game, 10.92f, 10.5f, 0, picks, 1), 1);
    CHECK_EQ_INT(game_items_under_point(&game, 12.5f, 11.5f, 0, picks, 8), 0);
    CHECK_EQ_INT(game_items_under_point(&game, 10.92f, 10.5f, 1, picks, 8), 0);

    /* Followed onto another tile, gone once picked up. */
    ItemPick pick = {flint, 10, 10, 0};
    REQUIRE(game_debug_move_item(&game, flint, 13.5f, 12.5f, 0));
    const Item *item = game_pick_item(&game, &pick);
    REQUIRE(item != NULL);
    CHECK_EQ_INT(item->type, ITEM_FLINT);
    CHECK_EQ_INT(pick.x, 13);
    CHECK_EQ_INT(pick.y, 12);
    ItemStack load = {.type = ITEM_NONE};
    REQUIRE(items_pick_up(&game.items, 13, 12, 0, 1, &load) == 1);
    CHECK(game_pick_item(&game, &pick) == NULL);
    items_use_up(&game.items, &load, 1);
    ItemPick none = {ITEM_ID_NONE, 0, 0, 0};
    CHECK(game_pick_item(&game, &none) == NULL);
    game_shutdown(&game);
}

static void test_teleport(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *walker = spawn_worker(&game, 10, 10);
    REQUIRE(walker != NULL);
    EntityId id = game_colonist_id_at(&game, 0);
    /* Someone walking to a job: it gives it up, and nobody holds the job. */
    map_set_structure(&game.map, 30, 30, 0, STRUCTURE_WALL);
    CHECK_EQ_INT(game_designate_rect(&game, 30, 30, 30, 30, 0, DESIGNATION_DECONSTRUCT), 1);
    for (int i = 0; i < 2000 && walker->task != TASK_WORK; i++) {
        run_ticks(&game, 1);
    }
    REQUIRE(walker->task == TASK_WORK);
    REQUIRE(walker->x != 40 || walker->y != 5);
    CHECK(game_debug_teleport(&game, id, 40, 5, 0));
    CHECK_EQ_INT(walker->x, 40);
    CHECK_EQ_INT(walker->y, 5);
    CHECK_EQ_INT(walker->z, 0);
    CHECK_EQ_INT(walker->task, TASK_IDLE);
    CHECK_EQ_INT(walker->path.count, 0);
    CHECK_EQ_INT(reservations_owner(&game.reservations, RESERVATION_WORK, (TilePos){30, 30, 0}), 0);
    run_ticks(&game, 5); /* Carries on from there. */
    CHECK(abs(walker->x - 40) <= 1 && abs(walker->y - 5) <= 1);
    /* Not into open air or out of the map; not a stale handle. */
    CHECK(!game_debug_teleport(&game, id, 20, 20, 1));
    CHECK(!game_debug_teleport(&game, id, 48, 20, 0));
    CHECK(game_remove_colonist(&game, id));
    CHECK(!game_debug_teleport(&game, id, 20, 20, 0));
    game_shutdown(&game);
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
    RUN_TEST(test_instant_fell_and_mine);
    RUN_TEST(test_weather_cheats);
    RUN_TEST(test_hash);
    RUN_TEST(test_character_editor);
    RUN_TEST(test_temperature_steps);
    RUN_TEST(test_learn_all);
    RUN_TEST(test_skip_and_next_season);
    RUN_TEST(test_spawn_items);
    RUN_TEST(test_move_items_by_hand);
    RUN_TEST(test_items_under_a_point);
    RUN_TEST(test_teleport);
    return test_summary();
}
