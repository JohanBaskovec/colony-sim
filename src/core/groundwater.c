#include "core/groundwater.h"

#include "core/i18n.h"
#include "core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* A drainable share (porosity less field capacity) of an open tile: all of
 * its height is water. Per mille. */
#define OPEN_YIELD 1000
/* The width of a tile, mm (docs/WATER.md §1: 1 m). */
#define TILE_MM 1000

/* What water does in a rock, in integers. */
typedef struct RockWater {
    int64_t room, capacity; /* 1/MAP_PORE_FINE mm. */
    int64_t yield;          /* Drainable share of the volume, per mille. */
    int64_t conductivity;   /* 1/1000 mm an hour. */
} RockWater;

static RockWater rock_water(RockType rock)
{
    RockWater water = {0};
    if ((unsigned)rock >= ROCK_COUNT) {
        return water;
    }
    const RockDef *def = &ROCK_DEFS[rock]; /* Not rock_def(): this is the hot loop. */
    water.room = def->pore_room;
    water.capacity = def->pore_capacity;
    water.yield = def->yield_permille > 1 ? def->yield_permille : 1;
    water.conductivity = def->conductivity_milli;
    return water;
}

static bool full(int64_t pore, int64_t room)
{
    return pore >= room - room / GROUNDWATER_FULL_SHARE;
}

bool groundwater_init(GroundWater *ground, const Map *map)
{
    memset(ground, 0, sizeof(*ground));
    size_t edges = 2 * (size_t)map->width + 2 * (size_t)map->height;
    ground->edge_head = malloc(edges * sizeof(*ground->edge_head));
    if (ground->edge_head == NULL) {
        return false;
    }
    ground->width = map->width;
    ground->height = map->height;
    uint32_t blocks_x = (uint32_t)(map->width + WATER_BLOCK_SIZE - 1) / WATER_BLOCK_SIZE;
    uint32_t blocks_y = (uint32_t)(map->height + WATER_BLOCK_SIZE - 1) / WATER_BLOCK_SIZE;
    ground->block_count = blocks_x * blocks_y * (uint32_t)map->depth;
    ground->pending = calloc(ground->block_count, sizeof(*ground->pending));
    if (ground->pending == NULL) {
        groundwater_free(ground);
        return false;
    }
    for (size_t slot = 0; slot < edges; slot++) {
        size_t width = (size_t)map->width, height = (size_t)map->height;
        int x, y;
        if (slot < width) {
            x = (int)slot, y = 0;
        } else if (slot < 2 * width) {
            x = (int)(slot - width), y = map->height - 1;
        } else if (slot < 2 * width + height) {
            x = 0, y = (int)(slot - 2 * width);
        } else {
            x = map->width - 1, y = (int)(slot - 2 * width - height);
        }
        ground->edge_head[slot] = groundwater_column_head(map, x, y);
    }
    return true;
}

void groundwater_free(GroundWater *ground)
{
    free(ground->edge_head);
    free(ground->pending);
    memset(ground, 0, sizeof(*ground));
}

bool groundwater_full_at(const Map *map, size_t index)
{
    int64_t room = map_pore_room_at(map, index);
    return room > 0 && full(map->pore[index], room);
}

/* The top of the water table a porous tile belongs to: up while the tile
 * above holds water free to move too (a tile not quite full under full ones
 * is in the table all the same). The tile itself when it holds none. */
static size_t table_top(const Map *map, size_t index, int z, int *top_z)
{
    size_t level = map_level_tile_count(map);
    if (map->pore[index] > rock_pore_capacity((RockType)map->rock[index])) {
        while (z + 1 < map->depth) {
            size_t above = index + level;
            if (map->rock[above] == ROCK_NONE ||
                map->pore[above] <= rock_pore_capacity((RockType)map->rock[above])) {
                break;
            }
            index = above;
            z++;
        }
    }
    *top_z = z;
    return index;
}

int64_t groundwater_head(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return GROUNDWATER_NO_HEAD;
    }
    size_t index = map_index(map, x, y, z);
    if (!groundwater_porous_at(map, index)) {
        return GROUNDWATER_NO_HEAD;
    }
    RockWater rock = rock_water((RockType)map->rock[index]);
    if (map->pore[index] <= rock.capacity) {
        return (int64_t)z * GROUNDWATER_LEVEL; /* No water free to move: its bottom. */
    }
    int top_z;
    size_t top = table_top(map, index, z, &top_z);
    rock = rock_water((RockType)map->rock[top]);
    int64_t bottom = (int64_t)top_z * GROUNDWATER_LEVEL;
    int64_t pore = map->pore[top];
    if (!full(pore, rock.room)) {
        return bottom + (pore - rock.capacity) * GROUNDWATER_LEVEL / (rock.room - rock.capacity);
    }
    /* Full: up to its top, and the free water standing on it. */
    size_t above = top + map_level_tile_count(map);
    if (top_z + 1 < map->depth && map->rock[above] == ROCK_NONE) {
        return bottom + GROUNDWATER_LEVEL + (int64_t)map_water_depth_at(map, above) * MAP_PORE_FINE;
    }
    return bottom + GROUNDWATER_LEVEL;
}

int64_t groundwater_column_head(const Map *map, int x, int y)
{
    for (int z = 0; z < map->depth; z++) {
        if (groundwater_porous_at(map, map_index(map, x, y, z))) {
            return groundwater_head(map, x, y, z);
        }
    }
    return GROUNDWATER_NO_HEAD;
}

void groundwater_lay_table(Map *map, int x, int y, int64_t head)
{
    for (int z = 0; z < map->depth; z++) {
        size_t index = map_index(map, x, y, z);
        RockWater rock = rock_water((RockType)map->rock[index]);
        if (rock.room == 0) {
            continue;
        }
        int64_t above = head - (int64_t)z * GROUNDWATER_LEVEL; /* Of the table over the bottom. */
        int64_t pore = above >= GROUNDWATER_LEVEL ? rock.room
                       : above > 0
                           ? rock.capacity + (rock.room - rock.capacity) * above / GROUNDWATER_LEVEL
                           : rock.capacity;
        map_set_pore(map, index, (uint32_t)pore);
    }
}

/* What flows in an hour through the face between two tiles of level z,
 * from the one at head `high` to the one at `low` (1/MAP_PORE_FINE mm):
 * `conductivity` (1/1000 mm an hour) times the difference of the heads over
 * the width of a tile times the height of water in the face (that of the
 * higher head on the level), at most half of what would even the heads out
 * given the drainable shares of the two tiles (per mille). */
static int64_t darcy(int64_t conductivity, int64_t high, int64_t low, int z, int64_t yield_high,
                     int64_t yield_low)
{
    int64_t difference = high - low;
    if (difference <= 0 || conductivity <= 0) {
        return 0;
    }
    int64_t wet = high - (int64_t)z * GROUNDWATER_LEVEL;
    wet = wet < GROUNDWATER_LEVEL ? wet : GROUNDWATER_LEVEL;
    if (wet <= 0) {
        return 0;
    }
    /* mm an hour: conductivity / 1000 × (difference / FINE) × (wet / FINE) / TILE_MM²,
     * times FINE for 1/FINE mm. */
    int64_t flow =
        conductivity * difference / 1000 * wet / ((int64_t)TILE_MM * TILE_MM * MAP_PORE_FINE);
    int64_t even = difference * yield_high * yield_low / ((yield_high + yield_low) * 1000);
    return flow < even / 2 ? flow : even / 2;
}

/* What an hour's pass works on. */
typedef struct Pass {
    GroundWater *ground;
    Map *map;
    Water *flow; /* NULL: free water set on the map directly. */
    Soil *soil;  /* NULL: no root zones. */
} Pass;

/* Gives or takes free water on an open tile (1/MAP_PORE_FINE mm; negative:
 * takes), through the flow; returns what was given or taken. */
static int64_t exchange_free(Pass *pass, size_t index, int64_t amount)
{
    enum { PER_PORE = WATER_FINE / MAP_PORE_FINE };
    _Static_assert(WATER_FINE % MAP_PORE_FINE == 0, "pore water converts to the flow's units");
    if (amount == 0) {
        return 0;
    }
    Map *map = pass->map;
    int64_t fine = amount * PER_PORE;
    fine = fine > INT32_MAX ? INT32_MAX : fine < -INT32_MAX ? -INT32_MAX : fine;
    if (pass->flow == NULL) {
        int x, y, z;
        map_tile_of(map, index, &x, &y, &z);
        int mm = (int)(fine / (PER_PORE * MAP_PORE_FINE));
        int before = map->water[index];
        map_set_water(map, x, y, z, before + mm);
        return (int64_t)(map->water[index] - before) * MAP_PORE_FINE;
    }
    /* Open water at rest: kept for the end of the hour (spread_pending()). */
    size_t surface = water_surface_index(map, index);
    if (map->water[surface] >= WATER_SHALLOWS_MM || map->water[index] >= WATER_SHALLOWS_MM) {
        int x, y, z;
        map_tile_of(map, surface, &x, &y, &z);
        if (!water_block_active(pass->flow, x, y, z)) {
            if (amount < 0) {
                int64_t there = (int64_t)map->water[index] * MAP_PORE_FINE;
                amount = -amount < there ? amount : -there;
            }
            pass->ground->pending[water_block_of(pass->flow, x, y, z)] += amount;
            return amount;
        }
    }
    return water_exchange(pass->flow, map, index, (int32_t)fine) / PER_PORE;
}

/* Water coming out of the ground onto an open tile: into the root zone of
 * its floor first, as far as it has room (seeping out at the foot of a
 * slope wets the ground there), then as free water (a spring). Returns what
 * was given. */
static int64_t seep_out(Pass *pass, size_t index, int64_t amount)
{
    Map *map = pass->map;
    Soil *soil = pass->soil;
    int64_t given = 0;
    /* Out into open space (a cliff's face): it runs down to the foot. */
    size_t level = map_level_tile_count(map);
    while (index >= level && map->water[index] == 0 &&
           (map->terrain[index] == TERRAIN_OPEN || terrain_is_foliage(map->terrain[index]))) {
        size_t below = index - level;
        if (map->rock[below] != ROCK_NONE || map_stops_light_at(map, below) ||
            map->water[below] >= WATER_CELL_FULL) {
            break;
        }
        index = below;
    }
    const SoilDef *def = soil_def((Terrain)map->terrain[index]);
    if (soil != NULL && soil->water != NULL && def != NULL && soil->water[index] != 0.0f &&
        map->water[index] < WATER_WET_MM && amount > 0) {
        float room = (def->saturation - soil->water[index]) * (float)MAP_PORE_FINE;
        int64_t put = room > 0.0f ? (int64_t)room : 0;
        put = put < amount ? put : amount;
        soil->water[index] += (float)put / (float)MAP_PORE_FINE;
        given = put;
        amount -= put;
    }
    if (amount > 0) {
        given += exchange_free(pass, index, amount);
    }
    pass->ground->seeped += (uint64_t)given;
    return given;
}

/* An hour's flow at a conductivity (1/1000 mm an hour) under a gradient of
 * one, 1/MAP_PORE_FINE mm: what can rise or fall through a tile. */
static int64_t vertical_flow(int64_t conductivity)
{
    return conductivity * MAP_PORE_FINE / 1000;
}

/* Puts `amount` into the porous tile `index` on level z, passing what it has
 * no room for up its column: into the porous tiles over it, then out onto
 * the first open tile (a spring rising), no faster than the tiles it rises
 * through let it (vertical_flow()). Returns what it placed: less when the
 * column cannot take it. */
static int64_t deposit(Pass *pass, size_t index, int z, int64_t amount)
{
    Map *map = pass->map;
    size_t level = map_level_tile_count(map);
    int64_t placed = 0;
    int64_t rising = INT64_MAX; /* What may still rise past the tiles passed. */
    while (amount > 0) {
        int64_t room = (int64_t)map_pore_room_at(map, index) - map->pore[index];
        int64_t put = amount < room ? amount : room;
        if (put > 0) {
            map->pore[index] = (uint16_t)(map->pore[index] + put);
            placed += put;
            amount -= put;
            rising -= rising == INT64_MAX ? 0 : put;
        }
        if (amount == 0 || z + 1 >= map->depth) {
            break;
        }
        index += level;
        z++;
        if (map->rock[index] == ROCK_NONE) {
            if (!map_stops_light_at(map, index)) { /* Not under a wall or a closed door. */
                int64_t out = amount < rising ? amount : rising;
                placed += seep_out(pass, index, out);
            }
            break;
        }
        if (!groundwater_porous_at(map, index)) {
            break;
        }
        int64_t through = vertical_flow(rock_water((RockType)map->rock[index]).conductivity);
        rising = rising < through ? rising : through;
        amount = amount < rising ? amount : rising;
    }
    return placed;
}

/* What a porous tile holds above its field capacity. */
static int64_t drainable(const Map *map, size_t index, const RockWater *rock)
{
    int64_t above = map->pore[index] - rock->capacity;
    return above > 0 ? above : 0;
}

/* What can flow out of a porous tile in an hour: what it holds above its
 * field capacity, and what the water table over it can press down into it
 * through the tiles between (vertical_flow() of the least conductive). */
static int64_t table_drainable(const Map *map, size_t index, int z)
{
    int top_z;
    size_t top = table_top(map, index, z, &top_z);
    size_t level = map_level_tile_count(map);
    RockWater own = rock_water((RockType)map->rock[index]);
    int64_t above = 0, through = INT64_MAX;
    for (size_t at = top; at != index; at -= level) {
        RockWater rock = rock_water((RockType)map->rock[at]);
        above += drainable(map, at, &rock);
        int64_t flow = vertical_flow(rock.conductivity);
        through = through < flow ? through : flow;
    }
    return drainable(map, index, &own) + (above < through ? above : through);
}

/* Takes `amount` (no more than table_drainable()) out through a porous
 * tile: from the top of the water table it belongs to down, as far as the
 * tiles between let it through (its level falls; the water under it is
 * pressed out from above), the rest from the tile itself. */
static void withdraw(Map *map, size_t index, int z, int64_t amount)
{
    int top_z;
    size_t top = table_top(map, index, z, &top_z);
    size_t level = map_level_tile_count(map);
    int64_t through = INT64_MAX;
    for (size_t at = top; at != index; at -= level) {
        int64_t flow = vertical_flow(rock_water((RockType)map->rock[at]).conductivity);
        through = through < flow ? through : flow;
    }
    int64_t from_above = amount < through ? amount : through;
    amount -= from_above;
    for (size_t at = top; from_above > 0 && at != index; at -= level) {
        RockWater rock = rock_water((RockType)map->rock[at]);
        int64_t take = drainable(map, at, &rock);
        take = take < from_above ? take : from_above;
        map->pore[at] = (uint16_t)(map->pore[at] - take);
        from_above -= take;
    }
    map->pore[index] = (uint16_t)(map->pore[index] - (amount + from_above));
}

/* The drainable share (per mille) of where a water table's level moves:
 * where water taken out through a tile comes from (the top of its table),
 * and where water put in rises to (the first tile with room up its column,
 * an open tile's free water). */
static int64_t giving_yield(const Map *map, size_t index, int z)
{
    int top_z;
    return rock_water((RockType)map->rock[table_top(map, index, z, &top_z)]).yield;
}

static int64_t taking_yield(const Map *map, size_t index, int z)
{
    size_t level = map_level_tile_count(map);
    while (groundwater_full_at(map, index) && z + 1 < map->depth) {
        index += level;
        z++;
        if (map->rock[index] == ROCK_NONE) {
            return OPEN_YIELD;
        }
    }
    RockWater rock = rock_water((RockType)map->rock[index]);
    return rock.room > 0 ? rock.yield : 1;
}

/* Down from a porous tile: what it holds above its field capacity soaks into
 * the porous tile under it at the lower of their conductivities, as far as
 * it has room; a full tile drips into an open tile under it. */
static void drain_down(Pass *pass, size_t index, int z, const RockWater *rock)
{
    Map *map = pass->map;
    if (z == 0) {
        return; /* The bottom of the map holds. */
    }
    int64_t spare = drainable(map, index, rock);
    if (spare <= 0) {
        return;
    }
    size_t below = index - map_level_tile_count(map);
    int64_t moved = 0;
    if (map->rock[below] == ROCK_NONE) {
        if (full(map->pore[index], rock->room) && !map_stops_light_at(map, below)) {
            int64_t drip = rock->conductivity * MAP_PORE_FINE / 1000;
            moved = seep_out(pass, below, drip < spare ? drip : spare);
        }
    } else {
        RockWater under = rock_water((RockType)map->rock[below]);
        int64_t room = under.room - map->pore[below];
        int64_t conductivity =
            rock->conductivity < under.conductivity ? rock->conductivity : under.conductivity;
        int64_t move = conductivity * MAP_PORE_FINE / 1000;
        move = move < spare ? move : spare;
        move = move < room ? move : room;
        if (move > 0) {
            map->pore[below] = (uint16_t)(map->pore[below] + move);
            moved = move;
            pass->ground->moved += (uint64_t)move;
        }
    }
    map->pore[index] = (uint16_t)(map->pore[index] - moved);
}

/* Between two porous tiles of a level: from the higher head to the lower. */
static void trade_ground(Pass *pass, size_t a, size_t b, int x, int y, int z, int bx, int by)
{
    Map *map = pass->map;
    RockWater rock_a = rock_water((RockType)map->rock[a]);
    RockWater rock_b = rock_water((RockType)map->rock[b]);
    int64_t head_a = groundwater_head(map, x, y, z);
    int64_t head_b = groundwater_head(map, bx, by, z);
    if (head_a == head_b) {
        return;
    }
    bool from_a = head_a > head_b;
    size_t giver = from_a ? a : b, taker = from_a ? b : a;
    int64_t conductivity =
        rock_a.conductivity < rock_b.conductivity ? rock_a.conductivity : rock_b.conductivity;
    int64_t amount = darcy(conductivity, from_a ? head_a : head_b, from_a ? head_b : head_a, z,
                           giving_yield(map, giver, z), taking_yield(map, taker, z));
    int64_t spare = table_drainable(map, giver, z);
    amount = amount < spare ? amount : spare;
    if (amount <= 0) {
        return;
    }
    int64_t placed = deposit(pass, taker, z, amount);
    withdraw(map, giver, z, placed);
    pass->ground->moved += (uint64_t)placed;
}

/* Between a porous tile and an open tile beside it: seeping out into it,
 * or its free water soaking in (as far as the tile has room). */
static void trade_open(Pass *pass, size_t index, size_t open, int x, int y, int z,
                       const RockWater *rock)
{
    Map *map = pass->map;
    int64_t head = groundwater_head(map, x, y, z);
    int64_t free_mm = map_water_depth_at(map, open);
    int64_t surface = (int64_t)z * GROUNDWATER_LEVEL + free_mm * MAP_PORE_FINE;
    if (head > surface) {
        int64_t amount =
            darcy(rock->conductivity, head, surface, z, giving_yield(map, index, z), OPEN_YIELD);
        int64_t spare = table_drainable(map, index, z);
        amount = amount < spare ? amount : spare;
        withdraw(map, index, z, seep_out(pass, open, amount));
    } else if (surface > head) {
        int64_t amount =
            darcy(rock->conductivity, surface, head, z, OPEN_YIELD, taking_yield(map, index, z));
        int64_t room = rock->room - map->pore[index];
        int64_t there = (int64_t)map->water[open] * MAP_PORE_FINE;
        amount = amount < room ? amount : room;
        amount = amount < there ? amount : there;
        int64_t in = -exchange_free(pass, open, -amount);
        map->pore[index] = (uint16_t)(map->pore[index] + in);
        pass->ground->moved += (uint64_t)in;
    }
}

/* Through the edge of the map: with the water table beyond, as it was. */
static void trade_edge(Pass *pass, size_t index, size_t slot, int x, int y, int z,
                       const RockWater *rock)
{
    Map *map = pass->map;
    GroundWater *ground = pass->ground;
    int64_t beyond = ground->edge_head[slot];
    if (beyond == GROUNDWATER_NO_HEAD) {
        return;
    }
    int64_t bottom = (int64_t)z * GROUNDWATER_LEVEL;
    beyond = beyond > bottom ? beyond : bottom; /* Dry at this level: it takes water in. */
    int64_t head = groundwater_head(map, x, y, z);
    if (head > beyond) {
        int64_t amount =
            darcy(rock->conductivity, head, beyond, z, giving_yield(map, index, z), rock->yield);
        int64_t spare = table_drainable(map, index, z);
        amount = amount < spare ? amount : spare;
        withdraw(map, index, z, amount);
        ground->edge_in -= amount;
        ground->moved += (uint64_t)amount;
    } else if (beyond > head) {
        int64_t amount =
            darcy(rock->conductivity, beyond, head, z, rock->yield, taking_yield(map, index, z));
        int64_t placed = deposit(pass, index, z, amount);
        ground->edge_in += placed;
        ground->moved += (uint64_t)placed;
    }
}

void groundwater_update(GroundWater *ground, Map *map, Water *flow, Soil *soil, size_t first,
                        size_t last)
{
    if (ground->edge_head == NULL || ground->width != map->width || ground->height != map->height) {
        return;
    }
    static const int DX[4] = {1, 0, -1, 0};
    static const int DY[4] = {0, 1, 0, -1};
    Pass pass = {ground, map, flow, soil};
    bool sealed = flow != NULL && flow->sealed;
    size_t count = map_tile_count(map);
    last = last < count ? last : count;
    for (size_t i = first; i < last; i++) {
        if (map->rock[i] == ROCK_NONE) {
            continue;
        }
        RockWater rock = rock_water((RockType)map->rock[i]);
        if (rock.room == 0) {
            continue;
        }
        /* A tile holding nothing above its field capacity has its bottom as
         * head: it gives nothing, and takes only from open water beside it,
         * a neighbour holding more, or the edge. */
        int x, y, z;
        map_tile_of(map, i, &x, &y, &z);
        if (map->pore[i] > rock.capacity) {
            drain_down(&pass, i, z, &rock);
        }
        for (int d = 0; d < 4; d++) {
            bool idle = map->pore[i] <= rock.capacity; /* It may have taken some in. */
            int nx = x + DX[d], ny = y + DY[d];
            if (nx < 0 || ny < 0 || nx >= map->width || ny >= map->height) {
                if (!sealed) {
                    size_t slot = DY[d] < 0   ? (size_t)x
                                  : DY[d] > 0 ? (size_t)map->width + (size_t)x
                                  : DX[d] < 0
                                      ? 2 * (size_t)map->width + (size_t)y
                                      : 2 * (size_t)map->width + (size_t)map->height + (size_t)y;
                    trade_edge(&pass, i, slot, x, y, z, &rock);
                }
                continue;
            }
            size_t n = map_index(map, nx, ny, z);
            if (map->rock[n] == ROCK_NONE) {
                if ((!idle || map->water[n] != 0) &&
                    !map_stops_light_at(map, n)) { /* Not into a wall or a closed door. */
                    trade_open(&pass, i, n, x, y, z, &rock);
                }
            } else if (d < 2 && groundwater_porous_at(map, n) &&
                       (!idle || map->pore[n] > rock_pore_capacity((RockType)map->rock[n]))) {
                /* East and south: each pair of porous tiles once. */
                trade_ground(&pass, i, n, x, y, z, nx, ny);
            }
        }
    }
}

/* Spreads what a block's open water was given or lost over the hour alike
 * over its tiles of open water (Shallows or deeper); kept for later where it
 * has none any more. */
static void spread_pending(GroundWater *ground, Map *map, Water *flow, uint32_t number)
{
    enum { PER_PORE = WATER_FINE / MAP_PORE_FINE };
    int blocks_x = (map->width + WATER_BLOCK_SIZE - 1) / WATER_BLOCK_SIZE;
    int blocks_y = (map->height + WATER_BLOCK_SIZE - 1) / WATER_BLOCK_SIZE;
    int bx = (int)(number % (uint32_t)blocks_x);
    int by = (int)(number / (uint32_t)blocks_x % (uint32_t)blocks_y);
    int z = (int)(number / (uint32_t)(blocks_x * blocks_y));
    int x0 = bx * WATER_BLOCK_SIZE, y0 = by * WATER_BLOCK_SIZE;
    int x1 = x0 + WATER_BLOCK_SIZE < map->width ? x0 + WATER_BLOCK_SIZE : map->width;
    int y1 = y0 + WATER_BLOCK_SIZE < map->height ? y0 + WATER_BLOCK_SIZE : map->height;
    int64_t tiles = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            tiles += map->water[map_index(map, x, y, z)] >= WATER_SHALLOWS_MM;
        }
    }
    if (tiles == 0) {
        return;
    }
    int64_t total = ground->pending[number] * PER_PORE; /* In the flow's units. */
    int64_t share = total / tiles, rest = total % tiles, done = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            size_t index = map_index(map, x, y, z);
            if (map->water[index] < WATER_SHALLOWS_MM) {
                continue;
            }
            int64_t give = share + (rest > 0 ? 1 : rest < 0 ? -1 : 0);
            rest -= rest > 0 ? 1 : rest < 0 ? -1 : 0;
            done += water_exchange(flow, map, index, (int32_t)give);
        }
    }
    /* What could not be taken stays owed (whole units of the ground). */
    ground->pending[number] -= done / PER_PORE;
}

void groundwater_end_hour(GroundWater *ground, Map *map, Water *flow)
{
    if (ground->pending != NULL && flow != NULL && map->width == ground->width &&
        map->height == ground->height) {
        for (uint32_t number = 0; number < ground->block_count; number++) {
            if (ground->pending[number] != 0) {
                spread_pending(ground, map, flow, number);
            }
        }
    }
    ground->moved_last_hour = ground->moved;
    ground->seeped_last_hour = ground->seeped;
    ground->moved = 0;
    ground->seeped = 0;
}

float groundwater_share(const Map *map, size_t index)
{
    uint32_t room = map_pore_room_at(map, index);
    return room > 0 ? (float)map->pore[index] / (float)room : 0.0f;
}

const char *groundwater_dampness_key(const Map *map, size_t index)
{
    RockType type = (RockType)map->rock[index];
    int64_t room = rock_pore_room(type);
    if (room == 0) {
        return NULL;
    }
    int64_t pore = map->pore[index], capacity = rock_pore_capacity(type);
    if (full(pore, room)) {
        return N_("ground.soaked");
    }
    if (pore > capacity) {
        return N_("ground.wet");
    }
    return pore * 2 > capacity ? N_("ground.damp") : N_("ground.dry");
}

uint64_t groundwater_hash(const GroundWater *ground, uint64_t hash)
{
    if (ground->edge_head == NULL) {
        return hash;
    }
    size_t edges = 2 * (size_t)ground->width + 2 * (size_t)ground->height;
    hash = hash_fnv1a64(hash, ground->edge_head, edges * sizeof(*ground->edge_head));
    hash = hash_fnv1a64(hash, ground->pending, ground->block_count * sizeof(*ground->pending));
    return hash_fnv1a64(hash, &ground->edge_in, sizeof(ground->edge_in));
}

void groundwater_serialize(Serializer *s, GroundWater *ground)
{
    int width = ground->width, height = ground->height;
    ser_int(s, &width);
    ser_int(s, &height);
    if (!ser_check(s, width == ground->width && height == ground->height)) {
        return;
    }
    size_t edges = 2 * (size_t)width + 2 * (size_t)height;
    for (size_t slot = 0; slot < edges; slot++) {
        ser_i64(s, &ground->edge_head[slot]);
    }
    ser_i64(s, &ground->edge_in);
    /* What is pending: the blocks holding some, then their amounts. */
    uint32_t count = 0;
    for (uint32_t number = 0; !s->reading && number < ground->block_count; number++) {
        count += ground->pending[number] != 0;
    }
    ser_u32(s, &count);
    if (!ser_check(s, count <= ground->block_count)) {
        return;
    }
    uint32_t number = 0;
    for (uint32_t k = 0; k < count && !s->failed; k++) {
        while (!s->reading && ground->pending[number] == 0) {
            number++;
        }
        ser_u32(s, &number);
        if (!ser_check(s, number < ground->block_count)) {
            return;
        }
        ser_i64(s, &ground->pending[number]);
        number++;
    }
}
