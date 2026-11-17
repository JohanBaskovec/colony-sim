/*
 * The first things the colony knows (docs/VISION.md, "The start of a game"):
 * what lies around the settlers' arrival point, and the way they came, a walk
 * from a map edge drawn at random to the arrival point, as if they had
 * crossed the land before the game began.
 */
#ifndef COLONY_CORE_EXPLORATION_H
#define COLONY_CORE_EXPLORATION_H

#include "core/map.h"
#include "core/pathfinding.h"
#include "core/vision.h"

#include <stdbool.h>
#include <stdint.h>

/* Steps from the arrival point that count as walked around (food, water and
 * flint are placed within reach of it). */
#define EXPLORATION_CAMP_STEPS 14
/* Along the way in, the whole field is cast every this many tiles, the near one
 * on every tile. */
#define EXPLORATION_WAY_FAR_EVERY 8

/*
 * Explores the surroundings of `center`: the tiles within EXPLORATION_CAMP_STEPS
 * steps, what is seen from them, and the way in from an edge of the map drawn
 * from `seed` (nothing of the latter when no edge can be walked from).
 * Returns the number of tiles of the way in (0 when none).
 */
int exploration_start(Vision *vision, const Map *map, Pathfinder *finder, TilePos center,
                      uint64_t seed);

#endif /* COLONY_CORE_EXPLORATION_H */
