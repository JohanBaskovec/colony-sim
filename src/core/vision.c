#include "core/vision.h"

#include "core/structure.h"
#include "core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* A slope bigger than any ray needs. */
#define SLOPE_INFINITY 1.0e9f
/* The shortest stretch of a ray in a column that counts (a ray that merely
 * brushes the corner of a tile does not see it). */
#define MIN_STRETCH 1.0e-4f
/* Slopes are computed over distances of at least this: a ray in the eye's own
 * column goes through floors and ceilings at any slope. */
#define MIN_DISTANCE 1.0e-12f
/* Intervals of open slopes a ray keeps; a block that would split one into a
 * fifteenth is ignored (the ray sees a little more: permissive, not wrong). */
#define OPEN_MAX 14
/* Rays per tile along the border of the region looked at: close to the eye
 * (a region with a radius) more of them, since shadows there are cast by
 * things the size of a tile; the whole map is looked at more coarsely. */
#define RAYS_PER_TILE_NEAR 2
#define RAYS_PER_TILE_FAR 1
/* Metres in a level. */
#define LEVEL_HEIGHT 2.0f

/* ------------------------------------------------------------------------ */
/* Tracked tiles and columns                                                */
/* ------------------------------------------------------------------------ */

static bool tracked_at(const Map *map, size_t index)
{
    return map->rock[index] != 0 || map->terrain[index] != TERRAIN_OPEN ||
           map->structure[index] != STRUCTURE_NONE;
}

bool vision_tracked(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) && tracked_at(map, map_index(map, x, y, z));
}

/* 1 + the highest tracked level of a column. */
static int column_height(const Map *map, int x, int y)
{
    size_t level = map_level_tile_count(map);
    size_t index = map_index(map, x, y, map->depth - 1);
    for (int z = map->depth - 1; z >= 0; z--, index -= level) {
        if (tracked_at(map, index)) {
            return z + 1;
        }
    }
    return 0;
}

static void refresh_column(Vision *vision, const Map *map, int x, int y)
{
    int top = column_height(map, x, y);
    vision->column_top[(size_t)y * (size_t)vision->width + (size_t)x] = (uint8_t)top;
    if (top > vision->top_level) {
        vision->top_level = top;
    }
}

static void refresh_all_columns(Vision *vision, const Map *map)
{
    vision->top_level = 0;
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            refresh_column(vision, map, x, y);
        }
    }
}

bool vision_init(Vision *vision, const Map *map)
{
    memset(vision, 0, sizeof(*vision));
    vision->width = map->width;
    vision->height = map->height;
    vision->depth = map->depth;
    size_t tiles = map_tile_count(map);
    vision->words = (tiles + 63) / 64;
    vision->column_top = malloc(map_level_tile_count(map));
    vision->seen = calloc(tiles, sizeof(*vision->seen));
    vision->scratch = malloc(vision->words * sizeof(*vision->scratch));
    vision->seen_bits = calloc(vision->words, sizeof(*vision->seen_bits));
    vision->explored = calloc(vision->words, sizeof(*vision->explored));
    vision->snapshot = calloc(vision->words, sizeof(*vision->snapshot));
    if (vision->column_top == NULL || vision->seen == NULL || vision->scratch == NULL ||
        vision->seen_bits == NULL || vision->explored == NULL || vision->snapshot == NULL) {
        vision_free(vision);
        return false;
    }
    refresh_all_columns(vision, map);
    vision->topology_revision = map->topology_revision;
    vision->build_revision = map->build_revision;
    return true;
}

void vision_free(Vision *vision)
{
    for (int i = 0; i < VISION_MAX_VIEWERS; i++) {
        free(vision->viewers[i].field);
    }
    free(vision->column_top);
    free(vision->seen);
    free(vision->scratch);
    free(vision->seen_bits);
    free(vision->explored);
    free(vision->snapshot);
    memset(vision, 0, sizeof(*vision));
}

/* Columns the slow sweep checks per call. */
#define SWEEP_COLUMNS 256

void vision_update_map(Vision *vision, const Map *map)
{
    if (vision->column_top == NULL) {
        return;
    }
    uint32_t topology = map->topology_revision - vision->topology_revision;
    uint32_t build = map->build_revision - vision->build_revision;
    if (topology != 0 || build != 0) {
        bool everything = topology > MAP_TOPOLOGY_LOG_SIZE || build > MAP_BUILD_LOG_SIZE;
        for (uint32_t r = 1; r <= topology && !everything; r++) {
            everything =
                map->topology_log[(vision->topology_revision + r) % MAP_TOPOLOGY_LOG_SIZE].anywhere;
        }
        if (everything) {
            refresh_all_columns(vision, map);
            for (int i = 0; i < VISION_MAX_VIEWERS; i++) {
                vision->viewers[i].near_dirty = true;
            }
        } else {
            for (uint32_t r = 1; r <= topology; r++) {
                const MapTopologyChange *change =
                    &map->topology_log[(vision->topology_revision + r) % MAP_TOPOLOGY_LOG_SIZE];
                refresh_column(vision, map, change->x, change->y);
                vision_tile_changed(vision, change->x, change->y, change->z);
            }
            for (uint32_t r = 1; r <= build; r++) {
                const MapBuildChange *change =
                    &map->build_log[(vision->build_revision + r) % MAP_BUILD_LOG_SIZE];
                refresh_column(vision, map, change->x, change->y);
                vision_tile_changed(vision, change->x, change->y, change->z);
            }
        }
        vision->topology_revision = map->topology_revision;
        vision->build_revision = map->build_revision;
    }
    /* What the logs miss (thin foliage coming and going): a slow sweep. */
    int columns = map->width * map->height;
    for (int n = 0; n < SWEEP_COLUMNS && columns > 0; n++) {
        int column = vision->sweep_column;
        refresh_column(vision, map, column % map->width, column / map->width);
        vision->sweep_column = column + 1 >= columns ? 0 : column + 1;
    }
}

/* ------------------------------------------------------------------------ */
/* Intervals of open slopes                                                  */
/* ------------------------------------------------------------------------ */

typedef struct OpenSet {
    float lo[OPEN_MAX];
    float hi[OPEN_MAX];
    int count;
} OpenSet;

/* Takes [lo, hi] off the set when `block`; returns whether any of it was open
 * (a ray with such a slope reaches the tile). */
static bool open_cut(OpenSet *set, float lo, float hi, bool block)
{
    bool hit = false;
    for (int i = 0; i < set->count; i++) {
        if (set->hi[i] <= lo || set->lo[i] >= hi) {
            continue;
        }
        hit = true;
        if (!block) {
            continue;
        }
        bool left = set->lo[i]<lo, right = set->hi[i]> hi;
        if (left && right) {
            if (set->count == OPEN_MAX) {
                continue; /* No room to split: leave it open. */
            }
            for (int j = set->count; j > i + 1; j--) {
                set->lo[j] = set->lo[j - 1];
                set->hi[j] = set->hi[j - 1];
            }
            set->count++;
            set->lo[i + 1] = hi;
            set->hi[i + 1] = set->hi[i];
            set->hi[i] = lo;
            i++; /* The right half lies beyond the block. */
        } else if (left) {
            set->hi[i] = lo;
        } else if (right) {
            set->lo[i] = hi;
        } else {
            for (int j = i; j + 1 < set->count; j++) {
                set->lo[j] = set->lo[j + 1];
                set->hi[j] = set->hi[j + 1];
            }
            set->count--;
            i--;
        }
    }
    return hit;
}

/* Keeps only the slopes in [lo, hi]. */
static void open_clip(OpenSet *set, float lo, float hi)
{
    int kept = 0;
    for (int i = 0; i < set->count; i++) {
        float a = set->lo[i] > lo ? set->lo[i] : lo;
        float b = set->hi[i] < hi ? set->hi[i] : hi;
        if (a < b) {
            set->lo[kept] = a;
            set->hi[kept] = b;
            kept++;
        }
    }
    set->count = kept;
}

/* ------------------------------------------------------------------------ */
/* Casting                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct Cast {
    Vision *vision;
    const Map *map;
    uint64_t *bits;
    size_t level_tiles;
    float eye_height;   /* In metres over the bottom of level 0. */
    int eye_level;      /* The level of the eye's tile. */
    size_t eye_column;  /* The column of the eye's tile. */
    int x0, x1, y0, y1; /* The region looked at (inclusive). */
    float max_height;   /* Height of the top of the highest tracked tile. */
    size_t added;       /* Bits set that were clear. */
} Cast;

static void mark(Cast *cast, size_t index)
{
    uint64_t *word = &cast->bits[index >> 6];
    uint64_t bit = (uint64_t)1 << (index & 63);
    if ((*word & bit) == 0) {
        *word |= bit;
        cast->added++;
    }
}

/* Whether a floor blocks sight between its level and the one below (the
 * thin foliage is seen through). */
static bool floor_blocks(uint8_t terrain)
{
    return terrain != TERRAIN_OPEN && terrain != TERRAIN_FOLIAGE_MEDIUM &&
           terrain != TERRAIN_FOLIAGE_SPARSE;
}

/* Looks at the tile of level `z` in a column, which a ray of open slopes
 * `set` goes through between distances 1/inv0 and 1/inv1; `pass` is +1 for
 * the slopes of 0 and above (going up through the levels in order), -1 for
 * those below 0 (going down). A solid tile is seen when a ray reaches its box,
 * any other when a ray reaches its floor (from above, or from below as a
 * ceiling): the floor is a plane at the bottom of the tile, which also stops
 * the ray when it is not thin foliage. What each hides is taken off the set. */
static void look_at_tile(Cast *cast, OpenSet *set, size_t column, int z, float inv0, float inv1,
                         int pass)
{
    const Map *map = cast->map;
    size_t index = column + (size_t)z * cast->level_tiles;
    uint8_t rock = map->rock[index];
    uint8_t terrain = map->terrain[index];
    uint8_t structure = map->structure[index];
    if (rock == 0 && terrain == TERRAIN_OPEN && structure == STRUCTURE_NONE) {
        return; /* Open space. */
    }
    bool own = column == cast->eye_column && z == cast->eye_level;
    bool solid =
        rock != 0 || (!own && (structure == STRUCTURE_WALL || structure == STRUCTURE_DOOR));
    bool plane = rock == 0;
    bool plane_blocks = plane && floor_blocks(terrain);
    float bottom = (float)z * LEVEL_HEIGHT - cast->eye_height; /* Relative to the eye. */
    float top = bottom + LEVEL_HEIGHT;
    bool seen = false;

    if (pass > 0) {
        if (plane && bottom > 0.0f) { /* Going up to the floor under the tile. */
            seen |= open_cut(set, bottom * inv1, bottom * inv0, plane_blocks);
        }
        if (solid) { /* The slopes crossing the tile's box. */
            float lo = bottom * inv0, lo2 = bottom * inv1;
            float hi = top * inv0, hi2 = top * inv1;
            lo = lo < lo2 ? lo : lo2;
            hi = hi > hi2 ? hi : hi2;
            if (lo < 0.0f) {
                lo = 0.0f;
            }
            if (lo < hi) {
                seen |= open_cut(set, lo, hi, true);
            }
        }
    } else {
        if (solid) {
            float lo = bottom * inv0, lo2 = bottom * inv1;
            float hi = top * inv0, hi2 = top * inv1;
            lo = lo < lo2 ? lo : lo2;
            hi = hi > hi2 ? hi : hi2;
            if (hi > 0.0f) {
                hi = 0.0f;
            }
            if (lo < hi) {
                seen |= open_cut(set, lo, hi, true);
            }
        }
        if (plane && bottom < 0.0f) { /* Down to the tile's floor. */
            seen |= open_cut(set, bottom * inv0, bottom * inv1, plane_blocks);
        }
    }
    if (seen) {
        mark(cast, index);
    }
}

/* One column of a ray: distances t0 to t1 from the eye, along the ray. */
static void look_at_column(Cast *cast, OpenSet *set, size_t column, float t0, float t1)
{
    int top = cast->vision->column_top[column];
    if (top == 0) {
        return;
    }
    if (t0 < MIN_DISTANCE) {
        t0 = MIN_DISTANCE;
    }
    float inv0 = 1.0f / t0, inv1 = 1.0f / t1;
    float highest = set->hi[set->count - 1], lowest = set->lo[0];

    if (highest >= 0.0f && top > cast->eye_level) {
        float reach = (cast->eye_height + highest * t1) * (1.0f / LEVEL_HEIGHT);
        int last = reach >= (float)(top - 1) ? top - 1 : (int)reach;
        for (int z = cast->eye_level; z <= last && set->count > 0; z++) {
            look_at_tile(cast, set, column, z, inv0, inv1, 1);
        }
    }
    if (set->count > 0 && lowest < 0.0f) {
        int first = top - 1 < cast->eye_level ? top - 1 : cast->eye_level;
        float reach = (cast->eye_height + lowest * t1) * (1.0f / LEVEL_HEIGHT);
        int last = reach <= 0.0f ? 0 : (int)reach;
        for (int z = first; z >= last && set->count > 0; z--) {
            look_at_tile(cast, set, column, z, inv0, inv1, -1);
        }
    }
}

/* One ray from the eye at (ex, ey) towards the middle of the tile (tx, ty). */
static void cast_ray(Cast *cast, float ex, float ey, float tx, float ty)
{
    float dx = tx - ex, dy = ty - ey;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 1.0e-3f) {
        return;
    }
    float ux = dx / length, uy = dy / length;
    int cx = (int)floorf(ex), cy = (int)floorf(ey);
    int step_x = ux > 0.0f ? 1 : -1, step_y = uy > 0.0f ? 1 : -1;
    float delta_x = ux != 0.0f ? 1.0f / fabsf(ux) : SLOPE_INFINITY;
    float delta_y = uy != 0.0f ? 1.0f / fabsf(uy) : SLOPE_INFINITY;
    float next_x = ux > 0.0f   ? ((float)(cx + 1) - ex) * delta_x
                   : ux < 0.0f ? (ex - (float)cx) * delta_x
                               : SLOPE_INFINITY;
    float next_y = uy > 0.0f   ? ((float)(cy + 1) - ey) * delta_y
                   : uy < 0.0f ? (ey - (float)cy) * delta_y
                               : SLOPE_INFINITY;
    OpenSet set;
    set.count = 1;
    set.lo[0] = -SLOPE_INFINITY;
    set.hi[0] = SLOPE_INFINITY;
    float from = 0.0f;
    size_t width = (size_t)cast->vision->width;
    uint64_t steps = 0;
    for (;;) {
        float to = next_x < next_y ? next_x : next_y;
        if (to - from > MIN_STRETCH) {
            steps++;
            look_at_column(cast, &set, (size_t)cy * width + (size_t)cx, from, to);
            if (set.count == 0) {
                break;
            }
            /* Nothing further can be seen above the highest tile, nor below
             * the lowest. */
            float inv = 1.0f / to;
            open_clip(&set, -cast->eye_height * inv, (cast->max_height - cast->eye_height) * inv);
            if (set.count == 0) {
                break;
            }
        }
        from = to;
        if (next_x < next_y) {
            cx += step_x;
            next_x += delta_x;
        } else {
            cy += step_y;
            next_y += delta_y;
        }
        if (cx < cast->x0 || cx > cast->x1 || cy < cast->y0 || cy > cast->y1) {
            break;
        }
    }
    cast->vision->steps += steps;
    cast->vision->rays++;
}

size_t vision_cast_eye(Vision *vision, const Map *map, int x, int y, int z, float eye_x,
                       float eye_y, int radius, uint64_t *bits)
{
    Cast cast = {
        .vision = vision,
        .map = map,
        .bits = bits,
        .level_tiles = map_level_tile_count(map),
        .eye_height = (float)z * LEVEL_HEIGHT + VISION_EYE_HEIGHT,
        .eye_level = z,
        .eye_column = (size_t)y * (size_t)map->width + (size_t)x,
        .x0 = 0,
        .x1 = map->width - 1,
        .y0 = 0,
        .y1 = map->height - 1,
        .max_height = (float)vision->top_level * LEVEL_HEIGHT,
    };
    if (radius > 0) {
        cast.x0 = x - radius > 0 ? x - radius : 0;
        cast.x1 = x + radius < map->width - 1 ? x + radius : map->width - 1;
        cast.y0 = y - radius > 0 ? y - radius : 0;
        cast.y1 = y + radius < map->height - 1 ? y + radius : map->height - 1;
    }
    if (map_in_bounds(map, x, y, z)) {
        mark(&cast, map_index(map, x, y, z));
    }
    /* A ray to the middle of each tile along the border of the region, or
     * more of them. */
    const float spacing = 1.0f / (float)(radius > 0 ? RAYS_PER_TILE_NEAR : RAYS_PER_TILE_FAR);
    const float west = (float)cast.x0 + 0.5f, east = (float)cast.x1 + 0.5f;
    const float north = (float)cast.y0 + 0.5f, south = (float)cast.y1 + 0.5f;
    for (float tx = west; tx <= east + 1.0e-3f; tx += spacing) {
        cast_ray(&cast, eye_x, eye_y, tx, north);
        cast_ray(&cast, eye_x, eye_y, tx, south);
    }
    for (float ty = north + spacing; ty < south - 1.0e-3f; ty += spacing) {
        cast_ray(&cast, eye_x, eye_y, west, ty);
        cast_ray(&cast, eye_x, eye_y, east, ty);
    }
    return cast.added;
}

/* Whether a tile blocks sideways (a wall for the eye to look round). */
static bool blocks_sideways(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return true;
    }
    size_t index = map_index(map, x, y, z);
    return map->rock[index] != 0 || map->structure[index] == STRUCTURE_WALL ||
           map->structure[index] == STRUCTURE_DOOR;
}

/* The tiles of a sleeper's field: its own, the ones around it on its level,
 * and those over and under it. */
static size_t cast_asleep(const Map *map, int x, int y, int z, uint64_t *bits)
{
    size_t added = 0;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dz != 0 && (dx != 0 || dy != 0)) {
                    continue;
                }
                int tx = x + dx, ty = y + dy, tz = z + dz;
                if (!vision_tracked(map, tx, ty, tz)) {
                    continue;
                }
                size_t index = map_index(map, tx, ty, tz);
                uint64_t bit = (uint64_t)1 << (index & 63);
                if ((bits[index >> 6] & bit) == 0) {
                    bits[index >> 6] |= bit;
                    added++;
                }
            }
        }
    }
    return added;
}

size_t vision_cast(Vision *vision, const Map *map, int x, int y, int z, bool asleep, int radius,
                   uint64_t *bits)
{
    if (!map_in_bounds(map, x, y, z)) {
        return 0;
    }
    if (asleep) {
        return cast_asleep(map, x, y, z, bits);
    }
    float middle_x = (float)x + 0.5f, middle_y = (float)y + 0.5f;
    size_t added = 0;
    if (radius == 0) {
        added += vision_cast_eye(vision, map, x, y, z, middle_x, middle_y, 0, bits);
    }
    /* The tiles around the eye with closer rays. */
    int near = radius == 0 ? VISION_NEAR_RADIUS : radius;
    added += vision_cast_eye(vision, map, x, y, z, middle_x, middle_y, near, bits);
    /* With a wall about, the eye leans towards each open side to look past
     * its corners. */
    static const int SIDES[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    bool wall_near = false;
    for (int dy = -1; dy <= 1 && !wall_near; dy++) {
        for (int dx = -1; dx <= 1 && !wall_near; dx++) {
            wall_near = (dx != 0 || dy != 0) && blocks_sideways(map, x + dx, y + dy, z);
        }
    }
    for (int side = 0; side < 4 && wall_near; side++) {
        int sx = SIDES[side][0], sy = SIDES[side][1];
        if (blocks_sideways(map, x + sx, y + sy, z)) {
            continue;
        }
        int reach = radius == 0 || radius > VISION_PEEK_RADIUS ? VISION_PEEK_RADIUS : radius;
        added += vision_cast_eye(vision, map, x, y, z, middle_x + (float)sx * VISION_PEEK_OFFSET,
                                 middle_y + (float)sy * VISION_PEEK_OFFSET, reach, bits);
    }
    return added;
}

/* ------------------------------------------------------------------------ */
/* Viewers                                                                   */
/* ------------------------------------------------------------------------ */

/* Replaces the bits [first, last] (inclusive) of a viewer's field with those
 * of `fresh`, keeping `seen` right. */
static void replace_bits(Vision *vision, uint64_t *field, const uint64_t *fresh, size_t first,
                         size_t last)
{
    for (size_t word = first >> 6; word <= last >> 6; word++) {
        uint64_t mask = ~(uint64_t)0;
        if (word == first >> 6) {
            mask &= ~(uint64_t)0 << (first & 63);
        }
        if (word == last >> 6 && (last & 63) != 63) {
            mask &= ~(~(uint64_t)0 << ((last & 63) + 1));
        }
        uint64_t before = field[word] & mask, after = fresh[word] & mask;
        uint64_t changed = before ^ after;
        if (changed == 0) {
            continue;
        }
        uint64_t gained = after & changed, lost = before & changed;
        while (gained != 0) {
            int bit = lowest_bit64(gained);
            if (vision->seen[word * 64 + (size_t)bit]++ == 0) {
                uint64_t tile_bit = (uint64_t)1 << bit;
                vision->seen_bits[word] |= tile_bit;
                vision->snapshot[word] |= tile_bit;
                vision->snapshot_pending = true;
                if ((vision->explored[word] & tile_bit) == 0) {
                    vision->explored[word] |= tile_bit;
                    vision->explored_count++;
                }
            }
            gained &= gained - 1;
        }
        while (lost != 0) {
            int bit = lowest_bit64(lost);
            if (--vision->seen[word * 64 + (size_t)bit] == 0) {
                uint64_t tile_bit = (uint64_t)1 << bit;
                vision->seen_bits[word] &= ~tile_bit;
                vision->snapshot[word] |= tile_bit;
                vision->snapshot_pending = true;
            }
            lost &= lost - 1;
        }
        field[word] = (field[word] & ~mask) | after;
    }
}

/* Recasts the whole field of a viewer. */
static void recast_far(Vision *vision, const Map *map, VisionViewer *viewer)
{
    memset(vision->scratch, 0, vision->words * sizeof(*vision->scratch));
    vision_cast(vision, map, viewer->x, viewer->y, viewer->z, viewer->asleep, 0, vision->scratch);
    replace_bits(vision, viewer->field, vision->scratch, 0, map_tile_count(map) - 1);
}

/* Recasts what a viewer sees within the near square around it. */
static void recast_near(Vision *vision, const Map *map, VisionViewer *viewer)
{
    int x0 = viewer->x - VISION_NEAR_RADIUS, x1 = viewer->x + VISION_NEAR_RADIUS;
    int y0 = viewer->y - VISION_NEAR_RADIUS, y1 = viewer->y + VISION_NEAR_RADIUS;
    x0 = x0 > 0 ? x0 : 0;
    y0 = y0 > 0 ? y0 : 0;
    x1 = x1 < map->width - 1 ? x1 : map->width - 1;
    y1 = y1 < map->height - 1 ? y1 : map->height - 1;
    /* Clear the rows of the square in the scratch field, cast into it, and
     * swap those rows in. */
    for (int z = 0; z < map->depth; z++) {
        for (int y = y0; y <= y1; y++) {
            size_t first = map_index(map, x0, y, z), last = map_index(map, x1, y, z);
            for (size_t word = first >> 6; word <= last >> 6; word++) {
                vision->scratch[word] = 0;
            }
        }
    }
    vision_cast(vision, map, viewer->x, viewer->y, viewer->z, viewer->asleep, VISION_NEAR_RADIUS,
                vision->scratch);
    for (int z = 0; z < map->depth; z++) {
        for (int y = y0; y <= y1; y++) {
            replace_bits(vision, viewer->field, vision->scratch, map_index(map, x0, y, z),
                         map_index(map, x1, y, z));
        }
    }
}

void vision_viewer_set(Vision *vision, const Map *map, int slot, int x, int y, int z, bool asleep,
                       uint64_t tick)
{
    if ((unsigned)slot >= VISION_MAX_VIEWERS || !map_in_bounds(map, x, y, z)) {
        return;
    }
    VisionViewer *viewer = &vision->viewers[slot];
    if (viewer->field == NULL) {
        viewer->field = calloc(vision->words, sizeof(*viewer->field));
        if (viewer->field == NULL) {
            return;
        }
    }
    bool first = !viewer->active;
    bool moved =
        first || viewer->x != x || viewer->y != y || viewer->z != z || viewer->asleep != asleep;
    bool far_due = first || tick - viewer->far_tick >= VISION_FAR_INTERVAL ||
                   abs(viewer->far_x - x) >= VISION_FAR_MOVE ||
                   abs(viewer->far_y - y) >= VISION_FAR_MOVE ||
                   abs(viewer->far_z - z) >= VISION_FAR_MOVE_LEVELS;
    if (!moved && !far_due && !viewer->near_dirty) {
        return;
    }
    viewer->x = (int16_t)x;
    viewer->y = (int16_t)y;
    viewer->z = (int16_t)z;
    viewer->asleep = asleep;
    viewer->near_dirty = false;
    if (far_due) {
        /* The whole field. The first one of each viewer is staggered, so that
         * settlers do not all recast at once from then on. */
        if (first) {
            uint64_t stagger = (uint64_t)slot * (VISION_FAR_INTERVAL / VISION_MAX_VIEWERS);
            viewer->far_tick = tick > stagger ? tick - stagger : 0;
        } else {
            viewer->far_tick = tick;
        }
        viewer->far_x = (int16_t)x;
        viewer->far_y = (int16_t)y;
        viewer->far_z = (int16_t)z;
        viewer->active = true;
        recast_far(vision, map, viewer);
    } else {
        recast_near(vision, map, viewer);
    }
}

void vision_viewer_remove(Vision *vision, int slot)
{
    if ((unsigned)slot >= VISION_MAX_VIEWERS) {
        return;
    }
    VisionViewer *viewer = &vision->viewers[slot];
    if (viewer->field != NULL && viewer->active) {
        memset(vision->scratch, 0, vision->words * sizeof(*vision->scratch));
        replace_bits(vision, viewer->field, vision->scratch, 0, vision->words * 64 - 1);
    }
    viewer->active = false;
}

void vision_tile_changed(Vision *vision, int x, int y, int z)
{
    (void)z; /* A tile changed at any level can hide or show any other. */
    for (int i = 0; i < VISION_MAX_VIEWERS; i++) {
        VisionViewer *viewer = &vision->viewers[i];
        if (!viewer->active) {
            continue;
        }
        if (abs(viewer->x - x) <= VISION_NEAR_RADIUS && abs(viewer->y - y) <= VISION_NEAR_RADIUS) {
            viewer->near_dirty = true;
        }
    }
}

void vision_explore_tile(Vision *vision, size_t index)
{
    uint64_t mask = (uint64_t)1 << (index & 63);
    if ((vision->explored[index >> 6] & mask) == 0) {
        vision->explored[index >> 6] |= mask;
        vision->explored_count++;
        vision->snapshot[index >> 6] |= mask;
        vision->snapshot_pending = true;
    }
}

void vision_explore_field(Vision *vision, const uint64_t *bits)
{
    for (size_t word = 0; word < vision->words; word++) {
        uint64_t fresh = bits[word] & ~vision->explored[word];
        if (fresh != 0) {
            vision->explored[word] |= fresh;
            vision->explored_count += (size_t)__builtin_popcountll(fresh);
            vision->snapshot[word] |= fresh;
            vision->snapshot_pending = true;
        }
    }
}

void vision_explore_all(Vision *vision, const Map *map)
{
    size_t tiles = map_tile_count(map);
    for (size_t i = 0; i < tiles; i++) {
        if (tracked_at(map, i)) {
            vision_explore_tile(vision, i);
        }
    }
}

bool vision_explored(const Vision *vision, int x, int y, int z)
{
    if (x < 0 || y < 0 || z < 0 || x >= vision->width || y >= vision->height ||
        z >= vision->depth) {
        return false;
    }
    size_t index =
        ((size_t)z * (size_t)vision->height + (size_t)y) * (size_t)vision->width + (size_t)x;
    return (vision->explored[index >> 6] >> (index & 63) & 1u) != 0;
}

uint64_t vision_explored_hash(const Vision *vision)
{
    return hash_fnv1a64(HASH_FNV1A64_INIT, vision->explored,
                        vision->words * sizeof(*vision->explored));
}

bool vision_seen(const Vision *vision, int x, int y, int z)
{
    if (x < 0 || y < 0 || z < 0 || x >= vision->width || y >= vision->height ||
        z >= vision->depth) {
        return false;
    }
    size_t index =
        ((size_t)z * (size_t)vision->height + (size_t)y) * (size_t)vision->width + (size_t)x;
    return vision->seen[index] != 0;
}

bool vision_viewer_sees(const Vision *vision, int slot, int x, int y, int z)
{
    if ((unsigned)slot >= VISION_MAX_VIEWERS || vision->viewers[slot].field == NULL || x < 0 ||
        y < 0 || z < 0 || x >= vision->width || y >= vision->height || z >= vision->depth) {
        return false;
    }
    size_t index =
        ((size_t)z * (size_t)vision->height + (size_t)y) * (size_t)vision->width + (size_t)x;
    return (vision->viewers[slot].field[index >> 6] >> (index & 63) & 1u) != 0;
}
