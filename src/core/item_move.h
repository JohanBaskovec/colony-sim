/*
 * Item moves: the player orders a stack of items carried to one chosen tile
 * (any tile a settler can stand on, in a stockpile or not). The stack is
 * forbidden when the order is given, so that nobody else takes it meanwhile,
 * and so is what lands on the destination, so that haulers do not take it
 * back to a stockpile at once: the player allows it again when they want.
 *
 * Settlers do this as hauling work, before the ordinary hauls (see
 * core/colonist.h): they pick the stack up whatever its forbidden flag, carry
 * it, as much as they can at a time, and put it down on the destination. The
 * order stays while the source holds items and goes when it is empty.
 */
#ifndef COLONY_CORE_ITEM_MOVE_H
#define COLONY_CORE_ITEM_MOVE_H

#include "core/item.h"
#include "core/map.h"

#include <stdbool.h>
#include <stdint.h>

/* Orders at a time: one per source tile. */
#define ITEM_MOVE_MAX 64

typedef struct ItemMove {
    TilePos source;
    TilePos destination;
} ItemMove;

typedef struct ItemMoves {
    ItemMove orders[ITEM_MOVE_MAX];
    int count;
    /* Incremented whenever an order is added or taken away, so that settlers
     * who found nothing to haul know when to look again. */
    uint32_t revision;
} ItemMoves;

void item_moves_init(ItemMoves *moves);

/* Index of the order on a source tile, -1 when there is none. */
int item_moves_find(const ItemMoves *moves, TilePos source);

/* Orders the stack on `source` carried to `destination`, replacing the order
 * it already had. Refused (false) when the two tiles are the same or when
 * there are ITEM_MOVE_MAX orders already. */
bool item_moves_add(ItemMoves *moves, TilePos source, TilePos destination);

/* Takes the order on a source tile back; false when there was none. */
bool item_moves_remove(ItemMoves *moves, TilePos source);

/* Whether an order is being carried out right now (a settler holds what it
 * took from the source and is on its way to the destination). */
typedef bool (*ItemMoveBusyFn)(const void *context, const ItemMove *order);

/* Drops the orders whose source holds nothing any more and that are not busy
 * (`busy` may be NULL: none is); returns how many. */
int item_moves_prune(ItemMoves *moves, const Items *items, ItemMoveBusyFn busy,
                     const void *context);

/* Items of `stack` a destination tile can still take (items_room()): any
 * tile with room, whatever else lies there. */
int item_move_room(const Items *items, TilePos destination, ItemStack stack);

uint64_t item_moves_hash(const ItemMoves *moves, uint64_t hash);

#endif /* COLONY_CORE_ITEM_MOVE_H */
