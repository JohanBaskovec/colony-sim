/*
 * Hearths: the fire of an open hearth, a workstation (core/workstation.h).
 *
 * Fuel is counted in kilograms, an item's weight (a branch 1.5 kg, a handful
 * of twigs 0.2 kg, a log 7 kg), up to HEARTH_FUEL_CAPACITY. A burning hearth
 * burns HEARTH_BURN_PER_HOUR and goes out once its fuel is gone. While the
 * player keeps it burning (the default), haulers bring the fuel types the
 * player allows once it falls below HEARTH_REFUEL_BELOW of the capacity, and
 * a settler who knows fire lights it when it has fuel and no fire: with
 * embers wrapped in grass at a burning fire within HEARTH_FIRE_CARRY_MAX_COST
 * of it (they go out after HEARTH_EMBER_TICKS), else by friction, an attempt
 * taking HEARTH_FRICTION_WORK and a handful of dry grass as tinder, which
 * succeeds more often the better the settler knows fire. A burning hearth
 * lights the tiles around it at night, and is where spears are hardened,
 * roots roasted and fireside stories told (core/colonist.h). Rain and snow
 * soak a fire under the open sky until it goes out (its fuel kept), and
 * nobody can light it while they fall (core/weather.h).
 * Design: docs/KNOWLEDGE.md §5.
 */
#ifndef COLONY_CORE_HEARTH_H
#define COLONY_CORE_HEARTH_H

#include "core/game_clock.h"
#include "core/map.h"
#include "core/temperature.h"
#include "core/workstation.h"

#include <stdbool.h>
#include <stdint.h>

#define HEARTH_FUEL_CAPACITY 20.0f
#define HEARTH_BURN_PER_HOUR 2.5f
#define HEARTH_REFUEL_BELOW 0.5f
#define HEARTH_FIRE_CARRY_MAX_COST 60.0f
#define HEARTH_EMBER_WORK 50.0f
#define HEARTH_EMBER_TICKS (4 * TICKS_PER_HOUR)
#define HEARTH_FRICTION_WORK 750.0f
#define HEARTH_FRICTION_CHANCE_AT_MASTERY 0.3f
#define HEARTH_FRICTION_CHANCE_PER_LEVEL 0.014f
/* Level in fire needed to light a hearth. */
#define HEARTH_LIGHT_MIN_LEVEL 50.0f
/* Degrees an hour a burning hearth adds to its tile, which spread around it
 * (core/temperature.h) (tuning). */
#define HEARTH_HEAT_PER_HOUR 200.0f
/* Warmth a burning fire radiates (what makes one sweat beside a campfire,
 * though the air around it barely warms): HEARTH_RADIANT_WARMTH degrees felt
 * one tile from it, losing HEARTH_RADIANT_FALLOFF of itself (as an
 * exponent) per tile further, nothing beyond HEARTH_RADIANT_RADIUS; walls,
 * doors and rock in between stop it. A fire low on fuel radiates less: from
 * HEARTH_RADIANT_LOW_FIRE of it to all of it at HEARTH_RADIANT_FULL_FUEL
 * kilograms (tuning). */
#define HEARTH_RADIANT_WARMTH 20.0f
#define HEARTH_RADIANT_FALLOFF 0.8f
#define HEARTH_RADIANT_RADIUS 5
#define HEARTH_RADIANT_LOW_FIRE 0.4f
#define HEARTH_RADIANT_FULL_FUEL 5.0f
/* A fire under the open sky soaks up what falls (weather_wetting(): 1 an
 * hour in the rain, 2 in a storm, 0.25 in the snow) and goes out at
 * HEARTH_SOAKED_OUT (two hours of rain); it dries by HEARTH_DRY_PER_HOUR
 * (tuning). */
#define HEARTH_SOAKED_OUT 2.0f
#define HEARTH_DRY_PER_HOUR 1.0f
/* Tiles lit around a burning hearth at night (fading to nothing there). */
#define HEARTH_LIGHT_RADIUS 6.0f

/* Fireside stories: from this hour to the night, a settler tells about a
 * knowledge the player marked to teach, beside a burning hearth; at most
 * HEARTH_STORY_LISTENERS settlers listen, within HEARTH_STORY_RANGE tiles of
 * it (they sit within HEARTH_LISTEN_SPOT_RANGE). */
#define HEARTH_STORY_START_HOUR 19
#define HEARTH_STORY_END_HOUR 22
#define HEARTH_STORY_RANGE 5
#define HEARTH_LISTEN_SPOT_RANGE 3
#define HEARTH_STORY_LISTENERS 8

/* Whether a station is a hearth (NULL: no). */
bool hearth_is(const Workstation *station);

/* Kilograms of fuel one item of this type gives, 0 when it is no fuel. */
float hearth_fuel_value(ItemType type);

/* Kilograms of fuel the hearth wants brought now: none unless it is kept
 * burning and its fuel, counting what is on its way, is below the refuel
 * mark; then enough to fill it. */
float hearth_fuel_wanted(const Workstation *station);

/* Items of a type the hearth takes now (allowed, and fitting in what it
 * wants); 0 when none. */
int hearth_fuel_items_wanted(const Workstation *station, ItemType type);

/* Puts fuel in the hearth at `index`: as many items as fit under the
 * capacity. Returns how many were taken. */
int hearth_add_fuel(Workstations *workstations, int index, ItemStack stack);

/* Fuel on its way to the hearth at `index`; a release is ignored when the
 * station is not the one numbered `number` any more. */
void hearth_promise_fuel(Workstations *workstations, int index, float kilograms);
void hearth_release_fuel(Workstations *workstations, int index, int number, float kilograms);

/* Whether a settler should light it: kept burning, fuel in it, no fire. */
bool hearth_needs_lighting(const Workstation *station);

/* Lights the hearth at `index` if it has fuel. Returns whether it burns. */
bool hearth_light(Workstations *workstations, int index);

/* Changing what the player wants of a hearth. */
bool hearth_set_keep_burning(Workstations *workstations, int index, bool keep);
bool hearth_set_fuel_type(Workstations *workstations, int index, ItemType type, bool allowed);

/* Whether rain or snow falls on the hearth (`wetting`: weather_wetting()),
 * which is under the open sky. */
bool hearth_rained_on(const Workstation *station, const Map *map, float wetting);

/* One tick: burning hearths burn their fuel and go out when it is gone, or
 * soaked by what falls on them (`wetting`: weather_wetting(), 0 when
 * nothing falls). Returns how many went out. */
int hearths_burn(Workstations *workstations, const Map *map, float wetting);

/* Chance (0..1) that an attempt at lighting by friction succeeds at a level. */
float hearth_friction_chance(double level);

/* Light (0..1) burning hearths give at a world point (tile centers at +0.5). */
float hearths_light_at(const Workstations *workstations, float x, float y);

/* Degrees felt on a tile from the warmth burning hearths radiate (added
 * up, 0 when none reaches it). */
float hearths_radiant_warmth(const Workstations *workstations, const Map *map, int x, int y);

/* Temperature felt on a tile: the air's (core/temperature.h) plus the
 * warmth hearths radiate. What settlers feel; the air's is what spreads. */
float hearths_felt_temperature(const Workstations *workstations, const Map *map,
                               const Temperatures *temperatures, int x, int y);

/* Whether it is the time of fireside stories. */
bool hearth_story_time(uint64_t tick);

#endif /* COLONY_CORE_HEARTH_H */
