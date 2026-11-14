/*
 * Hauling: finding items to move into storage, and where to put them.
 *
 * An item stack needs hauling when it lies outside any stockpile accepting it,
 * or in a stockpile while a better one (higher priority) accepts it and has
 * room. Destinations are chosen by stockpile priority first, then distance.
 * These are pure queries: carrying items out is the colonist's job
 * (core/colonist.c).
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

/* Items of `type` a tile can still take: empty or same type below the stack limit. */
int haul_storage_room(const Items *items, int x, int y, ItemType type);

/* Cheap check: is there any item that some stockpile with room accepts? */
bool haul_any_job_possible(const HaulWorld *world);

/*
 * Finds the nearest stack (from `from`, within `max_cost`) that needs hauling
 * and that nobody reserved, and a destination for it. Returns false when there
 * is nothing to haul.
 */
bool haul_find_job(const HaulWorld *world, TilePos from, float max_cost, HaulJob *job);

/*
 * Best free storage tile for items of `type` currently stored at priority
 * `current_priority` (0 = not stored): the highest priority stockpile better
 * than the current one, nearest to `from` among them. Writes the path from
 * `from` to it into `path` (may be NULL).
 */
bool haul_find_destination(const HaulWorld *world, TilePos from, ItemType type,
                           int current_priority, float max_cost, TilePos *out, Path *path);

#endif /* COLONY_CORE_HAULING_H */
