#include "core/game_clock.h"
#include "core/map.h"
#include "core/sky.h"
#include "core/structure.h"
#include "core/sun.h"
#include "core/tile_light.h"
#include "core/worldgen.h"

#include "test_framework.h"

#include <math.h>

#define MIDSUMMER SEASON_DAY(SEASON_SUMMER, 8)
#define MIDWINTER SEASON_DAY(SEASON_WINTER, 8)
/* In the period of 12:00 to 14:00: the sun of 13:00. */
#define SUMMER_NOON ((uint64_t)MIDSUMMER * TICKS_PER_DAY + 12 * TICKS_PER_HOUR)

static float floor_height(int z)
{
    return (float)z * 2.0f + SUN_RAY_LIFT;
}

/* Walls of one level around (x0, y0) .. (x1, y1), a door in the south wall
 * (closed), a roof over all of it. */
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

/* The rays cut short over everything in reach give what the whole rays
 * give, on generated worlds (relief, trees, caves). */
static void test_short_rays_change_nothing(void)
{
    static const uint64_t SEEDS[] = {42, 7};
    for (size_t s = 0; s < sizeof(SEEDS) / sizeof(SEEDS[0]); s++) {
        WorldGenParams params;
        worldgen_default_params(&params);
        params.seed = SEEDS[s];
        params.width = 64;
        params.height = 64;
        Map map;
        REQUIRE(worldgen_generate(&map, &params));
        TileLight light;
        REQUIRE(tile_light_init(&light, &map, SUMMER_NOON));
        SunPosition suns[] = {
            sun_position(SUN_LATITUDE_DEFAULT, MIDSUMMER, 13.0f),
            sun_position(SUN_LATITUDE_DEFAULT, MIDSUMMER, 7.0f),
            sun_position(SUN_LATITUDE_DEFAULT, MIDWINTER, 15.0f),
        };
        int surfaces = 0;
        for (size_t i = 0; i < light.count; i++) {
            if (!tile_light_has_surface(&map, i)) {
                CHECK_EQ_INT(light.open[i], 0);
                continue;
            }
            int x, y, z;
            map_tile_of(&map, i, &x, &y, &z);
            surfaces++;
            float px = (float)x + 0.3f, py = (float)y + 0.6f;
            float open = sky_open(&map, (float)x + 0.5f, (float)y + 0.5f, floor_height(z));
            CHECK_EQ_INT(light.open[i], (int)lroundf(open * 255.0f));
            for (size_t k = 0; k < sizeof(suns) / sizeof(suns[0]); k++) {
                CHECK_NEAR(tile_light_sun_lit(&light, &map, suns[k], px, py, floor_height(z)),
                           sun_lit(&map, suns[k], px, py, floor_height(z)), 1e-6);
            }
        }
        CHECK(surfaces > 64 * 64 * 3 / 4); /* Ground (not under trunks), some cave floors. */
        for (int ray = 0; ray < SKY_RAYS; ray++) {
            CHECK(light.sky_ahead_count[ray] > 0); /* Not too many blocks ahead. */
        }
        tile_light_free(&light);
        map_free(&map);
    }
}

/* The share of a tile in the sun is the mean of its nine points, with the
 * five first points standing for all of them where they agree. */
static void test_direct_is_the_mean_of_the_tile(void)
{
    WorldGenParams params;
    worldgen_default_params(&params);
    params.seed = 42;
    params.width = 64;
    params.height = 64;
    Map map;
    REQUIRE(worldgen_generate(&map, &params));
    TileLight light;
    uint64_t tick = (uint64_t)MIDSUMMER * TICKS_PER_DAY + 9 * TICKS_PER_HOUR;
    REQUIRE(tile_light_init(&light, &map, tick));
    SunPosition sun = sun_position(SUN_LATITUDE_DEFAULT, MIDSUMMER, 9.0f); /* 8:00 to 10:00. */
    int lit = 0, partly = 0, tiles = 0, off = 0;
    double error = 0.0;
    for (size_t i = 0; i < light.count; i++) {
        if (light.open[i] == 0) {
            CHECK_EQ_INT(light.direct[i], 0);
            continue;
        }
        int x, y, z;
        map_tile_of(&map, i, &x, &y, &z);
        float sum = 0.0f;
        for (int j = 0; j < TILE_LIGHT_SAMPLES; j++) {
            for (int k = 0; k < TILE_LIGHT_SAMPLES; k++) {
                sum += sun_lit(&map, sun, (float)x + ((float)k + 0.5f) / TILE_LIGHT_SAMPLES,
                               (float)y + ((float)j + 0.5f) / TILE_LIGHT_SAMPLES, floor_height(z));
            }
        }
        float mean = sum / (float)(TILE_LIGHT_SAMPLES * TILE_LIGHT_SAMPLES);
        float direct = (float)light.direct[i] / 255.0f;
        error += fabsf(direct - mean);
        off += fabsf(direct - mean) > 0.05f;
        tiles++;
        lit += light.direct[i] == 255;
        partly += light.direct[i] > 0 && light.direct[i] < 255;
    }
    REQUIRE(tiles > 0);
    CHECK(lit > tiles / 4);
    CHECK(partly > 0);
    CHECK(error / tiles < 0.01);
    CHECK(off * 50 < tiles); /* A thin shadow missed by the five points, rarely. */
    tile_light_free(&light);
    map_free(&map);
}

/* Open ground is in the sun and sees the sky; a closed hut neither; north
 * of a wall, at noon in summer, the shadow covers four fifths of the
 * tile (0.8 m of its metre: two of its three rows of points). */
static void test_ground_hut_and_wall(void)
{
    Map map;
    REQUIRE(map_init(&map, 64, 64, 6));
    build_hut(&map, 10, 10, 18, 18);
    for (int x = 30; x < 50; x++) {
        map_set_structure(&map, x, 40, 0, STRUCTURE_WALL);
    }
    TileLight light;
    REQUIRE(tile_light_init(&light, &map, SUMMER_NOON));
    CHECK_NEAR(tile_light_open(&light, 40, 50, 0), 1.0, 1e-6);
    CHECK_NEAR(tile_light_direct(&light, 40, 50, 0), 1.0, 1e-6);
    CHECK_NEAR(tile_light_open(&light, 14, 14, 0), 0.0, 1e-6);
    CHECK_NEAR(tile_light_direct(&light, 14, 14, 0), 0.0, 1e-6);
    CHECK(tile_light_open(&light, 14, 14, 1) > 0.95f);         /* The top of the roof. */
    CHECK_NEAR(tile_light_open(&light, 40, 40, 0), 0.0, 1e-6); /* A wall: no surface. */
    CHECK_NEAR(tile_light_open(&light, 40, 40, 1), 0.0, 1e-6); /* Nor its top (no floor). */
    CHECK_NEAR(tile_light_direct(&light, 40, 39, 0), 1.0 / 3.0, 1.0 / 255.0);
    CHECK_NEAR(tile_light_direct(&light, 40, 38, 0), 1.0, 1e-6);
    CHECK_NEAR(tile_light_direct(&light, 40, 41, 0), 1.0, 1e-6); /* South of it. */
    CHECK_NEAR(tile_light_open(&light, 40, 70, 0), 0.0, 1e-6);   /* Out of the map. */
    tile_light_free(&light);
    map_free(&map);
}

/* The light of the sky follows the map: the old values until the tiles
 * near a change are worked out again, within the budget of the ticks; then
 * nothing more to do. */
static void test_open_follows_the_map(void)
{
    Map map;
    REQUIRE(map_init(&map, 64, 64, 6));
    TileLight light;
    uint64_t tick = SUMMER_NOON;
    REQUIRE(tile_light_init(&light, &map, tick));
    CHECK(tile_light_open_settled(&light, &map));
    uint64_t rays = light.rays;
    tile_light_tick(&light, &map, ++tick);
    CHECK_EQ_UINT(light.rays, rays); /* Nothing changed: no work (this period done). */

    build_hut(&map, 20, 20, 28, 28);
    CHECK(!tile_light_open_settled(&light, &map));
    CHECK_NEAR(tile_light_open(&light, 24, 24, 0), 1.0, 1e-6);
    int ticks = 0;
    while (!tile_light_open_settled(&light, &map) && ticks < TICKS_PER_HOUR) {
        tile_light_tick(&light, &map, ++tick);
        ticks++;
        CHECK(light.rays - rays <=
              (uint64_t)ticks * TILE_LIGHT_OPEN_BUDGET / TILE_LIGHT_SURFACE_COST * SKY_RAYS);
    }
    CHECK(tile_light_open_settled(&light, &map));
    CHECK(ticks > 10); /* Spread over the ticks. */
    CHECK_NEAR(tile_light_open(&light, 24, 24, 0), 0.0, 1e-6);
    CHECK_NEAR(tile_light_open(&light, 24, 24, 1), sky_open(&map, 24.5f, 24.5f, floor_height(1)),
               1.0 / 255.0);
    CHECK_NEAR(tile_light_open(&light, 24, 30, 0), sky_open(&map, 24.5f, 30.5f, floor_height(0)),
               1.0 / 255.0);

    /* Its door kept open lets some sky in. */
    REQUIRE(map_set_door_kept_open(&map, 24, 28, 0, true));
    while (!tile_light_open_settled(&light, &map)) {
        tile_light_tick(&light, &map, ++tick);
    }
    CHECK(tile_light_open(&light, 24, 27, 0) > 0.05f);
    tile_light_free(&light);
    map_free(&map);
}

/* A change to the foliage alone is worked out slowly, a wall built meanwhile
 * first: the foliage waits. */
static void test_foliage_waits(void)
{
    Map map;
    REQUIRE(map_init(&map, 128, 64, 6)); /* The crowns out of reach of the hut. */
    TileLight light;
    uint64_t tick = SUMMER_NOON;
    REQUIRE(tile_light_init(&light, &map, tick));
    for (int y = 30; y < 36; y++) {
        for (int x = 100; x < 106; x++) {
            map_set_terrain(&map, x, y, 3, TERRAIN_FOLIAGE);
        }
    }
    CHECK(tile_light_open_firm(&light, &map));
    CHECK(!tile_light_open_settled(&light, &map));
    for (int i = 0; i < 1000; i++) {
        tile_light_tick(&light, &map, ++tick);
    }
    build_hut(&map, 5, 5, 11, 11);
    CHECK(!tile_light_open_firm(&light, &map));
    int ticks = 0;
    while (!tile_light_open_firm(&light, &map)) {
        tile_light_tick(&light, &map, ++tick);
        ticks++;
    }
    CHECK(ticks < TICKS_PER_HOUR);
    CHECK_NEAR(tile_light_open(&light, 8, 8, 0), 0.0, 1e-6);
    CHECK(!tile_light_open_settled(&light, &map));
    while (!tile_light_open_settled(&light, &map) && ticks < 4 * TICKS_PER_DAY) {
        tile_light_tick(&light, &map, ++tick);
        ticks++;
    }
    CHECK(tile_light_open_settled(&light, &map));
    CHECK(ticks > TICKS_PER_HOUR); /* Slowly. */
    CHECK_NEAR(tile_light_open(&light, 103, 33, 0), sky_open(&map, 103.5f, 33.5f, floor_height(0)),
               1.0 / 255.0);
    CHECK(tile_light_open(&light, 103, 33, 0) < 0.9f);
    tile_light_free(&light);
    map_free(&map);
}

/* Over a day the open ground adds up about the length of the day in sun
 * hours, kept as yesterday's at midnight; the inside of a hut none. */
static void test_sun_hours(void)
{
    Map map;
    REQUIRE(map_init(&map, 32, 32, 4));
    build_hut(&map, 4, 4, 10, 10);
    TileLight light;
    uint64_t tick = (uint64_t)MIDSUMMER * TICKS_PER_DAY;
    REQUIRE(tile_light_init(&light, &map, tick));
    CHECK_NEAR(tile_light_sun_hours(&light, 20, 20, 0, false), 0.0, 1e-6);
    CHECK(!tile_light_knows_yesterday(&light, tick));
    uint64_t noon = tick + 13 * TICKS_PER_HOUR;
    while (tick < noon) {
        tile_light_tick(&light, &map, ++tick);
    }
    float morning = tile_light_sun_hours(&light, 20, 20, 0, false);
    /* From 4:15 (1.75 h of the period from 4:00) to 12:00, and the period
     * from 12:00 to 14:00 already worked out for this tile. */
    CHECK_NEAR(morning, 9.8, 1e-5);
    uint64_t next = (uint64_t)(MIDSUMMER + 1) * TICKS_PER_DAY + TICKS_PER_HOUR;
    while (tick < next) {
        tile_light_tick(&light, &map, ++tick);
    }
    float day = game_day_length(MIDSUMMER);
    CHECK_NEAR(tile_light_sun_hours(&light, 20, 20, 0, true), day, 0.15);
    CHECK_NEAR(tile_light_sun_hours(&light, 20, 20, 0, false), 0.0, 1e-6); /* Night. */
    CHECK_NEAR(tile_light_sun_hours(&light, 7, 7, 0, true), 0.0, 1e-6);
    CHECK_NEAR(tile_light_direct(&light, 20, 20, 0), 0.0, 1e-6);
    CHECK(!tile_light_knows_yesterday(&light, tick)); /* Some tiles roll over until 2:00. */
    CHECK(tile_light_knows_yesterday(&light, tick + TICKS_PER_HOUR));
    tile_light_free(&light);
    map_free(&map);
}

/* The noon of another day, for the map as it is: on the day the light was
 * worked out, what `noon` holds; north of a wall, in the sun at midsummer's
 * noon and in its shadow at the spring's, when the sun is lower. */
static void test_noon_on_another_day(void)
{
    Map map;
    REQUIRE(map_init(&map, 64, 64, 6));
    for (int x = 20; x < 44; x++) {
        map_set_structure(&map, x, 30, 0, STRUCTURE_WALL);
    }
    TileLight light;
    REQUIRE(tile_light_init(&light, &map, SUMMER_NOON));
    size_t tiles[3] = {map_index(&map, 32, 31, 0), map_index(&map, 32, 29, 0),
                       map_index(&map, 32, 30, 0)};
    float midsummer[3], spring[3];
    tile_light_noon_on(&light, &map, MIDSUMMER, tiles, 3, midsummer);
    tile_light_noon_on(&light, &map, SEASON_DAY(SEASON_SPRING, 1), tiles, 3, spring);
    CHECK_NEAR(midsummer[0], tile_light_noon(&light, 32, 31, 0), 1.0 / 255.0); /* South of it. */
    CHECK_NEAR(midsummer[1], tile_light_noon(&light, 32, 29, 0), 1.0 / 255.0); /* North. */
    CHECK_NEAR(midsummer[0], 1.0, 1e-6);
    CHECK(spring[1] < midsummer[1]);  /* The shadow reaches further north. */
    CHECK_NEAR(spring[2], 0.0, 1e-6); /* The wall: no surface. */
    tile_light_free(&light);
    map_free(&map);
}

/* On a generated world (relief, crowns, shade), the noon of the day the light
 * was worked out is what `noon` holds, tile by tile: a tile all in the sun
 * at one noon sun is not worked out again for the other. */
static void test_noon_on_a_generated_world(void)
{
    WorldGenParams params;
    worldgen_default_params(&params);
    params.seed = 42;
    params.width = 64;
    params.height = 64;
    Map map;
    REQUIRE(worldgen_generate(&map, &params));
    TileLight light;
    REQUIRE(tile_light_init(&light, &map, SUMMER_NOON));
    size_t count = (size_t)map.width * (size_t)map.height;
    static size_t tiles[64 * 64];
    static float noon[64 * 64];
    for (int y = 0; y < map.height; y++) {
        for (int x = 0; x < map.width; x++) {
            tiles[(size_t)y * (size_t)map.width + (size_t)x] =
                map_index(&map, x, y, map_ground_level(&map, x, y));
        }
    }
    tile_light_noon_on(&light, &map, MIDSUMMER, tiles, count, noon);
    int full = 0, shaded = 0, wrong = 0;
    for (size_t i = 0; i < count; i++) {
        int x, y, z;
        map_tile_of(&map, tiles[i], &x, &y, &z);
        if (fabsf(noon[i] - tile_light_noon(&light, x, y, z)) > 1.0f / 255.0f) {
            wrong++;
        }
        full += noon[i] >= 1.0f;
        shaded += noon[i] > 0.0f && noon[i] < 1.0f;
    }
    CHECK_EQ_INT(wrong, 0);
    CHECK(full > 0);   /* In the open... */
    CHECK(shaded > 0); /* ...and under the crowns. */
    tile_light_free(&light);
    map_free(&map);
}

int main(void)
{
    RUN_TEST(test_short_rays_change_nothing);
    RUN_TEST(test_direct_is_the_mean_of_the_tile);
    RUN_TEST(test_ground_hut_and_wall);
    RUN_TEST(test_open_follows_the_map);
    RUN_TEST(test_foliage_waits);
    RUN_TEST(test_sun_hours);
    RUN_TEST(test_noon_on_another_day);
    RUN_TEST(test_noon_on_a_generated_world);
    return test_summary();
}
