#include "core/gather_zone.h"

#include "core/i18n.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

/* Felling a tree: a fixed part plus a part proportional to its size. */
#define CHOP_BASE_WORK 400.0f
#define CHOP_GROWTH_WORK 1600.0f
#define HARVEST_WORK 300.0f
#define BRANCH_WORK 300.0f /* Per branch. */
#define TWIGS_WORK 150.0f  /* Per handful. */
#define CUT_GRASS_WORK 150.0f
#define STONE_WORK 200.0f /* Per stone. */
/* Armfuls of grass cut from a fully grown tile, proportionally fewer when shorter. */
#define CUT_GRASS_YIELD 3.0f
#define CUT_GRASS_MIN_GROWTH 0.5f

#define ONE(type) ITEM_TYPE_BIT(type)

static const GatherResourceDef RESOURCE_DEFS[GATHER_RESOURCE_COUNT] = {
    [GATHER_WOOD] = {"wood", N_("gather_zone.resource.wood"), ITEM_WOOD, ONE(ITEM_WOOD),
                     GATHER_JOB_CHOP, true},
    [GATHER_BERRIES] = {"berries", N_("gather_zone.resource.berries"), ITEM_BERRIES,
                        ONE(ITEM_BERRIES), GATHER_JOB_HARVEST, true},
    [GATHER_BRANCHES] = {"branches", N_("gather_zone.resource.branches"), ITEM_BRANCHES,
                         ONE(ITEM_BRANCHES), GATHER_JOB_BRANCHES, false},
    [GATHER_TWIGS] = {"twigs", N_("gather_zone.resource.twigs"), ITEM_TWIGS, ONE(ITEM_TWIGS),
                      GATHER_JOB_TWIGS, false},
    [GATHER_GRASS] = {"grass", N_("gather_zone.resource.grass"), ITEM_FRESH_GRASS,
                      ONE(ITEM_FRESH_GRASS) | ONE(ITEM_GRASS), GATHER_JOB_CUT_GRASS, false},
    [GATHER_FLINT] = {"flint", N_("gather_zone.resource.flint"), ITEM_FLINT, ONE(ITEM_FLINT),
                      GATHER_JOB_STONES, false},
    [GATHER_COBBLES] = {"cobbles", N_("gather_zone.resource.cobbles"), ITEM_COBBLE,
                        ONE(ITEM_COBBLE), GATHER_JOB_STONES, false},
};

#undef ONE

#define GATHERING WORK_GATHERING, KNOWLEDGE_GATHERING, KNOWLEDGE_MASTERY_LEVEL
#define RESOURCE(r) GATHER_RESOURCE_BIT(r)

static const GatherJobDef JOB_DEFS[GATHER_JOB_COUNT] = {
    [GATHER_JOB_HARVEST] = {"harvest", N_("colonist.task.harvest"), GATHERING,
                            RESOURCE(GATHER_BERRIES)},
    [GATHER_JOB_BRANCHES] = {"branches", N_("colonist.task.gather_branches"), GATHERING,
                             RESOURCE(GATHER_BRANCHES)},
    [GATHER_JOB_TWIGS] = {"twigs", N_("colonist.task.gather_twigs"), GATHERING,
                          RESOURCE(GATHER_TWIGS)},
    [GATHER_JOB_CUT_GRASS] = {"cut_grass", N_("colonist.task.cut_grass"), GATHERING,
                              RESOURCE(GATHER_GRASS)},
    [GATHER_JOB_CHOP] = {"chop", N_("colonist.task.chop"), WORK_WOODCUTTING, KNOWLEDGE_NONE,
                         KNOWLEDGE_MASTERY_LEVEL, RESOURCE(GATHER_WOOD)},
    [GATHER_JOB_STONES] = {"stones", N_("colonist.task.gather_stones"), GATHERING,
                           RESOURCE(GATHER_FLINT) | RESOURCE(GATHER_COBBLES)},
};

#undef GATHERING
#undef RESOURCE

_Static_assert(GATHER_RESOURCE_COUNT <= 32 && GATHER_JOB_COUNT < 255, "masks and job layer");

const GatherResourceDef *gather_resource_def(GatherResource resource)
{
    return (unsigned)resource < GATHER_RESOURCE_COUNT ? &RESOURCE_DEFS[resource] : NULL;
}

const GatherJobDef *gather_job_def(GatherJob job)
{
    return (unsigned)job < GATHER_JOB_COUNT ? &JOB_DEFS[job] : NULL;
}

bool gather_job_doable_at(GatherJob job, double level)
{
    const GatherJobDef *def = gather_job_def(job);
    return def != NULL && def->knowledge != KNOWLEDGE_NONE && level >= def->min_level;
}

/* ---- Tiles ------------------------------------------------------------------ */

bool gather_zones_init(GatherZones *zones, int width, int height)
{
    memset(zones, 0, sizeof(*zones));
    if (width <= 0 || height <= 0) {
        return false;
    }
    size_t count = (size_t)width * (size_t)height;
    zones->zone = calloc(count, sizeof(*zones->zone));
    zones->job = calloc(count, sizeof(*zones->job));
    zones->progress = calloc(count, sizeof(*zones->progress));
    if (zones->zone == NULL || zones->job == NULL || zones->progress == NULL) {
        gather_zones_free(zones);
        return false;
    }
    zones->width = width;
    zones->height = height;
    zones->next_number = 1;
    return true;
}

void gather_zones_free(GatherZones *zones)
{
    free(zones->zone);
    free(zones->job);
    free(zones->progress);
    memset(zones, 0, sizeof(*zones));
}

static bool in_bounds(const GatherZones *zones, int x, int y)
{
    return x >= 0 && y >= 0 && x < zones->width && y < zones->height;
}

static size_t index_of(const GatherZones *zones, int x, int y)
{
    return (size_t)y * (size_t)zones->width + (size_t)x;
}

static bool is_valid(const GatherZones *zones, int index)
{
    return index >= 0 && index < GATHER_ZONE_MAX && zones->list[index].used;
}

static void clear_progress(GatherZones *zones, size_t i)
{
    zones->job[i] = GATHER_JOB_NONE;
    zones->progress[i] = 0.0f;
}

static void count_wanting_cells(GatherZones *zones)
{
    zones->wanting_cells = 0;
    for (int i = 0; i < GATHER_ZONE_MAX; i++) {
        if (zones->list[i].used && zones->list[i].wanted != 0) {
            zones->wanting_cells += zones->list[i].cell_count;
        }
    }
}

/* Something changed that may open or close jobs. */
static void changed(GatherZones *zones)
{
    count_wanting_cells(zones);
    zones->revision++;
}

bool gather_zone_can_cover(const Map *map, int x, int y)
{
    if (!map_in_bounds(map, x, y)) {
        return false;
    }
    size_t index = map_index(map, x, y);
    Terrain terrain = (Terrain)map->terrain[index];
    return map->rock[index] == ROCK_NONE && terrain != TERRAIN_DEEP_WATER &&
           terrain != TERRAIN_SHALLOW_WATER &&
           !structure_blocks((StructureType)map->structure[index]);
}

bool gather_stone_ground(const Map *map, int x, int y)
{
    if (!map_in_bounds(map, x, y)) {
        return false;
    }
    size_t index = map_index(map, x, y);
    Terrain terrain = (Terrain)map->terrain[index];
    return map->rock[index] == ROCK_NONE && map->structure[index] == STRUCTURE_NONE &&
           (terrain == TERRAIN_SOIL || terrain == TERRAIN_RICH_SOIL ||
            terrain == TERRAIN_GRAVEL || terrain == TERRAIN_ROUGH_STONE);
}

/* Clips a rectangle given by two corners in any order to the zone layer. */
static void clip_rect(const GatherZones *zones, int x0, int y0, int x1, int y1, int *min_x,
                      int *min_y, int *max_x, int *max_y)
{
    *min_x = clampi(x0 < x1 ? x0 : x1, 0, zones->width - 1);
    *max_x = clampi(x0 < x1 ? x1 : x0, 0, zones->width - 1);
    *min_y = clampi(y0 < y1 ? y0 : y1, 0, zones->height - 1);
    *max_y = clampi(y0 < y1 ? y1 : y0, 0, zones->height - 1);
}

/* Adds the free tiles of a rectangle a zone may cover and that `blocked` (a
 * layer the size of the map, may be NULL) leaves open. */
static int extend(GatherZones *zones, const Map *map, int index, int x0, int y0, int x1, int y1,
                  const uint8_t *blocked)
{
    if (!is_valid(zones, index)) {
        return 0;
    }
    int min_x, min_y, max_x, max_y;
    clip_rect(zones, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    int added = 0;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            size_t i = index_of(zones, x, y);
            if (zones->zone[i] == 0 && (blocked == NULL || blocked[i] == 0) &&
                gather_zone_can_cover(map, x, y)) {
                zones->zone[i] = (uint8_t)(index + 1);
                clear_progress(zones, i);
                added++;
            }
        }
    }
    zones->list[index].cell_count += added;
    if (added > 0) {
        changed(zones);
    }
    return added;
}

int gather_zone_extend(GatherZones *zones, const Map *map, int index, int x0, int y0, int x1,
                       int y1)
{
    return extend(zones, map, index, x0, y0, x1, y1, NULL);
}

static int create(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1,
                  const uint8_t *blocked)
{
    int index = GATHER_ZONE_NONE;
    for (int i = 0; i < GATHER_ZONE_MAX; i++) {
        if (!zones->list[i].used) {
            index = i;
            break;
        }
    }
    if (index == GATHER_ZONE_NONE) {
        return GATHER_ZONE_NONE;
    }
    GatherZone *zone = &zones->list[index];
    *zone = (GatherZone){.used = true, .number = zones->next_number};
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        zone->orders[r] = (GatherOrder){.ripe_only = RESOURCE_DEFS[r].ripe_option,
                                        .mode = GATHER_UNTIL,
                                        .target = GATHER_TARGET_DEFAULT};
    }
    if (extend(zones, map, index, x0, y0, x1, y1, blocked) == 0) {
        zone->used = false;
        return GATHER_ZONE_NONE;
    }
    zones->used_count++;
    zones->next_number++;
    return index;
}

int gather_zone_create(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1)
{
    return create(zones, map, x0, y0, x1, y1, NULL);
}

int gather_zones_add_rect(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1,
                          int preferred, const uint8_t *blocked)
{
    int min_x, min_y, max_x, max_y;
    clip_rect(zones, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    bool overlapped[GATHER_ZONE_MAX] = {false};
    int into = is_valid(zones, preferred) ? preferred : GATHER_ZONE_NONE;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int zone = zones->zone[index_of(zones, x, y)] - 1;
            if (zone == GATHER_ZONE_NONE || overlapped[zone]) {
                continue;
            }
            overlapped[zone] = true;
            if (into == GATHER_ZONE_NONE ||
                (into != preferred && zones->list[zone].number < zones->list[into].number)) {
                into = zone; /* The oldest, unless one is preferred. */
            }
        }
    }
    if (into == GATHER_ZONE_NONE) {
        return create(zones, map, x0, y0, x1, y1, blocked);
    }
    bool merged = false;
    size_t count = (size_t)zones->width * (size_t)zones->height;
    for (size_t i = 0; i < count; i++) {
        int zone = zones->zone[i] - 1;
        if (zone != GATHER_ZONE_NONE && zone != into && overlapped[zone]) {
            zones->zone[i] = (uint8_t)(into + 1); /* Its progress stays. */
            zones->list[into].cell_count++;
            merged = true;
        }
    }
    for (int zone = 0; zone < GATHER_ZONE_MAX; zone++) {
        if (overlapped[zone] && zone != into) {
            zones->list[zone] = (GatherZone){.used = false};
            zones->used_count--;
        }
    }
    if (merged) {
        changed(zones);
    }
    extend(zones, map, into, x0, y0, x1, y1, blocked);
    return into;
}

int gather_zones_remove_rect(GatherZones *zones, int x0, int y0, int x1, int y1)
{
    int min_x, min_y, max_x, max_y;
    clip_rect(zones, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    int removed = 0;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            size_t i = index_of(zones, x, y);
            if (zones->zone[i] != 0) {
                GatherZone *zone = &zones->list[zones->zone[i] - 1];
                if (--zone->cell_count == 0) {
                    zone->used = false;
                    zones->used_count--;
                }
                zones->zone[i] = 0;
                clear_progress(zones, i);
                removed++;
            }
        }
    }
    if (removed > 0) {
        changed(zones);
    }
    return removed;
}

void gather_zone_delete(GatherZones *zones, int index)
{
    if (!is_valid(zones, index)) {
        return;
    }
    size_t count = (size_t)zones->width * (size_t)zones->height;
    for (size_t i = 0; i < count; i++) {
        if (zones->zone[i] == index + 1) {
            zones->zone[i] = 0;
            clear_progress(zones, i);
        }
    }
    zones->list[index] = (GatherZone){.used = false};
    zones->used_count--;
    changed(zones);
}

const GatherZone *gather_zone_get(const GatherZones *zones, int index)
{
    return is_valid(zones, index) ? &zones->list[index] : NULL;
}

int gather_zone_at(const GatherZones *zones, int x, int y)
{
    if (!in_bounds(zones, x, y)) {
        return GATHER_ZONE_NONE;
    }
    return (int)zones->zone[index_of(zones, x, y)] - 1;
}

/* ---- Orders ----------------------------------------------------------------- */

static GatherOrder *get_order(GatherZones *zones, int index, GatherResource resource)
{
    if (!is_valid(zones, index) || (unsigned)resource >= GATHER_RESOURCE_COUNT) {
        return NULL;
    }
    return &zones->list[index].orders[resource];
}

void gather_zone_set_enabled(GatherZones *zones, int index, GatherResource resource, bool on)
{
    GatherOrder *order = get_order(zones, index, resource);
    if (order != NULL && order->enabled != on) {
        order->enabled = on;
        order->done = 0;
        changed(zones);
    }
}

void gather_zone_set_mode(GatherZones *zones, int index, GatherResource resource,
                          GatherMode mode)
{
    GatherOrder *order = get_order(zones, index, resource);
    if (order != NULL && (unsigned)mode < GATHER_MODE_COUNT && order->mode != mode) {
        order->mode = (uint8_t)mode;
        order->done = 0;
        changed(zones);
    }
}

void gather_zone_set_target(GatherZones *zones, int index, GatherResource resource,
                            int target)
{
    GatherOrder *order = get_order(zones, index, resource);
    if (order == NULL) {
        return;
    }
    uint16_t value = (uint16_t)clampi(target, GATHER_TARGET_MIN, GATHER_TARGET_MAX);
    if (order->target != value) {
        order->target = value;
        changed(zones);
    }
}

void gather_zone_set_ripe_only(GatherZones *zones, int index, GatherResource resource,
                               bool ripe_only)
{
    GatherOrder *order = get_order(zones, index, resource);
    if (order != NULL && RESOURCE_DEFS[resource].ripe_option && order->ripe_only != ripe_only) {
        order->ripe_only = ripe_only;
        changed(zones);
    }
}

long gather_resource_count(GatherResource resource, const long totals[ITEM_TYPE_COUNT])
{
    const GatherResourceDef *def = gather_resource_def(resource);
    long count = 0;
    for (int type = 0; def != NULL && type < ITEM_TYPE_COUNT; type++) {
        if (def->counted & ITEM_TYPE_BIT(type)) {
            count += totals[type];
        }
    }
    return count;
}

GatherResourceMask gather_zone_wanted(const GatherZone *zone, const long totals[ITEM_TYPE_COUNT])
{
    GatherResourceMask wanted = 0;
    for (int r = 0; zone != NULL && r < GATHER_RESOURCE_COUNT; r++) {
        const GatherOrder *order = &zone->orders[r];
        bool want = false;
        if (order->enabled) {
            switch ((GatherMode)order->mode) {
            case GATHER_DO_COUNT: want = order->done < order->target; break;
            case GATHER_UNTIL:
                want = gather_resource_count((GatherResource)r, totals) < (long)order->target;
                break;
            default: want = true; break;
            }
        }
        if (want) {
            wanted |= GATHER_RESOURCE_BIT(r);
        }
    }
    return wanted;
}

void gather_zones_update(GatherZones *zones, const long totals[ITEM_TYPE_COUNT])
{
    bool any_change = false;
    for (int i = 0; i < GATHER_ZONE_MAX; i++) {
        GatherZone *zone = &zones->list[i];
        if (!zone->used) {
            continue;
        }
        GatherResourceMask wanted = gather_zone_wanted(zone, totals);
        if (wanted != zone->wanted) {
            zone->wanted = wanted;
            any_change = true;
        }
    }
    if (any_change) {
        changed(zones);
    }
}

void gather_zones_plants_changed(GatherZones *zones)
{
    if (zones->wanting_cells > 0) {
        zones->revision++;
    }
}

/* ---- Jobs ------------------------------------------------------------------- */

static bool ripe_enough(const GatherZone *zone, GatherResource resource, const Plant *plant)
{
    return !zone->orders[resource].ripe_only || plant->growth >= 1.0f;
}

/* Whether the tile holds something for the job, given what the zone wants. */
static bool job_open(const GatherZone *zone, const Map *map, int x, int y, GatherJob job)
{
    if ((zone->wanted & JOB_DEFS[job].resources) == 0 || !gather_zone_can_cover(map, x, y)) {
        return false;
    }
    const Plant *plant = &map->plants[map_index(map, x, y)];
    switch (job) {
    case GATHER_JOB_HARVEST: {
        const PlantDef *def = plant_def((PlantType)plant->type);
        return def->harvest_item != ITEM_NONE && plant->growth >= def->min_edible_growth &&
               ripe_enough(zone, GATHER_BERRIES, plant);
    }
    case GATHER_JOB_BRANCHES: return plant_stock_available(plant, PLANT_STOCK_BRANCHES) > 0;
    case GATHER_JOB_TWIGS: return plant_stock_available(plant, PLANT_STOCK_TWIGS) > 0;
    case GATHER_JOB_CUT_GRASS:
        return plant->type == PLANT_GRASS && plant->growth >= CUT_GRASS_MIN_GROWTH;
    case GATHER_JOB_CHOP:
        return plant->type == PLANT_TREE && ripe_enough(zone, GATHER_WOOD, plant);
    case GATHER_JOB_STONES:
        return gather_stone_ground(map, x, y) &&
               ((zone->wanted & GATHER_RESOURCE_BIT(GATHER_COBBLES)) ||
                map_flint_chance(map, x, y) > 0);
    default: return false;
    }
}

bool gather_job_available(const GatherZones *zones, const Map *map, int x, int y, GatherJob job)
{
    const GatherZone *zone = gather_zone_get(zones, gather_zone_at(zones, x, y));
    return zone != NULL && (unsigned)job < GATHER_JOB_COUNT && job_open(zone, map, x, y, job);
}

GatherJob gather_tile_job(const GatherZones *zones, const Map *map, int x, int y,
                          GatherJobMask allowed)
{
    const GatherZone *zone = gather_zone_get(zones, gather_zone_at(zones, x, y));
    if (zone == NULL || zone->wanted == 0) {
        return GATHER_JOB_NONE;
    }
    for (int job = 0; job < GATHER_JOB_COUNT; job++) {
        if ((allowed & GATHER_JOB_BIT(job)) && job_open(zone, map, x, y, (GatherJob)job)) {
            return (GatherJob)job;
        }
    }
    return GATHER_JOB_NONE;
}

int gather_zone_flint_cells(const GatherZones *zones, const Map *map, int index)
{
    if (!is_valid(zones, index)) {
        return 0;
    }
    int count = 0;
    for (int y = 0; y < zones->height; y++) {
        for (int x = 0; x < zones->width; x++) {
            if (zones->zone[index_of(zones, x, y)] == index + 1 &&
                gather_stone_ground(map, x, y) && map_flint_chance(map, x, y) > 0) {
                count++;
            }
        }
    }
    return count;
}

float gather_job_work_needed(const Map *map, int x, int y, GatherJob job)
{
    if (!map_in_bounds(map, x, y)) {
        return 0.0f;
    }
    const Plant *plant = &map->plants[map_index(map, x, y)];
    switch (job) {
    case GATHER_JOB_HARVEST: return HARVEST_WORK;
    case GATHER_JOB_BRANCHES: {
        int available = plant_stock_available(plant, PLANT_STOCK_BRANCHES);
        return BRANCH_WORK * (float)(available > 1 ? available : 1);
    }
    case GATHER_JOB_TWIGS: {
        int available = plant_stock_available(plant, PLANT_STOCK_TWIGS);
        return TWIGS_WORK * (float)(available > 1 ? available : 1);
    }
    case GATHER_JOB_CUT_GRASS: return CUT_GRASS_WORK;
    case GATHER_JOB_CHOP: return CHOP_BASE_WORK + CHOP_GROWTH_WORK * plant->growth;
    case GATHER_JOB_STONES: return STONE_WORK;
    default: return 0.0f;
    }
}

/* Carries out a completed job on the map; returns what it gives (count 0: nothing). */
static ItemType complete(Map *map, int x, int y, GatherJob job, Rng *rng, int *count)
{
    Plant *plant = &map->plants[map_index(map, x, y)];
    const PlantDef *def = plant_def((PlantType)plant->type);
    ItemType item = ITEM_NONE;
    *count = 0;
    switch (job) {
    case GATHER_JOB_HARVEST:
        item = def->harvest_item;
        *count = (int)((float)def->harvest_yield * plant->growth + 0.5f);
        plant->growth = PLANT_GROWTH_AFTER_PICKING;
        break;
    case GATHER_JOB_BRANCHES:
    case GATHER_JOB_TWIGS: {
        PlantStock stock = job == GATHER_JOB_BRANCHES ? PLANT_STOCK_BRANCHES : PLANT_STOCK_TWIGS;
        item = plant_stock_item(stock);
        *count = plant_take_stock(plant, stock, INT16_MAX);
        break;
    }
    case GATHER_JOB_CUT_GRASS: {
        int armfuls = (int)(CUT_GRASS_YIELD * plant->growth + 0.5f);
        item = ITEM_FRESH_GRASS;
        *count = armfuls > 1 ? armfuls : 1;
        plant->growth = 0.0f;
        break;
    }
    case GATHER_JOB_CHOP:
        item = def->cut_item;
        *count = (int)((float)def->cut_yield * plant->growth + 0.5f);
        memset(plant, 0, sizeof(*plant));
        break;
    case GATHER_JOB_STONES: {
        /* On a flint patch, some of the stones are flint (randomness only drawn there). */
        int flint = map_flint_chance(map, x, y);
        item = flint > 0 && rng != NULL && rng_range_i32(rng, 0, 99) < flint ? ITEM_FLINT
                                                                             : ITEM_COBBLE;
        *count = 1;
        break;
    }
    default: break;
    }
    return item;
}

/* The resource an item counts as for its job's orders (GATHER_RESOURCE_COUNT: none). */
static GatherResource resource_of(GatherJob job, ItemType item)
{
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        if (RESOURCE_DEFS[r].job == job && RESOURCE_DEFS[r].item == item) {
            return (GatherResource)r;
        }
    }
    return GATHER_RESOURCE_COUNT;
}

bool gather_zone_add_work(GatherZones *zones, Map *map, Items *items, int x, int y, GatherJob job,
                          float work, double level, Rng *rng)
{
    int zone_index = gather_zone_at(zones, x, y);
    if (!gather_job_available(zones, map, x, y, job)) {
        return false;
    }
    GatherZone *zone = &zones->list[zone_index];
    size_t index = index_of(zones, x, y);
    if (zones->job[index] != job) {
        zones->job[index] = (uint8_t)job;
        zones->progress[index] = 0.0f;
    }
    zones->progress[index] += work;
    if (zones->progress[index] < gather_job_work_needed(map, x, y, job)) {
        return false;
    }
    clear_progress(zones, index);
    int count;
    ItemType item = complete(map, x, y, job, rng, &count);
    GatherResource resource = resource_of(job, item);
    if (resource == GATHER_RESOURCE_COUNT || count <= 0 ||
        !(zone->wanted & GATHER_RESOURCE_BIT(resource))) {
        return true; /* Nothing, or a stone nobody wants: left where it lay. */
    }
    if (items != NULL) {
        ItemStack stack = knowledge_product(item, count, level, rng);
        items_drop(items, map, x, y, stack);
    }
    GatherOrder *order = &zone->orders[resource];
    order->done = (uint16_t)clampi(order->done + count, 0, UINT16_MAX);
    if (order->mode == GATHER_DO_COUNT && order->done >= order->target) {
        zone->wanted &= ~GATHER_RESOURCE_BIT(resource);
        changed(zones);
    }
    return true;
}

float gather_zone_progress_fraction(const GatherZones *zones, const Map *map, int x, int y)
{
    if (!in_bounds(zones, x, y)) {
        return 0.0f;
    }
    size_t index = index_of(zones, x, y);
    float needed = gather_job_work_needed(map, x, y, (GatherJob)zones->job[index]);
    if (needed <= 0.0f) {
        return 0.0f;
    }
    return clampf(zones->progress[index] / needed, 0.0f, 1.0f);
}

uint64_t gather_zones_hash(const GatherZones *zones, uint64_t hash)
{
    size_t count = (size_t)zones->width * (size_t)zones->height;
    hash = hash_fnv1a64(hash, zones->zone, count * sizeof(*zones->zone));
    hash = hash_fnv1a64(hash, zones->job, count * sizeof(*zones->job));
    hash = hash_fnv1a64(hash, zones->progress, count * sizeof(*zones->progress));
    for (int i = 0; i < GATHER_ZONE_MAX; i++) {
        const GatherZone *zone = &zones->list[i];
        if (!zone->used) {
            continue;
        }
        int32_t fields[3] = {i, zone->number, (int32_t)zone->wanted};
        hash = hash_fnv1a64(hash, fields, sizeof(fields));
        for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
            const GatherOrder *order = &zone->orders[r];
            int32_t values[5] = {order->enabled, order->ripe_only, order->mode, order->target,
                                 order->done};
            hash = hash_fnv1a64(hash, values, sizeof(values));
        }
    }
    return hash;
}
