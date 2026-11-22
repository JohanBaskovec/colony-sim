#include "core/gather_zone.h"

#include "core/colonist.h"
#include "core/crown.h"
#include "core/i18n.h"
#include "core/tree.h"
#include "core/util.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Wild food, per item taken: 360 for a bramble's nine handfuls; 500 to dig
 * four cattail rhizomes up. */
#define HARVEST_WORK 40.0f
#define DIG_WORK 125.0f
#define CUT_GRASS_WORK 150.0f /* Clearing a tile of low plants. */
#define CUT_GRASS_ARMFUL_WORK 50.0f
#define CUT_STEMS_WORK 250.0f   /* A bundle of stems or leaves: 0.1 h (docs/CONTAINERS.md §4). */
#define PEEL_WORK 500.0f        /* A sheet of bark: 0.2 h (docs/CONTAINERS.md §4.3). */
#define CUT_RODS_WORK 750.0f    /* A bundle of rods: 0.3 h (docs/CONTAINERS.md §4.3). */
#define CLEAR_SHRUB_WORK 500.0f /* Pulling up a shrub by hand. */

#define ONE(type)                                                                                  \
    {                                                                                              \
        type                                                                                       \
    }

#define WOOD GATHER_GROUP_WOOD
#define FOOD GATHER_GROUP_FOOD
#define FIBRES GATHER_GROUP_FIBRES
#define MATERIALS GATHER_GROUP_MATERIALS

static const GatherResourceDef RESOURCE_DEFS[GATHER_RESOURCE_COUNT] = {
    [GATHER_WOOD] = {"wood", N_("gather_zone.resource.wood"), ITEM_WOOD, ONE(ITEM_WOOD),
                     GATHER_JOB_CHOP, true, WOOD},
    [GATHER_BLACKBERRIES] = {"blackberries", N_("gather_zone.resource.blackberries"),
                             ITEM_BLACKBERRIES, ONE(ITEM_BLACKBERRIES), GATHER_JOB_HARVEST, true,
                             FOOD},
    [GATHER_MUSHROOMS] = {"mushrooms", N_("gather_zone.resource.mushrooms"), ITEM_MUSHROOMS,
                          ONE(ITEM_MUSHROOMS), GATHER_JOB_HARVEST_CAREFULLY, true, FOOD},
    [GATHER_WILD_GARLIC] = {"wild_garlic", N_("gather_zone.resource.wild_garlic"), ITEM_WILD_GARLIC,
                            ONE(ITEM_WILD_GARLIC), GATHER_JOB_HARVEST_CAREFULLY, true, FOOD},
    [GATHER_NETTLES] = {"nettles", N_("gather_zone.resource.nettles"), ITEM_NETTLES,
                        ONE(ITEM_NETTLES), GATHER_JOB_HARVEST, true, FOOD},
    [GATHER_PLANTAIN] = {"plantain", N_("gather_zone.resource.plantain"), ITEM_PLANTAIN,
                         ONE(ITEM_PLANTAIN), GATHER_JOB_HARVEST, true, FOOD},
    [GATHER_WILD_CARROTS] = {"wild_carrots", N_("gather_zone.resource.wild_carrots"),
                             ITEM_WILD_CARROTS, ONE(ITEM_WILD_CARROTS), GATHER_JOB_DIG_CAREFULLY,
                             true, FOOD},
    [GATHER_CATTAIL_RHIZOMES] = {"cattail_rhizomes", N_("gather_zone.resource.cattail_rhizomes"),
                                 ITEM_CATTAIL_RHIZOMES, ONE(ITEM_CATTAIL_RHIZOMES), GATHER_JOB_DIG,
                                 true, FOOD},
    [GATHER_WOOD_SORREL] = {"wood_sorrel", N_("gather_zone.resource.wood_sorrel"), ITEM_WOOD_SORREL,
                            ONE(ITEM_WOOD_SORREL), GATHER_JOB_HARVEST, true, FOOD},
    [GATHER_WILD_STRAWBERRIES] = {"wild_strawberries", N_("gather_zone.resource.wild_strawberries"),
                                  ITEM_WILD_STRAWBERRIES, ONE(ITEM_WILD_STRAWBERRIES),
                                  GATHER_JOB_HARVEST, true, FOOD},
    [GATHER_BILBERRIES] = {"bilberries", N_("gather_zone.resource.bilberries"), ITEM_BILBERRIES,
                           ONE(ITEM_BILBERRIES), GATHER_JOB_HARVEST, true, FOOD},
    [GATHER_ROSE_HIPS] = {"rose_hips", N_("gather_zone.resource.rose_hips"), ITEM_ROSE_HIPS,
                          ONE(ITEM_ROSE_HIPS), GATHER_JOB_HARVEST, true, FOOD},
    [GATHER_HAZELNUTS] = {"hazelnuts", N_("gather_zone.resource.hazelnuts"), ITEM_HAZELNUTS,
                          ONE(ITEM_HAZELNUTS), GATHER_JOB_PICK, false, FOOD},
    [GATHER_CHESTNUTS] = {"chestnuts", N_("gather_zone.resource.chestnuts"), ITEM_CHESTNUTS,
                          ONE(ITEM_CHESTNUTS), GATHER_JOB_PICK, false, FOOD},
    [GATHER_APPLES] = {"apples", N_("gather_zone.resource.apples"), ITEM_APPLES, ONE(ITEM_APPLES),
                       GATHER_JOB_PICK, false, FOOD},
    [GATHER_PEARS] = {"pears", N_("gather_zone.resource.pears"), ITEM_PEARS, ONE(ITEM_PEARS),
                      GATHER_JOB_PICK, false, FOOD},
    [GATHER_GRASS] = {"grass", N_("gather_zone.resource.grass"), ITEM_GRASS, ONE(ITEM_GRASS),
                      GATHER_JOB_CUT_GRASS, true, FIBRES},
    [GATHER_MOSS] = {"moss", N_("gather_zone.resource.moss"), ITEM_MOSS, ONE(ITEM_MOSS),
                     GATHER_JOB_CUT_GRASS, true, FIBRES},
    [GATHER_BRACKEN] = {"bracken", N_("gather_zone.resource.bracken"), ITEM_BRACKEN,
                        ONE(ITEM_BRACKEN), GATHER_JOB_CUT_GRASS, true, FIBRES},
    [GATHER_HEATHER] = {"heather", N_("gather_zone.resource.heather"), ITEM_HEATHER,
                        ONE(ITEM_HEATHER), GATHER_JOB_CUT_GRASS, true, FIBRES},
    [GATHER_REEDS] = {"reeds", N_("gather_zone.resource.reeds"), ITEM_REEDS, ONE(ITEM_REEDS),
                      GATHER_JOB_CUT_GRASS, true, FIBRES},
    [GATHER_NETTLE_STEMS] = {"nettle_stems", N_("gather_zone.resource.nettle_stems"),
                             ITEM_NETTLE_STEMS, ONE(ITEM_NETTLE_STEMS), GATHER_JOB_CUT_STEMS, true,
                             MATERIALS},
    [GATHER_CATTAIL_LEAVES] = {"cattail_leaves", N_("gather_zone.resource.cattail_leaves"),
                               ITEM_CATTAIL_LEAVES, ONE(ITEM_CATTAIL_LEAVES), GATHER_JOB_CUT_STEMS,
                               true, MATERIALS},
    [GATHER_BIRCH_BARK] = {"birch_bark", N_("gather_zone.resource.birch_bark"), ITEM_BIRCH_BARK,
                           ONE(ITEM_BIRCH_BARK), GATHER_JOB_PEEL, false, MATERIALS},
    [GATHER_HAZEL_RODS] = {"hazel_rods", N_("gather_zone.resource.hazel_rods"), ITEM_HAZEL_RODS,
                           ONE(ITEM_HAZEL_RODS), GATHER_JOB_CUT_RODS, true, MATERIALS},
};

#undef WOOD
#undef FOOD
#undef FIBRES
#undef MATERIALS

#define MEMBER(r) GATHER_RESOURCE_BIT(r)

static const GatherGroupDef GROUP_DEFS[GATHER_GROUP_COUNT] = {
    [GATHER_GROUP_WOOD] = {"wood", N_("gather_zone.group.wood"), MEMBER(GATHER_WOOD), false},
    [GATHER_GROUP_FOOD] = {"food", N_("gather_zone.group.food"),
                           MEMBER(GATHER_BLACKBERRIES) | MEMBER(GATHER_MUSHROOMS) |
                               MEMBER(GATHER_WILD_GARLIC) | MEMBER(GATHER_NETTLES) |
                               MEMBER(GATHER_PLANTAIN) | MEMBER(GATHER_WILD_CARROTS) |
                               MEMBER(GATHER_CATTAIL_RHIZOMES) | MEMBER(GATHER_WOOD_SORREL) |
                               MEMBER(GATHER_WILD_STRAWBERRIES) | MEMBER(GATHER_BILBERRIES) |
                               MEMBER(GATHER_ROSE_HIPS) | MEMBER(GATHER_HAZELNUTS) |
                               MEMBER(GATHER_CHESTNUTS) | MEMBER(GATHER_APPLES) |
                               MEMBER(GATHER_PEARS),
                           true},
    [GATHER_GROUP_FIBRES] = {"fibres", N_("gather_zone.group.fibres"),
                             MEMBER(GATHER_GRASS) | MEMBER(GATHER_MOSS) | MEMBER(GATHER_BRACKEN) |
                                 MEMBER(GATHER_HEATHER) | MEMBER(GATHER_REEDS),
                             false},
    [GATHER_GROUP_MATERIALS] = {"materials", N_("gather_zone.group.materials"),
                                MEMBER(GATHER_NETTLE_STEMS) | MEMBER(GATHER_CATTAIL_LEAVES) |
                                    MEMBER(GATHER_BIRCH_BARK) | MEMBER(GATHER_HAZEL_RODS),
                                false},
};

#undef MEMBER

#undef ONE

#define GATHERING WORK_GATHERING, KNOWLEDGE_GATHERING, KNOWLEDGE_MASTERY_LEVEL
#define CAREFUL WORK_GATHERING, KNOWLEDGE_GATHERING, GATHER_CAREFUL_MIN_LEVEL
#define RESOURCE(r) GATHER_RESOURCE_BIT(r)

static const GatherJobDef JOB_DEFS[GATHER_JOB_COUNT] = {
    [GATHER_JOB_HARVEST] = {"harvest", N_("colonist.task.harvest"), GATHERING,
                            RESOURCE(GATHER_BLACKBERRIES) | RESOURCE(GATHER_NETTLES) |
                                RESOURCE(GATHER_PLANTAIN) | RESOURCE(GATHER_WOOD_SORREL) |
                                RESOURCE(GATHER_WILD_STRAWBERRIES) | RESOURCE(GATHER_BILBERRIES) |
                                RESOURCE(GATHER_ROSE_HIPS),
                            TOOL_USE_NONE, false},
    [GATHER_JOB_HARVEST_CAREFULLY] = {"harvest_carefully", N_("colonist.task.harvest_carefully"),
                                      CAREFUL,
                                      RESOURCE(GATHER_MUSHROOMS) | RESOURCE(GATHER_WILD_GARLIC),
                                      TOOL_USE_NONE, false},
    [GATHER_JOB_DIG] = {"dig", N_("colonist.task.dig"), GATHERING,
                        RESOURCE(GATHER_CATTAIL_RHIZOMES), TOOL_USE_DIGGING, true},
    [GATHER_JOB_DIG_CAREFULLY] = {"dig_carefully", N_("colonist.task.dig_carefully"), CAREFUL,
                                  RESOURCE(GATHER_WILD_CARROTS), TOOL_USE_DIGGING, true},
    [GATHER_JOB_CUT_GRASS] = {"cut_grass", N_("colonist.task.cut_grass"), GATHERING,
                              RESOURCE(GATHER_GRASS) | RESOURCE(GATHER_MOSS) |
                                  RESOURCE(GATHER_BRACKEN) | RESOURCE(GATHER_HEATHER) |
                                  RESOURCE(GATHER_REEDS),
                              TOOL_USE_CUT_GRASS, false},
    [GATHER_JOB_CUT_STEMS] = {"cut_stems", N_("colonist.task.cut_stems"), GATHERING,
                              RESOURCE(GATHER_NETTLE_STEMS) | RESOURCE(GATHER_CATTAIL_LEAVES),
                              TOOL_USE_CUTTING, true},
    [GATHER_JOB_PEEL] = {"peel", N_("colonist.task.peel"), GATHERING, RESOURCE(GATHER_BIRCH_BARK),
                         TOOL_USE_PEELING, true},
    [GATHER_JOB_CUT_RODS] = {"cut_rods", N_("colonist.task.cut_rods"), GATHERING,
                             RESOURCE(GATHER_HAZEL_RODS), TOOL_USE_CUTTING_RODS, true},
    [GATHER_JOB_PICK] = {"pick", N_("colonist.task.pick"), GATHERING,
                         RESOURCE(GATHER_HAZELNUTS) | RESOURCE(GATHER_CHESTNUTS) |
                             RESOURCE(GATHER_APPLES) | RESOURCE(GATHER_PEARS),
                         TOOL_USE_NONE, false},
    [GATHER_JOB_CHOP] = {"chop", N_("colonist.task.chop"), WORK_WOODCUTTING, KNOWLEDGE_NONE,
                         KNOWLEDGE_MASTERY_LEVEL, RESOURCE(GATHER_WOOD), TOOL_USE_NONE, false},
};

#undef GATHERING
#undef CAREFUL
#undef RESOURCE

_Static_assert(GATHER_RESOURCE_COUNT <= 32 && GATHER_JOB_COUNT < 255, "masks and job layer");

const GatherResourceDef *gather_resource_def(GatherResource resource)
{
    return (unsigned)resource < GATHER_RESOURCE_COUNT ? &RESOURCE_DEFS[resource] : NULL;
}

ItemTypeMask gather_resource_counted(const GatherResourceDef *def)
{
    return def != NULL ? item_types_list(def->counted, GATHER_COUNTED_MAX) : item_types_none();
}

const GatherJobDef *gather_job_def(GatherJob job)
{
    return (unsigned)job < GATHER_JOB_COUNT ? &JOB_DEFS[job] : NULL;
}

const GatherGroupDef *gather_group_def(GatherGroup group)
{
    return (unsigned)group < GATHER_GROUP_COUNT ? &GROUP_DEFS[group] : NULL;
}

GatherGroup gather_resource_group(GatherResource resource)
{
    return (unsigned)resource < GATHER_RESOURCE_COUNT ? RESOURCE_DEFS[resource].group
                                                      : GATHER_GROUP_NONE;
}

GatherResourceMask gather_ripe_options(void)
{
    GatherResourceMask options = 0;
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        if (RESOURCE_DEFS[r].ripe_option) {
            options |= GATHER_RESOURCE_BIT(r);
        }
    }
    return options;
}

bool gather_resource_is_food(GatherResource resource)
{
    GatherGroup group = gather_resource_group(resource);
    return group != GATHER_GROUP_NONE && GROUP_DEFS[group].food;
}

bool gather_job_doable_at(GatherJob job, double level)
{
    const GatherJobDef *def = gather_job_def(job);
    return def != NULL && def->knowledge != KNOWLEDGE_NONE && level >= def->min_level;
}

/* ---- Tiles ------------------------------------------------------------------ */

bool gather_zones_init(GatherZones *zones, int width, int height, int depth)
{
    memset(zones, 0, sizeof(*zones));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    size_t count = (size_t)width * (size_t)height * (size_t)depth;
    zones->box = tile_box_empty();
    zones->zone = calloc(count, sizeof(*zones->zone));
    zones->job = calloc(count, sizeof(*zones->job));
    zones->progress = calloc(count, sizeof(*zones->progress));
    if (zones->zone == NULL || zones->job == NULL || zones->progress == NULL) {
        gather_zones_free(zones);
        return false;
    }
    zones->width = width;
    zones->height = height;
    zones->depth = depth;
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

static bool in_bounds(const GatherZones *zones, int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < zones->width && y < zones->height && z < zones->depth;
}

static size_t index_of(const GatherZones *zones, int x, int y, int z)
{
    return ((size_t)z * (size_t)zones->height + (size_t)y) * (size_t)zones->width + (size_t)x;
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

bool gather_zone_can_cover(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    Terrain terrain = (Terrain)map->terrain[index];
    /* The foot of a trunk (ground under the wood) is felled from beside it. */
    RockType rock = (RockType)map->rock[index];
    return (rock == ROCK_NONE || rock == ROCK_WOOD) &&
           water_class_of(map_water_depth_at(map, index)) < WATER_DEEP && terrain != TERRAIN_OPEN &&
           !structure_blocks((StructureType)map->structure[index]);
}

bool gather_stone_ground(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
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
                  int z, const uint8_t *const *blocked, bool view)
{
    if (!is_valid(zones, index)) {
        return 0;
    }
    int min_x, min_y, max_x, max_y;
    clip_rect(zones, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    int added = 0;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int level = view ? map_view_level(map, x, y, z) : z;
            if (level < 0) {
                continue;
            }
            size_t i = index_of(zones, x, y, level);
            if (zones->zone[i] == 0 && !layers_any_set(blocked, i) &&
                gather_zone_can_cover(map, x, y, level)) {
                zones->zone[i] = (uint8_t)(index + 1);
                tile_box_add(&zones->box, x, y, level);
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
                       int y1, int z)
{
    return extend(zones, map, index, x0, y0, x1, y1, z, NULL, false);
}

/* Every plant a zone may uproot by default (no tree, never uprooted). */
static uint64_t uprootable_plants(void)
{
    uint64_t mask = 0;
    for (int type = 1; type < PLANT_COUNT; type++) {
        if (!plant_is_tree((PlantType)type)) {
            mask |= GATHER_PLANT_BIT(type);
        }
    }
    return mask;
}

static int create(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1, int z,
                  const uint8_t *const *blocked, bool view, bool uproot)
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
    *zone = (GatherZone){
        .used = true, .number = zones->next_number, .priority = GATHER_ZONE_PRIORITY_DEFAULT};
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        /* Wood: young trees give little. The food resources go on forever: the
         * group's days of food are what stops them. */
        zone->orders[r] = (GatherOrder){
            .ripe_only = r == GATHER_WOOD,
            .mode = RESOURCE_DEFS[r].group == GATHER_GROUP_FOOD ? GATHER_FOREVER : GATHER_UNTIL,
            .unit = GATHER_UNIT_ITEMS,
            .target = GATHER_TARGET_DEFAULT};
    }
    for (int g = 0; g < GATHER_GROUP_COUNT; g++) {
        zone->groups[g] = (GatherGroupOrder){
            .mode = GATHER_FOREVER, .unit = GATHER_UNIT_ITEMS, .target = GATHER_TARGET_DEFAULT};
    }
    zone->groups[GATHER_GROUP_FOOD] = (GatherGroupOrder){
        .mode = GATHER_UNTIL, .unit = GATHER_UNIT_FOOD_DAYS, .target = GATHER_FOOD_DAYS_DEFAULT};
    if (uproot) {
        zone->uproot = true;
        zone->uproot_plants = uprootable_plants();
    }
    if (extend(zones, map, index, x0, y0, x1, y1, z, blocked, view) == 0) {
        zone->used = false;
        return GATHER_ZONE_NONE;
    }
    zones->used_count++;
    zones->next_number++;
    return index;
}

int gather_zone_create(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1, int z)
{
    return create(zones, map, x0, y0, x1, y1, z, NULL, false, false);
}

static int add_rect(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1, int z,
                    int preferred, const uint8_t *const *blocked, bool view, bool uproot)
{
    int min_x, min_y, max_x, max_y;
    clip_rect(zones, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    bool overlapped[GATHER_ZONE_MAX] = {false};
    /* Only zones of the kind drawn merge: the others hold their tiles. */
    int into = is_valid(zones, preferred) && zones->list[preferred].uproot == uproot
                   ? preferred
                   : GATHER_ZONE_NONE;
    preferred = into;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int level = view ? map_view_level(map, x, y, z) : z;
            if (level < 0) {
                continue;
            }
            int zone = zones->zone[index_of(zones, x, y, level)] - 1;
            if (zone == GATHER_ZONE_NONE || overlapped[zone] ||
                zones->list[zone].uproot != uproot) {
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
        return create(zones, map, x0, y0, x1, y1, z, blocked, view, uproot);
    }
    bool merged = false;
    size_t count = (size_t)zones->width * (size_t)zones->height * (size_t)zones->depth;
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
    extend(zones, map, into, x0, y0, x1, y1, z, blocked, view);
    return into;
}

int gather_zones_add_rect(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1, int z,
                          int preferred, const uint8_t *const *blocked)
{
    return add_rect(zones, map, x0, y0, x1, y1, z, preferred, blocked, false, false);
}

int gather_zones_add_view_rect(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1,
                               int z, int preferred, const uint8_t *const *blocked)
{
    return add_rect(zones, map, x0, y0, x1, y1, z, preferred, blocked, true, false);
}

int gather_zones_add_uproot_view_rect(GatherZones *zones, const Map *map, int x0, int y0, int x1,
                                      int y1, int z, int preferred, const uint8_t *const *blocked)
{
    return add_rect(zones, map, x0, y0, x1, y1, z, preferred, blocked, true, true);
}

bool gather_zone_is_uproot(const GatherZones *zones, int index)
{
    return is_valid(zones, index) && zones->list[index].uproot;
}

bool gather_zone_uproots(const GatherZone *zone, PlantType plant)
{
    return zone != NULL && zone->uproot && (unsigned)plant < PLANT_COUNT &&
           (zone->uproot_plants & GATHER_PLANT_BIT(plant)) != 0;
}

void gather_zone_set_uproot_plant(GatherZones *zones, int index, PlantType plant, bool uproots)
{
    if (!gather_zone_is_uproot(zones, index) || (unsigned)plant >= PLANT_COUNT ||
        plant == PLANT_NONE || plant_is_tree(plant)) {
        return;
    }
    GatherZone *zone = &zones->list[index];
    uint64_t was = zone->uproot_plants;
    zone->uproot_plants = uproots ? was | GATHER_PLANT_BIT(plant) : was & ~GATHER_PLANT_BIT(plant);
    if (zone->uproot_plants != was) {
        zones->revision++;
    }
}

void gather_zone_set_all_uproot_plants(GatherZones *zones, int index, bool uproots)
{
    if (!gather_zone_is_uproot(zones, index)) {
        return;
    }
    GatherZone *zone = &zones->list[index];
    uint64_t was = zone->uproot_plants;
    zone->uproot_plants = uproots ? uprootable_plants() : 0;
    if (zone->uproot_plants != was) {
        zones->revision++;
    }
}

bool gather_zones_want_uprooted(const GatherZones *zones, const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z) || x >= zones->width || y >= zones->height ||
        z >= zones->depth) {
        return false;
    }
    int index = zones->zone[((size_t)z * (size_t)zones->height + (size_t)y) * (size_t)zones->width +
                            (size_t)x] -
                1;
    if (!is_valid(zones, index)) {
        return false;
    }
    const GatherZone *zone = &zones->list[index];
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    return zone->uproot && !zone->forbidden && plant->type != PLANT_NONE &&
           !plant_is_tree(plant->type) && gather_zone_uproots(zone, (PlantType)plant->type);
}

bool gather_zones_any_uprooting(const GatherZones *zones)
{
    for (int index = 0; index < GATHER_ZONE_MAX; index++) {
        const GatherZone *zone = &zones->list[index];
        if (zone->used && zone->uproot && !zone->forbidden && zone->uproot_plants != 0) {
            return true;
        }
    }
    return false;
}

/* Takes the tiles of a rectangle out of their zone, or out of zone `only`
 * alone (GATHER_ZONE_NONE: any). */
static int remove_rect(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1, int z,
                       int only)
{
    int min_x, min_y, max_x, max_y;
    clip_rect(zones, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    int removed = 0;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int level = map != NULL ? map_view_level(map, x, y, z) : z;
            if (level < 0) {
                continue;
            }
            size_t i = index_of(zones, x, y, level);
            if (zones->zone[i] != 0 && (only == GATHER_ZONE_NONE || zones->zone[i] == only + 1)) {
                GatherZone *zone = &zones->list[zones->zone[i] - 1];
                if (--zone->cell_count == 0) {
                    zone->used = false;
                    if (--zones->used_count == 0) {
                        zones->box = tile_box_empty();
                    }
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

int gather_zones_remove_rect(GatherZones *zones, int x0, int y0, int x1, int y1, int z)
{
    return remove_rect(zones, NULL, x0, y0, x1, y1, z, GATHER_ZONE_NONE);
}

int gather_zones_remove_view_rect(GatherZones *zones, const Map *map, int x0, int y0, int x1,
                                  int y1, int z)
{
    return remove_rect(zones, map, x0, y0, x1, y1, z, GATHER_ZONE_NONE);
}

int gather_zone_extend_view(GatherZones *zones, const Map *map, int index, int x0, int y0, int x1,
                            int y1, int z, const uint8_t *const *blocked)
{
    return extend(zones, map, index, x0, y0, x1, y1, z, blocked, true);
}

int gather_zone_shrink_view(GatherZones *zones, const Map *map, int index, int x0, int y0, int x1,
                            int y1, int z)
{
    return is_valid(zones, index) ? remove_rect(zones, map, x0, y0, x1, y1, z, index) : 0;
}

void gather_zones_count_view_box(const GatherZones *zones, const Map *map, int x0, int y0, int x1,
                                 int y1, int z0, int z1, const uint8_t *const *blocked,
                                 long *free_tiles, long held[GATHER_ZONE_MAX])
{
    *free_tiles = 0;
    memset(held, 0, sizeof(long) * GATHER_ZONE_MAX);
    int min_x, min_y, max_x, max_y;
    clip_rect(zones, x0, y0, x1, y1, &min_x, &min_y, &max_x, &max_y);
    int low = clampi(z0 < z1 ? z0 : z1, 0, zones->depth - 1);
    int high = clampi(z0 < z1 ? z1 : z0, 0, zones->depth - 1);
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            int last = -1; /* The level seen from z + 1 is z + 1 or the one seen from z. */
            for (int z = low; z <= high; z++) {
                int level = map_view_level(map, x, y, z);
                if (level < 0 || level == last) {
                    continue;
                }
                last = level;
                size_t i = index_of(zones, x, y, level);
                int zone = zones->zone[i];
                if (zone != 0) {
                    held[zone - 1]++;
                } else if (!layers_any_set(blocked, i) && gather_zone_can_cover(map, x, y, level)) {
                    (*free_tiles)++;
                }
            }
        }
    }
}

void gather_zone_delete(GatherZones *zones, int index)
{
    if (!is_valid(zones, index)) {
        return;
    }
    size_t count = (size_t)zones->width * (size_t)zones->height * (size_t)zones->depth;
    for (size_t i = 0; i < count; i++) {
        if (zones->zone[i] == index + 1) {
            zones->zone[i] = 0;
            clear_progress(zones, i);
        }
    }
    zones->list[index] = (GatherZone){.used = false};
    if (--zones->used_count == 0) {
        zones->box = tile_box_empty();
    }
    changed(zones);
}

const GatherZone *gather_zone_get(const GatherZones *zones, int index)
{
    return is_valid(zones, index) ? &zones->list[index] : NULL;
}

int gather_zone_at(const GatherZones *zones, int x, int y, int z)
{
    if (!in_bounds(zones, x, y, z)) {
        return GATHER_ZONE_NONE;
    }
    return (int)zones->zone[index_of(zones, x, y, z)] - 1;
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

/* What the target of an order counts: days of food only while it gathers until. */
static GatherUnit target_unit(uint8_t mode, uint8_t unit)
{
    return mode == GATHER_UNTIL ? (GatherUnit)unit : GATHER_UNIT_ITEMS;
}

static uint16_t unit_default_target(GatherUnit unit)
{
    return unit == GATHER_UNIT_FOOD_DAYS ? GATHER_FOOD_DAYS_DEFAULT : GATHER_TARGET_DEFAULT;
}

/* Sets the mode of an order (a resource's or a group's); when its target
 * comes to count something else, it is set to that unit's default. */
static bool set_mode(uint8_t *order_mode, uint8_t unit, uint16_t *target, uint16_t *done,
                     GatherMode mode)
{
    if ((unsigned)mode >= GATHER_MODE_COUNT || *order_mode == mode) {
        return false;
    }
    GatherUnit before = target_unit(*order_mode, unit);
    GatherUnit after = target_unit((uint8_t)mode, unit);
    if (before != after) {
        *target = unit_default_target(after);
    }
    *order_mode = (uint8_t)mode;
    *done = 0;
    return true;
}

void gather_zone_set_mode(GatherZones *zones, int index, GatherResource resource, GatherMode mode)
{
    GatherOrder *order = get_order(zones, index, resource);
    if (order != NULL && set_mode(&order->mode, order->unit, &order->target, &order->done, mode)) {
        changed(zones);
    }
}

/* Whether `unit` may count what a resource (or group) is: days of food for food only. */
static bool unit_allowed(GatherUnit unit, bool food)
{
    return unit == GATHER_UNIT_ITEMS || (unit == GATHER_UNIT_FOOD_DAYS && food);
}

void gather_zone_set_unit(GatherZones *zones, int index, GatherResource resource, GatherUnit unit)
{
    GatherOrder *order = get_order(zones, index, resource);
    if (order != NULL && unit_allowed(unit, gather_resource_is_food(resource)) &&
        order->unit != unit) {
        order->unit = (uint8_t)unit;
        order->target = unit_default_target(unit);
        changed(zones);
    }
}

void gather_zone_set_target(GatherZones *zones, int index, GatherResource resource, int target)
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

static GatherGroupOrder *get_group_order(GatherZones *zones, int index, GatherGroup group)
{
    if (!is_valid(zones, index) || (unsigned)group >= GATHER_GROUP_COUNT) {
        return NULL;
    }
    return &zones->list[index].groups[group];
}

void gather_zone_set_group_enabled(GatherZones *zones, int index, GatherGroup group, bool on)
{
    GatherGroupOrder *group_order = get_group_order(zones, index, group);
    if (group_order == NULL) {
        return;
    }
    if (on) {
        group_order->done = 0;
    }
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        if (GROUP_DEFS[group].members & GATHER_RESOURCE_BIT(r)) {
            gather_zone_set_enabled(zones, index, (GatherResource)r, on);
        }
    }
    changed(zones);
}

void gather_zone_set_group_mode(GatherZones *zones, int index, GatherGroup group, GatherMode mode)
{
    GatherGroupOrder *order = get_group_order(zones, index, group);
    if (order != NULL && set_mode(&order->mode, order->unit, &order->target, &order->done, mode)) {
        changed(zones);
    }
}

void gather_zone_set_group_unit(GatherZones *zones, int index, GatherGroup group, GatherUnit unit)
{
    GatherGroupOrder *order = get_group_order(zones, index, group);
    if (order != NULL && unit_allowed(unit, GROUP_DEFS[group].food) && order->unit != unit) {
        order->unit = (uint8_t)unit;
        order->target = unit_default_target(unit);
        changed(zones);
    }
}

void gather_zone_set_group_target(GatherZones *zones, int index, GatherGroup group, int target)
{
    GatherGroupOrder *order = get_group_order(zones, index, group);
    if (order == NULL) {
        return;
    }
    uint16_t value = (uint16_t)clampi(target, GATHER_TARGET_MIN, GATHER_TARGET_MAX);
    if (order->target != value) {
        order->target = value;
        changed(zones);
    }
}

void gather_zone_apply_group_order(GatherZones *zones, int index, GatherGroup group)
{
    GatherGroupOrder *group_order = get_group_order(zones, index, group);
    if (group_order == NULL) {
        return;
    }
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        if (!(GROUP_DEFS[group].members & GATHER_RESOURCE_BIT(r))) {
            continue;
        }
        GatherOrder *order = &zones->list[index].orders[r];
        GatherUnit unit = (GatherUnit)group_order->unit;
        order->mode = group_order->mode;
        order->unit = (uint8_t)(unit_allowed(unit, gather_resource_is_food((GatherResource)r))
                                    ? unit
                                    : GATHER_UNIT_ITEMS);
        order->target = group_order->target;
        order->done = 0;
    }
    changed(zones);
}

/* Sets the full harvest option of the resources among `members`. */
static void set_ripe_only_of(GatherZones *zones, int index, GatherResourceMask members,
                             bool ripe_only)
{
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        if (members & GATHER_RESOURCE_BIT(r)) {
            gather_zone_set_ripe_only(zones, index, (GatherResource)r, ripe_only);
        }
    }
}

void gather_zone_set_group_ripe_only(GatherZones *zones, int index, GatherGroup group,
                                     bool ripe_only)
{
    if ((unsigned)group < GATHER_GROUP_COUNT) {
        set_ripe_only_of(zones, index, GROUP_DEFS[group].members, ripe_only);
    }
}

void gather_zone_set_all_ripe_only(GatherZones *zones, int index, bool ripe_only)
{
    set_ripe_only_of(zones, index, ~(GatherResourceMask)0, ripe_only);
}

GatherResourceMask gather_zone_enabled_mask(const GatherZone *zone)
{
    GatherResourceMask mask = 0;
    for (int r = 0; zone != NULL && r < GATHER_RESOURCE_COUNT; r++) {
        if (zone->orders[r].enabled) {
            mask |= GATHER_RESOURCE_BIT(r);
        }
    }
    return mask;
}

GatherResourceMask gather_zone_ripe_mask(const GatherZone *zone)
{
    GatherResourceMask mask = 0;
    for (int r = 0; zone != NULL && r < GATHER_RESOURCE_COUNT; r++) {
        if (zone->orders[r].ripe_only && RESOURCE_DEFS[r].ripe_option) {
            mask |= GATHER_RESOURCE_BIT(r);
        }
    }
    return mask;
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

void gather_zone_set_climb(GatherZones *zones, int index, bool climb)
{
    if (is_valid(zones, index) && zones->list[index].climb != climb) {
        zones->list[index].climb = climb;
        changed(zones);
    }
}

bool gather_zones_climb(const GatherZones *zones)
{
    for (int i = 0; i < GATHER_ZONE_MAX; i++) {
        const GatherZone *zone = &zones->list[i];
        if (zone->used && zone->climb && (zone->wanted & JOB_DEFS[GATHER_JOB_PICK].resources)) {
            return true;
        }
    }
    return false;
}

GatherResourceMask gather_zones_wanted(const GatherZones *zones)
{
    GatherResourceMask wanted = 0;
    for (int i = 0; i < GATHER_ZONE_MAX; i++) {
        wanted |= zones->list[i].used ? zones->list[i].wanted : 0;
    }
    return wanted;
}

void gather_zone_set_forbidden(GatherZones *zones, int index, bool forbidden)
{
    if (is_valid(zones, index) && zones->list[index].forbidden != forbidden) {
        GatherZone *zone = &zones->list[index];
        zone->forbidden = forbidden;
        if (forbidden) {
            zone->wanted = 0; /* At once; allowed again, gather_zones_update() says. */
        }
        changed(zones);
    }
}

int gather_zone_priority_at(const GatherZones *zones, int x, int y, int z)
{
    const GatherZone *zone = gather_zone_get(zones, gather_zone_at(zones, x, y, z));
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
        if (item_types_has(gather_resource_counted(def), type)) {
            count += totals[type];
        }
    }
    return count;
}

long gather_group_count(GatherGroup group, const long totals[ITEM_TYPE_COUNT])
{
    long count = 0;
    for (int r = 0; (unsigned)group < GATHER_GROUP_COUNT && r < GATHER_RESOURCE_COUNT; r++) {
        if (GROUP_DEFS[group].members & GATHER_RESOURCE_BIT(r)) {
            count += gather_resource_count((GatherResource)r, totals);
        }
    }
    return count;
}

float gather_daily_food(int settlers)
{
    return settlers > 0 ? (float)settlers * NEED_FOOD_DECAY_PER_TICK * (float)TICKS_PER_DAY : 0.0f;
}

long gather_food_days(ItemTypeMask counted, const long totals[ITEM_TYPE_COUNT], float daily_food)
{
    if (daily_food <= 0.0f) {
        return 0;
    }
    double nutrition = 0.0;
    for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
        if ((item_types_has(counted, type)) && item_is_food((ItemType)type)) {
            nutrition += (double)totals[type] * (double)item_def((ItemType)type)->nutrition;
        }
    }
    /* A little slack so that exact multiples are not lost to rounding. */
    return (long)(nutrition / (double)daily_food + 1e-6);
}

long gather_resource_food_days(GatherResource resource, const long totals[ITEM_TYPE_COUNT],
                               float daily_food)
{
    const GatherResourceDef *def = gather_resource_def(resource);
    return def != NULL ? gather_food_days(gather_resource_counted(def), totals, daily_food) : 0;
}

long gather_group_food_days(GatherGroup group, const long totals[ITEM_TYPE_COUNT], float daily_food)
{
    if ((unsigned)group >= GATHER_GROUP_COUNT || !GROUP_DEFS[group].food) {
        return 0;
    }
    ItemTypeMask all_food = item_types_none();
    for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
        if (item_is_food((ItemType)type)) {
            item_types_add(&all_food, type);
        }
    }
    return gather_food_days(all_food, totals, daily_food);
}

/* Whether an order of `mode` wants more, given what it counts: items done,
 * and what lies in the colony (`count`, in its unit). */
static bool order_wants(uint8_t mode, uint16_t target, uint16_t done, long count)
{
    switch ((GatherMode)mode) {
    case GATHER_DO_COUNT: return done < target;
    case GATHER_UNTIL: return count < (long)target;
    default: return true;
    }
}

/* Whether a group's order lets its resources be gathered. */
static bool group_wants(const GatherGroupOrder *order, GatherGroup group,
                        const long totals[ITEM_TYPE_COUNT], float daily_food)
{
    long count = 0;
    if (order->mode == GATHER_UNTIL) {
        count = order->unit == GATHER_UNIT_FOOD_DAYS
                    ? gather_group_food_days(group, totals, daily_food)
                    : gather_group_count(group, totals);
    }
    return order_wants(order->mode, order->target, order->done, count);
}

GatherResourceMask gather_zone_wanted(const GatherZone *zone, const long totals[ITEM_TYPE_COUNT],
                                      float daily_food)
{
    if (zone == NULL || zone->forbidden) {
        return 0;
    }
    bool groups_want[GATHER_GROUP_COUNT + 1];
    for (int g = 0; g < GATHER_GROUP_COUNT; g++) {
        groups_want[g] = group_wants(&zone->groups[g], (GatherGroup)g, totals, daily_food);
    }
    groups_want[GATHER_GROUP_NONE] = true;
    GatherResourceMask wanted = 0;
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        const GatherOrder *order = &zone->orders[r];
        if (!order->enabled || !groups_want[RESOURCE_DEFS[r].group]) {
            continue;
        }
        long count = 0;
        if (order->mode == GATHER_UNTIL) {
            count = order->unit == GATHER_UNIT_FOOD_DAYS
                        ? gather_resource_food_days((GatherResource)r, totals, daily_food)
                        : gather_resource_count((GatherResource)r, totals);
        }
        if (order_wants(order->mode, order->target, order->done, count)) {
            wanted |= GATHER_RESOURCE_BIT(r);
        }
    }
    return wanted;
}

void gather_zones_update(GatherZones *zones, const long totals[ITEM_TYPE_COUNT], float daily_food)
{
    bool any_change = false;
    for (int i = 0; i < GATHER_ZONE_MAX; i++) {
        GatherZone *zone = &zones->list[i];
        if (!zone->used) {
            continue;
        }
        GatherResourceMask wanted = gather_zone_wanted(zone, totals, daily_food);
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

/* Whether a stock of a plant is full (every fruit ripe, every armful grown). */
static bool stock_full(const Plant *plant, int slot)
{
    int capacity = plant_stock_capacity(plant, slot);
    return capacity > 0 && plant_stock_available(plant, slot) >= capacity;
}

/* The resource an item is gathered as (GATHER_RESOURCE_COUNT: none). Asked for
 * every tile of a zone, so answered from a table built from RESOURCE_DEFS once. */
static GatherResource resource_of_item(ItemType item)
{
    static GatherResource table[ITEM_TYPE_COUNT];
    static bool ready;
    if (!ready) {
        for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
            table[type] = GATHER_RESOURCE_COUNT;
        }
        for (int r = GATHER_RESOURCE_COUNT - 1; r >= 0; r--) { /* The first resource wins. */
            table[RESOURCE_DEFS[r].item] = (GatherResource)r;
        }
        ready = true;
    }
    return (unsigned)item < ITEM_TYPE_COUNT ? table[item] : GATHER_RESOURCE_COUNT;
}

/* Whether a job takes what a plant bears (its products: fruit, roots, grass)
 * rather than what lies on the ground or the plant itself. */
static bool takes_product(GatherJob job)
{
    return job == GATHER_JOB_HARVEST || job == GATHER_JOB_HARVEST_CAREFULLY ||
           job == GATHER_JOB_DIG || job == GATHER_JOB_DIG_CAREFULLY ||
           job == GATHER_JOB_CUT_GRASS || job == GATHER_JOB_CUT_STEMS || job == GATHER_JOB_PEEL ||
           job == GATHER_JOB_CUT_RODS;
}

/* The slot of the product of a plant that a job takes (-1: none). */
static int product_slot_for(const Plant *plant, GatherJob job)
{
    for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
        GatherResource resource = resource_of_item(plant_stock_item((PlantType)plant->type, slot));
        if (resource != GATHER_RESOURCE_COUNT && RESOURCE_DEFS[resource].job == job) {
            return slot;
        }
    }
    return -1;
}

/* Whether a plant is fully ripe for a job that takes what it bears: a full stock, and a plant
 * that is taken whole (grass cut, roots dug up) fully grown. */
static bool ripe_for_product(const Plant *plant, GatherJob job)
{
    int slot = product_slot_for(plant, job);
    return slot >= 0 && stock_full(plant, slot) &&
           (plant_product_harvest((PlantType)plant->type, slot) != PLANT_HARVEST_RESETS ||
            plant->growth >= 1.0f);
}

/* Whether a plant is ready for a resource's order: always, unless it waits for
 * the full harvest (a full stock; a plant that is taken whole, grass cut or
 * roots dug up, and trees, fully grown). */
static bool ripe_enough(const GatherZone *zone, GatherResource resource, const Plant *plant)
{
    if (!zone->orders[resource].ripe_only || !RESOURCE_DEFS[resource].ripe_option) {
        return true;
    }
    GatherJob job = RESOURCE_DEFS[resource].job;
    if (takes_product(job)) {
        return ripe_for_product(plant, job);
    }
    return plant->growth >= 1.0f;
}

/* The resources the products of a plant type are (a mask), from a table built
 * once: asked for every tile a settler's search visits. */
static GatherResourceMask plant_resources(PlantType type)
{
    static GatherResourceMask table[PLANT_COUNT];
    static bool ready;
    if (!ready) {
        for (int p = 0; p < PLANT_COUNT; p++) {
            for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
                GatherResource resource = resource_of_item(plant_stock_item((PlantType)p, slot));
                if (resource != GATHER_RESOURCE_COUNT) {
                    table[p] |= GATHER_RESOURCE_BIT(resource);
                }
            }
        }
        ready = true;
    }
    return (unsigned)type < PLANT_COUNT ? table[type] : 0;
}

/* Whether a plant bears something now, of one of `resources`, ready for the
 * zone's order. */
static bool product_among(const GatherZone *zone, const Plant *plant, GatherResourceMask resources)
{
    if ((plant_resources((PlantType)plant->type) & resources) == 0) {
        return false;
    }
    const PlantDef *def = plant_def((PlantType)plant->type);
    for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
        const PlantProduct *product = &def->products[slot];
        /* Not even one item in the stock: refused without the rest (most tiles of
         * a zone have nothing to take, and are asked for again and again). */
        if (product->item == ITEM_NONE || plant->stock[slot] < product->regrow_hours) {
            continue;
        }
        GatherResource resource = resource_of_item(product->item);
        if (resource != GATHER_RESOURCE_COUNT && (resources & GATHER_RESOURCE_BIT(resource)) &&
            plant_stock_available(plant, slot) > 0 && ripe_enough(zone, resource, plant)) {
            return true;
        }
    }
    return false;
}

/* tile_holds() on a tile a zone can cover (checked by the caller). */
static bool covered_tile_holds(const GatherZone *zone, const Map *map, int x, int y, int z,
                               GatherJob job, GatherResourceMask resources)
{
    if (resources == 0) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    const Plant *plant = &map->plants[index];
    bool plant_forbidden = map->forbidden_count > 0 && (map->forbidden[index] & MAP_FORBID_PLANT);
    if (plant_forbidden) {
        return false;
    }
    if (takes_product(job)) {
        return product_among(zone, plant, resources);
    }
    switch (job) {
    case GATHER_JOB_CHOP:
        return plant_is_tree(plant->type) && ripe_enough(zone, GATHER_WOOD, plant);
    default: return false;
    }
}

/* Whether the tile holds something for the job, among `resources`. */
static bool tile_holds(const GatherZone *zone, const Map *map, int x, int y, int z, GatherJob job,
                       GatherResourceMask resources)
{
    if (resources == 0 || !gather_zone_can_cover(map, x, y, z)) {
        return false;
    }
    return covered_tile_holds(zone, map, x, y, z, job, resources);
}

/* Whether the tile holds something for the job that the zone wants. */
static bool job_open(const GatherZone *zone, const Map *map, int x, int y, int z, GatherJob job)
{
    return tile_holds(zone, map, x, y, z, job, zone->wanted & JOB_DEFS[job].resources);
}

/* Items a plant job would give on a tile now (0 for others). */
static int plant_yield(const Map *map, int x, int y, int z, GatherJob job)
{
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    if (takes_product(job)) {
        int slot = product_slot_for(plant, job);
        return slot >= 0 ? plant_stock_available(plant, slot) : 0;
    }
    switch (job) {
    case GATHER_JOB_CHOP: return tree_wood_yield(plant);
    default: return 0;
    }
}

/* ---- Picking in the crowns -------------------------------------------------- */

static bool is_foliage_tile(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    return map->rock[index] == ROCK_NONE && terrain_is_foliage((Terrain)map->terrain[index]);
}

int gather_crown_zone(const GatherZones *zones, const Map *map, int x, int y, int z)
{
    if (!in_bounds(zones, x, y, z) || !is_foliage_tile(map, x, y, z)) {
        return GATHER_ZONE_NONE;
    }
    int zone = zones->zone[index_of(zones, x, y, z)] - 1;
    if (zone != GATHER_ZONE_NONE) {
        return zone;
    }
    int ground = tree_landing_level(map, x, y, z);
    return ground >= 0 ? zones->zone[index_of(zones, x, y, ground)] - 1 : GATHER_ZONE_NONE;
}

bool gather_pick_reaches(const GatherZones *zones, const Map *map, int sx, int sy, int sz, int tx,
                         int ty, int tz)
{
    if (!map_in_bounds(map, sx, sy, sz) || !is_foliage_tile(map, tx, ty, tz)) {
        return false;
    }
    if (!terrain_is_foliage((Terrain)map->terrain[map_index(map, sx, sy, sz)])) {
        return tx == sx && ty == sy && tz == sz + 1; /* From the ground. */
    }
    const GatherZone *zone = gather_zone_get(zones, gather_crown_zone(zones, map, tx, ty, tz));
    if (zone == NULL || !zone->climb) {
        return false;
    }
    int dx = abs(tx - sx), dy = abs(ty - sy);
    return (dx <= 1 && dy <= 1 && tz == sz) || (dx == 0 && dy == 0 && tz == sz + 1);
}

/* Whether a block of foliage can be picked from the ground: a floor right
 * under it that is not foliage. */
static bool pickable_from_ground(const Map *map, int x, int y, int z)
{
    return z > 0 && !is_foliage_tile(map, x, y, z - 1) && map_is_passable(map, x, y, z - 1);
}

/* The items the picked resources among `resources` are. */
static ItemTypeMask picked_items(GatherResourceMask resources)
{
    ItemTypeMask items = item_types_none();
    resources &= JOB_DEFS[GATHER_JOB_PICK].resources;
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        if (resources & GATHER_RESOURCE_BIT(r)) {
            item_types_add(&items, RESOURCE_DEFS[r].item);
        }
    }
    return items;
}

/* Whether the tree whose crown holds (x, y, z) is forbidden: not picked. */
static bool crown_forbidden(const Map *map, int x, int y, int z)
{
    TreeRef tree;
    return map->forbidden_count > 0 && tree_at(map, x, y, z, &tree) &&
           map_forbidden(map, tree.x, tree.y, tree.z, MAP_FORBID_PLANT);
}

/* Fruit of `resources` hanging in a block of foliage. */
static int crown_fruit(const Map *map, int x, int y, int z, GatherResourceMask resources)
{
    ItemTypeMask items = picked_items(resources);
    return map->crowns != NULL && item_types_any(items) ? crowns_count(map->crowns, x, y, z, items)
                                                        : 0;
}

/* Whether a block of foliage holds fruit its crown zone wants picked. */
static bool pick_open(const GatherZone *zone, const Map *map, int x, int y, int z)
{
    return crown_fruit(map, x, y, z, zone->wanted) > 0 && !crown_forbidden(map, x, y, z);
}

bool gather_job_available(const GatherZones *zones, const Map *map, int x, int y, int z,
                          GatherJob job)
{
    if (job == GATHER_JOB_PICK) {
        const GatherZone *zone = gather_zone_get(zones, gather_crown_zone(zones, map, x, y, z));
        return zone != NULL && pick_open(zone, map, x, y, z);
    }
    const GatherZone *zone = gather_zone_get(zones, gather_zone_at(zones, x, y, z));
    return zone != NULL && (unsigned)job < GATHER_JOB_COUNT && job_open(zone, map, x, y, z, job);
}

/* First job among `allowed` that a covered tile (x, y, z) of `zone` holds;
 * none unless the zone has `priority` (0: any). */
static GatherJob first_job(const GatherZone *zone, const Map *map, int x, int y, int z,
                           GatherJobMask allowed, int priority)
{
    if (zone->wanted == 0 || (priority != 0 && zone->priority != priority)) {
        return GATHER_JOB_NONE;
    }
    for (int job = 0; job < GATHER_JOB_COUNT; job++) {
        GatherResourceMask wanted = zone->wanted & JOB_DEFS[job].resources;
        if ((allowed & GATHER_JOB_BIT(job)) && wanted != 0 &&
            covered_tile_holds(zone, map, x, y, z, (GatherJob)job, wanted)) {
            return (GatherJob)job;
        }
    }
    return GATHER_JOB_NONE;
}

GatherJob gather_tile_job_in(const GatherZones *zones, const Map *map, int x, int y, int z,
                             GatherJobMask allowed, int priority)
{
    if (!map_in_bounds(map, x, y, z)) {
        return GATHER_JOB_NONE;
    }
    if ((allowed & GATHER_JOB_BIT(GATHER_JOB_PICK)) && map->crowns != NULL &&
        map->crowns->count > 0 && is_foliage_tile(map, x, y, z)) {
        const GatherZone *crown = gather_zone_get(zones, gather_crown_zone(zones, map, x, y, z));
        if (crown != NULL && (priority == 0 || crown->priority == priority) &&
            pick_open(crown, map, x, y, z)) {
            return GATHER_JOB_PICK;
        }
    }
    const GatherZone *zone = gather_zone_get(zones, gather_zone_at(zones, x, y, z));
    if (zone == NULL || !gather_zone_can_cover(map, x, y, z)) {
        return GATHER_JOB_NONE;
    }
    return first_job(zone, map, x, y, z, allowed, priority);
}

GatherJob gather_tile_job(const GatherZones *zones, const Map *map, int x, int y, int z,
                          GatherJobMask allowed)
{
    return gather_tile_job_in(zones, map, x, y, z, allowed, 0);
}

void gather_zone_available(const GatherZones *zones, const Map *map, int index,
                           long available[GATHER_RESOURCE_COUNT])
{
    memset(available, 0, sizeof(long) * GATHER_RESOURCE_COUNT);
    if (!is_valid(zones, index)) {
        return;
    }
    const GatherZone *zone = &zones->list[index];
    for (int z = 0; z < zones->depth; z++) {
        for (int y = 0; y < zones->height; y++) {
            for (int x = 0; x < zones->width; x++) {
                size_t tile = index_of(zones, x, y, z);
                if (zones->zone[tile] != index + 1 || !gather_zone_can_cover(map, x, y, z)) {
                    continue;
                }
                for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
                    GatherJob job = RESOURCE_DEFS[r].job;
                    GatherResourceMask bit = GATHER_RESOURCE_BIT(r);
                    if (!covered_tile_holds(zone, map, x, y, z, job, bit)) {
                        continue;
                    }
                    available[r] += plant_yield(map, x, y, z, job);
                }
            }
        }
    }
    /* What hangs in the crowns over the zone, where its gatherers reach it. */
    const Crowns *crowns = map->crowns;
    for (uint32_t slot = 0; crowns != NULL && slot < crowns->pool_used; slot++) {
        const CrownItem *item = &crowns->pool[slot];
        GatherResource resource = resource_of_item((ItemType)item->type);
        if (item->type == ITEM_NONE || resource == GATHER_RESOURCE_COUNT ||
            RESOURCE_DEFS[resource].job != GATHER_JOB_PICK) {
            continue;
        }
        int x, y, z;
        map_tile_of(map, item->tile, &x, &y, &z);
        if (gather_crown_zone(zones, map, x, y, z) == index &&
            (zone->climb || pickable_from_ground(map, x, y, z)) && !crown_forbidden(map, x, y, z)) {
            available[resource]++;
        }
    }
}

float gather_job_work_needed(const Map *map, int x, int y, int z, GatherJob job)
{
    if (!map_in_bounds(map, x, y, z)) {
        return 0.0f;
    }
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    int items = takes_product(job) ? plant_yield(map, x, y, z, job) : 0;
    switch (job) {
    case GATHER_JOB_HARVEST:
    case GATHER_JOB_HARVEST_CAREFULLY: return HARVEST_WORK * (float)(items > 1 ? items : 1);
    case GATHER_JOB_DIG:
    case GATHER_JOB_DIG_CAREFULLY: return DIG_WORK * (float)(items > 1 ? items : 1);
    case GATHER_JOB_CUT_GRASS: return CUT_GRASS_ARMFUL_WORK * (float)(items > 1 ? items : 1);
    case GATHER_JOB_CUT_STEMS: return CUT_STEMS_WORK * (float)(items > 1 ? items : 1);
    case GATHER_JOB_PEEL: {
        int slot = product_slot_for(plant, job);
        float harder =
            slot >= 0 && plant_product_out_of_season(plant, slot) == PLANT_OFF_SEASON_HARDER
                ? PLANT_OUT_OF_SEASON_WORK
                : 1.0f;
        return PEEL_WORK * harder * (float)(items > 1 ? items : 1);
    }
    case GATHER_JOB_CUT_RODS: return CUT_RODS_WORK * (float)(items > 1 ? items : 1);
    case GATHER_JOB_PICK: {
        int fruit = crown_fruit(map, x, y, z, JOB_DEFS[GATHER_JOB_PICK].resources);
        return HARVEST_WORK * (float)(fruit > 1 ? fruit : 1);
    }
    case GATHER_JOB_CHOP: return tree_fell_work(plant);
    default: return 0.0f;
    }
}

/* Carries out a completed job on the map; returns what it gives (count 0:
 * nothing), and the factor on the gatherer's quality the plant's state sets
 * (plant_product_quality_factor()). */
static ItemType complete(Map *map, int x, int y, int z, GatherJob job, int *count,
                         float *quality_factor, int *quality_malus)
{
    Plant *plant = &map->plants[map_index(map, x, y, z)];
    ItemType item = ITEM_NONE;
    *count = plant_yield(map, x, y, z, job);
    *quality_factor = 1.0f;
    *quality_malus = 0;
    if (takes_product(job)) {
        int slot = product_slot_for(plant, job);
        if (slot >= 0) {
            item = plant_stock_item((PlantType)plant->type, slot);
            *quality_factor = plant_product_quality_factor(plant, slot); /* Before it is cut. */
            PlantOffSeason off_season = plant_product_out_of_season(plant, slot);
            *count = plant_take_stock(plant, slot, INT16_MAX);
            if (off_season == PLANT_OFF_SEASON_HARDER) { /* Half of it, and worse. */
                *count = (*count + 1) / 2;
                *quality_malus = PLANT_OUT_OF_SEASON_QUALITY;
            } else if (off_season == PLANT_OFF_SEASON_GREEN) {
                *quality_malus = PLANT_GREEN_QUALITY;
            }
            plant_product_harvested(map, x, y, z, slot);
        }
        return item;
    }
    return item;
}

/* Drops what was gathered on a tile, or beside it when it is the foot of a
 * trunk (bark peeled off): on the first passable tile around, in a fixed order. */
static void drop_gathered(Items *items, const Map *map, int x, int y, int z, ItemStack stack)
{
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    for (int i = 0; !map_is_passable(map, x, y, z) && i < 4; i++) {
        if (map_is_passable(map, x + DX[i], y + DY[i], z)) {
            x += DX[i];
            y += DY[i];
        }
    }
    items_drop(items, map, x, y, z, stack);
}

/* A gatherer's product, with what the plant's state and the season make of
 * its quality (complete()). */
static ItemStack gathered_stack(ItemType item, int count, double level, Rng *rng,
                                float quality_factor, int quality_malus)
{
    ItemStack stack = knowledge_product(item, count, level, rng);
    if ((quality_factor < 1.0f || quality_malus > 0) && item_def(item)->has_quality) {
        long quality = lroundf((float)stack.quality * quality_factor) - quality_malus;
        stack.quality = (uint8_t)(quality > ITEM_QUALITY_WORST ? quality : ITEM_QUALITY_WORST);
    }
    return stack;
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

/* Counts `count` items of a resource gathered in a zone, for its order and its
 * group's. */
static void count_gathered(GatherZones *zones, GatherZone *zone, GatherResource resource, int count)
{
    GatherOrder *order = &zone->orders[resource];
    order->done = (uint16_t)clampi(order->done + count, 0, UINT16_MAX);
    if (order->mode == GATHER_DO_COUNT && order->done >= order->target) {
        zone->wanted &= ~GATHER_RESOURCE_BIT(resource);
        changed(zones);
    }
    GatherGroup group = RESOURCE_DEFS[resource].group;
    if (group != GATHER_GROUP_NONE) {
        GatherGroupOrder *group_order = &zone->groups[group];
        group_order->done = (uint16_t)clampi(group_order->done + count, 0, UINT16_MAX);
        if (group_order->mode == GATHER_DO_COUNT && group_order->done >= group_order->target) {
            zone->wanted &= ~GROUP_DEFS[group].members;
            changed(zones);
        }
    }
}

/* Picks what the zone wants in a block of foliage: it falls to the ground
 * below (tossed down), as it is, and counts for the orders. */
static void pick_crown(GatherZones *zones, GatherZone *zone, Map *map, Items *items, int x, int y,
                       int z)
{
    ItemTypeMask types = picked_items(zone->wanted);
    int counts[GATHER_RESOURCE_COUNT] = {0};
    CrownItem taken[32];
    int count;
    while ((count = crowns_take(map->crowns, x, y, z, types, taken, 32)) > 0) {
        for (int i = 0; i < count; i++) {
            GatherResource resource = resource_of_item((ItemType)taken[i].type);
            if (resource != GATHER_RESOURCE_COUNT) {
                counts[resource]++;
            }
            if (items != NULL) {
                crown_drop(map, items, x, y, z, taken[i]);
            }
        }
    }
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        if (counts[r] > 0) {
            count_gathered(zones, zone, (GatherResource)r, counts[r]);
        }
    }
}

bool gather_zone_add_work(GatherZones *zones, Map *map, Items *items, int x, int y, int z,
                          GatherJob job, float work, double level, Rng *rng)
{
    int zone_index = job == GATHER_JOB_PICK ? gather_crown_zone(zones, map, x, y, z)
                                            : gather_zone_at(zones, x, y, z);
    if (!gather_job_available(zones, map, x, y, z, job)) {
        return false;
    }
    GatherZone *zone = &zones->list[zone_index];
    size_t index = index_of(zones, x, y, z);
    if (zones->job[index] != job) {
        zones->job[index] = (uint8_t)job;
        zones->progress[index] = 0.0f;
    }
    zones->progress[index] += work;
    if (zones->progress[index] < gather_job_work_needed(map, x, y, z, job)) {
        return false;
    }
    clear_progress(zones, index);
    if (job == GATHER_JOB_PICK) {
        pick_crown(zones, zone, map, items, x, y, z);
        return true;
    }
    int count;
    ItemType item;
    float quality_factor = 1.0f;
    int quality_malus = 0;
    if (job == GATHER_JOB_CHOP) { /* The tree stays until it falls, below. */
        item = plant_def((PlantType)map->plants[map_index(map, x, y, z)].type)->cut_item;
        count = plant_yield(map, x, y, z, job);
    } else {
        item = complete(map, x, y, z, job, &count, &quality_factor, &quality_malus);
    }
    GatherResource resource = resource_of(job, item);
    bool wanted = resource != GATHER_RESOURCE_COUNT && count > 0 &&
                  (zone->wanted & GATHER_RESOURCE_BIT(resource));
    ItemStack stack = {0};
    if (wanted && items != NULL) {
        stack = gathered_stack(item, count, level, rng, quality_factor, quality_malus);
    }
    if (job == GATHER_JOB_CHOP) {
        if (tree_fell(map, items, x, y, z, stack).harmed > 0) {
            zones->harmful_tree_falls++;
        }
    } else if (stack.count > 0) {
        drop_gathered(items, map, x, y, z, stack);
    }
    if (wanted) {
        count_gathered(zones, zone, resource, count);
    }
    return true;
}

float gather_zone_progress_fraction(const GatherZones *zones, const Map *map, int x, int y, int z)
{
    if (!in_bounds(zones, x, y, z)) {
        return 0.0f;
    }
    size_t index = index_of(zones, x, y, z);
    float needed = gather_job_work_needed(map, x, y, z, (GatherJob)zones->job[index]);
    if (needed <= 0.0f) {
        return 0.0f;
    }
    return clampf(zones->progress[index] / needed, 0.0f, 1.0f);
}

float gather_clear_work(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return 0.0f;
    }
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    if (plant_is_dormant(plant)) {
        return 0.0f; /* Nothing in the way: what is left in the ground goes with the work. */
    }
    switch ((PlantType)plant->type) {
    case PLANT_NONE:
    case PLANT_OAK:
    case PLANT_BEECH:
    case PLANT_CHESTNUT:
    case PLANT_ASH:
    case PLANT_BIRCH:
    case PLANT_WILD_APPLE:
    case PLANT_WILD_PEAR:
    case PLANT_HAZEL:
    case PLANT_COUNT: return 0.0f;
    case PLANT_BRAMBLE:
    case PLANT_DOG_ROSE:
    case PLANT_BILBERRY: return CLEAR_SHRUB_WORK;
    case PLANT_GRASS:
    case PLANT_MOSS:
    case PLANT_WOOD_SORREL:
    case PLANT_BRACKEN:
    case PLANT_WILD_STRAWBERRY:
    case PLANT_HEATHER:
    case PLANT_REED:
    case PLANT_WILD_GARLIC:
    case PLANT_NETTLE:
    case PLANT_PLANTAIN:
    case PLANT_WILD_CARROT:
    case PLANT_CATTAIL:
    case PLANT_MUSHROOMS: return CUT_GRASS_WORK;
    }
    return 0.0f;
}

ToolUse gather_clear_tool_use(const Map *map, int x, int y, int z)
{
    const GatherJobDef *job = gather_job_def(gather_plant_harvest_job(map, x, y, z));
    return job != NULL ? job->tool_use : TOOL_USE_NONE;
}

void gather_clear_plant(Map *map, Items *items, int x, int y, int z, double level, Rng *rng)
{
    if (!map_in_bounds(map, x, y, z)) {
        return;
    }
    Plant *plant = &map->plants[map_index(map, x, y, z)];
    /* Everything the plant bears (what lies on the tile stays). */
    for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
        ItemType item = plant_stock_item((PlantType)plant->type, slot);
        int count = plant_take_stock(plant, slot, INT16_MAX);
        if (item != ITEM_NONE && count > 0 && items != NULL) {
            items_drop(items, map, x, y, z, knowledge_product(item, count, level, rng));
        }
    }
    map_remove_plant(map, x, y, z);
}

GatherJob gather_plant_harvest_job(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return GATHER_JOB_NONE;
    }
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    if (plant->type == PLANT_NONE || plant_is_tree(plant->type)) {
        return GATHER_JOB_NONE;
    }
    for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
        GatherResource resource = resource_of_item(plant_stock_item((PlantType)plant->type, slot));
        if (resource != GATHER_RESOURCE_COUNT && takes_product(RESOURCE_DEFS[resource].job)) {
            return RESOURCE_DEFS[resource].job;
        }
    }
    return GATHER_JOB_NONE;
}

bool gather_plant_harvestable(const Map *map, int x, int y, int z)
{
    GatherJob job = gather_plant_harvest_job(map, x, y, z);
    return job != GATHER_JOB_NONE && plant_yield(map, x, y, z, job) > 0;
}

bool gather_plant_ripe(const Map *map, int x, int y, int z)
{
    GatherJob job = gather_plant_harvest_job(map, x, y, z);
    return job != GATHER_JOB_NONE && ripe_for_product(&map->plants[map_index(map, x, y, z)], job);
}

bool gather_harvest_plant(Map *map, Items *items, int x, int y, int z, double level, Rng *rng)
{
    if (!gather_plant_harvestable(map, x, y, z)) {
        return false;
    }
    int count, quality_malus;
    float quality_factor;
    ItemType item = complete(map, x, y, z, gather_plant_harvest_job(map, x, y, z), &count,
                             &quality_factor, &quality_malus);
    if (item != ITEM_NONE && count > 0 && items != NULL) {
        drop_gathered(items, map, x, y, z,
                      gathered_stack(item, count, level, rng, quality_factor, quality_malus));
    }
    return true;
}

bool gather_plant_uprootable(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    uint8_t type = map->plants[map_index(map, x, y, z)].type;
    return type != PLANT_NONE && !plant_is_tree(type);
}

float gather_uproot_work(const Map *map, int x, int y, int z)
{
    if (!gather_plant_uprootable(map, x, y, z)) {
        return 0.0f;
    }
    float work = gather_clear_work(map, x, y, z);
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    GatherJob job = gather_plant_harvest_job(map, x, y, z);
    if (plant_has_food(plant)) {
        work += gather_job_work_needed(map, x, y, z, job); /* Its food first. */
    }
    return work > HARVEST_WORK ? work : HARVEST_WORK; /* Even what is left in the ground. */
}

uint64_t gather_zones_hash(const GatherZones *zones, uint64_t hash)
{
    size_t count = (size_t)zones->width * (size_t)zones->height * (size_t)zones->depth;
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
        if (zone->forbidden) { /* Hashes of games that forbid nothing stay as they were. */
            hash = hash_fnv1a64(hash, &zone->forbidden, sizeof(zone->forbidden));
        }
        if (zone->climb) { /* The same for climbing. */
            hash = hash_fnv1a64(hash, &zone->climb, sizeof(zone->climb));
        }
        if (zone->uproot) { /* And for uprooting zones. */
            hash = hash_fnv1a64(hash, &zone->uproot_plants, sizeof(zone->uproot_plants));
        }
        for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
            const GatherOrder *order = &zone->orders[r];
            int32_t values[6] = {order->enabled, order->ripe_only, order->mode,
                                 order->unit,    order->target,    order->done};
            hash = hash_fnv1a64(hash, values, sizeof(values));
        }
        for (int g = 0; g < GATHER_GROUP_COUNT; g++) {
            const GatherGroupOrder *group = &zone->groups[g];
            int32_t values[4] = {group->mode, group->unit, group->target, group->done};
            hash = hash_fnv1a64(hash, values, sizeof(values));
        }
    }
    return hash;
}

void gather_zones_serialize(Serializer *s, GatherZones *zones)
{
    size_t count = (size_t)zones->width * (size_t)zones->height * (size_t)zones->depth;
    ser_layer_u8_below(s, zones->zone, count, GATHER_ZONE_MAX + 1);
    tile_box_serialize(s, &zones->box);
    ser_layer_u8_below(s, zones->job, count, GATHER_JOB_COUNT + 1);
    ser_layer_float(s, zones->progress, count);
    for (int i = 0; i < GATHER_ZONE_MAX; i++) {
        GatherZone *zone = &zones->list[i];
        ser_bool(s, &zone->used);
        ser_int(s, &zone->number);
        ser_int(s, &zone->cell_count);
        ser_int(s, &zone->priority);
        ser_bool(s, &zone->forbidden);
        for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
            GatherOrder *order = &zone->orders[r];
            ser_bool(s, &order->enabled);
            ser_bool(s, &order->ripe_only);
            ser_u8_below(s, &order->mode, GATHER_MODE_COUNT);
            ser_u8_below(s, &order->unit, GATHER_UNIT_COUNT);
            ser_u16(s, &order->target);
            ser_u16(s, &order->done);
        }
        for (int g = 0; g < GATHER_GROUP_COUNT; g++) {
            GatherGroupOrder *order = &zone->groups[g];
            ser_u8_below(s, &order->mode, GATHER_MODE_COUNT);
            ser_u8_below(s, &order->unit, GATHER_UNIT_COUNT);
            ser_u16(s, &order->target);
            ser_u16(s, &order->done);
        }
        ser_u32(s, &zone->wanted);
        ser_bool(s, &zone->climb);
        ser_bool(s, &zone->uproot);
        ser_u64(s, &zone->uproot_plants);
    }
    ser_int_in(s, &zones->used_count, 0, GATHER_ZONE_MAX);
    ser_int(s, &zones->next_number);
    ser_int(s, &zones->wanting_cells);
    ser_u32(s, &zones->revision);
    ser_u32(s, &zones->harmful_tree_falls);
}
