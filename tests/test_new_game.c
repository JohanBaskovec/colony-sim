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
    CHECK_NEAR(params.world.water_level, 0.37, 1e-6);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_WATER), 37);
    new_game_option_set(&params, NEW_GAME_TREES, 42);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_TREES), 40);
    new_game_option_set(&params, NEW_GAME_WILDLIFE, 260);
    CHECK_EQ_INT(new_game_option_value(&params, NEW_GAME_WILDLIFE), 250);
    CHECK_NEAR(params.wildlife, 2.5 * GAME_WILDLIFE_DENSITY, 1e-6);
    new_game_option_set(&params, NEW_GAME_COLONISTS, 100);
    CHECK_EQ_INT(params.colonist_count, COLONIST_MAX);
    new_game_option_set(&params, NEW_GAME_UNDERGROUND, 1);
    CHECK_EQ_INT(params.world.underground_levels, 2);
    new_game_option_set(&params, NEW_GAME_HILL_STEP, 9);
    CHECK_NEAR(params.world.level_step, 0.09, 1e-6);
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

/* A flat world has no hills nor levels under the plains. */
static void test_flat_world_options(void)
{
    NewGameParams params;
    new_game_default_params(&params);
    for (int option = 0; option < NEW_GAME_OPTION_COUNT; option++) {
        CHECK(new_game_option_applies(&params, (NewGameOption)option));
    }
    params.world.flat = true;
    CHECK(!new_game_option_applies(&params, NEW_GAME_HILLS));
    CHECK(!new_game_option_applies(&params, NEW_GAME_HILL_STEP));
    CHECK(!new_game_option_applies(&params, NEW_GAME_UNDERGROUND));
    CHECK(new_game_option_applies(&params, NEW_GAME_WATER));
    CHECK(new_game_option_applies(&params, NEW_GAME_TREES));
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
    new_game_option_set(&params, NEW_GAME_HILL_STEP, 2);
    new_game_option_set(&params, NEW_GAME_UNDERGROUND, 16);
    new_game_option_set(&params, NEW_GAME_WATER, 0);
    Map map;
    REQUIRE(worldgen_generate(&map, &params.world));
    CHECK(map.depth <= MAP_MAX_LEVELS);
    map_free(&map);
    new_game_option_set(&params, NEW_GAME_WATER, 60);
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
    RUN_TEST(test_flat_world_options);
    RUN_TEST(test_seed_from_text);
    RUN_TEST(test_extreme_worlds_generate);
    return test_summary();
}
