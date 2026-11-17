#include "core/health.h"

#include "core/i18n.h"
#include "core/util.h"

#include <string.h>

/* Body plans (docs/HEALTH.md §2): hit points for a body of size 1. */
static const BodyPartDef HUMAN_PARTS[] = {
    {N_("body.head"), 25.0f, 0.08f, true, BODY_ROLE_MIND, 1.0f},
    {N_("body.torso"), 40.0f, 0.42f, true, BODY_ROLE_NONE, 0.0f},
    {N_("body.left_arm"), 20.0f, 0.125f, false, BODY_ROLE_HANDS, 0.5f},
    {N_("body.right_arm"), 20.0f, 0.125f, false, BODY_ROLE_HANDS, 0.5f},
    {N_("body.left_leg"), 25.0f, 0.125f, false, BODY_ROLE_WALK, 0.5f},
    {N_("body.right_leg"), 25.0f, 0.125f, false, BODY_ROLE_WALK, 0.5f},
};

static const BodyPartDef QUADRUPED_PARTS[] = {
    {N_("body.head"), 20.0f, 0.08f, true, BODY_ROLE_MIND, 1.0f},
    {N_("body.neck"), 20.0f, 0.07f, true, BODY_ROLE_NONE, 0.0f},
    {N_("body.torso"), 45.0f, 0.45f, true, BODY_ROLE_NONE, 0.0f},
    {N_("body.front_left_leg"), 20.0f, 0.1f, false, BODY_ROLE_WALK, 0.25f},
    {N_("body.front_right_leg"), 20.0f, 0.1f, false, BODY_ROLE_WALK, 0.25f},
    {N_("body.hind_left_leg"), 20.0f, 0.1f, false, BODY_ROLE_WALK, 0.25f},
    {N_("body.hind_right_leg"), 20.0f, 0.1f, false, BODY_ROLE_WALK, 0.25f},
};

static const BodyPartDef HARE_PARTS[] = {
    {N_("body.head"), 20.0f, 0.1f, true, BODY_ROLE_MIND, 1.0f},
    {N_("body.torso"), 35.0f, 0.5f, true, BODY_ROLE_NONE, 0.0f},
    {N_("body.front_left_leg"), 15.0f, 0.1f, false, BODY_ROLE_WALK, 0.2f},
    {N_("body.front_right_leg"), 15.0f, 0.1f, false, BODY_ROLE_WALK, 0.2f},
    {N_("body.hind_left_leg"), 15.0f, 0.1f, false, BODY_ROLE_WALK, 0.3f},
    {N_("body.hind_right_leg"), 15.0f, 0.1f, false, BODY_ROLE_WALK, 0.3f},
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
};
#undef PLAN

_Static_assert(sizeof(QUADRUPED_PARTS) / sizeof(QUADRUPED_PARTS[0]) <= HEALTH_MAX_PARTS,
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

/* Consciousness lost at a destroyed head, and eating. */
#define HEAD_CONSCIOUSNESS_LOSS 0.6f
#define HEAD_EATING_LOSS 0.5f

void health_init(Health *health, BodyPlan plan, float size, float damage_factor, float pain_factor)
{
    memset(health, 0, sizeof(*health));
    health->plan = (uint8_t)((unsigned)plan < BODY_PLAN_COUNT ? plan : BODY_HUMAN);
    health->size = clampf(size, 0.05f, 10.0f);
    health_set_factors(health, damage_factor, pain_factor);
    health->blood = 1.0f;
}

void health_set_factors(Health *health, float damage_factor, float pain_factor)
{
    health->damage_factor = clampf(damage_factor, 0.25f, 4.0f);
    health->pain_factor = clampf(pain_factor, 0.25f, 4.0f);
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
    if (def->vital && health_part_damage(health, part) >= health_part_hit_points(health, part)) {
        die(health, DEATH_VITAL_PART, part);
    }
    return taken;
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
    return false;
}

bool health_is_dead(const Health *health)
{
    return health->death != DEATH_NONE;
}

bool health_is_hurt(const Health *health)
{
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

static float consciousness(const Health *health, float pain)
{
    if (pain >= HEALTH_PAIN_KNOCKDOWN) {
        return 0.0f;
    }
    float blood =
        clampf((health->blood - HEALTH_BLOOD_DEATH) / (HEALTH_BLOOD_FAINT - HEALTH_BLOOD_DEATH),
               0.0f, 1.0f);
    float head =
        1.0f - HEAD_CONSCIOUSNESS_LOSS * (1.0f - role_part_efficiency(health, BODY_ROLE_MIND));
    return blood * head;
}

float health_ability(const Health *health, HealthAbility ability)
{
    if (health->death != DEATH_NONE || (unsigned)ability >= HEALTH_ABILITY_COUNT) {
        return 0.0f;
    }
    if (!health_is_hurt(health)) {
        return ability == HEALTH_HANDS && role_sum(health, BODY_ROLE_HANDS) <= 0.0f ? 0.0f : 1.0f;
    }
    float pain = health_pain(health);
    float awake = consciousness(health, pain);
    if (ability == HEALTH_CONSCIOUSNESS) {
        return awake;
    }
    if (awake < HEALTH_CONSCIOUS_MIN) {
        return 0.0f; /* Knocked down: nothing else works. */
    }
    float slowdown = 1.0f - HEALTH_PAIN_SLOWDOWN * pain;
    switch (ability) {
    case HEALTH_WALKING: return role_sum(health, BODY_ROLE_WALK) * slowdown;
    case HEALTH_HANDS: return role_sum(health, BODY_ROLE_HANDS) * slowdown;
    case HEALTH_SIGHT: return role_part_efficiency(health, BODY_ROLE_MIND);
    case HEALTH_EATING:
        return 1.0f - HEAD_EATING_LOSS * (1.0f - role_part_efficiency(health, BODY_ROLE_MIND));
    case HEALTH_CONSCIOUSNESS:
    case HEALTH_ABILITY_COUNT: break;
    }
    return 0.0f;
}

bool health_is_unconscious(const Health *health)
{
    return health->death == DEATH_NONE &&
           health_ability(health, HEALTH_CONSCIOUSNESS) < HEALTH_CONSCIOUS_MIN;
}

bool health_is_down(const Health *health)
{
    return health_is_unconscious(health) ||
           (health->death == DEATH_NONE &&
            health_ability(health, HEALTH_WALKING) < HEALTH_WALKING_MIN);
}

uint64_t health_hash(const Health *health, uint64_t hash)
{
    if (!health_is_hurt(health) && health->death == DEATH_NONE) {
        return hash;
    }
    uint8_t head[4] = {health->plan, health->death, health->death_part, health->injury_count};
    hash = hash_fnv1a64(hash, head, sizeof(head));
    hash = hash_fnv1a64(hash, &health->blood, sizeof(health->blood));
    for (int i = 0; i < health->injury_count; i++) {
        const Injury *injury = &health->injuries[i];
        uint8_t kind[2] = {injury->part, injury->type};
        hash = hash_fnv1a64(hash, kind, sizeof(kind));
        hash = hash_fnv1a64(hash, &injury->damage, sizeof(injury->damage));
        hash = hash_fnv1a64(hash, &injury->hours, sizeof(injury->hours));
    }
    return hash;
}
