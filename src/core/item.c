#include "core/item.h"

#include "core/i18n.h"
#include "core/map.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

/*
 * Units are human-sized pieces with realistic weights:
 * - wood: a log about 1 m long and 11 cm thick, 7 kg of fresh wood (~10 l);
 *   a mature tree gives 60 of them (420 kg: trunk and big branches);
 * - stone block: a rough block of about 20 kg dug out of a rock wall (7.5 l
 *   of rock, irregular so it stacks with gaps);
 * - berries: a portion of about 300 g (half a liter, ~190 kcal);
 * - cobble: a rounded stone that fits the hand, about 1 kg, picked up from
 *   the ground; tough, the knapper's hammer (its grade is its toughness);
 * - flint: a nodule of about 1 kg, the stone that is knapped (its grade is how
 *   fine and even its grain is);
 * - branches: a dead or cut branch of about 1.5 kg, poorly packed;
 * - twigs: a handful (about 200 g) of kindling;
 * - grass: an armful, 2.5 kg cut fresh, 1 kg once dry (about 12 l either way).
 * Stone blocks never wear out; logs vary with the woodcutter (knots, cracks)
 * and berries with the gatherer (ripeness, bruises). Fresh grass left on the
 * ground dries in three days.
 * Tools and weapons (docs/KNOWLEDGE.md §6, §8; a plain cobble serves as
 * the knapper's hammer): a chopper (1 kg), a sharp flake (30 g, razor sharp
 * but soon worn out), a hand axe (600 g); a throwing spear (0.8 kg, a long
 * straight branch) and a thrusting spear (1.5 kg, thicker).
 */
#define DEF(id_, key, category_, weight_, bulk_, nutrition_, hp, quality, damage)                 \
    .id = (id_), .name_key = (key), .category = (category_), .weight = (weight_),              \
    .bulk = (bulk_), .nutrition = (nutrition_), .max_hit_points = (hp), .has_quality = (quality), \
    .damageable = (damage)

static const ItemDef ITEM_DEFS[ITEM_TYPE_COUNT] = {
    /* id, name, category, weight (kg), bulk, nutrition, hit points, quality?, damageable? */
    [ITEM_NONE] = {DEF("none", NULL, ITEM_CATEGORY_COUNT, 0.0f, 0.0f, 0.0f, 0, false, false)},
    [ITEM_WOOD] = {DEF("wood", N_("item.wood"), ITEM_CATEGORY_RESOURCES, 7.0f, 20.0f, 0.0f, 60,
                       true, true)},
    [ITEM_STONE_BLOCK] = {DEF("stone_block", N_("item.stone_block"), ITEM_CATEGORY_RESOURCES,
                              20.0f, 20.0f, 0.0f, 150, false, false)},
    [ITEM_BERRIES] = {DEF("berries", N_("item.berries"), ITEM_CATEGORY_FOOD, 0.3f, 1.0f, 0.05f, 10,
                          true, true)},
    [ITEM_COBBLE] = {DEF("cobble", N_("item.cobble"), ITEM_CATEGORY_RESOURCES, 1.0f, 1.5f, 0.0f,
                         200, true, false)},
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
    [ITEM_FLAKE] = {DEF("flake", N_("item.flake"), ITEM_CATEGORY_TOOLS, 0.03f, 0.2f, 0.0f, 15,
                        true, true)},
    [ITEM_HAND_AXE] = {DEF("hand_axe", N_("item.hand_axe"), ITEM_CATEGORY_TOOLS, 0.6f, 1.5f, 0.0f,
                           120, true, true)},
    [ITEM_THROWING_SPEAR] = {DEF("throwing_spear", N_("item.throwing_spear"),
                                 ITEM_CATEGORY_WEAPONS, 0.8f, 10.0f, 0.0f, 50, true, true)},
    [ITEM_THRUSTING_SPEAR] = {DEF("thrusting_spear", N_("item.thrusting_spear"),
                                  ITEM_CATEGORY_WEAPONS, 1.5f, 12.0f, 0.0f, 70, true, true)},
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
        .quality = (uint8_t)(def->has_quality ? clampi(quality, 0, ITEM_QUALITY_BEST)
                                              : ITEM_QUALITY_BEST),
        .count = (uint16_t)count,
        .hit_points = (uint16_t)(def->damageable ? clampi(hit_points, 1, def->max_hit_points)
                                                 : def->max_hit_points),
    };
}

/* Transformation progress made valid for a type: 0 when it never transforms. */
static uint16_t valid_progress(ItemType type, int progress)
{
    const ItemDef *def = &ITEM_DEFS[type];
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

bool items_init(Items *items, int width, int height)
{
    memset(items, 0, sizeof(*items));
    if (width <= 0 || height <= 0) {
        return false;
    }
    items->stacks = calloc((size_t)width * (size_t)height, sizeof(*items->stacks));
    if (items->stacks == NULL) {
        return false;
    }
    items->width = width;
    items->height = height;
    return true;
}

void items_free(Items *items)
{
    free(items->stacks);
    memset(items, 0, sizeof(*items));
}

static bool in_bounds(const Items *items, int x, int y)
{
    return x >= 0 && y >= 0 && x < items->width && y < items->height;
}

static ItemStack *stack_at(Items *items, int x, int y)
{
    return &items->stacks[(size_t)y * (size_t)items->width + (size_t)x];
}

ItemStack items_get(const Items *items, int x, int y)
{
    if (!in_bounds(items, x, y)) {
        return (ItemStack){.type = ITEM_NONE};
    }
    return items->stacks[(size_t)y * (size_t)items->width + (size_t)x];
}

int items_add(Items *items, int x, int y, ItemStack batch)
{
    ItemType type = (ItemType)batch.type;
    if (!in_bounds(items, x, y) || !is_item_type(type) || batch.count == 0) {
        return 0;
    }
    /* Callers may build stacks by hand: keep the condition valid for the type. */
    uint16_t progress = valid_progress(type, batch.progress);
    batch = item_stack_make(type, batch.count, batch.quality, batch.hit_points);
    batch.progress = progress;
    ItemStack *stack = stack_at(items, x, y);
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

int items_remove(Items *items, int x, int y, int count)
{
    if (!in_bounds(items, x, y) || count <= 0) {
        return 0;
    }
    ItemStack *stack = stack_at(items, x, y);
    int removed = count < (int)stack->count ? count : (int)stack->count;
    if (removed <= 0) {
        return 0;
    }
    items->totals[stack->type] -= removed;
    stack->count = (uint16_t)(stack->count - removed);
    if (stack->count == 0) {
        *stack = (ItemStack){.type = ITEM_NONE};
    }
    return removed;
}

int items_drop(Items *items, const Map *map, int x, int y, ItemStack batch)
{
    int count = batch.count;
    enum { SIDE = 2 * ITEM_DROP_RADIUS + 1 };
    static const int DX[4] = {0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1, 0};
    if (!map_is_passable(map, x, y)) {
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
        placed += items_add(items, cx, cy, rest);
        for (int i = 0; i < 4; i++) {
            int rx = cell % SIDE + DX[i];
            int ry = cell / SIDE + DY[i];
            if (rx < 0 || ry < 0 || rx >= SIDE || ry >= SIDE || visited[ry * SIDE + rx]) {
                continue;
            }
            visited[ry * SIDE + rx] = true;
            if (map_is_passable(map, x + rx - ITEM_DROP_RADIUS, y + ry - ITEM_DROP_RADIUS)) {
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

int items_advance(Items *items, size_t begin, size_t end, int hours)
{
    size_t count = (size_t)items->width * (size_t)items->height;
    if (end > count) {
        end = count;
    }
    if (hours <= 0) {
        return 0;
    }
    int transformed = 0;
    for (size_t i = begin; i < end; i++) {
        ItemStack *stack = &items->stacks[i];
        if (stack->type == ITEM_NONE || ITEM_DEFS[stack->type].transforms_into == ITEM_NONE) {
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
    size_t count = (size_t)items->width * (size_t)items->height;
    /* Field by field: struct padding bytes are unspecified. */
    for (size_t i = 0; i < count; i++) {
        const ItemStack *stack = &items->stacks[i];
        hash = hash_fnv1a64(hash, &stack->type, sizeof(stack->type));
        hash = hash_fnv1a64(hash, &stack->count, sizeof(stack->count));
        hash = hash_fnv1a64(hash, &stack->quality, sizeof(stack->quality));
        hash = hash_fnv1a64(hash, &stack->hit_points, sizeof(stack->hit_points));
        hash = hash_fnv1a64(hash, &stack->progress, sizeof(stack->progress));
    }
    return hash;
}
