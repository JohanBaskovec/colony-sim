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
} StructureDef;

/* NULL when out of range (STRUCTURE_NONE has a definition). */
const StructureDef *structure_def(StructureType type);

/* Whether a structure of this type blocks movement. */
bool structure_blocks(StructureType type);

/* Whether settlers sleep on it. */
bool structure_is_bed(StructureType type);

/* Work to take a built structure apart: a share of the building work. */
float structure_deconstruct_work(StructureType type);

/* Items given back when a built structure is taken apart, per material slot
 * (half the materials: lashings break, grass crumbles). */
int structure_salvage_count(StructureType type, int slot);

#endif /* COLONY_CORE_STRUCTURE_H */
