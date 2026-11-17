/*
 * Health (docs/HEALTH.md): bodies made of parts, injuries that bleed and
 * hurt, abilities that follow from the parts, death and natural healing. One
 * module for settlers and animals alike: only the body plan and the size
 * differ.
 *
 * A Health is owned by the being (settler, animal). Blows land on a part
 * (health_hurt()); health_update() runs the time: bleeding, clotting, healing,
 * blood coming back, and death. Abilities (consciousness, walking, the hands,
 * sight, eating) are read from the state at any time (health_ability()).
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
/* Blood: dead at HEALTH_BLOOD_DEATH, consciousness falls from
 * HEALTH_BLOOD_FAINT down to it; comes back at HEALTH_BLOOD_REGEN_PER_DAY
 * while nothing bleeds. */
#define HEALTH_BLOOD_DEATH 0.5f
#define HEALTH_BLOOD_FAINT 0.7f
#define HEALTH_BLOOD_REGEN_PER_DAY 0.1f
/* Points of damage healed a day by an injury that no longer bleeds (times
 * its kind's healing and the body's size). */
#define HEALTH_HEAL_PER_DAY 3.0f
/* Damage × pain of all injuries that makes full pain, for a body of size 1. */
#define HEALTH_PAIN_FULL 50.0f
/* Pain from which the being is knocked down. */
#define HEALTH_PAIN_KNOCKDOWN 0.8f
/* Walking and the hands are slowed by this much at full pain. */
#define HEALTH_PAIN_SLOWDOWN 0.4f
/* A fracture caps its part's efficiency at this until it has healed. */
#define HEALTH_FRACTURE_EFFICIENCY 0.1f
/* Unconscious below this consciousness; unable to walk below this walking. */
#define HEALTH_CONSCIOUS_MIN 0.3f
#define HEALTH_WALKING_MIN 0.15f

typedef enum BodyPlan {
    BODY_HUMAN,
    BODY_QUADRUPED, /* Deer, boar. */
    BODY_HARE,
    BODY_PLAN_COUNT
} BodyPlan;

/* What a part does for the body. */
typedef enum BodyRole {
    BODY_ROLE_NONE,
    BODY_ROLE_MIND,  /* The head: consciousness, sight, eating. */
    BODY_ROLE_WALK,  /* A leg. */
    BODY_ROLE_HANDS, /* An arm. */
} BodyRole;

typedef struct BodyPartDef {
    const char *name_key;
    float hit_points; /* For a body of size 1. */
    float coverage;   /* Share of the blows landing on it (a plan's sum to 1). */
    bool vital;       /* Destroyed: death. */
    BodyRole role;
    float share; /* Its share of its role's ability (walking, the hands). */
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
    DEATH_CAUSE_COUNT
} DeathCause;

typedef struct Injury {
    uint8_t part; /* Index in the body plan. */
    uint8_t type; /* InjuryType. */
    float damage; /* Points, after the damage factor. */
    float hours;  /* Since it was received (clotting). */
} Injury;

typedef struct Health {
    uint8_t plan;  /* BodyPlan. */
    uint8_t death; /* DeathCause; DEATH_NONE while alive. */
    uint8_t death_part;
    uint8_t injury_count;
    float size;          /* Hit points and pain scale: 1 for a human. */
    float damage_factor; /* Blows × this (toughness). */
    float pain_factor;   /* Pain × this (pain tolerance). */
    float blood;         /* 1 full; dead at HEALTH_BLOOD_DEATH. */
    Injury injuries[HEALTH_MAX_INJURIES];
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
 * Returns true when the being dies during it. */
bool health_update(Health *health, float hours);

bool health_is_dead(const Health *health);

/* Whether anything is wrong: an injury, or blood not yet back. */
bool health_is_hurt(const Health *health);

/* Blood lost per hour now (0 when nothing bleeds). */
float health_bleeding(const Health *health);

/* 0..1, the pain factor included. */
float health_pain(const Health *health);

/* 0..1 (0 for a dead body). */
float health_ability(const Health *health, HealthAbility ability);

/* Knocked down: unconscious (consciousness below HEALTH_CONSCIOUS_MIN). */
bool health_is_unconscious(const Health *health);

/* Down: unconscious, or unable to walk (walking below HEALTH_WALKING_MIN). */
bool health_is_down(const Health *health);

/* Damage of the injury over its part's hit points (0..1). */
float health_injury_severity(const Health *health, const Injury *injury);

/* Whether the injury is serious (it does not clot). */
bool health_injury_serious(const Health *health, const Injury *injury);

/* Translation key of the body's state in a word: healthy, hurt, unable to
 * walk, unconscious, dead. */
const char *health_status_key(const Health *health);

/* Blood the injury loses per hour now. */
float health_injury_bleeding(const Health *health, const Injury *injury);

/* The state enters the hash only when hurt or dead: hashes of games where
 * nobody is hurt stay as they were. */
uint64_t health_hash(const Health *health, uint64_t hash);

#endif /* COLONY_CORE_HEALTH_H */
