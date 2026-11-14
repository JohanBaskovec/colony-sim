#include "core/designation.h"

#include "core/i18n.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

/* Chopping a tree: a fixed part plus a part proportional to its size. */
#define CHOP_BASE_WORK 400.0f
#define CHOP_GROWTH_WORK 1600.0f

static const DesignationDef DESIGNATION_DEFS[DESIGNATION_COUNT] = {
    [DESIGNATION_NONE] = {"none", NULL, NULL},
    [DESIGNATION_CHOP] = {"chop", N_("designation.chop"), N_("colonist.task.chop")},
    [DESIGNATION_MINE] = {"mine", N_("designation.mine"), N_("colonist.task.mine")},
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
    designations->reserved_by = calloc(count, sizeof(*designations->reserved_by));
    designations->progress = calloc(count, sizeof(*designations->progress));
    if (designations->type == NULL || designations->reserved_by == NULL ||
        designations->progress == NULL) {
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
    free(designations->reserved_by);
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

bool designation_can_apply(const Map *map, int x, int y, DesignationType type)
{
    if (!map_in_bounds(map, x, y)) {
        return false;
    }
    size_t index = map_index(map, x, y);
    switch (type) {
    case DESIGNATION_CHOP:
        return map->plants[index].type == PLANT_TREE && map->rock[index] == ROCK_NONE;
    case DESIGNATION_MINE: return map->rock[index] != ROCK_NONE;
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
    if (designations->type[index] == DESIGNATION_NONE) {
        return;
    }
    designations->count--;
    if (designations->reserved_by[index] == DESIGNATION_NO_OWNER) {
        designations->available--;
    }
    designations->type[index] = DESIGNATION_NONE;
    designations->reserved_by[index] = DESIGNATION_NO_OWNER;
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
        designations->available++;
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

bool designation_is_available(const Designations *designations, int x, int y)
{
    if (!in_bounds(designations, x, y)) {
        return false;
    }
    size_t index = index_of(designations, x, y);
    return designations->type[index] != DESIGNATION_NONE &&
           designations->reserved_by[index] == DESIGNATION_NO_OWNER;
}

uint8_t designation_owner(const Designations *designations, int x, int y)
{
    if (!in_bounds(designations, x, y)) {
        return DESIGNATION_NO_OWNER;
    }
    return designations->reserved_by[index_of(designations, x, y)];
}

bool designation_reserve(Designations *designations, int x, int y, uint8_t owner)
{
    if (owner == DESIGNATION_NO_OWNER || !designation_is_available(designations, x, y)) {
        return false;
    }
    designations->reserved_by[index_of(designations, x, y)] = owner;
    designations->available--;
    return true;
}

void designation_release(Designations *designations, int x, int y, uint8_t owner)
{
    if (owner == DESIGNATION_NO_OWNER || designation_owner(designations, x, y) != owner) {
        return;
    }
    designations->reserved_by[index_of(designations, x, y)] = DESIGNATION_NO_OWNER;
    designations->available++;
    designations->revision++;
}

float designation_work_needed(const Designations *designations, const Map *map, int x, int y)
{
    if (!map_in_bounds(map, x, y)) {
        return 0.0f;
    }
    size_t index = map_index(map, x, y);
    switch (designation_get(designations, x, y)) {
    case DESIGNATION_CHOP: return CHOP_BASE_WORK + CHOP_GROWTH_WORK * map->plants[index].growth;
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

/* Carries out a completed order on the map. */
static void complete(Map *map, int x, int y, DesignationType type)
{
    size_t index = map_index(map, x, y);
    switch (type) {
    case DESIGNATION_CHOP:
        map->plants[index].type = PLANT_NONE;
        map->plants[index].growth = 0.0f;
        break;
    case DESIGNATION_MINE:
        /* The rough stone floor under the rock stays. */
        map->rock[index] = ROCK_NONE;
        break;
    default: break;
    }
}

bool designation_add_work(Designations *designations, Map *map, int x, int y, float work)
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
    complete(map, x, y, type);
    clear_tile(designations, index);
    designations->revision++; /* The map changed: new tiles may be reachable. */
    return true;
}

uint64_t designations_hash(const Designations *designations, uint64_t hash)
{
    size_t count = (size_t)designations->width * (size_t)designations->height;
    hash = hash_fnv1a64(hash, designations->type, count);
    hash = hash_fnv1a64(hash, designations->reserved_by, count);
    hash = hash_fnv1a64(hash, designations->progress, count * sizeof(*designations->progress));
    return hash;
}
