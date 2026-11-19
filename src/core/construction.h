/*
 * Construction sites: blueprints of structures (core/structure.h) and of
 * workstations (core/workstation.h) placed by the player and waiting to be
 * built.
 *
 * Builders and haulers bring the materials, one kind at a time; a builder
 * then does the work, always from a neighboring tile (as for walls, which
 * cannot be stood on once built). A roof is planned on the open tile over a
 * hut's walls, the level above: it is brought to and built from beside the
 * tile under it, overhead (see construction_worked_from()). When the work is
 * done the structure appears on the map (map_set_structure(),
 * map_set_roof()), or the workstation is placed, and the site is gone.
 * A blocking structure (wall) pushes the items lying on its tile to a
 * neighboring open tile.
 *
 * The structure built has a quality from the builder's level, capped by the
 * quality of the materials (structure_quality_cap()).
 *
 * A refresh site sits on a built structure that wears by itself (a grass bed
 * packing down): the game places one once the structure falls below
 * STRUCTURE_REFRESH_BELOW of its hit points; its material and work come from
 * StructureDef.refresh_*, and finishing it brings the structure back to full
 * hit points. Structures worn down to 0 fall apart.
 *
 * Several settlers may bring materials at once: each one first promises the
 * items it goes for (construction_promise()), so that nobody brings what
 * another is already bringing, and releases the promise once they are
 * delivered or given up.
 *
 * A site takes every tile of its structure (a bed: its head and its foot);
 * its position is the structure's head. Sites are kept in a fixed list with
 * a dense layer of indices the size of the map. Removing a site gives its
 * delivered materials back.
 */
#ifndef COLONY_CORE_CONSTRUCTION_H
#define COLONY_CORE_CONSTRUCTION_H

#include "core/item.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/structure.h"
#include "core/workstation.h"

#include <stdbool.h>
#include <stdint.h>

#define CONSTRUCTION_MAX 512
#define CONSTRUCTION_NONE (-1)
/* Returned by game_place_structure() for a structure built at once: no site. */
#define CONSTRUCTION_BUILT_AT_ONCE (-2)

typedef struct ConstructionSite {
    bool used;
    uint8_t type; /* StructureType; STRUCTURE_NONE for a workstation. */
    /* WorkstationType + 1 when the site is a workstation's blueprint, else 0. */
    uint8_t workstation;
    int16_t x, y, z;   /* The structure's head. */
    uint8_t direction; /* Direction: where its other tiles lie (core/structure.h). */
    int number;        /* Unique, never reused: tells a site from a later one in the same slot. */
    /* Materials brought so far, per slot of the structure's definition (a
     * slot keeps the lowest quality delivered). */
    ItemStack delivered[STRUCTURE_MATERIAL_SLOTS];
    /* Items of each slot on their way: promised by settlers fetching them. */
    uint16_t promised[STRUCTURE_MATERIAL_SLOTS];
    float progress;       /* Work done, once every material is in. */
    float clear_progress; /* Work done clearing the plant of a tile, before building. */
    bool refresh;         /* Refreshing the built structure of the tile, not building one. */
    bool forbidden;       /* Forbidden by the player: no material brought, no work done. */
    /* Builders and haulers go to the sites of the highest priority first
     * (JOB_PRIORITY_HIGHEST..JOB_PRIORITY_LOWEST, core/work.h); a
     * workstation's site passes it on to the station. */
    uint8_t priority;
} ConstructionSite;

typedef struct Constructions {
    int width;
    int height;
    int depth;
    /* Site index + 1 on every tile of a site, 0 when none. */
    uint16_t *at;
    /* 1 on the tile of a workstation's site: zones leave it out, as they do
     * the tiles of stations (see game_add_stockpile_rect()). */
    uint8_t *workstation_tiles;
    ConstructionSite list[CONSTRUCTION_MAX];
    int used_count;
    int workstation_count; /* Sites of workstations, among them. */
    int next_number;
    /* Incremented whenever sites change or get materials: settlers who found
     * nothing to build look again. */
    uint32_t revision;
} Constructions;

bool constructions_init(Constructions *constructions, int width, int height, int depth);
void constructions_free(Constructions *constructions);

/* Index of the site on a tile, or CONSTRUCTION_NONE. */
int construction_at(const Constructions *constructions, int x, int y, int z);

/* Whether a site is a roof's. */
bool construction_is_roof(const ConstructionSite *site);

/* The site worked on from beside (x, y, z) (CONSTRUCTION_NONE): the one on
 * the tile when it is not a roof's, or (`overhead`) the roof's over it, on
 * (x, y, z + 1). */
int construction_worked_from(const Constructions *constructions, int x, int y, int z,
                             bool overhead);

/* NULL for an unused or invalid index. */
const ConstructionSite *construction_get(const Constructions *constructions, int index);
ConstructionSite *construction_get_mutable(Constructions *constructions, int index);

/* Whether a structure may be planned on a tile: passable buildable ground
 * (map_is_buildable(): not a slope), no structure or site yet, no tree
 * (grass and bushes are cleared by the builder first). */
bool construction_can_place(const Constructions *constructions, const Map *map, int x, int y,
                            int z);

/* construction_can_place() with a site on the tile not counting: what a new
 * plan may replace. */
bool construction_can_replace(const Constructions *constructions, const Map *map, int x, int y,
                              int z);

/* Whether a roof on a tile would be held up: by a wall or rock in reach on
 * the level below (map_roof_supported()), or by the site of a wall there, so
 * that a whole hut can be planned at once. */
bool construction_roof_held(const Constructions *constructions, const Map *map, int x, int y,
                            int z);

/* Whether a roof may be planned on a tile: open, with open sky over it and
 * a floor without rock under it (not a roof, nor a slope or a ladder that
 * leads up through it), no site yet, held up (construction_roof_held()).
 * Whatever stands or is planned under it (a bed, a wall) stays. */
bool construction_can_place_roof(const Constructions *constructions, const Map *map, int x, int y,
                                 int z);

/* construction_can_place() on every tile a structure with its head at
 * (x, y) would take (construction_can_place_roof() for a roof). */
bool construction_can_place_structure(const Constructions *constructions, const Map *map,
                                      StructureType type, int x, int y, int z, Direction direction);

/* construction_can_place_structure() with the sites on the tiles it would
 * take not counting: what a new plan may replace (the caller removes them). */
bool construction_can_replace_structure(const Constructions *constructions, const Map *map,
                                        StructureType type, int x, int y, int z,
                                        Direction direction);

/* Whether this very structure is already planned there (the same type, head
 * and direction): replacing it would only lose its progress. Likewise for a
 * workstation on a tile. */
bool construction_is_planned(const Constructions *constructions, StructureType type, int x, int y,
                             int z, Direction direction);
bool construction_workstation_is_planned(const Constructions *constructions, WorkstationType type,
                                         int x, int y, int z);

/* Places a site, its head at (x, y). Returns its index, or CONSTRUCTION_NONE
 * when the type is invalid, a tile does not qualify or the list is full. */
int constructions_place(Constructions *constructions, const Map *map, StructureType type, int x,
                        int y, int z, Direction direction);

/* Places the site of a workstation on a tile that passes
 * construction_can_place(). Returns its index, or CONSTRUCTION_NONE. */
int constructions_place_workstation(Constructions *constructions, const Map *map,
                                    WorkstationType type, int x, int y, int z);

/* Tile `part` of a site (0: the head, up to construction_length() - 1). */
void construction_tile(const ConstructionSite *site, int part, int *x, int *y, int *z);

/* Tiles the site takes (1 for a workstation). */
int construction_length(const ConstructionSite *site);

/* What the site builds: its workstation type, or WORKSTATION_TYPE_COUNT for
 * a structure. */
WorkstationType construction_workstation(const ConstructionSite *site);

/* Knowledge the work uses (and teaches); KNOWLEDGE_NONE when none. */
KnowledgeType construction_knowledge(const ConstructionSite *site);

/* Lowest level in that knowledge the work needs. */
float construction_min_level(const ConstructionSite *site);

/* Translation key of what the site builds. */
const char *construction_name_key(const ConstructionSite *site);

/* Places a refresh site on a built structure that can be refreshed (see
 * StructureDef.refresh_work) and has no site yet, given any of its tiles. Returns its index, or
 * CONSTRUCTION_NONE. */
int constructions_place_refresh(Constructions *constructions, const Map *map, int x, int y, int z);

/* Removes a site; its delivered materials are given back in `returned`.
 * Returns the number of stacks (the caller drops them). */
int constructions_remove(Constructions *constructions, int index,
                         ItemStack returned[STRUCTURE_MATERIAL_SLOTS]);

/* Material of a slot and how many it takes (the refresh material for a
 * refresh site); ITEM_NONE and 0 for an unused slot. */
ItemType construction_material(const ConstructionSite *site, int slot);
int construction_material_count(const ConstructionSite *site, int slot);

/* The item types a slot takes: the material and its substitutes (see
 * structure_material_types()), and only the type already delivered once some
 * is in. 0 for an unused slot. */
ItemTypeMask construction_material_types(const ConstructionSite *site, int slot);

/* Work the site needs in all (building or refreshing). */
float construction_work_needed(const ConstructionSite *site);

/* Items of a material slot still to bring (0 for an unused slot). */
int construction_missing(const ConstructionSite *site, int slot);

/* Items of a slot still to bring that nobody has promised (0 for an unused
 * slot). */
int construction_unpromised(const ConstructionSite *site, int slot);

/* Promises up to `count` items of a slot, at most what nobody has promised
 * yet. Returns how many were promised. */
int construction_promise(Constructions *constructions, int index, int slot, int count);

/* Releases a promise of `count` items of a slot, if the site at `index` is
 * still the one numbered `number` (the site may be gone). */
void construction_release_promise(Constructions *constructions, int index, int number, int slot,
                                  int count);

/* Whether every material is in. */
bool construction_materials_complete(const ConstructionSite *site);

/* Forbids or allows a site: nobody brings it materials nor works on it
 * meanwhile (jobs under way are dropped). */
void construction_set_forbidden(Constructions *constructions, int index, bool forbidden);

/* Sets a site's priority (clamped, see ConstructionSite.priority). */
void construction_set_priority(Constructions *constructions, int index, int priority);

/* Whether the site is forbidden itself or touches something the player forbade
 * (see map_forbidden()): the structure a refresh site is on, a plant to clear
 * on one of its tiles. Nobody brings materials nor works there meanwhile. */
bool construction_forbidden(const ConstructionSite *site, const Map *map);

/* Whether the work can start: every material is in, nothing is forbidden
 * (construction_forbidden()) and, for a roof, a wall or rock holds it up now
 * (map_roof_supported(); its walls may still be sites). */
bool construction_can_work(const ConstructionSite *site, const Map *map);

/* Delivers items to the slot of their type; returns how many were taken. */
int construction_deliver(Constructions *constructions, int index, ItemStack stack);

/*
 * Adds work to a site whose materials are all in. When the work is done, the
 * structure is built on the map with a quality from the builder's `level`
 * (core/knowledge.h; `rng` may be NULL for the average quality), capped by the
 * materials (items lying on a blocking structure's tile are dropped next to
 * it), or refreshed to full hit points, or the workstation is placed in
 * `workstations`; the site is removed and true is returned. A workstation
 * that cannot be placed yet (a plant grew there, the list is full) keeps
 * its site.
 */
bool construction_add_work(Constructions *constructions, Map *map, Items *items,
                           Workstations *workstations, int index, float work, double level,
                           Rng *rng);

/*
 * Finishes a site at once (the debug cheat GameDebug.instant_build): the
 * missing materials appear, of standard quality, and all the work is done at
 * `level` (see construction_add_work()). Returns false, changing nothing,
 * when it cannot be finished now: a roof without support, a workstation that
 * cannot be placed (a plant on its tile: clear it first), a refresh site
 * whose structure is gone.
 */
bool construction_finish_now(Constructions *constructions, Map *map, Items *items,
                             Workstations *workstations, int index, double level, Rng *rng);

/*
 * A day goes by: structures that wear by themselves lose their hit points
 * of the day. Those worn down to 0 fall apart (with their refresh site,
 * whose materials are dropped on the tile); those below
 * STRUCTURE_REFRESH_BELOW get a refresh site. Returns the number of
 * structures that fell apart.
 */
int constructions_wear_day(Constructions *constructions, Map *map, Items *items);

/* Fraction of the work done, 0..1. */
float construction_work_fraction(const ConstructionSite *site);

uint64_t constructions_hash(const Constructions *constructions, uint64_t hash);

#endif /* COLONY_CORE_CONSTRUCTION_H */
