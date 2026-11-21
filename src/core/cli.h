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
#define CLI_BENCH_RENDER_MAX_FRAMES 100000
/* Interface scale (--ui-scale), in percent. */
#define CLI_UI_SCALE_MIN 75
#define CLI_UI_SCALE_MAX 200
/* Most wild animals (--animals), in percent of the usual density. */
#define CLI_WILDLIFE_MAX 400

/* Cheats turned on from the start (--debug), see GameDebug in core/game.h. */
#define CLI_DEBUG_INSTANT_BUILD (1u << 0)
#define CLI_DEBUG_FREE_CRAFTS (1u << 1)
#define CLI_DEBUG_NO_TOOLS (1u << 2)
#define CLI_DEBUG_INSTANT_DECONSTRUCT (1u << 3)
#define CLI_DEBUG_LEARN_ALL (1u << 4) /* game_debug_learn_all() once, at the start. */
#define CLI_DEBUG_INSTANT_FELL_AND_MINE (1u << 5)
#define CLI_DEBUG_VISION (1u << 6)           /* The overlay of what the selected settler sees. */
#define CLI_DEBUG_SEE_EVERYTHING (1u << 7)   /* No fog of war. */
#define CLI_DEBUG_CHARACTER_EDITOR (1u << 8) /* The - and + buttons (game/debug_panel.h). */
#define CLI_DEBUG_ALL ((1u << 9) - 1)
/* "god_mode": the first six cheats of the debug menu (building, crafting, tools,
 * taking apart, felling and mining, seeing everything) and the character editor. */
#define CLI_DEBUG_GOD_MODE                                                                         \
    (CLI_DEBUG_INSTANT_BUILD | CLI_DEBUG_FREE_CRAFTS | CLI_DEBUG_NO_TOOLS |                        \
     CLI_DEBUG_INSTANT_DECONSTRUCT | CLI_DEBUG_INSTANT_FELL_AND_MINE | CLI_DEBUG_SEE_EVERYTHING |  \
     CLI_DEBUG_CHARACTER_EDITOR)

typedef struct CliOptions {
    char language[CLI_LANGUAGE_CODE_SIZE]; /* Empty: use the system locale. */
    bool has_seed;
    uint64_t seed;
    int map_size;       /* 0: default size. */
    int colonist_count; /* 0: default count. */
    /* Wild animals (--animals): how many, in percent of the usual density
     * (0: none), when `has_wildlife`. */
    bool has_wildlife;
    int wildlife;
    int river;      /* Tiles across the river's channel (--river), 0: none. */
    int scenario;   /* A Scenario (core/scenario.h), SCENARIO_NONE by default. */
    unsigned debug; /* CLI_DEBUG_* bits. */
    bool no_vsync;  /* Frames as fast as possible (frame time measurements). */
    int ui_scale;   /* Interface scale in percent, 0: default (100). */
    /* Render benchmark (--bench-render): frames drawn per view, 0 when off. */
    int bench_render_frames;
    bool show_help;
} CliOptions;

void cli_default_options(CliOptions *options);

/*
 * Parses argv. Supports "--option value" and "--option=value".
 * On failure returns false and writes a message into `error`.
 */
bool cli_parse(CliOptions *options, int argc, char **argv, char *error, size_t error_size);

/*
 * Whether the game starts at once, without the title screen: an option about
 * the game itself was given (seed, size, settlers, animals, river, scenario, cheats,
 * render benchmark). The interface options (language, scale, vsync) leave the
 * title screen.
 */
bool cli_starts_game(const CliOptions *options);

const char *cli_usage(void);

#endif /* COLONY_CORE_CLI_H */
