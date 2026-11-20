/*
 * The options of a new game the player sets before it starts (the title
 * screen's New game window, game/title_screen.h): a table of the settings of
 * NewGameParams shown as whole numbers (tiles, percents, levels), each with
 * its range and step, read from and written into the params; and the seed
 * typed as text.
 *
 * The relief's thresholds are elevations (0 .. 1 in WorldGenParams) shown in
 * percent: where water ends, where hills begin, where bare rock begins, and
 * how much elevation one level up takes. Rock never begins below the hills
 * (setting one moves the other along if needed): the hills would sink into
 * the ground otherwise.
 */
#ifndef COLONY_CORE_NEW_GAME_H
#define COLONY_CORE_NEW_GAME_H

#include "core/game.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum NewGameOption {
    NEW_GAME_WIDTH,
    NEW_GAME_HEIGHT,
    NEW_GAME_FEATURE_SCALE,
    NEW_GAME_WATER,
    NEW_GAME_HILLS,
    NEW_GAME_HILL_STEP,
    NEW_GAME_ROCK,
    NEW_GAME_UNDERGROUND,
    NEW_GAME_TREES,
    NEW_GAME_COLONISTS,
    NEW_GAME_WILDLIFE,
    NEW_GAME_OPTION_COUNT
} NewGameOption;

/* The groups the options are shown in. */
typedef enum NewGameSection {
    NEW_GAME_SECTION_WORLD,  /* The seed, the size, the scale of the land. */
    NEW_GAME_SECTION_RELIEF, /* Water, hills, rock, levels (and the flat world). */
    NEW_GAME_SECTION_LIFE,   /* Trees, settlers, wild animals. */
    NEW_GAME_SECTION_COUNT
} NewGameSection;

/* How a value reads. */
typedef enum NewGameUnit {
    NEW_GAME_UNIT_TILES,
    NEW_GAME_UNIT_PERCENT,
    NEW_GAME_UNIT_LEVELS,
    NEW_GAME_UNIT_COUNT, /* A number of settlers. */
} NewGameUnit;

typedef struct NewGameOptionDef {
    const char *id;       /* Stable name, never translated. */
    const char *name_key; /* Translation key of the label, {0} the value. */
    const char *note_key; /* Translation key of what it does. */
    NewGameSection section;
    NewGameUnit unit;
    int min, max, step; /* The default is on a step from `min`. */
} NewGameOptionDef;

const NewGameOptionDef *new_game_option_def(NewGameOption option);
/* Translation key of a section's heading. */
const char *new_game_section_key(NewGameSection section);

/* The params of a game as played: game_default_params() with wild animals. */
void new_game_default_params(NewGameParams *params);

/* The value of an option, in its unit. */
int new_game_option_value(const NewGameParams *params, NewGameOption option);
/* Sets an option: `value` is brought within the range, on a step. The hills
 * and the rock keep their order (see above). */
void new_game_option_set(NewGameParams *params, NewGameOption option, int value);
/* Whether the option makes a difference with the others: a flat world has no
 * hills nor levels under the plains. */
bool new_game_option_applies(const NewGameParams *params, NewGameOption option);

/* Longest seed text kept (bytes, the terminating zero included). */
#define NEW_GAME_SEED_TEXT_SIZE 33

/*
 * The seed a text stands for: a number (digits, up to UINT64_MAX), or else a
 * hash of the text (any word or sentence gives its own world), spaces at
 * both ends left out. False for an empty text (a random seed then).
 */
bool new_game_seed_from_text(const char *text, uint64_t *seed);

#endif /* COLONY_CORE_NEW_GAME_H */
