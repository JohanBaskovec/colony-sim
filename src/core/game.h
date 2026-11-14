/*
 * The simulation root: owns the world state and advances it tick by tick.
 *
 * Everything reachable from Game is pure, deterministic C: no SDL, no I/O, no
 * wall-clock time, no global random state.
 */
#ifndef COLONY_CORE_GAME_H
#define COLONY_CORE_GAME_H

#include "core/colonist.h"
#include "core/designation.h"
#include "core/game_clock.h"
#include "core/map.h"
#include "core/pathfinding.h"
#include "core/pool.h"
#include "core/rng.h"
#include "core/worldgen.h"

#include <stdbool.h>
#include <stdint.h>

/* Plants are updated once per in-game hour. */
#define GAME_PLANT_UPDATE_INTERVAL TICKS_PER_HOUR

#define GAME_DEFAULT_COLONIST_COUNT 3

/* Radius (in tiles) around a world point within which a colonist can be picked. */
#define GAME_PICK_RADIUS 0.6f

typedef struct NewGameParams {
    WorldGenParams world;
    int colonist_count; /* Starting colonists, placed near the map center. */
} NewGameParams;

typedef struct Game {
    uint64_t seed;
    Map map;
    Designations designations; /* Player orders (chop, mine...), same size as the map. */
    GameClock clock;
    Rng rng; /* Simulation randomness (names, AI...), seeded from `seed`. */
    Pathfinder pathfinder;
    Pool colonist_pool;
    Colonist colonists[COLONIST_MAX]; /* Indexed by EntityId.index. */
} Game;

void game_default_params(NewGameParams *params);

/* Generates a new world. On failure the game is left empty (safe to shut down). */
bool game_init(Game *game, const NewGameParams *params);
void game_shutdown(Game *game);

/* Advances the simulation by `real_seconds` of real time. Returns ticks run. */
int game_update(Game *game, double real_seconds);

/* Runs exactly one simulation tick. */
void game_tick(Game *game);

/* Content hash of the whole simulation state (determinism checks). */
uint64_t game_hash(const Game *game);

/* ---- Colonists ----------------------------------------------------------- */

/*
 * Creates a colonist on a tile with a random unused name, age and needs.
 * Returns ENTITY_NONE if the tile is not passable or the colony is full.
 */
EntityId game_spawn_colonist(Game *game, int x, int y);
bool game_remove_colonist(Game *game, EntityId id);

/* NULL when the handle is stale. */
Colonist *game_colonist(Game *game, EntityId id);
const Colonist *game_colonist_const(const Game *game, EntityId id);

/* Iteration: for (uint32_t i = 0; i < game_colonist_slot_count(g); i++), skipping ENTITY_NONE. */
uint32_t game_colonist_slot_count(const Game *game);
EntityId game_colonist_id_at(const Game *game, uint32_t slot);

/* Colonist closest to a world point, within GAME_PICK_RADIUS, or ENTITY_NONE. */
EntityId game_colonist_at(const Game *game, float world_x, float world_y);

/* Colonists whose center lies in the rectangle (corners in any order). Returns the count. */
int game_colonists_in_rect(const Game *game, float x0, float y0, float x1, float y1,
                           EntityId *out, int max_count);

/* Whether a colonist currently stands on the tile. */
bool game_tile_has_colonist(const Game *game, int x, int y);

/* ---- Designations -------------------------------------------------------- */

/* Places (or with DESIGNATION_NONE, removes) an order on every valid tile of a
 * rectangle (corners in any order). Returns the number of tiles changed. */
int game_designate_rect(Game *game, int x0, int y0, int x1, int y1, DesignationType type);

#endif /* COLONY_CORE_GAME_H */
