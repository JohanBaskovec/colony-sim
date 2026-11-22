/* Priorities of workstations and construction sites (Workstation.priority,
 * ConstructionSite.priority): settlers take the jobs of the highest priority
 * first, whatever the distance. */
#include "core/game.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <string.h>

/* A small game on a flat, empty soil map with no starting colonists, at noon. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 5;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = map_level_tile_count(map); i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_OPEN;
        map->water[i] = 0;
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->water[i] = 0;
        map_clear_litter_at(map, i, item_types_of(ITEM_COBBLE));
        map_clear_litter_at(map, i, item_types_of(ITEM_FLINT));
        map_clear_litter_at(map, i, ITEM_TYPES(ITEM_BRANCHES, ITEM_TWIGS));
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
        make_ordinary(colonist);
    }
    return colonist;
}

/* Needs kept satisfied, the hour at noon. */
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

static void test_priority_values(void)
{
    CHECK_EQ_INT(job_priority_clamped(0), JOB_PRIORITY_HIGHEST);
    CHECK_EQ_INT(job_priority_clamped(99), JOB_PRIORITY_LOWEST);
    CHECK_EQ_INT(job_priority_clamped(3), 3);

    Game game;
    REQUIRE(init_flat_game(&game));
    int index =
        workstations_place(&game.workstations, &game.map, WORKSTATION_CRAFTING_SPOT, 10, 10, 0);
    REQUIRE(index != WORKSTATION_NONE);
    const Workstation *station = workstation_get_const(&game.workstations, index);
    CHECK_EQ_INT(station->priority, JOB_PRIORITY_DEFAULT);
    uint64_t hash = workstations_hash(&game.workstations, 0);
    uint32_t revision = game.workstations.revision;
    CHECK(!workstation_set_priority(&game.workstations, index, JOB_PRIORITY_DEFAULT));
    CHECK(workstation_set_priority(&game.workstations, index, 0)); /* Clamped. */
    CHECK_EQ_INT(station->priority, JOB_PRIORITY_HIGHEST);
    CHECK(game.workstations.revision != revision);
    CHECK(workstations_hash(&game.workstations, 0) != hash);
    CHECK(workstation_set_priority(&game.workstations, index, JOB_PRIORITY_DEFAULT));
    CHECK(workstations_hash(&game.workstations, 0) == hash); /* As it was. */
    CHECK(!workstation_set_priority(&game.workstations, 99, 1));

    /* A site's priority goes to the station built from it. */
    REQUIRE(game_place_workstation(&game, WORKSTATION_OPEN_HEARTH, 20, 20, 0));
    int site = construction_at(&game.constructions, 20, 20, 0);
    REQUIRE(site != CONSTRUCTION_NONE);
    CHECK_EQ_INT(construction_get(&game.constructions, site)->priority, JOB_PRIORITY_DEFAULT);
    hash = constructions_hash(&game.constructions, 0);
    construction_set_priority(&game.constructions, site, 2);
    CHECK_EQ_INT(construction_get(&game.constructions, site)->priority, 2);
    CHECK(constructions_hash(&game.constructions, 0) != hash);
    construction_set_priority(&game.constructions, site, -4);
    CHECK_EQ_INT(construction_get(&game.constructions, site)->priority, JOB_PRIORITY_HIGHEST);
    construction_set_priority(&game.constructions, site, 2);
    game_set_instant_build(&game, true);
    run_ticks(&game, 3);
    int hearth = workstation_at(&game.workstations, 20, 20, 0);
    REQUIRE(hearth != WORKSTATION_NONE);
    CHECK_EQ_INT(workstation_get_const(&game.workstations, hearth)->priority, 2);
    game_shutdown(&game);
}

/* Two knapping spots with work: the far one of a higher priority first. */
static void test_station_priority(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    colonist_add_tool(colonist, item_stack_make(ITEM_COBBLE, 1, ITEM_QUALITY_STANDARD, 0));
    int near =
        workstations_place(&game.workstations, &game.map, WORKSTATION_CRAFTING_SPOT, 12, 10, 0);
    int far =
        workstations_place(&game.workstations, &game.map, WORKSTATION_CRAFTING_SPOT, 30, 10, 0);
    REQUIRE(near != WORKSTATION_NONE && far != WORKSTATION_NONE);
    items_add(&game.items, 12, 11, 0, item_stack_make(ITEM_FLINT, 2, ITEM_QUALITY_STANDARD, 0));
    items_add(&game.items, 30, 11, 0, item_stack_make(ITEM_FLINT, 2, ITEM_QUALITY_STANDARD, 0));
    REQUIRE(workstation_add_bill(&game.workstations, near, RECIPE_FLAKES) >= 0);
    REQUIRE(workstation_add_bill(&game.workstations, far, RECIPE_FLAKES) >= 0);
    REQUIRE(workstation_set_priority(&game.workstations, far, JOB_PRIORITY_HIGHEST));
    for (int i = 0; i < 300 && colonist->task != TASK_CRAFT; i++) {
        run_ticks(&game, 1);
    }
    CHECK_EQ_INT(colonist->task, TASK_CRAFT);
    CHECK_EQ_INT(colonist->station, far);

    /* Equal again: the nearest one. */
    Game other;
    REQUIRE(init_flat_game(&other));
    Colonist *second = spawn_worker(&other, 10, 10);
    REQUIRE(second != NULL);
    colonist_add_tool(second, item_stack_make(ITEM_COBBLE, 1, ITEM_QUALITY_STANDARD, 0));
    near =
        workstations_place(&other.workstations, &other.map, WORKSTATION_CRAFTING_SPOT, 12, 10, 0);
    far = workstations_place(&other.workstations, &other.map, WORKSTATION_CRAFTING_SPOT, 30, 10, 0);
    items_add(&other.items, 12, 11, 0, item_stack_make(ITEM_FLINT, 2, ITEM_QUALITY_STANDARD, 0));
    items_add(&other.items, 30, 11, 0, item_stack_make(ITEM_FLINT, 2, ITEM_QUALITY_STANDARD, 0));
    REQUIRE(workstation_add_bill(&other.workstations, near, RECIPE_FLAKES) >= 0);
    REQUIRE(workstation_add_bill(&other.workstations, far, RECIPE_FLAKES) >= 0);
    for (int i = 0; i < 300 && second->task != TASK_CRAFT; i++) {
        run_ticks(&other, 1);
    }
    CHECK_EQ_INT(second->task, TASK_CRAFT);
    CHECK_EQ_INT(second->station, near);
    game_shutdown(&other);
    game_shutdown(&game);
}

/* Two wall plans with their materials about: the far one of a higher
 * priority is built first. */
static void test_site_priority(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int near = game_place_structure(&game, STRUCTURE_WALL, 12, 10, 0, DIRECTION_EAST);
    int far = game_place_structure(&game, STRUCTURE_WALL, 34, 34, 0, DIRECTION_EAST);
    REQUIRE(near != CONSTRUCTION_NONE && far != CONSTRUCTION_NONE);
    construction_set_priority(&game.constructions, far, JOB_PRIORITY_HIGHEST);
    for (int i = 0; i < 6; i++) {
        items_add(&game.items, 8 + i, 6, 0, item_stack_pristine(ITEM_GRASS, 40));
        items_add(&game.items, 8 + i, 7, 0, item_stack_pristine(ITEM_BRANCHES, 20));
    }
    REQUIRE(spawn_worker(&game, 10, 10) != NULL);
    for (int i = 0;
         i < 12 * TICKS_PER_HOUR && map_structure(&game.map, 34, 34, 0) == STRUCTURE_NONE &&
         map_structure(&game.map, 12, 10, 0) == STRUCTURE_NONE;
         i++) {
        run_ticks(&game, 1);
    }
    CHECK_EQ_INT(map_structure(&game.map, 34, 34, 0), STRUCTURE_WALL);
    CHECK_EQ_INT(map_structure(&game.map, 12, 10, 0), STRUCTURE_NONE);
    /* The near one meanwhile got nothing. */
    const ConstructionSite *site = construction_get(&game.constructions, near);
    REQUIRE(site != NULL);
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        CHECK_EQ_INT(site->delivered[slot].count, 0);
    }
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_priority_values);
    RUN_TEST(test_station_priority);
    RUN_TEST(test_site_priority);
    return test_summary();
}
