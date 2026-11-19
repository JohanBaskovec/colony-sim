#include "core/fire_light.h"
#include "core/hearth.h"
#include "core/map.h"
#include "core/structure.h"

#include "test_framework.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SCENE_SIZE 48
#define SCENE_DEPTH 6

static Workstation fire_at(int x, int y, int z)
{
    Workstation station;
    memset(&station, 0, sizeof(station));
    station.used = true;
    station.burning = true;
    station.x = (int16_t)x;
    station.y = (int16_t)y;
    station.z = (int16_t)z;
    return station;
}

/* The light at the middle of a tile with level z shown. */
static float seen(const Workstation *fire, const Map *map, int x, int y, int z)
{
    return fire_light_seen(fire, map, x, y, z, 0.5f, 0.5f);
}

/* On open ground the light fades with the distance, to nothing at the
 * radius. */
static void test_fades_with_distance(void)
{
    Map map;
    REQUIRE(map_init(&map, SCENE_SIZE, SCENE_SIZE, SCENE_DEPTH));
    Workstation fire = fire_at(20, 20, 0);
    float flame = FIRE_LIGHT_FLAME_HEIGHT - 0.01f; /* The flame over the ground. */
    CHECK_NEAR(seen(&fire, &map, 20, 20, 0), 1.0f - flame / HEARTH_LIGHT_RADIUS, 1e-4);
    CHECK_NEAR(seen(&fire, &map, 23, 20, 0),
               1.0f - sqrtf(9.0f + flame * flame) / HEARTH_LIGHT_RADIUS, 1e-4);
    CHECK(seen(&fire, &map, 22, 20, 0) > seen(&fire, &map, 24, 20, 0));
    CHECK_NEAR(seen(&fire, &map, 26, 20, 0), 0.0, 1e-6);
    CHECK_NEAR(seen(&fire, &map, 25, 24, 0), 0.0, 1e-6);
    map_free(&map);
}

/* A wall: nothing behind it, its face towards the fire lit; a shadow
 * pointing away from the fire past its end. */
static void test_stops_at_walls(void)
{
    Map map;
    REQUIRE(map_init(&map, SCENE_SIZE, SCENE_SIZE, SCENE_DEPTH));
    Workstation fire = fire_at(20, 20, 0);
    for (int y = 18; y <= 22; y++) {
        map_set_structure(&map, 22, y, 0, STRUCTURE_WALL);
    }
    CHECK_NEAR(seen(&fire, &map, 23, 20, 0), 0.0, 1e-6);
    CHECK_NEAR(seen(&fire, &map, 24, 21, 0), 0.0, 1e-6);
    CHECK(seen(&fire, &map, 22, 20, 0) > 0.3f); /* Its face. */
    CHECK(seen(&fire, &map, 21, 20, 0) > 0.5f);
    /* Past the wall's end: lit beside it, not in its shadow. */
    CHECK(seen(&fire, &map, 22, 24, 0) > 0.0f);
    CHECK_NEAR(seen(&fire, &map, 24, 22, 0), 0.0, 1e-6);
    map_free(&map);
}

/* A fire inside a closed hut lights its room and nothing outside; with the
 * door kept open its light falls out in front of the door, not beside. A
 * fire outside does not light the inside. */
static void test_a_fire_in_a_hut(void)
{
    Map map;
    REQUIRE(map_init(&map, SCENE_SIZE, SCENE_SIZE, SCENE_DEPTH));
    for (int y = 16; y <= 24; y++) {
        for (int x = 16; x <= 24; x++) {
            if (x == 16 || x == 24 || y == 16 || y == 24) {
                map_set_structure(&map, x, y, 0, STRUCTURE_WALL);
            }
            map_set_roof(&map, x, y, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
        }
    }
    map_set_structure(&map, 20, 24, 0, STRUCTURE_DOOR);
    Workstation fire = fire_at(20, 21, 0);
    CHECK(seen(&fire, &map, 18, 22, 0) > 0.3f);
    CHECK_NEAR(seen(&fire, &map, 20, 25, 0), 0.0, 1e-6);
    CHECK_NEAR(seen(&fire, &map, 20, 26, 0), 0.0, 1e-6);

    REQUIRE(map_set_door_kept_open(&map, 20, 24, 0, true));
    CHECK(seen(&fire, &map, 20, 25, 0) > 0.2f);
    CHECK(seen(&fire, &map, 20, 26, 0) > 0.0f);
    CHECK_NEAR(seen(&fire, &map, 23, 25, 0), 0.0, 1e-6);

    REQUIRE(map_set_door_kept_open(&map, 20, 24, 0, false));
    Workstation outside = fire_at(20, 27, 0);
    CHECK(seen(&outside, &map, 20, 26, 0) > 0.5f);
    CHECK_NEAR(seen(&outside, &map, 20, 23, 0), 0.0, 1e-6);
    map_free(&map);
}

/* The light goes down where nothing is in the way: a fire at the edge of a
 * rock shelf one level high lights the ground below it far enough out, not
 * right at the foot of the shelf, hidden by its edge. */
static void test_down_from_a_shelf(void)
{
    Map map;
    REQUIRE(map_init(&map, SCENE_SIZE, SCENE_SIZE, SCENE_DEPTH));
    for (int y = 0; y < SCENE_SIZE; y++) {
        for (int x = 0; x <= 10; x++) {
            map.rock[map_index(&map, x, y, 0)] = ROCK_GRANITE;
        }
    }
    Workstation fire = fire_at(10, 20, 1); /* On the shelf, at its edge. */
    CHECK_NEAR(seen(&fire, &map, 12, 20, 1), 0.0, 1e-6);
    CHECK(seen(&fire, &map, 14, 20, 1) > 0.0f);
    CHECK(seen(&fire, &map, 9, 20, 1) > 0.5f); /* On the shelf itself. */
    map_free(&map);
}

/* Kept per fire: worked out once, again when the map changes within its
 * reach (a door opening), not when it changes beyond; filled into the
 * screen's points as the brightest of the fires. */
static void test_kept_per_fire(void)
{
    Map map;
    REQUIRE(map_init(&map, SCENE_SIZE, SCENE_SIZE, SCENE_DEPTH));
    Workstations *stations = malloc(sizeof(*stations));
    REQUIRE(stations != NULL);
    REQUIRE(workstations_init(stations, SCENE_SIZE, SCENE_SIZE, SCENE_DEPTH));
    for (int y = 0; y < SCENE_SIZE; y++) {
        map_set_structure(&map, 20, y, 0, STRUCTURE_WALL);
    }
    map_set_structure(&map, 20, 10, 0, STRUCTURE_DOOR);
    stations->list[0] = fire_at(18, 10, 0);
    stations->list[1] = fire_at(5, 40, 0);
    stations->list[2] = fire_at(5, 10, 3); /* Over the level shown: left out. */
    const int samples = 2, columns = 32, rows = 48;
    float *points = malloc((size_t)(columns * samples) * (size_t)(rows * samples) * sizeof(float));
    REQUIRE(points != NULL);
    FireLight light;
    fire_light_init(&light);

    int64_t budget = 0;
    REQUIRE(fire_light_update(&light, stations, &map, 0, samples, 0, 0, columns, rows, &budget));
    CHECK(!fire_light_fill(&light, stations, 0, 0, 0, columns, rows, samples, points));

    budget = 1;
    REQUIRE(fire_light_update(&light, stations, &map, 0, samples, 0, 0, columns, rows, &budget));
    const int64_t per_fire = (int64_t)(FIRE_LIGHT_SPAN * samples) * (FIRE_LIGHT_SPAN * samples);
    CHECK(budget == 1 - per_fire); /* One fire, finished. */
    budget = 1000000;
    REQUIRE(fire_light_update(&light, stations, &map, 0, samples, 0, 0, columns, rows, &budget));
    CHECK(budget == 1000000 - (per_fire - 13 * samples * samples)); /* A column off the map. */
    CHECK(!light.slots[2].ready);
    uint64_t rays = light.rays;
    REQUIRE(fire_light_update(&light, stations, &map, 0, samples, 0, 0, columns, rows, &budget));
    CHECK_EQ_UINT(light.rays, rays);

    REQUIRE(fire_light_fill(&light, stations, 0, 0, 0, columns, rows, samples, points));
    const int width = columns * samples;
    /* The point (si, sj) of the tile (x, y). */
#define POINT(x, y, si, sj) points[((y) * samples + (sj)) * width + (x) * samples + (si)]
    CHECK_NEAR(POINT(19, 10, 0, 0),
               fire_light_seen(&stations->list[0], &map, 19, 10, 0, 0.25f, 0.25f), 1.0 / 255.0);
    CHECK_NEAR(POINT(21, 10, 1, 1), 0.0, 1e-6); /* Behind the closed door. */
    CHECK(POINT(5, 41, 0, 0) > 0.5f);           /* The other fire. */
    CHECK_NEAR(POINT(5, 25, 0, 0), 0.0, 1e-6);

    /* The door kept open: worked out again, lit behind it. */
    REQUIRE(map_set_door_kept_open(&map, 20, 10, 0, true));
    REQUIRE(fire_light_update(&light, stations, &map, 0, samples, 0, 0, columns, rows, &budget));
    CHECK_EQ_UINT(light.rays, rays + (uint64_t)per_fire);
    REQUIRE(fire_light_fill(&light, stations, 0, 0, 0, columns, rows, samples, points));
    CHECK(POINT(21, 10, 1, 1) > 0.2f);
    /* A change out of reach of both. */
    rays = light.rays;
    map_set_structure(&map, 40, 40, 0, STRUCTURE_WALL);
    REQUIRE(fire_light_update(&light, stations, &map, 0, samples, 0, 0, columns, rows, &budget));
    CHECK_EQ_UINT(light.rays, rays);
    /* Another level shown, other points: worked out again. */
    REQUIRE(fire_light_update(&light, stations, &map, 1, samples, 0, 0, columns, rows, &budget));
    CHECK(light.rays > rays);
#undef POINT

    fire_light_free(&light);
    free(points);
    workstations_free(stations);
    free(stations);
    map_free(&map);
}

int main(void)
{
    RUN_TEST(test_fades_with_distance);
    RUN_TEST(test_stops_at_walls);
    RUN_TEST(test_a_fire_in_a_hut);
    RUN_TEST(test_down_from_a_shelf);
    RUN_TEST(test_kept_per_fire);
    return test_summary();
}
