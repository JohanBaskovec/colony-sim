/*
 * Saves on disk (docs/SAVE.md §4): the folder "saves" in the user's data
 * folder (next to the settings), games written there safely (to a temporary
 * file first, which then takes the save's name), listed with what each tells
 * of itself (core/save.h), deleted, and read for app.c to load at the start
 * of a frame (app_request_load()). The quick save (APP_KEY_QUICK_SAVE and
 * APP_KEY_QUICK_LOAD) and the autosave at each new day of the game, the one
 * before it kept. What went right or wrong is told to the player
 * (app_show_message()).
 */
#ifndef COLONY_GAME_SAVES_H
#define COLONY_GAME_SAVES_H

#include "core/save.h"
#include "core/save_files.h"

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stddef.h>

/* Saves listed at most (the newest ones). */
#define SAVES_LISTED 256

typedef struct SaveEntry {
    char file[SAVE_FILE_NAME_SIZE]; /* Its name in the folder. */
    SaveFileKind kind;
    int number;        /* SAVE_FILE_MANUAL: its number. */
    SDL_Time modified; /* When it was written. */
    int version;       /* The version of its format (save_version_of()), -1: not a save. */
    bool has_info;     /* A save of this version: `info` read. */
    SaveInfo info;
} SaveEntry;

typedef struct SaveList {
    SaveEntry entries[SAVES_LISTED]; /* Newest first. */
    int count;
} SaveList;

/* What the saves window waits for the player to confirm, on one save. */
typedef enum SavesConfirm {
    SAVES_CONFIRM_NONE,
    SAVES_CONFIRM_OVERWRITE,
    SAVES_CONFIRM_DELETE,
    SAVES_CONFIRM_LOAD, /* In a game: what is not saved of it is lost. */
} SavesConfirm;

struct App;

/* Finds the saves folder (App.saves_folder; empty when there is none: saves
 * are then refused with a message). */
void saves_init(struct App *app);

/* The saves in the folder, newest first. */
void saves_list(const struct App *app, SaveList *list);

/* Writes the game in play to a save of that name (replaced if it is there).
 * Tells the player when it fails; `told`: also when it went right. */
bool saves_write(struct App *app, const char *file, bool told);

/* Writes the game in play to a new save, numbered after the others. */
bool saves_write_new(struct App *app);

bool saves_delete(struct App *app, const char *file);

/* The whole file of a save, read into memory (SDL_free() it), its size in
 * `*size`; NULL when it cannot be read (the player is told). */
void *saves_read(struct App *app, const char *file, size_t *size);

/* Tells the player why a save was refused (`version`: the one it was
 * written by, for SAVE_ERROR_VERSION). */
void saves_tell_error(struct App *app, SaveError error, int version);

/* The quick save: written, or asked to be loaded (when there is one). */
void saves_quick_save(struct App *app);
void saves_quick_load(struct App *app);

/* Called each frame of a game: writes the autosave when a new day of the
 * game has begun since the last one (or since the game began or was loaded,
 * App.autosave_day). */
void saves_autosave_update(struct App *app);

/* What the player sees of a save: its name ("Save 3", "Quick save"...) and a
 * line of details (the date in the game, the settlers, when it was written;
 * or why it cannot be loaded). */
void saves_entry_name(const SaveEntry *entry, char *out, size_t size);
void saves_entry_details(const SaveEntry *entry, char *out, size_t size);

#endif /* COLONY_GAME_SAVES_H */
