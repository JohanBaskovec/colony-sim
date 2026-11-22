#include "core/carry.h"
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

/* The nettle cord chain (docs/CONTAINERS.md §4.1): dry stems scraped into fibre, a wooden
 * scraper shaping it (a flake scrapes too sharply: a lower cap), fibre twisted into two hanks. */
static void test_nettle_cord_chain(void)
{
    const RecipeIngredient *stems = recipe_ingredient(RECIPE_NETTLE_FIBRE, 0);
    REQUIRE(stems != NULL);
    CHECK_EQ_INT(stems->types[0], ITEM_NETTLE_STEMS);
    CHECK_EQ_INT(stems->moisture, ITEM_WANT_DRY);
    const RecipeDef *fibre = recipe_def(RECIPE_NETTLE_FIBRE);
    CHECK(
        item_types_equal(recipe_tool_types(fibre, 0), ITEM_TYPES(ITEM_WOODEN_SCRAPER, ITEM_FLAKE)));
    CHECK(item_types_equal(recipe_tool_types(fibre, 1), item_types_of(ITEM_COBBLE)));
    CHECK_EQ_INT(fibre->tool_use, TOOL_USE_SCRAPING);
    CHECK_EQ_INT(fibre->knowledge, KNOWLEDGE_CORDAGE);

    ItemStack inputs[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_NETTLE_STEMS, 1, 40, 0)};
    ItemStack scraper = item_stack_make(ITEM_WOODEN_SCRAPER, 1, 80, 0);
    ItemStack flake = item_stack_make(ITEM_FLAKE, 1, 80, 0);
    int with_scraper = (40 * 3 + 80) / 4 + ITEM_QUALITY_CAP_BONUS;
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_NETTLE_FIBRE, inputs, scraper), with_scraper);
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_NETTLE_FIBRE, inputs, flake), with_scraper - 10);
    /* Retted stems: a finer fibre, scraped twice as fast; over retted, a rotten one. */
    CHECK_NEAR(recipe_work_factor(RECIPE_NETTLE_FIBRE, inputs), 1.0, 1e-6);
    ItemStack retted[RECIPE_INGREDIENT_MAX] = {inputs[0]};
    item_stack_set_retting(&retted[0], 1.0f);
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_NETTLE_FIBRE, retted, scraper),
                 with_scraper + ITEM_RETTING_CAP_BONUS);
    CHECK_NEAR(recipe_work_factor(RECIPE_NETTLE_FIBRE, retted), 2.0, 1e-6);
    item_stack_set_retting(&retted[0], 2.0f);
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_NETTLE_FIBRE, retted, scraper),
                 with_scraper + item_retting_cap_bonus(2.0f));
    ItemStack out[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft_with(RECIPE_NETTLE_FIBRE, inputs, scraper, 75.0, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_NETTLE_FIBRE);
    CHECK_EQ_INT(out[0].count, 1);

    ItemStack handful[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_NETTLE_FIBRE, 1, 60, 0)};
    REQUIRE(recipe_craft_with(RECIPE_NETTLE_CORD, handful, (ItemStack){.type = ITEM_NONE}, 75.0,
                              NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_NETTLE_CORD);
    CHECK_EQ_INT(out[0].count, 2); /* Two hanks from a handful. */
    CHECK(out[0].quality <= 60 + ITEM_QUALITY_CAP_BONUS);
}

/* The plaited cattail basket (docs/CONTAINERS.md §7.1): three bundles of leaves, a flake; woven
 * green, it works loose as it dries (cap −15); dried leaves are dampened by the weaver. */
static void test_cattail_basket(void)
{
    const RecipeIngredient *leaves = recipe_ingredient(RECIPE_CATTAIL_BASKET, 0);
    REQUIRE(leaves != NULL);
    CHECK_EQ_INT(leaves->types[0], ITEM_CATTAIL_LEAVES);
    CHECK_EQ_INT(leaves->count, 3);
    CHECK_EQ_INT(recipe_def(RECIPE_CATTAIL_BASKET)->knowledge, KNOWLEDGE_BASKETRY);
    ItemStack dry = item_stack_make(ITEM_CATTAIL_LEAVES, 3, 50, 0);
    item_stack_set_moisture(&dry, 0.1f);
    ItemStack green = dry;
    item_stack_set_fresh(&green);
    REQUIRE(item_moisture_green(ITEM_CATTAIL_LEAVES, item_stack_moisture(green)));
    ItemStack flake = item_stack_make(ITEM_FLAKE, 1, 80, 0);
    ItemStack dried[RECIPE_INGREDIENT_MAX] = {dry};
    ItemStack fresh[RECIPE_INGREDIENT_MAX] = {green};
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_CATTAIL_BASKET, dried, flake), 50 + 20);
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_CATTAIL_BASKET, fresh, flake), 50 + 20 - 15);
    ItemStack out[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft_with(RECIPE_CATTAIL_BASKET, dried, flake, 75.0, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_CATTAIL_BASKET);
    CHECK_EQ_INT(out[0].count, 1);
}

/* The sewn bark box, the frame basket and the pack basket (docs/CONTAINERS.md §7.1): pliable
 * bark and rods, cord; the box's holes pierced with the borer, which shapes it. */
static void test_rod_and_bark_containers(void)
{
    const RecipeDef *box = recipe_def(RECIPE_BARK_BOX);
    CHECK_EQ_INT(recipe_ingredient_count(RECIPE_BARK_BOX), 3);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_BARK_BOX, 0)->count, 2);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_BARK_BOX, 0)->moisture, ITEM_WANT_WET);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_BARK_BOX, 1)->types[0], ITEM_NETTLE_CORD);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_BARK_BOX, 2)->types[0], ITEM_WILLOW_RODS);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_BARK_BOX, 2)->types[1], ITEM_HAZEL_RODS);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_BARK_BOX, 2)->moisture, ITEM_WANT_WET);
    CHECK(box->tool_shapes && box->tool_use == TOOL_USE_PIERCING);
    CHECK(item_types_has(recipe_tool_types(box, 0), ITEM_FLINT_BORER));
    CHECK_EQ_INT(box->knowledge, KNOWLEDGE_BASKETRY);
    CHECK(box->min_level == 65.0f);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_FRAME_BASKET, 0)->count, 3);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_FRAME_BASKET, 0)->moisture, ITEM_WANT_WET);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_PACK_BASKET, 0)->count, 6);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_PACK_BASKET, 1)->count, 2);
    CHECK(recipe_def(RECIPE_PACK_BASKET)->min_level == 70.0f);
    CHECK(item_def(ITEM_PACK_BASKET)->on_back && !item_def(ITEM_FRAME_BASKET)->on_back);

    ItemStack inputs[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_BIRCH_BARK, 2, 60, 0),
                                               item_stack_make(ITEM_NETTLE_CORD, 1, 60, 0),
                                               item_stack_make(ITEM_HAZEL_RODS, 1, 60, 0)};
    ItemStack borer = item_stack_make(ITEM_FLINT_BORER, 1, 60, 0);
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_BARK_BOX, inputs, borer), 60 + 20);
    ItemStack out[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft_with(RECIPE_BARK_BOX, inputs, borer, 75.0, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_BARK_BOX);
    ItemStack rods[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_HAZEL_RODS, 6, 50, 0),
                                             item_stack_make(ITEM_NETTLE_CORD, 2, 50, 0)};
    REQUIRE(recipe_craft_with(RECIPE_PACK_BASKET, rods, item_stack_make(ITEM_HAND_AXE, 1, 50, 0),
                              75.0, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_PACK_BASKET);
    CHECK(out[0].quality <= 50 + 20);
}

/* Osier rods are longer and more pliable than hazel's (docs/CONTAINERS.md §7.1): a frame basket
 * of them is woven in four fifths of the time, and finer (cap +10). */
static void test_osier_weaves_faster_and_finer(void)
{
    ItemStack hazel[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_HAZEL_RODS, 3, 50, 0)};
    ItemStack osier[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_WILLOW_RODS, 3, 50, 0)};
    ItemStack axe = item_stack_make(ITEM_HAND_AXE, 1, 50, 0);
    CHECK(recipe_accepts_ingredient(RECIPE_FRAME_BASKET, 0, osier[0]));
    CHECK_NEAR(recipe_work_factor(RECIPE_FRAME_BASKET, hazel), 1.0, 1e-6);
    CHECK_NEAR(recipe_work_factor(RECIPE_FRAME_BASKET, osier), 1.25, 1e-6);
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_FRAME_BASKET, hazel, axe), 50 + 20);
    CHECK_EQ_INT(recipe_quality_cap_of(RECIPE_FRAME_BASKET, osier, axe), 50 + 20 + 10);
}

/* The looped net bag (docs/CONTAINERS.md §7.1): 8 hanks of cord, nettle or lime, by hand. */
static void test_net_bag(void)
{
    const RecipeIngredient *cord = recipe_ingredient(RECIPE_NET_BAG, 0);
    REQUIRE(cord != NULL);
    CHECK_EQ_INT(cord->count, 8);
    CHECK_EQ_INT(cord->types[0], ITEM_NETTLE_CORD);
    CHECK_EQ_INT(cord->types[1], ITEM_LIME_CORD);
    CHECK_EQ_INT(recipe_def(RECIPE_NET_BAG)->knowledge, KNOWLEDGE_BASKETRY);
    CHECK(!item_types_any(recipe_tool_types(recipe_def(RECIPE_NET_BAG), 0)));
    ItemStack hanks[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_LIME_CORD, 8, 50, 0)};
    ItemStack out[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft_with(RECIPE_NET_BAG, hanks, (ItemStack){.type = ITEM_NONE}, 75.0, NULL,
                              out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_NET_BAG);
    CHECK(item_def(ITEM_NET_BAG)->strap && item_def(ITEM_NET_BAG)->net);
}

/* Mending (docs/CONTAINERS.md §11): a worn container and a little of its material make the same
 * container, whole again, as good as it was. */
static void test_mending_containers(void)
{
    static const RecipeType MENDS[] = {RECIPE_MEND_CATTAIL_BASKET, RECIPE_MEND_FRAME_BASKET,
                                       RECIPE_MEND_PACK_BASKET,    RECIPE_MEND_BARK_BOX,
                                       RECIPE_MEND_NET_BAG,        RECIPE_MEND_COILED_BASKET};
    for (size_t i = 0; i < sizeof(MENDS) / sizeof(MENDS[0]); i++) {
        const RecipeDef *def = recipe_def(MENDS[i]);
        CHECK(def->mends);
        CHECK_EQ_INT(recipe_ingredient(MENDS[i], 0)->types[0], def->product);
        CHECK_EQ_INT(recipe_ingredient(MENDS[i], 0)->moisture, ITEM_WANT_WORN);
        CHECK(carry_is_container(def->product));
    }
    ItemStack inputs[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_FRAME_BASKET, 1, 83, 12),
                                               item_stack_make(ITEM_WILLOW_RODS, 1, 20, 0)};
    ItemStack out[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft_with(RECIPE_MEND_FRAME_BASKET, inputs,
                              item_stack_make(ITEM_FLAKE, 1, 50, 0), 55.0, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_FRAME_BASKET);
    CHECK_EQ_INT(out[0].count, 1);
    CHECK_EQ_INT(out[0].quality, 83); /* Kept, whoever mends it. */
    CHECK_EQ_INT(out[0].hit_points, item_def(ITEM_FRAME_BASKET)->max_hit_points);
}

/* A mat (docs/CONTAINERS.md §11): three bundles of cattail leaves or reeds, plaited by hand. */
static void test_plaited_mat(void)
{
    const RecipeIngredient *leaves = recipe_ingredient(RECIPE_MAT, 0);
    REQUIRE(leaves != NULL);
    CHECK_EQ_INT(leaves->count, 3);
    CHECK_EQ_INT(leaves->types[0], ITEM_CATTAIL_LEAVES);
    CHECK_EQ_INT(leaves->types[1], ITEM_REEDS);
    ItemStack reeds[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_REEDS, 3, 60, 0)};
    ItemStack out[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft_with(RECIPE_MAT, reeds, (ItemStack){.type = ITEM_NONE}, 75.0, NULL, out) ==
            1);
    CHECK_EQ_INT(out[0].type, ITEM_MAT);
}

/* Coiled basket (docs/CONTAINERS.md §11): canes split into binding, then coils of dry grass or
 * reeds sewn with it, 30 h; the strongest container. */
static void test_coiled_basket(void)
{
    ItemStack canes[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_BRAMBLE_CANES, 1, 60, 0)};
    ItemStack out[RECIPE_PRODUCT_MAX];
    REQUIRE(recipe_craft_with(RECIPE_SPLIT_CANES, canes, item_stack_make(ITEM_FLAKE, 1, 60, 0),
                              75.0, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_SPLIT_CANES);
    CHECK_EQ_INT(out[0].count, 2);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_COILED_BASKET, 1)->moisture, ITEM_WANT_DRY);
    CHECK(recipe_def(RECIPE_COILED_BASKET)->work >= 50000.0f);
    ItemStack inputs[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_SPLIT_CANES, 2, 60, 0),
                                               item_stack_make(ITEM_REEDS, 4, 50, 0)};
    REQUIRE(recipe_craft_with(RECIPE_COILED_BASKET, inputs,
                              item_stack_make(ITEM_FLINT_BORER, 1, 60, 0), 75.0, NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_COILED_BASKET);
    CHECK(carry_is_container(ITEM_COILED_BASKET));
    CHECK(item_def(ITEM_COILED_BASKET)->max_hit_points >
          2 * item_def(ITEM_FRAME_BASKET)->max_hit_points);
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

/* Lime bast (docs/CONTAINERS.md §4.2): fresh poles peeled with a flake (two bundles of bark),
 * the bark retted, then parted by hand into bast; bast twisted into two hanks of cord. Bast
 * laces a bark box, its cord makes the straps of a pack basket. */
static void test_lime_bast_chain(void)
{
    CHECK_EQ_INT(recipe_ingredient(RECIPE_LIME_BARK, 0)->moisture, ITEM_WANT_WET);
    CHECK_EQ_INT(recipe_def(RECIPE_LIME_BARK)->tool_use, TOOL_USE_PEELING);
    CHECK_EQ_INT(recipe_ingredient(RECIPE_LIME_BAST, 0)->moisture, ITEM_WANT_RETTED);
    CHECK_EQ_INT(recipe_def(RECIPE_LIME_BAST)->knowledge, KNOWLEDGE_CORDAGE);
    ItemStack out[RECIPE_PRODUCT_MAX];
    ItemStack poles[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_LIME_POLES, 1, 60, 0)};
    REQUIRE(recipe_craft_with(RECIPE_LIME_BARK, poles, item_stack_make(ITEM_FLAKE, 1, 60, 0), 75.0,
                              NULL, out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_LIME_BARK);
    CHECK_EQ_INT(out[0].count, 2);
    ItemStack bark[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_LIME_BARK, 1, 60, 0)};
    item_stack_set_retting(&bark[0], 1.0f);
    CHECK_NEAR(recipe_work_factor(RECIPE_LIME_BAST, bark), 2.0, 1e-6); /* 0.5 h of work. */
    REQUIRE(recipe_craft_with(RECIPE_LIME_BAST, bark, (ItemStack){.type = ITEM_NONE}, 75.0, NULL,
                              out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_LIME_BAST);
    ItemStack bast[RECIPE_INGREDIENT_MAX] = {item_stack_make(ITEM_LIME_BAST, 1, 60, 0)};
    REQUIRE(recipe_craft_with(RECIPE_LIME_CORD, bast, (ItemStack){.type = ITEM_NONE}, 75.0, NULL,
                              out) == 1);
    CHECK_EQ_INT(out[0].type, ITEM_LIME_CORD);
    CHECK_EQ_INT(out[0].count, 2);
    CHECK(recipe_accepts_ingredient(RECIPE_BARK_BOX, 1, bast[0]));
    CHECK(recipe_accepts_ingredient(RECIPE_PACK_BASKET, 1, out[0]));
    CHECK(!recipe_accepts_ingredient(RECIPE_PACK_BASKET, 1, bast[0]));
    /* Retted is a state of the item, not of its moisture. */
    CHECK(item_moisture_fits(ITEM_LIME_BARK, 0.1f, ITEM_WANT_RETTED));
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_inputs_and_levels);
    RUN_TEST(test_quality_cap_from_materials);
    RUN_TEST(test_products);
    RUN_TEST(test_several_ingredients);
    RUN_TEST(test_nettle_cord_chain);
    RUN_TEST(test_cattail_basket);
    RUN_TEST(test_rod_and_bark_containers);
    RUN_TEST(test_osier_weaves_faster_and_finer);
    RUN_TEST(test_lime_bast_chain);
    RUN_TEST(test_net_bag);
    RUN_TEST(test_mending_containers);
    RUN_TEST(test_plaited_mat);
    RUN_TEST(test_coiled_basket);
    RUN_TEST(test_hand_axe_failures);
    RUN_TEST(test_average_product);
    return test_summary();
}
