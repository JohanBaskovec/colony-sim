/*
 * Recipes: what settlers make at workstations (knapping flakes, making a
 * spear...).
 *
 * A recipe takes up to RECIPE_INGREDIENT_MAX kinds of ingredients, each with
 * its count, alternatives in order of preference (flint or a cobble) and the
 * moisture its items must have (dry stems, green rods); it needs tools that are used but not
 * consumed (one of each listed set; any item can serve, e.g. a cobble as a hammer), a workstation,
 * an amount of work, and a minimum level in a knowledge. What comes out, and how good it is,
 * depends on the worker's level and on the input: the quality of a product is capped at the quality
 * of its material + ITEM_QUALITY_CAP_BONUS points, and a cobble knapped instead of flint counts as
 * crude. Some recipes can fail (shaping a hand axe). Recipes made at a hearth need it burning
 * (recipe_needs_fire()); fire hardening keeps the quality and condition of the spear or digging
 * stick hardened; roasting gives as many roasted roots as it takes raw ones, keeping as long as the
 * raw ones would have at most. Design: docs/KNOWLEDGE.md §2.2, §2.5, §5.2, §6.2, §7.2, §8.2.
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
    RECIPE_HARDEN_THROWING_SPEAR,
    RECIPE_HARDEN_THRUSTING_SPEAR,
    RECIPE_DIGGING_STICK,
    RECIPE_HARDEN_DIGGING_STICK,
    RECIPE_ROAST_RHIZOMES,
    RECIPE_ROAST_CARROTS,
    /* Fibre work and containers (docs/CONTAINERS.md). */
    RECIPE_WOODEN_SCRAPER,
    RECIPE_FLINT_BORER,
    RECIPE_BARK_TRAY,
    RECIPE_NETTLE_FIBRE,   /* Dry stems broken and scraped into fibre. */
    RECIPE_NETTLE_CORD,    /* Fibre twisted into two hanks of cord. */
    RECIPE_CATTAIL_BASKET, /* Leaves plaited into a basket. */
    RECIPE_BARK_BOX,       /* Bark sewn round a rim of rods, with a lid. */
    RECIPE_FRAME_BASKET,   /* Rods woven on stakes. */
    RECIPE_PACK_BASKET,    /* Rods woven into a tall basket with cord straps. */
    RECIPE_LIME_BARK,      /* Fresh lime poles peeled of their bark. */
    RECIPE_LIME_BAST,      /* Retted lime bark parted into bast strips. */
    RECIPE_LIME_CORD,      /* Bast twisted into two hanks of cord. */
    RECIPE_COUNT
} RecipeType;

#define RECIPE_INPUT_ALTERNATIVES 2
#define RECIPE_INGREDIENT_MAX 3
/* Types a tool set may hold. */
#define RECIPE_TOOL_ALTERNATIVES 2
#define RECIPE_TOOL_MAX 2
/* Most stacks one craft gives (a hand axe and its flakes). */
#define RECIPE_PRODUCT_MAX 2

/* One kind of ingredient: `count` items of one of its types (one type per
 * craft), as moist as `moisture` asks. */
typedef struct RecipeIngredient {
    ItemType types[RECIPE_INPUT_ALTERNATIVES]; /* In order of preference (ITEM_NONE: unused). */
    int count;                                 /* 0: no such ingredient. */
    ItemMoistureWant moisture;
} RecipeIngredient;

typedef struct RecipeDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    unsigned stations; /* Workstations it is made at: bits of WorkstationType. */
    /* Its ingredients, the main one first (what a craft keeps of its input:
     * the hardened spear's quality, the roasted roots' keeping). */
    RecipeIngredient ingredients[RECIPE_INGREDIENT_MAX];
    ItemQuality input_min_quality;
    /* Tools: one item of each non-empty set must be carried (each set a list
     * of types, ITEM_NONE where it ends; recipe_tool_types()). */
    ItemType tools[RECIPE_TOOL_MAX][RECIPE_TOOL_ALTERNATIVES];
    /* The tool of the first set shapes the material (a scraper the fibre, a
     * borer the bark): its quality counts in the cap of the products' quality
     * (recipe_quality_cap_of()). */
    bool tool_shapes;
    float work; /* Work ticks at speed ×1. */
    /* Knowledge needed; KNOWLEDGE_NONE: anyone can make it. */
    KnowledgeType knowledge;
    float min_level;
    ItemType product;  /* Main product (what a bill counts in storage). */
    int product_count; /* Of the main product a craft makes (0: one). */
    /* Dirt left by each craft (chips, shavings) on the station's tile, half
     * as much around it (see dirt_spread()). */
    int dirt;
    /* Kind of work: the speed factor of the tool of the first set (see
     * core/tool.h). */
    ToolUse tool_use;
} RecipeDef;

/* NULL when out of range. */
const RecipeDef *recipe_def(RecipeType recipe);

/* The types of tool set `slot` (none for an empty set, or out of range). */
ItemTypeMask recipe_tool_types(const RecipeDef *def, int slot);

/* Every input type a recipe accepts, all its ingredients together. */
ItemTypeMask recipe_inputs(RecipeType recipe);

/* How many kinds of ingredients it takes (1 .. RECIPE_INGREDIENT_MAX; 0 for no recipe). */
int recipe_ingredient_count(RecipeType recipe);

/* Its ingredient `index`, NULL out of range. */
const RecipeIngredient *recipe_ingredient(RecipeType recipe, int index);

/* The types ingredient `index` may be (0 out of range). */
ItemTypeMask recipe_ingredient_types(RecipeType recipe, int index);

/* The ingredient a type is (the first that has it), -1 when none. */
int recipe_ingredient_of(RecipeType recipe, ItemType type);

/* Whether a stack can be the input of a recipe (type and quality; not the count). */
bool recipe_accepts_input(RecipeType recipe, ItemStack stack);

/* Whether a stack can be ingredient `index` (type and quality; not the count
 * nor the moisture). */
bool recipe_accepts_ingredient(RecipeType recipe, int index, ItemStack stack);

/* Whether a settler at `level` in the recipe's knowledge can make it. */
bool recipe_doable_at(RecipeType recipe, double level);

/* Best quality the products can have with this input (its only ingredient). */
ItemQuality recipe_quality_cap(RecipeType recipe, ItemStack input);

/* Best quality the products can have with these ingredients (inputs[i] for
 * ingredient i; empty stacks for ingredients it has not) and this tool (of
 * the first set; an empty stack for none): the average quality of the
 * materials that have one, weighted by count, the tool counting for a
 * quarter when it shapes them (RecipeDef.tool_shapes), plus
 * ITEM_QUALITY_CAP_BONUS (docs/KNOWLEDGE.md §2.2). */
ItemQuality recipe_quality_cap_of(RecipeType recipe, const ItemStack inputs[RECIPE_INGREDIENT_MAX],
                                  ItemStack tool);

/* The factor on the pace of a craft its inputs set: retted stems give their
 * fibre up to twice as fast (item_retting_work_factor()), osier rods weave
 * faster (ItemDef.craft_work_factor); 1 otherwise. */
float recipe_work_factor(RecipeType recipe, const ItemStack inputs[RECIPE_INGREDIENT_MAX]);

/* Whether the recipe is made at a fire (a burning hearth). */
bool recipe_needs_fire(RecipeType recipe);

/* Chance (0..1) that a craft at this level fails. */
float recipe_failure_chance(RecipeType recipe, double level);

/*
 * Products of one completed craft from `input` by a worker at `level`:
 * writes up to RECIPE_PRODUCT_MAX stacks to `out` and returns how many.
 * `rng` may be NULL (average products, never a failure).
 */
int recipe_craft(RecipeType recipe, ItemStack input, double level, Rng *rng,
                 ItemStack out[RECIPE_PRODUCT_MAX]);

/* recipe_craft() from every ingredient (inputs[i] for ingredient i) and the
 * tool of the first set (for its quality: recipe_quality_cap_of()). */
int recipe_craft_with(RecipeType recipe, const ItemStack inputs[RECIPE_INGREDIENT_MAX],
                      ItemStack tool, double level, Rng *rng, ItemStack out[RECIPE_PRODUCT_MAX]);

/*
 * The recipe's product as one craft makes it on average (recipe_craft()
 * without luck nor failure) by a worker at `level`, from items of `input`
 * for its ingredient (ITEM_NONE: the main ingredient's first type), the other
 * ingredients of their first type, all of quality `input_quality` in
 * perfect condition, of the species `woods` when wooden (0: of no known
 * species). Count 0 when it makes none; empty for no recipe.
 */
ItemStack recipe_average_product(RecipeType recipe, ItemType input, int input_quality,
                                 WoodMask woods, double level);

#endif /* COLONY_CORE_RECIPE_H */
