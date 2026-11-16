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
    CHECK(!options.show_help);
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
                                     CLI_DEBUG_LEARN_ALL | CLI_DEBUG_INSTANT_FELL_AND_MINE);

    char *fell[] = {"colony_sim", "--debug=instant_fell_and_mine"};
    CHECK(cli_parse(&options, ARGC(fell), fell, error, sizeof(error)));
    CHECK_EQ_UINT(options.debug, CLI_DEBUG_INSTANT_FELL_AND_MINE);

    char *bad[][3] = {{"colony_sim", "--colonists", "0"},     {"colony_sim", "--colonists", "65"},
                      {"colony_sim", "--scenario", "4"},      {"colony_sim", "--debug", "fly"},
                      {"colony_sim", "--debug", "no_tools,"}, {"colony_sim", "--debug", ""},
                      {"colony_sim", "--debug", "no_tool"}};
    for (int i = 0; i < ARGC(bad); i++) {
        CHECK(!cli_parse(&options, 3, bad[i], error, sizeof(error)));
        CHECK(strstr(error, bad[i][1]) != NULL);
    }
}

int main(void)
{
    RUN_TEST(test_no_arguments_gives_defaults);
    RUN_TEST(test_all_options_both_syntaxes);
    RUN_TEST(test_rejects_invalid_input);
    RUN_TEST(test_test_options);
    return test_summary();
}
