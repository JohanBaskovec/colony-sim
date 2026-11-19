/*
 * Item moves: the player orders the items of a tile, or a single item,
 * carried to one chosen tile (any tile a settler can stand on, in a stockpile
 * or not). What is ordered is forbidden when the order is given, so that
 * nobody else takes it meanwhile, and stays so on the destination, so that
 * haulers do not take it back to a stockpile at once: the player allows it
 * again when they want.
 *
 * Settlers do this as hauling work, before the ordinary hauls (see
 * core/colonist.h): they pick the items up whatever their forbidden flag,
 * carry them, as much as they can at a time, and put them down on the
 * destination. An order on a tile stays while the tile holds items and goes
 * when it is empty; an order on an item stays while the item lies on its
 * tile.
 */
#ifndef COLONY_CORE_ITEM_MOVE_H
#define COLONY_CORE_ITEM_MOVE_H

#include "core/item.h"
#include "core/map.h"

#include <stdbool.h>
#include <stdint.h>

/* Orders at a time: one per source tile, and one per item. */
#define ITEM_MOVE_MAX 64

typedef struct ItemMove {
    TilePos source;
    TilePos destination;
    ItemId item; /* The item ordered, ITEM_ID_NONE: every item on `source`. */
} ItemMove;

typedef struct ItemMoves {
    ItemMove orders[ITEM_MOVE_MAX];
    int count;
    /* Incremented whenever an order is added or taken away, so that settlers
     * who found nothing to haul know when to look again. */
    uint32_t revision;
} ItemMoves;

void item_moves_init(ItemMoves *moves);

/* Index of the order on a source tile (`item` ITEM_ID_NONE) or on one item
 * lying there, -1 when there is none. */
int item_moves_find(const ItemMoves *moves, TilePos source, ItemId item);

/* Orders the items on `source` (`item` ITEM_ID_NONE) or one item lying there
 * carried to `destination`, replacing the order they already had. Refused
 * (false) when the two tiles are the same or when there are ITEM_MOVE_MAX
 * orders already. */
bool item_moves_add(ItemMoves *moves, TilePos source, TilePos destination, ItemId item);

/* Takes an order back (as item_moves_find() names it); false when there was
 * none. */
bool item_moves_remove(ItemMoves *moves, TilePos source, ItemId item);

/* Takes back the orders on single items lying on `source`; returns how many. */
int item_moves_remove_items_on(ItemMoves *moves, TilePos source);

/* Whether an order is being carried out right now (a settler holds what it
 * took from the source and is on its way to the destination). */
typedef bool (*ItemMoveBusyFn)(const void *context, const ItemMove *order);

/* Drops the orders that are not busy (`busy` may be NULL: none is) and whose
 * source holds nothing any more, or whose item no longer lies there;
 * returns how many. */
int item_moves_prune(ItemMoves *moves, const Items *items, ItemMoveBusyFn busy,
                     const void *context);

/* Items of `stack` a destination tile can still take (items_room()): any
 * tile with room, whatever else lies there. */
int item_move_room(const Items *items, TilePos destination, ItemStack stack);

uint64_t item_moves_hash(const ItemMoves *moves, uint64_t hash);

#endif /* COLONY_CORE_ITEM_MOVE_H */
