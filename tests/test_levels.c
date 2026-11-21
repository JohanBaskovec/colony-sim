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
        map->pore[i] = (uint16_t)rock_pore_capacity((RockType)map->rock[i]); /* No table. */
        map->slope[i] = 0;
        map->water[i] = 0;
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_COBBLE));
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_FLINT));
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_BRANCHES) | ITEM_TYPE_BIT(ITEM_TWIGS));
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map->slope[map_index(map, PLATEAU_X - 1, 20, LOW)] = 1;
    map->slope_count = 1;
    map->surface_level = LOW;
    map->top_ground_level = HIGH;
    map_topology_changed(map);       /* The layers were written directly. */
    groundwater_free(&game->ground); /* No water table beyond the edges either. */
    if (!groundwater_init(&game->ground, map)) {
        return false;
    }
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

/* What is seen on a tile: the level shown, or the first one below that is not
 * open space, however deep. */
static void test_view_level(void)
{
    Game game;
    REQUIRE(init_raised_game(&game));
    const Map *map = &game.map;
    CHECK_EQ_INT(map_view_level(map, 30, 10, HIGH), HIGH);    /* The plateau itself. */
    CHECK_EQ_INT(map_view_level(map, 10, 10, HIGH), LOW);     /* Open air over the plains. */
    CHECK_EQ_INT(map_view_level(map, 10, 10, HIGH + 1), LOW); /* From higher up. */
    CHECK_EQ_INT(map_view_level(map, 10, 10, LOW), LOW);
    CHECK_EQ_INT(map_view_level(map, 30, 10, LOW), LOW); /* Earth: the wall is what is seen. */
    CHECK_EQ_INT(map_view_level(map, -1, 10, LOW), -1);
    CHECK_EQ_INT(map_view_level(map, 10, 10, map->depth), -1);
    /* However much air there is: from the top of the map, the plains are seen. */
    CHECK(map->depth - 1 - LOW > 6);
    CHECK_EQ_INT(map_view_level(map, 10, 10, map->depth - 1), LOW);
    /* Nothing seen down a pit open to the bottom of the map. */
    Game deep;
    REQUIRE(init_raised_game(&deep));
    Map *pit = &deep.map;
    for (int z = 0; z <= LOW; z++) {
        size_t i = map_index(pit, 5, 5, z);
        pit->terrain[i] = TERRAIN_OPEN;
        pit->water[i] = 0;
        pit->rock[i] = ROCK_NONE;
    }
    map_topology_changed(pit);
    int top = LOW + 1; /* Open from the ground up to here. */
    CHECK_EQ_INT(map_view_level(pit, 5, 5, top), -1);
    CHECK_EQ_INT(map_view_level(pit, 5, 5, 0), -1);
    game_shutdown(&deep);
    game_shutdown(&game);
}

/* A zone drawn from a level covers what is seen there: its own ground, and
 * the ground below where it is open space; the tools that remove tiles do
 * the same. */
static void test_zones_cover_the_levels_seen(void)
{
    Game game;
    REQUIRE(init_raised_game(&game));
    /* Seen from the plateau's level: x 20..23 is the plains below. */
    int stockpile = game_add_stockpile_rect(&game, 20, 10, 27, 11, HIGH, STOCKPILE_NONE);
    REQUIRE(stockpile != STOCKPILE_NONE);
    for (int y = 10; y <= 11; y++) {
        for (int x = 20; x <= 27; x++) {
            int level = x < PLATEAU_X ? LOW : HIGH;
            CHECK_EQ_INT(stockpile_at(&game.stockpiles, x, y, level), stockpile);
            CHECK_EQ_INT(stockpile_at(&game.stockpiles, x, y, level == LOW ? HIGH : LOW),
                         STOCKPILE_NONE);
        }
    }
    CHECK_EQ_INT(stockpile_get(&game.stockpiles, stockpile)->cell_count, 16);

    /* A gathering zone likewise, over the stockpile's tiles too (zones stack). */
    int zone = game_add_gather_zone_rect(&game, 18, 10, 21, 12, HIGH, GATHER_ZONE_NONE);
    REQUIRE(zone != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 18, 12, LOW), zone);
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 21, 12, LOW), zone);
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 21, 10, LOW), zone);
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 21, 10, LOW), stockpile);
    CHECK_EQ_INT(gather_zone_get(&game.gather_zones, zone)->cell_count, 4 * 3);

    /* From the plains' level the plateau is a wall: nothing there to cover. */
    CHECK_EQ_INT(game_add_stockpile_rect(&game, 30, 20, 33, 21, LOW, STOCKPILE_NONE),
                 STOCKPILE_NONE);

    /* Removing from the level shown takes what is seen there. */
    CHECK_EQ_INT(game_remove_stockpile_rect(&game, 20, 10, 27, 11, HIGH), 16);
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 21, 10, LOW), STOCKPILE_NONE);
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 25, 10, HIGH), STOCKPILE_NONE);
    CHECK_EQ_INT(game_remove_gather_zone_rect(&game, 0, 0, 47, 47, HIGH), 4 * 3);
    game_shutdown(&game);
}

/* Building follows what is seen: from the plateau's level, the plains below are
 * built on as if they were the level shown; a roof goes on the open level over
 * the ground seen. */
static void test_building_on_the_levels_seen(void)
{
    Game game;
    REQUIRE(init_raised_game(&game));
    CHECK_EQ_INT(game_build_level(&game, 10, 10, HIGH, false), LOW);
    CHECK_EQ_INT(game_build_level(&game, 10, 10, HIGH + 2, false), LOW);
    CHECK_EQ_INT(game_build_level(&game, 10, 10, LOW, false), LOW);
    CHECK_EQ_INT(game_build_level(&game, 30, 10, HIGH, false), HIGH);
    CHECK_EQ_INT(game_build_level(&game, 10, 10, HIGH, true), HIGH); /* Over the plains. */
    CHECK_EQ_INT(game_build_level(&game, 10, 10, HIGH + 2, true), HIGH);
    CHECK_EQ_INT(game_build_level(&game, 30, 10, HIGH, true), HIGH); /* The plateau itself. */

    /* A row of walls drawn from the plateau's level: each on the ground seen. */
    CHECK_EQ_INT(
        game_place_structure_rect(&game, STRUCTURE_WALL, DIRECTION_EAST, 20, 10, 27, 10, HIGH), 8);
    for (int x = 20; x <= 27; x++) {
        int level = x < PLATEAU_X ? LOW : HIGH;
        CHECK(construction_at(&game.constructions, x, 10, level) != CONSTRUCTION_NONE);
        CHECK_EQ_INT(construction_at(&game.constructions, x, 10, level == LOW ? HIGH : LOW),
                     CONSTRUCTION_NONE);
    }

    /* A workstation likewise. */
    CHECK(game_can_place_workstation(&game, 10, 12, game_build_level(&game, 10, 12, HIGH, false)));
    CHECK(game_place_workstation(&game, WORKSTATION_CRAFTING_SPOT, 10, 12,
                                 game_build_level(&game, 10, 12, HIGH, false)));
    CHECK(workstation_at(&game.workstations, 10, 12, LOW) != WORKSTATION_NONE ||
          construction_at(&game.constructions, 10, 12, LOW) != CONSTRUCTION_NONE);
    CHECK_EQ_INT(workstation_at(&game.workstations, 10, 12, HIGH), WORKSTATION_NONE);
    CHECK_EQ_INT(construction_at(&game.constructions, 10, 12, HIGH), CONSTRUCTION_NONE);

    /* A roof drawn from higher up goes on the open level over the wall's. */
    map_set_structure(&game.map, 5, 5, LOW, STRUCTURE_WALL);
    CHECK_EQ_INT(
        game_place_structure_rect(&game, STRUCTURE_ROOF, DIRECTION_EAST, 5, 5, 5, 5, HIGH + 2), 1);
    CHECK(construction_at(&game.constructions, 5, 5, HIGH) != CONSTRUCTION_NONE);
    game_shutdown(&game);
}

static void test_selection_covers_the_levels_seen(void)
{
    Game game;
    REQUIRE(init_raised_game(&game));
    EntityId plains = game_spawn_colonist(&game, 20, 10, LOW);
    EntityId plateau = game_spawn_colonist(&game, 30, 10, HIGH);
    REQUIRE(!entity_id_is_none(plains) && !entity_id_is_none(plateau));

    /* From the plateau's level, the plains below are seen (open air over them). */
    EntityId found[4];
    CHECK_EQ_INT(game_colonists_in_rect(&game, 0.0f, 0.0f, 48.0f, 48.0f, HIGH, found, 4), 2);
    CHECK(entity_id_equal(game_colonist_at(&game, 20.5f, 10.5f, HIGH), plains));
    CHECK(entity_id_equal(game_colonist_at(&game, 30.5f, 10.5f, HIGH), plateau));
    /* Likewise from higher up, through more open air. */
    CHECK_EQ_INT(game_colonists_in_rect(&game, 0.0f, 0.0f, 48.0f, 48.0f, HIGH + 1, found, 4), 2);
    /* The rectangle still limits which. */
    CHECK_EQ_INT(game_colonists_in_rect(&game, 18.0f, 8.0f, 22.0f, 12.0f, HIGH, found, 4), 1);
    CHECK(entity_id_equal(found[0], plains));

    /* From the plains' level the plateau is above, and its wall hides nothing: only
     * the plains' colonist is seen. */
    CHECK_EQ_INT(game_colonists_in_rect(&game, 0.0f, 0.0f, 48.0f, 48.0f, LOW, found, 4), 1);
    CHECK(entity_id_equal(found[0], plains));
    CHECK(entity_id_is_none(game_colonist_at(&game, 30.5f, 10.5f, LOW)));

    /* Under the roof of earth of the plateau, the level above does not show the colonist
     * standing in a tunnel: a closed tile hides what is below it. */
    Map *map = &game.map;
    int i = map_index(map, 40, 30, HIGH - 1);
    map->rock[i] = ROCK_NONE; /* A walkable cell of soil, with ground over it. */
    map_topology_changed(map);
    EntityId hidden = game_spawn_colonist(&game, 40, 30, HIGH - 1);
    REQUIRE(!entity_id_is_none(hidden));
    CHECK(entity_id_is_none(game_colonist_at(&game, 40.5f, 30.5f, HIGH)));
    CHECK_EQ_INT(game_colonists_in_rect(&game, 39.0f, 29.0f, 41.0f, 31.0f, HIGH, found, 4), 0);
    game_shutdown(&game);
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
    CHECK_EQ_INT(stockpile_create(stockpiles, &game.map, 28, 10, 32, 14, HIGH + 1), STOCKPILE_NONE);

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
    CHECK_EQ_INT(gather_zone_create(zones, &game.map, 28, 10, 32, 14, HIGH + 1), GATHER_ZONE_NONE);

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
    REQUIRE(game_place_workstation(&game, WORKSTATION_OPEN_HEARTH, 14, 16, LOW));
    REQUIRE(game_place_workstation(&game, WORKSTATION_OPEN_HEARTH, 30, 16, HIGH));
    REQUIRE(game_place_workstation(&game, WORKSTATION_OPEN_HEARTH, 14, 24, LOW));
    CHECK(construction_at(&game.constructions, 14, 16, LOW) != CONSTRUCTION_NONE);
    CHECK(construction_at(&game.constructions, 30, 16, HIGH) != CONSTRUCTION_NONE);
    CHECK_EQ_INT(game.workstations.used_count, 0);
    items_add(&game.items, 8, 14, LOW, item_stack_pristine(ITEM_COBBLE, 20));

    run_ticks(&game, 12000);
    CHECK_EQ_INT(game.constructions.workstation_count, 0);
    CHECK_EQ_INT(game.workstations.used_count, 3);
    station_on(&game, WORKSTATION_OPEN_HEARTH, 14, 16, LOW);
    station_on(&game, WORKSTATION_OPEN_HEARTH, 30, 16, HIGH);
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
    int index =
        workstations_place(&game.workstations, &game.map, WORKSTATION_CRAFTING_SPOT, 30, 12, HIGH);
    REQUIRE(index != WORKSTATION_NONE);
    station_on(&game, WORKSTATION_CRAFTING_SPOT, 30, 12, HIGH);
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
    CHECK_NEAR(hearths_radiant_warmth(&game.workstations, &game.map, 31, 30, HIGH + 1), 0.0, 1e-9);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_view_level);
    RUN_TEST(test_zones_cover_the_levels_seen);
    RUN_TEST(test_selection_covers_the_levels_seen);
    RUN_TEST(test_building_on_the_levels_seen);
    RUN_TEST(test_stockpiles_on_their_level);
    RUN_TEST(test_gather_zones_on_their_level);
    RUN_TEST(test_stations_on_their_level);
    RUN_TEST(test_built_stations_on_their_level);
    RUN_TEST(test_crafting_on_a_raised_level);
    RUN_TEST(test_hearth_on_its_level);
    return test_summary();
}
