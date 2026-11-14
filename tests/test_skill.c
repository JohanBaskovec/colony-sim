#include "core/i18n.h"
#include "core/skill.h"
#include "core/work.h"

#include "test_framework.h"

#include <stdlib.h>

static void test_definitions(void)
{
    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        const SkillDef *def = skill_def((SkillType)skill);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL);
        CHECK(i18n_is_valid_key(def->name_key));
    }
    CHECK(skill_def(SKILL_NONE) == NULL);

    /* Each skill is used by exactly one work type; hauling needs none. */
    int users[SKILL_COUNT] = {0};
    for (int type = 0; type < WORK_TYPE_COUNT; type++) {
        SkillType skill = work_type_def((WorkType)type)->skill;
        if (skill != SKILL_NONE) {
            REQUIRE((unsigned)skill < SKILL_COUNT);
            users[skill]++;
        }
    }
    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        CHECK_EQ_INT(users[skill], 1);
    }
    CHECK(work_type_def(WORK_HAULING)->skill == SKILL_NONE);
    CHECK(work_type_def(WORK_WOODCUTTING)->skill == SKILL_WOODCUTTING);
}

static void test_random_levels(void)
{
    Rng rng;
    rng_seed(&rng, 7, 1);
    int histogram[SKILL_LEVEL_MAX + 1] = {0};
    for (int i = 0; i < 2000; i++) {
        int level = skill_random_level(&rng);
        REQUIRE(level >= SKILL_LEVEL_MIN && level <= SKILL_LEVEL_MAX);
        histogram[level]++;
    }
    /* Two dice: the middle is common, both ends are rare but happen. */
    CHECK(histogram[SKILL_START_DIE] > histogram[0] * 3);
    CHECK(histogram[0] > 0);
    CHECK(histogram[2 * SKILL_START_DIE] > 0);
    CHECK_EQ_INT(histogram[2 * SKILL_START_DIE + 1], 0);
}

static void test_products_improve_with_skill(void)
{
    /* Average results (no rng) rise with the level. */
    for (int level = SKILL_LEVEL_MIN; level < SKILL_LEVEL_MAX; level++) {
        CHECK(skill_product_score(level + 1, NULL) > skill_product_score(level, NULL));
        CHECK(skill_product_condition(level + 1, NULL) >= skill_product_condition(level, NULL));
    }
    CHECK_EQ_INT(skill_product_score(0, NULL), SKILL_QUALITY_BASE);
    CHECK_EQ_INT(skill_product_condition(0, NULL), SKILL_CONDITION_BASE);
    CHECK_EQ_INT(skill_product_condition(SKILL_LEVEL_MAX, NULL), 100);
    /* Out of range levels are clamped. */
    CHECK_EQ_INT(skill_product_score(-5, NULL), skill_product_score(SKILL_LEVEL_MIN, NULL));
    CHECK_EQ_INT(skill_product_score(99, NULL), skill_product_score(SKILL_LEVEL_MAX, NULL));

    /* Random results stay within the spread and the valid range. */
    Rng rng;
    rng_seed(&rng, 3, 2);
    for (int level = SKILL_LEVEL_MIN; level <= SKILL_LEVEL_MAX; level++) {
        int mean_quality = skill_product_score(level, NULL);
        int mean_condition = skill_product_condition(level, NULL);
        for (int i = 0; i < 50; i++) {
            int quality = skill_product_score(level, &rng);
            int condition = skill_product_condition(level, &rng);
            CHECK(quality >= 0 && quality <= 100);
            CHECK(abs(quality - mean_quality) <= SKILL_PRODUCT_SPREAD);
            CHECK(condition >= 1 && condition <= 100);
            CHECK(condition <= mean_condition + SKILL_PRODUCT_SPREAD);
        }
    }
    /* An expert never damages what it produces. */
    for (int i = 0; i < 100; i++) {
        CHECK_EQ_INT(skill_product_condition(SKILL_LEVEL_MAX, &rng), 100);
    }
}

static void test_grades_of_scores(void)
{
    CHECK_EQ_INT(skill_quality_of_score(0), ITEM_QUALITY_CRUDE);
    CHECK_EQ_INT(skill_quality_of_score(19), ITEM_QUALITY_CRUDE);
    CHECK_EQ_INT(skill_quality_of_score(20), ITEM_QUALITY_ROUGH);
    CHECK_EQ_INT(skill_quality_of_score(50), ITEM_QUALITY_STANDARD);
    CHECK_EQ_INT(skill_quality_of_score(79), ITEM_QUALITY_FINE);
    CHECK_EQ_INT(skill_quality_of_score(80), ITEM_QUALITY_SUPERB);
    CHECK_EQ_INT(skill_quality_of_score(100), ITEM_QUALITY_SUPERB);
    CHECK_EQ_INT(skill_quality_of_score(-3), ITEM_QUALITY_CRUDE);

    /* Typical settlers: novices never make superb items, experts never crude ones. */
    Rng rng;
    rng_seed(&rng, 5, 5);
    for (int i = 0; i < 200; i++) {
        CHECK(skill_quality_of_score(skill_product_score(0, &rng)) <= ITEM_QUALITY_ROUGH);
        CHECK(skill_quality_of_score(skill_product_score(SKILL_LEVEL_MAX, &rng)) >=
              ITEM_QUALITY_FINE);
    }
}

static void test_product_stacks(void)
{
    /* Logs depend on the woodcutter. */
    ItemStack novice = skill_product(ITEM_WOOD, 10, 0, NULL);
    ItemStack expert = skill_product(ITEM_WOOD, 10, SKILL_LEVEL_MAX, NULL);
    CHECK_EQ_INT(novice.type, ITEM_WOOD);
    CHECK_EQ_INT(novice.count, 10);
    CHECK_EQ_INT(novice.quality, ITEM_QUALITY_ROUGH); /* Score 25. */
    CHECK_EQ_INT(item_stack_hit_points_percent(novice), SKILL_CONDITION_BASE);
    CHECK_EQ_INT(expert.quality, ITEM_QUALITY_SUPERB);
    CHECK_EQ_INT(expert.hit_points, item_def(ITEM_WOOD)->max_hit_points);

    /* Stone is always perfect, whoever mines it, and draws no randomness. */
    Rng rng, untouched;
    rng_seed(&rng, 11, 0);
    untouched = rng;
    ItemStack stone = skill_product(ITEM_STONE, 3, 0, &rng);
    CHECK_EQ_INT(stone.quality, ITEM_QUALITY_BEST);
    CHECK_EQ_INT(stone.hit_points, item_def(ITEM_STONE)->max_hit_points);
    CHECK_EQ_UINT(rng.state, untouched.state);

    /* Berries vary, and draw randomness. */
    ItemStack berries = skill_product(ITEM_BERRIES, 30, 5, &rng);
    CHECK(rng.state != untouched.state);
    CHECK(berries.quality <= ITEM_QUALITY_BEST);
    CHECK(berries.hit_points >= 1 && berries.hit_points <= item_def(ITEM_BERRIES)->max_hit_points);

    CHECK_EQ_INT(skill_product(ITEM_NONE, 3, 5, NULL).type, ITEM_NONE);
    CHECK_EQ_INT(skill_product(ITEM_WOOD, 0, 5, NULL).count, 0);
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_random_levels);
    RUN_TEST(test_products_improve_with_skill);
    RUN_TEST(test_grades_of_scores);
    RUN_TEST(test_product_stacks);
    return test_summary();
}
