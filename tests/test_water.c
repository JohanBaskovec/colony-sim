#include "core/game.h"
#include "core/map.h"
#include "core/region.h"
#include "core/river.h"
#include "core/water.h"
#include "test_framework.h"

#include <stdint.h>

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

/* Rock along the rim of the map on level z: no water crosses its edges
 * there (the land beyond is as the rim, docs/WATER.md §4.1). */
static void rock_rim(Map *map, int z)
{
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            if (x == 0 || y == 0 || x == map->width - 1 || y == map->height - 1) {
                map->rock[map_index(map, x, y, z)] = ROCK_GRANITE;
            }
        }
    }
}

/* Steps until every block sleeps (false when still awake after `limit`),
 * checking at each step that the water changes only by what crossed the
 * edges. */
static bool run_with_edges(Water *water, Map *map, int limit)
{
    int64_t start = (int64_t)water_total(water, map) - water->edge_in;
    for (int step = 0; step < limit; step++) {
        if (!water_step(water, map) || (int64_t)water_total(water, map) != start + water->edge_in) {
            return false;
        }
        if (water->active_count == 0) {
            return true;
        }
    }
    return false;
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

/* What the ground and the air give or take (water_exchange()) wakes only
 * water it sets moving: a pond laid on the level of its banks stays as it
 * lies when the sun takes from its edge; rain on stone wakes the block once
 * it stands a film deep. */
static void test_exchanges_wake_only_water_they_set_moving(void)
{
    Map map;
    REQUIRE(map_init(&map, 32, 32, 2));
    for (int y = 4; y <= 12; y++) {
        for (int x = 4; x <= 12; x++) {
            map.water[map_index(&map, x, y, 0)] = 400; /* A pond on a flat world. */
        }
    }
    map_topology_changed(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    size_t edge = map_index(&map, 4, 8, 0);
    CHECK_EQ_INT(water_exchange(&water, &map, edge, -10 * WATER_FINE), -10 * WATER_FINE);
    REQUIRE(water_step(&water, &map));
    CHECK_EQ_UINT(water.wakes, 0);
    CHECK_EQ_INT(map_water(&map, 4, 8, 0), 390);
    CHECK_EQ_INT(map_water(&map, 3, 8, 0), 0);

    size_t stone = map_index(&map, 24, 24, 0);
    water_exchange(&water, &map, stone, 2 * WATER_FINE);
    REQUIRE(water_step(&water, &map));
    CHECK_EQ_UINT(water.wakes, 0); /* A film: it stays. */
    water_exchange(&water, &map, stone, 2 * WATER_FINE);
    REQUIRE(water_step(&water, &map));
    CHECK_EQ_UINT(water.wakes, 1);
    CHECK(water_block_active(&water, 24, 24, 0));
    CHECK(!water_block_active(&water, 4, 8, 0));
    water_free(&water);
    map_free(&map);
}

static void test_a_channel_fills_to_the_level_of_the_lake(void)
{
    Map map;
    REQUIRE(map_init(&map, 48, 20, 1));
    rock_but(&map, 0, 1, 1, 15, 15); /* The lake, off the edges. */
    for (int x = 16; x <= 40; x++) { /* The channel, shut from it by rock at x 16. */
        map.rock[map_index(&map, x, 8, 0)] = x == 16 ? ROCK_GRANITE : ROCK_NONE;
    }
    for (int y = 1; y <= 15; y++) {
        for (int x = 1; x <= 15; x++) {
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
    /* 225 tiles of 1000 mm over 225 + 25. */
    int level = 1000 * 225 / 250;
    for (int x = 16; x <= 40; x++) {
        CHECK(abs(map_water(&map, x, 8, 0) - level) <= 2 * WATER_FLOW_DIVISOR);
    }
    CHECK(abs(map_water(&map, 3, 3, 0) - level) <= 2 * WATER_FLOW_DIVISOR);
    CHECK_EQ_UINT(water_total(&water, &map), 225000u * WATER_FINE);
    water_free(&water);
    map_free(&map);
}

/* A lake cut by the west edge of the map: what is drawn from it comes back
 * from the lake beyond, up to its level. */
static void test_a_lake_cut_by_the_edge_keeps_its_level(void)
{
    Map map;
    REQUIRE(map_init(&map, 32, 16, 1));
    rock_but(&map, 0, 0, 4, 9, 11);
    for (int y = 4; y <= 11; y++) {
        for (int x = 0; x <= 9; x++) {
            map.water[map_index(&map, x, y, 0)] = 600;
        }
    }
    map_topology_changed(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    CHECK_EQ_INT(water_beyond_edge(&water, 0, 6, 0, -1, 0), 600 * WATER_FINE);
    CHECK_EQ_INT(water_beyond_edge(&water, 0, 2, 0, -1, 0), -1); /* Rock there. */
    CHECK_EQ_INT(water_beyond_edge(&water, 5, 6, 0, -1, 0), -1); /* Not on the edge. */
    for (int step = 0; step < 100; step++) {                     /* At rest with the lake beyond. */
        REQUIRE(water_step(&water, &map));
    }
    CHECK_EQ_UINT(water.active_count, 0);
    for (int y = 4; y <= 11; y++) { /* Drawn from: a channel dug east of it. */
        map_set_water(&map, 8, y, 0, 0);
        map_set_water(&map, 9, y, 0, 0);
    }
    CHECK(run_with_edges(&water, &map, 20000));
    CHECK(water.edge_in > 0);
    CHECK(abs(map_water(&map, 9, 7, 0) - 600) <= 2 * WATER_FLOW_DIVISOR);
    CHECK(abs(map_water(&map, 0, 7, 0) - 600) <= 2 * WATER_FLOW_DIVISOR);
    water_free(&water);
    map_free(&map);
}

/* A flood reaching a dry edge flows out of the map, into the basins beyond
 * (four of WATER_EDGE_BASIN tiles), all but a film over what they rose. */
static void test_a_flood_flows_out_at_a_dry_edge(void)
{
    Map map;
    REQUIRE(map_init(&map, 32, 16, 1));
    rock_but(&map, 0, 0, 6, 12, 9); /* A gully four tiles wide, open to the west. */
    map_topology_changed(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    CHECK_EQ_INT(water_beyond_edge(&water, 0, 7, 0, -1, 0), 0);
    map_set_water(&map, 10, 7, 0, 2000);
    map_set_water(&map, 10, 8, 0, 2000);
    CHECK(run_with_edges(&water, &map, 20000));
    CHECK(water.edge_in < -3000 * WATER_FINE);
    CHECK((int64_t)water_total(&water, &map) == 4000 * WATER_FINE + water.edge_in);
    /* The basins rose by what came out, spread over them. */
    int risen = (int)(-water.edge_in / (4 * WATER_EDGE_BASIN));
    for (int y = 6; y <= 9; y++) {
        int32_t beyond = water_beyond_edge(&water, 0, y, 0, -1, 0);
        CHECK(beyond > 0 && beyond < 2 * risen);
    }
    for (int x = 0; x <= 12; x++) {
        CHECK(map_water(&map, x, 7, 0) * WATER_FINE <= 2 * risen + WATER_FILM_MM * WATER_FINE);
    }
    water_free(&water);
    map_free(&map);
}

/* Two basins beyond a lake a little apart (the sun took more from one edge
 * tile than from the next): the current they start through the lake dies
 * out, the lake falls asleep. */
static void test_basins_apart_even_out(void)
{
    Map map;
    REQUIRE(map_init(&map, 32, 16, 1));
    rock_but(&map, 0, 0, 4, 9, 11);
    for (int y = 4; y <= 11; y++) {
        for (int x = 0; x <= 9; x++) {
            map.water[map_index(&map, x, y, 0)] = 600;
        }
    }
    map_topology_changed(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    size_t west = 2 * (size_t)map.width; /* The basins beyond the west column. */
    water.edge_surface[west + 4] += (int64_t)8 * WATER_FINE * WATER_EDGE_BASIN;
    water.edge_surface[west + 11] -= (int64_t)8 * WATER_FINE * WATER_EDGE_BASIN;
    map_set_water(&map, 5, 7, 0, 0); /* Wakes it. */
    CHECK(run_with_edges(&water, &map, 20000));
    int32_t high = water_beyond_edge(&water, 0, 4, 0, -1, 0);
    int32_t low = water_beyond_edge(&water, 0, 11, 0, -1, 0);
    CHECK(high - low < 3 * WATER_FILM_MM * WATER_FINE);
    water_free(&water);
    map_free(&map);
}

/* The land beyond lives the edge's weather: what the sun takes from an edge
 * tile it takes beyond, and nothing flows nor wakes. */
static void test_the_land_beyond_shares_the_weather(void)
{
    Map map;
    REQUIRE(map_init(&map, 32, 16, 1));
    rock_but(&map, 0, 0, 4, 9, 11);
    for (int y = 4; y <= 11; y++) {
        for (int x = 0; x <= 9; x++) {
            map.water[map_index(&map, x, y, 0)] = 600;
        }
    }
    map_topology_changed(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    for (int y = 4; y <= 11; y++) {
        for (int x = 0; x <= 9; x++) {
            CHECK_EQ_INT(water_exchange(&water, &map, map_index(&map, x, y, 0), -2 * WATER_FINE),
                         -2 * WATER_FINE);
        }
    }
    CHECK_EQ_INT(water_beyond_edge(&water, 0, 7, 0, -1, 0), 598 * WATER_FINE);
    REQUIRE(water_step(&water, &map));
    CHECK_EQ_UINT(water.active_count, 0);
    CHECK_EQ_INT(water.edge_in, 0);
    water_free(&water);
    map_free(&map);
}

/* Under the ground of the land beyond, the edge is solid: a cave cut by the
 * edge keeps its water. Sealed edges let nothing through anywhere. */
static void test_no_water_crosses_under_the_ground_or_sealed_edges(void)
{
    Map map;
    REQUIRE(map_init(&map, 32, 16, 3));
    rock_but(&map, 0, 0, 6, 8, 9); /* The cave, on the west edge. */
    rock_but(&map, 1, 1, 1, 0, 0); /* Its roof: rock all over level 1. */
    map_topology_changed(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    CHECK_EQ_INT(water_beyond_edge(&water, 0, 7, 0, -1, 0), -1);
    CHECK_EQ_INT(water_beyond_edge(&water, 0, 7, 2, -1, 0), 0); /* Open sky above the roof. */
    map_set_water(&map, 0, 7, 0, 2000);
    map_set_water(&map, 0, 8, 0, 2000);
    CHECK(run_with_edges(&water, &map, 20000));
    CHECK_EQ_INT(water.edge_in, 0);
    CHECK_EQ_UINT(water_total(&water, &map), 4000u * WATER_FINE);
    water_seal_edges(&water);
    map_set_water(&map, 0, 7, 2, 2000); /* On the roof, at a dry edge. */
    CHECK_EQ_INT(water_beyond_edge(&water, 0, 7, 2, -1, 0), -1);
    CHECK(run_with_edges(&water, &map, 20000));
    CHECK_EQ_INT(water.edge_in, 0);
    water_settle(&water, &map); /* Still sealed after. */
    CHECK_EQ_INT(water_beyond_edge(&water, 0, 7, 2, -1, 0), -1);
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
    rock_rim(map, 3);
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
    rock_rim(&map, 0);
    map_topology_changed(&map);
    for (int y = 1; y < 15; y++) {
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
    rock_rim(&map, 0);
    map_topology_changed(&map);
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

/* The water of the pit of test_water_flows_in_a_game(), mm, and whether its
 * tiles are level (within 2 mm of their mean). */
static int pit_water(const Map *map, bool *level)
{
    int total = 0;
    for (int y = 20; y <= 21; y++) {
        for (int x = 20; x <= 23; x++) {
            total += map_water(map, x, y, 0);
        }
    }
    *level = true;
    for (int y = 20; y <= 21; y++) {
        for (int x = 20; x <= 23; x++) {
            *level &= abs(map_water(map, x, y, 0) * 8 - total) <= 16;
        }
    }
    return total;
}

/* In a game: water poured into a pit spreads tick by tick and comes level,
 * and the regions learn where it got too deep to walk. The pit's floor is a
 * bed of silt, which takes no water in (the sun may take a little). */
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
        map->terrain[i] = TERRAIN_SILT;
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
    game_tick(&game);
    CHECK(game.water.active_count > 0);
    for (int tick = 0; tick < 600; tick++) {
        game_tick(&game);
    }
    /* About 5200 mm over 8 tiles: 650 each, wading everywhere. */
    bool level = false;
    int total = pit_water(map, &level);
    CHECK(level);
    CHECK(total > 5100 && total <= 5200);
    CHECK(map_is_passable(map, 20, 20, 0));
    CHECK(regions_at(&game.regions, map, 20, 20, 0) != REGION_NONE);
    CHECK_EQ_INT(regions_at(&game.regions, map, 20, 20, 0),
                 regions_at(&game.regions, map, 23, 21, 0));
    /* About 9750 mm, 1218 each: too deep, out of every region (poured well
     * below the brim, so that no wave splashes onto the level above). */
    for (int i = 0; i < 4; i++) {
        CHECK_EQ_INT(game_debug_pour_water(&game, 20 + i, 20, 0, 800), 800);
        CHECK_EQ_INT(game_debug_pour_water(&game, 20 + i, 21, 0, i < 2 ? 338 : 337),
                     i < 2 ? 338 : 337);
    }
    for (int tick = 0; tick < 600; tick++) {
        game_tick(&game);
    }
    total = pit_water(map, &level);
    CHECK(level);
    CHECK(total > 9650 && total <= 9750);
    CHECK(!map_is_passable(map, 22, 21, 0));
    CHECK_EQ_INT(regions_at(&game.regions, map, 22, 21, 0), REGION_NONE);
    game_shutdown(&game);
}

/* The surface of the water at (x, y), in mm above the bottom of the map
 * (the highest tile holding water); -1 when dry. */
static int surface_mm(const Map *map, int x, int y)
{
    for (int z = map->depth - 1; z >= 0; z--) {
        int water = map_water(map, x, y, z);
        if (water > 0) {
            return z * WATER_CELL_FULL + water;
        }
    }
    return -1;
}

/* A channel of `rows` rows across a map of `width` × 16 (rows from y = 5)
 * holding 400 mm, its west end fed by a river's source `head` mm higher,
 * its east end an outlet held at 400 mm. */
static void make_channel(Map *map, Water *water, int width, int rows, int head)
{
    REQUIRE(map_init(map, width, 16, 1));
    rock_but(map, 0, 0, 5, width - 1, 5 + rows - 1);
    for (int y = 5; y < 5 + rows; y++) {
        for (int x = 0; x < width; x++) {
            map->water[map_index(map, x, y, 0)] = 400;
        }
    }
    map_topology_changed(map);
    REQUIRE(water_init(water, map));
    for (int y = 5; y < 5 + rows; y++) {
        water_set_edge(water, map, water_edge_slot(water, 0, y, -1, 0), WATER_EDGE_SOURCE,
                       (int64_t)(400 + head) * WATER_FINE);
        water_set_edge(water, map, water_edge_slot(water, width - 1, y, 1, 0), WATER_EDGE_OUTLET,
                       (int64_t)400 * WATER_FINE);
    }
}

/* A current fed at one end and let out at the other: it takes its slope,
 * then flows on steadily, and a steady current falls asleep all along. */
static void test_a_steady_current_falls_asleep(void)
{
    Map map;
    Water water;
    make_channel(&map, &water, 120, 6, 100);
    CHECK(water_edge_slot(&water, 5, 7, -1, 0) == SIZE_MAX); /* Not on that edge. */
    CHECK(run_with_edges(&water, &map, 20000));
    CHECK(water.edge_in > 0); /* What its slope holds. */
    int source = map_water(&map, 0, 7, 0), middle = map_water(&map, 60, 7, 0);
    int outlet = map_water(&map, 119, 7, 0);
    CHECK(source > 490 && source <= 500);
    CHECK(middle > 440 && middle < 460);
    CHECK(outlet >= 400 && outlet < 415);
    for (int step = 0; step < 300; step++) {
        REQUIRE(water_step(&water, &map));
    }
    CHECK_EQ_UINT(water.active_count, 0);
    water_free(&water);
    map_free(&map);
}

/* A lake let out through a long, narrow channel drains slowly: each of its
 * tiles changes by little and its current hardly changes, but it loses
 * water, and it stays awake until it is down to the outlet's level. */
static void test_a_lake_drains_through_a_slow_current(void)
{
    Map map;
    REQUIRE(map_init(&map, 64, 16, 1));
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 64; x++) {
            bool lake = x >= 2 && x <= 25 && y >= 2 && y <= 13;
            bool channel = x > 25 && y == 7;
            map.rock[map_index(&map, x, y, 0)] = lake || channel ? ROCK_NONE : ROCK_GRANITE;
            map.water[map_index(&map, x, y, 0)] = lake || channel ? 600 : 0;
        }
    }
    map_topology_changed(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    water_set_edge(&water, &map, water_edge_slot(&water, 63, 7, 1, 0), WATER_EDGE_OUTLET,
                   (int64_t)400 * WATER_FINE);
    CHECK(run_with_edges(&water, &map, 60000));
    CHECK(water.edge_in < -150 * 288 * WATER_FINE);
    CHECK(map_water(&map, 10, 7, 0) < 410);
    CHECK(map_water(&map, 10, 7, 0) >= 400);
    water_free(&water);
    map_free(&map);
}

/* A river as world generation lays it flows from the edge it comes in by,
 * where its level is held higher, to its outlet, then falls asleep once
 * steady. */
static void test_a_river_flows_then_sleeps(void)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 11;
    params.world.width = 96;
    params.world.height = 96;
    params.world.water_level = 0; /* No lakes to fill: steady sooner. */
    params.world.river.width = RIVER_MAX_WIDTH;
    params.colonist_count = 0;
    Game game;
    REQUIRE(game_init(&game, &params));
    int tick = 0;
    for (; tick < TICKS_PER_DAY && (tick < 60 || game.water.active_count > 0); tick++) {
        game.weather.type = WEATHER_CLEAR;
        game_tick(&game);
    }
    CHECK_EQ_UINT(game.water.active_count, 0);
    CHECK(game.water.edge_in > 0); /* Its slope. */
    /* Higher where it comes in than where it goes out. */
    const Map *map = &game.map;
    int high = -1, low = INT32_MAX;
    for (size_t slot = 0; slot < 2 * (size_t)map->width + 2 * (size_t)map->height; slot++) {
        if (game.water.edge_kind[slot] == WATER_EDGE_LAND) {
            continue;
        }
        int x, y;
        size_t w = (size_t)map->width, h = (size_t)map->height;
        if (slot < w) {
            x = (int)slot, y = 0;
        } else if (slot < 2 * w) {
            x = (int)(slot - w), y = map->height - 1;
        } else if (slot < 2 * w + h) {
            x = 0, y = (int)(slot - 2 * w);
        } else {
            x = map->width - 1, y = (int)(slot - 2 * w - h);
        }
        int surface = surface_mm(map, x, y);
        if (game.water.edge_kind[slot] == WATER_EDGE_SOURCE) {
            high = surface > high ? surface : high;
        } else {
            low = surface < low ? surface : low;
        }
    }
    CHECK(high - low > RIVER_HEAD_MM / 3);
    CHECK(high - low <= RIVER_HEAD_MM);
    /* It stays asleep. */
    for (int i = 0; i < 300; i++) {
        game.weather.type = WEATHER_CLEAR;
        game_tick(&game);
    }
    CHECK_EQ_UINT(game.water.active_count, 0);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_water_at_rest_stays_asleep);
    RUN_TEST(test_water_spreads_and_is_kept);
    RUN_TEST(test_films_do_not_flow);
    RUN_TEST(test_exchanges_wake_only_water_they_set_moving);
    RUN_TEST(test_a_channel_fills_to_the_level_of_the_lake);
    RUN_TEST(test_a_lake_cut_by_the_edge_keeps_its_level);
    RUN_TEST(test_a_flood_flows_out_at_a_dry_edge);
    RUN_TEST(test_basins_apart_even_out);
    RUN_TEST(test_the_land_beyond_shares_the_weather);
    RUN_TEST(test_no_water_crosses_under_the_ground_or_sealed_edges);
    RUN_TEST(test_a_shaft_fills_from_the_bottom);
    RUN_TEST(test_water_pours_down_a_shaft);
    RUN_TEST(test_walls_and_shut_doors_hold_water);
    RUN_TEST(test_blocks_wake_as_water_reaches_them);
    RUN_TEST(test_the_map_learns_where_water_got_deep);
    RUN_TEST(test_the_map_learns_where_open_water_spread);
    RUN_TEST(test_water_is_deterministic);
    RUN_TEST(test_water_flows_in_a_game);
    RUN_TEST(test_a_steady_current_falls_asleep);
    RUN_TEST(test_a_lake_drains_through_a_slow_current);
    RUN_TEST(test_a_river_flows_then_sleeps);
    return test_summary();
}
