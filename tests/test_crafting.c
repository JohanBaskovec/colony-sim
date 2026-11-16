/* Building workstations, carried tools and crafting through bills (colonists in a game). */
#include "core/game.h"

#include "test_framework.h"

#include <string.h>

/* A small game on a flat, empty soil map with no starting colonists, at noon. */
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
    map_topology_changed(map);
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

/* Rested colonist at level 75 in every knowledge (speed ×1). */
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

/* Keeps needs satisfied so that long runs are only about work. */
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
        game->clock.tick = 12 * TICKS_PER_HOUR; /* Always noon. */
        game_tick(game);
    }
}

/* A station already built (its construction skipped). */
static int built_station(Game *game, WorkstationType type, int x, int y)
{
    return workstations_place(&game->workstations, &game->map, type, x, y, 0);
}

static ItemStack stones(ItemType type, int count, ItemQuality quality)
{
    return item_stack_make(type, count, quality, 0);
}

static void test_carried_tools(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    CHECK_EQ_INT(colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_FLAKE)), -1);
    CHECK(!colonist_add_tool(colonist, (ItemStack){.type = ITEM_NONE, .count = 1}));
    ItemStack flake = item_stack_make(ITEM_FLAKE, 4, ITEM_QUALITY_FINE, 15);
    REQUIRE(colonist_add_tool(colonist, flake));
    int index = colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_FLAKE));
    REQUIRE(index >= 0);
    CHECK_EQ_INT(colonist->tools[index].count, 1); /* One per type. */
    CHECK(!colonist_add_tool(colonist, flake));    /* Already has one. */

    /* Tools count in the weight, and leave less room for a load. */
    int before = colonist_carry_capacity(colonist, ITEM_COBBLE);
    CHECK(colonist_add_tool(colonist, item_stack_pristine(ITEM_THRUSTING_SPEAR, 1)));
    CHECK_NEAR(colonist_carried_weight(colonist), 1.53, 1e-4);
    CHECK(colonist_carry_capacity(colonist, ITEM_COBBLE) < before);

    /* The best of several matching tools. */
    CHECK(colonist_add_tool(colonist, item_stack_make(ITEM_HAND_AXE, 1, ITEM_QUALITY_SUPERB, 120)));
    ItemTypeMask cutting = ITEM_TYPE_BIT(ITEM_FLAKE) | ITEM_TYPE_BIT(ITEM_HAND_AXE);
    CHECK_EQ_INT(colonist->tools[colonist_tool_index(colonist, cutting)].type, ITEM_HAND_AXE);

    colonist_drop_tools(colonist, &game.items, &game.map);
    CHECK_EQ_INT(colonist_tool_index(colonist, cutting), -1);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLAKE], 1);
    CHECK_EQ_INT((int)game.items.totals[ITEM_HAND_AXE], 1);
    game_shutdown(&game);
}

static void test_crafting_spot_is_ready_at_once(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    CHECK(game_workstation_buildable(&game, WORKSTATION_CRAFTING_SPOT));
    REQUIRE(game_place_workstation(&game, WORKSTATION_CRAFTING_SPOT, 14, 10, 0));
    CHECK(workstation_at(&game.workstations, 14, 10, 0) != WORKSTATION_NONE);
    CHECK_EQ_INT(construction_at(&game.constructions, 14, 10, 0), CONSTRUCTION_NONE);
    /* Nobody comes to build it. */
    run_ticks(&game, 300);
    CHECK(colonist->task != TASK_CONSTRUCT);
    game_shutdown(&game);
}

static void test_builder_brings_the_materials(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 14, 10, 0));
    /* A blueprint: a construction site, no station yet. */
    const ConstructionSite *site =
        construction_get(&game.constructions, construction_at(&game.constructions, 14, 10, 0));
    REQUIRE(site != NULL);
    CHECK_EQ_INT(construction_workstation(site), WORKSTATION_KNAPPING_SPOT);
    CHECK_EQ_INT(workstation_at(&game.workstations, 14, 10, 0), WORKSTATION_NONE);
    /* No cobble anywhere: nothing happens. */
    run_ticks(&game, 300);
    CHECK_EQ_INT(workstation_at(&game.workstations, 14, 10, 0), WORKSTATION_NONE);
    CHECK(colonist->task != TASK_CONSTRUCT);

    items_add(&game.items, 6, 14, 0, stones(ITEM_COBBLE, 3, ITEM_QUALITY_STANDARD));
    run_ticks(&game, 1500);
    CHECK(workstation_at(&game.workstations, 14, 10, 0) != WORKSTATION_NONE);
    CHECK_EQ_INT(construction_at(&game.constructions, 14, 10, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT(game.constructions.workstation_count, 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_COBBLE], 2); /* One became the anvil. */
    CHECK_EQ_INT(colonist->carried.count, 0);
    game_shutdown(&game);
}

static void test_building_comes_before_gathering_forever(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    colonist->work_priorities[WORK_HAULING] = WORK_PRIORITY_DISABLED;
    /* Same priority for gathering and construction; the zone is closer. */
    int zone = gather_zone_create(&game.gather_zones, &game.map, 9, 12, 11, 13, 0);
    gather_zone_set_enabled(&game.gather_zones, zone, GATHER_COBBLES, true);
    gather_zone_set_mode(&game.gather_zones, zone, GATHER_COBBLES, GATHER_FOREVER);
    run_ticks(&game, 300);
    REQUIRE(colonist->task == TASK_WORK && colonist->gathering);
    REQUIRE(game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 16, 10, 0));
    run_ticks(&game, 1500);
    /* The gatherer stopped at the next decision to build with a cobble just found. */
    CHECK(workstation_at(&game.workstations, 16, 10, 0) != WORKSTATION_NONE);
    game_shutdown(&game);
}

static void test_knapping_spot_needs_the_knowledge(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    colonist_set_knowledge_level(colonist, KNOWLEDGE_STONE_KNAPPING, 40.0);
    CHECK(!game_workstation_buildable(&game, WORKSTATION_KNAPPING_SPOT));
    CHECK(game_workstation_buildable(&game, WORKSTATION_CRAFTING_SPOT));
    CHECK(game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 14, 10, 0));
    items_add(&game.items, 12, 10, 0, stones(ITEM_COBBLE, 3, ITEM_QUALITY_STANDARD));
    run_ticks(&game, 1500);
    CHECK_EQ_INT(workstation_at(&game.workstations, 14, 10, 0), WORKSTATION_NONE);
    CHECK(construction_at(&game.constructions, 14, 10, 0) != CONSTRUCTION_NONE);
    game_shutdown(&game);
}

/* Haulers bring the materials of a station's site; a builder who knows how
 * then only has the work to do. */
static void test_haulers_supply_station_sites(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *hauler = spawn_worker(&game, 10, 10);
    REQUIRE(hauler != NULL);
    colonist_set_knowledge_level(hauler, KNOWLEDGE_STONE_KNAPPING, 40.0); /* Cannot build it. */
    REQUIRE(game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 14, 10, 0));
    const ConstructionSite *site =
        construction_get(&game.constructions, construction_at(&game.constructions, 14, 10, 0));
    REQUIRE(site != NULL);
    items_add(&game.items, 6, 14, 0, stones(ITEM_COBBLE, 3, ITEM_QUALITY_STANDARD));
    bool supplied = false;
    for (int i = 0; i < 1500 && !supplied; i++) {
        run_ticks(&game, 1);
        supplied = site->delivered[0].count == 1;
    }
    CHECK(supplied);
    CHECK_EQ_INT((int)game.items.totals[ITEM_COBBLE], 2);
    run_ticks(&game, 300);
    CHECK_EQ_INT(workstation_at(&game.workstations, 14, 10, 0), WORKSTATION_NONE); /* Unbuilt. */

    Colonist *builder = spawn_worker(&game, 20, 10);
    REQUIRE(builder != NULL);
    builder->work_priorities[WORK_HAULING] = WORK_PRIORITY_DISABLED;
    run_ticks(&game, 600);
    CHECK(workstation_at(&game.workstations, 14, 10, 0) != WORKSTATION_NONE);
    CHECK_EQ_INT((int)game.items.totals[ITEM_COBBLE], 2); /* Nothing more was brought. */
    game_shutdown(&game);
}

static void test_placement_rules(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    CHECK(game_can_place_workstation(&game, 5, 5, 0));
    CHECK(game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 5, 5, 0));
    CHECK(!game_can_place_workstation(&game, 5, 5, 0)); /* One station per tile. */
    CHECK(!game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 5, 5, 0));
    CHECK(!game_place_workstation(&game, WORKSTATION_TYPE_COUNT, 7, 5, 0));
    CHECK(!game_can_place_workstation(&game, -1, 5, 0));
    /* Nor on a structure's site. */
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 8, 5, 0, DIRECTION_SOUTH) !=
            CONSTRUCTION_NONE);
    CHECK(!game_can_place_workstation(&game, 8, 5, 0));
    int built = built_station(&game, WORKSTATION_CRAFTING_SPOT, 6, 5);
    /* Cancelling orders removes blueprints (the station's and the wall's), not
     * built stations. */
    CHECK_EQ_INT(game_remove_blueprints_rect(&game, 0, 0, 10, 10, 0), 2);
    CHECK_EQ_INT(construction_at(&game.constructions, 5, 5, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT(game.constructions.workstation_count, 0);
    CHECK(workstation_get(&game.workstations, built) != NULL);
    CHECK(game_remove_workstation(&game, built));
    CHECK(!game_remove_workstation(&game, built));

    /* Room is kept in the station list for the stations of the sites. */
    for (int i = 0; i < WORKSTATION_MAX - 1; i++) {
        REQUIRE(built_station(&game, WORKSTATION_CRAFTING_SPOT, 10 + i % 30, 20 + i / 30) !=
                WORKSTATION_NONE);
    }
    CHECK(game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 5, 5, 0)); /* The last one. */
    CHECK(!game_can_place_workstation(&game, 6, 5, 0));
    game_shutdown(&game);
}

static void test_no_zone_under_a_station(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int stockpile = game_add_stockpile_rect(&game, 20, 20, 22, 22, 0, STOCKPILE_NONE);
    int gather = game_add_gather_zone_rect(&game, 30, 20, 32, 22, 0, GATHER_ZONE_NONE);
    REQUIRE(stockpile != STOCKPILE_NONE && gather != GATHER_ZONE_NONE);
    uint32_t stockpiles_revision = game.stockpiles.revision;
    uint32_t gather_revision = game.gather_zones.revision;
    /* A station placed on a zone takes the tile out of it. */
    CHECK(game_can_place_workstation(&game, 21, 21, 0));
    CHECK(game_place_workstation(&game, WORKSTATION_CRAFTING_SPOT, 21, 21, 0));
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 21, 21, 0), STOCKPILE_NONE);
    CHECK_EQ_INT(stockpile_get(&game.stockpiles, stockpile)->cell_count, 8);
    CHECK(game.stockpiles.revision != stockpiles_revision);
    CHECK(game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 31, 21, 0)); /* A site. */
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 31, 21, 0), GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_get(&game.gather_zones, gather)->cell_count, 8);
    CHECK(game.gather_zones.revision != gather_revision);
    /* A zone drawn over stations leaves their tiles out, blueprints included. */
    CHECK_EQ_INT(game_add_stockpile_rect(&game, 20, 20, 22, 22, 0, stockpile), stockpile);
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 21, 21, 0), STOCKPILE_NONE);
    CHECK_EQ_INT(game_add_gather_zone_rect(&game, 30, 20, 32, 22, 0, gather), gather);
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 31, 21, 0), GATHER_ZONE_NONE);
    CHECK_EQ_INT(game_add_gather_zone_rect(&game, 31, 21, 31, 21, 0, GATHER_ZONE_NONE),
                 GATHER_ZONE_NONE);
    /* A zone made of a single tile is deleted along with it. */
    int single = game_add_stockpile_rect(&game, 40, 40, 40, 40, 0, STOCKPILE_NONE);
    REQUIRE(single != STOCKPILE_NONE);
    CHECK(game_place_workstation(&game, WORKSTATION_CRAFTING_SPOT, 40, 40, 0));
    CHECK(stockpile_get(&game.stockpiles, single) == NULL);
    /* Once the station is gone, the tile can be zoned again. */
    CHECK(game_remove_workstation(&game, workstation_at(&game.workstations, 21, 21, 0)));
    CHECK_EQ_INT(game_add_stockpile_rect(&game, 21, 21, 21, 21, 0, stockpile), stockpile);
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 21, 21, 0), stockpile);
    game_shutdown(&game);
}

static void test_knapper_takes_a_cobble_as_hammer_then_strikes_flakes(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    REQUIRE(index != WORKSTATION_NONE);
    items_add(&game.items, 18, 12, 0, stones(ITEM_FLINT, 4, ITEM_QUALITY_FINE));
    int flakes = workstation_add_bill(&game.workstations, index, RECIPE_FLAKES);
    workstation_set_bill_target(&game.workstations, index, flakes, 2);
    CHECK(game_recipe_known(&game, RECIPE_FLAKES));
    CHECK_EQ_UINT(game_recipe_missing_tools(&game, RECIPE_FLAKES), ITEM_TYPE_BIT(ITEM_COBBLE));

    /* No cobble to strike with: the flakes cannot be struck yet. */
    run_ticks(&game, 600);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLAKE], 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLINT], 4);

    items_add(&game.items, 18, 10, 0, stones(ITEM_COBBLE, 1, ITEM_QUALITY_FINE));
    run_ticks(&game, 3000);
    /* The cobble is carried as a hammer, and kept. */
    int tool = colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_COBBLE));
    REQUIRE(tool >= 0);
    CHECK_EQ_INT(colonist->tools[tool].quality, ITEM_QUALITY_FINE);
    CHECK_EQ_INT((int)game.items.totals[ITEM_COBBLE], 0);
    /* Flint is struck: two, 5 flakes each at level 75. */
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLINT], 2);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLAKE], 10);
    const Workstation *station = workstation_get_const(&game.workstations, index);
    CHECK_EQ_INT(station->bills[flakes].done, 2);
    /* Knapping teaches knapping. */
    CHECK(colonist_knowledge_level(colonist, KNOWLEDGE_STONE_KNAPPING) > 75.0);
    game_shutdown(&game);
}

/* Work done on a craft of flakes after `ticks` at the station, with a hammer
 * of the given grade already carried (the worker starts on the station). */
static float knapping_progress(ItemQuality hammer, int ticks)
{
    Game game;
    if (!init_flat_game(&game)) {
        return -1.0f;
    }
    Colonist *colonist = spawn_worker(&game, 14, 10);
    colonist_add_tool(colonist, item_stack_make(ITEM_COBBLE, 1, hammer, 0));
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    items_add(&game.items, 15, 10, 0, stones(ITEM_FLINT, 1, ITEM_QUALITY_FINE));
    workstation_add_bill(&game.workstations, index, RECIPE_FLAKES);
    run_ticks(&game, ticks);
    float progress = workstation_get_const(&game.workstations, index)->work_progress;
    game_shutdown(&game);
    return progress;
}

static void test_tool_grade_sets_the_pace(void)
{
    float crude = knapping_progress(ITEM_QUALITY_CRUDE, 200);
    float standard = knapping_progress(ITEM_QUALITY_STANDARD, 200);
    float superb = knapping_progress(ITEM_QUALITY_SUPERB, 200);
    REQUIRE(standard > 0.0f);
    /* Same walk, same ticks of work: ×0.8, ×1, ×1.2. */
    float worked = standard / (float)COLONIST_WORK_SPEED;
    CHECK_NEAR(crude, standard - worked * 0.2f * (float)COLONIST_WORK_SPEED, 1.0);
    CHECK_NEAR(superb, standard + worked * 0.2f * (float)COLONIST_WORK_SPEED, 1.0);
}

static void test_tools_wear_and_break(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 14, 10);
    colonist_add_tool(colonist, item_stack_make(ITEM_COBBLE, 1, ITEM_QUALITY_STANDARD, 2));
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    items_add(&game.items, 15, 10, 0, stones(ITEM_FLINT, 5, ITEM_QUALITY_FINE));
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_HAND_AXE);
    REQUIRE(bill >= 0);
    workstation_set_bill_mode(&game.workstations, index, bill, BILL_FOREVER);
    /* An hour of work takes a hit point: here the hour is nearly over. */
    int tool = colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_COBBLE));
    colonist->tool_wear[tool] = TOOL_WEAR_TICKS - 50;
    run_ticks(&game, 20); /* Walking to the flint: no wear. */
    CHECK_EQ_INT(colonist->tools[tool].hit_points, 2);
    run_ticks(&game, 100);
    CHECK_EQ_INT(colonist->tools[tool].hit_points, 1);
    CHECK(colonist->tool_wear[tool] < 100);
    CHECK(workstation_get_const(&game.workstations, index)->work_progress > 0.0f);
    /* At the end of the next hour, the hammer breaks: the craft waits. */
    colonist->tool_wear[tool] = TOOL_WEAR_TICKS - 1;
    run_ticks(&game, 1);
    CHECK_EQ_INT(colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_COBBLE)), -1);
    CHECK_EQ_INT(colonist->tools_broken, 1);
    run_ticks(&game, 1);
    float progress = workstation_get_const(&game.workstations, index)->work_progress;
    CHECK(progress > 0.0f);
    CHECK(colonist->task != TASK_CRAFT);
    run_ticks(&game, 100);
    CHECK_NEAR(workstation_get_const(&game.workstations, index)->work_progress, progress, 1e-3);
    /* A new cobble: fetched, and the craft resumes where it was. */
    items_add(&game.items, 17, 10, 0, stones(ITEM_COBBLE, 1, ITEM_QUALITY_STANDARD));
    run_ticks(&game, 400);
    CHECK(colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_COBBLE)) >= 0);
    CHECK(workstation_get_const(&game.workstations, index)->work_progress > progress);
    game_shutdown(&game);
}

static void test_better_tool_close_by_is_swapped(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 14, 10);
    colonist_add_tool(colonist, item_stack_make(ITEM_COBBLE, 1, ITEM_QUALITY_CRUDE, 0));
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    items_add(&game.items, 15, 10, 0, stones(ITEM_FLINT, 3, ITEM_QUALITY_FINE));
    items_add(&game.items, 18, 10, 0, stones(ITEM_COBBLE, 1, ITEM_QUALITY_FINE));
    /* A better one far away is not worth the trip. */
    items_add(&game.items, 14, 45, 0, stones(ITEM_COBBLE, 1, ITEM_QUALITY_SUPERB));
    workstation_add_bill(&game.workstations, index, RECIPE_FLAKES);
    run_ticks(&game, 300);
    int tool = colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_COBBLE));
    REQUIRE(tool >= 0);
    CHECK_EQ_INT(colonist->tools[tool].quality, ITEM_QUALITY_FINE);
    /* The crude one is left where the fine one lay. */
    ItemStack left = items_get(&game.items, 18, 10, 0);
    CHECK_EQ_INT(left.type, ITEM_COBBLE);
    CHECK_EQ_INT(left.quality, ITEM_QUALITY_CRUDE);
    CHECK_EQ_INT(items_get(&game.items, 14, 45, 0).quality, ITEM_QUALITY_SUPERB);
    /* Not picked up again: it is worse. */
    run_ticks(&game, 1500);
    CHECK_EQ_INT(colonist->tools[tool].quality, ITEM_QUALITY_FINE);
    CHECK_EQ_INT(items_get(&game.items, 18, 10, 0).quality, ITEM_QUALITY_CRUDE);
    game_shutdown(&game);
}

static void test_knapping_cobbles_takes_two(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_CHOPPER);
    REQUIRE(bill >= 0);
    workstation_set_bill_input(&game.workstations, index, bill, ITEM_FLINT, false);
    const Bill *chopper = &workstation_get(&game.workstations, index)->bills[bill];
    /* One cobble: the hammer, nothing left to knap. Nobody even starts. */
    items_add(&game.items, 18, 10, 0, stones(ITEM_COBBLE, 1, ITEM_QUALITY_STANDARD));
    CHECK(!game_bill_has_input(&game, chopper));
    run_ticks(&game, 600);
    CHECK(colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_COBBLE)) < 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_COBBLE], 1);
    /* A second one: one to strike with, one to knap. */
    items_add(&game.items, 18, 12, 0, stones(ITEM_COBBLE, 1, ITEM_QUALITY_STANDARD));
    CHECK(game_bill_has_input(&game, chopper));
    run_ticks(&game, 2000);
    CHECK(colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_COBBLE)) >= 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_COBBLE], 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_CHOPPER], 1);
    game_shutdown(&game);
}

static void test_bill_inputs_and_quality_cap(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    colonist_set_knowledge_level(colonist, KNOWLEDGE_STONE_KNAPPING, 100.0);
    colonist_add_tool(colonist, item_stack_pristine(ITEM_COBBLE, 1));
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    items_add(&game.items, 18, 10, 0, stones(ITEM_COBBLE, 2, ITEM_QUALITY_SUPERB));
    items_add(&game.items, 18, 12, 0, stones(ITEM_FLINT, 2, ITEM_QUALITY_SUPERB));
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_FLAKES);
    workstation_set_bill_input(&game.workstations, index, bill, ITEM_FLINT, false);
    run_ticks(&game, 1500);
    /* Only cobbles allowed: a master knapper gets rough flakes out of one. */
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLINT], 2);
    CHECK_EQ_INT((int)game.items.totals[ITEM_COBBLE], 1);
    ItemStack flakes = items_get(&game.items, 14, 10, 0);
    CHECK_EQ_INT(flakes.type, ITEM_FLAKE);
    CHECK_EQ_INT(flakes.count, 8);
    CHECK_EQ_INT(flakes.quality, ITEM_QUALITY_ROUGH);
    game_shutdown(&game);
}

static void test_bill_until_in_storage(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    colonist_set_knowledge_level(colonist, KNOWLEDGE_STONE_KNAPPING, 55.0); /* 3 flakes each. */
    colonist_add_tool(colonist, item_stack_pristine(ITEM_COBBLE, 1));
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    items_add(&game.items, 16, 10, 0, stones(ITEM_FLINT, 10, ITEM_QUALITY_FINE));
    int stockpile = stockpile_create(&game.stockpiles, &game.map, 10, 14, 12, 16, 0);
    REQUIRE(stockpile != STOCKPILE_NONE);
    stockpile_set_allowed(&game.stockpiles, stockpile, ITEM_TYPE_BIT(ITEM_FLAKE));
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_FLAKES);
    workstation_set_bill_mode(&game.workstations, index, bill, BILL_UNTIL_STOCK);
    workstation_set_bill_target(&game.workstations, index, bill, 5);
    run_ticks(&game, 8000);
    /* 3 flakes, 3 more: 6 in stock, then it stops. They end up in storage. */
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLAKE], 6);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLINT], 8);
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 14, 10, 0), STOCKPILE_NONE);
    CHECK(items_get(&game.items, 14, 10, 0).type != ITEM_FLAKE);
    game_shutdown(&game);
}

static void test_interrupted_craft_resumes(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    colonist_set_knowledge_level(colonist, KNOWLEDGE_STONE_KNAPPING, 95.0); /* Never fails. */
    colonist_add_tool(colonist, item_stack_pristine(ITEM_COBBLE, 1));
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    items_add(&game.items, 16, 10, 0, stones(ITEM_FLINT, 1, ITEM_QUALITY_FINE));
    workstation_add_bill(&game.workstations, index, RECIPE_HAND_AXE);
    run_ticks(&game, 600);
    const Workstation *station = workstation_get_const(&game.workstations, index);
    CHECK_EQ_INT(colonist->task, TASK_CRAFT);
    CHECK_EQ_INT(station->work_input.type, ITEM_FLINT);
    float progress = station->work_progress;
    CHECK(progress > 0.0f);

    /* Crafting forbidden: the colonist leaves, the work stays on the station. */
    game_set_work_priority(&game, game_colonist_id_at(&game, 0), WORK_CRAFTING,
                           WORK_PRIORITY_DISABLED);
    run_ticks(&game, 300);
    CHECK(colonist->task != TASK_CRAFT);
    CHECK_EQ_INT(station->work_input.type, ITEM_FLINT);
    CHECK_NEAR(station->work_progress, progress, 1e-3);

    game_set_work_priority(&game, game_colonist_id_at(&game, 0), WORK_CRAFTING, 1);
    run_ticks(&game, 3000);
    CHECK_EQ_INT((int)game.items.totals[ITEM_HAND_AXE], 1);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLAKE], 2);
    CHECK_EQ_INT(station->work_input.count, 0);
    game_shutdown(&game);
}

static void test_spears_need_a_cutting_tool_and_straight_branches(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    int index = built_station(&game, WORKSTATION_CRAFTING_SPOT, 14, 10);
    workstation_add_bill(&game.workstations, index, RECIPE_THROWING_SPEAR);
    items_add(&game.items, 16, 10, 0, item_stack_make(ITEM_BRANCHES, 3, ITEM_QUALITY_ROUGH, 20));
    items_add(&game.items, 16, 12, 0, item_stack_make(ITEM_FLAKE, 1, ITEM_QUALITY_STANDARD, 15));
    run_ticks(&game, 1000);
    /* Crooked branches only: no spear. */
    CHECK_EQ_INT((int)game.items.totals[ITEM_THROWING_SPEAR], 0);

    items_add(&game.items, 16, 14, 0, item_stack_make(ITEM_BRANCHES, 1, ITEM_QUALITY_STANDARD, 20));
    run_ticks(&game, 6000);
    CHECK_EQ_INT((int)game.items.totals[ITEM_THROWING_SPEAR], 1);
    CHECK_EQ_INT((int)game.items.totals[ITEM_BRANCHES], 3);
    /* The flake was fetched and kept. */
    CHECK(colonist_tool_index(colonist, ITEM_TYPE_BIT(ITEM_FLAKE)) >= 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLAKE], 0);
    game_shutdown(&game);
}

static void test_removing_a_station_drops_its_input(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    colonist_set_knowledge_level(colonist, KNOWLEDGE_STONE_KNAPPING, 95.0);
    colonist_add_tool(colonist, item_stack_pristine(ITEM_COBBLE, 1));
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    items_add(&game.items, 16, 10, 0, stones(ITEM_FLINT, 1, ITEM_QUALITY_FINE));
    workstation_add_bill(&game.workstations, index, RECIPE_HAND_AXE);
    run_ticks(&game, 600);
    REQUIRE(workstation_get_const(&game.workstations, index)->work_input.count == 1);
    CHECK(game_remove_workstation(&game, index));
    CHECK_EQ_INT(items_get(&game.items, 14, 10, 0).type, ITEM_FLINT);
    run_ticks(&game, 10);
    CHECK(colonist->task != TASK_CRAFT);
    game_shutdown(&game);
}

static void test_removing_a_colonist_drops_its_tools(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    colonist_add_tool(colonist, item_stack_pristine(ITEM_FLAKE, 1));
    uint64_t before = game_hash(&game);
    colonist_add_tool(colonist, item_stack_pristine(ITEM_COBBLE, 1));
    CHECK(game_hash(&game) != before); /* Tools are part of the state. */
    CHECK(game_remove_colonist(&game, game_colonist_id_at(&game, 0)));
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLAKE], 1);
    CHECK_EQ_INT((int)game.items.totals[ITEM_COBBLE], 1);
    game_shutdown(&game);
}

static void test_missing_tools_and_inputs_are_reported(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    int index = built_station(&game, WORKSTATION_CRAFTING_SPOT, 14, 10);
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_THRUSTING_SPEAR);
    REQUIRE(bill >= 0);
    const Bill *spear = &workstation_get(&game.workstations, index)->bills[bill];
    ItemTypeMask axe = ITEM_TYPE_BIT(ITEM_HAND_AXE), flake = ITEM_TYPE_BIT(ITEM_FLAKE);
    CHECK_EQ_UINT(game_recipe_missing_tools(&game, RECIPE_THRUSTING_SPEAR), axe | flake);
    /* A tool counts whether it lies somewhere or is carried. */
    items_add(&game.items, 30, 30, 0, item_stack_pristine(ITEM_HAND_AXE, 1));
    CHECK_EQ_UINT(game_recipe_missing_tools(&game, RECIPE_THRUSTING_SPEAR), flake);
    colonist_add_tool(colonist, item_stack_pristine(ITEM_FLAKE, 1));
    CHECK_EQ_UINT(game_recipe_missing_tools(&game, RECIPE_THRUSTING_SPEAR), 0);

    /* Two branches are needed, of a type the bill allows. */
    CHECK(!game_bill_has_input(&game, spear));
    items_add(&game.items, 31, 30, 0, item_stack_make(ITEM_BRANCHES, 1, ITEM_QUALITY_STANDARD, 20));
    CHECK(!game_bill_has_input(&game, spear));
    items_add(&game.items, 32, 30, 0, item_stack_make(ITEM_BRANCHES, 1, ITEM_QUALITY_STANDARD, 20));
    CHECK(game_bill_has_input(&game, spear));
    workstation_set_bill_input(&game.workstations, index, bill, ITEM_BRANCHES, false);
    CHECK(!game_bill_has_input(&game, spear));
    game_shutdown(&game);
}

static void test_bill_worker_levels(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *novice = spawn_worker(&game, 10, 10);
    Colonist *master = spawn_worker(&game, 10, 14);
    REQUIRE(novice != NULL && master != NULL);
    colonist_set_knowledge_level(novice, KNOWLEDGE_STONE_KNAPPING, 60.0);
    colonist_set_knowledge_level(master, KNOWLEDGE_STONE_KNAPPING, 90.0);
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 12);
    items_add(&game.items, 18, 12, 0, stones(ITEM_FLINT, 4, ITEM_QUALITY_FINE));
    items_add(&game.items, 18, 13, 0, stones(ITEM_COBBLE, 2, ITEM_QUALITY_FINE));
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_FLAKES);
    REQUIRE(bill >= 0);
    workstation_set_bill_mode(&game.workstations, index, bill, BILL_FOREVER);
    const Bill *flakes = &workstation_get(&game.workstations, index)->bills[bill];

    /* Nobody in the range: the bill waits. */
    workstation_set_bill_min_level(&game.workstations, index, bill, 95);
    CHECK(game_recipe_known(&game, RECIPE_FLAKES));
    CHECK(!game_bill_has_worker(&game, flakes));
    run_ticks(&game, 1500);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLAKE], 0);

    /* Masters only: the novice leaves it to the master. */
    workstation_set_bill_min_level(&game.workstations, index, bill, 80);
    CHECK(game_bill_has_worker(&game, flakes));
    run_ticks(&game, 3000);
    CHECK(game.items.totals[ITEM_FLAKE] > 0);
    CHECK(colonist_knowledge_level(master, KNOWLEDGE_STONE_KNAPPING) > 90.0);
    CHECK(colonist_knowledge_level(novice, KNOWLEDGE_STONE_KNAPPING) == 60.0);

    /* Beginners only: the master stops (once the craft under way is done). */
    workstation_set_bill_min_level(&game.workstations, index, bill, BILL_LEVEL_MIN);
    workstation_set_bill_max_level(&game.workstations, index, bill, 70);
    items_add(&game.items, 18, 14, 0, stones(ITEM_FLINT, 4, ITEM_QUALITY_FINE));
    items_add(&game.items, 18, 15, 0, stones(ITEM_COBBLE, 1, ITEM_QUALITY_FINE));
    run_ticks(&game, 600);
    double master_level = colonist_knowledge_level(master, KNOWLEDGE_STONE_KNAPPING);
    run_ticks(&game, 3000);
    CHECK(colonist_knowledge_level(master, KNOWLEDGE_STONE_KNAPPING) == master_level);
    CHECK(colonist_knowledge_level(novice, KNOWLEDGE_STONE_KNAPPING) > 60.0);
    game_shutdown(&game);
}

static void test_paused_bill(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    colonist_set_knowledge_level(colonist, KNOWLEDGE_STONE_KNAPPING, 95.0); /* Never fails. */
    colonist_add_tool(colonist, item_stack_pristine(ITEM_COBBLE, 1));
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    items_add(&game.items, 16, 10, 0, stones(ITEM_FLINT, 2, ITEM_QUALITY_FINE));
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_HAND_AXE);
    workstation_set_bill_target(&game.workstations, index, bill, 2);
    const Workstation *station = workstation_get_const(&game.workstations, index);

    /* Paused: nothing starts. */
    workstation_set_bill_paused(&game.workstations, index, bill, true);
    run_ticks(&game, 1500);
    CHECK(colonist->task != TASK_CRAFT);
    CHECK_EQ_INT(station->work_input.count, 0);

    /* Resumed, then paused again during the craft: it is finished, and no
     * other one is started. */
    workstation_set_bill_paused(&game.workstations, index, bill, false);
    run_ticks(&game, 600);
    CHECK_EQ_INT(station->work_input.type, ITEM_FLINT);
    workstation_set_bill_paused(&game.workstations, index, bill, true);
    run_ticks(&game, 3000);
    CHECK_EQ_INT((int)game.items.totals[ITEM_HAND_AXE], 1);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLINT], 1);
    CHECK_EQ_INT(station->work_input.count, 0);
    CHECK_EQ_INT(station->bills[bill].done, 1);
    game_shutdown(&game);
}

static void test_bill_input_radius(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    colonist_add_tool(colonist, item_stack_pristine(ITEM_COBBLE, 1));
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 10, 12);
    items_add(&game.items, 40, 12, 0, stones(ITEM_FLINT, 1, ITEM_QUALITY_FINE)); /* 30 tiles. */
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_FLAKES);
    workstation_set_bill_input_radius(&game.workstations, index, bill, 20);
    run_ticks(&game, 3000);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLAKE], 0);
    CHECK_EQ_INT(items_get(&game.items, 40, 12, 0).type, ITEM_FLINT);

    workstation_set_bill_input_radius(&game.workstations, index, bill, 40);
    run_ticks(&game, 4000);
    CHECK(game.items.totals[ITEM_FLAKE] > 0);
    game_shutdown(&game);
}

static void test_stock_counts_stored_and_waiting_items(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    int pile = stockpile_create(&game.stockpiles, &game.map, 20, 20, 22, 22, 0);
    REQUIRE(index != WORKSTATION_NONE && pile != STOCKPILE_NONE);
    items_add(&game.items, 20, 20, 0, item_stack_pristine(ITEM_FLAKE, 2)); /* Stored. */
    items_add(&game.items, 14, 10, 0, item_stack_pristine(ITEM_FLAKE, 3)); /* Just made. */
    items_add(&game.items, 40, 40, 0, item_stack_pristine(ITEM_FLAKE, 7)); /* Lost in the wild. */
    Colonist *hauler = spawn_worker(&game, 30, 30);
    REQUIRE(hauler != NULL);
    hauler->task = TASK_HAUL;
    hauler->carried = item_stack_pristine(ITEM_FLAKE, 1);
    game_update_stock(&game);
    CHECK_EQ_INT((int)game.stock[ITEM_FLAKE], 2 + 3 + 1);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLAKE], 2 + 3 + 7);
    game_shutdown(&game);
}

static void test_until_stock_ignores_items_out_of_storage(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    colonist_set_knowledge_level(colonist, KNOWLEDGE_STONE_KNAPPING, 55.0); /* 3 flakes each. */
    colonist_add_tool(colonist, item_stack_pristine(ITEM_COBBLE, 1));
    game_set_work_priority(&game, game_colonist_id_at(&game, 0), WORK_HAULING,
                           WORK_PRIORITY_DISABLED);
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    items_add(&game.items, 16, 10, 0, stones(ITEM_FLINT, 10, ITEM_QUALITY_FINE));
    items_add(&game.items, 44, 44, 0, item_stack_pristine(ITEM_FLAKE, 9)); /* Not in stock. */
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_FLAKES);
    workstation_set_bill_mode(&game.workstations, index, bill, BILL_UNTIL_STOCK);
    workstation_set_bill_target(&game.workstations, index, bill, 5);
    run_ticks(&game, 8000);
    /* The flakes made wait on the station (nobody hauls) and count: 3 + 3. */
    CHECK_EQ_INT((int)game.stock[ITEM_FLAKE], 6);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLAKE], 9 + 6);
    game_shutdown(&game);
}

static void test_crafts_make_the_station_dirty(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    colonist_set_knowledge_level(colonist, KNOWLEDGE_STONE_KNAPPING, 95.0); /* Never fails. */
    colonist_add_tool(colonist, item_stack_pristine(ITEM_COBBLE, 1));
    game_set_work_priority(&game, game_colonist_id_at(&game, 0), WORK_CLEANING,
                           WORK_PRIORITY_DISABLED);
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    items_add(&game.items, 16, 10, 0, stones(ITEM_FLINT, 1, ITEM_QUALITY_FINE));
    workstation_add_bill(&game.workstations, index, RECIPE_HAND_AXE);
    run_ticks(&game, 4000);
    REQUIRE((int)game.items.totals[ITEM_HAND_AXE] == 1);
    int dirt = recipe_def(RECIPE_HAND_AXE)->dirt;
    CHECK(dirt > 0);
    CHECK_EQ_INT(dirt_at(&game.dirt, 14, 10, 0), dirt);
    CHECK_EQ_INT(dirt_at(&game.dirt, 15, 11, 0), dirt / 2);
    CHECK_EQ_INT(dirt_at(&game.dirt, 16, 10, 0), 0);
    CHECK_EQ_INT(game.dirt.dirty_count, 9);

    /* Once cleaning is allowed, the colonist cleans it all. */
    game_set_work_priority(&game, game_colonist_id_at(&game, 0), WORK_CLEANING, 1);
    run_ticks(&game, 2000);
    CHECK_EQ_INT(game.dirt.dirty_count, 0);
    game_shutdown(&game);
}

static void test_plants_are_cleared_before_building(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    Plant *tree = &game.map.plants[map_index(&game.map, 20, 20, 0)];
    *tree = (Plant){.type = PLANT_TREE, .growth = 1.0f};
    CHECK(!game_can_place_workstation(&game, 20, 20, 0));
    Plant *hazel = &game.map.plants[map_index(&game.map, 14, 10, 0)];
    *hazel = (Plant){.type = PLANT_HAZEL, .growth = 1.0f};
    plant_settle(hazel, game_day_of_year(game.clock.tick));
    int twigs = plant_stock_available(hazel, PLANT_STOCK_TWIGS);
    int branches = plant_stock_available(hazel, PLANT_STOCK_BRANCHES);
    REQUIRE(twigs > 0 && branches > 0);
    REQUIRE(game_place_workstation(&game, WORKSTATION_CRAFTING_SPOT, 14, 10, 0));
    /* The hazel is in the way: a site, to clear it first. */
    CHECK_EQ_INT(workstation_at(&game.workstations, 14, 10, 0), WORKSTATION_NONE);
    const ConstructionSite *site =
        construction_get(&game.constructions, construction_at(&game.constructions, 14, 10, 0));
    REQUIRE(site != NULL);

    /* Interrupted while clearing: the work done is kept. */
    run_ticks(&game, 300);
    CHECK_EQ_INT(colonist->task, TASK_CONSTRUCT);
    float cleared = site->clear_progress;
    CHECK(cleared > 0.0f);
    game_set_work_priority(&game, game_colonist_id_at(&game, 0), WORK_CONSTRUCTION,
                           WORK_PRIORITY_DISABLED);
    run_ticks(&game, 30);
    CHECK(colonist->task != TASK_CONSTRUCT);
    CHECK_NEAR(site->clear_progress, cleared, 1e-4);
    CHECK_EQ_INT(hazel->type, PLANT_HAZEL);

    game_set_work_priority(&game, game_colonist_id_at(&game, 0), WORK_CONSTRUCTION, 1);
    run_ticks(&game, 30); /* Picked up again at once. */
    CHECK_EQ_INT(colonist->task, TASK_CONSTRUCT);
    run_ticks(&game, 1000);
    CHECK(workstation_at(&game.workstations, 14, 10, 0) != WORKSTATION_NONE);
    CHECK_EQ_INT(hazel->type, PLANT_NONE);
    CHECK_EQ_INT((int)game.items.totals[ITEM_TWIGS], twigs);
    CHECK_EQ_INT((int)game.items.totals[ITEM_BRANCHES], branches);
    game_shutdown(&game);
}

/* First task of a knapper with a bill to work through and branches to
 * gather in a zone, at the given priorities. */
static ColonistTask first_task(int crafting, int gathering)
{
    Game game;
    if (!init_flat_game(&game)) {
        return TASK_COUNT;
    }
    Colonist *colonist = spawn_worker(&game, 10, 10);
    colonist_add_tool(colonist, item_stack_pristine(ITEM_COBBLE, 1));
    int index = built_station(&game, WORKSTATION_KNAPPING_SPOT, 14, 10);
    items_add(&game.items, 16, 10, 0, stones(ITEM_FLINT, 1, ITEM_QUALITY_FINE));
    workstation_add_bill(&game.workstations, index, RECIPE_FLAKES);
    map_add_litter(&game.map, 11, 11, 0, ITEM_BRANCHES);
    int zone = gather_zone_create(&game.gather_zones, &game.map, 11, 11, 12, 12, 0);
    gather_zone_set_enabled(&game.gather_zones, zone, GATHER_BRANCHES, true);
    gather_zone_set_mode(&game.gather_zones, zone, GATHER_BRANCHES, GATHER_FOREVER);
    EntityId id = game_colonist_id_at(&game, 0);
    game_set_work_priority(&game, id, WORK_CRAFTING, crafting);
    game_set_work_priority(&game, id, WORK_GATHERING, gathering);
    run_ticks(&game, 5);
    ColonistTask task = colonist->task;
    game_shutdown(&game);
    return task;
}

static void test_same_priority_follows_the_columns(void)
{
    /* Crafting is left of gathering in the grid: done first at equal priority. */
    CHECK((int)WORK_CRAFTING < (int)WORK_GATHERING);
    CHECK_EQ_INT(first_task(3, 3), TASK_CRAFT);
    /* A higher priority wins over the column order. */
    CHECK_EQ_INT(first_task(3, 2), TASK_WORK);
    CHECK_EQ_INT(first_task(1, 2), TASK_CRAFT);
}

int main(void)
{
    RUN_TEST(test_carried_tools);
    RUN_TEST(test_crafting_spot_is_ready_at_once);
    RUN_TEST(test_builder_brings_the_materials);
    RUN_TEST(test_building_comes_before_gathering_forever);
    RUN_TEST(test_knapping_spot_needs_the_knowledge);
    RUN_TEST(test_haulers_supply_station_sites);
    RUN_TEST(test_placement_rules);
    RUN_TEST(test_no_zone_under_a_station);
    RUN_TEST(test_tool_grade_sets_the_pace);
    RUN_TEST(test_tools_wear_and_break);
    RUN_TEST(test_better_tool_close_by_is_swapped);
    RUN_TEST(test_knapper_takes_a_cobble_as_hammer_then_strikes_flakes);
    RUN_TEST(test_knapping_cobbles_takes_two);
    RUN_TEST(test_bill_inputs_and_quality_cap);
    RUN_TEST(test_bill_until_in_storage);
    RUN_TEST(test_interrupted_craft_resumes);
    RUN_TEST(test_spears_need_a_cutting_tool_and_straight_branches);
    RUN_TEST(test_removing_a_station_drops_its_input);
    RUN_TEST(test_removing_a_colonist_drops_its_tools);
    RUN_TEST(test_missing_tools_and_inputs_are_reported);
    RUN_TEST(test_bill_worker_levels);
    RUN_TEST(test_paused_bill);
    RUN_TEST(test_bill_input_radius);
    RUN_TEST(test_stock_counts_stored_and_waiting_items);
    RUN_TEST(test_until_stock_ignores_items_out_of_storage);
    RUN_TEST(test_crafts_make_the_station_dirty);
    RUN_TEST(test_plants_are_cleared_before_building);
    RUN_TEST(test_same_priority_follows_the_columns);
    return test_summary();
}
