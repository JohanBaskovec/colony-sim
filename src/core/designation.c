#include "core/designation.h"

#include "core/i18n.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

static const DesignationDef DESIGNATION_DEFS[DESIGNATION_COUNT] = {
    [DESIGNATION_NONE] = {"none", NULL, NULL, WORK_TYPE_COUNT, KNOWLEDGE_NONE, 0.0f},
    /* Mining needs a pick and the Mining knowledge: neither exists yet. */
    [DESIGNATION_MINE] = {"mine", N_("designation.mine"), N_("colonist.task.mine"), WORK_MINING,
                          KNOWLEDGE_NONE, KNOWLEDGE_MASTERY_LEVEL},
    [DESIGNATION_DECONSTRUCT] = {"deconstruct", N_("designation.deconstruct"),
                                 N_("colonist.task.deconstruct"), WORK_CONSTRUCTION,
                                 KNOWLEDGE_BASIC_SHELTERS, KNOWLEDGE_MASTERY_LEVEL},
};

const DesignationDef *designation_def(DesignationType type)
{
    return (unsigned)type < DESIGNATION_COUNT ? &DESIGNATION_DEFS[type] : NULL;
}

bool designations_init(Designations *designations, int width, int height)
{
    memset(designations, 0, sizeof(*designations));
    if (width <= 0 || height <= 0) {
        return false;
    }
    size_t count = (size_t)width * (size_t)height;
    designations->type = calloc(count, sizeof(*designations->type));
    designations->progress = calloc(count, sizeof(*designations->progress));
    if (designations->type == NULL || designations->progress == NULL) {
        designations_free(designations);
        return false;
    }
    designations->width = width;
    designations->height = height;
    return true;
}

void designations_free(Designations *designations)
{
    free(designations->type);
    free(designations->progress);
    memset(designations, 0, sizeof(*designations));
}

static bool in_bounds(const Designations *designations, int x, int y)
{
    return x >= 0 && y >= 0 && x < designations->width && y < designations->height;
}

static size_t index_of(const Designations *designations, int x, int y)
{
    return (size_t)y * (size_t)designations->width + (size_t)x;
}

bool designation_doable_at(DesignationType type, double level)
{
    const DesignationDef *def = designation_def(type);
    return def != NULL && type != DESIGNATION_NONE && def->knowledge != KNOWLEDGE_NONE &&
           level >= def->min_level;
}

bool designation_can_apply(const Map *map, int x, int y, DesignationType type)
{
    if (!map_in_bounds(map, x, y)) {
        return false;
    }
    switch (type) {
    case DESIGNATION_MINE: return map->rock[map_index(map, x, y)] != ROCK_NONE;
    case DESIGNATION_DECONSTRUCT: return map_structure(map, x, y) != STRUCTURE_NONE;
    default: return false;
    }
}

DesignationType designation_get(const Designations *designations, int x, int y)
{
    if (!in_bounds(designations, x, y)) {
        return DESIGNATION_NONE;
    }
    return (DesignationType)designations->type[index_of(designations, x, y)];
}

/* Removes the order on a tile, keeping the counters in sync. */
static void clear_tile(Designations *designations, size_t index)
{
    DesignationType type = (DesignationType)designations->type[index];
    if (type == DESIGNATION_NONE) {
        return;
    }
    designations->count--;
    designations->count_by_work[DESIGNATION_DEFS[type].work_type]--;
    designations->type[index] = DESIGNATION_NONE;
    designations->progress[index] = 0.0f;
}

bool designation_set(Designations *designations, const Map *map, int x, int y,
                     DesignationType type)
{
    if (!in_bounds(designations, x, y) || (unsigned)type >= DESIGNATION_COUNT) {
        return false;
    }
    size_t index = index_of(designations, x, y);
    if (designations->type[index] == type) {
        return false;
    }
    if (type != DESIGNATION_NONE && !designation_can_apply(map, x, y, type)) {
        return false;
    }
    clear_tile(designations, index);
    if (type != DESIGNATION_NONE) {
        designations->type[index] = (uint8_t)type;
        designations->count++;
        designations->count_by_work[DESIGNATION_DEFS[type].work_type]++;
        designations->revision++;
    }
    return true;
}

int designations_set_rect(Designations *designations, const Map *map, int x0, int y0, int x1,
                          int y1, DesignationType type)
{
    int min_x = clampi(x0 < x1 ? x0 : x1, 0, designations->width - 1);
    int max_x = clampi(x0 < x1 ? x1 : x0, 0, designations->width - 1);
    int min_y = clampi(y0 < y1 ? y0 : y1, 0, designations->height - 1);
    int max_y = clampi(y0 < y1 ? y1 : y0, 0, designations->height - 1);
    int changed = 0;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            changed += designation_set(designations, map, x, y, type) ? 1 : 0;
        }
    }
    return changed;
}

int designations_count_for_work(const Designations *designations, WorkTypeMask mask)
{
    int total = 0;
    for (int type = 0; type < WORK_TYPE_COUNT; type++) {
        if (mask & WORK_TYPE_BIT(type)) {
            total += designations->count_by_work[type];
        }
    }
    return total;
}

float designation_work_needed(const Designations *designations, const Map *map, int x, int y)
{
    if (!map_in_bounds(map, x, y)) {
        return 0.0f;
    }
    size_t index = map_index(map, x, y);
    switch (designation_get(designations, x, y)) {
    case DESIGNATION_MINE: {
        const RockDef *rock = rock_def((RockType)map->rock[index]);
        return rock != NULL ? rock->mine_work : 0.0f;
    }
    case DESIGNATION_DECONSTRUCT:
        return structure_deconstruct_work((StructureType)map->structure[index]);
    default: return 0.0f;
    }
}

float designation_progress_fraction(const Designations *designations, const Map *map, int x,
                                    int y)
{
    float needed = designation_work_needed(designations, map, x, y);
    if (needed <= 0.0f) {
        return 0.0f;
    }
    return clampf(designations->progress[index_of(designations, x, y)] / needed, 0.0f, 1.0f);
}

ItemType designation_yield(const Designations *designations, const Map *map, int x, int y,
                           int *count)
{
    *count = 0;
    if (!map_in_bounds(map, x, y)) {
        return ITEM_NONE;
    }
    size_t index = map_index(map, x, y);
    switch (designation_get(designations, x, y)) {
    case DESIGNATION_MINE: {
        const RockDef *def = rock_def((RockType)map->rock[index]);
        *count = def->mined_yield;
        return *count > 0 ? def->mined_item : ITEM_NONE;
    }
    default: return ITEM_NONE;
    }
}

/* Carries out a completed order on the map. */
static void complete(Map *map, Items *items, int x, int y, DesignationType type)
{
    switch (type) {
    case DESIGNATION_MINE:
        /* The rough stone floor under the rock stays. */
        map_set_rock(map, x, y, ROCK_NONE);
        break;
    case DESIGNATION_DECONSTRUCT: {
        StructureType structure = map_structure(map, x, y);
        const StructureDef *def = structure_def(structure);
        map_set_structure(map, x, y, STRUCTURE_NONE);
        for (int slot = 0; items != NULL && slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
            int count = structure_salvage_count(structure, slot);
            if (count > 0) {
                items_drop(items, map, x, y, item_stack_pristine(def->materials[slot].type, count));
            }
        }
        break;
    }
    default: break;
    }
}

bool designation_add_work(Designations *designations, Map *map, Items *items, int x, int y,
                          float work, double level, Rng *rng)
{
    DesignationType type = designation_get(designations, x, y);
    if (type == DESIGNATION_NONE) {
        return false;
    }
    size_t index = index_of(designations, x, y);
    designations->progress[index] += work;
    if (designations->progress[index] < designation_work_needed(designations, map, x, y)) {
        return false;
    }
    int count;
    ItemType product = designation_yield(designations, map, x, y, &count);
    complete(map, items, x, y, type);
    if (items != NULL && product != ITEM_NONE) {
        /* After mining: the tile is open. */
        items_drop(items, map, x, y, knowledge_product(product, count, level, rng));
    }
    clear_tile(designations, index);
    designations->revision++; /* The map changed: new tiles may be reachable. */
    return true;
}

uint64_t designations_hash(const Designations *designations, uint64_t hash)
{
    size_t count = (size_t)designations->width * (size_t)designations->height;
    hash = hash_fnv1a64(hash, designations->type, count);
    hash = hash_fnv1a64(hash, designations->progress, count * sizeof(*designations->progress));
    return hash;
}
