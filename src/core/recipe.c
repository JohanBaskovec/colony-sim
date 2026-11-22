#include "core/recipe.h"

#include "core/i18n.h"
#include "core/util.h"
#include "core/wood.h"
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

#define CRAFTING_SPOT WORKSTATION_BIT(WORKSTATION_CRAFTING_SPOT)
#define OPEN_HEARTH WORKSTATION_BIT(WORKSTATION_OPEN_HEARTH)
/* Knapping is done with a plain cobble as a hammer. */
#define HAMMER                                                                                     \
    {                                                                                              \
        ITEM_COBBLE                                                                                \
    }
#define CUTTING_TOOL                                                                               \
    {                                                                                              \
        ITEM_FLAKE, ITEM_HAND_AXE                                                                  \
    }

static const RecipeDef RECIPE_DEFS[RECIPE_COUNT] = {
    [RECIPE_CHOPPER] = {.id = "chopper",
                        .name_key = N_("recipe.chopper"),
                        .stations = CRAFTING_SPOT,
                        .ingredients = {{{ITEM_COBBLE, ITEM_FLINT}, 1, ITEM_WANT_ANY}},
                        .input_min_quality = ITEM_QUALITY_WORST,
                        .tools = {HAMMER},
                        .work = 250.0f,
                        .knowledge = KNOWLEDGE_STONE_KNAPPING,
                        .min_level = 50.0f,
                        .product = ITEM_CHOPPER,
                        .dirt = 6,
                        .tool_use = TOOL_USE_KNAPPING},
    [RECIPE_FLAKES] = {.id = "flakes",
                       .name_key = N_("recipe.flakes"),
                       .stations = CRAFTING_SPOT,
                       .ingredients = {{{ITEM_FLINT, ITEM_COBBLE}, 1, ITEM_WANT_ANY}},
                       .input_min_quality = ITEM_QUALITY_WORST,
                       .tools = {HAMMER},
                       .work = 375.0f,
                       .knowledge = KNOWLEDGE_STONE_KNAPPING,
                       .min_level = 55.0f,
                       .product = ITEM_FLAKE,
                       .dirt = 8,
                       .tool_use = TOOL_USE_KNAPPING},
    [RECIPE_HAND_AXE] = {.id = "hand_axe",
                         .name_key = N_("recipe.hand_axe"),
                         .stations = CRAFTING_SPOT,
                         .ingredients = {{{ITEM_FLINT, ITEM_COBBLE}, 1, ITEM_WANT_ANY}},
                         .input_min_quality = ITEM_QUALITY_WORST,
                         .tools = {HAMMER},
                         .work = 2500.0f,
                         .knowledge = KNOWLEDGE_STONE_KNAPPING,
                         .min_level = 70.0f,
                         .product = ITEM_HAND_AXE,
                         .dirt = 12,
                         .tool_use = TOOL_USE_KNAPPING},
    /* Spears want straight branches: standard quality or better. */
    [RECIPE_THROWING_SPEAR] = {.id = "throwing_spear",
                               .name_key = N_("recipe.throwing_spear"),
                               .stations = CRAFTING_SPOT,
                               .ingredients = {{{ITEM_BRANCHES, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                               .input_min_quality = ITEM_QUALITY_STANDARD,
                               .tools = {CUTTING_TOOL},
                               .work = 3750.0f,
                               .knowledge = KNOWLEDGE_SPEAR_MAKING,
                               .min_level = 50.0f,
                               .product = ITEM_THROWING_SPEAR,
                               .dirt = 4,
                               .tool_use = TOOL_USE_WOODWORKING},
    [RECIPE_THRUSTING_SPEAR] = {.id = "thrusting_spear",
                                .name_key = N_("recipe.thrusting_spear"),
                                .stations = CRAFTING_SPOT,
                                .ingredients = {{{ITEM_BRANCHES, ITEM_NONE}, 2, ITEM_WANT_ANY}},
                                .input_min_quality = ITEM_QUALITY_STANDARD,
                                .tools = {{ITEM_HAND_AXE}, {ITEM_FLAKE}},
                                .work = 6250.0f,
                                .knowledge = KNOWLEDGE_SPEAR_MAKING,
                                .min_level = 60.0f,
                                .product = ITEM_THRUSTING_SPEAR,
                                .dirt = 6,
                                .tool_use = TOOL_USE_WOODWORKING},
    /* Drying and charring a wooden point over the fire. */
    [RECIPE_HARDEN_THROWING_SPEAR] =
        {.id = "harden_throwing_spear",
         .name_key = N_("recipe.harden_throwing_spear"),
         .stations = OPEN_HEARTH,
         .ingredients = {{{ITEM_THROWING_SPEAR, ITEM_NONE}, 1, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {{ITEM_NONE}},
         .work = 750.0f,
         .knowledge = KNOWLEDGE_FIRE,
         .min_level = 55.0f,
         .product = ITEM_THROWING_SPEAR_HARDENED,
         .dirt = 0,
         .tool_use = TOOL_USE_NONE},
    [RECIPE_HARDEN_THRUSTING_SPEAR] =
        {.id = "harden_thrusting_spear",
         .name_key = N_("recipe.harden_thrusting_spear"),
         .stations = OPEN_HEARTH,
         .ingredients = {{{ITEM_THRUSTING_SPEAR, ITEM_NONE}, 1, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {{ITEM_NONE}},
         .work = 750.0f,
         .knowledge = KNOWLEDGE_FIRE,
         .min_level = 55.0f,
         .product = ITEM_THRUSTING_SPEAR_HARDENED,
         .dirt = 0,
         .tool_use = TOOL_USE_NONE},
    /* A stout branch, its end cut to a point: part of gathering. */
    [RECIPE_DIGGING_STICK] = {.id = "digging_stick",
                              .name_key = N_("recipe.digging_stick"),
                              .stations = CRAFTING_SPOT,
                              .ingredients = {{{ITEM_BRANCHES, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                              .input_min_quality = ITEM_QUALITY_WORST,
                              .tools = {CUTTING_TOOL},
                              .work = 375.0f,
                              .knowledge = KNOWLEDGE_GATHERING,
                              .min_level = 50.0f,
                              .product = ITEM_DIGGING_STICK,
                              .dirt = 2,
                              .tool_use = TOOL_USE_WOODWORKING},
    [RECIPE_HARDEN_DIGGING_STICK] =
        {.id = "harden_digging_stick",
         .name_key = N_("recipe.harden_digging_stick"),
         .stations = OPEN_HEARTH,
         .ingredients = {{{ITEM_DIGGING_STICK, ITEM_NONE}, 1, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {{ITEM_NONE}},
         .work = 750.0f,
         .knowledge = KNOWLEDGE_FIRE,
         .min_level = 55.0f,
         .product = ITEM_DIGGING_STICK_HARDENED,
         .dirt = 0,
         .tool_use = TOOL_USE_NONE},
    /* Roots roasted in the embers: more of their starch is digested. */
    [RECIPE_ROAST_RHIZOMES] =
        {.id = "roast_rhizomes",
         .name_key = N_("recipe.roast_rhizomes"),
         .stations = OPEN_HEARTH,
         .ingredients = {{{ITEM_CATTAIL_RHIZOMES, ITEM_NONE}, 4, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {{ITEM_NONE}},
         .work = 250.0f,
         .knowledge = KNOWLEDGE_FIRE,
         .min_level = 50.0f,
         .product = ITEM_ROASTED_RHIZOMES,
         .dirt = 0,
         .tool_use = TOOL_USE_NONE},
    [RECIPE_ROAST_CARROTS] = {.id = "roast_carrots",
                              .name_key = N_("recipe.roast_carrots"),
                              .stations = OPEN_HEARTH,
                              .ingredients = {{{ITEM_WILD_CARROTS, ITEM_NONE}, 4, ITEM_WANT_ANY}},
                              .input_min_quality = ITEM_QUALITY_WORST,
                              .tools = {{ITEM_NONE}},
                              .work = 250.0f,
                              .knowledge = KNOWLEDGE_FIRE,
                              .min_level = 50.0f,
                              .product = ITEM_ROASTED_CARROTS,
                              .dirt = 0,
                              .tool_use = TOOL_USE_NONE},
    /* A branch split and its edge rounded: the blade shapes it (docs/CONTAINERS.md §5). */
    [RECIPE_WOODEN_SCRAPER] = {.id = "wooden_scraper",
                               .name_key = N_("recipe.wooden_scraper"),
                               .stations = CRAFTING_SPOT,
                               .ingredients = {{{ITEM_BRANCHES, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                               .input_min_quality = ITEM_QUALITY_WORST,
                               .tools = {CUTTING_TOOL},
                               .tool_shapes = true,
                               .work = 750.0f,
                               .knowledge = KNOWLEDGE_CORDAGE,
                               .min_level = 50.0f,
                               .product = ITEM_WOODEN_SCRAPER,
                               .dirt = 3,
                               .tool_use = TOOL_USE_WOODWORKING},
    /* A sharp flake retouched to a point with the hammer stone. */
    [RECIPE_FLINT_BORER] = {.id = "flint_borer",
                            .name_key = N_("recipe.flint_borer"),
                            .stations = CRAFTING_SPOT,
                            .ingredients = {{{ITEM_FLAKE, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                            .input_min_quality = ITEM_QUALITY_WORST,
                            .tools = {HAMMER},
                            .work = 500.0f,
                            .knowledge = KNOWLEDGE_STONE_KNAPPING,
                            .min_level = 60.0f,
                            .product = ITEM_FLINT_BORER,
                            .dirt = 2,
                            .tool_use = TOOL_USE_KNAPPING},
    /* A sheet of bark scored, folded at the corners and pinned with twigs:
     * pliable bark only (fresh, or softened in water), dry bark cracks. */
    [RECIPE_BARK_TRAY] = {.id = "bark_tray",
                          .name_key = N_("recipe.bark_tray"),
                          .stations = CRAFTING_SPOT,
                          .ingredients = {{{ITEM_BIRCH_BARK, ITEM_NONE}, 1, ITEM_WANT_WET},
                                          {{ITEM_TWIGS, ITEM_NONE}, 2, ITEM_WANT_ANY}},
                          .input_min_quality = ITEM_QUALITY_WORST,
                          .tools = {{ITEM_FLAKE}},
                          .tool_shapes = true,
                          .work = 1250.0f,
                          .knowledge = KNOWLEDGE_BASKETRY,
                          .min_level = 50.0f,
                          .product = ITEM_BARK_TRAY,
                          .dirt = 2,
                          .tool_use = TOOL_USE_CUTTING},
    /* The dry stems broken on a log with a cobble, then their fibre scraped
     * clean of bark and pith with a dull wooden edge (docs/CONTAINERS.md §4.1). */
    [RECIPE_NETTLE_FIBRE] = {.id = "nettle_fibre",
                             .name_key = N_("recipe.nettle_fibre"),
                             .stations = CRAFTING_SPOT,
                             .ingredients = {{{ITEM_NETTLE_STEMS, ITEM_NONE}, 1, ITEM_WANT_DRY}},
                             .input_min_quality = ITEM_QUALITY_WORST,
                             .tools = {{ITEM_WOODEN_SCRAPER, ITEM_FLAKE}, HAMMER},
                             .tool_shapes = true,
                             .work = 2500.0f, /* An hour a bundle. */
                             .knowledge = KNOWLEDGE_CORDAGE,
                             .min_level = 50.0f,
                             .product = ITEM_NETTLE_FIBRE,
                             .dirt = 4, /* Shives and dust. */
                             .tool_use = TOOL_USE_SCRAPING},
    /* Two-ply cord twisted by reverse wrapping, with bare hands: a handful of
     * fibre makes two hanks of 5 m, 0.75 h each. */
    [RECIPE_NETTLE_CORD] = {.id = "nettle_cord",
                            .name_key = N_("recipe.nettle_cord"),
                            .stations = CRAFTING_SPOT,
                            .ingredients = {{{ITEM_NETTLE_FIBRE, ITEM_NONE}, 1, ITEM_WANT_ANY}},
                            .input_min_quality = ITEM_QUALITY_WORST,
                            .tools = {{ITEM_NONE}},
                            .work = 3750.0f,
                            .knowledge = KNOWLEDGE_CORDAGE,
                            .min_level = 50.0f,
                            .product = ITEM_NETTLE_CORD,
                            .product_count = 2,
                            .dirt = 0,
                            .tool_use = TOOL_USE_NONE},
    /* Three bundles of leaves plaited by hand, trimmed with a flake: 5 h
     * (docs/CONTAINERS.md §7.1). Woven green, it works loose as it dries. */
    [RECIPE_CATTAIL_BASKET] =
        {.id = "cattail_basket",
         .name_key = N_("recipe.cattail_basket"),
         .stations = CRAFTING_SPOT,
         .ingredients = {{{ITEM_CATTAIL_LEAVES, ITEM_NONE}, 3, ITEM_WANT_ANY}},
         .input_min_quality = ITEM_QUALITY_WORST,
         .tools = {{ITEM_FLAKE}},
         .work = 12500.0f,
         .knowledge = KNOWLEDGE_BASKETRY,
         .min_level = 55.0f,
         .product = ITEM_CATTAIL_BASKET,
         .dirt = 2,
         .tool_use = TOOL_USE_NONE},
};

#undef CRAFTING_SPOT
#undef OPEN_HEARTH
#undef HAMMER
#undef CUTTING_TOOL

const RecipeDef *recipe_def(RecipeType recipe)
{
    return (unsigned)recipe < RECIPE_COUNT ? &RECIPE_DEFS[recipe] : NULL;
}

const RecipeIngredient *recipe_ingredient(RecipeType recipe, int index)
{
    const RecipeDef *def = recipe_def(recipe);
    return def != NULL && index >= 0 && index < RECIPE_INGREDIENT_MAX &&
                   def->ingredients[index].count > 0
               ? &def->ingredients[index]
               : NULL;
}

int recipe_ingredient_count(RecipeType recipe)
{
    int count = 0;
    while (recipe_ingredient(recipe, count) != NULL) {
        count++;
    }
    return count;
}

ItemTypeMask recipe_ingredient_types(RecipeType recipe, int index)
{
    const RecipeIngredient *ingredient = recipe_ingredient(recipe, index);
    ItemTypeMask mask = item_types_none();
    for (int i = 0; ingredient != NULL && i < RECIPE_INPUT_ALTERNATIVES; i++) {
        if (ingredient->types[i] != ITEM_NONE) {
            item_types_add(&mask, ingredient->types[i]);
        }
    }
    return mask;
}

ItemTypeMask recipe_inputs(RecipeType recipe)
{
    ItemTypeMask mask = item_types_none();
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        mask = item_types_union(mask, recipe_ingredient_types(recipe, i));
    }
    return mask;
}

ItemTypeMask recipe_tool_types(const RecipeDef *def, int slot)
{
    if (def == NULL || slot < 0 || slot >= RECIPE_TOOL_MAX) {
        return item_types_none();
    }
    return item_types_list(def->tools[slot], RECIPE_TOOL_ALTERNATIVES);
}

int recipe_ingredient_of(RecipeType recipe, ItemType type)
{
    if ((unsigned)type >= ITEM_TYPE_COUNT || type == ITEM_NONE) {
        return -1;
    }
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        if (item_types_has(recipe_ingredient_types(recipe, i), type)) {
            return i;
        }
    }
    return -1;
}

bool recipe_accepts_ingredient(RecipeType recipe, int index, ItemStack stack)
{
    const RecipeDef *def = recipe_def(recipe);
    return def != NULL && stack.type != ITEM_NONE &&
           item_types_has(recipe_ingredient_types(recipe, index), (ItemType)stack.type) &&
           stack.quality >= def->input_min_quality;
}

bool recipe_accepts_input(RecipeType recipe, ItemStack stack)
{
    return recipe_accepts_ingredient(recipe, recipe_ingredient_of(recipe, (ItemType)stack.type),
                                     stack);
}

bool recipe_doable_at(RecipeType recipe, double level)
{
    const RecipeDef *def = recipe_def(recipe);
    return def != NULL && (def->knowledge == KNOWLEDGE_NONE || level >= def->min_level);
}

/* Knapping a cobble rather than flint: it counts as a crude stone (this
 * quality) whatever its own. */
#define COBBLE_KNAPPING_QUALITY 10
/* A tool that shapes the material counts for this share of the average
 * quality that caps the products (docs/CONTAINERS.md §6). */
#define SHAPING_TOOL_SHARE 0.25
/* A flake scrapes fibre too sharply: it cuts some of it (docs/CONTAINERS.md §5). */
#define FLAKE_SCRAPING_CAP_MALUS 10
/* Green cattail leaves shrink as they dry: the basket works loose (§4.3). */
#define GREEN_WEAVING_CAP_MALUS 15

static bool knaps_a_cobble(const RecipeDef *def, ItemStack input)
{
    return def->knowledge == KNOWLEDGE_STONE_KNAPPING && input.type == ITEM_COBBLE;
}

/* The quality an ingredient counts for in the cap. */
static int material_quality(const RecipeDef *def, ItemStack input)
{
    return knaps_a_cobble(def, input) ? COBBLE_KNAPPING_QUALITY : input.quality;
}

ItemQuality recipe_quality_cap_of(RecipeType recipe, const ItemStack inputs[RECIPE_INGREDIENT_MAX],
                                  ItemStack tool)
{
    const RecipeDef *def = recipe_def(recipe);
    if (def == NULL) {
        return ITEM_QUALITY_BEST;
    }
    double sum = 0.0;
    long count = 0;
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        const ItemDef *material = item_def((ItemType)inputs[i].type);
        if (inputs[i].count > 0 && material != NULL && material->has_quality) {
            sum += (double)material_quality(def, inputs[i]) * inputs[i].count;
            count += inputs[i].count;
        }
    }
    const ItemDef *shaping = item_def((ItemType)tool.type);
    bool tool_counts =
        def->tool_shapes && tool.count > 0 && shaping != NULL && shaping->has_quality;
    if (count == 0 && !tool_counts) {
        return ITEM_QUALITY_BEST; /* Nothing caps it. */
    }
    double base = count > 0 ? sum / (double)count : (double)tool.quality;
    if (tool_counts && count > 0) {
        base = (1.0 - SHAPING_TOOL_SHARE) * base + SHAPING_TOOL_SHARE * (double)tool.quality;
    }
    int cap = (int)floor(base + 0.5) + ITEM_QUALITY_CAP_BONUS;
    /* Woven green, what shrinks as it dries works loose. */
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        const ItemDef *material = item_def((ItemType)inputs[i].type);
        if (inputs[i].count > 0 && material != NULL && material->shrinks_green &&
            item_moisture_green((ItemType)inputs[i].type, item_stack_moisture(inputs[i]))) {
            cap -= GREEN_WEAVING_CAP_MALUS;
            break;
        }
    }
    /* Retted stems give a finer fibre; over retted, a rotten one. */
    if (inputs[0].count > 0 && item_def((ItemType)inputs[0].type) != NULL &&
        item_def((ItemType)inputs[0].type)->rets) {
        cap += item_retting_cap_bonus(item_stack_retting(inputs[0]));
    }
    if (def->tool_use == TOOL_USE_SCRAPING && tool.count > 0 && tool.type == ITEM_FLAKE) {
        cap -= FLAKE_SCRAPING_CAP_MALUS;
    }
    return clampi(cap, ITEM_QUALITY_WORST, ITEM_QUALITY_BEST);
}

ItemQuality recipe_quality_cap(RecipeType recipe, ItemStack input)
{
    ItemStack inputs[RECIPE_INGREDIENT_MAX] = {input};
    return recipe_quality_cap_of(recipe, inputs, (ItemStack){.type = ITEM_NONE});
}

float recipe_work_factor(RecipeType recipe, const ItemStack inputs[RECIPE_INGREDIENT_MAX])
{
    const ItemDef *main = item_def((ItemType)inputs[0].type);
    if (recipe_def(recipe) == NULL || inputs[0].count <= 0 || main == NULL || !main->rets) {
        return 1.0f;
    }
    return item_retting_work_factor(item_stack_retting(inputs[0]));
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

/* The products of recipe_craft_with(), before the wood species: `input` is the
 * main ingredient (the first), `cap` the products' best quality. */
static int craft_products(RecipeType recipe, const RecipeDef *def, ItemStack input, ItemQuality cap,
                          double level, Rng *rng, ItemStack out[RECIPE_PRODUCT_MAX])
{
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
        /* The same spear or stick, its quality and wear kept, lasting longer. */
        out[0] = item_stack_make(
            def->product, 1, input.quality,
            item_hit_points_at_percent(def->product, item_stack_hit_points_percent(input)));
        return 1;
    case RECIPE_ROAST_RHIZOMES:
    case RECIPE_ROAST_CARROTS: {
        /* One roasted root per raw one, keeping no longer than the raw ones
         * would have (as long, though: a root about to rot does not rot
         * sooner for being roasted). */
        out[0] = product(def->product, def->ingredients[0].count, level, rng, cap);
        float hours = item_stack_rot_hours(input, 1.0f);
        if (out[0].count > 0 && hours >= 0.0f) {
            int most = item_hit_points_lasting(def->product, hours);
            if (out[0].hit_points > most) {
                out[0].hit_points = (uint16_t)most;
            }
        }
        return 1;
    }
    default:
        out[0] =
            product(def->product, def->product_count > 0 ? def->product_count : 1, level, rng, cap);
        return 1;
    }
}

/* The species of the first wooden ingredient (0 when none is wooden). */
static WoodMask ingredient_woods(const ItemStack inputs[RECIPE_INGREDIENT_MAX])
{
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        const ItemDef *material = item_def((ItemType)inputs[i].type);
        if (inputs[i].count > 0 && material != NULL && material->wooden) {
            return inputs[i].woods;
        }
    }
    return 0;
}

int recipe_craft_with(RecipeType recipe, const ItemStack inputs[RECIPE_INGREDIENT_MAX],
                      ItemStack tool, double level, Rng *rng, ItemStack out[RECIPE_PRODUCT_MAX])
{
    const RecipeDef *def = recipe_def(recipe);
    if (def == NULL) {
        return 0;
    }
    ItemQuality cap = recipe_quality_cap_of(recipe, inputs, tool);
    int count = craft_products(recipe, def, inputs[0], cap, level, rng, out);
    /* What is made of wood keeps every species of its material
     * (docs/TREES.md §6); a new shaft is as good as its wood makes it, within
     * the material's cap (hardening keeps the shaft's quality). */
    bool hardening = recipe == RECIPE_HARDEN_THROWING_SPEAR ||
                     recipe == RECIPE_HARDEN_THRUSTING_SPEAR ||
                     recipe == RECIPE_HARDEN_DIGGING_STICK;
    WoodMask woods = ingredient_woods(inputs);
    for (int i = 0; i < count; i++) {
        if (!item_def((ItemType)out[i].type)->wooden) {
            continue;
        }
        out[i].woods = woods;
        if (!hardening && out[i].count > 0) {
            int quality = out[i].quality + wood_shafts(out[i].woods);
            out[i].quality = (uint8_t)clampi(quality, ITEM_QUALITY_WORST,
                                             quality > out[i].quality ? cap : ITEM_QUALITY_BEST);
        }
    }
    return count;
}

int recipe_craft(RecipeType recipe, ItemStack input, double level, Rng *rng,
                 ItemStack out[RECIPE_PRODUCT_MAX])
{
    ItemStack inputs[RECIPE_INGREDIENT_MAX] = {input};
    return recipe_craft_with(recipe, inputs, (ItemStack){.type = ITEM_NONE}, level, rng, out);
}

ItemStack recipe_average_product(RecipeType recipe, ItemType input, int input_quality,
                                 WoodMask woods, double level)
{
    ItemStack none = {.type = ITEM_NONE};
    const RecipeDef *def = recipe_def(recipe);
    if (def == NULL) {
        return none;
    }
    int given = input != ITEM_NONE ? recipe_ingredient_of(recipe, input) : -1;
    ItemStack inputs[RECIPE_INGREDIENT_MAX] = {{.type = ITEM_NONE}};
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        const RecipeIngredient *ingredient = recipe_ingredient(recipe, i);
        if (ingredient == NULL) {
            continue;
        }
        ItemType type = i == given ? input : ingredient->types[0];
        const ItemDef *material = item_def(type);
        if (type == ITEM_NONE || material == NULL) {
            continue;
        }
        inputs[i] = item_stack_make(type, ingredient->count,
                                    clampi(input_quality, ITEM_QUALITY_WORST, ITEM_QUALITY_BEST),
                                    material->max_hit_points);
        inputs[i].woods = material->wooden ? woods : 0;
    }
    ItemStack made[RECIPE_PRODUCT_MAX];
    int count =
        recipe_craft_with(recipe, inputs, (ItemStack){.type = ITEM_NONE}, level, NULL, made);
    for (int i = 0; i < count; i++) {
        if (made[i].type == def->product) {
            return made[i];
        }
    }
    return item_stack_make(def->product, 0, ITEM_QUALITY_STANDARD, 0);
}
