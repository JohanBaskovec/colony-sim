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

static void edges_from_map(Water *water, const Map *map);

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
    size_t edges = 2 * (size_t)map->width + 2 * (size_t)map->height;
    water->edge_ground = calloc(edges > 0 ? edges : 1, sizeof(*water->edge_ground));
    water->edge_surface = calloc(edges > 0 ? edges : 1, sizeof(*water->edge_surface));
    water->edge_kind = calloc(edges > 0 ? edges : 1, sizeof(*water->edge_kind));
    if (water->slot == NULL || water->free_entries == NULL || water->active == NULL ||
        water->woken == NULL || water->fine == NULL || water->edge_ground == NULL ||
        water->edge_surface == NULL || water->edge_kind == NULL) {
        water_free(water);
        return false;
    }
    edges_from_map(water, map);
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
    free(water->edge_ground);
    free(water->edge_surface);
    free(water->edge_kind);
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
/* What a tile's water (or a block's) may change by over its quiet steps,
 * flowing in and out, and still count as quiet: a ripple's flow over
 * WATER_SLEEP_STEPS steps. */
#define QUIET_NET ((int32_t)WATER_QUIET_FLOW * WATER_SLEEP_STEPS)
/* A full tile in the units of Water.edge_surface. */
#define FULL_BASIN ((int64_t)FULL_FINE * WATER_EDGE_BASIN)

/* The column of the land beyond the edge next to the edge tile (x, y), on the
 * side (dx, dy): its slot in Water.edge_ground and edge_surface (north row,
 * south row, west column, east column), SIZE_MAX when the tile is not on
 * that edge. */
static size_t edge_slot(const Water *water, int x, int y, int dx, int dy)
{
    size_t width = (size_t)water->width, height = (size_t)water->height;
    if (dx == 0 && dy < 0 && y == 0) {
        return (size_t)x;
    }
    if (dx == 0 && dy > 0 && y == water->height - 1) {
        return width + (size_t)x;
    }
    if (dy == 0 && dx < 0 && x == 0) {
        return 2 * width + (size_t)y;
    }
    if (dy == 0 && dx > 0 && x == water->width - 1) {
        return 2 * width + height + (size_t)y;
    }
    return SIZE_MAX;
}

/* The water of the tile beyond the edge at level z (WATER_FINE units), -1
 * where it is solid (under the ground of the land beyond). */
static int32_t beyond(const Water *water, size_t slot, int z)
{
    if (slot == SIZE_MAX || z < water->edge_ground[slot]) {
        return -1;
    }
    int64_t level = (water->edge_surface[slot] - (int64_t)z * FULL_BASIN) / WATER_EDGE_BASIN;
    return level < 0 ? 0 : level > FULL_FINE ? FULL_FINE : (int32_t)level;
}

/* Moves the surface of the basin beyond an edge column (units of
 * Water.edge_surface), never under its ground nor over the top. */
static void move_edge_surface(Water *water, size_t slot, int64_t change)
{
    if (water->edge_kind[slot] != WATER_EDGE_LAND) {
        return; /* A river's ends: their levels are set from outside. */
    }
    int64_t surface = water->edge_surface[slot] + change;
    int64_t lowest = (int64_t)water->edge_ground[slot] * FULL_BASIN;
    int64_t highest = (int64_t)water->depth * FULL_BASIN;
    water->edge_surface[slot] = surface < lowest ? lowest : surface > highest ? highest : surface;
}

void water_seal_edges(Water *water)
{
    water->sealed = true;
    size_t edges = 2 * (size_t)water->width + 2 * (size_t)water->height;
    for (size_t i = 0; i < edges; i++) {
        water->edge_ground[i] = (uint8_t)water->depth;
        water->edge_surface[i] = (int64_t)water->depth * FULL_BASIN;
        water->edge_kind[i] = WATER_EDGE_LAND;
    }
}

size_t water_edge_slot(const Water *water, int x, int y, int dx, int dy)
{
    if (x < 0 || y < 0 || x >= water->width || y >= water->height) {
        return SIZE_MAX;
    }
    return edge_slot(water, x, y, dx, dy);
}

/* The edge tile next to a column of the edge. */
static void edge_tile_of(const Water *water, size_t slot, int *x, int *y)
{
    size_t width = (size_t)water->width, height = (size_t)water->height;
    if (slot < width) {
        *x = (int)slot, *y = 0;
    } else if (slot < 2 * width) {
        *x = (int)(slot - width), *y = water->height - 1;
    } else if (slot < 2 * width + height) {
        *x = 0, *y = (int)(slot - 2 * width);
    } else {
        *x = water->width - 1, *y = (int)(slot - 2 * width - height);
    }
}

void water_set_edge(Water *water, const Map *map, size_t slot, WaterEdgeKind kind, int64_t surface)
{
    size_t edges = 2 * (size_t)water->width + 2 * (size_t)water->height;
    if (slot >= edges || (unsigned)kind >= WATER_EDGE_KIND_COUNT || water->sealed) {
        return;
    }
    water->edge_kind[slot] = WATER_EDGE_LAND; /* So that the surface is held in range. */
    water->edge_surface[slot] = (int64_t)water->edge_ground[slot] * FULL_BASIN;
    move_edge_surface(water, slot, surface * WATER_EDGE_BASIN - water->edge_surface[slot]);
    water->edge_kind[slot] = (uint8_t)kind;
    water->fine_used = true; /* Hashed from now on, with the edges. */
    /* Wake the edge tile where water would cross. */
    int x, y;
    edge_tile_of(water, slot, &x, &y);
    for (int z = water->edge_ground[slot]; z < water->depth; z++) {
        int32_t outside = beyond(water, slot, z);
        size_t index = map_index(map, x, y, z);
        int32_t held = level_at(water, map, index);
        if (outside > 0 || held > 0) {
            (void)wake_number(water, map, water_block_of(water, x, y, z));
        }
        if (outside < FULL_FINE && held < FULL_FINE) {
            break;
        }
    }
}

int32_t water_beyond_edge(const Water *water, int x, int y, int z, int dx, int dy)
{
    if (x < 0 || y < 0 || z < 0 || x >= water->width || y >= water->height || z >= water->depth) {
        return -1;
    }
    return beyond(water, edge_slot(water, x, y, dx, dy), z);
}

/* The land beyond an edge column as the column is: its ground, and the
 * surface of the water standing on it (full tiles carry the next). A column
 * of rock up to the top is solid all the way (its ground: the map's depth). */
static void edge_column(Water *water, const Map *map, size_t slot, int x, int y)
{
    int ground = map_ground_level(map, x, y);
    size_t level = map_level_tile_count(map);
    size_t index = map_index(map, x, y, ground);
    uint8_t rock = map->rock[index];
    if (water->edge_kind[slot] != WATER_EDGE_LAND) {
        return; /* A river's end stays as set. */
    }
    if (water->sealed || (rock != ROCK_NONE && rock != ROCK_WOOD)) {
        water->edge_ground[slot] = (uint8_t)map->depth;
        water->edge_surface[slot] = (int64_t)map->depth * FULL_BASIN;
        return;
    }
    int64_t surface = (int64_t)ground * FULL_FINE;
    for (int z = ground; z < map->depth; z++, index += level) {
        int32_t held = level_at(water, map, index);
        surface += held < FULL_FINE ? held : FULL_FINE;
        if (held < FULL_FINE || z + 1 >= map->depth || !floorless(map, index + level)) {
            break;
        }
    }
    water->edge_ground[slot] = (uint8_t)ground;
    water->edge_surface[slot] = surface * WATER_EDGE_BASIN;
}

/* The land beyond every edge, from the map as it is. */
static void edges_from_map(Water *water, const Map *map)
{
    size_t width = (size_t)water->width, height = (size_t)water->height;
    for (int x = 0; x < water->width; x++) {
        edge_column(water, map, (size_t)x, x, 0);
        edge_column(water, map, width + (size_t)x, x, water->height - 1);
    }
    for (int y = 0; y < water->height; y++) {
        edge_column(water, map, 2 * width + (size_t)y, 0, y);
        edge_column(water, map, 2 * width + height + (size_t)y, water->width - 1, y);
    }
}

/*
 * The new flow of the pipe from tile a out to the tile beyond the edge at the
 * column `slot`, level z (none where the ground is solid there); positive
 * out of a. What lies beyond gives a step at most what it holds.
 *
 * Land beyond (a basin) answers only to what goes past a film's difference:
 * the tenths of a millimetre the sun takes more from one edge tile than from
 * the next would otherwise keep a current running along the edge, in by one
 * tile and out by the next, for ever. Past it the current grows from nothing
 * (from a ripple), so that it eases off smoothly as the levels near each
 * other: a current switched on and off at the film by a lake slowly rising
 * would keep the water awake. A river's source and outlet carry its gentle
 * slope whatever it is.
 */
static int32_t edge_pipe(const Water *water, const Map *map, int32_t flow, size_t a, size_t slot,
                         int z)
{
    int32_t outside = beyond(water, slot, z);
    if (outside < 0) {
        return 0;
    }
    int32_t la = level_at(water, map, a);
    int32_t difference = la - outside;
    if (water->edge_kind[slot] == WATER_EDGE_LAND) {
        const int32_t still = FILM_FINE + WATER_QUIET_FLOW * WATER_FLOW_DIVISOR;
        difference = difference > still    ? difference - FILM_FINE
                     : difference < -still ? difference + FILM_FINE
                                           : 0;
    }
    flow = flow * WATER_FLOW_KEEP / 16 + difference / WATER_FLOW_DIVISOR;
    if ((flow > 0 && la < FILM_FINE) || (flow < 0 && outside < FILM_FINE)) {
        return 0;
    }
    flow = flow < -outside ? -outside : flow;
    return flow > FULL_FINE ? FULL_FINE : flow;
}

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

/* Whether a pipe's flow changed by more than a ripple: a current that holds
 * steady is water at rest as much as still water is. */
static bool flow_changed(int32_t was, int32_t now)
{
    return now - was > WATER_QUIET_FLOW || was - now > WATER_QUIET_FLOW;
}

/* The pipes of a block's tiles, from the levels as they are; notes the
 * block moved where a flow changed. */
static void compute_flows(const Water *water, const Map *map, WaterBlock *block)
{
    BlockPlace place = place_of(water, block->number);
    int x0, y0, x1, y1;
    block_bounds(water, place, &x0, &y0, &x1, &y1);
    bool east_active = x1 < water->width && block_get(water, place.bx + 1, place.by, place.z);
    bool south_active = y1 < water->height && block_get(water, place.bx, place.by + 1, place.z);
    bool on_edge = x0 == 0 || y0 == 0 || x1 == water->width || y1 == water->height;
    for (int ly = 0; ly < WATER_BLOCK_SIZE && y0 + ly < y1; ly++) {
        for (int lx = 0; lx < WATER_BLOCK_SIZE && x0 + lx < x1; lx++) {
            int x = x0 + lx, y = y0 + ly;
            size_t a = map_index(map, x, y, place.z);
            int local = ly * WATER_BLOCK_SIZE + lx;
            if (map_stops_light_at(map, a)) {
                block->east[local] = 0;
                block->south[local] = 0;
                if (x == 0) {
                    block->west_edge[ly] = 0;
                }
                if (y == 0) {
                    block->north_edge[lx] = 0;
                }
                continue;
            }
            bool east = x + 1 < x1 || east_active;
            bool south = y + 1 < y1 || south_active;
            int32_t was_east = block->east[local], was_south = block->south[local];
            block->east[local] = east ? pipe_flow(water, map, block->east[local], a, a + 1) : 0;
            block->south[local] =
                south ? pipe_flow(water, map, block->south[local], a, a + (size_t)map->width) : 0;
            if (on_edge) {
                /* The pipes out to the land beyond the edges. */
                if (x + 1 == water->width) {
                    block->east[local] = edge_pipe(water, map, block->east[local], a,
                                                   edge_slot(water, x, y, 1, 0), place.z);
                }
                if (y + 1 == water->height) {
                    block->south[local] = edge_pipe(water, map, block->south[local], a,
                                                    edge_slot(water, x, y, 0, 1), place.z);
                }
                if (x == 0) {
                    int32_t was = block->west_edge[ly];
                    block->west_edge[ly] = -edge_pipe(water, map, -block->west_edge[ly], a,
                                                      edge_slot(water, x, y, -1, 0), place.z);
                    block->moved |= flow_changed(was, block->west_edge[ly]);
                }
                if (y == 0) {
                    int32_t was = block->north_edge[lx];
                    block->north_edge[lx] = -edge_pipe(water, map, -block->north_edge[lx], a,
                                                       edge_slot(water, x, y, 0, -1), place.z);
                    block->moved |= flow_changed(was, block->north_edge[lx]);
                }
            }
            block->moved |= flow_changed(was_east, block->east[local]) ||
                            flow_changed(was_south, block->south[local]);
        }
    }
}

/* The four pipes of a tile: east and south its own (in `block`), west and
 * north those of its neighbours, or the block's own from beyond the map's
 * west and north edges (NULL where there are none, or they belong to a
 * sleeping block). Each points the same way as an east or south pipe. */
typedef struct TilePipes {
    int32_t *east, *south, *west, *north;
} TilePipes;

/* The neighbours of a block: the active blocks west and north of it (NULL
 * when asleep), and whether it lies along the map's west or north edge. */
typedef struct BlockSides {
    WaterBlock *west, *north;
    bool west_edge, north_edge;
} BlockSides;

static BlockSides block_sides(const Water *water, BlockPlace place)
{
    BlockSides sides = {block_get(water, place.bx - 1, place.by, place.z),
                        block_get(water, place.bx, place.by - 1, place.z), place.bx == 0,
                        place.by == 0};
    return sides;
}

static TilePipes tile_pipes(WaterBlock *block, int lx, int ly, BlockSides sides)
{
    int local = ly * WATER_BLOCK_SIZE + lx;
    TilePipes pipes = {&block->east[local], &block->south[local], NULL, NULL};
    if (lx > 0) {
        pipes.west = &block->east[local - 1];
    } else if (sides.west != NULL) {
        pipes.west = &sides.west->east[local + WATER_BLOCK_SIZE - 1];
    } else if (sides.west_edge) {
        pipes.west = &block->west_edge[ly];
    }
    if (ly > 0) {
        pipes.north = &block->south[local - WATER_BLOCK_SIZE];
    } else if (sides.north != NULL) {
        pipes.north = &sides.north->south[local + WATER_BLOCK_TILES - WATER_BLOCK_SIZE];
    } else if (sides.north_edge) {
        pipes.north = &block->north_edge[lx];
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
    BlockSides sides = block_sides(water, place);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            TilePipes pipes = tile_pipes(block, x - x0, y - y0, sides);
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
    BlockSides sides = block_sides(water, place);
    /* In through the edges, from the basins beyond (each moves by what
     * crosses, in its units). */
    int64_t through = 0;
    int last_x = x1 - 1 - x0, last_y = y1 - 1 - y0;
    size_t width = (size_t)water->width, height = (size_t)water->height;
    for (int i = 0; i < WATER_BLOCK_SIZE; i++) {
        if (x0 == 0 && i <= last_y && block->west_edge[i] != 0) {
            through += block->west_edge[i];
            move_edge_surface(water, 2 * width + (size_t)(y0 + i), -block->west_edge[i]);
        }
        if (y0 == 0 && i <= last_x && block->north_edge[i] != 0) {
            through += block->north_edge[i];
            move_edge_surface(water, (size_t)(x0 + i), -block->north_edge[i]);
        }
        int32_t out =
            x1 == water->width && i <= last_y ? block->east[i * WATER_BLOCK_SIZE + last_x] : 0;
        if (out != 0) {
            through -= out;
            move_edge_surface(water, 2 * width + height + (size_t)(y0 + i), out);
        }
        out = y1 == water->height && i <= last_x ? block->south[last_y * WATER_BLOCK_SIZE + i] : 0;
        if (out != 0) {
            through -= out;
            move_edge_surface(water, width + (size_t)(x0 + i), out);
        }
    }
    water->edge_in += through;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            TilePipes pipes = tile_pipes(block, x - x0, y - y0, sides);
            int32_t change = -*pipes.east - *pipes.south + (pipes.west != NULL ? *pipes.west : 0) +
                             (pipes.north != NULL ? *pipes.north : 0);
            int32_t east = *pipes.east > 0 ? *pipes.east : -*pipes.east;
            int32_t south = *pipes.south > 0 ? *pipes.south : -*pipes.south;
            *moved += (uint64_t)east + (uint64_t)south;
            if (change == 0) {
                continue;
            }
            int local = (y - y0) * WATER_BLOCK_SIZE + (x - x0);
            block->net[local] += change;
            size_t index = map_index(map, x, y, place.z);
            int32_t held = level_at(water, map, index) + change;
            const int32_t most = (int32_t)UINT16_MAX * WATER_FINE + WATER_FINE - 1;
            held = held < 0 ? 0 : held > most ? most : held; /* Never: the flows are scaled. */
            set_level(water, map, index, held);
            /* Ripples come and go (the sun or rain on a tile, the water
             * around evening it out); what moves on adds up. */
            if (block->net[local] > QUIET_NET || block->net[local] < -QUIET_NET) {
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

static bool block_ready(WaterBlock *block);
static void hold_ready_blocks(Water *water);

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
    for (uint32_t i = 0; i < water->active_count; i++) {
        WaterBlock *block = &water->pool[water->slot[water->active[i]] - 1];
        block->quiet = block->moved ? 0 : (uint16_t)(block->quiet + 1);
        if (block->moved) {
            memset(block->net, 0, sizeof(block->net));
        }
        block->ready = block_ready(block);
    }
    hold_ready_blocks(water);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < water->active_count; i++) {
        uint32_t number = water->active[i];
        uint32_t entry = water->slot[number] - 1;
        WaterBlock *block = &water->pool[entry];
        if (block->ready) {
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

/* Whether a pipe of `count` along a block's side, `stride` apart, carries
 * more than a ripple. */
static bool side_flows(const int32_t *pipe, int count, int stride)
{
    for (int i = 0; i < count; i++, pipe += stride) {
        if (*pipe > WATER_QUIET_FLOW || *pipe < -WATER_QUIET_FLOW) {
            return true;
        }
    }
    return false;
}

/* Whether a block is ready to sleep: quiet for WATER_SLEEP_STEPS (no flow
 * changed and no tile's water changed by more than ripples), and having
 * gained or lost no more than ripples over those steps (else a lake draining
 * slowly through a channel would stop half drained). A block quiet long
 * enough but not steady starts its quiet steps again. */
static bool block_ready(WaterBlock *block)
{
    if (block->quiet < WATER_SLEEP_STEPS) {
        return false;
    }
    int64_t gained = 0;
    for (int i = 0; i < WATER_BLOCK_TILES; i++) {
        gained += block->net[i];
    }
    if (gained > QUIET_NET || gained < -QUIET_NET) {
        block->quiet = 0;
        memset(block->net, 0, sizeof(block->net));
        return false;
    }
    return true;
}

/* The active block next to a block on one side (0 east, 1 west, 2 south,
 * 3 north) when the pipes between them carry more than a ripple: the two
 * trade a current. NULL otherwise. */
static WaterBlock *current_neighbour(const Water *water, const WaterBlock *block, int side)
{
    static const int SIDES[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    BlockPlace place = place_of(water, block->number);
    WaterBlock *other =
        block_get(water, place.bx + SIDES[side][0], place.by + SIDES[side][1], place.z);
    if (other == NULL) {
        return NULL;
    }
    int x0, y0, x1, y1;
    block_bounds(water, place, &x0, &y0, &x1, &y1);
    int columns = x1 - x0, rows = y1 - y0;
    const int n = WATER_BLOCK_SIZE;
    bool flows;
    switch (side) {
    case 0: flows = side_flows(&block->east[columns - 1], rows, n); break;
    case 1: flows = side_flows(&other->east[n - 1], rows, n); break;
    case 2: flows = side_flows(&block->south[(rows - 1) * n], columns, 1); break;
    default: flows = side_flows(&other->south[(n - 1) * n], columns, 1); break;
    }
    return flows ? other : NULL;
}

/*
 * Keeps awake the blocks ready to sleep that trade a current, directly or
 * through other ready blocks, with a block that is not ready: the current
 * would stop at the edge of the sleeping ones, the jolt waking them again and
 * sending a wave down the water that keeps it all awake. A steady river falls
 * asleep all along at once, its levels and its slope kept as they are, and
 * costs nothing until something changes. Uses Water.woken as its stack (empty
 * once the woken blocks are merged).
 */
static void hold_ready_blocks(Water *water)
{
    uint32_t *stack = water->woken;
    uint32_t top = 0;
    for (uint32_t i = 0; i < water->active_count; i++) {
        WaterBlock *block = &water->pool[water->slot[water->active[i]] - 1];
        if (!block->ready) {
            continue;
        }
        for (int side = 0; side < 4; side++) {
            const WaterBlock *other = current_neighbour(water, block, side);
            if (other != NULL && !other->ready) {
                block->ready = false;
                stack[top++] = block->number;
                break;
            }
        }
    }
    while (top > 0) {
        const WaterBlock *block = &water->pool[water->slot[stack[--top]] - 1];
        for (int side = 0; side < 4; side++) {
            WaterBlock *other = current_neighbour(water, block, side);
            if (other != NULL && other->ready) {
                other->ready = false;
                stack[top++] = other->number;
            }
        }
    }
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
            continue; /* The rain falls beyond the edge alike. */
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

/* The land beyond the edges next to (x, y, z) lives the same weather as the
 * edge: what rain, the sun or the soil gave or took there (`change`), it gets
 * too, above its ground (else a lake cut by the edge, lowered by the sun,
 * would be filled from beyond without end, and rain on a dry edge flow out). */
static void weather_beyond(Water *water, int x, int y, int z, int32_t change)
{
    static const int SIDES[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int side = 0; side < 4 && !water->sealed; side++) {
        size_t slot = edge_slot(water, x, y, SIDES[side][0], SIDES[side][1]);
        if (slot == SIZE_MAX || z < water->edge_ground[slot]) {
            continue;
        }
        move_edge_surface(water, slot, (int64_t)change * WATER_EDGE_BASIN);
    }
}

static int32_t exchange(Water *water, Map *map, size_t index, int32_t fine, bool weather)
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
    if (weather && (x == 0 || y == 0 || x + 1 == water->width || y + 1 == water->height)) {
        weather_beyond(water, x, y, z, after - before);
    }
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

int32_t water_exchange(Water *water, Map *map, size_t index, int32_t fine)
{
    return exchange(water, map, index, fine, true);
}

int32_t water_exchange_here(Water *water, Map *map, size_t index, int32_t fine)
{
    return exchange(water, map, index, fine, false);
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
    edges_from_map(water, map);
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
        /* The land beyond the edges: the weather changes it as it does the
         * fractions. */
        size_t edges = 2 * (size_t)water->width + 2 * (size_t)water->height;
        hash = hash_fnv1a64(hash, water->edge_ground, edges * sizeof(*water->edge_ground));
        hash = hash_fnv1a64(hash, water->edge_surface, edges * sizeof(*water->edge_surface));
        hash = hash_fnv1a64(hash, water->edge_kind, edges * sizeof(*water->edge_kind));
    }
    if (water->active_count == 0) {
        return hash;
    }
    hash = hash_fnv1a64(hash, &water->active_count, sizeof(water->active_count));
    for (uint32_t i = 0; i < water->active_count; i++) {
        const WaterBlock *block = &water->pool[water->slot[water->active[i]] - 1];
        hash = hash_fnv1a64(hash, &block->number, sizeof(block->number));
        hash = hash_fnv1a64(hash, &block->quiet, sizeof(block->quiet));
        hash = hash_fnv1a64(hash, block->net, sizeof(block->net));
        hash = hash_fnv1a64(hash, block->east, sizeof(block->east));
        hash = hash_fnv1a64(hash, block->south, sizeof(block->south));
        hash = hash_fnv1a64(hash, block->west_edge, sizeof(block->west_edge));
        hash = hash_fnv1a64(hash, block->north_edge, sizeof(block->north_edge));
    }
    if (water->edge_in != 0) {
        hash = hash_fnv1a64(hash, &water->edge_in, sizeof(water->edge_in));
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
    for (int i = 0; i < WATER_BLOCK_SIZE; i++) {
        ser_i32(s, &block->west_edge[i]);
        ser_i32(s, &block->north_edge[i]);
    }
    for (int i = 0; i < WATER_BLOCK_TILES; i++) {
        ser_i32(s, &block->net[i]);
    }
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
    size_t edges = 2 * (size_t)water->width + 2 * (size_t)water->height;
    for (size_t i = 0; i < edges && ser_ok(s); i++) {
        ser_u8_below(s, &water->edge_ground[i], (unsigned)water->depth + 1);
        ser_i64(s, &water->edge_surface[i]);
        ser_u8_below(s, &water->edge_kind[i], WATER_EDGE_KIND_COUNT);
        ser_check(s, water->edge_surface[i] >= (int64_t)water->edge_ground[i] * FULL_BASIN &&
                         water->edge_surface[i] <= (int64_t)water->depth * FULL_BASIN);
    }
    ser_i64(s, &water->edge_in);
    ser_bool(s, &water->sealed);
    ser_u32(s, &water->topology_seen);
    ser_u32(s, &water->build_seen);
    ser_u32(s, &water->water_seen);
    ser_u64(s, &water->steps);
    ser_u64(s, &water->wakes);
    ser_u64(s, &water->wake_alls);
    ser_u64(s, &water->moved_last_step);
}
