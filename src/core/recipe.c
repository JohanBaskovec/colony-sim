#include "core/recipe.h"

#include "core/i18n.h"
#include "core/util.h"
#include "core/workstation.h"

#include <math.h>

/*
 * Flakes struck from one stone: FLAKES_BASE plus one per FLAKES_LEVEL_STEP
 * points above mastery (3 at 50, 8 at 100). Shaping a hand axe gives
 * HAND_AXE_FLAKES flakes besides the axe; it fails with chance
 * HAND_AXE_FAILURE_AT_MIN at its minimum level, HAND_AXE_FAILURE_PER_LEVEL
 * less per point above (none from 90), and a failure leaves
 * FAILED_FLAKES_BASE flakes plus one per FLAKES_LEVEL_STEP points above
 * mastery. Values from docs/KNOWLEDGE.md §6.2 (tuning).
 */
#define FLAKES_BASE 3
#define FLAKES_LEVEL_STEP 10.0
#define HAND_AXE_FLAKES 2
#define HAND_AXE_FAILURE_AT_MIN 0.40f
#define HAND_AXE_FAILURE_PER_LEVEL 0.02f
#define FAILED_FLAKES_BASE 1

#define KNAPPING_SPOT WORKSTATION_BIT(WORKSTATION_KNAPPING_SPOT)
#define CRAFTING_SPOT WORKSTATION_BIT(WORKSTATION_CRAFTING_SPOT)
#define OPEN_HEARTH WORKSTATION_BIT(WORKSTATION_OPEN_HEARTH)
/* Knapping is done with a plain cobble as a hammer. */
#define HAMMER ITEM_TYPE_BIT(ITEM_COBBLE)
#define CUTTING_TOOL (ITEM_TYPE_BIT(ITEM_FLAKE) | ITEM_TYPE_BIT(ITEM_HAND_AXE))

static const RecipeDef RECIPE_DEFS[RECIPE_COUNT] = {
    [RECIPE_CHOPPER] = {"chopper", N_("recipe.chopper"), KNAPPING_SPOT, {ITEM_COBBLE, ITEM_FLINT},
                        1, ITEM_QUALITY_CRUDE, {HAMMER, 0}, 250.0f, KNOWLEDGE_STONE_KNAPPING,
                        50.0f, ITEM_CHOPPER, 6, TOOL_USE_KNAPPING},
    [RECIPE_FLAKES] = {"flakes", N_("recipe.flakes"), KNAPPING_SPOT, {ITEM_FLINT, ITEM_COBBLE}, 1,
                       ITEM_QUALITY_CRUDE, {HAMMER, 0}, 375.0f, KNOWLEDGE_STONE_KNAPPING,
                       55.0f, ITEM_FLAKE, 8, TOOL_USE_KNAPPING},
    [RECIPE_HAND_AXE] = {"hand_axe", N_("recipe.hand_axe"), KNAPPING_SPOT,
                         {ITEM_FLINT, ITEM_COBBLE}, 1, ITEM_QUALITY_CRUDE, {HAMMER, 0},
                         2500.0f, KNOWLEDGE_STONE_KNAPPING, 70.0f, ITEM_HAND_AXE, 12,
                         TOOL_USE_KNAPPING},
    /* Spears want straight branches: standard grade or better. */
    [RECIPE_THROWING_SPEAR] = {"throwing_spear", N_("recipe.throwing_spear"), CRAFTING_SPOT,
                               {ITEM_BRANCHES, ITEM_NONE}, 1, ITEM_QUALITY_STANDARD,
                               {CUTTING_TOOL, 0}, 3750.0f, KNOWLEDGE_SPEAR_MAKING, 50.0f,
                               ITEM_THROWING_SPEAR, 4, TOOL_USE_WOODWORKING},
    [RECIPE_THRUSTING_SPEAR] = {"thrusting_spear", N_("recipe.thrusting_spear"), CRAFTING_SPOT,
                                {ITEM_BRANCHES, ITEM_NONE}, 2, ITEM_QUALITY_STANDARD,
                                {ITEM_TYPE_BIT(ITEM_HAND_AXE), ITEM_TYPE_BIT(ITEM_FLAKE)},
                                6250.0f, KNOWLEDGE_SPEAR_MAKING, 60.0f, ITEM_THRUSTING_SPEAR, 6,
                                TOOL_USE_WOODWORKING},
    /* Drying and charring a wooden point over the fire. */
    [RECIPE_HARDEN_THROWING_SPEAR] = {"harden_throwing_spear",
                                      N_("recipe.harden_throwing_spear"), OPEN_HEARTH,
                                      {ITEM_THROWING_SPEAR, ITEM_NONE}, 1, ITEM_QUALITY_CRUDE,
                                      {0, 0}, 750.0f, KNOWLEDGE_FIRE, 55.0f,
                                      ITEM_THROWING_SPEAR_HARDENED, 0, TOOL_USE_NONE},
    [RECIPE_HARDEN_THRUSTING_SPEAR] = {"harden_thrusting_spear",
                                       N_("recipe.harden_thrusting_spear"), OPEN_HEARTH,
                                       {ITEM_THRUSTING_SPEAR, ITEM_NONE}, 1, ITEM_QUALITY_CRUDE,
                                       {0, 0}, 750.0f, KNOWLEDGE_FIRE, 55.0f,
                                       ITEM_THRUSTING_SPEAR_HARDENED, 0, TOOL_USE_NONE},
    /* A stout branch, its end cut to a point: part of gathering. */
    [RECIPE_DIGGING_STICK] = {"digging_stick", N_("recipe.digging_stick"), CRAFTING_SPOT,
                              {ITEM_BRANCHES, ITEM_NONE}, 1, ITEM_QUALITY_CRUDE,
                              {CUTTING_TOOL, 0}, 375.0f, KNOWLEDGE_GATHERING, 50.0f,
                              ITEM_DIGGING_STICK, 2, TOOL_USE_WOODWORKING},
    [RECIPE_HARDEN_DIGGING_STICK] = {"harden_digging_stick", N_("recipe.harden_digging_stick"),
                                     OPEN_HEARTH, {ITEM_DIGGING_STICK, ITEM_NONE}, 1,
                                     ITEM_QUALITY_CRUDE, {0, 0}, 750.0f, KNOWLEDGE_FIRE, 55.0f,
                                     ITEM_DIGGING_STICK_HARDENED, 0, TOOL_USE_NONE},
    /* Roots roasted in the embers: more of their starch is digested. */
    [RECIPE_ROAST_RHIZOMES] = {"roast_rhizomes", N_("recipe.roast_rhizomes"), OPEN_HEARTH,
                               {ITEM_CATTAIL_RHIZOMES, ITEM_NONE}, 4, ITEM_QUALITY_CRUDE,
                               {0, 0}, 250.0f, KNOWLEDGE_FIRE, 50.0f, ITEM_ROASTED_RHIZOMES, 0,
                               TOOL_USE_NONE},
    [RECIPE_ROAST_CARROTS] = {"roast_carrots", N_("recipe.roast_carrots"), OPEN_HEARTH,
                              {ITEM_WILD_CARROTS, ITEM_NONE}, 4, ITEM_QUALITY_CRUDE, {0, 0},
                              250.0f, KNOWLEDGE_FIRE, 50.0f, ITEM_ROASTED_CARROTS, 0,
                              TOOL_USE_NONE},
};

#undef KNAPPING_SPOT
#undef CRAFTING_SPOT
#undef OPEN_HEARTH
#undef HAMMER
#undef CUTTING_TOOL

const RecipeDef *recipe_def(RecipeType recipe)
{
    return (unsigned)recipe < RECIPE_COUNT ? &RECIPE_DEFS[recipe] : NULL;
}

ItemTypeMask recipe_inputs(RecipeType recipe)
{
    const RecipeDef *def = recipe_def(recipe);
    ItemTypeMask mask = 0;
    for (int i = 0; def != NULL && i < RECIPE_INPUT_ALTERNATIVES; i++) {
        if (def->inputs[i] != ITEM_NONE) {
            mask |= ITEM_TYPE_BIT(def->inputs[i]);
        }
    }
    return mask;
}

bool recipe_accepts_input(RecipeType recipe, ItemStack stack)
{
    const RecipeDef *def = recipe_def(recipe);
    return def != NULL && stack.type != ITEM_NONE &&
           (recipe_inputs(recipe) & ITEM_TYPE_BIT(stack.type)) &&
           stack.quality >= def->input_min_quality;
}

bool recipe_doable_at(RecipeType recipe, double level)
{
    const RecipeDef *def = recipe_def(recipe);
    return def != NULL && (def->knowledge == KNOWLEDGE_NONE || level >= def->min_level);
}

/* Knapping a cobble rather than flint: it counts as crude whatever its grade. */
static bool knaps_a_cobble(const RecipeDef *def, ItemStack input)
{
    return def->knowledge == KNOWLEDGE_STONE_KNAPPING && input.type == ITEM_COBBLE;
}

ItemQuality recipe_quality_cap(RecipeType recipe, ItemStack input)
{
    const RecipeDef *def = recipe_def(recipe);
    const ItemDef *material = item_def((ItemType)input.type);
    if (def == NULL || material == NULL || !material->has_quality) {
        return ITEM_QUALITY_BEST;
    }
    int grade = knaps_a_cobble(def, input) ? ITEM_QUALITY_CRUDE : input.quality;
    return (ItemQuality)clampi(grade + 1, ITEM_QUALITY_CRUDE, ITEM_QUALITY_BEST);
}

bool recipe_needs_fire(RecipeType recipe)
{
    const RecipeDef *def = recipe_def(recipe);
    return def != NULL && (def->stations & WORKSTATION_BIT(WORKSTATION_OPEN_HEARTH));
}

float recipe_failure_chance(RecipeType recipe, double level)
{
    if (recipe != RECIPE_HAND_AXE) {
        return 0.0f;
    }
    float above = (float)level - RECIPE_DEFS[recipe].min_level;
    return clampf(HAND_AXE_FAILURE_AT_MIN - HAND_AXE_FAILURE_PER_LEVEL * above, 0.0f, 1.0f);
}

/* Points above mastery, never negative. */
static int steps_above_mastery(double level)
{
    double above = level - KNOWLEDGE_MASTERY_LEVEL;
    return above > 0.0 ? (int)floor(above / FLAKES_LEVEL_STEP) : 0;
}

/* `count` items of a product made at `level`, no better than `cap`. */
static ItemStack product(ItemType type, int count, double level, Rng *rng, ItemQuality cap)
{
    ItemStack stack = knowledge_product(type, count, level, rng);
    if (stack.count > 0 && stack.quality > cap) {
        stack.quality = (uint8_t)cap;
    }
    return stack;
}

int recipe_craft(RecipeType recipe, ItemStack input, double level, Rng *rng,
                 ItemStack out[RECIPE_PRODUCT_MAX])
{
    const RecipeDef *def = recipe_def(recipe);
    if (def == NULL) {
        return 0;
    }
    ItemQuality cap = recipe_quality_cap(recipe, input);
    switch (recipe) {
    case RECIPE_FLAKES:
        out[0] = product(ITEM_FLAKE, FLAKES_BASE + steps_above_mastery(level), level, rng, cap);
        return 1;
    case RECIPE_HAND_AXE: {
        float failure = recipe_failure_chance(recipe, level);
        if (rng != NULL && failure > 0.0f && rng_next_float(rng) < failure) {
            /* The stone is lost; some flakes are left. */
            out[0] = product(ITEM_FLAKE, FAILED_FLAKES_BASE + steps_above_mastery(level), level,
                             rng, cap);
            return 1;
        }
        out[0] = product(ITEM_HAND_AXE, 1, level, rng, cap);
        out[1] = product(ITEM_FLAKE, HAND_AXE_FLAKES, level, rng, cap);
        return 2;
    }
    case RECIPE_HARDEN_THROWING_SPEAR:
    case RECIPE_HARDEN_THRUSTING_SPEAR:
    case RECIPE_HARDEN_DIGGING_STICK:
        /* The same spear or stick, its grade and wear kept, lasting longer. */
        out[0] = item_stack_make(def->product, 1, input.quality,
                                 item_hit_points_at_percent(
                                     def->product, item_stack_hit_points_percent(input)));
        return 1;
    case RECIPE_ROAST_RHIZOMES:
    case RECIPE_ROAST_CARROTS: {
        /* One roasted root per raw one, no fresher than they were. */
        out[0] = product(def->product, def->input_count, level, rng, cap);
        int percent = item_stack_hit_points_percent(input);
        if (out[0].count > 0 && item_stack_hit_points_percent(out[0]) > percent) {
            out[0].hit_points = (uint16_t)item_hit_points_at_percent(def->product, percent);
        }
        return 1;
    }
    default: out[0] = product(def->product, 1, level, rng, cap); return 1;
    }
}
