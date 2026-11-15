#include "core/gather_zone.h"

#include "core/i18n.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

/* Felling a tree: a fixed part plus a part proportional to its size. */
#define CHOP_BASE_WORK 400.0f
#define CHOP_GROWTH_WORK 1600.0f
/* Wild food, per item taken: 360 for a bramble's nine handfuls, 480 for a
 * hazel's twelve nuts; 500 to dig four cattail rhizomes up. */
#define HARVEST_WORK 40.0f
#define DIG_WORK 125.0f
#define BRANCH_WORK 300.0f /* Per branch. */
#define TWIGS_WORK 150.0f  /* Per handful. */
#define CUT_GRASS_WORK 150.0f
#define STONE_WORK 200.0f /* Per stone. */
/* Armfuls of grass cut from a fully grown tile, proportionally fewer when shorter. */
#define CUT_GRASS_YIELD 3.0f
#define CLEAR_SHRUB_WORK 500.0f /* Pulling up a shrub by hand. */
#define CUT_GRASS_MIN_GROWTH 0.5f

#define ONE(type) ITEM_TYPE_BIT(type)

static const GatherResourceDef RESOURCE_DEFS[GATHER_RESOURCE_COUNT] = {
    [GATHER_WOOD] = {"wood", N_("gather_zone.resource.wood"), ITEM_WOOD, ONE(ITEM_WOOD),
                     GATHER_JOB_CHOP, true},
    [GATHER_BLACKBERRIES] = {"blackberries", N_("gather_zone.resource.blackberries"),
                             ITEM_BLACKBERRIES, ONE(ITEM_BLACKBERRIES), GATHER_JOB_HARVEST, false},
    [GATHER_HAZELNUTS] = {"hazelnuts", N_("gather_zone.resource.hazelnuts"), ITEM_HAZELNUTS,
                          ONE(ITEM_HAZELNUTS), GATHER_JOB_HARVEST, false},
    [GATHER_MUSHROOMS] = {"mushrooms", N_("gather_zone.resource.mushrooms"), ITEM_MUSHROOMS,
                          ONE(ITEM_MUSHROOMS), GATHER_JOB_HARVEST_CAREFULLY, false},
    [GATHER_WILD_GARLIC] = {"wild_garlic", N_("gather_zone.resource.wild_garlic"),
                            ITEM_WILD_GARLIC, ONE(ITEM_WILD_GARLIC), GATHER_JOB_HARVEST_CAREFULLY,
                            false},
    [GATHER_NETTLES] = {"nettles", N_("gather_zone.resource.nettles"), ITEM_NETTLES,
                        ONE(ITEM_NETTLES), GATHER_JOB_HARVEST, false},
    [GATHER_PLANTAIN] = {"plantain", N_("gather_zone.resource.plantain"), ITEM_PLANTAIN,
                         ONE(ITEM_PLANTAIN), GATHER_JOB_HARVEST, false},
    [GATHER_WILD_CARROTS] = {"wild_carrots", N_("gather_zone.resource.wild_carrots"),
                             ITEM_WILD_CARROTS, ONE(ITEM_WILD_CARROTS), GATHER_JOB_DIG_CAREFULLY,
                             false},
    [GATHER_CATTAIL_RHIZOMES] = {"cattail_rhizomes", N_("gather_zone.resource.cattail_rhizomes"),
                                 ITEM_CATTAIL_RHIZOMES, ONE(ITEM_CATTAIL_RHIZOMES), GATHER_JOB_DIG,
                                 false},
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
#define CAREFUL WORK_GATHERING, KNOWLEDGE_GATHERING, GATHER_CAREFUL_MIN_LEVEL
#define RESOURCE(r) GATHER_RESOURCE_BIT(r)

static const GatherJobDef JOB_DEFS[GATHER_JOB_COUNT] = {
    [GATHER_JOB_HARVEST] = {"harvest", N_("colonist.task.harvest"), GATHERING,
                            RESOURCE(GATHER_BLACKBERRIES) | RESOURCE(GATHER_HAZELNUTS) |
                                RESOURCE(GATHER_NETTLES) | RESOURCE(GATHER_PLANTAIN),
                            TOOL_USE_NONE, false},
    [GATHER_JOB_HARVEST_CAREFULLY] = {"harvest_carefully", N_("colonist.task.harvest_carefully"),
                                      CAREFUL,
                                      RESOURCE(GATHER_MUSHROOMS) | RESOURCE(GATHER_WILD_GARLIC),
                                      TOOL_USE_NONE, false},
    [GATHER_JOB_DIG] = {"dig", N_("colonist.task.dig"), GATHERING,
                        RESOURCE(GATHER_CATTAIL_RHIZOMES), TOOL_USE_DIGGING, true},
    [GATHER_JOB_DIG_CAREFULLY] = {"dig_carefully", N_("colonist.task.dig_carefully"), CAREFUL,
                                  RESOURCE(GATHER_WILD_CARROTS), TOOL_USE_DIGGING, true},
    [GATHER_JOB_BRANCHES] = {"branches", N_("colonist.task.gather_branches"), GATHERING,
                             RESOURCE(GATHER_BRANCHES), TOOL_USE_NONE, false},
    [GATHER_JOB_TWIGS] = {"twigs", N_("colonist.task.gather_twigs"), GATHERING,
                          RESOURCE(GATHER_TWIGS), TOOL_USE_NONE, false},
    [GATHER_JOB_CUT_GRASS] = {"cut_grass", N_("colonist.task.cut_grass"), GATHERING,
                              RESOURCE(GATHER_GRASS), TOOL_USE_CUT_GRASS, false},
    [GATHER_JOB_CHOP] = {"chop", N_("colonist.task.chop"), WORK_WOODCUTTING, KNOWLEDGE_NONE,
                         KNOWLEDGE_MASTERY_LEVEL, RESOURCE(GATHER_WOOD), TOOL_USE_NONE, false},
    [GATHER_JOB_STONES] = {"stones", N_("colonist.task.gather_stones"), GATHERING,
                           RESOURCE(GATHER_FLINT) | RESOURCE(GATHER_COBBLES), TOOL_USE_NONE,
                           false},
};

#undef GATHERING
#undef CAREFUL
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
           !structure_blocks((StructureType)map->structure[index]);
}

bool gather_stone_ground(const Map *map, int x, int y)
{
    if (!map_in_bounds(map, x, y)) {
        return false;
    }
    size_t index = map_index(map, x, y);
    return map->rock[index] == ROCK_NONE && map->structure[index] == STRUCTURE_NONE &&
           terrain_def((Terrain)map->terrain[index])->stony;
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

/* Adds the free tiles of a rectangle a zone may cover and that no layer of
 * `blocked` (see gather_zones_add_rect()) covers. */
static int extend(GatherZones *zones, const Map *map, int index, int x0, int y0, int x1, int y1,
                  const uint8_t *const *blocked)
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
            if (zones->zone[i] == 0 && !layers_any_set(blocked, i) &&
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
                  const uint8_t *const *blocked)
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
    *zone = (GatherZone){.used = true,
                         .number = zones->next_number,
                         .priority = GATHER_ZONE_PRIORITY_DEFAULT};
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
                          int preferred, const uint8_t *const *blocked)
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

void gather_zone_set_priority(GatherZones *zones, int index, int priority)
{
    if (!is_valid(zones, index)) {
        return;
    }
    priority = clampi(priority, GATHER_ZONE_PRIORITY_HIGHEST, GATHER_ZONE_PRIORITY_LOWEST);
    if (zones->list[index].priority != priority) {
        zones->list[index].priority = priority;
        changed(zones);
    }
}

int gather_zone_priority_at(const GatherZones *zones, int x, int y)
{
    const GatherZone *zone = gather_zone_get(zones, gather_zone_at(zones, x, y));
    return zone != NULL ? zone->priority : 0;
}

uint32_t gather_zones_wanting_priorities(const GatherZones *zones)
{
    uint32_t priorities = 0;
    for (int i = 0; i < GATHER_ZONE_MAX; i++) {
        const GatherZone *zone = &zones->list[i];
        if (zone->used && zone->wanted != 0) {
            priorities |= 1u << (unsigned)zone->priority;
        }
    }
    return priorities;
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

/* The resource an item is gathered as (GATHER_RESOURCE_COUNT: none). */
static GatherResource resource_of_item(ItemType item)
{
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        if (RESOURCE_DEFS[r].item == item) {
            return (GatherResource)r;
        }
    }
    return GATHER_RESOURCE_COUNT;
}

/* Whether a plant bears food now, of one of `resources`. */
static bool food_among(const Plant *plant, GatherResourceMask resources)
{
    GatherResource resource = resource_of_item(plant_stock_item((PlantType)plant->type,
                                                                PLANT_STOCK_FOOD));
    return resource != GATHER_RESOURCE_COUNT && (resources & GATHER_RESOURCE_BIT(resource)) &&
           plant_has_food(plant);
}

/* Stones of a resource's type on a tile (0 for the other resources). */
static int stones_of(const Map *map, int x, int y, GatherResource resource)
{
    const GatherResourceDef *def = &RESOURCE_DEFS[resource];
    return def->job == GATHER_JOB_STONES ? map_stones(map, x, y, def->item) : 0;
}

/* tile_holds() on a tile a zone can cover (checked by the caller). */
static bool covered_tile_holds(const GatherZone *zone, const Map *map, int x, int y,
                               GatherJob job, GatherResourceMask resources)
{
    if (resources == 0) {
        return false;
    }
    const Plant *plant = &map->plants[map_index(map, x, y)];
    switch (job) {
    case GATHER_JOB_HARVEST:
    case GATHER_JOB_HARVEST_CAREFULLY:
    case GATHER_JOB_DIG:
    case GATHER_JOB_DIG_CAREFULLY: return food_among(plant, resources);
    case GATHER_JOB_BRANCHES: return plant_stock_available(plant, PLANT_STOCK_BRANCHES) > 0;
    case GATHER_JOB_TWIGS: return plant_stock_available(plant, PLANT_STOCK_TWIGS) > 0;
    case GATHER_JOB_CUT_GRASS:
        return plant->type == PLANT_GRASS && plant->growth >= CUT_GRASS_MIN_GROWTH;
    case GATHER_JOB_CHOP:
        return plant->type == PLANT_TREE && ripe_enough(zone, GATHER_WOOD, plant);
    case GATHER_JOB_STONES:
        return gather_stone_ground(map, x, y) &&
               (((resources & GATHER_RESOURCE_BIT(GATHER_COBBLES)) &&
                 stones_of(map, x, y, GATHER_COBBLES) > 0) ||
                ((resources & GATHER_RESOURCE_BIT(GATHER_FLINT)) &&
                 stones_of(map, x, y, GATHER_FLINT) > 0));
    default: return false;
    }
}

/* Whether the tile holds something for the job, among `resources` (only
 * stones care which: a tile may hold cobbles and no flint). */
static bool tile_holds(const GatherZone *zone, const Map *map, int x, int y, GatherJob job,
                       GatherResourceMask resources)
{
    return resources != 0 && gather_zone_can_cover(map, x, y) &&
           covered_tile_holds(zone, map, x, y, job, resources);
}

/* Whether the tile holds something for the job that the zone wants. */
static bool job_open(const GatherZone *zone, const Map *map, int x, int y, GatherJob job)
{
    return tile_holds(zone, map, x, y, job, zone->wanted & JOB_DEFS[job].resources);
}

/* Items a plant job would give on a tile now (0 for stones and others). */
static int plant_yield(const Map *map, int x, int y, GatherJob job)
{
    const Plant *plant = &map->plants[map_index(map, x, y)];
    const PlantDef *def = plant_def((PlantType)plant->type);
    switch (job) {
    case GATHER_JOB_HARVEST:
    case GATHER_JOB_HARVEST_CAREFULLY:
    case GATHER_JOB_DIG:
    case GATHER_JOB_DIG_CAREFULLY: return plant_stock_available(plant, PLANT_STOCK_FOOD);
    case GATHER_JOB_BRANCHES: return plant_stock_available(plant, PLANT_STOCK_BRANCHES);
    case GATHER_JOB_TWIGS: return plant_stock_available(plant, PLANT_STOCK_TWIGS);
    case GATHER_JOB_CUT_GRASS: {
        int armfuls = (int)(CUT_GRASS_YIELD * plant->growth + 0.5f);
        return armfuls > 1 ? armfuls : 1;
    }
    case GATHER_JOB_CHOP: return (int)((float)def->cut_yield * plant->growth + 0.5f);
    default: return 0;
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
    /* Called for every tile a job search looks at: the cover is checked once. */
    if (!gather_zone_can_cover(map, x, y)) {
        return GATHER_JOB_NONE;
    }
    for (int job = 0; job < GATHER_JOB_COUNT; job++) {
        GatherResourceMask wanted = zone->wanted & JOB_DEFS[job].resources;
        if ((allowed & GATHER_JOB_BIT(job)) && wanted != 0 &&
            covered_tile_holds(zone, map, x, y, (GatherJob)job, wanted)) {
            return (GatherJob)job;
        }
    }
    return GATHER_JOB_NONE;
}

void gather_zone_available(const GatherZones *zones, const Map *map, int index,
                           long available[GATHER_RESOURCE_COUNT])
{
    memset(available, 0, sizeof(long) * GATHER_RESOURCE_COUNT);
    if (!is_valid(zones, index)) {
        return;
    }
    const GatherZone *zone = &zones->list[index];
    for (int y = 0; y < zones->height; y++) {
        for (int x = 0; x < zones->width; x++) {
            if (zones->zone[index_of(zones, x, y)] != index + 1) {
                continue;
            }
            for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
                GatherJob job = RESOURCE_DEFS[r].job;
                if (!tile_holds(zone, map, x, y, job, GATHER_RESOURCE_BIT(r))) {
                    continue;
                }
                available[r] += job == GATHER_JOB_STONES ? stones_of(map, x, y, (GatherResource)r)
                                                         : plant_yield(map, x, y, job);
            }
        }
    }
}

float gather_job_work_needed(const Map *map, int x, int y, GatherJob job)
{
    if (!map_in_bounds(map, x, y)) {
        return 0.0f;
    }
    const Plant *plant = &map->plants[map_index(map, x, y)];
    int food = plant_stock_available(plant, PLANT_STOCK_FOOD);
    switch (job) {
    case GATHER_JOB_HARVEST:
    case GATHER_JOB_HARVEST_CAREFULLY: return HARVEST_WORK * (float)(food > 1 ? food : 1);
    case GATHER_JOB_DIG:
    case GATHER_JOB_DIG_CAREFULLY: return DIG_WORK * (float)(food > 1 ? food : 1);
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

/* The stone a gatherer finds on a tile among the `wanted` types it holds:
 * picked at random in proportion to their numbers (the first one without
 * `rng`). ITEM_NONE when there is none. */
static ItemType find_stone(const Map *map, int x, int y, GatherResourceMask wanted, Rng *rng)
{
    int cobbles = wanted & GATHER_RESOURCE_BIT(GATHER_COBBLES)
                      ? stones_of(map, x, y, GATHER_COBBLES)
                      : 0;
    int flint = wanted & GATHER_RESOURCE_BIT(GATHER_FLINT) ? stones_of(map, x, y, GATHER_FLINT)
                                                            : 0;
    if (cobbles + flint == 0) {
        return ITEM_NONE;
    }
    if (cobbles == 0 || (flint > 0 && rng != NULL && rng_range_i32(rng, 1, cobbles + flint) <=
                                                         flint)) {
        return ITEM_FLINT;
    }
    return ITEM_COBBLE;
}

/* Carries out a completed job on the map; returns what it gives (count 0:
 * nothing). Stones are taken among the `wanted` types. */
static ItemType complete(Map *map, int x, int y, GatherJob job, GatherResourceMask wanted,
                         Rng *rng, int *count)
{
    Plant *plant = &map->plants[map_index(map, x, y)];
    const PlantDef *def = plant_def((PlantType)plant->type);
    ItemType item = ITEM_NONE;
    *count = plant_yield(map, x, y, job);
    switch (job) {
    case GATHER_JOB_HARVEST:
    case GATHER_JOB_HARVEST_CAREFULLY:
    case GATHER_JOB_DIG:
    case GATHER_JOB_DIG_CAREFULLY:
        item = def->food_item;
        *count = plant_take_stock(plant, PLANT_STOCK_FOOD, INT16_MAX);
        if (def->vanishes) {
            memset(plant, 0, sizeof(*plant)); /* The patch is gone. */
        } else if (def->dug_whole) {
            plant->growth = 0.0f; /* Grows again from what is left in the ground. */
            memset(plant->stock, 0, sizeof(plant->stock));
        }
        break;
    case GATHER_JOB_BRANCHES:
    case GATHER_JOB_TWIGS: {
        PlantStock stock = job == GATHER_JOB_BRANCHES ? PLANT_STOCK_BRANCHES : PLANT_STOCK_TWIGS;
        item = plant_stock_item((PlantType)plant->type, stock);
        *count = plant_take_stock(plant, stock, INT16_MAX);
        break;
    }
    case GATHER_JOB_CUT_GRASS:
        item = ITEM_FRESH_GRASS;
        plant->growth = 0.0f;
        break;
    case GATHER_JOB_CHOP:
        item = def->cut_item;
        memset(plant, 0, sizeof(*plant));
        break;
    case GATHER_JOB_STONES:
        item = find_stone(map, x, y, wanted, rng);
        *count = map_take_stone(map, x, y, item) ? 1 : 0;
        break;
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
    ItemType item = complete(map, x, y, job, zone->wanted, rng, &count);
    GatherResource resource = resource_of(job, item);
    if (resource == GATHER_RESOURCE_COUNT || count <= 0 ||
        !(zone->wanted & GATHER_RESOURCE_BIT(resource))) {
        return true; /* Nothing. */
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

float gather_clear_work(const Map *map, int x, int y)
{
    if (!map_in_bounds(map, x, y)) {
        return 0.0f;
    }
    const Plant *plant = &map->plants[map_index(map, x, y)];
    if (plant_is_dormant(plant)) {
        return 0.0f; /* Nothing in the way: what is left in the ground goes with the work. */
    }
    switch ((PlantType)plant->type) {
    case PLANT_NONE:
    case PLANT_TREE:
    case PLANT_COUNT: return 0.0f;
    case PLANT_HAZEL:
    case PLANT_BRAMBLE: return CLEAR_SHRUB_WORK;
    case PLANT_GRASS:
    case PLANT_WILD_GARLIC:
    case PLANT_NETTLE:
    case PLANT_PLANTAIN:
    case PLANT_WILD_CARROT:
    case PLANT_CATTAIL:
    case PLANT_MUSHROOMS: return CUT_GRASS_WORK;
    }
    return 0.0f;
}

ToolUse gather_clear_tool_use(const Map *map, int x, int y)
{
    return map_in_bounds(map, x, y) && map->plants[map_index(map, x, y)].type == PLANT_GRASS
               ? TOOL_USE_CUT_GRASS
               : TOOL_USE_NONE;
}

void gather_clear_plant(Map *map, Items *items, int x, int y, double level, Rng *rng)
{
    if (!map_in_bounds(map, x, y)) {
        return;
    }
    Plant *plant = &map->plants[map_index(map, x, y)];
    GatherJob jobs[4];
    int job_count = 0;
    if (plant->type == PLANT_GRASS && plant->growth > 0.0f) {
        jobs[job_count++] = GATHER_JOB_CUT_GRASS;
    } else if (plant->type != PLANT_GRASS) {
        jobs[job_count++] = GATHER_JOB_BRANCHES;
        jobs[job_count++] = GATHER_JOB_TWIGS;
        if (plant_has_food(plant)) {
            jobs[job_count++] = GATHER_JOB_HARVEST; /* Food of any kind, dug or picked. */
        }
    }
    for (int i = 0; i < job_count; i++) {
        int count;
        ItemType item = complete(map, x, y, jobs[i], 0, rng, &count);
        if (item != ITEM_NONE && count > 0 && items != NULL) {
            items_drop(items, map, x, y, knowledge_product(item, count, level, rng));
        }
    }
    memset(plant, 0, sizeof(*plant));
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
        int32_t fields[4] = {i, zone->number, (int32_t)zone->wanted, zone->priority};
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
