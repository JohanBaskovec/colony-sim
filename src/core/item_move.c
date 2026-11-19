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

int item_moves_find(const ItemMoves *moves, TilePos source)
{
    for (int i = 0; i < moves->count; i++) {
        if (same_tile(moves->orders[i].source, source)) {
            return i;
        }
    }
    return -1;
}

bool item_moves_add(ItemMoves *moves, TilePos source, TilePos destination)
{
    if (same_tile(source, destination)) {
        return false;
    }
    int index = item_moves_find(moves, source);
    if (index < 0) {
        if (moves->count == ITEM_MOVE_MAX) {
            return false;
        }
        index = moves->count++;
    }
    moves->orders[index] = (ItemMove){source, destination};
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

bool item_moves_remove(ItemMoves *moves, TilePos source)
{
    int index = item_moves_find(moves, source);
    if (index < 0) {
        return false;
    }
    remove_at(moves, index);
    return true;
}

int item_moves_prune(ItemMoves *moves, const Items *items, ItemMoveBusyFn busy, const void *context)
{
    int removed = 0;
    for (int i = 0; i < moves->count;) {
        TilePos source = moves->orders[i].source;
        if (items_get(items, source.x, source.y, source.z).type == ITEM_NONE &&
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
    for (int i = 0; i < moves->count; i++) {
        hash = hash_fnv1a64(hash, &moves->orders[i], sizeof(ItemMove));
    }
    return hash;
}
