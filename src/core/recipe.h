/*
 * Recipes: what settlers make at workstations (knapping flakes, making a
 * spear...).
 *
 * A recipe takes one kind of input (with alternatives, in order of
 * preference: flint or a cobble), needs tools that are used but not consumed
 * (one of each listed set; any item can serve, e.g. a cobble as a hammer), a
 * workstation, an amount of work, and a minimum level in a knowledge. What
 * comes out, and how good it is, depends on the worker's level and on the
 * input: the grade of a product is capped at the grade of its material + 1,
 * and a cobble knapped instead of flint counts as crude. Some recipes can
 * fail (shaping a hand axe).
 * Design: docs/KNOWLEDGE.md §2.2, §2.5, §6.2, §8.2.
 */
#ifndef COLONY_CORE_RECIPE_H
#define COLONY_CORE_RECIPE_H

#include "core/item.h"
#include "core/knowledge.h"
#include "core/rng.h"
#include "core/tool.h"

#include <stdbool.h>

typedef enum RecipeType {
    RECIPE_CHOPPER,
    RECIPE_FLAKES,
    RECIPE_HAND_AXE,
    RECIPE_THROWING_SPEAR,
    RECIPE_THRUSTING_SPEAR,
    RECIPE_COUNT
} RecipeType;

#define RECIPE_INPUT_ALTERNATIVES 2
#define RECIPE_TOOL_MAX 2
/* Most stacks one craft gives (a hand axe and its flakes). */
#define RECIPE_PRODUCT_MAX 2

typedef struct RecipeDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    unsigned stations; /* Workstations it is made at: bits of WorkstationType. */
    /* Input types, in order of preference (ITEM_NONE: unused). */
    ItemType inputs[RECIPE_INPUT_ALTERNATIVES];
    int input_count;
    ItemQuality input_min_quality;
    /* Tools: one item of each non-empty set must be carried. */
    ItemTypeMask tools[RECIPE_TOOL_MAX];
    float work; /* Work ticks at speed ×1. */
    /* Knowledge needed; KNOWLEDGE_NONE: anyone can make it. */
    KnowledgeType knowledge;
    float min_level;
    ItemType product; /* Main product (what a bill counts in storage). */
    /* Dirt left by each craft (chips, shavings) on the station's tile, half
     * as much around it (see dirt_spread()). */
    int dirt;
    /* Kind of work: the speed factor of the tool of the first set (see
     * core/tool.h). */
    ToolUse tool_use;
} RecipeDef;

/* NULL when out of range. */
const RecipeDef *recipe_def(RecipeType recipe);

/* Every input type a recipe accepts. */
ItemTypeMask recipe_inputs(RecipeType recipe);

/* Whether a stack can be the input of a recipe (type and grade; not the count). */
bool recipe_accepts_input(RecipeType recipe, ItemStack stack);

/* Whether a settler at `level` in the recipe's knowledge can make it. */
bool recipe_doable_at(RecipeType recipe, double level);

/* Best grade the products can have with this input. */
ItemQuality recipe_quality_cap(RecipeType recipe, ItemStack input);

/* Chance (0..1) that a craft at this level fails. */
float recipe_failure_chance(RecipeType recipe, double level);

/*
 * Products of one completed craft from `input` by a worker at `level`:
 * writes up to RECIPE_PRODUCT_MAX stacks to `out` and returns how many.
 * `rng` may be NULL (average products, never a failure).
 */
int recipe_craft(RecipeType recipe, ItemStack input, double level, Rng *rng,
                 ItemStack out[RECIPE_PRODUCT_MAX]);

#endif /* COLONY_CORE_RECIPE_H */
