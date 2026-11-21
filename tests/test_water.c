#include "core/game.h"
#include "core/map.h"
#include "core/region.h"
#include "core/water.h"
#include "test_framework.h"

/* Steps until every block sleeps (false when still awake after `limit`),
 * checking that the water is kept at each step. */
static bool run_until_asleep(Water *water, Map *map, int limit)
{
    uint64_t total = water_total(water, map);
    for (int step = 0; step < limit; step++) {
        if (!water_step(water, map) || water_total(water, map) != total) {
            return false;
        }
        if (water->active_count == 0) {
            return true;
        }
    }
    return false;
}

/* Rock over the whole of level z but for the tiles (x0..x1, y0..y1). */
static void rock_but(Map *map, int z, int x0, int y0, int x1, int y1)
{
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            bool open = x >= x0 && x <= x1 && y >= y0 && y <= y1;
            map->rock[map_index(map, x, y, z)] = open ? ROCK_NONE : ROCK_GRANITE;
        }
    }
}

static void test_water_at_rest_stays_asleep(void)
{
    Map map;
    REQUIRE(map_init(&map, 32, 32, 2));
    rock_but(&map, 0, 4, 4, 20, 20);
    for (int y = 4; y <= 20; y++) {
        for (int x = 4; x <= 20; x++) {
            map.water[map_index(&map, x, y, 0)] = 700; /* Laid, as world generation does. */
        }
    }
    map_topology_changed(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    for (int step = 0; step < 200; step++) {
        REQUIRE(water_step(&water, &map));
    }
    CHECK_EQ_UINT(water.active_count, 0);
    CHECK_EQ_UINT(water.wakes, 0);
    CHECK_EQ_INT(map_water(&map, 10, 10, 0), 700);
    water_free(&water);
    map_free(&map);
}

static void test_water_spreads_and_is_kept(void)
{
    Map map;
    REQUIRE(map_init(&map, 40, 40, 2));
    Water water;
    REQUIRE(water_init(&water, &map));
    map_set_water(&map, 20, 20, 0, 2000);
    REQUIRE(water_step(&water, &map));
    CHECK(water_block_active(&water, 20, 20, 0));
    CHECK(run_until_asleep(&water, &map, 20000));
    CHECK_EQ_UINT(water_total(&water, &map), 2000u * WATER_FINE);
    CHECK(map_water(&map, 20, 20, 0) < 100);
    CHECK(map_water(&map, 23, 20, 0) > 0);
    CHECK(map_water(&map, 20, 17, 0) > 0);
    /* Spread evenly around where it was poured. */
    CHECK_EQ_INT(map_water(&map, 23, 20, 0), map_water(&map, 17, 20, 0));
    CHECK_EQ_INT(map_water(&map, 20, 23, 0), map_water(&map, 20, 17, 0));
    water_free(&water);
    map_free(&map);
}

static void test_films_do_not_flow(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 1));
    Water water;
    REQUIRE(water_init(&water, &map));
    map_set_water(&map, 8, 8, 0, WATER_FILM_MM - 1);
    CHECK(run_until_asleep(&water, &map, WATER_SLEEP_STEPS + 5));
    CHECK_EQ_INT(map_water(&map, 8, 8, 0), WATER_FILM_MM - 1);
    CHECK_EQ_INT(map_water(&map, 9, 8, 0), 0);
    water_free(&water);
    map_free(&map);
}

static void test_a_channel_fills_to_the_level_of_the_lake(void)
{
    Map map;
    REQUIRE(map_init(&map, 48, 20, 1));
    rock_but(&map, 0, 0, 0, 15, 15); /* The lake. */
    for (int x = 16; x <= 40; x++) { /* The channel, shut from it by rock at x 16. */
        map.rock[map_index(&map, x, 8, 0)] = x == 16 ? ROCK_GRANITE : ROCK_NONE;
    }
    for (int y = 0; y <= 15; y++) {
        for (int x = 0; x <= 15; x++) {
            map.water[map_index(&map, x, y, 0)] = 1000;
        }
    }
    map_topology_changed(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    REQUIRE(water_step(&water, &map));
    CHECK_EQ_UINT(water.active_count, 0);
    map_set_rock(&map, 16, 8, 0, ROCK_NONE); /* Dug through. */
    CHECK(run_until_asleep(&water, &map, 20000));
    /* 256 tiles of 1000 mm over 256 + 25. */
    int level = 1000 * 256 / 281;
    for (int x = 16; x <= 40; x++) {
        CHECK(abs(map_water(&map, x, 8, 0) - level) <= 2 * WATER_FLOW_DIVISOR);
    }
    CHECK(abs(map_water(&map, 3, 3, 0) - level) <= 2 * WATER_FLOW_DIVISOR);
    CHECK_EQ_UINT(water_total(&water, &map), 256000u * WATER_FINE);
    water_free(&water);
    map_free(&map);
}

/* Rock on levels 0 to 2 but for a shaft at (8, 8) over a floor on level 0. */
static void make_shaft(Map *map)
{
    for (int z = 0; z <= 2; z++) {
        rock_but(map, z, 8, 8, 8, 8);
    }
    map->terrain[map_index(map, 8, 8, 0)] = TERRAIN_SOIL;
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            map->terrain[map_index(map, x, y, 3)] = TERRAIN_SOIL; /* Ground above the rock. */
        }
    }
    map->terrain[map_index(map, 8, 8, 3)] = TERRAIN_OPEN; /* The mouth of the shaft. */
    map_topology_changed(map);
}

static void test_a_shaft_fills_from_the_bottom(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 5));
    make_shaft(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    map_set_water(&map, 8, 8, 2, 1500);
    CHECK(run_until_asleep(&water, &map, 1000));
    CHECK_EQ_INT(map_water(&map, 8, 8, 0), 1500);
    CHECK_EQ_INT(map_water(&map, 8, 8, 2), 0);
    map_set_water(&map, 8, 8, 2, 2000);
    CHECK(run_until_asleep(&water, &map, 1000));
    CHECK_EQ_INT(map_water(&map, 8, 8, 0), WATER_CELL_FULL);
    CHECK_EQ_INT(map_water(&map, 8, 8, 1), 1500);
    CHECK_EQ_INT(map_water(&map, 8, 8, 2), 0);
    CHECK_EQ_INT(map_water_class(&map, 8, 8, 0), WATER_DEEP);
    water_free(&water);
    map_free(&map);
}

static void test_water_pours_down_a_shaft(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 5));
    make_shaft(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    map_set_water(&map, 5, 8, 3, 2000); /* On the ground, three tiles from the mouth. */
    map_set_water(&map, 6, 8, 3, 2000);
    CHECK(run_until_asleep(&water, &map, 20000));
    int fallen = map_water(&map, 8, 8, 0) + map_water(&map, 8, 8, 1);
    CHECK(fallen > 2000);
    CHECK_EQ_INT(map_water(&map, 8, 8, 3), 0);
    CHECK_EQ_UINT(water_total(&water, &map), 4000u * WATER_FINE);
    water_free(&water);
    map_free(&map);
}

/* Water on the west side of a wall at x 16 crosses nowhere. */
static int water_east_of(const Map *map, int x0)
{
    int total = 0;
    for (int y = 0; y < map->height; y++) {
        for (int x = x0; x < map->width; x++) {
            total += map_water(map, x, y, 0);
        }
    }
    return total;
}

static void test_walls_and_shut_doors_hold_water(void)
{
    Map map;
    REQUIRE(map_init(&map, 32, 16, 1));
    for (int y = 0; y < 16; y++) {
        map_set_structure(&map, 16, y, 0, y == 8 ? STRUCTURE_DOOR : STRUCTURE_WALL);
    }
    Water water;
    REQUIRE(water_init(&water, &map));
    map_set_water(&map, 13, 8, 0, 2000);
    map_set_water(&map, 14, 8, 0, 2000);
    CHECK(run_until_asleep(&water, &map, 20000));
    CHECK_EQ_INT(water_east_of(&map, 16), 0);
    CHECK(map_water(&map, 15, 8, 0) > 0);
    REQUIRE(map_set_door_kept_open(&map, 16, 8, 0, true));
    CHECK(run_until_asleep(&water, &map, 20000));
    CHECK(water_east_of(&map, 17) > 0);
    CHECK_EQ_UINT(water_total(&water, &map), 4000u * WATER_FINE);
    water_free(&water);
    map_free(&map);
}

static void test_blocks_wake_as_water_reaches_them(void)
{
    Map map;
    REQUIRE(map_init(&map, 64, 16, 1));
    Water water;
    REQUIRE(water_init(&water, &map));
    map_set_water(&map, 2, 8, 0, 2000);
    map_set_water(&map, 3, 8, 0, 2000);
    REQUIRE(water_step(&water, &map));
    CHECK(water_block_active(&water, 2, 8, 0));
    CHECK(!water_block_active(&water, 40, 8, 0));
    bool reached = false;
    for (int step = 0; step < 5000 && !reached; step++) {
        REQUIRE(water_step(&water, &map));
        reached = water_block_active(&water, 20, 8, 0);
    }
    CHECK(reached);
    CHECK(run_until_asleep(&water, &map, 20000));
    CHECK_EQ_UINT(water_total(&water, &map), 4000u * WATER_FINE);
    water_free(&water);
    map_free(&map);
}

static void test_the_map_learns_where_water_got_deep(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 2));
    rock_but(&map, 0, 4, 4, 5, 4); /* Two tiles. */
    map_topology_changed(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    map_set_water(&map, 4, 4, 0, 2000);
    map_set_water(&map, 5, 4, 0, 600); /* Wading: one walks there. */
    CHECK(map_is_passable(&map, 5, 4, 0));
    uint32_t revision = map.topology_revision;
    CHECK(run_until_asleep(&water, &map, 1000));
    CHECK_EQ_INT(map_water(&map, 5, 4, 0), 1300);
    CHECK(!map_is_passable(&map, 5, 4, 0));
    CHECK(map.topology_revision != revision);
    /* Its own reports do not wake it again. */
    for (int step = 0; step < 10; step++) {
        REQUIRE(water_step(&water, &map));
    }
    CHECK_EQ_UINT(water.active_count, 0);
    water_free(&water);
    map_free(&map);
}

static void test_the_map_learns_where_open_water_spread(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 2));
    rock_but(&map, 0, 4, 4, 5, 4);
    map_topology_changed(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    map_set_water(&map, 4, 4, 0, 1000);
    CHECK(!map_is_water(&map, 5, 4, 0));
    uint32_t water_revision = map.water_revision;
    CHECK(run_until_asleep(&water, &map, 1000));
    CHECK_EQ_INT(map_water(&map, 5, 4, 0), 500);
    CHECK(map_is_water(&map, 5, 4, 0));
    CHECK(map.water_revision != water_revision);
    water_free(&water);
    map_free(&map);
}

static void test_water_is_deterministic(void)
{
    Map maps[2];
    Water waters[2];
    for (int i = 0; i < 2; i++) {
        REQUIRE(map_init(&maps[i], 40, 40, 3));
        rock_but(&maps[i], 0, 2, 2, 37, 37);
        map_set_structure(&maps[i], 20, 20, 1, STRUCTURE_WALL);
        map_topology_changed(&maps[i]);
        REQUIRE(water_init(&waters[i], &maps[i]));
        map_set_water(&maps[i], 10, 30, 0, 2000);
        map_set_water(&maps[i], 30, 5, 0, 1700);
        map_set_water(&maps[i], 20, 21, 1, 2000);
    }
    for (int step = 0; step < 100; step++) {
        REQUIRE(water_step(&waters[0], &maps[0]));
        REQUIRE(water_step(&waters[1], &maps[1]));
    }
    CHECK_EQ_UINT(map_hash(&maps[0]), map_hash(&maps[1]));
    CHECK_EQ_UINT(water_hash(&waters[0], 1), water_hash(&waters[1], 1));
    CHECK(waters[0].active_count > 0); /* Still flowing: hashed. */
    for (int i = 0; i < 2; i++) {
        water_free(&waters[i]);
        map_free(&maps[i]);
    }
}

/* In a game: water poured on the plain spreads tick by tick, the regions
 * learn where it got too deep to walk, and nothing else moves the water. */
static void test_water_flows_in_a_game(void)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 5;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    params.wildlife = 0.0f;
    Game game;
    REQUIRE(game_init(&game, &params));
    Map *map = &game.map;
    for (size_t i = 0; i < map_level_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->water[i] = 0;
        map->rock[i] = ROCK_NONE;
    }
    rock_but(map, 0, 20, 20, 23, 21); /* A pit of 4 × 2 tiles in the rock. */
    map_topology_changed(map);
    game_tick(&game);
    CHECK_EQ_UINT(game.water.active_count, 0); /* No water anywhere. */
    CHECK_EQ_INT(game_debug_pour_water(&game, 20, 20, 0, 2000), 2000);
    CHECK_EQ_INT(game_debug_pour_water(&game, 20, 21, 0, 2000), 2000);
    CHECK_EQ_INT(game_debug_pour_water(&game, 21, 20, 0, 1200), 1200);
    uint64_t total = water_total(&game.water, map);
    game_tick(&game);
    CHECK(game.water.active_count > 0);
    for (int tick = 0; tick < 600; tick++) {
        game_tick(&game);
    }
    CHECK_EQ_UINT(water_total(&game.water, map), total);
    CHECK_EQ_UINT(game.water.active_count, 0);
    /* 5200 mm over 8 tiles: 650 each, wading everywhere. */
    CHECK(abs(map_water(map, 23, 21, 0) - 650) <= 1);
    CHECK(abs(map_water(map, 20, 20, 0) - 650) <= 1);
    CHECK(map_is_passable(map, 20, 20, 0));
    CHECK(regions_at(&game.regions, map, 20, 20, 0) != REGION_NONE);
    CHECK_EQ_INT(regions_at(&game.regions, map, 20, 20, 0),
                 regions_at(&game.regions, map, 23, 21, 0));
    /* 9750 mm over 8 tiles, 1218 each: too deep, out of every region (poured
     * well below the brim, so that no wave splashes onto the level above). */
    for (int i = 0; i < 4; i++) {
        CHECK_EQ_INT(game_debug_pour_water(&game, 20 + i, 20, 0, 800), 800);
        CHECK_EQ_INT(game_debug_pour_water(&game, 20 + i, 21, 0, i < 2 ? 338 : 337),
                     i < 2 ? 338 : 337);
    }
    total = water_total(&game.water, map);
    for (int tick = 0; tick < 600; tick++) {
        game_tick(&game);
    }
    CHECK_EQ_UINT(water_total(&game.water, map), total);
    CHECK(abs(map_water(map, 22, 21, 0) - 1218) <= 1);
    CHECK(!map_is_passable(map, 22, 21, 0));
    CHECK_EQ_INT(regions_at(&game.regions, map, 22, 21, 0), REGION_NONE);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_water_at_rest_stays_asleep);
    RUN_TEST(test_water_spreads_and_is_kept);
    RUN_TEST(test_films_do_not_flow);
    RUN_TEST(test_a_channel_fills_to_the_level_of_the_lake);
    RUN_TEST(test_a_shaft_fills_from_the_bottom);
    RUN_TEST(test_water_pours_down_a_shaft);
    RUN_TEST(test_walls_and_shut_doors_hold_water);
    RUN_TEST(test_blocks_wake_as_water_reaches_them);
    RUN_TEST(test_the_map_learns_where_water_got_deep);
    RUN_TEST(test_the_map_learns_where_open_water_spread);
    RUN_TEST(test_water_is_deterministic);
    RUN_TEST(test_water_flows_in_a_game);
    return test_summary();
}
