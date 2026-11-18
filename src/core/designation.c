#include "core/designation.h"

#include "core/gather_zone.h"
#include "core/i18n.h"
#include "core/tree.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

static const DesignationDef DESIGNATION_DEFS[DESIGNATION_COUNT] = {
    [DESIGNATION_NONE] = {"none", NULL, NULL, WORK_TYPE_COUNT, KNOWLEDGE_NONE, 0.0f, TOOL_USE_NONE},
    /* Mining needs a pick and the Mining knowledge: neither exists yet. */
    [DESIGNATION_MINE] = {"mine", N_("designation.mine"), N_("colonist.task.mine"), WORK_MINING,
                          KNOWLEDGE_NONE, KNOWLEDGE_MASTERY_LEVEL, TOOL_USE_NONE},
    [DESIGNATION_DECONSTRUCT] = {"deconstruct", N_("designation.deconstruct"),
                                 N_("colonist.task.deconstruct"), WORK_CONSTRUCTION,
                                 KNOWLEDGE_BASIC_SHELTERS, KNOWLEDGE_MASTERY_LEVEL, TOOL_USE_NONE},
    /* What "mine" becomes on earth (designation_set()): pit dwellings and
     * dugouts, which the shelter builders dig. Same name for the player. */
    [DESIGNATION_DIG] = {"dig", N_("designation.mine"), N_("colonist.task.dig_earth"), WORK_MINING,
                         KNOWLEDGE_BASIC_SHELTERS, KNOWLEDGE_MASTERY_LEVEL, TOOL_USE_DIGGING},
    [DESIGNATION_DIG_RAMP] = {"dig_ramp", N_("designation.dig_ramp"), N_("colonist.task.dig_ramp"),
                              WORK_MINING, KNOWLEDGE_BASIC_SHELTERS, KNOWLEDGE_MASTERY_LEVEL,
                              TOOL_USE_DIGGING},
    [DESIGNATION_DIG_DOWN] = {"dig_down", N_("designation.dig_down"), N_("colonist.task.dig_down"),
                              WORK_MINING, KNOWLEDGE_BASIC_SHELTERS, KNOWLEDGE_MASTERY_LEVEL,
                              TOOL_USE_DIGGING},
    /* Felling needs an axe and the Woodcutting knowledge: neither exists yet. */
    [DESIGNATION_CHOP] = {"chop", N_("designation.chop"), N_("colonist.task.chop"),
                          WORK_WOODCUTTING, KNOWLEDGE_NONE, KNOWLEDGE_MASTERY_LEVEL, TOOL_USE_NONE},
    /* Gatherers' work; the plant decides the level and the tool
     * (designation_min_level_at(), designation_tool_use_at()). */
    [DESIGNATION_HARVEST] = {"harvest", N_("designation.harvest"), N_("colonist.task.harvest"),
                             WORK_GATHERING, KNOWLEDGE_GATHERING, KNOWLEDGE_MASTERY_LEVEL,
                             TOOL_USE_NONE},
    [DESIGNATION_UPROOT] = {"uproot", N_("designation.uproot"), N_("colonist.task.uproot"),
                            WORK_GATHERING, KNOWLEDGE_GATHERING, KNOWLEDGE_MASTERY_LEVEL,
                            TOOL_USE_NONE},
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
    designations->box = tile_box_empty();
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
        map->structure[over] != STRUCTURE_NONE || map_roof(map, x, y, z + 1) != STRUCTURE_NONE ||
        map_plant_blocks_building(map, x, y, z + 1) ||
        (map_in_bounds(map, x, y, z + 2) && !map_is_open(map, x, y, z + 2))) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        int nx = x + DX[i], ny = y + DY[i];
        if (map_in_bounds(map, nx, ny, z + 1) && map_has_floor(map, nx, ny, z + 1) &&
            map->rock[map_index(map, nx, ny, z + 1)] == ROCK_NONE &&
            map_roof(map, nx, ny, z + 1) == STRUCTURE_NONE) {
            return true;
        }
    }
    return false;
}

/* Whether a hole can be dug down from this tile: bare ground (nothing
 * standing on it) over earth. */
static bool dig_down_possible(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z - 1) &&
           map->rock[map_index(map, x, y, z - 1)] == ROCK_EARTH && map_is_buildable(map, x, y, z) &&
           map_structure(map, x, y, z) == STRUCTURE_NONE &&
           !map_plant_blocks_building(map, x, y, z);
}

/* The tile whose earth an order digs out: the one under the ground for a
 * hole dug down, the order's own otherwise. */
static size_t dug_index(const Designations *designations, const Map *map, int x, int y, int z)
{
    int dug = designation_get(designations, x, y, z) == DESIGNATION_DIG_DOWN && z > 0 ? z - 1 : z;
    return map_index(map, x, y, dug);
}

bool designation_can_apply(const Map *map, int x, int y, int z, DesignationType type)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    RockType rock = (RockType)map->rock[map_index(map, x, y, z)];
    switch (type) {
    case DESIGNATION_MINE: return rock != ROCK_NONE && rock != ROCK_WOOD; /* Rock or earth. */
    case DESIGNATION_DIG: return rock == ROCK_EARTH;
    case DESIGNATION_DIG_RAMP: return ramp_possible(map, x, y, z);
    case DESIGNATION_DIG_DOWN: return dig_down_possible(map, x, y, z);
    case DESIGNATION_DECONSTRUCT:
        return map_structure(map, x, y, z) != STRUCTURE_NONE ||
               map_roof(map, x, y, z) != STRUCTURE_NONE;
    case DESIGNATION_CHOP: return plant_is_tree(map->plants[map_index(map, x, y, z)].type);
    case DESIGNATION_HARVEST: return gather_plant_harvestable(map, x, y, z);
    case DESIGNATION_UPROOT: return gather_plant_uprootable(map, x, y, z);
    default: return false;
    }
}

float designation_min_level_at(const Map *map, int x, int y, int z, DesignationType type)
{
    const DesignationDef *def = designation_def(type);
    if (def == NULL) {
        return 0.0f;
    }
    if (type == DESIGNATION_HARVEST || type == DESIGNATION_UPROOT) {
        const GatherJobDef *job = gather_job_def(gather_plant_harvest_job(map, x, y, z));
        if (job != NULL && job->min_level > def->min_level) {
            return job->min_level;
        }
    }
    return def->min_level;
}

ToolUse designation_tool_use_at(const Map *map, int x, int y, int z, DesignationType type)
{
    const DesignationDef *def = designation_def(type);
    if (def == NULL) {
        return TOOL_USE_NONE;
    }
    if (type == DESIGNATION_HARVEST || type == DESIGNATION_UPROOT) {
        const GatherJobDef *job = gather_job_def(gather_plant_harvest_job(map, x, y, z));
        return job != NULL ? job->tool_use : TOOL_USE_NONE;
    }
    return def->tool_use;
}

void designation_target(const Map *map, int *x, int *y, int *z)
{
    TreeRef tree;
    if (map != NULL && map_in_bounds(map, *x, *y, *z) &&
        map->rock[map_index(map, *x, *y, *z)] == ROCK_WOOD && tree_at(map, *x, *y, *z, &tree)) {
        *x = tree.x;
        *y = tree.y;
        *z = tree.z;
    }
}

bool designation_ground_free(const Constructions *constructions, const Workstations *workstations,
                             const Stockpiles *stockpiles, DesignationType type, int x, int y,
                             int z)
{
    if (type != DESIGNATION_DIG_RAMP && type != DESIGNATION_DIG_DOWN) {
        return true;
    }
    int ground = type == DESIGNATION_DIG_RAMP ? z + 1 : z;
    return construction_at(constructions, x, y, ground) == CONSTRUCTION_NONE &&
           workstation_at(workstations, x, y, ground) == WORKSTATION_NONE &&
           stockpile_at(stockpiles, x, y, ground) == STOCKPILE_NONE;
}

bool designation_waits_for_support(const Designations *designations, const Map *map, int x, int y,
                                   int z)
{
    DesignationType type = designation_get(designations, x, y, z);
    if (type == DESIGNATION_DIG_DOWN) {
        return z > 0 && !map_dig_keeps_ceilings(map, x, y, z - 1, true);
    }
    if (type != DESIGNATION_DIG && type != DESIGNATION_MINE && type != DESIGNATION_DIG_RAMP) {
        return false;
    }
    return !map_dig_keeps_ceilings(map, x, y, z, type == DESIGNATION_DIG_RAMP);
}

bool designation_forbidden(const Designations *designations, const Map *map, int x, int y, int z)
{
    if (map->forbidden_count == 0) {
        return false;
    }
    switch (designation_get(designations, x, y, z)) {
    case DESIGNATION_DECONSTRUCT:
        return map_structure(map, x, y, z) != STRUCTURE_NONE
                   ? map_forbidden(map, x, y, z, MAP_FORBID_STRUCTURE)
                   : map_forbidden(map, x, y, z, MAP_FORBID_ROOF);
    case DESIGNATION_CHOP:
    case DESIGNATION_HARVEST:
    case DESIGNATION_UPROOT:
    case DESIGNATION_DIG_DOWN: return map_forbidden(map, x, y, z, MAP_FORBID_PLANT);
    case DESIGNATION_DIG_RAMP: return map_forbidden(map, x, y, z + 1, MAP_FORBID_PLANT);
    default: return false;
    }
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
    if (--designations->count == 0) {
        designations->box = tile_box_empty();
    }
    designations->count_by_work[DESIGNATION_DEFS[type].work_type]--;
    designations->type[index] = DESIGNATION_NONE;
    designations->progress[index] = 0.0f;
}

bool designation_set(Designations *designations, const Map *map, int x, int y, int z,
                     DesignationType type)
{
    designation_target(map, &x, &y, &z);
    if (!in_bounds(designations, x, y, z) || (unsigned)type >= DESIGNATION_COUNT) {
        return false;
    }
    size_t index = index_of(designations, x, y, z);
    if (type == DESIGNATION_MINE && map->rock[map_index(map, x, y, z)] == ROCK_EARTH) {
        type = DESIGNATION_DIG; /* One order for the player, two jobs. */
    }
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
        tile_box_add(&designations->box, x, y, z);
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
    case DESIGNATION_DIG_RAMP:
    case DESIGNATION_DIG_DOWN: {
        const RockDef *rock = rock_def((RockType)map->rock[dug_index(designations, map, x, y, z)]);
        return rock != NULL ? rock->mine_work * DESIGNATION_RAMP_WORK_FACTOR : 0.0f;
    }
    case DESIGNATION_DECONSTRUCT:
        return structure_deconstruct_work(map->structure[index] != STRUCTURE_NONE
                                              ? (StructureType)map->structure[index]
                                              : map_roof(map, x, y, z));
    case DESIGNATION_CHOP: return tree_fell_work(&map->plants[index]);
    case DESIGNATION_HARVEST:
        return gather_job_work_needed(map, x, y, z, gather_plant_harvest_job(map, x, y, z));
    case DESIGNATION_UPROOT: return gather_uproot_work(map, x, y, z);
    default: return 0.0f;
    }
}

float designation_progress_fraction(const Designations *designations, const Map *map, int x, int y,
                                    int z)
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
    switch (designation_get(designations, x, y, z)) {
    case DESIGNATION_MINE:
    case DESIGNATION_DIG:
    case DESIGNATION_DIG_RAMP:
    case DESIGNATION_DIG_DOWN: {
        const RockDef *def = rock_def((RockType)map->rock[dug_index(designations, map, x, y, z)]);
        if (def == NULL) {
            return ITEM_NONE;
        }
        *count = def->mined_yield;
        return *count > 0 ? def->mined_item : ITEM_NONE;
    }
    case DESIGNATION_CHOP: {
        const Plant *plant = &map->plants[map_index(map, x, y, z)];
        const PlantDef *def = plant_def((PlantType)plant->type);
        *count = tree_wood_yield(plant);
        return *count > 0 && def != NULL ? def->cut_item : ITEM_NONE;
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
    map_remove_plant(map, x, y, z + 1);
    map_set_terrain(map, x, y, z + 1, TERRAIN_OPEN);
    map_set_slope(map, x, y, z, true);
    ItemStack lying = items != NULL ? items_get(items, x, y, z + 1) : (ItemStack){0};
    if (lying.type != ITEM_NONE) {
        items_remove(items, x, y, z + 1, lying.count);
        items_drop(items, map, x, y, z, lying);
    }
}

/* Digs a hole down: the ground goes, then the earth under it; the hole
 * slopes up to the ground beside it where it can (where its digger stood),
 * and what lay on the ground falls into it. */
static void dig_down(Map *map, Items *items, int x, int y, int z)
{
    ItemStack lying = items != NULL ? items_get(items, x, y, z) : (ItemStack){0};
    if (lying.type != ITEM_NONE) {
        items_remove(items, x, y, z, lying.count);
    }
    map_remove_plant(map, x, y, z);
    map_set_terrain(map, x, y, z, TERRAIN_OPEN);
    map_set_rock(map, x, y, z - 1, ROCK_NONE);
    map_set_slope(map, x, y, z - 1, true);
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    bool leads_up = false;
    for (int d = 0; d < 4; d++) {
        leads_up |= map_can_climb(map, x, y, z - 1, x + DX[d], y + DY[d]);
    }
    if (!leads_up) {
        map_set_slope(map, x, y, z - 1, false); /* A pit: a ladder gets out of it. */
    }
    if (lying.type != ITEM_NONE) {
        items_drop(items, map, x, y, z - 1, lying);
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
    case DESIGNATION_DIG_DOWN: dig_down(map, items, x, y, z); break;
    case DESIGNATION_DECONSTRUCT: {
        StructureType structure = map_structure(map, x, y, z);
        StructureType roof = map_roof(map, x, y, z);
        if (structure != STRUCTURE_NONE) {
            map_set_structure(map, x, y, z, STRUCTURE_NONE);
            drop_salvage(structure, map, items, x, y, z);
        } else if (roof != STRUCTURE_NONE) { /* Taken down onto the floor below. */
            map_set_roof(map, x, y, z, STRUCTURE_NONE, ITEM_QUALITY_WORST);
            drop_salvage(roof, map, items, x, y, z - 1);
        }
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
    if (type == DESIGNATION_HARVEST) {
        gather_harvest_plant(map, items, x, y, z, level, rng);
        product = ITEM_NONE; /* Dropped there. */
    } else if (type == DESIGNATION_UPROOT) {
        gather_clear_plant(map, items, x, y, z, level, rng); /* What it bears first. */
        product = ITEM_NONE;
    } else if (type == DESIGNATION_CHOP) { /* Its wood falls with it. */
        ItemStack wood = items != NULL && product != ITEM_NONE
                             ? knowledge_product(product, count, level, rng)
                             : (ItemStack){0};
        if (tree_fell(map, items, x, y, z, wood).harmed > 0) {
            designations->harmful_tree_falls++;
        }
    } else {
        complete(map, items, x, y, z, type);
    }
    if (type != DESIGNATION_CHOP && items != NULL && product != ITEM_NONE) {
        /* After mining: the tile is open. */
        items_drop(items, map, x, y, z, knowledge_product(product, count, level, rng));
    }
    clear_tile(designations, index);
    designations->revision++; /* The map changed: new tiles may be reachable. */
    return true;
}

uint64_t designations_hash(const Designations *designations, uint64_t hash)
{
    size_t count =
        (size_t)designations->width * (size_t)designations->height * (size_t)designations->depth;
    hash = hash_fnv1a64(hash, designations->type, count);
    hash = hash_fnv1a64(hash, designations->progress, count * sizeof(*designations->progress));
    return hash;
}
