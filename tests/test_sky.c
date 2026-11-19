#include "core/map.h"
#include "core/sky.h"
#include "core/structure.h"

#include "test_framework.h"

#define SCENE_SIZE 64
#define SCENE_DEPTH 8

/* Flat soil on level 0 and open space over it. */
static bool scene_init(Map *map)
{
    return map_init(map, SCENE_SIZE, SCENE_SIZE, SCENE_DEPTH);
}

/* How much of the sky the ground of level 0 sees at the middle of a tile. */
static float ground(const Map *map, int x, int y)
{
    return sky_open_drawn(map, x, y, 0);
}

/* Walls of one level around (x0, y0) .. (x1, y1) (inclusive, the walls on
 * that border), a door in the middle of the south wall, a thatched roof over
 * all of it. */
static void build_hut(Map *map, int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (x == x0 || x == x1 || y == y0 || y == y1) {
                map_set_structure(map, x, y, 0, STRUCTURE_WALL);
            }
            map_set_roof(map, x, y, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
        }
    }
    map_set_structure(map, (x0 + x1) / 2, y1, 0, STRUCTURE_DOOR);
}

/* Open ground sees all the sky, the top of a wall too, and where nothing is
 * drawn counts as open. */
static void test_open_ground_sees_all_of_it(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    CHECK_NEAR(ground(&map, 30, 30), 1.0, 1e-4);
    CHECK_NEAR(ground(&map, 0, 0), 1.0, 1e-4); /* Rays leaving the map keep their light. */
    map_set_structure(&map, 10, 10, 0, STRUCTURE_WALL);
    CHECK_NEAR(ground(&map, 10, 10), 1.0, 1e-4);
    map_free(&map);
}

/* A closed hut is dark inside, whatever the hour; its door kept open lets
 * some sky in near it, less further in; with its south wall gone it is
 * brighter still, but not as bright as outside. */
static void test_a_hut_is_dark_inside(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    build_hut(&map, 20, 20, 28, 28); /* Inside: 21 .. 27, the door at (24, 28). */
    CHECK_NEAR(ground(&map, 24, 24), 0.0, 1e-6);
    CHECK_NEAR(ground(&map, 24, 27), 0.0, 1e-6);

    REQUIRE(map_set_door_kept_open(&map, 24, 28, 0, true));
    float by_the_door = ground(&map, 24, 27);
    CHECK(by_the_door > 0.05f && by_the_door < 0.5f);
    CHECK(ground(&map, 24, 22) < by_the_door);

    for (int x = 21; x <= 27; x++) {
        map_set_structure(&map, x, 28, 0, STRUCTURE_NONE);
    }
    float open_side = ground(&map, 24, 27);
    CHECK(open_side > by_the_door && open_side < 0.9f);
    map_free(&map);
}

/* A roof on posts hides the sky above, not the low sky around: under a
 * small one it is bright, in the middle of a wide one dark. */
static void test_a_roof_on_posts(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    for (int y = 10; y <= 12; y++) {
        for (int x = 10; x <= 12; x++) {
            map_set_roof(&map, x, y, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
        }
    }
    map_set_structure(&map, 10, 10, 0, STRUCTURE_PROP);
    float small = ground(&map, 11, 11);
    CHECK(small > 0.5f && small < 0.9f);

    for (int y = 30; y < 50; y++) {
        for (int x = 30; x < 50; x++) {
            map_set_roof(&map, x, y, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
        }
    }
    CHECK(ground(&map, 40, 40) < 0.02f);
    CHECK(ground(&map, 40, 49) > ground(&map, 40, 45)); /* Brighter towards the edge. */
    map_free(&map);
}

/* Rock two levels thick south of y = 20, a tunnel one tile wide dug north
 * to south into it: the cave darkens from its mouth inwards, dark three
 * tiles in. */
static void test_a_cave_darkens_inwards(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    for (int y = 20; y < SCENE_SIZE; y++) {
        for (int x = 0; x < SCENE_SIZE; x++) {
            for (int z = 0; z < 2; z++) {
                if (z > 0 || x != 30) {
                    map.rock[map_index(&map, x, y, z)] = ROCK_GRANITE;
                }
            }
        }
    }
    float mouth = ground(&map, 30, 20);
    CHECK(mouth > 0.1f);
    CHECK(ground(&map, 30, 21) < mouth);
    CHECK(ground(&map, 30, 23) < 0.05f);
    CHECK_NEAR(ground(&map, 30, 30), 0.0, 1e-6);
    map_free(&map);
}

/* At the foot of a high cliff the ground sees about half the sky. */
static void test_the_foot_of_a_cliff(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    for (int y = 30; y < SCENE_SIZE; y++) {
        for (int x = 0; x < SCENE_SIZE; x++) {
            for (int z = 0; z < 5; z++) {
                map.rock[map_index(&map, x, y, z)] = ROCK_GRANITE;
            }
        }
    }
    float foot = ground(&map, 30, 29);
    CHECK(foot > 0.4f && foot < 0.65f);
    CHECK(ground(&map, 30, 25) > foot); /* Further away, more sky. */
    map_free(&map);
}

/* Foliage over the ground lets some sky through, more when bare. */
static void test_through_foliage(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    for (int y = 0; y < SCENE_SIZE; y++) {
        for (int x = 0; x < SCENE_SIZE; x++) {
            map.terrain[map_index(&map, x, y, 2)] = TERRAIN_FOLIAGE_MEDIUM;
        }
    }
    float in_leaf = ground(&map, 30, 30);
    CHECK(in_leaf > 0.05f && in_leaf < MAP_LIGHT_FOLIAGE_MEDIUM);
    map_set_bare(&map, MAP_BARE_STEPS);
    CHECK(ground(&map, 30, 30) > in_leaf);
    map_free(&map);
}

/* The field gives what sky_open_drawn() gives, keeps it while nothing
 * changes nearby, works it out again within the budget once something did,
 * keeps the old value meanwhile, and falls back on the cover above when it
 * never worked a column out. */
static void test_field_follows_the_map(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    build_hut(&map, 20, 20, 28, 28);
    SkyField field;
    sky_field_init(&field);

    REQUIRE(sky_field_begin(&field, &map, 0, 0, SCENE_SIZE, SCENE_SIZE));
    int none = 0;
    CHECK_NEAR(sky_field_open(&field, 24, 24, 0, &none), map_light_cover(&map, 24, 24, 0), 1e-6);
    CHECK_NEAR(sky_field_open(&field, 5, 5, 0, &none), 1.0, 1e-6);

    int budget = 1000;
    for (int y = 18; y < 31; y++) {
        for (int x = 18; x < 31; x++) {
            CHECK_NEAR(sky_field_open(&field, x, y, 0, &budget), ground(&map, x, y), 1.0 / 255.0);
        }
    }
    CHECK_EQ_INT(budget, 1000 - 13 * 13);
    uint64_t rays = field.rays;
    CHECK_EQ_UINT(rays, (uint64_t)(13 * 13 * SKY_RAYS));

    /* Nothing changed: no work. */
    REQUIRE(sky_field_begin(&field, &map, 0, 0, SCENE_SIZE, SCENE_SIZE));
    CHECK_NEAR(sky_field_open(&field, 24, 27, 0, &budget), 0.0, 1e-6);
    CHECK_EQ_UINT(field.rays, rays);

    /* The door kept open: the old value without budget, the new one with. */
    REQUIRE(map_set_door_kept_open(&map, 24, 28, 0, true));
    REQUIRE(sky_field_begin(&field, &map, 0, 0, SCENE_SIZE, SCENE_SIZE));
    CHECK_NEAR(sky_field_open(&field, 24, 27, 0, &none), 0.0, 1e-6);
    float lit = sky_field_open(&field, 24, 27, 0, &budget);
    CHECK(lit > 0.05f);
    CHECK_NEAR(lit, ground(&map, 24, 27), 1e-6);

    /* A change further than SKY_REACH (to the block) leaves a column alone:
     * the corner block (48 .. 63) is out of reach of the first (0 .. 15). */
    CHECK_NEAR(sky_field_open(&field, 5, 5, 0, &budget), 1.0, 1e-4);
    map_set_structure(&map, 62, 62, 0, STRUCTURE_WALL);
    REQUIRE(sky_field_begin(&field, &map, 0, 0, SCENE_SIZE, SCENE_SIZE));
    rays = field.rays;
    CHECK_NEAR(sky_field_open(&field, 5, 5, 0, &budget), 1.0, 1e-4);
    CHECK_EQ_UINT(field.rays, rays);

    /* Another level shown is another value: the top of the roof. */
    CHECK_NEAR(sky_field_open(&field, 24, 24, 1, &budget), 1.0, 1e-4);
    CHECK_EQ_UINT(field.rays, rays + SKY_RAYS);

    sky_field_free(&field);
    map_free(&map);
}

int main(void)
{
    RUN_TEST(test_open_ground_sees_all_of_it);
    RUN_TEST(test_a_hut_is_dark_inside);
    RUN_TEST(test_a_roof_on_posts);
    RUN_TEST(test_a_cave_darkens_inwards);
    RUN_TEST(test_the_foot_of_a_cliff);
    RUN_TEST(test_through_foliage);
    RUN_TEST(test_field_follows_the_map);
    return test_summary();
}
