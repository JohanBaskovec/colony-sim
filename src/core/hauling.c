#include "core/hauling.h"

#include "core/util.h"

/* Where items can go right now. Built at the start of each search by visiting
 * the tiles of every stockpile (never the whole map). */
typedef struct StorageLevels {
    /* For each item type: bit p set when some stockpile of priority p allows
     * the type and has a free (not reserved) tile with room for it. Conditions
     * are not taken into account: a quick upper bound. */
    uint32_t levels[ITEM_TYPE_COUNT];
    /* Stockpiles with room for something, and for what: room[i][type] is
     * set when stockpile zones[i] has room for items of that type. */
    int zones[STOCKPILE_MAX];
    bool room[STOCKPILE_MAX][ITEM_TYPE_COUNT];
    int zone_count;
} StorageLevels;

int haul_storage_room(const Items *items, int x, int y, int z, ItemStack stack)
{
    if (item_stack_limit((ItemType)stack.type) == 0 || items_any_forbidden(items, x, y, z)) {
        return 0; /* Forbidden items are left alone: nothing is added to them. */
    }
    /* Storage keeps one group per tile. */
    ItemStack there = items_get(items, x, y, z);
    if (there.type != ITEM_NONE &&
        (!item_stacks_match(there, stack) || items_mixed(items, x, y, z))) {
        return 0;
    }
    return items_room(items, x, y, z, stack);
}

int haul_destination_room(const Stockpiles *stockpiles, const Items *items, int x, int y, int z,
                          ItemStack stack)
{
    const Stockpile *stockpile = stockpile_get(stockpiles, stockpile_at(stockpiles, x, y, z));
    if (!stockpile_accepts(stockpile, stack) || stockpile->forbidden) {
        return 0;
    }
    ItemStack there = items_get(items, x, y, z);
    if (there.type != ITEM_NONE && !stockpile_accepts(stockpile, there)) {
        return 0;
    }
    return haul_storage_room(items, x, y, z, stack);
}

bool haul_drop_filter(const void *data, const Items *items, int x, int y, int z,
                      const ItemStack *load)
{
    const Stockpiles *stockpiles = data;
    return stockpile_at(stockpiles, x, y, z) == STOCKPILE_NONE ||
           haul_destination_room(stockpiles, items, x, y, z, *load) > 0;
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
        if (stockpile == NULL || stockpile->allowed == 0 || stockpile->forbidden) {
            continue; /* Forbidden: nothing is brought there. */
        }
        bool *room = out->room[out->zone_count];
        for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
            room[type] = false;
        }
        bool any_room = false, empty_tile = false;
        for (int z = stockpile->min_z; z <= stockpile->max_z && !empty_tile; z++) {
            for (int y = stockpile->min_y; y <= stockpile->max_y && !empty_tile; y++) {
                for (int x = stockpile->min_x; x <= stockpile->max_x; x++) {
                    if (stockpile_at(stockpiles, x, y, z) != index ||
                        (any_reserved &&
                         reservations_taken_by_other(world->reservations, RESERVATION_STORAGE,
                                                     (TilePos){(int16_t)x, (int16_t)y, (int16_t)z},
                                                     world->self))) {
                        continue; /* Other zone, or someone is already bringing items there. */
                    }
                    if (items_any_forbidden(world->items, x, y, z)) {
                        continue; /* Left alone: no room there. */
                    }
                    ItemStack stack = items_get(world->items, x, y, z);
                    if (stack.type == ITEM_NONE) {
                        empty_tile = true; /* Takes anything allowed: no need to look further. */
                        break;
                    }
                    if (stockpile_accepts(stockpile, stack) &&
                        haul_storage_room(world->items, x, y, z, stack) > 0) {
                        room[stack.type] = true;
                        any_room = true;
                    }
                }
            }
        }
        if (empty_tile) {
            for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
                if (stockpile->allowed & ITEM_TYPE_BIT(type)) {
                    room[type] = true;
                    any_room = true;
                }
            }
        }
        if (!any_room) {
            continue;
        }
        out->zones[out->zone_count++] = index;
        for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
            if (room[type]) {
                out->levels[type] |= 1u << stockpile->priority;
            }
        }
    }
}

/* Priority levels (bit p for priority p) of the stockpiles with room that
 * accept this very stack, condition included, lying on (x, y, z): those
 * whose radius reaches it. */
static uint32_t levels_for(const HaulWorld *world, const StorageLevels *storage, ItemStack stack,
                           int x, int y, int z)
{
    if (storage->levels[stack.type] == 0) {
        return 0;
    }
    uint32_t levels = 0;
    for (int i = 0; i < storage->zone_count; i++) {
        const Stockpile *stockpile = stockpile_get(world->stockpiles, storage->zones[i]);
        if (storage->room[i][stack.type] && stockpile_accepts(stockpile, stack) &&
            stockpile_in_radius(stockpile, x, y, z)) {
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

typedef struct SourceSearch {
    const HaulWorld *world;
    const StorageLevels *storage;
} SourceSearch;

static bool is_haul_source(const Map *map, int x, int y, int z, void *userdata);

bool haul_any_job_possible(const HaulWorld *world)
{
    if (world->stockpiles->used_count == 0) {
        return false;
    }
    StorageLevels storage;
    storage_levels(world, &storage);
    bool any_type = false;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT && !any_type; type++) {
        any_type = storage.levels[type] != 0 && world->items->totals[type] > 0;
    }
    if (!any_type) {
        return false;
    }
    const Map *map = world->map;
    const uint32_t *regions = NULL;
    if (world->regions != NULL && world->region != REGION_NONE &&
        regions_current(world->regions, map)) {
        regions = world->regions->id;
    }
    SourceSearch search = {world, &storage};
    if (world->from_z < 0 || world->from_z >= map->depth) {
        return false;
    }
    /* Only stacks in the hauler's region count: within its box. */
    TileBox box = regions != NULL
                      ? regions_box(world->regions, map, world->region)
                      : (TileBox){0, 0, 0, map->width - 1, map->height - 1, map->depth - 1};
    /* The hauler's level first, from its row outwards: a stack nearby is
     * found at once. */
    for (int level = 0; level < map->depth; level++) {
        int z = (world->from_z + level) % map->depth;
        if (world->items->level_stacks[z] == 0 || z < box.z0 || z > box.z1) {
            continue;
        }
        for (int step = 0; step < 2 * map->height; step++) {
            int y = row_outwards(world->from_y, step, map->height);
            if (y < box.y0 || y > box.y1) {
                continue; /* Out of the map (-1) or of the region. */
            }
            const ItemStack *row = &world->items->stacks[map_index(map, 0, y, z)];
            const uint8_t *mixed = &world->items->mixed[map_index(map, 0, y, z)];
            for (int x = box.x0; x <= box.x1; x++) {
                if (row[x].type == ITEM_NONE || (storage.levels[row[x].type] == 0 && !mixed[x]) ||
                    (regions != NULL && regions[map_index(map, x, y, z)] != world->region)) {
                    continue;
                }
                if (is_haul_source(map, x, y, z, &search)) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* Groups of a tile looked at for hauling (the newest first). */
#define HAUL_GROUPS_SEEN 8

/* The newest group of allowed items lying on a tile that some better
 * storage wants: an empty stack for none. */
static ItemStack haulable_group(const HaulWorld *world, const StorageLevels *storage, int x, int y,
                                int z)
{
    ItemStack groups[HAUL_GROUPS_SEEN];
    int count = 0;
    if (!items_forbidden(world->items, x, y, z)) {
        /* The newest group first, on its own: most tiles hold only it. */
        groups[0] = items_get_allowed(world->items, x, y, z);
        count = groups[0].type != ITEM_NONE;
        if (count > 0 && items_mixed(world->items, x, y, z)) {
            int current = stockpile_storage_priority(world->stockpiles, x, y, z, groups[0]);
            if (storage->levels[groups[0].type] != 0 &&
                has_better_level(levels_for(world, storage, groups[0], x, y, z), current)) {
                return groups[0];
            }
            count = items_groups(world->items, x, y, z, groups, HAUL_GROUPS_SEEN);
        }
    }
    for (int i = 0; i < count; i++) {
        if (storage->levels[groups[i].type] == 0 || groups[i].forbidden) {
            continue;
        }
        int current = stockpile_storage_priority(world->stockpiles, x, y, z, groups[i]);
        if (has_better_level(levels_for(world, storage, groups[i], x, y, z), current)) {
            return groups[i];
        }
    }
    return (ItemStack){.type = ITEM_NONE};
}

static bool is_haul_source(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const SourceSearch *search = userdata;
    const HaulWorld *world = search->world;
    ItemStack stack = items_get_allowed(world->items, x, y, z);
    if (stack.type == ITEM_NONE ||
        (search->storage->levels[stack.type] == 0 && !items_mixed(world->items, x, y, z))) {
        return false;
    }
    if (stockpile_forbidden_at(world->stockpiles, x, y, z)) {
        return false; /* Nothing leaves a forbidden stockpile. */
    }
    return haulable_group(world, search->storage, x, y, z).type != ITEM_NONE &&
           !reservations_taken_by_other(world->reservations, RESERVATION_HAUL_SOURCE,
                                        (TilePos){(int16_t)x, (int16_t)y, (int16_t)z}, world->self);
}

typedef struct DestinationSearch {
    const HaulWorld *world;
    ItemStack stack;
    TilePos from; /* Where the items are: the stockpile's radius must reach it. */
    int priority;
} DestinationSearch;

static bool is_destination(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const DestinationSearch *search = userdata;
    const HaulWorld *world = search->world;
    const Stockpile *stockpile =
        stockpile_get(world->stockpiles, stockpile_at(world->stockpiles, x, y, z));
    return stockpile != NULL && stockpile_accepts(stockpile, search->stack) &&
           stockpile->priority == search->priority &&
           stockpile_in_radius(stockpile, search->from.x, search->from.y, search->from.z) &&
           haul_destination_room(world->stockpiles, world->items, x, y, z, search->stack) > 0 &&
           !reservations_taken_by_other(world->reservations, RESERVATION_STORAGE,
                                        (TilePos){(int16_t)x, (int16_t)y, (int16_t)z}, world->self);
}

bool haul_find_destination(const HaulWorld *world, TilePos from, ItemStack stack,
                           int current_priority, float max_cost, TilePos *out, Path *path)
{
    if (stack.type == ITEM_NONE || (unsigned)stack.type >= ITEM_TYPE_COUNT) {
        return false;
    }
    StorageLevels storage;
    storage_levels(world, &storage);
    uint32_t levels = levels_for(world, &storage, stack, from.x, from.y, from.z);
    int worst = current_priority == 0 ? STOCKPILE_PRIORITY_LOWEST : current_priority - 1;
    DestinationSearch search = {world, stack, from, 0};
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
    ItemStack stack = haulable_group(world, &storage, source.x, source.y, source.z);
    int current =
        stockpile_storage_priority(world->stockpiles, source.x, source.y, source.z, stack);
    TilePos destination;
    if (!haul_find_destination(world, source, stack, current, max_cost, &destination, NULL)) {
        return false;
    }
    int room = haul_destination_room(world->stockpiles, world->items, destination.x, destination.y,
                                     destination.z, stack);
    job->source = source;
    job->destination = destination;
    job->type = (ItemType)stack.type;
    job->count = stack.count < room ? stack.count : room;
    return true;
}

typedef struct ExtraSearch {
    const HaulWorld *world;
    ItemStack load;
    const Stockpile *zone; /* Of the destination. */
    const StorageLevels *storage;
} ExtraSearch;

static bool is_extra_source(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const ExtraSearch *search = userdata;
    const HaulWorld *world = search->world;
    /* The load's group: allowed items, as the load is. */
    ItemStack stack = items_get_group(world->items, x, y, z, search->load);
    if (stack.type == ITEM_NONE || !stockpile_accepts(search->zone, stack) ||
        !stockpile_in_radius(search->zone, x, y, z)) {
        return false;
    }
    if (stockpile_forbidden_at(world->stockpiles, x, y, z)) {
        return false;
    }
    int current = stockpile_storage_priority(world->stockpiles, x, y, z, stack);
    if (current != 0 && current <= search->zone->priority) {
        return false; /* Already stored as well (the destination itself included). */
    }
    if (has_better_level(levels_for(world, search->storage, stack, x, y, z),
                         search->zone->priority)) {
        return false; /* A better stockpile wants it: it goes there, on its own trip. */
    }
    return !reservations_taken_by_other(world->reservations, RESERVATION_HAUL_SOURCE,
                                        (TilePos){(int16_t)x, (int16_t)y, (int16_t)z}, world->self);
}

bool haul_find_extra_source(const HaulWorld *world, TilePos from, ItemStack load,
                            TilePos destination, float max_cost, TilePos *out, Path *path)
{
    const Stockpile *zone =
        stockpile_get(world->stockpiles,
                      stockpile_at(world->stockpiles, destination.x, destination.y, destination.z));
    if (zone == NULL || load.type == ITEM_NONE) {
        return false;
    }
    StorageLevels storage;
    storage_levels(world, &storage);
    ExtraSearch search = {world, load, zone, &storage};
    return pathfinder_find_nearest(world->pathfinder, world->map, from, is_extra_source, &search,
                                   max_cost, path, out);
}
