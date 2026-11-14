#include "core/item.h"

#include "core/i18n.h"
#include "core/map.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

static const ItemDef ITEM_DEFS[ITEM_TYPE_COUNT] = {
    [ITEM_NONE] = {"none", NULL, 0},
    [ITEM_WOOD] = {"wood", N_("item.wood"), 50},
    [ITEM_STONE] = {"stone", N_("item.stone"), 30},
};

const ItemDef *item_def(ItemType type)
{
    return (unsigned)type < ITEM_TYPE_COUNT ? &ITEM_DEFS[type] : NULL;
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
        return (ItemStack){ITEM_NONE, 0};
    }
    return items->stacks[(size_t)y * (size_t)items->width + (size_t)x];
}

int items_add(Items *items, int x, int y, ItemType type, int count)
{
    if (!in_bounds(items, x, y) || type == ITEM_NONE || (unsigned)type >= ITEM_TYPE_COUNT ||
        count <= 0) {
        return 0;
    }
    ItemStack *stack = stack_at(items, x, y);
    if (stack->type != ITEM_NONE && stack->type != type) {
        return 0;
    }
    int room = ITEM_DEFS[type].stack_limit - (int)stack->count;
    int added = count < room ? count : room;
    if (added <= 0) {
        return 0;
    }
    stack->type = (uint8_t)type;
    stack->count = (uint16_t)(stack->count + added);
    items->totals[type] += added;
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
        stack->type = ITEM_NONE;
    }
    return removed;
}

int items_drop(Items *items, const Map *map, int x, int y, ItemType type, int count)
{
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
        placed += items_add(items, cx, cy, type, count - placed);
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

uint64_t items_hash(const Items *items, uint64_t hash)
{
    size_t count = (size_t)items->width * (size_t)items->height;
    /* Field by field: struct padding bytes are unspecified. */
    for (size_t i = 0; i < count; i++) {
        const ItemStack *stack = &items->stacks[i];
        hash = hash_fnv1a64(hash, &stack->type, sizeof(stack->type));
        hash = hash_fnv1a64(hash, &stack->count, sizeof(stack->count));
    }
    return hash;
}
