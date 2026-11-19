/* Structures in a running game: placing, building, sleeping in beds, taking
 * apart (core/construction.h, core/structure.h, core/colonist.h). */
#include "core/game.h"

#include "test_framework.h"
#include "test_settlers.h"

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
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_COBBLE));
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_FLINT));
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_BRANCHES) | ITEM_TYPE_BIT(ITEM_TWIGS));
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
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, 0));
    if (colonist != NULL) {
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
        for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
            colonist_set_knowledge_level(colonist, (KnowledgeType)k, STANDARD_LEVEL);
        }
        colonist_set_strength(colonist, COLONIST_STRENGTH_STANDARD);
        make_ordinary(colonist);
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
        if (map_structure(&game->map, x, y, 0) != STRUCTURE_NONE) {
            return true;
        }
        game_tick(game);
    }
    return map_structure(&game->map, x, y, 0) != STRUCTURE_NONE;
}

static void test_placing(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    /* Walls go along the edges of the rectangle, beds fill it. */
    CHECK_EQ_INT(game_place_structure_rect(&game, STRUCTURE_WALL, DIRECTION_SOUTH, 6, 5, 3, 3, 0),
                 10);
    CHECK_EQ_INT(game.constructions.used_count, 10);
    CHECK(construction_at(&game.constructions, 4, 4, 0) == CONSTRUCTION_NONE); /* Inside. */
    CHECK(construction_at(&game.constructions, 3, 4, 0) != CONSTRUCTION_NONE);
    /* Beds of two tiles: two side by side on a 2 × 2 rectangle. */
    CHECK_EQ_INT(
        game_place_structure_rect(&game, STRUCTURE_GRASS_BED, DIRECTION_SOUTH, 10, 10, 11, 11, 0),
        2);
    CHECK_EQ_INT(
        game_place_structure_rect(&game, STRUCTURE_NONE, DIRECTION_SOUTH, 20, 20, 21, 21, 0), 0);

    /* Not on a stockpile, a workstation or another site; no workstation on a
     * site or a structure. */
    CHECK(stockpile_create(&game.stockpiles, &game.map, 20, 20, 21, 21, 0) != STOCKPILE_NONE);
    CHECK(!game_can_place_structure(&game, STRUCTURE_WALL, 20, 20, 0, DIRECTION_SOUTH));
    CHECK(game_place_workstation(&game, WORKSTATION_CRAFTING_SPOT, 25, 25, 0));
    CHECK(!game_can_place_structure(&game, STRUCTURE_WALL, 25, 25, 0, DIRECTION_SOUTH));
    CHECK(!game_can_place_structure(&game, STRUCTURE_WALL, 3, 3, 0, DIRECTION_SOUTH));
    CHECK(game_can_place_workstation(&game, 3, 3, 0)); /* A site is replaced. */
    map_set_structure(&game.map, 30, 30, 0, STRUCTURE_GRASS_BED);
    CHECK(!game_can_place_workstation(&game, 30, 30, 0));
    CHECK(!game_can_place_structure(&game, STRUCTURE_WALL, 30, 30, 0, DIRECTION_SOUTH));
    game_shutdown(&game);
}

/* A ground bedding costs nothing, in material or work: it is there at once,
 * the plants under it cleared, and nobody has to build it. */
static void test_ground_bedding_needs_no_material(void)
{
    const StructureDef *def = structure_def(STRUCTURE_GROUND_BEDDING);
    REQUIRE(def != NULL);
    CHECK_EQ_INT(def->length, 2);
    CHECK_EQ_INT(def->materials[0].type, ITEM_NONE);
    CHECK_EQ_INT(def->materials[1].type, ITEM_NONE);
    CHECK(structure_is_bed(STRUCTURE_GROUND_BEDDING));
    CHECK_NEAR(def->rest_factor, COLONIST_GROUND_REST_FACTOR,
               1e-6); /* No better than the ground. */
    CHECK_EQ_INT(def->wear_per_day, 0);

    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *builder = spawn_rested(&game, 10, 10);
    REQUIRE(builder != NULL);
    CHECK(game_structure_buildable(&game, STRUCTURE_GROUND_BEDDING));
    CHECK(!structure_needs_construction(STRUCTURE_GROUND_BEDDING));
    game.map.plants[map_index(&game.map, 10, 15, 0)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    CHECK_EQ_INT(game_place_structure(&game, STRUCTURE_GROUND_BEDDING, 10, 14, 0, DIRECTION_SOUTH),
                 CONSTRUCTION_BUILT_AT_ONCE);
    CHECK_EQ_INT(game.constructions.used_count, 0); /* No site. */
    /* The grass under it is not destroyed: it is ordered uprooted. */
    CHECK_EQ_INT(game.map.plants[map_index(&game.map, 10, 15, 0)].type, PLANT_GRASS);
    CHECK_EQ_INT(designation_get(&game.designations, 10, 15, 0), DESIGNATION_UPROOT);
    CHECK_EQ_INT(designation_get(&game.designations, 10, 14, 0), DESIGNATION_NONE);
    CHECK_EQ_INT(map_structure(&game.map, 10, 14, 0), STRUCTURE_GROUND_BEDDING);
    CHECK_EQ_INT(map_structure(&game.map, 10, 15, 0), STRUCTURE_GROUND_BEDDING);
    CHECK_EQ_INT(game.constructions.used_count, 0);
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        CHECK_EQ_INT(game.items.totals[type], 0); /* Nothing was brought. */
    }
    game_shutdown(&game);
}

/* With nothing but ground beddings around, settlers go and sleep on one,
 * and remember a night on the ground all the same. */
static void test_settlers_sleep_on_a_ground_bedding(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    map_set_structure(&game.map, 13, 10, 0, STRUCTURE_GROUND_BEDDING);
    game.weather.spell = WEATHER_SPELL_MAX; /* A mild night. */
    Colonist *sleeper = spawn_rested(&game, 20, 20);
    REQUIRE(sleeper != NULL);
    sleeper->needs[NEED_REST] = 0.2f;
    run_ticks(&game, 1);
    CHECK_EQ_INT(sleeper->task, TASK_SLEEP);
    CHECK_EQ_INT(reservations_owner(&game.reservations, RESERVATION_BED, (TilePos){13, 10, 0}), 1);
    run_ticks(&game, 3 * TICKS_PER_HOUR);
    CHECK(sleeper->x == 13 && sleeper->y == 10);
    run_ticks(&game, 6 * TICKS_PER_HOUR);
    REQUIRE(sleeper->task != TASK_SLEEP);
    CHECK(mood_has(&sleeper->mood, THOUGHT_SLEPT_ON_GROUND));
    CHECK(!mood_has(&sleeper->mood, THOUGHT_SLEPT_WELL));
    game_shutdown(&game);
}

/* A new plan on a site in progress cancels it and takes its place. */
static void test_a_new_plan_replaces_the_one_in_progress(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int wall = game_place_structure(&game, STRUCTURE_WALL, 5, 5, 0, DIRECTION_SOUTH);
    REQUIRE(wall != CONSTRUCTION_NONE);
    construction_deliver(&game.constructions, wall, item_stack_pristine(ITEM_BRANCHES, 4));

    /* The same plan again changes nothing: the materials stay delivered. */
    CHECK(!game_can_place_structure(&game, STRUCTURE_WALL, 5, 5, 0, DIRECTION_SOUTH));
    CHECK_EQ_INT(game_place_structure(&game, STRUCTURE_WALL, 5, 5, 0, DIRECTION_SOUTH),
                 CONSTRUCTION_NONE);
    CHECK_EQ_INT(construction_at(&game.constructions, 5, 5, 0), wall);
    CHECK_EQ_INT(game.items.totals[ITEM_BRANCHES], 0);

    /* A bed over the wall (and the free tile beside it): the wall is given
     * up and its materials dropped. */
    CHECK(game_can_place_structure(&game, STRUCTURE_GRASS_BED, 5, 5, 0, DIRECTION_SOUTH));
    int bed = game_place_structure(&game, STRUCTURE_GRASS_BED, 5, 5, 0, DIRECTION_SOUTH);
    REQUIRE(bed != CONSTRUCTION_NONE);
    CHECK_EQ_INT(game.constructions.used_count, 1);
    CHECK_EQ_INT(construction_get(&game.constructions, bed)->type, STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(construction_at(&game.constructions, 5, 6, 0), bed);
    CHECK_EQ_INT(game.items.totals[ITEM_BRANCHES], 4);

    /* The bed turned the other way: its old second tile is free again. */
    int turned = game_place_structure(&game, STRUCTURE_GRASS_BED, 5, 5, 0, DIRECTION_EAST);
    REQUIRE(turned != CONSTRUCTION_NONE);
    CHECK_EQ_INT(game.constructions.used_count, 1);
    CHECK_EQ_INT(construction_at(&game.constructions, 5, 6, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT(construction_at(&game.constructions, 6, 5, 0), turned);

    /* A workstation over a structure's site, and the other way round. */
    CHECK(game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 6, 5, 0));
    CHECK_EQ_INT(game.constructions.used_count, 1);
    CHECK_EQ_INT(construction_at(&game.constructions, 5, 5, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT(game.constructions.workstation_count, 1);
    CHECK(!game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 6, 5, 0));
    CHECK_EQ_INT(game.constructions.workstation_count, 1);
    int replacing = game_place_structure(&game, STRUCTURE_WALL, 6, 5, 0, DIRECTION_SOUTH);
    REQUIRE(replacing != CONSTRUCTION_NONE);
    CHECK_EQ_INT(game.constructions.used_count, 1);
    CHECK_EQ_INT(game.constructions.workstation_count, 0);

    /* What is built is not replaced. */
    map_set_structure(&game.map, 12, 12, 0, STRUCTURE_WALL);
    CHECK(!game_can_place_structure(&game, STRUCTURE_GRASS_BED, 12, 12, 0, DIRECTION_SOUTH));

    /* Dragging a rectangle of walls over walls in progress keeps them. */
    int before = game.constructions.used_count;
    int placed = game_place_structure_rect(&game, STRUCTURE_WALL, DIRECTION_SOUTH, 5, 5, 8, 8, 0);
    CHECK(placed > 0);
    CHECK_EQ_INT(construction_at(&game.constructions, 6, 5, 0), replacing);
    CHECK_EQ_INT(game.constructions.used_count, before + placed);
    game_shutdown(&game);
}

static void test_cancelling_gives_materials_back(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int index = game_place_structure(&game, STRUCTURE_WALL, 5, 5, 0, DIRECTION_SOUTH);
    REQUIRE(index != CONSTRUCTION_NONE);
    construction_deliver(&game.constructions, index, item_stack_pristine(ITEM_BRANCHES, 4));
    construction_deliver(&game.constructions, index, item_stack_pristine(ITEM_GRASS, 6));
    CHECK_EQ_INT(game_remove_blueprints_rect(&game, 0, 0, 10, 10, 0), 1);
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
    items_add(&game.items, 12, 10, 0, item_stack_pristine(ITEM_BRANCHES, 6));
    items_add(&game.items, 14, 10, 0, item_stack_pristine(ITEM_GRASS, 30));
    items_add(&game.items, 16, 10, 0, item_stack_pristine(ITEM_FRESH_GRASS, 30)); /* Not used. */
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 10, 14, 0, DIRECTION_SOUTH) !=
            CONSTRUCTION_NONE);
    /* The wall tile is in a gathering zone and a stockpile: built, it leaves them. */
    int zone = gather_zone_create(&game.gather_zones, &game.map, 10, 14, 11, 14, 0);
    REQUIRE(zone != GATHER_ZONE_NONE);

    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_CONSTRUCT);
    CHECK(run_until_built(&game, 10, 14, 8 * TICKS_PER_HOUR));
    CHECK_EQ_INT(map_structure(&game.map, 10, 14, 0), STRUCTURE_WALL);
    CHECK(!map_is_passable(&game.map, 10, 14, 0));
    CHECK_EQ_INT(game.constructions.used_count, 0);
    /* Exactly what a wall needs was used: 5 branches, 24 dry grass. */
    CHECK_EQ_INT(game.items.totals[ITEM_BRANCHES], 1);
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 6);
    CHECK_EQ_INT(game.items.totals[ITEM_FRESH_GRASS], 30);
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 10, 14, 0), GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 11, 14, 0), zone);
    CHECK(map_is_passable(&game.map, colonist->x, colonist->y, colonist->z));
    /* Building teaches the knowledge. */
    CHECK(colonist_knowledge_level(colonist, KNOWLEDGE_BASIC_SHELTERS) > STANDARD_LEVEL);
    game_shutdown(&game);
}

/* Only one kind of work: the others disabled. */
static void only_work(Colonist *colonist, WorkType type)
{
    for (int t = 0; t < WORK_TYPE_COUNT; t++) {
        colonist_set_work_priority(colonist, (WorkType)t,
                                   t == (int)type ? WORK_PRIORITY_HIGHEST : WORK_PRIORITY_DISABLED);
    }
}

static int promised(const Game *game, int index)
{
    const ConstructionSite *site = construction_get(&game->constructions, index);
    return site != NULL ? site->promised[0] + site->promised[1] : -1;
}

/* Haulers bring the materials; a builder only has the work left. */
static void test_haulers_bring_materials(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *hauler = spawn_rested(&game, 10, 10);
    REQUIRE(hauler != NULL);
    only_work(hauler, WORK_HAULING);
    items_add(&game.items, 12, 10, 0, item_stack_pristine(ITEM_BRANCHES, 6));
    items_add(&game.items, 14, 10, 0, item_stack_pristine(ITEM_GRASS, 30));
    int index = game_place_structure(&game, STRUCTURE_WALL, 10, 14, 0, DIRECTION_SOUTH);
    REQUIRE(index != CONSTRUCTION_NONE);

    game_tick(&game);
    CHECK_EQ_INT(hauler->task, TASK_SUPPLY);
    CHECK(promised(&game, index) > 0);
    const ConstructionSite *site = construction_get(&game.constructions, index);
    for (int i = 0; i < 4 * TICKS_PER_HOUR && !construction_materials_complete(site); i++) {
        game_tick(&game);
    }
    CHECK(construction_materials_complete(site));
    run_ticks(&game, 10);
    CHECK(hauler->task != TASK_SUPPLY);
    CHECK_EQ_INT(promised(&game, index), 0);
    /* Exactly what the wall needs, and it is not built without a builder. */
    CHECK_EQ_INT(game.items.totals[ITEM_BRANCHES], 1);
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 6);
    CHECK_EQ_INT(map_structure(&game.map, 10, 14, 0), STRUCTURE_NONE);

    Colonist *builder = spawn_rested(&game, 20, 20);
    REQUIRE(builder != NULL);
    only_work(builder, WORK_CONSTRUCTION);
    CHECK(run_until_built(&game, 10, 14, 4 * TICKS_PER_HOUR));
    game_shutdown(&game);
}

/* Two haulers share the work: nobody goes for what another brings, and
 * promises are given up with the task. */
static void test_haulers_share_a_site(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *first = spawn_rested(&game, 10, 10);
    Colonist *second = spawn_rested(&game, 10, 11);
    REQUIRE(first != NULL && second != NULL);
    only_work(first, WORK_HAULING);
    only_work(second, WORK_HAULING);
    /* Two stacks of branches: the second hauler could fetch the other one. */
    items_add(&game.items, 12, 10, 0, item_stack_pristine(ITEM_BRANCHES, 3));
    items_add(&game.items, 11, 12, 0, item_stack_pristine(ITEM_BRANCHES, 3));
    items_add(&game.items, 14, 10, 0, item_stack_pristine(ITEM_GRASS, 30));
    int index = game_place_structure(&game, STRUCTURE_WALL, 10, 14, 0, DIRECTION_SOUTH);
    REQUIRE(index != CONSTRUCTION_NONE);

    game_tick(&game);
    const ConstructionSite *site = construction_get(&game.constructions, index);
    CHECK_EQ_INT(first->task, TASK_SUPPLY);
    CHECK_EQ_INT(second->task, TASK_SUPPLY);
    CHECK(first->supply_slot != second->supply_slot); /* Branches and grass. */
    CHECK_EQ_INT(site->promised[0], 5);
    CHECK(site->promised[1] > 0 && site->promised[1] <= 24);

    /* The one bringing branches stops hauling: its promise is released. */
    Colonist *branches = first->supply_slot == 0 ? first : second;
    colonist_set_work_priority(branches, WORK_HAULING, WORK_PRIORITY_DISABLED);
    for (int i = 0; i < 200 && branches->task == TASK_SUPPLY; i++) {
        game_tick(&game); /* Noticed on the next tile. */
    }
    CHECK(branches->task != TASK_SUPPLY);
    CHECK_EQ_INT(site->promised[0], 0);
    colonist_set_work_priority(branches, WORK_HAULING, WORK_PRIORITY_HIGHEST);
    for (int i = 0; i < 4 * TICKS_PER_HOUR && !construction_materials_complete(site); i++) {
        game_tick(&game);
    }
    CHECK(construction_materials_complete(site));
    CHECK_EQ_INT(game.items.totals[ITEM_BRANCHES], 1);
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 6);
    game_shutdown(&game);
}

static void test_a_bush_is_cleared_before_building(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    items_add(&game.items, 12, 10, 0, item_stack_pristine(ITEM_BRANCHES, 5));
    items_add(&game.items, 14, 10, 0, item_stack_pristine(ITEM_GRASS, 24));
    Plant *bush = &game.map.plants[map_index(&game.map, 10, 14, 0)];
    *bush = (Plant){.type = PLANT_BRAMBLE, .growth = 0.3f}; /* Young: no fruit. */
    REQUIRE(game_place_structure(&game, STRUCTURE_WALL, 10, 14, 0, DIRECTION_SOUTH) !=
            CONSTRUCTION_NONE);
    CHECK(run_until_built(&game, 10, 14, 8 * TICKS_PER_HOUR));
    CHECK_EQ_INT(map_structure(&game.map, 10, 14, 0), STRUCTURE_WALL);
    CHECK_EQ_INT(bush->type, PLANT_NONE);
    CHECK_EQ_INT((int)game.items.totals[ITEM_BLACKBERRIES], 0);
    game_shutdown(&game);
}

static void test_no_building_without_materials_or_knowledge(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    REQUIRE(game_place_structure(&game, STRUCTURE_GRASS_BED, 12, 12, 0, DIRECTION_SOUTH) !=
            CONSTRUCTION_NONE);
    /* Fresh grass would mould: only dry grass makes a bed. */
    items_add(&game.items, 14, 10, 0, item_stack_pristine(ITEM_FRESH_GRASS, 20));
    run_ticks(&game, 10);
    CHECK(colonist->task != TASK_CONSTRUCT);

    /* Dry grass, but not enough knowledge. */
    items_add(&game.items, 16, 10, 0, item_stack_pristine(ITEM_GRASS, 20));
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
    CHECK(map_is_passable(&game.map, 12, 12, 0));
    game_shutdown(&game);
}

static void test_colonist_steps_out_of_a_new_wall(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    map_set_structure(&game.map, 10, 10, 0, STRUCTURE_WALL);
    game_tick(&game);
    CHECK(colonist->x != 10 || colonist->y != 10);
    CHECK(map_is_passable(&game.map, colonist->x, colonist->y, colonist->z));
    CHECK(abs(colonist->x - 10) <= 1 && abs(colonist->y - 10) <= 1);
    game_shutdown(&game);
}

static void test_doors_let_settlers_through_slowly(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    map_set_structure(&game.map, 10, 10, 0, STRUCTURE_DOOR);
    CHECK(map_is_passable(&game.map, 10, 10, 0));
    CHECK_NEAR(map_move_cost(&game.map, 10, 10, 0), 1.5f, 0.001f);
    game_shutdown(&game);
}

static void test_settlers_sleep_in_beds(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *sleeper = spawn_rested(&game, 10, 10);
    Colonist *other = spawn_rested(&game, 20, 20);
    REQUIRE(sleeper != NULL && other != NULL);
    map_set_structure(&game.map, 13, 10, 0, STRUCTURE_GRASS_BED); /* Only one bed. */
    sleeper->needs[NEED_REST] = 0.2f;
    other->needs[NEED_REST] = 0.2f;

    game_tick(&game);
    CHECK_EQ_INT(sleeper->task, TASK_SLEEP);
    CHECK_EQ_INT(other->task, TASK_SLEEP);
    CHECK_EQ_INT(reservations_owner(&game.reservations, RESERVATION_BED, (TilePos){13, 10, 0}), 1);
    CHECK_EQ_INT(beds_owner(&game.beds, 13, 10, 0), 1); /* It takes the bed. */
    run_ticks(&game, 3 * TICKS_PER_HOUR);
    /* The first one walked to the bed, the other one sleeps where it is. */
    CHECK(sleeper->x == 13 && sleeper->y == 10);
    CHECK(other->x == 20 && other->y == 20);
    /* Rest comes back faster in a bed. */
    CHECK(sleeper->needs[NEED_REST] > other->needs[NEED_REST] + 0.03f);

    /* Fully rested (before the night): each remembers how it slept. */
    run_ticks(&game, 6 * TICKS_PER_HOUR);
    CHECK(sleeper->task != TASK_SLEEP && other->task != TASK_SLEEP);
    CHECK(!mood_has(&sleeper->mood, THOUGHT_SLEPT_ON_GROUND));
    CHECK(mood_has(&other->mood, THOUGHT_SLEPT_ON_GROUND));
    game_shutdown(&game);
}

/* Settlers keep the bed they took; a bed taken apart or a settler gone frees
 * it. */
static void test_settlers_own_their_beds(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *owner = spawn_rested(&game, 10, 10);
    Colonist *newcomer = spawn_rested(&game, 20, 10);
    REQUIRE(owner != NULL && newcomer != NULL);
    map_set_structure(&game.map, 11, 10, 0, STRUCTURE_GRASS_BED);
    map_set_structure(&game.map, 13, 10, 0, STRUCTURE_GRASS_BED);
    map_set_structure(&game.map, 16, 10, 0, STRUCTURE_GRASS_BED);
    REQUIRE(beds_assign(&game.beds, 1, 16, 10, 0)); /* The first settler's bed. */
    REQUIRE(beds_assign(&game.beds, 3, 13, 10, 0)); /* A settler away from home. */
    owner->needs[NEED_REST] = 0.2f;
    newcomer->needs[NEED_REST] = 0.2f;

    game_tick(&game);
    run_ticks(&game, TICKS_PER_HOUR);
    /* The owner walks past two free beds to its own; the newcomer takes the
     * free one, past the beds of the others. */
    CHECK(owner->x == 16 && owner->y == 10);
    CHECK(newcomer->x == 11 && newcomer->y == 10);
    TilePos bed;
    CHECK(beds_owned(&game.beds, 2, &bed) && bed.x == 11 && bed.y == 10);
    CHECK_EQ_INT(beds_owner(&game.beds, 13, 10, 0), 3);
    CHECK_EQ_INT(beds_owner(&game.beds, 16, 10, 0), 1);

    /* Its bed gone, the owner has none; the newcomer gone, its bed is free. */
    map_set_structure(&game.map, 16, 11, 0, STRUCTURE_NONE); /* By the foot. */
    game_tick(&game);
    CHECK(!beds_owned(&game.beds, 1, &bed));
    CHECK(game_remove_colonist(&game, game_colonist_id_at(&game, 1)));
    CHECK_EQ_INT(beds_owner(&game.beds, 11, 10, 0), BED_NO_OWNER);
    game_shutdown(&game);
}

typedef struct PlannedBeds {
    int count;
    int heads[8][2];
    int allowed;
} PlannedBeds;

static void collect_planned(int x, int y, int z, bool allowed, void *userdata)
{
    PlannedBeds *planned = userdata;
    CHECK_EQ_INT(z, 0);
    if (planned->count < 8) {
        planned->heads[planned->count][0] = x;
        planned->heads[planned->count][1] = y;
    }
    planned->count++;
    planned->allowed += allowed;
}

static void test_planning_beds(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    /* A click: one bed, its head on the tile clicked, whichever way it goes. */
    PlannedBeds planned = {0};
    game_plan_structure_rect(&game, STRUCTURE_GRASS_BED, DIRECTION_WEST, 10, 10, 10, 10, 0,
                             collect_planned, &planned);
    CHECK_EQ_INT(planned.count, 1);
    CHECK(planned.heads[0][0] == 10 && planned.heads[0][1] == 10);
    planned = (PlannedBeds){0};
    game_plan_structure_rect(&game, STRUCTURE_GRASS_BED, DIRECTION_NORTH, 10, 10, 10, 10, 0,
                             collect_planned, &planned);
    CHECK(planned.count == 1 && planned.heads[0][0] == 10 && planned.heads[0][1] == 10);

    /* 3 × 3 facing south: a row of three beds, the last row too short. */
    planned = (PlannedBeds){0};
    game_plan_structure_rect(&game, STRUCTURE_GRASS_BED, DIRECTION_SOUTH, 12, 12, 10, 10, 0,
                             collect_planned, &planned);
    CHECK_EQ_INT(planned.count, 3);
    CHECK_EQ_INT(planned.allowed, 3);
    CHECK(planned.heads[2][0] == 12 && planned.heads[2][1] == 10);
    /* 4 × 1 facing east: two beds, heads on the west. */
    planned = (PlannedBeds){0};
    game_plan_structure_rect(&game, STRUCTURE_GRASS_BED, DIRECTION_EAST, 10, 20, 13, 20, 0,
                             collect_planned, &planned);
    CHECK_EQ_INT(planned.count, 2);
    CHECK(planned.heads[1][0] == 12 && planned.heads[1][1] == 20);

    /* A bed whose foot would lie on a stockpile is refused. */
    CHECK(stockpile_create(&game.stockpiles, &game.map, 30, 31, 30, 31, 0) != STOCKPILE_NONE);
    CHECK(!game_can_place_structure(&game, STRUCTURE_GRASS_BED, 30, 30, 0, DIRECTION_SOUTH));
    CHECK(game_can_place_structure(&game, STRUCTURE_GRASS_BED, 30, 30, 0, DIRECTION_NORTH));
    CHECK_EQ_INT(
        game_place_structure_rect(&game, STRUCTURE_GRASS_BED, DIRECTION_EAST, 10, 20, 13, 20, 0),
        2);
    CHECK_EQ_INT(construction_at(&game.constructions, 13, 20, 0),
                 construction_at(&game.constructions, 12, 20, 0));
    game_shutdown(&game);
}

/* A bed facing east is built whole by a settler working next to it. */
static void test_building_a_bed_facing_east(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *builder = spawn_rested(&game, 8, 8);
    REQUIRE(builder != NULL);
    items_add(&game.items, 6, 8, 0, item_stack_pristine(ITEM_GRASS, 12));
    REQUIRE(game_place_structure(&game, STRUCTURE_GRASS_BED, 12, 8, 0, DIRECTION_EAST) !=
            CONSTRUCTION_NONE);
    CHECK(run_until_built(&game, 13, 8, 4 * TICKS_PER_HOUR));
    CHECK(map_structure_is_origin(&game.map, 12, 8, 0));
    CHECK_EQ_INT(map_structure(&game.map, 13, 8, 0), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map_structure_direction(&game.map, 13, 8, 0), DIRECTION_EAST);
    CHECK_EQ_INT(game.constructions.used_count, 0);
    game_shutdown(&game);
}

static void test_ground_sleep_takes_longer(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    /* In summer, where it is never cold enough to rest less. */
    game.clock.tick = (uint64_t)(DAYS_PER_SEASON + 7) * TICKS_PER_DAY + 12 * TICKS_PER_HOUR;
    for (size_t i = 0; i < map_tile_count(&game.map); i++) {
        game.temperatures.celsius[i] = game_outdoor_temperature(&game);
    }
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
    map_set_structure(&game.map, 12, 10, 0, STRUCTURE_WALL);
    CHECK_EQ_INT(game_designate_rect(&game, 0, 0, 1, 1, 0, DESIGNATION_DECONSTRUCT),
                 0); /* Nothing. */
    CHECK_EQ_INT(game_designate_rect(&game, 12, 10, 12, 10, 0, DESIGNATION_DECONSTRUCT), 1);
    CHECK(game_designation_available(&game, DESIGNATION_DECONSTRUCT));
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_WORK);
    run_ticks(&game, TICKS_PER_HOUR);
    CHECK_EQ_INT(map_structure(&game.map, 12, 10, 0), STRUCTURE_NONE);
    CHECK(map_is_passable(&game.map, 12, 10, 0));
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
    map_place_structure(&game.map, 12, 12, 0, STRUCTURE_GRASS_BED, DIRECTION_SOUTH,
                        ITEM_QUALITY_STANDARD);
    map_set_structure_hit_points(&game.map, 12, 12, 0, 10);
    items_add(&game.items, 14, 10, 0, item_stack_pristine(ITEM_GRASS, 6));
    /* The day ends: the bed wears below half, a refresh site appears. */
    game.clock.tick = TICKS_PER_DAY - 1;
    game_tick(&game);
    CHECK_EQ_INT(map_structure_hit_points(&game.map, 12, 12, 0), 9);
    int site = construction_at(&game.constructions, 12, 12, 0);
    REQUIRE(site != CONSTRUCTION_NONE);
    CHECK(construction_get(&game.constructions, site)->refresh);
    /* A builder brings 4 armfuls of dry grass and refreshes it. */
    for (int i = 0;
         i < TICKS_PER_HOUR && construction_at(&game.constructions, 12, 12, 0) != CONSTRUCTION_NONE;
         i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(construction_at(&game.constructions, 12, 12, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT(map_structure_hit_points(&game.map, 12, 12, 0), 20);
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 2);
    CHECK_EQ_INT(map_structure(&game.map, 12, 12, 0), STRUCTURE_GRASS_BED);
    game_shutdown(&game);
}

static void test_taking_apart_a_bed_being_refreshed(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    map_set_structure(&game.map, 12, 10, 0, STRUCTURE_GRASS_BED);
    int site = constructions_place_refresh(&game.constructions, &game.map, 12, 10, 0);
    REQUIRE(site != CONSTRUCTION_NONE);
    construction_deliver(&game.constructions, site, item_stack_pristine(ITEM_GRASS, 3));
    CHECK_EQ_INT(game_designate_rect(&game, 12, 10, 12, 10, 0, DESIGNATION_DECONSTRUCT), 1);
    run_ticks(&game, TICKS_PER_HOUR);
    CHECK_EQ_INT(map_structure(&game.map, 12, 10, 0), STRUCTURE_NONE);
    /* The refresh site goes too: its grass comes back with half the bed's. */
    CHECK_EQ_INT(construction_at(&game.constructions, 12, 10, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 3 + 6);
    game_shutdown(&game);
}

/* A settler roofs a hut: walls around, a roof on the level above over the
 * inside, built from below without clearing the grass under it. The hut
 * becomes a roofed room. */
static void test_roofing_a_hut(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    REQUIRE(spawn_rested(&game, 10, 10) != NULL);
    REQUIRE(spawn_rested(&game, 11, 10) != NULL);
    /* Walls around two tiles, (15, 15) and (16, 15), a door on the east. */
    for (int x = 14; x <= 17; x++) {
        map_set_structure(&game.map, x, 14, 0, STRUCTURE_WALL);
        map_set_structure(&game.map, x, 16, 0, STRUCTURE_WALL);
    }
    map_set_structure(&game.map, 14, 15, 0, STRUCTURE_WALL);
    map_set_structure(&game.map, 17, 15, 0, STRUCTURE_DOOR);
    game.map.plants[map_index(&game.map, 15, 15, 0)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    /* A stockpile inside: a roof goes over it. */
    REQUIRE(stockpile_create(&game.stockpiles, &game.map, 16, 15, 16, 15, 0) != STOCKPILE_NONE);
    CHECK(game_can_place_structure(&game, STRUCTURE_ROOF, 16, 15, 1, DIRECTION_SOUTH));
    CHECK(!game_can_place_structure(&game, STRUCTURE_ROOF, 16, 15, 0, DIRECTION_SOUTH));
    CHECK(!game_can_place_structure(&game, STRUCTURE_ROOF, 30, 30, 1, DIRECTION_SOUTH));
    CHECK_EQ_INT(
        game_place_structure_rect(&game, STRUCTURE_ROOF, DIRECTION_SOUTH, 15, 15, 16, 15, 1), 2);
    game_tick(&game);
    CHECK(!map_is_roofed(&game.map, 15, 15, 0)); /* Only planned. */

    items_add(&game.items, 10, 12, 0, item_stack_pristine(ITEM_BRANCHES, 6));
    for (int x = 8; x < 10; x++) { /* 20 armfuls a tile. */
        CHECK_EQ_INT(items_add(&game.items, x, 7, 0, item_stack_pristine(ITEM_GRASS, 20)), 20);
    }
    for (int i = 0; i < 6 * TICKS_PER_HOUR && game.constructions.used_count > 0; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(game.constructions.used_count, 0);
    CHECK_EQ_INT(game.map.roof_count, 2);
    CHECK_EQ_INT(map_roof(&game.map, 15, 15, 1), STRUCTURE_ROOF);
    CHECK_EQ_INT(map_roof_over(&game.map, 15, 15, 0), STRUCTURE_ROOF);
    CHECK_EQ_INT(game.items.totals[ITEM_BRANCHES], 0);
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 0);
    CHECK_EQ_INT(game.map.plants[map_index(&game.map, 15, 15, 0)].type, PLANT_GRASS);
    CHECK(stockpile_at(&game.stockpiles, 16, 15, 0) != STOCKPILE_NONE);
    CHECK(map_is_roofed(&game.map, 15, 15, 0));
    CHECK(map_is_roofed(&game.map, 16, 15, 0));
    game_shutdown(&game);
}

/* Taking apart a door under a roof, and taking roofs down: the same order,
 * on the door's level or on the roof's, the level above (worked from
 * below, the materials falling onto the floor). */
static void test_taking_a_roof_apart(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_rested(&game, 10, 10);
    REQUIRE(colonist != NULL);
    map_set_structure(&game.map, 13, 12, 0, STRUCTURE_WALL); /* Holds the roofs up. */
    map_set_structure(&game.map, 12, 10, 0, STRUCTURE_DOOR);
    map_set_roof(&game.map, 12, 10, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    map_set_roof(&game.map, 14, 10, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    /* Taking apart on the ground: the door only, not the roofs. */
    CHECK_EQ_INT(game_designate_rect(&game, 12, 10, 14, 10, 0, DESIGNATION_DECONSTRUCT), 1);
    /* On the level above: the roof over the door is left for now. */
    CHECK_EQ_INT(game_designate_rect(&game, 13, 10, 14, 10, 1, DESIGNATION_DECONSTRUCT), 1);
    CHECK_EQ_INT(designation_get(&game.designations, 12, 10, 0), DESIGNATION_DECONSTRUCT);
    for (int i = 0; i < 2 * TICKS_PER_HOUR && game.designations.count > 0; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(game.designations.count, 0);
    CHECK_EQ_INT(map_roof(&game.map, 14, 10, 1), STRUCTURE_NONE);
    CHECK(map_is_open(&game.map, 14, 10, 1));
    CHECK_EQ_INT(map_structure(&game.map, 12, 10, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(map_roof(&game.map, 12, 10, 1), STRUCTURE_ROOF); /* One order, one thing. */
    /* Half of the roof's materials (3 branches, 20 armfuls of grass) come
     * back, on the ground. */
    CHECK_EQ_INT(game.items.totals[ITEM_GRASS], 10);
    CHECK_EQ_INT(game.items.totals[ITEM_BRANCHES], 1 + 2);
    CHECK_EQ_INT((int)game.items.level_stacks[1], 0);
    CHECK_EQ_INT(game_designate_rect(&game, 12, 10, 12, 10, 1, DESIGNATION_DECONSTRUCT), 1);
    run_ticks(&game, TICKS_PER_HOUR);
    CHECK_EQ_INT(map_roof(&game.map, 12, 10, 1), STRUCTURE_NONE);
    CHECK_EQ_INT(game.map.roof_count, 0);
    CHECK_EQ_INT(colonist->z, 0);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_placing);
    RUN_TEST(test_a_new_plan_replaces_the_one_in_progress);
    RUN_TEST(test_cancelling_gives_materials_back);
    RUN_TEST(test_builder_brings_materials_and_builds_a_wall);
    RUN_TEST(test_ground_bedding_needs_no_material);
    RUN_TEST(test_settlers_sleep_on_a_ground_bedding);
    RUN_TEST(test_haulers_bring_materials);
    RUN_TEST(test_haulers_share_a_site);
    RUN_TEST(test_a_bush_is_cleared_before_building);
    RUN_TEST(test_no_building_without_materials_or_knowledge);
    RUN_TEST(test_colonist_steps_out_of_a_new_wall);
    RUN_TEST(test_doors_let_settlers_through_slowly);
    RUN_TEST(test_settlers_sleep_in_beds);
    RUN_TEST(test_settlers_own_their_beds);
    RUN_TEST(test_planning_beds);
    RUN_TEST(test_building_a_bed_facing_east);
    RUN_TEST(test_ground_sleep_takes_longer);
    RUN_TEST(test_taking_a_wall_apart);
    RUN_TEST(test_worn_bed_is_refreshed);
    RUN_TEST(test_taking_apart_a_bed_being_refreshed);
    RUN_TEST(test_roofing_a_hut);
    RUN_TEST(test_taking_a_roof_apart);
    return test_summary();
}
