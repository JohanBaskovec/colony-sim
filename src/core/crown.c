#include "core/crown.h"

#include "core/tree.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

#define POOL_INITIAL 1024u

bool crowns_init(Crowns *crowns, int width, int height, int depth)
{
    memset(crowns, 0, sizeof(*crowns));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    size_t count = (size_t)width * (size_t)height * (size_t)depth;
    crowns->heads = calloc(count, sizeof(*crowns->heads));
    crowns->pool = calloc(POOL_INITIAL, sizeof(*crowns->pool));
    if (crowns->heads == NULL || crowns->pool == NULL) {
        crowns_free(crowns);
        return false;
    }
    crowns->pool_capacity = POOL_INITIAL;
    crowns->width = width;
    crowns->height = height;
    crowns->depth = depth;
    return true;
}

void crowns_free(Crowns *crowns)
{
    free(crowns->heads);
    free(crowns->pool);
    memset(crowns, 0, sizeof(*crowns));
}

static bool in_bounds(const Crowns *crowns, int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < crowns->width && y < crowns->height &&
           z < crowns->depth;
}

static size_t index_of(const Crowns *crowns, int x, int y, int z)
{
    return ((size_t)z * (size_t)crowns->height + (size_t)y) * (size_t)crowns->width + (size_t)x;
}

static void tile_of(const Crowns *crowns, size_t index, int *x, int *y, int *z)
{
    size_t level = (size_t)crowns->width * (size_t)crowns->height;
    *z = (int)(index / level);
    *y = (int)(index % level / (size_t)crowns->width);
    *x = (int)(index % (size_t)crowns->width);
}

float crown_room(Terrain terrain)
{
    switch (tree_terrain_density(terrain)) {
    case TREE_DENSITY_DENSE: return CROWN_ROOM_DENSE;
    case TREE_DENSITY_MEDIUM: return CROWN_ROOM_MEDIUM;
    case TREE_DENSITY_SPARSE: return CROWN_ROOM_SPARSE;
    case TREE_DENSITY_NONE: break;
    }
    return 0.0f;
}

/* Bulk of what hangs on a tile. */
static float tile_bulk(const Crowns *crowns, size_t index)
{
    float bulk = 0.0f;
    for (uint32_t slot = crowns->heads[index]; slot != 0; slot = crowns->pool[slot - 1].next) {
        bulk += item_def((ItemType)crowns->pool[slot - 1].type)->bulk;
    }
    return bulk;
}

int crowns_room(const Crowns *crowns, const Map *map, int x, int y, int z, ItemType type)
{
    const ItemDef *def = item_def(type);
    if (!in_bounds(crowns, x, y, z) || !map_in_bounds(map, x, y, z) || def == NULL ||
        type == ITEM_NONE || def->bulk <= 0.0f) {
        return 0;
    }
    size_t index = index_of(crowns, x, y, z);
    if (map->rock[map_index(map, x, y, z)] != ROCK_NONE) {
        return 0;
    }
    float room =
        crown_room((Terrain)map->terrain[map_index(map, x, y, z)]) - tile_bulk(crowns, index);
    return room > 0.0f ? (int)(room / def->bulk + 1e-4f) : 0;
}

/* A free slot of the pool (slot + 1), the pool grown when full; 0 when out of
 * memory. */
static uint32_t alloc_slot(Crowns *crowns)
{
    if (crowns->free_head != 0) {
        uint32_t slot = crowns->free_head;
        crowns->free_head = crowns->pool[slot - 1].next;
        return slot;
    }
    if (crowns->pool_used == crowns->pool_capacity) {
        uint32_t capacity = crowns->pool_capacity * 2u;
        CrownItem *pool = realloc(crowns->pool, (size_t)capacity * sizeof(*pool));
        if (pool == NULL) {
            return 0;
        }
        memset(pool + crowns->pool_capacity, 0,
               (size_t)(capacity - crowns->pool_capacity) * sizeof(*pool));
        crowns->pool = pool;
        crowns->pool_capacity = capacity;
    }
    return ++crowns->pool_used;
}

bool crowns_add(Crowns *crowns, const Map *map, int x, int y, int z, CrownItem item)
{
    if (crowns_room(crowns, map, x, y, z, (ItemType)item.type) < 1) {
        return false;
    }
    uint32_t slot = alloc_slot(crowns);
    if (slot == 0) {
        return false;
    }
    size_t index = index_of(crowns, x, y, z);
    item.woods = item_def((ItemType)item.type)->wooden ? item.woods : 0;
    item.tile = (uint32_t)index;
    item.next = crowns->heads[index];
    crowns->pool[slot - 1] = item;
    crowns->heads[index] = slot;
    crowns->count++;
    crowns->totals[item.type]++;
    crowns->revision++;
    return true;
}

int crowns_count(const Crowns *crowns, int x, int y, int z, ItemTypeMask types)
{
    if (!in_bounds(crowns, x, y, z)) {
        return 0;
    }
    int count = 0;
    for (uint32_t slot = crowns->heads[index_of(crowns, x, y, z)]; slot != 0;
         slot = crowns->pool[slot - 1].next) {
        count += (types & ITEM_TYPE_BIT(crowns->pool[slot - 1].type)) != 0;
    }
    return count;
}

int crowns_list(const Crowns *crowns, int x, int y, int z, CrownItem *out, int max)
{
    if (!in_bounds(crowns, x, y, z)) {
        return 0;
    }
    int count = 0;
    for (uint32_t slot = crowns->heads[index_of(crowns, x, y, z)]; slot != 0;
         slot = crowns->pool[slot - 1].next) {
        if (count < max) {
            out[count] = crowns->pool[slot - 1];
        }
        count++;
    }
    return count;
}

ItemStack crown_item_stack(CrownItem item)
{
    ItemStack stack = item_stack_make((ItemType)item.type, 1, item.quality, item.hit_points);
    stack.woods = item.woods;
    return stack;
}

/* Unlinks the slot after `previous` (0: the head) from tile `index` and frees
 * it. */
static void remove_slot(Crowns *crowns, size_t index, uint32_t previous, uint32_t slot)
{
    CrownItem *item = &crowns->pool[slot - 1];
    if (previous == 0) {
        crowns->heads[index] = item->next;
    } else {
        crowns->pool[previous - 1].next = item->next;
    }
    crowns->count--;
    crowns->totals[item->type]--;
    crowns->revision++;
    *item = (CrownItem){.type = ITEM_NONE, .next = crowns->free_head};
    crowns->free_head = slot;
}

int crowns_take(Crowns *crowns, int x, int y, int z, ItemTypeMask types, CrownItem *out, int max)
{
    if (!in_bounds(crowns, x, y, z)) {
        return 0;
    }
    size_t index = index_of(crowns, x, y, z);
    int taken = 0;
    uint32_t previous = 0, slot = crowns->heads[index];
    while (slot != 0 && taken < max) {
        uint32_t next = crowns->pool[slot - 1].next;
        if (types & ITEM_TYPE_BIT(crowns->pool[slot - 1].type)) {
            out[taken++] = crowns->pool[slot - 1];
            remove_slot(crowns, index, previous, slot);
        } else {
            previous = slot;
        }
        slot = next;
    }
    return taken;
}

static bool same_item(const CrownItem *a, const CrownItem *b)
{
    return a->type == b->type && a->quality == b->quality && a->hit_points == b->hit_points &&
           a->woods == b->woods && a->day == b->day;
}

bool crowns_copy_tile(Crowns *dst, const Crowns *src, size_t index)
{
    uint32_t a = dst->heads[index], b = src->heads[index];
    while (a != 0 && b != 0 && same_item(&dst->pool[a - 1], &src->pool[b - 1])) {
        a = dst->pool[a - 1].next;
        b = src->pool[b - 1].next;
    }
    if (a == 0 && b == 0) {
        return true; /* (Most tiles: nothing hangs there, or nothing changed.) */
    }
    while (dst->heads[index] != 0) {
        remove_slot(dst, index, 0, dst->heads[index]);
    }
    uint32_t tail = 0; /* The copy keeps the order: appended at the tail. */
    for (uint32_t from = src->heads[index]; from != 0; from = src->pool[from - 1].next) {
        uint32_t slot = alloc_slot(dst);
        if (slot == 0) {
            return false;
        }
        CrownItem item = src->pool[from - 1];
        item.next = 0;
        item.tile = (uint32_t)index;
        dst->pool[slot - 1] = item;
        if (tail == 0) {
            dst->heads[index] = slot;
        } else {
            dst->pool[tail - 1].next = slot;
        }
        tail = slot;
        dst->count++;
        dst->totals[item.type]++;
    }
    dst->revision++;
    return true;
}

bool crown_drop(const Map *map, Items *items, int x, int y, int z, CrownItem item)
{
    int level = tree_landing_level(map, x, y, z);
    if (items == NULL || level < 0) {
        return false;
    }
    return items_drop(items, map, x, y, level, crown_item_stack(item)) == 1;
}

int crowns_drop_tile(Crowns *crowns, const Map *map, Items *items, int x, int y, int z)
{
    if (!in_bounds(crowns, x, y, z)) {
        return 0;
    }
    size_t index = index_of(crowns, x, y, z);
    int fell = 0;
    while (crowns->heads[index] != 0) {
        uint32_t slot = crowns->heads[index];
        CrownItem item = crowns->pool[slot - 1];
        remove_slot(crowns, index, 0, slot);
        crown_drop(map, items, x, y, z, item);
        fell++;
    }
    return fell;
}

bool crown_fruit_in_season(ItemType type, int day_of_year)
{
    for (int plant = PLANT_TREE_FIRST; plant <= PLANT_TREE_LAST; plant++) {
        const PlantDef *def = plant_def((PlantType)plant);
        if (def->fall_item == type && type != ITEM_NONE) {
            return plant_days_contain(def->fall_days, day_of_year);
        }
    }
    return true;
}

/* Whether an item is a tree's fruit (what PlantDef.fall_item names). */
static bool is_fruit(ItemType type)
{
    for (int plant = PLANT_TREE_FIRST; plant <= PLANT_TREE_LAST; plant++) {
        if (plant_def((PlantType)plant)->fall_item == type && type != ITEM_NONE) {
            return true;
        }
    }
    return false;
}

float crown_fall_chance(CrownItem item, const CrownHour *hour)
{
    if (is_fruit((ItemType)item.type)) {
        if (!crown_fruit_in_season((ItemType)item.type, hour->day_of_year)) {
            return 1.0f;
        }
        float days = (float)(hour->day - (int)item.day);
        days = days > 0.0f ? days : 0.0f;
        return CROWN_FRUIT_FALL_CHANCE * (1.0f + days / CROWN_FRUIT_RIPENING_DAYS) *
               (hour->storm ? CROWN_STORM_FRUIT : 1.0f);
    }
    return CROWN_DEADWOOD_FALL_CHANCE * (hour->storm ? CROWN_STORM_DEADWOOD : 1.0f);
}

/* A number in [0, 1) from the seed, a slot and the hour. */
static float slot_roll(uint64_t seed, uint32_t slot, uint64_t hour)
{
    uint64_t key[3] = {seed, slot, hour ^ 0xC40Fu};
    uint64_t hash = hash_fnv1a64(HASH_FNV1A64_INIT, key, sizeof(key));
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    return (float)(hash >> 40) / (float)(1u << 24);
}

/* The slot before `slot` in its tile's list (0 for the head). */
static uint32_t previous_slot(const Crowns *crowns, size_t index, uint32_t slot)
{
    uint32_t previous = 0;
    for (uint32_t at = crowns->heads[index]; at != 0 && at != slot;
         at = crowns->pool[at - 1].next) {
        previous = at;
    }
    return previous;
}

int crowns_hour(Crowns *crowns, const Map *map, Items *items, uint32_t begin, uint32_t end,
                const CrownHour *hour)
{
    int fell = 0;
    end = end < crowns->pool_used ? end : crowns->pool_used;
    for (uint32_t i = begin; i < end; i++) {
        CrownItem item = crowns->pool[i];
        if (item.type == ITEM_NONE) {
            continue;
        }
        uint32_t slot = i + 1;
        int x, y, z;
        tile_of(crowns, item.tile, &x, &y, &z);
        bool foliage = map->rock[item.tile] == ROCK_NONE &&
                       crown_room((Terrain)map->terrain[item.tile]) > 0.0f;
        if (foliage && slot_roll(hour->seed, slot, hour->hour) >= crown_fall_chance(item, hour)) {
            continue;
        }
        remove_slot(crowns, item.tile, previous_slot(crowns, item.tile, slot), slot);
        crown_drop(map, items, x, y, z, item);
        fell++;
    }
    return fell;
}

uint64_t crowns_hash(const Crowns *crowns, uint64_t hash)
{
    if (crowns->count == 0) {
        return hash; /* Hashes without anything hanging stay as they were. */
    }
    size_t tiles = (size_t)crowns->width * (size_t)crowns->height * (size_t)crowns->depth;
    for (size_t index = 0; index < tiles; index++) {
        for (uint32_t slot = crowns->heads[index]; slot != 0; slot = crowns->pool[slot - 1].next) {
            const CrownItem *item = &crowns->pool[slot - 1];
            uint64_t key[3] = {index,
                               (uint64_t)item->type | (uint64_t)item->quality << 8 |
                                   (uint64_t)item->hit_points << 16 | (uint64_t)item->woods << 32,
                               item->day};
            hash = hash_fnv1a64(hash, key, sizeof(key));
        }
    }
    return hash;
}

bool crowns_check(const Crowns *crowns)
{
    long totals[ITEM_TYPE_COUNT] = {0};
    long count = 0;
    size_t tiles = (size_t)crowns->width * (size_t)crowns->height * (size_t)crowns->depth;
    for (size_t index = 0; index < tiles; index++) {
        for (uint32_t slot = crowns->heads[index]; slot != 0; slot = crowns->pool[slot - 1].next) {
            const CrownItem *item = &crowns->pool[slot - 1];
            if (slot > crowns->pool_used || item->type == ITEM_NONE ||
                item->type >= ITEM_TYPE_COUNT || item->tile != index || count > crowns->count) {
                return false;
            }
            totals[item->type]++;
            count++;
        }
    }
    return count == crowns->count && memcmp(totals, crowns->totals, sizeof(totals)) == 0;
}
