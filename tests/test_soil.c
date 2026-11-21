/* Soil: the water in the ground (docs/CLIMATE.md §3), and the air's humidity (§4). */
#include "core/soil.h"
#include "core/weather.h"

#include "test_framework.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* A flat map of soil, 16 x 16, open space above. */
static void flat_map(Map *map, int depth)
{
    REQUIRE(map_init(map, 16, 16, depth));
}

/* `hours` of the same weather over the whole map. */
static void run_hours(Soil *soil, const Map *map, const SoilHour *hour, int hours)
{
    for (int h = 0; h < hours; h++) {
        soil_update(soil, map, 0, map_level_tile_count(map), hour, NULL);
    }
}

static float water_at(const Soil *soil, const Map *map, int x, int y)
{
    return soil->water[map_index(map, x, y, 0)];
}

static void test_buckets_by_terrain(void)
{
    static const Terrain GROUND[] = {TERRAIN_SAND, TERRAIN_GRAVEL, TERRAIN_SOIL, TERRAIN_RICH_SOIL};
    for (size_t i = 0; i < sizeof(GROUND) / sizeof(GROUND[0]); i++) {
        const SoilDef *def = soil_def(GROUND[i]);
        REQUIRE(def != NULL);
        CHECK(def->wilting > 0.0f && def->wilting < def->capacity);
        CHECK(def->capacity < def->saturation);
        CHECK(def->drainage > 0.0f && def->drainage < 1.0f);
    }
    CHECK(soil_def(TERRAIN_SILT) != NULL); /* A lake's bed, once dry. */
    CHECK(soil_def(TERRAIN_OPEN) == NULL);
    CHECK(soil_def(TERRAIN_ROUGH_STONE) == NULL);
    CHECK(soil_def(TERRAIN_FOLIAGE) == NULL);
    CHECK(soil_def(TERRAIN_SAND)->capacity < soil_def(TERRAIN_SOIL)->capacity);
    CHECK(soil_def(TERRAIN_SAND)->drainage > soil_def(TERRAIN_SOIL)->drainage);
}

/* A storm fills the ground, up to saturation at most; drainage brings it
 * back to field capacity, sand much faster than soil. */
static void test_rain_fills_and_drains(void)
{
    Map map;
    flat_map(&map, 2);
    map_set_terrain(&map, 3, 3, 0, TERRAIN_SAND);
    Soil soil;
    REQUIRE(soil_init(&soil, &map, 0.0f));
    const SoilDef *loam = soil_def(TERRAIN_SOIL), *sand = soil_def(TERRAIN_SAND);
    CHECK_NEAR(water_at(&soil, &map, 8, 8), loam->wilting, 1e-3);
    SoilHour storm = {.rain = 1.2f};
    run_hours(&soil, &map, &storm, 24 * 10);
    CHECK(water_at(&soil, &map, 8, 8) > loam->capacity);
    CHECK(water_at(&soil, &map, 8, 8) <= loam->saturation + 1e-3);
    CHECK(soil_moisture(&soil, &map, 8, 8, 0) > 1.0f);
    SoilHour still = {0};
    run_hours(&soil, &map, &still, 24);
    float sand_left = water_at(&soil, &map, 3, 3) - sand->capacity;
    float loam_left = water_at(&soil, &map, 8, 8) - loam->capacity;
    CHECK(sand_left < 0.5f);
    CHECK(loam_left > 5.0f * sand_left);
    CHECK_NEAR(soil_moisture(&soil, &map, 0, 0, 1), -1.0, 1e-6); /* Open space. */
    soil_free(&soil);
    map_free(&map);
}

/* The sun dries the ground through the plants on it, and less once it is
 * dry; bare ground dries from its top only. */
static void test_sun_dries_the_ground(void)
{
    Map map;
    flat_map(&map, 2);
    map.plants[map_index(&map, 4, 4, 0)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    Soil soil;
    REQUIRE(soil_init(&soil, &map, 1.0f));
    float pet = soil_potential_evaporation(1.0f, 1.0f, 20.0f, 0.5f);
    CHECK(pet > 0.4f && pet < 0.8f);
    CHECK(soil_potential_evaporation(0.0f, 1.0f, 20.0f, 0.5f) < 0.05f);  /* Night. */
    CHECK(soil_potential_evaporation(1.0f, 1.0f, -10.0f, 0.5f) < 0.05f); /* Frozen. */
    SoilHour sun = {.pet = pet};
    run_hours(&soil, &map, &sun, 10);
    const SoilDef *loam = soil_def(TERRAIN_SOIL);
    float grass = water_at(&soil, &map, 4, 4), bare = water_at(&soil, &map, 8, 8);
    CHECK_NEAR(loam->capacity - grass, 10.0f * pet, 0.5f); /* All the sun, through the grass. */
    CHECK(bare > grass);
    CHECK(soil.evap[map_index(&map, 4, 4, 0)] > 240);
    /* Once dry, it hardly loses any more. */
    run_hours(&soil, &map, &sun, 24 * 30);
    float dry = water_at(&soil, &map, 4, 4);
    CHECK(soil_moisture(&soil, &map, 4, 4, 0) < SOIL_DRY_BELOW);
    run_hours(&soil, &map, &sun, 10);
    CHECK(dry - water_at(&soil, &map, 4, 4) < 10.0f * pet * 0.2f);
    CHECK(water_at(&soil, &map, 4, 4) > loam->wilting - 1e-3);
    soil_free(&soil);
    map_free(&map);
}

/* Crowns hold part of the rain, a roof all of it; under a roof the ground
 * hardly dries. */
static void test_crowns_and_roofs_keep_rain_off(void)
{
    Map map;
    flat_map(&map, 3);
    map_set_terrain(&map, 4, 4, 1, TERRAIN_FOLIAGE);
    map_set_terrain(&map, 4, 4, 2, TERRAIN_FOLIAGE);
    map_set_rock(&map, 10, 10, 1, ROCK_GRANITE); /* A cave's ceiling. */
    Soil soil;
    REQUIRE(soil_init(&soil, &map, 0.0f));
    SoilHour rain = {.rain = 0.25f};
    run_hours(&soil, &map, &rain, 20);
    float open = water_at(&soil, &map, 8, 8), wood = water_at(&soil, &map, 4, 4);
    float cave = water_at(&soil, &map, 10, 10);
    const SoilDef *loam = soil_def(TERRAIN_SOIL);
    CHECK_NEAR(open - loam->wilting, 5.0f, 1e-2);
    CHECK(wood - loam->wilting < 0.75f * (open - loam->wilting));
    CHECK(wood - loam->wilting > 0.5f * (open - loam->wilting));
    CHECK_NEAR(cave, loam->wilting, 1e-3);
    CHECK(soil.canopy[map_index(&map, 4, 4, 0)] < 40);
    CHECK_EQ_INT(soil.canopy[map_index(&map, 8, 8, 0)], 255);
    soil_free(&soil);
    map_free(&map);
}

/* One moisture scale for every terrain: 0 at the wilting point, 1 at field
 * capacity, 2 saturated; open water is standing water, a trunk has the
 * moisture of the ground its roots are in; ground opened since the start is
 * unknown until its first hour, then as damp as fresh earth. */
static void test_moisture_scale(void)
{
    static const Terrain GROUND[] = {TERRAIN_SAND, TERRAIN_GRAVEL, TERRAIN_SOIL, TERRAIN_RICH_SOIL};
    for (size_t i = 0; i < sizeof(GROUND) / sizeof(GROUND[0]); i++) {
        const SoilDef *def = soil_def(GROUND[i]);
        CHECK_NEAR(soil_moisture_of(def, def->wilting), 0.0, 1e-5);
        CHECK_NEAR(soil_moisture_of(def, def->capacity), 1.0, 1e-5);
        CHECK_NEAR(soil_moisture_of(def, def->saturation), SOIL_MOISTURE_SATURATED, 1e-5);
        for (float m = -0.2f; m <= 2.0f; m += 0.3f) {
            CHECK_NEAR(soil_moisture_of(def, soil_water_for(def, m)), m, 1e-4);
        }
    }
    Map map;
    flat_map(&map, 3);
    map_set_water(&map, 0, 0, 0, WATER_SHALLOWS_MM);
    map.rock[map_index(&map, 8, 8, 0)] = ROCK_WOOD; /* A trunk, two levels high. */
    map.rock[map_index(&map, 8, 8, 1)] = ROCK_WOOD;
    map_set_rock(&map, 12, 12, 0, ROCK_GRANITE);
    Soil soil;
    REQUIRE(soil_init(&soil, &map, 0.5f));
    CHECK_NEAR(soil_moisture(&soil, &map, 0, 0, 0), PLANT_MOISTURE_STANDING, 1e-6);
    CHECK_NEAR(soil_moisture(&soil, &map, 8, 8, 1), 0.5, 1e-4);
    CHECK(soil_moisture_key(&soil, &map, 8, 8, 0) == NULL);
    CHECK(soil_moisture_key(&soil, &map, 0, 0, 0) == NULL);
    /* The rock dug out: unknown, then fresh earth. */
    map_set_rock(&map, 12, 12, 0, ROCK_NONE);
    CHECK_NEAR(soil_moisture(&soil, &map, 12, 12, 0), -1.0, 1e-6);
    SoilHour still = {0};
    run_hours(&soil, &map, &still, 1);
    CHECK_NEAR(soil_moisture(&soil, &map, 12, 12, 0), 1.0, 0.01);
    soil_free(&soil);
    map_free(&map);
}

/* Near open water the ground stays wet, whatever the sun does; farther
 * away it dries. */
static void test_banks_stay_wet(void)
{
    Map map;
    flat_map(&map, 2);
    for (int y = 0; y < 16; y++) {
        map_set_water(&map, 0, y, 0, WATER_SHALLOWS_MM);
    }
    Soil soil;
    REQUIRE(soil_init(&soil, &map, 0.0f));
    CHECK_EQ_INT(soil.wet[map_index(&map, 0, 5, 0)], 0);
    CHECK_EQ_INT(soil.wet[map_index(&map, 1, 5, 0)], 1);
    CHECK_EQ_INT(soil.wet[map_index(&map, 4, 5, 0)], 4);
    CHECK_EQ_INT(soil.wet[map_index(&map, 12, 5, 0)], SOIL_WET_FAR);
    for (int x = 1; x < 16; x++) {
        map.plants[map_index(&map, x, 5, 0)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    }
    SoilHour sun = {.pet = 0.5f};
    run_hours(&soil, &map, &sun, 24 * 10);
    /* Beside the water the water table holds it near saturation, which
     * drainage does not go below; a tile farther has a lower table. */
    CHECK(soil_moisture(&soil, &map, 1, 5, 0) > 1.8f);
    CHECK(soil_moisture(&soil, &map, 2, 5, 0) > 1.3f);
    CHECK(soil_moisture(&soil, &map, 2, 5, 0) < 1.6f);
    CHECK(soil_moisture(&soil, &map, 3, 5, 0) > 0.6f);
    CHECK(soil_moisture(&soil, &map, 12, 5, 0) < 0.1f);
    /* Drained: the bank moves with the water. */
    for (int y = 0; y < 16; y++) {
        map_set_water(&map, 0, y, 0, 0);
    }
    soil_refresh_banks(&soil, &map);
    CHECK_EQ_INT(soil.wet[map_index(&map, 1, 5, 0)], SOIL_WET_FAR);
    soil_free(&soil);
    map_free(&map);
}

/* Snow waits on the ground and melts into it above 0 °C. */
static void test_snow_melts_into_the_ground(void)
{
    Map map;
    flat_map(&map, 2);
    Soil soil;
    REQUIRE(soil_init(&soil, &map, 0.0f));
    CHECK_NEAR(soil_snow_hour(&soil, 1.0f, -5.0f, false), 0.0, 1e-6);
    CHECK_NEAR(soil.snowpack, 1.0, 1e-6);
    float melt = soil_snow_hour(&soil, 0.0f, 5.0f, false);
    CHECK_NEAR(melt, 5.0f * SOIL_MELT_PER_DEGREE, 1e-5);
    CHECK(soil_snow_hour(&soil, 0.0f, 50.0f, true) <= 1.0f); /* Never more than lies there. */
    CHECK_NEAR(soil.snowpack, 0.0, 1e-6);
    SoilHour thaw = {.melt = 0.5f};
    run_hours(&soil, &map, &thaw, 4);
    CHECK_NEAR(water_at(&soil, &map, 5, 5) - soil_def(TERRAIN_SOIL)->wilting, 2.0, 1e-3);
    soil_free(&soil);
    map_free(&map);
}

/* Dew forms on cold open ground at night. */
static void test_dew_at_night(void)
{
    Map map;
    flat_map(&map, 2);
    Soil soil;
    REQUIRE(soil_init(&soil, &map, 0.0f));
    float *cold = calloc(map_tile_count(&map), sizeof(float));
    REQUIRE(cold != NULL);
    for (size_t i = 0; i < map_tile_count(&map); i++) {
        cold[i] = 2.0f;
    }
    SoilHour night = {.night = true, .dew_point = 4.0f};
    soil_update(&soil, &map, 0, map_level_tile_count(&map), &night, cold);
    CHECK_NEAR(water_at(&soil, &map, 5, 5) - soil_def(TERRAIN_SOIL)->wilting, SOIL_DEW_MM, 1e-5);
    night.dew_point = 0.0f;
    soil_update(&soil, &map, 0, map_level_tile_count(&map), &night, cold);
    CHECK_NEAR(water_at(&soil, &map, 5, 5) - soil_def(TERRAIN_SOIL)->wilting, SOIL_DEW_MM, 1e-5);
    free(cold);
    soil_free(&soil);
    map_free(&map);
}

/* The air: saturated at the dew point, drier the warmer. */
/* A crown transpires what its roots draw from the ground under it: its
 * foliage turns the sun into vapour while that ground is moist, not once it
 * is dry, nor when the crown is bare (then it is as the ground under it). */
static void test_crowns_transpire_from_the_ground(void)
{
    Map map;
    flat_map(&map, 4);
    /* Foliage two levels up over (4, 4) and (10, 10), the trunk under the
     * first: the roots are under the crown either way. */
    map_set_terrain(&map, 4, 4, 2, TERRAIN_FOLIAGE);
    map_set_terrain(&map, 10, 10, 2, TERRAIN_FOLIAGE_SPARSE);
    map.rock[map_index(&map, 4, 4, 1)] = ROCK_WOOD;
    map.rock[map_index(&map, 4, 4, 0)] = ROCK_WOOD;
    Soil soil;
    REQUIRE(soil_init(&soil, &map, 1.0f));
    SoilHour sun = {.pet = 0.5f, .look_up = true};
    size_t count = map_tile_count(&map);
    size_t crown = map_index(&map, 4, 4, 2), edge = map_index(&map, 10, 10, 2);
    soil_update(&soil, &map, 0, count, &sun, NULL);
    CHECK(soil.canopy[crown] == 255); /* Nothing over the top of the crown. */
    CHECK(soil.evap[crown] > 240);
    CHECK(soil.evap[edge] > 240);
    /* The ground dried to the wilting point: the crown closes its pores. */
    const SoilDef *loam = soil_def(TERRAIN_SOIL);
    for (int y = 0; y < map.height; y++) {
        for (int x = 0; x < map.width; x++) {
            soil.water[map_index(&map, x, y, 0)] = loam->wilting;
        }
    }
    soil_update(&soil, &map, 0, count, &sun, NULL);
    CHECK(soil.evap[crown] < 10);
    CHECK(soil.evap[edge] < 10);
    /* Moist again but the crown bare: it no longer transpires, its sun goes
     * to the ground under it, which evaporates as bare ground does. */
    for (int y = 0; y < map.height; y++) {
        for (int x = 0; x < map.width; x++) {
            soil.water[map_index(&map, x, y, 0)] = loam->capacity;
        }
    }
    map_set_bare(&map, MAP_BARE_STEPS);
    soil_update(&soil, &map, 0, count, &sun, NULL);
    uint8_t ground = soil.evap[map_index(&map, 10, 10, 0)];
    CHECK(ground < 200);
    CHECK(abs((int)soil.evap[edge] - (int)ground) <= 1);
    soil_free(&soil);
    map_free(&map);
}

static void test_air_humidity(void)
{
    CHECK_NEAR(air_relative_humidity(10.0f, 10.0f), 1.0, 1e-6);
    CHECK_NEAR(air_relative_humidity(10.0f, 5.0f), 1.0, 1e-6);
    float afternoon = air_relative_humidity(10.0f, 20.0f);
    CHECK(afternoon > 0.45f && afternoon < 0.6f); /* About 52 %. */
    CHECK(air_relative_humidity(10.0f, 25.0f) < afternoon);
    Weather weather;
    uint64_t tick = (uint64_t)SEASON_DAY(SEASON_SUMMER, 8) * TICKS_PER_DAY + 15 * TICKS_PER_HOUR;
    weather_init(&weather, 3, tick);
    weather.type = WEATHER_RAIN;
    CHECK_NEAR(weather_dew_point(&weather, tick), weather_outdoor(&weather, tick), 1e-4);
    CHECK(weather_rainfall(&weather, tick) > 0.0f);
    CHECK_NEAR(weather_snowfall(&weather, tick), 0.0, 1e-6);
    weather.type = WEATHER_CLEAR;
    CHECK(weather_dew_point(&weather, tick) < weather_outdoor(&weather, tick) - 3.0f);
    CHECK_NEAR(weather_rainfall(&weather, tick), 0.0, 1e-6);
}

static void test_hash_follows_the_water(void)
{
    Map map;
    flat_map(&map, 2);
    Soil soil;
    REQUIRE(soil_init(&soil, &map, 0.5f));
    uint64_t before = soil_hash(&soil, 1);
    SoilHour rain = {.rain = 0.25f};
    run_hours(&soil, &map, &rain, 1);
    CHECK(soil_hash(&soil, 1) != before);
    soil_free(&soil);
    map_free(&map);
}

/* The words of the inspector, from dry to soaked. */
static void test_moisture_in_words(void)
{
    Map map;
    flat_map(&map, 2);
    map_set_terrain(&map, 1, 1, 0, TERRAIN_ROUGH_STONE);
    Soil soil;
    REQUIRE(soil_init(&soil, &map, 0.0f));
    const SoilDef *def = soil_def(TERRAIN_SOIL);
    size_t i = map_index(&map, 5, 5, 0);
    const float useful = def->capacity - def->wilting;
    const struct {
        float water;
        const char *key;
    } CASES[] = {
        {def->wilting, "soil.dry"},
        {def->wilting + 0.3f * useful, "soil.fairly_dry"},
        {def->capacity, "soil.moist"},
        {def->capacity + 0.3f * (def->saturation - def->capacity), "soil.wet"},
        {def->saturation, "soil.soaked"},
    };
    for (size_t c = 0; c < sizeof(CASES) / sizeof(CASES[0]); c++) {
        soil.water[i] = CASES[c].water;
        const char *key = soil_moisture_key(&soil, &map, 5, 5, 0);
        REQUIRE(key != NULL);
        CHECK(strcmp(key, CASES[c].key) == 0);
    }
    CHECK(soil_moisture_key(&soil, &map, 1, 1, 0) == NULL); /* Stone. */
    CHECK(soil_moisture_key(&soil, &map, 1, 1, 1) == NULL); /* Open space. */
    soil_free(&soil);
    map_free(&map);
}

int main(void)
{
    RUN_TEST(test_buckets_by_terrain);
    RUN_TEST(test_rain_fills_and_drains);
    RUN_TEST(test_sun_dries_the_ground);
    RUN_TEST(test_crowns_and_roofs_keep_rain_off);
    RUN_TEST(test_crowns_transpire_from_the_ground);
    RUN_TEST(test_moisture_scale);
    RUN_TEST(test_banks_stay_wet);
    RUN_TEST(test_snow_melts_into_the_ground);
    RUN_TEST(test_dew_at_night);
    RUN_TEST(test_air_humidity);
    RUN_TEST(test_moisture_in_words);
    RUN_TEST(test_hash_follows_the_water);
    return test_summary();
}
