/*
 * Stall watch: settlers stuck on their way, for the tests and benchmarks.
 * A settler with a path to follow that stays on the same tile for
 * STALL_TICKS is stuck: a decision loop that undoes every step before it is
 * taken shows up here (a tired settler once chose to sleep again every tick,
 * before stepping aside, for hours). The longest legitimate wait on a tile
 * with a path is about 260 ticks, in every scenario.
 *
 * It only reads the game, after each tick: kept by the caller, outside the
 * game's state (nothing saved, nothing changed).
 */
#ifndef COLONY_CORE_STALL_H
#define COLONY_CORE_STALL_H

#include "core/game.h"

#include <stdint.h>

/* Ticks on one tile with a path to follow before a settler counts as stuck
 * (half an in-game hour). */
#define STALL_TICKS (TICKS_PER_HOUR / 2)

typedef struct StallWatch {
    TilePos tile[COLONIST_MAX];   /* Where each settler was. */
    uint32_t still[COLONIST_MAX]; /* Ticks there since, with a path to follow. */
    int stalls;                   /* Times a settler got stuck (reached STALL_TICKS). */
    /* The longest stay so far, by whom and at which tick it last grew. */
    uint32_t longest;
    int longest_slot;
    uint64_t longest_tick;
} StallWatch;

void stall_watch_init(StallWatch *watch);

/* After a tick: counts how long each settler with a path to follow has stayed
 * on its tile. Returns how many are stuck now (STALL_TICKS or more). */
int stall_watch_update(StallWatch *watch, const Game *game);

#endif
