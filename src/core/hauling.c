#include "core/hauling.h"

/* Where items can go right now. Built at the start of each search by visiting
 * the tiles of every stockpile (never the whole map). */
typedef struct StorageLevels {
    /* For each item type: bit p set when some stockpile of priority p allows
     * the type and has a free (not reserved) tile with room for it. Conditions
     * are not taken into account: a quick upper bound. */
    uint32_t levels[ITEM_TYPE_COUNT];
    /* Stockpiles with room for something, and for what: bit g of
     * grades[i][type] when stockpile zones[i] has room for items of that type
     * and grade (an empty tile has room for every grade). */
    int zones[STOCKPILE_MAX];
    uint8_t grades[STOCKPILE_MAX][ITEM_TYPE_COUNT];
    int zone_count;
} StorageLevels;

#define ALL_GRADES ((uint8_t)((1u << ITEM_QUALITY_COUNT) - 1u))
_Static_assert(ITEM_QUALITY_COUNT <= 8, "grade masks are bytes");

int haul_storage_room(const Items *items, int x, int y, ItemStack stack)
{
    int limit = item_stack_limit((ItemType)stack.type);
    if (limit == 0) {
        return 0;
    }
    ItemStack there = items_get(items, x, y);
    if (there.type != ITEM_NONE && !item_stacks_match(there, stack)) {
        return 0;
    }
    return limit - (int)there.count;
}

int haul_destination_room(const Stockpiles *stockpiles, const Items *items, int x, int y,
                          ItemStack stack)
{
    const Stockpile *stockpile = stockpile_get(stockpiles, stockpile_at(stockpiles, x, y));
    if (!stockpile_accepts(stockpile, stack)) {
        return 0;
    }
    ItemStack there = items_get(items, x, y);
    if (there.type != ITEM_NONE && !stockpile_accepts(stockpile, there)) {
        return 0;
    }
    return haul_storage_room(items, x, y, stack);
}

static void storage_levels(const HaulWorld *world, StorageLevels *out)
{
    const Stockpiles *stockpiles = world->stockpiles;
    for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
        out->levels[type] = 0;
    }
    out->zone_count = 0;
    bool any_reserved = world->reservations->count_by_kind[RESERVATION_STORAGE] > 0;
    for (int index = 0; index < STOCKPILE_MAX; index++) {
        const Stockpile *stockpile = stockpile_get(stockpiles, index);
        if (stockpile == NULL || stockpile->allowed == 0) {
            continue;
        }
        uint8_t *grades = out->grades[out->zone_count];
        for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
            grades[type] = 0;
        }
        bool any_room = false, empty_tile = false;
        for (int y = stockpile->min_y; y <= stockpile->max_y && !empty_tile; y++) {
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
                    empty_tile = true; /* Takes anything allowed: no need to look further. */
                    break;
                }
                if (stockpile_accepts(stockpile, stack) &&
                    (int)stack.count < item_stack_limit((ItemType)stack.type)) {
                    grades[stack.type] |= (uint8_t)(1u << stack.quality);
                    any_room = true;
                }
            }
        }
        if (empty_tile) {
            for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
                if (stockpile->allowed & ITEM_TYPE_BIT(type)) {
                    grades[type] = ALL_GRADES;
                    any_room = true;
                }
            }
        }
        if (!any_room) {
            continue;
        }
        out->zones[out->zone_count++] = index;
        for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
            if (grades[type] != 0) {
                out->levels[type] |= 1u << stockpile->priority;
            }
        }
    }
}

/* Priority levels (bit p for priority p) of the stockpiles with room that
 * accept this very stack, condition included. */
static uint32_t levels_for(const HaulWorld *world, const StorageLevels *storage, ItemStack stack)
{
    if (storage->levels[stack.type] == 0) {
        return 0;
    }
    uint32_t levels = 0;
    for (int i = 0; i < storage->zone_count; i++) {
        const Stockpile *stockpile = stockpile_get(world->stockpiles, storage->zones[i]);
        if ((storage->grades[i][stack.type] & (1u << stack.quality)) &&
            stockpile_accepts(stockpile, stack)) {
            levels |= 1u << stockpile->priority;
        }
    }
    return levels;
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
    if (stack.type == ITEM_NONE || search->storage->levels[stack.type] == 0) {
        return false;
    }
    int current = stockpile_storage_priority(world->stockpiles, x, y, stack);
    return has_better_level(levels_for(world, search->storage, stack), current) &&
           !reservations_taken_by_other(world->reservations, RESERVATION_HAUL_SOURCE,
                                        (TilePos){(int16_t)x, (int16_t)y}, world->self);
}

typedef struct DestinationSearch {
    const HaulWorld *world;
    ItemStack stack;
    int priority;
} DestinationSearch;

static bool is_destination(const Map *map, int x, int y, void *userdata)
{
    (void)map;
    const DestinationSearch *search = userdata;
    const HaulWorld *world = search->world;
    return stockpile_storage_priority(world->stockpiles, x, y, search->stack) ==
               search->priority &&
           haul_destination_room(world->stockpiles, world->items, x, y, search->stack) > 0 &&
           !reservations_taken_by_other(world->reservations, RESERVATION_STORAGE,
                                        (TilePos){(int16_t)x, (int16_t)y}, world->self);
}

bool haul_find_destination(const HaulWorld *world, TilePos from, ItemStack stack,
                           int current_priority, float max_cost, TilePos *out, Path *path)
{
    if (stack.type == ITEM_NONE || (unsigned)stack.type >= ITEM_TYPE_COUNT) {
        return false;
    }
    StorageLevels storage;
    storage_levels(world, &storage);
    uint32_t levels = levels_for(world, &storage, stack);
    int worst = current_priority == 0 ? STOCKPILE_PRIORITY_LOWEST : current_priority - 1;
    DestinationSearch search = {world, stack, 0};
    for (int priority = STOCKPILE_PRIORITY_HIGHEST; priority <= worst; priority++) {
        if (!(levels & (1u << priority))) {
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
    int current = stockpile_storage_priority(world->stockpiles, source.x, source.y, stack);
    TilePos destination;
    if (!haul_find_destination(world, source, stack, current, max_cost, &destination, NULL)) {
        return false;
    }
    int room =
        haul_destination_room(world->stockpiles, world->items, destination.x, destination.y, stack);
    job->source = source;
    job->destination = destination;
    job->type = (ItemType)stack.type;
    job->count = stack.count < room ? stack.count : room;
    return true;
}
