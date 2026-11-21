/*
 * The options of a new game the player sets before it starts (the title
 * screen's New game window, game/title_screen.h): a table of the settings of
 * NewGameParams shown as whole numbers (tiles, percents, levels), each with
 * its range and step, read from and written into the params; and the seed
 * typed as text.
 *
 * The relief is one setting, from a plain (0 %: no hills, no rock outcrops)
 * through the usual land (NEW_GAME_RELIEF_USUAL, worldgen_default_params())
 * to a rugged one (100 %); it sets the advanced settings it stands for: where
 * hills begin, how tall they rise, where bare rock begins (elevations, 0 .. 1
 * in WorldGenParams, shown in percent), and is read back from where the hills
 * begin. Rock never begins below the hills (setting one moves the other
 * along if needed): the hills would sink into the ground otherwise. Water is
 * how much of the land lies under it (0 %: no lakes); lakes always lie in
 * hollows, so a world without relief nor water is all flat. The river is the
 * width of its channel (0: none), its course from the seed (core/river.h).
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
    NEW_GAME_RELIEF,
    NEW_GAME_WATER,
    NEW_GAME_RIVER,
    NEW_GAME_UNDERGROUND,
    NEW_GAME_HILLS, /* Advanced, as the next two. */
    NEW_GAME_HILL_HEIGHT,
    NEW_GAME_ROCK,
    NEW_GAME_TREES,
    NEW_GAME_COLONISTS,
    NEW_GAME_WILDLIFE,
    NEW_GAME_OPTION_COUNT
} NewGameOption;

/* The groups the options are shown in. */
typedef enum NewGameSection {
    NEW_GAME_SECTION_WORLD,  /* The seed, the size, the scale of the land. */
    NEW_GAME_SECTION_RELIEF, /* Relief, water, levels; hills and rock (advanced). */
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
    int min, max, step;   /* The default is on a step from `min`. */
    bool advanced;        /* Shown only with the advanced options. */
    const char *zero_key; /* Translation key of the whole label at 0 (NULL: as any value). */
} NewGameOptionDef;

/* The relief of worldgen_default_params(), and how much of the land lies
 * under water at most (an elevation, WorldGenParams.water_level, at 100 %). */
#define NEW_GAME_RELIEF_USUAL 50
#define NEW_GAME_WATER_MAX_ELEVATION 60

const NewGameOptionDef *new_game_option_def(NewGameOption option);
/* Translation key of a section's heading. */
const char *new_game_section_key(NewGameSection section);

/* The river of a game as played, tiles across. */
#define NEW_GAME_RIVER_USUAL 6

/* The params of a game as played: game_default_params() with wild animals
 * and a river. */
void new_game_default_params(NewGameParams *params);

/* The value of an option, in its unit. */
int new_game_option_value(const NewGameParams *params, NewGameOption option);
/* Sets an option: `value` is brought within the range, on a step. The hills
 * and the rock keep their order (see above). */
void new_game_option_set(NewGameParams *params, NewGameOption option, int value);
/* Whether the option makes a difference with the others: the hills' height
 * without hills does not. (The flat world of the tests, WorldGenParams.flat,
 * has no relief nor levels under the plains.) */
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
