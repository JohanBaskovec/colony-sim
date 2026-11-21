/*
 * The names of the save files (docs/SAVE.md §4): the player's saves are
 * numbered (save-001.sav...), besides the quick save and the autosave with
 * the one before it. Only the names: the frontend lists, reads and writes
 * the files.
 */
#ifndef COLONY_CORE_SAVE_FILES_H
#define COLONY_CORE_SAVE_FILES_H

#include <stdbool.h>
#include <stddef.h>

typedef enum SaveFileKind {
    SAVE_FILE_NONE, /* No save's name. */
    SAVE_FILE_MANUAL,
    SAVE_FILE_QUICK,
    SAVE_FILE_AUTO,
    SAVE_FILE_AUTO_PREVIOUS, /* The autosave before the last one. */
    SAVE_FILE_KIND_COUNT
} SaveFileKind;

/* The player's saves are numbered 1 to SAVE_FILE_NUMBER_MAX. */
#define SAVE_FILE_NUMBER_MAX 999
/* Room for any save's name, its terminating zero included. */
#define SAVE_FILE_NAME_SIZE 32
/* What a save is written to first, before it takes its name (appended). */
#define SAVE_FILE_TEMP_SUFFIX ".tmp"

/* What kind of save a file name is, and for a player's save its number
 * (`number` may be NULL; 0 for the others). */
SaveFileKind save_file_kind(const char *name, int *number);

/* The name of a save (`number` only for SAVE_FILE_MANUAL). False when it
 * does not fit or the number is out of range. */
bool save_file_name(SaveFileKind kind, int number, char *out, size_t size);

/* The number of the next player's save: one past the highest of the names
 * given (1 when there is none), or 0 when the numbers are used up. */
int save_file_next_number(const char *const *names, int count);

#endif
