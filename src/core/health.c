#include "core/health.h"

#include "core/i18n.h"
#include "core/util.h"

#include <math.h>
#include <string.h>

/* Body plans (docs/HEALTH.md §2): hit points for a body of size 1. */
static const BodyPartDef HUMAN_PARTS[] = {
    {N_("body.head"), 25.0f, 0.08f, true, BODY_ROLE_MIND, 1.0f, BODY_LIMB_NONE},
    {N_("body.torso"), 40.0f, 0.42f, true, BODY_ROLE_NONE, 0.0f, BODY_LIMB_NONE},
    {N_("body.left_arm"), 20.0f, 0.125f, false, BODY_ROLE_HANDS, 0.5f, BODY_LIMB_ARMS},
    {N_("body.right_arm"), 20.0f, 0.125f, false, BODY_ROLE_HANDS, 0.5f, BODY_LIMB_ARMS},
    {N_("body.left_leg"), 25.0f, 0.125f, false, BODY_ROLE_WALK, 0.5f, BODY_LIMB_NONE},
    {N_("body.right_leg"), 25.0f, 0.125f, false, BODY_ROLE_WALK, 0.5f, BODY_LIMB_NONE},
};

static const BodyPartDef QUADRUPED_PARTS[] = {
    {N_("body.head"), 20.0f, 0.08f, true, BODY_ROLE_MIND, 1.0f, BODY_LIMB_HEAD},
    {N_("body.neck"), 20.0f, 0.07f, true, BODY_ROLE_NONE, 0.0f, BODY_LIMB_NONE},
    {N_("body.torso"), 45.0f, 0.45f, true, BODY_ROLE_NONE, 0.0f, BODY_LIMB_NONE},
    {N_("body.front_left_leg"), 20.0f, 0.1f, false, BODY_ROLE_WALK, 0.25f, BODY_LIMB_FORELEGS},
    {N_("body.front_right_leg"), 20.0f, 0.1f, false, BODY_ROLE_WALK, 0.25f, BODY_LIMB_FORELEGS},
    {N_("body.hind_left_leg"), 20.0f, 0.1f, false, BODY_ROLE_WALK, 0.25f, BODY_LIMB_HINDLEGS},
    {N_("body.hind_right_leg"), 20.0f, 0.1f, false, BODY_ROLE_WALK, 0.25f, BODY_LIMB_HINDLEGS},
};

static const BodyPartDef HARE_PARTS[] = {
    {N_("body.head"), 20.0f, 0.1f, true, BODY_ROLE_MIND, 1.0f, BODY_LIMB_NONE},
    {N_("body.torso"), 35.0f, 0.5f, true, BODY_ROLE_NONE, 0.0f, BODY_LIMB_NONE},
    {N_("body.front_left_leg"), 15.0f, 0.1f, false, BODY_ROLE_WALK, 0.2f, BODY_LIMB_NONE},
    {N_("body.front_right_leg"), 15.0f, 0.1f, false, BODY_ROLE_WALK, 0.2f, BODY_LIMB_NONE},
    {N_("body.hind_left_leg"), 15.0f, 0.1f, false, BODY_ROLE_WALK, 0.3f, BODY_LIMB_NONE},
    {N_("body.hind_right_leg"), 15.0f, 0.1f, false, BODY_ROLE_WALK, 0.3f, BODY_LIMB_NONE},
};

/* Wolves and foxes: a longer muzzle (a bigger share of the blows), and the
 * forepaws that claw. */
static const BodyPartDef CANINE_PARTS[] = {
    {N_("body.head"), 20.0f, 0.1f, true, BODY_ROLE_MIND, 1.0f, BODY_LIMB_HEAD},
    {N_("body.neck"), 20.0f, 0.07f, true, BODY_ROLE_NONE, 0.0f, BODY_LIMB_NONE},
    {N_("body.torso"), 45.0f, 0.43f, true, BODY_ROLE_NONE, 0.0f, BODY_LIMB_NONE},
    {N_("body.front_left_leg"), 20.0f, 0.1f, false, BODY_ROLE_WALK, 0.25f, BODY_LIMB_FORELEGS},
    {N_("body.front_right_leg"), 20.0f, 0.1f, false, BODY_ROLE_WALK, 0.25f, BODY_LIMB_FORELEGS},
    {N_("body.hind_left_leg"), 20.0f, 0.1f, false, BODY_ROLE_WALK, 0.25f, BODY_LIMB_HINDLEGS},
    {N_("body.hind_right_leg"), 20.0f, 0.1f, false, BODY_ROLE_WALK, 0.25f, BODY_LIMB_HINDLEGS},
};

typedef struct BodyPlanDef {
    const BodyPartDef *parts;
    int count;
} BodyPlanDef;

#define PLAN(parts)                                                                                \
    {                                                                                              \
        (parts), (int)(sizeof(parts) / sizeof((parts)[0]))                                         \
    }
static const BodyPlanDef BODY_PLANS[BODY_PLAN_COUNT] = {
    [BODY_HUMAN] = PLAN(HUMAN_PARTS),
    [BODY_QUADRUPED] = PLAN(QUADRUPED_PARTS),
    [BODY_HARE] = PLAN(HARE_PARTS),
    [BODY_CANINE] = PLAN(CANINE_PARTS),
};
#undef PLAN

_Static_assert(sizeof(QUADRUPED_PARTS) / sizeof(QUADRUPED_PARTS[0]) <= HEALTH_MAX_PARTS &&
                   sizeof(CANINE_PARTS) / sizeof(CANINE_PARTS[0]) <= HEALTH_MAX_PARTS,
               "parts fit in HEALTH_MAX_PARTS");

/* Injuries (docs/HEALTH.md §3): bleeding, pain, healing. */
static const InjuryDef INJURY_DEFS[INJURY_TYPE_COUNT] = {
    [INJURY_CUT] = {N_("injury.cut"), 1.0f, 1.0f, 1.0f},
    [INJURY_BRUISE] = {N_("injury.bruise"), 0.0f, 0.8f, 1.5f},
    [INJURY_BITE] = {N_("injury.bite"), 0.8f, 1.2f, 0.8f},
    [INJURY_FRACTURE] = {N_("injury.fracture"), 0.0f, 1.5f, 0.25f},
    [INJURY_CRUSH] = {N_("injury.crush"), 0.3f, 1.2f, 0.6f},
};

static const char *const ABILITY_KEYS[HEALTH_ABILITY_COUNT] = {
    [HEALTH_CONSCIOUSNESS] = N_("health.ability.consciousness"),
    [HEALTH_WALKING] = N_("health.ability.walking"),
    [HEALTH_HANDS] = N_("health.ability.hands"),
    [HEALTH_SIGHT] = N_("health.ability.sight"),
    [HEALTH_EATING] = N_("health.ability.eating"),
};

/* Deprivations (docs/HEALTH.md §10): hours at full pressure to kill, hours to
 * recover, what they are called, what they do, and the death they bring. */
typedef struct DeprivationDef {
    const char *name_key;
    const char *description_key;
    float death_hours;
    float recovery_hours;
    DeathCause cause;
} DeprivationDef;

static const DeprivationDef DEPRIVATION_DEFS[DEPRIVATION_COUNT] = {
    [DEPRIVATION_HUNGER] = {N_("health.deprivation.hunger"),
                            N_("health.deprivation.hunger.description"), 96.0f, 48.0f,
                            DEATH_STARVATION},
    [DEPRIVATION_THIRST] = {N_("health.deprivation.thirst"),
                            N_("health.deprivation.thirst.description"), 40.0f, 10.0f,
                            DEATH_DEHYDRATION},
    [DEPRIVATION_COLD] = {N_("health.deprivation.cold"), N_("health.deprivation.cold.description"),
                          14.0f, 6.0f, DEATH_HYPOTHERMIA},
    [DEPRIVATION_HEAT] = {N_("health.deprivation.heat"), N_("health.deprivation.heat.description"),
                          10.0f, 6.0f, DEATH_HEATSTROKE},
};

/* What takes consciousness off (docs/HEALTH.md §11.2): its name, whether it
 * can kill, and the death it brings. */
typedef struct ConsciousnessSourceDef {
    const char *name_key;
    bool deadly;
    DeathCause cause;
} ConsciousnessSourceDef;

static const ConsciousnessSourceDef SOURCE_DEFS[CONSCIOUSNESS_SOURCE_COUNT] = {
    [CONSCIOUSNESS_HUNGER] = {N_("health.deprivation.hunger"), true, DEATH_STARVATION},
    [CONSCIOUSNESS_THIRST] = {N_("health.deprivation.thirst"), true, DEATH_DEHYDRATION},
    [CONSCIOUSNESS_COLD] = {N_("health.deprivation.cold"), true, DEATH_HYPOTHERMIA},
    [CONSCIOUSNESS_HEAT] = {N_("health.deprivation.heat"), true, DEATH_HEATSTROKE},
    [CONSCIOUSNESS_BLOOD] = {N_("health.consciousness.blood"), true, DEATH_BLOOD_LOSS},
    [CONSCIOUSNESS_HEAD] = {N_("health.consciousness.head"), true, DEATH_VITAL_PART},
    [CONSCIOUSNESS_PAIN] = {N_("health.pain"), false, DEATH_NONE},
};

_Static_assert((int)CONSCIOUSNESS_HUNGER == (int)DEPRIVATION_HUNGER &&
                   (int)CONSCIOUSNESS_THIRST == (int)DEPRIVATION_THIRST &&
                   (int)CONSCIOUSNESS_COLD == (int)DEPRIVATION_COLD &&
                   (int)CONSCIOUSNESS_HEAT == (int)DEPRIVATION_HEAT,
               "the deprivations come first, in their order");

/* Eating lost at a destroyed head. */
#define HEAD_EATING_LOSS 0.5f

static bool settle(Health *health);

void health_init(Health *health, BodyPlan plan, float size, float damage_factor, float pain_factor)
{
    memset(health, 0, sizeof(*health));
    health->plan = (uint8_t)((unsigned)plan < BODY_PLAN_COUNT ? plan : BODY_HUMAN);
    health->size = clampf(size, 0.05f, 10.0f);
    health->blood = 1.0f;
    health_set_factors(health, damage_factor, pain_factor);
}

void health_set_factors(Health *health, float damage_factor, float pain_factor)
{
    health->damage_factor = clampf(damage_factor, 0.25f, 4.0f);
    health->pain_factor = clampf(pain_factor, 0.25f, 4.0f);
    settle(health); /* Pain may have changed. */
}

int body_part_count(BodyPlan plan)
{
    return (unsigned)plan < BODY_PLAN_COUNT ? BODY_PLANS[plan].count : 0;
}

const BodyPartDef *body_part_def(BodyPlan plan, int part)
{
    if (part < 0 || part >= body_part_count(plan)) {
        return NULL;
    }
    return &BODY_PLANS[plan].parts[part];
}

bool body_plan_has_limb(BodyPlan plan, BodyLimb limb)
{
    if (limb <= BODY_LIMB_NONE || (unsigned)limb >= BODY_LIMB_COUNT) {
        return false;
    }
    for (int part = 0; part < body_part_count(plan); part++) {
        if (BODY_PLANS[plan].parts[part].limb == limb) {
            return true;
        }
    }
    return false;
}

const char *body_limb_name_key(BodyLimb limb)
{
    static const char *const KEYS[BODY_LIMB_COUNT] = {
        [BODY_LIMB_ARMS] = N_("body.limb.arms"),
        [BODY_LIMB_HEAD] = N_("body.limb.head"),
        [BODY_LIMB_FORELEGS] = N_("body.limb.forelegs"),
        [BODY_LIMB_HINDLEGS] = N_("body.limb.hindlegs"),
    };
    return limb > BODY_LIMB_NONE && (unsigned)limb < BODY_LIMB_COUNT ? KEYS[limb] : NULL;
}

const InjuryDef *injury_def(InjuryType type)
{
    return (unsigned)type < INJURY_TYPE_COUNT ? &INJURY_DEFS[type] : NULL;
}

const char *health_ability_name_key(HealthAbility ability)
{
    return (unsigned)ability < HEALTH_ABILITY_COUNT ? ABILITY_KEYS[ability] : NULL;
}

float health_part_hit_points(const Health *health, int part)
{
    const BodyPartDef *def = body_part_def((BodyPlan)health->plan, part);
    return def != NULL ? def->hit_points * health->size : 0.0f;
}

float health_part_damage(const Health *health, int part)
{
    float damage = 0.0f;
    for (int i = 0; i < health->injury_count; i++) {
        if (health->injuries[i].part == part) {
            damage += health->injuries[i].damage;
        }
    }
    float hit_points = health_part_hit_points(health, part);
    return damage < hit_points ? damage : hit_points;
}

float health_part_efficiency(const Health *health, int part)
{
    float hit_points = health_part_hit_points(health, part);
    if (hit_points <= 0.0f) {
        return 0.0f;
    }
    float efficiency = 1.0f - health_part_damage(health, part) / hit_points;
    for (int i = 0; i < health->injury_count; i++) {
        const Injury *injury = &health->injuries[i];
        if (injury->part == part && injury->type == INJURY_FRACTURE && injury->damage > 0.0f &&
            efficiency > HEALTH_FRACTURE_EFFICIENCY) {
            efficiency = HEALTH_FRACTURE_EFFICIENCY;
        }
    }
    return clampf(efficiency, 0.0f, 1.0f);
}

int health_random_part(const Health *health, Rng *rng)
{
    int count = body_part_count((BodyPlan)health->plan);
    float roll = rng_next_float(rng), sum = 0.0f;
    for (int part = 0; part < count; part++) {
        sum += BODY_PLANS[health->plan].parts[part].coverage;
        if (roll < sum) {
            return part;
        }
    }
    return count - 1;
}

float health_blow(Health *health, Rng *rng, InjuryType type, float damage)
{
    return health_hurt(health, health_random_part(health, rng), type, damage);
}

/* A number between `min` and `max`. */
static float roll_between(Rng *rng, float min, float max)
{
    return min + (max - min) * rng_next_float(rng);
}

void health_fall(Health *health, Rng *rng, int levels)
{
    if (levels <= 0 || health->death != DEATH_NONE) {
        return;
    }
    float size = health->size;
    uint8_t cause = health->hurt_cause;
    health->hurt_cause = HURT_FALL;
    if (levels >= 2) { /* Lands on a leg, and breaks it. */
        int legs[HEALTH_MAX_PARTS], count = 0;
        for (int part = 0; part < body_part_count((BodyPlan)health->plan); part++) {
            if (BODY_PLANS[health->plan].parts[part].role == BODY_ROLE_WALK) {
                legs[count++] = part;
            }
        }
        if (count > 0) {
            health_hurt(health, legs[rng_range_i32(rng, 0, count - 1)], INJURY_FRACTURE,
                        size *
                            roll_between(rng, HEALTH_FALL_FRACTURE_MIN, HEALTH_FALL_FRACTURE_MAX));
        }
    }
    /* A bruise for every level, the worse the further. */
    for (int i = 0; i < levels && health->death == DEATH_NONE; i++) {
        health_blow(health, rng, INJURY_BRUISE,
                    size * (float)levels *
                        roll_between(rng, HEALTH_FALL_BRUISE_MIN, HEALTH_FALL_BRUISE_MAX));
    }
    health->hurt_cause = cause;
}

/* Index of the largest injury matching (part, kind; any kind when `type` is
 * INJURY_TYPE_COUNT), -1 when none. */
static int largest_injury(const Health *health, int part, InjuryType type)
{
    int best = -1;
    for (int i = 0; i < health->injury_count; i++) {
        const Injury *injury = &health->injuries[i];
        if (injury->part == part && (type == INJURY_TYPE_COUNT || injury->type == type) &&
            (best < 0 || injury->damage > health->injuries[best].damage)) {
            best = i;
        }
    }
    return best;
}

static void die(Health *health, DeathCause cause, int part)
{
    if (health->death == DEATH_NONE) {
        health->death = (uint8_t)cause;
        health->death_part = (uint8_t)(part >= 0 ? part : 0);
    }
}

float health_hurt(Health *health, int part, InjuryType type, float damage)
{
    const BodyPartDef *def = body_part_def((BodyPlan)health->plan, part);
    if (health->death != DEATH_NONE || def == NULL || (unsigned)type >= INJURY_TYPE_COUNT ||
        !(damage > 0.0f)) {
        return 0.0f;
    }
    float taken = damage * health->damage_factor;
    Injury *injury;
    if (health->injury_count < HEALTH_MAX_INJURIES) {
        injury = &health->injuries[health->injury_count++];
        *injury = (Injury){(uint8_t)part, (uint8_t)type, 0.0f, 0.0f};
    } else {
        int index = largest_injury(health, part, type);
        if (index < 0) {
            index = largest_injury(health, part, INJURY_TYPE_COUNT);
        }
        if (index < 0) { /* Nothing on that part: the smallest injury makes room. */
            index = 0;
            for (int i = 1; i < health->injury_count; i++) {
                if (health->injuries[i].damage < health->injuries[index].damage) {
                    index = i;
                }
            }
            health->injuries[index] = (Injury){(uint8_t)part, (uint8_t)type, 0.0f, 0.0f};
        }
        injury = &health->injuries[index];
        injury->hours = 0.0f; /* Opened again: it bleeds anew. */
    }
    injury->damage += taken;
    if (health->note_count < HEALTH_NOTES_MAX) {
        health->notes[health->note_count++] =
            (HurtNote){(uint8_t)part, (uint8_t)type, health->hurt_cause};
    }
    if (def->vital && health_part_damage(health, part) >= health_part_hit_points(health, part)) {
        die(health, DEATH_VITAL_PART, part);
    }
    settle(health);
    return taken;
}

int health_take_notes(Health *health, HurtNote *out)
{
    int count = health->note_count;
    if (out != NULL) {
        memcpy(out, health->notes, (size_t)count * sizeof(HurtNote));
    }
    health->note_count = 0;
    return count;
}

float health_injury_severity(const Health *health, const Injury *injury)
{
    float hit_points = health_part_hit_points(health, injury->part);
    return hit_points > 0.0f ? clampf(injury->damage / hit_points, 0.0f, 1.0f) : 0.0f;
}

bool health_injury_serious(const Health *health, const Injury *injury)
{
    float hit_points = health_part_hit_points(health, injury->part);
    return hit_points > 0.0f && injury->damage >= hit_points * HEALTH_SERIOUS_SEVERITY;
}

const char *health_status_key(const Health *health)
{
    if (health->death != DEATH_NONE) {
        return N_("health.status.dead");
    }
    if (health_is_unconscious(health)) {
        return N_("health.status.unconscious");
    }
    if (health_is_collapsed(health)) {
        return N_("health.status.collapsed");
    }
    if (health_is_down(health)) {
        return N_("health.status.cannot_walk");
    }
    return health_is_hurt(health) ? N_("health.status.hurt") : N_("health.status.healthy");
}

float health_injury_bleeding(const Health *health, const Injury *injury)
{
    const InjuryDef *def = injury_def((InjuryType)injury->type);
    if (health->death != DEATH_NONE || def == NULL || def->bleeding <= 0.0f ||
        injury->damage <= 0.0f) {
        return 0.0f;
    }
    float clot = 1.0f;
    if (!health_injury_serious(health, injury)) {
        clot = 1.0f - injury->hours / HEALTH_CLOT_HOURS;
        if (clot <= 0.0f) {
            return 0.0f;
        }
    }
    /* Damage over the size: a hare bleeds out from its small wounds as a
     * human does from large ones. */
    return def->bleeding * (injury->damage / health->size) * HEALTH_BLEED_PER_DAMAGE_HOUR * clot;
}

float health_bleeding(const Health *health)
{
    float total = 0.0f;
    for (int i = 0; i < health->injury_count; i++) {
        total += health_injury_bleeding(health, &health->injuries[i]);
    }
    return total;
}

bool health_update(Health *health, float hours)
{
    if (health->death != DEATH_NONE || !(hours > 0.0f) || !health_is_hurt(health)) {
        return false;
    }
    float bleeding = health_bleeding(health);
    int kept = 0;
    for (int i = 0; i < health->injury_count; i++) {
        Injury injury = health->injuries[i];
        if (health_injury_bleeding(health, &injury) <= 0.0f) { /* Closed: it heals. */
            injury.damage -= HEALTH_HEAL_PER_DAY * INJURY_DEFS[injury.type].healing * health->size *
                             hours / 24.0f;
        }
        injury.hours += hours;
        if (injury.damage > 0.0f) {
            health->injuries[kept++] = injury;
        }
    }
    health->injury_count = (uint8_t)kept;
    if (bleeding > 0.0f) {
        health->blood -= bleeding * hours;
    } else if (health->blood < 1.0f) {
        health->blood =
            clampf(health->blood + HEALTH_BLOOD_REGEN_PER_DAY * hours / 24.0f, 0.0f, 1.0f);
    }
    if (health->blood <= HEALTH_BLOOD_DEATH) {
        health->blood = HEALTH_BLOOD_DEATH;
        die(health, DEATH_BLOOD_LOSS, -1);
        return true;
    }
    return settle(health);
}

bool health_is_dead(const Health *health)
{
    return health->death != DEATH_NONE;
}

float deprivation_death_hours(Deprivation kind)
{
    return (unsigned)kind < DEPRIVATION_COUNT ? DEPRIVATION_DEFS[kind].death_hours : 0.0f;
}

float deprivation_recovery_hours(Deprivation kind)
{
    return (unsigned)kind < DEPRIVATION_COUNT ? DEPRIVATION_DEFS[kind].recovery_hours : 0.0f;
}

const char *deprivation_name_key(Deprivation kind)
{
    return (unsigned)kind < DEPRIVATION_COUNT ? DEPRIVATION_DEFS[kind].name_key : NULL;
}

const char *deprivation_description_key(Deprivation kind)
{
    return (unsigned)kind < DEPRIVATION_COUNT ? DEPRIVATION_DEFS[kind].description_key : NULL;
}

bool health_strain(Health *health, Deprivation kind, float pressure, float hours)
{
    if (health->death != DEATH_NONE || (unsigned)kind >= DEPRIVATION_COUNT || !(hours > 0.0f)) {
        return false;
    }
    const DeprivationDef *def = &DEPRIVATION_DEFS[kind];
    float *severity = &health->deprivation[kind];
    if (pressure > 0.0f) {
        *severity += clampf(pressure, 0.0f, 1.0f) * hours / def->death_hours;
    } else if (*severity > 0.0f) {
        *severity = fmaxf(0.0f, *severity - hours / def->recovery_hours);
    }
    if (*severity >= 1.0f) {
        *severity = 1.0f;
        die(health, def->cause, -1);
        return true;
    }
    return settle(health);
}

float health_deprivation(const Health *health, Deprivation kind)
{
    if (health->death != DEATH_NONE || (unsigned)kind >= DEPRIVATION_COUNT) {
        return 0.0f;
    }
    return health->deprivation[kind];
}

const char *deprivation_severity_key(float severity)
{
    if (!(severity > 0.0f)) {
        return NULL;
    }
    return severity < 1.0f / 3.0f   ? N_("health.severity.mild")
           : severity < 2.0f / 3.0f ? N_("health.severity.moderate")
                                    : N_("health.severity.severe");
}

const char *consciousness_source_name_key(ConsciousnessSource source)
{
    return (unsigned)source < CONSCIOUSNESS_SOURCE_COUNT ? SOURCE_DEFS[source].name_key : NULL;
}

bool consciousness_source_deadly(ConsciousnessSource source)
{
    return (unsigned)source < CONSCIOUSNESS_SOURCE_COUNT && SOURCE_DEFS[source].deadly;
}

bool health_is_hurt(const Health *health)
{
    for (int kind = 0; kind < DEPRIVATION_COUNT; kind++) {
        if (health->deprivation[kind] > 0.0f) {
            return true;
        }
    }
    return health->injury_count > 0 || health->blood < 1.0f;
}

float health_pain(const Health *health)
{
    if (health->death != DEATH_NONE || health->injury_count == 0) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (int i = 0; i < health->injury_count; i++) {
        sum += health->injuries[i].damage * INJURY_DEFS[health->injuries[i].type].pain;
    }
    return clampf(sum / (HEALTH_PAIN_FULL * health->size) * health->pain_factor, 0.0f, 1.0f);
}

/* Efficiency of the part with a role (the head), 1 when the plan has none. */
static float role_part_efficiency(const Health *health, BodyRole role)
{
    int count = body_part_count((BodyPlan)health->plan);
    for (int part = 0; part < count; part++) {
        if (BODY_PLANS[health->plan].parts[part].role == role) {
            return health_part_efficiency(health, part);
        }
    }
    return 1.0f;
}

/* Sum of the parts' shares of a role, by their efficiency. */
static float role_sum(const Health *health, BodyRole role)
{
    int count = body_part_count((BodyPlan)health->plan);
    float sum = 0.0f;
    for (int part = 0; part < count; part++) {
        const BodyPartDef *def = &BODY_PLANS[health->plan].parts[part];
        if (def->role == role) {
            sum += def->share * health_part_efficiency(health, part);
        }
    }
    return clampf(sum, 0.0f, 1.0f);
}

/* Index of the part with a role (the head), -1 when the plan has none. */
static int role_part(const Health *health, BodyRole role)
{
    int count = body_part_count((BodyPlan)health->plan);
    for (int part = 0; part < count; part++) {
        if (BODY_PLANS[health->plan].parts[part].role == role) {
            return part;
        }
    }
    return -1;
}

float health_consciousness_loss(const Health *health, ConsciousnessSource source)
{
    if (health->death != DEATH_NONE || (unsigned)source >= CONSCIOUSNESS_SOURCE_COUNT) {
        return 0.0f;
    }
    switch (source) {
    case CONSCIOUSNESS_HUNGER:
    case CONSCIOUSNESS_THIRST:
    case CONSCIOUSNESS_COLD:
    case CONSCIOUSNESS_HEAT: {
        float severity = health->deprivation[source];
        return severity > 0.0f ? powf(clampf(severity, 0.0f, 1.0f), HEALTH_DEPRIVATION_CURVE)
                               : 0.0f;
    }
    case CONSCIOUSNESS_BLOOD:
        return clampf((HEALTH_BLOOD_LOSS_FROM - health->blood) /
                          (HEALTH_BLOOD_LOSS_FROM - HEALTH_BLOOD_DEATH),
                      0.0f, 1.0f);
    case CONSCIOUSNESS_HEAD:
        return health->injury_count > 0
                   ? HEALTH_HEAD_LOSS * (1.0f - role_part_efficiency(health, BODY_ROLE_MIND))
                   : 0.0f;
    case CONSCIOUSNESS_PAIN: return HEALTH_PAIN_LOSS * health_pain(health);
    case CONSCIOUSNESS_SOURCE_COUNT: break;
    }
    return 0.0f;
}

/* What the deadly sources take, and the others, and the deadly source taking
 * the most. */
static void sum_losses(const Health *health, float *deadly, float *other,
                       ConsciousnessSource *worst)
{
    *deadly = 0.0f;
    *other = 0.0f;
    *worst = CONSCIOUSNESS_BLOOD;
    float most = -1.0f;
    for (int source = 0; source < CONSCIOUSNESS_SOURCE_COUNT; source++) {
        float loss = health_consciousness_loss(health, (ConsciousnessSource)source);
        if (!SOURCE_DEFS[source].deadly) {
            *other += loss;
            continue;
        }
        *deadly += loss;
        if (loss > most) {
            most = loss;
            *worst = (ConsciousnessSource)source;
        }
    }
}

float health_consciousness(const Health *health)
{
    if (health->death != DEATH_NONE) {
        return 0.0f;
    }
    if (!health_is_hurt(health)) {
        return 1.0f;
    }
    float deadly, other;
    ConsciousnessSource worst;
    sum_losses(health, &deadly, &other, &worst);
    return clampf(1.0f - deadly - other, HEALTH_CONSCIOUS_FLOOR, 1.0f);
}

/* The posture after a change, with margins: one does not fall and get up
 * again from one tick to the next. Dies when the deadly sources take
 * everything. Returns true when it dies. */
static bool settle(Health *health)
{
    if (health->death != DEATH_NONE) {
        return false;
    }
    if (!health_is_hurt(health)) {
        health->posture = POSTURE_UP;
        return false;
    }
    float deadly, other;
    ConsciousnessSource worst;
    sum_losses(health, &deadly, &other, &worst);
    if (deadly >= 1.0f) {
        die(health, SOURCE_DEFS[worst].cause,
            worst == CONSCIOUSNESS_HEAD ? role_part(health, BODY_ROLE_MIND) : -1);
        return true;
    }
    float level = clampf(1.0f - deadly - other, HEALTH_CONSCIOUS_FLOOR, 1.0f);
    HealthPosture posture = (HealthPosture)health->posture;
    if (level < HEALTH_FAINT_BELOW) {
        posture = POSTURE_UNCONSCIOUS;
    } else if (posture == POSTURE_UNCONSCIOUS && level < HEALTH_COME_TO_FROM) {
        /* Still out. */
    } else if (level < HEALTH_COLLAPSE_BELOW ||
               (posture != POSTURE_UP && level < HEALTH_RISE_FROM)) {
        posture = POSTURE_COLLAPSED;
    } else {
        posture = POSTURE_UP;
    }
    health->posture = (uint8_t)posture;
    return false;
}

float health_vigor(const Health *health)
{
    if (health->death != DEATH_NONE) {
        return 0.0f;
    }
    float level = health_consciousness(health);
    float slope = (1.0f - HEALTH_VIGOR_AT_COLLAPSE) / (HEALTH_VIGOR_FULL - HEALTH_COLLAPSE_BELOW);
    return clampf(HEALTH_VIGOR_AT_COLLAPSE + (level - HEALTH_COLLAPSE_BELOW) * slope, 0.0f, 1.0f);
}

float health_ability(const Health *health, HealthAbility ability)
{
    if (health->death != DEATH_NONE || (unsigned)ability >= HEALTH_ABILITY_COUNT) {
        return 0.0f;
    }
    if (!health_is_hurt(health)) {
        return ability == HEALTH_HANDS && role_sum(health, BODY_ROLE_HANDS) <= 0.0f ? 0.0f : 1.0f;
    }
    if (ability == HEALTH_CONSCIOUSNESS) {
        return health_consciousness(health);
    }
    bool out = health->posture == POSTURE_UNCONSCIOUS;
    bool collapsed = health->posture != POSTURE_UP;
    switch (ability) {
    case HEALTH_WALKING:
        return collapsed ? 0.0f : role_sum(health, BODY_ROLE_WALK) * health_vigor(health);
    case HEALTH_HANDS:
        return collapsed ? 0.0f : role_sum(health, BODY_ROLE_HANDS) * health_vigor(health);
    case HEALTH_SIGHT:
        return out ? 0.0f : role_part_efficiency(health, BODY_ROLE_MIND) * health_vigor(health);
    case HEALTH_EATING:
        return out ? 0.0f
                   : 1.0f -
                         HEAD_EATING_LOSS * (1.0f - role_part_efficiency(health, BODY_ROLE_MIND));
    case HEALTH_CONSCIOUSNESS:
    case HEALTH_ABILITY_COUNT: break;
    }
    return 0.0f;
}

float health_limb_ability(const Health *health, BodyLimb limb)
{
    if (health->death != DEATH_NONE || !body_plan_has_limb((BodyPlan)health->plan, limb)) {
        return 0.0f;
    }
    if (!health_is_hurt(health)) {
        return 1.0f;
    }
    if (health->posture != POSTURE_UP) {
        return 0.0f;
    }
    float sum = 0.0f;
    int count = 0;
    for (int part = 0; part < body_part_count((BodyPlan)health->plan); part++) {
        if (BODY_PLANS[health->plan].parts[part].limb == limb) {
            sum += health_part_efficiency(health, part);
            count++;
        }
    }
    return sum / (float)count * health_vigor(health);
}

float health_arm_efficiency(const Health *health, int arm)
{
    if (health->death != DEATH_NONE || health->posture != POSTURE_UP || arm < 0) {
        return 0.0f;
    }
    int seen = 0;
    for (int part = 0; part < body_part_count((BodyPlan)health->plan); part++) {
        if (BODY_PLANS[health->plan].parts[part].role == BODY_ROLE_HANDS && seen++ == arm) {
            return health_part_efficiency(health, part);
        }
    }
    return 0.0f;
}

bool health_is_collapsed(const Health *health)
{
    return health->death == DEATH_NONE && health->posture != POSTURE_UP;
}

bool health_is_unconscious(const Health *health)
{
    return health->death == DEATH_NONE && health->posture == POSTURE_UNCONSCIOUS;
}

int health_sight_reach(const Health *health)
{
    if (!health_is_collapsed(health)) {
        return 0;
    }
    float share = (health_consciousness(health) - HEALTH_FAINT_BELOW) /
                  (HEALTH_COLLAPSE_BELOW - HEALTH_FAINT_BELOW);
    return 1 + (int)(clampf(share, 0.0f, 1.0f) * (float)(HEALTH_SIGHT_REACH_MAX - 1) + 0.5f);
}

bool health_in_danger(const Health *health)
{
    return health->death == DEATH_NONE && health_consciousness(health) < HEALTH_DANGER_BELOW;
}

bool health_is_down(const Health *health)
{
    return health_is_collapsed(health) ||
           (health->death == DEATH_NONE && health->injury_count > 0 &&
            role_sum(health, BODY_ROLE_WALK) < HEALTH_WALKING_MIN);
}

uint64_t health_hash(const Health *health, uint64_t hash)
{
    if (!health_is_hurt(health) && health->death == DEATH_NONE) {
        return hash;
    }
    uint8_t head[5] = {health->plan, health->death, health->death_part, health->injury_count,
                       health->posture};
    hash = hash_fnv1a64(hash, head, sizeof(head));
    hash = hash_fnv1a64(hash, &health->blood, sizeof(health->blood));
    bool deprived = false;
    for (int kind = 0; kind < DEPRIVATION_COUNT; kind++) {
        deprived |= health->deprivation[kind] > 0.0f;
    }
    if (deprived || health->death != DEATH_NONE) {
        hash = hash_fnv1a64(hash, health->deprivation, sizeof(health->deprivation)); /* Likewise. */
    }
    for (int i = 0; i < health->injury_count; i++) {
        const Injury *injury = &health->injuries[i];
        uint8_t kind[2] = {injury->part, injury->type};
        hash = hash_fnv1a64(hash, kind, sizeof(kind));
        hash = hash_fnv1a64(hash, &injury->damage, sizeof(injury->damage));
        hash = hash_fnv1a64(hash, &injury->hours, sizeof(injury->hours));
    }
    return hash;
}

void health_serialize(Serializer *s, Health *health)
{
    ser_u8_below(s, &health->plan, BODY_PLAN_COUNT);
    unsigned parts = (unsigned)body_part_count((BodyPlan)health->plan);
    ser_u8_below(s, &health->death, DEATH_CAUSE_COUNT);
    ser_u8(s, &health->death_part);
    ser_u8_below(s, &health->injury_count, HEALTH_MAX_INJURIES + 1);
    ser_u8_below(s, &health->posture, POSTURE_COUNT);
    ser_float(s, &health->size);
    ser_float(s, &health->damage_factor);
    ser_float(s, &health->pain_factor);
    ser_float(s, &health->blood);
    ser_floats(s, health->deprivation, DEPRIVATION_COUNT);
    for (int i = 0; i < HEALTH_MAX_INJURIES; i++) {
        Injury *injury = &health->injuries[i];
        ser_u8(s, &injury->part);
        ser_u8_below(s, &injury->type, INJURY_TYPE_COUNT);
        ser_float(s, &injury->damage);
        ser_float(s, &injury->hours);
        if (i < health->injury_count) {
            ser_check(s, injury->part < parts);
        }
    }
    ser_u8_below(s, &health->hurt_cause, HURT_CAUSE_COUNT);
    ser_u8_below(s, &health->note_count, HEALTH_NOTES_MAX + 1);
    for (int i = 0; i < HEALTH_NOTES_MAX; i++) {
        ser_u8(s, &health->notes[i].part);
        ser_u8_below(s, &health->notes[i].type, INJURY_TYPE_COUNT);
        ser_u8_below(s, &health->notes[i].cause, HURT_CAUSE_COUNT);
    }
}
