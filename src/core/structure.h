/*
 * Structures: what settlers build on a tile from gathered materials (grass
 * bed, grass and branch wall, branch screen). This module only holds their
 * definitions; built structures are a layer of the map (core/map.h) and
 * blueprints waiting to be built are construction sites
 * (core/construction.h).
 *
 * A tile is about 1 m × 1 m. Material counts are worked out from real
 * structures (docs/KNOWLEDGE.md §10): a wall segment is 1 m long and 2 m
 * high, a frame of posts and rails thatched with dry grass.
 *
 * A built structure has a grade (the builder's level, capped by the grade of
 * its materials) and hit points (more for a better grade). Some wear by
 * themselves (a grass bed loses a hit point a day) and are refreshed with a
 * little material before they fall apart.
 */
#ifndef COLONY_CORE_STRUCTURE_H
#define COLONY_CORE_STRUCTURE_H

#include "core/item.h"
#include "core/knowledge.h"

#include <stdbool.h>

typedef enum StructureType {
    STRUCTURE_NONE = 0,
    STRUCTURE_GRASS_BED,
    STRUCTURE_WALL, /* Grass and branch wall. */
    STRUCTURE_DOOR, /* Branch screen: pushed aside to pass. */
    STRUCTURE_TYPE_COUNT
} StructureType;

#define STRUCTURE_MATERIAL_SLOTS 2

typedef struct StructureMaterial {
    ItemType type; /* ITEM_NONE: unused slot. */
    int count;
} StructureMaterial;

typedef struct StructureDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    const char *description_key; /* One short sentence for the build menu. */
    StructureMaterial materials[STRUCTURE_MATERIAL_SLOTS];
    float build_work; /* Work ticks at speed ×1. */
    KnowledgeType knowledge; /* Needed to build it. */
    float min_level;
    /* Movement cost multiplier of a tile holding it; 0: blocks movement. */
    float move_cost;
    /* Rest gain multiplier of a settler sleeping on it; 0: not a bed. */
    float rest_factor;
    /* Placed along the edges of a dragged rectangle (walls), not one by one. */
    bool drawn_as_outline;
    int max_hit_points; /* At standard grade (see structure_max_hit_points()). */
    int wear_per_day;   /* Hit points lost each day by itself (bedding packs down). */
    /* Refreshing (a new layer of bedding): material and work; no refresh
     * when the work is 0. Asked for below STRUCTURE_REFRESH_BELOW. */
    StructureMaterial refresh_material;
    float refresh_work;
} StructureDef;

/* Fraction of its hit points under which a structure that can be refreshed
 * gets a refresh site. */
#define STRUCTURE_REFRESH_BELOW 0.5f

/* NULL when out of range (STRUCTURE_NONE has a definition). */
const StructureDef *structure_def(StructureType type);

/* Whether a structure of this type blocks movement. */
bool structure_blocks(StructureType type);

/* Whether settlers sleep on it. */
bool structure_is_bed(StructureType type);

/* Hit points of a structure of a grade: the standard ones × the grade
 * factor (×0.6 crude to ×1.5 superb, docs/KNOWLEDGE.md §2.2); at least 1.
 * 0 for STRUCTURE_NONE. */
int structure_max_hit_points(StructureType type, ItemQuality quality);

/* Best grade a structure can have with materials of these grades: their
 * average + 1, among the materials that have a grade (grass and twigs do not
 * cap). `delivered` holds one stack per material slot. */
ItemQuality structure_quality_cap(StructureType type,
                                  const ItemStack delivered[STRUCTURE_MATERIAL_SLOTS]);

/* Work to take a built structure apart: a share of the building work. */
float structure_deconstruct_work(StructureType type);

/* Items given back when a built structure is taken apart, per material slot
 * (half the materials: lashings break, grass crumbles). */
int structure_salvage_count(StructureType type, int slot);

#endif /* COLONY_CORE_STRUCTURE_H */
