/*
 * Weapons (docs/COMBAT.md §7): which items a settler can strike with, the
 * attack each makes and the hands it takes, and what a weapon is worth, to
 * pick the best. What is soft, light or growing (twigs, grass, plants,
 * food) is no weapon, nor what is too heavy to swing (a stone block).
 */
#ifndef COLONY_CORE_WEAPON_H
#define COLONY_CORE_WEAPON_H

#include "core/combat.h"
#include "core/item.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct WeaponDef {
    uint8_t attack; /* AttackType */
    uint8_t hands;  /* 1 or 2. */
    bool spear;     /* Struck with the spear hunting knowledge's skill. */
} WeaponDef;

/* NULL when the item is no weapon (and for ITEM_NONE, out of range). */
const WeaponDef *weapon_def(ItemType type);

bool item_is_weapon(ItemType type);

/* What a weapon is worth, to pick the best: its attack's worth
 * (attack_worth()) times its quality factor (tool_quality_factor()) and its
 * condition (hit points left, 0..1). 0 for what is no weapon. */
float weapon_worth(ItemStack weapon);

/* Whether `a` is a better weapon than `b`: worth, then quality, then
 * condition. */
bool weapon_better(ItemStack a, ItemStack b);

#endif /* COLONY_CORE_WEAPON_H */
