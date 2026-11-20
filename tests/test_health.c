/* Bodies, injuries, bleeding, pain, abilities, death and healing
 * (core/health.h, docs/HEALTH.md). */
#include "core/animal.h"
#include "core/attributes.h"
#include "core/game.h"
#include "core/health.h"
#include "core/i18n.h"
#include "core/tree.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <math.h>
#include <string.h>

/* Parts of the human plan. */
enum { HEAD, TORSO, LEFT_ARM, RIGHT_ARM, LEFT_LEG, RIGHT_LEG };

static Health human(void)
{
    Health health;
    health_init(&health, BODY_HUMAN, 1.0f, 1.0f, 1.0f);
    return health;
}

/* Runs `hours` in steps of a tick's worth of time. */
static bool run_hours(Health *health, float hours)
{
    const float step = 1.0f / 60.0f;
    bool died = false;
    for (float t = 0.0f; t < hours; t += step) {
        died = health_update(health, step) || died;
    }
    return died;
}

static void test_body_plans(void)
{
    CHECK_EQ_INT(body_part_count(BODY_HUMAN), 6);
    CHECK_EQ_INT(body_part_count(BODY_QUADRUPED), 7);
    CHECK_EQ_INT(body_part_count(BODY_HARE), 6);
    CHECK_EQ_INT(body_part_count(BODY_CANINE), 7);
    CHECK_EQ_INT(body_part_count(BODY_PLAN_COUNT), 0);
    CHECK(body_part_def(BODY_HUMAN, -1) == NULL);
    CHECK(body_part_def(BODY_HUMAN, 6) == NULL);
    for (int plan = 0; plan < BODY_PLAN_COUNT; plan++) {
        REQUIRE(body_part_count((BodyPlan)plan) <= HEALTH_MAX_PARTS);
        float coverage = 0.0f, walk = 0.0f, hands = 0.0f;
        int minds = 0, vitals = 0;
        for (int part = 0; part < body_part_count((BodyPlan)plan); part++) {
            const BodyPartDef *def = body_part_def((BodyPlan)plan, part);
            REQUIRE(def != NULL);
            CHECK(i18n_is_valid_key(def->name_key));
            CHECK(def->hit_points > 0.0f);
            coverage += def->coverage;
            walk += def->role == BODY_ROLE_WALK ? def->share : 0.0f;
            hands += def->role == BODY_ROLE_HANDS ? def->share : 0.0f;
            minds += def->role == BODY_ROLE_MIND;
            vitals += def->vital;
        }
        CHECK_NEAR(coverage, 1.0, 1e-5);
        CHECK_NEAR(walk, 1.0, 1e-5);
        CHECK(plan == BODY_HUMAN ? fabsf(hands - 1.0f) < 1e-5f : hands == 0.0f);
        CHECK_EQ_INT(minds, 1);
        CHECK(vitals >= 2);
    }
    for (int type = 0; type < INJURY_TYPE_COUNT; type++) {
        CHECK(i18n_is_valid_key(injury_def((InjuryType)type)->name_key));
    }
    CHECK(injury_def(INJURY_TYPE_COUNT) == NULL);
    for (int ability = 0; ability < HEALTH_ABILITY_COUNT; ability++) {
        CHECK(i18n_is_valid_key(health_ability_name_key((HealthAbility)ability)));
    }
}

/* What a plan strikes with: the limbs its parts carry. */
static void test_limbs(void)
{
    CHECK(body_plan_has_limb(BODY_HUMAN, BODY_LIMB_ARMS));
    CHECK(!body_plan_has_limb(BODY_HUMAN, BODY_LIMB_HEAD));
    CHECK(!body_plan_has_limb(BODY_HUMAN, BODY_LIMB_FORELEGS));
    for (int plan = BODY_QUADRUPED; plan < BODY_PLAN_COUNT; plan++) {
        bool hare = plan == BODY_HARE;
        CHECK(!body_plan_has_limb((BodyPlan)plan, BODY_LIMB_ARMS));
        CHECK_EQ_INT(body_plan_has_limb((BodyPlan)plan, BODY_LIMB_HEAD), !hare);
        CHECK_EQ_INT(body_plan_has_limb((BodyPlan)plan, BODY_LIMB_FORELEGS), !hare);
        CHECK_EQ_INT(body_plan_has_limb((BodyPlan)plan, BODY_LIMB_HINDLEGS), !hare);
    }
    CHECK(!body_plan_has_limb(BODY_HUMAN, BODY_LIMB_NONE));
    CHECK(!body_plan_has_limb(BODY_HUMAN, BODY_LIMB_COUNT));
    CHECK(body_limb_name_key(BODY_LIMB_NONE) == NULL);
    for (int limb = BODY_LIMB_ARMS; limb < BODY_LIMB_COUNT; limb++) {
        CHECK(i18n_is_valid_key(body_limb_name_key((BodyLimb)limb)));
    }
    /* A part carries one limb, and a limb's parts are at least one. */
    for (int plan = 0; plan < BODY_PLAN_COUNT; plan++) {
        for (int part = 0; part < body_part_count((BodyPlan)plan); part++) {
            CHECK((unsigned)body_part_def((BodyPlan)plan, part)->limb < BODY_LIMB_COUNT);
        }
    }
}

static void test_limb_ability(void)
{
    Health health = human();
    CHECK_NEAR(health_limb_ability(&health, BODY_LIMB_ARMS), 1.0, 1e-6);
    CHECK_NEAR(health_limb_ability(&health, BODY_LIMB_HEAD), 0.0, 1e-9); /* No such limb. */
    CHECK_NEAR(health_limb_ability(&health, BODY_LIMB_NONE), 0.0, 1e-9);
    /* One arm lost of two: half the strength (less, for the pain's toll on
     * consciousness); the hands agree. */
    health_hurt(&health, LEFT_ARM, INJURY_BRUISE, 20.0f);
    CHECK_NEAR(health_limb_ability(&health, BODY_LIMB_ARMS), 0.45, 0.05);
    CHECK_NEAR(health_limb_ability(&health, BODY_LIMB_ARMS), health_ability(&health, HEALTH_HANDS),
               1e-6);
    /* Both: nothing. */
    health_hurt(&health, RIGHT_ARM, INJURY_BRUISE, 20.0f);
    CHECK_NEAR(health_limb_ability(&health, BODY_LIMB_ARMS), 0.0, 1e-6);

    /* A fracture leaves a tenth of it. */
    Health fractured = human();
    health_hurt(&fractured, LEFT_ARM, INJURY_FRACTURE, 2.0f);
    health_hurt(&fractured, RIGHT_ARM, INJURY_FRACTURE, 2.0f);
    CHECK(health_limb_ability(&fractured, BODY_LIMB_ARMS) < 0.15f);

    /* A wolf: ruined forepaws leave the head and the hind legs whole. */
    Health wolf;
    health_init(&wolf, BODY_CANINE, 0.7f, 1.0f, 1.0f);
    CHECK_NEAR(health_limb_ability(&wolf, BODY_LIMB_FORELEGS), 1.0, 1e-6);
    health_hurt(&wolf, 3, INJURY_FRACTURE, 1.0f);
    health_hurt(&wolf, 4, INJURY_FRACTURE, 1.0f);
    CHECK(health_limb_ability(&wolf, BODY_LIMB_FORELEGS) < 0.15f);
    CHECK(health_limb_ability(&wolf, BODY_LIMB_HEAD) > 0.9f); /* A little pain only. */
    CHECK(health_limb_ability(&wolf, BODY_LIMB_HINDLEGS) > 0.9f);
    CHECK_NEAR(health_limb_ability(&wolf, BODY_LIMB_ARMS), 0.0, 1e-9);

    /* Knocked out: nothing works. */
    Health out = human();
    health_hurt(&out, TORSO, INJURY_BRUISE, 38.0f);
    health_hurt(&out, HEAD, INJURY_BRUISE, 20.0f);
    REQUIRE(health_is_unconscious(&out));
    CHECK_NEAR(health_limb_ability(&out, BODY_LIMB_ARMS), 0.0, 1e-9);
    /* And the dead. */
    Health dead = human();
    health_hurt(&dead, HEAD, INJURY_CUT, 100.0f);
    REQUIRE(health_is_dead(&dead));
    CHECK_NEAR(health_limb_ability(&dead, BODY_LIMB_ARMS), 0.0, 1e-9);
}

static void test_healthy_body(void)
{
    Health health = human();
    CHECK(!health_is_hurt(&health));
    CHECK(!health_is_dead(&health));
    CHECK(!health_is_down(&health));
    for (int ability = 0; ability < HEALTH_ABILITY_COUNT; ability++) {
        CHECK_NEAR(health_ability(&health, (HealthAbility)ability), 1.0, 1e-6);
    }
    CHECK_NEAR(health_pain(&health), 0.0, 1e-9);
    CHECK_NEAR(health_bleeding(&health), 0.0, 1e-9);
    CHECK(!health_update(&health, 10.0f));
    CHECK_EQ_UINT(health_hash(&health, 1234u), 1234u); /* Out of the hash. */

    /* Animals have no hands. */
    Health deer;
    health_init(&deer, BODY_QUADRUPED, 1.4f, 1.0f, 1.0f);
    CHECK_NEAR(health_ability(&deer, HEALTH_HANDS), 0.0, 1e-9);
    CHECK_NEAR(health_ability(&deer, HEALTH_WALKING), 1.0, 1e-6);
    CHECK_NEAR(health_part_hit_points(&deer, 2), 45.0 * 1.4, 1e-4); /* Its torso. */
}

static void test_hurting(void)
{
    Health health = human();
    CHECK_NEAR(health_hurt(&health, LEFT_ARM, INJURY_BRUISE, 5.0f), 5.0, 1e-6);
    CHECK(health_is_hurt(&health));
    CHECK_NEAR(health_part_damage(&health, LEFT_ARM), 5.0, 1e-6);
    CHECK_NEAR(health_part_efficiency(&health, LEFT_ARM), 0.75, 1e-6);
    CHECK_NEAR(health_part_efficiency(&health, RIGHT_ARM), 1.0, 1e-6);
    CHECK(health_hash(&health, 1234u) != 1234u);
    /* Nothing for an invalid part or kind, or no damage. */
    CHECK_NEAR(health_hurt(&health, 9, INJURY_CUT, 5.0f), 0.0, 1e-9);
    CHECK_NEAR(health_hurt(&health, HEAD, INJURY_TYPE_COUNT, 5.0f), 0.0, 1e-9);
    CHECK_NEAR(health_hurt(&health, HEAD, INJURY_CUT, 0.0f), 0.0, 1e-9);
    CHECK_EQ_INT(health.injury_count, 1);

    /* Damage is capped at the part's hit points; a destroyed limb is useless. */
    health_hurt(&health, LEFT_ARM, INJURY_CUT, 50.0f);
    CHECK_NEAR(health_part_damage(&health, LEFT_ARM), 20.0, 1e-6);
    CHECK_NEAR(health_part_efficiency(&health, LEFT_ARM), 0.0, 1e-6);
    CHECK(!health_is_dead(&health)); /* Not vital. */

    /* A fracture caps the part's efficiency. */
    health_hurt(&health, LEFT_LEG, INJURY_FRACTURE, 2.0f);
    CHECK_NEAR(health_part_efficiency(&health, LEFT_LEG), HEALTH_FRACTURE_EFFICIENCY, 1e-6);

    /* The damage factor (toughness). */
    Health fragile;
    health_init(&fragile, BODY_HUMAN, 1.0f, 2.0f, 1.0f);
    CHECK_NEAR(health_hurt(&fragile, TORSO, INJURY_BRUISE, 5.0f), 10.0, 1e-6);
}

static void test_vital_part_kills(void)
{
    Health health = human();
    health_hurt(&health, HEAD, INJURY_CRUSH, 20.0f);
    CHECK(!health_is_dead(&health));
    health_hurt(&health, HEAD, INJURY_CRUSH, 5.0f);
    CHECK(health_is_dead(&health));
    CHECK_EQ_INT(health.death, DEATH_VITAL_PART);
    CHECK_EQ_INT(health.death_part, HEAD);
    /* A dead body takes no more blows and has no abilities. */
    CHECK_NEAR(health_hurt(&health, TORSO, INJURY_CUT, 5.0f), 0.0, 1e-9);
    CHECK_NEAR(health_ability(&health, HEALTH_WALKING), 0.0, 1e-9);
    CHECK(!health_is_down(&health)); /* Dead, not down. */
    CHECK(!health_update(&health, 1.0f));
}

/* A small cut bleeds, clots within a few hours, then heals and the blood
 * comes back. */
static void test_small_cut_heals(void)
{
    Health health = human();
    health_hurt(&health, LEFT_ARM, INJURY_CUT, 5.0f);
    CHECK(!health_injury_serious(&health, &health.injuries[0]));
    CHECK_NEAR(health_bleeding(&health), 5.0 * HEALTH_BLEED_PER_DAMAGE_HOUR, 1e-6);
    CHECK(!run_hours(&health, HEALTH_CLOT_HOURS + 0.1f));
    CHECK_NEAR(health_bleeding(&health), 0.0, 1e-9);
    /* Lost about half the first hour's rate over the clotting time. */
    float lost = 1.0f - health.blood;
    CHECK_NEAR(lost, 5.0 * HEALTH_BLEED_PER_DAMAGE_HOUR * HEALTH_CLOT_HOURS * 0.5, 2e-3);
    /* 3 points a day: gone in under two days. */
    run_hours(&health, 40.0f);
    CHECK_EQ_INT(health.injury_count, 0);
    run_hours(&health, 24.0f);
    CHECK(!health_is_hurt(&health)); /* The blood is back too. */
}

/* A serious cut on the torso keeps bleeding: dead in about 8 hours. */
static void test_serious_cut_kills(void)
{
    Health health = human();
    health_hurt(&health, TORSO, INJURY_CUT, 16.0f);
    CHECK(health_injury_serious(&health, &health.injuries[0]));
    CHECK(!run_hours(&health, 6.0f));
    CHECK(health.blood < 0.65f);
    CHECK(!health_is_dead(&health));
    CHECK(run_hours(&health, 3.0f));
    CHECK(health_is_dead(&health));
    CHECK_EQ_INT(health.death, DEATH_BLOOD_LOSS);
}

/* Sets the blood and lets the body take note of it (the posture). */
static void set_blood(Health *health, float blood)
{
    health->blood = blood;
    health_update(health, 1e-6f);
}

/* Blood loss lowers consciousness from 85 % of the blood: collapsed, then
 * unconscious; getting up again takes a little more than falling. */
static void test_blood_loss_and_consciousness(void)
{
    Health health = human();
    set_blood(&health, 0.9f);
    CHECK_NEAR(health_consciousness(&health), 1.0, 1e-6);
    CHECK_NEAR(health_consciousness_loss(&health, CONSCIOUSNESS_BLOOD), 0.0, 1e-9);
    set_blood(&health, 0.7f);
    CHECK_NEAR(health_consciousness(&health), 1.0 - 0.15 / 0.35, 1e-4);
    CHECK(!health_is_collapsed(&health));
    set_blood(&health, 0.65f);
    CHECK(health_is_collapsed(&health));
    CHECK(!health_is_unconscious(&health));
    CHECK(health_is_down(&health));
    CHECK_STR_EQ(health_status_key(&health), "health.status.collapsed");
    CHECK_NEAR(health_ability(&health, HEALTH_WALKING), 0.0, 1e-9);
    CHECK_NEAR(health_ability(&health, HEALTH_HANDS), 0.0, 1e-9);
    CHECK(health_ability(&health, HEALTH_SIGHT) > 0.2f); /* Still sees. */
    set_blood(&health, 0.55f);
    CHECK(health_is_unconscious(&health));
    CHECK_STR_EQ(health_status_key(&health), "health.status.unconscious");
    CHECK_NEAR(health_ability(&health, HEALTH_SIGHT), 0.0, 1e-9);
    CHECK_NEAR(health_ability(&health, HEALTH_EATING), 0.0, 1e-9);

    /* Coming back: out until 30 %, down until 55 %. */
    set_blood(&health, 0.6f); /* 29 %. */
    CHECK(health_is_unconscious(&health));
    set_blood(&health, 0.62f); /* 34 %. */
    CHECK(!health_is_unconscious(&health));
    CHECK(health_is_collapsed(&health));
    set_blood(&health, 0.69f); /* 54 %: above the fall, below getting up. */
    CHECK(health_is_collapsed(&health));
    set_blood(&health, 0.71f); /* 60 %. */
    CHECK(!health_is_collapsed(&health));
    CHECK(!health_is_down(&health));
}

/* Lying collapsed, one sees less and less far; up, with no limit. */
static void test_sight_reach(void)
{
    Health health = human();
    CHECK_EQ_INT(health_sight_reach(&health), 0);
    set_blood(&health, 0.85f - 0.035f); /* 90 %: up. */
    CHECK_EQ_INT(health_sight_reach(&health), 0);
    set_blood(&health, 0.65f); /* 57 %... */
    set_blood(&health, 0.62f); /* 34 %: collapsed. */
    REQUIRE(health_is_collapsed(&health));
    int far = health_sight_reach(&health);
    CHECK(far > 1 && far < HEALTH_SIGHT_REACH_MAX);
    set_blood(&health, 0.61f); /* Lower: a shorter reach. */
    CHECK(health_sight_reach(&health) < far);
    set_blood(&health, 0.605f);
    CHECK(health_sight_reach(&health) >= 1);
    /* Just collapsed: as far as it will see. */
    Health fresh = human();
    set_blood(&fresh, 0.672f); /* 49 %. */
    REQUIRE(health_is_collapsed(&fresh));
    CHECK_EQ_INT(health_sight_reach(&fresh), HEALTH_SIGHT_REACH_MAX);
    Health dead = human();
    health_hurt(&dead, HEAD, INJURY_CUT, 100.0f);
    CHECK_EQ_INT(health_sight_reach(&dead), 0);
}

/* What consciousness puts on the body: nothing from 90 % up, half at 50 %. */
static void test_vigor(void)
{
    Health health = human();
    CHECK_NEAR(health_vigor(&health), 1.0, 1e-6);
    set_blood(&health, 0.85f - 0.035f); /* 90 %. */
    CHECK_NEAR(health_consciousness(&health), 0.9, 1e-4);
    CHECK_NEAR(health_vigor(&health), 1.0, 1e-3);
    set_blood(&health, 0.85f - 0.1225f); /* 65 %: in danger. */
    CHECK_NEAR(health_vigor(&health), 0.6875, 1e-3);
    CHECK(health_in_danger(&health));
    CHECK_NEAR(health_ability(&health, HEALTH_WALKING), 0.6875, 1e-3);
    CHECK_NEAR(health_ability(&health, HEALTH_HANDS), 0.6875, 1e-3);
    CHECK_NEAR(health_limb_ability(&health, BODY_LIMB_ARMS), 0.6875, 1e-3);
    set_blood(&health, 0.85f - 0.0525f); /* 85 %. */
    CHECK(!health_in_danger(&health));
    Health dead = human();
    health_hurt(&dead, HEAD, INJURY_CUT, 100.0f);
    CHECK_NEAR(health_vigor(&dead), 0.0, 1e-9);
    CHECK(!health_in_danger(&dead));
}

static void test_pain(void)
{
    Health health = human();
    health_hurt(&health, LEFT_ARM, INJURY_BRUISE, 10.0f); /* 8 points of pain over 50. */
    CHECK_NEAR(health_pain(&health), 8.0 / HEALTH_PAIN_FULL, 1e-5);
    /* Pain takes consciousness; a little of it slows nothing. */
    CHECK_NEAR(health_consciousness_loss(&health, CONSCIOUSNESS_PAIN), HEALTH_PAIN_LOSS * 0.16,
               1e-5);
    CHECK_NEAR(health_ability(&health, HEALTH_WALKING), 1.0, 1e-5);
    /* Down from about 80 %: 8 + 12 + 22 = 42 points of pain. */
    health_hurt(&health, RIGHT_ARM, INJURY_BRUISE, 15.0f);
    health_hurt(&health, LEFT_LEG, INJURY_BRUISE, 27.5f);
    CHECK_NEAR(health_pain(&health), 0.84, 1e-5);
    CHECK(health_is_collapsed(&health));
    CHECK(!health_is_unconscious(&health));
    CHECK_NEAR(health_ability(&health, HEALTH_HANDS), 0.0, 1e-9);
    /* Full pain does not knock out by itself... */
    health_hurt(&health, TORSO, INJURY_BRUISE, 20.0f);
    CHECK_NEAR(health_pain(&health), 1.0, 1e-5);
    CHECK(!health_is_unconscious(&health));
    /* ...nor kill, whatever comes with it. */
    health_strain(&health, DEPRIVATION_HUNGER, 1.0f, 90.0f);
    CHECK(!health_is_dead(&health));
    CHECK(health_is_unconscious(&health));
    CHECK_NEAR(health_consciousness(&health), HEALTH_CONSCIOUS_FLOOR, 1e-6);

    /* Hard to pain: the same blows hurt half as much. */
    Health hardy;
    health_init(&hardy, BODY_HUMAN, 1.0f, 1.0f, 0.5f);
    health_hurt(&hardy, LEFT_ARM, INJURY_BRUISE, 10.0f);
    health_hurt(&hardy, RIGHT_ARM, INJURY_BRUISE, 15.0f);
    health_hurt(&hardy, LEFT_LEG, INJURY_BRUISE, 25.0f);
    CHECK_NEAR(health_pain(&hardy), 0.4, 1e-5);
    CHECK(!health_is_unconscious(&hardy));
}

static void test_legs_and_walking(void)
{
    Health health;
    health_init(&health, BODY_HUMAN, 1.0f, 1.0f, 0.25f); /* Little pain. */
    health_hurt(&health, LEFT_LEG, INJURY_CRUSH, 25.0f); /* Destroyed. */
    /* Half the legs; too little pain to slow anything else. */
    CHECK(health_consciousness(&health) > HEALTH_VIGOR_FULL);
    CHECK_NEAR(health_ability(&health, HEALTH_WALKING), 0.5, 1e-5);
    CHECK(!health_is_down(&health));
    health_hurt(&health, RIGHT_LEG, INJURY_FRACTURE, 1.0f);
    /* Half a leg's share at most, from the fractured one: down. */
    CHECK(health_ability(&health, HEALTH_WALKING) < HEALTH_WALKING_MIN);
    CHECK(health_is_down(&health));
    CHECK(!health_is_collapsed(&health));
    CHECK_STR_EQ(health_status_key(&health), "health.status.cannot_walk");
    CHECK(health_ability(&health, HEALTH_HANDS) > 0.8f);
}

/* A fracture heals slowly, without bleeding. */
static void test_fracture_heals_slowly(void)
{
    Health health = human();
    health_hurt(&health, RIGHT_ARM, INJURY_FRACTURE, 15.0f);
    CHECK_NEAR(health_bleeding(&health), 0.0, 1e-9);
    run_hours(&health, 24.0f * 10.0f);
    CHECK_EQ_INT(health.injury_count, 1); /* 0.75 point a day. */
    CHECK_NEAR(health.injuries[0].damage, 15.0 - 0.75 * 10.0, 0.05);
    run_hours(&health, 24.0f * 11.0f);
    CHECK_EQ_INT(health.injury_count, 0);
    CHECK_NEAR(health_part_efficiency(&health, RIGHT_ARM), 1.0, 1e-6);
}

/* A hare is hurt as a human is, in proportion to its size. */
static void test_size(void)
{
    Health hare;
    health_init(&hare, BODY_HARE, 0.15f, 1.0f, 1.0f);
    CHECK_NEAR(health_part_hit_points(&hare, 1), 35.0 * 0.15, 1e-5);
    float severity = HEALTH_SERIOUS_SEVERITY + 0.01f; /* Serious on both. */
    health_hurt(&hare, 1, INJURY_CUT, severity * health_part_hit_points(&hare, 1));
    CHECK(health_injury_serious(&hare, &hare.injuries[0]));
    Health man = human();
    health_hurt(&man, TORSO, INJURY_CUT, severity * 40.0f);
    CHECK_NEAR(health_bleeding(&hare) / health_bleeding(&man), 35.0 / 40.0, 1e-4);
    CHECK_NEAR(health_pain(&hare) / health_pain(&man), 35.0 / 40.0, 1e-4);
}

/* A full list: blows add to the largest injury of the same kind there. */
static void test_full_injury_list(void)
{
    Health health = human();
    for (int i = 0; i < HEALTH_MAX_INJURIES; i++) {
        health_hurt(&health, i % 2 ? LEFT_ARM : RIGHT_ARM, INJURY_BRUISE, 0.5f + 0.01f * (float)i);
    }
    CHECK_EQ_INT(health.injury_count, HEALTH_MAX_INJURIES);
    health_hurt(&health, LEFT_ARM, INJURY_BRUISE, 1.0f);
    CHECK_EQ_INT(health.injury_count, HEALTH_MAX_INJURIES);
    CHECK_NEAR(health.injuries[HEALTH_MAX_INJURIES - 1].damage, 0.65 + 1.0, 1e-5);
    /* Nothing on the part: the smallest injury makes room. */
    health_hurt(&health, HEAD, INJURY_CUT, 2.0f);
    CHECK_NEAR(health_part_damage(&health, HEAD), 2.0, 1e-6);
    CHECK_NEAR(health_part_damage(&health, LEFT_ARM), 8 * 0.5 + 0.64 + 1.0, 1e-4);
    CHECK_NEAR(health_part_damage(&health, RIGHT_ARM), 8 * 0.5 + 0.56 - 0.5, 1e-4); /* 0.5 gone. */
}

static void test_random_part(void)
{
    Health health = human();
    Rng rng;
    rng_seed(&rng, 7, 1);
    int hits[6] = {0};
    for (int i = 0; i < 10000; i++) {
        int part = health_random_part(&health, &rng);
        REQUIRE(part >= 0 && part < 6);
        hits[part]++;
    }
    CHECK(hits[TORSO] > 3800 && hits[TORSO] < 4600);
    CHECK(hits[HEAD] > 600 && hits[HEAD] < 1000);
    CHECK(hits[LEFT_LEG] > 1000 && hits[LEFT_LEG] < 1500);
}

/* Falls: a level bruises; further, a leg breaks too, and more bruises. */
static void test_falls(void)
{
    Rng rng;
    rng_seed(&rng, 3, 1);
    for (int trial = 0; trial < 20; trial++) {
        Health health = human();
        health_fall(&health, &rng, 1);
        CHECK_EQ_INT(health.injury_count, 1);
        CHECK_EQ_INT(health.injuries[0].type, INJURY_BRUISE);
        CHECK(health.injuries[0].damage >= HEALTH_FALL_BRUISE_MIN &&
              health.injuries[0].damage <= HEALTH_FALL_BRUISE_MAX);

        Health far = human();
        health_fall(&far, &rng, 2);
        int fractures = 0;
        for (int i = 0; i < far.injury_count; i++) {
            if (far.injuries[i].type == INJURY_FRACTURE) {
                fractures++;
                CHECK(far.injuries[i].part == LEFT_LEG || far.injuries[i].part == RIGHT_LEG);
            }
        }
        CHECK_EQ_INT(fractures, 1);
        CHECK(health_ability(&far, HEALTH_WALKING) < 0.6f);
    }
    /* A hare lands lighter. */
    Health hare;
    health_init(&hare, BODY_HARE, 0.15f, 1.0f, 1.0f);
    health_fall(&hare, &rng, 1);
    CHECK(hare.injuries[0].damage <= 0.15f * HEALTH_FALL_BRUISE_MAX + 1e-5f);
    Health none = human();
    health_fall(&none, &rng, 0);
    CHECK(!health_is_hurt(&none));
}

/* ---- In the game ---------------------------------------------------------- */

/* 48 x 48 bare soil, open air over it, nobody, at noon. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 5;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = map_level_tile_count(map); i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_OPEN;
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map);
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

/* An ordinary settler with nothing to do and every need satisfied. */
static Colonist *spawn_settler(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, 0));
    if (colonist == NULL) {
        return NULL;
    }
    make_ordinary(colonist);
    for (int work = 0; work < WORK_TYPE_COUNT; work++) {
        colonist_set_work_priority(colonist, (WorkType)work, WORK_PRIORITY_DISABLED);
    }
    for (int need = 0; need < NEED_COUNT; need++) {
        colonist->needs[need] = 1.0f;
    }
    return colonist;
}

/* How many notices of a kind the game has pushed since `from`. */
static int notices_of(const Game *game, NoticeType type, uint32_t from)
{
    int count = 0;
    for (uint32_t number = from; number < game->notices.count; number++) {
        const Notice *notice = notices_get(&game->notices, number);
        count += notice != NULL && notice->type == type;
    }
    return count;
}

/* Lowered consciousness slows the settler's walk and work, its learning, and
 * lowers the quality of what it makes, by its vigor. */
static void test_consciousness_slows_the_settler(void)
{
    Colonist healthy, dazed;
    colonist_init(&healthy, "Ada", 30, 0, 0, 0);
    colonist_init(&dazed, "Bea", 30, 0, 0, 0);
    healthy.knowledge[KNOWLEDGE_FIRE] = dazed.knowledge[KNOWLEDGE_FIRE] = 40.0;
    dazed.health.blood = 0.85f - 0.1225f; /* 65 % consciousness. */
    health_update(&dazed.health, 1e-6f);
    float vigor = health_vigor(&dazed.health);
    CHECK_NEAR(vigor, 0.6875, 1e-3);
    CHECK_NEAR(colonist_walk_factor(&dazed), colonist_walk_factor(&healthy) * vigor, 1e-4);
    CHECK_NEAR(colonist_work_speed(&dazed, KNOWLEDGE_FIRE),
               colonist_work_speed(&healthy, KNOWLEDGE_FIRE) * vigor, 1e-4);
    double offset = colonist_work_level(&healthy, KNOWLEDGE_FIRE) - 40.0; /* The mood's. */
    CHECK_NEAR(colonist_work_level(&dazed, KNOWLEDGE_FIRE), 40.0 * vigor + offset, 1e-3);
    double quick =
        colonist_learn(&healthy, KNOWLEDGE_FIRE, KNOWLEDGE_SOURCE_DOING, 1.0, 1.0, 100.0, 0);
    double slow =
        colonist_learn(&dazed, KNOWLEDGE_FIRE, KNOWLEDGE_SOURCE_DOING, 1.0, 1.0, 100.0, 0);
    CHECK(slow > 0.0 && slow < quick * 0.8);
    colonist_free(&healthy);
    colonist_free(&dazed);
}

/* Two settlers who collapse on the same tile do not lie on top of each other:
 * the second crawls to a tile of its own. */
static void test_collapsing_on_someone_who_lies_there(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *first = spawn_settler(&game, 20, 20);
    Colonist *second = spawn_settler(&game, 22, 20);
    REQUIRE(first != NULL && second != NULL);
    first->health.blood = 0.62f;
    health_update(&first->health, 1e-6f);
    for (int tick = 0; tick < 5; tick++) {
        game_tick(&game);
    }
    REQUIRE(colonist_is_down(first));
    int x = first->x, y = first->y;
    /* The second walks over it (crossing is allowed) and falls on it. */
    second->x = x;
    second->y = y;
    second->health.blood = 0.62f;
    health_update(&second->health, 1e-6f);
    for (int tick = 0; tick < 5; tick++) {
        game_tick(&game);
    }
    CHECK(colonist_is_down(second));
    CHECK_EQ_INT(first->x, x); /* The first one has not moved. */
    CHECK_EQ_INT(first->y, y);
    CHECK(second->x != x || second->y != y);
    CHECK(abs(second->x - x) <= OCCUPANCY_CRAWL_REACH &&
          abs(second->y - y) <= OCCUPANCY_CRAWL_REACH);
    CHECK_EQ_INT(second->z, first->z);
    /* They stay apart. */
    for (int tick = 0; tick < 60; tick++) {
        game_tick(&game);
    }
    CHECK(first->x != second->x || first->y != second->y);
    game_shutdown(&game);
}

/* The player is told once each time a settler collapses. */
static void test_collapse_is_noticed(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_settler(&game, 20, 20);
    REQUIRE(colonist != NULL);
    uint32_t from = game.notices.count;
    game_tick(&game);
    CHECK_EQ_INT(notices_of(&game, NOTICE_SETTLER_COLLAPSED, from), 0);
    colonist->health.blood = 0.65f;
    health_update(&colonist->health, 1e-6f);
    for (int tick = 0; tick < 10; tick++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_DOWN);
    CHECK_STR_EQ(colonist_activity_key(colonist), "colonist.activity.collapsed");
    CHECK_EQ_INT(notices_of(&game, NOTICE_SETTLER_COLLAPSED, from), 1);
    /* Up again, then down again: told again. */
    colonist->health.blood = 0.9f;
    health_update(&colonist->health, 1e-6f);
    game_tick(&game);
    CHECK(colonist->task != TASK_DOWN);
    colonist->health.blood = 0.65f;
    health_update(&colonist->health, 1e-6f);
    game_tick(&game);
    CHECK_EQ_INT(notices_of(&game, NOTICE_SETTLER_COLLAPSED, from), 2);
    game_shutdown(&game);
}

/* Bruises all over (72 points, times 0.8, over 50: full pain, 62 % of
 * consciousness) and on the head (24 % more): knocked out, as pain alone
 * only brings one down; nothing bleeds. */
static void beat_up(Health *health)
{
    health_hurt(health, HEAD, INJURY_BRUISE, 10.0f);
    health_hurt(health, LEFT_ARM, INJURY_BRUISE, 19.0f);
    health_hurt(health, RIGHT_ARM, INJURY_BRUISE, 19.0f);
    health_hurt(health, LEFT_LEG, INJURY_BRUISE, 12.0f);
    health_hurt(health, RIGHT_LEG, INJURY_BRUISE, 12.0f);
}

static void test_toughness_and_pain_tolerance(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0, 0);
    CHECK_NEAR(colonist.health.damage_factor, 1.0, 1e-6);
    CHECK_NEAR(colonist.health.pain_factor, 1.0, 1e-6);
    Attributes attributes;
    attributes_init(&attributes);
    REQUIRE(attributes_set(&attributes, ATTRIBUTE_TOUGHNESS, ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&attributes, ATTRIBUTE_PAIN_TOLERANCE, -ATTRIBUTE_POSITION_MAX));
    colonist_set_attributes(&colonist, &attributes);
    /* Tough: half the damage; low pain tolerance: twice the pain. */
    CHECK_NEAR(colonist.health.damage_factor, 0.5, 1e-5);
    CHECK_NEAR(colonist.health.pain_factor, 2.0, 1e-5);
    CHECK_NEAR(health_hurt(&colonist.health, LEFT_ARM, INJURY_CUT, 10.0f), 5.0, 1e-5);
    CHECK_NEAR(health_pain(&colonist.health), 2.0 * 5.0 / HEALTH_PAIN_FULL, 1e-5);
    CHECK_EQ_INT(attribute_group(ATTRIBUTE_TOUGHNESS), ATTRIBUTE_GROUP_BODY);
    CHECK_EQ_INT(attribute_group(ATTRIBUTE_PAIN_TOLERANCE), ATTRIBUTE_GROUP_BODY);
    colonist_free(&colonist);
}

/* Hurt legs slow the walk, hurt arms the work and lower what it carries; a
 * healthy settler is as before. */
static void test_hurt_settler_is_slower(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0, 0);
    float walk = colonist_walk_factor(&colonist);
    float work = colonist_work_speed(&colonist, KNOWLEDGE_NONE);
    CHECK_NEAR(walk, 1.0, 1e-6);
    health_hurt(&colonist.health, LEFT_LEG, INJURY_BRUISE, 12.5f);
    float hurt_walk = colonist_walk_factor(&colonist);
    CHECK(hurt_walk < 0.8f && hurt_walk > 0.6f); /* Half a leg, and some pain. */
    float hurt_work = colonist_work_speed(&colonist, KNOWLEDGE_NONE);
    CHECK(hurt_work < work && hurt_work > 0.9f * work); /* Only the pain. */
    int logs = colonist_carry_capacity(&colonist, ITEM_WOOD);
    float limit = colonist_max_carry_weight(&colonist);
    health_hurt(&colonist.health, RIGHT_ARM, INJURY_FRACTURE, 2.0f);
    CHECK(colonist_work_speed(&colonist, KNOWLEDGE_NONE) < 0.65f * work);
    /* A broken arm: its places bear less (docs/CARRYING.md §4), the weight
     * carried without slowing down only follows the pain. */
    CHECK(colonist_max_carry_weight(&colonist) > 0.9f * limit);
    CarryBody body = colonist_carry_body(&colonist);
    CHECK(body.arm[CARRY_RIGHT] <= HEALTH_FRACTURE_EFFICIENCY + 1e-6f);
    CHECK_NEAR(body.arm[CARRY_LEFT], 1.0, 1e-6);
    CHECK(colonist_carry_capacity(&colonist, ITEM_WOOD) < logs);
    colonist_free(&colonist);
}

static void test_arm_efficiency(void)
{
    Health health;
    health_init(&health, BODY_HUMAN, 1.0f, 1.0f, 1.0f);
    CHECK_NEAR(health_arm_efficiency(&health, HEALTH_ARM_LEFT), 1.0, 1e-6);
    CHECK_NEAR(health_arm_efficiency(&health, HEALTH_ARM_RIGHT), 1.0, 1e-6);
    CHECK_NEAR(health_arm_efficiency(&health, 2), 0.0, 1e-6); /* Two arms only. */
    CHECK_NEAR(health_arm_efficiency(&health, -1), 0.0, 1e-6);
    /* A bruised arm holds less, the other is whole. */
    health_hurt(&health, LEFT_ARM, INJURY_BRUISE, 5.0f);
    CHECK_NEAR(health_arm_efficiency(&health, HEALTH_ARM_LEFT), 0.75, 1e-4);
    CHECK_NEAR(health_arm_efficiency(&health, HEALTH_ARM_RIGHT), 1.0, 1e-6);
    /* Animals have no arms. */
    Health deer;
    health_init(&deer, BODY_QUADRUPED, 1.0f, 1.0f, 1.0f);
    CHECK_NEAR(health_arm_efficiency(&deer, HEALTH_ARM_LEFT), 0.0, 1e-6);
}

/* Knocked out: it drops what it carried, lies where it is, cannot be sent
 * anywhere and sees nothing; once the pain has eased it gets up. */
static void test_settler_knocked_out_lies_and_gets_up(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    REQUIRE(game_set_fog(&game, true));
    Colonist *colonist = spawn_settler(&game, 20, 20);
    REQUIRE(colonist != NULL);
    EntityId id = game_colonist_id_at(&game, 0);
    colonist->carried = item_stack_pristine(ITEM_BRANCHES, 2);
    game_tick(&game);
    CHECK(vision_seen(&game.vision, 22, 20, 0));
    uint64_t healthy_hash = game_hash(&game);

    beat_up(&colonist->health);
    CHECK(game_hash(&game) != healthy_hash); /* The hash sees injuries. */
    CHECK(health_is_unconscious(&colonist->health));
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_DOWN);
    CHECK(colonist_is_down(colonist));
    CHECK_EQ_INT(colonist->carried.count, 0);
    ItemStack dropped = items_get(&game.items, colonist->x, colonist->y, 0);
    CHECK_EQ_INT(dropped.type, ITEM_BRANCHES);
    CHECK_EQ_INT(dropped.count, 2);
    CHECK_STR_EQ(colonist_activity_key(colonist), "colonist.activity.unconscious");
    CHECK(!vision_seen(&game.vision, 22, 20, 0));
    CHECK_EQ_INT(game_order_move(&game, &id, 1, 30, 20, 0), 0);
    int x = colonist->x, y = colonist->y;
    for (int tick = 0; tick < TICKS_PER_HOUR; tick++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_DOWN);
    CHECK_EQ_INT(colonist->x, x);
    CHECK_EQ_INT(colonist->y, y);

    /* The bruises heal by 4.5 points a day each: back to 55 % of
     * consciousness, to get up, after a day and a half. */
    int hours = 1;
    while (colonist->task == TASK_DOWN && hours < 48) {
        for (int tick = 0; tick < TICKS_PER_HOUR; tick++) {
            game_tick(&game);
        }
        /* Nobody feeds one lying down yet (care): no thirst on top here. */
        colonist->needs[NEED_FOOD] = 1.0f;
        colonist->needs[NEED_WATER] = 1.0f;
        hours++;
    }
    CHECK(colonist->task != TASK_DOWN);
    CHECK(hours > 30 && hours < 42);
    CHECK(!health_is_dead(&colonist->health));
    CHECK(health_is_hurt(&colonist->health)); /* Still healing. */
    CHECK_EQ_INT(game_order_move(&game, &id, 1, 30, 20, 0), 1);
    game_shutdown(&game);
}

/* Unable to walk but awake: it lies there, and still sees. */
static void test_settler_unable_to_walk(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    REQUIRE(game_set_fog(&game, true));
    Colonist *colonist = spawn_settler(&game, 20, 20);
    REQUIRE(colonist != NULL);
    health_hurt(&colonist->health, LEFT_LEG, INJURY_FRACTURE, 3.0f);
    health_hurt(&colonist->health, RIGHT_LEG, INJURY_FRACTURE, 3.0f);
    CHECK(!health_is_unconscious(&colonist->health));
    CHECK(health_is_down(&colonist->health));
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_DOWN);
    CHECK_STR_EQ(colonist_activity_key(colonist), "colonist.activity.cannot_walk");
    CHECK(vision_seen(&game.vision, 22, 20, 0));
    game_shutdown(&game);
}

/* Animals: hurt legs slow them, and one that cannot walk lies where it is. */
static void test_hurt_animal(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId id = spawn_ordinary_animal(&game, ANIMAL_HARE, ANIMAL_MALE, 20, 20, 0);
    Animal *hare = animals_get(&game.animals, id);
    REQUIRE(hare != NULL);
    CHECK_EQ_INT(hare->health.plan, BODY_HARE);
    CHECK_NEAR(hare->health.size, 0.15, 1e-6);
    const AnimalDef *deer = animal_def(ANIMAL_RED_DEER);
    CHECK_EQ_INT(deer->body, BODY_QUADRUPED);
    CHECK(deer->size > 1.0f);

    uint64_t before = game_hash(&game);
    /* Every leg broken (the hare's have 2.25 hit points). */
    for (int part = 0; part < body_part_count(BODY_HARE); part++) {
        if (body_part_def(BODY_HARE, part)->role == BODY_ROLE_WALK) {
            health_hurt(&hare->health, part, INJURY_FRACTURE, 0.5f);
        }
    }
    CHECK(game_hash(&game) != before);
    CHECK(health_is_down(&hare->health));
    CHECK(!health_is_unconscious(&hare->health));
    int x = hare->x, y = hare->y;
    for (int tick = 0; tick < TICKS_PER_HOUR; tick++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(hare->task, ANIMAL_TASK_DOWN);
    CHECK_EQ_INT(hare->x, x);
    CHECK_EQ_INT(hare->y, y);
    CHECK(!animal_is_moving(hare));
    CHECK_STR_EQ(animal_activity_key(hare), "animal.activity.cannot_walk");
    game_shutdown(&game);
}

static void run_ticks_flat(Game *game, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        game_tick(game);
    }
}

/* The latest notice raised, NULL if none. */
static const Notice *last_notice(const Game *game)
{
    return game->notices.count > 0 ? notices_get(&game->notices, game->notices.count - 1) : NULL;
}

/* A settler killed leaves its body where it fell; the others remember it,
 * worse for those who saw it; the colony goes on. */
static void test_settler_death(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *dying = spawn_settler(&game, 20, 20);
    Colonist *near = spawn_settler(&game, 22, 20);
    Colonist *far = spawn_settler(&game, 45, 45);
    REQUIRE(dying != NULL && near != NULL && far != NULL);
    EntityId dying_id = game_colonist_id_at(&game, 0);
    char name[COLONIST_NAME_SIZE];
    memcpy(name, dying->name, sizeof(name));
    dying->carried = item_stack_pristine(ITEM_BRANCHES, 2);

    CHECK(health_hurt(&dying->health, HEAD, INJURY_CRUSH, 30.0f) > 0.0f);
    CHECK(health_is_dead(&dying->health));
    CHECK_EQ_INT(dying->health.death, DEATH_VITAL_PART);
    game_tick(&game);
    CHECK(game_colonist(&game, dying_id) == NULL);
    CHECK_EQ_INT(game.colonist_pool.count, 2);
    ItemStack body = items_get(&game.items, 20, 20, 0);
    CHECK_EQ_INT(body.type, ITEM_SETTLER_BODY);
    CHECK_EQ_INT(body.count, 1);
    /* What it carried lies around it. */
    int branches = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            ItemStack stack = items_get(&game.items, 20 + dx, 20 + dy, 0);
            branches += stack.type == ITEM_BRANCHES ? stack.count : 0;
        }
    }
    CHECK_EQ_INT(branches, 2);
    const Notice *notice = last_notice(&game);
    REQUIRE(notice != NULL);
    CHECK_EQ_INT(notice->type, NOTICE_DEATH_INJURY);
    CHECK_STR_EQ(notice->name, name);
    CHECK(mood_has(&near->mood, THOUGHT_DEATH));
    CHECK(mood_has(&near->mood, THOUGHT_SAW_DEATH));
    CHECK(mood_has(&far->mood, THOUGHT_DEATH));
    CHECK(!mood_has(&far->mood, THOUGHT_SAW_DEATH));
    CHECK(near->mood.value < far->mood.value);
    CHECK(!game.colony_lost);
    CHECK(game.clock.speed != GAME_SPEED_PAUSED);
    game_shutdown(&game);
}

/* A serious wound bleeds a settler to death within hours. */
static void test_settler_bleeds_to_death(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *dying = spawn_settler(&game, 20, 20);
    REQUIRE(spawn_settler(&game, 30, 30) != NULL);
    REQUIRE(dying != NULL);
    EntityId id = game_colonist_id_at(&game, 0);
    health_hurt(&dying->health, TORSO, INJURY_CUT, 20.0f);
    int ticks = 0;
    while (game_colonist(&game, id) != NULL && ticks < 12 * TICKS_PER_HOUR) {
        game_tick(&game);
        ticks++;
    }
    CHECK(game_colonist(&game, id) == NULL);
    CHECK(ticks > 3 * TICKS_PER_HOUR);
    const Notice *notice = last_notice(&game);
    REQUIRE(notice != NULL);
    CHECK_EQ_INT(notice->type, NOTICE_DEATH_BLOOD_LOSS);
    game_shutdown(&game);
}

/* The last settler dead: the colony is lost, and the game pauses. */
static void test_colony_lost(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *last = spawn_settler(&game, 20, 20);
    REQUIRE(last != NULL);
    game_clock_set_speed(&game.clock, GAME_SPEED_NORMAL);
    uint64_t start = game.clock.tick;
    CHECK_EQ_INT(game_update(&game, 0.5), TICKS_PER_SECOND / 2);
    health_hurt(&last->health, TORSO, INJURY_CRUSH, 50.0f);
    uint64_t before = game_hash(&game);
    /* Two seconds' worth of ticks are due; the first one is the last. */
    CHECK_EQ_INT(game_update(&game, 2.0), 1);
    CHECK_EQ_UINT(game.clock.tick, start + TICKS_PER_SECOND / 2 + 1);
    CHECK(game.colony_lost);
    CHECK_EQ_INT(game.clock.speed, GAME_SPEED_PAUSED);
    CHECK_EQ_INT(game.colonist_pool.count, 0);
    const Notice *notice = last_notice(&game);
    REQUIRE(notice != NULL);
    CHECK_EQ_INT(notice->type, NOTICE_COLONY_LOST);
    CHECK(game_hash(&game) != before);
    CHECK_EQ_INT(game_update(&game, 1.0), 0); /* Paused. */
    game_shutdown(&game);
}

/* A dead animal leaves its carcass, and its herd follows another. */
static void test_animal_death(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId leader_id = spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 20, 20, 0);
    EntityId first_id = spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 22, 20, 0);
    EntityId second_id = spawn_ordinary_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 22, 22, 0);
    REQUIRE(animals_set_leader(&game.animals, first_id, leader_id));
    REQUIRE(animals_set_leader(&game.animals, second_id, leader_id));
    Animal *leader = animals_get(&game.animals, leader_id);
    REQUIRE(leader != NULL);
    /* The neck is vital: 20 × 1.4 hit points. */
    health_hurt(&leader->health, 1, INJURY_CUT, 30.0f);
    CHECK(health_is_dead(&leader->health));
    int x = leader->x, y = leader->y;
    game_tick(&game);
    CHECK(animals_get(&game.animals, leader_id) == NULL);
    ItemStack carcass = items_get(&game.items, x, y, 0);
    CHECK_EQ_INT(carcass.type, ITEM_RED_DEER_CARCASS);
    CHECK_EQ_INT(carcass.count, 1);
    CHECK(entity_id_equal(animals_leader(&game.animals, first_id), first_id));
    CHECK(entity_id_equal(animals_leader(&game.animals, second_id), first_id));
    game_shutdown(&game);
}

/* Wounds are noted as they are dealt, each with its cause, until taken. */
static void test_hurt_notes(void)
{
    Health health = human();
    HurtNote notes[HEALTH_NOTES_MAX];
    CHECK_EQ_INT(health_take_notes(&health, notes), 0);
    health.hurt_cause = HURT_CAVE_IN;
    health_hurt(&health, TORSO, INJURY_CRUSH, 4.0f);
    health_hurt(&health, LEFT_LEG, INJURY_BRUISE, 0.0f); /* No damage: no wound. */
    CHECK_EQ_INT(health_take_notes(&health, notes), 1);
    CHECK_EQ_INT(notes[0].part, TORSO);
    CHECK_EQ_INT(notes[0].type, INJURY_CRUSH);
    CHECK_EQ_INT(notes[0].cause, HURT_CAVE_IN);
    CHECK_EQ_INT(health_take_notes(&health, notes), 0); /* Taken. */
    /* A fall says so, and gives the previous cause back. */
    Rng rng;
    rng_seed(&rng, 8, 1);
    health.hurt_cause = HURT_OTHER;
    health_fall(&health, &rng, 2);
    int count = health_take_notes(&health, notes);
    CHECK(count >= 3); /* A fracture and a bruise for each level. */
    for (int i = 0; i < count; i++) {
        CHECK_EQ_INT(notes[i].cause, HURT_FALL);
    }
    CHECK_EQ_INT(health.hurt_cause, HURT_OTHER);
    /* More than the notes can hold are dropped, not overwritten, and the
     * count never passes the limit. */
    for (int i = 0; i < 3 * HEALTH_NOTES_MAX; i++) {
        health_hurt(&health, TORSO, INJURY_BRUISE, 0.01f);
    }
    CHECK_EQ_INT(health_take_notes(&health, NULL), HEALTH_NOTES_MAX);
}

/* A felled tree comes down on those in its way: its trunk crushes, its crown
 * bruises; beside its foot, the woodcutter is spared. */
static void test_felled_tree_hurts(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    REQUIRE(game_debug_plant_tree(&game, 20, 10, 0));
    REQUIRE(game_debug_grow_tree(&game, 20, 10, 0, true));
    TreeFall aim = tree_fall_aim(&game.map, 20, 10, 0);
    REQUIRE(aim.direction == DIRECTION_SOUTH && aim.length >= 4);
    Colonist *cutter = spawn_settler(&game, 20, 11);
    Colonist *under = spawn_settler(&game, 20, 13);
    Colonist *aside = spawn_settler(&game, 22, 13);
    EntityId hare_id =
        spawn_ordinary_animal(&game, ANIMAL_HARE, ANIMAL_MALE, 20, 10 + aim.length, 0);
    REQUIRE(cutter != NULL && under != NULL && aside != NULL);
    tree_fell(&game.map, &game.items, 20, 10, 0, (ItemStack){0});
    game_tick(&game);
    CHECK(!health_is_hurt(&cutter->health));
    CHECK(!health_is_hurt(&aside->health));
    REQUIRE(under->health.injury_count == 1);
    CHECK_EQ_INT(under->health.injuries[0].type, INJURY_CRUSH);
    /* Its journal says what happened, without an author. */
    REQUIRE(under->journal.count == 1);
    const JournalEntry *entry = journal_recent(&under->journal, 0);
    CHECK_EQ_INT(entry->event, JOURNAL_HURT);
    CHECK_EQ_INT(entry->what, HURT_TREE_TRUNK);
    CHECK_EQ_INT(entry->injury, INJURY_CRUSH);
    CHECK_EQ_INT(entry->part, under->health.injuries[0].part);
    CHECK_EQ_INT(cutter->journal.count, 0);
    /* The crown's end: bruises, for the hare a heavy blow. */
    const Animal *hare = animals_get_const(&game.animals, hare_id);
    if (hare != NULL) {
        CHECK(health_is_hurt(&hare->health));
        CHECK_EQ_INT(hare->health.injuries[0].type, INJURY_BRUISE);
    } else {
        CHECK_EQ_INT(items_get(&game.items, 20, 10 + aim.length, 0).type, ITEM_HARE_CARCASS);
    }
    game_shutdown(&game);
}

/* The debug blow hurts whoever is on the tile, and only them. */
static void test_debug_blow(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *hit = spawn_settler(&game, 20, 20);
    Colonist *missed = spawn_settler(&game, 21, 20);
    REQUIRE(hit != NULL && missed != NULL);
    REQUIRE(!entity_id_is_none(spawn_ordinary_animal(&game, ANIMAL_BOAR, ANIMAL_MALE, 20, 20, 0)));
    CHECK_EQ_INT(game_debug_hurt(&game, 20, 20, 0), 2);
    CHECK(health_is_hurt(&hit->health));
    CHECK(!health_is_hurt(&missed->health));
    CHECK_EQ_INT(game_debug_hurt(&game, 30, 30, 0), 0);
    CHECK_EQ_INT(hit->journal.count, hit->health.injury_count);
    CHECK_EQ_INT(journal_recent(&hit->journal, 0)->event, JOURNAL_HURT);
    CHECK_EQ_INT(journal_recent(&hit->journal, 0)->what, HURT_OTHER);
    CHECK_EQ_INT(missed->journal.count, 0);
    game_shutdown(&game);
}

/* ---- Deprivation ---------------------------------------------------------- */

static void test_deprivation_tables(void)
{
    for (int kind = 0; kind < DEPRIVATION_COUNT; kind++) {
        CHECK(i18n_is_valid_key(deprivation_name_key((Deprivation)kind)));
        CHECK(i18n_is_valid_key(deprivation_description_key((Deprivation)kind)));
        CHECK(deprivation_death_hours((Deprivation)kind) > 0.0f);
        CHECK(deprivation_recovery_hours((Deprivation)kind) > 0.0f);
    }
    CHECK(deprivation_name_key(DEPRIVATION_COUNT) == NULL);
    CHECK(deprivation_death_hours(DEPRIVATION_COUNT) == 0.0f);
    /* Thirst kills faster than hunger. */
    CHECK(deprivation_death_hours(DEPRIVATION_THIRST) <
          deprivation_death_hours(DEPRIVATION_HUNGER));
}

/* Starving for four days: weaker and weaker, slowly at first, collapsed after
 * two and a half days, out cold on the fourth, dead at its end. */
static void test_hunger_weakens_then_kills(void)
{
    Health health = human();
    CHECK(!health_is_hurt(&health));
    CHECK_STR_EQ(health_status_key(&health), "health.status.healthy");
    uint64_t healthy = health_hash(&health, 1);
    float walk = 1.0f;
    float hours_down = -1.0f, hours_out = -1.0f;
    bool died = false;
    for (int hour = 1; hour <= 100 && !died; hour++) {
        died = health_strain(&health, DEPRIVATION_HUNGER, 1.0f, 1.0f);
        float now = health_ability(&health, HEALTH_WALKING);
        CHECK(now <= walk + 1e-6f); /* Steadily weaker. */
        walk = now;
        if (hours_down < 0.0f && health_is_collapsed(&health)) {
            hours_down = (float)hour;
        }
        if (hours_out < 0.0f && health_is_unconscious(&health)) {
            hours_out = (float)hour;
        }
        if (hour == 24) {
            CHECK_NEAR(health_deprivation(&health, DEPRIVATION_HUNGER), 0.25, 1e-4);
            CHECK_NEAR(health_consciousness(&health), 0.875, 1e-4); /* 0.25^1.5 off. */
            CHECK(walk < 1.0f && walk > 0.95f);
            CHECK(health_is_hurt(&health));
            CHECK_STR_EQ(health_status_key(&health), "health.status.hurt");
            CHECK(health_hash(&health, 1) != healthy);
            CHECK(!health_is_down(&health));
        }
    }
    CHECK(died);
    CHECK(health_is_dead(&health));
    CHECK_EQ_INT(health.death, DEATH_STARVATION);
    CHECK(hours_down > 58.0f && hours_down < 64.0f);
    CHECK(hours_out > 77.0f && hours_out < 82.0f); /* Out cold before the end. */
    CHECK_NEAR(health_deprivation(&health, DEPRIVATION_HUNGER), 0.0, 1e-9); /* Dead: none. */
    CHECK(!health_strain(&health, DEPRIVATION_HUNGER, 1.0f, 10.0f));
}

static void test_thirst_and_the_weather_kill_too(void)
{
    static const struct {
        Deprivation kind;
        DeathCause cause;
    } CASES[] = {{DEPRIVATION_THIRST, DEATH_DEHYDRATION},
                 {DEPRIVATION_COLD, DEATH_HYPOTHERMIA},
                 {DEPRIVATION_HEAT, DEATH_HEATSTROKE}};
    for (int i = 0; i < 3; i++) {
        Health health = human();
        float hours = deprivation_death_hours(CASES[i].kind);
        CHECK(!health_strain(&health, CASES[i].kind, 1.0f, hours * 0.99f));
        CHECK(health_strain(&health, CASES[i].kind, 1.0f, hours * 0.02f));
        CHECK_EQ_INT(health.death, CASES[i].cause);
    }
    /* Half the pressure, twice the time. */
    Health half = human();
    CHECK(!health_strain(&half, DEPRIVATION_COLD, 0.5f, 14.0f));
    CHECK_NEAR(health_deprivation(&half, DEPRIVATION_COLD), 0.5, 1e-5);
    CHECK(health_strain(&half, DEPRIVATION_COLD, 0.5f, 14.0f));
}

static void test_deprivation_wears_off(void)
{
    Health health = human();
    health_strain(&health, DEPRIVATION_HUNGER, 1.0f, 48.0f); /* Halfway to dead. */
    health_strain(&health, DEPRIVATION_THIRST, 1.0f, 8.0f);
    CHECK(!health_is_collapsed(&health));
    /* They add up. */
    CHECK_NEAR(health_consciousness(&health), 1.0 - pow(0.5, 1.5) - pow(0.2, 1.5), 1e-4);
    CHECK_STR_EQ(deprivation_severity_key(health_deprivation(&health, DEPRIVATION_HUNGER)),
                 "health.severity.moderate");
    CHECK_STR_EQ(deprivation_severity_key(health_deprivation(&health, DEPRIVATION_THIRST)),
                 "health.severity.mild");
    CHECK_STR_EQ(deprivation_severity_key(0.9f), "health.severity.severe");
    CHECK(deprivation_severity_key(0.0f) == NULL);
    /* Eaten and drunk: thirst goes in hours, hunger in days. */
    health_strain(&health, DEPRIVATION_THIRST, 0.0f, 10.0f);
    CHECK_NEAR(health_deprivation(&health, DEPRIVATION_THIRST), 0.0, 1e-6);
    health_strain(&health, DEPRIVATION_HUNGER, 0.0f, 24.0f);
    CHECK_NEAR(health_deprivation(&health, DEPRIVATION_HUNGER), 0.0, 1e-5);
    health_strain(&health, DEPRIVATION_HUNGER, 0.0f, 100.0f);
    CHECK_NEAR(health_deprivation(&health, DEPRIVATION_HUNGER), 0.0, 1e-9);
    CHECK(!health_is_hurt(&health));
    CHECK(health_hash(&health, 1) == health_hash(&(Health){0}, 1) || !health_is_hurt(&health));
    /* Bad inputs change nothing. */
    CHECK(!health_strain(&health, DEPRIVATION_COUNT, 1.0f, 1.0f));
    CHECK(!health_strain(&health, DEPRIVATION_HUNGER, 1.0f, 0.0f));
    CHECK_NEAR(health_deprivation(&health, DEPRIVATION_COUNT), 0.0, 1e-9);
}

/* Two deprivations kill together where neither would alone: of the worse
 * of them. */
static void test_deprivations_kill_together(void)
{
    Health health = human();
    CHECK(!health_strain(&health, DEPRIVATION_HUNGER, 1.0f, 0.8f * 96.0f));
    CHECK(health_is_collapsed(&health)); /* 0.8^1.5: 72 % off. */
    bool died = false;
    int hours = 0;
    while (!died && hours < 14) {
        died = health_strain(&health, DEPRIVATION_COLD, 1.0f, 1.0f);
        hours++;
    }
    CHECK(died);
    CHECK_EQ_INT(health.death, DEATH_STARVATION);
    CHECK(hours <= 7); /* Alone, the cold would take 14 hours. */

    /* Blood loss and a hurt head, likewise. */
    Health wounded = human();
    health_hurt(&wounded, HEAD, INJURY_BRUISE, 20.0f); /* 48 % off. */
    REQUIRE(!health_is_dead(&wounded));
    set_blood(&wounded, 0.62f); /* 66 % off. */
    CHECK(health_is_dead(&wounded));
    CHECK_EQ_INT(wounded.death, DEATH_BLOOD_LOSS);

    /* The sources' names, and which kill. */
    for (int source = 0; source < CONSCIOUSNESS_SOURCE_COUNT; source++) {
        CHECK(consciousness_source_name_key((ConsciousnessSource)source) != NULL);
    }
    CHECK(consciousness_source_name_key(CONSCIOUSNESS_SOURCE_COUNT) == NULL);
    CHECK(consciousness_source_deadly(CONSCIOUSNESS_COLD));
    CHECK(!consciousness_source_deadly(CONSCIOUSNESS_PAIN));
    CHECK(!consciousness_source_deadly(CONSCIOUSNESS_SOURCE_COUNT));
}

/* Deprivation and wounds add up: a wounded settler faints sooner. */
static void test_deprivation_with_wounds(void)
{
    Health health = human();
    health_hurt(&health, LEFT_LEG, INJURY_BRUISE, 12.5f);
    float wounded = health_ability(&health, HEALTH_WALKING);
    health_strain(&health, DEPRIVATION_HUNGER, 1.0f, 48.0f);
    CHECK(health_ability(&health, HEALTH_WALKING) < wounded * 0.75f);
    CHECK(health_ability(&health, HEALTH_HANDS) < 0.75f);
    CHECK(health_in_danger(&health));
    CHECK_STR_EQ(health_status_key(&health), "health.status.hurt");
}

/* The cold presses below freezing, the heat above 36 degrees; cold tolerance
 * and heat tolerance move the limits. */
static void test_weather_pressure(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0, 0);
    CHECK_NEAR(colonist_cold_pressure(&colonist, 10.0f), 0.0, 1e-6);
    CHECK_NEAR(colonist_cold_pressure(&colonist, 0.0f), 0.0, 1e-6);
    CHECK_NEAR(colonist_cold_pressure(&colonist, -7.5f), 0.5, 1e-5);
    CHECK_NEAR(colonist_cold_pressure(&colonist, -30.0f), 1.0, 1e-6);
    CHECK_NEAR(colonist_heat_pressure(&colonist, 30.0f), 0.0, 1e-6);
    CHECK_NEAR(colonist_heat_pressure(&colonist, 41.0f), 0.5, 1e-5);
    Attributes hardy;
    attributes_init(&hardy);
    REQUIRE(attributes_set(&hardy, ATTRIBUTE_COLD_TOLERANCE, ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&hardy, ATTRIBUTE_HEAT_TOLERANCE, -ATTRIBUTE_POSITION_MAX));
    colonist_set_attributes(&colonist, &hardy);
    CHECK_NEAR(colonist_cold_pressure(&colonist, -7.5f), 0.167, 1e-3); /* 5 degrees more bearable */
    CHECK_NEAR(colonist_heat_pressure(&colonist, 36.0f), 0.5, 1e-5);   /* 5 degrees less */
    colonist_free(&colonist);
}

/* The temperatures a settler finds pleasant and bears, moved by its
 * tolerances; how a temperature feels; the temperature it feels, colder wet;
 * an animal's ranges. */
static void test_temperature_ranges(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0, 0);
    TemperatureRanges ranges = colonist_temperature_ranges(&colonist);
    CHECK_NEAR(ranges.pleasant_low, MOOD_COLD_CELSIUS, 1e-6);
    CHECK_NEAR(ranges.pleasant_high, MOOD_HOT_CELSIUS, 1e-6);
    CHECK_NEAR(ranges.bearable_low, COLONIST_HYPOTHERMIA_CELSIUS, 1e-6);
    CHECK_NEAR(ranges.bearable_high, COLONIST_HEATSTROKE_CELSIUS, 1e-6);
    CHECK_EQ_INT(temperature_feel(&ranges, 20.0f), TEMPERATURE_PLEASANT);
    CHECK_EQ_INT(temperature_feel(&ranges, 3.0f), TEMPERATURE_COLD);
    CHECK_EQ_INT(temperature_feel(&ranges, -1.0f), TEMPERATURE_FREEZING);
    CHECK_EQ_INT(temperature_feel(&ranges, 33.0f), TEMPERATURE_HOT);
    CHECK_EQ_INT(temperature_feel(&ranges, 37.0f), TEMPERATURE_SCORCHING);
    CHECK_STR_EQ(temperature_feel_key(TEMPERATURE_COLD), "temperature.feel.cold");
    Attributes hardy;
    attributes_init(&hardy);
    REQUIRE(attributes_set(&hardy, ATTRIBUTE_COLD_TOLERANCE, ATTRIBUTE_POSITION_MAX));
    REQUIRE(attributes_set(&hardy, ATTRIBUTE_HEAT_TOLERANCE, -ATTRIBUTE_POSITION_MAX));
    colonist_set_attributes(&colonist, &hardy);
    ranges = colonist_temperature_ranges(&colonist);
    CHECK_NEAR(ranges.pleasant_low, MOOD_COLD_CELSIUS - 5.0f, 1e-5);
    CHECK_NEAR(ranges.bearable_low, COLONIST_HYPOTHERMIA_CELSIUS - 5.0f, 1e-5);
    CHECK_NEAR(ranges.pleasant_high, MOOD_HOT_CELSIUS - 5.0f, 1e-5);
    CHECK_NEAR(ranges.bearable_high, COLONIST_HEATSTROKE_CELSIUS - 5.0f, 1e-5);
    CHECK_EQ_INT(temperature_feel(&ranges, 3.0f), TEMPERATURE_PLEASANT);
    colonist_free(&colonist);

    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    REQUIRE(settler != NULL);
    float air = temperature_at(&game.temperatures, 20, 20, 0);
    CHECK_NEAR(
        colonist_felt_temperature(settler, &game.workstations, &game.map, &game.temperatures), air,
        1e-4);
    settler->wetness = 0.5f;
    CHECK_NEAR(
        colonist_felt_temperature(settler, &game.workstations, &game.map, &game.temperatures),
        air - 0.5f * COLONIST_WET_CHILL, 1e-4);
    game_shutdown(&game);

    Animal wolf = {.species = ANIMAL_WOLF};
    attributes_init(&wolf.attributes);
    ranges = animal_temperature_ranges(&wolf);
    const AnimalDef *def = animal_def(ANIMAL_WOLF);
    CHECK_NEAR(ranges.bearable_low, def->cold_below, 1e-5);
    CHECK_NEAR(ranges.bearable_high, def->heat_above, 1e-5);
    CHECK_NEAR(ranges.pleasant_low, def->cold_below + ANIMAL_SHELTER_COLD_MARGIN, 1e-5);
    CHECK_NEAR(ranges.pleasant_high, def->heat_above - ANIMAL_SHELTER_HEAT_MARGIN, 1e-5);
}

/* A settler with nothing to drink weakens, collapses and dies in under two
 * days; every need being met, nothing of the sort. */
static void test_settler_dies_of_thirst(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *thirsty = spawn_settler(&game, 20, 20);
    Colonist *fine = spawn_settler(&game, 30, 30);
    REQUIRE(thirsty != NULL && fine != NULL);
    EntityId id = game_colonist_id_at(&game, 0);
    thirsty->needs[NEED_WATER] = 0.0f;
    int tick = 0;
    float weakest_walk = 1.0f;
    bool collapsed = false;
    for (; tick < 3 * TICKS_PER_DAY && game_colonist(&game, id) != NULL; tick++) {
        thirsty->needs[NEED_WATER] = 0.0f;
        thirsty->needs[NEED_FOOD] = 1.0f;
        thirsty->needs[NEED_REST] = 1.0f;
        fine->needs[NEED_WATER] = 1.0f;
        fine->needs[NEED_FOOD] = 1.0f;
        fine->needs[NEED_REST] = 1.0f;
        game_tick(&game);
        if (game_colonist(&game, id) != NULL) {
            weakest_walk = fminf(weakest_walk, colonist_walk_factor(thirsty));
            collapsed |= thirsty->task == TASK_DOWN;
        }
    }
    CHECK(game_colonist(&game, id) == NULL);                         /* Dead... */
    CHECK(tick > 30 * TICKS_PER_HOUR && tick < 42 * TICKS_PER_HOUR); /* ...after 40 hours. */
    CHECK(weakest_walk < 0.5f);
    CHECK(collapsed);
    const Notice *notice = last_notice(&game);
    REQUIRE(notice != NULL);
    CHECK_EQ_INT(notice->type, NOTICE_DEATH_DEHYDRATION);
    CHECK_EQ_INT((int)game.items.totals[ITEM_SETTLER_BODY], 1);
    CHECK(!health_is_hurt(&fine->health)); /* Unaffected. */
    game_shutdown(&game);
}

/* Eating and drinking again undoes the weakness. */
static void test_settler_recovers_when_fed(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    REQUIRE(settler != NULL);
    health_strain(&settler->health, DEPRIVATION_THIRST, 1.0f, 20.0f); /* Half way. */
    health_strain(&settler->health, DEPRIVATION_HUNGER, 1.0f, 30.0f);
    float weak = colonist_walk_factor(settler);
    CHECK(weak < 0.75f);
    for (int tick = 0; tick < 8 * TICKS_PER_HOUR; tick++) {
        settler->needs[NEED_WATER] = 1.0f;
        settler->needs[NEED_FOOD] = 1.0f;
        settler->needs[NEED_REST] = 1.0f;
        game_tick(&game);
    }
    CHECK_NEAR(health_deprivation(&settler->health, DEPRIVATION_THIRST), 0.0, 1e-5);
    CHECK(health_deprivation(&settler->health, DEPRIVATION_HUNGER) < 0.27f);
    CHECK(colonist_walk_factor(settler) > weak);
    game_shutdown(&game);
}

/* A settler freezes in the cold of a winter night with nothing over it. */
static void test_settler_freezes_in_winter(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    REQUIRE(settler != NULL);
    /* Mid-winter, before dawn, in a cold spell. */
    game.clock.tick = (uint64_t)SEASON_DAY(SEASON_WINTER, 8) * TICKS_PER_DAY + 4 * TICKS_PER_HOUR;
    game_tick(&game); /* The weather of the new day, then a cold one. */
    game_debug_cold_snap(&game);
    game_settle_climate(&game);
    run_ticks_flat(&game, TICKS_PER_HOUR);
    float cold = health_deprivation(&settler->health, DEPRIVATION_COLD);
    float felt =
        hearths_felt_temperature(&game.workstations, &game.map, &game.temperatures, 20, 20, 0);
    CHECK(felt < -3.0f);
    CHECK(cold > 0.0f);
    run_ticks_flat(&game, 2 * TICKS_PER_HOUR); /* Still before dawn's end. */
    CHECK(health_deprivation(&settler->health, DEPRIVATION_COLD) > cold);
    CHECK(health_is_hurt(&settler->health));
    /* The cold spell passes, it warms up in the day, and recovers. */
    game.weather.spell = 0.0f;
    run_ticks_flat(&game, 10 * TICKS_PER_HOUR);
    CHECK(health_deprivation(&settler->health, DEPRIVATION_COLD) < 0.01f);
    game_shutdown(&game);
}

/* Wild animals waste away without food or water. */
static void test_animals_starve(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId id = spawn_ordinary_animal(&game, ANIMAL_FOX, ANIMAL_MALE, 20, 20, 0);
    Animal *fox = animals_get(&game.animals, id);
    REQUIRE(fox != NULL);
    int tick = 0;
    for (; tick < 200 * TICKS_PER_HOUR && animals_get(&game.animals, id) != NULL; tick++) {
        fox->needs[ANIMAL_NEED_FOOD] = 0.0f;
        fox->needs[ANIMAL_NEED_WATER] = 1.0f;
        fox->needs[ANIMAL_NEED_REST] = 1.0f;
        game_tick(&game);
    }
    CHECK(animals_get(&game.animals, id) == NULL);
    CHECK(tick > 90 * TICKS_PER_HOUR && tick < 100 * TICKS_PER_HOUR);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FOX_CARCASS], 1);
    game_shutdown(&game);
}

/* ---- Animals and the weather --------------------------------------------- */

static void test_animal_weather_tolerance(void)
{
    for (int species = 0; species < ANIMAL_SPECIES_COUNT; species++) {
        const AnimalDef *def = animal_def((AnimalSpecies)species);
        Animal animal;
        memset(&animal, 0, sizeof(animal));
        animal.species = (uint8_t)species;
        attributes_init(&animal.attributes);
        CHECK(def->cold_below < 0.0f && def->heat_above > 30.0f &&
              def->cold_below < def->heat_above);
        CHECK_NEAR(animal_cold_limit(&animal), def->cold_below, 1e-6);
        CHECK_NEAR(animal_cold_pressure(&animal, 0.0f), 0.0, 1e-6);
        CHECK_NEAR(animal_heat_pressure(&animal, 20.0f), 0.0, 1e-6);
        CHECK_NEAR(animal_cold_pressure(&animal, def->cold_below - 7.5f), 0.5, 1e-5);
        CHECK_NEAR(animal_cold_pressure(&animal, -200.0f), 1.0, 1e-6);
        CHECK_NEAR(animal_heat_pressure(&animal, def->heat_above + 5.0f), 0.5, 1e-5);
        /* Hardy to the cold: five degrees later. */
        REQUIRE(
            attributes_set(&animal.attributes, ATTRIBUTE_COLD_TOLERANCE, ATTRIBUTE_POSITION_MAX));
        CHECK_NEAR(animal_cold_limit(&animal), def->cold_below - 5.0, 1e-5);
        CHECK_NEAR(animal_cold_pressure(&animal, def->cold_below - 7.5f), 1.0 / 6.0, 1e-4);
    }
    /* Wolves and foxes feel the cold later than a hare. */
    CHECK(animal_def(ANIMAL_WOLF)->cold_below < animal_def(ANIMAL_HARE)->cold_below);
    CHECK(animal_def(ANIMAL_FOX)->cold_below < animal_def(ANIMAL_ROE_DEER)->cold_below);
}

/* A game set in the cold of a winter night, in a cold snap, with the soil
 * grown as cold as the air. */
static void make_it_freezing(Game *game)
{
    game->clock.tick = (uint64_t)SEASON_DAY(SEASON_WINTER, 8) * TICKS_PER_DAY + 3 * TICKS_PER_HOUR;
    game_tick(game); /* The weather of the new day, then the spell. */
    game_debug_cold_snap(game);
    game_settle_climate(game);
    for (int tick = 0; tick < 2 * TICKS_PER_HOUR; tick++) {
        game_debug_cold_snap(game);
        game_tick(game);
    }
}

/* A deer freezes in the cold of a snap that a wolf, with its fur, does not feel. */
static void test_cold_hurts_animals_by_species(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    make_it_freezing(&game);
    EntityId deer_id = spawn_ordinary_animal(&game, ANIMAL_ROE_DEER, ANIMAL_MALE, 10, 10, 0);
    EntityId wolf_id = spawn_ordinary_animal(&game, ANIMAL_WOLF, ANIMAL_MALE, 30, 30, 0);
    Animal *deer = animals_get(&game.animals, deer_id);
    Animal *wolf = animals_get(&game.animals, wolf_id);
    REQUIRE(deer != NULL && wolf != NULL);
    float celsius = temperature_at(&game.temperatures, 10, 10, 0);
    CHECK(celsius < animal_def(ANIMAL_ROE_DEER)->cold_below);
    CHECK(celsius > animal_def(ANIMAL_WOLF)->cold_below);
    for (int tick = 0; tick < TICKS_PER_HOUR; tick++) {
        game_debug_cold_snap(&game);
        deer->needs[ANIMAL_NEED_FOOD] = deer->needs[ANIMAL_NEED_WATER] = 1.0f;
        wolf->needs[ANIMAL_NEED_FOOD] = wolf->needs[ANIMAL_NEED_WATER] = 1.0f;
        game_tick(&game);
    }
    CHECK(health_deprivation(&deer->health, DEPRIVATION_COLD) > 0.01f);
    CHECK(health_is_hurt(&deer->health));
    CHECK_NEAR(health_deprivation(&wolf->health, DEPRIVATION_COLD), 0.0, 1e-9);
    CHECK(!health_is_hurt(&wolf->health));
    game_shutdown(&game);
}

/* An animal that is cold goes to a warmer spot close by, if there is one. */
static void test_cold_animals_seek_a_warm_spot(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    make_it_freezing(&game);
    EntityId deer_id = spawn_ordinary_animal(&game, ANIMAL_ROE_DEER, ANIMAL_MALE, 20, 22, 0);
    Animal *deer = animals_get(&game.animals, deer_id);
    REQUIRE(deer != NULL);
    deer->timer = 1;
    deer->needs[ANIMAL_NEED_REST] = 1.0f;
    float here = temperature_at(&game.temperatures, 20, 22, 0);
    CHECK(here < animal_def(ANIMAL_ROE_DEER)->cold_below + ANIMAL_SHELTER_COLD_MARGIN);
    /* A warm patch 12 tiles off (a fire's reach, a cave), kept warm. */
    for (int y = 20; y <= 24; y++) {
        for (int x = 31; x <= 35; x++) {
            temperatures_add_heat(&game.temperatures, x, y, 0, 40.0f);
        }
    }
    for (int tick = 0; tick < 25 * TICKS_PER_SECOND; tick++) {
        for (int y = 20; y <= 24; y++) {
            for (int x = 31; x <= 35; x++) {
                temperatures_add_heat(&game.temperatures, x, y, 0, 0.3f);
            }
        }
        deer->needs[ANIMAL_NEED_FOOD] = deer->needs[ANIMAL_NEED_WATER] = 1.0f;
        game_tick(&game);
    }
    CHECK(deer->x >= 29);
    CHECK(temperature_at(&game.temperatures, deer->x, deer->y, 0) > here + 1.0f);
    game_shutdown(&game);
}

/* Nowhere warmer: it waits, and tries again later rather than every tick. */
static void test_cold_animals_without_shelter_stay_put(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    make_it_freezing(&game);
    EntityId deer_id = spawn_ordinary_animal(&game, ANIMAL_ROE_DEER, ANIMAL_MALE, 20, 22, 0);
    Animal *deer = animals_get(&game.animals, deer_id);
    REQUIRE(deer != NULL);
    deer->timer = 1;
    deer->needs[ANIMAL_NEED_REST] = 1.0f;
    for (int tick = 0; tick < 2 * TICKS_PER_SECOND; tick++) {
        game_tick(&game);
    }
    CHECK(deer->search_cooldown[ANIMAL_SEARCH_SHELTER] > 0);
    CHECK(deer->search_failures[ANIMAL_SEARCH_SHELTER] >= 1);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_body_plans);
    RUN_TEST(test_limbs);
    RUN_TEST(test_limb_ability);
    RUN_TEST(test_healthy_body);
    RUN_TEST(test_hurting);
    RUN_TEST(test_vital_part_kills);
    RUN_TEST(test_small_cut_heals);
    RUN_TEST(test_serious_cut_kills);
    RUN_TEST(test_blood_loss_and_consciousness);
    RUN_TEST(test_vigor);
    RUN_TEST(test_sight_reach);
    RUN_TEST(test_pain);
    RUN_TEST(test_legs_and_walking);
    RUN_TEST(test_fracture_heals_slowly);
    RUN_TEST(test_size);
    RUN_TEST(test_full_injury_list);
    RUN_TEST(test_random_part);
    RUN_TEST(test_falls);
    RUN_TEST(test_hurt_notes);
    RUN_TEST(test_toughness_and_pain_tolerance);
    RUN_TEST(test_hurt_settler_is_slower);
    RUN_TEST(test_arm_efficiency);
    RUN_TEST(test_settler_knocked_out_lies_and_gets_up);
    RUN_TEST(test_consciousness_slows_the_settler);
    RUN_TEST(test_collapsing_on_someone_who_lies_there);
    RUN_TEST(test_collapse_is_noticed);
    RUN_TEST(test_settler_unable_to_walk);
    RUN_TEST(test_hurt_animal);
    RUN_TEST(test_settler_death);
    RUN_TEST(test_settler_bleeds_to_death);
    RUN_TEST(test_colony_lost);
    RUN_TEST(test_animal_death);
    RUN_TEST(test_felled_tree_hurts);
    RUN_TEST(test_debug_blow);
    RUN_TEST(test_deprivation_tables);
    RUN_TEST(test_hunger_weakens_then_kills);
    RUN_TEST(test_thirst_and_the_weather_kill_too);
    RUN_TEST(test_deprivation_wears_off);
    RUN_TEST(test_deprivations_kill_together);
    RUN_TEST(test_deprivation_with_wounds);
    RUN_TEST(test_weather_pressure);
    RUN_TEST(test_temperature_ranges);
    RUN_TEST(test_settler_dies_of_thirst);
    RUN_TEST(test_settler_recovers_when_fed);
    RUN_TEST(test_settler_freezes_in_winter);
    RUN_TEST(test_animals_starve);
    RUN_TEST(test_animal_weather_tolerance);
    RUN_TEST(test_cold_hurts_animals_by_species);
    RUN_TEST(test_cold_animals_seek_a_warm_spot);
    RUN_TEST(test_cold_animals_without_shelter_stay_put);
    return test_summary();
}
