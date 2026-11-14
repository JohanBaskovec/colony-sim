/* Structures in a running game: placing, building, sleeping in beds, taking
 * apart (core/construction.h, core/structure.h, core/colonist.h). */
#include "core/game.h"

#include "test_framework.h"

#include <string.h>

/* A small game on a flat, empty soil map with no starting colonists, at noon. */
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
        map->cobbles[i] = MAP_STONES_MAX; /* Cobbles everywhere, no flint. */
        map->flint[i] = 0;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map); /* The layers were written directly. */
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

/* Level at which work goes at the normal speed. */
#define STANDARD_LEVEL 75.0

static Colonist *spawn_rested(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y));
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

static void run_ticks(Game *game, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        game_tick(game);
    }
}

/* Runs until a structure stands on (x, y), at most `max_ticks`. */
static bool run_until_built(Game *game, int x, int y, int max_ticks)
{
    for (int i = 0; i < max_ticks; i++) {
        if (map_structure(&game->map, x, y) != STRUCTURE_NONE) {
            return true;
        }
        game_tick(game);
    }
    return map_structure(&game->map, x, y) != STRUCTURE_NONE;
}

static void test_placing(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    /* Walls go along the edges of the rectangle, beds fill it. */
    CHECK_EQ_INT(game_place_structure_rect(&game, STRUCTURE_WALL, 6, 5, 3, 3), 10);
    CHECK_EQ_INT(game.constructions.used_count, 10);
    CHECK(construction_at(&game.constructions, 4, 4) == CONSTRUCTION_NONE); /* Inside. */
    CHECK(construction_at(&game.constructions, 3, 4) != CONSTRUCTION_NONE);
    CHECK_EQ_INT(game_place_structure_rect(&game, STRUCTURE_GRASS_BED, 10, 10, 11, 11), 4);
    CHECK_EQ_INT(game_place_structure_rect(&game, STRUCTURE_NONE, 20, 20, 21, 21), 0);

    /* Not on a stockpile, a workstation or another site; no workstation on a
     * site or a structure. */
    CHECK(stockpile_create(&game.stockpiles, &game.map, 20, 20, 21, 21) != STOCKPILE_NONE);
    CHECK(!game_can_place_structure(&game, 20, 20));
    CHECK(game_place_workstation(&game, WORKSTATION_CRAFTING_SPOT, 25, 25) != WORKSTATION_NONE);
    CHECK(!game_can_place_structure(&game, 25, 25));
    CHECK(!game_can_place_structure(&game, 3, 3));
    CHECK(!game_can_place_workstation(&game, 3, 3));
    map_set_structure(&game.map, 30, 30, STRUCTURE_GRASS_BED);
    CHECK(!game_can_place_workstation(&game, 30, 30));
    CHECK(!game_can_place_structure(&game, 30, 30));
    game_shutdown(&game);
}

static void test_cancelling_gives_materials_back(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int index = game_place_structure(&game, STRUCTURE_WALL, 5, 5);
    REQUIRE(index != CONSTRUCTION_NONE);
    construction_deliver(&game.constructions, index, item_stack_pristine(ITEM_BRANCHES, 4));
    construction_deliver(&game.constructions, index, item_stack_pristine(ITEM_GRASS, 6));
    CHECK_EQ_INT(game_remove_blueprints_rect(&game, 0, 0, 10, 10), 1);
    CHECK_EQ_INT(game.constructions.used_count, 0);
    CHECK_EQ_INT(game.items.totals[ITEM_BRANCHES], 4);
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 6);
    CHECK(!game_remove_construction(&game, index));
    game_shutdown(&game);
}

static void test_builder_brings_materials_and_builds_a_wall(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    items_add(&game.items, 12, 10, item_stack_pristine(ITEM_BRANCHES, 6));
    items_add(&game.items, 14, 10, item_stack_pristine(ITEM_GRASS, 30));
    items_add(&game.items, 16, 10, item_stack_pristine(ITEM_FRESH_GRASS, 30)); /* Not used. */
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 10, 14) != CONSTRUCTION_NONE);
    /* The wall tile is in a gathering zone and a stockpile: built, it leaves them. */
    int zone = gather_zone_create(&game.gather_zones, &game.map, 10, 14, 11, 14);
    REQUIRE(zone != GATHER_ZONE_NONE);

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_CONSTRUCT);
    CHECK(run_until_built(&game, 10, 14, 8 * TICKS_PER_HOUR));
    CHECK_EQ_INT(map_structure(&game.map, 10, 14), STRUCTURE_WALL);
    CHECK(!map_is_passable(&game.map, 10, 14));
    CHECK_EQ_INT(game.constructions.used_count, 0);
    /* Exactly what a wall needs was used: 5 branches, 24 dry grass. */
    CHECK_EQ_INT(game.items.totals[ITEM_BRANCHES], 1);
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 6);
    CHECK_EQ_INT(game.items.totals[ITEM_FRESH_GRASS], 30);
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 10, 14), GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 11, 14), zone);
    CHECK(map_is_passable(&game.map, colonist->x, colonist->y));
    /* Building teaches the knowledge. */
    CHECK(colonist_knowledge_level(colonist, KNOWLEDGE_BASIC_SHELTERS) > STANDARD_LEVEL);
    game_shutdown(&game);
}

static void test_a_bush_is_cleared_before_building(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    items_add(&game.items, 12, 10, item_stack_pristine(ITEM_BRANCHES, 5));
    items_add(&game.items, 14, 10, item_stack_pristine(ITEM_GRASS, 24));
    Plant *bush = &game.map.plants[map_index(&game.map, 10, 14)];
    *bush = (Plant){.type = PLANT_BUSH, .growth = 0.3f}; /* Young: no berries. */
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 10, 14) != CONSTRUCTION_NONE);
    CHECK(run_until_built(&game, 10, 14, 8 * TICKS_PER_HOUR));
    CHECK_EQ_INT(map_structure(&game.map, 10, 14), STRUCTURE_WALL);
    CHECK_EQ_INT(bush->type, PLANT_NONE);
    CHECK_EQ_INT((int)game.items.totals[ITEM_BERRIES], 0);
    game_shutdown(&game);
}

static void test_no_building_without_materials_or_knowledge(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    REQUIRE(game_place_structure(&game, STRUCTURE_GRASS_BED, 12, 12) != CONSTRUCTION_NONE);
    /* Fresh grass would mould: only dry grass makes a bed. */
    items_add(&game.items, 14, 10, item_stack_pristine(ITEM_FRESH_GRASS, 20));
    run_ticks(&game, 10);
    CHECK(colonist->task != TASK_CONSTRUCT);

    /* Dry grass, but not enough knowledge. */
    items_add(&game.items, 16, 10, item_stack_pristine(ITEM_GRASS, 20));
    colonist_set_knowledge_level(colonist, KNOWLEDGE_BASIC_SHELTERS, 40.0);
    CHECK(!game_structure_buildable(&game, STRUCTURE_GRASS_BED));
    colonist->search_cooldown[SEARCH_CONSTRUCTION] = 0;
    run_ticks(&game, 10);
    CHECK(colonist->task != TASK_CONSTRUCT);

    colonist_set_knowledge_level(colonist, KNOWLEDGE_BASIC_SHELTERS, STANDARD_LEVEL);
    CHECK(game_structure_buildable(&game, STRUCTURE_GRASS_BED));
    colonist->search_cooldown[SEARCH_CONSTRUCTION] = 0;
    CHECK(run_until_built(&game, 12, 12, 4 * TICKS_PER_HOUR));
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 8); /* 12 armfuls of dry grass. */
    CHECK(map_is_passable(&game.map, 12, 12));
    game_shutdown(&game);
}

static void test_colonist_steps_out_of_a_new_wall(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    map_set_structure(&game.map, 10, 10, STRUCTURE_WALL);
    game_tick(&game);
    CHECK(colonist->x != 10 || colonist->y != 10);
    CHECK(map_is_passable(&game.map, colonist->x, colonist->y));
    CHECK(abs(colonist->x - 10) <= 1 && abs(colonist->y - 10) <= 1);
    game_shutdown(&game);
}

static void test_doors_let_settlers_through_slowly(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    map_set_structure(&game.map, 10, 10, STRUCTURE_DOOR);
    CHECK(map_is_passable(&game.map, 10, 10));
    CHECK_NEAR(map_move_cost(&game.map, 10, 10), 1.5f, 0.001f);
    game_shutdown(&game);
}

static void test_settlers_sleep_in_beds(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *sleeper = spawn_rested(&game, 10, 10);
    Colonist *other = spawn_rested(&game, 20, 20);
    REQUIRE(sleeper != NULL && other != NULL);
    map_set_structure(&game.map, 13, 10, STRUCTURE_GRASS_BED); /* Only one bed. */
    sleeper->needs[NEED_REST] = 0.2f;
    other->needs[NEED_REST] = 0.2f;

    game_tick(&game);
    CHECK_EQ_INT(sleeper->task, TASK_SLEEP);
    CHECK_EQ_INT(other->task, TASK_SLEEP);
    CHECK_EQ_INT(reservations_owner(&game.reservations, RESERVATION_BED, (TilePos){13, 10}), 1);
    run_ticks(&game, 3 * TICKS_PER_HOUR);
    /* The first one walked to the bed, the other one sleeps where it is. */
    CHECK(sleeper->x == 13 && sleeper->y == 10);
    CHECK(other->x == 20 && other->y == 20);
    /* Rest comes back faster in a bed. */
    CHECK(sleeper->needs[NEED_REST] > other->needs[NEED_REST] + 0.03f);

    /* Fully rested (before the night): stiff after the ground, fine after the bed. */
    run_ticks(&game, 6 * TICKS_PER_HOUR);
    CHECK(sleeper->task != TASK_SLEEP && other->task != TASK_SLEEP);
    CHECK_EQ_INT(sleeper->ground_sleep_ticks, 0);
    CHECK(other->ground_sleep_ticks > 0);
    float rested_speed = colonist_work_speed(sleeper, KNOWLEDGE_GATHERING);
    CHECK_NEAR(colonist_work_speed(other, KNOWLEDGE_GATHERING),
               COLONIST_GROUND_SLEEP_WORK_FACTOR * rested_speed, 0.0001f);
    /* The stiffness wears off. */
    int left = other->ground_sleep_ticks;
    run_ticks(&game, 100);
    CHECK_EQ_INT(other->ground_sleep_ticks, left - 100);
    game_shutdown(&game);
}

static void test_ground_sleep_takes_longer(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_REST] = 0.2f;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_SLEEP);
    /* From 0.2: 6.4 hours in a bed, 8 hours on the ground. */
    run_ticks(&game, 7 * TICKS_PER_HOUR);
    CHECK_EQ_INT(colonist->task, TASK_SLEEP);
    run_ticks(&game, TICKS_PER_HOUR + 100);
    CHECK(colonist->task != TASK_SLEEP);
    game_shutdown(&game);
}

static void test_taking_a_wall_apart(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    map_set_structure(&game.map, 12, 10, STRUCTURE_WALL);
    CHECK_EQ_INT(game_designate_rect(&game, 0, 0, 1, 1, DESIGNATION_DECONSTRUCT), 0); /* Nothing. */
    CHECK_EQ_INT(game_designate_rect(&game, 12, 10, 12, 10, DESIGNATION_DECONSTRUCT), 1);
    CHECK(game_designation_available(&game, DESIGNATION_DECONSTRUCT));
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    run_ticks(&game, TICKS_PER_HOUR);
    CHECK_EQ_INT(map_structure(&game.map, 12, 10), STRUCTURE_NONE);
    CHECK(map_is_passable(&game.map, 12, 10));
    /* Half of the materials come back. */
    CHECK_EQ_INT(game.items.totals[ITEM_BRANCHES], 2);
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 12);
    CHECK_EQ_INT(game.map.structure_counts[STRUCTURE_WALL], 0);
    game_shutdown(&game);
}

static void test_worn_bed_is_refreshed(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    map_build_structure(&game.map, 12, 12, STRUCTURE_GRASS_BED, ITEM_QUALITY_STANDARD);
    map_set_structure_hit_points(&game.map, 12, 12, 10);
    items_add(&game.items, 14, 10, item_stack_pristine(ITEM_GRASS, 6));
    /* The day ends: the bed wears below half, a refresh site appears. */
    game.clock.tick = TICKS_PER_DAY - 1;
    game_tick(&game);
    CHECK_EQ_INT(map_structure_hit_points(&game.map, 12, 12), 9);
    int site = construction_at(&game.constructions, 12, 12);
    REQUIRE(site != CONSTRUCTION_NONE);
    CHECK(construction_get(&game.constructions, site)->refresh);
    /* A builder brings 4 armfuls of dry grass and refreshes it. */
    for (int i = 0; i < TICKS_PER_HOUR &&
                    construction_at(&game.constructions, 12, 12) != CONSTRUCTION_NONE;
         i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(construction_at(&game.constructions, 12, 12), CONSTRUCTION_NONE);
    CHECK_EQ_INT(map_structure_hit_points(&game.map, 12, 12), 20);
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 2);
    CHECK_EQ_INT(map_structure(&game.map, 12, 12), STRUCTURE_GRASS_BED);
    game_shutdown(&game);
}

static void test_taking_apart_a_bed_being_refreshed(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    map_set_structure(&game.map, 12, 10, STRUCTURE_GRASS_BED);
    int site = constructions_place_refresh(&game.constructions, &game.map, 12, 10);
    REQUIRE(site != CONSTRUCTION_NONE);
    construction_deliver(&game.constructions, site, item_stack_pristine(ITEM_GRASS, 3));
    CHECK_EQ_INT(game_designate_rect(&game, 12, 10, 12, 10, DESIGNATION_DECONSTRUCT), 1);
    run_ticks(&game, TICKS_PER_HOUR);
    CHECK_EQ_INT(map_structure(&game.map, 12, 10), STRUCTURE_NONE);
    /* The refresh site goes too: its grass comes back with half the bed's. */
    CHECK_EQ_INT(construction_at(&game.constructions, 12, 10), CONSTRUCTION_NONE);
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 3 + 6);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_placing);
    RUN_TEST(test_cancelling_gives_materials_back);
    RUN_TEST(test_builder_brings_materials_and_builds_a_wall);
    RUN_TEST(test_a_bush_is_cleared_before_building);
    RUN_TEST(test_no_building_without_materials_or_knowledge);
    RUN_TEST(test_colonist_steps_out_of_a_new_wall);
    RUN_TEST(test_doors_let_settlers_through_slowly);
    RUN_TEST(test_settlers_sleep_in_beds);
    RUN_TEST(test_ground_sleep_takes_longer);
    RUN_TEST(test_taking_a_wall_apart);
    RUN_TEST(test_worn_bed_is_refreshed);
    RUN_TEST(test_taking_apart_a_bed_being_refreshed);
    return test_summary();
}
