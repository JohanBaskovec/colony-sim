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
    /* A gathering zone over the whole map (grass cut and wild food
     * gathered, forever), two stockpiles: work, hauling (the branches, twigs
     * and stones lying about too), and many jobs that cannot be reached. */
    SCENARIO_GATHERING,
    /* Only jobs that nobody can reach: a zone felling the trees growing
     * where the colonists cannot walk to (other islands). */
    SCENARIO_UNREACHABLE,
    /* Construction: an enclosure of grass walls with a branch screen and beds
     * inside, planned next to the colony, with a gathering zone around it
     * (grass) and a stockpile: gathering, drying, hauling to the sites (the
     * branches lying about too), building, sleeping in the beds and refreshing them. */
    SCENARIO_CONSTRUCTION,
    /* Feuds: the gathering scenario, with the settlers split in two clans
     * who loathe each other, half of them abrasive (game_debug_feud()):
     * arguments, brawls, gossip passed on, apologies (docs/SOCIAL.md). */
    SCENARIO_FEUDS,
    /* Cooking: two lit hearths next to the colony, one roasting wild carrots
     * and one cattail rhizomes (only of quality 40 or better), again and
     * again (BILL_DO_TIMES, BILL_TARGET_MAX: Bill.done counts the crafts);
     * heaps of both, fresh and half rotten, some under cobbles or on
     * branches (tiles of several kinds), a stockpile for food, a zone
     * gathering wild food around (the fuel lies about); every settler knows fire well enough to
     * roast. */
    SCENARIO_COOKING,
    SCENARIO_COUNT
} Scenario;

/* Sets a scenario up on a game (fresh from game_init(), with at least one
 * colonist for the scenarios built next to the colony). Fails on an invalid
 * scenario. */
bool scenario_apply(Game *game, Scenario scenario);

#endif /* COLONY_CORE_SCENARIO_H */
