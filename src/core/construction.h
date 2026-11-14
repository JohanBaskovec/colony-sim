/*
 * Construction sites: blueprints of structures (core/structure.h) placed by
 * the player and waiting to be built.
 *
 * A builder brings the materials, one kind at a time, then does the work,
 * always from a neighboring tile (as for walls, which cannot be stood on once
 * built). When the work is done the structure appears on the map
 * (map_set_structure()) and the site is gone. A blocking structure (wall)
 * pushes the items lying on its tile to a neighboring open tile.
 *
 * The structure built has a grade from the builder's level, capped by the
 * grade of the materials (structure_quality_cap()).
 *
 * A refresh site sits on a built structure that wears by itself (a grass bed
 * packing down): the game places one once the structure falls below
 * STRUCTURE_REFRESH_BELOW of its hit points; its material and work come from
 * StructureDef.refresh_*, and finishing it brings the structure back to full
 * hit points. Structures worn down to 0 fall apart.
 *
 * Sites are kept in a fixed list with a dense layer of indices the size of
 * the map. Removing a site gives its delivered materials back.
 */
#ifndef COLONY_CORE_CONSTRUCTION_H
#define COLONY_CORE_CONSTRUCTION_H

#include "core/item.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/structure.h"

#include <stdbool.h>
#include <stdint.h>

#define CONSTRUCTION_MAX 512
#define CONSTRUCTION_NONE (-1)

typedef struct ConstructionSite {
    bool used;
    uint8_t type; /* StructureType */
    int16_t x, y;
    int number; /* Unique, never reused: tells a site from a later one in the same slot. */
    /* Materials brought so far, per slot of the structure's definition (a
     * slot keeps the lowest grade delivered). */
    ItemStack delivered[STRUCTURE_MATERIAL_SLOTS];
    float progress; /* Work done, once every material is in. */
    float clear_progress; /* Work done clearing the plant on the tile, before building. */
    bool refresh; /* Refreshing the built structure of the tile, not building one. */
} ConstructionSite;

typedef struct Constructions {
    int width;
    int height;
    uint16_t *at; /* Site index + 1 per tile, 0 when none. */
    ConstructionSite list[CONSTRUCTION_MAX];
    int used_count;
    int next_number;
    /* Incremented whenever sites change or get materials: settlers who found
     * nothing to build look again. */
    uint32_t revision;
} Constructions;

bool constructions_init(Constructions *constructions, int width, int height);
void constructions_free(Constructions *constructions);

/* Index of the site on a tile, or CONSTRUCTION_NONE. */
int construction_at(const Constructions *constructions, int x, int y);

/* NULL for an unused or invalid index. */
const ConstructionSite *construction_get(const Constructions *constructions, int index);
ConstructionSite *construction_get_mutable(Constructions *constructions, int index);

/* Whether a structure may be planned on a tile: passable buildable ground,
 * no structure or site yet, no tree (grass and bushes are cleared by the
 * builder first). */
bool construction_can_place(const Constructions *constructions, const Map *map, int x, int y);

/* Places a site. Returns its index, or CONSTRUCTION_NONE when the type is
 * invalid, the tile does not qualify or the list is full. */
int constructions_place(Constructions *constructions, const Map *map, StructureType type, int x,
                        int y);

/* Places a refresh site on a built structure that can be refreshed (see
 * StructureDef.refresh_work) and has no site yet. Returns its index, or
 * CONSTRUCTION_NONE. */
int constructions_place_refresh(Constructions *constructions, const Map *map, int x, int y);

/* Removes a site; its delivered materials are given back in `returned`.
 * Returns the number of stacks (the caller drops them). */
int constructions_remove(Constructions *constructions, int index,
                         ItemStack returned[STRUCTURE_MATERIAL_SLOTS]);

/* Material of a slot and how many it takes (the refresh material for a
 * refresh site); ITEM_NONE and 0 for an unused slot. */
ItemType construction_material(const ConstructionSite *site, int slot);
int construction_material_count(const ConstructionSite *site, int slot);

/* Work the site needs in all (building or refreshing). */
float construction_work_needed(const ConstructionSite *site);

/* Items of a material slot still to bring (0 for an unused slot). */
int construction_missing(const ConstructionSite *site, int slot);

/* Whether every material is in: the work can start. */
bool construction_materials_complete(const ConstructionSite *site);

/* Delivers items to the slot of their type; returns how many were taken. */
int construction_deliver(Constructions *constructions, int index, ItemStack stack);

/*
 * Adds work to a site whose materials are all in. When the work is done, the
 * structure is built on the map with a grade from the builder's `level`
 * (core/knowledge.h; `rng` may be NULL for the average grade), capped by the
 * materials (items lying on a blocking structure's tile are dropped next to
 * it), or refreshed to full hit points; the site is removed and true is
 * returned.
 */
bool construction_add_work(Constructions *constructions, Map *map, Items *items, int index,
                           float work, double level, Rng *rng);

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
