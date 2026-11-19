#include "core/item_move.h"

#include "core/util.h"

#include <string.h>

static bool same_tile(TilePos a, TilePos b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

void item_moves_init(ItemMoves *moves)
{
    memset(moves, 0, sizeof(*moves));
}

static bool same_item(ItemId a, ItemId b)
{
    return a.slot == b.slot && (a.slot == UINT32_MAX || a.generation == b.generation);
}

int item_moves_find(const ItemMoves *moves, TilePos source, ItemId item)
{
    for (int i = 0; i < moves->count; i++) {
        if (same_tile(moves->orders[i].source, source) && same_item(moves->orders[i].item, item)) {
            return i;
        }
    }
    return -1;
}

bool item_moves_add(ItemMoves *moves, TilePos source, TilePos destination, ItemId item)
{
    if (same_tile(source, destination)) {
        return false;
    }
    int index = item_moves_find(moves, source, item);
    if (index < 0) {
        if (moves->count == ITEM_MOVE_MAX) {
            return false;
        }
        index = moves->count++;
    }
    moves->orders[index] = (ItemMove){source, destination, item};
    moves->revision++;
    return true;
}

static void remove_at(ItemMoves *moves, int index)
{
    moves->count--;
    moves->orders[index] = moves->orders[moves->count]; /* Order is not meaningful. */
    memset(&moves->orders[moves->count], 0, sizeof(ItemMove));
    moves->revision++;
}

bool item_moves_remove(ItemMoves *moves, TilePos source, ItemId item)
{
    int index = item_moves_find(moves, source, item);
    if (index < 0) {
        return false;
    }
    remove_at(moves, index);
    return true;
}

int item_moves_remove_items_on(ItemMoves *moves, TilePos source)
{
    int removed = 0;
    for (int i = 0; i < moves->count;) {
        if (same_tile(moves->orders[i].source, source) &&
            moves->orders[i].item.slot != UINT32_MAX) {
            remove_at(moves, i);
            removed++;
        } else {
            i++;
        }
    }
    return removed;
}

/* Whether what an order is about is still there: an item on the source, its
 * very item. */
static bool order_stands(const ItemMove *order, const Items *items)
{
    TilePos source = order->source;
    if (order->item.slot == UINT32_MAX) {
        return items_get(items, source.x, source.y, source.z).type != ITEM_NONE;
    }
    const Item *item = items_by_id(items, order->item);
    return item != NULL &&
           item->tile == ((size_t)source.z * (size_t)items->height + (size_t)source.y) *
                                 (size_t)items->width +
                             (size_t)source.x;
}

int item_moves_prune(ItemMoves *moves, const Items *items, ItemMoveBusyFn busy, const void *context)
{
    int removed = 0;
    for (int i = 0; i < moves->count;) {
        if (!order_stands(&moves->orders[i], items) &&
            (busy == NULL || !busy(context, &moves->orders[i]))) {
            remove_at(moves, i);
            removed++;
        } else {
            i++;
        }
    }
    return removed;
}

int item_move_room(const Items *items, TilePos destination, ItemStack stack)
{
    if (stack.type == ITEM_NONE) {
        return 0;
    }
    return items_room(items, destination.x, destination.y, destination.z, stack);
}

uint64_t item_moves_hash(const ItemMoves *moves, uint64_t hash)
{
    hash = hash_fnv1a64(hash, &moves->count, sizeof(moves->count));
    /* Field by field: struct padding bytes are unspecified. */
    for (int i = 0; i < moves->count; i++) {
        const ItemMove *order = &moves->orders[i];
        hash = hash_fnv1a64(hash, &order->source, sizeof(order->source));
        hash = hash_fnv1a64(hash, &order->destination, sizeof(order->destination));
        if (order->item.slot != UINT32_MAX) {
            hash = hash_fnv1a64(hash, &order->item.slot, sizeof(order->item.slot));
            hash = hash_fnv1a64(hash, &order->item.generation, sizeof(order->item.generation));
        }
    }
    return hash;
}
