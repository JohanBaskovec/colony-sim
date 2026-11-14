/*
 * The simulation root: owns the world state and advances it tick by tick.
 *
 * Everything reachable from Game is pure, deterministic C: no SDL, no I/O, no
 * wall-clock time, no global random state.
 */
#ifndef COLONY_CORE_GAME_H
#define COLONY_CORE_GAME_H

#include "core/game_clock.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/worldgen.h"

#include <stdbool.h>
#include <stdint.h>

/* Plants are updated once per in-game hour. */
#define GAME_PLANT_UPDATE_INTERVAL TICKS_PER_HOUR

typedef struct Game {
    uint64_t seed;
    Map map;
    GameClock clock;
    Rng rng; /* Simulation randomness (events, AI...), seeded from `seed`. */
} Game;

/* Generates a new world. On failure the game is left empty (safe to shut down). */
bool game_init(Game *game, const WorldGenParams *params);
void game_shutdown(Game *game);

/* Advances the simulation by `real_seconds` of real time. Returns ticks run. */
int game_update(Game *game, double real_seconds);

/* Runs exactly one simulation tick. */
void game_tick(Game *game);

#endif /* COLONY_CORE_GAME_H */
