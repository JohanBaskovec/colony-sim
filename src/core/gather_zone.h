/*
 * Gathering zones: areas drawn by the player where settlers take what the
 * land offers: they fell trees, pick wild food (fruit, nuts, leaves,
 * mushrooms), dig roots up, pick up fallen nuts and cut grass. What lies on
 * the ground (branches, twigs, stones) is no gathering: those are items like
 * any other, for the haulers.
 *
 * Each tile belongs to at most one zone (dense layer of zone ids, the size of
 * the map). A zone holds one order per resource, off by default. An enabled
 * order either gathers a number of items and stops, or gathers until the
 * colony has a target number of the resource (items lying anywhere on the map
 * count, stored or not, like bills: see core/workstation.h), or gathers
 * forever. Food may instead be gathered until the colony has a number of days
 * of food: the nutrition of the food lying in the colony over what all the
 * settlers eat in a day (gather_food_days()).
 *
 * Resources come in groups (wood, food, fibres), each
 * with an order of its own that limits all of its resources: a resource is
 * gathered only while its own order and its group's order both want it (the
 * group's order is never turned off; food: until 5 days of food by default).
 *
 * An order may wait for the full harvest (GatherOrder.ripe_only): what a
 * plant bears (wild food, grass) is taken only from plants whose stock is full
 * (every fruit ripe, every armful grown, not only a grown plant), trees felled
 * fully grown only, so that settlers do not walk to plants that give little.
 * Wild food is there in its season only (see PlantProduct.warmth_from); what is easily mistaken
 * for a poisonous plant is only taken by skilled gatherers
 * (GATHER_CAREFUL_MIN_LEVEL).
 *
 * A tile offers a job when it holds something an order wants (a tree to fell,
 * ripe fruit, fallen nuts...). When a tile offers several, the nuts lying
 * there come first, then the plant's food, and felling it last.
 *
 * Every tile keeps the work done on its current job, so that interrupted work
 * resumes. Gathering is a background job: at the same work priority, settlers
 * first carry out orders, build and craft. Among zones, settlers gather in
 * those of the highest zone priority first, then in the nearest. No zone lies
 * under a workstation (see game_place_workstation()).
 */
#ifndef COLONY_CORE_GATHER_ZONE_H
#define COLONY_CORE_GATHER_ZONE_H

#include "core/item.h"
#include "core/knowledge.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/tile_box.h"
#include "core/tool.h"
#include "core/tree.h"
#include "core/work.h"

#include <stdbool.h>
#include <stdint.h>

#define GATHER_ZONE_MAX 32
#define GATHER_ZONE_NONE (-1)

/* 1 is the highest priority, like stockpiles. */
#define GATHER_ZONE_PRIORITY_HIGHEST 1
#define GATHER_ZONE_PRIORITY_LOWEST 10
#define GATHER_ZONE_PRIORITY_DEFAULT 5

/* What a zone may gather, in display order. */
typedef enum GatherResource {
    GATHER_WOOD,
    GATHER_BLACKBERRIES,
    GATHER_HAZELNUTS,
    GATHER_MUSHROOMS,
    GATHER_WILD_GARLIC,
    GATHER_NETTLES,
    GATHER_PLANTAIN,
    GATHER_WILD_CARROTS,
    GATHER_CATTAIL_RHIZOMES,
    GATHER_WOOD_SORREL,
    GATHER_WILD_STRAWBERRIES,
    GATHER_BILBERRIES,
    GATHER_ROSE_HIPS,
    GATHER_GRASS,
    GATHER_MOSS,
    GATHER_BRACKEN,
    GATHER_HEATHER,
    GATHER_REEDS,
    GATHER_RESOURCE_COUNT
} GatherResource;

typedef uint32_t GatherResourceMask;
#define GATHER_RESOURCE_BIT(resource) ((GatherResourceMask)1u << (unsigned)(resource))

/* What a settler does on a tile, in order of preference on the same tile. */
typedef enum GatherJob {
    GATHER_JOB_NUTS,    /* Pick up the hazelnuts lying on a tile. */
    GATHER_JOB_HARVEST, /* Pick fruit, nuts or leaves; the plant stays. */
    /* Pick what is easily mistaken for a poisonous plant (mushrooms, wild
     * garlic): skilled gatherers only. */
    GATHER_JOB_HARVEST_CAREFULLY,
    GATHER_JOB_DIG,           /* Dig roots up (a digging stick helps). */
    GATHER_JOB_DIG_CAREFULLY, /* The same, easily mistaken for a poisonous plant. */
    GATHER_JOB_CUT_GRASS,     /* Cut grass or another fibre plant, which grows back. */
    GATHER_JOB_CHOP,          /* Fell a tree. */
    GATHER_JOB_COUNT
} GatherJob;

#define GATHER_JOB_NONE GATHER_JOB_COUNT

/* Gathering level needed to take wild food easily mistaken for a poisonous
 * plant (docs/KNOWLEDGE.md §7: the risk of a mistake below it comes with
 * health). */
#define GATHER_CAREFUL_MIN_LEVEL 60.0f
typedef uint32_t GatherJobMask;
#define GATHER_JOB_BIT(job) ((GatherJobMask)1u << (unsigned)(job))

/* Groups of resources, each with an order limiting all of them. */
typedef enum GatherGroup {
    GATHER_GROUP_WOOD,   /* Wood. */
    GATHER_GROUP_FOOD,   /* Wild food. */
    GATHER_GROUP_FIBRES, /* Grass, moss, bracken, heather, reeds: bedding, thatch, fuel. */
    GATHER_GROUP_COUNT
} GatherGroup;

#define GATHER_GROUP_NONE GATHER_GROUP_COUNT

typedef struct GatherResourceDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    ItemType item;        /* What gathering gives. */
    ItemTypeMask counted; /* Items counted as this resource in the colony (grass: fresh or dry). */
    GatherJob job;
    bool ripe_option;  /* Whether the order may wait for the full harvest. */
    GatherGroup group; /* GATHER_GROUP_NONE: on its own. */
} GatherResourceDef;

typedef struct GatherGroupDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    GatherResourceMask members; /* In display order. */
    bool food;                  /* May be gathered until a number of days of food. */
} GatherGroupDef;

typedef struct GatherJobDef {
    const char *id;
    const char *activity_key; /* What a settler doing it is busy with. */
    WorkType work_type;
    /* Knowledge needed, KNOWLEDGE_NONE when nobody can do it yet (felling
     * needs an axe and a knowledge that are not in the game yet). */
    KnowledgeType knowledge;
    float min_level;
    GatherResourceMask resources; /* The resources it gives. */
    ToolUse tool_use;             /* What a carried tool speeds up (TOOL_USE_NONE: nothing). */
    /* A better tool for it lying close by is worth fetching on the way (the
     * other jobs use whatever tool the settler carries). */
    bool fetches_tool;
} GatherJobDef;

/* NULL when out of range. */
const GatherResourceDef *gather_resource_def(GatherResource resource);
const GatherJobDef *gather_job_def(GatherJob job);
const GatherGroupDef *gather_group_def(GatherGroup group);

/* Group of a resource (GATHER_GROUP_NONE: none, or out of range). */
GatherGroup gather_resource_group(GatherResource resource);

/* Resources whose orders may wait for the full harvest. */
GatherResourceMask gather_ripe_options(void);

/* Whether a resource is food (it may be gathered until days of food). */
bool gather_resource_is_food(GatherResource resource);

/* Whether a settler at `level` in the job's knowledge can do it. */
bool gather_job_doable_at(GatherJob job, double level);

/* How long an order runs (like bills). */
typedef enum GatherMode {
    GATHER_DO_COUNT, /* Gather `target` items, then stop. */
    GATHER_UNTIL,    /* Until `target` items lie in the colony (not carried). */
    GATHER_FOREVER,  /* Again and again. */
    GATHER_MODE_COUNT
} GatherMode;

/* What GATHER_UNTIL counts. */
typedef enum GatherUnit {
    GATHER_UNIT_ITEMS,     /* Items lying in the colony. */
    GATHER_UNIT_FOOD_DAYS, /* Days of food (food only, see gather_food_days()). */
    GATHER_UNIT_COUNT
} GatherUnit;

#define GATHER_TARGET_MIN 1
#define GATHER_TARGET_MAX 9999
#define GATHER_TARGET_DEFAULT 20
#define GATHER_FOOD_DAYS_DEFAULT 5

typedef struct GatherOrder {
    bool enabled;
    bool ripe_only; /* Full harvest only (resources with ripe_option). */
    uint8_t mode;   /* GatherMode */
    uint8_t unit;   /* GatherUnit of GATHER_UNTIL. */
    uint16_t target;
    uint16_t done; /* Items gathered (counted by GATHER_DO_COUNT). */
} GatherOrder;

/* The order of a group: it is always on, and limits each of its resources. */
typedef struct GatherGroupOrder {
    uint8_t mode; /* GatherMode */
    uint8_t unit; /* GatherUnit of GATHER_UNTIL. */
    uint16_t target;
    uint16_t done; /* Items of the group's resources gathered. */
} GatherGroupOrder;

typedef struct GatherZone {
    bool used;
    int number; /* Shown to the player ("Gathering 2"); unique, never reused. */
    int cell_count;
    /* Zones of a higher priority (smaller number) are gathered in first, at
     * any distance; then the nearest job among zones of the same priority. */
    int priority;
    /* Forbidden by the player: the settlers gather nothing in it (its orders
     * stay as they are). */
    bool forbidden;
    GatherOrder orders[GATHER_RESOURCE_COUNT];
    GatherGroupOrder groups[GATHER_GROUP_COUNT];
    /* Resources wanted now (orders enabled and not satisfied, see
     * gather_zones_update()). */
    GatherResourceMask wanted;
} GatherZone;

typedef struct GatherZones {
    int width;
    int height;
    int depth;
    uint8_t *zone;   /* Zone index + 1 per tile, 0 when outside any zone. */
    TileBox box;     /* Holds every tile of a zone (core/tile_box.h). */
    uint8_t *job;    /* GatherJob the tile's progress belongs to. */
    float *progress; /* Work done on the tile's current job. */
    GatherZone list[GATHER_ZONE_MAX];
    int used_count;
    int next_number;
    /* Tiles in zones that want something (a cheap upper bound of the open jobs). */
    int wanting_cells;
    /* Incremented whenever work may have become available or gone (tiles,
     * orders, wanted resources, plants that grew): settlers who found nothing
     * to do look again. */
    uint32_t revision;
    /* Trees felled onto a structure (no side was clear, see tree_fell()):
     * the game raises a notice when it grows. Not part of the hash. */
    uint32_t harmful_tree_falls;
} GatherZones;

bool gather_zones_init(GatherZones *zones, int width, int height, int depth);
void gather_zones_free(GatherZones *zones);

/* Whether a zone may cover a tile: ground (a floor, no rock wall, no built
 * wall) or shallow water (cattails grow there), not deep water. */
bool gather_zone_can_cover(const Map *map, int x, int y, int z);

/* Whether stones lie about a tile: stony ground (soil, rich soil, gravel,
 * rough stone: TerrainDef.stony), no rock wall, no structure. How many lie
 * there: map_stones(). */
bool gather_stone_ground(const Map *map, int x, int y, int z);

/*
 * Creates a zone from the tiles of a rectangle (corners in any order) that
 * gather_zone_can_cover() accepts and that are in no other zone. Every order
 * is off (until GATHER_TARGET_DEFAULT items, wood waiting for full-grown
 * trees, the food resources forever: their group's days of food limit them);
 * the food group gathers until GATHER_FOOD_DAYS_DEFAULT days of food, the
 * other groups forever. Returns its index, or GATHER_ZONE_NONE when no tile qualifies or
 * the list is full.
 */
int gather_zone_create(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1, int z);

/* Adds the free tiles of a rectangle a zone may cover. Returns the number of
 * tiles added. */
int gather_zone_extend(GatherZones *zones, const Map *map, int index, int x0, int y0, int x1,
                       int y1, int z);

/*
 * Adds a rectangle to the gathering zones, as the zone tool does: `preferred`
 * (the selected zone, or GATHER_ZONE_NONE) and every zone the rectangle
 * overlaps become one zone, which gets the free tiles of the rectangle it may
 * cover. The merged zone keeps the orders of `preferred`, else of the oldest
 * one; work under way on its tiles is kept. With no zone involved, a new one
 * is created. Tiles where a layer of `blocked` (layers the size of the map,
 * the list ended by NULL; or NULL) is not 0 are left out: the game passes the
 * other kinds of zones, which may not overlap, and the workstations. Returns
 * the zone's index, or GATHER_ZONE_NONE when nothing could be added.
 */
int gather_zones_add_rect(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1, int z,
                          int preferred, const uint8_t *const *blocked);

/*
 * As gather_zones_add_rect(), with `z` the level shown: each tile of the
 * rectangle stands for what is seen there (map_view_level()), the level
 * below when it is open space. A zone may thus cover several levels.
 */
int gather_zones_add_view_rect(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1,
                               int z, int preferred, const uint8_t *const *blocked);

/* gather_zones_remove_rect() on what is seen (map_view_level()) with level z shown. */
int gather_zones_remove_view_rect(GatherZones *zones, const Map *map, int x0, int y0, int x1,
                                  int y1, int z);

/* Removes every tile of a rectangle from its zone; zones left without tiles
 * are deleted. Returns the number of tiles removed. */
int gather_zones_remove_rect(GatherZones *zones, int x0, int y0, int x1, int y1, int z);

/* What is seen in a rectangle (level z shown, map_view_level()) added to
 * zone `index` alone: the free tiles it may cover (no merging with the zones
 * it touches; `blocked` as in gather_zones_add_rect()), or taken out of it
 * alone (the other zones keep theirs; the zone is deleted once empty).
 * Return the number of tiles added or removed. */
int gather_zone_extend_view(GatherZones *zones, const Map *map, int index, int x0, int y0, int x1,
                            int y1, int z, const uint8_t *const *blocked);
int gather_zone_shrink_view(GatherZones *zones, const Map *map, int index, int x0, int y0, int x1,
                            int y1, int z);

void gather_zone_delete(GatherZones *zones, int index);

/* NULL when the index is out of range or the slot is unused. */
const GatherZone *gather_zone_get(const GatherZones *zones, int index);

/* Index of the zone covering a tile, or GATHER_ZONE_NONE. */
int gather_zone_at(const GatherZones *zones, int x, int y, int z);

/*
 * Order settings. Invalid zones, resources, groups, modes and units are
 * ignored, and so is the days-of-food unit for what is not food; targets are
 * clamped to GATHER_TARGET_MIN..GATHER_TARGET_MAX. Enabling an order or
 * changing its mode starts its count over. When the target comes to count
 * something else (another unit, or days of food that become items to gather
 * as the mode changes), it is set to that unit's default
 * (GATHER_TARGET_DEFAULT items, GATHER_FOOD_DAYS_DEFAULT days). Call
 * gather_zones_update() afterwards (the game does it every tick).
 */
void gather_zone_set_enabled(GatherZones *zones, int index, GatherResource resource, bool on);
void gather_zone_set_mode(GatherZones *zones, int index, GatherResource resource, GatherMode mode);
void gather_zone_set_unit(GatherZones *zones, int index, GatherResource resource, GatherUnit unit);
void gather_zone_set_target(GatherZones *zones, int index, GatherResource resource, int target);
void gather_zone_set_ripe_only(GatherZones *zones, int index, GatherResource resource,
                               bool ripe_only);

/* The same for the order of a group. Turning a group on or off does it for
 * each of its resources (turning it on starts the group's count over); its
 * full harvest option is set for each of its resources that has it. */
void gather_zone_set_group_enabled(GatherZones *zones, int index, GatherGroup group, bool on);
void gather_zone_set_group_mode(GatherZones *zones, int index, GatherGroup group, GatherMode mode);
void gather_zone_set_group_unit(GatherZones *zones, int index, GatherGroup group, GatherUnit unit);
void gather_zone_set_group_target(GatherZones *zones, int index, GatherGroup group, int target);
void gather_zone_set_group_ripe_only(GatherZones *zones, int index, GatherGroup group,
                                     bool ripe_only);

/* Full harvest only, or not, for every resource of a zone that has the option. */
void gather_zone_set_all_ripe_only(GatherZones *zones, int index, bool ripe_only);

/* Resources of a zone whose order is on, and that wait for the full harvest. */
GatherResourceMask gather_zone_enabled_mask(const GatherZone *zone);
GatherResourceMask gather_zone_ripe_mask(const GatherZone *zone);

/* Forbids or allows the zone: while forbidden it wants nothing, so that
 * nobody gathers in it. */
void gather_zone_set_forbidden(GatherZones *zones, int index, bool forbidden);

/* GATHER_ZONE_PRIORITY_HIGHEST..GATHER_ZONE_PRIORITY_LOWEST (clamped). */
void gather_zone_set_priority(GatherZones *zones, int index, int priority);

/* Priority of the zone covering a tile, 0 outside any zone. */
int gather_zone_priority_at(const GatherZones *zones, int x, int y, int z);

/* Priorities of the zones that want something: bit p set for priority p. */
uint32_t gather_zones_wanting_priorities(const GatherZones *zones);

/* Items counted as a resource among `totals` (items lying per type). */
long gather_resource_count(GatherResource resource, const long totals[ITEM_TYPE_COUNT]);

/* Items counted as the resources of a group among `totals`. */
long gather_group_count(GatherGroup group, const long totals[ITEM_TYPE_COUNT]);

/* Food need all the settlers lose in a day (what gather_food_days() divides by). */
float gather_daily_food(int settlers);

/*
 * Whole days of food: the nutrition of the items of the `counted` types among
 * `totals` (items lying in the colony; their state is ignored) over
 * `daily_food` (gather_daily_food()). 0 when nobody eats.
 */
long gather_food_days(ItemTypeMask counted, const long totals[ITEM_TYPE_COUNT], float daily_food);

/* Days of food in a resource, and in a group (the food group: every food,
 * cooked or not). */
long gather_resource_food_days(GatherResource resource, const long totals[ITEM_TYPE_COUNT],
                               float daily_food);
long gather_group_food_days(GatherGroup group, const long totals[ITEM_TYPE_COUNT],
                            float daily_food);

/* Resources a zone wants given the items lying in the colony and the food
 * the settlers eat in a day (gather_daily_food()); none while it is forbidden. */
GatherResourceMask gather_zone_wanted(const GatherZone *zone, const long totals[ITEM_TYPE_COUNT],
                                      float daily_food);

/* Recomputes what every zone wants. */
void gather_zones_update(GatherZones *zones, const long totals[ITEM_TYPE_COUNT], float daily_food);

/* Plants grew: tiles may offer new jobs. */
void gather_zones_plants_changed(GatherZones *zones);

/* Whether a tile offers a job to anyone: in a zone that wants something the
 * tile holds. Each tile follows the zone covering it: a tree's foliage is
 * gathered from only when a zone covers the foliage itself. */
bool gather_job_available(const GatherZones *zones, const Map *map, int x, int y, int z,
                          GatherJob job);

/* First job a tile offers among `allowed`, in order of preference, or
 * GATHER_JOB_NONE; gather_tile_job_in() only when the zone whose orders it
 * follows has `priority` (0: any). */
GatherJob gather_tile_job(const GatherZones *zones, const Map *map, int x, int y, int z,
                          GatherJobMask allowed);
GatherJob gather_tile_job_in(const GatherZones *zones, const Map *map, int x, int y, int z,
                             GatherJobMask allowed, int priority);
/* How many items of each resource a zone's tiles hold now: wood of the trees,
 * ripe fruit, fallen nuts, grass to cut.
 * Plants the zone's orders leave alone (not ready, when waiting for the full
 * harvest) do not count. All zeros for an invalid zone. */
void gather_zone_available(const GatherZones *zones, const Map *map, int index,
                           long available[GATHER_RESOURCE_COUNT]);

/* Total work a job needs on a tile right now (0 when the tile has nothing for it). */
float gather_job_work_needed(const Map *map, int x, int y, int z, GatherJob job);

/*
 * Adds work to a job on a tile (the work done on another job there is lost).
 * When enough is done, the job is carried out on the map (the tree falls,
 * the grass is cut, the nuts are picked up...), its products are dropped on
 * the tile with quality and condition from the worker's `level`
 * (core/knowledge.h; `rng` may be NULL for average products), the orders
 * count them, and true is returned. A felled tree falls full length (tree_fell():
 * its wood along the trunk, its branches and twigs at the crown). False when
 * the job is not available.
 */
bool gather_zone_add_work(GatherZones *zones, Map *map, Items *items, int x, int y, int z,
                          GatherJob job, float work, double level, Rng *rng);

/* Work done on the tile's current job, 0..1. */
float gather_zone_progress_fraction(const GatherZones *zones, const Map *map, int x, int y, int z);

/* Work to clear the plant of a tile by hand before building there (0 when
 * there is none, or nothing of it shows above ground; a tree cannot be
 * cleared: see map_plant_blocks_building()). */
float gather_clear_work(const Map *map, int x, int y, int z);

/* What a tool speeds up while clearing a tile: what takes the plant's product
 * (cutting grass, digging roots), nothing for a shrub pulled up by hand. */
ToolUse gather_clear_tool_use(const Map *map, int x, int y, int z);

/* Removes the plant of a tile (cut grass, a pulled up shrub) and drops what it
 * gives (everything it bears; what lies on the tile stays) with quality
 * from the worker's `level`; `rng` may be NULL. */
void gather_clear_plant(Map *map, Items *items, int x, int y, int z, double level, Rng *rng);

/*
 * Single plants, for the Harvest and Uproot orders (core/designation.h).
 * The job that takes what the plant on a tile bears without uprooting it:
 * picking its food, digging it up for roots, cutting grass (what happens to
 * the plant: PlantDef.harvest). GATHER_JOB_NONE without a plant, for a tree
 * (felled instead), or a plant that bears nothing.
 */
GatherJob gather_plant_harvest_job(const Map *map, int x, int y, int z);

/* Whether the plant on a tile has something to harvest now (ripe food, grass
 * grown enough to cut: a stock to take). */
bool gather_plant_harvestable(const Map *map, int x, int y, int z);

/* Takes what the plant gives (gather_plant_harvest_job()) and drops it on
 * the tile with quality from the worker's `level`; `rng` may be NULL. False
 * when there is nothing to harvest. */
bool gather_harvest_plant(Map *map, Items *items, int x, int y, int z, double level, Rng *rng);

/* Whether the plant on a tile can be uprooted: any plant but a tree. */
bool gather_plant_uprootable(const Map *map, int x, int y, int z);

/* Work to uproot the plant on a tile: harvesting what it bears, then pulling
 * it up (gather_clear_plant() carries it out). */
float gather_uproot_work(const Map *map, int x, int y, int z);

uint64_t gather_zones_hash(const GatherZones *zones, uint64_t hash);

#endif /* COLONY_CORE_GATHER_ZONE_H */
