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

/* Blood loss makes faint, then unconscious. */
static void test_blood_loss_and_consciousness(void)
{
    Health health = human();
    health.blood = 0.8f;
    CHECK_NEAR(health_ability(&health, HEALTH_CONSCIOUSNESS), 1.0, 1e-6);
    health.blood = 0.6f;
    CHECK_NEAR(health_ability(&health, HEALTH_CONSCIOUSNESS), 0.5, 1e-5);
    CHECK(!health_is_unconscious(&health));
    health.blood = 0.55f;
    CHECK(health_is_unconscious(&health));
    CHECK(health_is_down(&health));
    CHECK_NEAR(health_ability(&health, HEALTH_WALKING), 0.0, 1e-9);
}

static void test_pain(void)
{
    Health health = human();
    health_hurt(&health, LEFT_ARM, INJURY_BRUISE, 10.0f); /* 8 points of pain over 50. */
    CHECK_NEAR(health_pain(&health), 8.0 / HEALTH_PAIN_FULL, 1e-5);
    /* Walking slowed by the pain, the legs whole. */
    CHECK_NEAR(health_ability(&health, HEALTH_WALKING), 1.0 - HEALTH_PAIN_SLOWDOWN * 0.16, 1e-5);
    /* Knocked down from 80 %: 8 + 12 + 20 = 40 points of pain. */
    health_hurt(&health, RIGHT_ARM, INJURY_BRUISE, 15.0f);
    health_hurt(&health, LEFT_LEG, INJURY_BRUISE, 25.0f);
    CHECK_NEAR(health_pain(&health), 0.8, 1e-5);
    CHECK(health_is_unconscious(&health));
    CHECK_NEAR(health_ability(&health, HEALTH_HANDS), 0.0, 1e-9);

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
    float pain = health_pain(&health);
    CHECK_NEAR(health_ability(&health, HEALTH_WALKING), 0.5 * (1.0 - HEALTH_PAIN_SLOWDOWN * pain),
               1e-5);
    CHECK(!health_is_down(&health));
    health_hurt(&health, RIGHT_LEG, INJURY_FRACTURE, 1.0f);
    /* Half a leg's share at most, from the fractured one: down. */
    CHECK(health_ability(&health, HEALTH_WALKING) < HEALTH_WALKING_MIN);
    CHECK(health_is_down(&health));
    CHECK(!health_is_unconscious(&health));
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

/* Bruises all over the limbs: pain enough to knock a settler out (62 points
 * of bruises, times 0.8, over 50: 99 %), and nothing that bleeds. */
static void beat_up(Health *health)
{
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
    /* A broken arm: it carries less, and slows down sooner. */
    CHECK(colonist_max_carry_weight(&colonist) < 0.65f * limit);
    CHECK(colonist_carry_capacity(&colonist, ITEM_WOOD) < logs);
    colonist_free(&colonist);
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

    /* The bruises heal by 4.5 points a day each: under 50 points of them
     * (pain 80 %) within a day. */
    int hours = 1;
    while (colonist->task == TASK_DOWN && hours < 30) {
        for (int tick = 0; tick < TICKS_PER_HOUR; tick++) {
            game_tick(&game);
        }
        hours++;
    }
    CHECK(colonist->task != TASK_DOWN);
    CHECK(hours > 10 && hours < 24);
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
    EntityId id = game_spawn_animal(&game, ANIMAL_HARE, ANIMAL_MALE, 20, 20, 0);
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
    EntityId leader_id = game_spawn_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 20, 20, 0);
    EntityId first_id = game_spawn_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 22, 20, 0);
    EntityId second_id = game_spawn_animal(&game, ANIMAL_RED_DEER, ANIMAL_FEMALE, 22, 22, 0);
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
    EntityId hare_id = game_spawn_animal(&game, ANIMAL_HARE, ANIMAL_MALE, 20, 10 + aim.length, 0);
    REQUIRE(cutter != NULL && under != NULL && aside != NULL);
    tree_fell(&game.map, &game.items, 20, 10, 0, (ItemStack){0});
    game_tick(&game);
    CHECK(!health_is_hurt(&cutter->health));
    CHECK(!health_is_hurt(&aside->health));
    REQUIRE(under->health.injury_count == 1);
    CHECK_EQ_INT(under->health.injuries[0].type, INJURY_CRUSH);
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
    REQUIRE(!entity_id_is_none(game_spawn_animal(&game, ANIMAL_BOAR, ANIMAL_MALE, 20, 20, 0)));
    CHECK_EQ_INT(game_debug_hurt(&game, 20, 20, 0), 2);
    CHECK(health_is_hurt(&hit->health));
    CHECK(!health_is_hurt(&missed->health));
    CHECK_EQ_INT(game_debug_hurt(&game, 30, 30, 0), 0);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_body_plans);
    RUN_TEST(test_healthy_body);
    RUN_TEST(test_hurting);
    RUN_TEST(test_vital_part_kills);
    RUN_TEST(test_small_cut_heals);
    RUN_TEST(test_serious_cut_kills);
    RUN_TEST(test_blood_loss_and_consciousness);
    RUN_TEST(test_pain);
    RUN_TEST(test_legs_and_walking);
    RUN_TEST(test_fracture_heals_slowly);
    RUN_TEST(test_size);
    RUN_TEST(test_full_injury_list);
    RUN_TEST(test_random_part);
    RUN_TEST(test_falls);
    RUN_TEST(test_toughness_and_pain_tolerance);
    RUN_TEST(test_hurt_settler_is_slower);
    RUN_TEST(test_settler_knocked_out_lies_and_gets_up);
    RUN_TEST(test_settler_unable_to_walk);
    RUN_TEST(test_hurt_animal);
    RUN_TEST(test_settler_death);
    RUN_TEST(test_settler_bleeds_to_death);
    RUN_TEST(test_colony_lost);
    RUN_TEST(test_animal_death);
    RUN_TEST(test_felled_tree_hurts);
    RUN_TEST(test_debug_blow);
    return test_summary();
}
