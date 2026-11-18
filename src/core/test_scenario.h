/*
 * Test scenarios (the F2 debug menu): ready-made situations on a flat bare
 * world, built in the running game when asked: a game that replaces the one
 * being played, nothing is saved. Unlike core/scenario.h (what the benchmarks
 * measure, set up on a new game from the command line), each builds its own
 * world and places animals and settlers by hand.
 */
#ifndef COLONY_CORE_TEST_SCENARIO_H
#define COLONY_CORE_TEST_SCENARIO_H

#include "core/game.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum TestScenario {
    /* A pack of hungry wolves ten tiles from a herd of roe deer; no settlers. */
    TEST_SCENARIO_WOLVES_AND_DEER,
    /* A pack of hungry wolves ten tiles from two settlers, one bare-handed and
     * one with a thrusting spear; no other animals. */
    TEST_SCENARIO_WOLVES_AND_SETTLERS,
    TEST_SCENARIO_COUNT
} TestScenario;

/* Side of the flat world, in tiles. */
#define TEST_SCENARIO_WORLD_SIZE 96
/* How hungry the wolves are (0..1: they hunt below ANIMAL_HUNGRY and go for
 * settlers below ANIMAL_DESPERATE). */
#define TEST_SCENARIO_WOLF_FOOD 0.2f
/* Roe deer in the herd, and wolves in the pack. */
#define TEST_SCENARIO_DEER_COUNT 3
#define TEST_SCENARIO_WOLF_COUNT 4
/* Tiles between the wolves and what they hunt. */
#define TEST_SCENARIO_DISTANCE 10

/* Translation key of the scenario's name; NULL out of range. */
const char *test_scenario_name_key(TestScenario scenario);

/*
 * Replaces the world of `game` (shut down, then built again) with the
 * scenario's, from the same seed. The debug flags are kept; the fog of war is
 * off, and the clock keeps its speed. Returns false for a scenario out of range
 * (`game` untouched), or when the world cannot be built (`game` is then shut
 * down: do not use it).
 */
bool test_scenario_load(Game *game, TestScenario scenario);

#endif /* COLONY_CORE_TEST_SCENARIO_H */
