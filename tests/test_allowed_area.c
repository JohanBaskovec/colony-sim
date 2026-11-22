/* Allowed areas (core/allowed_area.h): the areas themselves, the searches that keep to them,
 * and the settlers bound to them (game_add_area()... in core/game.h). */
#include "core/alert.h"
#include "core/allowed_area.h"
#include "core/game.h"
#include "core/pathfinding.h"
#include "core/serialize.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <stdlib.h>
#include <string.h>

/* ---- The areas ------------------------------------------------------------- */

static void test_a_new_set_holds_the_whole_map(void)
{
    AllowedAreas areas;
    REQUIRE(allowed_areas_init(&areas, 8, 6, 3));
    CHECK_EQ_INT(areas.count, 1);
    CHECK_EQ_INT((int)areas.list[0].tiles, 8 * 6 * 3);
    CHECK_STR_EQ(areas.list[0].name, "");
    CHECK(allowed_areas_holds(&areas, 0, 7, 5, 2));
    CHECK(!allowed_areas_holds(&areas, 0, 8, 5, 2)); /* Out of the map. */
    CHECK(!allowed_areas_holds(&areas, 1, 0, 0, 0)); /* No such area. */
    allowed_areas_free(&areas);
}

static void test_boxes_add_and_take_away_tiles(void)
{
    AllowedAreas areas;
    REQUIRE(allowed_areas_init(&areas, 10, 10, 4));
    int inside = allowed_areas_add(&areas, "Inside");
    REQUIRE(inside == 1);
    CHECK_EQ_INT((int)areas.list[inside].tiles, 0);
    CHECK_EQ_INT((int)allowed_areas_set_box(&areas, inside, 4, 4, 2, 2, 2, 1, true), 3 * 3 * 2);
    CHECK_EQ_INT((int)areas.list[inside].tiles, 18);
    CHECK(allowed_areas_holds(&areas, inside, 3, 3, 1));
    CHECK(!allowed_areas_holds(&areas, inside, 3, 3, 0));
    CHECK(!allowed_areas_holds(&areas, inside, 3, 3, 3));
    /* Again: nothing changes. Cut to the map. */
    CHECK_EQ_INT((int)allowed_areas_set_box(&areas, inside, 2, 2, 1, 4, 4, 2, true), 0);
    CHECK_EQ_INT((int)allowed_areas_set_box(&areas, inside, -5, 9, 0, -1, 20, 3, true), 0);
    CHECK_EQ_INT((int)allowed_areas_set_box(&areas, inside, 8, 8, 3, 30, 30, 9, true), 4);
    CHECK_EQ_INT((int)areas.list[inside].tiles, 22);
    CHECK_EQ_INT((int)allowed_areas_set_box(&areas, inside, 3, 3, 1, 3, 3, 2, false), 2);
    CHECK_EQ_INT((int)areas.list[inside].tiles, 20);
    CHECK(!allowed_areas_holds(&areas, inside, 3, 3, 1));
    /* The other areas keep their tiles. */
    CHECK_EQ_INT((int)areas.list[0].tiles, 400);
    CHECK(allowed_areas_holds(&areas, 0, 3, 3, 1));
    allowed_areas_free(&areas);
}

static void test_inverting_swaps_the_tiles(void)
{
    AllowedAreas areas;
    REQUIRE(allowed_areas_init(&areas, 5, 5, 2));
    int area = allowed_areas_add(&areas, "A");
    allowed_areas_set_box(&areas, area, 0, 0, 0, 1, 1, 1, true);
    CHECK(allowed_areas_invert(&areas, area));
    CHECK_EQ_INT((int)areas.list[area].tiles, 50 - 8);
    CHECK(!allowed_areas_holds(&areas, area, 1, 1, 1));
    CHECK(allowed_areas_holds(&areas, area, 2, 1, 1));
    CHECK_EQ_INT((int)areas.list[0].tiles, 50);
    CHECK(!allowed_areas_invert(&areas, 5));
    allowed_areas_free(&areas);
}

static void test_removing_an_area_moves_the_others_up(void)
{
    AllowedAreas areas;
    REQUIRE(allowed_areas_init(&areas, 4, 4, 1));
    int a = allowed_areas_add(&areas, "A");
    int b = allowed_areas_add(&areas, "B");
    allowed_areas_set_box(&areas, a, 0, 0, 0, 0, 0, 0, true);
    allowed_areas_set_box(&areas, b, 1, 1, 0, 2, 2, 0, true);
    CHECK(allowed_areas_remove(&areas, a));
    CHECK_EQ_INT(areas.count, 2);
    CHECK_STR_EQ(areas.list[1].name, "B");
    CHECK_EQ_INT((int)areas.list[1].tiles, 4);
    CHECK(allowed_areas_holds(&areas, 1, 2, 2, 0));
    CHECK(!allowed_areas_holds(&areas, 1, 0, 0, 0));
    CHECK(allowed_areas_holds(&areas, 0, 0, 0, 0)); /* The first one as it was. */
    for (int i = 0; i < 16; i++) {
        CHECK_EQ_INT(areas.masks[i] & ~3, 0); /* No bit left for an area gone. */
    }
    /* A new area starts empty, wherever the bits of the old ones were. */
    int c = allowed_areas_add(&areas, "C");
    CHECK_EQ_INT(c, 2);
    CHECK(!allowed_areas_holds(&areas, c, 1, 1, 0));
    CHECK(allowed_areas_remove(&areas, 0));
    CHECK(allowed_areas_remove(&areas, 0));
    CHECK(!allowed_areas_remove(&areas, 0)); /* Never the last one. */
    CHECK_EQ_INT(areas.count, 1);
    CHECK_STR_EQ(areas.list[0].name, "C");
    allowed_areas_free(&areas);
}

static void test_there_are_at_most_sixteen_areas(void)
{
    AllowedAreas areas;
    REQUIRE(allowed_areas_init(&areas, 2, 2, 1));
    for (int i = 1; i < ALLOWED_AREA_MAX; i++) {
        CHECK_EQ_INT(allowed_areas_add(&areas, "More"), i);
    }
    CHECK_EQ_INT(allowed_areas_add(&areas, "Too many"), ALLOWED_AREA_NONE);
    allowed_areas_set_box(&areas, ALLOWED_AREA_MAX - 1, 0, 0, 0, 1, 1, 0, true);
    CHECK(allowed_areas_holds(&areas, ALLOWED_AREA_MAX - 1, 1, 1, 0));
    allowed_areas_free(&areas);
}

static void test_names_are_cut_between_characters(void)
{
    AllowedAreas areas;
    REQUIRE(allowed_areas_init(&areas, 2, 2, 1));
    /* 30 letters, then "é" (two bytes) would end at byte 32: it is left out. */
    char name[64];
    memset(name, 'a', 30);
    strcpy(name + 30, "\xC3\xA9tage");
    CHECK(allowed_areas_rename(&areas, 0, name));
    CHECK_EQ_INT((int)strlen(areas.list[0].name), 30);
    memset(name, 'b', 29);
    strcpy(name + 29, "\xC3\xA9");
    CHECK(allowed_areas_rename(&areas, 0, name));
    CHECK_EQ_INT((int)strlen(areas.list[0].name), 31); /* It fits. */
    CHECK(!allowed_areas_rename(&areas, 1, "None"));
    allowed_areas_free(&areas);
}

static void test_areas_are_saved(void)
{
    AllowedAreas areas;
    REQUIRE(allowed_areas_init(&areas, 6, 5, 3));
    int area = allowed_areas_add(&areas, "Camp");
    allowed_areas_set_box(&areas, area, 1, 1, 0, 3, 2, 2, true);
    allowed_areas_set_box(&areas, 0, 0, 0, 1, 5, 4, 1, false);
    Serializer writer;
    ser_writer_init(&writer);
    allowed_areas_serialize(&writer, &areas);
    size_t size = 0;
    uint8_t *data = ser_release(&writer, &size);
    REQUIRE(data != NULL);

    AllowedAreas back;
    REQUIRE(allowed_areas_init(&back, 6, 5, 3));
    Serializer reader;
    ser_reader_init(&reader, data, size);
    allowed_areas_serialize(&reader, &back);
    CHECK(ser_ok(&reader));
    CHECK_EQ_INT(back.count, 2);
    CHECK_STR_EQ(back.list[area].name, "Camp");
    CHECK_EQ_INT((int)back.list[area].tiles, (int)areas.list[area].tiles);
    CHECK_EQ_INT((int)back.list[0].tiles, (int)areas.list[0].tiles);
    CHECK(memcmp(back.masks, areas.masks, 6 * 5 * 3 * sizeof(uint16_t)) == 0);
    CHECK_EQ_UINT(allowed_areas_hash(&back, 1), allowed_areas_hash(&areas, 1));
    free(data);

    /* A tile holding an area that does not exist is refused. */
    areas.masks[7] |= allowed_area_bit(5);
    ser_writer_init(&writer);
    allowed_areas_serialize(&writer, &areas);
    data = ser_release(&writer, &size);
    REQUIRE(data != NULL);
    ser_reader_init(&reader, data, size);
    allowed_areas_serialize(&reader, &back);
    CHECK(!ser_ok(&reader));
    free(data);
    allowed_areas_free(&back);
    allowed_areas_free(&areas);
}

/* ---- In a game ------------------------------------------------------------- */

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
        map->terrain[i] = TERRAIN_OPEN;
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
    map_topology_changed(map); /* The layers were written directly. */
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

/* Needs kept satisfied (but food and water when `keep_needs`), the hour at noon. */
static void run_ticks_fed(Game *game, int ticks, bool keep_needs)
{
    for (int i = 0; i < ticks; i++) {
        for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
            if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
                for (int need = 0; need < NEED_COUNT; need++) {
                    if (!keep_needs || (need != NEED_FOOD && need != NEED_WATER)) {
                        game->colonists[slot].needs[need] = 1.0f;
                    }
                }
            }
        }
        game->clock.tick = 12 * TICKS_PER_HOUR;
        game_tick(game);
    }
}

static void run_ticks(Game *game, int ticks)
{
    run_ticks_fed(game, ticks, false);
}

/* The default area keeps to the columns x0..x1 (every row, every level). */
static void keep_to_columns(Game *game, int x0, int x1)
{
    const Map *map = &game->map;
    game_set_area_rect(game, 0, 0, 0, map->width - 1, map->height - 1, 0, map->depth - 1, false);
    game_set_area_rect(game, 0, x0, 0, x1, map->height - 1, 0, map->depth - 1, true);
}

static void test_a_game_as_played_starts_in_a_square(void)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 11;
    params.world.width = 96;
    params.world.height = 96;
    params.colonist_count = 3;
    params.start_area = GAME_START_AREA_SIZE;
    Game game;
    REQUIRE(game_init(&game, &params));
    REQUIRE(game.has_arrival);
    const int half = GAME_START_AREA_SIZE / 2;
    const int x = game.arrival.x, y = game.arrival.y, top = game.map.depth - 1;
    CHECK_EQ_INT(game.areas.count, 1);
    CHECK_EQ_INT((int)game.areas.list[0].tiles,
                 GAME_START_AREA_SIZE * GAME_START_AREA_SIZE * game.map.depth);
    CHECK(allowed_areas_holds(&game.areas, 0, x - half, y - half, 0));
    CHECK(allowed_areas_holds(&game.areas, 0, x + half - 1, y + half - 1, top));
    CHECK(!allowed_areas_holds(&game.areas, 0, x + half, y, game.arrival.z));
    CHECK(!allowed_areas_holds(&game.areas, 0, x, y - half - 1, game.arrival.z));
    /* Every settler has the default area, and nothing lies forbidden. */
    for (uint32_t slot = 0; slot < game_colonist_slot_count(&game); slot++) {
        const Colonist *colonist = game_colonist_const(&game, game_colonist_id_at(&game, slot));
        if (colonist != NULL) {
            CHECK_EQ_INT(colonist->area, 0);
        }
    }
    CHECK_EQ_INT(game.items.forbidden_count, 0);
    game_shutdown(&game);
}

static void test_searches_keep_to_the_area(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    keep_to_columns(&game, 0, 19);
    Pathfinder *finder = &game.pathfinder;
    finder->area_masks = game.areas.masks;
    finder->area_bit = allowed_area_bit(0);
    Path path = {0};
    /* A goal out of the area is refused. */
    CHECK(!pathfinder_find(finder, &game.map, (TilePos){10, 10, 0}, (TilePos){30, 10, 0},
                           PATH_NO_COST_LIMIT, &path));
    CHECK(pathfinder_find(finder, &game.map, (TilePos){10, 10, 0}, (TilePos){19, 30, 0},
                          PATH_NO_COST_LIMIT, &path));
    /* The way may cross outside it: from outside, back in. */
    CHECK(pathfinder_find(finder, &game.map, (TilePos){40, 10, 0}, (TilePos){5, 10, 0},
                          PATH_NO_COST_LIMIT, &path));
    finder->area_bit = 0; /* None: anywhere. */
    CHECK(pathfinder_find(finder, &game.map, (TilePos){10, 10, 0}, (TilePos){30, 10, 0},
                          PATH_NO_COST_LIMIT, &path));
    path_free(&path);
    finder->area_masks = NULL;
    game_shutdown(&game);
}

static void test_haulers_leave_what_lies_out_of_their_area(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    keep_to_columns(&game, 0, 19);
    items_add(&game.items, 30, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    items_add(&game.items, 15, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 4));
    REQUIRE(stockpile_create(&game.stockpiles, &game.map, 5, 10, 5, 10, 0) != STOCKPILE_NONE);
    REQUIRE(spawn_worker(&game, 10, 10) != NULL);
    run_ticks(&game, 2000);
    CHECK_EQ_INT(items_get(&game.items, 30, 10, 0).count, 10);
    CHECK_EQ_INT(items_get(&game.items, 15, 10, 0).count, 0);
    CHECK_EQ_INT(items_get(&game.items, 5, 10, 0).count, 4);
    /* The area grown over the berries: they are fetched. */
    CHECK(game_set_area_rect(&game, 0, 20, 0, 40, 47, 0, 0, true) > 0);
    run_ticks(&game, 6000);
    CHECK_EQ_INT(items_get(&game.items, 30, 10, 0).count, 0);
    CHECK_EQ_INT(items_get(&game.items, 5, 10, 0).count, 14);
    game_shutdown(&game);
}

static void test_a_shrunk_area_stops_a_hauler_on_its_way(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 30, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    REQUIRE(stockpile_create(&game.stockpiles, &game.map, 5, 10, 5, 10, 0) != STOCKPILE_NONE);
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    run_ticks(&game, 30);
    REQUIRE(colonist->task == TASK_HAUL);
    keep_to_columns(&game, 0, 19);
    CHECK(colonist->task != TASK_HAUL);
    CHECK_EQ_INT(game.reservations.count, 0);
    run_ticks(&game, 1500);
    CHECK_EQ_INT(items_get(&game.items, 30, 10, 0).count, 10);
    CHECK(colonist->x <= 19);
    game_shutdown(&game);
}

static void test_orders_go_beyond_the_area_and_settlers_come_back(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    keep_to_columns(&game, 0, 19);
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    EntityId id = game_colonist_id_at(&game, 0);
    CHECK_EQ_INT(game_order_move(&game, &id, 1, 35, 10, 0), 1);
    bool arrived = false;
    for (int i = 0; i < 1200 && !arrived; i++) {
        run_ticks(&game, 1);
        arrived = colonist->x == 35 && colonist->y == 10;
    }
    CHECK(arrived);
    /* Its order done, it walks back into its area. */
    run_ticks(&game, 3000);
    CHECK(colonist->x <= 19);
    game_shutdown(&game);
}

static void test_each_settler_keeps_to_its_own_area(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    keep_to_columns(&game, 0, 19);
    int east = game_add_area(&game, "East");
    REQUIRE(east == 1);
    CHECK(game_set_area_rect(&game, east, 20, 0, 47, 47, 0, 0, true) == 28 * 48);
    items_add(&game.items, 30, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    REQUIRE(stockpile_create(&game.stockpiles, &game.map, 35, 10, 35, 10, 0) != STOCKPILE_NONE);
    Colonist *west = spawn_worker(&game, 10, 10);
    REQUIRE(west != NULL);
    run_ticks(&game, 1000);
    CHECK_EQ_INT(items_get(&game.items, 30, 10, 0).count, 10);
    CHECK(game_stockpile_tiles_out_of_areas(&game, 0) == 1);
    CHECK(game_set_colonist_area(&game, game_colonist_id_at(&game, 0), east));
    CHECK_EQ_INT(game_area_settler_count(&game, east), 1);
    CHECK_EQ_INT(game_area_settler_count(&game, 0), 0);
    CHECK(game_stockpile_tiles_out_of_areas(&game, 0) == 0);
    run_ticks(&game, 2000);
    CHECK_EQ_INT(items_get(&game.items, 35, 10, 0).count, 10);
    CHECK(!game_set_colonist_area(&game, game_colonist_id_at(&game, 0), 5));
    game_shutdown(&game);
}

static void test_settlers_of_a_deleted_area_get_the_default_one(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int a = game_add_area(&game, "A");
    int b = game_add_area(&game, "B");
    REQUIRE(a == 1 && b == 2);
    REQUIRE(spawn_worker(&game, 10, 10) != NULL);
    REQUIRE(spawn_worker(&game, 12, 10) != NULL);
    REQUIRE(spawn_worker(&game, 14, 10) != NULL);
    CHECK(game_set_colonist_area(&game, game_colonist_id_at(&game, 0), a));
    CHECK(game_set_colonist_area(&game, game_colonist_id_at(&game, 2), b));
    CHECK(game_remove_area(&game, a));
    CHECK_EQ_INT(game.colonists[0].area, 0);
    CHECK_EQ_INT(game.colonists[1].area, 0);
    CHECK_EQ_INT(game.colonists[2].area, 1); /* "B", one place up. */
    CHECK_STR_EQ(game.areas.list[1].name, "B");
    CHECK(game_remove_area(&game, 0)); /* "B" becomes the default one. */
    CHECK_EQ_INT(game.colonists[0].area, 0);
    CHECK_EQ_INT(game.colonists[2].area, 0);
    CHECK(!game_remove_area(&game, 0));
    game_shutdown(&game);
}

static void test_painting_reaches_the_ground_seen(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    REQUIRE(game.map.depth >= 3);
    int area = game_add_area(&game, "Seen");
    /* Shown level 2 is open air over the ground (level 0): the ground is painted too. */
    CHECK(game_set_area_rect(&game, area, 4, 4, 1, 1, 2, 2, false) == 0);
    CHECK_EQ_INT((int)game_area_drag_size(&game, area, 4, 4, 1, 1, 2, 2, true), 16 * 3);
    CHECK_EQ_INT((int)game.areas.list[area].tiles, 0); /* Told, not done. */
    CHECK(game_set_area_rect(&game, area, 4, 4, 1, 1, 2, 2, true) == 16 * 3);
    /* Over it and beyond: the tiles held already count once. */
    CHECK_EQ_INT((int)game_area_drag_size(&game, area, 3, 3, 6, 4, 2, 2, true), 16 * 3 + 4 * 3);
    CHECK_EQ_INT((int)game_area_drag_size(&game, area, 3, 3, 6, 4, 2, 2, false), 16 * 3 - 4 * 3);
    CHECK_EQ_INT((int)game_area_drag_size(&game, 7, 3, 3, 6, 4, 2, 2, true), 0);
    CHECK(allowed_areas_holds(&game.areas, area, 2, 2, 0));
    CHECK(allowed_areas_holds(&game.areas, area, 2, 2, 2));
    CHECK(!allowed_areas_holds(&game.areas, area, 2, 2, 3));
    CHECK(game_area_holds_seen(&game, area, 2, 2, 2));
    CHECK(!game_area_holds_seen(&game, area, 5, 2, 2));
    CHECK(game_invert_area(&game, area));
    CHECK(!game_area_holds_seen(&game, area, 2, 2, 2));
    CHECK(game_rename_area(&game, area, "Seen again"));
    CHECK_STR_EQ(game.areas.list[area].name, "Seen again");
    game_shutdown(&game);
}

static void test_settlers_with_nothing_to_eat_in_their_area_are_an_alert(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    keep_to_columns(&game, 0, 19);
    items_add(&game.items, 30, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = NEED_LOW_THRESHOLD * 0.5f;
    colonist->needs[NEED_WATER] = NEED_LOW_THRESHOLD * 0.5f; /* No water on the map. */
    run_ticks_fed(&game, 60, true);
    CHECK_EQ_INT(items_get(&game.items, 30, 10, 0).count, 10);
    Alerts alerts;
    alerts_compute(&game, &alerts);
    bool food = false, water = false;
    for (int i = 0; i < alerts.count; i++) {
        food |= alerts.list[i].type == ALERT_FOOD_OUT_OF_REACH;
        water |= alerts.list[i].type == ALERT_WATER_OUT_OF_REACH;
    }
    CHECK(food);
    CHECK(water);
    /* The area grown up to the berries: it eats, and the food alert goes. */
    game_set_area_rect(&game, 0, 20, 0, 40, 47, 0, 0, true);
    run_ticks_fed(&game, 1200, true);
    CHECK(items_get(&game.items, 30, 10, 0).count < 10);
    alerts_compute(&game, &alerts);
    food = false;
    for (int i = 0; i < alerts.count; i++) {
        food |= alerts.list[i].type == ALERT_FOOD_OUT_OF_REACH;
    }
    CHECK(!food);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_a_new_set_holds_the_whole_map);
    RUN_TEST(test_boxes_add_and_take_away_tiles);
    RUN_TEST(test_inverting_swaps_the_tiles);
    RUN_TEST(test_removing_an_area_moves_the_others_up);
    RUN_TEST(test_there_are_at_most_sixteen_areas);
    RUN_TEST(test_names_are_cut_between_characters);
    RUN_TEST(test_areas_are_saved);
    RUN_TEST(test_a_game_as_played_starts_in_a_square);
    RUN_TEST(test_searches_keep_to_the_area);
    RUN_TEST(test_haulers_leave_what_lies_out_of_their_area);
    RUN_TEST(test_a_shrunk_area_stops_a_hauler_on_its_way);
    RUN_TEST(test_orders_go_beyond_the_area_and_settlers_come_back);
    RUN_TEST(test_each_settler_keeps_to_its_own_area);
    RUN_TEST(test_settlers_of_a_deleted_area_get_the_default_one);
    RUN_TEST(test_painting_reaches_the_ground_seen);
    RUN_TEST(test_settlers_with_nothing_to_eat_in_their_area_are_an_alert);
    return test_summary();
}
