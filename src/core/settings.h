/*
 * Player settings kept between sessions: the interface size, the colors and
 * the language. They are read from and written to a short "key = value"
 * text; the frontend loads and saves the file (the core does no file I/O).
 * Unknown keys and invalid values are skipped (the value stays as it was), so
 * that an old or hand-edited file never stops the game from starting.
 */
#ifndef COLONY_CORE_SETTINGS_H
#define COLONY_CORE_SETTINGS_H

#include "core/cli.h"

#include <stddef.h>

/* Interface size, in percent: CLI_UI_SCALE_MIN..CLI_UI_SCALE_MAX (the same
 * range as --ui-scale); the settings window offers it in steps of this. */
#define SETTINGS_UI_SCALE_DEFAULT 100
#define SETTINGS_UI_SCALE_STEP 25
#define SETTINGS_LANGUAGE_SIZE CLI_LANGUAGE_CODE_SIZE

/* Sets of interface colors (game/theme.h). */
typedef enum SettingsPalette {
    SETTINGS_PALETTE_STANDARD,
    SETTINGS_PALETTE_COLORBLIND, /* No red against green: blues and oranges. */
    SETTINGS_PALETTE_COUNT
} SettingsPalette;

typedef struct Settings {
    int ui_scale; /* Percent. */
    SettingsPalette palette;
    /* A language code ("fr", "pt_BR"); empty: the system's. */
    char language[SETTINGS_LANGUAGE_SIZE];
} Settings;

void settings_default(Settings *settings);

/* Reads the `length` bytes of `text` (no terminating NUL needed) over the
 * current values: each valid line sets its key, the rest is skipped. */
void settings_parse(Settings *settings, const char *text, size_t length);

/* Writes the settings as text into `out` (always terminated, cut short if
 * too small). Returns the length of the whole text, as snprintf() does. */
size_t settings_format(const Settings *settings, char *out, size_t size);

/* Stable identifier of a palette in the file, NULL if out of range. */
const char *settings_palette_id(SettingsPalette palette);

/* Translation key of a palette's name, NULL if out of range. */
const char *settings_palette_name_key(SettingsPalette palette);

#endif /* COLONY_CORE_SETTINGS_H */
