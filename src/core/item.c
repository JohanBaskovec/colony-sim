#include "core/item.h"

#include "core/game_clock.h"
#include "core/i18n.h"
#include "core/map.h"
#include "core/util.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Units are human-sized pieces with realistic weights:
 * - wood: a log about 1 m long and 11 cm thick, 7 kg of fresh wood (~10 l);
 *   a mature tree gives 60 of them (420 kg: trunk and big branches);
 * - stone block: a rough block of about 20 kg dug out of a rock wall (7.5 l
 *   of rock, irregular so it stacks with gaps);
 * - blackberries: a handful of about 300 g (half a liter, ~190 kcal: ripe
 *   wild fruit, generously counted);
 * - hazelnuts: 100 g in their shells (~285 kcal), which keep for months;
 * - mushrooms: 100 g of caps (~30 kcal), more taste than food;
 * - wild garlic, nettles, plantain: a bunch of about 200 g of leaves or young
 *   shoots (~60 kcal for garlic and nettles, ~50 for plantain);
 * - wild carrots: a bunch of thin roots, about 250 g (~100 kcal);
 * - cattail rhizomes: about 250 g of starchy rootstock (~250 kcal, half of it
 *   digested raw); roasted in the embers, most of it (~350 kcal);
 * - roasted carrots: a bunch of wild carrots roasted, softer (~130 kcal);
 * - wood sorrel, wild strawberries, bilberries, rose hips: a bunch of leaves (~40 kcal), a handful
 *   of small fruit (~65 kcal), of bilberries (~170 kcal), of dried hips (~240 kcal: they keep
 *   for two months);
 * - moss (an armful, 0.4 kg), bracken, heather and reeds (bundles of 1 to 1.5 kg, dry): bedding,
 *   thatch and fuel;
 * - cobble: a rounded stone that fits the hand, about 1 kg, picked up from
 *   the ground; tough, the knapper's hammer (its quality is its toughness);
 * - flint: a nodule of about 1 kg, the stone that is knapped (its quality is how
 *   fine and even its grain is);
 * - branches: a dead or cut branch of about 1.5 kg, poorly packed;
 * - twigs: a handful (about 200 g) of kindling;
 * - grass: an armful, 2.5 kg cut fresh, 1 kg once dry (about 12 l either way).
 * Stone blocks never wear out; logs vary with the woodcutter
 * (knots, cracks) and wild food with the gatherer (ripeness, bruises). Fresh grass left on the
 * ground dries in three days. Food lying in the open at 15 °C keeps (rot_days):
 * greens and mushrooms 2 days, blackberries 3, roasted roots 4 (moist and
 * soft), cattail rhizomes 15, wild carrots 20, hazelnuts in their shells 90.
 * Bodies (one to a tile but a hare's, dragged by whoever moves them) rot
 * away in 4 (a hare) to 10 days (a settler, a red deer, a boar).
 * Tools and weapons (docs/KNOWLEDGE.md §6, §8; a plain cobble serves as
 * the knapper's hammer): a chopper (1 kg), a sharp flake (30 g, razor sharp
 * but soon worn out), a hand axe (600 g); a throwing spear (0.8 kg, a long
 * straight branch) and a thrusting spear (1.5 kg, thicker). Fire hardening
 * (docs/KNOWLEDGE.md §5.2) makes spears that last half as long again. The
 * digging stick (1 kg, a stout pointed branch) digs roots up (§7); hardened
 * in the fire, it lasts half as long again too.
 */
#define DEF(id_, key, category_, weight_, bulk_, nutrition_, hp, quality, damage)                  \
    .id = (id_), .name_key = (key), .category = (category_), .weight = (weight_), .bulk = (bulk_), \
    .nutrition = (nutrition_), .max_hit_points = (hp), .has_quality = (quality),                   \
    .damageable = (damage)

static const ItemDef ITEM_DEFS[ITEM_TYPE_COUNT] = {
    /* id, name, category, weight (kg), bulk, nutrition, hit points, quality?, damageable? */
    [ITEM_NONE] = {DEF("none", NULL, ITEM_CATEGORY_COUNT, 0.0f, 0.0f, 0.0f, 0, false, false)},
    [ITEM_WOOD] = {DEF("wood", N_("item.wood"), ITEM_CATEGORY_RESOURCES, 7.0f, 20.0f, 0.0f, 60,
                       true, true),
                   .length_cm = 70, .form = ITEM_FORM_RIGID},
    [ITEM_STONE_BLOCK] = {DEF("stone_block", N_("item.stone_block"), ITEM_CATEGORY_RESOURCES, 20.0f,
                              20.0f, 0.0f, 150, false, false),
                          .length_cm = 40, .form = ITEM_FORM_RIGID},
    [ITEM_BLACKBERRIES] = {DEF("blackberries", N_("item.blackberries"), ITEM_CATEGORY_FOOD, 0.3f,
                               1.0f, 0.05f, 10, true, true),
                           .length_cm = 0, .form = ITEM_FORM_LOOSE, .rot_days = 3.0f},
    /* Damageable: a cobble used as a hammer wears out. */
    [ITEM_COBBLE] = {DEF("cobble", N_("item.cobble"), ITEM_CATEGORY_RESOURCES, 1.0f, 1.5f, 0.0f,
                         200, true, true),
                     .length_cm = 10, .form = ITEM_FORM_RIGID},
    [ITEM_BRANCHES] = {DEF("branches", N_("item.branches"), ITEM_CATEGORY_RESOURCES, 1.5f, 10.0f,
                           0.0f, 20, true, true),
                       .length_cm = 150, .form = ITEM_FORM_RIGID},
    [ITEM_TWIGS] = {DEF("twigs", N_("item.twigs"), ITEM_CATEGORY_RESOURCES, 0.2f, 2.0f, 0.0f, 10,
                        false, true),
                    .length_cm = 40, .form = ITEM_FORM_BUNDLE, .wets_into = ITEM_WET_TWIGS},
    /* Twigs from wet ground, out in the rain or green from a felled crown:
     * heavier with their water, they burn on a fire but do not catch a
     * flame (core/hearth.h), and dry in a day out of the rain. */
    [ITEM_WET_TWIGS] = {DEF("wet_twigs", N_("item.wet_twigs"), ITEM_CATEGORY_RESOURCES, 0.3f, 2.0f,
                            0.0f, 10, false, true),
                        .length_cm = 40, .form = ITEM_FORM_BUNDLE, .transforms_into = ITEM_TWIGS,
                        .transform_hours = 24},
    [ITEM_FRESH_GRASS] = {DEF("fresh_grass", N_("item.fresh_grass"), ITEM_CATEGORY_RESOURCES, 2.5f,
                              25.0f, 0.0f, 10, false, true),
                          .length_cm = 60, .form = ITEM_FORM_BUNDLE, .transforms_into = ITEM_GRASS,
                          .transform_hours = 72},
    [ITEM_GRASS] = {DEF("grass", N_("item.grass"), ITEM_CATEGORY_RESOURCES, 1.0f, 25.0f, 0.0f, 15,
                        false, true),
                    .length_cm = 60, .form = ITEM_FORM_BUNDLE},
    [ITEM_FLINT] = {DEF("flint", N_("item.flint"), ITEM_CATEGORY_RESOURCES, 1.0f, 1.5f, 0.0f, 150,
                        true, false),
                    .length_cm = 12, .form = ITEM_FORM_RIGID},
    [ITEM_CHOPPER] = {DEF("chopper", N_("item.chopper"), ITEM_CATEGORY_TOOLS, 1.0f, 2.0f, 0.0f, 80,
                          true, true),
                      .length_cm = 12, .form = ITEM_FORM_RIGID},
    [ITEM_FLAKE] = {DEF("flake", N_("item.flake"), ITEM_CATEGORY_TOOLS, 0.03f, 0.2f, 0.0f, 15, true,
                        true),
                    .length_cm = 6, .form = ITEM_FORM_RIGID},
    [ITEM_HAND_AXE] = {DEF("hand_axe", N_("item.hand_axe"), ITEM_CATEGORY_TOOLS, 0.6f, 1.5f, 0.0f,
                           120, true, true),
                       .length_cm = 15, .form = ITEM_FORM_RIGID},
    [ITEM_THROWING_SPEAR] = {DEF("throwing_spear", N_("item.throwing_spear"), ITEM_CATEGORY_WEAPONS,
                                 0.8f, 10.0f, 0.0f, 50, true, true),
                             .length_cm = 180, .form = ITEM_FORM_RIGID},
    [ITEM_THRUSTING_SPEAR] = {DEF("thrusting_spear", N_("item.thrusting_spear"),
                                  ITEM_CATEGORY_WEAPONS, 1.5f, 12.0f, 0.0f, 70, true, true),
                              .length_cm = 220, .form = ITEM_FORM_RIGID},
    [ITEM_THROWING_SPEAR_HARDENED] = {DEF("throwing_spear_hardened",
                                          N_("item.throwing_spear_hardened"), ITEM_CATEGORY_WEAPONS,
                                          0.8f, 10.0f, 0.0f, 75, true, true),
                                      .length_cm = 180, .form = ITEM_FORM_RIGID},
    [ITEM_THRUSTING_SPEAR_HARDENED] = {DEF("thrusting_spear_hardened",
                                           N_("item.thrusting_spear_hardened"),
                                           ITEM_CATEGORY_WEAPONS, 1.5f, 12.0f, 0.0f, 105, true,
                                           true),
                                       .length_cm = 220, .form = ITEM_FORM_RIGID},
    [ITEM_HAZELNUTS] = {DEF("hazelnuts", N_("item.hazelnuts"), ITEM_CATEGORY_FOOD, 0.1f, 0.4f,
                            0.075f, 100, true, true),
                        .length_cm = 0, .form = ITEM_FORM_LOOSE, .rot_days = 90.0f},
    [ITEM_MUSHROOMS] = {DEF("mushrooms", N_("item.mushrooms"), ITEM_CATEGORY_FOOD, 0.1f, 0.5f,
                            0.01f, 5, true, true),
                        .length_cm = 8, .form = ITEM_FORM_RIGID, .rot_days = 2.0f},
    [ITEM_WILD_GARLIC] = {DEF("wild_garlic", N_("item.wild_garlic"), ITEM_CATEGORY_FOOD, 0.2f, 1.5f,
                              0.016f, 5, true, true),
                          .length_cm = 25, .form = ITEM_FORM_BUNDLE, .rot_days = 2.0f},
    [ITEM_NETTLES] = {DEF("nettles", N_("item.nettles"), ITEM_CATEGORY_FOOD, 0.2f, 1.5f, 0.016f, 5,
                          true, true),
                      .length_cm = 40, .form = ITEM_FORM_BUNDLE, .rot_days = 2.0f},
    [ITEM_PLANTAIN] = {DEF("plantain", N_("item.plantain"), ITEM_CATEGORY_FOOD, 0.2f, 1.5f, 0.013f,
                           5, true, true),
                       .length_cm = 25, .form = ITEM_FORM_BUNDLE, .rot_days = 2.0f},
    [ITEM_WILD_CARROTS] = {DEF("wild_carrots", N_("item.wild_carrots"), ITEM_CATEGORY_FOOD, 0.25f,
                               1.0f, 0.027f, 30, true, true),
                           .length_cm = 20, .form = ITEM_FORM_RIGID, .rot_days = 20.0f},
    [ITEM_CATTAIL_RHIZOMES] = {DEF("cattail_rhizomes", N_("item.cattail_rhizomes"),
                                   ITEM_CATEGORY_FOOD, 0.25f, 1.0f, 0.05f, 30, true, true),
                               .length_cm = 30, .form = ITEM_FORM_RIGID, .rot_days = 15.0f},
    [ITEM_DIGGING_STICK] = {DEF("digging_stick", N_("item.digging_stick"), ITEM_CATEGORY_TOOLS,
                                1.0f, 6.0f, 0.0f, 40, true, true),
                            .length_cm = 100, .form = ITEM_FORM_RIGID},
    [ITEM_DIGGING_STICK_HARDENED] = {DEF("digging_stick_hardened",
                                         N_("item.digging_stick_hardened"), ITEM_CATEGORY_TOOLS,
                                         1.0f, 6.0f, 0.0f, 60, true, true),
                                     .length_cm = 100, .form = ITEM_FORM_RIGID},
    [ITEM_ROASTED_RHIZOMES] = {DEF("roasted_rhizomes", N_("item.roasted_rhizomes"),
                                   ITEM_CATEGORY_FOOD, 0.25f, 1.0f, 0.07f, 15, true, true),
                               .length_cm = 30, .form = ITEM_FORM_RIGID, .rot_days = 4.0f},
    [ITEM_ROASTED_CARROTS] = {DEF("roasted_carrots", N_("item.roasted_carrots"), ITEM_CATEGORY_FOOD,
                                  0.25f, 1.0f, 0.035f, 15, true, true),
                              .length_cm = 20, .form = ITEM_FORM_RIGID, .rot_days = 4.0f},
    [ITEM_WOOD_SORREL] = {DEF("wood_sorrel", N_("item.wood_sorrel"), ITEM_CATEGORY_FOOD, 0.2f, 1.5f,
                              0.01f, 5, true, true),
                          .length_cm = 15, .form = ITEM_FORM_BUNDLE, .rot_days = 2.0f},
    [ITEM_WILD_STRAWBERRIES] = {DEF("wild_strawberries", N_("item.wild_strawberries"),
                                    ITEM_CATEGORY_FOOD, 0.2f, 0.4f, 0.017f, 5, true, true),
                                .length_cm = 0, .form = ITEM_FORM_LOOSE, .rot_days = 2.0f},
    [ITEM_BILBERRIES] = {DEF("bilberries", N_("item.bilberries"), ITEM_CATEGORY_FOOD, 0.3f, 0.5f,
                             0.045f, 10, true, true),
                         .length_cm = 0, .form = ITEM_FORM_LOOSE, .rot_days = 4.0f},
    /* Dry, with seeds in them: they keep for months. */
    [ITEM_ROSE_HIPS] = {DEF("rose_hips", N_("item.rose_hips"), ITEM_CATEGORY_FOOD, 0.15f, 0.4f,
                            0.06f, 50, true, true),
                        .length_cm = 0, .form = ITEM_FORM_LOOSE, .rot_days = 60.0f},
    [ITEM_MOSS] = {DEF("moss", N_("item.moss"), ITEM_CATEGORY_RESOURCES, 0.4f, 20.0f, 0.0f, 10,
                       false, true),
                   .length_cm = 20, .form = ITEM_FORM_BUNDLE},
    [ITEM_BRACKEN] = {DEF("bracken", N_("item.bracken"), ITEM_CATEGORY_RESOURCES, 1.0f, 25.0f, 0.0f,
                          15, false, true),
                      .length_cm = 100, .form = ITEM_FORM_BUNDLE},
    [ITEM_HEATHER] = {DEF("heather", N_("item.heather"), ITEM_CATEGORY_RESOURCES, 1.5f, 15.0f, 0.0f,
                          20, false, true),
                      .length_cm = 50, .form = ITEM_FORM_BUNDLE},
    [ITEM_REEDS] = {DEF("reeds", N_("item.reeds"), ITEM_CATEGORY_RESOURCES, 1.2f, 20.0f, 0.0f, 20,
                        false, true),
                    .length_cm = 200, .form = ITEM_FORM_BUNDLE},
    [ITEM_SETTLER_BODY] = {DEF("settler_body", N_("item.settler_body"), ITEM_CATEGORY_BODIES, 65.0f,
                               1000.0f, 0.0f, 100, false, true),
                           .length_cm = 170, .form = ITEM_FORM_LIMP, .rot_days = 10.0f},
    [ITEM_ROE_DEER_CARCASS] = {DEF("roe_deer_carcass", N_("item.roe_deer_carcass"),
                                   ITEM_CATEGORY_BODIES, 22.0f, 500.0f, 0.0f, 100, false, true),
                               .length_cm = 120, .form = ITEM_FORM_LIMP, .rot_days = 8.0f},
    [ITEM_RED_DEER_CARCASS] = {DEF("red_deer_carcass", N_("item.red_deer_carcass"),
                                   ITEM_CATEGORY_BODIES, 120.0f, 1000.0f, 0.0f, 100, false, true),
                               .length_cm = 200, .form = ITEM_FORM_LIMP, .rot_days = 10.0f},
    [ITEM_BOAR_CARCASS] = {DEF("boar_carcass", N_("item.boar_carcass"), ITEM_CATEGORY_BODIES, 70.0f,
                               1000.0f, 0.0f, 100, false, true),
                           .length_cm = 140, .form = ITEM_FORM_LIMP, .rot_days = 10.0f},
    [ITEM_WOLF_CARCASS] = {DEF("wolf_carcass", N_("item.wolf_carcass"), ITEM_CATEGORY_BODIES, 35.0f,
                               700.0f, 0.0f, 100, false, true),
                           .length_cm = 130, .form = ITEM_FORM_LIMP, .rot_days = 8.0f},
    [ITEM_FOX_CARCASS] = {DEF("fox_carcass", N_("item.fox_carcass"), ITEM_CATEGORY_BODIES, 6.0f,
                              150.0f, 0.0f, 100, false, true),
                          .length_cm = 100, .form = ITEM_FORM_LIMP, .rot_days = 5.0f},
    [ITEM_HARE_CARCASS] = {DEF("hare_carcass", N_("item.hare_carcass"), ITEM_CATEGORY_BODIES, 3.5f,
                               60.0f, 0.0f, 100, false, true),
                           .length_cm = 60, .form = ITEM_FORM_LIMP, .rot_days = 4.0f},
};

#undef DEF

static const char *const CATEGORY_KEYS[ITEM_CATEGORY_COUNT] = {
    [ITEM_CATEGORY_FOOD] = N_("item.category.food"),
    [ITEM_CATEGORY_RESOURCES] = N_("item.category.resources"),
    [ITEM_CATEGORY_TOOLS] = N_("item.category.tools"),
    [ITEM_CATEGORY_WEAPONS] = N_("item.category.weapons"),
    [ITEM_CATEGORY_BODIES] = N_("item.category.bodies"),
};

const char *item_category_name_key(ItemCategory category)
{
    return (unsigned)category < ITEM_CATEGORY_COUNT ? CATEGORY_KEYS[category] : NULL;
}

ItemTypeMask item_types_all(void)
{
    ItemTypeMask mask = 0;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        mask |= ITEM_TYPE_BIT(type);
    }
    return mask;
}

ItemTypeMask item_types_in_category(ItemCategory category)
{
    ItemTypeMask mask = 0;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        if (ITEM_DEFS[type].category == category) {
            mask |= ITEM_TYPE_BIT(type);
        }
    }
    return mask;
}

const ItemDef *item_def(ItemType type)
{
    return (unsigned)type < ITEM_TYPE_COUNT ? &ITEM_DEFS[type] : NULL;
}

bool item_is_long(ItemType type)
{
    return (unsigned)type < ITEM_TYPE_COUNT && ITEM_DEFS[type].length_cm >= ITEM_LONG_CM;
}

bool item_is_food(ItemType type)
{
    return (unsigned)type < ITEM_TYPE_COUNT && ITEM_DEFS[type].nutrition > 0.0f;
}

ItemTypeMask item_types_food(void)
{
    ItemTypeMask mask = 0;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        if (item_is_food((ItemType)type)) {
            mask |= ITEM_TYPE_BIT(type);
        }
    }
    return mask;
}

bool item_rots(ItemType type)
{
    return (unsigned)type < ITEM_TYPE_COUNT && ITEM_DEFS[type].rot_days > 0.0f;
}

float item_rot_rate(float celsius, bool sheltered)
{
    if (celsius <= ITEM_ROT_FROZEN_CELSIUS) {
        return 0.0f;
    }
    float rate = exp2f((celsius - ITEM_ROT_REFERENCE_CELSIUS) / ITEM_ROT_DOUBLING_CELSIUS);
    if (rate > ITEM_ROT_RATE_MAX) {
        rate = ITEM_ROT_RATE_MAX;
    }
    return sheltered ? rate * ITEM_ROT_SHELTERED : rate;
}

/* Rot units an item of a rotting type loses per hour at `rate`. */
static float rot_units_per_hour(const ItemDef *def, float rate)
{
    return rate * (float)ITEM_ROT_STEP * (float)def->max_hit_points / (def->rot_days * 24.0f);
}

float item_stack_rot_hours(ItemStack stack, float rate)
{
    if (stack.type == ITEM_NONE || !item_rots((ItemType)stack.type) || rate <= 0.0f) {
        return -1.0f;
    }
    float left = (float)stack.hit_points * (float)ITEM_ROT_STEP - (float)stack.progress;
    return left / rot_units_per_hour(&ITEM_DEFS[stack.type], rate);
}

int item_stack_limit(ItemType type)
{
    if (type == ITEM_NONE || (unsigned)type >= ITEM_TYPE_COUNT) {
        return 0;
    }
    float limit = ITEM_TILE_BULK / ITEM_DEFS[type].bulk;
    return limit < 1.0f ? 1 : (limit > (float)UINT16_MAX ? UINT16_MAX : (int)limit);
}

float item_stack_weight(ItemType type, int count)
{
    const ItemDef *def = item_def(type);
    return def != NULL ? def->weight * (float)count : 0.0f;
}

float item_stack_mass(ItemStack stack)
{
    const ItemDef *def = item_def((ItemType)stack.type);
    if (def == NULL || stack.type == ITEM_NONE) {
        return 0.0f;
    }
    float whole = def->weight * (float)stack.count;
    return whole * (1.0f - (float)stack.eaten / (float)ITEM_EATEN_FULL);
}

static bool is_item_type(ItemType type)
{
    return type != ITEM_NONE && (unsigned)type < ITEM_TYPE_COUNT;
}

ItemStack item_stack_make(ItemType type, int count, int quality, int hit_points)
{
    if (!is_item_type(type) || count <= 0) {
        return (ItemStack){.type = ITEM_NONE};
    }
    const ItemDef *def = &ITEM_DEFS[type];
    if (count > UINT16_MAX) {
        count = UINT16_MAX;
    }
    return (ItemStack){
        .type = (uint8_t)type,
        .quality =
            (uint8_t)(def->has_quality ? clampi(quality, ITEM_QUALITY_WORST, ITEM_QUALITY_BEST)
                                       : ITEM_QUALITY_BEST),
        .count = (uint16_t)count,
        .hit_points = (uint16_t)(def->damageable ? clampi(hit_points, 1, def->max_hit_points)
                                                 : def->max_hit_points),
    };
}

/* Transformation or rot progress made valid for a type: 0 when it neither
 * transforms nor rots. */
static uint16_t valid_progress(ItemType type, int progress)
{
    const ItemDef *def = &ITEM_DEFS[type];
    if (def->rot_days > 0.0f) {
        return (uint16_t)clampi(progress, 0, ITEM_ROT_STEP - 1);
    }
    if (def->transforms_into == ITEM_NONE) {
        return 0;
    }
    return (uint16_t)clampi(progress, 0, def->transform_hours);
}

ItemStack item_stack_pristine(ItemType type, int count)
{
    const ItemDef *def = item_def(type);
    return item_stack_make(type, count, ITEM_QUALITY_BEST, def != NULL ? def->max_hit_points : 0);
}

float item_stack_eat(ItemStack *stack, float kg)
{
    const ItemDef *def = item_def((ItemType)stack->type);
    if (def == NULL || stack->type == ITEM_NONE || stack->count == 0 || kg <= 0.0f) {
        return 0.0f;
    }
    float whole = def->weight * (float)stack->count;
    float left = item_stack_mass(*stack);
    if (kg >= left) {
        *stack = (ItemStack){.type = ITEM_NONE};
        return left;
    }
    int steps = (int)lroundf(kg / whole * (float)ITEM_EATEN_FULL);
    steps = steps > 1 ? steps : 1;
    int eaten = (int)stack->eaten + steps;
    if (eaten >= ITEM_EATEN_FULL) {
        *stack = (ItemStack){.type = ITEM_NONE};
        return left;
    }
    stack->eaten = (uint16_t)eaten;
    return kg;
}

bool item_stacks_match(ItemStack a, ItemStack b)
{
    return a.type == b.type && a.corpse == b.corpse && (a.forbidden != 0) == (b.forbidden != 0);
}

int item_hit_points_at_percent(ItemType type, int percent)
{
    if (!is_item_type(type)) {
        return 0;
    }
    int max = ITEM_DEFS[type].max_hit_points;
    int hit_points = (max * clampi(percent, 0, 100) + 50) / 100;
    return clampi(hit_points, 1, max);
}

int item_hit_points_lasting(ItemType type, float hours)
{
    if (!is_item_type(type)) {
        return 0;
    }
    int max = ITEM_DEFS[type].max_hit_points;
    if (!item_rots(type)) {
        return max;
    }
    float hit_points = ceilf(hours * (float)max / (ITEM_DEFS[type].rot_days * 24.0f));
    return hit_points < 1.0f ? 1 : (hit_points > (float)max ? max : (int)hit_points);
}

int item_stack_hit_points_percent(ItemStack stack)
{
    if (!is_item_type((ItemType)stack.type) || stack.count == 0) {
        return 0;
    }
    return (int)stack.hit_points * 100 / ITEM_DEFS[stack.type].max_hit_points;
}

/* Average of two values weighted by item counts, rounded to the nearest:
 * always between the two values. */
static int weighted_average(int a, int count_a, int b, int count_b)
{
    long total = (long)count_a + count_b;
    return (int)(((long)a * count_a + (long)b * count_b + total / 2) / total);
}

bool item_stack_merge(ItemStack *into, ItemStack from)
{
    if (from.type == ITEM_NONE || from.count == 0) {
        return true;
    }
    if (into->type == ITEM_NONE || into->count == 0) {
        *into = from;
        return true;
    }
    if (!item_stacks_match(*into, from)) {
        return false;
    }
    int count = (int)into->count + (int)from.count;
    if (count > UINT16_MAX) {
        count = UINT16_MAX;
    }
    into->quality = (uint8_t)weighted_average(into->quality, into->count, from.quality, from.count);
    into->hit_points =
        (uint16_t)weighted_average(into->hit_points, into->count, from.hit_points, from.count);
    into->progress =
        (uint16_t)weighted_average(into->progress, into->count, from.progress, from.count);
    into->eaten = (uint16_t)weighted_average(into->eaten, into->count, from.eaten, from.count);
    into->count = (uint16_t)count;
    return true;
}

/* ---- The pool of items ---- */

#define ITEM_POOL_FIRST_CAPACITY 1024u

static Item *slot_item(const Items *items, uint32_t slot_plus_one)
{
    return slot_plus_one != 0 ? &items->pool[slot_plus_one - 1] : NULL;
}

/* A free slot (slot + 1), 0 when out of memory. */
static uint32_t alloc_slot(Items *items)
{
    if (items->free_slots != 0) {
        uint32_t slot = items->free_slots;
        items->free_slots = items->pool[slot - 1].next;
        return slot;
    }
    if (items->pool_used == items->pool_capacity) {
        uint32_t capacity =
            items->pool_capacity != 0 ? items->pool_capacity * 2u : ITEM_POOL_FIRST_CAPACITY;
        if (capacity <= items->pool_capacity || capacity > UINT32_MAX / 2u) {
            return 0;
        }
        Item *pool = realloc(items->pool, (size_t)capacity * sizeof(*pool));
        if (pool == NULL) {
            return 0;
        }
        items->pool = pool;
        items->pool_capacity = capacity;
    }
    uint32_t slot = ++items->pool_used;
    items->pool[slot - 1].generation = 0;
    return slot;
}

static void free_slot(Items *items, uint32_t slot)
{
    Item *item = &items->pool[slot - 1];
    uint16_t generation = (uint16_t)(item->generation + 1u);
    *item = (Item){.generation = generation, .next = items->free_slots};
    items->free_slots = slot;
}

/* A well mixed 32-bit value of two (splitmix64's finalizer). */
static uint32_t mix(uint64_t a, uint64_t b)
{
    uint64_t z = a * 0x9E3779B97F4A7C15u + b + 0x632BE59BD9B4E5B9u;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9u;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBu;
    return (uint32_t)((z ^ (z >> 31)) >> 16);
}

/* Spots tried for an item dropped on a tile with few items. */
#define ITEM_SPOT_CANDIDATES 4
/* With more items than this on the tile, an item takes any spot (a heap). */
#define ITEM_SPOT_CROWD 12
/* Spots stay this far from the edge of the tile, in 1/256 of a tile. */
#define ITEM_SPOT_MARGIN 40

/* Chooses where on its tile a new item lies, and how it is turned: from a
 * hash of the tile and the stamp clock, never from a random stream, so that
 * the way items look never changes what happens. With few items there, the
 * candidate spot farthest from them wins. */
static void place_item(const Items *items, Item *item, size_t index, uint32_t salt)
{
    uint32_t seed = mix(index, ((uint64_t)items->stamp_clock << 20) ^ salt);
    item->angle = (uint8_t)seed;
    const int span = 256 - 2 * ITEM_SPOT_MARGIN;
    int crowd = 0;
    for (const Item *other = slot_item(items, items->heads[index]); other != NULL;
         other = slot_item(items, other->next)) {
        crowd++;
    }
    int candidates = crowd == 0 || crowd > ITEM_SPOT_CROWD ? 1 : ITEM_SPOT_CANDIDATES;
    long best = -1;
    for (int c = 0; c < candidates; c++) {
        uint32_t h = mix(seed, (uint64_t)c + 1u);
        int sx = ITEM_SPOT_MARGIN + (int)(h & 0xFFFFu) % span;
        int sy = ITEM_SPOT_MARGIN + (int)(h >> 16) % span;
        long nearest = LONG_MAX;
        if (candidates > 1) {
            for (const Item *other = slot_item(items, items->heads[index]); other != NULL;
                 other = slot_item(items, other->next)) {
                long dx = sx - other->spot_x;
                long dy = sy - other->spot_y;
                long distance = dx * dx + dy * dy;
                nearest = distance < nearest ? distance : nearest;
            }
        }
        if (nearest > best) {
            best = nearest;
            item->spot_x = (uint8_t)sx;
            item->spot_y = (uint8_t)sy;
        }
    }
}

/* ---- Tiles ---- */

static bool in_bounds(const Items *items, int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < items->width && y < items->height && z < items->depth;
}

static size_t index_of(const Items *items, int x, int y, int z)
{
    return ((size_t)z * (size_t)items->height + (size_t)y) * (size_t)items->width + (size_t)x;
}

static size_t level_of(const Items *items, size_t index)
{
    return index / ((size_t)items->width * (size_t)items->height);
}

/* Whether an item belongs to the same stack as the summary: same type, same
 * dead one, forbidden or not alike. */
static bool item_matches(const Item *item, const ItemStack *stack)
{
    return item->type == stack->type && item->corpse == stack->corpse &&
           (item->forbidden != 0) == (stack->forbidden != 0);
}

/* Counts an item lying on its tile in (sign 1) or out (sign -1) of the
 * totals. */
static void count_item(Items *items, const Item *item, int sign)
{
    items->totals[item->type] += sign;
    items->item_count += sign;
}

/* The items of the tile at `index` that belong with `key` (same type, same
 * dead one, forbidden or not alike) as a stack: their count and average condition. An
 * empty stack when there are none. */
static ItemStack group_of(const Items *items, size_t index, ItemStack key)
{
    long count = 0, quality = 0, hit_points = 0, progress = 0, eaten = 0;
    for (const Item *item = slot_item(items, items->heads[index]); item != NULL;
         item = slot_item(items, item->next)) {
        if (item_matches(item, &key)) {
            count++;
            quality += item->quality;
            hit_points += item->hit_points;
            progress += item->progress;
            eaten += item->eaten;
        }
    }
    if (count == 0) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return (ItemStack){.type = key.type,
                       .corpse = key.corpse,
                       .forbidden = key.forbidden != 0,
                       .count = (uint16_t)(count < UINT16_MAX ? count : UINT16_MAX),
                       .quality = (uint8_t)((quality + count / 2) / count),
                       .hit_points = (uint16_t)((hit_points + count / 2) / count),
                       .progress = (uint16_t)((progress + count / 2) / count),
                       .eaten = (uint16_t)((eaten + count / 2) / count)};
}

/* An item as the key of its group. */
static ItemStack key_of(const Item *item)
{
    return (ItemStack){
        .type = item->type, .corpse = item->corpse, .forbidden = item->forbidden != 0, .count = 1};
}

/* What the items of the tile at `index` are forbidden as (Items.forbidden). */
static uint8_t tile_forbidden(const Items *items, size_t index)
{
    bool some = false, all = true;
    for (const Item *item = slot_item(items, items->heads[index]); item != NULL;
         item = slot_item(items, item->next)) {
        some |= item->forbidden != 0;
        all &= item->forbidden != 0;
    }
    return !some ? 0 : all ? ITEM_TILE_ALL_FORBIDDEN : ITEM_TILE_SOME_FORBIDDEN;
}

/* Rebuilds the summary of a tile from its items, whether it holds other
 * groups, whether they are forbidden, and the count of tiles with items on
 * its level. */
static void refresh_tile(Items *items, size_t index)
{
    ItemStack *stack = &items->stacks[index];
    bool had = stack->type != ITEM_NONE;
    const Item *head = slot_item(items, items->heads[index]);
    bool mixed = false;
    if (head == NULL) {
        *stack = (ItemStack){.type = ITEM_NONE};
    } else {
        *stack = group_of(items, index, key_of(head));
        for (const Item *item = head; item != NULL && !mixed; item = slot_item(items, item->next)) {
            mixed = !item_matches(item, stack);
        }
    }
    items->mixed[index] = mixed;
    bool has = stack->type != ITEM_NONE;
    if (had != has) {
        items->level_stacks[level_of(items, index)] += has ? 1u : (uint32_t)-1;
    }
    /* Forbidden items share no group with allowed ones: an unmixed tile is
     * forbidden as its summary is. */
    uint8_t forbidden =
        !mixed ? (stack->forbidden ? ITEM_TILE_ALL_FORBIDDEN : 0) : tile_forbidden(items, index);
    items->forbidden_count += (forbidden != 0) - (items->forbidden[index] != 0);
    items->forbidden[index] = forbidden;
}

/* The newest group of the tile at `index` among `types`, only of allowed
 * items when `allowed` (an empty stack for none). */
static ItemStack newest_of(const Items *items, size_t index, ItemTypeMask types, bool allowed)
{
    const ItemStack *stack = &items->stacks[index];
    if (stack->type == ITEM_NONE) {
        return (ItemStack){.type = ITEM_NONE};
    }
    if ((types & ITEM_TYPE_BIT(stack->type)) && !(allowed && stack->forbidden)) {
        return *stack;
    }
    if (!items->mixed[index]) {
        return (ItemStack){.type = ITEM_NONE};
    }
    for (const Item *item = slot_item(items, items->heads[index]); item != NULL;
         item = slot_item(items, item->next)) {
        if ((types & ITEM_TYPE_BIT(item->type)) && !(allowed && item->forbidden)) {
            return group_of(items, index, key_of(item));
        }
    }
    return (ItemStack){.type = ITEM_NONE};
}

/* The group of the tile at `index` that `key` belongs with (an empty stack
 * for none). */
static ItemStack matching_group(const Items *items, size_t index, ItemStack key)
{
    const ItemStack *stack = &items->stacks[index];
    if (stack->type != ITEM_NONE && item_stacks_match(*stack, key)) {
        return *stack;
    }
    return items->mixed[index] ? group_of(items, index, key) : (ItemStack){.type = ITEM_NONE};
}

/* Takes the item `slot` off the tile it lies on (`previous` is the item
 * before it in the list, 0 when it is the first) and frees it. */
static void unlink_item(Items *items, size_t index, uint32_t previous, uint32_t slot)
{
    Item *item = &items->pool[slot - 1];
    if (previous == 0) {
        items->heads[index] = item->next;
    } else {
        items->pool[previous - 1].next = item->next;
    }
    count_item(items, item, -1);
    free_slot(items, slot);
}

/* Frees every item of a tile. */
static void clear_tile(Items *items, size_t index)
{
    while (items->heads[index] != 0) {
        unlink_item(items, index, 0, items->heads[index]);
    }
}

void items_copy_tile(Items *dst, const Items *src, size_t index)
{
    if (dst->stamps[index] == src->stamps[index]) {
        return;
    }
    clear_tile(dst, index);
    /* Same order as in src: append behind the last copied item. */
    uint32_t last = 0;
    for (const Item *item = items_first(src, index); item != NULL; item = items_next(src, item)) {
        uint32_t slot = alloc_slot(dst);
        if (slot == 0) {
            break; /* Out of memory: the picture misses items. */
        }
        Item *copy = &dst->pool[slot - 1];
        uint16_t generation = copy->generation;
        *copy = *item;
        copy->generation = generation;
        copy->next = 0;
        if (last == 0) {
            dst->heads[index] = slot;
        } else {
            dst->pool[last - 1].next = slot;
        }
        last = slot;
        count_item(dst, copy, 1);
    }
    refresh_tile(dst, index);
    dst->stamps[index] = src->stamps[index];
}

bool items_init(Items *items, int width, int height, int depth)
{
    memset(items, 0, sizeof(*items));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    size_t tiles = (size_t)width * (size_t)height * (size_t)depth;
    items->stacks = calloc(tiles, sizeof(*items->stacks));
    items->level_stacks = calloc((size_t)depth, sizeof(*items->level_stacks));
    items->forbidden = calloc(tiles, 1);
    items->heads = calloc(tiles, sizeof(*items->heads));
    items->stamps = calloc(tiles, sizeof(*items->stamps));
    items->mixed = calloc(tiles, 1);
    if (items->stacks == NULL || items->level_stacks == NULL || items->forbidden == NULL ||
        items->heads == NULL || items->stamps == NULL || items->mixed == NULL) {
        items_free(items);
        return false;
    }
    items->width = width;
    items->height = height;
    items->depth = depth;
    return true;
}

void items_free(Items *items)
{
    free(items->stacks);
    free(items->level_stacks);
    free(items->forbidden);
    free(items->pool);
    free(items->heads);
    free(items->stamps);
    free(items->mixed);
    memset(items, 0, sizeof(*items));
}

ItemStack items_get(const Items *items, int x, int y, int z)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return items->stacks[index_of(items, x, y, z)];
}

ItemStack items_get_allowed(const Items *items, int x, int y, int z)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    size_t index = index_of(items, x, y, z);
    if (items->forbidden_count == 0 || items->forbidden[index] == 0) {
        return items->stacks[index];
    }
    return newest_of(items, index, item_types_all(), true);
}

ItemStack items_get_of(const Items *items, int x, int y, int z, ItemTypeMask types)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return newest_of(items, index_of(items, x, y, z), types, false);
}

ItemStack items_get_allowed_of(const Items *items, int x, int y, int z, ItemTypeMask types)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return newest_of(items, index_of(items, x, y, z), types, true);
}

ItemStack items_get_group(const Items *items, int x, int y, int z, ItemStack group)
{
    if (!in_bounds(items, x, y, z) || group.type == ITEM_NONE) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return matching_group(items, index_of(items, x, y, z), group);
}

bool items_mixed(const Items *items, int x, int y, int z)
{
    return in_bounds(items, x, y, z) && items->mixed[index_of(items, x, y, z)] != 0;
}

int items_groups(const Items *items, int x, int y, int z, ItemStack *out, int capacity)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    if (items->stacks[index].type == ITEM_NONE) {
        return 0;
    }
    if (!items->mixed[index] || capacity <= 1) {
        if (capacity > 0) {
            out[0] = items->stacks[index];
        }
        return capacity > 0 ? 1 : 0;
    }
    /* One pass: each item added to its group's sums (groups beyond
     * `capacity` are left out). */
    enum { MOST = 16 };
    long sums[MOST][5] = {{0}};
    capacity = capacity < MOST ? capacity : MOST;
    int count = 0;
    for (const Item *item = items_first(items, index); item != NULL;
         item = items_next(items, item)) {
        int group = 0;
        while (group < count && !item_matches(item, &out[group])) {
            group++;
        }
        if (group == count) {
            if (count == capacity) {
                continue;
            }
            out[count++] = key_of(item);
        }
        long *sum = sums[group];
        sum[0]++;
        sum[1] += item->quality;
        sum[2] += item->hit_points;
        sum[3] += item->progress;
        sum[4] += item->eaten;
    }
    for (int group = 0; group < count; group++) {
        const long *sum = sums[group];
        long n = sum[0];
        out[group].count = (uint16_t)(n < UINT16_MAX ? n : UINT16_MAX);
        out[group].quality = (uint8_t)((sum[1] + n / 2) / n);
        out[group].hit_points = (uint16_t)((sum[2] + n / 2) / n);
        out[group].progress = (uint16_t)((sum[3] + n / 2) / n);
        out[group].eaten = (uint16_t)((sum[4] + n / 2) / n);
    }
    return count;
}

const Item *items_first(const Items *items, size_t index)
{
    return slot_item(items, items->heads[index]);
}

const Item *items_next(const Items *items, const Item *item)
{
    return slot_item(items, item->next);
}

int items_count_at(const Items *items, int x, int y, int z, ItemType type)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    int count = 0;
    for (const Item *item = items_first(items, index_of(items, x, y, z)); item != NULL;
         item = items_next(items, item)) {
        count += type == ITEM_NONE || item->type == type;
    }
    return count;
}

bool items_forbidden(const Items *items, int x, int y, int z)
{
    return items->forbidden_count > 0 && in_bounds(items, x, y, z) &&
           items->forbidden[index_of(items, x, y, z)] == ITEM_TILE_ALL_FORBIDDEN;
}

bool items_any_forbidden(const Items *items, int x, int y, int z)
{
    return items->forbidden_count > 0 && in_bounds(items, x, y, z) &&
           items->forbidden[index_of(items, x, y, z)] != 0;
}

/* Notes that the items of a tile (an index) changed: a new stamp, a summary
 * rebuilt (and whether they are forbidden), and copied into the picture at
 * once where it is in view. */
static void note_change(Items *items, size_t index)
{
    refresh_tile(items, index);
    items->stamp_clock++; /* Also salts the spots of the next items. */
    items->stamps[index] = items->picture ? UINT32_MAX : items->stamp_clock;
    if (items->mirror != NULL && (items->mirror_when[index >> 6] >> (index & 63) & 1u) != 0) {
        items_copy_tile(items->mirror, items, index);
    }
}

bool items_set_forbidden(Items *items, int x, int y, int z, bool forbidden)
{
    if (!in_bounds(items, x, y, z)) {
        return false;
    }
    size_t index = index_of(items, x, y, z);
    bool changed = false;
    for (uint32_t slot = items->heads[index]; slot != 0; slot = items->pool[slot - 1].next) {
        Item *item = &items->pool[slot - 1];
        changed |= (item->forbidden != 0) != forbidden;
        item->forbidden = forbidden;
    }
    if (!changed) {
        return false;
    }
    note_change(items, index);
    if (!forbidden) {
        items->additions++; /* Something to store again. */
    }
    return true;
}

bool items_set_item_forbidden(Items *items, ItemId id, bool forbidden)
{
    const Item *found = items_by_id(items, id);
    if (found == NULL || (found->forbidden != 0) == forbidden) {
        return false;
    }
    items->pool[id.slot].forbidden = forbidden;
    note_change(items, found->tile);
    if (!forbidden) {
        items->additions++; /* Something to store again. */
    }
    return true;
}

/* A stack made valid for its type, as items_add() keeps it: condition
 * clamped, progress, eaten and corpse only where the type has them; never a
 * load. */
static ItemStack valid_batch(ItemStack batch)
{
    ItemType type = (ItemType)batch.type;
    if (!is_item_type(type) || batch.count == 0) {
        return (ItemStack){.type = ITEM_NONE};
    }
    uint16_t progress = valid_progress(type, batch.progress);
    int eaten = ITEM_DEFS[type].category == ITEM_CATEGORY_BODIES ? batch.eaten : 0;
    uint16_t corpse = ITEM_DEFS[type].category == ITEM_CATEGORY_BODIES ? batch.corpse : 0;
    ItemStack valid = item_stack_make(type, batch.count, batch.quality, batch.hit_points);
    valid.progress = progress;
    valid.eaten = (uint16_t)clampi(eaten, 0, ITEM_EATEN_FULL - 1);
    valid.corpse = corpse;
    valid.forbidden = batch.forbidden != 0;
    return valid;
}

/* How many items of a stack the tile at `index` takes: the type's stack
 * limit, less what lies there already (items of other types too, by their
 * bulk). May be negative. */
static int tile_room(const Items *items, size_t index, const ItemStack *batch)
{
    float other_bulk = 0.0f;
    int own = 0;
    for (const Item *item = items_first(items, index); item != NULL;
         item = items_next(items, item)) {
        if (item_matches(item, batch)) {
            own++;
        } else {
            other_bulk += ITEM_DEFS[item->type].bulk;
        }
    }
    ItemType type = (ItemType)batch->type;
    return item_stack_limit(type) - own - (int)ceilf(other_bulk / ITEM_DEFS[type].bulk);
}

int items_room(const Items *items, int x, int y, int z, ItemStack batch)
{
    if (!in_bounds(items, x, y, z) || !is_item_type((ItemType)batch.type)) {
        return 0;
    }
    int room = tile_room(items, index_of(items, x, y, z), &batch);
    return room > 0 ? room : 0;
}

/* A new item of a valid stack's condition, not linked anywhere yet: its
 * slot + 1, 0 when out of memory. */
static uint32_t new_item(Items *items, const ItemStack *batch, uint32_t tile)
{
    uint32_t slot = alloc_slot(items);
    if (slot != 0) {
        Item *item = &items->pool[slot - 1];
        *item = (Item){.type = batch->type,
                       .quality = batch->quality,
                       .hit_points = batch->hit_points,
                       .progress = batch->progress,
                       .eaten = batch->eaten,
                       .corpse = batch->corpse,
                       .forbidden = batch->forbidden,
                       .generation = item->generation,
                       .tile = tile};
    }
    return slot;
}

/* Links an item at the head of a tile's list, at a spot of its own. */
static void lay_item(Items *items, size_t index, uint32_t slot, uint32_t salt)
{
    Item *item = &items->pool[slot - 1];
    item->tile = (uint32_t)index;
    place_item(items, item, index, salt);
    item->next = items->heads[index];
    items->heads[index] = slot;
    count_item(items, item, 1);
}

int items_add(Items *items, int x, int y, int z, ItemStack batch)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    /* Callers may build stacks by hand: keep the condition valid for the type. */
    batch = valid_batch(batch);
    if (batch.type == ITEM_NONE) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    int room = tile_room(items, index, &batch);
    int added = (int)batch.count < room ? (int)batch.count : room;
    for (int i = 0; i < added; i++) {
        uint32_t slot = new_item(items, &batch, (uint32_t)index);
        if (slot == 0) {
            added = i; /* Out of memory: the rest is not added. */
            break;
        }
        lay_item(items, index, slot, (uint32_t)i);
    }
    if (added <= 0) {
        return 0;
    }
    items->additions++;
    note_change(items, index);
    return added;
}

/* ---- Loads ---- */

/* Rebuilds the summary of a load from its items. */
static void refresh_load(const Items *items, ItemStack *load)
{
    const Item *head = slot_item(items, load->held);
    if (head == NULL) {
        *load = (ItemStack){.type = ITEM_NONE};
        return;
    }
    long count = 0, quality = 0, hit_points = 0, progress = 0, eaten = 0;
    for (const Item *item = head; item != NULL; item = slot_item(items, item->next)) {
        count++;
        quality += item->quality;
        hit_points += item->hit_points;
        progress += item->progress;
        eaten += item->eaten;
    }
    *load = (ItemStack){.type = head->type,
                        .corpse = head->corpse,
                        .forbidden = head->forbidden != 0,
                        .count = (uint16_t)(count < UINT16_MAX ? count : UINT16_MAX),
                        .quality = (uint8_t)((quality + count / 2) / count),
                        .hit_points = (uint16_t)((hit_points + count / 2) / count),
                        .progress = (uint16_t)((progress + count / 2) / count),
                        .eaten = (uint16_t)((eaten + count / 2) / count),
                        .held = load->held};
}

/* Turns a load that is only numbers into items of its condition. */
static void materialize(Items *items, ItemStack *load)
{
    if (load->held != 0 || load->count == 0 || load->type == ITEM_NONE) {
        return;
    }
    ItemStack batch = valid_batch(*load);
    for (int i = 0; batch.type != ITEM_NONE && i < (int)batch.count; i++) {
        uint32_t slot = new_item(items, &batch, ITEM_TILE_HELD);
        if (slot == 0) {
            break; /* Out of memory: a smaller load. */
        }
        items->pool[slot - 1].next = load->held;
        load->held = slot;
        items->held_count++;
    }
    refresh_load(items, load);
}

/* Whether a load holds something (its type then decides what it takes). */
static bool load_held(const ItemStack *load)
{
    return load->type != ITEM_NONE && load->count > 0;
}

/* Picks up to `count` items of the group `stack` (the newest first) off the
 * tile at `index` into `load`. */
static int pick_up_group(Items *items, size_t index, ItemStack stack, int count, ItemStack *load)
{
    if (stack.type == ITEM_NONE || count <= 0 ||
        (load_held(load) && !item_stacks_match(*load, stack))) {
        return 0;
    }
    materialize(items, load);
    int taken = 0;
    uint32_t previous = 0;
    uint32_t slot = items->heads[index];
    while (slot != 0 && taken < count) {
        Item *item = &items->pool[slot - 1];
        uint32_t next = item->next;
        if (item_matches(item, &stack)) {
            if (previous == 0) {
                items->heads[index] = next;
            } else {
                items->pool[previous - 1].next = next;
            }
            count_item(items, item, -1);
            item->tile = ITEM_TILE_HELD;
            item->next = load->held;
            load->held = slot;
            items->held_count++;
            taken++;
        } else {
            previous = slot;
        }
        slot = next;
    }
    if (taken > 0) {
        note_change(items, index);
        refresh_load(items, load);
    }
    return taken;
}

int items_pick_up(Items *items, int x, int y, int z, int count, ItemStack *load)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    ItemStack stack = load_held(load) ? matching_group(items, index, *load) : items->stacks[index];
    return pick_up_group(items, index, stack, count, load);
}

int items_pick_up_of(Items *items, int x, int y, int z, ItemTypeMask types, int count,
                     ItemStack *load)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    ItemStack stack = !load_held(load)                      ? newest_of(items, index, types, true)
                      : (types & ITEM_TYPE_BIT(load->type)) ? matching_group(items, index, *load)
                                                            : (ItemStack){.type = ITEM_NONE};
    return pick_up_group(items, index, stack, count, load);
}

int items_pick_up_group(Items *items, int x, int y, int z, ItemStack group, int count,
                        ItemStack *load)
{
    if (!in_bounds(items, x, y, z) || group.type == ITEM_NONE) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    return pick_up_group(items, index, matching_group(items, index, group), count, load);
}

int items_pick_up_item(Items *items, ItemId id, ItemStack *load)
{
    const Item *found = items_by_id(items, id);
    if (found == NULL || (load_held(load) && !item_stacks_match(*load, item_as_stack(found)))) {
        return 0;
    }
    materialize(items, load);
    size_t index = found->tile;
    uint32_t slot = id.slot + 1, previous = 0;
    for (uint32_t at = items->heads[index]; at != slot; at = items->pool[at - 1].next) {
        previous = at;
    }
    Item *item = &items->pool[id.slot];
    if (previous == 0) {
        items->heads[index] = item->next;
    } else {
        items->pool[previous - 1].next = item->next;
    }
    count_item(items, item, -1);
    item->tile = ITEM_TILE_HELD;
    item->next = load->held;
    load->held = slot;
    items->held_count++;
    note_change(items, index);
    refresh_load(items, load);
    return 1;
}

void items_load_set_forbidden(Items *items, ItemStack *load, bool forbidden)
{
    if (!load_held(load)) {
        return;
    }
    materialize(items, load);
    for (uint32_t slot = load->held; slot != 0; slot = items->pool[slot - 1].next) {
        items->pool[slot - 1].forbidden = forbidden;
    }
    refresh_load(items, load);
}

static bool chosen(const Item *item, const ItemStack *stack, ItemChoice choice)
{
    return item_matches(item, stack) && item->quality >= choice.min_quality &&
           item->quality <= choice.max_quality;
}

int items_count_chosen(const Items *items, int x, int y, int z, ItemType type, ItemChoice choice)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    ItemStack stack = newest_of(items, index, ITEM_TYPE_BIT(type), true);
    if (stack.type == ITEM_NONE) {
        return 0;
    }
    int count = 0;
    for (const Item *item = items_first(items, index); item != NULL;
         item = items_next(items, item)) {
        count += chosen(item, &stack, choice);
    }
    return count;
}

float items_soonest_rot_hours(const Items *items, int x, int y, int z, ItemType type,
                              ItemChoice choice)
{
    if (!in_bounds(items, x, y, z) || !item_rots(type)) {
        return -1.0f;
    }
    size_t index = index_of(items, x, y, z);
    ItemStack stack = newest_of(items, index, ITEM_TYPE_BIT(type), true);
    if (stack.type == ITEM_NONE) {
        return -1.0f;
    }
    float soonest = -1.0f;
    for (const Item *item = items_first(items, index); item != NULL;
         item = items_next(items, item)) {
        if (chosen(item, &stack, choice)) {
            float hours = item_stack_rot_hours(item_as_stack(item), 1.0f);
            soonest = soonest < 0.0f || hours < soonest ? hours : soonest;
        }
    }
    return soonest;
}

ItemType items_soonest_rotting(const Items *items, int x, int y, int z, ItemTypeMask types,
                               float *hours)
{
    ItemType type = ITEM_NONE;
    float soonest = -1.0f;
    if (in_bounds(items, x, y, z)) {
        size_t index = index_of(items, x, y, z);
        for (const Item *item = items_first(items, index); item != NULL;
             item = items_next(items, item)) {
            if (item->corpse != 0 || item->forbidden || !(types & ITEM_TYPE_BIT(item->type)) ||
                !item_rots((ItemType)item->type)) {
                continue;
            }
            float left = item_stack_rot_hours(item_as_stack(item), 1.0f);
            if (type == ITEM_NONE || left < soonest) { /* Ties: the newest. */
                type = (ItemType)item->type;
                soonest = left;
            }
        }
    }
    if (hours != NULL) {
        *hours = soonest;
    }
    return type;
}

int items_pick_up_chosen(Items *items, int x, int y, int z, ItemType type, int count,
                         ItemChoice choice, ItemStack *load)
{
    if (choice.min_quality <= ITEM_QUALITY_WORST && choice.max_quality >= ITEM_QUALITY_BEST &&
        !choice.soonest_rot) {
        return items_pick_up_of(items, x, y, z, ITEM_TYPE_BIT(type), count, load);
    }
    if (!in_bounds(items, x, y, z) || count <= 0) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    const ItemStack stack = load_held(load) ? matching_group(items, index, *load)
                                            : newest_of(items, index, ITEM_TYPE_BIT(type), true);
    if (stack.type != type || (load_held(load) && !item_stacks_match(*load, stack))) {
        return 0;
    }
    materialize(items, load);
    int taken = 0;
    while (taken < count) {
        uint32_t best = 0, best_previous = 0, previous = 0;
        long best_left = 0;
        for (uint32_t slot = items->heads[index]; slot != 0;
             previous = slot, slot = items->pool[slot - 1].next) {
            const Item *item = &items->pool[slot - 1];
            if (!chosen(item, &stack, choice)) {
                continue;
            }
            long left = item_rot_left(item);
            if (best == 0 || (choice.soonest_rot && left < best_left)) {
                best = slot;
                best_previous = previous;
                best_left = left;
            }
            if (!choice.soonest_rot) {
                break;
            }
        }
        if (best == 0) {
            break;
        }
        Item *item = &items->pool[best - 1];
        if (best_previous == 0) {
            items->heads[index] = item->next;
        } else {
            items->pool[best_previous - 1].next = item->next;
        }
        count_item(items, item, -1);
        item->tile = ITEM_TILE_HELD;
        item->next = load->held;
        load->held = best;
        items->held_count++;
        taken++;
    }
    if (taken > 0) {
        note_change(items, index);
        refresh_load(items, load);
    }
    return taken;
}

int items_put(Items *items, int x, int y, int z, ItemStack *load)
{
    if (!in_bounds(items, x, y, z) || load->type == ITEM_NONE || load->count == 0) {
        return 0;
    }
    materialize(items, load);
    if (load->held == 0) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    int room = tile_room(items, index, load);
    int put = 0;
    while (put < room && load->held != 0) {
        uint32_t slot = load->held;
        load->held = items->pool[slot - 1].next;
        items->held_count--;
        lay_item(items, index, slot, (uint32_t)put);
        put++;
    }
    if (put == 0) {
        return 0;
    }
    refresh_load(items, load);
    items->additions++;
    note_change(items, index);
    return put;
}

void items_use_up(Items *items, ItemStack *load, int count)
{
    materialize(items, load);
    for (int i = 0; i < count && load->held != 0; i++) {
        uint32_t slot = load->held;
        load->held = items->pool[slot - 1].next;
        items->held_count--;
        free_slot(items, slot);
    }
    refresh_load(items, load);
}

int items_load_move(Items *items, ItemStack *from, ItemStack *to, int count)
{
    if (count <= 0 || !load_held(from) || (load_held(to) && !item_stacks_match(*to, *from))) {
        return 0;
    }
    materialize(items, from);
    if (load_held(to)) {
        materialize(items, to);
    }
    int moved = 0;
    while (moved < count && from->held != 0) {
        uint32_t slot = from->held;
        from->held = items->pool[slot - 1].next;
        items->pool[slot - 1].next = to->held;
        to->held = slot;
        moved++;
    }
    refresh_load(items, from);
    refresh_load(items, to);
    return moved;
}

int items_load_lowest_quality(const Items *items, ItemStack load)
{
    if (!load_held(&load)) {
        return ITEM_QUALITY_WORST;
    }
    if (load.held == 0) {
        return load.quality;
    }
    int lowest = ITEM_QUALITY_BEST;
    for (const Item *item = slot_item(items, load.held); item != NULL;
         item = slot_item(items, item->next)) {
        lowest = item->quality < lowest ? item->quality : lowest;
    }
    return lowest;
}

const Item *items_load_first(const Items *items, ItemStack load)
{
    return slot_item(items, load.held);
}

/* Takes up to `count` items of the group `stack` off the tile at `index`:
 * gone. Returns what was taken. */
static ItemStack take_group(Items *items, size_t index, const ItemStack stack, int count)
{
    if (stack.type == ITEM_NONE || count <= 0) {
        return (ItemStack){.type = ITEM_NONE};
    }
    long taken = 0, quality = 0, hit_points = 0, progress = 0, eaten = 0;
    uint32_t previous = 0;
    uint32_t slot = items->heads[index];
    while (slot != 0 && taken < count) {
        const Item *item = &items->pool[slot - 1];
        uint32_t next = item->next;
        if (item_matches(item, &stack)) {
            taken++;
            quality += item->quality;
            hit_points += item->hit_points;
            progress += item->progress;
            eaten += item->eaten;
            unlink_item(items, index, previous, slot);
        } else {
            previous = slot;
        }
        slot = next;
    }
    if (taken == 0) {
        return (ItemStack){.type = ITEM_NONE};
    }
    note_change(items, index);
    ItemStack result = stack;
    result.count = (uint16_t)taken;
    result.quality = (uint8_t)((quality + taken / 2) / taken);
    result.hit_points = (uint16_t)((hit_points + taken / 2) / taken);
    result.progress = (uint16_t)((progress + taken / 2) / taken);
    result.eaten = (uint16_t)((eaten + taken / 2) / taken);
    return result;
}

ItemStack items_take(Items *items, int x, int y, int z, int count)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    size_t index = index_of(items, x, y, z);
    return take_group(items, index, items->stacks[index], count);
}

int items_remove(Items *items, int x, int y, int z, int count)
{
    return items_take(items, x, y, z, count).count;
}

ItemStack items_take_of(Items *items, int x, int y, int z, ItemTypeMask types, int count)
{
    if (!in_bounds(items, x, y, z)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    size_t index = index_of(items, x, y, z);
    return take_group(items, index, newest_of(items, index, types, false), count);
}

ItemStack items_take_group(Items *items, int x, int y, int z, ItemStack group, int count)
{
    if (!in_bounds(items, x, y, z) || group.type == ITEM_NONE) {
        return (ItemStack){.type = ITEM_NONE};
    }
    size_t index = index_of(items, x, y, z);
    return take_group(items, index, matching_group(items, index, group), count);
}

int items_remove_of(Items *items, int x, int y, int z, ItemTypeMask types, int count)
{
    return items_take_of(items, x, y, z, types, count).count;
}

void items_clear(Items *items, int x, int y, int z)
{
    if (!in_bounds(items, x, y, z)) {
        return;
    }
    size_t index = index_of(items, x, y, z);
    while (items->stacks[index].type != ITEM_NONE) {
        take_group(items, index, items->stacks[index], UINT16_MAX);
    }
}

/* ---- Items one by one ---- */

float item_drawn_size(ItemType type)
{
    float bulk = is_item_type(type) ? ITEM_DEFS[type].bulk : 1.0f;
    float size = 0.4f + 0.1f * log2f(1.0f + bulk);
    return size < 0.4f ? 0.4f : size > 1.0f ? 1.0f : size;
}

ItemId items_id(const Items *items, const Item *item)
{
    if (item == NULL || item < items->pool || item >= items->pool + items->pool_used) {
        return ITEM_ID_NONE;
    }
    return (ItemId){(uint32_t)(item - items->pool), item->generation};
}

const Item *items_by_id(const Items *items, ItemId id)
{
    if (id.slot >= items->pool_used) {
        return NULL;
    }
    const Item *item = &items->pool[id.slot];
    return item->type != ITEM_NONE && item->generation == id.generation &&
                   item->tile != ITEM_TILE_HELD
               ? item
               : NULL;
}

ItemId items_at_point(const Items *items, float x, float y, int z)
{
    int tx = (int)floorf(x), ty = (int)floorf(y);
    ItemId found = ITEM_ID_NONE;
    float nearest = 0.0f;
    /* The tile under the point first (its items are drawn over those of
     * the tiles before it), then its neighbors. */
    static const int DX[9] = {0, -1, 0, 1, -1, 1, -1, 0, 1};
    static const int DY[9] = {0, -1, -1, -1, 0, 0, 1, 1, 1};
    for (int n = 0; n < 9 && found.slot == UINT32_MAX; n++) {
        int cx = tx + DX[n], cy = ty + DY[n];
        if (!in_bounds(items, cx, cy, z)) {
            continue;
        }
        for (const Item *item = items_first(items, index_of(items, cx, cy, z)); item != NULL;
             item = items_next(items, item)) {
            float ix = (float)cx + ((float)item->spot_x + 0.5f) / 256.0f;
            float iy = (float)cy + ((float)item->spot_y + 0.5f) / 256.0f;
            float radius = item_drawn_size((ItemType)item->type) * 0.5f;
            float d = (ix - x) * (ix - x) + (iy - y) * (iy - y);
            if (d <= radius * radius && (found.slot == UINT32_MAX || d < nearest)) {
                found = items_id(items, item);
                nearest = d;
            }
        }
    }
    return found;
}

ItemId items_find_like(const Items *items, int x, int y, int z, const Item *like)
{
    if (like == NULL || !in_bounds(items, x, y, z)) {
        return ITEM_ID_NONE;
    }
    for (const Item *item = items_first(items, index_of(items, x, y, z)); item != NULL;
         item = items_next(items, item)) {
        if (item->type == like->type && item->spot_x == like->spot_x &&
            item->spot_y == like->spot_y && item->angle == like->angle) {
            return items_id(items, item);
        }
    }
    return ITEM_ID_NONE;
}

bool items_move(Items *items, ItemId id, int x, int y, int z, int spot_x, int spot_y)
{
    const Item *found = items_by_id(items, id);
    if (found == NULL || !in_bounds(items, x, y, z)) {
        return false;
    }
    uint32_t slot = id.slot + 1;
    size_t from = found->tile;
    uint32_t previous = 0;
    for (uint32_t at = items->heads[from]; at != slot; at = items->pool[at - 1].next) {
        previous = at;
    }
    Item *item = &items->pool[slot - 1];
    count_item(items, item, -1);
    if (previous == 0) {
        items->heads[from] = item->next;
    } else {
        items->pool[previous - 1].next = item->next;
    }
    note_change(items, from);
    size_t to = index_of(items, x, y, z);
    item->tile = (uint32_t)to;
    item->spot_x = (uint8_t)clampi(spot_x, 0, 255);
    item->spot_y = (uint8_t)clampi(spot_y, 0, 255);
    item->next = items->heads[to];
    items->heads[to] = slot;
    count_item(items, item, 1);
    items->additions++;
    note_change(items, to);
    return true;
}

bool items_turn(Items *items, ItemId id, int steps)
{
    if (items_by_id(items, id) == NULL) {
        return false;
    }
    Item *item = &items->pool[id.slot];
    item->angle = (uint8_t)((int)item->angle + steps);
    note_change(items, item->tile);
    return true;
}

ItemStack item_as_stack(const Item *item)
{
    return (ItemStack){.type = item->type,
                       .quality = item->quality,
                       .count = 1,
                       .hit_points = item->hit_points,
                       .progress = item->progress,
                       .eaten = item->eaten,
                       .corpse = item->corpse,
                       .forbidden = item->forbidden != 0};
}

long item_rot_left(const Item *item)
{
    if (!item_rots((ItemType)item->type)) {
        return -1;
    }
    return (long)item->hit_points * ITEM_ROT_STEP - (long)item->progress;
}

/* Whether `a` comes before `b` in an order (not for ties). */
static bool comes_before(const Item *a, const Item *b, ItemOrder order)
{
    switch (order) {
    case ITEM_ORDER_QUALITY: return a->quality > b->quality;
    case ITEM_ORDER_CONDITION:
        return item_stack_hit_points_percent(item_as_stack(a)) >
               item_stack_hit_points_percent(item_as_stack(b));
    case ITEM_ORDER_ROT: {
        long left_a = item_rot_left(a), left_b = item_rot_left(b);
        return left_a >= 0 && (left_b < 0 || left_a < left_b);
    }
    case ITEM_ORDER_NEWEST:
    case ITEM_ORDER_COUNT: break;
    }
    return false;
}

int items_list(const Items *items, size_t index, ItemOrder order, const Item **out, int capacity)
{
    int count = 0, kept = 0;
    for (const Item *item = items_first(items, index); item != NULL;
         item = items_next(items, item)) {
        count++;
        /* Insertion into the kept ones, after those it does not come before
         * (the list is newest first: ties keep that order). */
        int at = kept;
        while (at > 0 && comes_before(item, out[at - 1], order)) {
            at--;
        }
        if (at >= capacity) {
            continue;
        }
        int last = kept < capacity ? kept : capacity - 1;
        for (int i = last; i > at; i--) {
            out[i] = out[i - 1];
        }
        out[at] = item;
        kept += kept < capacity;
    }
    return count;
}

/* ---- Natural items ---- */

int items_add_natural(Items *items, int x, int y, int z, ItemType type, int count)
{
    if (!in_bounds(items, x, y, z) || !is_item_type(type) || count <= 0) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    ItemStack key = {.type = type, .count = 1};
    int room = tile_room(items, index, &key);
    count = count < room ? count : room;
    int added = 0;
    for (; added < count; added++) {
        /* Quality: the mean of two draws, most items middling; condition 60 to 100 %. */
        uint32_t h = mix(index, ((uint64_t)items->stamp_clock << 20) ^ (uint64_t)added ^ 0xA5A5u);
        int quality = (int)((h & 0xFFu) % 101u + ((h >> 8) & 0xFFu) % 101u) / 2;
        int percent = 60 + (int)((h >> 16) % 41u);
        ItemStack batch =
            item_stack_make(type, 1, quality, item_hit_points_at_percent(type, percent));
        uint32_t slot = new_item(items, &batch, (uint32_t)index);
        if (slot == 0) {
            break;
        }
        lay_item(items, index, slot, (uint32_t)added);
    }
    if (added > 0) {
        items->additions++;
        note_change(items, index);
    }
    return added;
}

int items_count_type(const Items *items, int x, int y, int z, ItemType type)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    int count = 0;
    for (const Item *item = items_first(items, index_of(items, x, y, z)); item != NULL;
         item = items_next(items, item)) {
        count += item->type == type && item->corpse == 0;
    }
    return count;
}

/* Puts the items of a load down around (x, y) as items_drop_load() does, on
 * the tiles `filter` accepts (all when NULL); what finds no room stays in the
 * load. Returns how many found room. */
static int spread_load_where(Items *items, const Map *map, int x, int y, int z, ItemStack *load,
                             ItemDropFilter filter)
{
    enum { SIDE = 2 * ITEM_DROP_RADIUS + 1 };
    static const int DX[4] = {0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1, 0};
    int placed = 0;
    if (map_is_passable(map, x, y, z)) {
        /* Breadth-first search over passable tiles, so that items never land
         * behind a wall; cells are indexed relative to (x, y). */
        bool visited[SIDE * SIDE] = {false};
        int queue[SIDE * SIDE];
        int head = 0, tail = 0;
        queue[tail++] = ITEM_DROP_RADIUS * SIDE + ITEM_DROP_RADIUS;
        visited[queue[0]] = true;
        while (head < tail && load->count > 0) {
            int cell = queue[head++];
            int cx = x + cell % SIDE - ITEM_DROP_RADIUS;
            int cy = y + cell / SIDE - ITEM_DROP_RADIUS;
            if (!items_any_forbidden(items, cx, cy, z) && /* Left alone. */
                (filter == NULL || filter(items->drop_data, items, cx, cy, z, load))) {
                placed += items_put(items, cx, cy, z, load);
            }
            for (int i = 0; i < 4; i++) {
                int rx = cell % SIDE + DX[i];
                int ry = cell / SIDE + DY[i];
                if (rx < 0 || ry < 0 || rx >= SIDE || ry >= SIDE || visited[ry * SIDE + rx]) {
                    continue;
                }
                visited[ry * SIDE + rx] = true;
                if (map_is_passable(map, x + rx - ITEM_DROP_RADIUS, y + ry - ITEM_DROP_RADIUS, z)) {
                    queue[tail++] = ry * SIDE + rx;
                }
            }
        }
    }
    return placed;
}

/* spread_load_where() with the drop filter first, then (what found no room
 * on the tiles it accepts) anywhere. */
static int spread_load(Items *items, const Map *map, int x, int y, int z, ItemStack *load)
{
    int placed = spread_load_where(items, map, x, y, z, load, items->drop_filter);
    if (items->drop_filter != NULL && load->count > 0) {
        placed += spread_load_where(items, map, x, y, z, load, NULL);
    }
    return placed;
}

void items_set_drop_filter(Items *items, ItemDropFilter filter, const void *data)
{
    items->drop_filter = filter;
    items->drop_data = data;
}

int items_drop_load(Items *items, const Map *map, int x, int y, int z, ItemStack *load)
{
    int placed = spread_load(items, map, x, y, z, load);
    items_use_up(items, load, load->count); /* No room: lost. */
    return placed;
}

int items_lift_all(Items *items, int x, int y, int z, ItemStack *loads, int capacity)
{
    if (!in_bounds(items, x, y, z)) {
        return 0;
    }
    size_t index = index_of(items, x, y, z);
    int count = 0;
    while (items->stacks[index].type != ITEM_NONE && count < capacity) {
        loads[count] = (ItemStack){.type = ITEM_NONE};
        if (pick_up_group(items, index, items->stacks[index], UINT16_MAX, &loads[count]) == 0) {
            break;
        }
        count++;
    }
    return count;
}

int items_move_all(Items *items, const Map *map, int x, int y, int z, int to_x, int to_y, int to_z,
                   bool keep)
{
    /* Every group taken off first (the newest first), then each put down in
     * turn: what lands back on the tile is not taken again. */
    ItemStack loads[ITEM_LIFT_MAX];
    int load_count = items_lift_all(items, x, y, z, loads, ITEM_LIFT_MAX), moved = 0;
    for (int i = 0; i < load_count; i++) {
        moved += spread_load(items, map, to_x, to_y, to_z, &loads[i]);
    }
    for (int i = 0; i < load_count; i++) { /* No room: back where it lay, or lost. */
        if (keep) {
            items_put(items, x, y, z, &loads[i]);
        }
        items_use_up(items, &loads[i], loads[i].count);
    }
    return moved;
}

int items_drop(Items *items, const Map *map, int x, int y, int z, ItemStack batch)
{
    batch.held = 0; /* Only numbers. */
    return items_drop_load(items, map, x, y, z, &batch);
}

/* Turns an item that has reached its transformation into the new type (same
 * hit points in percent, quality kept when both types have one). */
static void transform(Items *items, Item *item, ItemType to)
{
    ItemType from = (ItemType)item->type;
    ItemStack old = {.type = item->type, .count = 1, .hit_points = item->hit_points};
    int percent = item_stack_hit_points_percent(old);
    int quality = ITEM_DEFS[from].has_quality ? item->quality : ITEM_QUALITY_BEST;
    ItemStack made = item_stack_make(to, 1, quality, item_hit_points_at_percent(to, percent));
    item->type = made.type;
    item->quality = made.quality;
    item->hit_points = made.hit_points;
    item->progress = 0;
    item->eaten = 0;
    item->corpse = 0;
    items->totals[from]--;
    items->totals[to]++;
}

/* What `hours` do to one item in the conditions of its tile. */
typedef enum ItemFate {
    ITEM_FATE_SAME,
    ITEM_FATE_CHANGED,
    ITEM_FATE_TRANSFORMED,
    ITEM_FATE_GONE
} ItemFate;

static ItemFate advance_item(Items *items, Item *item, int hours, ItemConditions here)
{
    const ItemDef *def = &ITEM_DEFS[item->type];
    if (def->rot_days > 0.0f) {
        if (here.rot_rate <= 0.0f) {
            return ITEM_FATE_SAME;
        }
        float units = rot_units_per_hour(def, here.rot_rate) * (float)hours;
        long total = (long)item->progress + lroundf(units);
        long lost = total / ITEM_ROT_STEP;
        if (lost >= item->hit_points) {
            return ITEM_FATE_GONE;
        }
        item->hit_points = (uint16_t)(item->hit_points - lost);
        item->progress = (uint16_t)(total % ITEM_ROT_STEP);
        return ITEM_FATE_CHANGED;
    }
    if (!here.dries) { /* Out in the rain: what gets wet does. */
        if (def->wets_into != ITEM_NONE) {
            transform(items, item, def->wets_into);
            return ITEM_FATE_TRANSFORMED;
        }
        return ITEM_FATE_SAME;
    }
    if (def->transforms_into == ITEM_NONE) {
        return ITEM_FATE_SAME;
    }
    int progress = (int)item->progress + hours;
    if (progress >= def->transform_hours) {
        transform(items, item, def->transforms_into);
        return ITEM_FATE_TRANSFORMED;
    }
    item->progress = (uint16_t)progress;
    return ITEM_FATE_CHANGED;
}

static bool type_changes(ItemType type)
{
    const ItemDef *def = &ITEM_DEFS[type];
    return def->rot_days > 0.0f || def->transforms_into != ITEM_NONE || def->wets_into != ITEM_NONE;
}

int items_advance(Items *items, size_t begin, size_t end, int hours, ItemConditionsFn conditions,
                  const void *context)
{
    size_t count = (size_t)items->width * (size_t)items->height * (size_t)items->depth;
    if (end > count) {
        end = count;
    }
    if (hours <= 0) {
        return 0;
    }
    int transformed = 0;
    const size_t level_size = (size_t)items->width * (size_t)items->height;
    for (size_t i = begin; i < end; i++) {
        if (items->level_stacks[i / level_size] == 0) {
            i = (i / level_size + 1) * level_size - 1; /* Nothing on this level. */
            continue;
        }
        if (items->heads[i] == 0) {
            continue;
        }
        bool asked = false, changed = false, counted = false, renewed = false;
        ItemConditions here = {1.0f, true};
        uint32_t previous = 0;
        uint32_t slot = items->heads[i];
        while (slot != 0) {
            Item *item = &items->pool[slot - 1];
            uint32_t next = item->next;
            if (!type_changes((ItemType)item->type)) {
                previous = slot;
                slot = next;
                continue;
            }
            if (!asked) {
                here = conditions != NULL ? conditions(context, i) : here;
                asked = true;
            }
            ItemFate fate = advance_item(items, item, hours, here);
            if (fate == ITEM_FATE_GONE) {
                items->rotted[item->type]++;
                unlink_item(items, i, previous, slot);
            } else {
                previous = slot;
            }
            if (fate != ITEM_FATE_SAME) {
                changed = true;
            }
            renewed |= fate == ITEM_FATE_TRANSFORMED;
            if ((fate == ITEM_FATE_GONE || fate == ITEM_FATE_TRANSFORMED) && !counted) {
                transformed++; /* One per tile. */
                counted = true;
            }
            slot = next;
        }
        if (renewed) {
            items->additions++; /* Something new to store: haulers look again. */
        }
        if (changed) {
            note_change(items, i);
        }
    }
    return transformed;
}

/* Notes a problem found by items_check() (the first one is described). */
static void found(ItemsCheck *check, const char *what, long tile)
{
    if (check->problems++ == 0) {
        check->first = what;
        check->first_tile = tile;
    }
}

ItemsCheck items_check(const Items *items)
{
    ItemsCheck check = {0, NULL, -1};
    size_t level_size = (size_t)items->width * (size_t)items->height;
    long totals[ITEM_TYPE_COUNT] = {0};
    long listed = 0;
    int forbidden = 0;
    for (int z = 0; z < items->depth; z++) {
        uint32_t stacks = 0;
        for (size_t i = (size_t)z * level_size; i < (size_t)(z + 1) * level_size; i++) {
            long steps = 0;
            for (const Item *item = items_first(items, i); item != NULL;
                 item = items_next(items, item)) {
                if (++steps > (long)items->pool_used) {
                    found(&check, "a tile's list loops", (long)i);
                    break;
                }
                if (item->type == ITEM_NONE || (unsigned)item->type >= ITEM_TYPE_COUNT) {
                    found(&check, "a free or invalid item lies on a tile", (long)i);
                    continue;
                }
                if (item->tile != (uint32_t)i) {
                    found(&check, "an item lies on another tile than its own", (long)i);
                }
                totals[item->type]++;
                listed++;
            }
            /* The summary and the mixed mark, as refresh_tile() makes them. */
            const Item *head = items_first(items, i);
            ItemStack expected =
                head != NULL ? group_of(items, i, key_of(head)) : (ItemStack){.type = ITEM_NONE};
            const ItemStack *stack = &items->stacks[i];
            if (stack->type != expected.type || stack->count != expected.count ||
                stack->corpse != expected.corpse || stack->forbidden != expected.forbidden ||
                stack->quality != expected.quality || stack->hit_points != expected.hit_points ||
                stack->progress != expected.progress || stack->eaten != expected.eaten ||
                stack->held != 0) {
                found(&check, "a tile's summary is not its newest group", (long)i);
            }
            bool mixed = false;
            for (const Item *item = head; item != NULL && !mixed; item = items_next(items, item)) {
                mixed = !item_matches(item, &expected);
            }
            if (mixed != (items->mixed[i] != 0)) {
                found(&check, "a tile's mixed mark is wrong", (long)i);
            }
            stacks += expected.type != ITEM_NONE;
            if (items->forbidden[i] != tile_forbidden(items, i)) {
                found(&check, "a tile's forbidden mark is wrong", (long)i);
            }
            forbidden += items->forbidden[i] != 0;
        }
        if (items->level_stacks[z] != stacks) {
            found(&check, "a level's count of tiles with items is wrong", -1);
        }
    }
    for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
        if (totals[type] != items->totals[type]) {
            found(&check, "the totals of a type are wrong", -1);
            break;
        }
    }
    if (listed != items->item_count) {
        found(&check, "the count of items lying about is wrong", -1);
    }
    if (forbidden != items->forbidden_count) {
        found(&check, "the count of forbidden tiles is wrong", -1);
    }
    /* Every slot used is lying on a tile, in a load, or free: no more, no
     * less (an item in two lists would be counted twice). */
    long held = 0, free_count = 0;
    for (uint32_t slot = 0; slot < items->pool_used; slot++) {
        const Item *item = &items->pool[slot];
        held += item->type != ITEM_NONE && item->tile == ITEM_TILE_HELD;
    }
    for (uint32_t slot = items->free_slots; slot != 0 && free_count <= (long)items->pool_used;
         slot = items->pool[slot - 1].next) {
        if (items->pool[slot - 1].type != ITEM_NONE) {
            found(&check, "an item in use is on the free list", -1);
            break;
        }
        free_count++;
    }
    if (held != items->held_count) {
        found(&check, "the count of items in loads is wrong", -1);
    }
    if (listed + held + free_count != (long)items->pool_used) {
        found(&check, "some slots are neither lying, held nor free", -1);
    }
    return check;
}

bool items_check_load(const Items *items, ItemStack load)
{
    if (load.held == 0) {
        return true; /* Only numbers. */
    }
    long count = 0;
    for (const Item *item = slot_item(items, load.held); item != NULL;
         item = slot_item(items, item->next)) {
        if (++count > (long)items->pool_used || item->tile != ITEM_TILE_HELD ||
            item->type != load.type || item->corpse != load.corpse ||
            (item->forbidden != 0) != (load.forbidden != 0)) {
            return false;
        }
    }
    return count == load.count;
}

ItemHeaps items_heaps(const Items *items)
{
    ItemHeaps heaps = {0, 0, 0, 0};
    size_t count = (size_t)items->width * (size_t)items->height * (size_t)items->depth;
    for (size_t i = 0; i < count; i++) {
        if (items->stacks[i].type == ITEM_NONE) {
            continue;
        }
        heaps.tiles++;
        if (!items->mixed[i]) {
            continue;
        }
        ItemStack groups[16];
        int x = (int)(i % (size_t)items->width);
        int y = (int)(i / (size_t)items->width % (size_t)items->height);
        int z = (int)(i / ((size_t)items->width * (size_t)items->height));
        int kinds = items_groups(items, x, y, z, groups, 16);
        heaps.mixed++;
        heaps.mixed_groups += kinds;
        heaps.most_groups = kinds > heaps.most_groups ? kinds : heaps.most_groups;
    }
    return heaps;
}

uint64_t items_hash(const Items *items, uint64_t hash)
{
    size_t count = (size_t)items->width * (size_t)items->height * (size_t)items->depth;
    /* Field by field: struct padding bytes are unspecified. */
    for (size_t i = 0; i < count; i++) {
        if (items->heads[i] == 0) {
            continue;
        }
        uint64_t index = i;
        hash = hash_fnv1a64(hash, &index, sizeof(index));
        for (const Item *item = items_first(items, i); item != NULL;
             item = items_next(items, item)) {
            hash = hash_fnv1a64(hash, &item->type, sizeof(item->type));
            hash = hash_fnv1a64(hash, &item->quality, sizeof(item->quality));
            hash = hash_fnv1a64(hash, &item->hit_points, sizeof(item->hit_points));
            hash = hash_fnv1a64(hash, &item->progress, sizeof(item->progress));
            hash = hash_fnv1a64(hash, &item->eaten, sizeof(item->eaten));
            hash = hash_fnv1a64(hash, &item->corpse, sizeof(item->corpse));
            hash = hash_fnv1a64(hash, &item->spot_x, sizeof(item->spot_x));
            hash = hash_fnv1a64(hash, &item->spot_y, sizeof(item->spot_y));
            hash = hash_fnv1a64(hash, &item->angle, sizeof(item->angle));
            if (item->forbidden) { /* Hashes without anything forbidden stay as they were. */
                hash = hash_fnv1a64(hash, &item->forbidden, sizeof(item->forbidden));
            }
        }
    }
    /* Items in loads, by slot. */
    for (uint32_t slot = 0; slot < items->pool_used; slot++) {
        const Item *item = &items->pool[slot];
        if (item->type == ITEM_NONE || item->tile != ITEM_TILE_HELD) {
            continue;
        }
        hash = hash_fnv1a64(hash, &slot, sizeof(slot));
        hash = hash_fnv1a64(hash, &item->type, sizeof(item->type));
        hash = hash_fnv1a64(hash, &item->quality, sizeof(item->quality));
        hash = hash_fnv1a64(hash, &item->hit_points, sizeof(item->hit_points));
        hash = hash_fnv1a64(hash, &item->progress, sizeof(item->progress));
        hash = hash_fnv1a64(hash, &item->eaten, sizeof(item->eaten));
        hash = hash_fnv1a64(hash, &item->corpse, sizeof(item->corpse));
        if (item->forbidden) {
            hash = hash_fnv1a64(hash, &item->forbidden, sizeof(item->forbidden));
        }
    }
    return hash;
}
