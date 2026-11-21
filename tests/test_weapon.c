/* Weapons: which items strike, how, with how many hands, and which is best
 * (docs/COMBAT.md §7). */
#include "core/weapon.h"

#include "test_framework.h"

static void test_what_is_a_weapon(void)
{
    /* Stones, blades, sticks and spears strike. */
    const ItemType weapons[] = {
        ITEM_COBBLE,   ITEM_FLINT, ITEM_FLAKE,         ITEM_CHOPPER,        ITEM_HAND_AXE,
        ITEM_BRANCHES, ITEM_WOOD,  ITEM_DIGGING_STICK, ITEM_THROWING_SPEAR, ITEM_THRUSTING_SPEAR,
    };
    for (size_t i = 0; i < sizeof(weapons) / sizeof(weapons[0]); i++) {
        const WeaponDef *def = weapon_def(weapons[i]);
        REQUIRE(def != NULL);
        const AttackDef *attack = attack_def((AttackType)def->attack);
        REQUIRE(attack != NULL);
        CHECK_EQ_INT(attack->limb, BODY_LIMB_ARMS);
        CHECK(def->hands == 1 || def->hands == 2);
        CHECK(attack_worth((AttackType)def->attack) > attack_worth(ATTACK_FISTS));
    }
    /* Soft, light, growing or too heavy: no weapon. */
    const ItemType others[] = {
        ITEM_NONE,        ITEM_TWIGS,        ITEM_GRASS,           ITEM_NETTLES,
        ITEM_HAZELNUTS,   ITEM_BLACKBERRIES, ITEM_REEDS,           ITEM_MOSS,
        ITEM_STONE_BLOCK, ITEM_SETTLER_BODY, ITEM_ROE_DEER_CARCASS};
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
        CHECK(!item_is_weapon(others[i]));
    }
    CHECK(!item_is_weapon(ITEM_TYPE_COUNT));
    /* No food and nothing that rots is a weapon. */
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        const ItemDef *def = item_def((ItemType)type);
        if (item_is_weapon((ItemType)type)) {
            CHECK(def->category != ITEM_CATEGORY_FOOD && def->category != ITEM_CATEGORY_BODIES);
            CHECK(def->form == ITEM_FORM_RIGID);
        }
    }
}

static void test_hands_and_spears(void)
{
    CHECK_EQ_INT(weapon_def(ITEM_THRUSTING_SPEAR)->hands, 2);
    CHECK_EQ_INT(weapon_def(ITEM_WOOD)->hands, 2);
    CHECK_EQ_INT(weapon_def(ITEM_HAND_AXE)->hands, 1);
    CHECK_EQ_INT(weapon_def(ITEM_THROWING_SPEAR)->hands, 1);
    CHECK(weapon_def(ITEM_THRUSTING_SPEAR)->spear);
    CHECK(weapon_def(ITEM_THROWING_SPEAR_HARDENED)->spear);
    CHECK(!weapon_def(ITEM_HAND_AXE)->spear);
    /* Fire-hardened: a fifth stronger. */
    const ItemType pairs[][2] = {
        {ITEM_THRUSTING_SPEAR, ITEM_THRUSTING_SPEAR_HARDENED},
        {ITEM_THROWING_SPEAR, ITEM_THROWING_SPEAR_HARDENED},
        {ITEM_DIGGING_STICK, ITEM_DIGGING_STICK_HARDENED},
    };
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        const AttackDef *raw = attack_def((AttackType)weapon_def(pairs[i][0])->attack);
        const AttackDef *hard = attack_def((AttackType)weapon_def(pairs[i][1])->attack);
        CHECK_NEAR(hard->damage, raw->damage * 1.2, 0.3);
        CHECK_EQ_INT(hard->mark, raw->mark);
    }
}

static void test_the_best_weapon(void)
{
    /* The table's order: a thrusting spear, then a hand axe, then a stone. */
    ItemStack spear = item_stack_pristine(ITEM_THRUSTING_SPEAR, 1);
    ItemStack axe = item_stack_pristine(ITEM_HAND_AXE, 1);
    ItemStack stone = item_stack_pristine(ITEM_COBBLE, 1);
    CHECK(weapon_better(spear, axe));
    CHECK(weapon_better(axe, stone));
    CHECK(!weapon_better(stone, axe));
    CHECK(weapon_worth(item_stack_pristine(ITEM_TWIGS, 1)) == 0.0f);
    CHECK(weapon_worth((ItemStack){.type = ITEM_HAND_AXE}) == 0.0f); /* None of it. */
    /* Quality and condition count. */
    ItemStack fine = item_stack_make(ITEM_HAND_AXE, 1, 90, item_def(ITEM_HAND_AXE)->max_hit_points);
    ItemStack crude =
        item_stack_make(ITEM_HAND_AXE, 1, 10, item_def(ITEM_HAND_AXE)->max_hit_points);
    CHECK(weapon_better(fine, crude));
    ItemStack worn = fine;
    worn.hit_points = (uint16_t)(fine.hit_points / 4);
    CHECK(weapon_better(fine, worn));
    CHECK(weapon_worth(worn) < weapon_worth(fine));
    /* A badly worn spear is worth less than a sound stone. */
    ItemStack broken = spear;
    broken.hit_points = 1;
    CHECK(weapon_better(stone, broken));
}

int main(void)
{
    RUN_TEST(test_what_is_a_weapon);
    RUN_TEST(test_hands_and_spears);
    RUN_TEST(test_the_best_weapon);
    return test_summary();
}
