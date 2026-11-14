/*
 * Hauling: finding items to move into storage, and where to put them.
 *
 * An item stack needs hauling when it lies outside any stockpile accepting it
 * (type and condition: see stockpile_accepts()), or in a stockpile while a
 * better one (higher priority) accepts it and has room. Destinations are
 * chosen by stockpile priority first, then distance. These are pure queries:
 * carrying items out is the colonist's job (core/colonist.c).
 */
#ifndef COLONY_CORE_HAULING_H
#define COLONY_CORE_HAULING_H

#include "core/item.h"
#include "core/map.h"
#include "core/pathfinding.h"
#include "core/reservation.h"
#include "core/stockpile.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct HaulWorld {
    const Map *map;
    const Items *items;
    const Stockpiles *stockpiles;
    const Reservations *reservations;
    Pathfinder *pathfinder;
    uint8_t self; /* Reservation owner of the hauler. */
} HaulWorld;

typedef struct HaulJob {
    TilePos source;
    TilePos destination;
    ItemType type;
    int count; /* Items to carry: what the destination can take, at most the whole stack. */
} HaulJob;

/* Items of `stack` a tile can still take: empty, or holding a matching stack
 * (same type and grade) below the stack limit. */
int haul_storage_room(const Items *items, int x, int y, ItemStack stack);

/*
 * Items of `stack` a storage tile can take: none unless the tile is in a
 * stockpile accepting them and is empty or holds a matching stack the
 * stockpile accepts too (one waiting to be hauled out is not added to), up to
 * the stack limit.
 */
int haul_destination_room(const Stockpiles *stockpiles, const Items *items, int x, int y,
                          ItemStack stack);

/* Cheap check: is there any item whose type some stockpile with room allows?
 * (Conditions are not looked at: a true answer may still find nothing.) */
bool haul_any_job_possible(const HaulWorld *world);

/*
 * Finds the nearest stack (from `from`, within `max_cost`) that needs hauling
 * and that nobody reserved, and a destination for it. Returns false when there
 * is nothing to haul.
 */
bool haul_find_job(const HaulWorld *world, TilePos from, float max_cost, HaulJob *job);

/*
 * Best free storage tile for `stack` currently stored at priority
 * `current_priority` (0 = not stored): the highest priority stockpile better
 * than the current one and accepting the items, nearest to `from` among them.
 * Writes the path from `from` to it into `path` (may be NULL).
 */
bool haul_find_destination(const HaulWorld *world, TilePos from, ItemStack stack,
                           int current_priority, float max_cost, TilePos *out, Path *path);

#endif /* COLONY_CORE_HAULING_H */
