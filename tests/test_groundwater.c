#include "core/groundwater.h"
#include "core/map.h"
#include "core/water.h"
#include "test_framework.h"

#include <stdint.h>
#include <stdlib.h>

/* A world of `rock` up to level `ground` - 1, its floor on level `ground`
 * (soil), open air above; the pores at their field capacity. */
static bool ground_world(Map *map, int size, int depth, int ground, RockType rock)
{
    if (!map_init(map, size, size, depth)) {
        return false;
    }
    for (int z = 0; z < depth; z++) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                size_t index = map_index(map, x, y, z);
                map->rock[index] = (uint8_t)(z < ground ? rock : ROCK_NONE);
                map->terrain[index] = (uint8_t)(z < ground    ? rock_def(rock)->floor
                                                : z == ground ? TERRAIN_SOIL
                                                              : TERRAIN_OPEN);
                map->pore[index] = (uint16_t)rock_pore_capacity((RockType)map->rock[index]);
            }
        }
    }
    map_topology_changed(map);
    return true;
}

/* Every column's water table at `head`. */
static void lay_everywhere(Map *map, int64_t head)
{
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            groundwater_lay_table(map, x, y, head);
        }
    }
}

/* All the water of the world, in 1/WATER_FINE mm: free and in the pores. */
static int64_t all_water(const Water *water, const Map *map)
{
    int64_t total = (int64_t)water_total(water, map);
    for (size_t i = 0; i < map_tile_count(map); i++) {
        total += (int64_t)map->pore[i] * (WATER_FINE / MAP_PORE_FINE);
    }
    return total;
}

/* What crossed the edges so far, in 1/WATER_FINE mm. */
static int64_t edges_in(const Water *water, const GroundWater *ground)
{
    return water->edge_in + ground->edge_in * (WATER_FINE / MAP_PORE_FINE);
}

/* `hours` hours of the ground, the free water flowing `steps` steps after
 * each; false when water came or went other than through the edges. */
static bool run_hours(GroundWater *ground, Map *map, Water *water, int hours, int steps)
{
    int64_t start = all_water(water, map) - edges_in(water, ground);
    for (int hour = 0; hour < hours; hour++) {
        groundwater_update(ground, map, water, NULL, 0, map_tile_count(map));
        groundwater_end_hour(ground, map, water);
        for (int step = 0; step < steps; step++) {
            if (!water_step(water, map)) {
                return false;
            }
        }
        if (all_water(water, map) - edges_in(water, ground) != start) {
            return false;
        }
    }
    return true;
}

/* Opens a gallery tile on level z: no rock, a floor of what it was dug in. */
static void dig(Map *map, int x, int y, int z)
{
    map_set_rock(map, x, y, z, ROCK_NONE);
}

/* The rooms and capacities follow the table of docs/WATER.md §5.1. */
static void test_rock_rooms(void)
{
    CHECK_EQ_INT((int)rock_pore_room(ROCK_EARTH), 900 * MAP_PORE_FINE);
    CHECK_EQ_INT((int)rock_pore_capacity(ROCK_EARTH), 600 * MAP_PORE_FINE);
    CHECK_EQ_INT((int)rock_pore_room(ROCK_GRANITE), 20 * MAP_PORE_FINE);
    CHECK_EQ_INT((int)rock_pore_room(ROCK_GRAVEL), 600 * MAP_PORE_FINE);
    CHECK_EQ_INT((int)rock_pore_room(ROCK_WOOD), 0);
    CHECK_EQ_INT((int)rock_pore_room(ROCK_NONE), 0);
    /* Every room fits a tile's layer. */
    for (int r = 0; r < ROCK_COUNT; r++) {
        CHECK(rock_pore_room((RockType)r) <= UINT16_MAX);
        CHECK(rock_pore_capacity((RockType)r) <= rock_pore_room((RockType)r));
    }
}

/* A table laid in a column: full under it, in proportion where it crosses a
 * tile, at field capacity over it; the heads read it back. */
static void test_a_table_laid_and_its_heads(void)
{
    Map map;
    REQUIRE(ground_world(&map, 16, 6, 4, ROCK_EARTH));
    int64_t table = GROUNDWATER_LEVEL + GROUNDWATER_LEVEL / 4; /* A quarter into level 1. */
    groundwater_lay_table(&map, 3, 3, table);
    uint32_t room = rock_pore_room(ROCK_EARTH), capacity = rock_pore_capacity(ROCK_EARTH);
    CHECK_EQ_INT(map.pore[map_index(&map, 3, 3, 0)], (int)room);
    CHECK_EQ_INT(map.pore[map_index(&map, 3, 3, 1)], (int)(capacity + (room - capacity) / 4));
    CHECK_EQ_INT(map.pore[map_index(&map, 3, 3, 2)], (int)capacity);
    CHECK_EQ_INT(map.pore[map_index(&map, 3, 3, 3)], (int)capacity);
    CHECK(groundwater_full_at(&map, map_index(&map, 3, 3, 0)));
    /* The full tile stands as high as the one over it. */
    CHECK(llabs(groundwater_head(&map, 3, 3, 0) - table) <= 1);
    CHECK(llabs(groundwater_head(&map, 3, 3, 1) - table) <= 1);
    CHECK(llabs(groundwater_column_head(&map, 3, 3) - table) <= 1);
    /* Over the table: the bottom of the tile. */
    CHECK_EQ_INT((int)groundwater_head(&map, 3, 3, 2), (int)(2 * GROUNDWATER_LEVEL));
    CHECK(groundwater_head(&map, 3, 3, 4) == GROUNDWATER_NO_HEAD); /* Open air. */
    /* Full to the floor, with free water over it: the water's surface. */
    groundwater_lay_table(&map, 5, 5, 4 * GROUNDWATER_LEVEL);
    map.water[map_index(&map, 5, 5, 4)] = 300;
    CHECK_EQ_INT((int)groundwater_head(&map, 5, 5, 1),
                 (int)(4 * GROUNDWATER_LEVEL + 300 * MAP_PORE_FINE));
    /* Words: dry, damp, wet, soaked. */
    size_t tile = map_index(&map, 7, 7, 1);
    map.pore[tile] = 0;
    CHECK_STR_EQ(groundwater_dampness_key(&map, tile), "ground.dry");
    map.pore[tile] = (uint16_t)capacity;
    CHECK_STR_EQ(groundwater_dampness_key(&map, tile), "ground.damp");
    map.pore[tile] = (uint16_t)(capacity + 64);
    CHECK_STR_EQ(groundwater_dampness_key(&map, tile), "ground.wet");
    map.pore[tile] = (uint16_t)room;
    CHECK_STR_EQ(groundwater_dampness_key(&map, tile), "ground.soaked");
    CHECK_NEAR(groundwater_share(&map, tile), 1.0, 1e-6);
    CHECK(groundwater_dampness_key(&map, map_index(&map, 7, 7, 4)) == NULL);
    map_free(&map);
}

/* Digging a tile leaves the water it held above its field capacity as free
 * water (the rest goes with what is dug); rock filling a tile takes its
 * free water into its pores. */
static void test_digging_frees_pore_water(void)
{
    Map map;
    REQUIRE(ground_world(&map, 16, 4, 2, ROCK_EARTH));
    size_t tile = map_index(&map, 5, 5, 1);
    map.pore[tile] = (uint16_t)(700 * MAP_PORE_FINE);
    dig(&map, 5, 5, 1);
    CHECK_EQ_INT(map.pore[tile], 0);
    CHECK_EQ_INT(map.water[tile], 100); /* Earth holds 600 mm against gravity. */
    size_t dry = map_index(&map, 6, 5, 1);
    map.pore[dry] = (uint16_t)(300 * MAP_PORE_FINE);
    dig(&map, 6, 5, 1);
    CHECK_EQ_INT(map.water[dry], 0);
    map_set_water(&map, 5, 5, 1, 800);
    map_set_rock(&map, 5, 5, 1, ROCK_SAND);
    CHECK_EQ_INT(map.water[tile], 0);
    CHECK_EQ_INT(map.pore[tile], 700 * MAP_PORE_FINE); /* Sand has room for 700 mm. */
    map_set_rock(&map, 5, 5, 1, ROCK_GRANITE);
    CHECK_EQ_INT(map.pore[tile], (int)rock_pore_room(ROCK_GRANITE)); /* What it has room for. */
    map_free(&map);
}

/* Layers written directly (tests, scenarios) move no water: the pores keep
 * within the room of the rock written, none where there is no rock. */
static void test_moisture_of_a_block(void)
{
    Map map;
    REQUIRE(ground_world(&map, 16, 4, 2, ROCK_EARTH));
    size_t tile = map_index(&map, 5, 5, 1);
    map.pore[tile] = 0;
    CHECK_NEAR(groundwater_moisture(&map, tile), 0.0, 1e-6);
    map.pore[tile] = (uint16_t)rock_pore_capacity(ROCK_EARTH);
    CHECK_NEAR(groundwater_moisture(&map, tile), 1.0, 1e-6);
    map.pore[tile] = (uint16_t)rock_pore_room(ROCK_EARTH);
    CHECK_NEAR(groundwater_moisture(&map, tile), 2.0, 1e-6);
    map.pore[tile] = (uint16_t)(750 * MAP_PORE_FINE); /* Half way from 600 to 900 mm. */
    CHECK_NEAR(groundwater_moisture(&map, tile), 1.5, 1e-3);
    CHECK_NEAR(groundwater_moisture(&map, map_index(&map, 5, 5, 3)), -1.0, 1e-6); /* Open. */
    map_free(&map);
}

/* Digging under the water table or beside a lake lets water in; above the
 * table, beside dry ground, it does not. */
static void test_digging_that_lets_water_in(void)
{
    Map map;
    REQUIRE(ground_world(&map, 16, 6, 4, ROCK_EARTH));
    for (int y = 0; y < 8; y++) { /* The table half way up level 2 in the north. */
        for (int x = 0; x < 16; x++) {
            groundwater_lay_table(&map, x, y, 2 * GROUNDWATER_LEVEL + GROUNDWATER_LEVEL / 2);
        }
    }
    CHECK(groundwater_dig_floods(&map, 5, 3, 1));
    CHECK(groundwater_dig_floods(&map, 5, 3, 2));   /* 1 m of water over its floor. */
    CHECK(!groundwater_dig_floods(&map, 5, 3, 3));  /* Above the table. */
    CHECK(!groundwater_dig_floods(&map, 5, 12, 2)); /* No table in the south. */
    CHECK(!groundwater_dig_floods(&map, 5, 3, 4));  /* Open: nothing to dig. */

    /* A pool on level 3 in the south: the blocks beside it. */
    dig(&map, 8, 12, 3);
    map_set_water(&map, 8, 12, 3, 500);
    CHECK(groundwater_dig_floods(&map, 9, 12, 3));
    CHECK(!groundwater_dig_floods(&map, 10, 12, 3));
    map_set_water(&map, 8, 12, 3, GROUNDWATER_FLOOD_MM / 2); /* A puddle. */
    CHECK(!groundwater_dig_floods(&map, 9, 12, 3));
    map_free(&map);
}

static void test_direct_writes_keep_pores_within_their_room(void)
{
    Map map;
    REQUIRE(ground_world(&map, 16, 4, 2, ROCK_EARTH));
    size_t gone = map_index(&map, 5, 5, 1);
    size_t granite = map_index(&map, 6, 5, 1);
    size_t earth = map_index(&map, 7, 5, 1);
    map.pore[gone] = map.pore[granite] = map.pore[earth] = (uint16_t)(700 * MAP_PORE_FINE);
    map.rock[gone] = ROCK_NONE;
    map.terrain[gone] = TERRAIN_SOIL;
    map.rock[granite] = ROCK_GRANITE;
    map_topology_changed(&map);
    CHECK_EQ_INT(map.pore[gone], 0);
    CHECK_EQ_INT(map.water[gone], 0);
    CHECK_EQ_INT(map.pore[granite], (int)rock_pore_room(ROCK_GRANITE));
    CHECK_EQ_INT(map.pore[earth], 700 * MAP_PORE_FINE);
    map_free(&map);
}

/* Rain soaked into the top of the ground goes down to the water table, at
 * the pace of the earth, and stops over clay. */
static void test_water_soaks_down_and_stops_over_clay(void)
{
    Map map;
    REQUIRE(ground_world(&map, 16, 6, 4, ROCK_EARTH));
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            if (x >= 8) { /* Clay under the top tile on the east side. */
                size_t clay = map_index(&map, x, y, 2);
                map.rock[clay] = ROCK_CLAY;
                map.pore[clay] = (uint16_t)rock_pore_capacity(ROCK_CLAY);
            }
            map.pore[map_index(&map, x, y, 3)] = (uint16_t)rock_pore_room(ROCK_EARTH);
        }
    }
    Water water;
    REQUIRE(water_init(&water, &map));
    water_seal_edges(&water);
    GroundWater ground;
    REQUIRE(groundwater_init(&ground, &map));
    REQUIRE(run_hours(&ground, &map, &water, 10, 0));
    /* Earth: 10 mm an hour went down from the top tile, through the tiles
     * under it. */
    size_t top = map_index(&map, 2, 8, 3);
    int64_t under = 0;
    for (int z = 0; z < 3; z++) {
        under += map.pore[map_index(&map, 2, 8, z)] - (int64_t)rock_pore_capacity(ROCK_EARTH);
    }
    CHECK(under > 90 * MAP_PORE_FINE); /* And some from the clay's side. */
    CHECK(map.pore[top] < rock_pore_room(ROCK_EARTH) - 90 * MAP_PORE_FINE);
    /* Clay lets almost nothing through: the water stays over it. */
    size_t over_clay = map_index(&map, 12, 8, 3);
    CHECK(map.pore[over_clay] > map.pore[top]);
    water_free(&water);
    groundwater_free(&ground);
    map_free(&map);
}

/* Two halves of a plain at different tables even out under the ground, the
 * water kept. */
static void test_tables_even_out(void)
{
    Map map;
    REQUIRE(ground_world(&map, 16, 5, 4, ROCK_SAND));
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            groundwater_lay_table(&map, x, y, x < 8 ? 3 * GROUNDWATER_LEVEL : GROUNDWATER_LEVEL);
        }
    }
    Water water;
    REQUIRE(water_init(&water, &map));
    water_seal_edges(&water);
    GroundWater ground;
    REQUIRE(groundwater_init(&ground, &map));
    REQUIRE(run_hours(&ground, &map, &water, 200, 0));
    int64_t west = groundwater_column_head(&map, 2, 5), east = groundwater_column_head(&map, 13, 5);
    CHECK(west > east);
    CHECK(west - east < GROUNDWATER_LEVEL / 10); /* Within 20 cm. */
    CHECK(east > GROUNDWATER_LEVEL + GROUNDWATER_LEVEL / 2);
    water_free(&water);
    groundwater_free(&ground);
    map_free(&map);
}

/* A gallery dug under the water table fills with water up to near the
 * table: in hours in earth. */
static void test_a_gallery_under_the_table_fills(void)
{
    Map map;
    REQUIRE(ground_world(&map, 16, 6, 4, ROCK_EARTH));
    lay_everywhere(&map, 2 * GROUNDWATER_LEVEL + GROUNDWATER_LEVEL / 2); /* 1 m into level 2. */
    dig(&map, 8, 8, 2);
    Water water;
    REQUIRE(water_init(&water, &map));
    GroundWater ground;
    REQUIRE(groundwater_init(&ground, &map));
    size_t gallery = map_index(&map, 8, 8, 2);
    int before = map.water[gallery];
    REQUIRE(run_hours(&ground, &map, &water, 12, 10));
    CHECK(map.water[gallery] > before + 100);
    REQUIRE(run_hours(&ground, &map, &water, 300, 10));
    /* Up to about the table (1 m), never over it. */
    CHECK(map.water[gallery] > 600);
    CHECK(map.water[gallery] <= 1000);
    water_free(&water);
    groundwater_free(&ground);
    map_free(&map);
}

/* What a gallery dug a level under the water table in `rock` holds after
 * `hours` hours (mm). */
static double gallery_after(RockType rock, int hours)
{
    Map map;
    if (!ground_world(&map, 16, 6, 4, rock)) {
        return -1.0;
    }
    lay_everywhere(&map, 3 * GROUNDWATER_LEVEL + GROUNDWATER_LEVEL / 2);
    dig(&map, 8, 8, 2);
    size_t gallery = map_index(&map, 8, 8, 2);
    map_set_water(&map, 8, 8, 2, 0); /* What digging freed. */
    Water water;
    GroundWater ground;
    double mm = -1.0;
    if (water_init(&water, &map) && groundwater_init(&ground, &map)) {
        if (run_hours(&ground, &map, &water, hours, 10)) {
            mm = water_mm_at(&water, &map, gallery);
        }
        groundwater_free(&ground);
    }
    water_free(&water);
    map_free(&map);
    return mm;
}

/* In granite the same gallery only drips: a few mm an hour, where one in
 * earth fills by the hundreds of mm and one in gravel at once. */
static void test_a_gallery_in_granite_drips(void)
{
    double granite = gallery_after(ROCK_GRANITE, 12);
    double earth = gallery_after(ROCK_EARTH, 12);
    double gravel = gallery_after(ROCK_GRAVEL, 2);
    CHECK(granite > 0.2);
    CHECK(granite < 12.0 * 5.0);
    CHECK(earth > 5.0 * granite);
    CHECK(gravel > 1000.0); /* Its level reached within two hours. */
}

/* A hill holding a high water table: a spring runs out of its side onto the
 * plain at its foot. */
static void test_a_spring_on_a_hillside(void)
{
    Map map;
    REQUIRE(ground_world(&map, 16, 7, 2, ROCK_EARTH));
    /* The hill: x >= 8, three levels higher. */
    for (int y = 0; y < 16; y++) {
        for (int x = 8; x < 16; x++) {
            for (int z = 2; z < 5; z++) {
                size_t index = map_index(&map, x, y, z);
                map.rock[index] = ROCK_EARTH;
                map.terrain[index] = TERRAIN_SOIL;
            }
            map.terrain[map_index(&map, x, y, 5)] = TERRAIN_SOIL;
        }
    }
    map_topology_changed(&map);
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            groundwater_lay_table(&map, x, y, x >= 8 ? 4 * GROUNDWATER_LEVEL : GROUNDWATER_LEVEL);
        }
    }
    Water water;
    REQUIRE(water_init(&water, &map));
    water_seal_edges(&water);
    GroundWater ground;
    REQUIRE(groundwater_init(&ground, &map));
    REQUIRE(run_hours(&ground, &map, &water, 24, 30));
    /* Water came out of the hill's side onto the plain. */
    CHECK(ground.seeped_last_hour > 0);
    double plain = 0.0;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 8; x++) {
            plain += water_mm_at(&water, &map, map_index(&map, x, y, 2));
        }
    }
    CHECK(plain > 10.0);
    water_free(&water);
    groundwater_free(&ground);
    map_free(&map);
}

/* A shaft dug down to under the water table fills up to it: a well. */
static void test_a_well(void)
{
    Map map;
    REQUIRE(ground_world(&map, 16, 7, 5, ROCK_EARTH));
    lay_everywhere(&map, GROUNDWATER_LEVEL + GROUNDWATER_LEVEL / 2); /* 1 m into level 1. */
    for (int z = 1; z < 5; z++) {
        dig(&map, 8, 8, z);
        map_set_terrain(&map, 8, 8, z, z == 1 ? TERRAIN_SOIL : TERRAIN_OPEN);
    }
    Water water;
    REQUIRE(water_init(&water, &map));
    GroundWater ground;
    REQUIRE(groundwater_init(&ground, &map));
    REQUIRE(run_hours(&ground, &map, &water, 200, 10));
    int bottom = map.water[map_index(&map, 8, 8, 1)];
    CHECK(bottom > 600 && bottom <= 1000); /* Up to about the table. */
    CHECK_EQ_INT(map.water[map_index(&map, 8, 8, 2)], 0);
    water_free(&water);
    groundwater_free(&ground);
    map_free(&map);
}

/* A full tile over a gallery drips from its ceiling. */
static void test_a_soaked_ceiling_drips(void)
{
    Map map;
    REQUIRE(ground_world(&map, 16, 5, 3, ROCK_EARTH));
    lay_everywhere(&map, 3 * GROUNDWATER_LEVEL);
    dig(&map, 8, 8, 1);
    map.pore[map_index(&map, 8, 8, 0)] = 0; /* Nothing from under it. */
    map.rock[map_index(&map, 8, 8, 0)] = ROCK_GRANITE;
    for (int d = 0; d < 4; d++) { /* Walls of granite around: no seepage from the side. */
        static const int DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};
        size_t wall = map_index(&map, 8 + DX[d], 8 + DY[d], 1);
        map.rock[wall] = ROCK_GRANITE;
        map.pore[wall] = 0;
    }
    map_topology_changed(&map);
    Water water;
    REQUIRE(water_init(&water, &map));
    water_seal_edges(&water);
    GroundWater ground;
    REQUIRE(groundwater_init(&ground, &map));
    REQUIRE(run_hours(&ground, &map, &water, 3, 0));
    CHECK(water_mm_at(&water, &map, map_index(&map, 8, 8, 1)) > 20.0); /* 10 mm an hour. */
    water_free(&water);
    groundwater_free(&ground);
    map_free(&map);
}

/* No water seeps into a wall: it would be held there for ever. */
static void test_no_seepage_into_walls(void)
{
    Map map;
    REQUIRE(ground_world(&map, 16, 5, 3, ROCK_EARTH));
    lay_everywhere(&map, 3 * GROUNDWATER_LEVEL);
    dig(&map, 8, 8, 2);
    map_set_water(&map, 8, 8, 2, 0); /* What digging freed. */
    map_set_structure(&map, 8, 8, 2, STRUCTURE_WALL);
    Water water;
    REQUIRE(water_init(&water, &map));
    water_seal_edges(&water);
    GroundWater ground;
    REQUIRE(groundwater_init(&ground, &map));
    REQUIRE(run_hours(&ground, &map, &water, 10, 0));
    CHECK_EQ_INT(map.water[map_index(&map, 8, 8, 2)], 0);
    water_free(&water);
    groundwater_free(&ground);
    map_free(&map);
}

/* The edges keep the table beyond as it was: a plain drained from inside
 * (a gallery pumped out) is fed again from beyond; sealed edges let nothing
 * through. */
static void test_the_edges_keep_their_table(void)
{
    for (int sealed = 0; sealed < 2; sealed++) {
        Map map;
        REQUIRE(ground_world(&map, 16, 5, 4, ROCK_GRAVEL));
        lay_everywhere(&map, 2 * GROUNDWATER_LEVEL);
        Water water;
        REQUIRE(water_init(&water, &map));
        if (sealed) {
            water_seal_edges(&water);
        }
        GroundWater ground;
        REQUIRE(groundwater_init(&ground, &map));
        /* Then the table falls inside the map (taken away). */
        lay_everywhere(&map, GROUNDWATER_LEVEL);
        REQUIRE(run_hours(&ground, &map, &water, 100, 0));
        int64_t middle = groundwater_column_head(&map, 8, 8);
        if (sealed) {
            CHECK_EQ_INT((int)ground.edge_in, 0);
            CHECK(llabs(middle - GROUNDWATER_LEVEL) <= 2);
        } else {
            CHECK(ground.edge_in > 0);
            CHECK(middle > GROUNDWATER_LEVEL + GROUNDWATER_LEVEL / 2);
        }
        water_free(&water);
        groundwater_free(&ground);
        map_free(&map);
    }
}

/* The same world worked out twice gives the same water. */
static void test_deterministic(void)
{
    uint64_t hashes[2];
    for (int run = 0; run < 2; run++) {
        Map map;
        REQUIRE(ground_world(&map, 16, 6, 4, ROCK_EARTH));
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                groundwater_lay_table(&map, x, y, (int64_t)(x + y) * GROUNDWATER_LEVEL / 8);
            }
        }
        dig(&map, 5, 9, 1);
        Water water;
        REQUIRE(water_init(&water, &map));
        GroundWater ground;
        REQUIRE(groundwater_init(&ground, &map));
        REQUIRE(run_hours(&ground, &map, &water, 30, 5));
        hashes[run] = groundwater_hash(&ground, map_hash(&map));
        water_free(&water);
        groundwater_free(&ground);
        map_free(&map);
    }
    CHECK(hashes[0] == hashes[1]);
}

int main(void)
{
    RUN_TEST(test_rock_rooms);
    RUN_TEST(test_a_table_laid_and_its_heads);
    RUN_TEST(test_digging_frees_pore_water);
    RUN_TEST(test_direct_writes_keep_pores_within_their_room);
    RUN_TEST(test_moisture_of_a_block);
    RUN_TEST(test_digging_that_lets_water_in);
    RUN_TEST(test_water_soaks_down_and_stops_over_clay);
    RUN_TEST(test_tables_even_out);
    RUN_TEST(test_a_gallery_under_the_table_fills);
    RUN_TEST(test_a_gallery_in_granite_drips);
    RUN_TEST(test_a_spring_on_a_hillside);
    RUN_TEST(test_a_well);
    RUN_TEST(test_a_soaked_ceiling_drips);
    RUN_TEST(test_no_seepage_into_walls);
    RUN_TEST(test_the_edges_keep_their_table);
    RUN_TEST(test_deterministic);
    return test_summary();
}
