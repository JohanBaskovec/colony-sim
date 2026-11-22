#include "core/weapon.h"

#include "core/tool.h"

static const WeaponDef WEAPON_DEFS[ITEM_TYPE_COUNT] = {
    [ITEM_COBBLE] = {ATTACK_STONE, 1, false},
    [ITEM_FLINT] = {ATTACK_STONE, 1, false},
    [ITEM_FLAKE] = {ATTACK_SLASH, 1, false},
    [ITEM_CHOPPER] = {ATTACK_CHOP, 1, false},
    [ITEM_HAND_AXE] = {ATTACK_AXE_CHOP, 1, false},
    [ITEM_BRANCHES] = {ATTACK_CLUB, 1, false},
    [ITEM_WOOD] = {ATTACK_HEAVY_CLUB, 2, false},
    [ITEM_DIGGING_STICK] = {ATTACK_STAFF, 2, false},
    [ITEM_DIGGING_STICK_HARDENED] = {ATTACK_HARDENED_STAFF, 2, false},
    [ITEM_THROWING_SPEAR] = {ATTACK_LIGHT_SPEAR_THRUST, 1, true},
    [ITEM_THROWING_SPEAR_HARDENED] = {ATTACK_HARDENED_LIGHT_SPEAR_THRUST, 1, true},
    [ITEM_THRUSTING_SPEAR] = {ATTACK_SPEAR_THRUST, 2, true},
    [ITEM_THRUSTING_SPEAR_HARDENED] = {ATTACK_HARDENED_SPEAR_THRUST, 2, true},
    [ITEM_STONE_TIPPED_THROWING_SPEAR] = {ATTACK_STONE_LIGHT_SPEAR_THRUST, 1, true},
    [ITEM_STONE_TIPPED_THRUSTING_SPEAR] = {ATTACK_STONE_SPEAR_THRUST, 2, true},
    [ITEM_HAFTED_AXE] = {ATTACK_HAFTED_AXE_CHOP, 1, false},
};

const WeaponDef *weapon_def(ItemType type)
{
    if (type <= ITEM_NONE || (unsigned)type >= ITEM_TYPE_COUNT ||
        WEAPON_DEFS[type].attack == ATTACK_NONE) {
        return NULL;
    }
    return &WEAPON_DEFS[type];
}

bool item_is_weapon(ItemType type)
{
    return weapon_def(type) != NULL;
}

float weapon_worth(ItemStack weapon)
{
    const WeaponDef *def = weapon_def((ItemType)weapon.type);
    if (def == NULL || weapon.count == 0) {
        return 0.0f;
    }
    return attack_worth((AttackType)def->attack) * tool_quality_factor(weapon) *
           ((float)item_stack_hit_points_percent(weapon) / 100.0f);
}

bool weapon_better(ItemStack a, ItemStack b)
{
    float worth_a = weapon_worth(a), worth_b = weapon_worth(b);
    if (worth_a != worth_b) {
        return worth_a > worth_b;
    }
    if (a.quality != b.quality) {
        return a.quality > b.quality;
    }
    return a.hit_points > b.hit_points;
}
