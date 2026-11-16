#include "core/workstation.h"

#include "core/i18n.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

static const WorkstationDef WORKSTATION_DEFS[WORKSTATION_TYPE_COUNT] = {
    /* A place to sit and work: nothing to bring, so it is ready at once. */
    [WORKSTATION_CRAFTING_SPOT] = {"crafting_spot", N_("workstation.crafting_spot"),
                                   N_("workstation.crafting_spot.description"),
                                   {{ITEM_NONE, 0}, {ITEM_NONE, 0}}, 0.0f, KNOWLEDGE_NONE, 0.0f},
    /* An anvil stone to knap on. */
    [WORKSTATION_KNAPPING_SPOT] = {"knapping_spot", N_("workstation.knapping_spot"),
                                   N_("workstation.knapping_spot.description"),
                                   {{ITEM_COBBLE, 1}, {ITEM_NONE, 0}}, 250.0f,
                                   KNOWLEDGE_STONE_KNAPPING, 50.0f},
    /* A shallow pit ringed with stones: nothing to bring, a little digging. */
    [WORKSTATION_OPEN_HEARTH] = {"open_hearth", N_("workstation.open_hearth"),
                                 N_("workstation.open_hearth.description"),
                                 {{ITEM_NONE, 0}, {ITEM_NONE, 0}}, 250.0f, KNOWLEDGE_FIRE, 50.0f,
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
           map_is_buildable(map, x, y, z) &&
           !map_plant_blocks_building(map, x, y, z) &&
           workstation_at(workstations, x, y, z) == WORKSTATION_NONE;
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
        if (WORKSTATION_DEFS[type].hearth) {
            station->keep_burning = true;
            station->fuel_types = HEARTH_FUEL_TYPES;
        }
        workstations->at[index_of(workstations, x, y, z)] = (uint8_t)(index + 1);
        workstations->used_count++;
        workstations->revision++;
        return index;
    }
    return WORKSTATION_NONE;
}

int workstations_remove(Workstations *workstations, int index, ItemStack returned[1])
{
    Workstation *station = workstation_get(workstations, index);
    if (station == NULL) {
        return 0;
    }
    int count = 0;
    if (station->work_input.count > 0 && !station->free_input) {
        returned[count++] = station->work_input;
    }
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

static Bill *get_bill(Workstations *workstations, int index, int bill)
{
    Workstation *station = workstation_get(workstations, index);
    if (station == NULL || bill < 0 || bill >= station->bill_count) {
        return NULL;
    }
    return &station->bills[bill];
}

int workstation_add_bill(Workstations *workstations, int index, RecipeType recipe)
{
    Workstation *station = workstation_get(workstations, index);
    const RecipeDef *def = recipe_def(recipe);
    if (station == NULL || def == NULL || !(def->stations & WORKSTATION_BIT(station->type)) ||
        station->bill_count >= WORKSTATION_BILL_MAX) {
        return -1;
    }
    Bill *bill = &station->bills[station->bill_count];
    memset(bill, 0, sizeof(*bill));
    bill->recipe = (uint8_t)recipe;
    bill->mode = BILL_DO_TIMES;
    bill->target = BILL_TARGET_DEFAULT;
    bill->inputs = recipe_inputs(recipe);
    bill->min_level = (uint8_t)BILL_LEVEL_MIN;
    bill->max_level = (uint8_t)BILL_LEVEL_MAX;
    bill->input_radius = BILL_INPUT_RADIUS_DEFAULT;
    workstations->revision++;
    return station->bill_count++;
}

bool workstation_remove_bill(Workstations *workstations, int index, int bill,
                             ItemStack *returned)
{
    Workstation *station = workstation_get(workstations, index);
    if (get_bill(workstations, index, bill) == NULL) {
        return false;
    }
    uint8_t recipe = station->bills[bill].recipe;
    memmove(&station->bills[bill], &station->bills[bill + 1],
            (size_t)(station->bill_count - bill - 1) * sizeof(Bill));
    station->bill_count--;
    workstations->revision++;
    for (int i = 0; i < station->bill_count; i++) {
        if (station->bills[i].recipe == recipe) {
            return false; /* Another bill can finish the craft in progress. */
        }
    }
    if (station->work_recipe != recipe || station->work_input.count == 0) {
        return false;
    }
    bool given_back = !station->free_input;
    *returned = station->work_input;
    station->work_input = (ItemStack){.type = ITEM_NONE};
    station->work_recipe = RECIPE_COUNT;
    station->work_progress = 0.0f;
    station->free_input = false;
    return given_back;
}

bool workstation_set_bill_mode(Workstations *workstations, int index, int bill, BillMode mode)
{
    Bill *entry = get_bill(workstations, index, bill);
    if (entry == NULL || (unsigned)mode >= BILL_MODE_COUNT) {
        return false;
    }
    if (entry->mode != mode) {
        entry->mode = (uint8_t)mode;
        entry->done = 0;
        workstations->revision++;
    }
    return true;
}

bool workstation_set_bill_target(Workstations *workstations, int index, int bill, int target)
{
    Bill *entry = get_bill(workstations, index, bill);
    if (entry == NULL) {
        return false;
    }
    entry->target = (uint16_t)clampi(target, BILL_TARGET_MIN, BILL_TARGET_MAX);
    workstations->revision++;
    return true;
}

bool workstation_set_bill_input(Workstations *workstations, int index, int bill, ItemType type,
                                bool allowed)
{
    Bill *entry = get_bill(workstations, index, bill);
    if (entry == NULL || (unsigned)type >= ITEM_TYPE_COUNT ||
        !(recipe_inputs((RecipeType)entry->recipe) & ITEM_TYPE_BIT(type))) {
        return false;
    }
    if (allowed) {
        entry->inputs |= ITEM_TYPE_BIT(type);
    } else {
        entry->inputs &= ~ITEM_TYPE_BIT(type);
    }
    workstations->revision++;
    return true;
}

bool workstation_set_bill_min_level(Workstations *workstations, int index, int bill, int level)
{
    Bill *entry = get_bill(workstations, index, bill);
    if (entry == NULL) {
        return false;
    }
    entry->min_level = (uint8_t)clampi(level, BILL_LEVEL_MIN, entry->max_level);
    workstations->revision++;
    return true;
}

bool workstation_set_bill_max_level(Workstations *workstations, int index, int bill, int level)
{
    Bill *entry = get_bill(workstations, index, bill);
    if (entry == NULL) {
        return false;
    }
    entry->max_level = (uint8_t)clampi(level, entry->min_level, BILL_LEVEL_MAX);
    workstations->revision++;
    return true;
}

bool workstation_set_bill_paused(Workstations *workstations, int index, int bill, bool paused)
{
    Bill *entry = get_bill(workstations, index, bill);
    if (entry == NULL) {
        return false;
    }
    entry->paused = paused;
    workstations->revision++;
    return true;
}

bool workstation_set_bill_input_radius(Workstations *workstations, int index, int bill,
                                       int radius)
{
    Bill *entry = get_bill(workstations, index, bill);
    if (entry == NULL) {
        return false;
    }
    entry->input_radius =
        (uint16_t)clampi(radius, BILL_INPUT_RADIUS_MIN, BILL_INPUT_RADIUS_MAX);
    workstations->revision++;
    return true;
}

bool bill_accepts_level(const Bill *bill, double level)
{
    const RecipeDef *def = recipe_def((RecipeType)bill->recipe);
    if (def == NULL || def->knowledge == KNOWLEDGE_NONE) {
        return true;
    }
    return level >= bill->min_level && level <= bill->max_level;
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
    ItemTypeMask inputs = bill->inputs & recipe_inputs((RecipeType)bill->recipe);
    long left[ITEM_TYPE_COUNT];
    memcpy(left, totals, sizeof(left));
    /* Each tool still to fetch takes one item: of a type the bill does not
     * use if possible, else the most plentiful one. */
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        int best = ITEM_NONE;
        for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
            if (!(tools[slot] & ITEM_TYPE_BIT(type)) || left[type] <= 0) {
                continue;
            }
            bool used = (inputs & ITEM_TYPE_BIT(type)) != 0;
            bool best_used = best != ITEM_NONE && (inputs & ITEM_TYPE_BIT(best)) != 0;
            if (best == ITEM_NONE || (best_used && !used) ||
                (best_used == used && left[type] > left[best])) {
                best = type;
            }
        }
        if (best != ITEM_NONE) {
            left[best]--;
        }
    }
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        if ((inputs & ITEM_TYPE_BIT(type)) && left[type] >= def->input_count) {
            return true;
        }
    }
    return false;
}

bool workstation_start_craft(Workstations *workstations, int index, RecipeType recipe,
                             ItemStack input)
{
    Workstation *station = workstation_get(workstations, index);
    if (station == NULL || station->work_input.count > 0 ||
        !recipe_accepts_input(recipe, input)) {
        return false;
    }
    station->work_recipe = (uint8_t)recipe;
    station->work_input = input;
    station->work_progress = 0.0f;
    station->free_input = false;
    return true;
}

ItemStack bill_free_input(const Bill *bill)
{
    const RecipeDef *def = recipe_def((RecipeType)bill->recipe);
    for (int i = 0; def != NULL && i < RECIPE_INPUT_ALTERNATIVES; i++) {
        ItemType type = def->inputs[i];
        if (type != ITEM_NONE && (bill->inputs & ITEM_TYPE_BIT(type))) {
            int quality = def->input_min_quality > ITEM_QUALITY_STANDARD
                              ? (int)def->input_min_quality
                              : (int)ITEM_QUALITY_STANDARD;
            return item_stack_make(type, def->input_count, quality, UINT16_MAX);
        }
    }
    return item_stack_make(ITEM_NONE, 0, 0, 0);
}

bool workstation_start_free_craft(Workstations *workstations, int index, int bill)
{
    Bill *entry = get_bill(workstations, index, bill);
    if (entry == NULL ||
        !workstation_start_craft(workstations, index, (RecipeType)entry->recipe,
                                 bill_free_input(entry))) {
        return false;
    }
    workstations->list[index].free_input = true;
    return true;
}

bool workstation_add_craft_work(Workstations *workstations, int index, int bill, Items *items,
                                const Map *map, float work, double level, Rng *rng)
{
    Workstation *station = workstation_get(workstations, index);
    if (station == NULL || station->work_input.count == 0) {
        return false;
    }
    RecipeType recipe = (RecipeType)station->work_recipe;
    station->work_progress += work;
    if (station->work_progress < recipe_def(recipe)->work) {
        return false;
    }
    ItemStack products[RECIPE_PRODUCT_MAX];
    int count = recipe_craft(recipe, station->work_input, level, rng, products);
    for (int i = 0; i < count; i++) {
        items_drop(items, map, station->x, station->y, station->z, products[i]);
    }
    station->work_input = (ItemStack){.type = ITEM_NONE};
    station->work_recipe = RECIPE_COUNT;
    station->work_progress = 0.0f;
    station->free_input = false;
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
        ItemStack input = station->work_input;
        int32_t craft[6] = {station->work_recipe, input.type,       input.quality,
                            input.count,          input.hit_points, input.progress};
        hash = hash_fnv1a64(hash, craft, sizeof(craft));
        if (station->free_input) { /* (Only then: hashes stay as they were without it.) */
            hash = hash_fnv1a64(hash, &station->free_input, sizeof(station->free_input));
        }
        if (station->forbidden) { /* Likewise. */
            hash = hash_fnv1a64(hash, &station->forbidden, sizeof(station->forbidden));
        }
        float fire[3] = {station->fuel, station->fuel_promised, station->soaked};
        hash = hash_fnv1a64(hash, fire, sizeof(fire));
        uint32_t fire_state[3] = {station->burning, station->keep_burning, station->fuel_types};
        hash = hash_fnv1a64(hash, fire_state, sizeof(fire_state));
        for (int b = 0; b < station->bill_count; b++) {
            const Bill *bill = &station->bills[b];
            uint32_t fields[9] = {bill->recipe,    bill->mode,      bill->target,
                                  bill->done,      bill->inputs,    bill->min_level,
                                  bill->max_level, bill->paused,    bill->input_radius};
            hash = hash_fnv1a64(hash, fields, sizeof(fields));
        }
    }
    return hash;
}
