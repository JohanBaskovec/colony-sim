#include "core/designation.h"

#include "core/i18n.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

static const DesignationDef DESIGNATION_DEFS[DESIGNATION_COUNT] = {
    [DESIGNATION_NONE] = {"none", NULL, NULL, WORK_TYPE_COUNT, KNOWLEDGE_NONE, 0.0f,
                          TOOL_USE_NONE},
    /* Mining needs a pick and the Mining knowledge: neither exists yet. */
    [DESIGNATION_MINE] = {"mine", N_("designation.mine"), N_("colonist.task.mine"), WORK_MINING,
                          KNOWLEDGE_NONE, KNOWLEDGE_MASTERY_LEVEL, TOOL_USE_NONE},
    [DESIGNATION_DECONSTRUCT] = {"deconstruct", N_("designation.deconstruct"),
                                 N_("colonist.task.deconstruct"), WORK_CONSTRUCTION,
                                 KNOWLEDGE_BASIC_SHELTERS, KNOWLEDGE_MASTERY_LEVEL,
                                 TOOL_USE_NONE},
    [DESIGNATION_REMOVE_ROOF] = {"remove_roof", N_("designation.remove_roof"),
                                 N_("colonist.task.remove_roof"), WORK_CONSTRUCTION,
                                 KNOWLEDGE_BASIC_SHELTERS, KNOWLEDGE_MASTERY_LEVEL,
                                 TOOL_USE_NONE},
    /* Pit dwellings and dugouts: the shelter builders dig. */
    [DESIGNATION_DIG] = {"dig", N_("designation.dig"), N_("colonist.task.dig_earth"), WORK_MINING,
                         KNOWLEDGE_BASIC_SHELTERS, KNOWLEDGE_MASTERY_LEVEL, TOOL_USE_DIGGING},
    [DESIGNATION_DIG_RAMP] = {"dig_ramp", N_("designation.dig_ramp"),
                              N_("colonist.task.dig_ramp"), WORK_MINING,
                              KNOWLEDGE_BASIC_SHELTERS, KNOWLEDGE_MASTERY_LEVEL,
                              TOOL_USE_DIGGING},
};

const DesignationDef *designation_def(DesignationType type)
{
    return (unsigned)type < DESIGNATION_COUNT ? &DESIGNATION_DEFS[type] : NULL;
}

bool designations_init(Designations *designations, int width, int height, int depth)
{
    memset(designations, 0, sizeof(*designations));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    size_t count = (size_t)width * (size_t)height * (size_t)depth;
    designations->type = calloc(count, sizeof(*designations->type));
    designations->progress = calloc(count, sizeof(*designations->progress));
    if (designations->type == NULL || designations->progress == NULL) {
        designations_free(designations);
        return false;
    }
    designations->width = width;
    designations->height = height;
    designations->depth = depth;
    return true;
}

void designations_free(Designations *designations)
{
    free(designations->type);
    free(designations->progress);
    memset(designations, 0, sizeof(*designations));
}

static bool in_bounds(const Designations *designations, int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < designations->width && y < designations->height &&
           z < designations->depth;
}

static size_t index_of(const Designations *designations, int x, int y, int z)
{
    return ((size_t)z * (size_t)designations->height + (size_t)y) * (size_t)designations->width +
           (size_t)x;
}

bool designation_doable_at(DesignationType type, double level)
{
    const DesignationDef *def = designation_def(type);
    return def != NULL && type != DESIGNATION_NONE && def->knowledge != KNOWLEDGE_NONE &&
           level >= def->min_level;
}

bool designation_is_roof(DesignationType type)
{
    return type == DESIGNATION_REMOVE_ROOF;
}

/* Whether a ramp can be cut into this tile: earth, with bare ground over it
 * (nothing standing on it, open air above, so that it can be taken away) and
 * ground to lead up to beside it. */
static bool ramp_possible(const Map *map, int x, int y, int z)
{
    static const int DX[4] = {0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1, 0};
    if (map->rock[map_index(map, x, y, z)] != ROCK_EARTH || !map_in_bounds(map, x, y, z + 1)) {
        return false;
    }
    size_t over = map_index(map, x, y, z + 1);
    if (!map_has_floor(map, x, y, z + 1) || map->rock[over] != ROCK_NONE || map->slope[over] ||
        map->structure[over] != STRUCTURE_NONE || map->roof[over] != STRUCTURE_NONE ||
        map_plant_blocks_building(map, x, y, z + 1) ||
        (map_in_bounds(map, x, y, z + 2) && !map_is_open(map, x, y, z + 2))) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        int nx = x + DX[i], ny = y + DY[i];
        if (map_in_bounds(map, nx, ny, z + 1) && map_has_floor(map, nx, ny, z + 1) &&
            map->rock[map_index(map, nx, ny, z + 1)] == ROCK_NONE) {
            return true;
        }
    }
    return false;
}

bool designation_can_apply(const Map *map, int x, int y, int z, DesignationType type)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    RockType rock = (RockType)map->rock[map_index(map, x, y, z)];
    switch (type) {
    case DESIGNATION_MINE: return rock != ROCK_NONE && rock != ROCK_EARTH;
    case DESIGNATION_DIG: return rock == ROCK_EARTH;
    case DESIGNATION_DIG_RAMP: return ramp_possible(map, x, y, z);
    case DESIGNATION_DECONSTRUCT: return map_structure(map, x, y, z) != STRUCTURE_NONE;
    case DESIGNATION_REMOVE_ROOF: return map_roof(map, x, y, z) != STRUCTURE_NONE;
    default: return false;
    }
}

bool designation_ramp_ground_free(const Constructions *constructions,
                                  const Workstations *workstations, const Stockpiles *stockpiles,
                                  int x, int y, int z)
{
    return construction_at(constructions, x, y, z + 1) == CONSTRUCTION_NONE &&
           construction_roof_at(constructions, x, y, z + 1) == CONSTRUCTION_NONE &&
           workstation_at(workstations, x, y, z + 1) == WORKSTATION_NONE &&
           stockpile_at(stockpiles, x, y, z + 1) == STOCKPILE_NONE;
}

bool designation_waits_for_support(const Designations *designations, const Map *map, int x,
                                   int y, int z)
{
    DesignationType type = designation_get(designations, x, y, z);
    if (type != DESIGNATION_DIG && type != DESIGNATION_MINE && type != DESIGNATION_DIG_RAMP) {
        return false;
    }
    return !map_dig_keeps_ceilings(map, x, y, z, type == DESIGNATION_DIG_RAMP);
}

DesignationType designation_get(const Designations *designations, int x, int y, int z)
{
    if (!in_bounds(designations, x, y, z)) {
        return DESIGNATION_NONE;
    }
    return (DesignationType)designations->type[index_of(designations, x, y, z)];
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

bool designation_set(Designations *designations, const Map *map, int x, int y, int z,
                     DesignationType type)
{
    if (!in_bounds(designations, x, y, z) || (unsigned)type >= DESIGNATION_COUNT) {
        return false;
    }
    size_t index = index_of(designations, x, y, z);
    if (designations->type[index] == type) {
        return false;
    }
    if (type != DESIGNATION_NONE && !designation_can_apply(map, x, y, z, type)) {
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
                          int y1, int z, DesignationType type)
{
    int min_x = clampi(x0 < x1 ? x0 : x1, 0, designations->width - 1);
    int max_x = clampi(x0 < x1 ? x1 : x0, 0, designations->width - 1);
    int min_y = clampi(y0 < y1 ? y0 : y1, 0, designations->height - 1);
    int max_y = clampi(y0 < y1 ? y1 : y0, 0, designations->height - 1);
    int changed = 0;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            changed += designation_set(designations, map, x, y, z, type) ? 1 : 0;
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

float designation_work_needed(const Designations *designations, const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return 0.0f;
    }
    size_t index = map_index(map, x, y, z);
    switch (designation_get(designations, x, y, z)) {
    case DESIGNATION_MINE:
    case DESIGNATION_DIG: {
        const RockDef *rock = rock_def((RockType)map->rock[index]);
        return rock != NULL ? rock->mine_work : 0.0f;
    }
    case DESIGNATION_DIG_RAMP: {
        const RockDef *rock = rock_def((RockType)map->rock[index]);
        return rock != NULL ? rock->mine_work * DESIGNATION_RAMP_WORK_FACTOR : 0.0f;
    }
    case DESIGNATION_DECONSTRUCT:
        return structure_deconstruct_work((StructureType)map->structure[index]);
    case DESIGNATION_REMOVE_ROOF:
        return structure_deconstruct_work((StructureType)map->roof[index]);
    default: return 0.0f;
    }
}

float designation_progress_fraction(const Designations *designations, const Map *map, int x,
                                    int y, int z)
{
    float needed = designation_work_needed(designations, map, x, y, z);
    if (needed <= 0.0f) {
        return 0.0f;
    }
    return clampf(designations->progress[index_of(designations, x, y, z)] / needed, 0.0f, 1.0f);
}

ItemType designation_yield(const Designations *designations, const Map *map, int x, int y, int z,
                           int *count)
{
    *count = 0;
    if (!map_in_bounds(map, x, y, z)) {
        return ITEM_NONE;
    }
    size_t index = map_index(map, x, y, z);
    switch (designation_get(designations, x, y, z)) {
    case DESIGNATION_MINE:
    case DESIGNATION_DIG:
    case DESIGNATION_DIG_RAMP: {
        const RockDef *def = rock_def((RockType)map->rock[index]);
        *count = def->mined_yield;
        return *count > 0 ? def->mined_item : ITEM_NONE;
    }
    default: return ITEM_NONE;
    }
}

/* Half the materials of a structure taken apart, dropped on its tile. */
static void drop_salvage(StructureType type, Map *map, Items *items, int x, int y, int z)
{
    const StructureDef *def = structure_def(type);
    for (int slot = 0; def != NULL && items != NULL && slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        int count = structure_salvage_count(type, slot);
        if (count > 0) {
            items_drop(items, map, x, y, z, item_stack_pristine(def->materials[slot].type, count));
        }
    }
}

/* Cuts a ramp: the earth goes, its floor slopes up, and the ground over it
 * is taken away (what lay on it slides down onto the ramp). */
static void cut_ramp(Map *map, Items *items, int x, int y, int z)
{
    map_set_rock(map, x, y, z, ROCK_NONE);
    size_t over = map_index(map, x, y, z + 1);
    map->plants[over] = (Plant){0};
    map_set_terrain(map, x, y, z + 1, TERRAIN_OPEN);
    map_set_slope(map, x, y, z, true);
    ItemStack lying = items != NULL ? items_get(items, x, y, z + 1) : (ItemStack){0};
    if (lying.type != ITEM_NONE) {
        items_remove(items, x, y, z + 1, lying.count);
        items_drop(items, map, x, y, z, lying);
    }
}

/* Carries out a completed order on the map. */
static void complete(Map *map, Items *items, int x, int y, int z, DesignationType type)
{
    switch (type) {
    case DESIGNATION_MINE:
    case DESIGNATION_DIG:
        /* The floor under the rock stays. */
        map_set_rock(map, x, y, z, ROCK_NONE);
        break;
    case DESIGNATION_DIG_RAMP: cut_ramp(map, items, x, y, z); break;
    case DESIGNATION_DECONSTRUCT: {
        StructureType structure = map_structure(map, x, y, z);
        map_set_structure(map, x, y, z, STRUCTURE_NONE);
        drop_salvage(structure, map, items, x, y, z);
        break;
    }
    case DESIGNATION_REMOVE_ROOF: {
        StructureType roof = map_roof(map, x, y, z);
        map_set_roof(map, x, y, z, STRUCTURE_NONE, ITEM_QUALITY_CRUDE);
        drop_salvage(roof, map, items, x, y, z);
        break;
    }
    default: break;
    }
}

bool designation_add_work(Designations *designations, Map *map, Items *items, int x, int y, int z,
                          float work, double level, Rng *rng)
{
    DesignationType type = designation_get(designations, x, y, z);
    if (type == DESIGNATION_NONE) {
        return false;
    }
    size_t index = index_of(designations, x, y, z);
    designations->progress[index] += work;
    if (designations->progress[index] < designation_work_needed(designations, map, x, y, z)) {
        return false;
    }
    int count;
    ItemType product = designation_yield(designations, map, x, y, z, &count);
    complete(map, items, x, y, z, type);
    if (items != NULL && product != ITEM_NONE) {
        /* After mining: the tile is open. */
        items_drop(items, map, x, y, z, knowledge_product(product, count, level, rng));
    }
    clear_tile(designations, index);
    designations->revision++; /* The map changed: new tiles may be reachable. */
    return true;
}

uint64_t designations_hash(const Designations *designations, uint64_t hash)
{
    size_t count = (size_t)designations->width * (size_t)designations->height *
                   (size_t)designations->depth;
    hash = hash_fnv1a64(hash, designations->type, count);
    hash = hash_fnv1a64(hash, designations->progress, count * sizeof(*designations->progress));
    return hash;
}
