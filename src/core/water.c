#include "core/water.h"

#include "core/util.h"

#include <stdlib.h>
#include <string.h>

/* The tile of a block and a local index (ly * WATER_BLOCK_SIZE + lx). */
typedef struct BlockPlace {
    int bx, by, z;
} BlockPlace;

static BlockPlace place_of(const Water *water, uint32_t number)
{
    BlockPlace place;
    place.bx = (int)(number % (uint32_t)water->blocks_x);
    uint32_t rest = number / (uint32_t)water->blocks_x;
    place.by = (int)(rest % (uint32_t)water->blocks_y);
    place.z = (int)(rest / (uint32_t)water->blocks_y);
    return place;
}

static uint32_t number_of(const Water *water, int bx, int by, int z)
{
    return ((uint32_t)z * (uint32_t)water->blocks_y + (uint32_t)by) * (uint32_t)water->blocks_x +
           (uint32_t)bx;
}

uint32_t water_block_of(const Water *water, int x, int y, int z)
{
    return number_of(water, x / WATER_BLOCK_SIZE, y / WATER_BLOCK_SIZE, z);
}

/* The state of an active block, NULL when asleep or out of bounds. Not kept
 * across a wake (the pool may move). */
static WaterBlock *block_get(const Water *water, int bx, int by, int z)
{
    if (bx < 0 || by < 0 || z < 0 || bx >= water->blocks_x || by >= water->blocks_y ||
        z >= water->depth) {
        return NULL;
    }
    uint32_t slot = water->slot[number_of(water, bx, by, z)];
    return slot != 0 ? &water->pool[slot - 1] : NULL;
}

bool water_block_active(const Water *water, int x, int y, int z)
{
    if (x < 0 || y < 0 || z < 0 || x >= water->width || y >= water->height || z >= water->depth) {
        return false;
    }
    return water->slot[water_block_of(water, x, y, z)] != 0;
}

static bool bit_get(const uint64_t *bits, int local)
{
    return (bits[local / 64] >> (local % 64) & 1u) != 0;
}

static void bit_set(uint64_t *bits, int local, bool on)
{
    uint64_t mask = (uint64_t)1 << (local % 64);
    bits[local / 64] = on ? bits[local / 64] | mask : bits[local / 64] & ~mask;
}

/* Deep water, where nobody walks (map_move_cost_at()). */
static bool tile_deep(const Map *map, size_t index)
{
    return map->water[index] != 0 && map_water_depth_at(map, index) >= WATER_DEEP_MM;
}

/* Whether one could walk on the tile but for its water. */
static bool tile_walkable_dry(const Map *map, size_t index)
{
    return map->rock[index] == ROCK_NONE && !map_locked_at(map, index) &&
           terrain_def((Terrain)map->terrain[index])->move_cost > 0.0f;
}

/* The tiles of a block in bounds: x0..x1, y0..y1 (exclusive ends). */
static void block_bounds(const Water *water, BlockPlace place, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = place.bx * WATER_BLOCK_SIZE;
    *y0 = place.by * WATER_BLOCK_SIZE;
    *x1 = *x0 + WATER_BLOCK_SIZE < water->width ? *x0 + WATER_BLOCK_SIZE : water->width;
    *y1 = *y0 + WATER_BLOCK_SIZE < water->height ? *y0 + WATER_BLOCK_SIZE : water->height;
}

/* What the map knows of the classes of a block's tiles, as they are now. */
static void block_read_classes(const Water *water, const Map *map, WaterBlock *block)
{
    BlockPlace place = place_of(water, block->number);
    int x0, y0, x1, y1;
    block_bounds(water, place, &x0, &y0, &x1, &y1);
    memset(block->passable, 0, sizeof(block->passable));
    memset(block->open_water, 0, sizeof(block->open_water));
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            size_t index = map_index(map, x, y, place.z);
            int local = (y - y0) * WATER_BLOCK_SIZE + (x - x0);
            bit_set(block->passable, local, !tile_deep(map, index));
            bit_set(block->open_water, local, map_is_water_at(map, index));
        }
    }
}

/* Tells the map how the classes of a block's tiles changed since it was last
 * told. `passable` holds "not deep": a tile's way is shut by deep water only
 * where it could be walked dry. */
static void block_report_classes(const Water *water, Map *map, WaterBlock *block)
{
    BlockPlace place = place_of(water, block->number);
    int x0, y0, x1, y1;
    block_bounds(water, place, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            size_t index = map_index(map, x, y, place.z);
            int local = (y - y0) * WATER_BLOCK_SIZE + (x - x0);
            bool shallow = !tile_deep(map, index);
            bool open_water = map_is_water_at(map, index);
            bool shallow_changed = shallow != bit_get(block->passable, local);
            bool water_changed = open_water != bit_get(block->open_water, local);
            if (!shallow_changed && !water_changed) {
                continue;
            }
            bit_set(block->passable, local, shallow);
            bit_set(block->open_water, local, open_water);
            map_water_class_changed(map, x, y, place.z,
                                    shallow_changed && tile_walkable_dry(map, index), shallow,
                                    water_changed);
        }
    }
}

/* Wakes a block by its number. */
static bool wake_number(Water *water, const Map *map, uint32_t number)
{
    if (water->slot[number] != 0) {
        return true;
    }
    uint32_t entry;
    if (water->free_count > 0) {
        entry = water->free_entries[--water->free_count];
    } else {
        if (water->pool_used == water->pool_capacity) {
            uint32_t capacity = water->pool_capacity > 0 ? water->pool_capacity * 2 : 16;
            if (capacity > water->block_count) {
                capacity = water->block_count;
            }
            WaterBlock *pool = realloc(water->pool, (size_t)capacity * sizeof(*pool));
            if (pool == NULL) {
                return false;
            }
            water->pool = pool;
            water->pool_capacity = capacity;
        }
        entry = water->pool_used++;
    }
    WaterBlock *block = &water->pool[entry];
    memset(block, 0, sizeof(*block));
    block->number = number;
    block_read_classes(water, map, block);
    water->slot[number] = entry + 1;
    water->woken[water->woken_count++] = number;
    water->wakes++;
    return true;
}

bool water_wake(Water *water, const Map *map, int x, int y, int z)
{
    if (x < 0 || y < 0 || z < 0 || x >= water->width || y >= water->height || z >= water->depth) {
        return true;
    }
    return wake_number(water, map, water_block_of(water, x, y, z));
}

static int compare_numbers(const void *a, const void *b)
{
    uint32_t na = *(const uint32_t *)a, nb = *(const uint32_t *)b;
    return na < nb ? -1 : na > nb ? 1 : 0;
}

/* Merges the blocks woken into the active ones, keeping them in order. */
static void merge_woken(Water *water)
{
    if (water->woken_count == 0) {
        return;
    }
    qsort(water->woken, water->woken_count, sizeof(*water->woken), compare_numbers);
    /* From the end, so that the active list can take the merge in place. */
    uint32_t i = water->active_count, j = water->woken_count;
    uint32_t out = water->active_count + water->woken_count;
    while (j > 0) {
        if (i > 0 && water->active[i - 1] > water->woken[j - 1]) {
            water->active[--out] = water->active[--i];
        } else {
            water->active[--out] = water->woken[--j];
        }
    }
    water->active_count += water->woken_count;
    water->woken_count = 0;
}

/* Whether a block holds any water. */
static bool block_wet(const Water *water, const Map *map, uint32_t number)
{
    BlockPlace place = place_of(water, number);
    int x0, y0, x1, y1;
    block_bounds(water, place, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++) {
        const uint16_t *row = &map->water[map_index(map, 0, y, place.z)];
        for (int x = x0; x < x1; x++) {
            if (row[x] != 0) {
                return true;
            }
        }
    }
    return false;
}

bool water_wake_all(Water *water, const Map *map)
{
    water->wake_alls++;
    for (uint32_t number = 0; number < water->block_count; number++) {
        if (water->slot[number] == 0 && block_wet(water, map, number) &&
            !wake_number(water, map, number)) {
            return false;
        }
    }
    return true;
}

/* After a change of the map at a tile: wakes the blocks around it when some
 * water lies close enough to be moved by it. */
static bool wake_around(Water *water, const Map *map, int x, int y, int z)
{
    bool wet = false;
    for (int dz = -1; dz <= 1 && !wet; dz++) {
        for (int dy = -1; dy <= 1 && !wet; dy++) {
            for (int dx = -1; dx <= 1 && !wet; dx++) {
                wet = map_water(map, x + dx, y + dy, z + dz) != 0;
            }
        }
    }
    if (!wet) {
        return true;
    }
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy += 2) {
            for (int dx = -1; dx <= 1; dx += 2) {
                if (!water_wake(water, map, x + dx, y + dy, z + dz)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* Water taken from a tile from outside the flow (drunk, dried, rock in its
 * place) takes the fraction of a millimetre left with it. */
static void clear_fine(Water *water, const Map *map, int x, int y, int z)
{
    if (map_in_bounds(map, x, y, z)) {
        size_t index = map_index(map, x, y, z);
        if (map->water[index] == 0 || map->rock[index] != ROCK_NONE) {
            water->fine[index] = 0;
        }
    }
}

/* Reads what the map's logs tell since the last step. */
static bool read_logs(Water *water, const Map *map)
{
    uint32_t topology = map->topology_revision - water->topology_seen;
    uint32_t build = map->build_revision - water->build_seen;
    uint32_t poured = map->water_log_revision - water->water_seen;
    bool all = topology > MAP_TOPOLOGY_LOG_SIZE || build > MAP_BUILD_LOG_SIZE ||
               poured > MAP_WATER_LOG_SIZE;
    bool ok = true;
    for (uint32_t k = 1; k <= topology && !all && ok; k++) {
        const MapTopologyChange *change =
            &map->topology_log[(water->topology_seen + k) % MAP_TOPOLOGY_LOG_SIZE];
        all = change->anywhere;
        ok = all || wake_around(water, map, change->x, change->y, change->z);
    }
    for (uint32_t k = 1; k <= build && !all && ok; k++) {
        const MapBuildChange *change =
            &map->build_log[(water->build_seen + k) % MAP_BUILD_LOG_SIZE];
        ok = wake_around(water, map, change->x, change->y, change->z);
    }
    for (uint32_t k = 1; k <= poured && !all && ok; k++) {
        const TilePos *change = &map->water_log[(water->water_seen + k) % MAP_WATER_LOG_SIZE];
        clear_fine(water, map, change->x, change->y, change->z);
        ok = wake_around(water, map, change->x, change->y, change->z);
    }
    if (ok && all) {
        ok = water_wake_all(water, map);
    }
    if (ok) {
        water->topology_seen = map->topology_revision;
        water->build_seen = map->build_revision;
        water->water_seen = map->water_log_revision;
    }
    return ok;
}

bool water_init(Water *water, const Map *map)
{
    memset(water, 0, sizeof(*water));
    water->width = map->width;
    water->height = map->height;
    water->depth = map->depth;
    water->blocks_x = (map->width + WATER_BLOCK_SIZE - 1) / WATER_BLOCK_SIZE;
    water->blocks_y = (map->height + WATER_BLOCK_SIZE - 1) / WATER_BLOCK_SIZE;
    water->block_count =
        (uint32_t)water->blocks_x * (uint32_t)water->blocks_y * (uint32_t)water->depth;
    size_t count = water->block_count > 0 ? water->block_count : 1;
    water->slot = calloc(count, sizeof(*water->slot));
    water->free_entries = malloc(count * sizeof(*water->free_entries));
    water->active = malloc(count * sizeof(*water->active));
    water->woken = malloc(count * sizeof(*water->woken));
    water->fine = calloc(map_tile_count(map) > 0 ? map_tile_count(map) : 1, 1);
    if (water->slot == NULL || water->free_entries == NULL || water->active == NULL ||
        water->woken == NULL || water->fine == NULL) {
        water_free(water);
        return false;
    }
    water->list_capacity = (uint32_t)count;
    water->topology_seen = map->topology_revision;
    water->build_seen = map->build_revision;
    water->water_seen = map->water_log_revision;
    return true;
}

void water_free(Water *water)
{
    free(water->slot);
    free(water->pool);
    free(water->free_entries);
    free(water->active);
    free(water->woken);
    free(water->overfull);
    free(water->fine);
    memset(water, 0, sizeof(*water));
}

/* Whether the water of a tile (an index in bounds) falls through it: open
 * space or foliage of any density (leaves hold no water, even those one can
 * walk on). */
static bool floorless(const Map *map, size_t index)
{
    uint8_t terrain = map->terrain[index];
    return map->rock[index] == ROCK_NONE &&
           (terrain == TERRAIN_OPEN || terrain_is_foliage(terrain)) &&
           !map_stops_light_at(map, index);
}

/* The water of a tile (an index in bounds), in WATER_FINE units. */
static int32_t level_at(const Water *water, const Map *map, size_t index)
{
    return (int32_t)map->water[index] * WATER_FINE + water->fine[index];
}

static void set_level(Water *water, Map *map, size_t index, int32_t level)
{
    map->water[index] = (uint16_t)(level / WATER_FINE);
    water->fine[index] = (uint8_t)(level % WATER_FINE);
    water->fine_used |= water->fine[index] != 0;
}

#define FULL_FINE ((int32_t)WATER_CELL_FULL * WATER_FINE)
#define FILM_FINE ((int32_t)WATER_FILM_MM * WATER_FINE)

/* Whether a flow from a tile holding `held` to a neighbour holding `other`
 * (WATER_FINE units) would start, the pipe being still, more than a ripple
 * (two quiet blocks would wake each other in turn for less). */
static bool would_flow(int32_t held, int32_t other)
{
    return held >= FILM_FINE && (held - other) / WATER_FLOW_DIVISOR > WATER_QUIET_FLOW;
}

/* Wakes the sleeping blocks next to an active one that its water would reach,
 * or whose water would reach it. */
static bool wake_neighbours(Water *water, Map *map, uint32_t number)
{
    BlockPlace place = place_of(water, number);
    int x0, y0, x1, y1;
    block_bounds(water, place, &x0, &y0, &x1, &y1);
    static const int SIDES[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int side = 0; side < 4; side++) {
        int nbx = place.bx + SIDES[side][0], nby = place.by + SIDES[side][1];
        if (nbx < 0 || nby < 0 || nbx >= water->blocks_x || nby >= water->blocks_y ||
            water->slot[number_of(water, nbx, nby, place.z)] != 0) {
            continue;
        }
        /* The tiles along the shared edge: (x, y) ours, (x + dx, y + dy) theirs. */
        int dx = SIDES[side][0], dy = SIDES[side][1];
        int ex0 = dx > 0 ? x1 - 1 : x0, ex1 = dx < 0 ? x0 + 1 : x1;
        int ey0 = dy > 0 ? y1 - 1 : y0, ey1 = dy < 0 ? y0 + 1 : y1;
        const WaterBlock *block = &water->pool[water->slot[number] - 1];
        bool wake = false;
        for (int y = ey0; y < ey1 && !wake; y++) {
            for (int x = ex0; x < ex1 && !wake; x++) {
                size_t a = map_index(map, x, y, place.z);
                size_t b = map_index(map, x + dx, y + dy, place.z);
                if (map_stops_light_at(map, a) || map_stops_light_at(map, b)) {
                    continue;
                }
                int local = (y - y0) * WATER_BLOCK_SIZE + (x - x0);
                int32_t pipe = dx > 0 ? block->east[local] : dy > 0 ? block->south[local] : 0;
                int32_t la = level_at(water, map, a), lb = level_at(water, map, b);
                wake = pipe > WATER_QUIET_FLOW || pipe < -WATER_QUIET_FLOW || would_flow(la, lb) ||
                       would_flow(lb, la);
            }
        }
        if (wake && !wake_number(water, map, number_of(water, nbx, nby, place.z))) {
            return false;
        }
    }
    /* Above and below: water falling out of it or into it. */
    size_t level = map_level_tile_count(map);
    bool below_asleep =
        place.z > 0 && water->slot[number_of(water, place.bx, place.by, place.z - 1)] == 0;
    bool above_asleep = place.z + 1 < water->depth &&
                        water->slot[number_of(water, place.bx, place.by, place.z + 1)] == 0;
    bool wake_below = false, wake_above = false;
    for (int y = y0; y < y1 && (below_asleep || above_asleep); y++) {
        for (int x = x0; x < x1; x++) {
            size_t index = map_index(map, x, y, place.z);
            if (below_asleep && !wake_below && level_at(water, map, index) != 0 &&
                floorless(map, index) && !map_stops_light_at(map, index - level) &&
                level_at(water, map, index - level) < FULL_FINE) {
                wake_below = true;
            }
            if (above_asleep && !wake_above && level_at(water, map, index) < FULL_FINE &&
                !map_stops_light_at(map, index) && level_at(water, map, index + level) != 0 &&
                floorless(map, index + level)) {
                wake_above = true;
            }
        }
    }
    if (wake_below && !wake_number(water, map, number_of(water, place.bx, place.by, place.z - 1))) {
        return false;
    }
    if (wake_above && !wake_number(water, map, number_of(water, place.bx, place.by, place.z + 1))) {
        return false;
    }
    return true;
}

/* Water falls out of the floorless tiles of a block into the tiles below. */
static uint64_t fall(Water *water, Map *map, WaterBlock *block)
{
    BlockPlace place = place_of(water, block->number);
    if (place.z == 0) {
        return 0;
    }
    WaterBlock *below = block_get(water, place.bx, place.by, place.z - 1);
    if (below == NULL) {
        return 0;
    }
    int x0, y0, x1, y1;
    block_bounds(water, place, &x0, &y0, &x1, &y1);
    size_t level = map_level_tile_count(map);
    uint64_t moved = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            size_t index = map_index(map, x, y, place.z);
            int32_t held = level_at(water, map, index);
            if (held == 0 || !floorless(map, index) || map_stops_light_at(map, index - level)) {
                continue;
            }
            int32_t under = level_at(water, map, index - level);
            int32_t falls = held < FULL_FINE - under ? held : FULL_FINE - under;
            if (falls <= 0) {
                continue;
            }
            set_level(water, map, index, held - falls);
            set_level(water, map, index - level, under + falls);
            moved += (uint64_t)falls;
        }
    }
    if (moved > 0) {
        block->moved = true;
        below->moved = true;
    }
    return moved;
}

/* The new flow of a pipe from tile a to tile b (indices). */
static int32_t pipe_flow(const Water *water, const Map *map, int32_t flow, size_t a, size_t b)
{
    if (map_stops_light_at(map, b)) {
        return 0;
    }
    int32_t la = level_at(water, map, a), lb = level_at(water, map, b);
    flow = flow * WATER_FLOW_KEEP / 16 + (la - lb) / WATER_FLOW_DIVISOR;
    if ((flow > 0 && la < FILM_FINE) || (flow < 0 && lb < FILM_FINE)) {
        return 0;
    }
    return flow > FULL_FINE ? FULL_FINE : flow < -FULL_FINE ? -FULL_FINE : flow;
}

/* The pipes of a block's tiles, from the levels as they are. */
static void compute_flows(const Water *water, const Map *map, WaterBlock *block)
{
    BlockPlace place = place_of(water, block->number);
    int x0, y0, x1, y1;
    block_bounds(water, place, &x0, &y0, &x1, &y1);
    bool east_active = x1 < water->width && block_get(water, place.bx + 1, place.by, place.z);
    bool south_active = y1 < water->height && block_get(water, place.bx, place.by + 1, place.z);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            size_t a = map_index(map, x, y, place.z);
            int local = (y - y0) * WATER_BLOCK_SIZE + (x - x0);
            if (map_stops_light_at(map, a)) {
                block->east[local] = 0;
                block->south[local] = 0;
                continue;
            }
            bool east = x + 1 < x1 || east_active;
            bool south = y + 1 < y1 || south_active;
            block->east[local] = east ? pipe_flow(water, map, block->east[local], a, a + 1) : 0;
            block->south[local] =
                south ? pipe_flow(water, map, block->south[local], a, a + (size_t)map->width) : 0;
        }
    }
}

/* The four pipes of a tile: east and south its own (in `block`), west and
 * north those of its neighbours (NULL where there are none, or they belong to
 * a sleeping block). */
typedef struct TilePipes {
    int32_t *east, *south, *west, *north;
} TilePipes;

static TilePipes tile_pipes(WaterBlock *block, int lx, int ly, WaterBlock *west_block,
                            WaterBlock *north_block)
{
    int local = ly * WATER_BLOCK_SIZE + lx;
    TilePipes pipes = {&block->east[local], &block->south[local], NULL, NULL};
    if (lx > 0) {
        pipes.west = &block->east[local - 1];
    } else if (west_block != NULL) {
        pipes.west = &west_block->east[local + WATER_BLOCK_SIZE - 1];
    }
    if (ly > 0) {
        pipes.north = &block->south[local - WATER_BLOCK_SIZE];
    } else if (north_block != NULL) {
        pipes.north = &north_block->south[local + WATER_BLOCK_TILES - WATER_BLOCK_SIZE];
    }
    return pipes;
}

/* Scales a pipe whose water leaves the tile down by held / out. */
static void scale_out(int32_t *pipe, bool outgoing_positive, int32_t held, int32_t out)
{
    int64_t flow = *pipe;
    if (outgoing_positive && flow > 0) {
        *pipe = (int32_t)(flow * held / out);
    } else if (!outgoing_positive && flow < 0) {
        *pipe = (int32_t)(-(-flow * held / out));
    }
}

/* What leaves each tile of a block is scaled down to what it holds. A pipe
 * has a single source, so the order of the tiles does not matter. */
static void scale_flows(const Water *water, const Map *map, WaterBlock *block)
{
    BlockPlace place = place_of(water, block->number);
    int x0, y0, x1, y1;
    block_bounds(water, place, &x0, &y0, &x1, &y1);
    WaterBlock *west_block = block_get(water, place.bx - 1, place.by, place.z);
    WaterBlock *north_block = block_get(water, place.bx, place.by - 1, place.z);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            TilePipes pipes = tile_pipes(block, x - x0, y - y0, west_block, north_block);
            int32_t out = (*pipes.east > 0 ? *pipes.east : 0) +
                          (*pipes.south > 0 ? *pipes.south : 0) +
                          (pipes.west != NULL && *pipes.west < 0 ? -*pipes.west : 0) +
                          (pipes.north != NULL && *pipes.north < 0 ? -*pipes.north : 0);
            int32_t held = level_at(water, map, map_index(map, x, y, place.z));
            if (out <= held) {
                continue;
            }
            scale_out(pipes.east, true, held, out);
            scale_out(pipes.south, true, held, out);
            if (pipes.west != NULL) {
                scale_out(pipes.west, false, held, out);
            }
            if (pipes.north != NULL) {
                scale_out(pipes.north, false, held, out);
            }
        }
    }
}

/* Moves the water along the pipes; notes the tiles past full. */
static bool apply_flows(Water *water, Map *map, WaterBlock *block, uint64_t *moved)
{
    BlockPlace place = place_of(water, block->number);
    int x0, y0, x1, y1;
    block_bounds(water, place, &x0, &y0, &x1, &y1);
    WaterBlock *west_block = block_get(water, place.bx - 1, place.by, place.z);
    WaterBlock *north_block = block_get(water, place.bx, place.by - 1, place.z);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            TilePipes pipes = tile_pipes(block, x - x0, y - y0, west_block, north_block);
            int32_t change = -*pipes.east - *pipes.south + (pipes.west != NULL ? *pipes.west : 0) +
                             (pipes.north != NULL ? *pipes.north : 0);
            int32_t east = *pipes.east > 0 ? *pipes.east : -*pipes.east;
            int32_t south = *pipes.south > 0 ? *pipes.south : -*pipes.south;
            *moved += (uint64_t)east + (uint64_t)south;
            if (east > WATER_QUIET_FLOW || south > WATER_QUIET_FLOW) {
                block->moved = true;
            }
            if (change == 0) {
                continue;
            }
            size_t index = map_index(map, x, y, place.z);
            int32_t held = level_at(water, map, index) + change;
            const int32_t most = (int32_t)UINT16_MAX * WATER_FINE + WATER_FINE - 1;
            held = held < 0 ? 0 : held > most ? most : held; /* Never: the flows are scaled. */
            set_level(water, map, index, held);
            if (change > WATER_QUIET_FLOW || change < -WATER_QUIET_FLOW) {
                block->moved = true;
            }
            if (held > FULL_FINE) {
                if (water->overfull_count == water->overfull_capacity) {
                    uint32_t capacity =
                        water->overfull_capacity > 0 ? water->overfull_capacity * 2 : 64;
                    size_t *list = realloc(water->overfull, capacity * sizeof(*list));
                    if (list == NULL) {
                        return false;
                    }
                    water->overfull = list;
                    water->overfull_capacity = capacity;
                }
                water->overfull[water->overfull_count++] = index;
            }
        }
    }
    return true;
}

/* A tile past full passes what it has too much to the open tile above it. */
static bool pass_up(Water *water, Map *map, size_t index)
{
    int x, y, z;
    map_tile_of(map, index, &x, &y, &z);
    if (z + 1 >= map->depth) {
        return true;
    }
    size_t above = index + map_level_tile_count(map);
    int32_t held = level_at(water, map, index);
    int32_t over = level_at(water, map, above);
    int32_t excess = held - FULL_FINE, room = FULL_FINE - over;
    if (excess <= 0 || room <= 0 || !floorless(map, above)) {
        return true;
    }
    if (!water_wake(water, map, x, y, z + 1)) {
        return false;
    }
    int32_t passed = excess < room ? excess : room;
    set_level(water, map, index, held - passed);
    set_level(water, map, above, over + passed);
    water->pool[water->slot[water_block_of(water, x, y, z + 1)] - 1].moved = true;
    return true;
}

bool water_step(Water *water, Map *map)
{
    if (map->width != water->width || map->height != water->height || map->depth != water->depth) {
        return false;
    }
    water->steps++;
    water->moved_last_step = 0;
    if (!read_logs(water, map)) {
        return false;
    }
    merge_woken(water);
    if (water->active_count == 0) {
        return true;
    }
    for (uint32_t i = 0; i < water->active_count; i++) {
        if (!wake_neighbours(water, map, water->active[i])) {
            merge_woken(water);
            return false;
        }
    }
    merge_woken(water);
    uint64_t moved = 0;
    for (uint32_t i = 0; i < water->active_count; i++) {
        water->pool[water->slot[water->active[i]] - 1].moved = false;
    }
    for (uint32_t i = 0; i < water->active_count; i++) {
        moved += fall(water, map, &water->pool[water->slot[water->active[i]] - 1]);
    }
    for (uint32_t i = 0; i < water->active_count; i++) {
        compute_flows(water, map, &water->pool[water->slot[water->active[i]] - 1]);
    }
    for (uint32_t i = 0; i < water->active_count; i++) {
        scale_flows(water, map, &water->pool[water->slot[water->active[i]] - 1]);
    }
    water->overfull_count = 0;
    bool ok = true;
    for (uint32_t i = 0; i < water->active_count && ok; i++) {
        ok = apply_flows(water, map, &water->pool[water->slot[water->active[i]] - 1], &moved);
    }
    for (uint32_t i = 0; i < water->overfull_count && ok; i++) {
        ok = pass_up(water, map, water->overfull[i]);
    }
    merge_woken(water);
    water->moved_last_step = moved / WATER_FINE;
    /* Sleep, and tell the map of the classes: its topology log then holds our
     * own changes, which need not wake anything. */
    uint32_t topology_before = map->topology_revision;
    bool report = water->steps % WATER_CLASS_MIN_TICKS == 0;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < water->active_count; i++) {
        uint32_t number = water->active[i];
        uint32_t entry = water->slot[number] - 1;
        WaterBlock *block = &water->pool[entry];
        block->quiet = block->moved ? 0 : (uint16_t)(block->quiet + 1);
        if (block->quiet >= WATER_SLEEP_STEPS) {
            block_report_classes(water, map, block);
            water->slot[number] = 0;
            water->free_entries[water->free_count++] = entry;
            continue;
        }
        if (report) {
            block_report_classes(water, map, block);
        }
        water->active[kept++] = number;
    }
    water->active_count = kept;
    if (water->topology_seen == topology_before) {
        water->topology_seen = map->topology_revision;
    }
    return ok;
}

/* Whether a tile holding `held` would send a neighbour holding `other` at
 * least a film's difference (WATER_FINE units). */
static bool would_spill(int32_t held, int32_t other)
{
    return held >= FILM_FINE && held - other >= FILM_FINE;
}

/* Whether the water of a tile (an index in bounds, at x, y, z) holding
 * `held` would move: fall through it, or flow to or from a neighbour by a
 * film's difference at least. Rain falling alike on flat ground or on a
 * lake moves nothing, nor the tenths of a millimetre the sun takes more
 * from one tile than from the next (they even out when the block wakes for
 * something else). */
static bool would_move(const Water *water, const Map *map, size_t index, int32_t held, int x, int y,
                       int z)
{
    if (map_stops_light_at(map, index)) {
        return false;
    }
    size_t level = map_level_tile_count(map);
    if (held > 0 && z > 0 && floorless(map, index) && !map_stops_light_at(map, index - level) &&
        level_at(water, map, index - level) < FULL_FINE) {
        return true;
    }
    static const int SIDES[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int side = 0; side < 4; side++) {
        int nx = x + SIDES[side][0], ny = y + SIDES[side][1];
        if (!map_in_bounds(map, nx, ny, z)) {
            continue;
        }
        size_t other = map_index(map, nx, ny, z);
        if (map_stops_light_at(map, other)) {
            continue;
        }
        int32_t beside = level_at(water, map, other);
        if (would_spill(held, beside) || would_spill(beside, held)) {
            return true;
        }
    }
    return false;
}

size_t water_surface_index(const Map *map, size_t index)
{
    size_t level = map_level_tile_count(map), count = map_tile_count(map);
    while (map->water[index] >= WATER_CELL_FULL && index + level < count &&
           floorless(map, index + level)) {
        index += level;
    }
    return index;
}

float water_mm_at(const Water *water, const Map *map, size_t index)
{
    return (float)level_at(water, map, index) * (1.0f / (float)WATER_FINE);
}

int32_t water_exchange(Water *water, Map *map, size_t index, int32_t fine)
{
    if (fine == 0 || map->rock[index] != ROCK_NONE) {
        return 0;
    }
    int32_t before = level_at(water, map, index);
    const int32_t most = (int32_t)UINT16_MAX * WATER_FINE + WATER_FINE - 1;
    int64_t wanted = (int64_t)before + fine;
    int32_t after = wanted < 0 ? 0 : wanted > most ? most : (int32_t)wanted;
    if (after == before) {
        return 0;
    }
    bool was_shallow = !tile_deep(map, index);
    bool was_water = map_is_water_at(map, index);
    set_level(water, map, index, after);
    int x, y, z;
    map_tile_of(map, index, &x, &y, &z);
    uint32_t number = water_block_of(water, x, y, z);
    if (water->slot[number] != 0) {
        return after - before; /* Active: the flow tells the map. */
    }
    bool shallow = !tile_deep(map, index);
    bool open_water = map_is_water_at(map, index);
    if (shallow != was_shallow || open_water != was_water) {
        uint32_t topology_before = map->topology_revision;
        map_water_class_changed(map, x, y, z,
                                shallow != was_shallow && tile_walkable_dry(map, index), shallow,
                                open_water != was_water);
        if (water->topology_seen == topology_before) {
            water->topology_seen = map->topology_revision; /* Our own: wakes nothing. */
        }
    }
    /* Only water the exchange sets moving wakes: water asleep beside lower
     * ground (a lake on the level of its banks in a flat world) stays held
     * as it was (docs/WATER.md §3.5). */
    if (would_move(water, map, index, after, x, y, z) &&
        !would_move(water, map, index, before, x, y, z)) {
        (void)wake_number(water, map, number); /* Merged at the next step. */
    }
    return after - before;
}

void water_settle(Water *water, Map *map)
{
    merge_woken(water);
    for (uint32_t i = 0; i < water->active_count; i++) {
        uint32_t number = water->active[i];
        uint32_t entry = water->slot[number] - 1;
        block_report_classes(water, map, &water->pool[entry]);
        water->slot[number] = 0;
        water->free_entries[water->free_count++] = entry;
    }
    water->active_count = 0;
    water->topology_seen = map->topology_revision;
    water->build_seen = map->build_revision;
    water->water_seen = map->water_log_revision;
}

uint64_t water_total(const Water *water, const Map *map)
{
    uint64_t total = 0;
    size_t count = map_tile_count(map);
    for (size_t i = 0; i < count; i++) {
        total += (uint64_t)map->water[i] * WATER_FINE + water->fine[i];
    }
    return total;
}

uint64_t water_hash(const Water *water, uint64_t hash)
{
    /* Water at rest as laid: hashes stay as they were. */
    if (water->fine_used) {
        hash = hash_fnv1a64(hash, water->fine,
                            (size_t)water->width * (size_t)water->height * (size_t)water->depth);
    }
    if (water->active_count == 0) {
        return hash;
    }
    hash = hash_fnv1a64(hash, &water->active_count, sizeof(water->active_count));
    for (uint32_t i = 0; i < water->active_count; i++) {
        const WaterBlock *block = &water->pool[water->slot[water->active[i]] - 1];
        hash = hash_fnv1a64(hash, &block->number, sizeof(block->number));
        hash = hash_fnv1a64(hash, &block->quiet, sizeof(block->quiet));
        hash = hash_fnv1a64(hash, block->east, sizeof(block->east));
        hash = hash_fnv1a64(hash, block->south, sizeof(block->south));
    }
    return hash;
}

static void water_block_serialize(Serializer *s, WaterBlock *block)
{
    for (int i = 0; i < WATER_BLOCK_TILES; i++) {
        ser_i32(s, &block->east[i]);
    }
    for (int i = 0; i < WATER_BLOCK_TILES; i++) {
        ser_i32(s, &block->south[i]);
    }
    ser_u64s(s, block->passable, WATER_BLOCK_TILES / 64);
    ser_u64s(s, block->open_water, WATER_BLOCK_TILES / 64);
    ser_u16(s, &block->quiet);
    ser_bool(s, &block->moved);
}

/* The blocks of a list (active or woken), their contents after their
 * numbers; when reading, each given an entry of the pool. */
static void block_list_serialize(Serializer *s, Water *water, uint32_t *list, uint32_t *count)
{
    ser_u32_upto(s, count, water->block_count);
    ser_u32s(s, list, *count);
    for (uint32_t i = 0; i < *count && ser_ok(s); i++) {
        uint32_t number = list[i];
        if (s->reading) {
            if (!ser_check(s, number < water->block_count && water->slot[number] == 0)) {
                return;
            }
            if (water->pool_used == water->pool_capacity) {
                uint32_t capacity = water->pool_capacity > 0 ? water->pool_capacity * 2 : 16;
                capacity = capacity < water->block_count ? capacity : water->block_count;
                WaterBlock *pool = realloc(water->pool, (size_t)capacity * sizeof(*pool));
                if (!ser_check(s, pool != NULL)) {
                    return;
                }
                water->pool = pool;
                water->pool_capacity = capacity;
            }
            uint32_t entry = water->pool_used++;
            memset(&water->pool[entry], 0, sizeof(water->pool[entry]));
            water->pool[entry].number = number;
            water->slot[number] = entry + 1;
        }
        water_block_serialize(s, &water->pool[water->slot[number] - 1]);
    }
}

void water_serialize(Serializer *s, Water *water)
{
    size_t tiles = (size_t)water->width * (size_t)water->height * (size_t)water->depth;
    if (s->reading) { /* As after water_init(): every block asleep. */
        memset(water->slot, 0, (size_t)water->block_count * sizeof(*water->slot));
        water->pool_used = water->free_count = 0;
        water->active_count = water->woken_count = 0;
        water->overfull_count = 0;
    }
    ser_bool(s, &water->fine_used);
    ser_layer_u8(s, water->fine, tiles);
    block_list_serialize(s, water, water->active, &water->active_count);
    block_list_serialize(s, water, water->woken, &water->woken_count);
    uint32_t overfull = water->overfull_count;
    ser_u32_upto(s, &overfull, (uint32_t)(tiles < UINT32_MAX ? tiles : UINT32_MAX));
    if (s->reading && overfull > 0) {
        if (!ser_alloc(s, (void **)&water->overfull, overfull, sizeof(*water->overfull), tiles)) {
            return;
        }
        water->overfull_capacity = overfull;
    }
    for (uint32_t i = 0; i < overfull && ser_ok(s); i++) {
        ser_size(s, &water->overfull[i]);
        ser_check(s, water->overfull[i] < tiles);
    }
    water->overfull_count = ser_ok(s) ? overfull : 0;
    ser_u32(s, &water->topology_seen);
    ser_u32(s, &water->build_seen);
    ser_u32(s, &water->water_seen);
    ser_u64(s, &water->steps);
    ser_u64(s, &water->wakes);
    ser_u64(s, &water->wake_alls);
    ser_u64(s, &water->moved_last_step);
}
