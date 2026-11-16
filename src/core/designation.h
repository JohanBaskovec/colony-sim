/*
 * Designations: work orders placed by the player on single tiles (mine this
 * rock, take this wall apart). They are kept apart from the Map, which only describes the physical
 * world. What the land offers (wood, berries, grass, stones...) is gathered
 * in zones instead (core/gather_zone.h).
 *
 * Each order belongs to a knowledge (core/knowledge.h): only settlers with at
 * least its minimum level carry it out. Mining needs a pick and a knowledge
 * that are not in the game yet: nobody can do it for now, but the order
 * exists. Taking a structure apart gives back half of its materials, and so
 * does taking a roof down. Roof orders belong to the roof layer (see
 * designation_is_roof()): the player places them in the roof view.
 *
 * Each designated tile also stores how much work has been done, so that
 * interrupted work resumes where it stopped. Who works on which tile is
 * tracked by the reservations (core/reservation.h).
 */
#ifndef COLONY_CORE_DESIGNATION_H
#define COLONY_CORE_DESIGNATION_H

#include "core/knowledge.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/work.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum DesignationType {
    DESIGNATION_NONE = 0,
    DESIGNATION_MINE, /* Dig out a rock wall. */
    DESIGNATION_DECONSTRUCT, /* Take a built structure apart (core/structure.h), not its roof. */
    DESIGNATION_REMOVE_ROOF, /* Take the roof over a tile down. */
    DESIGNATION_COUNT
} DesignationType;

typedef struct DesignationDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key; /* Order name, e.g. "Mine rock". */
    const char *activity_key; /* What a settler doing it is busy with, e.g. "Mining". */
    WorkType work_type;
    /* Knowledge needed, KNOWLEDGE_NONE when nobody can carry it out yet. */
    KnowledgeType knowledge;
    float min_level; /* At least KNOWLEDGE_MASTERY_LEVEL. */
} DesignationDef;

typedef struct Designations {
    int width;
    int height;
    int depth;
    uint8_t *type; /* DesignationType per tile. */
    float *progress; /* Work done so far, in work ticks. */
    int count; /* Designated tiles. */
    int count_by_work[WORK_TYPE_COUNT]; /* Designated tiles per work type. */
    /* Incremented whenever work may have become available (new designation,
     * completed work that opens access...), so that settlers who found
     * nothing to do know when to look again. */
    uint32_t revision;
} Designations;

const DesignationDef *designation_def(DesignationType type);

bool designations_init(Designations *designations, int width, int height, int depth);
void designations_free(Designations *designations);

/* Whether a settler at `level` in the order's knowledge can carry it out. */
bool designation_doable_at(DesignationType type, double level);

/* Whether an order is about roofs (the roof layer), not what stands below. */
bool designation_is_roof(DesignationType type);

/* Whether the order makes sense on this tile (a wall to mine...). */
bool designation_can_apply(const Map *map, int x, int y, int z, DesignationType type);

DesignationType designation_get(const Designations *designations, int x, int y, int z);

/*
 * Places an order on a tile. DESIGNATION_NONE removes the order. Invalid
 * orders (nothing to mine...) are ignored. Re-placing the same order keeps
 * its progress. Returns whether the tile changed.
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
float designation_progress_fraction(const Designations *designations, const Map *map, int x,
                                    int y, int z);

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
