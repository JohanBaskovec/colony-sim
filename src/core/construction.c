#include "core/construction.h"

#include "core/util.h"

#include <stdlib.h>
#include <string.h>

bool constructions_init(Constructions *constructions, int width, int height)
{
    memset(constructions, 0, sizeof(*constructions));
    if (width <= 0 || height <= 0) {
        return false;
    }
    constructions->at = calloc((size_t)width * (size_t)height, sizeof(*constructions->at));
    if (constructions->at == NULL) {
        return false;
    }
    constructions->width = width;
    constructions->height = height;
    constructions->next_number = 1;
    return true;
}

void constructions_free(Constructions *constructions)
{
    free(constructions->at);
    memset(constructions, 0, sizeof(*constructions));
}

static bool in_bounds(const Constructions *constructions, int x, int y)
{
    return x >= 0 && y >= 0 && x < constructions->width && y < constructions->height;
}

static size_t index_of(const Constructions *constructions, int x, int y)
{
    return (size_t)y * (size_t)constructions->width + (size_t)x;
}

int construction_at(const Constructions *constructions, int x, int y)
{
    if (!in_bounds(constructions, x, y)) {
        return CONSTRUCTION_NONE;
    }
    return (int)constructions->at[index_of(constructions, x, y)] - 1;
}

static ConstructionSite *site_get(Constructions *constructions, int index)
{
    if (index < 0 || index >= CONSTRUCTION_MAX || !constructions->list[index].used) {
        return NULL;
    }
    return &constructions->list[index];
}

const ConstructionSite *construction_get(const Constructions *constructions, int index)
{
    return site_get((Constructions *)constructions, index);
}

bool construction_can_place(const Constructions *constructions, const Map *map, int x, int y)
{
    if (!in_bounds(constructions, x, y) || !map_is_passable(map, x, y)) {
        return false;
    }
    size_t index = map_index(map, x, y);
    PlantType plant = (PlantType)map->plants[index].type;
    return terrain_def((Terrain)map->terrain[index])->buildable &&
           map->structure[index] == STRUCTURE_NONE && plant != PLANT_TREE &&
           plant != PLANT_BUSH && construction_at(constructions, x, y) == CONSTRUCTION_NONE;
}

int constructions_place(Constructions *constructions, const Map *map, StructureType type, int x,
                        int y)
{
    if (type == STRUCTURE_NONE || (unsigned)type >= STRUCTURE_TYPE_COUNT ||
        !construction_can_place(constructions, map, x, y)) {
        return CONSTRUCTION_NONE;
    }
    for (int index = 0; index < CONSTRUCTION_MAX; index++) {
        ConstructionSite *site = &constructions->list[index];
        if (site->used) {
            continue;
        }
        *site = (ConstructionSite){.used = true,
                                   .type = (uint8_t)type,
                                   .x = (int16_t)x,
                                   .y = (int16_t)y,
                                   .number = constructions->next_number++};
        for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
            site->delivered[slot] = (ItemStack){.type = ITEM_NONE};
        }
        constructions->at[index_of(constructions, x, y)] = (uint16_t)(index + 1);
        constructions->used_count++;
        constructions->revision++;
        return index;
    }
    return CONSTRUCTION_NONE;
}

int constructions_remove(Constructions *constructions, int index,
                         ItemStack returned[STRUCTURE_MATERIAL_SLOTS])
{
    ConstructionSite *site = site_get(constructions, index);
    if (site == NULL) {
        return 0;
    }
    int count = 0;
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        if (site->delivered[slot].count > 0) {
            returned[count++] = site->delivered[slot];
        }
    }
    constructions->at[index_of(constructions, site->x, site->y)] = 0;
    memset(site, 0, sizeof(*site));
    constructions->used_count--;
    constructions->revision++;
    return count;
}

int construction_missing(const ConstructionSite *site, int slot)
{
    const StructureDef *def = structure_def((StructureType)site->type);
    if (def == NULL || slot < 0 || slot >= STRUCTURE_MATERIAL_SLOTS ||
        def->materials[slot].type == ITEM_NONE) {
        return 0;
    }
    int missing = def->materials[slot].count - (int)site->delivered[slot].count;
    return missing > 0 ? missing : 0;
}

bool construction_materials_complete(const ConstructionSite *site)
{
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        if (construction_missing(site, slot) > 0) {
            return false;
        }
    }
    return true;
}

int construction_deliver(Constructions *constructions, int index, ItemStack stack)
{
    ConstructionSite *site = site_get(constructions, index);
    if (site == NULL || stack.count == 0) {
        return 0;
    }
    const StructureDef *def = structure_def((StructureType)site->type);
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        int missing = construction_missing(site, slot);
        if (def->materials[slot].type != stack.type || missing <= 0) {
            continue;
        }
        ItemStack taken = stack;
        taken.count = (uint16_t)(stack.count < missing ? stack.count : missing);
        ItemStack *into = &site->delivered[slot];
        if (into->count > 0) {
            /* Materials of any grade go into the same structure. */
            uint8_t lowest = into->quality < taken.quality ? into->quality : taken.quality;
            into->quality = lowest;
            taken.quality = lowest;
        }
        item_stack_merge(into, taken);
        constructions->revision++;
        return taken.count;
    }
    return 0;
}

/* A blocking structure leaves no room for items: they go next door. */
static void clear_items(Map *map, Items *items, int x, int y)
{
    ItemStack stack = items_get(items, x, y);
    if (stack.count == 0) {
        return;
    }
    static const int DX[4] = {0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1, 0};
    for (int i = 0; i < 4; i++) {
        if (map_is_passable(map, x + DX[i], y + DY[i])) {
            items_remove(items, x, y, stack.count);
            int placed = items_drop(items, map, x + DX[i], y + DY[i], stack);
            if (placed < stack.count) {
                stack.count = (uint16_t)(stack.count - placed);
                items_add(items, x, y, stack); /* No room: what is left stays. */
            }
            return;
        }
    }
}

bool construction_add_work(Constructions *constructions, Map *map, Items *items, int index,
                           float work)
{
    ConstructionSite *site = site_get(constructions, index);
    if (site == NULL || !construction_materials_complete(site)) {
        return false;
    }
    StructureType type = (StructureType)site->type;
    site->progress += work;
    if (site->progress < structure_def(type)->build_work) {
        return false;
    }
    int x = site->x, y = site->y;
    ItemStack unused[STRUCTURE_MATERIAL_SLOTS];
    constructions_remove(constructions, index, unused); /* Used up. */
    map_set_structure(map, x, y, type);
    if (structure_blocks(type) && items != NULL) {
        clear_items(map, items, x, y);
    }
    return true;
}

float construction_work_fraction(const ConstructionSite *site)
{
    float needed = structure_def((StructureType)site->type)->build_work;
    return needed > 0.0f ? clampf(site->progress / needed, 0.0f, 1.0f) : 1.0f;
}

uint64_t constructions_hash(const Constructions *constructions, uint64_t hash)
{
    /* Field by field: struct padding bytes are unspecified. */
    for (int index = 0; index < CONSTRUCTION_MAX; index++) {
        const ConstructionSite *site = &constructions->list[index];
        if (!site->used) {
            continue;
        }
        int32_t ints[5] = {index, site->type, site->x, site->y, site->number};
        hash = hash_fnv1a64(hash, ints, sizeof(ints));
        for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
            ItemStack stack = site->delivered[slot];
            int32_t fields[4] = {stack.type, stack.quality, stack.count, stack.hit_points};
            hash = hash_fnv1a64(hash, fields, sizeof(fields));
        }
        hash = hash_fnv1a64(hash, &site->progress, sizeof(site->progress));
    }
    return hash;
}
