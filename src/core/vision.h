/*
 * Vision: who sees what (docs/VISION.md). Sight goes in straight lines through
 * the volume of the map, with no range limit: rock, walls, doors, trunks and
 * the floors between levels stop it, open space and thin foliage do not. A
 * tile is 1 m wide and a level 2 m high; the eyes of a settler are
 * VISION_EYE_HEIGHT above the floor of its tile.
 *
 * Only tiles with something in them are looked at (rock, a floor, a
 * structure: "tracked" tiles); open space is not, so a field of view is a set
 * of tracked tiles, one bit per tile of the map, in map_index() order.
 *
 * A field of view is cast as rays along the map's grid, one per tile of the
 * border of the region looked at. Each ray is a problem in its own vertical
 * plane: marching from column to column, it keeps the intervals of slopes
 * (height over distance) still open. A solid tile is seen when an open slope
 * reaches its box, any other when one reaches its floor (a plane at the
 * bottom of the tile, seen from above, or from below as a ceiling); solid
 * tiles and floors take their slopes off the open set. A column only looks at
 * the levels the open slopes can reach there, so open ground costs one tile a
 * step.
 *
 * Fields are kept per viewer (a settler) and summed per tile (`seen`: how
 * many viewers see it now). A viewer's near field (VISION_NEAR_RADIUS tiles
 * around it) is recast whenever it moves or the map changes there; the whole
 * field now and then (it is much costlier).
 *
 * All of it is derived from the map and the viewers' positions: nothing here
 * is part of the game hash.
 */
#ifndef COLONY_CORE_VISION_H
#define COLONY_CORE_VISION_H

#include "core/map.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VISION_MAX_VIEWERS 64

/* Height of the eyes over the floor of the tile (metres). */
#define VISION_EYE_HEIGHT 1.6f
/* How far a settler leans to look round a corner: from the middle of its
 * tile, towards the middle of an edge (tile units). */
#define VISION_PEEK_OFFSET 0.4f
/* How far the leaning eyes look (the middle one looks as far as it can): what
 * a lean shows past a corner is mostly what is near it. */
#define VISION_PEEK_RADIUS 8
/* Tiles around a viewer (a square, any level) whose field is recast at every
 * step it takes. */
#define VISION_NEAR_RADIUS 16
/* Ticks between two recasts of a viewer's whole field (staggered per viewer),
 * and the tiles it may walk (on a level, or by levels) before that. */
#define VISION_FAR_INTERVAL 300
#define VISION_FAR_MOVE 16
#define VISION_FAR_MOVE_LEVELS 3

typedef struct VisionViewer {
    bool active;
    bool asleep;                 /* Sees only what touches its tile. */
    int16_t x, y, z;             /* Where its field was cast. */
    int16_t far_x, far_y, far_z; /* Where its whole field was last cast. */
    uint64_t far_tick;           /* The tick of its last whole recast. */
    bool near_dirty;             /* The map changed in its near square since. */
    uint64_t *field;             /* Tiles it sees: a bit per tile (NULL until first cast). */
} VisionViewer;

typedef struct Vision {
    int width, height, depth;
    size_t words; /* 64-bit words of a field. */
    /* Per column (y * width + x): 1 + the highest level holding a tracked
     * tile, 0 for a column with none. */
    uint8_t *column_top;
    int top_level;     /* The highest of them, over the map. */
    uint16_t *seen;    /* Per tile: viewers seeing it now. */
    uint64_t *scratch; /* A field being cast. */
    /* One bit per tile, like a field: seen by someone now (`seen_bits`), seen
     * by anyone ever or marked explored (`explored`), and to copy into the
     * known world (`snapshot`: it came into view, or went out of it, or was
     * explored without being seen; core/known.h). `snapshot_pending`: some
     * bit of it is set. */
    uint64_t *seen_bits;
    uint64_t *explored;
    uint64_t *snapshot;
    size_t explored_count;
    bool snapshot_pending;
    VisionViewer viewers[VISION_MAX_VIEWERS];
    /* The map revisions columns were last checked at, and the column the
     * slow sweep over all of them has reached. */
    uint32_t topology_revision;
    uint32_t build_revision;
    int sweep_column;
    /* Statistics, for benchmarks: rays cast and columns they went through. */
    uint64_t rays;
    uint64_t steps;
} Vision;

/* Allocates for a map and looks at it. Fails (nothing allocated) on a lack of
 * memory. */
bool vision_init(Vision *vision, const Map *map);
void vision_free(Vision *vision);

/* Brings the tracked columns up to date with the map's changes since the last
 * call (the logs of the map, and a slow sweep over every column for what is
 * not logged: thin foliage). Called before looking at viewers. */
void vision_update_map(Vision *vision, const Map *map);

/* Whether a tile holds something to see (rock, a floor or a structure): the
 * tiles fields are made of. */
bool vision_tracked(const Map *map, int x, int y, int z);

/*
 * The field of view of an eye standing on a tile, written into `bits` (a field
 * of `vision->words` words, OR-ed into: clear it first) and returned as the
 * number of tiles added. `radius` limits it to the square of that many tiles
 * around the tile (0: the whole map). `asleep`: only the tile, its eight
 * neighbors and the tiles over and under it. Includes the viewer's own tile.
 * Peeking round corners is added when a wall is next to the tile.
 */
size_t vision_cast(Vision *vision, const Map *map, int x, int y, int z, bool asleep, int radius,
                   uint64_t *bits);

/* The same for a single eye at (eye_x, eye_y) (tile units) in the tile, no
 * peeking: what vision_cast() does once, or up to five times. */
size_t vision_cast_eye(Vision *vision, const Map *map, int x, int y, int z, float eye_x,
                       float eye_y, int radius, uint64_t *bits);

/*
 * Viewers. A viewer is put where it stands (`slot` in 0..VISION_MAX_VIEWERS-1,
 * a settler's index): its near field is recast when it changed tile or
 * posture, its whole field every VISION_FAR_INTERVAL ticks, and the tiles
 * counted in `seen` follow. vision_viewer_remove() takes its field off.
 */
void vision_viewer_set(Vision *vision, const Map *map, int slot, int x, int y, int z, bool asleep,
                       uint64_t tick);
void vision_viewer_remove(Vision *vision, int slot);

/* The tiles of the map around a changed tile whose viewers need a new near
 * field: call for every tile the map logged as changed (vision_update_map()
 * does it itself with the logs). */
void vision_tile_changed(Vision *vision, int x, int y, int z);

/* Whether anyone has ever seen the tile (or it was marked explored). False
 * out of bounds. */
bool vision_explored(const Vision *vision, int x, int y, int z);

/* Marks as explored the tiles of a field (see vision_cast()): they will be
 * copied into the known world. */
void vision_explore_field(Vision *vision, const uint64_t *bits);

/* Marks every tracked tile of the map explored (the Reveal map cheat). */
void vision_explore_all(Vision *vision, const Map *map);

/* Marks a tile explored. */
void vision_explore_tile(Vision *vision, size_t index);

/* A hash of what is explored, for the game hash. */
uint64_t vision_explored_hash(const Vision *vision);

/* Whether some viewer sees the tile now; whether a given viewer does. False
 * out of bounds. */
bool vision_seen(const Vision *vision, int x, int y, int z);
bool vision_viewer_sees(const Vision *vision, int slot, int x, int y, int z);

#endif /* COLONY_CORE_VISION_H */
