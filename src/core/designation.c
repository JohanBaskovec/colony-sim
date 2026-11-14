#include "core/designation.h"

#include "core/i18n.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

/* Chopping a tree: a fixed part plus a part proportional to its size. */
#define CHOP_BASE_WORK 400.0f
#define CHOP_GROWTH_WORK 1600.0f
#define HARVEST_WORK 300.0f
#define GATHER_STONE_WORK 200.0f /* Per stone. */
#define GATHER_BRANCH_WORK 300.0f /* Per branch. */
#define GATHER_TWIGS_WORK 150.0f /* Per handful. */
#define CUT_GRASS_WORK 150.0f
/* Armfuls of grass cut from a fully grown tile, proportionally fewer when shorter. */
#define CUT_GRASS_YIELD 3.0f
#define CUT_GRASS_MIN_GROWTH 0.5f

#define GATHERING(level) WORK_GATHERING, KNOWLEDGE_GATHERING, (level)

static const DesignationDef DESIGNATION_DEFS[DESIGNATION_COUNT] = {
    [DESIGNATION_NONE] = {"none", NULL, NULL, WORK_TYPE_COUNT, KNOWLEDGE_NONE, 0.0f, false},
    /* Felling needs an axe and the Felling knowledge, mining a pick and the
     * Mining knowledge: neither exists yet. */
    [DESIGNATION_CHOP] = {"chop", N_("designation.chop"), N_("colonist.task.chop"),
                          WORK_WOODCUTTING, KNOWLEDGE_NONE, KNOWLEDGE_MASTERY_LEVEL, false},
    [DESIGNATION_MINE] = {"mine", N_("designation.mine"), N_("colonist.task.mine"), WORK_MINING,
                          KNOWLEDGE_NONE, KNOWLEDGE_MASTERY_LEVEL, false},
    [DESIGNATION_HARVEST] = {"harvest", N_("designation.harvest"), N_("colonist.task.harvest"),
                             GATHERING(KNOWLEDGE_MASTERY_LEVEL), false},
    [DESIGNATION_GATHER_STONES] = {"gather_stones", N_("designation.gather_stones"),
                                   N_("colonist.task.gather_stones"),
                                   GATHERING(KNOWLEDGE_MASTERY_LEVEL), true},
    [DESIGNATION_GATHER_BRANCHES] = {"gather_branches", N_("designation.gather_branches"),
                                     N_("colonist.task.gather_branches"),
                                     GATHERING(KNOWLEDGE_MASTERY_LEVEL), false},
    [DESIGNATION_GATHER_TWIGS] = {"gather_twigs", N_("designation.gather_twigs"),
                                  N_("colonist.task.gather_twigs"),
                                  GATHERING(KNOWLEDGE_MASTERY_LEVEL), false},
    [DESIGNATION_CUT_GRASS] = {"cut_grass", N_("designation.cut_grass"),
                               N_("colonist.task.cut_grass"), GATHERING(KNOWLEDGE_MASTERY_LEVEL),
                               false},
};

#undef GATHERING

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

/* Ground where loose stones lie about. */
static bool has_loose_stones(Terrain terrain)
{
    return terrain == TERRAIN_SOIL || terrain == TERRAIN_RICH_SOIL || terrain == TERRAIN_GRAVEL ||
           terrain == TERRAIN_ROUGH_STONE;
}

/* The stock a gathering order takes, PLANT_STOCK_COUNT for other orders. */
static PlantStock stock_of(DesignationType type)
{
    switch (type) {
    case DESIGNATION_GATHER_BRANCHES: return PLANT_STOCK_BRANCHES;
    case DESIGNATION_GATHER_TWIGS: return PLANT_STOCK_TWIGS;
    default: return PLANT_STOCK_COUNT;
    }
}

bool designation_can_apply(const Map *map, int x, int y, DesignationType type)
{
    if (!map_in_bounds(map, x, y)) {
        return false;
    }
    size_t index = map_index(map, x, y);
    const Plant *plant = &map->plants[index];
    bool open = map->rock[index] == ROCK_NONE;
    switch (type) {
    case DESIGNATION_CHOP:
        return map->plants[index].type == PLANT_TREE && map->rock[index] == ROCK_NONE;
    case DESIGNATION_MINE: return map->rock[index] != ROCK_NONE;
    case DESIGNATION_HARVEST: {
        const PlantDef *def = plant_def((PlantType)plant->type);
        return def->harvest_item != ITEM_NONE && plant->growth >= def->min_edible_growth && open;
    }
    case DESIGNATION_GATHER_STONES:
        return open && has_loose_stones((Terrain)map->terrain[index]);
    case DESIGNATION_GATHER_BRANCHES:
    case DESIGNATION_GATHER_TWIGS: return open && plant_stock_available(plant, stock_of(type)) > 0;
    case DESIGNATION_CUT_GRASS:
        return open && plant->type == PLANT_GRASS && plant->growth >= CUT_GRASS_MIN_GROWTH;
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
    case DESIGNATION_CHOP: return CHOP_BASE_WORK + CHOP_GROWTH_WORK * map->plants[index].growth;
    case DESIGNATION_HARVEST: return HARVEST_WORK;
    case DESIGNATION_GATHER_STONES: return GATHER_STONE_WORK;
    case DESIGNATION_GATHER_BRANCHES:
    case DESIGNATION_GATHER_TWIGS: {
        DesignationType type = designation_get(designations, x, y);
        int available = plant_stock_available(&map->plants[index], stock_of(type));
        float per_item =
            type == DESIGNATION_GATHER_BRANCHES ? GATHER_BRANCH_WORK : GATHER_TWIGS_WORK;
        return per_item * (float)(available > 1 ? available : 1);
    }
    case DESIGNATION_CUT_GRASS: return CUT_GRASS_WORK;
    case DESIGNATION_MINE: {
        const RockDef *rock = rock_def((RockType)map->rock[index]);
        return rock != NULL ? rock->mine_work : 0.0f;
    }
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
    case DESIGNATION_CHOP: {
        const Plant *plant = &map->plants[index];
        const PlantDef *def = plant_def((PlantType)plant->type);
        *count = (int)((float)def->cut_yield * plant->growth + 0.5f);
        return *count > 0 ? def->cut_item : ITEM_NONE;
    }
    case DESIGNATION_MINE: {
        const RockDef *def = rock_def((RockType)map->rock[index]);
        *count = def->mined_yield;
        return *count > 0 ? def->mined_item : ITEM_NONE;
    }
    case DESIGNATION_HARVEST: {
        const Plant *plant = &map->plants[index];
        const PlantDef *def = plant_def((PlantType)plant->type);
        *count = (int)((float)def->harvest_yield * plant->growth + 0.5f);
        return *count > 0 ? def->harvest_item : ITEM_NONE;
    }
    case DESIGNATION_GATHER_STONES: *count = 1; return ITEM_STONE;
    case DESIGNATION_GATHER_BRANCHES:
    case DESIGNATION_GATHER_TWIGS: {
        PlantStock stock = stock_of(designation_get(designations, x, y));
        *count = plant_stock_available(&map->plants[index], stock);
        return *count > 0 ? plant_stock_item(stock) : ITEM_NONE;
    }
    case DESIGNATION_CUT_GRASS: {
        int armfuls = (int)(CUT_GRASS_YIELD * map->plants[index].growth + 0.5f);
        *count = armfuls > 1 ? armfuls : 1;
        return ITEM_FRESH_GRASS;
    }
    default: return ITEM_NONE;
    }
}

/* Carries out a completed order on the map. */
static void complete(Map *map, int x, int y, DesignationType type)
{
    size_t index = map_index(map, x, y);
    switch (type) {
    case DESIGNATION_CHOP: memset(&map->plants[index], 0, sizeof(map->plants[index])); break;
    case DESIGNATION_MINE:
        /* The rough stone floor under the rock stays. */
        map_set_rock(map, x, y, ROCK_NONE);
        break;
    case DESIGNATION_HARVEST: map->plants[index].growth = PLANT_GROWTH_AFTER_PICKING; break;
    case DESIGNATION_GATHER_BRANCHES:
    case DESIGNATION_GATHER_TWIGS:
        plant_take_stock(&map->plants[index], stock_of(type), INT16_MAX);
        break;
    case DESIGNATION_CUT_GRASS: map->plants[index].growth = 0.0f; break;
    default: break; /* Stones: the ground keeps plenty. */
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
    complete(map, x, y, type);
    if (items != NULL && product != ITEM_NONE) {
        /* After mining: the tile is open. */
        items_drop(items, map, x, y, knowledge_product(product, count, level, rng));
    }
    if (DESIGNATION_DEFS[type].repeats) {
        designations->progress[index] = 0.0f;
    } else {
        clear_tile(designations, index);
    }
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
