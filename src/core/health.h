/*
 * Health (docs/HEALTH.md): bodies made of parts, injuries that bleed and
 * hurt, abilities that follow from the parts, death and natural healing. One
 * module for settlers and animals alike: only the body plan and the size
 * differ.
 *
 * A Health is owned by the being (settler, animal). Blows land on a part
 * (health_hurt()); health_update() runs the time: bleeding, clotting, healing,
 * blood coming back, and death. Consciousness (docs/HEALTH.md §11) is what
 * every deprivation, blood loss, the head and pain leave of it; it sets the
 * posture (up, collapsed, unconscious) after every change, and a factor on
 * what the body does. Abilities (consciousness, walking, the hands, sight,
 * eating) are read from the state at any time (health_ability()).
 */
#ifndef COLONY_CORE_HEALTH_H
#define COLONY_CORE_HEALTH_H

#include "core/rng.h"

#include <stdbool.h>
#include <stdint.h>

/* Tuning (docs/HEALTH.md). */
#define HEALTH_MAX_INJURIES 16
#define HEALTH_MAX_PARTS 8
/* An injury is serious from this share of its part's hit points: it does
 * not clot. */
#define HEALTH_SERIOUS_SEVERITY 0.4f
/* Blood lost per hour per point of damage of an injury (times its kind's
 * bleeding), as a share of the whole blood. */
#define HEALTH_BLEED_PER_DAMAGE_HOUR 0.004f
/* Hours a wound that is not serious takes to stop bleeding. */
#define HEALTH_CLOT_HOURS 3.0f
/* Blood: dead at HEALTH_BLOOD_DEATH; comes back at HEALTH_BLOOD_REGEN_PER_DAY
 * while nothing bleeds. */
#define HEALTH_BLOOD_DEATH 0.5f
#define HEALTH_BLOOD_REGEN_PER_DAY 0.1f
/* Points of damage healed a day by an injury that no longer bleeds (times
 * its kind's healing and the body's size). */
#define HEALTH_HEAL_PER_DAY 3.0f
/* Damage × pain of all injuries that makes full pain, for a body of size 1. */
#define HEALTH_PAIN_FULL 50.0f
/* A fracture caps its part's efficiency at this until it has healed. */
#define HEALTH_FRACTURE_EFFICIENCY 0.1f
/* Unable to walk below this share of the legs. */
#define HEALTH_WALKING_MIN 0.15f

/* Consciousness (docs/HEALTH.md §11): 1 less what every source takes off it.
 * Blood loss takes nothing down to HEALTH_BLOOD_LOSS_FROM, everything at
 * HEALTH_BLOOD_DEATH; a destroyed head takes HEALTH_HEAD_LOSS; full pain
 * HEALTH_PAIN_LOSS; a deprivation its severity to the power
 * HEALTH_DEPRIVATION_CURVE (slow at first, then steep). */
#define HEALTH_BLOOD_LOSS_FROM 0.85f
#define HEALTH_HEAD_LOSS 0.6f
#define HEALTH_PAIN_LOSS 0.625f
#define HEALTH_DEPRIVATION_CURVE 1.5f
/* Alive, consciousness stays at this at least: what never kills (pain) can
 * knock out, not kill. */
#define HEALTH_CONSCIOUS_FLOOR 0.05f
/* Collapsed below HEALTH_COLLAPSE_BELOW, up again from HEALTH_RISE_FROM;
 * unconscious below HEALTH_FAINT_BELOW, comes to from HEALTH_COME_TO_FROM. */
#define HEALTH_COLLAPSE_BELOW 0.5f
#define HEALTH_RISE_FROM 0.55f
#define HEALTH_FAINT_BELOW 0.25f
#define HEALTH_COME_TO_FROM 0.3f
/* In danger (an alert) below this. */
#define HEALTH_DANGER_BELOW 0.7f
/* The factor consciousness puts on what the body does (health_vigor()): 1
 * from HEALTH_VIGOR_FULL up, HEALTH_VIGOR_AT_COLLAPSE at HEALTH_COLLAPSE_BELOW,
 * on a straight line. */
#define HEALTH_VIGOR_FULL 0.9f
#define HEALTH_VIGOR_AT_COLLAPSE 0.5f

typedef enum BodyPlan {
    BODY_HUMAN,
    BODY_QUADRUPED, /* Deer, boar. */
    BODY_HARE,
    BODY_CANINE, /* Wolf, fox. */
    BODY_PLAN_COUNT
} BodyPlan;

/* What a part does for the body. */
typedef enum BodyRole {
    BODY_ROLE_NONE,
    BODY_ROLE_MIND,  /* The head: consciousness, sight, eating. */
    BODY_ROLE_WALK,  /* A leg. */
    BODY_ROLE_HANDS, /* An arm. */
} BodyRole;

/* What a being strikes with (docs/COMBAT.md §2): every attack is made with
 * one limb, and is as good as the parts that carry it are whole. A plan
 * lacking the limb cannot make the attack at all. */
typedef enum BodyLimb {
    BODY_LIMB_NONE,
    BODY_LIMB_ARMS,     /* Fists, a spear. */
    BODY_LIMB_HEAD,     /* Bites, tusks. */
    BODY_LIMB_FORELEGS, /* Claws, hooves striking. */
    BODY_LIMB_HINDLEGS, /* Kicks. */
    BODY_LIMB_COUNT
} BodyLimb;

typedef struct BodyPartDef {
    const char *name_key;
    float hit_points; /* For a body of size 1. */
    float coverage;   /* Share of the blows landing on it (a plan's sum to 1). */
    bool vital;       /* Destroyed: death. */
    BodyRole role;
    float share;   /* Its share of its role's ability (walking, the hands). */
    BodyLimb limb; /* What it strikes with, if anything (a part carries one limb at most). */
} BodyPartDef;

typedef enum InjuryType {
    INJURY_CUT,
    INJURY_BRUISE,
    INJURY_BITE,
    INJURY_FRACTURE,
    INJURY_CRUSH,
    INJURY_TYPE_COUNT
} InjuryType;

typedef struct InjuryDef {
    const char *name_key;
    float bleeding; /* × HEALTH_BLEED_PER_DAMAGE_HOUR per point. */
    float pain;     /* × damage, toward HEALTH_PAIN_FULL. */
    float healing;  /* × HEALTH_HEAL_PER_DAY. */
} InjuryDef;

typedef enum HealthAbility {
    HEALTH_CONSCIOUSNESS,
    HEALTH_WALKING,
    HEALTH_HANDS,
    HEALTH_SIGHT,
    HEALTH_EATING,
    HEALTH_ABILITY_COUNT
} HealthAbility;

typedef enum DeathCause {
    DEATH_NONE,
    DEATH_BLOOD_LOSS,
    DEATH_VITAL_PART, /* Health.death_part says which. */
    DEATH_STARVATION,
    DEATH_DEHYDRATION,
    DEATH_HYPOTHERMIA,
    DEATH_HEATSTROKE,
    DEATH_CAUSE_COUNT
} DeathCause;

/* What wears a being down when its needs or its surroundings go unanswered:
 * each is a severity from 0 to 1 that grows while the cause lasts, recovers
 * after it, and kills at 1. */
typedef enum Deprivation {
    DEPRIVATION_HUNGER,
    DEPRIVATION_THIRST,
    DEPRIVATION_COLD,
    DEPRIVATION_HEAT,
    DEPRIVATION_COUNT
} Deprivation;

/* What takes consciousness off (docs/HEALTH.md §11.2): the deprivations
 * first, in their order. */
typedef enum ConsciousnessSource {
    CONSCIOUSNESS_HUNGER,
    CONSCIOUSNESS_THIRST,
    CONSCIOUSNESS_COLD,
    CONSCIOUSNESS_HEAT,
    CONSCIOUSNESS_BLOOD,
    CONSCIOUSNESS_HEAD,
    CONSCIOUSNESS_PAIN, /* Never kills. */
    CONSCIOUSNESS_SOURCE_COUNT
} ConsciousnessSource;

/* Up, or lying where it fell (docs/HEALTH.md §11.3). */
typedef enum HealthPosture {
    POSTURE_UP,
    POSTURE_COLLAPSED,   /* Awake: sees, cannot walk nor work. */
    POSTURE_UNCONSCIOUS, /* Sees nothing either. */
    POSTURE_COUNT
} HealthPosture;

typedef struct Injury {
    uint8_t part; /* Index in the body plan. */
    uint8_t type; /* InjuryType. */
    float damage; /* Points, after the damage factor. */
    float hours;  /* Since it was received (clotting). */
} Injury;

/* What is hurting a body now, for the journal (core/journal.h): the caller
 * sets Health.hurt_cause before hurting it (health_fall() does it for falls). */
typedef enum HurtCause {
    HURT_OTHER, /* Unknown (the default), a debug blow. */
    HURT_FALL,
    HURT_CAVE_IN,
    HURT_TREE_TRUNK, /* A felled tree's trunk. */
    HURT_TREE_CROWN, /* Its branches. */
    HURT_CAUSE_COUNT
} HurtCause;

/* A wound just dealt, kept until the journal takes it (health_take_notes()). */
typedef struct HurtNote {
    uint8_t part;
    uint8_t type;  /* InjuryType */
    uint8_t cause; /* HurtCause */
} HurtNote;

#define HEALTH_NOTES_MAX 8

typedef struct Health {
    uint8_t plan;  /* BodyPlan. */
    uint8_t death; /* DeathCause; DEATH_NONE while alive. */
    uint8_t death_part;
    uint8_t injury_count;
    uint8_t posture;                      /* HealthPosture, kept for the thresholds' margins. */
    float size;                           /* Hit points and pain scale: 1 for a human. */
    float damage_factor;                  /* Blows × this (toughness). */
    float pain_factor;                    /* Pain × this (pain tolerance). */
    float blood;                          /* 1 full; dead at HEALTH_BLOOD_DEATH. */
    float deprivation[DEPRIVATION_COUNT]; /* Severity of each, 0 (none) to 1 (dead). */
    Injury injuries[HEALTH_MAX_INJURIES];
    /* For the journal only (not hashed): what hurts it now, and the wounds
     * dealt since the journal last took them. */
    uint8_t hurt_cause; /* HurtCause */
    uint8_t note_count;
    HurtNote notes[HEALTH_NOTES_MAX];
} Health;

/* A healthy body. Factors and size are clamped to sensible ranges. */
void health_init(Health *health, BodyPlan plan, float size, float damage_factor, float pain_factor);

/* Changes the factors (an attribute changed), clamped like health_init(). */
void health_set_factors(Health *health, float damage_factor, float pain_factor);

/* Parts of a plan: their count (0 for an invalid plan) and definitions
 * (NULL out of range). */
int body_part_count(BodyPlan plan);
const BodyPartDef *body_part_def(BodyPlan plan, int part);
const InjuryDef *injury_def(InjuryType type);

/* Whether a plan has parts that carry the limb, and its translation key
 * (NULL: out of range). */
bool body_plan_has_limb(BodyPlan plan, BodyLimb limb);
const char *body_limb_name_key(BodyLimb limb);
const char *health_ability_name_key(HealthAbility ability);

/* Hit points of a part of this body (its plan's times the size). */
float health_part_hit_points(const Health *health, int part);

/* Damage the part has taken: the sum of its injuries, capped at its hit
 * points. */
float health_part_damage(const Health *health, int part);

/* 0 (destroyed) to 1 (whole); a fracture caps it. */
float health_part_efficiency(const Health *health, int part);

/* A part drawn by coverage. */
int health_random_part(const Health *health, Rng *rng);

/*
 * A blow of `damage` points (before the damage factor) on a part: a new
 * injury, or added to the largest one of the same kind on the part when the
 * list is full. A destroyed vital part kills. Returns the damage taken (0 for
 * a dead body, an invalid part or kind, or no damage).
 */
float health_hurt(Health *health, int part, InjuryType type, float damage);

/* Takes the wounds noted since the last call (at most HEALTH_NOTES_MAX; `out`
 * may be NULL to just forget them). Returns how many. */
int health_take_notes(Health *health, HurtNote *out);

/* A blow on a part drawn by coverage (health_random_part()); `damage` is not
 * scaled by the size. Returns the damage taken. */
float health_blow(Health *health, Rng *rng, InjuryType type, float damage);

/* Falls of HEALTH_FALL_* (tuning): a drop of one level bruises, a longer one
 * breaks a leg and bruises more the further it was. Scaled by the size (a
 * heavier body lands harder). */
#define HEALTH_FALL_BRUISE_MIN 3.0f
#define HEALTH_FALL_BRUISE_MAX 8.0f
#define HEALTH_FALL_FRACTURE_MIN 3.0f
#define HEALTH_FALL_FRACTURE_MAX 6.0f
void health_fall(Health *health, Rng *rng, int levels);

/* Runs `hours` of time: bleeding, clotting, healing, blood coming back.
 * Returns true when the being dies during it (blood, or the deadly sources of
 * consciousness loss adding up to everything). */
bool health_update(Health *health, float hours);

bool health_is_dead(const Health *health);

/* Hours of full pressure a deprivation takes to kill, and to wear off once
 * the cause is gone (tuning). NULL key: out of range. */
float deprivation_death_hours(Deprivation kind);
float deprivation_recovery_hours(Deprivation kind);
const char *deprivation_name_key(Deprivation kind);
const char *deprivation_description_key(Deprivation kind);

/*
 * `hours` of time under a pressure from 0 (none) to 1 (full): the deprivation
 * grows by pressure × hours over its death hours, or, with no pressure,
 * recovers over its recovery hours. Returns true when the being dies: the
 * deprivation reaches 1, or the deadly sources of consciousness loss add up
 * to everything.
 * Separate from health_update(): the caller sees to the needs and the
 * temperature.
 */
bool health_strain(Health *health, Deprivation kind, float pressure, float hours);

/* Severity of a deprivation (0 for a dead body or an invalid kind). */
float health_deprivation(const Health *health, Deprivation kind);

/* How bad a deprivation is, in a word (mild, moderate, severe), for its
 * severity; NULL at 0. */
const char *deprivation_severity_key(float severity);

/* What a source of consciousness loss is called (NULL out of range), and
 * whether it can kill. */
const char *consciousness_source_name_key(ConsciousnessSource source);
bool consciousness_source_deadly(ConsciousnessSource source);

/* What a source takes off consciousness now, 0..1 (0 for a dead body or an
 * invalid source). */
float health_consciousness_loss(const Health *health, ConsciousnessSource source);

/* Consciousness, 0..1: 1 less what every source takes, at least
 * HEALTH_CONSCIOUS_FLOOR while alive; 0 dead. */
float health_consciousness(const Health *health);

/* The factor consciousness puts on what the body does (walking, work, blows,
 * running, learning... docs/HEALTH.md §11.4): 1 from HEALTH_VIGOR_FULL up,
 * HEALTH_VIGOR_AT_COLLAPSE at HEALTH_COLLAPSE_BELOW, less below; 0 dead. */
float health_vigor(const Health *health);

/* Whether anything is wrong: an injury, blood not yet back, or a deprivation. */
bool health_is_hurt(const Health *health);

/* Blood lost per hour now (0 when nothing bleeds). */
float health_bleeding(const Health *health);

/* 0..1, the pain factor included. */
float health_pain(const Health *health);

/* 0..1 (0 for a dead body). Walking and the hands: the legs and the arms,
 * times health_vigor(); 0 when collapsed. */
float health_ability(const Health *health, HealthAbility ability);

/*
 * How well the being can strike with a limb, 0..1: the mean efficiency of the
 * parts that carry it (one arm lost of two: half), times health_vigor() like
 * the hands. 0 for a dead or collapsed body, and for a limb the plan does not
 * have.
 */
float health_limb_ability(const Health *health, BodyLimb limb);

/* Lying where it fell from lack of consciousness: collapsed or unconscious
 * (below HEALTH_COLLAPSE_BELOW, up again from HEALTH_RISE_FROM). */
bool health_is_collapsed(const Health *health);

/* Knocked out (below HEALTH_FAINT_BELOW, comes to from HEALTH_COME_TO_FROM):
 * sees nothing. */
bool health_is_unconscious(const Health *health);

/* How far a being lying collapsed sees, in tiles: from HEALTH_SIGHT_REACH_MAX
 * at HEALTH_COLLAPSE_BELOW down to 1 (what touches it) at HEALTH_FAINT_BELOW;
 * 0 (no limit) when it is up. */
#define HEALTH_SIGHT_REACH_MAX 12
int health_sight_reach(const Health *health);

/* Alive with consciousness below HEALTH_DANGER_BELOW. */
bool health_in_danger(const Health *health);

/* Down: collapsed, or unable to walk (the legs below HEALTH_WALKING_MIN). */
bool health_is_down(const Health *health);

/* Damage of the injury over its part's hit points (0..1). */
float health_injury_severity(const Health *health, const Injury *injury);

/* Whether the injury is serious (it does not clot). */
bool health_injury_serious(const Health *health, const Injury *injury);

/* Translation key of the body's state in a word: healthy, hurt (an injury,
 * blood or a deprivation), unable to walk, collapsed, unconscious, dead. */
const char *health_status_key(const Health *health);

/* Blood the injury loses per hour now. */
float health_injury_bleeding(const Health *health, const Injury *injury);

/* The state enters the hash only when hurt or dead: hashes of games where
 * nobody is hurt stay as they were. */
uint64_t health_hash(const Health *health, uint64_t hash);

#endif /* COLONY_CORE_HEALTH_H */
