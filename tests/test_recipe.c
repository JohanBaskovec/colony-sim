#include "core/recipe.h"
#include "core/wood.h"
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
        CHECK(recipe_ingredient_count((RecipeType)recipe) >= 1);
        for (int i = 0; i < recipe_ingredient_count((RecipeType)recipe); i++) {
            CHECK(def->ingredients[i].types[0] != ITEM_NONE);
            CHECK(def->ingredients[i].count >= 1);
            /* No type is two ingredients at once. */
            for (int j = 0; j < i; j++) {
                CHECK(!item_types_meet(recipe_ingredient_types((RecipeType)recipe, i),
                                       recipe_ingredient_types((RecipeType)recipe, j)));
            }
        }
        CHECK(def->work > 0.0f);
        CHECK(def->min_level >= KNOWLEDGE_MASTERY_LEVEL);
        CHECK(def->product != ITEM_NONE);
        CHECK(item_types_has(recipe_inputs((RecipeType)recipe), def->ingredients[0].types[0]));
    }
    CHECK(recipe_def(RECIPE_COUNT) == NULL);
    CHECK(item_types_equal(recipe_inputs(RECIPE_COUNT), item_types_none()));
    /* Flint is preferred for flakes, a cobble for a chopper. */
    CHECK_EQ_INT(recipe_def(RECIPE_FLAKES)->ingredients[0].types[0], ITEM_FLINT);
    CHECK_EQ_INT(recipe_def(RECIPE_CHOPPER)->ingredients[0].types[0], ITEM_COBBLE);
    /* The chain can always start: knapping takes a plain cobble as a hammer. */
    CHECK(item_types_equal(recipe_tool_types(recipe_def(RECIPE_FLAKES), 0),
                           item_types_of(ITEM_COBBLE)));
    CHECK(item_types_equal(recipe_tool_types(recipe_def(RECIPE_CHOPPER), 0),
                           item_types_of(ITEM_COBBLE)));
}

static void test_inputs_and_levels(void)
{
    CHECK(recipe_accepts_input(RECIPE_FLAKES, stone(ITEM_FLINT, TEST_QUALITY_CRUDE)));
    CHECK(recipe_accepts_input(RECIPE_FLAKES, stone(ITEM_COBBLE, TEST_QUALITY_CRUDE)));
    CHECK(!recipe_accepts_input(RECIPE_FLAKES, item_stack_pristine(ITEM_WOOD, 1)));
    CHECK(!recipe_accepts_input(RECIPE_THROWING_SPEAR, stone(ITEM_FLINT, TEST_QUALITY_FINE)));
    /* Spears want straight branches. */
    ItemStack crooked = item_stack_make(ITEM_BRANCHES, 1, TEST_QUALITY_ROUGH, 20);
    ItemStack straight = item_stack_make(ITEM_BRANCHES, 1, ITEM_QUALITY_STANDARD, 20);
    CHECK(!recipe_accepts_input(RECIPE_THROWING_SPEAR, crooked));
    CHECK(recipe_accepts_input(RECIPE_THROWING_SPEAR, straight));
    CHECK(!recipe_accepts_input(RECIPE_COUNT, straight));

    CHECK(recipe_doable_at(RECIPE_CHOPPER, 50.0));
    CHECK(!recipe_doable_at(RECIPE_CHOPPER, 49.9));
    CHECK(!recipe_doable_at(RECIPE_FLAKES, 54.0));
    CHECK(!recipe_doable_at(RECIPE_HAND_AXE, 69.0));
    CHECK(recipe_doable_at(RECIPE_HAND_AXE, 70.0));
    CHECK(!recipe_doable_at(RECIPE_COUNT, 100.0));
}

static void test_quality_cap_from_materials(void)
{
    /* The material's quality + 20, whatever the value. */
    CHECK_EQ_INT(recipe_quality_cap(RECIPE_FLAKES, stone(ITEM_FLINT, 47)), 67);
    CHECK_EQ_INT(recipe_quality_cap(RECIPE_FLAKES, stone(ITEM_FLINT, 95)), ITEM_QUALITY_BEST);
    CHECK_EQ_INT(recipe_quality_cap(RECIPE_FLAKES, stone(ITEM_FLINT, 0)), ITEM_QUALITY_CAP_BONUS);
    ItemStack capped[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft(RECIPE_FLAKES, stone(ITEM_FLINT, 47), 100.0, NULL, capped) == 1);
    CHECK_EQ_INT(capped[0].quality, 67); /* A master's 95, held at the cap. */
    REQUIRE(recipe_craft(RECIPE_FLAKES, stone(ITEM_FLINT, 95), 50.0, NULL, capped) == 1);
    CHECK_EQ_INT(capped[0].quality, 25); /* A beginner's, far under it. */
    CHECK_EQ_INT(recipe_quality_cap(RECIPE_FLAKES, stone(ITEM_FLINT, TEST_QUALITY_CRUDE)),
                 TEST_QUALITY_ROUGH);
    CHECK_EQ_INT(recipe_quality_cap(RECIPE_FLAKES, stone(ITEM_FLINT, TEST_QUALITY_FINE)),
                 TEST_QUALITY_SUPERB);
    /* A cobble knaps like crude flint, whatever its quality. */
    CHECK_EQ_INT(recipe_quality_cap(RECIPE_HAND_AXE, stone(ITEM_COBBLE, TEST_QUALITY_SUPERB)),
                 TEST_QUALITY_ROUGH);

    /* A master knapper with crude flint makes at best rough flakes. */
    ItemStack out[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft(RECIPE_FLAKES, stone(ITEM_FLINT, TEST_QUALITY_CRUDE), 100.0, NULL, out) ==
            1);
    CHECK_EQ_INT(out[0].type, ITEM_FLAKE);
    CHECK_EQ_INT(out[0].quality, TEST_QUALITY_ROUGH);
    REQUIRE(recipe_craft(RECIPE_FLAKES, stone(ITEM_FLINT, TEST_QUALITY_FINE), 100.0, NULL, out) ==
            1);
    CHECK_EQ_INT(out[0].quality, TEST_QUALITY_SUPERB);
}

/* docs/CONTAINERS.md §9: a recipe of several ingredients, each with its count and the
 * moisture it wants; the shaping tool counts for a quarter of the quality cap. */
static void test_several_ingredients(void)
{
    CHECK_EQ_INT(recipe_ingredient_count(RECIPE_FLAKES), 1);
    CHECK_EQ_INT(recipe_ingredient_count(RECIPE_BARK_TRAY), 2);
    CHECK_EQ_INT(recipe_ingredient_count(RECIPE_COUNT), 0);
    CHECK(recipe_ingredient(RECIPE_BARK_TRAY, 2) == NULL);
    CHECK(recipe_ingredient(RECIPE_BARK_TRAY, -1) == NULL);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_BARK_TRAY, 0)->moisture, ITEM_WANT_WET);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_BARK_TRAY, 1)->count, 2);
    CHECK_EQ_INT(recipe_ingredient_of(RECIPE_BARK_TRAY, ITEM_BIRCH_BARK), 0);
    CHECK_EQ_INT(recipe_ingredient_of(RECIPE_BARK_TRAY, ITEM_TWIGS), 1);
    CHECK_EQ_INT(recipe_ingredient_of(RECIPE_BARK_TRAY, ITEM_FLINT), -1);
    CHECK(
        item_types_equal(recipe_inputs(RECIPE_BARK_TRAY), ITEM_TYPES(ITEM_BIRCH_BARK, ITEM_TWIGS)));
    ItemStack bark = item_stack_make(ITEM_BIRCH_BARK, 1, 40, 0);
    ItemStack twigs = item_stack_make(ITEM_TWIGS, 2, 60, 0);
    CHECK(recipe_accepts_ingredient(RECIPE_BARK_TRAY, 0, bark));
    CHECK(!recipe_accepts_ingredient(RECIPE_BARK_TRAY, 1, bark));
    CHECK(recipe_accepts_input(RECIPE_BARK_TRAY, twigs));

    /* The cap: the materials' average quality weighted by count, the shaping tool for a
     * quarter, + 20. Twigs have no quality of their own: only the bark counts. */
    ItemStack inputs[RECIPE_INGREDIENT_MAX] = {bark, twigs};
    ItemStack none = {.type = ITEM_NONE};
    ItemStack flake = item_stack_make(ITEM_FLAKE, 1, 80, 0);
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_BARK_TRAY, inputs, none), 40 + 20);
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_BARK_TRAY, inputs, flake),
                 (40 * 3 + 80) / 4 + ITEM_QUALITY_CAP_BONUS);
    /* A tool that only cuts does not shape: spears ignore it. */
    ItemStack branches[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_BRANCHES, 1, 40, 0)};
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_THROWING_SPEAR, branches, flake), 60);
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_WOODEN_SCRAPER, branches, flake), 70);

    ItemStack out[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft_with(RECIPE_BARK_TRAY, inputs, flake, 100.0, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_BARK_TRAY);
    CHECK_EQ_INT(out[0].count, 1);
    CHECK_EQ_INT(out[0].quality, 70); /* A master's 95, held at the cap. */
    /* A borer is a flake retouched: one flake in, one borer out. */
    REQUIRE(recipe_craft(RECIPE_FLINT_BORER, flake, 75.0, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_FLINT_BORER);
    CHECK_EQ_INT(out[0].count, 1);
}

static void test_products(void)
{
    ItemStack out[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft(RECIPE_CHOPPER, stone(ITEM_COBBLE, TEST_QUALITY_FINE), 50.0, NULL, out) ==
            1);
    CHECK_EQ_INT(out[0].type, ITEM_CHOPPER);
    CHECK_EQ_INT(out[0].count, 1);

    /* Flakes: 3 at mastery, one more per 10 points, 8 at 100. */
    recipe_craft(RECIPE_FLAKES, stone(ITEM_FLINT, TEST_QUALITY_SUPERB), 55.0, NULL, out);
    CHECK_EQ_INT(out[0].count, 3);
    recipe_craft(RECIPE_FLAKES, stone(ITEM_FLINT, TEST_QUALITY_SUPERB), 79.0, NULL, out);
    CHECK_EQ_INT(out[0].count, 5);
    recipe_craft(RECIPE_FLAKES, stone(ITEM_FLINT, TEST_QUALITY_SUPERB), 100.0, NULL, out);
    CHECK_EQ_INT(out[0].count, 8);

    /* A hand axe comes with two flakes (no failure without randomness). */
    REQUIRE(recipe_craft(RECIPE_HAND_AXE, stone(ITEM_FLINT, TEST_QUALITY_FINE), 70.0, NULL, out) ==
            2);
    CHECK_EQ_INT(out[0].type, ITEM_HAND_AXE);
    CHECK_EQ_INT(out[0].count, 1);
    CHECK_EQ_INT(out[1].type, ITEM_FLAKE);
    CHECK_EQ_INT(out[1].count, 2);

    ItemStack branches = item_stack_make(ITEM_BRANCHES, 2, ITEM_QUALITY_STANDARD, 20);
    REQUIRE(recipe_craft(RECIPE_THRUSTING_SPEAR, branches, 60.0, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_THRUSTING_SPEAR);
    CHECK_EQ_INT(out[0].count, 1);
    CHECK(out[0].quality <= TEST_QUALITY_FINE); /* Standard branches + 1. */
    CHECK_EQ_INT(recipe_craft(RECIPE_COUNT, branches, 60.0, NULL, out), 0);
}

/* What one craft makes on average, as the bills' window shows it. */
static void test_average_product(void)
{
    /* Flakes: 5 at level 79, of the quality a craft makes then. */
    ItemStack out[RECIPE_PRODUCT_MAX];
    recipe_craft(RECIPE_FLAKES, stone(ITEM_FLINT, ITEM_QUALITY_BEST), 79.0, NULL, out);
    ItemStack flakes = recipe_average_product(RECIPE_FLAKES, ITEM_NONE, ITEM_QUALITY_BEST, 0, 79.0);
    CHECK_EQ_INT(flakes.type, ITEM_FLAKE);
    CHECK_EQ_INT(flakes.count, 5);
    CHECK_EQ_INT(flakes.quality, out[0].quality);
    /* Held at the material's quality + 20. */
    flakes = recipe_average_product(RECIPE_FLAKES, ITEM_FLINT, 30, 0, 100.0);
    CHECK_EQ_INT(flakes.quality, 30 + ITEM_QUALITY_CAP_BONUS);
    /* A hand axe, not its flakes. */
    ItemStack axe = recipe_average_product(RECIPE_HAND_AXE, ITEM_NONE, ITEM_QUALITY_BEST, 0, 70.0);
    CHECK_EQ_INT(axe.type, ITEM_HAND_AXE);
    CHECK_EQ_INT(axe.count, 1);
    /* A spear of ash, the wood of shafts, beats one of birch; it keeps its wood. */
    WoodMask ash = wood_bit(PLANT_ASH), birch = wood_bit(PLANT_BIRCH);
    ItemStack of_ash =
        recipe_average_product(RECIPE_THRUSTING_SPEAR, ITEM_NONE, ITEM_QUALITY_STANDARD, ash, 60.0);
    ItemStack of_birch = recipe_average_product(RECIPE_THRUSTING_SPEAR, ITEM_NONE,
                                                ITEM_QUALITY_STANDARD, birch, 60.0);
    CHECK_EQ_INT(of_ash.type, ITEM_THRUSTING_SPEAR);
    CHECK(of_ash.quality > of_birch.quality);
    CHECK_EQ_UINT(of_ash.woods, ash);
    /* Hardening keeps the spear's quality. */
    ItemStack hardened =
        recipe_average_product(RECIPE_HARDEN_THRUSTING_SPEAR, ITEM_NONE, 42, ash, 30.0);
    CHECK_EQ_INT(hardened.quality, 42);
    CHECK_EQ_INT(recipe_average_product(RECIPE_COUNT, ITEM_NONE, 50, 0, 50.0).type, ITEM_NONE);
}

static void test_hand_axe_failures(void)
{
    CHECK_NEAR(recipe_failure_chance(RECIPE_HAND_AXE, 70.0), 0.40, 1e-6);
    CHECK_NEAR(recipe_failure_chance(RECIPE_HAND_AXE, 80.0), 0.20, 1e-6);
    CHECK_NEAR(recipe_failure_chance(RECIPE_HAND_AXE, 90.0), 0.0, 1e-6);
    CHECK_NEAR(recipe_failure_chance(RECIPE_FLAKES, 55.0), 0.0, 1e-6);

    Rng rng;
    rng_seed(&rng, 3, 9);
    int failures = 0;
    ItemStack out[RECIPE_PRODUCT_MAX];
    for (int i = 0; i < 1000; i++) {
        int count =
            recipe_craft(RECIPE_HAND_AXE, stone(ITEM_FLINT, TEST_QUALITY_FINE), 70.0, &rng, out);
        if (count == 1) {
            failures++;
            /* The stone is lost, a few flakes are left: 1 + 2 at level 70. */
            CHECK_EQ_INT(out[0].type, ITEM_FLAKE);
            CHECK_EQ_INT(out[0].count, 3);
        }
    }
    CHECK(failures > 340 && failures < 460);
    for (int i = 0; i < 100; i++) {
        CHECK_EQ_INT(
            recipe_craft(RECIPE_HAND_AXE, stone(ITEM_FLINT, TEST_QUALITY_FINE), 95.0, &rng, out),
            2);
    }
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_inputs_and_levels);
    RUN_TEST(test_quality_cap_from_materials);
    RUN_TEST(test_products);
    RUN_TEST(test_several_ingredients);
    RUN_TEST(test_hand_axe_failures);
    RUN_TEST(test_average_product);
    return test_summary();
}
