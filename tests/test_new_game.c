#include "core/new_game.h"
#include "core/worldgen.h"

#include "test_framework.h"

#include <string.h>

/* Every option: a key, a range holding its default on a step, in the defaults
 * of a game as played (worldgen_default_params(), wild animals at their
 * usual number). */
static void test_defaults_are_on_the_steps(void)
{
    NewGameParams params;
    new_game_default_params(&params);
    CHECK_NEAR(params.wildlife, GAME_WILDLIFE_DENSITY, 1e-6);
    WorldGenParams world;
    worldgen_default_params(&world);
    CHECK_EQ_INT(params.world.width, world.width);
    CHECK_NEAR(params.world.water_level, world.water_level, 1e-6);
    for (int option = 0; option < NEW_GAME_OPTION_COUNT; option++) {
        const NewGameOptionDef *def = new_game_option_def((NewGameOption)option);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL && def->name_key != NULL && def->note_key != NULL);
        CHECK(new_game_section_key(def->section) != NULL);
        CHECK(def->min < def->max && def->step > 0);
        int value = new_game_option_value(&params, (NewGameOption)option);
        CHECK(value >= def->min && value <= def->max);
        CHECK_EQ_INT((value - def->min) % def->step, 0);
        /* Setting it again changes nothing. */
        NewGameParams copy = params;
        new_game_option_set(&copy, (NewGameOption)option, value);
        CHECK(memcmp(&copy, &params, sizeof(copy)) == 0);
    }
    CHECK(new_game_option_def(NEW_GAME_OPTION_COUNT) == NULL);
}

/* What is set is read back, within the range and on a step. */
static void test_set_clamps_and_steps(void)
{
    NewGameParams params;
    new_game_default_params(&params);
    new_game_option_set(&params, NEW_GAME_WIDTH, 131);
    CHECK_EQ_INT(params.world.width, 128); /* Steps of 8 from 64. */
    new_game_option_set(&params, NEW_GAME_WIDTH, 5000);
    CHECK_EQ_INT(params.world.width, MAP_MAX_SIZE);
    new_game_option_set(&params, NEW_GAME_HEIGHT, 0);
    CHECK_EQ_INT(params.world.height, 64);
    new_game_option_set(&params, NEW_GAME_WATER, 37);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_WATER), 35); /* Steps of 5. */
    CHECK_NEAR(params.world.water_level, 0.21, 1e-6);                 /* 35 % of 0.6. */
    new_game_option_set(&params, NEW_GAME_WATER, 100);
    CHECK_NEAR(params.world.water_level, NEW_GAME_WATER_MAX_ELEVATION / 100.0, 1e-6);
    new_game_option_set(&params, NEW_GAME_RELIEF, 120);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_RELIEF), 100);
    new_game_option_set(&params, NEW_GAME_TREES, 42);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_TREES), 40);
    new_game_option_set(&params, NEW_GAME_WILDLIFE, 260);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_WILDLIFE), 250);
    CHECK_NEAR(params.wildlife, 2.5 * GAME_WILDLIFE_DENSITY, 1e-6);
    new_game_option_set(&params, NEW_GAME_COLONISTS, 100);
    CHECK_EQ_INT(params.colonist_count, COLONIST_MAX);
    new_game_option_set(&params, NEW_GAME_UNDERGROUND, 1);
    CHECK_EQ_INT(params.world.underground_levels, 2);
    new_game_option_set(&params, NEW_GAME_UNDERGROUND, 16);
    CHECK_EQ_INT(params.world.underground_levels, WORLDGEN_MAX_UNDERGROUND);
    new_game_option_set(&params, NEW_GAME_HILL_HEIGHT, 9);
    CHECK_EQ_INT(params.world.hill_height, 9);
    new_game_option_set(&params, NEW_GAME_HILL_HEIGHT, 99);
    CHECK_EQ_INT(params.world.hill_height, WORLDGEN_MAX_HILL_HEIGHT);
    new_game_option_set(&params, NEW_GAME_FEATURE_SCALE, 70);
    CHECK_NEAR(params.world.feature_scale, 72.0, 1e-6);
}

/* The rock never begins below the hills: setting one moves the other. */
static void test_hills_and_rock_keep_their_order(void)
{
    NewGameParams params;
    new_game_default_params(&params);
    new_game_option_set(&params, NEW_GAME_HILLS, 85);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_HILLS), 85);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_ROCK), 85);
    new_game_option_set(&params, NEW_GAME_ROCK, 40);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_ROCK), 40);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_HILLS), 40);
    /* Apart, neither moves the other. */
    new_game_option_set(&params, NEW_GAME_ROCK, 90);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_HILLS), 40);
    new_game_option_set(&params, NEW_GAME_HILLS, 60);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_ROCK), 90);
}

/* The usual world is at 50 % of relief and 40 % of water. */
static void test_usual_relief_and_water(void)
{
    NewGameParams params;
    new_game_default_params(&params);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_RELIEF), NEW_GAME_RELIEF_USUAL);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_WATER), 40);
    for (int option = 0; option < NEW_GAME_OPTION_COUNT; option++) {
        CHECK(new_game_option_applies(&params, (NewGameOption)option));
    }
    /* Away and back: the usual world exactly. */
    NewGameParams usual = params;
    new_game_option_set(&params, NEW_GAME_RELIEF, 0);
    new_game_option_set(&params, NEW_GAME_WATER, 0);
    new_game_option_set(&params, NEW_GAME_RELIEF, NEW_GAME_RELIEF_USUAL);
    new_game_option_set(&params, NEW_GAME_WATER, 40);
    CHECK(memcmp(&params, &usual, sizeof(params)) == 0);
}

/* The relief sets the hills and the rock (the advanced options), and is read
 * back from where the hills begin, whatever sets it. */
static void test_relief_sets_the_advanced_options(void)
{
    NewGameParams params;
    new_game_default_params(&params);
    new_game_option_set(&params, NEW_GAME_RELIEF, 0);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_HILLS), 100);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_ROCK), 100);
    CHECK(!new_game_option_applies(&params, NEW_GAME_HILL_HEIGHT)); /* No hills. */
    new_game_option_set(&params, NEW_GAME_RELIEF, 100);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_HILLS), 35);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_ROCK), 60);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_HILL_HEIGHT), WORLDGEN_MAX_HILL_HEIGHT);
    int previous = -1;
    for (int relief = 0; relief <= 100; relief++) { /* Read back as set, ever higher. */
        new_game_option_set(&params, NEW_GAME_RELIEF, relief);
        CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_RELIEF), relief);
        int hills = new_game_option_value(&params, NEW_GAME_HILLS);
        CHECK(hills <= new_game_option_value(&params, NEW_GAME_ROCK));
        CHECK(previous < 0 || hills <= previous);
        previous = hills;
    }
    new_game_option_set(&params, NEW_GAME_RELIEF, 25);
    CHECK(new_game_option_value(&params, NEW_GAME_HILL_HEIGHT) < WORLDGEN_HILL_HEIGHT);
    /* An advanced option moves the relief. */
    new_game_option_set(&params, NEW_GAME_HILLS, 55);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_RELIEF), NEW_GAME_RELIEF_USUAL);
    /* The flat world of the tests has no relief to set. */
    params.world.flat = true;
    CHECK(!new_game_option_applies(&params, NEW_GAME_RELIEF));
    CHECK(!new_game_option_applies(&params, NEW_GAME_HILLS));
    CHECK(!new_game_option_applies(&params, NEW_GAME_UNDERGROUND));
    CHECK(new_game_option_applies(&params, NEW_GAME_WATER));
    CHECK(new_game_option_applies(&params, NEW_GAME_TREES));
}

/* The sandy tiles of the plains' level. */
static size_t count_sand(const Map *map)
{
    size_t sand = 0;
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            sand += map->terrain[map_index(map, x, y, map->surface_level)] == TERRAIN_SAND;
        }
    }
    return sand;
}

/* Without relief, water nor river, the ground is all flat on the plains'
 * level, all dry, without beaches (sandy ground stays where it is dry) nor
 * rock outcrops; with water, the lakes lie in hollows and the rest stays
 * flat. */
static void test_a_world_without_relief_is_flat(void)
{
    NewGameParams params;
    new_game_default_params(&params);
    params.world.seed = 4;
    new_game_option_set(&params, NEW_GAME_WIDTH, 64);
    new_game_option_set(&params, NEW_GAME_HEIGHT, 64);
    new_game_option_set(&params, NEW_GAME_RELIEF, 0);
    new_game_option_set(&params, NEW_GAME_WATER, 0);
    new_game_option_set(&params, NEW_GAME_RIVER, 0);
    Map map;
    REQUIRE(worldgen_generate(&map, &params.world));
    CHECK_EQ_INT(map.top_ground_level, map.surface_level);
    size_t off_level = 0, wet = 0, rock = 0;
    for (int y = 0; y < map.height; y++) {
        for (int x = 0; x < map.width; x++) {
            int z = map_ground_level(&map, x, y);
            size_t index = map_index(&map, x, y, map.surface_level);
            off_level += z != map.surface_level;
            wet += map.water[index] > 0;
            rock += map.terrain[index] == TERRAIN_ROUGH_STONE;
        }
    }
    CHECK_EQ_UINT(off_level, 0);
    CHECK_EQ_UINT(wet, 0);
    CHECK_EQ_UINT(rock, 0);
    CHECK_EQ_INT(map.slope_count, 0);
    size_t dry_sand = count_sand(&map);
    map_free(&map);
    /* A trace of water brings beaches along the lowest ground. */
    params.world.water_level = 0.0001f;
    REQUIRE(worldgen_generate(&map, &params.world));
    CHECK(count_sand(&map) > dry_sand);
    map_free(&map);

    new_game_option_set(&params, NEW_GAME_WATER, 60);
    REQUIRE(worldgen_generate(&map, &params.world));
    size_t above = 0, hollows = 0;
    for (int y = 0; y < map.height; y++) {
        for (int x = 0; x < map.width; x++) {
            int z = map_ground_level(&map, x, y);
            above += z > map.surface_level;
            hollows += z < map.surface_level;
        }
    }
    CHECK_EQ_UINT(above, 0);
    CHECK(hollows > 0);
    map_free(&map);
}

/* The river: a game as played has one, set in tiles across on steps of a
 * stream's width, 0 for none (its label says so); on a plain without water
 * it lies in its hollow and the rest stays flat. */
static void test_the_river_option(void)
{
    NewGameParams params;
    new_game_default_params(&params);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_RIVER), NEW_GAME_RIVER_USUAL);
    new_game_option_set(&params, NEW_GAME_RIVER, 7);
    CHECK_EQ_INT(params.world.river.width, 6);
    new_game_option_set(&params, NEW_GAME_RIVER, 100);
    CHECK_EQ_INT(params.world.river.width, RIVER_MAX_WIDTH);
    new_game_option_set(&params, NEW_GAME_RIVER, 0);
    CHECK_EQ_INT(params.world.river.width, 0);
    CHECK(new_game_option_def(NEW_GAME_RIVER)->zero_key != NULL);
    CHECK(new_game_option_applies(&params, NEW_GAME_RIVER));

    params.world.seed = 4;
    new_game_option_set(&params, NEW_GAME_WIDTH, 64);
    new_game_option_set(&params, NEW_GAME_HEIGHT, 64);
    new_game_option_set(&params, NEW_GAME_RELIEF, 0);
    new_game_option_set(&params, NEW_GAME_WATER, 0);
    new_game_option_set(&params, NEW_GAME_RIVER, 6);
    Map map;
    REQUIRE(worldgen_generate(&map, &params.world));
    size_t above = 0, hollows = 0;
    for (int y = 0; y < map.height; y++) {
        for (int x = 0; x < map.width; x++) {
            int z = map_ground_level(&map, x, y);
            above += z > map.surface_level;
            hollows += z < map.surface_level;
        }
    }
    CHECK_EQ_UINT(above, 0);
    CHECK(hollows > 64 * 4); /* Across the map, about six tiles wide. */
    map_free(&map);
}

static void test_seed_from_text(void)
{
    uint64_t seed = 7;
    CHECK(!new_game_seed_from_text("", &seed));
    CHECK(!new_game_seed_from_text("   ", &seed));
    CHECK(!new_game_seed_from_text(NULL, &seed));
    CHECK_EQ_UINT(seed, 7); /* Untouched. */
    CHECK(new_game_seed_from_text("42", &seed));
    CHECK_EQ_UINT(seed, 42);
    CHECK(new_game_seed_from_text("  42 ", &seed));
    CHECK_EQ_UINT(seed, 42);
    CHECK(new_game_seed_from_text("18446744073709551615", &seed));
    CHECK(seed == UINT64_MAX);
    /* Too large for a number, or not one: a hash, the same for the same text. */
    uint64_t big = 0, word = 0, again = 0, other = 0;
    CHECK(new_game_seed_from_text("18446744073709551616", &big));
    CHECK(big != 0 && big != UINT64_MAX);
    CHECK(new_game_seed_from_text("Thawmoor", &word));
    CHECK(new_game_seed_from_text(" Thawmoor ", &again));
    CHECK(new_game_seed_from_text("thawmoor", &other));
    CHECK(word == again);
    CHECK(word != other);
    uint64_t number_word = 0;
    CHECK(new_game_seed_from_text("42a", &number_word));
    CHECK(number_word != 42);
}

/* A world made with options at their ends is generated. */
static void test_extreme_worlds_generate(void)
{
    NewGameParams params;
    new_game_default_params(&params);
    params.world.seed = 3;
    new_game_option_set(&params, NEW_GAME_WIDTH, 64);
    new_game_option_set(&params, NEW_GAME_HEIGHT, 64);
    new_game_option_set(&params, NEW_GAME_HILLS, 30);
    new_game_option_set(&params, NEW_GAME_HILL_HEIGHT, 2);
    new_game_option_set(&params, NEW_GAME_UNDERGROUND, 16);
    new_game_option_set(&params, NEW_GAME_WATER, 0);
    Map map;
    REQUIRE(worldgen_generate(&map, &params.world));
    CHECK(map.depth <= MAP_MAX_LEVELS);
    map_free(&map);
    new_game_option_set(&params, NEW_GAME_WATER, 100);
    new_game_option_set(&params, NEW_GAME_ROCK, 100);
    new_game_option_set(&params, NEW_GAME_TREES, 100);
    params.world.flat = true;
    REQUIRE(worldgen_generate(&map, &params.world));
    CHECK_EQ_INT(map.surface_level, 0);
    map_free(&map);
}

int main(void)
{
    RUN_TEST(test_defaults_are_on_the_steps);
    RUN_TEST(test_set_clamps_and_steps);
    RUN_TEST(test_hills_and_rock_keep_their_order);
    RUN_TEST(test_usual_relief_and_water);
    RUN_TEST(test_relief_sets_the_advanced_options);
    RUN_TEST(test_a_world_without_relief_is_flat);
    RUN_TEST(test_the_river_option);
    RUN_TEST(test_seed_from_text);
    RUN_TEST(test_extreme_worlds_generate);
    return test_summary();
}
