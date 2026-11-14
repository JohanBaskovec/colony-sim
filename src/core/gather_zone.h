/*
 * Gathering zones: areas drawn by the player where settlers take what the
 * land offers: they fell trees, pick berries, take branches and twigs, cut
 * grass and pick up stones (cobbles and flint nodules).
 *
 * Each tile belongs to at most one zone (dense layer of zone ids, the size of
 * the map). A zone holds one order per resource, off by default. An enabled
 * order either gathers a number of items and stops, or gathers until the
 * colony has a target number of the resource (items lying anywhere on the map
 * count, stored or not, like bills: see core/workstation.h), or gathers
 * forever. Wood and berries may be taken from fully grown plants only, so
 * that settlers do not walk to young plants that give little.
 *
 * A tile offers a job when it holds something an order wants (a tree to fell,
 * a ripe bush, a stock of twigs, stony ground...). When a tile offers several,
 * the plant's stocks come first (picking its fruit or felling it would take
 * them away), then its fruit, felling it, and stones last.
 * Stones are limited: each tile holds a number of cobbles and flint nodules
 * (Map.cobbles, Map.flint), and gathering takes them one by one, only of the
 * types wanted.
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
    GATHER_BERRIES,
    GATHER_BRANCHES,
    GATHER_TWIGS,
    GATHER_GRASS,
    GATHER_FLINT,
    GATHER_COBBLES,
    GATHER_RESOURCE_COUNT
} GatherResource;

typedef uint32_t GatherResourceMask;
#define GATHER_RESOURCE_BIT(resource) ((GatherResourceMask)1u << (unsigned)(resource))

/* What a settler does on a tile, in order of preference on the same tile. */
typedef enum GatherJob {
    GATHER_JOB_BRANCHES,  /* Take the dead branches of a tree. */
    GATHER_JOB_TWIGS,     /* Take the twigs of a tree or bush. */
    GATHER_JOB_HARVEST,   /* Pick the fruit of a plant; the plant stays. */
    GATHER_JOB_CUT_GRASS, /* Cut grass, which grows back. */
    GATHER_JOB_CHOP,      /* Fell a tree. */
    GATHER_JOB_STONES,    /* Search the ground for a stone. */
    GATHER_JOB_COUNT
} GatherJob;

#define GATHER_JOB_NONE GATHER_JOB_COUNT
typedef uint32_t GatherJobMask;
#define GATHER_JOB_BIT(job) ((GatherJobMask)1u << (unsigned)(job))

typedef struct GatherResourceDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    ItemType item;        /* What gathering gives. */
    ItemTypeMask counted; /* Items counted as this resource in the colony (grass: fresh or dry). */
    GatherJob job;
    bool ripe_option; /* Whether the order may wait for fully grown plants. */
} GatherResourceDef;

typedef struct GatherJobDef {
    const char *id;
    const char *activity_key; /* What a settler doing it is busy with. */
    WorkType work_type;
    /* Knowledge needed, KNOWLEDGE_NONE when nobody can do it yet (felling
     * needs an axe and a knowledge that are not in the game yet). */
    KnowledgeType knowledge;
    float min_level;
    GatherResourceMask resources; /* The resources it gives. */
} GatherJobDef;

/* NULL when out of range. */
const GatherResourceDef *gather_resource_def(GatherResource resource);
const GatherJobDef *gather_job_def(GatherJob job);

/* Whether a settler at `level` in the job's knowledge can do it. */
bool gather_job_doable_at(GatherJob job, double level);

/* How long an order runs (like bills). */
typedef enum GatherMode {
    GATHER_DO_COUNT, /* Gather `target` items, then stop. */
    GATHER_UNTIL,    /* Until `target` items lie in the colony (not carried). */
    GATHER_FOREVER,  /* Again and again. */
    GATHER_MODE_COUNT
} GatherMode;

#define GATHER_TARGET_MIN 1
#define GATHER_TARGET_MAX 9999
#define GATHER_TARGET_DEFAULT 20

typedef struct GatherOrder {
    bool enabled;
    bool ripe_only; /* Fully grown plants only (resources with ripe_option). */
    uint8_t mode;   /* GatherMode */
    uint16_t target;
    uint16_t done; /* Items gathered (counted by GATHER_DO_COUNT). */
} GatherOrder;

typedef struct GatherZone {
    bool used;
    int number; /* Shown to the player ("Gathering 2"); unique, never reused. */
    int cell_count;
    /* Zones of a higher priority (smaller number) are gathered in first, at
     * any distance; then the nearest job among zones of the same priority. */
    int priority;
    GatherOrder orders[GATHER_RESOURCE_COUNT];
    /* Resources wanted now (orders enabled and not satisfied, see
     * gather_zones_update()). */
    GatherResourceMask wanted;
} GatherZone;

typedef struct GatherZones {
    int width;
    int height;
    uint8_t *zone;   /* Zone index + 1 per tile, 0 when outside any zone. */
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
} GatherZones;

bool gather_zones_init(GatherZones *zones, int width, int height);
void gather_zones_free(GatherZones *zones);

/* Whether a zone may cover a tile: open ground (no rock wall, no built
 * wall), not water. */
bool gather_zone_can_cover(const Map *map, int x, int y);

/* Whether stones can be gathered on a tile: stony ground (soil, rich soil,
 * gravel, rough stone: TerrainDef.stony), no rock wall, no structure. How
 * many lie there: map_stones(). */
bool gather_stone_ground(const Map *map, int x, int y);

/*
 * Creates a zone from the tiles of a rectangle (corners in any order) that
 * gather_zone_can_cover() accepts and that are in no other zone. Every order
 * is off. Returns its index, or GATHER_ZONE_NONE when no tile qualifies or
 * the list is full.
 */
int gather_zone_create(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1);

/* Adds the free tiles of a rectangle a zone may cover. Returns the number of
 * tiles added. */
int gather_zone_extend(GatherZones *zones, const Map *map, int index, int x0, int y0, int x1,
                       int y1);

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
int gather_zones_add_rect(GatherZones *zones, const Map *map, int x0, int y0, int x1, int y1,
                          int preferred, const uint8_t *const *blocked);

/* Removes every tile of a rectangle from its zone; zones left without tiles
 * are deleted. Returns the number of tiles removed. */
int gather_zones_remove_rect(GatherZones *zones, int x0, int y0, int x1, int y1);

void gather_zone_delete(GatherZones *zones, int index);

/* NULL when the index is out of range or the slot is unused. */
const GatherZone *gather_zone_get(const GatherZones *zones, int index);

/* Index of the zone covering a tile, or GATHER_ZONE_NONE. */
int gather_zone_at(const GatherZones *zones, int x, int y);

/*
 * Order settings. Invalid zones, resources and modes are ignored; targets
 * are clamped to GATHER_TARGET_MIN..GATHER_TARGET_MAX. Enabling an order or
 * changing its mode starts its count over. Call gather_zones_update()
 * afterwards (the game does it every tick).
 */
void gather_zone_set_enabled(GatherZones *zones, int index, GatherResource resource, bool on);
void gather_zone_set_mode(GatherZones *zones, int index, GatherResource resource,
                          GatherMode mode);
void gather_zone_set_target(GatherZones *zones, int index, GatherResource resource,
                            int target);
void gather_zone_set_ripe_only(GatherZones *zones, int index, GatherResource resource,
                               bool ripe_only);

/* GATHER_ZONE_PRIORITY_HIGHEST..GATHER_ZONE_PRIORITY_LOWEST (clamped). */
void gather_zone_set_priority(GatherZones *zones, int index, int priority);

/* Priority of the zone covering a tile, 0 outside any zone. */
int gather_zone_priority_at(const GatherZones *zones, int x, int y);

/* Priorities of the zones that want something: bit p set for priority p. */
uint32_t gather_zones_wanting_priorities(const GatherZones *zones);

/* Items counted as a resource among `totals` (items lying per type). */
long gather_resource_count(GatherResource resource, const long totals[ITEM_TYPE_COUNT]);

/* Resources a zone wants given the items lying in the colony. */
GatherResourceMask gather_zone_wanted(const GatherZone *zone, const long totals[ITEM_TYPE_COUNT]);

/* Recomputes what every zone wants. */
void gather_zones_update(GatherZones *zones, const long totals[ITEM_TYPE_COUNT]);

/* Plants grew: tiles may offer new jobs. */
void gather_zones_plants_changed(GatherZones *zones);

/* Whether a tile offers a job to anyone: in a zone that wants something the
 * tile holds. */
bool gather_job_available(const GatherZones *zones, const Map *map, int x, int y, GatherJob job);

/* First job a tile offers among `allowed`, in order of preference, or
 * GATHER_JOB_NONE. */
GatherJob gather_tile_job(const GatherZones *zones, const Map *map, int x, int y,
                          GatherJobMask allowed);

/* How many items of each resource a zone's tiles hold now: wood of the trees,
 * ripe fruit, branches, twigs, grass to cut, cobbles and flint in the ground.
 * Plants the zone's orders leave alone (not fully grown, with that option)
 * do not count. All zeros for an invalid zone. */
void gather_zone_available(const GatherZones *zones, const Map *map, int index,
                           long available[GATHER_RESOURCE_COUNT]);

/* Total work a job needs on a tile right now (0 when the tile has nothing for it). */
float gather_job_work_needed(const Map *map, int x, int y, GatherJob job);

/*
 * Adds work to a job on a tile (the work done on another job there is lost).
 * When enough is done, the job is carried out on the map (the tree falls,
 * the grass is cut, a stone is taken from the ground...), its products are
 * dropped on the tile with quality and condition from the worker's `level`
 * (core/knowledge.h; `rng` may be NULL for average products, and a cobble is
 * then found before flint), the orders count them, and true is returned.
 * Stones are found among the wanted types the tile holds, at random in
 * proportion to their numbers. False when the job is not available.
 */
bool gather_zone_add_work(GatherZones *zones, Map *map, Items *items, int x, int y, GatherJob job,
                          float work, double level, Rng *rng);

/* Work done on the tile's current job, 0..1. */
float gather_zone_progress_fraction(const GatherZones *zones, const Map *map, int x, int y);

/* Work to clear the plant of a tile by hand before building there (0 when
 * there is none; a tree cannot be cleared: see map_plant_blocks_building()). */
float gather_clear_work(const Map *map, int x, int y);

/* Removes the plant of a tile (cut grass, a pulled up bush) and drops what it
 * gives (cut grass, its twigs and branches, ripe fruit) with quality from the
 * worker's `level`; `rng` may be NULL. */
void gather_clear_plant(Map *map, Items *items, int x, int y, double level, Rng *rng);

uint64_t gather_zones_hash(const GatherZones *zones, uint64_t hash);

#endif /* COLONY_CORE_GATHER_ZONE_H */
