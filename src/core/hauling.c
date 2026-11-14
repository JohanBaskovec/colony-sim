#include "core/hauling.h"

/* For each item type: bit p set when some stockpile of priority p accepts the
 * type and has a free (not reserved) tile with room for it. */
typedef struct StorageLevels {
    uint32_t levels[ITEM_TYPE_COUNT];
} StorageLevels;

int haul_storage_room(const Items *items, int x, int y, ItemType type)
{
    if (item_stack_limit(type) == 0) {
        return 0;
    }
    ItemStack stack = items_get(items, x, y);
    if (stack.type != ITEM_NONE && stack.type != type) {
        return 0;
    }
    return item_stack_limit(type) - (int)stack.count;
}

static void storage_levels(const HaulWorld *world, StorageLevels *out)
{
    const Stockpiles *stockpiles = world->stockpiles;
    for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
        out->levels[type] = 0;
    }
    bool any_reserved = world->reservations->count_by_kind[RESERVATION_STORAGE] > 0;
    /* Only the tiles of each zone are visited (through its bounding box),
     * never the whole map. */
    for (int index = 0; index < STOCKPILE_MAX; index++) {
        const Stockpile *stockpile = stockpile_get(stockpiles, index);
        if (stockpile == NULL || stockpile->allowed == 0) {
            continue;
        }
        for (int y = stockpile->min_y; y <= stockpile->max_y; y++) {
            for (int x = stockpile->min_x; x <= stockpile->max_x; x++) {
                if (stockpile_at(stockpiles, x, y) != index ||
                    (any_reserved &&
                     reservations_taken_by_other(world->reservations, RESERVATION_STORAGE,
                                                 (TilePos){(int16_t)x, (int16_t)y},
                                                 world->self))) {
                    continue; /* Other zone, or someone is already bringing items there. */
                }
                ItemStack stack = items_get(world->items, x, y);
                if (stack.type == ITEM_NONE) {
                    /* An empty tile takes any allowed type. */
                    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
                        if (stockpile->allowed & ITEM_TYPE_BIT(type)) {
                            out->levels[type] |= 1u << stockpile->priority;
                        }
                    }
                } else if ((stockpile->allowed & ITEM_TYPE_BIT(stack.type)) &&
                           (int)stack.count < item_stack_limit((ItemType)stack.type)) {
                    out->levels[stack.type] |= 1u << stockpile->priority;
                }
            }
        }
    }
}

/* Whether some level strictly better than `current` (0 = not stored) has room. */
static bool has_better_level(uint32_t levels, int current)
{
    if (current == 0) {
        return levels != 0;
    }
    return (levels & ((1u << current) - 1u)) != 0; /* Bits 1..current-1. */
}

bool haul_any_job_possible(const HaulWorld *world)
{
    if (world->stockpiles->used_count == 0) {
        return false;
    }
    StorageLevels storage;
    storage_levels(world, &storage);
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        if (storage.levels[type] != 0 && world->items->totals[type] > 0) {
            return true;
        }
    }
    return false;
}

typedef struct SourceSearch {
    const HaulWorld *world;
    const StorageLevels *storage;
} SourceSearch;

static bool is_haul_source(const Map *map, int x, int y, void *userdata)
{
    (void)map;
    const SourceSearch *search = userdata;
    const HaulWorld *world = search->world;
    ItemStack stack = items_get(world->items, x, y);
    if (stack.type == ITEM_NONE) {
        return false;
    }
    int current = stockpile_storage_priority(world->stockpiles, x, y, (ItemType)stack.type);
    return has_better_level(search->storage->levels[stack.type], current) &&
           !reservations_taken_by_other(world->reservations, RESERVATION_HAUL_SOURCE,
                                        (TilePos){(int16_t)x, (int16_t)y}, world->self);
}

typedef struct DestinationSearch {
    const HaulWorld *world;
    ItemType type;
    int priority;
} DestinationSearch;

static bool is_destination(const Map *map, int x, int y, void *userdata)
{
    (void)map;
    const DestinationSearch *search = userdata;
    const HaulWorld *world = search->world;
    return stockpile_storage_priority(world->stockpiles, x, y, search->type) == search->priority &&
           haul_storage_room(world->items, x, y, search->type) > 0 &&
           !reservations_taken_by_other(world->reservations, RESERVATION_STORAGE,
                                        (TilePos){(int16_t)x, (int16_t)y}, world->self);
}

bool haul_find_destination(const HaulWorld *world, TilePos from, ItemType type,
                           int current_priority, float max_cost, TilePos *out, Path *path)
{
    StorageLevels storage;
    storage_levels(world, &storage);
    int worst = current_priority == 0 ? STOCKPILE_PRIORITY_LOWEST : current_priority - 1;
    DestinationSearch search = {world, type, 0};
    for (int priority = STOCKPILE_PRIORITY_HIGHEST; priority <= worst; priority++) {
        if (!(storage.levels[type] & (1u << priority))) {
            continue;
        }
        search.priority = priority;
        if (pathfinder_find_nearest(world->pathfinder, world->map, from, is_destination, &search,
                                    max_cost, path, out)) {
            return true;
        }
    }
    return false;
}

bool haul_find_job(const HaulWorld *world, TilePos from, float max_cost, HaulJob *job)
{
    StorageLevels storage;
    storage_levels(world, &storage);
    SourceSearch search = {world, &storage};
    TilePos source;
    if (!pathfinder_find_nearest(world->pathfinder, world->map, from, is_haul_source, &search,
                                 max_cost, NULL, &source)) {
        return false;
    }
    ItemStack stack = items_get(world->items, source.x, source.y);
    ItemType type = (ItemType)stack.type;
    int current = stockpile_storage_priority(world->stockpiles, source.x, source.y, type);
    TilePos destination;
    if (!haul_find_destination(world, source, type, current, max_cost, &destination, NULL)) {
        return false;
    }
    int room = haul_storage_room(world->items, destination.x, destination.y, type);
    job->source = source;
    job->destination = destination;
    job->type = type;
    job->count = stack.count < room ? stack.count : room;
    return true;
}
