#include "core/cli.h"

#include "core/colonist.h"
#include "core/map.h"
#include "core/scenario.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRINGIFY_TOKEN(x) #x
#define STRINGIFY(x) STRINGIFY_TOKEN(x)

void cli_default_options(CliOptions *options)
{
    memset(options, 0, sizeof(*options));
}

const char *cli_usage(void)
{
    return "Usage: colony_sim [options]\n"
           "  --lang <code>   Interface language (e.g. en, fr). Default: system locale.\n"
           "  --seed <n>      World seed (unsigned integer). Default: random.\n"
           "  --size <n>      Map width and height in tiles (16-1024). Default: 200.\n"
           "  --colonists <n> Starting colonists (1-" STRINGIFY(
               COLONIST_MAX) "). Default: 3.\n"
                             "  --scenario <n>  Test situation set up at the start, as in "
                             "bench_simulation:\n"
                             "                  0 none, 1 gathering, 2 unreachable jobs, 3 "
                             "construction.\n"
                             "  --debug <list>  Cheats on from the start, separated by commas: "
                             "instant_build,\n"
                             "                  free_crafts, no_tools, instant_deconstruct, "
                             "learn_all,\n"
                             "                  instant_fell_and_mine, or all.\n"
                             "  --help          Show this message.\n";
}

/* Names of the CLI_DEBUG_* bits, in bit order. */
static const char *const DEBUG_NAMES[] = {"instant_build", "free_crafts",
                                          "no_tools",      "instant_deconstruct",
                                          "learn_all",     "instant_fell_and_mine"};
_Static_assert(sizeof(DEBUG_NAMES) / sizeof(DEBUG_NAMES[0]) == 6, "one name per debug bit");

/* Parses a comma-separated list of DEBUG_NAMES (or "all"), adding its bits
 * to `out` (left unchanged on failure). */
static bool parse_debug(const char *text, unsigned *out)
{
    unsigned bits = 0;
    const char *p = text;
    do {
        const char *end = strchr(p, ',');
        size_t length = end != NULL ? (size_t)(end - p) : strlen(p);
        bool known = false;
        if (length == 3 && strncmp(p, "all", 3) == 0) {
            bits |= CLI_DEBUG_ALL;
            known = true;
        }
        for (size_t i = 0; !known && i < sizeof(DEBUG_NAMES) / sizeof(DEBUG_NAMES[0]); i++) {
            if (strlen(DEBUG_NAMES[i]) == length && strncmp(p, DEBUG_NAMES[i], length) == 0) {
                bits |= 1u << i;
                known = true;
            }
        }
        if (!known) {
            return false;
        }
        p = end != NULL ? end + 1 : NULL;
    } while (p != NULL);
    *out |= bits;
    return true;
}

static bool parse_u64(const char *text, uint64_t *out)
{
    if (text == NULL || *text == '\0' || *text == '-' || *text == '+') {
        return false;
    }
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static bool is_valid_language_code(const char *code)
{
    size_t length = strlen(code);
    if (length == 0 || length >= CLI_LANGUAGE_CODE_SIZE) {
        return false;
    }
    for (const char *p = code; *p != '\0'; p++) {
        char ch = *p;
        bool allowed =
            (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '-';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

/*
 * If argv[*index] is `name` or `name=value`, stores the value (taken from the
 * next argument in the first case) and returns 1; returns 0 if the option does
 * not match and -1 if its value is missing.
 */
static int match_option(const char *name, int argc, char **argv, int *index, const char **value)
{
    const char *arg = argv[*index];
    size_t name_length = strlen(name);
    if (strncmp(arg, name, name_length) != 0) {
        return 0;
    }
    if (arg[name_length] == '=') {
        *value = arg + name_length + 1;
        return 1;
    }
    if (arg[name_length] != '\0') {
        return 0;
    }
    if (*index + 1 >= argc) {
        return -1;
    }
    *index += 1;
    *value = argv[*index];
    return 1;
}

bool cli_parse(CliOptions *options, int argc, char **argv, char *error, size_t error_size)
{
    cli_default_options(options);
    if (error_size > 0) {
        error[0] = '\0';
    }

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = NULL;
        int match;

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            options->show_help = true;
        } else if ((match = match_option("--lang", argc, argv, &i, &value)) != 0) {
            if (match < 0 || !is_valid_language_code(value)) {
                snprintf(error, error_size, "--lang expects a language code such as 'fr'");
                return false;
            }
            snprintf(options->language, sizeof(options->language), "%s", value);
        } else if ((match = match_option("--seed", argc, argv, &i, &value)) != 0) {
            if (match < 0 || !parse_u64(value, &options->seed)) {
                snprintf(error, error_size, "--seed expects an unsigned integer");
                return false;
            }
            options->has_seed = true;
        } else if ((match = match_option("--size", argc, argv, &i, &value)) != 0) {
            uint64_t size = 0;
            if (match < 0 || !parse_u64(value, &size) || size < MAP_MIN_SIZE ||
                size > MAP_MAX_SIZE) {
                snprintf(error, error_size, "--size expects an integer between %d and %d",
                         MAP_MIN_SIZE, MAP_MAX_SIZE);
                return false;
            }
            options->map_size = (int)size;
        } else if ((match = match_option("--colonists", argc, argv, &i, &value)) != 0) {
            uint64_t count = 0;
            if (match < 0 || !parse_u64(value, &count) || count < 1 || count > COLONIST_MAX) {
                snprintf(error, error_size, "--colonists expects an integer between 1 and %d",
                         COLONIST_MAX);
                return false;
            }
            options->colonist_count = (int)count;
        } else if ((match = match_option("--scenario", argc, argv, &i, &value)) != 0) {
            uint64_t scenario = 0;
            if (match < 0 || !parse_u64(value, &scenario) || scenario >= SCENARIO_COUNT) {
                snprintf(error, error_size, "--scenario expects an integer between 0 and %d",
                         SCENARIO_COUNT - 1);
                return false;
            }
            options->scenario = (int)scenario;
        } else if ((match = match_option("--debug", argc, argv, &i, &value)) != 0) {
            if (match < 0 || !parse_debug(value, &options->debug)) {
                snprintf(error, error_size,
                         "--debug expects cheats separated by commas (see --help)");
                return false;
            }
        } else {
            snprintf(error, error_size, "unknown option '%s'", arg);
            return false;
        }
    }
    return true;
}
