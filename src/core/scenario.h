/*
 * Ready-made test situations set up on a new game: what bench_simulation
 * measures, and what the game can start on (--scenario) to look at it.
 * Built next to the first colonist, deterministically.
 */
#ifndef COLONY_CORE_SCENARIO_H
#define COLONY_CORE_SCENARIO_H

#include "core/game.h"

#include <stdbool.h>

typedef enum Scenario {
    /* Nothing: colonists left alone (needs and wandering only). */
    SCENARIO_NONE,
    /* A gathering zone over the whole map (grass cut, twigs, branches and
     * wild food gathered, forever), a zone gathering stones forever next to
     * the colony, two stockpiles: work, hauling, and many jobs that cannot be
     * reached. */
    SCENARIO_GATHERING,
    /* Only jobs that nobody can reach: a zone gathering the branches of the
     * trees growing where the colonists cannot walk to (other islands). */
    SCENARIO_UNREACHABLE,
    /* Construction: an enclosure of grass walls with a branch screen and beds
     * inside, planned next to the colony, with a gathering zone around it
     * (grass, branches, twigs) and a stockpile: gathering, drying, hauling to
     * the sites, building, sleeping in the beds and refreshing them. */
    SCENARIO_CONSTRUCTION,
    SCENARIO_COUNT
} Scenario;

/* Sets a scenario up on a game (fresh from game_init(), with at least one
 * colonist for the scenarios built next to the colony). Fails on an invalid
 * scenario. */
bool scenario_apply(Game *game, Scenario scenario);

#endif /* COLONY_CORE_SCENARIO_H */
