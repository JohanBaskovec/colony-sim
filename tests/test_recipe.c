#include "core/recipe.h"
#include "core/workstation.h"

#include "test_framework.h"

static ItemStack stone(ItemType type, ItemQuality quality)
{
    return item_stack_make(type, 1, quality, 0);
}

static void test_definitions(void)
{
    for (int recipe = 0; recipe < RECIPE_COUNT; recipe++) {
        const RecipeDef *def = recipe_def((RecipeType)recipe);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL && def->name_key != NULL);
        CHECK(def->stations != 0);
        CHECK(def->inputs[0] != ITEM_NONE);
        CHECK(def->input_count >= 1);
        CHECK(def->work > 0.0f);
        CHECK(def->min_level >= KNOWLEDGE_MASTERY_LEVEL);
        CHECK(def->product != ITEM_NONE);
        CHECK(recipe_inputs((RecipeType)recipe) & ITEM_TYPE_BIT(def->inputs[0]));
    }
    CHECK(recipe_def(RECIPE_COUNT) == NULL);
    CHECK_EQ_UINT(recipe_inputs(RECIPE_COUNT), 0);
    /* Flint is preferred for flakes, a cobble for a chopper. */
    CHECK_EQ_INT(recipe_def(RECIPE_STRIKE_FLAKES)->inputs[0], ITEM_FLINT);
    CHECK_EQ_INT(recipe_def(RECIPE_KNAP_CHOPPER)->inputs[0], ITEM_COBBLE);
    /* The chain can always start: picking a hammerstone needs no tool. */
    CHECK_EQ_UINT(recipe_def(RECIPE_PICK_HAMMERSTONE)->tools[0], 0);
    CHECK(recipe_def(RECIPE_PICK_HAMMERSTONE)->stations &
          WORKSTATION_BIT(WORKSTATION_CRAFTING_SPOT));
}

static void test_inputs_and_levels(void)
{
    CHECK(recipe_accepts_input(RECIPE_STRIKE_FLAKES, stone(ITEM_FLINT, ITEM_QUALITY_CRUDE)));
    CHECK(recipe_accepts_input(RECIPE_STRIKE_FLAKES, stone(ITEM_COBBLE, ITEM_QUALITY_CRUDE)));
    CHECK(!recipe_accepts_input(RECIPE_STRIKE_FLAKES, item_stack_pristine(ITEM_WOOD, 1)));
    CHECK(!recipe_accepts_input(RECIPE_PICK_HAMMERSTONE, stone(ITEM_FLINT, ITEM_QUALITY_FINE)));
    /* Spears want straight branches. */
    ItemStack crooked = item_stack_make(ITEM_BRANCHES, 1, ITEM_QUALITY_ROUGH, 20);
    ItemStack straight = item_stack_make(ITEM_BRANCHES, 1, ITEM_QUALITY_STANDARD, 20);
    CHECK(!recipe_accepts_input(RECIPE_MAKE_THROWING_SPEAR, crooked));
    CHECK(recipe_accepts_input(RECIPE_MAKE_THROWING_SPEAR, straight));
    CHECK(!recipe_accepts_input(RECIPE_COUNT, straight));

    CHECK(recipe_doable_at(RECIPE_KNAP_CHOPPER, 50.0));
    CHECK(!recipe_doable_at(RECIPE_KNAP_CHOPPER, 49.9));
    CHECK(!recipe_doable_at(RECIPE_STRIKE_FLAKES, 54.0));
    CHECK(!recipe_doable_at(RECIPE_SHAPE_HAND_AXE, 69.0));
    CHECK(recipe_doable_at(RECIPE_SHAPE_HAND_AXE, 70.0));
    CHECK(!recipe_doable_at(RECIPE_COUNT, 100.0));
}

static void test_quality_cap_from_materials(void)
{
    /* Grade of the material + 1. */
    CHECK_EQ_INT(recipe_quality_cap(RECIPE_STRIKE_FLAKES, stone(ITEM_FLINT, ITEM_QUALITY_CRUDE)),
                 ITEM_QUALITY_ROUGH);
    CHECK_EQ_INT(recipe_quality_cap(RECIPE_STRIKE_FLAKES, stone(ITEM_FLINT, ITEM_QUALITY_FINE)),
                 ITEM_QUALITY_SUPERB);
    /* A cobble knaps like crude flint, whatever its grade. */
    CHECK_EQ_INT(
        recipe_quality_cap(RECIPE_SHAPE_HAND_AXE, stone(ITEM_COBBLE, ITEM_QUALITY_SUPERB)),
        ITEM_QUALITY_ROUGH);

    /* A master knapper with crude flint makes at best rough flakes. */
    ItemStack out[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft(RECIPE_STRIKE_FLAKES, stone(ITEM_FLINT, ITEM_QUALITY_CRUDE), 100.0, NULL,
                         out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_FLAKE);
    CHECK_EQ_INT(out[0].quality, ITEM_QUALITY_ROUGH);
    REQUIRE(recipe_craft(RECIPE_STRIKE_FLAKES, stone(ITEM_FLINT, ITEM_QUALITY_FINE), 100.0, NULL,
                         out) == 1);
    CHECK_EQ_INT(out[0].quality, ITEM_QUALITY_SUPERB);
}

static void test_products(void)
{
    ItemStack out[RECIPE_PRODUCT_MAX];
    /* The hammerstone is as good as the cobble, not the knapper. */
    REQUIRE(recipe_craft(RECIPE_PICK_HAMMERSTONE, stone(ITEM_COBBLE, ITEM_QUALITY_FINE), 50.0,
                         NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_HAMMERSTONE);
    CHECK_EQ_INT(out[0].quality, ITEM_QUALITY_FINE);
    CHECK_EQ_INT(out[0].hit_points, item_def(ITEM_HAMMERSTONE)->max_hit_points);

    /* Flakes: 3 at mastery, one more per 10 points, 8 at 100. */
    recipe_craft(RECIPE_STRIKE_FLAKES, stone(ITEM_FLINT, ITEM_QUALITY_SUPERB), 55.0, NULL, out);
    CHECK_EQ_INT(out[0].count, 3);
    recipe_craft(RECIPE_STRIKE_FLAKES, stone(ITEM_FLINT, ITEM_QUALITY_SUPERB), 79.0, NULL, out);
    CHECK_EQ_INT(out[0].count, 5);
    recipe_craft(RECIPE_STRIKE_FLAKES, stone(ITEM_FLINT, ITEM_QUALITY_SUPERB), 100.0, NULL, out);
    CHECK_EQ_INT(out[0].count, 8);

    /* A hand axe comes with two flakes (no failure without randomness). */
    REQUIRE(recipe_craft(RECIPE_SHAPE_HAND_AXE, stone(ITEM_FLINT, ITEM_QUALITY_FINE), 70.0, NULL,
                         out) == 2);
    CHECK_EQ_INT(out[0].type, ITEM_HAND_AXE);
    CHECK_EQ_INT(out[0].count, 1);
    CHECK_EQ_INT(out[1].type, ITEM_FLAKE);
    CHECK_EQ_INT(out[1].count, 2);

    ItemStack branches = item_stack_make(ITEM_BRANCHES, 2, ITEM_QUALITY_STANDARD, 20);
    REQUIRE(recipe_craft(RECIPE_MAKE_THRUSTING_SPEAR, branches, 60.0, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_THRUSTING_SPEAR);
    CHECK_EQ_INT(out[0].count, 1);
    CHECK(out[0].quality <= ITEM_QUALITY_FINE); /* Standard branches + 1. */
    CHECK_EQ_INT(recipe_craft(RECIPE_COUNT, branches, 60.0, NULL, out), 0);
}

static void test_hand_axe_failures(void)
{
    CHECK_NEAR(recipe_failure_chance(RECIPE_SHAPE_HAND_AXE, 70.0), 0.40, 1e-6);
    CHECK_NEAR(recipe_failure_chance(RECIPE_SHAPE_HAND_AXE, 80.0), 0.20, 1e-6);
    CHECK_NEAR(recipe_failure_chance(RECIPE_SHAPE_HAND_AXE, 90.0), 0.0, 1e-6);
    CHECK_NEAR(recipe_failure_chance(RECIPE_STRIKE_FLAKES, 55.0), 0.0, 1e-6);

    Rng rng;
    rng_seed(&rng, 3, 9);
    int failures = 0;
    ItemStack out[RECIPE_PRODUCT_MAX];
    for (int i = 0; i < 1000; i++) {
        int count =
            recipe_craft(RECIPE_SHAPE_HAND_AXE, stone(ITEM_FLINT, ITEM_QUALITY_FINE), 70.0, &rng,
                         out);
        if (count == 1) {
            failures++;
            /* The stone is lost, a few flakes are left: 1 + 2 at level 70. */
            CHECK_EQ_INT(out[0].type, ITEM_FLAKE);
            CHECK_EQ_INT(out[0].count, 3);
        }
    }
    CHECK(failures > 340 && failures < 460);
    for (int i = 0; i < 100; i++) {
        CHECK_EQ_INT(recipe_craft(RECIPE_SHAPE_HAND_AXE, stone(ITEM_FLINT, ITEM_QUALITY_FINE),
                                  95.0, &rng, out),
                     2);
    }
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_inputs_and_levels);
    RUN_TEST(test_quality_cap_from_materials);
    RUN_TEST(test_products);
    RUN_TEST(test_hand_axe_failures);
    return test_summary();
}
