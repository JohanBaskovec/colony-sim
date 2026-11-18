/* How a blow is played on screen (core/strike_anim.h). */
#include "core/combat.h"
#include "core/game_clock.h"
#include "core/strike_anim.h"

#include "test_framework.h"

/* The attacker draws back, lunges past its place, and is back before its
 * next blow. */
static void test_attacker_pose(void)
{
    StrikePose rest = strike_anim_attacker(0);
    CHECK_NEAR(rest.advance, 0.0f, 1e-6f);
    StrikePose back = strike_anim_attacker(STRIKE_ANIM_WIND_TICKS - 1);
    CHECK(back.advance < -0.15f);
    CHECK(back.lean < 0.0f); /* Rearing. */
    StrikePose hit = strike_anim_attacker(STRIKE_ANIM_HIT_TICKS - 1);
    CHECK_NEAR(hit.advance, STRIKE_ANIM_FORWARD, 1e-4f);
    CHECK(hit.lean > 0.0f); /* Head down toward the target. */
    /* Drawing back moves steadily away, the return steadily back. */
    for (int t = 1; t < STRIKE_ANIM_WIND_TICKS; t++) {
        CHECK(strike_anim_attacker(t).advance <= strike_anim_attacker(t - 1).advance);
    }
    for (int t = STRIKE_ANIM_HIT_TICKS; t < STRIKE_ANIM_HIT_TICKS + STRIKE_ANIM_RETURN_TICKS; t++) {
        CHECK(strike_anim_attacker(t).advance <= strike_anim_attacker(t - 1).advance + 1e-6f);
    }
    StrikePose after = strike_anim_attacker(STRIKE_ANIM_HIT_TICKS + STRIKE_ANIM_RETURN_TICKS);
    CHECK_NEAR(after.advance, 0.0f, 1e-6f);
    CHECK_NEAR(after.lean, 0.0f, 1e-6f);
    CHECK_NEAR(strike_anim_attacker(-1).advance, 0.0f, 1e-6f);
    /* The quickest attack strikes again only once the last lunge is over. */
    for (int type = ATTACK_NONE + 1; type < ATTACK_TYPE_COUNT; type++) {
        CHECK(attack_interval_ticks((AttackType)type) >=
              STRIKE_ANIM_HIT_TICKS + STRIKE_ANIM_RETURN_TICKS);
    }
}

/* The one struck flinches and flashes when the blow lands, not before. */
static void test_struck(void)
{
    CHECK_NEAR(strike_anim_recoil(STRIKE_ANIM_HIT_TICKS - 1), 0.0f, 1e-6f);
    CHECK_NEAR(strike_anim_flash(STRIKE_ANIM_HIT_TICKS - 1), 0.0f, 1e-6f);
    CHECK(strike_anim_recoil(STRIKE_ANIM_HIT_TICKS) > 0.0f);
    CHECK(strike_anim_flash(STRIKE_ANIM_HIT_TICKS) > 0.0f);
    CHECK_NEAR(strike_anim_recoil(STRIKE_ANIM_TICKS), 0.0f, 1e-6f);
    CHECK_NEAR(strike_anim_flash(STRIKE_ANIM_TICKS), 0.0f, 1e-6f);
}

/* The mark is drawn quickly, held, then fades out by the end. */
static void test_mark(void)
{
    float drawn = -1.0f, opacity = -1.0f;
    CHECK(!strike_anim_mark(STRIKE_ANIM_HIT_TICKS - 1, &drawn, &opacity));
    REQUIRE(strike_anim_mark(STRIKE_ANIM_HIT_TICKS, &drawn, &opacity));
    CHECK(drawn > 0.0f && drawn < 1.0f);
    CHECK_NEAR(opacity, 1.0f, 1e-6f);
    REQUIRE(
        strike_anim_mark(STRIKE_ANIM_HIT_TICKS + STRIKE_ANIM_MARK_DRAW_TICKS, &drawn, &opacity));
    CHECK_NEAR(drawn, 1.0f, 1e-6f);
    CHECK_NEAR(opacity, 1.0f, 1e-6f);
    REQUIRE(strike_anim_mark(STRIKE_ANIM_TICKS - 1, &drawn, &opacity));
    CHECK(opacity > 0.0f && opacity < 0.1f);
    CHECK(!strike_anim_mark(STRIKE_ANIM_TICKS, &drawn, &opacity));
}

int main(void)
{
    RUN_TEST(test_attacker_pose);
    RUN_TEST(test_struck);
    RUN_TEST(test_mark);
    return test_summary();
}
