#include "core/designation.h"
#include "core/game_clock.h"
#include "core/gather_zone.h"
#include "core/i18n.h"
#include "core/knowledge.h"
#include "core/work.h"

#include "test_framework.h"

#include <stdlib.h>
#include <string.h>

static void test_definitions(void)
{
    for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
        const KnowledgeDef *def = knowledge_def((KnowledgeType)k);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL);
        CHECK(i18n_is_valid_key(def->name_key));
        CHECK(def->starting); /* Only the starting knowledges exist so far. */
        for (int other = 0; other < k; other++) {
            CHECK(strcmp(def->id, knowledge_def((KnowledgeType)other)->id) != 0);
        }
    }
    CHECK(knowledge_def(KNOWLEDGE_NONE) == NULL);

    CHECK(work_type_def(WORK_GATHERING)->knowledge == KNOWLEDGE_GATHERING);
    CHECK(work_type_def(WORK_HAULING)->knowledge == KNOWLEDGE_NONE);

    /* Every order needs at least the mastery level of its knowledge. */
    for (int type = DESIGNATION_NONE + 1; type < DESIGNATION_COUNT; type++) {
        const DesignationDef *def = designation_def((DesignationType)type);
        CHECK(def->min_level >= KNOWLEDGE_MASTERY_LEVEL);
        CHECK(def->knowledge == KNOWLEDGE_NONE || (unsigned)def->knowledge < KNOWLEDGE_COUNT);
    }
}

static void test_mastery_and_orders(void)
{
    CHECK(!knowledge_is_mastered(0.0));
    CHECK(!knowledge_is_mastered(49.99));
    CHECK(knowledge_is_mastered(50.0));
    CHECK(knowledge_is_mastered(100.0));

    /* Gathering orders from 50 on; nobody can fell trees or mine yet. */
    CHECK(!gather_job_doable_at(GATHER_JOB_HARVEST, 49.9));
    CHECK(gather_job_doable_at(GATHER_JOB_HARVEST, 50.0));
    CHECK(gather_job_doable_at(GATHER_JOB_HARVEST, 80.0));
    CHECK(!gather_job_doable_at(GATHER_JOB_CHOP, 100.0));
    CHECK(!gather_job_doable_at(GATHER_JOB_COUNT, 100.0));
    CHECK(!designation_doable_at(DESIGNATION_MINE, 100.0));
    CHECK(!designation_doable_at(DESIGNATION_NONE, 100.0));
    CHECK(!designation_doable_at(DESIGNATION_COUNT, 100.0));
}

static void test_work_speed(void)
{
    CHECK_NEAR(knowledge_work_speed(50.0), KNOWLEDGE_SPEED_AT_MASTERY, 1e-6);
    CHECK_NEAR(knowledge_work_speed(75.0), 1.0, 1e-6);
    CHECK_NEAR(knowledge_work_speed(100.0), KNOWLEDGE_SPEED_AT_MAX, 1e-6);
    /* Out of range levels are clamped. */
    CHECK_NEAR(knowledge_work_speed(20.0), KNOWLEDGE_SPEED_AT_MASTERY, 1e-6);
    CHECK_NEAR(knowledge_work_speed(130.0), KNOWLEDGE_SPEED_AT_MAX, 1e-6);
    for (int level = 50; level < 100; level++) {
        CHECK(knowledge_work_speed(level + 1) > knowledge_work_speed(level));
    }
}

static void test_difficulty_and_age(void)
{
    CHECK_NEAR(knowledge_difficulty(0.0), 110.0 / 60.0, 1e-9);
    CHECK_NEAR(knowledge_difficulty(50.0), 1.0, 1e-9);
    CHECK_NEAR(knowledge_difficulty(100.0), 10.0 / 60.0, 1e-9);
    CHECK_NEAR(knowledge_difficulty(120.0), 0.0, 1e-9);

    CHECK_NEAR(knowledge_age_factor(3), KNOWLEDGE_CHILD_FACTOR, 1e-9);
    CHECK_NEAR(knowledge_age_factor(13), KNOWLEDGE_CHILD_FACTOR, 1e-9);
    CHECK_NEAR(knowledge_age_factor(14), 1.0, 1e-9);
    CHECK_NEAR(knowledge_age_factor(59), 1.0, 1e-9);
    CHECK_NEAR(knowledge_age_factor(60), KNOWLEDGE_ELDER_FACTOR, 1e-9);
}

static void test_learning(void)
{
    /* One hour of doing at 50 (difficulty 1): one point for an adult. */
    CHECK_NEAR(knowledge_learn(50.0, KNOWLEDGE_DOING_RATE, 1.0, 30), 51.0, 1e-9);
    CHECK_NEAR(knowledge_learn(50.0, KNOWLEDGE_DOING_RATE, 1.0, 10), 52.0, 1e-9);
    CHECK_NEAR(knowledge_learn(50.0, KNOWLEDGE_DOING_RATE, 1.0, 70), 50.5, 1e-9);
    /* Nothing to learn from nothing; never above the maximum. */
    CHECK_NEAR(knowledge_learn(60.0, 0.0, 1.0, 30), 60.0, 1e-9);
    CHECK_NEAR(knowledge_learn(60.0, 1.0, -1.0, 30), 60.0, 1e-9);
    CHECK_NEAR(knowledge_learn(99.9, 1.0, 10.0, 30), KNOWLEDGE_LEVEL_MAX, 1e-9);

    /* Tick by tick, as settlers learn: from 50 to about 98.7 in 100 hours of
     * work, and to the maximum in 110 hours (level = 110 − 60 e^(−t/60)). */
    double level = 50.0;
    for (long tick = 0; tick < 100L * TICKS_PER_HOUR; tick++) {
        level = knowledge_learn(level, KNOWLEDGE_DOING_RATE, 1.0 / TICKS_PER_HOUR, 30);
    }
    CHECK(level > 98.4 && level < 99.0);
    for (long tick = 0; tick < 10L * TICKS_PER_HOUR; tick++) {
        level = knowledge_learn(level, KNOWLEDGE_DOING_RATE, 1.0 / TICKS_PER_HOUR, 30);
    }
    CHECK_NEAR(level, KNOWLEDGE_LEVEL_MAX, 1e-9);
}

static void test_starting_levels(void)
{
    Rng rng;
    rng_seed(&rng, 7, 1);
    int low = 0, high = 0, middle = 0;
    for (int i = 0; i < 2000; i++) {
        float level = knowledge_random_starting_level(&rng);
        REQUIRE(level >= KNOWLEDGE_START_MIN &&
                level <= KNOWLEDGE_START_MIN + KNOWLEDGE_START_SPAN);
        low += level == KNOWLEDGE_START_MIN;
        high += level == KNOWLEDGE_START_MIN + KNOWLEDGE_START_SPAN;
        middle += level == KNOWLEDGE_START_MIN + KNOWLEDGE_START_SPAN / 2.0f;
    }
    /* Two dice: mostly around 65, the ends are rare but happen. */
    CHECK(low > 0 && high > 0);
    CHECK(middle > 3 * low && middle > 3 * high);
}

static void test_products_improve_with_level(void)
{
    /* Average results (no rng) rise with the level. */
    for (int level = 50; level < 100; level += 5) {
        CHECK(knowledge_product_score(level + 5, NULL) > knowledge_product_score(level, NULL));
        CHECK(knowledge_product_condition(level + 5, NULL) >=
              knowledge_product_condition(level, NULL));
    }
    CHECK_EQ_INT(knowledge_product_score(50.0, NULL), 25);
    CHECK_EQ_INT(knowledge_product_score(75.0, NULL), 60);
    CHECK_EQ_INT(knowledge_product_score(100.0, NULL), 95);
    CHECK_EQ_INT(knowledge_product_condition(50.0, NULL), 50);
    CHECK_EQ_INT(knowledge_product_condition(75.0, NULL), 90);
    CHECK_EQ_INT(knowledge_product_condition(100.0, NULL), 100);
    /* Below mastery: as at mastery (callers do not let anyone work there). */
    CHECK_EQ_INT(knowledge_product_score(10.0, NULL), knowledge_product_score(50.0, NULL));

    /* Random results stay within the spread and the valid range. */
    Rng rng;
    rng_seed(&rng, 3, 2);
    for (int level = 50; level <= 100; level += 10) {
        int mean_quality = knowledge_product_score(level, NULL);
        int mean_condition = knowledge_product_condition(level, NULL);
        for (int i = 0; i < 50; i++) {
            int quality = knowledge_product_score(level, &rng);
            int condition = knowledge_product_condition(level, &rng);
            CHECK(quality >= 0 && quality <= 100);
            CHECK(abs(quality - mean_quality) <= KNOWLEDGE_PRODUCT_SPREAD);
            CHECK(condition >= 1 && condition <= 100);
            CHECK(condition <= mean_condition + KNOWLEDGE_PRODUCT_SPREAD);
        }
    }
    /* A master never damages what it produces. */
    for (int i = 0; i < 100; i++) {
        CHECK_EQ_INT(knowledge_product_condition(100.0, &rng), 100);
    }
}

static void test_quality_is_the_score(void)
{
    /* The quality of a product is its score, 0..100: 25 at mastery, 95 at
     * the top (before the spread). */
    CHECK_EQ_INT(knowledge_product_score(50.0, NULL), 25);
    CHECK_EQ_INT(knowledge_product_score(100.0, NULL), 95);
    CHECK_EQ_INT(knowledge_product_score(75.0, NULL), 60);

    /* Beginners never make the best items, masters never the worst ones. */
    Rng rng;
    rng_seed(&rng, 5, 5);
    for (int i = 0; i < 200; i++) {
        int beginner = knowledge_product_score(50.0, &rng);
        int master = knowledge_product_score(100.0, &rng);
        CHECK(beginner >= 15 && beginner <= 35);
        CHECK(master >= 85 && master <= 100);
        CHECK(beginner < master);
    }
}

static void test_product_stacks(void)
{
    /* Branches depend on the gatherer. */
    ItemStack beginner = knowledge_product(ITEM_BRANCHES, 4, 50.0, NULL);
    ItemStack master = knowledge_product(ITEM_BRANCHES, 4, 100.0, NULL);
    CHECK_EQ_INT(beginner.type, ITEM_BRANCHES);
    CHECK_EQ_INT(beginner.count, 4);
    CHECK_EQ_INT(beginner.quality, 25); /* Score 25. */
    CHECK_EQ_INT(item_stack_hit_points_percent(beginner), 50);
    CHECK_EQ_INT(master.quality, 95);
    CHECK_EQ_INT(master.hit_points, item_def(ITEM_BRANCHES)->max_hit_points);

    /* Stones: a quality (how well they knap), never damaged. */
    ItemStack stones = knowledge_product(ITEM_COBBLE, 1, 100.0, NULL);
    CHECK_EQ_INT(stones.quality, 95);
    CHECK_EQ_INT(stones.hit_points, item_def(ITEM_COBBLE)->max_hit_points);
    CHECK_EQ_INT(knowledge_product(ITEM_COBBLE, 1, 50.0, NULL).quality, 25);

    /* Stone blocks are always perfect and draw no randomness. */
    Rng rng, untouched;
    rng_seed(&rng, 11, 0);
    untouched = rng;
    ItemStack block = knowledge_product(ITEM_STONE_BLOCK, 3, 50.0, &rng);
    CHECK_EQ_INT(block.quality, ITEM_QUALITY_BEST);
    CHECK_EQ_INT(block.hit_points, item_def(ITEM_STONE_BLOCK)->max_hit_points);
    CHECK_EQ_UINT(rng.state, untouched.state);

    /* Berries vary, and draw randomness. */
    ItemStack berries = knowledge_product(ITEM_BLACKBERRIES, 30, 60.0, &rng);
    CHECK(rng.state != untouched.state);
    CHECK(berries.hit_points >= 1 &&
          berries.hit_points <= item_def(ITEM_BLACKBERRIES)->max_hit_points);

    CHECK_EQ_INT(knowledge_product(ITEM_NONE, 3, 60.0, NULL).type, ITEM_NONE);
    CHECK_EQ_INT(knowledge_product(ITEM_WOOD, 0, 60.0, NULL).count, 0);
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_mastery_and_orders);
    RUN_TEST(test_work_speed);
    RUN_TEST(test_difficulty_and_age);
    RUN_TEST(test_learning);
    RUN_TEST(test_starting_levels);
    RUN_TEST(test_products_improve_with_level);
    RUN_TEST(test_quality_is_the_score);
    RUN_TEST(test_product_stacks);
    return test_summary();
}
