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

typedef struct CliOptions {
    char language[CLI_LANGUAGE_CODE_SIZE]; /* Empty: use the system locale. */
    bool has_seed;
    uint64_t seed;
    int map_size; /* 0: default size. */
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
