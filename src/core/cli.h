/*
 * Command-line options. Usage and error messages are developer-facing and
 * therefore written in English (not translated).
 */
#ifndef COLONY_CORE_CLI_H
#define COLONY_CORE_CLI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLI_LANGUAGE_CODE_SIZE 16

/* Cheats turned on from the start (--debug), see GameDebug in core/game.h. */
#define CLI_DEBUG_INSTANT_BUILD (1u << 0)
#define CLI_DEBUG_FREE_CRAFTS (1u << 1)
#define CLI_DEBUG_NO_TOOLS (1u << 2)
#define CLI_DEBUG_INSTANT_DECONSTRUCT (1u << 3)
#define CLI_DEBUG_LEARN_ALL (1u << 4) /* game_debug_learn_all() once, at the start. */
#define CLI_DEBUG_ALL ((1u << 5) - 1)

typedef struct CliOptions {
    char language[CLI_LANGUAGE_CODE_SIZE]; /* Empty: use the system locale. */
    bool has_seed;
    uint64_t seed;
    int map_size; /* 0: default size. */
    int colonist_count; /* 0: default count. */
    int scenario; /* A Scenario (core/scenario.h), SCENARIO_NONE by default. */
    unsigned debug; /* CLI_DEBUG_* bits. */
    bool show_help;
} CliOptions;

void cli_default_options(CliOptions *options);

/*
 * Parses argv. Supports "--option value" and "--option=value".
 * On failure returns false and writes a message into `error`.
 */
bool cli_parse(CliOptions *options, int argc, char **argv, char *error, size_t error_size);

const char *cli_usage(void);

#endif /* COLONY_CORE_CLI_H */
