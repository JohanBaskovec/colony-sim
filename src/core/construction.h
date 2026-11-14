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
 * Sites are kept in a fixed list with a dense layer of indices the size of
 * the map. Removing a site gives its delivered materials back.
 */
#ifndef COLONY_CORE_CONSTRUCTION_H
#define COLONY_CORE_CONSTRUCTION_H

#include "core/item.h"
#include "core/map.h"
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

/* Removes a site; its delivered materials are given back in `returned`.
 * Returns the number of stacks (the caller drops them). */
int constructions_remove(Constructions *constructions, int index,
                         ItemStack returned[STRUCTURE_MATERIAL_SLOTS]);

/* Items of a material slot still to bring (0 for an unused slot). */
int construction_missing(const ConstructionSite *site, int slot);

/* Whether every material is in: the work can start. */
bool construction_materials_complete(const ConstructionSite *site);

/* Delivers items to the slot of their type; returns how many were taken. */
int construction_deliver(Constructions *constructions, int index, ItemStack stack);

/*
 * Adds work to a site whose materials are all in. When the work is done, the
 * structure is built on the map (items lying on a blocking structure's tile
 * are dropped next to it), the site is removed and true is returned.
 */
bool construction_add_work(Constructions *constructions, Map *map, Items *items, int index,
                           float work);

/* Fraction of the work done, 0..1. */
float construction_work_fraction(const ConstructionSite *site);

uint64_t constructions_hash(const Constructions *constructions, uint64_t hash);

#endif /* COLONY_CORE_CONSTRUCTION_H */
