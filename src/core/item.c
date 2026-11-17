#include "core/item.h"

#include "core/i18n.h"
#include "core/map.h"
#include "core/util.h"

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
 * - cobble: a rounded stone that fits the hand, about 1 kg, picked up from
 *   the ground; tough, the knapper's hammer (its grade is its toughness);
 * - flint: a nodule of about 1 kg, the stone that is knapped (its grade is how
 *   fine and even its grain is);
 * - branches: a dead or cut branch of about 1.5 kg, poorly packed;
 * - twigs: a handful (about 200 g) of kindling;
 * - grass: an armful, 2.5 kg cut fresh, 1 kg once dry (about 12 l either way).
 * Stone blocks never wear out; logs vary with the woodcutter (knots, cracks)
 * and wild food with the gatherer (ripeness, bruises). Fresh grass left on the
 * ground dries in three days. Food lying in the open at 15 °C keeps (rot_days):
 * greens and mushrooms 2 days, blackberries 3, roasted roots 4 (moist and
 * soft), cattail rhizomes 15, wild carrots 20, hazelnuts in their shells 90.
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
                       true, true)},
    [ITEM_STONE_BLOCK] = {DEF("stone_block", N_("item.stone_block"), ITEM_CATEGORY_RESOURCES, 20.0f,
                              20.0f, 0.0f, 150, false, false)},
    [ITEM_BLACKBERRIES] = {DEF("blackberries", N_("item.blackberries"), ITEM_CATEGORY_FOOD, 0.3f,
                               1.0f, 0.05f, 10, true, true),
                           .rot_days = 3.0f},
    /* Damageable: a cobble used as a hammer wears out. */
    [ITEM_COBBLE] = {DEF("cobble", N_("item.cobble"), ITEM_CATEGORY_RESOURCES, 1.0f, 1.5f, 0.0f,
                         200, true, true)},
    [ITEM_BRANCHES] = {DEF("branches", N_("item.branches"), ITEM_CATEGORY_RESOURCES, 1.5f, 10.0f,
                           0.0f, 20, true, true)},
    [ITEM_TWIGS] = {DEF("twigs", N_("item.twigs"), ITEM_CATEGORY_RESOURCES, 0.2f, 2.0f, 0.0f, 10,
                        false, true)},
    [ITEM_FRESH_GRASS] = {DEF("fresh_grass", N_("item.fresh_grass"), ITEM_CATEGORY_RESOURCES, 2.5f,
                              25.0f, 0.0f, 10, false, true),
                          .transforms_into = ITEM_GRASS, .transform_hours = 72},
    [ITEM_GRASS] = {DEF("grass", N_("item.grass"), ITEM_CATEGORY_RESOURCES, 1.0f, 25.0f, 0.0f, 15,
                        false, true)},
    [ITEM_FLINT] = {DEF("flint", N_("item.flint"), ITEM_CATEGORY_RESOURCES, 1.0f, 1.5f, 0.0f, 150,
                        true, false)},
    [ITEM_CHOPPER] = {DEF("chopper", N_("item.chopper"), ITEM_CATEGORY_TOOLS, 1.0f, 2.0f, 0.0f, 80,
                          true, true)},
    [ITEM_FLAKE] = {DEF("flake", N_("item.flake"), ITEM_CATEGORY_TOOLS, 0.03f, 0.2f, 0.0f, 15, true,
                        true)},
    [ITEM_HAND_AXE] = {DEF("hand_axe", N_("item.hand_axe"), ITEM_CATEGORY_TOOLS, 0.6f, 1.5f, 0.0f,
                           120, true, true)},
    [ITEM_THROWING_SPEAR] = {DEF("throwing_spear", N_("item.throwing_spear"), ITEM_CATEGORY_WEAPONS,
                                 0.8f, 10.0f, 0.0f, 50, true, true)},
    [ITEM_THRUSTING_SPEAR] = {DEF("thrusting_spear", N_("item.thrusting_spear"),
                                  ITEM_CATEGORY_WEAPONS, 1.5f, 12.0f, 0.0f, 70, true, true)},
    [ITEM_THROWING_SPEAR_HARDENED] = {DEF("throwing_spear_hardened",
                                          N_("item.throwing_spear_hardened"), ITEM_CATEGORY_WEAPONS,
                                          0.8f, 10.0f, 0.0f, 75, true, true)},
    [ITEM_THRUSTING_SPEAR_HARDENED] = {DEF(
        "thrusting_spear_hardened", N_("item.thrusting_spear_hardened"), ITEM_CATEGORY_WEAPONS,
        1.5f, 12.0f, 0.0f, 105, true, true)},
    [ITEM_HAZELNUTS] = {DEF("hazelnuts", N_("item.hazelnuts"), ITEM_CATEGORY_FOOD, 0.1f, 0.4f,
                            0.075f, 100, true, true),
                        .rot_days = 90.0f},
    [ITEM_MUSHROOMS] = {DEF("mushrooms", N_("item.mushrooms"), ITEM_CATEGORY_FOOD, 0.1f, 0.5f,
                            0.01f, 5, true, true),
                        .rot_days = 2.0f},
    [ITEM_WILD_GARLIC] = {DEF("wild_garlic", N_("item.wild_garlic"), ITEM_CATEGORY_FOOD, 0.2f, 1.5f,
                              0.016f, 5, true, true),
                          .rot_days = 2.0f},
    [ITEM_NETTLES] = {DEF("nettles", N_("item.nettles"), ITEM_CATEGORY_FOOD, 0.2f, 1.5f, 0.016f, 5,
                          true, true),
                      .rot_days = 2.0f},
    [ITEM_PLANTAIN] = {DEF("plantain", N_("item.plantain"), ITEM_CATEGORY_FOOD, 0.2f, 1.5f, 0.013f,
                           5, true, true),
                       .rot_days = 2.0f},
    [ITEM_WILD_CARROTS] = {DEF("wild_carrots", N_("item.wild_carrots"), ITEM_CATEGORY_FOOD, 0.25f,
                               1.0f, 0.027f, 30, true, true),
                           .rot_days = 20.0f},
    [ITEM_CATTAIL_RHIZOMES] = {DEF("cattail_rhizomes", N_("item.cattail_rhizomes"),
                                   ITEM_CATEGORY_FOOD, 0.25f, 1.0f, 0.05f, 30, true, true),
                               .rot_days = 15.0f},
    [ITEM_DIGGING_STICK] = {DEF("digging_stick", N_("item.digging_stick"), ITEM_CATEGORY_TOOLS,
                                1.0f, 6.0f, 0.0f, 40, true, true)},
    [ITEM_DIGGING_STICK_HARDENED] = {DEF("digging_stick_hardened",
                                         N_("item.digging_stick_hardened"), ITEM_CATEGORY_TOOLS,
                                         1.0f, 6.0f, 0.0f, 60, true, true)},
    [ITEM_ROASTED_RHIZOMES] = {DEF("roasted_rhizomes", N_("item.roasted_rhizomes"),
                                   ITEM_CATEGORY_FOOD, 0.25f, 1.0f, 0.07f, 15, true, true),
                               .rot_days = 4.0f},
    [ITEM_ROASTED_CARROTS] = {DEF("roasted_carrots", N_("item.roasted_carrots"), ITEM_CATEGORY_FOOD,
                                  0.25f, 1.0f, 0.035f, 15, true, true),
                              .rot_days = 4.0f},
};

#undef DEF

static const char *const QUALITY_KEYS[ITEM_QUALITY_COUNT] = {
    [ITEM_QUALITY_CRUDE] = N_("item.quality.crude"),
    [ITEM_QUALITY_ROUGH] = N_("item.quality.rough"),
    [ITEM_QUALITY_STANDARD] = N_("item.quality.standard"),
    [ITEM_QUALITY_FINE] = N_("item.quality.fine"),
    [ITEM_QUALITY_SUPERB] = N_("item.quality.superb"),
};

const char *item_quality_name_key(ItemQuality quality)
{
    return (unsigned)quality < ITEM_QUALITY_COUNT ? QUALITY_KEYS[quality] : NULL;
}

static const char *const CATEGORY_KEYS[ITEM_CATEGORY_COUNT] = {
    [ITEM_CATEGORY_FOOD] = N_("item.category.food"),
    [ITEM_CATEGORY_RESOURCES] = N_("item.category.resources"),
    [ITEM_CATEGORY_TOOLS] = N_("item.category.tools"),
    [ITEM_CATEGORY_WEAPONS] = N_("item.category.weapons"),
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

bool item_is_food(ItemType type)
{
    return (unsigned)type < ITEM_TYPE_COUNT && ITEM_DEFS[type].nutrition > 0.0f;
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
            (uint8_t)(def->has_quality ? clampi(quality, 0, ITEM_QUALITY_BEST) : ITEM_QUALITY_BEST),
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

bool item_stacks_match(ItemStack a, ItemStack b)
{
    return a.type == b.type && a.quality == b.quality;
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
    into->hit_points =
        (uint16_t)weighted_average(into->hit_points, into->count, from.hit_points, from.count);
    into->progress =
        (uint16_t)weighted_average(into->progress, into->count, from.progress, from.count);
    into->count = (uint16_t)count;
    return true;
}

void items_copy_tile(Items *dst, const Items *src, size_t index)
{
    size_t level = (size_t)dst->width * (size_t)dst->height;
    size_t z = index / level;
    ItemStack *held = &dst->stacks[index];
    if (held->type != ITEM_NONE) {
        dst->totals[held->type] -= held->count;
        dst->level_stacks[z]--;
    }
    *held = src->stacks[index];
    if (held->type != ITEM_NONE) {
        dst->totals[held->type] += held->count;
        dst->level_stacks[z]++;
    }
    dst->forbidden_count += (src->forbidden[index] != 0) - (dst->forbidden[index] != 0);
    dst->forbidden[index] = src->forbidden[index];
}

bool items_init(Items *items, int width, int height, int depth)
{
    memset(items, 0, sizeof(*items));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    items->stacks = calloc((size_t)width * (size_t)height * (size_t)depth, sizeof(*items->stacks));
    items->level_stacks = calloc((size_t)depth, sizeof(*items->level_stacks));
    items->forbidden = calloc((size_t)width * (size_t)height * (size_t)depth, 1);
    if (items->stacks == NULL || items->level_stacks == NULL || items->forbidden == NULL) {
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
    memset(items, 0, sizeof(*items));
}

static bool in_bounds(const Items *items, int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < items->width && y < items->height && z < items->depth;
}

static size_t index_of(const Items *items, int x, int y, int z)
{
    return ((size_t)z * (size_t)items->height + (size_t)y) * (size_t)items->width + (size_t)x;
}

static ItemStack *stack_at(Items *items, int x, int y, int z)
{
    return &items->stacks[index_of(items, x, y, z)];
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
    if (!in_bounds(items, x, y, z) ||
        (items->forbidden_count > 0 && items->forbidden[index_of(items, x, y, z)])) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return items->stacks[index_of(items, x, y, z)];
}

bool items_forbidden(const Items *items, int x, int y, int z)
{
    return items->forbidden_count > 0 && in_bounds(items, x, y, z) &&
           items->forbidden[index_of(items, x, y, z)] != 0;
}

bool items_set_forbidden(Items *items, int x, int y, int z, bool forbidden)
{
    if (!in_bounds(items, x, y, z)) {
        return false;
    }
    size_t index = index_of(items, x, y, z);
    if (items->stacks[index].type == ITEM_NONE || (items->forbidden[index] != 0) == forbidden) {
        return false;
    }
    items->forbidden[index] = forbidden;
    items->forbidden_count += forbidden ? 1 : -1;
    if (!forbidden) {
        items->additions++; /* Something to store again. */
    }
    return true;
}

/* A stack is gone from a tile (an index): it was forbidden no more. */
static void stack_gone(Items *items, size_t index)
{
    if (items->forbidden[index]) {
        items->forbidden[index] = 0;
        items->forbidden_count--;
    }
}

int items_add(Items *items, int x, int y, int z, ItemStack batch)
{
    ItemType type = (ItemType)batch.type;
    if (!in_bounds(items, x, y, z) || !is_item_type(type) || batch.count == 0) {
        return 0;
    }
    /* Callers may build stacks by hand: keep the condition valid for the type. */
    uint16_t progress = valid_progress(type, batch.progress);
    batch = item_stack_make(type, batch.count, batch.quality, batch.hit_points);
    batch.progress = progress;
    ItemStack *stack = stack_at(items, x, y, z);
    if (stack->type != ITEM_NONE && !item_stacks_match(*stack, batch)) {
        return 0;
    }
    int room = item_stack_limit(type) - (int)stack->count;
    int added = (int)batch.count < room ? (int)batch.count : room;
    if (added <= 0) {
        return 0;
    }
    if (stack->type == ITEM_NONE) {
        *stack = batch;
        stack->count = 0;
        items->level_stacks[z]++;
    } else {
        stack->hit_points =
            (uint16_t)weighted_average(stack->hit_points, stack->count, batch.hit_points, added);
        stack->progress =
            (uint16_t)weighted_average(stack->progress, stack->count, batch.progress, added);
    }
    stack->count = (uint16_t)(stack->count + added);
    items->totals[type] += added;
    items->additions++;
    return added;
}

int items_remove(Items *items, int x, int y, int z, int count)
{
    if (!in_bounds(items, x, y, z) || count <= 0) {
        return 0;
    }
    ItemStack *stack = stack_at(items, x, y, z);
    int removed = count < (int)stack->count ? count : (int)stack->count;
    if (removed <= 0) {
        return 0;
    }
    items->totals[stack->type] -= removed;
    stack->count = (uint16_t)(stack->count - removed);
    if (stack->count == 0) {
        *stack = (ItemStack){.type = ITEM_NONE};
        items->level_stacks[z]--;
        stack_gone(items, index_of(items, x, y, z));
    }
    return removed;
}

int items_drop(Items *items, const Map *map, int x, int y, int z, ItemStack batch)
{
    int count = batch.count;
    enum { SIDE = 2 * ITEM_DROP_RADIUS + 1 };
    static const int DX[4] = {0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1, 0};
    if (!map_is_passable(map, x, y, z)) {
        return 0;
    }
    /* Breadth-first search over passable tiles, so that items never land
     * behind a wall; cells are indexed relative to (x, y). */
    bool visited[SIDE * SIDE] = {false};
    int queue[SIDE * SIDE];
    int head = 0, tail = 0;
    queue[tail++] = ITEM_DROP_RADIUS * SIDE + ITEM_DROP_RADIUS;
    visited[queue[0]] = true;

    int placed = 0;
    while (head < tail && placed < count) {
        int cell = queue[head++];
        int cx = x + cell % SIDE - ITEM_DROP_RADIUS;
        int cy = y + cell / SIDE - ITEM_DROP_RADIUS;
        ItemStack rest = batch;
        rest.count = (uint16_t)(count - placed);
        if (!items_forbidden(items, cx, cy, z)) { /* Left alone. */
            placed += items_add(items, cx, cy, z, rest);
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
    return placed;
}

/* Turns a stack that has reached its transformation into the new type. */
static void transform(Items *items, ItemStack *stack)
{
    ItemType from = (ItemType)stack->type;
    ItemType to = ITEM_DEFS[from].transforms_into;
    int percent = item_stack_hit_points_percent(*stack);
    int quality = ITEM_DEFS[from].has_quality ? stack->quality : ITEM_QUALITY_BEST;
    int count = stack->count;
    *stack = item_stack_make(to, count, quality, item_hit_points_at_percent(to, percent));
    items->totals[from] -= count;
    items->totals[to] += count;
    items->additions++; /* Something new to store: haulers look again. */
}

/* Rots a stack for `hours` at `rate`; returns true when it rotted away. */
static bool rot(Items *items, ItemStack *stack, int hours, float rate)
{
    if (rate <= 0.0f) {
        return false;
    }
    float units = rot_units_per_hour(&ITEM_DEFS[stack->type], rate) * (float)hours;
    long total = (long)stack->progress + lroundf(units);
    long lost = total / ITEM_ROT_STEP;
    if (lost >= stack->hit_points) {
        items->totals[stack->type] -= stack->count;
        *stack = (ItemStack){0};
        size_t level_size = (size_t)items->width * (size_t)items->height;
        items->level_stacks[(size_t)(stack - items->stacks) / level_size]--;
        stack_gone(items, (size_t)(stack - items->stacks));
        return true;
    }
    stack->hit_points = (uint16_t)(stack->hit_points - lost);
    stack->progress = (uint16_t)(total % ITEM_ROT_STEP);
    return false;
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
        ItemStack *stack = &items->stacks[i];
        if (stack->type == ITEM_NONE) {
            continue;
        }
        const ItemDef *def = &ITEM_DEFS[stack->type];
        if (def->rot_days <= 0.0f && def->transforms_into == ITEM_NONE) {
            continue;
        }
        ItemConditions here =
            conditions != NULL ? conditions(context, i) : (ItemConditions){1.0f, true};
        if (def->rot_days > 0.0f) {
            transformed += rot(items, stack, hours, here.rot_rate);
            continue;
        }
        if (!here.dries) {
            continue;
        }
        int progress = (int)stack->progress + hours;
        if (progress >= ITEM_DEFS[stack->type].transform_hours) {
            transform(items, stack);
            transformed++;
        } else {
            stack->progress = (uint16_t)progress;
        }
    }
    return transformed;
}

uint64_t items_hash(const Items *items, uint64_t hash)
{
    size_t count = (size_t)items->width * (size_t)items->height * (size_t)items->depth;
    /* Field by field: struct padding bytes are unspecified. */
    for (size_t i = 0; i < count; i++) {
        const ItemStack *stack = &items->stacks[i];
        hash = hash_fnv1a64(hash, &stack->type, sizeof(stack->type));
        hash = hash_fnv1a64(hash, &stack->count, sizeof(stack->count));
        hash = hash_fnv1a64(hash, &stack->quality, sizeof(stack->quality));
        hash = hash_fnv1a64(hash, &stack->hit_points, sizeof(stack->hit_points));
        hash = hash_fnv1a64(hash, &stack->progress, sizeof(stack->progress));
    }
    if (items->forbidden_count > 0) { /* Hashes without anything forbidden stay as they were. */
        hash = hash_fnv1a64(hash, items->forbidden, count);
    }
    return hash;
}
