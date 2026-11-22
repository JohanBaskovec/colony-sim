#include "core/workstation.h"

#include "core/i18n.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

static const WorkstationDef WORKSTATION_DEFS[WORKSTATION_TYPE_COUNT] = {
    /* A place to sit and work (knapping stone, whittling wood): nothing to bring, so it is
     * ready at once. */
    [WORKSTATION_CRAFTING_SPOT] = {"crafting_spot",
                                   N_("workstation.crafting_spot"),
                                   N_("workstation.crafting_spot.description"),
                                   {{ITEM_NONE, 0}, {ITEM_NONE, 0}},
                                   0.0f,
                                   KNOWLEDGE_NONE,
                                   0.0f},
    /* A shallow pit ringed with six stones, dug and built by a settler. */
    [WORKSTATION_OPEN_HEARTH] = {"open_hearth",
                                 N_("workstation.open_hearth"),
                                 N_("workstation.open_hearth.description"),
                                 {{ITEM_COBBLE, 6}, {ITEM_NONE, 0}},
                                 250.0f,
                                 KNOWLEDGE_BASIC_SHELTERS,
                                 50.0f,
                                 .hearth = true},
};

const WorkstationDef *workstation_def(WorkstationType type)
{
    return (unsigned)type < WORKSTATION_TYPE_COUNT ? &WORKSTATION_DEFS[type] : NULL;
}

bool workstations_init(Workstations *workstations, int width, int height, int depth)
{
    memset(workstations, 0, sizeof(*workstations));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    workstations->at = calloc((size_t)width * (size_t)height * (size_t)depth, 1);
    if (workstations->at == NULL) {
        return false;
    }
    workstations->width = width;
    workstations->height = height;
    workstations->depth = depth;
    workstations->next_number = 1;
    return true;
}

void workstations_free(Workstations *workstations)
{
    free(workstations->at);
    memset(workstations, 0, sizeof(*workstations));
}

static bool in_bounds(const Workstations *workstations, int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < workstations->width && y < workstations->height &&
           z < workstations->depth;
}

static size_t index_of(const Workstations *workstations, int x, int y, int z)
{
    return ((size_t)z * (size_t)workstations->height + (size_t)y) * (size_t)workstations->width +
           (size_t)x;
}

int workstation_at(const Workstations *workstations, int x, int y, int z)
{
    if (!in_bounds(workstations, x, y, z)) {
        return WORKSTATION_NONE;
    }
    return (int)workstations->at[index_of(workstations, x, y, z)] - 1;
}

Workstation *workstation_get(Workstations *workstations, int index)
{
    if (index < 0 || index >= WORKSTATION_MAX || !workstations->list[index].used) {
        return NULL;
    }
    return &workstations->list[index];
}

const Workstation *workstation_get_const(const Workstations *workstations, int index)
{
    return workstation_get((Workstations *)workstations, index);
}

bool workstation_can_place(const Workstations *workstations, const Map *map, int x, int y, int z)
{
    return in_bounds(workstations, x, y, z) && map_is_passable(map, x, y, z) &&
           map_is_buildable(map, x, y, z) && !map_plant_blocks_building(map, x, y, z) &&
           workstation_at(workstations, x, y, z) == WORKSTATION_NONE;
}

bool workstation_zone_contains(const Workstation *station, int x, int y, int z)
{
    return station->used && z == station->z && abs(x - station->x) <= WORKSTATION_ZONE_REACH &&
           abs(y - station->y) <= WORKSTATION_ZONE_REACH;
}

void workstations_count_stored(const Workstations *workstations, const Stockpiles *stockpiles,
                               const Items *items, long stored[ITEM_TYPE_COUNT])
{
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        const Workstation *station = &workstations->list[index];
        if (!station->used) {
            continue;
        }
        for (int dy = -WORKSTATION_ZONE_REACH; dy <= WORKSTATION_ZONE_REACH; dy++) {
            for (int dx = -WORKSTATION_ZONE_REACH; dx <= WORKSTATION_ZONE_REACH; dx++) {
                int x = station->x + dx, y = station->y + dy, z = station->z;
                if (!in_bounds(workstations, x, y, z) ||
                    stockpile_at(stockpiles, x, y, z) != STOCKPILE_NONE) {
                    continue;
                }
                bool counted = false; /* By a station before this one. */
                for (int before = 0; before < index && !counted; before++) {
                    counted = workstation_zone_contains(&workstations->list[before], x, y, z);
                }
                if (counted) {
                    continue;
                }
                ItemStack groups[STOCKPILE_GROUPS_COUNTED];
                int count = items_groups(items, x, y, z, groups, STOCKPILE_GROUPS_COUNTED);
                for (int i = 0; i < count; i++) {
                    stored[groups[i].type] += groups[i].count;
                }
            }
        }
    }
}

bool workstation_needs_construction(WorkstationType type)
{
    const WorkstationDef *def = workstation_def(type);
    if (def == NULL) {
        return false;
    }
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        if (def->materials[slot].type != ITEM_NONE) {
            return true;
        }
    }
    return def->build_work > 0.0f;
}

/* What a bill starts with before anything is saved (bill_init()). */
static void defaults_standard(BillDefaults *defaults)
{
    *defaults = (BillDefaults){.mode = BILL_DO_TIMES,
                               .target = BILL_TARGET_DEFAULT,
                               .input_radius = BILL_INPUT_RADIUS_DEFAULT,
                               .worker = BILL_WORKER_ANYONE,
                               .min_level = (uint8_t)BILL_LEVEL_MIN,
                               .max_level = (uint8_t)BILL_LEVEL_MAX,
                               .colonist = ENTITY_NONE,
                               .min_quality = ITEM_QUALITY_WORST,
                               .max_quality = ITEM_QUALITY_BEST};
}

int workstations_place(Workstations *workstations, const Map *map, WorkstationType type, int x,
                       int y, int z)
{
    if ((unsigned)type >= WORKSTATION_TYPE_COUNT ||
        !workstation_can_place(workstations, map, x, y, z)) {
        return WORKSTATION_NONE;
    }
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        Workstation *station = &workstations->list[index];
        if (station->used) {
            continue;
        }
        memset(station, 0, sizeof(*station));
        station->used = true;
        station->type = (uint8_t)type;
        station->x = (int16_t)x;
        station->y = (int16_t)y;
        station->z = (int16_t)z;
        station->number = workstations->next_number++;
        station->work_recipe = RECIPE_COUNT;
        station->priority = JOB_PRIORITY_DEFAULT;
        defaults_standard(&station->defaults);
        if (WORKSTATION_DEFS[type].hearth) {
            station->keep_burning = true;
            station->fuel_types = hearth_default_fuel_types();
        }
        workstations->at[index_of(workstations, x, y, z)] = (uint8_t)(index + 1);
        workstations->used_count++;
        workstations->revision++;
        return index;
    }
    return WORKSTATION_NONE;
}

/* The ingredients of the craft under way, unless free, into `returned`: how many stacks. */
static int given_back(const Workstation *station, ItemStack returned[RECIPE_INGREDIENT_MAX])
{
    int count = 0;
    for (int i = 0; !station->free_input && i < RECIPE_INGREDIENT_MAX; i++) {
        if (station->work_inputs[i].count > 0) {
            returned[count++] = station->work_inputs[i];
        }
    }
    return count;
}

/* No craft under way any more. */
static void clear_craft(Workstation *station)
{
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        station->work_inputs[i] = (ItemStack){.type = ITEM_NONE};
    }
    station->work_recipe = RECIPE_COUNT;
    station->work_progress = 0.0f;
    station->free_input = false;
}

int workstations_remove(Workstations *workstations, int index,
                        ItemStack returned[RECIPE_INGREDIENT_MAX])
{
    Workstation *station = workstation_get(workstations, index);
    if (station == NULL) {
        return 0;
    }
    int count = given_back(station, returned);
    workstations->at[index_of(workstations, station->x, station->y, station->z)] = 0;
    memset(station, 0, sizeof(*station));
    workstations->used_count--;
    workstations->revision++;
    return count;
}

bool workstation_set_forbidden(Workstations *workstations, int index, bool forbidden)
{
    Workstation *station = workstation_get(workstations, index);
    if (station == NULL || station->forbidden == forbidden) {
        return false;
    }
    station->forbidden = forbidden;
    workstations->revision++;
    return true;
}

bool workstation_set_priority(Workstations *workstations, int index, int priority)
{
    Workstation *station = workstation_get(workstations, index);
    priority = job_priority_clamped(priority);
    if (station == NULL || station->priority == priority) {
        return false;
    }
    station->priority = (uint8_t)priority;
    workstations->revision++;
    return true;
}

static Bill *get_bill(Workstations *workstations, int index, int bill)
{
    Workstation *station = workstation_get(workstations, index);
    if (station == NULL || bill < 0 || bill >= station->bill_count) {
        return NULL;
    }
    return &station->bills[bill];
}

bool bill_init(Bill *bill, RecipeType recipe)
{
    if (recipe_def(recipe) == NULL) {
        return false;
    }
    memset(bill, 0, sizeof(*bill));
    bill->recipe = (uint8_t)recipe;
    bill->mode = BILL_DO_TIMES;
    bill->target = BILL_TARGET_DEFAULT;
    bill->inputs = recipe_inputs(recipe);
    bill->min_level = (uint8_t)BILL_LEVEL_MIN;
    bill->max_level = (uint8_t)BILL_LEVEL_MAX;
    bill->input_radius = BILL_INPUT_RADIUS_DEFAULT;
    bill->min_quality = ITEM_QUALITY_WORST;
    bill->max_quality = ITEM_QUALITY_BEST;
    return true;
}

bool workstation_bill_draft(const Workstations *workstations, int index, RecipeType recipe,
                            Bill *draft)
{
    const Workstation *station = workstation_get_const(workstations, index);
    if (station == NULL || !bill_init(draft, recipe)) {
        return false;
    }
    const BillDefaults *saved = &station->defaults;
    bill_set_mode(draft, (BillMode)saved->mode);
    bill_set_target(draft, saved->target);
    bill_set_input_radius(draft, saved->input_radius);
    /* Levels first, then the worker: a recipe that needs no knowledge keeps "anyone". */
    bill_set_max_level(draft, saved->max_level);
    bill_set_min_level(draft, saved->min_level);
    bill_set_worker(draft, (BillWorker)saved->worker, saved->colonist);
    bill_set_quality_range(draft, saved->min_quality, saved->max_quality);
    return true;
}

bool workstation_bill_has_default(const Workstation *station, const Bill *bill, BillSetting setting)
{
    const BillDefaults *saved = &station->defaults;
    switch (setting) {
    case BILL_SETTING_GOAL: return bill->mode == saved->mode && bill->target == saved->target;
    case BILL_SETTING_RADIUS: return bill->input_radius == saved->input_radius;
    case BILL_SETTING_WORKER:
        return bill->worker == saved->worker && bill->min_level == saved->min_level &&
               bill->max_level == saved->max_level &&
               entity_id_equal(bill->colonist, saved->colonist);
    case BILL_SETTING_QUALITY:
        return bill->min_quality == saved->min_quality && bill->max_quality == saved->max_quality;
    case BILL_SETTING_COUNT: break;
    }
    return false;
}

bool workstation_save_bill_default(Workstations *workstations, int index, const Bill *bill,
                                   BillSetting setting)
{
    Workstation *station = workstation_get(workstations, index);
    if (station == NULL || bill == NULL || (unsigned)setting >= BILL_SETTING_COUNT ||
        workstation_bill_has_default(station, bill, setting)) {
        return false;
    }
    BillDefaults *saved = &station->defaults;
    switch (setting) {
    case BILL_SETTING_GOAL:
        saved->mode = bill->mode;
        saved->target = bill->target;
        break;
    case BILL_SETTING_RADIUS: saved->input_radius = bill->input_radius; break;
    case BILL_SETTING_WORKER:
        saved->worker = bill->worker;
        saved->min_level = bill->min_level;
        saved->max_level = bill->max_level;
        saved->colonist = bill->colonist;
        break;
    case BILL_SETTING_QUALITY:
        saved->min_quality = bill->min_quality;
        saved->max_quality = bill->max_quality;
        break;
    case BILL_SETTING_COUNT: break;
    }
    station->defaults_saved = true;
    return true;
}

int workstation_add_bill(Workstations *workstations, int index, RecipeType recipe)
{
    Bill draft;
    return bill_init(&draft, recipe) ? workstation_add_bill_from(workstations, index, &draft) : -1;
}

int workstation_add_bill_from(Workstations *workstations, int index, const Bill *draft)
{
    Workstation *station = workstation_get(workstations, index);
    const RecipeDef *def = recipe_def((RecipeType)draft->recipe);
    if (station == NULL || def == NULL || !(def->stations & WORKSTATION_BIT(station->type)) ||
        station->bill_count >= WORKSTATION_BILL_MAX) {
        return -1;
    }
    /* Each setting through its setter: what is invalid stays as a new bill's. */
    Bill bill = {0};
    bill_init(&bill, (RecipeType)draft->recipe);
    bill_set_mode(&bill, (BillMode)draft->mode);
    bill_set_target(&bill, draft->target);
    bill.inputs = item_types_common(draft->inputs, recipe_inputs((RecipeType)draft->recipe));
    bill_set_worker(&bill, (BillWorker)draft->worker, draft->colonist);
    bill_set_max_level(&bill, draft->max_level);
    bill_set_min_level(&bill, draft->min_level);
    bill_set_paused(&bill, draft->paused);
    bill_set_input_radius(&bill, draft->input_radius);
    bill_set_quality_range(&bill, draft->min_quality, draft->max_quality);
    bill_set_woods(&bill, draft->woods);
    station->bills[station->bill_count] = bill;
    workstations->revision++;
    return station->bill_count++;
}

int workstation_remove_bill(Workstations *workstations, int index, int bill,
                            ItemStack returned[RECIPE_INGREDIENT_MAX])
{
    Workstation *station = workstation_get(workstations, index);
    if (get_bill(workstations, index, bill) == NULL) {
        return 0;
    }
    uint8_t recipe = station->bills[bill].recipe;
    memmove(&station->bills[bill], &station->bills[bill + 1],
            (size_t)(station->bill_count - bill - 1) * sizeof(Bill));
    station->bill_count--;
    workstations->revision++;
    for (int i = 0; i < station->bill_count; i++) {
        if (station->bills[i].recipe == recipe) {
            return 0; /* Another bill can finish the craft in progress. */
        }
    }
    if (station->work_recipe != recipe || !workstation_crafting(station)) {
        return 0;
    }
    int count = given_back(station, returned);
    clear_craft(station);
    return count;
}

bool bill_set_mode(Bill *bill, BillMode mode)
{
    if ((unsigned)mode >= BILL_MODE_COUNT) {
        return false;
    }
    if (bill->mode != mode) {
        bill->mode = (uint8_t)mode;
        bill->done = 0;
    }
    return true;
}

bool bill_set_target(Bill *bill, int target)
{
    bill->target = (uint16_t)clampi(target, BILL_TARGET_MIN, BILL_TARGET_MAX);
    return true;
}

bool bill_set_input(Bill *bill, ItemType type, bool allowed)
{
    if ((unsigned)type >= ITEM_TYPE_COUNT ||
        !item_types_has(recipe_inputs((RecipeType)bill->recipe), type)) {
        return false;
    }
    if (allowed) {
        item_types_add(&bill->inputs, type);
    } else {
        item_types_remove(&bill->inputs, type);
    }
    return true;
}

bool bill_set_worker(Bill *bill, BillWorker worker, EntityId colonist)
{
    if ((unsigned)worker >= BILL_WORKER_COUNT) {
        return false;
    }
    if (worker == BILL_WORKER_LEVEL &&
        recipe_def((RecipeType)bill->recipe)->knowledge == KNOWLEDGE_NONE) {
        return false;
    }
    if (worker == BILL_WORKER_SPECIFIC && entity_id_is_none(colonist)) {
        return false;
    }
    bill->worker = (uint8_t)worker;
    bill->colonist = worker == BILL_WORKER_SPECIFIC ? colonist : ENTITY_NONE;
    return true;
}

bool bill_set_min_level(Bill *bill, int level)
{
    bill->min_level = (uint8_t)clampi(level, BILL_LEVEL_MIN, bill->max_level);
    return true;
}

bool bill_set_max_level(Bill *bill, int level)
{
    bill->max_level = (uint8_t)clampi(level, bill->min_level, BILL_LEVEL_MAX);
    return true;
}

bool bill_set_woods(Bill *bill, WoodMask woods)
{
    bill->woods = woods;
    return true;
}

bool bill_set_quality_range(Bill *bill, int low, int high)
{
    low = clampi(low, ITEM_QUALITY_WORST, ITEM_QUALITY_BEST);
    high = clampi(high, ITEM_QUALITY_WORST, ITEM_QUALITY_BEST);
    bill->min_quality = (uint8_t)(low < high ? low : high);
    bill->max_quality = (uint8_t)(low < high ? high : low);
    return true;
}

bool bill_set_paused(Bill *bill, bool paused)
{
    bill->paused = paused;
    return true;
}

bool bill_set_input_radius(Bill *bill, int radius)
{
    bill->input_radius = (uint16_t)clampi(radius, BILL_INPUT_RADIUS_MIN, BILL_INPUT_RADIUS_MAX);
    return true;
}

/* A setter of a station's bill: through the bill's own, counted in the
 * revision when it takes. */
#define SET_BILL(call)                                                                             \
    do {                                                                                           \
        Bill *entry = get_bill(workstations, index, bill);                                         \
        if (entry == NULL || !(call)) {                                                            \
            return false;                                                                          \
        }                                                                                          \
        workstations->revision++;                                                                  \
        return true;                                                                               \
    } while (0)

bool workstation_set_bill_mode(Workstations *workstations, int index, int bill, BillMode mode)
{
    SET_BILL(bill_set_mode(entry, mode));
}

bool workstation_set_bill_target(Workstations *workstations, int index, int bill, int target)
{
    SET_BILL(bill_set_target(entry, target));
}

bool workstation_set_bill_input(Workstations *workstations, int index, int bill, ItemType type,
                                bool allowed)
{
    SET_BILL(bill_set_input(entry, type, allowed));
}

bool workstation_set_bill_worker(Workstations *workstations, int index, int bill, BillWorker worker,
                                 EntityId colonist)
{
    SET_BILL(bill_set_worker(entry, worker, colonist));
}

bool workstation_set_bill_min_level(Workstations *workstations, int index, int bill, int level)
{
    SET_BILL(bill_set_min_level(entry, level));
}

bool workstation_set_bill_max_level(Workstations *workstations, int index, int bill, int level)
{
    SET_BILL(bill_set_max_level(entry, level));
}

bool workstation_set_bill_woods(Workstations *workstations, int index, int bill, WoodMask woods)
{
    SET_BILL(bill_set_woods(entry, woods));
}

bool workstation_set_bill_quality_range(Workstations *workstations, int index, int bill, int low,
                                        int high)
{
    SET_BILL(bill_set_quality_range(entry, low, high));
}

bool workstation_set_bill_paused(Workstations *workstations, int index, int bill, bool paused)
{
    SET_BILL(bill_set_paused(entry, paused));
}

bool workstation_set_bill_input_radius(Workstations *workstations, int index, int bill, int radius)
{
    SET_BILL(bill_set_input_radius(entry, radius));
}

#undef SET_BILL

bool bill_accepts_worker(const Bill *bill, double level, EntityId who)
{
    switch ((BillWorker)bill->worker) {
    case BILL_WORKER_SPECIFIC: return entity_id_equal(bill->colonist, who);
    case BILL_WORKER_LEVEL: {
        const RecipeDef *def = recipe_def((RecipeType)bill->recipe);
        if (def == NULL || def->knowledge == KNOWLEDGE_NONE) {
            return true;
        }
        return level >= bill->min_level && level <= bill->max_level;
    }
    default: return true;
    }
}

bool bill_wants_more(const Bill *bill, int stored)
{
    switch ((BillMode)bill->mode) {
    case BILL_DO_TIMES: return bill->done < bill->target;
    case BILL_UNTIL_STOCK: return stored < bill->target;
    case BILL_FOREVER: return true;
    default: return false;
    }
}

bool bill_has_input(const Bill *bill, const long totals[ITEM_TYPE_COUNT],
                    const ItemTypeMask tools[RECIPE_TOOL_MAX])
{
    const RecipeDef *def = recipe_def((RecipeType)bill->recipe);
    if (def == NULL) {
        return false;
    }
    RecipeType recipe = (RecipeType)bill->recipe;
    ItemTypeMask inputs = item_types_common(bill->inputs, recipe_inputs(recipe));
    long left[ITEM_TYPE_COUNT];
    memcpy(left, totals, sizeof(left));
    /* Each tool still to fetch takes one item: of a type the bill does not
     * use if possible, else the most plentiful one. */
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        int best = ITEM_NONE;
        for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
            if (!(item_types_has(tools[slot], type)) || left[type] <= 0) {
                continue;
            }
            bool used = (item_types_has(inputs, type)) != 0;
            bool best_used = best != ITEM_NONE && (item_types_has(inputs, best)) != 0;
            if (best == ITEM_NONE || (best_used && !used) ||
                (best_used == used && left[type] > left[best])) {
                best = type;
            }
        }
        if (best != ITEM_NONE) {
            left[best]--;
        }
    }
    /* Each ingredient wants enough of one of its types (two kinds of one
     * recipe never share a type). */
    for (int i = 0; i < recipe_ingredient_count(recipe); i++) {
        ItemTypeMask types = item_types_common(inputs, recipe_ingredient_types(recipe, i));
        bool enough = false;
        for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT && !enough; type++) {
            enough = (item_types_has(types, type)) && left[type] >= def->ingredients[i].count;
        }
        if (!enough) {
            return false;
        }
    }
    return true;
}

bool workstation_start_craft(Workstations *workstations, int index, RecipeType recipe,
                             const ItemStack inputs[RECIPE_INGREDIENT_MAX])
{
    Workstation *station = workstation_get(workstations, index);
    int count = recipe_ingredient_count(recipe);
    if (station == NULL || workstation_crafting(station) || count == 0) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (inputs[i].count <= 0 || !recipe_accepts_ingredient(recipe, i, inputs[i])) {
            return false;
        }
    }
    station->work_recipe = (uint8_t)recipe;
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        station->work_inputs[i] = i < count ? inputs[i] : (ItemStack){.type = ITEM_NONE};
    }
    station->work_progress = 0.0f;
    station->free_input = false;
    return true;
}

bool workstation_gathers(const Workstation *station, ItemType type)
{
    for (int i = 0; station->used && i < station->bill_count; i++) {
        const Bill *bill = &station->bills[i];
        RecipeType recipe = (RecipeType)bill->recipe;
        if (!bill->paused && recipe_ingredient_count(recipe) > 1 &&
            recipe_ingredient_of(recipe, type) >= 0 && (item_types_has(bill->inputs, type))) {
            return true;
        }
    }
    return false;
}

bool bill_free_inputs(const Bill *bill, ItemStack out[RECIPE_INGREDIENT_MAX])
{
    RecipeType recipe = (RecipeType)bill->recipe;
    const RecipeDef *def = recipe_def(recipe);
    int count = recipe_ingredient_count(recipe);
    bool all = count > 0;
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        out[i] = item_stack_make(ITEM_NONE, 0, 0, 0);
        for (int alt = 0; i < count && alt < RECIPE_INPUT_ALTERNATIVES; alt++) {
            ItemType type = def->ingredients[i].types[alt];
            if (type != ITEM_NONE && (item_types_has(bill->inputs, type))) {
                int quality = def->input_min_quality > ITEM_QUALITY_STANDARD
                                  ? (int)def->input_min_quality
                                  : (int)ITEM_QUALITY_STANDARD;
                out[i] = item_stack_make(type, def->ingredients[i].count, quality, UINT16_MAX);
                break;
            }
        }
        all = all && (i >= count || out[i].count > 0);
    }
    return all;
}

bool workstation_start_free_craft(Workstations *workstations, int index, int bill)
{
    Bill *entry = get_bill(workstations, index, bill);
    ItemStack inputs[RECIPE_INGREDIENT_MAX];
    if (entry == NULL || !bill_free_inputs(entry, inputs) ||
        !workstation_start_craft(workstations, index, (RecipeType)entry->recipe, inputs)) {
        return false;
    }
    workstations->list[index].free_input = true;
    return true;
}

bool workstation_add_craft_work(Workstations *workstations, int index, int bill, Items *items,
                                const Map *map, float work, double level, ItemStack tool, Rng *rng,
                                uint16_t maker)
{
    Workstation *station = workstation_get(workstations, index);
    if (station == NULL || !workstation_crafting(station)) {
        return false;
    }
    RecipeType recipe = (RecipeType)station->work_recipe;
    station->work_progress += work * recipe_work_factor(recipe, station->work_inputs);
    if (station->work_progress < recipe_def(recipe)->work) {
        return false;
    }
    ItemStack products[RECIPE_PRODUCT_MAX];
    int count = recipe_craft_with(recipe, station->work_inputs, tool, level, rng, products);
    for (int i = 0; i < count; i++) {
        products[i].maker = maker;
        items_drop(items, map, station->x, station->y, station->z, products[i]);
    }
    for (int i = 0; i < RECIPE_INGREDIENT_MAX; i++) {
        ItemStack *input = &station->work_inputs[i];
        if (input->count > 0 && input->held != 0) { /* Its items are used up. */
            items_use_up(items, input, input->count);
        }
    }
    clear_craft(station);
    if (bill < 0 || bill >= station->bill_count || station->bills[bill].recipe != recipe) {
        bill = -1;
        for (int i = 0; i < station->bill_count && bill < 0; i++) {
            bill = station->bills[i].recipe == recipe ? i : -1;
        }
    }
    if (bill >= 0 && station->bills[bill].done < UINT16_MAX) {
        station->bills[bill].done++;
    }
    workstations->revision++;
    return true;
}

uint64_t workstations_hash(const Workstations *workstations, uint64_t hash)
{
    /* Field by field: struct padding bytes are unspecified. */
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        const Workstation *station = &workstations->list[index];
        if (!station->used) {
            continue;
        }
        int32_t ints[5] = {index, station->type, station->x, station->y, station->number};
        hash = hash_fnv1a64(hash, ints, sizeof(ints));
        if (station->z != 0) { /* Hashes of one-level games stay as they were. */
            hash = hash_fnv1a64(hash, &station->z, sizeof(station->z));
        }
        hash = hash_fnv1a64(hash, &station->work_progress, sizeof(station->work_progress));
        ItemStack input = station->work_inputs[0];
        int32_t craft[6] = {station->work_recipe, input.type,       input.quality,
                            input.count,          input.hit_points, input.progress};
        hash = hash_fnv1a64(hash, craft, sizeof(craft));
        for (int i = 1; i < RECIPE_INGREDIENT_MAX; i++) { /* (Hashes of one kind stay.) */
            ItemStack more = station->work_inputs[i];
            if (more.count > 0) {
                int32_t also[5] = {more.type, more.quality, more.count, more.hit_points,
                                   more.progress};
                hash = hash_fnv1a64(hash, also, sizeof(also));
            }
        }
        if (station->free_input) { /* (Only then: hashes stay as they were without it.) */
            hash = hash_fnv1a64(hash, &station->free_input, sizeof(station->free_input));
        }
        if (station->forbidden) { /* Likewise. */
            hash = hash_fnv1a64(hash, &station->forbidden, sizeof(station->forbidden));
        }
        if (station->priority != JOB_PRIORITY_DEFAULT) { /* Likewise. */
            hash = hash_fnv1a64(hash, &station->priority, sizeof(station->priority));
        }
        if (station->defaults_saved) { /* Likewise. */
            const BillDefaults *saved = &station->defaults;
            uint32_t defaults[9] = {saved->mode,
                                    saved->target,
                                    saved->input_radius,
                                    saved->worker,
                                    saved->min_level,
                                    saved->max_level,
                                    saved->colonist.index,
                                    saved->colonist.generation,
                                    (uint32_t)saved->min_quality << 8 | saved->max_quality};
            hash = hash_fnv1a64(hash, defaults, sizeof(defaults));
        }
        float fire[3] = {station->fuel, station->fuel_promised, station->soaked};
        hash = hash_fnv1a64(hash, fire, sizeof(fire));
        uint32_t fire_state[3] = {station->burning, station->keep_burning,
                                  (uint32_t)station->fuel_types.words[0]};
        hash = hash_fnv1a64(hash, fire_state, sizeof(fire_state));
        hash = item_types_hash_more(station->fuel_types, hash);
        hash = hash_fnv1a64(hash, &station->laid, sizeof(station->laid));
        for (int b = 0; b < station->bill_count; b++) {
            const Bill *bill = &station->bills[b];
            uint32_t fields[9] = {bill->recipe,
                                  bill->mode,
                                  bill->target,
                                  bill->done,
                                  (uint32_t)bill->inputs.words[0],
                                  bill->min_level,
                                  bill->max_level,
                                  bill->paused,
                                  bill->input_radius};
            hash = hash_fnv1a64(hash, fields, sizeof(fields));
            hash = item_types_hash_more(bill->inputs, hash);
            if (bill->min_quality != ITEM_QUALITY_WORST || bill->max_quality != ITEM_QUALITY_BEST) {
                uint8_t range[2] = {bill->min_quality, bill->max_quality};
                hash = hash_fnv1a64(hash, range, sizeof(range));
            }
            if (bill->woods != 0) {
                hash = hash_fnv1a64(hash, &bill->woods, sizeof(bill->woods));
            }
            if (bill->worker != BILL_WORKER_ANYONE) {
                uint32_t worker[3] = {bill->worker, bill->colonist.index,
                                      bill->colonist.generation};
                hash = hash_fnv1a64(hash, worker, sizeof(worker));
            }
        }
    }
    return hash;
}

static void bill_serialize(Serializer *s, Bill *bill)
{
    ser_u8_below(s, &bill->recipe, RECIPE_COUNT);
    ser_u8_below(s, &bill->mode, BILL_MODE_COUNT);
    ser_u16(s, &bill->target);
    ser_u16(s, &bill->done);
    item_types_serialize(s, &bill->inputs);
    ser_u8_below(s, &bill->worker, BILL_WORKER_COUNT);
    ser_u8(s, &bill->min_level);
    ser_u8(s, &bill->max_level);
    entity_id_serialize(s, &bill->colonist);
    ser_bool(s, &bill->paused);
    ser_u16(s, &bill->input_radius);
    ser_u8(s, &bill->min_quality);
    ser_u8(s, &bill->max_quality);
    ser_u16(s, &bill->woods);
}

void workstations_serialize(Serializer *s, Workstations *stations)
{
    size_t count = (size_t)stations->width * (size_t)stations->height * (size_t)stations->depth;
    ser_layer_u8_below(s, stations->at, count, WORKSTATION_MAX + 1);
    for (int i = 0; i < WORKSTATION_MAX; i++) {
        Workstation *station = &stations->list[i];
        ser_bool(s, &station->used);
        ser_u8_below(s, &station->type, WORKSTATION_TYPE_COUNT);
        ser_i16(s, &station->x);
        ser_i16(s, &station->y);
        ser_i16(s, &station->z);
        ser_int(s, &station->number);
        for (int b = 0; b < WORKSTATION_BILL_MAX; b++) {
            bill_serialize(s, &station->bills[b]);
        }
        ser_int_in(s, &station->bill_count, 0, WORKSTATION_BILL_MAX);
        ser_u8(s, &station->work_recipe);
        for (int input = 0; input < RECIPE_INGREDIENT_MAX; input++) {
            item_stack_serialize(s, &station->work_inputs[input]);
        }
        ser_float(s, &station->work_progress);
        ser_bool(s, &station->free_input);
        ser_float(s, &station->fuel);
        ser_bool(s, &station->burning);
        ser_bool(s, &station->keep_burning);
        item_types_serialize(s, &station->fuel_types);
        ser_float(s, &station->fuel_promised);
        ser_float(s, &station->soaked);
        ser_u8(s, &station->laid);
        ser_bool(s, &station->forbidden);
        ser_u8(s, &station->priority);
        ser_u8_below(s, &station->defaults.mode, BILL_MODE_COUNT);
        ser_u16(s, &station->defaults.target);
        ser_u16(s, &station->defaults.input_radius);
        ser_u8_below(s, &station->defaults.worker, BILL_WORKER_COUNT);
        ser_u8(s, &station->defaults.min_level);
        ser_u8(s, &station->defaults.max_level);
        entity_id_serialize(s, &station->defaults.colonist);
        ser_u8(s, &station->defaults.min_quality);
        ser_u8(s, &station->defaults.max_quality);
        ser_bool(s, &station->defaults_saved);
    }
    ser_int_in(s, &stations->used_count, 0, WORKSTATION_MAX);
    ser_int(s, &stations->next_number);
    ser_u32(s, &stations->revision);
}
