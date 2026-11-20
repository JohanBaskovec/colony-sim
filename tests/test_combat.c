/* Combat: attacks, blows, breaking off, and fights in a game (core/combat.h,
 * docs/COMBAT.md). */
#include "core/animal.h"
#include "core/attributes.h"
#include "core/combat.h"
#include "core/game.h"
#include "core/i18n.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <math.h>
#include <string.h>

enum { HEAD, TORSO, LEFT_ARM, RIGHT_ARM, LEFT_LEG, RIGHT_LEG };
/* The parts of a canine body (BODY_CANINE) that are not the head or torso. */
enum { FRONT_LEFT_PAW = 3, FRONT_RIGHT_PAW, HIND_LEFT_LEG, HIND_RIGHT_LEG };

static Health human(void)
{
    Health health;
    health_init(&health, BODY_HUMAN, 1.0f, 1.0f, 1.0f);
    return health;
}

/* The log of blows keeps the last COMBAT_LOG_MAX, the most recent first. */
static void test_combat_log(void)
{
    CombatLog log;
    memset(&log, 0, sizeof(log));
    CHECK(combat_log_recent(&log, 0) == NULL);
    for (int i = 0; i < COMBAT_LOG_MAX + 5; i++) {
        combat_log_push(&log, (CombatEvent){(uint64_t)i,
                                            combatant_none(),
                                            combatant_none(),
                                            {0, 0, 0},
                                            ATTACK_FISTS,
                                            i % 2 == 0});
    }
    CHECK_EQ_INT(log.count, COMBAT_LOG_MAX);
    CHECK_EQ_INT((int)combat_log_recent(&log, 0)->tick, COMBAT_LOG_MAX + 4);
    CHECK_EQ_INT((int)combat_log_recent(&log, 1)->tick, COMBAT_LOG_MAX + 3);
    CHECK_EQ_INT((int)combat_log_recent(&log, COMBAT_LOG_MAX - 1)->tick, 5);
    CHECK(combat_log_recent(&log, COMBAT_LOG_MAX) == NULL);
    CHECK(combat_log_recent(&log, -1) == NULL);
}

static void test_attack_definitions(void)
{
    CHECK(attack_def(ATTACK_NONE) == NULL);
    CHECK(attack_def(ATTACK_TYPE_COUNT) == NULL);
    CHECK_EQ_INT(attack_interval_ticks(ATTACK_NONE), 0);
    for (int type = ATTACK_NONE + 1; type < ATTACK_TYPE_COUNT; type++) {
        const AttackDef *def = attack_def((AttackType)type);
        REQUIRE(def != NULL);
        CHECK(i18n_is_valid_key(def->name_key));
        CHECK(def->damage > 0.0f);
        CHECK(def->accuracy > 0.0f && def->accuracy <= 1.0f);
        CHECK(def->interval > 0.0f);
        CHECK(def->reach >= 1);
        CHECK_EQ_INT(attack_interval_ticks((AttackType)type),
                     (int)(def->interval * TICKS_PER_SECOND + 0.5f));
        CHECK((unsigned)def->injury < INJURY_TYPE_COUNT);
        CHECK(def->limb > BODY_LIMB_NONE && def->limb < BODY_LIMB_COUNT);
    }
    /* The mark each leaves. */
    for (int type = ATTACK_NONE + 1; type < ATTACK_TYPE_COUNT; type++) {
        CHECK((unsigned)attack_def((AttackType)type)->mark < STRIKE_MARK_COUNT);
    }
    CHECK_EQ_INT(attack_def(ATTACK_WOLF_CLAWS)->mark, STRIKE_MARK_CLAWS);
    CHECK_EQ_INT(attack_def(ATTACK_FOX_CLAWS)->mark, STRIKE_MARK_CLAWS);
    CHECK_EQ_INT(attack_def(ATTACK_WOLF_BITE)->mark, STRIKE_MARK_BITE);
    CHECK_EQ_INT(attack_def(ATTACK_SPEAR_THRUST)->mark, STRIKE_MARK_CUT);
    CHECK_EQ_INT(attack_def(ATTACK_HARDENED_SPEAR_THRUST)->mark, STRIKE_MARK_CUT);
    CHECK_EQ_INT(attack_def(ATTACK_FISTS)->mark, STRIKE_MARK_BLOW);
    /* What each is made with. */
    CHECK_EQ_INT(attack_def(ATTACK_FISTS)->limb, BODY_LIMB_ARMS);
    CHECK_EQ_INT(attack_def(ATTACK_SPEAR_THRUST)->limb, BODY_LIMB_ARMS);
    CHECK_EQ_INT(attack_def(ATTACK_KICK)->limb, BODY_LIMB_HINDLEGS);
    CHECK_EQ_INT(attack_def(ATTACK_HOOF_STRIKE)->limb, BODY_LIMB_FORELEGS);
    CHECK_EQ_INT(attack_def(ATTACK_WOLF_BITE)->limb, BODY_LIMB_HEAD);
    CHECK_EQ_INT(attack_def(ATTACK_WOLF_CLAWS)->limb, BODY_LIMB_FORELEGS);
    CHECK_EQ_INT(attack_def(ATTACK_TUSKS)->limb, BODY_LIMB_HEAD);
    /* Every species' attacks are made with limbs its body has. */
    for (int species = 0; species < ANIMAL_SPECIES_COUNT; species++) {
        const AnimalDef *animal = animal_def((AnimalSpecies)species);
        for (int i = 0; i < combat_choice_count(animal->attacks); i++) {
            const AttackDef *def = attack_def((AttackType)animal->attacks[i].attack);
            REQUIRE(def != NULL);
            CHECK(body_plan_has_limb(animal->body, def->limb));
            CHECK(animal->attacks[i].weight > 0.0f);
        }
    }
    /* The wolf and the deer have two; the boar and the hare one and none. */
    CHECK_EQ_INT(combat_choice_count(animal_def(ANIMAL_WOLF)->attacks), 2);
    CHECK_EQ_INT(combat_choice_count(animal_def(ANIMAL_RED_DEER)->attacks), 2);
    CHECK_EQ_INT(combat_choice_count(animal_def(ANIMAL_BOAR)->attacks), 1);
    CHECK_EQ_INT(combat_choice_count(animal_def(ANIMAL_HARE)->attacks), 0);
    /* A hardened spear hits harder than a plain one. */
    CHECK_NEAR(attack_def(ATTACK_HARDENED_SPEAR_THRUST)->damage,
               1.2 * attack_def(ATTACK_SPEAR_THRUST)->damage, 0.3);
    CHECK(attack_def(ATTACK_FISTS)->damage < attack_def(ATTACK_SPEAR_THRUST)->damage);
}

/* A being draws its attack among those whose limb is whole enough. */
static void test_choosing_an_attack(void)
{
    const AttackChoice *wolf_attacks = animal_def(ANIMAL_WOLF)->attacks;
    Health wolf;
    health_init(&wolf, BODY_CANINE, 0.7f, 1.0f, 1.0f);
    CHECK(combat_can_attack(&wolf, wolf_attacks));
    CHECK_EQ_INT(combat_choice_reach(&wolf, wolf_attacks), 1);
    /* Without a random stream it uses its favorite. */
    float ability = 0.0f;
    CHECK_EQ_INT(combat_choose_attack(&wolf, wolf_attacks, NULL, &ability), ATTACK_WOLF_BITE);
    CHECK_NEAR(ability, 1.0, 1e-6);
    /* With one: both, the bite three times as often as the claws. */
    Rng rng;
    rng_seed(&rng, 7, 3);
    int bites = 0, claws = 0;
    for (int i = 0; i < 4000; i++) {
        AttackType type = combat_choose_attack(&wolf, wolf_attacks, &rng, NULL);
        bites += type == ATTACK_WOLF_BITE;
        claws += type == ATTACK_WOLF_CLAWS;
    }
    CHECK_EQ_INT(bites + claws, 4000);
    CHECK(claws > 800 && claws < 1200);

    /* Ruined forepaws: no more claws. */
    health_hurt(&wolf, FRONT_LEFT_PAW, INJURY_FRACTURE, 1.0f);
    health_hurt(&wolf, FRONT_RIGHT_PAW, INJURY_FRACTURE, 1.0f);
    Rng before = rng;
    for (int i = 0; i < 200; i++) {
        float bite_ability = 0.0f;
        CHECK_EQ_INT(combat_choose_attack(&wolf, wolf_attacks, &rng, &bite_ability),
                     ATTACK_WOLF_BITE);
        CHECK(bite_ability > 0.9f);
    }
    CHECK_EQ_UINT(rng.state, before.state); /* One left: nothing was drawn. */
    CHECK(combat_can_attack(&wolf, wolf_attacks));

    /* A bruised head bites with less force (dazed, too: less consciousness),
     * and a little less often. */
    Health bruised;
    health_init(&bruised, BODY_CANINE, 0.7f, 1.0f, 1.0f);
    health_hurt(&bruised, HEAD, INJURY_BRUISE, 8.0f);
    CHECK_EQ_INT(combat_choose_attack(&bruised, wolf_attacks, NULL, &ability), ATTACK_WOLF_BITE);
    CHECK(ability < 0.9f && ability > 0.15f);

    /* The deer kicks and strikes with its hooves: a deer that has lost its
     * hind legs still strikes, one with none of either does not. */
    const AttackChoice *deer_attacks = animal_def(ANIMAL_RED_DEER)->attacks;
    Health deer;
    health_init(&deer, BODY_QUADRUPED, 1.4f, 1.0f, 1.0f);
    for (int part = 5; part < 7; part++) {
        health_hurt(&deer, part, INJURY_FRACTURE, 1.0f); /* Hind legs. */
    }
    CHECK_EQ_INT(combat_choose_attack(&deer, deer_attacks, &rng, NULL), ATTACK_HOOF_STRIKE);
    for (int part = 3; part < 5; part++) {
        health_hurt(&deer, part, INJURY_FRACTURE, 1.0f); /* Front legs. */
    }
    CHECK_EQ_INT(combat_choose_attack(&deer, deer_attacks, &rng, NULL), ATTACK_NONE);
    CHECK(!combat_can_attack(&deer, deer_attacks));
    CHECK_EQ_INT(combat_choice_reach(&deer, deer_attacks), 0);

    /* A hare has nothing to strike with; nor does a dead or knocked-out wolf. */
    Health hare;
    health_init(&hare, BODY_HARE, 0.15f, 1.0f, 1.0f);
    CHECK(!combat_can_attack(&hare, animal_def(ANIMAL_HARE)->attacks));
    Health dead;
    health_init(&dead, BODY_CANINE, 0.7f, 1.0f, 1.0f);
    health_hurt(&dead, HEAD, INJURY_CUT, 100.0f);
    CHECK(!combat_can_attack(&dead, wolf_attacks));
}

static void test_reach(void)
{
    TilePos a = {10, 10, 0};
    CHECK(combat_in_reach(a, (TilePos){10, 10, 0}, 1));
    CHECK(combat_in_reach(a, (TilePos){11, 11, 0}, 1));
    CHECK(combat_in_reach(a, (TilePos){9, 10, 0}, 1));
    CHECK(!combat_in_reach(a, (TilePos){12, 10, 0}, 1));
    CHECK(combat_in_reach(a, (TilePos){12, 10, 0}, 2));
    CHECK(!combat_in_reach(a, (TilePos){10, 11, 1}, 1)); /* Another level. */
    CHECK(combat_can_strike(NULL, a, (TilePos){11, 11, 0}, 1));
    CHECK(!combat_can_strike(NULL, a, (TilePos){12, 11, 0}, 1));
}

static void test_hit_chance(void)
{
    Health body = human();
    float base = combat_hit_chance(ATTACK_SPEAR_THRUST, 1.0f, &body, 0.25f);
    CHECK_NEAR(base, 0.8 * (1.0 - 0.5 * 0.25), 1e-5);
    /* Nimbler targets are harder to hit; skill helps. */
    CHECK(combat_hit_chance(ATTACK_SPEAR_THRUST, 1.0f, &body, 0.6f) < base);
    CHECK(combat_hit_chance(ATTACK_SPEAR_THRUST, 1.3f, &body, 0.25f) > base);
    /* Always a chance, never a sure thing. */
    CHECK_NEAR(combat_hit_chance(ATTACK_FISTS, 0.01f, &body, 1.0f), COMBAT_HIT_MIN, 1e-6);
    CHECK_NEAR(combat_hit_chance(ATTACK_SPEAR_THRUST, 5.0f, &body, 0.0f), COMBAT_HIT_MAX, 1e-6);
    /* Someone who cannot walk does not dodge. */
    Health lame = human();
    health_hurt(&lame, LEFT_LEG, INJURY_FRACTURE, 3.0f);
    health_hurt(&lame, RIGHT_LEG, INJURY_FRACTURE, 3.0f);
    CHECK_NEAR(combat_dodge(&lame, 0.5f), 0.0, 1e-6);
    CHECK_NEAR(combat_hit_chance(ATTACK_SPEAR_THRUST, 1.0f, &lame, 0.5f), 0.8, 1e-5);
    CHECK_NEAR(combat_dodge(&body, 0.5f), 0.5, 1e-6);
    /* Nothing hits the dead, nor with no attack. */
    Health dead = human();
    health_hurt(&dead, HEAD, INJURY_CRUSH, 100.0f);
    REQUIRE(health_is_dead(&dead));
    CHECK_NEAR(combat_hit_chance(ATTACK_FISTS, 1.0f, &dead, 0.0f), 0.0, 1e-9);
    CHECK_NEAR(combat_hit_chance(ATTACK_NONE, 1.0f, &body, 0.0f), 0.0, 1e-9);
}

static void test_blows(void)
{
    Rng rng;
    rng_seed(&rng, 9, 4);
    int hits = 0, trials = 4000;
    float damage_min = 1000.0f, damage_max = 0.0f;
    int parts[6] = {0};
    for (int i = 0; i < trials; i++) {
        Health body = human();
        BlowResult blow = combat_blow(ATTACK_SPEAR_THRUST, 1.0f, 1.0f, &body, 0.25f, &rng);
        CHECK(!(blow.hit && blow.dodged));
        if (!blow.hit) {
            CHECK_EQ_INT(blow.part, -1);
            CHECK(!health_is_hurt(&body));
            continue;
        }
        hits++;
        CHECK(health_is_hurt(&body));
        REQUIRE(blow.part >= 0 && blow.part < 6);
        parts[blow.part]++;
        CHECK_EQ_INT(body.injuries[0].type, INJURY_CUT);
        damage_min = fminf(damage_min, blow.damage);
        damage_max = fmaxf(damage_max, blow.damage);
    }
    CHECK(hits > 0.65 * trials && hits < 0.80 * trials); /* 70 % */
    float nominal = attack_def(ATTACK_SPEAR_THRUST)->damage;
    CHECK(damage_min >= nominal * (1.0f - COMBAT_DAMAGE_SPREAD) - 1e-3f);
    CHECK(damage_max <= nominal * (1.0f + COMBAT_DAMAGE_SPREAD) + 1e-3f);
    CHECK(damage_max - damage_min > 0.3f * nominal); /* It varies. */
    CHECK(parts[TORSO] > parts[HEAD] && parts[TORSO] > parts[LEFT_ARM]);

    /* Power and the defender's toughness scale the damage. */
    float strong = 0.0f, soft = 0.0f;
    for (int i = 0; i < 200; i++) {
        Health a = human(), b;
        health_init(&b, BODY_HUMAN, 1.0f, 0.5f, 1.0f);
        strong += combat_blow(ATTACK_TUSKS, 3.0f, 2.0f, &a, 0.0f, &rng).damage;
        soft += combat_blow(ATTACK_TUSKS, 3.0f, 1.0f, &b, 0.0f, &rng).damage;
    }
    CHECK(strong > 3.0f * soft); /* ×2 power against ×0.5 toughness. */

    /* Blows keep landing until it is dead; the dead are not hit. */
    Health victim = human();
    int blows = 0;
    while (!health_is_dead(&victim) && blows < 500) {
        combat_blow(ATTACK_WOLF_BITE, 2.0f, 2.0f, &victim, 0.0f, &rng);
        blows++;
    }
    CHECK(health_is_dead(&victim));
    BlowResult after = combat_blow(ATTACK_WOLF_BITE, 2.0f, 2.0f, &victim, 0.0f, &rng);
    CHECK(!after.hit);
}

static void test_breaking_off(void)
{
    Health body = human();
    CHECK_NEAR(combat_hurt_fraction(&body), 0.0, 1e-6);
    CHECK(!combat_breaks_off(&body, 0.4f));
    /* Pain: 25 points of bruises × 0.8 over 50 = 40 %. */
    health_hurt(&body, LEFT_ARM, INJURY_BRUISE, 12.5f);
    health_hurt(&body, RIGHT_ARM, INJURY_BRUISE, 12.5f);
    CHECK_NEAR(combat_hurt_fraction(&body), 0.4, 1e-3);
    CHECK(combat_breaks_off(&body, 0.4f));
    CHECK(!combat_breaks_off(&body, 0.5f));
    CHECK(!combat_breaks_off(&body, 1.1f)); /* The bravest never. */
    /* Blood lost counts: half way to death. */
    Health bled = human();
    bled.blood = 0.75f;
    CHECK_NEAR(combat_hurt_fraction(&bled), 0.5, 1e-5);
    CHECK_NEAR(combat_hurt_fraction(&(Health){.blood = 0.0f}), 1.0, 1e-6);

    /* Courage sets the nerve: 0.4 when ordinary, 0.08 at the most fearful,
     * 1.1 at the bravest, steadily in between. */
    Attributes attributes;
    attributes_init(&attributes);
    CHECK_NEAR(attribute_nerve(&attributes), 0.4, 1e-6);
    REQUIRE(attributes_set(&attributes, ATTRIBUTE_COURAGE, -ATTRIBUTE_POSITION_MAX));
    CHECK_NEAR(attribute_nerve(&attributes), 0.08, 1e-5);
    REQUIRE(attributes_set(&attributes, ATTRIBUTE_COURAGE, ATTRIBUTE_POSITION_MAX));
    CHECK_NEAR(attribute_nerve(&attributes), 1.1, 1e-5);
    float last = -1.0f;
    for (int position = -ATTRIBUTE_POSITION_MAX; position <= ATTRIBUTE_POSITION_MAX;
         position += 100) {
        REQUIRE(attributes_set(&attributes, ATTRIBUTE_COURAGE, position));
        CHECK(attribute_nerve(&attributes) > last);
        last = attribute_nerve(&attributes);
    }
    CHECK_EQ_INT(attribute_group(ATTRIBUTE_COURAGE), ATTRIBUTE_GROUP_MIND);
}

static void test_queue(void)
{
    CombatQueue queue = {0};
    CombatStrike strike = {
        {COMBATANT_ANIMAL, {1, 1}}, {COMBATANT_SETTLER, {2, 1}}, ATTACK_FISTS, 1.0f, 1.0f};
    for (int i = 0; i < COMBAT_QUEUE_MAX; i++) {
        CHECK(combat_queue_push(&queue, strike));
    }
    CHECK(!combat_queue_push(&queue, strike));
    CHECK_EQ_INT(queue.count, COMBAT_QUEUE_MAX);
    CHECK(combatant_is_none(combatant_none()));
    CHECK(!combatant_is_none(strike.attacker));
    CHECK(combatant_equal(strike.attacker, strike.attacker));
    CHECK(!combatant_equal(strike.attacker, strike.target));
    CHECK(combatant_equal(combatant_none(), combatant_none()));
}

/* ---- In the game ---------------------------------------------------------- */

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
    map_clear_litter(map); /* What the generated trees had let fall. */
    map_topology_changed(map);
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

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

static Animal *spawn_animal(Game *game, AnimalSpecies species, int x, int y, EntityId *id)
{
    EntityId made = spawn_ordinary_animal(game, species, ANIMAL_MALE, x, y, 0);
    if (id != NULL) {
        *id = made;
    }
    Animal *animal = animals_get(&game->animals, made);
    if (animal != NULL) {
        for (int need = 0; need < ANIMAL_NEED_COUNT; need++) {
            animal->needs[need] = 1.0f;
        }
        animal->timer = 100000; /* Stands there. */
    }
    return animal;
}

static void run_ticks(Game *game, int ticks)
{
    for (int i = 0; i < ticks; i++) {
        game_tick(game);
    }
}

static void test_combatants_at(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 10, 10);
    EntityId boar_id;
    REQUIRE(spawn_animal(&game, ANIMAL_BOAR, 12, 10, &boar_id) != NULL);
    REQUIRE(settler != NULL);
    Combatant at_settler = game_combatant_at(&game, 10, 10, 0);
    CHECK_EQ_INT(at_settler.kind, COMBATANT_SETTLER);
    Combatant at_boar = game_combatant_at(&game, 12, 10, 0);
    CHECK_EQ_INT(at_boar.kind, COMBATANT_ANIMAL);
    CHECK(entity_id_equal(at_boar.id, boar_id));
    CHECK(combatant_is_none(game_combatant_at(&game, 20, 20, 0)));
    CHECK(combatant_is_none(game_combatant_at(&game, 12, 10, 1)));

    /* The debug attack asks for living, different, armed fighters. */
    CHECK(!game_debug_attack(&game, at_boar, at_boar));
    CHECK(!game_debug_attack(&game, combatant_none(), at_boar));
    CHECK(!game_debug_attack(&game, at_settler, at_settler));
    CHECK(game_debug_attack(&game, at_boar, at_settler));
    CHECK(game_debug_attack(&game, at_settler, at_boar));
    EntityId hare_id;
    REQUIRE(spawn_animal(&game, ANIMAL_HARE, 14, 10, &hare_id) != NULL);
    Combatant hare = {COMBATANT_ANIMAL, hare_id};
    CHECK(!game_debug_attack(&game, hare, at_boar)); /* A hare has no weapon. */
    CHECK(game_debug_attack(&game, at_boar, hare));
    game_shutdown(&game);
}

/* A boar goes for a hare that cannot run: it catches it, kills it, and the
 * carcass lies there. */
static void test_animal_hunts_a_hare_that_cannot_run(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId boar_id, hare_id;
    Animal *boar = spawn_animal(&game, ANIMAL_BOAR, 20, 20, &boar_id);
    Animal *hare = spawn_animal(&game, ANIMAL_HARE, 26, 20, &hare_id);
    REQUIRE(boar != NULL && hare != NULL);
    for (int part = 0; part < body_part_count(BODY_HARE); part++) {
        if (body_part_def(BODY_HARE, part)->role == BODY_ROLE_WALK) {
            health_hurt(&hare->health, part, INJURY_FRACTURE, 0.5f);
        }
    }
    REQUIRE(health_is_down(&hare->health));
    REQUIRE(game_debug_attack(&game, (Combatant){COMBATANT_ANIMAL, boar_id},
                              (Combatant){COMBATANT_ANIMAL, hare_id}));
    CHECK_EQ_INT(boar->task, ANIMAL_TASK_ATTACK);
    CHECK_STR_EQ(animal_activity_key(boar), "animal.activity.attack");
    run_ticks(&game, 3 * TICKS_PER_SECOND);
    /* The boar has run to it (6 tiles in a second or two) and bitten. */
    CHECK(abs(boar->x - 26) <= 1);
    CHECK(game.strikes_resolved > 0);
    /* Each blow is remembered for the screen: who struck whom, with what, when. */
    CHECK_EQ_INT(game.combat_log.count, (int)game.strikes_resolved);
    const CombatEvent *last = combat_log_recent(&game.combat_log, 0);
    REQUIRE(last != NULL);
    CHECK(combatant_equal(last->attacker, (Combatant){COMBATANT_ANIMAL, boar_id}));
    CHECK(combatant_equal(last->target, (Combatant){COMBATANT_ANIMAL, hare_id}));
    CHECK_EQ_INT(last->attack, ATTACK_TUSKS);
    CHECK_EQ_INT(last->target_at.x, 26);
    CHECK_EQ_INT(last->target_at.y, 20);
    CHECK(last->tick > 0 && last->tick <= game.clock.tick);
    run_ticks(&game, 20 * TICKS_PER_SECOND);
    CHECK(animals_get(&game.animals, hare_id) == NULL);
    bool carcass = false;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            carcass |= items_get(&game.items, 26 + dx, 20 + dy, 0).type == ITEM_HARE_CARCASS;
        }
    }
    CHECK(carcass);
    /* Nobody left to fight: the boar lets go. */
    CHECK(combatant_is_none(boar->enemy));
    CHECK(boar->task != ANIMAL_TASK_ATTACK);
    game_shutdown(&game);
}

/* A struck animal that does not fight back runs; one that does, fights. */
static void test_struck_animals_flee_or_fight_back(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId deer_id, boar_id, hare_id;
    Animal *deer = spawn_animal(&game, ANIMAL_RED_DEER, 10, 10, &deer_id);
    Animal *boar = spawn_animal(&game, ANIMAL_BOAR, 11, 10, &boar_id);
    Animal *hare = spawn_animal(&game, ANIMAL_HARE, 30, 30, &hare_id);
    REQUIRE(deer != NULL && boar != NULL && hare != NULL);
    Combatant by_deer = {COMBATANT_ANIMAL, deer_id};
    CHECK(animals_provoke(&game.animals, boar_id, by_deer));
    CHECK_EQ_INT(boar->task, ANIMAL_TASK_ATTACK); /* A boar fights back. */
    CHECK(combatant_equal(boar->enemy, by_deer));
    CHECK(!animals_provoke(&game.animals, boar_id, by_deer)); /* Not the first time. */
    Combatant by_boar = {COMBATANT_ANIMAL, boar_id};
    CHECK(animals_provoke(&game.animals, deer_id, by_boar));
    CHECK(deer->alarmed); /* A deer runs. */
    CHECK(deer->task != ANIMAL_TASK_ATTACK);
    CHECK(animals_provoke(&game.animals, hare_id, by_boar));
    CHECK(hare->alarmed);
    run_ticks(&game, 2 * TICKS_PER_SECOND);
    CHECK_EQ_INT(deer->task, ANIMAL_TASK_FLEE);
    /* Far from the boar now. */
    run_ticks(&game, 6 * TICKS_PER_SECOND);
    CHECK(abs(deer->x - 11) + abs(deer->y - 10) > 8);
    game_shutdown(&game);
}

/* A dazed attacker aims worse: its skill times its vigor. */
static void test_dazed_attacker_aims_worse(void)
{
    Health health;
    health_init(&health, BODY_HUMAN, 1.0f, 1.0f, 1.0f);
    CHECK_NEAR(combat_aim(1.2f, &health), 1.2, 1e-6);
    health.blood = 0.85f - 0.1225f; /* 65 % consciousness. */
    health_update(&health, 1e-6f);
    CHECK_NEAR(combat_aim(1.2f, &health), 1.2 * 0.6875, 1e-3);
    CHECK(combat_hit_chance(ATTACK_FISTS, combat_aim(1.0f, &health), &health, 0.0f) <
          combat_hit_chance(ATTACK_FISTS, 1.0f, &health, 0.0f));
}

/* A hurt animal breaks off: it runs from whom it fought. */
static void test_hurt_animal_breaks_off(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId boar_id, deer_id;
    Animal *boar = spawn_animal(&game, ANIMAL_BOAR, 20, 20, &boar_id);
    Animal *deer = spawn_animal(&game, ANIMAL_RED_DEER, 21, 20, &deer_id);
    REQUIRE(boar != NULL && deer != NULL);
    REQUIRE(game_debug_attack(&game, (Combatant){COMBATANT_ANIMAL, boar_id},
                              (Combatant){COMBATANT_ANIMAL, deer_id}));
    /* Bruised all over: past the boar's nerve (0.7), not yet down. */
    health_hurt(&boar->health, LEFT_LEG, INJURY_BRUISE, 20.0f);
    health_hurt(&boar->health, RIGHT_ARM, INJURY_BRUISE, 15.0f);
    health_hurt(&boar->health, TORSO, INJURY_BRUISE, 18.0f);
    REQUIRE(!health_is_down(&boar->health));
    REQUIRE(combat_breaks_off(&boar->health, animal_def(ANIMAL_BOAR)->nerve));
    run_ticks(&game, 2 * TICKS_PER_SECOND);
    CHECK(boar->task != ANIMAL_TASK_ATTACK);
    game_shutdown(&game);
}

/* A settler attacked by an animal is told so, fights back with its fists, and
 * the fight is resolved blow by blow. */
static void test_settler_fights_back(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    EntityId boar_id;
    Animal *boar = spawn_animal(&game, ANIMAL_BOAR, 24, 20, &boar_id);
    REQUIRE(settler != NULL && boar != NULL);
    EntityId settler_id = game_colonist_id_at(&game, 0);
    uint64_t before = game_hash(&game);
    uint32_t notices = game.notices.count;
    REQUIRE(game_debug_attack(&game, (Combatant){COMBATANT_ANIMAL, boar_id},
                              (Combatant){COMBATANT_SETTLER, settler_id}));
    bool raised = false, fought = false, struck_back = false;
    for (int tick = 0; tick < 40 * TICKS_PER_SECOND && !health_is_dead(&settler->health) &&
                       !health_is_dead(&boar->health);
         tick++) {
        game_tick(&game);
        if (game.notices.count > notices) {
            const Notice *notice = notices_get(&game.notices, notices);
            raised |= notice != NULL && notice->type == NOTICE_SETTLER_ATTACKED &&
                      strcmp(notice->name, settler->name) == 0;
        }
        fought |= settler->task == TASK_FIGHT;
        struck_back |= health_is_hurt(&boar->health);
    }
    CHECK(raised);
    CHECK(fought);
    CHECK(struck_back);
    CHECK(health_is_hurt(&settler->health));
    CHECK(game.strikes_resolved > 3);
    CHECK(game_hash(&game) != before);
    CHECK_STR_EQ(colonist_task_name_key(TASK_FIGHT), "colonist.task.fight");
    game_shutdown(&game);
}

/* A settler with a thrusting spear fights with it, better with the knowledge. */
static void test_settler_attack_by_weapon(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0, 0);
    REQUIRE(colonist_set_strength(&colonist, COLONIST_STRENGTH_STANDARD));
    float skill, power;
    CHECK_EQ_INT(colonist_attack(&colonist, &skill, &power), ATTACK_FISTS);
    CHECK_NEAR(skill, 1.0, 1e-6);
    CHECK_NEAR(power, 1.0, 1e-5); /* 0.6 + 0.04 × 10 */
    REQUIRE(colonist_set_strength(&colonist, COLONIST_STRENGTH_MAX));
    colonist_attack(&colonist, &skill, &power);
    CHECK_NEAR(power, 1.4, 1e-5);
    REQUIRE(colonist_set_strength(&colonist, COLONIST_STRENGTH_STANDARD));
    colonist.tools[0] = item_stack_pristine(ITEM_THRUSTING_SPEAR, 1);
    CHECK_EQ_INT(colonist_attack(&colonist, &skill, &power), ATTACK_SPEAR_THRUST);
    CHECK_NEAR(skill, COLONIST_SPEAR_SKILL_MIN, 1e-5);
    REQUIRE(colonist_set_knowledge_level(&colonist, KNOWLEDGE_SPEAR_HUNTING, 100.0));
    colonist_attack(&colonist, &skill, &power);
    CHECK_NEAR(skill, COLONIST_SPEAR_SKILL_MAX, 1e-5);
    colonist.tools[1] = item_stack_pristine(ITEM_THRUSTING_SPEAR_HARDENED, 1);
    CHECK_EQ_INT(colonist_attack(&colonist, &skill, &power), ATTACK_HARDENED_SPEAR_THRUST);
    /* A broken arm: weaker blows. */
    health_hurt(&colonist.health, LEFT_ARM, INJURY_FRACTURE, 2.0f);
    colonist_attack(&colonist, &skill, &power);
    CHECK(power < 0.7f && power > 0.3f);
    /* Both: it cannot strike at all, with a spear or without. */
    health_hurt(&colonist.health, RIGHT_ARM, INJURY_FRACTURE, 2.0f);
    CHECK_EQ_INT(colonist_attack(&colonist, &skill, &power), ATTACK_NONE);
    CHECK_NEAR(power, 0.0, 1e-9);
    colonist.tools[0] = item_stack_pristine(ITEM_THRUSTING_SPEAR, 1);
    CHECK_EQ_INT(colonist_attack(&colonist, &skill, &power), ATTACK_NONE);
    colonist_free(&colonist);
}

/* Any weapon it carries: the primary one, else the best; one taking both
 * hands needs both arms (docs/COMBAT.md §7). */
static void test_settler_weapon_choice(void)
{
    Colonist colonist;
    colonist_init(&colonist, "Ada", 30, 0, 0, 0);
    REQUIRE(colonist_set_strength(&colonist, COLONIST_STRENGTH_STANDARD));
    float skill, power;
    REQUIRE(colonist_add_tool(&colonist, item_stack_pristine(ITEM_COBBLE, 1)));
    CHECK_EQ_INT(colonist_attack(&colonist, &skill, &power), ATTACK_STONE);
    CHECK_NEAR(skill, 1.0, 1e-6); /* No knowledge for stones. */
    REQUIRE(colonist_add_tool(&colonist, item_stack_pristine(ITEM_HAND_AXE, 1)));
    CHECK_EQ_INT(colonist_attack(&colonist, &skill, &power), ATTACK_AXE_CHOP);
    REQUIRE(colonist_add_tool(&colonist, item_stack_pristine(ITEM_TWIGS, 1)));
    CHECK_EQ_INT(colonist_attack(&colonist, &skill, &power), ATTACK_AXE_CHOP);
    REQUIRE(colonist_add_tool(&colonist, item_stack_pristine(ITEM_THRUSTING_SPEAR, 1)));
    int spear = colonist_tool_index(&colonist, ITEM_TYPE_BIT(ITEM_THRUSTING_SPEAR));
    int cobble = colonist_tool_index(&colonist, ITEM_TYPE_BIT(ITEM_COBBLE));
    int twigs = colonist_tool_index(&colonist, ITEM_TYPE_BIT(ITEM_TWIGS));
    CHECK_EQ_INT(colonist_weapon(&colonist), spear);
    CHECK_EQ_INT(colonist_attack(&colonist, &skill, &power), ATTACK_SPEAR_THRUST);
    /* The primary weapon, even a worse one; locked on the settler. */
    CHECK(!colonist_set_primary_weapon(&colonist, twigs, true));                 /* No weapon. */
    CHECK(!colonist_set_primary_weapon(&colonist, COLONIST_TOOL_MAX - 1, true)); /* Empty. */
    CHECK_EQ_INT(colonist_primary_weapon(&colonist), -1);
    REQUIRE(colonist_set_primary_weapon(&colonist, cobble, true));
    CHECK_EQ_INT(colonist_primary_weapon(&colonist), cobble);
    CHECK_EQ_INT(colonist.tool_locks[cobble], CARRY_LOCK_KEPT);
    CHECK_EQ_INT(colonist_attack(&colonist, &skill, &power), ATTACK_STONE);
    /* One primary weapon only. */
    REQUIRE(colonist_set_primary_weapon(&colonist, spear, true));
    CHECK_EQ_INT(colonist_primary_weapon(&colonist), spear);
    /* The mark taken away: the lock stays, the best weapon again. */
    REQUIRE(colonist_set_primary_weapon(&colonist, spear, false));
    CHECK_EQ_INT(colonist_primary_weapon(&colonist), -1);
    CHECK_EQ_INT(colonist.tool_locks[spear], CARRY_LOCK_KEPT);
    /* Freeing it takes the mark away. */
    REQUIRE(colonist_set_primary_weapon(&colonist, cobble, true));
    REQUIRE(colonist_set_lock(&colonist, cobble, CARRY_LOCK_NONE));
    CHECK_EQ_INT(colonist_primary_weapon(&colonist), -1);
    /* Gone: no longer primary, and a new item in its slot is not. */
    REQUIRE(colonist_set_primary_weapon(&colonist, cobble, true));
    colonist.tools[cobble] = (ItemStack){.type = ITEM_NONE}; /* Worn out. */
    CHECK_EQ_INT(colonist_primary_weapon(&colonist), -1);
    REQUIRE(colonist_add_tool(&colonist, item_stack_pristine(ITEM_FLINT, 1)));
    CHECK_EQ_INT(colonist_tool_index(&colonist, ITEM_TYPE_BIT(ITEM_FLINT)), cobble);
    CHECK_EQ_INT(colonist_primary_weapon(&colonist), -1);
    /* One arm of no use: no thrusting spear (both hands), the hand axe. */
    health_hurt(&colonist.health, LEFT_ARM, INJURY_CUT, 30.0f);
    if (health_arm_efficiency(&colonist.health, HEALTH_ARM_LEFT) < CARRY_ARM_MIN &&
        !health_is_dead(&colonist.health)) {
        CHECK_EQ_INT(colonist_attack(&colonist, &skill, &power), ATTACK_AXE_CHOP);
        /* Even marked primary. */
        REQUIRE(colonist_set_primary_weapon(&colonist, spear, true));
        CHECK_EQ_INT(colonist_attack(&colonist, &skill, &power), ATTACK_AXE_CHOP);
    } else {
        CHECK(false); /* The cut should leave the arm of no use. */
    }
    colonist_free(&colonist);
}

/* A settler with no use of its arms cannot fight back: it runs, and does not
 * strike (the animal wins). */
static void test_armless_settler_does_not_fight(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    EntityId boar_id;
    Animal *boar = spawn_animal(&game, ANIMAL_BOAR, 23, 20, &boar_id);
    REQUIRE(settler != NULL && boar != NULL);
    health_hurt(&settler->health, LEFT_ARM, INJURY_FRACTURE, 2.0f);
    health_hurt(&settler->health, RIGHT_ARM, INJURY_FRACTURE, 2.0f);
    REQUIRE(!health_is_down(&settler->health));
    EntityId settler_id = game_colonist_id_at(&game, 0);
    REQUIRE(game_debug_attack(&game, (Combatant){COMBATANT_ANIMAL, boar_id},
                              (Combatant){COMBATANT_SETTLER, settler_id}));
    bool fought = false;
    for (int tick = 0; tick < 10 * TICKS_PER_SECOND && !health_is_dead(&settler->health); tick++) {
        game_tick(&game);
        fought |= settler->task == TASK_FIGHT;
    }
    CHECK(!fought);
    CHECK(!health_is_hurt(&boar->health)); /* It never struck back. */
    game_shutdown(&game);
}

/* A wolf bites and claws at a deer that cannot get away: both kinds of wound. */
static void test_wolf_bites_and_claws(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId wolf_id, deer_id;
    Animal *wolf = spawn_animal(&game, ANIMAL_WOLF, 20, 20, &wolf_id);
    Animal *deer = spawn_animal(&game, ANIMAL_RED_DEER, 21, 20, &deer_id);
    REQUIRE(wolf != NULL && deer != NULL);
    health_set_factors(&deer->health, 0.25f, 0.25f); /* Tough: it lasts. */
    for (int part = 3; part < 7; part++) {
        health_hurt(&deer->health, part, INJURY_FRACTURE, 0.5f); /* It cannot run. */
    }
    REQUIRE(health_is_down(&deer->health));
    REQUIRE(game_debug_attack(&game, (Combatant){COMBATANT_ANIMAL, wolf_id},
                              (Combatant){COMBATANT_ANIMAL, deer_id}));
    bool bitten = false, clawed = false;
    for (int tick = 0; tick < 120 * TICKS_PER_SECOND && !(bitten && clawed); tick++) {
        game_tick(&game);
        for (int i = 0; i < deer->health.injury_count; i++) {
            bitten |= deer->health.injuries[i].type == INJURY_BITE;
            clawed |= deer->health.injuries[i].type == INJURY_CUT;
        }
    }
    CHECK(bitten);
    CHECK(clawed);
    game_shutdown(&game);
}

/* A wolf that has lost its forepaws can only bite. */
static void test_wolf_without_forepaws_only_bites(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId wolf_id, deer_id;
    Animal *wolf = spawn_animal(&game, ANIMAL_WOLF, 20, 20, &wolf_id);
    Animal *deer = spawn_animal(&game, ANIMAL_RED_DEER, 21, 20, &deer_id);
    REQUIRE(wolf != NULL && deer != NULL);
    health_hurt(&wolf->health, FRONT_LEFT_PAW, INJURY_FRACTURE, 1.0f);
    health_hurt(&wolf->health, FRONT_RIGHT_PAW, INJURY_FRACTURE, 1.0f);
    health_set_factors(&deer->health, 0.25f, 0.25f);
    for (int part = 3; part < 7; part++) {
        health_hurt(&deer->health, part, INJURY_FRACTURE, 0.5f);
    }
    int fractures = deer->health.injury_count;
    REQUIRE(game_debug_attack(&game, (Combatant){COMBATANT_ANIMAL, wolf_id},
                              (Combatant){COMBATANT_ANIMAL, deer_id}));
    bool bitten = false, clawed = false;
    for (int tick = 0; tick < 60 * TICKS_PER_SECOND; tick++) {
        game_tick(&game);
        for (int i = fractures; i < deer->health.injury_count; i++) {
            bitten |= deer->health.injuries[i].type == INJURY_BITE;
            clawed |= deer->health.injuries[i].type == INJURY_CUT;
        }
    }
    CHECK(bitten);
    CHECK(!clawed);
    game_shutdown(&game);
}

/* A fearful settler, once hurt, runs instead; the player's order ends a fight. */
static void test_fearful_settler_flees(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    Colonist *brave = spawn_settler(&game, 20, 30);
    EntityId boar_id, other_id;
    Animal *boar = spawn_animal(&game, ANIMAL_BOAR, 23, 20, &boar_id);
    Animal *other = spawn_animal(&game, ANIMAL_BOAR, 23, 30, &other_id);
    REQUIRE(settler != NULL && brave != NULL && boar != NULL && other != NULL);
    Attributes timid = settler->attributes;
    REQUIRE(attributes_set(&timid, ATTRIBUTE_COURAGE, -ATTRIBUTE_POSITION_MAX));
    colonist_set_attributes(settler, &timid);
    Attributes bold = brave->attributes;
    REQUIRE(attributes_set(&bold, ATTRIBUTE_COURAGE, ATTRIBUTE_POSITION_MAX));
    colonist_set_attributes(brave, &bold);
    /* Both a little hurt already (pain 20 %): over the timid one's nerve. */
    for (int i = 0; i < 2; i++) {
        Colonist *who = i == 0 ? settler : brave;
        health_hurt(&who->health, LEFT_ARM, INJURY_BRUISE, 6.0f);
        health_hurt(&who->health, RIGHT_ARM, INJURY_BRUISE, 6.0f);
    }
    REQUIRE(combat_breaks_off(&settler->health, attribute_nerve(&settler->attributes)));
    REQUIRE(!combat_breaks_off(&brave->health, attribute_nerve(&brave->attributes)));
    /* Struck: the timid one runs. */
    REQUIRE(colonist_provoke(settler, (Combatant){COMBATANT_ANIMAL, boar_id}));
    REQUIRE(colonist_provoke(brave, (Combatant){COMBATANT_ANIMAL, other_id}));
    run_ticks(&game, 10);
    CHECK_EQ_INT(settler->task, TASK_FLEE);
    CHECK_STR_EQ(colonist_activity_key(settler), "colonist.task.flee");
    CHECK_EQ_INT(brave->task, TASK_FIGHT);
    int x = settler->x;
    run_ticks(&game, 2 * TICKS_PER_SECOND);
    CHECK(x - settler->x >= 3 || settler->x - x >= 3 || settler->task != TASK_FLEE);
    /* An order ends the fight. */
    EntityId brave_id = game_colonist_id_at(&game, 1);
    CHECK_EQ_INT(game_order_move(&game, &brave_id, 1, 10, 40, 0), 1);
    CHECK(combatant_is_none(brave->enemy));
    game_shutdown(&game);
}

/* Whether a notice of `type` was raised since notice number `from`. */
static bool notice_raised(const Game *game, uint32_t from, NoticeType type)
{
    for (uint32_t number = from; number < game->notices.count; number++) {
        const Notice *notice = notices_get(&game->notices, number);
        if (notice != NULL && notice->type == type) {
            return true;
        }
    }
    return false;
}

/* A mobilized settler ignores its needs and its work: it stands where it is
 * (asleep, it wakes up), its needs still falling; demobilized, it takes up
 * its life again (docs/COMBAT.md §8). */
static void test_drafted_settler_ignores_its_needs(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    REQUIRE(settler != NULL);
    EntityId id = game_colonist_id_at(&game, 0);
    settler->needs[NEED_REST] = 0.05f;
    for (int tick = 0; tick < 10 * TICKS_PER_SECOND && settler->task != TASK_SLEEP; tick++) {
        game_tick(&game);
    }
    REQUIRE(settler->task == TASK_SLEEP);
    CHECK(game_can_draft(&game, id));
    CHECK(!game_is_drafted(&game, id));
    CHECK_EQ_INT(game_set_drafted(&game, &id, 1, true), 1);
    CHECK(game_is_drafted(&game, id));
    CHECK_EQ_INT(settler->task, TASK_IDLE);                 /* Woke up. */
    CHECK_EQ_INT(game_set_drafted(&game, &id, 1, true), 0); /* Already. */
    settler->needs[NEED_FOOD] = 0.1f;
    settler->needs[NEED_WATER] = 0.1f;
    TilePos here = {(int16_t)settler->x, (int16_t)settler->y, (int16_t)settler->z};
    float rest = settler->needs[NEED_REST];
    bool stayed = true;
    for (int tick = 0; tick < 60 * TICKS_PER_SECOND; tick++) {
        game_tick(&game);
        stayed &= settler->task == TASK_IDLE && settler->x == here.x && settler->y == here.y;
    }
    CHECK(stayed);
    CHECK(settler->needs[NEED_REST] < rest);
    CHECK_STR_EQ(colonist_activity_key(settler), "colonist.activity.drafted");
    /* Sent somewhere, it stays there. */
    CHECK_EQ_INT(game_order_move(&game, &id, 1, 26, 20, 0), 1);
    CHECK_STR_EQ(colonist_activity_key(settler), "colonist.activity.drafted_moving");
    run_ticks(&game, 10 * TICKS_PER_SECOND + 2 * COLONIST_MOVE_HOLD_TICKS);
    CHECK_EQ_INT(settler->x, 26);
    CHECK_EQ_INT(settler->y, 20);
    CHECK_EQ_INT(settler->task, TASK_IDLE);
    /* Demobilized: back to its needs (sleep, first of all). */
    CHECK_EQ_INT(game_set_drafted(&game, &id, 1, false), 1);
    CHECK(!game_is_drafted(&game, id));
    run_ticks(&game, 10 * TICKS_PER_SECOND);
    CHECK(settler->task != TASK_IDLE);
    game_shutdown(&game);
}

/* Not a settler in a mental break, fleeing or down. */
static void test_who_can_be_drafted(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *sulking = spawn_settler(&game, 10, 10);
    Colonist *down = spawn_settler(&game, 14, 10);
    Colonist *running = spawn_settler(&game, 18, 10);
    Colonist *fine = spawn_settler(&game, 22, 10);
    EntityId boar_id;
    REQUIRE(spawn_animal(&game, ANIMAL_BOAR, 18, 13, &boar_id) != NULL);
    REQUIRE(sulking != NULL && down != NULL && running != NULL && fine != NULL);
    mood_start_break(&sulking->mood, MOOD_BREAK_SULK, 2 * TICKS_PER_HOUR);
    health_hurt(&down->health, LEFT_LEG, INJURY_FRACTURE, 2.0f);
    health_hurt(&down->health, RIGHT_LEG, INJURY_FRACTURE, 2.0f);
    health_hurt(&running->health, LEFT_ARM, INJURY_FRACTURE, 2.0f);
    health_hurt(&running->health, RIGHT_ARM, INJURY_FRACTURE, 2.0f);
    REQUIRE(colonist_provoke(running, (Combatant){COMBATANT_ANIMAL, boar_id}));
    run_ticks(&game, 10);
    REQUIRE(colonist_is_down(down));
    REQUIRE(running->task == TASK_FLEE);
    EntityId ids[4];
    for (int i = 0; i < 4; i++) {
        ids[i] = game_colonist_id_at(&game, (uint32_t)i);
    }
    CHECK(!game_can_draft(&game, ids[0]));
    CHECK(!game_can_draft(&game, ids[1]));
    CHECK(!game_can_draft(&game, ids[2]));
    CHECK(game_can_draft(&game, ids[3]));
    CHECK_EQ_INT(game_set_drafted(&game, ids, 4, true), 1);
    CHECK(!sulking->drafted && !down->drafted && !running->drafted);
    CHECK(fine->drafted);
    CHECK(!game_can_draft(&game, ENTITY_NONE));
    CHECK_EQ_INT(game_set_drafted(&game, ids, 4, false), 1);
    CHECK(!fine->drafted);
    game_shutdown(&game);
}

/* A mobilized settler no longer flees from fear; it is demobilized (with a
 * notice) when it breaks down, is knocked out, or flees because it cannot
 * fight any more. */
static void test_draft_ends(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *timid = spawn_settler(&game, 10, 10);
    Colonist *sulker = spawn_settler(&game, 10, 30);
    Colonist *faller = spawn_settler(&game, 30, 30);
    Colonist *armless = spawn_settler(&game, 30, 10);
    EntityId boar_id, other_id;
    REQUIRE(spawn_animal(&game, ANIMAL_BOAR, 11, 10, &boar_id) != NULL); /* In reach. */
    REQUIRE(spawn_animal(&game, ANIMAL_BOAR, 33, 10, &other_id) != NULL);
    REQUIRE(timid != NULL && sulker != NULL && faller != NULL && armless != NULL);
    EntityId ids[4];
    for (int i = 0; i < 4; i++) {
        ids[i] = game_colonist_id_at(&game, (uint32_t)i);
    }
    REQUIRE(game_set_drafted(&game, ids, 4, true) == 4);
    /* Hurt past its nerve, mobilized: it fights. */
    Attributes attributes = timid->attributes;
    REQUIRE(attributes_set(&attributes, ATTRIBUTE_COURAGE, -ATTRIBUTE_POSITION_MAX));
    colonist_set_attributes(timid, &attributes);
    health_hurt(&timid->health, LEFT_ARM, INJURY_BRUISE, 6.0f);
    health_hurt(&timid->health, RIGHT_ARM, INJURY_BRUISE, 6.0f);
    REQUIRE(combat_breaks_off(&timid->health, attribute_nerve(&timid->attributes)));
    REQUIRE(colonist_provoke(timid, (Combatant){COMBATANT_ANIMAL, boar_id}));
    uint32_t from = game.notices.count;
    run_ticks(&game, 10);
    CHECK_EQ_INT(timid->task, TASK_FIGHT);
    CHECK(timid->drafted);
    CHECK_STR_EQ(colonist_activity_key(timid), "colonist.activity.drafted_fighting");
    /* A break. */
    mood_start_break(&sulker->mood, MOOD_BREAK_SULK, 2 * TICKS_PER_HOUR);
    run_ticks(&game, 2);
    CHECK(!sulker->drafted);
    CHECK(notice_raised(&game, from, NOTICE_UNDRAFTED_BREAK));
    /* Knocked out. */
    health_hurt(&faller->health, LEFT_LEG, INJURY_FRACTURE, 2.0f);
    health_hurt(&faller->health, RIGHT_LEG, INJURY_FRACTURE, 2.0f);
    run_ticks(&game, 2);
    CHECK(!faller->drafted);
    CHECK(colonist_is_down(faller));
    CHECK(notice_raised(&game, from, NOTICE_UNDRAFTED_DOWN));
    /* No use of its arms: it runs. */
    CHECK(!notice_raised(&game, from, NOTICE_UNDRAFTED_FLED));
    health_hurt(&armless->health, LEFT_ARM, INJURY_FRACTURE, 2.0f);
    health_hurt(&armless->health, RIGHT_ARM, INJURY_FRACTURE, 2.0f);
    REQUIRE(!health_is_down(&armless->health));
    REQUIRE(colonist_provoke(armless, (Combatant){COMBATANT_ANIMAL, other_id}));
    run_ticks(&game, 10);
    CHECK_EQ_INT(armless->task, TASK_FLEE);
    CHECK(!armless->drafted);
    CHECK(notice_raised(&game, from, NOTICE_UNDRAFTED_FLED));
    CHECK_EQ_INT(armless->draft_end, DRAFT_END_NONE); /* Told, once. */
    game_shutdown(&game);
}

/* Mobilized, a settler wields its primary weapon, else its best one, after
 * drawing it; demobilized, it no longer wields it (docs/COMBAT.md §7, §8). */
static void test_drafted_settler_wields(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    REQUIRE(settler != NULL);
    EntityId id = game_colonist_id_at(&game, 0);
    REQUIRE(colonist_add_tool(settler, item_stack_pristine(ITEM_HAND_AXE, 1)));
    REQUIRE(colonist_add_tool(settler, item_stack_pristine(ITEM_THRUSTING_SPEAR, 1)));
    int axe = colonist_tool_index(settler, ITEM_TYPE_BIT(ITEM_HAND_AXE));
    int spear = colonist_tool_index(settler, ITEM_TYPE_BIT(ITEM_THRUSTING_SPEAR));
    CHECK_EQ_INT(colonist_wielded(settler), -1);
    /* Kept: a spear in both hands leaves no hand for it, but it is locked. */
    REQUIRE(colonist_set_lock(settler, axe, CARRY_LOCK_KEPT));
    REQUIRE(game_set_drafted(&game, &id, 1, true) == 1);
    CHECK(settler->wielding);
    CHECK_EQ_INT(colonist_wielded(settler), spear);              /* The best. */
    CHECK_EQ_INT(settler->attack_cooldown, COLONIST_DRAW_TICKS); /* From under an arm. */
    CarryArrangement arrangement;
    int slots[COLONIST_TOOL_MAX];
    colonist_carry_arrangement(settler, &arrangement, slots);
    CHECK_EQ_INT(slots[0], spear);
    CHECK_EQ_INT(arrangement.kept_place[0], CARRY_BOTH_HANDS);
    CHECK_EQ_INT(arrangement.hold, CARRY_HOLD_GRIP);
    CHECK_EQ_INT(colonist_hands_locked(settler), 0); /* Not the player's locks. */
    float skill, power;
    CHECK_EQ_INT(colonist_attack(settler, &skill, &power), ATTACK_SPEAR_THRUST);
    /* Demobilized: back under its arm. */
    REQUIRE(game_set_drafted(&game, &id, 1, false) == 1);
    CHECK(!settler->wielding);
    CHECK_EQ_INT(colonist_wielded(settler), -1);
    /* The primary one, drawn from the hand: no wait. */
    REQUIRE(colonist_set_primary_weapon(settler, axe, true));
    settler->attack_cooldown = 0;
    REQUIRE(game_set_drafted(&game, &id, 1, true) == 1);
    CHECK_EQ_INT(colonist_wielded(settler), axe);
    CHECK_EQ_INT(settler->attack_cooldown, 0);
    CHECK_EQ_INT(colonist_attack(settler, &skill, &power), ATTACK_AXE_CHOP);
    /* Its fists, chosen: no weapon. */
    WeaponChoice fists = {-1, ITEM_ID_NONE, ITEM_NONE};
    CHECK(game_wield(&game, id, fists));
    CHECK(settler->wielding);
    CHECK_EQ_INT(colonist_wielded(settler), -1);
    CHECK_EQ_INT(colonist_attack(settler, &skill, &power), ATTACK_FISTS);
    game_shutdown(&game);
}

/* The Change weapon list: its fists, the weapons it keeps, those lying
 * within a tile; one taken from the ground becomes a kept item, locked. */
static void test_change_weapon(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    REQUIRE(settler != NULL);
    EntityId id = game_colonist_id_at(&game, 0);
    REQUIRE(colonist_add_tool(settler, item_stack_pristine(ITEM_HAND_AXE, 1)));
    REQUIRE(colonist_add_tool(settler, item_stack_pristine(ITEM_TWIGS, 1)));
    int x = settler->x, y = settler->y;
    for (int dy = -2; dy <= 2; dy++) { /* Only what the test lays there. */
        for (int dx = -2; dx <= 2; dx++) {
            items_clear(&game.items, x + dx, y + dy, 0);
        }
    }
    REQUIRE(items_add(&game.items, x + 1, y + 1, 0, item_stack_pristine(ITEM_FLINT, 1)) == 1);
    REQUIRE(items_add(&game.items, x - 1, y, 0, item_stack_pristine(ITEM_FLINT, 2)) == 2);
    REQUIRE(items_add(&game.items, x, y + 1, 0, item_stack_pristine(ITEM_GRASS, 1)) == 1);
    REQUIRE(items_add(&game.items, x + 2, y, 0, item_stack_pristine(ITEM_COBBLE, 1)) == 1);
    REQUIRE(game_set_drafted(&game, &id, 1, true) == 1);
    WeaponChoice choices[16];
    int count = game_weapon_choices(&game, id, choices, 16);
    /* Fists, the axe, a flint (alike, the three are one line; the cobble is
     * two tiles away). */
    REQUIRE(count == 3);
    CHECK_EQ_INT(choices[0].type, ITEM_NONE);
    CHECK_EQ_INT(choices[1].type, ITEM_HAND_AXE);
    CHECK(choices[1].item.slot == UINT32_MAX);
    CHECK_EQ_INT(choices[2].type, ITEM_FLINT);
    CHECK(choices[2].item.slot != UINT32_MAX);
    CHECK_EQ_INT(game_wielded_choice(&game, id), choices[1].slot);
    /* The flint, taken up. */
    settler->attack_cooldown = 0;
    REQUIRE(game_wield(&game, id, choices[2]));
    int flint = colonist_tool_index(settler, ITEM_TYPE_BIT(ITEM_FLINT));
    REQUIRE(flint >= 0);
    CHECK_EQ_INT(colonist_wielded(settler), flint);
    CHECK_EQ_INT(settler->tool_locks[flint], CARRY_LOCK_KEPT);
    CHECK_EQ_INT(items_count_at(&game.items, x + 1, y + 1, 0, ITEM_FLINT) +
                     items_count_at(&game.items, x - 1, y, 0, ITEM_FLINT),
                 2);
    CHECK_EQ_INT(settler->attack_cooldown, COLONIST_DRAW_GROUND_TICKS);
    CHECK(!game_wield(&game, id, choices[2])); /* Gone from the ground. */
    /* Not mobilized: no choosing. */
    REQUIRE(game_set_drafted(&game, &id, 1, false) == 1);
    CHECK(!game_wield(&game, id, choices[1]));
    game_shutdown(&game);
}

/* Hands locked by other items: a two-handed weapon cannot be wielded, and
 * mobilization takes the best one that can. */
static void test_wielding_with_locked_hands(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    REQUIRE(settler != NULL);
    EntityId id = game_colonist_id_at(&game, 0);
    REQUIRE(colonist_add_tool(settler, item_stack_pristine(ITEM_THRUSTING_SPEAR, 1)));
    REQUIRE(colonist_add_tool(settler, item_stack_pristine(ITEM_HAND_AXE, 1)));
    REQUIRE(colonist_add_tool(settler, item_stack_pristine(ITEM_COBBLE, 1)));
    int spear = colonist_tool_index(settler, ITEM_TYPE_BIT(ITEM_THRUSTING_SPEAR));
    int axe = colonist_tool_index(settler, ITEM_TYPE_BIT(ITEM_HAND_AXE));
    int cobble = colonist_tool_index(settler, ITEM_TYPE_BIT(ITEM_COBBLE));
    REQUIRE(colonist_set_lock(settler, cobble, CARRY_LOCK_IN_HAND));
    CHECK(!colonist_can_wield(settler, spear));
    CHECK(colonist_can_wield(settler, axe));
    CHECK(colonist_can_wield(settler, -1));
    CHECK_EQ_INT(colonist_weapon(settler), axe);
    REQUIRE(game_set_drafted(&game, &id, 1, true) == 1);
    CHECK_EQ_INT(colonist_wielded(settler), axe);
    /* Both hands now taken: nothing more kept in hand. */
    CHECK(!colonist_can_keep(settler, ITEM_FLAKE, 1, CARRY_LOCK_IN_HAND));
    WeaponChoice choice = {spear, ITEM_ID_NONE, ITEM_THRUSTING_SPEAR};
    CHECK(!game_wield(&game, id, choice));
    /* Freed, the spear can be. */
    REQUIRE(colonist_set_lock(settler, cobble, CARRY_LOCK_NONE));
    CHECK(game_wield(&game, id, choice));
    CHECK_EQ_INT(colonist_wielded(settler), spear);
    game_shutdown(&game);
}

/* Struck, a settler draws its best weapon to defend itself, and puts it
 * back once the fight is over. */
static void test_self_defense_draws(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    EntityId boar_id;
    Animal *boar = spawn_animal(&game, ANIMAL_BOAR, 23, 20, &boar_id);
    REQUIRE(settler != NULL && boar != NULL);
    REQUIRE(colonist_add_tool(settler, item_stack_pristine(ITEM_THRUSTING_SPEAR, 1)));
    REQUIRE(colonist_provoke(settler, (Combatant){COMBATANT_ANIMAL, boar_id}));
    run_ticks(&game, 5);
    REQUIRE(settler->task == TASK_FIGHT);
    CHECK(settler->wielding);
    CHECK_EQ_INT(colonist_wielded(settler), 0);
    /* The boar dead, the fight ends. */
    health_hurt(&boar->health, HEAD, INJURY_CUT, 1000.0f);
    REQUIRE(health_is_dead(&boar->health));
    run_ticks(&game, 2 * TICKS_PER_SECOND);
    CHECK(settler->task != TASK_FIGHT);
    CHECK(!settler->wielding);
    game_shutdown(&game);
}

/* Mobilized, a settler stands its ground: it strikes an animal attacking
 * it from where it stands, without going after it, and leaves alone an
 * animal that does not attack (docs/COMBAT.md §8). */
static void test_drafted_settler_holds_its_ground(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    Colonist *guard = spawn_settler(&game, 10, 30);
    EntityId boar_id, deer_id;
    Animal *boar = spawn_animal(&game, ANIMAL_BOAR, 24, 20, &boar_id);
    Animal *deer = spawn_animal(&game, ANIMAL_RED_DEER, 11, 30, &deer_id);
    REQUIRE(settler != NULL && guard != NULL && boar != NULL && deer != NULL);
    REQUIRE(colonist_add_tool(settler, item_stack_pristine(ITEM_THRUSTING_SPEAR, 1)));
    EntityId ids[2] = {game_colonist_id_at(&game, 0), game_colonist_id_at(&game, 1)};
    REQUIRE(game_set_drafted(&game, ids, 2, true) == 2);
    health_set_factors(&boar->health, 0.25f, 0.25f); /* Tough: it lasts. */
    REQUIRE(game_debug_attack(&game, (Combatant){COMBATANT_ANIMAL, boar_id},
                              (Combatant){COMBATANT_SETTLER, ids[0]}));
    bool struck = false, stayed = true;
    for (int tick = 0; tick < 20 * TICKS_PER_SECOND && !struck; tick++) {
        game_tick(&game);
        struck |= health_is_hurt(&boar->health);
        stayed &= settler->x == 20 && settler->y == 20;
    }
    CHECK(struck);
    CHECK(stayed);
    CHECK(settler->drafted || health_is_down(&settler->health));
    /* The deer beside the guard is left alone. */
    run_ticks(&game, 5 * TICKS_PER_SECOND);
    CHECK(!health_is_hurt(&deer->health));
    CHECK(combatant_is_none(guard->enemy));
    CHECK_EQ_INT(guard->x, 10);
    game_shutdown(&game);
}

/* The Attack order: a mobilized settler goes after the animal and strikes
 * it; refused to a settler not mobilized; a move order takes it back. */
static void test_attack_order(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 10, 10);
    Colonist *idle = spawn_settler(&game, 10, 20);
    EntityId deer_id;
    Animal *deer = spawn_animal(&game, ANIMAL_RED_DEER, 16, 10, &deer_id);
    REQUIRE(settler != NULL && idle != NULL && deer != NULL);
    for (int part = 3; part < 7; part++) {
        health_hurt(&deer->health, part, INJURY_FRACTURE, 0.5f); /* It cannot run. */
    }
    int injuries = deer->health.injury_count;
    REQUIRE(colonist_add_tool(settler, item_stack_pristine(ITEM_HAND_AXE, 1)));
    EntityId id = game_colonist_id_at(&game, 0), idle_id = game_colonist_id_at(&game, 1);
    Combatant target = {COMBATANT_ANIMAL, deer_id};
    CHECK_EQ_INT(game_order_attack(&game, &idle_id, 1, target), 0); /* Not mobilized. */
    REQUIRE(game_set_drafted(&game, &id, 1, true) == 1);
    CHECK_EQ_INT(game_order_attack(&game, &id, 1, (Combatant){COMBATANT_SETTLER, idle_id}), 0);
    CHECK_EQ_INT(game_order_attack(&game, &id, 1, target), 1);
    CHECK(combatant_equal(settler->attack_target, target));
    bool struck = false;
    for (int tick = 0; tick < 20 * TICKS_PER_SECOND && !struck; tick++) {
        game_tick(&game);
        struck |= deer->health.injury_count > injuries;
    }
    CHECK(struck);
    CHECK(abs(settler->x - deer->x) <= 1 && abs(settler->y - deer->y) <= 1);
    CHECK_STR_EQ(colonist_activity_key(settler), "colonist.activity.drafted_fighting");
    /* Sent away: the order is over, it does not come back. */
    CHECK_EQ_INT(game_order_move(&game, &id, 1, 10, 10, 0), 1);
    CHECK(combatant_is_none(settler->attack_target));
    run_ticks(&game, 10 * TICKS_PER_SECOND);
    CHECK(abs(settler->x - 10) <= 1 && abs(settler->y - 10) <= 1);
    CHECK(settler->drafted);
    game_shutdown(&game);
}

/* An animal that loses its enemy (dead, or far away) is left in peace. */
static void test_enemy_forgotten(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 10, 10);
    EntityId boar_id;
    Animal *boar = spawn_animal(&game, ANIMAL_BOAR, 42, 42, &boar_id);
    REQUIRE(settler != NULL && boar != NULL);
    EntityId settler_id = game_colonist_id_at(&game, 0);
    REQUIRE(colonist_provoke(settler, (Combatant){COMBATANT_ANIMAL, boar_id}));
    run_ticks(&game, 5); /* 32 tiles away: forgotten. */
    CHECK(combatant_is_none(settler->enemy));
    CHECK(settler->task != TASK_FIGHT && settler->task != TASK_FLEE);
    /* The same for the animal against a far settler. */
    REQUIRE(game_debug_attack(&game, (Combatant){COMBATANT_ANIMAL, boar_id},
                              (Combatant){COMBATANT_SETTLER, settler_id}));
    run_ticks(&game, 5);
    CHECK(combatant_is_none(boar->enemy));
    CHECK(boar->task != ANIMAL_TASK_ATTACK);
    game_shutdown(&game);
}

/* A game where nobody fights hashes as before. */
static void test_hash_unchanged_without_fights(void)
{
    static Game a, b;
    REQUIRE(init_flat_game(&a));
    REQUIRE(init_flat_game(&b));
    REQUIRE(spawn_settler(&a, 10, 10) != NULL);
    REQUIRE(spawn_settler(&b, 10, 10) != NULL);
    REQUIRE(spawn_animal(&a, ANIMAL_BOAR, 30, 30, NULL) != NULL);
    REQUIRE(spawn_animal(&b, ANIMAL_BOAR, 30, 30, NULL) != NULL);
    run_ticks(&a, 100);
    run_ticks(&b, 100);
    CHECK(game_hash(&a) == game_hash(&b));
    CHECK_EQ_UINT(a.strikes_resolved, 0);
    game_shutdown(&a);
    game_shutdown(&b);
}

/* ---- Carnivores ----------------------------------------------------------- */

/* A hungry animal about to decide what to do. */
static Animal *spawn_hungry(Game *game, AnimalSpecies species, int x, int y, EntityId *id,
                            float food)
{
    Animal *animal = spawn_animal(game, species, x, y, id);
    if (animal != NULL) {
        animal->needs[ANIMAL_NEED_FOOD] = food;
        animal->timer = 1;
    }
    return animal;
}

/* Items of a type lying within `radius` of a tile. */
static int items_near(const Game *game, int x, int y, int radius, ItemType type)
{
    int count = 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            ItemStack stack = items_get(&game->items, x + dx, y + dy, 0);
            count += stack.type == type ? stack.count : 0;
        }
    }
    return count;
}

/* Whoever is struck notes it in its journal: by whom, with what, where. */
static void test_wounds_in_journals(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 10, 10);
    EntityId boar_id;
    Animal *boar = spawn_animal(&game, ANIMAL_BOAR, 11, 10, &boar_id);
    REQUIRE(settler != NULL && boar != NULL);
    Combatant at_settler = game_combatant_at(&game, 10, 10, 0);
    Combatant at_boar = {COMBATANT_ANIMAL, boar_id};
    CHECK(game_debug_attack(&game, at_boar, at_settler));
    const JournalEntry *wound = NULL;
    for (int t = 0; t < 60 * TICKS_PER_SECOND && wound == NULL; t++) {
        game_tick(&game);
        for (int i = 0; i < settler->journal.count && wound == NULL; i++) {
            const JournalEntry *entry = journal_recent(&settler->journal, i);
            wound = entry->event == JOURNAL_WOUND ? entry : NULL;
        }
    }
    REQUIRE(wound != NULL);
    CHECK_EQ_INT(wound->species, ANIMAL_BOAR + 1);
    CHECK_EQ_INT(wound->plan, BODY_HUMAN);
    CHECK(wound->part < body_part_count(BODY_HUMAN));
    CHECK_EQ_INT(wound->what, ATTACK_TUSKS);
    CHECK_EQ_INT(wound->injury, attack_def(ATTACK_TUSKS)->injury);
    CHECK(wound->tick >= 12 * TICKS_PER_HOUR);
    /* The settler hits back (of its own accord, or ordered): the boar's journal names who. */
    CHECK(game_debug_attack(&game, at_settler, at_boar));
    for (int t = 0; t < 60 * TICKS_PER_SECOND && boar->journal.count == 0; t++) {
        game_tick(&game);
    }
    const JournalEntry *back = NULL;
    for (int t = 0; t < 60 * TICKS_PER_SECOND && back == NULL; t++) {
        game_tick(&game);
        for (int i = 0; i < boar->journal.count && back == NULL; i++) {
            const JournalEntry *entry = journal_recent(&boar->journal, i);
            back = entry->event == JOURNAL_WOUND ? entry : NULL;
        }
    }
    REQUIRE(back != NULL);
    CHECK_EQ_INT(back->species, 0);
    CHECK(strcmp(back->other, settler->name) == 0);
    CHECK_EQ_INT(back->plan, BODY_QUADRUPED);
    /* Every blow is in both journals: given by one, taken by the other. */
    int boar_gave = 0, boar_took = 0, settler_gave = 0, settler_took = 0;
    for (int i = 0; i < boar->journal.count; i++) {
        const JournalEntry *entry = journal_recent(&boar->journal, i);
        int times = 1 + entry->repeats;
        if (entry->event == JOURNAL_STRIKE) {
            boar_gave += times;
            CHECK(strcmp(entry->other, settler->name) == 0);
            CHECK_EQ_INT(entry->species, 0);
            CHECK((unsigned)entry->outcome < JOURNAL_BLOW_COUNT);
            if (entry->outcome == JOURNAL_BLOW_WOUNDED) {
                CHECK_EQ_INT(entry->plan, BODY_HUMAN);
                CHECK_EQ_INT(entry->injury, attack_def((AttackType)entry->what)->injury);
            }
        }
        boar_took += (entry->event == JOURNAL_STRUCK || entry->event == JOURNAL_WOUND) ? times : 0;
    }
    for (int i = 0; i < settler->journal.count; i++) {
        const JournalEntry *entry = journal_recent(&settler->journal, i);
        int times = 1 + entry->repeats;
        if (entry->event == JOURNAL_STRIKE) {
            settler_gave += times;
            CHECK_EQ_INT(entry->species, ANIMAL_BOAR + 1);
        }
        if (entry->event == JOURNAL_STRUCK || entry->event == JOURNAL_WOUND) {
            settler_took += times;
            CHECK_EQ_INT(entry->species, ANIMAL_BOAR + 1);
        }
    }
    CHECK(boar_gave > 0 && settler_gave > 0);
    if (settler->journal.count < JOURNAL_MAX && boar->journal.count < JOURNAL_MAX) {
        CHECK_EQ_INT(boar_gave, settler_took);
        CHECK_EQ_INT(settler_gave, boar_took);
    }
    game_shutdown(&game);
}

/* A hungry wolf with a carcass close by walks to it and eats; the food comes
 * back a bite at a time, and the carcass shrinks. */
static void test_wolf_eats_a_carcass(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Animal *wolf = spawn_hungry(&game, ANIMAL_WOLF, 20, 20, NULL, 0.3f);
    REQUIRE(wolf != NULL);
    items_add(&game.items, 30, 20, 0, item_stack_pristine(ITEM_ROE_DEER_CARCASS, 1));
    run_ticks(&game, 6 * TICKS_PER_SECOND);
    CHECK_EQ_INT(wolf->task, ANIMAL_TASK_EAT);
    CHECK_EQ_INT(wolf->x, 30);
    float food = wolf->needs[ANIMAL_NEED_FOOD];
    run_ticks(&game, 30 * TICKS_PER_SECOND);
    CHECK(wolf->needs[ANIMAL_NEED_FOOD] > food + 0.3f);
    ItemStack left = items_get(&game.items, 30, 20, 0);
    CHECK_EQ_INT(left.type, ITEM_ROE_DEER_CARCASS);
    /* A few bites of 22 kg: it weighs less, and is no less sound. */
    CHECK_EQ_INT(left.hit_points, 100);
    CHECK(item_stack_mass(left) < 22.0f - 2.0f && item_stack_mass(left) > 22.0f * 0.4f);
    /* Full, it leaves it be. */
    run_ticks(&game, 30 * TICKS_PER_SECOND);
    CHECK(wolf->needs[ANIMAL_NEED_FOOD] >= ANIMAL_SATED - 0.15f);
    CHECK(wolf->task != ANIMAL_TASK_EAT);
    CHECK_EQ_INT(items_get(&game.items, 30, 20, 0).type, ITEM_ROE_DEER_CARCASS);
    game_shutdown(&game);
}

/* A hare is a few bites. Settlers' bodies are not on the menu. */
static void test_small_carcass_and_settler_bodies(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Animal *fox = spawn_hungry(&game, ANIMAL_FOX, 20, 20, NULL, 0.2f);
    REQUIRE(fox != NULL);
    items_add(&game.items, 24, 20, 0, item_stack_pristine(ITEM_SETTLER_BODY, 1));
    run_ticks(&game, 20 * TICKS_PER_SECOND);
    CHECK(fox->task != ANIMAL_TASK_EAT && fox->x != 24);
    items_add(&game.items, 26, 22, 0, item_stack_pristine(ITEM_HARE_CARCASS, 1));
    fox->search_cooldown[ANIMAL_SEARCH_FOOD] = 0;
    fox->timer = 1;
    fox->task = ANIMAL_TASK_IDLE;
    run_ticks(&game, 3 * 60 * TICKS_PER_SECOND);
    CHECK(fox->needs[ANIMAL_NEED_FOOD] > 0.8f);
    CHECK_EQ_INT(items_get(&game.items, 24, 20, 0).type, ITEM_SETTLER_BODY);
    /* The fox's 1 kg stomach took a share of the 3.5 kg hare; the rest lies there, sound. */
    ItemStack hare = items_get(&game.items, 26, 22, 0);
    CHECK_EQ_INT(hare.type, ITEM_HARE_CARCASS);
    CHECK(item_stack_mass(hare) < 3.5f - 0.5f && item_stack_mass(hare) > 1.5f);
    CHECK(hare.hit_points >= 95); /* Only rot wears it down. */
    game_shutdown(&game);
}

/* A hungry fox in sight of a hare goes for it, and catches it: the hare outruns
 * it at first, but tires sooner. */
static void test_fox_catches_a_hare(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId fox_id, hare_id;
    Animal *fox = spawn_hungry(&game, ANIMAL_FOX, 20, 20, &fox_id, 0.2f);
    Animal *hare = spawn_animal(&game, ANIMAL_HARE, 30, 20, &hare_id);
    REQUIRE(fox != NULL && hare != NULL);
    run_ticks(&game, 2 * TICKS_PER_SECOND);
    CHECK_EQ_INT(fox->task, ANIMAL_TASK_ATTACK);
    CHECK(combatant_equal(fox->enemy, (Combatant){COMBATANT_ANIMAL, hare_id}));
    bool fled = false, tired = false;
    for (int tick = 0; tick < 120 * TICKS_PER_SECOND && animals_get(&game.animals, hare_id) != NULL;
         tick++) {
        game_tick(&game);
        fled |= hare->task == ANIMAL_TASK_FLEE;
        tired |= hare->fatigue > 0.9f;
    }
    CHECK(fled);
    CHECK(tired);
    CHECK(animals_get(&game.animals, hare_id) == NULL);
    CHECK_EQ_INT(game_combatant_at(&game, 0, 0, 0).kind, COMBATANT_NONE);
    game_shutdown(&game);
}

/* A pack hunts together: when one goes for a deer, the others join. */
static void test_wolf_pack_hunts_together(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId lead_id, second_id, third_id, deer_id;
    Animal *lead = spawn_hungry(&game, ANIMAL_WOLF, 20, 20, &lead_id, 0.3f);
    Animal *second = spawn_animal(&game, ANIMAL_WOLF, 22, 21, &second_id);
    Animal *third = spawn_animal(&game, ANIMAL_WOLF, 21, 23, &third_id);
    Animal *deer = spawn_animal(&game, ANIMAL_ROE_DEER, 40, 20, &deer_id);
    REQUIRE(lead != NULL && second != NULL && third != NULL && deer != NULL);
    REQUIRE(animals_set_leader(&game.animals, second_id, lead_id));
    REQUIRE(animals_set_leader(&game.animals, third_id, lead_id));
    run_ticks(&game, 3);
    Combatant target = {COMBATANT_ANIMAL, deer_id};
    CHECK(combatant_equal(lead->enemy, target));
    CHECK(combatant_equal(second->enemy, target));
    CHECK(combatant_equal(third->enemy, target));
    CHECK_EQ_INT(second->task, ANIMAL_TASK_ATTACK);
    /* The deer is brought down, and eaten. */
    run_ticks(&game, 90 * TICKS_PER_SECOND);
    CHECK(animals_get(&game.animals, deer_id) == NULL);
    CHECK(!health_is_dead(&lead->health));
    CHECK_EQ_INT(items_near(&game, 30, 20, 30, ITEM_ROE_DEER_CARCASS), 1);
    run_ticks(&game, 120 * TICKS_PER_SECOND);
    CHECK(lead->needs[ANIMAL_NEED_FOOD] > 0.5f);
    game_shutdown(&game);
}

/* Prey that see a carnivore run, and their herd with them. */
static void test_prey_flee_carnivores(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId wolf_id, deer_id, mate_id;
    Animal *wolf = spawn_animal(&game, ANIMAL_WOLF, 20, 20, &wolf_id); /* Not hungry. */
    Animal *deer = spawn_animal(&game, ANIMAL_RED_DEER, 28, 20, &deer_id);
    Animal *mate = spawn_animal(&game, ANIMAL_RED_DEER, 31, 22, &mate_id);
    REQUIRE(wolf != NULL && deer != NULL && mate != NULL);
    REQUIRE(animals_set_leader(&game.animals, mate_id, deer_id));
    bool fled = false;
    for (int tick = 0; tick < 3 * TICKS_PER_SECOND; tick++) {
        game_tick(&game);
        fled |= deer->task == ANIMAL_TASK_FLEE;
    }
    CHECK(fled);
    CHECK(combatant_equal(deer->enemy, (Combatant){COMBATANT_ANIMAL, wolf_id}));
    CHECK(mate->alarmed || mate->task == ANIMAL_TASK_FLEE || mate->x > 36);
    CHECK(deer->fatigue > 0.0f);
    run_ticks(&game, 10 * TICKS_PER_SECOND);
    CHECK(deer->x > 36); /* Away from the wolf. */
    /* A carnivore fears no other carnivore. */
    EntityId fox_id;
    Animal *fox = spawn_animal(&game, ANIMAL_FOX, 20, 30, &fox_id);
    REQUIRE(fox != NULL);
    run_ticks(&game, 3 * TICKS_PER_SECOND);
    CHECK(fox->task != ANIMAL_TASK_FLEE);
    CHECK(wolf->task != ANIMAL_TASK_ATTACK); /* Not hungry: no hunt. */
    game_shutdown(&game);
}

/* A wolf past hunger goes for a settler it sees, a well fed one runs from them. */
static void test_hungry_wolves_attack_settlers(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *settler = spawn_settler(&game, 20, 20);
    REQUIRE(settler != NULL);
    EntityId settler_id = game_colonist_id_at(&game, 0);
    EntityId fed_id, starving_id;
    Animal *fed = spawn_hungry(&game, ANIMAL_WOLF, 30, 20, &fed_id, 0.9f);
    Animal *starving = spawn_hungry(&game, ANIMAL_WOLF, 20, 36, &starving_id, 0.1f);
    REQUIRE(fed != NULL && starving != NULL);
    uint32_t notices = game.notices.count;
    bool fed_fled = false, fed_hunted = false;
    for (int tick = 0; tick < 3 * TICKS_PER_SECOND; tick++) {
        game_tick(&game);
        fed_fled |= fed->task == ANIMAL_TASK_FLEE;
        fed_hunted |= fed->task == ANIMAL_TASK_ATTACK;
    }
    CHECK(combatant_equal(starving->enemy, (Combatant){COMBATANT_SETTLER, settler_id}));
    CHECK_EQ_INT(starving->task, ANIMAL_TASK_ATTACK);
    CHECK(fed_fled && !fed_hunted); /* The fed one is afraid. */
    bool settler_fought = false;
    for (int tick = 0; tick < 20 * TICKS_PER_SECOND && game_colonist(&game, settler_id) != NULL;
         tick++) {
        game_tick(&game);
        settler_fought |= settler->task == TASK_FIGHT || settler->task == TASK_FLEE;
    }
    CHECK(settler_fought);
    CHECK(game.notices.count > notices);
    CHECK(game.strikes_resolved > 0);
    game_shutdown(&game);
}

/* An animal that runs gets tired and slower, and recovers resting. */
static void test_fatigue(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    EntityId hare_id;
    Animal *hare = spawn_animal(&game, ANIMAL_HARE, 20, 20, &hare_id);
    REQUIRE(hare != NULL);
    CHECK_NEAR(hare->fatigue, 0.0, 1e-9);
    uint64_t before = game_hash(&game);
    Colonist *settler = spawn_settler(&game, 23, 20);
    REQUIRE(settler != NULL);
    float tired = 0.0f;
    for (int tick = 0; tick < 10 * TICKS_PER_SECOND; tick++) {
        game_tick(&game);
        tired = fmaxf(tired, hare->fatigue);
    }
    CHECK(tired > 0.1f); /* It ran (8 seconds flat out spend a hare). */
    CHECK(game_hash(&game) != before);
    /* The settler leaves: the hare stands, and recovers. */
    REQUIRE(game_remove_colonist(&game, game_colonist_id_at(&game, 0)));
    run_ticks(&game, 20 * TICKS_PER_SECOND);
    CHECK(hare->fatigue < tired);
    run_ticks(&game, 70 * TICKS_PER_SECOND);
    CHECK_NEAR(hare->fatigue, 0.0, 1e-6);
    game_shutdown(&game);
}

/* A miss is a dodge when the same roll would have hit a defender that did not
 * dodge: never against one that cannot, about as often as the dodge takes off. */
static void test_dodges(void)
{
    Rng rng;
    rng_seed(&rng, 3, 8);
    int trials = 20000, dodged = 0, missed = 0, hits = 0;
    for (int i = 0; i < trials; i++) {
        Health body = human();
        BlowResult still = combat_blow(ATTACK_FISTS, 1.0f, 1.0f, &body, 0.0f, &rng);
        CHECK(!still.dodged);
        Health other = human();
        BlowResult blow = combat_blow(ATTACK_FISTS, 1.0f, 1.0f, &other, 0.8f, &rng);
        hits += blow.hit;
        dodged += blow.dodged;
        missed += !blow.hit && !blow.dodged;
    }
    /* Fists: 70 % without the dodge, 70 % × (1 - 0.4) = 42 % with it. */
    float accuracy = attack_def(ATTACK_FISTS)->accuracy;
    CHECK_NEAR((double)hits / trials, accuracy * 0.6, 0.02);
    CHECK_NEAR((double)dodged / trials, accuracy * 0.4, 0.02);
    CHECK_NEAR((double)missed / trials, 1.0 - accuracy, 0.02);
}

/* A blow reaches a diagonal tile only if a step could: not across a wall's corner. */
static void test_no_strike_across_a_corner(void)
{
    static Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    TilePos from = {10, 10, 0};
    CHECK(combat_can_strike(map, from, (TilePos){11, 11, 0}, 1));
    CHECK(combat_can_strike(map, from, (TilePos){11, 10, 0}, 1));
    map_set_rock(map, 11, 10, 0, ROCK_GRANITE);
    CHECK(!combat_can_strike(map, from, (TilePos){11, 11, 0}, 1));
    CHECK(!combat_can_strike(map, (TilePos){11, 11, 0}, from, 1));
    CHECK(!combat_can_strike(map, from, (TilePos){11, 9, 0}, 1));  /* Past the rock too... */
    CHECK(combat_can_strike(map, from, (TilePos){9, 11, 0}, 1));   /* ...not this way. */
    CHECK(!combat_can_strike(map, from, (TilePos){12, 10, 0}, 1)); /* Too far anyway. */
    /* Where to stand to strike: around the enemy, not on its tile nor in rock. */
    CombatSpot spot = {{12, 12, 0}, 1};
    CHECK(combat_is_strike_spot(map, 11, 12, 0, &spot));
    CHECK(!combat_is_strike_spot(map, 12, 12, 0, &spot));
    CHECK(!combat_is_strike_spot(map, 10, 12, 0, &spot));
    map_set_rock(map, 13, 12, 0, ROCK_GRANITE);
    CHECK(!combat_is_strike_spot(map, 13, 12, 0, &spot));
    CHECK(!combat_is_strike_spot(map, 13, 13, 0, &spot)); /* Past the rock's corner. */
    TileBox box = combat_spot_box(&spot);
    CHECK(box.x0 == 11 && box.x1 == 13 && box.y0 == 11 && box.y1 == 13 && box.z0 == 0 &&
          box.z1 == 0);
    game_shutdown(&game);
}

/* Down for good: it cannot walk. */
static void knock_down(Animal *animal)
{
    BodyPlan body = (BodyPlan)animal->health.plan;
    for (int part = 0; part < body_part_count(body); part++) {
        if (body_part_def(body, part)->role == BODY_ROLE_WALK) {
            health_hurt(&animal->health, part, INJURY_FRACTURE, 0.5f);
        }
    }
}

static TilePos tile_of_animal(const Animal *animal)
{
    return (TilePos){(int16_t)animal->x, (int16_t)animal->y, (int16_t)animal->z};
}

/* Whether two of the wolves stand still on one tile, or one on its prey's. */
static bool wolves_overlap(Animal *const *wolves, int count, TilePos prey)
{
    for (int i = 0; i < count; i++) {
        const Animal *a = wolves[i];
        if (animal_is_moving(a) || a->move_progress > 0.0f) {
            continue;
        }
        if (a->x == prey.x && a->y == prey.y && a->z == prey.z) {
            return true;
        }
        for (int j = 0; j < i; j++) {
            const Animal *b = wolves[j];
            if (!animal_is_moving(b) && b->move_progress == 0.0f && a->x == b->x && a->y == b->y &&
                a->z == b->z) {
                return true;
            }
        }
    }
    return false;
}

static int wolves_beside(Animal *const *wolves, int count, TilePos prey)
{
    int beside = 0;
    for (int i = 0; i < count; i++) {
        const Animal *wolf = wolves[i];
        beside += !animal_is_moving(wolf) && wolf->move_progress == 0.0f &&
                  combat_in_reach((TilePos){(int16_t)wolf->x, (int16_t)wolf->y, (int16_t)wolf->z},
                                  prey, 1) &&
                  !(wolf->x == prey.x && wolf->y == prey.y);
    }
    return beside;
}

/* Attackers stand around their enemy, each on a tile of its own. */
static void test_attackers_surround_their_enemy(void)
{
    static Game game;
    REQUIRE(init_flat_game(&game));
    EntityId boar_id;
    Animal *boar = spawn_animal(&game, ANIMAL_BOAR, 24, 24, &boar_id);
    REQUIRE(boar != NULL);
    knock_down(boar);
    Animal *wolves[4];
    for (int i = 0; i < 4; i++) {
        EntityId id;
        wolves[i] = spawn_animal(&game, ANIMAL_WOLF, 16, 20 + 2 * i, &id);
        REQUIRE(wolves[i] != NULL);
        REQUIRE(game_debug_attack(&game, (Combatant){COMBATANT_ANIMAL, id},
                                  (Combatant){COMBATANT_ANIMAL, boar_id}));
    }
    bool overlap = false;
    int most_beside = 0;
    for (int tick = 0; tick < 8 * TICKS_PER_SECOND && animals_get(&game.animals, boar_id) != NULL;
         tick++) {
        game_tick(&game);
        TilePos prey = {(int16_t)boar->x, (int16_t)boar->y, (int16_t)boar->z};
        overlap |= wolves_overlap(wolves, 4, prey);
        int beside = wolves_beside(wolves, 4, prey);
        most_beside = beside > most_beside ? beside : most_beside;
    }
    CHECK(!overlap);
    CHECK(most_beside >= 3); /* Around it together. */
    game_shutdown(&game);
}

/* At the end of a dead end, there is room for one attacker only: the others
 * wait behind, then give up. */
static void test_no_room_around_the_enemy(void)
{
    static Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    for (int x = 10; x <= 30; x++) {
        for (int y = 19; y <= 21; y++) {
            if (y != 20 || x == 10) {
                map_set_rock(map, x, y, 0, ROCK_GRANITE);
            }
        }
    }
    /* The corridor y = 20 runs from x = 11 to 30, and opens at x = 31. */
    EntityId boar_id;
    Animal *boar = spawn_animal(&game, ANIMAL_BOAR, 11, 20, &boar_id);
    REQUIRE(boar != NULL);
    knock_down(boar);
    Animal *wolves[3];
    for (int i = 0; i < 3; i++) {
        EntityId id;
        wolves[i] = spawn_animal(&game, ANIMAL_WOLF, 34, 18 + 2 * i, &id);
        REQUIRE(wolves[i] != NULL);
        REQUIRE(game_debug_attack(&game, (Combatant){COMBATANT_ANIMAL, id},
                                  (Combatant){COMBATANT_ANIMAL, boar_id}));
    }
    bool overlap = false, waited = false;
    int most_beside = 0;
    for (int tick = 0; tick < 20 * TICKS_PER_SECOND && animals_get(&game.animals, boar_id) != NULL;
         tick++) {
        game_tick(&game);
        TilePos prey = {(int16_t)boar->x, (int16_t)boar->y, (int16_t)boar->z};
        overlap |= wolves_overlap(wolves, 3, prey);
        int beside = wolves_beside(wolves, 3, prey);
        most_beside = beside > most_beside ? beside : most_beside;
        for (int i = 0; i < 3; i++) {
            waited |= wolves[i]->task == ANIMAL_TASK_ATTACK && wolves[i]->surround_waits > 0;
        }
    }
    CHECK(!overlap);
    CHECK_EQ_INT(most_beside, 1);
    CHECK(waited);
    game_shutdown(&game);
}

/* Wolves share a carcass: one on it, the others around it, each on a tile of
 * its own, and all of them eat. */
static void test_wolves_share_a_carcass(void)
{
    static Game game;
    REQUIRE(init_flat_game(&game));
    Animal *wolves[3];
    for (int i = 0; i < 3; i++) {
        wolves[i] = spawn_hungry(&game, ANIMAL_WOLF, 20, 18 + 2 * i, NULL, 0.3f);
        REQUIRE(wolves[i] != NULL);
    }
    items_add(&game.items, 30, 20, 0, item_stack_pristine(ITEM_RED_DEER_CARCASS, 1));
    TilePos carcass = {30, 20, 0};
    bool overlap = false;
    for (int tick = 0; tick < 8 * TICKS_PER_SECOND; tick++) {
        game_tick(&game);
        overlap |= wolves_overlap(wolves, 3, (TilePos){0, 0, 0});
    }
    CHECK(!overlap);
    int on_it = 0, beside = 0;
    for (int i = 0; i < 3; i++) {
        const Animal *wolf = wolves[i];
        CHECK_EQ_INT(wolf->task, ANIMAL_TASK_EAT);
        CHECK(!animal_is_moving(wolf));
        CHECK(wolf->target.x == carcass.x && wolf->target.y == carcass.y);
        on_it += wolf->x == carcass.x && wolf->y == carcass.y;
        beside += combat_in_reach(tile_of_animal(wolf), carcass, 1) &&
                  !(wolf->x == carcass.x && wolf->y == carcass.y);
    }
    CHECK_EQ_INT(on_it, 1);
    CHECK_EQ_INT(beside, 2);
    run_ticks(&game, 20 * TICKS_PER_SECOND);
    for (int i = 0; i < 3; i++) {
        CHECK(wolves[i]->needs[ANIMAL_NEED_FOOD] > 0.45f); /* Each had its share. */
    }
    game_shutdown(&game);
}

/* Food beside a busy eater is eaten from beside it, not across a wall's corner. */
static void test_eating_from_beside(void)
{
    static Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    /* Rock around the carcass but on one side and one corner. */
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if ((dx != 0 || dy != 0) && !(dx == -1 && dy == 0) && !(dx == 1 && dy == 1)) {
                map_set_rock(map, 30 + dx, 20 + dy, 0, ROCK_GRANITE);
            }
        }
    }
    Animal *first = spawn_hungry(&game, ANIMAL_WOLF, 20, 20, NULL, 0.3f);
    Animal *second = spawn_hungry(&game, ANIMAL_WOLF, 20, 22, NULL, 0.3f);
    REQUIRE(first != NULL && second != NULL);
    items_add(&game.items, 30, 20, 0, item_stack_pristine(ITEM_RED_DEER_CARCASS, 1));
    run_ticks(&game, 8 * TICKS_PER_SECOND);
    Animal *wolves[2] = {first, second};
    CHECK(!wolves_overlap(wolves, 2, (TilePos){0, 0, 0}));
    /* One eats on the carcass, the other from the open side (29, 20): the
     * tile (31, 21) only touches it across two corners of rock. */
    int on_it = 0, west = 0;
    for (int i = 0; i < 2; i++) {
        on_it += wolves[i]->x == 30 && wolves[i]->y == 20;
        west += wolves[i]->x == 29 && wolves[i]->y == 20 && wolves[i]->task == ANIMAL_TASK_EAT;
        CHECK(!(wolves[i]->x == 31 && wolves[i]->y == 21 && wolves[i]->task == ANIMAL_TASK_EAT));
    }
    CHECK_EQ_INT(on_it, 1);
    CHECK_EQ_INT(west, 1);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_attack_definitions);
    RUN_TEST(test_combat_log);
    RUN_TEST(test_choosing_an_attack);
    RUN_TEST(test_reach);
    RUN_TEST(test_hit_chance);
    RUN_TEST(test_blows);
    RUN_TEST(test_dodges);
    RUN_TEST(test_breaking_off);
    RUN_TEST(test_queue);
    RUN_TEST(test_combatants_at);
    RUN_TEST(test_animal_hunts_a_hare_that_cannot_run);
    RUN_TEST(test_struck_animals_flee_or_fight_back);
    RUN_TEST(test_dazed_attacker_aims_worse);
    RUN_TEST(test_hurt_animal_breaks_off);
    RUN_TEST(test_settler_fights_back);
    RUN_TEST(test_settler_attack_by_weapon);
    RUN_TEST(test_settler_weapon_choice);
    RUN_TEST(test_armless_settler_does_not_fight);
    RUN_TEST(test_wolf_bites_and_claws);
    RUN_TEST(test_wolf_without_forepaws_only_bites);
    RUN_TEST(test_fearful_settler_flees);
    RUN_TEST(test_enemy_forgotten);
    RUN_TEST(test_hash_unchanged_without_fights);
    RUN_TEST(test_wounds_in_journals);
    RUN_TEST(test_wolf_eats_a_carcass);
    RUN_TEST(test_small_carcass_and_settler_bodies);
    RUN_TEST(test_fox_catches_a_hare);
    RUN_TEST(test_wolf_pack_hunts_together);
    RUN_TEST(test_prey_flee_carnivores);
    RUN_TEST(test_hungry_wolves_attack_settlers);
    RUN_TEST(test_fatigue);
    RUN_TEST(test_no_strike_across_a_corner);
    RUN_TEST(test_attackers_surround_their_enemy);
    RUN_TEST(test_no_room_around_the_enemy);
    RUN_TEST(test_wolves_share_a_carcass);
    RUN_TEST(test_eating_from_beside);
    RUN_TEST(test_drafted_settler_ignores_its_needs);
    RUN_TEST(test_who_can_be_drafted);
    RUN_TEST(test_draft_ends);
    RUN_TEST(test_drafted_settler_wields);
    RUN_TEST(test_change_weapon);
    RUN_TEST(test_wielding_with_locked_hands);
    RUN_TEST(test_self_defense_draws);
    RUN_TEST(test_drafted_settler_holds_its_ground);
    RUN_TEST(test_attack_order);
    return test_summary();
}
