/*
 * Structures: what settlers build on a tile from gathered materials (grass
 * bed, grass and branch wall, branch screen, grass roof). This module only holds their
 * definitions; built structures are a layer of the map (core/map.h) and
 * blueprints waiting to be built are construction sites
 * (core/construction.h).
 *
 * A tile is about 1 m × 1 m. Material counts are worked out from real
 * structures (docs/KNOWLEDGE.md §10): a wall segment is 1 m long and 2 m
 * high, a frame of posts and rails thatched with dry grass.
 *
 * Most structures take one tile; a grass bed takes two (2 m × 0.8 m), its
 * head and its foot, turned in one of four directions. The head is the
 * structure's origin: where its quality and hit points are kept, where a
 * settler lies down.
 *
 * A roof is built as the floor of the open tile over a hut (the level above
 * its walls, see map_roof()): it covers whatever stands under it (a bed, a
 * wall, a stockpile), nobody stands on it, and it must rest on a wall or
 * rock nearby on the level below (ROOF_SUPPORT_RANGE).
 *
 * A built structure has a quality (the builder's level, capped by the quality of
 * its materials) and hit points (more for a better quality). Some wear by
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
    STRUCTURE_WALL,           /* Grass and branch wall. */
    STRUCTURE_DOOR,           /* Branch screen: pushed aside to pass. */
    STRUCTURE_ROOF,           /* Grass roof on branch rafters, over a tile. */
    STRUCTURE_PROP,           /* Forked post of branches: holds up a ceiling or a roof. */
    STRUCTURE_LADDER,         /* Branch ladder: up to the level above (see map_can_climb()). */
    STRUCTURE_GROUND_BEDDING, /* A spot of trodden ground to sleep on, no material. */
    STRUCTURE_TYPE_COUNT
} StructureType;

#define STRUCTURE_MATERIAL_SLOTS 2
/* The longest structure, in tiles (a bed or a ladder run): see structure_length(). */
#define STRUCTURE_MAX_LENGTH 4

/* Tiles of the longest structure. */
#define STRUCTURE_LENGTH_MAX 2

/* Where a long structure goes from its head: the foot of a bed facing south
 * lies under its head. */
typedef enum Direction {
    DIRECTION_NORTH = 0,
    DIRECTION_EAST,
    DIRECTION_SOUTH,
    DIRECTION_WEST,
    DIRECTION_COUNT
} Direction;

/* One step in a direction (y grows southwards). */
int direction_dx(Direction direction);
int direction_dy(Direction direction);

/* The next direction clockwise. */
Direction direction_turn(Direction direction);

typedef struct StructureMaterial {
    ItemType type; /* ITEM_NONE: unused slot. */
    int count;
} StructureMaterial;

typedef struct StructureDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    const char *description_key; /* One short sentence for the build menu. */
    StructureMaterial materials[STRUCTURE_MATERIAL_SLOTS];
    float build_work;        /* Work ticks at speed ×1. */
    KnowledgeType knowledge; /* Needed to build it. */
    float min_level;
    /* Movement cost multiplier of a tile holding it; 0: blocks movement. */
    float move_cost;
    /* Rest gain multiplier of a settler sleeping on it; 0: not a bed. */
    float rest_factor;
    /* Placed along the edges of a dragged rectangle (walls), not one by one. */
    bool drawn_as_outline;
    int length;         /* Tiles, from the head along its direction (a bed: 2). */
    int max_hit_points; /* At standard quality (see structure_max_hit_points()). */
    int wear_per_day;   /* Hit points lost each day by itself (bedding packs down). */
    /* Refreshing (a new layer of bedding): material and work; no refresh
     * when the work is 0. Asked for below STRUCTURE_REFRESH_BELOW. */
    StructureMaterial refresh_material;
    float refresh_work;
    bool roof; /* Built as a floor, over the walls (see map_roof()). */
} StructureDef;

/* A roof tile needs a wall (a structure that blocks movement) or rock at
 * most this many tiles away, in any direction (branch rafters span about
 * 2.5 m): a hut of up to 4 × 4 tiles inside its walls can be roofed. */
#define ROOF_SUPPORT_RANGE 2

/* Fraction of its hit points under which a structure that can be refreshed
 * gets a refresh site. */
#define STRUCTURE_REFRESH_BELOW 0.5f

/* NULL when out of range (STRUCTURE_NONE has a definition). */
const StructureDef *structure_def(StructureType type);

/* The item types that can serve as `material` (one of the structure's
 * materials): the type itself, and for dry grass the other fibres cut from
 * plants (bracken, heather, reeds; moss too for a bed), which thatch and bed
 * as well (docs/PLANTS.md §10). */
ItemTypeMask structure_material_types(StructureType type, ItemType material);

/* Whether a structure of this type blocks movement. */
bool structure_blocks(StructureType type);

/* Whether settlers sleep on it. */
bool structure_is_bed(StructureType type);

/* Whether it takes building: a material to bring or work to do. A structure
 * with neither (ground bedding) is built at once where the player puts it. */
bool structure_needs_construction(StructureType type);

/* Whether it is a roof (built as a floor over the walls). */
bool structure_is_roof(StructureType type);

/* Whether it is a ladder: built where it leads up (map_ladder_fits()). */
bool structure_is_ladder(StructureType type);

/* Tiles it takes: 1 for most, 2 for a bed (0 for STRUCTURE_NONE). Part `i`
 * of a structure whose head is at (x, y) lies at (x + i × direction_dx(),
 * y + i × direction_dy()). */
int structure_length(StructureType type);

/* Hit points of a structure of a quality: the standard ones × the quality
 * factor (×0.6 crude to ×1.5 superb, docs/KNOWLEDGE.md §2.2); at least 1.
 * 0 for STRUCTURE_NONE. */
int structure_max_hit_points(StructureType type, ItemQuality quality);

/* The first wooden material of a type (ItemDef.wooden: branches), whose
 * species the player may choose (docs/TREES.md §6.4); ITEM_NONE when it
 * takes none. */
ItemType structure_wooden_material(StructureType type);

/* structure_max_hit_points() of a structure built of wood of the species
 * `woods` (ItemStack.woods): × their toughness (core/wood.h; ×1 for 0). */
int structure_wood_max_hit_points(StructureType type, ItemQuality quality, WoodMask woods);

/* Best quality a structure can have with materials of these qualities: their
 * average + ITEM_QUALITY_CAP_BONUS points, among the materials that have a quality (grass and twigs
 * do not cap). `delivered` holds one stack per material slot. */
ItemQuality structure_quality_cap(StructureType type,
                                  const ItemStack delivered[STRUCTURE_MATERIAL_SLOTS]);

/* Work to take a built structure apart: a share of the building work. */
float structure_deconstruct_work(StructureType type);

/* Items given back when a built structure is taken apart, per material slot
 * (half the materials: lashings break, grass crumbles). */
int structure_salvage_count(StructureType type, int slot);

#endif /* COLONY_CORE_STRUCTURE_H */
