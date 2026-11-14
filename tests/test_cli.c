#include "core/cli.h"

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

int main(void)
{
    RUN_TEST(test_no_arguments_gives_defaults);
    RUN_TEST(test_all_options_both_syntaxes);
    RUN_TEST(test_rejects_invalid_input);
    return test_summary();
}
