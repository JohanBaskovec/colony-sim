/*
 * Designations: work orders placed by the player on single tiles (dig this
 * earth, mine this rock, take this wall apart). They are kept apart from the
 * Map, which only describes the physical world. What the land offers (wood,
 * berries, grass, stones...) is gathered in zones instead
 * (core/gather_zone.h).
 *
 * Each order belongs to a knowledge (core/knowledge.h): only settlers with at
 * least its minimum level carry it out. Earth is dug out with the shelter
 * builders' knowledge, faster with a digging stick: galleries into hills,
 * ramps cut into a one-level bank of earth (the bank's tile becomes a slope
 * up to the ground behind it, the ground over it is taken away), and holes
 * dug down from the ground (the ground goes with the earth under it; the
 * hole slopes up to where its digger stood: the way into a cellar).
 * Nobody digs where a ceiling would be left without support (see
 * map_dig_keeps_ceilings()): such orders wait for a prop or a wall.
 * Mining rock needs a pick and a knowledge that are not in the game yet:
 * nobody can do it for now, but the order exists. So does felling trees
 * (woodcutting, with an axe): the order goes on the tree's foot (any tile of
 * its trunk points there) and the tree falls as tree_fell() says. Taking a structure apart
 * gives back half of its materials, and so does taking a roof down: the
 * order goes on the roof's tile (the level above the hut), the work is done
 * from below and the materials fall onto the floor under it.
 *
 * Each designated tile also stores how much work has been done, so that
 * interrupted work resumes where it stopped. Who works on which tile is
 * tracked by the reservations (core/reservation.h).
 */
#ifndef COLONY_CORE_DESIGNATION_H
#define COLONY_CORE_DESIGNATION_H

#include "core/construction.h"
#include "core/knowledge.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/stockpile.h"
#include "core/tool.h"
#include "core/work.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum DesignationType {
    DESIGNATION_NONE = 0,
    DESIGNATION_MINE,        /* Dig out a rock wall. */
    DESIGNATION_DECONSTRUCT, /* Take a built structure apart (core/structure.h), or a roof down. */
    DESIGNATION_DIG,         /* Dig out earth (ROCK_EARTH). */
    DESIGNATION_DIG_RAMP,    /* Cut a ramp into a bank of earth one level high. */
    DESIGNATION_DIG_DOWN,    /* Dig the ground and the earth under it out. */
    DESIGNATION_CHOP,        /* Fell a tree (core/tree.h), or cut a sapling. */
    DESIGNATION_COUNT
} DesignationType;

/* A ramp, or a hole dug down, takes this many times the work of digging its
 * earth out (the ground goes too). */
#define DESIGNATION_RAMP_WORK_FACTOR 1.5f

typedef struct DesignationDef {
    const char *id;           /* Stable identifier (saves), never shown. */
    const char *name_key;     /* Order name, e.g. "Mine rock". */
    const char *activity_key; /* What a settler doing it is busy with, e.g. "Mining". */
    WorkType work_type;
    /* Knowledge needed, KNOWLEDGE_NONE when nobody can carry it out yet. */
    KnowledgeType knowledge;
    float min_level;  /* At least KNOWLEDGE_MASTERY_LEVEL. */
    ToolUse tool_use; /* A tool that speeds the work up (fetched on the way), or none. */
} DesignationDef;

typedef struct Designations {
    int width;
    int height;
    int depth;
    uint8_t *type;                      /* DesignationType per tile. */
    float *progress;                    /* Work done so far, in work ticks. */
    int count;                          /* Designated tiles. */
    int count_by_work[WORK_TYPE_COUNT]; /* Designated tiles per work type. */
    /* Incremented whenever work may have become available (new designation,
     * completed work that opens access...), so that settlers who found
     * nothing to do know when to look again. */
    uint32_t revision;
    /* Trees felled onto a structure (no side was clear, see tree_fell()). */
    uint32_t harmful_tree_falls;
} Designations;

const DesignationDef *designation_def(DesignationType type);

bool designations_init(Designations *designations, int width, int height, int depth);
void designations_free(Designations *designations);

/* Whether a settler at `level` in the order's knowledge can carry it out. */
bool designation_doable_at(DesignationType type, double level);

/* Whether the order makes sense on this tile (a wall to mine, the foot of a
 * tree to fell...). */
bool designation_can_apply(const Map *map, int x, int y, int z, DesignationType type);

/* The tile an order given on (x, y, z) goes on: the foot of the tree for a
 * tile of trunk (see tree_at()), the tile itself otherwise. */
void designation_target(const Map *map, int *x, int *y, int *z);

/* Whether an order of this type at (x, y, z) leaves alone what lives
 * outside the map: construction sites, workstations, stockpiles. A ramp
 * takes the ground over it away, a hole dug down the ground it is ordered
 * on: such orders are refused there, or dropped when the ground gets used
 * before the work is done. Other orders: always true. */
bool designation_ground_free(const Constructions *constructions, const Workstations *workstations,
                             const Stockpiles *stockpiles, DesignationType type, int x, int y,
                             int z);

/* Whether the order on the tile digs (earth, rock, a ramp) and has to wait:
 * carrying it out now would leave a ceiling around without support. */
bool designation_waits_for_support(const Designations *designations, const Map *map, int x, int y,
                                   int z);

/* Whether the order on the tile touches something the player forbade (see
 * map_forbidden()): the structure or roof to take apart, the tree to fell,
 * the plant on the ground a ramp or a hole takes away. Nobody carries it out
 * meanwhile. */
bool designation_forbidden(const Designations *designations, const Map *map, int x, int y, int z);

DesignationType designation_get(const Designations *designations, int x, int y, int z);

/*
 * Places an order on a tile (on its designation_target()). DESIGNATION_NONE
 * removes the order. Invalid orders (nothing to mine...) are ignored.
 * Re-placing the same order keeps its progress. Returns whether the tile
 * changed.
 */
bool designation_set(Designations *designations, const Map *map, int x, int y, int z,
                     DesignationType type);

/* designation_set() on every tile of a rectangle (corners in any order, clipped
 * to the map). Returns the number of tiles changed. */
int designations_set_rect(Designations *designations, const Map *map, int x0, int y0, int x1,
                          int y1, int z, DesignationType type);

/* Designated tiles whose work type is in `mask`. */
int designations_count_for_work(const Designations *designations, WorkTypeMask mask);

/* Total work needed to complete the order on this tile (0 if none). */
float designation_work_needed(const Designations *designations, const Map *map, int x, int y,
                              int z);

/* Work done as a fraction 0..1 of the work needed. */
float designation_progress_fraction(const Designations *designations, const Map *map, int x, int y,
                                    int z);

/*
 * Adds work to a designated tile. When enough work is done, the order is
 * carried out on the map (the wall disappears...), its
 * products are dropped on the ground (`items` may be NULL) and true is
 * returned; the designation is removed. The products' quality and condition
 * come from the worker's `level` in the order's knowledge
 * (core/knowledge.h); `rng` may be NULL for average products.
 */
bool designation_add_work(Designations *designations, Map *map, Items *items, int x, int y, int z,
                          float work, double level, Rng *rng);

/* Items the order on this tile would produce if completed now (ITEM_NONE:
 * nothing). */
ItemType designation_yield(const Designations *designations, const Map *map, int x, int y, int z,
                           int *count);

uint64_t designations_hash(const Designations *designations, uint64_t hash);

#endif /* COLONY_CORE_DESIGNATION_H */
