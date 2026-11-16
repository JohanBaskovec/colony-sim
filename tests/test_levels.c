/* Zones and workstations on levels other than 0: where they are found, what
 * the level shown holds (the renderer draws a level from stockpile_at(),
 * gather_zone_at() and Workstation.z), and settlers using them there. */
#include "core/game.h"
#include "core/hearth.h"

#include "test_framework.h"

#include <string.h>

/* The plains' level and the plateau's, one above. */
#define LOW 3
#define HIGH 4
/* The plateau covers x >= PLATEAU_X; a slope climbs to it from (PLATEAU_X - 1, 20). */
#define PLATEAU_X 24

/* A 48 x 48 game with no colonists, at noon: soil plains on level LOW over
 * earth, a plateau of soil on level HIGH over x >= PLATEAU_X, open air above. */
static bool init_raised_game(Game *game)
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
    if (map->depth <= HIGH + 2) {
        return false;
    }
    for (size_t i = 0; i < map_tile_count(map); i++) {
        int x, y, z;
        map_tile_of(map, i, &x, &y, &z);
        int ground = x >= PLATEAU_X ? HIGH : LOW;
        map->terrain[i] = (uint8_t)(z <= ground ? TERRAIN_SOIL : TERRAIN_OPEN);
        map->rock[i] = (uint8_t)(z < ground ? ROCK_EARTH : ROCK_NONE);
        map->slope[i] = 0;
        map->cobbles[i] = 0;
        map->flint[i] = 0;
        map->branches[i] = map->twigs[i] = 0;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map->slope[map_index(map, PLATEAU_X - 1, 20, LOW)] = 1;
    map->slope_count = 1;
    map->surface_level = LOW;
    map->top_ground_level = HIGH;
    map_topology_changed(map); /* The layers were written directly. */
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

/* Rested colonist at level 75 in every knowledge (speed x1). */
static Colonist *spawn_worker(Game *game, int x, int y, int z)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, z));
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

/* Keeps needs satisfied and the hour at noon. */
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

/* Every station is found on its own tile and level, and on no other level
 * of its column: what the renderer and settlers rely on. */
static void check_stations_on_their_tiles(const Game *game)
{
    const Workstations *workstations = &game->workstations;
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        const Workstation *station = workstation_get_const(workstations, index);
        if (station == NULL) {
            continue;
        }
        for (int z = 0; z < game->map.depth; z++) {
            CHECK_EQ_INT(workstation_at(workstations, station->x, station->y, z),
                         z == station->z ? index : WORKSTATION_NONE);
        }
    }
}

/* Index of the station on a tile, checked to be there and on that level. */
static int station_on(const Game *game, WorkstationType type, int x, int y, int z)
{
    int index = workstation_at(&game->workstations, x, y, z);
    const Workstation *station = workstation_get_const(&game->workstations, index);
    CHECK(station != NULL);
    if (station == NULL) {
        return WORKSTATION_NONE;
    }
    CHECK_EQ_INT(station->type, type);
    CHECK_EQ_INT(station->x, x);
    CHECK_EQ_INT(station->y, y);
    CHECK_EQ_INT(station->z, z);
    return index;
}

static void test_stockpiles_on_their_level(void)
{
    Game game;
    REQUIRE(init_raised_game(&game));
    Stockpiles *stockpiles = &game.stockpiles;
    int low = stockpile_create(stockpiles, &game.map, 2, 2, 6, 6, LOW);
    int high = stockpile_create(stockpiles, &game.map, 28, 2, 32, 6, HIGH);
    REQUIRE(low != STOCKPILE_NONE && high != STOCKPILE_NONE && low != high);
    for (int z = 0; z < game.map.depth; z++) {
        CHECK_EQ_INT(stockpile_at(stockpiles, 4, 4, z), z == LOW ? low : STOCKPILE_NONE);
        CHECK_EQ_INT(stockpile_at(stockpiles, 30, 4, z), z == HIGH ? high : STOCKPILE_NONE);
    }
    /* Not inside the ground nor in the air over it. */
    CHECK_EQ_INT(stockpile_create(stockpiles, &game.map, 2, 10, 6, 14, LOW - 1), STOCKPILE_NONE);
    CHECK_EQ_INT(stockpile_create(stockpiles, &game.map, 2, 10, 6, 14, LOW + 1), STOCKPILE_NONE);
    CHECK_EQ_INT(stockpile_create(stockpiles, &game.map, 28, 10, 32, 14, HIGH + 1),
                 STOCKPILE_NONE);

    /* Items there are stored, on their level only. */
    ItemStack cobbles = item_stack_pristine(ITEM_COBBLE, 2);
    CHECK(stockpile_storage_priority(stockpiles, 4, 4, LOW, cobbles) > 0);
    CHECK(stockpile_storage_priority(stockpiles, 30, 4, HIGH, cobbles) > 0);
    CHECK_EQ_INT(stockpile_storage_priority(stockpiles, 30, 4, LOW, cobbles), 0);

    /* Taking the tiles of one level away leaves the other's. */
    stockpiles_remove_rect(stockpiles, 0, 0, 47, 47, LOW);
    CHECK_EQ_INT(stockpile_at(stockpiles, 4, 4, LOW), STOCKPILE_NONE);
    CHECK_EQ_INT(stockpile_at(stockpiles, 30, 4, HIGH), high);
    game_shutdown(&game);
}

static void test_gather_zones_on_their_level(void)
{
    Game game;
    REQUIRE(init_raised_game(&game));
    GatherZones *zones = &game.gather_zones;
    int low = gather_zone_create(zones, &game.map, 2, 2, 6, 6, LOW);
    int high = gather_zone_create(zones, &game.map, 28, 2, 32, 6, HIGH);
    REQUIRE(low != GATHER_ZONE_NONE && high != GATHER_ZONE_NONE && low != high);
    for (int z = 0; z < game.map.depth; z++) {
        CHECK_EQ_INT(gather_zone_at(zones, 4, 4, z), z == LOW ? low : GATHER_ZONE_NONE);
        CHECK_EQ_INT(gather_zone_at(zones, 30, 4, z), z == HIGH ? high : GATHER_ZONE_NONE);
    }
    CHECK_EQ_INT(gather_zone_create(zones, &game.map, 2, 10, 6, 14, LOW - 1), GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_create(zones, &game.map, 28, 10, 32, 14, HIGH + 1),
                 GATHER_ZONE_NONE);

    /* One zone over both levels, each tile on its own level. */
    int both = gather_zone_create(zones, &game.map, 10, 30, 14, 34, LOW);
    REQUIRE(both != GATHER_ZONE_NONE);
    CHECK(gather_zone_extend(zones, &game.map, both, 36, 30, 40, 34, HIGH) > 0);
    CHECK_EQ_INT(gather_zone_at(zones, 12, 32, LOW), both);
    CHECK_EQ_INT(gather_zone_at(zones, 38, 32, HIGH), both);
    CHECK_EQ_INT(gather_zone_at(zones, 38, 32, LOW), GATHER_ZONE_NONE);

    gather_zones_remove_rect(zones, 0, 0, 47, 47, LOW);
    CHECK_EQ_INT(gather_zone_at(zones, 4, 4, LOW), GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_at(zones, 12, 32, LOW), GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_at(zones, 30, 4, HIGH), high);
    CHECK_EQ_INT(gather_zone_at(zones, 38, 32, HIGH), both);
    game_shutdown(&game);
}

static void test_stations_on_their_level(void)
{
    Game game;
    REQUIRE(init_raised_game(&game));
    /* A zone under a station leaves it, on that level only. */
    int stockpile = stockpile_create(&game.stockpiles, &game.map, 8, 8, 12, 12, LOW);
    REQUIRE(stockpile != STOCKPILE_NONE);

    REQUIRE(game_place_workstation(&game, WORKSTATION_CRAFTING_SPOT, 10, 10, LOW));
    REQUIRE(game_place_workstation(&game, WORKSTATION_CRAFTING_SPOT, 30, 10, HIGH));
    int low = station_on(&game, WORKSTATION_CRAFTING_SPOT, 10, 10, LOW);
    int high = station_on(&game, WORKSTATION_CRAFTING_SPOT, 30, 10, HIGH);
    CHECK(low != high);
    check_stations_on_their_tiles(&game);
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 10, 10, LOW), STOCKPILE_NONE);
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 11, 10, LOW), stockpile);
    /* Not on the tile again, nor in the air or the ground. */
    CHECK(!game_can_place_workstation(&game, 10, 10, LOW));
    CHECK(!game_can_place_workstation(&game, 10, 10, LOW + 1));
    CHECK(!game_can_place_workstation(&game, 30, 10, HIGH - 1));

    /* Removed from its own level: the tile is free again, the other stays. */
    CHECK(game_remove_workstation(&game, low));
    CHECK_EQ_INT(workstation_at(&game.workstations, 10, 10, LOW), WORKSTATION_NONE);
    CHECK_EQ_INT(workstation_at(&game.workstations, 30, 10, HIGH), high);
    check_stations_on_their_tiles(&game);
    CHECK(game_can_place_workstation(&game, 10, 10, LOW));
    REQUIRE(game_place_workstation(&game, WORKSTATION_CRAFTING_SPOT, 10, 10, LOW));
    station_on(&game, WORKSTATION_CRAFTING_SPOT, 10, 10, LOW);
    check_stations_on_their_tiles(&game);
    game_shutdown(&game);
}

/* Station sites built by a settler become stations on the site's level. */
static void test_built_stations_on_their_level(void)
{
    Game game;
    REQUIRE(init_raised_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 20, LOW);
    REQUIRE(colonist != NULL);
    REQUIRE(game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 14, 16, LOW));
    REQUIRE(game_place_workstation(&game, WORKSTATION_KNAPPING_SPOT, 30, 16, HIGH));
    REQUIRE(game_place_workstation(&game, WORKSTATION_OPEN_HEARTH, 14, 24, LOW));
    CHECK(construction_at(&game.constructions, 14, 16, LOW) != CONSTRUCTION_NONE);
    CHECK(construction_at(&game.constructions, 30, 16, HIGH) != CONSTRUCTION_NONE);
    CHECK_EQ_INT(game.workstations.used_count, 0);
    items_add(&game.items, 8, 14, LOW, item_stack_pristine(ITEM_COBBLE, 3));

    run_ticks(&game, 6000);
    CHECK_EQ_INT(game.constructions.workstation_count, 0);
    CHECK_EQ_INT(game.workstations.used_count, 3);
    station_on(&game, WORKSTATION_KNAPPING_SPOT, 14, 16, LOW);
    station_on(&game, WORKSTATION_KNAPPING_SPOT, 30, 16, HIGH);
    station_on(&game, WORKSTATION_OPEN_HEARTH, 14, 24, LOW);
    check_stations_on_their_tiles(&game);
    game_shutdown(&game);
}

/* A settler on the plains crafts at a station up on the plateau. */
static void test_crafting_on_a_raised_level(void)
{
    Game game;
    REQUIRE(init_raised_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 20, LOW);
    REQUIRE(colonist != NULL);
    int index = workstations_place(&game.workstations, &game.map, WORKSTATION_KNAPPING_SPOT, 30,
                                   12, HIGH);
    REQUIRE(index != WORKSTATION_NONE);
    station_on(&game, WORKSTATION_KNAPPING_SPOT, 30, 12, HIGH);
    items_add(&game.items, 12, 20, LOW, item_stack_pristine(ITEM_COBBLE, 1)); /* The hammer. */
    items_add(&game.items, 32, 14, HIGH, item_stack_pristine(ITEM_FLINT, 2));
    int bill = workstation_add_bill(&game.workstations, index, RECIPE_FLAKES);
    REQUIRE(bill >= 0);
    workstation_set_bill_target(&game.workstations, index, bill, 1);

    run_ticks(&game, 6000);
    CHECK_EQ_INT(workstation_get_const(&game.workstations, index)->bills[bill].done, 1);
    CHECK(game.items.totals[ITEM_FLAKE] > 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLINT], 1);
    game_shutdown(&game);
}

/* A fire lights and warms its own level, not the one above nor below. */
static void test_hearth_on_its_level(void)
{
    Game game;
    REQUIRE(init_raised_game(&game));
    int index =
        workstations_place(&game.workstations, &game.map, WORKSTATION_OPEN_HEARTH, 30, 30, HIGH);
    REQUIRE(index != WORKSTATION_NONE);
    workstation_get(&game.workstations, index)->fuel = HEARTH_FUEL_CAPACITY;
    REQUIRE(hearth_light(&game.workstations, index));
    CHECK_NEAR(hearths_light_at(&game.workstations, 30.5f, 30.5f, HIGH), 1.0, 1e-6);
    CHECK_NEAR(hearths_light_at(&game.workstations, 30.5f, 30.5f, HIGH + 1), 0.0, 1e-9);
    CHECK_NEAR(hearths_light_at(&game.workstations, 30.5f, 30.5f, LOW), 0.0, 1e-9);
    CHECK(hearths_radiant_warmth(&game.workstations, &game.map, 31, 30, HIGH) > 0.0f);
    CHECK_NEAR(hearths_radiant_warmth(&game.workstations, &game.map, 31, 30, HIGH + 1), 0.0,
               1e-9);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_stockpiles_on_their_level);
    RUN_TEST(test_gather_zones_on_their_level);
    RUN_TEST(test_stations_on_their_level);
    RUN_TEST(test_built_stations_on_their_level);
    RUN_TEST(test_crafting_on_a_raised_level);
    RUN_TEST(test_hearth_on_its_level);
    return test_summary();
}
