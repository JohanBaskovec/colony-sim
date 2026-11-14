/*
 * Items lying on the ground (wood, stone...): one stack per tile, stored as a
 * dense layer the size of the map and owned by the Game.
 *
 * Stacks hold a single item type, up to the type's stack limit. Dropping more
 * items than a tile can take spreads them over nearby reachable tiles.
 */
#ifndef COLONY_CORE_ITEM_H
#define COLONY_CORE_ITEM_H

#include <stdbool.h>
#include <stdint.h>

struct Map; /* core/map.h includes this header. */

typedef enum ItemType {
    ITEM_NONE = 0,
    ITEM_WOOD,
    ITEM_STONE,
    ITEM_BERRIES,
    ITEM_TYPE_COUNT
} ItemType;

typedef struct ItemDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    int stack_limit; /* Maximum number of items in one stack (one tile). */
    float nutrition; /* Food need restored by eating one item; 0 = not food. */
} ItemDef;

typedef struct ItemStack {
    uint8_t type; /* ItemType, ITEM_NONE when the tile is empty. */
    uint16_t count;
} ItemStack;

typedef struct Items {
    int width;
    int height;
    ItemStack *stacks;
    long totals[ITEM_TYPE_COUNT]; /* Items of each type on the whole map. */
} Items;

/* Items dropped by a single action spread at most this far (in steps). */
#define ITEM_DROP_RADIUS 4

const ItemDef *item_def(ItemType type);

bool item_is_food(ItemType type);

bool items_init(Items *items, int width, int height);
void items_free(Items *items);

/* Empty stack when out of bounds. */
ItemStack items_get(const Items *items, int x, int y);

/* Adds items to a tile that is empty or holds the same type, up to the stack
 * limit. Returns the number actually added. */
int items_add(Items *items, int x, int y, ItemType type, int count);

/* Removes up to `count` items from a tile. Returns the number removed. */
int items_remove(Items *items, int x, int y, int count);

/*
 * Puts items on (x, y) and, when it is full or holds something else, on the
 * nearest passable tiles reachable from it (breadth-first, fixed order,
 * within ITEM_DROP_RADIUS). Returns the number placed: items that find no
 * room are lost.
 */
int items_drop(Items *items, const struct Map *map, int x, int y, ItemType type, int count);

uint64_t items_hash(const Items *items, uint64_t hash);

#endif /* COLONY_CORE_ITEM_H */
