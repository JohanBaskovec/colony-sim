/*
 * Workstations: places where settlers make things (crafting spot, knapping
 * spot), and the bills the player gives them.
 *
 * A workstation is built, not placed: the player places a blueprint, a
 * builder brings its materials and does the construction work. A station
 * made of nothing (crafting spot) is ready as soon as it is placed. Once built,
 * it carries a list of bills: a recipe (core/recipe.h), a repeat mode (do N
 * times, until N in stock, forever), the input types allowed, how far from
 * the station inputs are searched for, the range of worker levels in the
 * recipe's knowledge, and whether it is paused. Settlers take the first bill
 * that wants doing. A craft in progress keeps its input
 * and its work on the station, so that interrupted work resumes.
 * Design: docs/KNOWLEDGE.md §2.5, §6.3, §8.2.
 */
#ifndef COLONY_CORE_WORKSTATION_H
#define COLONY_CORE_WORKSTATION_H

#include "core/item.h"
#include "core/knowledge.h"
#include "core/map.h"
#include "core/recipe.h"
#include "core/rng.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum WorkstationType {
    WORKSTATION_CRAFTING_SPOT,
    WORKSTATION_KNAPPING_SPOT,
    WORKSTATION_TYPE_COUNT
} WorkstationType;

#define WORKSTATION_BIT(type) (1u << (unsigned)(type))

typedef struct WorkstationDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    const char *description_key; /* One short sentence for the build menu. */
    ItemType material; /* Brought by the builder; ITEM_NONE: none. */
    int material_count;
    /* Work ticks at speed ×1. A station with no material is placed ready,
     * with no construction work. */
    float build_work;
    /* Knowledge needed to build it; KNOWLEDGE_NONE: anyone can. */
    KnowledgeType knowledge;
    float min_level;
} WorkstationDef;

typedef enum BillMode {
    BILL_DO_TIMES,    /* Do it `target` times. */
    BILL_UNTIL_STOCK, /* Until `target` products are in stock (see Game.stock). */
    BILL_FOREVER,     /* Again and again. */
    BILL_MODE_COUNT
} BillMode;

#define BILL_TARGET_MIN 1
#define BILL_TARGET_MAX 999
#define BILL_TARGET_DEFAULT 1

/* How far from the station a bill's inputs are searched for, in travel cost
 * (about one per tile on open ground). */
#define BILL_INPUT_RADIUS_MIN 10
#define BILL_INPUT_RADIUS_MAX 250
#define BILL_INPUT_RADIUS_DEFAULT 80

/* Worker levels a bill accepts by default: everyone who knows the recipe. */
#define BILL_LEVEL_MIN ((int)KNOWLEDGE_LEVEL_MIN)
#define BILL_LEVEL_MAX ((int)KNOWLEDGE_LEVEL_MAX)

typedef struct Bill {
    uint8_t recipe; /* RecipeType */
    uint8_t mode;   /* BillMode */
    uint16_t target;
    uint16_t done; /* Crafts completed (counted by BILL_DO_TIMES). */
    ItemTypeMask inputs; /* Input types allowed, among the recipe's. */
    /* Settlers whose level in the recipe's knowledge lies in this inclusive
     * range may take the bill (beginners for cheap things, masters for the
     * best ones). Has no effect on recipes that need no knowledge. */
    uint8_t min_level;
    uint8_t max_level;
    /* A paused bill starts no craft (one under way on the station is
     * finished, so that the station is not blocked). */
    bool paused;
    uint16_t input_radius; /* BILL_INPUT_RADIUS_MIN..BILL_INPUT_RADIUS_MAX. */
} Bill;

#define WORKSTATION_MAX 64
#define WORKSTATION_NONE (-1)
#define WORKSTATION_BILL_MAX 8

typedef struct Workstation {
    bool used;
    uint8_t type; /* WorkstationType */
    int16_t x, y;
    int number; /* Unique, never reused: tells a station from a later one in the same slot. */
    bool built;
    int materials; /* Delivered so far (blueprint). */
    float build_progress; /* Work done on the blueprint. */
    Bill bills[WORKSTATION_BILL_MAX];
    int bill_count;
    /* Craft in progress: its recipe (RECIPE_COUNT: none), the input put down
     * on the station and the work done. */
    uint8_t work_recipe;
    ItemStack work_input;
    float work_progress;
} Workstation;

typedef struct Workstations {
    int width;
    int height;
    uint8_t *at; /* Workstation index + 1 per tile, 0 when none. */
    Workstation list[WORKSTATION_MAX];
    int used_count;
    int next_number;
    /* Incremented whenever stations or bills change, or a station gets
     * materials or is built: settlers who found nothing to do look again. */
    uint32_t revision;
} Workstations;

/* NULL when out of range. */
const WorkstationDef *workstation_def(WorkstationType type);

bool workstations_init(Workstations *workstations, int width, int height);
void workstations_free(Workstations *workstations);

/* Index of the station on a tile, or WORKSTATION_NONE. */
int workstation_at(const Workstations *workstations, int x, int y);

/* NULL for an unused or invalid index. */
Workstation *workstation_get(Workstations *workstations, int index);
const Workstation *workstation_get_const(const Workstations *workstations, int index);

/* Whether a station may be placed here: passable, buildable ground, no
 * station yet. */
bool workstation_can_place(const Workstations *workstations, const Map *map, int x, int y);

/* Places a blueprint, or the station itself, already built, when it needs
 * no material. Returns its index, or WORKSTATION_NONE when the tile does not
 * qualify or the list is full. */
int workstations_place(Workstations *workstations, const Map *map, WorkstationType type, int x,
                       int y);

/*
 * Removes a station (blueprint or built). What it held is given back in
 * `returned` (up to 2 stacks: materials delivered to a blueprint, input of a
 * craft in progress); returns the number of stacks. The caller drops them.
 */
int workstations_remove(Workstations *workstations, int index, ItemStack returned[2]);

/* Materials still missing on a blueprint (0 once built). */
int workstation_materials_missing(const Workstation *station);

/* Adds delivered materials to a blueprint; returns how many were taken. */
int workstation_deliver(Workstations *workstations, int index, ItemStack stack);

/* Adds construction work (only once all materials are in). Returns true when
 * the station becomes built. */
bool workstation_add_build_work(Workstations *workstations, int index, float work);

/* Fraction of the construction work done, 0..1 (1 once built). */
float workstation_build_fraction(const Workstation *station);

/* Adds a bill for a recipe made at this kind of station (do once, every
 * input allowed). Returns its index, or -1. */
int workstation_add_bill(Workstations *workstations, int index, RecipeType recipe);

/* Removes a bill; later bills move up. The input of a craft in progress for
 * this recipe is given back in `returned` when no other bill makes it
 * (returns whether it was). */
bool workstation_remove_bill(Workstations *workstations, int index, int bill,
                             ItemStack *returned);

/* Changing a bill. Invalid values are refused (targets are clamped to
 * BILL_TARGET_MIN..BILL_TARGET_MAX). */
bool workstation_set_bill_mode(Workstations *workstations, int index, int bill, BillMode mode);
bool workstation_set_bill_target(Workstations *workstations, int index, int bill, int target);
/* Allows or forbids one of the recipe's input types. */
bool workstation_set_bill_input(Workstations *workstations, int index, int bill, ItemType type,
                                bool allowed);
/* Worker level range: each bound is clamped between BILL_LEVEL_MIN, the
 * other bound and BILL_LEVEL_MAX. */
bool workstation_set_bill_min_level(Workstations *workstations, int index, int bill, int level);
bool workstation_set_bill_max_level(Workstations *workstations, int index, int bill, int level);
bool workstation_set_bill_paused(Workstations *workstations, int index, int bill, bool paused);
/* Clamped to BILL_INPUT_RADIUS_MIN..BILL_INPUT_RADIUS_MAX. */
bool workstation_set_bill_input_radius(Workstations *workstations, int index, int bill,
                                       int radius);

/* Whether a settler at `level` in the recipe's knowledge may take the bill
 * (always true for recipes that need no knowledge). */
bool bill_accepts_level(const Bill *bill, double level);

/* Whether a bill still wants doing, given how many of its product are in
 * stock (only used by BILL_UNTIL_STOCK). Paused or not: see Bill.paused. */
bool bill_wants_more(const Bill *bill, int stored);

/*
 * Whether enough of an input type the bill allows lies in the colony
 * (`totals`: items lying per type), once the tools still to fetch (`tools`:
 * one item of each non-empty set, 0 for none) have taken theirs: knapping
 * with a cobble as a hammer needs a second stone to knap.
 */
bool bill_has_input(const Bill *bill, const long totals[ITEM_TYPE_COUNT],
                    const ItemTypeMask tools[RECIPE_TOOL_MAX]);

/*
 * Starts a craft of `recipe` with `input` (the station must hold no other
 * input). Returns false, changing nothing, if one is already in progress.
 */
bool workstation_start_craft(Workstations *workstations, int index, RecipeType recipe,
                             ItemStack input);

/*
 * Adds work to the craft in progress. When it is complete, the products are
 * dropped on the station's tile, the bill `bill` (or the first one of the
 * same recipe if that one changed) counts it, and true is returned. Products
 * come from the worker's `level` (see recipe_craft()).
 */
bool workstation_add_craft_work(Workstations *workstations, int index, int bill, Items *items,
                                const Map *map, float work, double level, Rng *rng);

uint64_t workstations_hash(const Workstations *workstations, uint64_t hash);

#endif /* COLONY_CORE_WORKSTATION_H */
