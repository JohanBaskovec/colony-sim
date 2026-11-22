/*
 * Saving and loading a game (docs/SAVE.md): the whole state of a Game into a
 * block of memory and back, exactly, so that a game loaded plays on as it
 * would have. A header ("THMR", SAVE_VERSION), one section per module (each
 * module serializes itself, core/serialize.h) and a hash of it all. No file
 * I/O: the frontend reads and writes the files.
 */
#ifndef COLONY_CORE_SAVE_H
#define COLONY_CORE_SAVE_H

#include "core/game.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Raised whenever what is saved changes: other versions are refused. */
#define SAVE_VERSION 16

typedef enum SaveError {
    SAVE_OK,
    SAVE_ERROR_MEMORY,     /* Out of memory. */
    SAVE_ERROR_NOT_A_SAVE, /* Not a save of this game. */
    SAVE_ERROR_VERSION,    /* A save of another version. */
    SAVE_ERROR_DAMAGED,    /* Cut short, changed, or holding values refused. */
    SAVE_ERROR_COUNT
} SaveError;

/* What a save tells of itself without being loaded (its INFO section, right
 * after the header): for listing saves. */
typedef struct SaveInfo {
    uint64_t tick;     /* The game's clock when saved. */
    uint32_t settlers; /* Alive. */
    uint64_t seed;
    int width, height, depth;
    bool colony_lost;
} SaveInfo;

/* The bytes at the start of a save that hold its header and INFO section:
 * save_read_info() needs no more. */
#define SAVE_INFO_BYTES 64

/* Reads what a save tells of itself from its first bytes (at least
 * SAVE_INFO_BYTES of it, or all of a shorter one). False when it is no save
 * of this version (save_version_of() tells which) or the bytes are refused.
 * The rest of the save is not checked. */
bool save_read_info(const void *data, size_t size, SaveInfo *info);

/* The game saved into a new block of memory (the caller frees it with
 * free()), its size in `*size`. NULL when out of memory. The game is not
 * changed. */
uint8_t *game_save(const Game *game, size_t *size);

/* A new game from a save (`game` is overwritten, not shut down first: pass
 * an empty one or one shut down). On SAVE_OK the game owns its memory as
 * after game_init() (game_shutdown() frees it); otherwise it is left empty
 * (safe to shut down). The clock is paused. */
SaveError game_load(Game *game, const void *data, size_t size);

/* What game_load() checks before reading anything: the magic, the version
 * and the hash of the whole save. SAVE_OK says the save is whole and of this
 * version (only a hand-edited one may still be refused by game_load()):
 * the frontend checks it before giving up the game in play. */
SaveError save_check(const void *data, size_t size);

/* The version a save was written by, or -1 when it is no save of this game
 * (for telling the player which version an old save needs). */
int save_version_of(const void *data, size_t size);

#endif
