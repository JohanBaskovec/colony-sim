#include "core/cli.h"

#include "core/scenario.h"

#include "test_framework.h"

#define ARGC(array) ((int)(sizeof(array) / sizeof((array)[0])))

static void test_no_arguments_gives_defaults(void)
{
    char *argv[] = {"colony_sim"};
    CliOptions options;
    char error[128];
    CHECK(cli_parse(&options, ARGC(argv), argv, error, sizeof(error)));
    CHECK_STR_EQ(options.language, "");
    CHECK(!options.has_seed);
    CHECK_EQ_INT(options.map_size, 0);
    CHECK_EQ_INT(options.colonist_count, 0);
    CHECK_EQ_INT(options.scenario, SCENARIO_NONE);
    CHECK_EQ_UINT(options.debug, 0);
    CHECK(!options.no_vsync);
    CHECK_EQ_INT(options.bench_render_frames, 0);
    CHECK(!options.show_help);
}

static void test_measurement_options(void)
{
    CliOptions options;
    char error[128];
    char *argv[] = {"colony_sim", "--no-vsync", "--bench-render", "300"};
    CHECK(cli_parse(&options, ARGC(argv), argv, error, sizeof(error)));
    CHECK(options.no_vsync);
    CHECK_EQ_INT(options.bench_render_frames, 300);

    char *equals[] = {"colony_sim", "--bench-render=1"};
    CHECK(cli_parse(&options, ARGC(equals), equals, error, sizeof(error)));
    CHECK_EQ_INT(options.bench_render_frames, 1);
    CHECK(!options.no_vsync);

    char *zero[] = {"colony_sim", "--bench-render", "0"};
    CHECK(!cli_parse(&options, ARGC(zero), zero, error, sizeof(error)));
    CHECK(strstr(error, "--bench-render") != NULL);
    char *missing[] = {"colony_sim", "--bench-render"};
    CHECK(!cli_parse(&options, ARGC(missing), missing, error, sizeof(error)));
    char *huge[] = {"colony_sim", "--bench-render", "100001"};
    CHECK(!cli_parse(&options, ARGC(huge), huge, error, sizeof(error)));
    char *value[] = {"colony_sim", "--no-vsync=1"};
    CHECK(!cli_parse(&options, ARGC(value), value, error, sizeof(error)));
}

static void test_wildlife_option(void)
{
    CliOptions options;
    char error[128];
    char *none[] = {"colony_sim"};
    CHECK(cli_parse(&options, ARGC(none), none, error, sizeof(error)));
    CHECK(!options.has_wildlife); /* The usual number. */

    char *argv[] = {"colony_sim", "--animals", "250"};
    CHECK(cli_parse(&options, ARGC(argv), argv, error, sizeof(error)));
    CHECK(options.has_wildlife);
    CHECK_EQ_INT(options.wildlife, 250);

    char *zero[] = {"colony_sim", "--animals=0"};
    CHECK(cli_parse(&options, ARGC(zero), zero, error, sizeof(error)));
    CHECK(options.has_wildlife);
    CHECK_EQ_INT(options.wildlife, 0);

    char *huge[] = {"colony_sim", "--animals", "401"};
    CHECK(!cli_parse(&options, ARGC(huge), huge, error, sizeof(error)));
    CHECK(strstr(error, "--animals") != NULL);
    char *missing[] = {"colony_sim", "--animals"};
    CHECK(!cli_parse(&options, ARGC(missing), missing, error, sizeof(error)));
    char *text[] = {"colony_sim", "--animals", "many"};
    CHECK(!cli_parse(&options, ARGC(text), text, error, sizeof(error)));
}

static void test_river_option(void)
{
    CliOptions options;
    char error[128];
    char *none[] = {"colony_sim"};
    CHECK(cli_parse(&options, ARGC(none), none, error, sizeof(error)));
    CHECK_EQ_INT(options.river, 0);
    char *argv[] = {"colony_sim", "--river", "6"};
    CHECK(cli_parse(&options, ARGC(argv), argv, error, sizeof(error)));
    CHECK_EQ_INT(options.river, 6);
    CHECK(cli_starts_game(&options));
    char *wide[] = {"colony_sim", "--river=13"};
    CHECK(!cli_parse(&options, ARGC(wide), wide, error, sizeof(error)));
    CHECK(strstr(error, "--river") != NULL);
}

static void test_all_options_both_syntaxes(void)
{
    char *argv[] = {"colony_sim", "--lang", "fr", "--seed=18446744073709551615", "--size", "256"};
    CliOptions options;
    char error[128];
    CHECK(cli_parse(&options, ARGC(argv), argv, error, sizeof(error)));
    CHECK_STR_EQ(options.language, "fr");
    CHECK(options.has_seed);
    CHECK_EQ_UINT(options.seed, 18446744073709551615ULL);
    CHECK_EQ_INT(options.map_size, 256);

    char *argv2[] = {"colony_sim", "--lang=pt_BR", "-h"};
    CHECK(cli_parse(&options, ARGC(argv2), argv2, error, sizeof(error)));
    CHECK_STR_EQ(options.language, "pt_BR");
    CHECK(options.show_help);
}

static void test_rejects_invalid_input(void)
{
    CliOptions options;
    char error[128];

    char *unknown[] = {"colony_sim", "--frobnicate"};
    CHECK(!cli_parse(&options, ARGC(unknown), unknown, error, sizeof(error)));
    CHECK(strstr(error, "--frobnicate") != NULL);

    char *missing[] = {"colony_sim", "--seed"};
    CHECK(!cli_parse(&options, ARGC(missing), missing, error, sizeof(error)));

    char *negative[] = {"colony_sim", "--seed", "-5"};
    CHECK(!cli_parse(&options, ARGC(negative), negative, error, sizeof(error)));

    char *garbage[] = {"colony_sim", "--seed", "12abc"};
    CHECK(!cli_parse(&options, ARGC(garbage), garbage, error, sizeof(error)));

    char *small[] = {"colony_sim", "--size", "8"};
    CHECK(!cli_parse(&options, ARGC(small), small, error, sizeof(error)));

    char *bad_lang[] = {"colony_sim", "--lang", "../etc"};
    CHECK(!cli_parse(&options, ARGC(bad_lang), bad_lang, error, sizeof(error)));

    char *prefix[] = {"colony_sim", "--language", "fr"};
    CHECK(!cli_parse(&options, ARGC(prefix), prefix, error, sizeof(error)));
}

static void test_test_options(void)
{
    CliOptions options;
    char error[128];
    char *argv[] = {"colony_sim",       "--colonists", "64",
                    "--scenario=3",     "--debug",     "no_tools,instant_build",
                    "--debug=learn_all"};
    CHECK(cli_parse(&options, ARGC(argv), argv, error, sizeof(error)));
    CHECK_EQ_INT(options.colonist_count, 64);
    CHECK_EQ_INT(options.scenario, SCENARIO_CONSTRUCTION);
    CHECK_EQ_UINT(options.debug,
                  CLI_DEBUG_NO_TOOLS | CLI_DEBUG_INSTANT_BUILD | CLI_DEBUG_LEARN_ALL);

    char *all[] = {"colony_sim", "--debug", "all"};
    CHECK(cli_parse(&options, ARGC(all), all, error, sizeof(error)));
    CHECK_EQ_UINT(options.debug, CLI_DEBUG_INSTANT_BUILD | CLI_DEBUG_FREE_CRAFTS |
                                     CLI_DEBUG_NO_TOOLS | CLI_DEBUG_INSTANT_DECONSTRUCT |
                                     CLI_DEBUG_LEARN_ALL | CLI_DEBUG_INSTANT_FELL_AND_MINE |
                                     CLI_DEBUG_VISION | CLI_DEBUG_SEE_EVERYTHING |
                                     CLI_DEBUG_CHARACTER_EDITOR);

    char *god[] = {"colony_sim", "--debug", "god_mode"};
    CHECK(cli_parse(&options, ARGC(god), god, error, sizeof(error)));
    CHECK_EQ_UINT(options.debug, CLI_DEBUG_GOD_MODE);
    CHECK((options.debug & CLI_DEBUG_CHARACTER_EDITOR) != 0);
    CHECK((options.debug & CLI_DEBUG_SEE_EVERYTHING) != 0);
    CHECK((options.debug & (CLI_DEBUG_LEARN_ALL | CLI_DEBUG_VISION)) == 0);

    char *fell[] = {"colony_sim", "--debug=instant_fell_and_mine"};
    CHECK(cli_parse(&options, ARGC(fell), fell, error, sizeof(error)));
    CHECK_EQ_UINT(options.debug, CLI_DEBUG_INSTANT_FELL_AND_MINE);

    char *bad[][3] = {{"colony_sim", "--colonists", "0"},     {"colony_sim", "--colonists", "65"},
                      {"colony_sim", "--scenario", "7"},      {"colony_sim", "--debug", "fly"},
                      {"colony_sim", "--debug", "no_tools,"}, {"colony_sim", "--debug", ""},
                      {"colony_sim", "--debug", "no_tool"}};
    for (int i = 0; i < ARGC(bad); i++) {
        CHECK(!cli_parse(&options, 3, bad[i], error, sizeof(error)));
        CHECK(strstr(error, bad[i][1]) != NULL);
    }
}

static void test_ui_scale(void)
{
    CliOptions options;
    char error[128];
    char *none[] = {"colony_sim"};
    CHECK(cli_parse(&options, ARGC(none), none, error, sizeof(error)));
    CHECK_EQ_INT(options.ui_scale, 0);

    char *large[] = {"colony_sim", "--ui-scale", "150"};
    CHECK(cli_parse(&options, ARGC(large), large, error, sizeof(error)));
    CHECK_EQ_INT(options.ui_scale, 150);

    char *bad[][3] = {{"colony_sim", "--ui-scale", "74"},
                      {"colony_sim", "--ui-scale", "201"},
                      {"colony_sim", "--ui-scale", "1.5"}};
    for (int i = 0; i < ARGC(bad); i++) {
        CHECK(!cli_parse(&options, 3, bad[i], error, sizeof(error)));
        CHECK(strstr(error, "--ui-scale") != NULL);
    }
}

/* A game option skips the title screen; interface options do not. */
static void test_starts_game(void)
{
    CliOptions options;
    char error[128];
    char *none[] = {"colony_sim", "--lang", "fr", "--ui-scale", "150", "--no-vsync"};
    CHECK(cli_parse(&options, ARGC(none), none, error, sizeof(error)));
    CHECK(!cli_starts_game(&options));
    char *seed[] = {"colony_sim", "--seed", "42"};
    CHECK(cli_parse(&options, ARGC(seed), seed, error, sizeof(error)));
    CHECK(cli_starts_game(&options));
    char *size[] = {"colony_sim", "--size", "64"};
    CHECK(cli_parse(&options, ARGC(size), size, error, sizeof(error)));
    CHECK(cli_starts_game(&options));
    char *animals[] = {"colony_sim", "--animals", "0"};
    CHECK(cli_parse(&options, ARGC(animals), animals, error, sizeof(error)));
    CHECK(cli_starts_game(&options));
    char *debug[] = {"colony_sim", "--debug", "see_everything"};
    CHECK(cli_parse(&options, ARGC(debug), debug, error, sizeof(error)));
    CHECK(cli_starts_game(&options));
    char *scenario[] = {"colony_sim", "--scenario", "3"};
    CHECK(cli_parse(&options, ARGC(scenario), scenario, error, sizeof(error)));
    CHECK(cli_starts_game(&options));
    char *colonists[] = {"colony_sim", "--colonists", "8"};
    CHECK(cli_parse(&options, ARGC(colonists), colonists, error, sizeof(error)));
    CHECK(cli_starts_game(&options));
    char *bench[] = {"colony_sim", "--bench-render", "10"};
    CHECK(cli_parse(&options, ARGC(bench), bench, error, sizeof(error)));
    CHECK(cli_starts_game(&options));
}

int main(void)
{
    RUN_TEST(test_no_arguments_gives_defaults);
    RUN_TEST(test_all_options_both_syntaxes);
    RUN_TEST(test_measurement_options);
    RUN_TEST(test_rejects_invalid_input);
    RUN_TEST(test_test_options);
    RUN_TEST(test_ui_scale);
    RUN_TEST(test_wildlife_option);
    RUN_TEST(test_river_option);
    RUN_TEST(test_starts_game);
    return test_summary();
}
