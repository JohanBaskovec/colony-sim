/* Open hearths: fuel, burning, refuelling, lighting by friction or with
 * embers, fire hardening, roasting and fireside stories. */
#include "core/game.h"
#include "core/hearth.h"
#include "core/util.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <math.h>
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
        map->water[i] = 0;
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) { /* The ground level. */
        map->terrain[i] = TERRAIN_SOIL;
        map->water[i] = 0;
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

/* Rested colonist at `level` in every knowledge, doing only `work`
 * (WORK_TYPE_COUNT: nothing). */
static Colonist *spawn_settler(Game *game, int x, int y, double level, WorkType work)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, 0));
    if (colonist != NULL) {
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
        for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
            colonist->knowledge[k] = level;
            colonist->knowledge_peak[k] = level;
        }
        for (int type = 0; type < WORK_TYPE_COUNT; type++) {
            colonist_set_work_priority(colonist, (WorkType)type,
                                       type == (int)work ? WORK_PRIORITY_HIGHEST
                                                         : WORK_PRIORITY_DISABLED);
        }
        colonist_set_strength(colonist, COLONIST_STRENGTH_STANDARD);
        make_ordinary(colonist);
    }
    return colonist;
}

/* Keeps needs satisfied; `noon` pins the hour, else time goes on. */
static void run_ticks(Game *game, int ticks, bool noon)
{
    for (int i = 0; i < ticks; i++) {
        for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
            if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
                for (int need = 0; need < NEED_COUNT; need++) {
                    game->colonists[slot].needs[need] = 1.0f;
                }
            }
        }
        if (noon) {
            game->clock.tick = 12 * TICKS_PER_HOUR;
        }
        game_tick(game);
    }
}

/* A hearth already built, with `fuel` kilograms in it. */
static int built_hearth(Game *game, int x, int y, float fuel)
{
    int index =
        workstations_place(&game->workstations, &game->map, WORKSTATION_OPEN_HEARTH, x, y, 0);
    Workstation *station = workstation_get(&game->workstations, index);
    if (station != NULL) {
        station->fuel = fuel;
    }
    return index;
}

static const Workstation *hearth(const Game *game, int index)
{
    return workstation_get_const(&game->workstations, index);
}

/* What a fire is laid with (dry twigs, branches), lying at (x, y) and the
 * tile east of it. */
static void kindling_at(Game *game, int x, int y, int count)
{
    items_add(&game->items, x, y, 0, item_stack_make(ITEM_TWIGS, count, 0, 0));
    items_add(&game->items, x + 1, y, 0, item_stack_make(ITEM_BRANCHES, count, 0, 0));
}

/* The first stack of a type on the map (count 0 when none). */
static ItemStack find_item(const Game *game, ItemType type)
{
    for (int y = 0; y < game->map.height; y++) {
        for (int x = 0; x < game->map.width; x++) {
            ItemStack stack = items_get(&game->items, x, y, 0);
            if (stack.type == type && stack.count > 0) {
                return stack;
            }
        }
    }
    return (ItemStack){.type = ITEM_NONE};
}

static void test_fuel(void)
{
    CHECK_NEAR(hearth_fuel_value(ITEM_BRANCHES), item_def(ITEM_BRANCHES)->weight, 1e-6);
    CHECK_NEAR(hearth_fuel_value(ITEM_WOOD), item_def(ITEM_WOOD)->weight, 1e-6);
    CHECK(hearth_fuel_value(ITEM_TWIGS) > 0.0f);
    CHECK_NEAR(hearth_fuel_value(ITEM_GRASS), 0.0, 1e-9);
    /* Dry bracken and heather burn, but are not brought by default (they are what
     * beds and roofs are made of); reeds and moss do not. */
    CHECK(hearth_fuel_value(ITEM_BRACKEN) > 0.0f);
    CHECK(hearth_fuel_value(ITEM_HEATHER) > 0.0f);
    CHECK_NEAR(hearth_fuel_value(ITEM_REEDS), 0.0, 1e-9);
    CHECK_NEAR(hearth_fuel_value(ITEM_MOSS), 0.0, 1e-9);
    CHECK(!(HEARTH_DEFAULT_FUEL_TYPES & ITEM_TYPE_BIT(ITEM_BRACKEN)));
    CHECK(!(HEARTH_DEFAULT_FUEL_TYPES & ITEM_TYPE_BIT(ITEM_HEATHER)));
    CHECK((ITEM_TINDER_TYPES & ITEM_TYPE_BIT(ITEM_GRASS)) &&
          (ITEM_TINDER_TYPES & ITEM_TYPE_BIT(ITEM_MOSS)));
    CHECK_NEAR(hearth_fuel_value(ITEM_COBBLE), 0.0, 1e-9);
    CHECK_NEAR(hearth_fuel_value(ITEM_NONE), 0.0, 1e-9);
    /* Wet fuel burns worse: green logs give about half what seasoned ones do,
     * fresh grass next to nothing; stones are no fuel wet or dry. */
    CHECK_NEAR(hearth_moisture_factor(ITEM_WOOD, item_reference_moisture(ITEM_WOOD)), 1.0f, 1e-6);
    float green = hearth_moisture_factor(ITEM_WOOD, item_fresh_moisture(ITEM_WOOD));
    CHECK(green > 0.45f && green < 0.65f);
    CHECK(hearth_moisture_factor(ITEM_GRASS, item_fresh_moisture(ITEM_GRASS)) < 0.1f);
    CHECK(hearth_moisture_factor(ITEM_WOOD, 0.05f) > 1.0f);
    ItemStack logs = item_stack_pristine(ITEM_WOOD, 1);
    item_stack_set_fresh(&logs);
    CHECK_NEAR(hearth_stack_fuel_value(logs),
               hearth_stack_fuel_value(item_stack_pristine(ITEM_WOOD, 1)) * green, 1e-4);
    CHECK(hearth_ingredient_dry(HEARTH_TINDER) && hearth_ingredient_dry(HEARTH_KINDLING));
    CHECK(!hearth_ingredient_dry(HEARTH_BRANCH));

    Game game;
    REQUIRE(init_flat_game(&game));
    Workstations *workstations = &game.workstations;
    int index = built_hearth(&game, 10, 10, 0.0f);
    REQUIRE(index >= 0);
    const Workstation *station = hearth(&game, index);
    CHECK(hearth_is(station));
    CHECK(!station->burning);
    CHECK(station->keep_burning);
    CHECK_EQ_UINT(station->fuel_types, HEARTH_DEFAULT_FUEL_TYPES);
    CHECK_NEAR(hearth_fuel_wanted(station), HEARTH_FUEL_CAPACITY, 1e-6);
    CHECK(!hearth_needs_lighting(station)); /* Nothing to burn. */
    CHECK(!hearth_light(workstations, index));
    float branch = hearth_fuel_value(ITEM_BRANCHES);
    CHECK_EQ_INT(hearth_fuel_items_wanted(station, ITEM_BRANCHES),
                 (int)(HEARTH_FUEL_CAPACITY / branch));
    CHECK_EQ_INT(hearth_fuel_items_wanted(station, ITEM_GRASS), 0);

    /* Filled up to the capacity; the rest is left. */
    int taken = hearth_add_fuel(workstations, index, item_stack_make(ITEM_BRANCHES, 40, 0, 0));
    CHECK_EQ_INT(taken, (int)(HEARTH_FUEL_CAPACITY / branch));
    CHECK(station->fuel <= HEARTH_FUEL_CAPACITY);
    CHECK(station->fuel > HEARTH_FUEL_CAPACITY - branch);
    CHECK_NEAR(hearth_fuel_wanted(station), 0.0, 1e-9);
    CHECK_EQ_INT(hearth_add_fuel(workstations, index, item_stack_make(ITEM_GRASS, 1, 0, 0)), 0);
    CHECK(hearth_needs_lighting(station));

    /* Not kept burning: nobody refuels or lights it. */
    CHECK(hearth_set_keep_burning(workstations, index, false));
    CHECK(!hearth_needs_lighting(station));
    station = hearth(&game, index);
    workstation_get(workstations, index)->fuel = 0.0f;
    CHECK_NEAR(hearth_fuel_wanted(station), 0.0, 1e-9);
    CHECK(hearth_set_keep_burning(workstations, index, true));

    /* Fuel filter. */
    CHECK(hearth_set_fuel_type(workstations, index, ITEM_TWIGS, false));
    CHECK_EQ_INT(hearth_fuel_items_wanted(station, ITEM_TWIGS), 0);
    CHECK(hearth_fuel_items_wanted(station, ITEM_BRANCHES) > 0);
    CHECK(!hearth_set_fuel_type(workstations, index, ITEM_GRASS, true));
    CHECK(hearth_set_fuel_type(workstations, index, ITEM_TWIGS, true));

    /* Fuel on its way counts, until released. */
    hearth_promise_fuel(workstations, index, 15.0f);
    CHECK_NEAR(hearth_fuel_wanted(station), 0.0, 1e-9);
    hearth_release_fuel(workstations, index, station->number + 1, 15.0f); /* Another station. */
    CHECK_NEAR(station->fuel_promised, 15.0, 1e-6);
    hearth_release_fuel(workstations, index, station->number, 15.0f);
    CHECK_NEAR(station->fuel_promised, 0.0, 1e-6);
    CHECK_NEAR(hearth_fuel_wanted(station), HEARTH_FUEL_CAPACITY, 1e-6);

    /* Other stations are no hearth. */
    int spot = workstations_place(workstations, &game.map, WORKSTATION_CRAFTING_SPOT, 20, 20, 0);
    CHECK(!hearth_is(hearth(&game, spot)));
    CHECK(!hearth_is(NULL));
    CHECK_EQ_INT(hearth_add_fuel(workstations, spot, item_stack_make(ITEM_BRANCHES, 2, 0, 0)), 0);
    CHECK(!hearth_light(workstations, spot));
    game_shutdown(&game);
}

static void test_burning(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Workstations *workstations = &game.workstations;
    int index = built_hearth(&game, 10, 10, HEARTH_FUEL_CAPACITY);
    REQUIRE(hearth_light(workstations, index));
    const Workstation *station = hearth(&game, index);
    CHECK(station->burning);
    for (int i = 0; i < TICKS_PER_HOUR; i++) {
        CHECK_EQ_INT(hearths_burn(workstations, &game.map, 0.0f), 0);
    }
    CHECK_NEAR(station->fuel, HEARTH_FUEL_CAPACITY - HEARTH_BURN_PER_HOUR, 0.05);

    /* Crossing the refuel mark tells haulers. */
    workstation_get(workstations, index)->fuel =
        HEARTH_FUEL_CAPACITY * HEARTH_REFUEL_BELOW + 0.0001f;
    uint32_t revision = workstations->revision;
    hearths_burn(workstations, &game.map, 0.0f);
    CHECK(workstations->revision != revision);
    CHECK(hearth_fuel_wanted(station) > 0.0f);
    revision = workstations->revision;
    hearths_burn(workstations, &game.map, 0.0f);
    CHECK_EQ_UINT(workstations->revision, revision); /* Only once. */

    /* Out once the fuel is gone. */
    workstation_get(workstations, index)->fuel = 0.0001f;
    CHECK_EQ_INT(hearths_burn(workstations, &game.map, 0.0f), 1);
    CHECK(!station->burning);
    CHECK_NEAR(station->fuel, 0.0, 1e-9);
    CHECK_EQ_INT(hearths_burn(workstations, &game.map, 0.0f), 0);

    /* In a game, every tick. */
    workstation_get(workstations, index)->fuel = 5.0f;
    REQUIRE(hearth_light(workstations, index));
    game_tick(&game);
    CHECK(station->fuel < 5.0f);
    game_shutdown(&game);
}

static void test_chance_light_and_time(void)
{
    CHECK_NEAR(hearth_friction_chance(KNOWLEDGE_MASTERY_LEVEL), 0.3, 1e-6);
    CHECK_NEAR(hearth_friction_chance(75.0), 0.65, 1e-5);
    CHECK_NEAR(hearth_friction_chance(100.0), 1.0, 1e-5);
    CHECK_NEAR(hearth_friction_chance(0.0), 0.0, 1e-9);

    Game game;
    REQUIRE(init_flat_game(&game));
    int index = built_hearth(&game, 10, 10, 5.0f);
    CHECK_NEAR(hearths_light_at(&game.workstations, 10.5f, 10.5f, 0), 0.0, 1e-9); /* Out. */
    REQUIRE(hearth_light(&game.workstations, index));
    CHECK_NEAR(hearths_light_at(&game.workstations, 10.5f, 10.5f, 0), 1.0, 1e-6);
    CHECK_NEAR(hearths_light_at(&game.workstations, 13.5f, 10.5f, 0), 0.5, 1e-6);
    CHECK_NEAR(hearths_light_at(&game.workstations, 10.5f + HEARTH_LIGHT_RADIUS, 10.5f, 0), 0.0,
               1e-6);
    CHECK_NEAR(hearths_light_at(&game.workstations, 30.5f, 30.5f, 0), 0.0, 1e-9);
    /* The brightest fire wins. */
    int other = built_hearth(&game, 13, 10, 5.0f);
    REQUIRE(hearth_light(&game.workstations, other));
    CHECK_NEAR(hearths_light_at(&game.workstations, 13.5f, 10.5f, 0), 1.0, 1e-6);
    game_shutdown(&game);

    CHECK(!hearth_story_time(19 * TICKS_PER_HOUR - 1));
    CHECK(hearth_story_time(19 * TICKS_PER_HOUR));
    CHECK(hearth_story_time(TICKS_PER_DAY + 22 * TICKS_PER_HOUR - 1));
    CHECK(!hearth_story_time(22 * TICKS_PER_HOUR));
    CHECK(!hearth_story_time(12 * TICKS_PER_HOUR));
}

static void test_radiant_warmth(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    const Workstations *workstations = &game.workstations;
    int index = built_hearth(&game, 20, 20, HEARTH_FUEL_CAPACITY);
    CHECK_NEAR(hearths_radiant_warmth(workstations, &game.map, 21, 20, 0), 0.0, 1e-9); /* Out. */
    REQUIRE(hearth_light(&game.workstations, index));
    /* Falling off with the distance, nothing beyond the radius. */
    float one = hearths_radiant_warmth(workstations, &game.map, 21, 20, 0);
    CHECK_NEAR(one, HEARTH_RADIANT_WARMTH, 1e-4);
    CHECK_NEAR(hearths_radiant_warmth(workstations, &game.map, 20, 20, 0), HEARTH_RADIANT_WARMTH,
               1e-4);
    CHECK_NEAR(hearths_radiant_warmth(workstations, &game.map, 20, 22, 0),
               HEARTH_RADIANT_WARMTH * expf(-HEARTH_RADIANT_FALLOFF), 1e-4);
    CHECK_NEAR(hearths_radiant_warmth(workstations, &game.map, 17, 20, 0),
               HEARTH_RADIANT_WARMTH * expf(-2.0f * HEARTH_RADIANT_FALLOFF), 1e-4);
    CHECK(hearths_radiant_warmth(workstations, &game.map, 20 + HEARTH_RADIANT_RADIUS, 20, 0) >
          0.0f);
    CHECK_NEAR(hearths_radiant_warmth(workstations, &game.map, 21 + HEARTH_RADIANT_RADIUS, 20, 0),
               0.0, 1e-9);
    CHECK_NEAR(hearths_radiant_warmth(workstations, &game.map, 24, 24, 0), 0.0, 1e-9); /* 5.7. */
    /* A wall in between stops it, not one beside the way. */
    map_set_structure(&game.map, 22, 20, 0, STRUCTURE_WALL);
    CHECK_NEAR(hearths_radiant_warmth(workstations, &game.map, 23, 20, 0), 0.0, 1e-9);
    CHECK_NEAR(hearths_radiant_warmth(workstations, &game.map, 21, 20, 0), one, 1e-6);
    CHECK(hearths_radiant_warmth(workstations, &game.map, 22, 22, 0) > 0.0f);
    /* Felt: the air's plus the fire's. */
    CHECK_NEAR(hearths_felt_temperature(workstations, &game.map, &game.temperatures, 21, 20, 0),
               temperature_at(&game.temperatures, 21, 20, 0) + one, 1e-4);
    CHECK_NEAR(hearths_felt_temperature(workstations, &game.map, &game.temperatures, 40, 40, 0),
               temperature_at(&game.temperatures, 40, 40, 0), 1e-6);
    /* A fire low on fuel radiates less. */
    workstation_get(&game.workstations, index)->fuel = HEARTH_RADIANT_FULL_FUEL * 0.5f;
    CHECK_NEAR(hearths_radiant_warmth(workstations, &game.map, 21, 20, 0),
               HEARTH_RADIANT_WARMTH *
                   (HEARTH_RADIANT_LOW_FIRE + (1.0f - HEARTH_RADIANT_LOW_FIRE) * 0.5f),
               1e-4);
    /* Two fires add up. */
    workstation_get(&game.workstations, index)->fuel = HEARTH_FUEL_CAPACITY;
    int other = built_hearth(&game, 20, 24, HEARTH_FUEL_CAPACITY);
    REQUIRE(hearth_light(&game.workstations, other));
    CHECK_NEAR(hearths_radiant_warmth(workstations, &game.map, 20, 22, 0),
               2.0f * HEARTH_RADIANT_WARMTH * expf(-HEARTH_RADIANT_FALLOFF), 1e-4);
    game_shutdown(&game);
}

static void test_hearth_is_built_from_six_stones(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    spawn_settler(&game, 10, 10, 75.0, WORK_CONSTRUCTION);
    CHECK(game_workstation_buildable(&game, WORKSTATION_OPEN_HEARTH));
    REQUIRE(game_place_workstation(&game, WORKSTATION_OPEN_HEARTH, 14, 10, 0));
    CHECK(construction_at(&game.constructions, 14, 10, 0) != CONSTRUCTION_NONE);
    CHECK_EQ_INT(workstation_at(&game.workstations, 14, 10, 0), WORKSTATION_NONE);
    /* Without the stones, nothing is built. */
    run_ticks(&game, 1200, true);
    CHECK_EQ_INT(workstation_at(&game.workstations, 14, 10, 0), WORKSTATION_NONE);
    /* Six cobbles are brought, then the work is done. */
    items_add(&game.items, 6, 14, 0, item_stack_pristine(ITEM_COBBLE, 6));
    run_ticks(&game, 3000, true);
    CHECK_EQ_INT(game.items.totals[ITEM_COBBLE], 0);
    int index = workstation_at(&game.workstations, 14, 10, 0);
    REQUIRE(index != WORKSTATION_NONE);
    CHECK_EQ_INT(construction_at(&game.constructions, 14, 10, 0), CONSTRUCTION_NONE);
    const Workstation *station = hearth(&game, index);
    CHECK(hearth_is(station));
    CHECK(station->keep_burning);
    CHECK_EQ_UINT(station->fuel_types, HEARTH_DEFAULT_FUEL_TYPES);
    CHECK_NEAR(station->fuel, 0.0, 1e-9);
    game_shutdown(&game);
}

static void test_haulers_refuel(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *hauler = spawn_settler(&game, 10, 10, 75.0, WORK_HAULING);
    REQUIRE(hauler != NULL);
    int index = built_hearth(&game, 14, 10, 0.0f);
    items_add(&game.items, 20, 14, 0, item_stack_make(ITEM_BRANCHES, 20, 0, 0));
    items_add(&game.items, 6, 16, 0, item_stack_make(ITEM_TWIGS, 30, 0, 0));
    /* Twigs not allowed: only branches go in. */
    REQUIRE(hearth_set_fuel_type(&game.workstations, index, ITEM_TWIGS, false));
    run_ticks(&game, 3000, true);
    const Workstation *station = hearth(&game, index);
    CHECK(station->fuel >= HEARTH_FUEL_CAPACITY * HEARTH_REFUEL_BELOW);
    CHECK(station->fuel <= HEARTH_FUEL_CAPACITY);
    CHECK_NEAR(station->fuel_promised, 0.0, 1e-6);
    CHECK_EQ_INT((int)game.items.totals[ITEM_TWIGS], 30);
    float branch = hearth_fuel_value(ITEM_BRANCHES);
    CHECK_EQ_INT(20 - (int)game.items.totals[ITEM_BRANCHES], (int)(station->fuel / branch + 0.5f));
    CHECK_EQ_INT(hauler->carried.count, 0);

    /* Above the refuel mark: nothing more brought. */
    float fuel = station->fuel;
    run_ticks(&game, 600, true);
    CHECK_NEAR(station->fuel, fuel, 1e-6);

    /* Not kept burning: never refuelled. */
    workstation_get(&game.workstations, index)->fuel = 0.0f;
    REQUIRE(hearth_set_keep_burning(&game.workstations, index, false));
    run_ticks(&game, 1500, true);
    CHECK_NEAR(station->fuel, 0.0, 1e-9);
    /* Kept burning again, branches forbidden: twigs now. */
    REQUIRE(hearth_set_keep_burning(&game.workstations, index, true));
    REQUIRE(hearth_set_fuel_type(&game.workstations, index, ITEM_TWIGS, true));
    REQUIRE(hearth_set_fuel_type(&game.workstations, index, ITEM_BRANCHES, false));
    int branches = (int)game.items.totals[ITEM_BRANCHES];
    run_ticks(&game, 3000, true);
    CHECK(station->fuel > 0.0f);
    CHECK((int)game.items.totals[ITEM_TWIGS] < 30);
    CHECK_EQ_INT((int)game.items.totals[ITEM_BRANCHES], branches);
    game_shutdown(&game);
}

static void test_lighting_by_friction(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    /* Below the level needed: never lit. */
    Colonist *novice = spawn_settler(&game, 10, 10, HEARTH_LIGHT_MIN_LEVEL - 1.0, WORK_FIRE);
    REQUIRE(novice != NULL);
    int index = built_hearth(&game, 14, 10, 5.0f);
    items_add(&game.items, 18, 13, 0, item_stack_make(ITEM_GRASS, 3, 0, 0));
    kindling_at(&game, 6, 14, 3);
    CHECK(!colonist_can_light(novice));
    run_ticks(&game, 1500, true);
    CHECK(!hearth(&game, index)->burning);
    CHECK(novice->task != TASK_LIGHT);

    /* At level 100 friction always works: one handful of grass burnt. */
    Colonist *master = spawn_settler(&game, 10, 12, 100.0, WORK_FIRE);
    REQUIRE(master != NULL);
    CHECK(colonist_can_light(master));
    run_ticks(&game, 2500, true);
    CHECK(hearth(&game, index)->burning);
    CHECK_EQ_INT((int)game.items.totals[ITEM_GRASS], 2);
    /* Laid with one handful of twigs and one branch, gone in the flame. */
    CHECK_EQ_INT((int)game.items.totals[ITEM_TWIGS], 2);
    CHECK_EQ_INT((int)game.items.totals[ITEM_BRANCHES], 2);
    CHECK_EQ_UINT(hearth(&game, index)->laid, 0);
    CHECK(master->task != TASK_LIGHT);
    CHECK_EQ_INT(master->carried.count, 0);
    CHECK(colonist_knowledge_gain_today(master, KNOWLEDGE_FIRE, KNOWLEDGE_SOURCE_DOING,
                                        game.clock.tick) >= 0.0);
    game_shutdown(&game);
}

static void test_lighter_brings_fuel_to_an_empty_hearth(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *lighter = spawn_settler(&game, 10, 10, 100.0, WORK_FIRE);
    REQUIRE(lighter != NULL);
    int index = built_hearth(&game, 14, 10, 0.0f);
    items_add(&game.items, 18, 13, 0, item_stack_make(ITEM_GRASS, 3, 0, 0));
    CHECK(hearth_needs_kindling(hearth(&game, index)));

    /* Nothing to lay in it: nothing to do. */
    run_ticks(&game, 600, true);
    CHECK(!hearth(&game, index)->burning);
    CHECK(lighter->task != TASK_LIGHT);

    /* Twigs and branches lying somewhere: the fire laid, the other branches
     * put in, and the fire lit, by the one who lights fires (no hauler here). */
    items_add(&game.items, 6, 16, 0, item_stack_make(ITEM_BRANCHES, 4, 0, 0));
    items_add(&game.items, 8, 18, 0, item_stack_make(ITEM_TWIGS, 1, 0, 0));
    run_ticks(&game, 4000, true);
    const Workstation *station = hearth(&game, index);
    CHECK(station->burning);
    CHECK(station->fuel > HEARTH_LIGHT_FUEL_MIN);
    CHECK_EQ_INT((int)game.items.totals[ITEM_BRANCHES], 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_TWIGS], 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_GRASS], 2);
    CHECK_EQ_INT(lighter->carried.count, 0);
    CHECK(lighter->task != TASK_LIGHT);

    /* No branch left to lay in another: it stays cold, the twigs lying
     * where they are. */
    int other = built_hearth(&game, 30, 30, 0.0f);
    items_add(&game.items, 26, 30, 0, item_stack_make(ITEM_TWIGS, 5, 0, 0));
    items_add(&game.items, 31, 33, 0, item_stack_make(ITEM_GRASS, 3, 0, 0));
    run_ticks(&game, 2500, true);
    CHECK(!hearth(&game, other)->burning);
    CHECK_EQ_INT((int)game.items.totals[ITEM_TWIGS], 5);
    game_shutdown(&game);
}

static void test_failed_friction_burns_the_tinder(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    /* Out of luck (level 50: 30%): tries again with more grass, until lit
     * or out of grass; each attempt burns a handful. */
    Colonist *lighter = spawn_settler(&game, 10, 10, HEARTH_LIGHT_MIN_LEVEL, WORK_FIRE);
    REQUIRE(lighter != NULL);
    int index = built_hearth(&game, 14, 10, 20.0f);
    items_add(&game.items, 18, 13, 0, item_stack_make(ITEM_GRASS, 6, 0, 0));
    kindling_at(&game, 6, 14, 1);
    int attempts = 0;
    for (int i = 0; i < 40 && !hearth(&game, index)->burning; i++) {
        run_ticks(&game, 500, true);
        attempts = 6 - (int)game.items.totals[ITEM_GRASS] -
                   (lighter->carried.type == ITEM_GRASS ? lighter->carried.count : 0);
    }
    CHECK(attempts >= 1);
    CHECK(hearth(&game, index)->burning || game.items.totals[ITEM_GRASS] == 0);
    game_shutdown(&game);
}

static void test_laying_the_fire(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Workstations *stations = &game.workstations;
    int index = built_hearth(&game, 14, 10, 1.0f);
    REQUIRE(index >= 0);
    Workstation *station = workstation_get(stations, index);
    CHECK_EQ_INT(hearth_next_to_lay(station), HEARTH_KINDLING);
    CHECK(!hearth_laid(station, HEARTH_KINDLING) && !hearth_laid(station, HEARTH_BRANCH));
    CHECK(hearth_wants_fuel_to_light(station));
    /* Twigs first, not a branch. */
    CHECK(!hearth_lay(stations, index, ITEM_BRANCHES));
    CHECK(hearth_lay(stations, index, ITEM_TWIGS));
    CHECK(hearth_laid(station, HEARTH_KINDLING));
    CHECK_NEAR(station->fuel, 1.0f + hearth_fuel_value(ITEM_TWIGS), 1e-5);
    CHECK_EQ_INT(hearth_next_to_lay(station), HEARTH_BRANCH);
    CHECK(!hearth_lay(stations, index, ITEM_TWIGS));
    CHECK(hearth_lay(stations, index, ITEM_BRANCHES));
    CHECK_EQ_INT(hearth_next_to_lay(station), HEARTH_INGREDIENT_COUNT);
    CHECK(!hearth_lay(stations, index, ITEM_BRANCHES));
    CHECK(hearth_ingredient_types(HEARTH_TINDER) & ITEM_TYPE_BIT(ITEM_GRASS));
    /* It catches: laid again next time. */
    CHECK(hearth_light(stations, index));
    CHECK_EQ_UINT(station->laid, 0);
    CHECK_EQ_INT(hearth_next_to_lay(station), HEARTH_KINDLING);
    CHECK(!hearth_wants_fuel_to_light(station));
    game_shutdown(&game);
}

static void test_no_lighting_without_kindling(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *lighter = spawn_settler(&game, 10, 10, 100.0, WORK_FIRE);
    REQUIRE(lighter != NULL);
    int index = built_hearth(&game, 14, 10, 5.0f);
    items_add(&game.items, 18, 13, 0, item_stack_make(ITEM_GRASS, 3, 0, 0));
    /* Branches but only wet twigs: nothing catches. */
    items_add(&game.items, 6, 14, 0, item_stack_make(ITEM_BRANCHES, 2, 0, 0));
    ItemStack wet = item_stack_make(ITEM_TWIGS, 2, 0, 0);
    item_stack_set_moisture(&wet, 0.5f);
    items_add(&game.items, 6, 16, 0, wet);
    run_ticks(&game, 2000, true);
    CHECK(!hearth(&game, index)->burning);
    CHECK(lighter->task != TASK_LIGHT);
    CHECK_EQ_INT((int)game.items.totals[ITEM_GRASS], 3);
    CHECK_EQ_INT((int)game.items.totals[ITEM_TWIGS], 2);
    /* Dry twigs come: laid, then lit. */
    items_add(&game.items, 8, 16, 0, item_stack_make(ITEM_TWIGS, 2, 0, 0));
    run_ticks(&game, 3000, true);
    CHECK(hearth(&game, index)->burning);
    CHECK_EQ_INT((int)game.items.totals[ITEM_TWIGS], 3); /* The wet ones left alone. */
    CHECK_EQ_INT((int)game.items.totals[ITEM_BRANCHES], 1);
    game_shutdown(&game);
}

static void test_what_lighting_lacks(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int index = built_hearth(&game, 14, 10, 5.0f);
    const Workstation *station = hearth(&game, index);
    for (HearthIngredient i = 0; i < HEARTH_INGREDIENT_COUNT; i++) {
        CHECK(game_hearth_lacks(&game, station, i));
    }
    /* Wet twigs are no kindling. */
    ItemStack wet = item_stack_make(ITEM_TWIGS, 2, 0, 0);
    item_stack_set_moisture(&wet, 0.5f);
    items_add(&game.items, 4, 4, 0, wet);
    CHECK(game_hearth_lacks(&game, station, HEARTH_KINDLING));
    items_add(&game.items, 5, 4, 0, item_stack_make(ITEM_TWIGS, 1, 0, 0));
    items_add(&game.items, 6, 4, 0, item_stack_make(ITEM_MOSS, 1, 0, 0));
    CHECK(!game_hearth_lacks(&game, station, HEARTH_KINDLING));
    CHECK(!game_hearth_lacks(&game, station, HEARTH_TINDER));
    CHECK(game_hearth_lacks(&game, station, HEARTH_BRANCH));
    /* Laid: no longer lacking, though none lies anywhere. */
    REQUIRE(hearth_lay(&game.workstations, index, ITEM_TWIGS));
    REQUIRE(hearth_lay(&game.workstations, index, ITEM_BRANCHES));
    CHECK(!game_hearth_lacks(&game, station, HEARTH_BRANCH));
    /* Embers from another fire stand for tinder. */
    items_remove(&game.items, 6, 4, 0, 1);
    CHECK(game_hearth_lacks(&game, station, HEARTH_TINDER));
    int other = built_hearth(&game, 30, 30, 5.0f);
    REQUIRE(hearth_light(&game.workstations, other));
    CHECK(!game_hearth_lacks(&game, station, HEARTH_TINDER));
    game_shutdown(&game);
}

static void test_lighting_with_embers(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *lighter = spawn_settler(&game, 12, 12, 75.0, WORK_FIRE);
    REQUIRE(lighter != NULL);
    int source = built_hearth(&game, 10, 10, HEARTH_FUEL_CAPACITY);
    REQUIRE(hearth_light(&game.workstations, source));
    int index = built_hearth(&game, 18, 10, 5.0f);
    kindling_at(&game, 20, 14, 1);
    /* No grass anywhere: embers from the burning fire. */
    run_ticks(&game, 800, true);
    CHECK(hearth(&game, index)->burning);
    CHECK(lighter->task != TASK_LIGHT);
    CHECK_EQ_INT(lighter->ember_ticks, 0); /* Used up. */
    CHECK_EQ_INT((int)game.items.totals[ITEM_GRASS], 0);
    game_shutdown(&game);
}

static void test_embers_do_not_go_far(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *lighter = spawn_settler(&game, 3, 3, 75.0, WORK_FIRE);
    REQUIRE(lighter != NULL);
    int source = built_hearth(&game, 1, 1, HEARTH_FUEL_CAPACITY);
    REQUIRE(hearth_light(&game.workstations, source));
    int index = built_hearth(&game, 46, 46, 5.0f);
    run_ticks(&game, 800, true);
    CHECK(!hearth(&game, index)->burning);
    CHECK(lighter->task != TASK_LIGHT);
    CHECK_EQ_INT(lighter->ember_ticks, 0);
    game_shutdown(&game);
}

static void test_fire_hardening(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *crafter = spawn_settler(&game, 12, 12, 75.0, WORK_CRAFTING);
    REQUIRE(crafter != NULL);
    int index = built_hearth(&game, 14, 10, HEARTH_FUEL_CAPACITY);
    items_add(&game.items, 16, 12, 0,
              item_stack_make(ITEM_THROWING_SPEAR, 1, TEST_QUALITY_FINE,
                              item_hit_points_at_percent(ITEM_THROWING_SPEAR, 60)));
    REQUIRE(workstation_add_bill(&game.workstations, index, RECIPE_HARDEN_THROWING_SPEAR) >= 0);
    CHECK(recipe_needs_fire(RECIPE_HARDEN_THROWING_SPEAR));
    CHECK(!recipe_needs_fire(RECIPE_THROWING_SPEAR));

    /* No fire: the bill waits. */
    run_ticks(&game, 600, true);
    CHECK(crafter->task != TASK_CRAFT);
    CHECK_EQ_INT((int)game.items.totals[ITEM_THROWING_SPEAR], 1);
    CHECK_EQ_INT(hearth(&game, index)->work_input.count, 0);

    /* Lit: the spear is hardened, its quality and condition kept. */
    REQUIRE(hearth_light(&game.workstations, index));
    run_ticks(&game, 300, true);
    CHECK_EQ_INT(crafter->task, TASK_CRAFT);
    CHECK(hearth(&game, index)->work_progress > 0.0f);

    /* Out in the middle of it: the work stops and stays on the hearth. */
    REQUIRE(hearth_set_keep_burning(&game.workstations, index, false));
    workstation_get(&game.workstations, index)->fuel = 0.0001f;
    run_ticks(&game, 5, true);
    CHECK(!hearth(&game, index)->burning);
    float progress = hearth(&game, index)->work_progress;
    run_ticks(&game, 200, true);
    CHECK(crafter->task != TASK_CRAFT);
    CHECK_NEAR(hearth(&game, index)->work_progress, progress, 1e-4);
    CHECK_EQ_INT(hearth(&game, index)->work_input.type, ITEM_THROWING_SPEAR);

    workstation_get(&game.workstations, index)->fuel = HEARTH_FUEL_CAPACITY;
    REQUIRE(hearth_light(&game.workstations, index));
    run_ticks(&game, 1500, true);
    CHECK_EQ_INT((int)game.items.totals[ITEM_THROWING_SPEAR], 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_THROWING_SPEAR_HARDENED], 1);
    ItemStack spear = find_item(&game, ITEM_THROWING_SPEAR_HARDENED);
    if (spear.count == 0) {
        spear = crafter->carried;
    }
    CHECK_EQ_INT(spear.type, ITEM_THROWING_SPEAR_HARDENED);
    CHECK_EQ_INT(spear.quality, TEST_QUALITY_FINE);
    CHECK_EQ_INT(spear.hit_points, item_hit_points_at_percent(ITEM_THROWING_SPEAR_HARDENED, 60));
    /* Half as strong again. */
    CHECK_EQ_INT(item_def(ITEM_THROWING_SPEAR_HARDENED)->max_hit_points * 2,
                 item_def(ITEM_THROWING_SPEAR)->max_hit_points * 3);
    CHECK_EQ_INT(item_def(ITEM_THRUSTING_SPEAR_HARDENED)->max_hit_points * 2,
                 item_def(ITEM_THRUSTING_SPEAR)->max_hit_points * 3);
    game_shutdown(&game);
}

/* Rain falling all the time, as hard as `type` (rain or storm), and mild. */
static void run_rainy_ticks(Game *game, int ticks, WeatherType type)
{
    for (int i = 0; i < ticks; i++) {
        game->weather.type = (uint8_t)type;
        game->weather.spell = 5.0f;
        game->weather.debug_rain = WEATHER_WETTING_RAIN_MM; /* Every hour: soaked in one. */
        run_ticks(game, 1, true);
    }
}

static void test_rain_puts_out_fires_in_the_open(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int open = built_hearth(&game, 10, 10, HEARTH_FUEL_CAPACITY);
    int roofed = built_hearth(&game, 20, 10, HEARTH_FUEL_CAPACITY);
    map_set_structure(&game.map, 21, 10, 0, STRUCTURE_WALL); /* Holds the roof up. */
    map_set_roof(&game.map, 20, 10, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    REQUIRE(hearth_light(&game.workstations, open));
    REQUIRE(hearth_light(&game.workstations, roofed));
    float wetting = 1.0f; /* Soaked in an hour. */
    CHECK(hearth_rained_on(hearth(&game, open), &game.map, wetting));
    CHECK(!hearth_rained_on(hearth(&game, roofed), &game.map, wetting));
    CHECK(!hearth_rained_on(hearth(&game, open), &game.map, 0.0f));

    /* Two hours of rain. */
    run_rainy_ticks(&game, 2 * TICKS_PER_HOUR - 10, WEATHER_RAIN);
    CHECK(hearth(&game, open)->burning);
    CHECK(hearth(&game, open)->soaked > 0.9f * HEARTH_SOAKED_OUT);
    run_rainy_ticks(&game, 20, WEATHER_RAIN);
    CHECK(!hearth(&game, open)->burning);
    CHECK(hearth(&game, open)->fuel > 0.0f); /* The fuel is still there. */
    CHECK(hearth(&game, roofed)->burning);
    CHECK_NEAR(hearth(&game, roofed)->soaked, 0.0f, 1e-6);

    /* Nobody lights it in the rain; it is lit once the rain stops (laid
     * again with twigs and a branch kept dry under a roof). */
    Colonist *lighter = spawn_settler(&game, 12, 12, 75.0, WORK_FIRE);
    REQUIRE(lighter != NULL);
    map_set_structure(&game.map, 24, 12, 0, STRUCTURE_WALL);
    map_set_roof(&game.map, 22, 12, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    map_set_roof(&game.map, 23, 12, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    kindling_at(&game, 22, 12, 1);
    run_rainy_ticks(&game, 600, WEATHER_STORM);
    CHECK(!hearth(&game, open)->burning);
    CHECK(lighter->task != TASK_LIGHT);
    game.weather.debug_rain = 0.0f;
    for (int i = 0; i < 3000 && !hearth(&game, open)->burning; i++) {
        game.weather.type = WEATHER_CLEAR;
        run_ticks(&game, 1, true);
    }
    CHECK(hearth(&game, open)->burning);
    game_shutdown(&game);
}

static void test_roasting(void)
{
    /* One roasted root per raw one, keeping no longer than the raw ones
     * would have: bruised rhizomes with six days left (of 15) make roasted
     * ones as fresh as any (they keep 4 days)... */
    ItemStack raw = item_stack_make(ITEM_CATTAIL_RHIZOMES, 4, ITEM_QUALITY_STANDARD,
                                    item_hit_points_at_percent(ITEM_CATTAIL_RHIZOMES, 40));
    ItemStack out[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft(RECIPE_ROAST_RHIZOMES, raw, 90.0, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_ROASTED_RHIZOMES);
    CHECK_EQ_INT(out[0].count, 4);
    CHECK_EQ_INT(item_stack_hit_points_percent(out[0]), 100);
    /* ...and ones a day and a half from rotting away make roasted ones that
     * last as long (not 10 % of 4 days: roasting does not hasten the rot). */
    float raw_hours =
        item_stack_rot_hours(item_stack_make(ITEM_CATTAIL_RHIZOMES, 4, ITEM_QUALITY_STANDARD,
                                             item_hit_points_at_percent(ITEM_CATTAIL_RHIZOMES, 10)),
                             1.0f);
    raw.hit_points = (uint16_t)item_hit_points_at_percent(ITEM_CATTAIL_RHIZOMES, 10);
    REQUIRE(recipe_craft(RECIPE_ROAST_RHIZOMES, raw, 90.0, NULL, out) == 1);
    float roasted_hours = item_stack_rot_hours(out[0], 1.0f);
    float per_hit_point = item_def(ITEM_ROASTED_RHIZOMES)->rot_days * 24.0f /
                          (float)item_def(ITEM_ROASTED_RHIZOMES)->max_hit_points;
    CHECK(roasted_hours >= raw_hours && roasted_hours < raw_hours + per_hit_point);
    CHECK(item_stack_hit_points_percent(out[0]) < 100);
    CHECK(recipe_needs_fire(RECIPE_ROAST_RHIZOMES) && recipe_needs_fire(RECIPE_ROAST_CARROTS));
    CHECK_EQ_INT(recipe_def(RECIPE_ROAST_CARROTS)->product, ITEM_ROASTED_CARROTS);

    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *cook = spawn_settler(&game, 12, 12, 75.0, WORK_CRAFTING);
    REQUIRE(cook != NULL);
    int index = built_hearth(&game, 14, 10, HEARTH_FUEL_CAPACITY);
    REQUIRE(hearth_light(&game.workstations, index));
    items_add(&game.items, 16, 12, 0, item_stack_pristine(ITEM_WILD_CARROTS, 8));
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_ROAST_CARROTS);
    REQUIRE(bill >= 0);
    workstation_get(&game.workstations, index)->bills[bill].target = 2;
    run_ticks(&game, 2500, true);
    int roasted = (int)game.items.totals[ITEM_ROASTED_CARROTS] +
                  (cook->carried.type == ITEM_ROASTED_CARROTS ? cook->carried.count : 0);
    CHECK_EQ_INT(roasted, 8);
    CHECK_EQ_INT((int)game.items.totals[ITEM_WILD_CARROTS], 0);
    game_shutdown(&game);
}

/* Raw carrots lying in a heap: `count` of a quality, `percent` of their hit points. */
static void add_carrots(Game *game, int x, int y, int count, int quality, int percent)
{
    items_add(&game->items, x, y, 0,
              item_stack_make(ITEM_WILD_CARROTS, count, quality,
                              item_hit_points_at_percent(ITEM_WILD_CARROTS, percent)));
}

/* Carrots of the quality left on a tile (raw ones). */
static int carrots_left(const Game *game, int x, int y)
{
    return items_get_of(&game->items, x, y, 0, ITEM_TYPE_BIT(ITEM_WILD_CARROTS)).count;
}

/* A bill only takes ingredients within its quality range, item by item. */
static void test_bill_ingredient_quality(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *cook = spawn_settler(&game, 12, 12, 75.0, WORK_CRAFTING);
    REQUIRE(cook != NULL);
    int index = built_hearth(&game, 14, 10, HEARTH_FUEL_CAPACITY);
    REQUIRE(hearth_light(&game.workstations, index));
    add_carrots(&game, 16, 12, 4, 20, 100); /* Poor, nearest. */
    add_carrots(&game, 16, 12, 4, 90, 100); /* Fine, on top of them. */
    add_carrots(&game, 20, 12, 4, 50, 100); /* Middling, farther. */
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_ROAST_CARROTS);
    REQUIRE(bill >= 0);
    Bill *entry = &workstation_get(&game.workstations, index)->bills[bill];
    CHECK_EQ_INT(entry->min_quality, ITEM_QUALITY_WORST);
    CHECK_EQ_INT(entry->max_quality, ITEM_QUALITY_BEST);
    uint64_t before = workstations_hash(&game.workstations, HASH_FNV1A64_INIT);
    CHECK(workstation_set_bill_quality_range(&game.workstations, index, bill, 70, 40));
    CHECK_EQ_INT(entry->min_quality, 40); /* Crossed bounds are swapped. */
    CHECK_EQ_INT(entry->max_quality, 70);
    CHECK(workstations_hash(&game.workstations, HASH_FNV1A64_INIT) != before);
    CHECK(workstation_set_bill_quality_range(&game.workstations, index, bill, -5, 500));
    CHECK_EQ_INT(entry->min_quality, ITEM_QUALITY_WORST); /* Clamped. */
    CHECK_EQ_INT(entry->max_quality, ITEM_QUALITY_BEST);
    CHECK(!workstation_set_bill_quality_range(&game.workstations, index, 9, 0, 10));

    /* Only the middling ones fit 40..70: the nearer heap is left alone. */
    CHECK(workstation_set_bill_quality_range(&game.workstations, index, bill, 40, 70));
    entry->target = 1;
    run_ticks(&game, 2500, true);
    CHECK_EQ_INT((int)game.items.totals[ITEM_ROASTED_CARROTS] +
                     (cook->carried.type == ITEM_ROASTED_CARROTS ? cook->carried.count : 0),
                 4);
    CHECK_EQ_INT(carrots_left(&game, 20, 12), 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_WILD_CARROTS], 8);
    CHECK_EQ_INT(items_count_chosen(&game.items, 16, 12, 0, ITEM_WILD_CARROTS,
                                    (ItemChoice){80, 100, false, 0, false}),
                 4);
    game_shutdown(&game);
}

/* Cooking takes the ingredients that would rot first, even farther away. */
static void test_cooking_takes_what_rots_first(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *cook = spawn_settler(&game, 12, 12, 75.0, WORK_CRAFTING);
    REQUIRE(cook != NULL);
    int index = built_hearth(&game, 14, 10, HEARTH_FUEL_CAPACITY);
    REQUIRE(hearth_light(&game.workstations, index));
    add_carrots(&game, 16, 12, 4, 50, 100); /* Fresh, nearest. */
    add_carrots(&game, 24, 12, 2, 50, 30);  /* Soon gone, farther. */
    add_carrots(&game, 24, 12, 2, 50, 100);
    add_carrots(&game, 30, 12, 4, 50, 25); /* Sooner still, farther still... */
    items_add(&game.items, 30, 12, 0, item_stack_pristine(ITEM_COBBLE, 3)); /* ...under cobbles. */
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_ROAST_CARROTS);
    REQUIRE(bill >= 0);
    workstation_get(&game.workstations, index)->bills[bill].target = 1;
    run_ticks(&game, 3000, true);
    CHECK_EQ_INT((int)game.items.totals[ITEM_ROASTED_CARROTS] +
                     (cook->carried.type == ITEM_ROASTED_CARROTS ? cook->carried.count : 0),
                 4);
    CHECK_EQ_INT(carrots_left(&game, 30, 12), 0);
    CHECK_EQ_INT(items_get_of(&game.items, 30, 12, 0, ITEM_TYPE_BIT(ITEM_COBBLE)).count, 3);
    CHECK_EQ_INT(carrots_left(&game, 16, 12), 4);
    CHECK_EQ_INT(carrots_left(&game, 24, 12), 4);
    game_shutdown(&game);
}

/* Counts colonists doing `task`. */
static int count_task(const Game *game, ColonistTask task)
{
    int count = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            game->colonists[slot].task == task) {
            count++;
        }
    }
    return count;
}

static void test_fireside_stories(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int index = built_hearth(&game, 20, 20, HEARTH_FUEL_CAPACITY);
    REQUIRE(hearth_light(&game.workstations, index));
    Colonist *teller = spawn_settler(&game, 26, 20, 90.0, WORK_TYPE_COUNT);
    Colonist *listener = spawn_settler(&game, 14, 22, 10.0, WORK_TYPE_COUNT);
    Colonist *nearly = spawn_settler(&game, 20, 26, 10.0, WORK_TYPE_COUNT);
    REQUIRE(teller != NULL && listener != NULL && nearly != NULL);
    nearly->knowledge[KNOWLEDGE_FIRE] = KNOWLEDGE_STORY_CAP - 0.01;
    EntityId teller_id = game_colonist_id_at(&game, 0);
    CHECK(colonist_story_topic(teller, game.teach) == KNOWLEDGE_NONE);

    /* Nothing marked to teach: no story, even in the evening. */
    game.clock.tick = 19 * TICKS_PER_HOUR;
    run_ticks(&game, 300, false);
    CHECK_EQ_INT(count_task(&game, TASK_STORY), 0);

    /* Marked: the best placed tells about it, the others listen. */
    REQUIRE(game_set_teach(&game, KNOWLEDGE_FIRE, true));
    CHECK_EQ_INT(colonist_story_topic(teller, game.teach), KNOWLEDGE_FIRE);
    CHECK(colonist_story_topic(listener, game.teach) == KNOWLEDGE_NONE); /* Not mastered. */
    run_ticks(&game, TICKS_PER_HOUR, false);
    CHECK_EQ_INT(teller->task, TASK_STORY);
    CHECK_EQ_INT(teller->story_topic, KNOWLEDGE_FIRE);
    CHECK(colonist_telling(teller, &game.workstations));
    CHECK_EQ_INT(listener->task, TASK_LISTEN);
    CHECK(entity_id_equal(listener->mentor, teller_id));
    CHECK_EQ_INT(nearly->task, TASK_LISTEN);
    CHECK(listener->knowledge[KNOWLEDGE_FIRE] > 10.0);
    CHECK(colonist_knowledge_gain_today(listener, KNOWLEDGE_FIRE, KNOWLEDGE_SOURCE_STORIES,
                                        game.clock.tick) > 0.0);
    CHECK_NEAR(nearly->knowledge[KNOWLEDGE_FIRE], KNOWLEDGE_STORY_CAP, 1e-9); /* Capped. */
    /* Telling is practice. */
    CHECK(teller->knowledge_practice[KNOWLEDGE_FIRE] + 2 >= game.clock.tick);
    CHECK_NEAR(teller->knowledge[KNOWLEDGE_FIRE], 90.0, 1e-9);

    /* The evening ends at 22:00. */
    game.clock.tick = 22 * TICKS_PER_HOUR - 10;
    run_ticks(&game, 20, false);
    CHECK(teller->task != TASK_STORY);
    CHECK(listener->task != TASK_LISTEN);
    CHECK(nearly->task != TASK_LISTEN);

    /* A fire gone out ends it too. */
    game.clock.tick = TICKS_PER_DAY + 19 * TICKS_PER_HOUR;
    run_ticks(&game, TICKS_PER_HOUR / 2, false);
    CHECK_EQ_INT(teller->task, TASK_STORY);
    REQUIRE(hearth_set_keep_burning(&game.workstations, index, false));
    workstation_get(&game.workstations, index)->fuel = 0.0001f;
    run_ticks(&game, 20, false);
    CHECK(teller->task != TASK_STORY);
    CHECK(listener->task != TASK_LISTEN);
    game_shutdown(&game);
}

static void test_at_most_eight_listeners(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int index = built_hearth(&game, 20, 20, HEARTH_FUEL_CAPACITY);
    REQUIRE(hearth_light(&game.workstations, index));
    REQUIRE(game_set_teach(&game, KNOWLEDGE_FIRE, true));
    spawn_settler(&game, 22, 20, 90.0, WORK_TYPE_COUNT);
    for (int i = 0; i < HEARTH_STORY_LISTENERS + 3; i++) {
        spawn_settler(&game, 10 + i * 2, 30, 5.0, WORK_TYPE_COUNT);
    }
    game.clock.tick = 19 * TICKS_PER_HOUR;
    run_ticks(&game, TICKS_PER_HOUR, false);
    CHECK_EQ_INT(count_task(&game, TASK_STORY), 1);
    CHECK_EQ_INT(count_task(&game, TASK_LISTEN), HEARTH_STORY_LISTENERS);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_fuel);
    RUN_TEST(test_burning);
    RUN_TEST(test_chance_light_and_time);
    RUN_TEST(test_radiant_warmth);
    RUN_TEST(test_hearth_is_built_from_six_stones);
    RUN_TEST(test_haulers_refuel);
    RUN_TEST(test_lighting_by_friction);
    RUN_TEST(test_lighter_brings_fuel_to_an_empty_hearth);
    RUN_TEST(test_failed_friction_burns_the_tinder);
    RUN_TEST(test_laying_the_fire);
    RUN_TEST(test_no_lighting_without_kindling);
    RUN_TEST(test_what_lighting_lacks);
    RUN_TEST(test_lighting_with_embers);
    RUN_TEST(test_embers_do_not_go_far);
    RUN_TEST(test_fire_hardening);
    RUN_TEST(test_roasting);
    RUN_TEST(test_bill_ingredient_quality);
    RUN_TEST(test_cooking_takes_what_rots_first);
    RUN_TEST(test_rain_puts_out_fires_in_the_open);
    RUN_TEST(test_fireside_stories);
    RUN_TEST(test_at_most_eight_listeners);
    return test_summary();
}
