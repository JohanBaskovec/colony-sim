/* Tools: speed factors by use and grade, choosing the best carried tool,
 * wear and breaking. */
#include "core/tool.h"

#include "test_framework.h"

static ItemStack tool(ItemType type, ItemQuality quality, int hit_points)
{
    return item_stack_make(type, 1, quality, hit_points);
}

static void test_definitions(void)
{
    for (int use = 0; use < TOOL_USE_COUNT; use++) {
        const ToolUseDef *def = tool_use_def((ToolUse)use);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL);
        CHECK(def->bare_hands >= 0.0f && def->bare_hands <= 1.0f);
        CHECK_NEAR(def->factor[ITEM_NONE], 0.0, 1e-6);
    }
    CHECK(tool_use_def(TOOL_USE_NONE) == NULL);
}

static void test_type_factors(void)
{
    /* Bare hands: slower grass, slower building, no knapping, no woodworking. */
    CHECK_NEAR(tool_type_factor(TOOL_USE_CUT_GRASS, ITEM_NONE), 0.6, 1e-6);
    CHECK_NEAR(tool_type_factor(TOOL_USE_BUILDING, ITEM_NONE), 0.8, 1e-6);
    CHECK_NEAR(tool_type_factor(TOOL_USE_KNAPPING, ITEM_NONE), 0.0, 1e-6);
    CHECK_NEAR(tool_type_factor(TOOL_USE_WOODWORKING, ITEM_NONE), 0.0, 1e-6);
    CHECK_NEAR(tool_type_factor(TOOL_USE_CUT_GRASS, ITEM_FLAKE), 1.0, 1e-6);
    CHECK_NEAR(tool_type_factor(TOOL_USE_CUT_GRASS, ITEM_COBBLE), 0.0, 1e-6);
    CHECK_NEAR(tool_type_factor(TOOL_USE_KNAPPING, ITEM_COBBLE), 1.0, 1e-6);
    CHECK_NEAR(tool_type_factor(TOOL_USE_WOODWORKING, ITEM_CHOPPER), 0.6, 1e-6);
    CHECK_NEAR(tool_type_factor(TOOL_USE_BUILDING, ITEM_HAND_AXE), 1.0, 1e-6);
    /* No tool helps: always ×1. */
    CHECK_NEAR(tool_type_factor(TOOL_USE_NONE, ITEM_NONE), 1.0, 1e-6);
    CHECK_NEAR(tool_type_factor(TOOL_USE_NONE, ITEM_FLAKE), 1.0, 1e-6);
    CHECK_NEAR(tool_type_factor(TOOL_USE_BUILDING, ITEM_TYPE_COUNT), 0.0, 1e-6);
}

static void test_grade_factors(void)
{
    CHECK_NEAR(tool_grade_factor(tool(ITEM_FLAKE, ITEM_QUALITY_CRUDE, 15)), 0.8, 1e-6);
    CHECK_NEAR(tool_grade_factor(tool(ITEM_FLAKE, ITEM_QUALITY_STANDARD, 15)), 1.0, 1e-6);
    CHECK_NEAR(tool_grade_factor(tool(ITEM_FLAKE, ITEM_QUALITY_SUPERB, 15)), 1.2, 1e-6);
    /* Items without quality: ×1. */
    CHECK_NEAR(tool_grade_factor(tool(ITEM_TWIGS, ITEM_QUALITY_CRUDE, 10)), 1.0, 1e-6);
    CHECK_NEAR(tool_speed_factor(TOOL_USE_CUT_GRASS, tool(ITEM_HAND_AXE, ITEM_QUALITY_FINE, 9)),
               0.9 * 1.1, 1e-5);
    CHECK_NEAR(tool_speed_factor(TOOL_USE_CUT_GRASS, (ItemStack){.type = ITEM_NONE}), 0.6, 1e-6);
    CHECK_NEAR(tool_best_possible_factor(TOOL_USE_CUT_GRASS), 1.2, 1e-5);
    CHECK_NEAR(tool_best_possible_factor(TOOL_USE_NONE), 1.0, 1e-6);
}

static void test_best_tool(void)
{
    ItemStack tools[4] = {
        tool(ITEM_COBBLE, ITEM_QUALITY_SUPERB, 200),
        tool(ITEM_FLAKE, ITEM_QUALITY_CRUDE, 15),
        {.type = ITEM_NONE},
        tool(ITEM_HAND_AXE, ITEM_QUALITY_STANDARD, 100),
    };
    const ItemTypeMask any = ~(ItemTypeMask)0;
    /* Grass: hand axe ×0.9 beats a crude flake ×0.8; the cobble cannot cut. */
    CHECK_EQ_INT(tool_best(tools, 4, TOOL_USE_CUT_GRASS, any, true), 3);
    /* Only flakes allowed. */
    CHECK_EQ_INT(tool_best(tools, 4, TOOL_USE_CUT_GRASS, ITEM_TYPE_BIT(ITEM_FLAKE), true), 1);
    /* Building: a superb cobble (×0.9 × 1.2) beats the hand axe (×1.0). */
    CHECK_EQ_INT(tool_best(tools, 4, TOOL_USE_BUILDING, any, true), 0);
    /* Knapping: only the cobble. */
    CHECK_EQ_INT(tool_best(tools, 4, TOOL_USE_KNAPPING, any, true), 0);
    /* A crude flake (×0.8 × 0.8) is no better than bare hands for building. */
    ItemStack flake_only[1] = {tool(ITEM_FLAKE, ITEM_QUALITY_CRUDE, 15)};
    CHECK_EQ_INT(tool_best(flake_only, 1, TOOL_USE_BUILDING, any, true), -1);
    CHECK_EQ_INT(tool_best(flake_only, 1, TOOL_USE_BUILDING, any, false), 0);
    CHECK_EQ_INT(tool_best(flake_only, 1, TOOL_USE_KNAPPING, any, false), -1);
    /* Equal factors: the one with more hit points. */
    ItemStack two[2] = {tool(ITEM_COBBLE, ITEM_QUALITY_STANDARD, 50),
                        tool(ITEM_COBBLE, ITEM_QUALITY_STANDARD, 120)};
    CHECK_EQ_INT(tool_best(two, 2, TOOL_USE_KNAPPING, any, true), 1);
}

static void test_wear(void)
{
    ItemStack flake = tool(ITEM_FLAKE, ITEM_QUALITY_STANDARD, 2);
    CHECK(!tool_wear(&flake));
    CHECK_EQ_INT(flake.hit_points, 1);
    CHECK(tool_wear(&flake)); /* Broken. */
    CHECK_EQ_INT(flake.type, ITEM_NONE);
    CHECK_EQ_INT(flake.count, 0);
    CHECK(!tool_wear(&flake));
    /* Cobbles wear too (a hammer stone cracks in the end). */
    ItemStack cobble = tool(ITEM_COBBLE, ITEM_QUALITY_STANDARD, 200);
    CHECK(item_def(ITEM_COBBLE)->damageable);
    CHECK(!tool_wear(&cobble));
    CHECK_EQ_INT(cobble.hit_points, 199);
    /* Undamageable items never wear. */
    ItemStack block = tool(ITEM_STONE_BLOCK, ITEM_QUALITY_STANDARD, 150);
    CHECK(!tool_wear(&block));
    CHECK_EQ_INT(block.hit_points, item_def(ITEM_STONE_BLOCK)->max_hit_points);
    CHECK_EQ_INT(TOOL_WEAR_TICKS, TICKS_PER_HOUR);
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_type_factors);
    RUN_TEST(test_grade_factors);
    RUN_TEST(test_best_tool);
    RUN_TEST(test_wear);
    return test_summary();
}
