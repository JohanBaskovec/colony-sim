#include "core/game_clock.h"
#include "core/map.h"
#include "core/structure.h"
#include "core/sun.h"

#include "test_framework.h"

#include <math.h>
#include <stdlib.h>

#define MIDSUMMER SEASON_DAY(SEASON_SUMMER, 8)
#define MIDWINTER SEASON_DAY(SEASON_WINTER, 8)
#define SPRING_EQUINOX SEASON_DAY(SEASON_SPRING, 8)
#define AUTUMN_EQUINOX SEASON_DAY(SEASON_AUTUMN, 8)

/* Length of the shadow a wall of one level (2 m) casts on flat ground. */
static float wall_shadow(SunPosition sun)
{
    return 2.0f * hypotf(sun.east, sun.south) / sun.up;
}

/* About ±23.4° at the solstices, 0 at the equinoxes, rising from midwinter
 * to midsummer and falling back. */
static void test_declination_follows_the_year(void)
{
    CHECK_NEAR(sun_declination(MIDSUMMER), 23.44, 0.1);
    CHECK_NEAR(sun_declination(MIDWINTER), -23.44, 0.1);
    CHECK_NEAR(sun_declination(SPRING_EQUINOX), 0.0, 1e-3);
    CHECK_NEAR(sun_declination(AUTUMN_EQUINOX), 0.0, 1e-3);
    for (int day = SPRING_EQUINOX; day < MIDSUMMER; day++) {
        CHECK(sun_declination(day + 1) > sun_declination(day));
    }
    for (int day = MIDSUMMER; day < MIDWINTER; day++) {
        CHECK(sun_declination(day + 1) < sun_declination(day));
    }
}

/* docs/SHADOWS.md §2.2: the noon sun by season, in the south, and the shadow
 * of a 2 m wall pointing north. */
static void test_noon_by_season(void)
{
    SunPosition summer = sun_position(SUN_LATITUDE_DEFAULT, MIDSUMMER, 12.0f);
    SunPosition equinox = sun_position(SUN_LATITUDE_DEFAULT, SPRING_EQUINOX, 12.0f);
    SunPosition winter = sun_position(SUN_LATITUDE_DEFAULT, MIDWINTER, 12.0f);
    CHECK_NEAR(summer.elevation, 67.8, 0.1);
    CHECK_NEAR(equinox.elevation, 44.4, 0.1);
    CHECK_NEAR(winter.elevation, 21.0, 0.1);
    CHECK_NEAR(wall_shadow(summer), 0.8, 0.05);
    CHECK_NEAR(wall_shadow(equinox), 2.0, 0.05);
    CHECK_NEAR(wall_shadow(winter), 5.2, 0.05);
    SunPosition noons[] = {summer, equinox, winter};
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(noons[i].azimuth, 180.0, 1e-3);
        CHECK_NEAR(noons[i].east, 0.0, 1e-6);
        CHECK(noons[i].south > 0.0f); /* The sun in the south: shadows to the north. */
    }
}

/* East in the morning, west in the afternoon, the same height at the same
 * distance from noon, highest at noon; in summer it rises north of east, in
 * winter south of it. */
static void test_course_of_a_day(void)
{
    for (int day = 0; day < DAYS_PER_YEAR; day += 5) {
        float noon = sun_position(SUN_LATITUDE_DEFAULT, day, 12.0f).elevation;
        for (float before = 0.5f; before < 12.0f; before += 0.5f) {
            SunPosition morning = sun_position(SUN_LATITUDE_DEFAULT, day, 12.0f - before);
            SunPosition afternoon = sun_position(SUN_LATITUDE_DEFAULT, day, 12.0f + before);
            CHECK(morning.east > 0.0f && morning.azimuth > 0.0f && morning.azimuth < 180.0f);
            CHECK(afternoon.east < 0.0f && afternoon.azimuth > 180.0f);
            CHECK_NEAR(morning.elevation, afternoon.elevation, 1e-3);
            CHECK_NEAR(morning.azimuth, 360.0f - afternoon.azimuth, 1e-2);
            CHECK(morning.elevation < noon);
        }
    }
    float summer_rise = game_sunrise(MIDSUMMER), winter_rise = game_sunrise(MIDWINTER);
    CHECK(sun_position(SUN_LATITUDE_DEFAULT, MIDSUMMER, summer_rise).azimuth < 90.0f);
    CHECK(sun_position(SUN_LATITUDE_DEFAULT, MIDWINTER, winter_rise).azimuth > 90.0f);
    /* Long in the morning, shorter until noon. */
    CHECK(wall_shadow(sun_position(SUN_LATITUDE_DEFAULT, SPRING_EQUINOX, 8.0f)) >
          wall_shadow(sun_position(SUN_LATITUDE_DEFAULT, SPRING_EQUINOX, 10.0f)));
}

/* At the default latitude the sun crosses the horizon where the clock puts
 * sunrise and sunset, every day of the year. */
static void test_sunrise_and_sunset_match_the_clock(void)
{
    for (int day = 0; day < DAYS_PER_YEAR; day++) {
        float rise = game_sunrise(day), set = rise + game_day_length(day);
        CHECK_NEAR(sun_position(SUN_LATITUDE_DEFAULT, day, rise).elevation, 0.0, 0.01);
        CHECK_NEAR(sun_position(SUN_LATITUDE_DEFAULT, day, set).elevation, 0.0, 0.01);
        CHECK(!sun_is_up(sun_position(SUN_LATITUDE_DEFAULT, day, rise - 0.25f)));
        CHECK(sun_is_up(sun_position(SUN_LATITUDE_DEFAULT, day, rise + 0.25f)));
        CHECK(sun_is_up(sun_position(SUN_LATITUDE_DEFAULT, day, set - 0.25f)));
        CHECK(!sun_is_up(sun_position(SUN_LATITUDE_DEFAULT, day, set + 0.25f)));
        CHECK(!sun_is_up(sun_position(SUN_LATITUDE_DEFAULT, day, 0.0f)));
    }
}

/* The direction is a unit vector that agrees with the elevation and the
 * azimuth, at any latitude. */
static void test_direction_agrees_with_the_angles(void)
{
    const float latitudes[] = {-90.0f, -60.0f, -10.0f, 0.0f, 23.4f, SUN_LATITUDE_DEFAULT,
                               80.0f,  90.0f};
    for (int l = 0; l < (int)(sizeof latitudes / sizeof latitudes[0]); l++) {
        for (int day = 0; day < DAYS_PER_YEAR; day += 7) {
            for (float hour = 0.0f; hour < 24.0f; hour += 1.25f) {
                SunPosition sun = sun_position(latitudes[l], day, hour);
                float length = sqrtf(sun.east * sun.east + sun.south * sun.south + sun.up * sun.up);
                CHECK_NEAR(length, 1.0, 1e-5);
                CHECK_NEAR(sun.up, sinf(sun.elevation * 3.14159265f / 180.0f), 1e-5);
                CHECK(sun.azimuth >= 0.0f && sun.azimuth < 360.0f);
                float flat = hypotf(sun.east, sun.south);
                if (flat > 1e-3f) {
                    float azimuth = sun.azimuth * 3.14159265f / 180.0f;
                    CHECK_NEAR(sun.east / flat, sinf(azimuth), 1e-3);
                    CHECK_NEAR(-sun.south / flat, cosf(azimuth), 1e-3);
                }
            }
        }
    }
}

/* Other places: the sun overhead at the equator at the equinox, in the north
 * at noon in the south with the seasons the other way round, a sun that does
 * not set in summer nor rise in winter beyond the polar circle, circling at
 * one height at the pole. */
static void test_other_latitudes(void)
{
    SunPosition equator = sun_position(0.0f, SPRING_EQUINOX, 12.0f);
    CHECK_NEAR(equator.elevation, 90.0, 0.01);
    CHECK_NEAR(equator.up, 1.0, 1e-6);

    SunPosition south_summer = sun_position(-SUN_LATITUDE_DEFAULT, MIDWINTER, 12.0f);
    SunPosition south_winter = sun_position(-SUN_LATITUDE_DEFAULT, MIDSUMMER, 12.0f);
    CHECK_NEAR(south_summer.elevation, 67.8, 0.1);
    CHECK_NEAR(south_winter.elevation, 21.0, 0.1);
    CHECK(south_summer.south < 0.0f && fabsf(south_summer.east) < 1e-6f); /* In the north. */

    for (float hour = 0.0f; hour < 24.0f; hour += 0.5f) {
        CHECK(sun_is_up(sun_position(75.0f, MIDSUMMER, hour)));
        CHECK(!sun_is_up(sun_position(75.0f, MIDWINTER, hour)));
        CHECK_NEAR(sun_position(90.0f, MIDSUMMER, hour).elevation, sun_declination(MIDSUMMER),
                   0.01);
    }
    /* The midnight sun stands in the north. */
    CHECK_NEAR(sun_position(75.0f, MIDSUMMER, 0.0f).azimuth, 0.0, 0.01);
}

/* The sun at a tick moves with the minutes, not by whole hours. */
static void test_sun_at_tick(void)
{
    uint64_t day = (uint64_t)(DAYS_PER_YEAR + AUTUMN_EQUINOX) * TICKS_PER_DAY; /* Year 2. */
    uint64_t tick = day + 9 * TICKS_PER_HOUR + TICKS_PER_HOUR / 2;
    SunPosition at_tick = sun_at_tick(tick, SUN_LATITUDE_DEFAULT);
    SunPosition expected = sun_position(SUN_LATITUDE_DEFAULT, AUTUMN_EQUINOX, 9.5f);
    CHECK_NEAR(at_tick.elevation, expected.elevation, 1e-4);
    CHECK_NEAR(at_tick.azimuth, expected.azimuth, 1e-4);
    SunPosition next = sun_at_tick(tick + 1, SUN_LATITUDE_DEFAULT);
    CHECK(next.elevation > at_tick.elevation);
    CHECK(next.elevation - at_tick.elevation < 0.01f);
    CHECK(next.azimuth > at_tick.azimuth);
}

/* ------------------------------------------------------------------------ */
/* The sun's rays                                                           */
/* ------------------------------------------------------------------------ */

#define SCENE_SIZE 48
#define SCENE_DEPTH 6

/* Flat soil on level 0 and open space over it. */
static bool scene_init(Map *map)
{
    return map_init(map, SCENE_SIZE, SCENE_SIZE, SCENE_DEPTH);
}

static SunPosition noon(int day)
{
    return sun_position(SUN_LATITUDE_DEFAULT, day, 12.0f);
}

/* The ground of level 0 at a point. */
static float ground(const Map *map, SunPosition sun, float x, float y)
{
    return sun_lit_floor(map, sun, x, y, 0);
}

/* A wall of one level running east to west casts its shadow to the north at
 * noon, as long as §2.2 says by season; nothing to the south. */
static void test_wall_shadow_by_season(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    const int wall = 30; /* Its north face at y = 30. */
    for (int x = 10; x < 30; x++) {
        map_set_structure(&map, x, wall, 0, STRUCTURE_WALL);
    }
    const struct {
        int day;
        float length;
    } SEASONS[] = {{MIDSUMMER, 0.82f}, {SPRING_EQUINOX, 2.04f}, {MIDWINTER, 5.21f}};
    for (int i = 0; i < 3; i++) {
        SunPosition sun = noon(SEASONS[i].day);
        float edge = (float)wall - SEASONS[i].length;
        CHECK_NEAR(ground(&map, sun, 20.5f, edge + 0.1f), 0.0, 1e-6);
        CHECK_NEAR(ground(&map, sun, 20.5f, edge - 0.1f), 1.0, 1e-6);
        CHECK_NEAR(ground(&map, sun, 20.5f, (float)wall + 1.5f), 1.0, 1e-6);
        /* Past the end of the wall the sun comes through. */
        CHECK_NEAR(ground(&map, sun, 30.5f, edge + 0.1f), 1.0, 1e-6);
        /* The top of the wall is in the sun, its inside is not. */
        CHECK_NEAR(sun_lit_floor(&map, sun, 20.5f, (float)wall + 0.5f, 1), 1.0, 1e-6);
        CHECK_NEAR(ground(&map, sun, 20.5f, (float)wall + 0.5f), 0.0, 1e-6);
    }
    /* In the morning the shadow points west: the tile west of the wall's
     * end is shaded, the one east of it is not. */
    SunPosition morning = sun_position(SUN_LATITUDE_DEFAULT, SPRING_EQUINOX, 8.0f);
    CHECK_NEAR(ground(&map, morning, 9.5f, (float)wall + 0.5f), 0.0, 1e-6);
    CHECK_NEAR(ground(&map, morning, 30.5f, (float)wall + 0.5f), 1.0, 1e-6);
    /* At night nothing is lit. */
    CHECK_NEAR(ground(&map, sun_position(SUN_LATITUDE_DEFAULT, SPRING_EQUINOX, 2.0f), 5.5f, 5.5f),
               0.0, 1e-6);
    map_free(&map);
}

/* High ground two levels up to the south: the cliff shades the foot of it
 * to the north, its top is in the sun. A door is closed: it shades as a
 * wall. */
static void test_cliff_shades_the_level_below(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    const int face = 30;
    for (int y = face; y < SCENE_SIZE; y++) {
        for (int x = 0; x < SCENE_SIZE; x++) {
            for (int z = 0; z < 2; z++) {
                map.rock[map_index(&map, x, y, z)] = ROCK_GRANITE;
            }
        }
    }
    SunPosition sun = noon(SPRING_EQUINOX); /* 4 m of cliff: a shadow of 4.1 m. */
    CHECK_NEAR(ground(&map, sun, 20.5f, (float)face - 3.9f), 0.0, 1e-6);
    CHECK_NEAR(ground(&map, sun, 20.5f, (float)face - 4.3f), 1.0, 1e-6);
    CHECK_NEAR(sun_lit_floor(&map, sun, 20.5f, (float)face + 0.5f, 2), 1.0, 1e-6);
    CHECK_NEAR(sun_lit(&map, sun, 20.5f, (float)face + 0.5f, 1.0f), 0.0, 1e-6); /* In rock. */

    map_set_structure(&map, 5, 10, 0, STRUCTURE_DOOR);
    CHECK_NEAR(ground(&map, sun, 5.5f, 9.5f), 0.0, 1e-6);
    map_free(&map);
}

/* A thatched roof on posts lets no sun through, but a low sun comes in under
 * its edge: in winter the floor under the south edge is in the sun, deeper
 * under it is not; in summer only what lies outside is. */
static void test_sun_under_the_edge_of_a_roof(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    for (int y = 10; y < 20; y++) { /* The roof's south edge at y = 20. */
        for (int x = 10; x < 20; x++) {
            map_set_roof(&map, x, y, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
        }
    }
    map_set_structure(&map, 10, 19, 0, STRUCTURE_PROP);
    REQUIRE(map.terrain[map_index(&map, 15, 15, 1)] == TERRAIN_GRASS_ROOF);
    SunPosition winter = noon(MIDWINTER), summer = noon(MIDSUMMER);
    CHECK_NEAR(ground(&map, winter, 15.5f, 17.5f), 1.0, 1e-6); /* 2.5 m in. */
    CHECK_NEAR(ground(&map, winter, 15.5f, 13.5f), 0.0, 1e-6); /* 6.5 m in. */
    CHECK_NEAR(ground(&map, summer, 15.5f, 19.5f), 1.0, 1e-6); /* 0.5 m in: 0.8 m of shade. */
    CHECK_NEAR(ground(&map, summer, 15.5f, 18.5f), 0.0, 1e-6); /* Nothing through thatch. */
    CHECK_NEAR(ground(&map, summer, 15.5f, 15.5f), 0.0, 1e-6);
    CHECK_NEAR(ground(&map, summer, 15.5f, 20.5f), 1.0, 1e-6);
    /* A post casts nothing: the tile north of it is lit in winter. */
    CHECK_NEAR(ground(&map, winter, 10.5f, 18.5f), 1.0, 1e-6);
    /* The top of the roof is in the sun. */
    CHECK_NEAR(sun_lit_floor(&map, summer, 15.5f, 15.5f, 1), 1.0, 1e-6);
    map_free(&map);
}

/* A tree: a trunk three levels high, a crown of foliage over levels 2 and
 * 3. Its crown shades beside it, less than a wall would and less when its
 * leaves are gone; its trunk shades fully. */
static void test_crown_shades_beside_its_tree(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    const int tx = 20, ty = 30;
    for (int z = 0; z < 3; z++) {
        map.rock[map_index(&map, tx, ty, z)] = ROCK_WOOD;
    }
    for (int z = 2; z < 4; z++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                size_t index = map_index(&map, tx + dx, ty + dy, z);
                if (map.rock[index] == ROCK_NONE) {
                    map.terrain[index] = TERRAIN_FOLIAGE_MEDIUM;
                }
            }
        }
    }
    SunPosition sun = noon(SPRING_EQUINOX);
    /* Six metres north of the crown, beside the trunk: the ray climbs
     * through the crown, not through the trunk. */
    float in_leaf = ground(&map, sun, (float)tx + 1.5f, (float)ty - 6.0f);
    CHECK(in_leaf > 0.05f && in_leaf < 0.9f);
    /* Nearer, the ray goes through the crown's bottom layer only: brighter.
     * Right under it the noon sun comes from under the crown (it starts 4 m
     * up). */
    float nearer = ground(&map, sun, (float)tx + 1.5f, (float)ty - 3.0f);
    CHECK(nearer > in_leaf && nearer < 1.0f);
    CHECK_NEAR(ground(&map, sun, (float)tx + 1.5f, (float)ty + 0.5f), 1.0, 1e-6);
    /* Out of its shadow. */
    CHECK_NEAR(ground(&map, sun, (float)tx + 5.5f, (float)ty - 6.0f), 1.0, 1e-6);
    CHECK_NEAR(ground(&map, sun, (float)tx + 1.5f, (float)ty + 3.5f), 1.0, 1e-6);
    /* The trunk's own shadow, at the foot of the tree. */
    CHECK_NEAR(ground(&map, sun, (float)tx + 0.5f, (float)ty - 0.5f), 0.0, 1e-6);
    /* Bare branches let more through. */
    map_set_bare(&map, MAP_BARE_STEPS);
    float bare = ground(&map, sun, (float)tx + 1.5f, (float)ty - 6.0f);
    CHECK(bare > in_leaf && bare < 1.0f);
    map_free(&map);
}

/* With the sun straight overhead a ray crosses each layer once, top to
 * bottom: the light is what map_light_cover() gives under foliage. */
static void test_overhead_sun_matches_the_cover(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    const Terrain LAYERS[] = {TERRAIN_FOLIAGE, TERRAIN_FOLIAGE_MEDIUM, TERRAIN_FOLIAGE_SPARSE};
    for (int i = 0; i < 3; i++) {
        map.terrain[map_index(&map, 5 + i, 5, 2)] = LAYERS[i];
        map.terrain[map_index(&map, 5 + i, 5, 3 + i % 2)] = LAYERS[(i + 1) % 3];
    }
    SunPosition overhead = sun_position(0.0f, SPRING_EQUINOX, 12.0f);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(ground(&map, overhead, 5.5f + (float)i, 5.5f),
                   map_light_cover(&map, 5 + i, 5, 0), 1e-4);
    }
    map_free(&map);
}

/* What is not a block casts nothing: furniture, posts, ladders, plants. A
 * ray is only followed SUN_SHADOW_REACH tiles; a ray leaving the map's side
 * leaves the shadows behind. */
static void test_what_casts_nothing(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    SunPosition sun = noon(MIDWINTER);
    map_set_structure(&map, 10, 11, 0, STRUCTURE_GRASS_BED);
    map_set_structure(&map, 12, 11, 0, STRUCTURE_LADDER);
    map_set_structure(&map, 14, 11, 0, STRUCTURE_GROUND_BEDDING);
    map.plants[map_index(&map, 16, 11, 0)].type = PLANT_BRAMBLE;
    for (int x = 10; x <= 16; x++) {
        CHECK_NEAR(ground(&map, sun, (float)x + 0.5f, 10.5f), 1.0, 1e-6);
    }
    /* A wall further south than the reach of the rays casts nothing here,
     * even with a sun low enough for its shadow to get this far. */
    for (int x = 0; x < SCENE_SIZE; x++) {
        for (int z = 0; z < SCENE_DEPTH - 1; z++) {
            map.rock[map_index(&map, x, SCENE_SIZE - 1, z)] = ROCK_GRANITE;
        }
    }
    SunPosition low = sun_position(SUN_LATITUDE_DEFAULT, MIDWINTER, 9.5f);
    CHECK_NEAR(ground(&map, low, 20.5f, (float)SCENE_SIZE - 3.5f), 0.0, 1e-6);
    CHECK_NEAR(ground(&map, low, 20.5f, 1.5f), 1.0, 1e-6);
    /* Out of the map. */
    CHECK_NEAR(sun_lit(&map, sun, -1.0f, 5.0f, 0.5f), 0.0, 1e-6);
    CHECK_NEAR(sun_lit(&map, sun, 5.0f, 5.0f, 2.0f * SCENE_DEPTH + 1.0f), 1.0, 1e-6);
    map_free(&map);
}

/* A slow walk along the ray in small steps, for comparison: the cells it
 * goes through, the floors it crosses going up, the share of the foliage by
 * the length in it. */
static float brute_force(const Map *map, SunPosition sun, float x, float y, float height)
{
    const float step = 0.002f;
    float flat = hypotf(sun.east, sun.south);
    float reach = flat > 0.0f ? (float)SUN_SHADOW_REACH / flat : 1.0e30f;
    float light = 1.0f;
    int last_z = (int)floorf(height / 2.0f);
    for (float t = 0.0f; t < reach; t += step) {
        float px = x + sun.east * t, py = y + sun.south * t, pz = height + sun.up * t;
        int cx = (int)floorf(px), cy = (int)floorf(py), cz = (int)floorf(pz / 2.0f);
        if (cx < 0 || cy < 0 || cx >= map->width || cy >= map->height || cz >= map->depth) {
            break;
        }
        size_t index = map_index(map, cx, cy, cz);
        uint8_t terrain = map->terrain[index];
        if (cz > last_z && terrain != TERRAIN_OPEN && !terrain_is_foliage(terrain)) {
            return 0.0f;
        }
        last_z = cz;
        if (map->rock[index] != ROCK_NONE || map->structure[index] == STRUCTURE_WALL ||
            map->structure[index] == STRUCTURE_DOOR) {
            return 0.0f;
        }
        if (terrain_is_foliage(terrain)) {
            light *= powf(map_foliage_light((Terrain)terrain, map_bare_fraction(map)), step / 2.0f);
        }
    }
    return light;
}

/* A cluttered scene (rock, walls, roofs, foliage of every kind at random)
 * seen at many hours: the walk through the grid agrees with the slow walk
 * but for a few points on the edges of shadows. */
static void test_matches_brute_force(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    srand(7);
    for (int z = 0; z < SCENE_DEPTH - 1; z++) {
        for (int y = 0; y < SCENE_SIZE; y++) {
            for (int x = 0; x < SCENE_SIZE; x++) {
                size_t index = map_index(&map, x, y, z);
                int roll = rand() % 100;
                if (roll < 4 - z / 2) {
                    map.rock[index] = ROCK_GRANITE;
                } else if (roll < 7) {
                    map_set_structure(&map, x, y, z, STRUCTURE_WALL);
                } else if (roll < 9 && z > 0) {
                    map.terrain[index] = TERRAIN_GRASS_ROOF;
                } else if (roll < 18 && z > 0) {
                    map.terrain[index] = (uint8_t)(TERRAIN_FOLIAGE + rand() % 3);
                }
            }
        }
    }
    int points = 0, wrong = 0;
    for (int day = 0; day < DAYS_PER_YEAR; day += 11) {
        for (float hour = 7.0f; hour <= 17.0f; hour += 0.75f) {
            SunPosition sun = sun_position(SUN_LATITUDE_DEFAULT, day, hour);
            if (!sun_is_up(sun)) {
                continue;
            }
            for (int k = 0; k < 40; k++) {
                float x = 4.0f + (float)(rand() % 4000) / 100.0f;
                float y = 4.0f + (float)(rand() % 4000) / 100.0f;
                int z = rand() % 3;
                float fast = sun_lit_floor(&map, sun, x, y, z);
                float slow = brute_force(&map, sun, x, y, (float)z * 2.0f + SUN_RAY_LIFT);
                points++;
                wrong += fabsf(fast - slow) > 0.02f;
            }
        }
    }
    CHECK(points > 1000);
    CHECK(wrong * 200 < points); /* Under 0.5 %. */
    map_free(&map);
}

int main(void)
{
    RUN_TEST(test_declination_follows_the_year);
    RUN_TEST(test_noon_by_season);
    RUN_TEST(test_course_of_a_day);
    RUN_TEST(test_sunrise_and_sunset_match_the_clock);
    RUN_TEST(test_direction_agrees_with_the_angles);
    RUN_TEST(test_other_latitudes);
    RUN_TEST(test_sun_at_tick);
    RUN_TEST(test_wall_shadow_by_season);
    RUN_TEST(test_cliff_shades_the_level_below);
    RUN_TEST(test_sun_under_the_edge_of_a_roof);
    RUN_TEST(test_crown_shades_beside_its_tree);
    RUN_TEST(test_overhead_sun_matches_the_cover);
    RUN_TEST(test_what_casts_nothing);
    RUN_TEST(test_matches_brute_force);
    return test_summary();
}
