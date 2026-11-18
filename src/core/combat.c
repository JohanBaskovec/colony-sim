#include "core/combat.h"

#include "core/game_clock.h"
#include "core/i18n.h"
#include "core/util.h"

#include <stdlib.h>

static const AttackDef ATTACK_DEFS[ATTACK_TYPE_COUNT] = {
    [ATTACK_FISTS] = {"fists", N_("attack.fists"), INJURY_BRUISE, 3.0f, 0.7f, 1.0f, 1},
    [ATTACK_SPEAR_THRUST] = {"spear_thrust", N_("attack.spear_thrust"), INJURY_CUT, 9.0f, 0.8f,
                             1.4f, 1},
    [ATTACK_HARDENED_SPEAR_THRUST] = {"hardened_spear_thrust", N_("attack.spear_thrust"),
                                      INJURY_CUT, 11.0f, 0.8f, 1.4f, 1},
    [ATTACK_KICK] = {"kick", N_("attack.kick"), INJURY_BRUISE, 5.0f, 0.5f, 1.5f, 1},
    [ATTACK_TUSKS] = {"tusks", N_("attack.tusks"), INJURY_CUT, 8.0f, 0.65f, 1.4f, 1},
    [ATTACK_WOLF_BITE] = {"wolf_bite", N_("attack.bite"), INJURY_BITE, 8.0f, 0.7f, 1.4f, 1},
    [ATTACK_FOX_BITE] = {"fox_bite", N_("attack.bite"), INJURY_BITE, 3.0f, 0.7f, 1.0f, 1},
};

const AttackDef *attack_def(AttackType type)
{
    if (type <= ATTACK_NONE || (unsigned)type >= ATTACK_TYPE_COUNT) {
        return NULL;
    }
    return &ATTACK_DEFS[type];
}

int attack_interval_ticks(AttackType type)
{
    const AttackDef *def = attack_def(type);
    return def != NULL ? (int)(def->interval * (float)TICKS_PER_SECOND + 0.5f) : 0;
}

bool combat_in_reach(TilePos a, TilePos b, int reach)
{
    return a.z == b.z && abs(a.x - b.x) <= reach && abs(a.y - b.y) <= reach;
}

float combat_dodge(const Health *defender, float agility)
{
    if (health_is_down(defender)) {
        return 0.0f;
    }
    return clampf(agility, 0.0f, 1.0f) * health_ability(defender, HEALTH_WALKING);
}

float combat_hit_chance(AttackType type, float skill, const Health *defender, float agility)
{
    const AttackDef *def = attack_def(type);
    if (def == NULL || health_is_dead(defender)) {
        return 0.0f;
    }
    float chance = def->accuracy * skill * (1.0f - 0.5f * combat_dodge(defender, agility));
    return clampf(chance, COMBAT_HIT_MIN, COMBAT_HIT_MAX);
}

BlowResult combat_blow(AttackType type, float skill, float power, Health *defender, float agility,
                       Rng *rng)
{
    BlowResult result = {false, -1, 0.0f, false};
    const AttackDef *def = attack_def(type);
    if (def == NULL || health_is_dead(defender)) {
        return result;
    }
    if (rng_next_float(rng) >= combat_hit_chance(type, skill, defender, agility)) {
        return result;
    }
    float spread = 1.0f + COMBAT_DAMAGE_SPREAD * (2.0f * rng_next_float(rng) - 1.0f);
    result.part = health_random_part(defender, rng);
    result.damage = health_hurt(defender, result.part, def->injury, def->damage * power * spread);
    result.hit = true;
    result.killed = health_is_dead(defender);
    return result;
}

float combat_hurt_fraction(const Health *health)
{
    float lost = (1.0f - health->blood) / (1.0f - HEALTH_BLOOD_DEATH);
    return clampf(lost > health_pain(health) ? lost : health_pain(health), 0.0f, 1.0f);
}

bool combat_breaks_off(const Health *health, float nerve)
{
    return combat_hurt_fraction(health) >= nerve;
}

bool combat_queue_push(CombatQueue *queue, CombatStrike strike)
{
    if (queue->count >= COMBAT_QUEUE_MAX) {
        return false;
    }
    queue->list[queue->count++] = strike;
    return true;
}
