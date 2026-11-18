#include "core/combat.h"

#include "core/game_clock.h"
#include "core/i18n.h"
#include "core/util.h"

#include <stdlib.h>

static const AttackDef ATTACK_DEFS[ATTACK_TYPE_COUNT] = {
    [ATTACK_FISTS] = {"fists", N_("attack.fists"), INJURY_BRUISE, 3.0f, 0.7f, 1.0f, 1,
                      BODY_LIMB_ARMS, STRIKE_MARK_BLOW},
    [ATTACK_SPEAR_THRUST] = {"spear_thrust", N_("attack.spear_thrust"), INJURY_CUT, 9.0f, 0.8f,
                             1.4f, 1, BODY_LIMB_ARMS, STRIKE_MARK_CUT},
    [ATTACK_HARDENED_SPEAR_THRUST] = {"hardened_spear_thrust", N_("attack.spear_thrust"),
                                      INJURY_CUT, 11.0f, 0.8f, 1.4f, 1, BODY_LIMB_ARMS,
                                      STRIKE_MARK_CUT},
    [ATTACK_KICK] = {"kick", N_("attack.kick"), INJURY_BRUISE, 5.0f, 0.5f, 1.5f, 1,
                     BODY_LIMB_HINDLEGS, STRIKE_MARK_BLOW},
    [ATTACK_HOOF_STRIKE] = {"hoof_strike", N_("attack.hoof_strike"), INJURY_BRUISE, 4.0f, 0.5f,
                            1.5f, 1, BODY_LIMB_FORELEGS, STRIKE_MARK_BLOW},
    [ATTACK_TUSKS] = {"tusks", N_("attack.tusks"), INJURY_CUT, 8.0f, 0.65f, 1.4f, 1, BODY_LIMB_HEAD,
                      STRIKE_MARK_CUT},
    [ATTACK_WOLF_BITE] = {"wolf_bite", N_("attack.bite"), INJURY_BITE, 8.0f, 0.7f, 1.4f, 1,
                          BODY_LIMB_HEAD, STRIKE_MARK_BITE},
    [ATTACK_WOLF_CLAWS] = {"wolf_claws", N_("attack.claws"), INJURY_CUT, 4.0f, 0.6f, 1.2f, 1,
                           BODY_LIMB_FORELEGS, STRIKE_MARK_CLAWS},
    [ATTACK_FOX_BITE] = {"fox_bite", N_("attack.bite"), INJURY_BITE, 3.0f, 0.7f, 1.0f, 1,
                         BODY_LIMB_HEAD, STRIKE_MARK_BITE},
    [ATTACK_FOX_CLAWS] = {"fox_claws", N_("attack.claws"), INJURY_CUT, 1.5f, 0.6f, 1.0f, 1,
                          BODY_LIMB_FORELEGS, STRIKE_MARK_CLAWS},
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

float combat_attack_ability(const Health *attacker, AttackType type)
{
    const AttackDef *def = attack_def(type);
    return def != NULL ? health_limb_ability(attacker, def->limb) : 0.0f;
}

int combat_choice_count(const AttackChoice choices[COMBAT_ATTACKS_MAX])
{
    int count = 0;
    while (count < COMBAT_ATTACKS_MAX && choices[count].attack != ATTACK_NONE) {
        count++;
    }
    return count;
}

bool combat_can_attack(const Health *attacker, const AttackChoice choices[COMBAT_ATTACKS_MAX])
{
    return combat_choice_reach(attacker, choices) > 0;
}

int combat_choice_reach(const Health *attacker, const AttackChoice choices[COMBAT_ATTACKS_MAX])
{
    int reach = 0;
    for (int i = 0; i < combat_choice_count(choices); i++) {
        const AttackDef *def = attack_def((AttackType)choices[i].attack);
        if (def != NULL &&
            combat_attack_ability(attacker, (AttackType)choices[i].attack) >= COMBAT_LIMB_MIN) {
            reach = def->reach > reach ? def->reach : reach;
        }
    }
    return reach;
}

AttackType combat_choose_attack(const Health *attacker,
                                const AttackChoice choices[COMBAT_ATTACKS_MAX], Rng *rng,
                                float *ability)
{
    float weights[COMBAT_ATTACKS_MAX], abilities[COMBAT_ATTACKS_MAX];
    int count = combat_choice_count(choices), usable = 0, last = -1, favorite = -1;
    float total = 0.0f;
    for (int i = 0; i < count; i++) {
        abilities[i] = combat_attack_ability(attacker, (AttackType)choices[i].attack);
        weights[i] = abilities[i] >= COMBAT_LIMB_MIN ? choices[i].weight * abilities[i] : 0.0f;
        if (weights[i] > 0.0f) {
            usable++;
            last = i;
            total += weights[i];
            favorite = favorite < 0 || weights[i] > weights[favorite] ? i : favorite;
        }
    }
    if (usable == 0) {
        return ATTACK_NONE;
    }
    int picked = last;
    if (usable > 1) {
        picked = favorite;
        if (rng != NULL) {
            float roll = rng_next_float(rng) * total;
            for (int i = 0; i < count; i++) {
                if (weights[i] > 0.0f && roll < weights[i]) {
                    picked = i;
                    break;
                }
                roll -= weights[i];
            }
        }
    }
    if (ability != NULL) {
        *ability = abilities[picked];
    }
    return (AttackType)choices[picked].attack;
}

bool combat_in_reach(TilePos a, TilePos b, int reach)
{
    return a.z == b.z && abs(a.x - b.x) <= reach && abs(a.y - b.y) <= reach;
}

bool combat_can_strike(const Map *map, TilePos from, TilePos to, int reach)
{
    if (!combat_in_reach(from, to, reach)) {
        return false;
    }
    int dx = to.x - from.x, dy = to.y - from.y;
    if (map == NULL || abs(dx) != 1 || abs(dy) != 1) {
        return true;
    }
    return map_is_passable(map, from.x + dx, from.y, from.z) &&
           map_is_passable(map, from.x, from.y + dy, from.z);
}

bool combat_is_strike_spot(const Map *map, int x, int y, int z, void *userdata)
{
    const CombatSpot *spot = userdata;
    TilePos tile = {(int16_t)x, (int16_t)y, (int16_t)z};
    return !(x == spot->enemy.x && y == spot->enemy.y && z == spot->enemy.z) &&
           map_is_passable(map, x, y, z) && combat_can_strike(map, tile, spot->enemy, spot->reach);
}

TileBox combat_spot_box(const CombatSpot *spot)
{
    TilePos at = spot->enemy;
    return (TileBox){at.x - spot->reach, at.y - spot->reach, at.z,
                     at.x + spot->reach, at.y + spot->reach, at.z};
}

float combat_dodge(const Health *defender, float agility)
{
    if (health_is_down(defender)) {
        return 0.0f;
    }
    return clampf(agility, 0.0f, 1.0f) * health_ability(defender, HEALTH_WALKING);
}

float combat_aim(float skill, const Health *attacker)
{
    return skill * health_vigor(attacker);
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
    BlowResult result = {false, false, -1, 0.0f, false};
    const AttackDef *def = attack_def(type);
    if (def == NULL || health_is_dead(defender)) {
        return result;
    }
    float roll = rng_next_float(rng);
    if (roll >= combat_hit_chance(type, skill, defender, agility)) {
        /* The same roll against the chance without the dodge: a hit then. */
        result.dodged = roll < clampf(def->accuracy * skill, COMBAT_HIT_MIN, COMBAT_HIT_MAX);
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

void combat_log_push(CombatLog *log, CombatEvent event)
{
    log->list[log->next] = event;
    log->next = (log->next + 1) % COMBAT_LOG_MAX;
    if (log->count < COMBAT_LOG_MAX) {
        log->count++;
    }
}

const CombatEvent *combat_log_recent(const CombatLog *log, int index)
{
    if (index < 0 || index >= log->count) {
        return NULL;
    }
    return &log->list[(log->next - 1 - index + COMBAT_LOG_MAX) % COMBAT_LOG_MAX];
}
