/*
 * Water in the ground (docs/WATER.md §5): the pore water of solid tiles
 * (Map.pore) soaking down, flowing sideways from the higher water table to the
 * lower, seeping out into the open tiles beside it and back.
 *
 * Every porous tile has a head, the height (mm over the bottom of the map)
 * its water stands at. A tile at its field capacity (RockDef.field_capacity)
 * or below holds no water free to move: its head is its bottom. Tiles
 * holding more, one over the other, are a water table: their head is its
 * top, the bottom of the highest of them plus the share of its drainable
 * room filled, of its height; when that one is full, its top plus the free
 * water standing on it. Once an hour, in slices over the ticks like the soil
 * (groundwater_update()), each porous tile:
 * - passes what it holds above its field capacity down to the tile under
 *   it, at the lower of their conductivities, as far as that one has room:
 *   water soaks down and stops over clay and granite; a full tile drips from
 *   the ceiling of a gallery under it;
 * - trades water with its neighbours on its level, from the higher head to
 *   the lower (Darcy): the lower of their conductivities, times the
 *   difference of the heads over the width of a tile, times the height of
 *   water in the face. A full tile receiving more passes it up its column,
 *   out of the ground at the top (a spring rising in a hollow);
 * - trades water with the open tiles beside it the same way, their head
 *   the surface of their free water: a gallery dug under the water table
 *   fills, slowly in granite (a drip), in hours in earth, fast in gravel; a
 *   spring runs out of a hillside; a flooded gallery soaks into its walls.
 *   Water coming out onto a floor of rooting ground wets its root zone
 *   first (core/soil.h): the foot of a slope gets wet, and only what that
 *   ground has no room for lies on it.
 * Open water at rest (a lake, the river asleep) is not fed tile by tile:
 * what seeps into it, or out of it, over the hour is spread alike over the
 * open water of its block at the end of the hour, so that ground water
 * reaching a lake along its shores raises it as a whole instead of waking it
 * for every millimetre (docs/WATER.md §5.2).
 * A flow never moves more than half of what would even the two heads out,
 * so that water settles instead of swinging. All amounts are integers moved
 * from one tile to another: the water is kept (but for the edges).
 *
 * The edges (docs/WATER.md §5.2): beyond each column of the edge, the land
 * keeps the water table it had when the game began (groundwater_init()), in
 * rock like that of the edge tile. Sealed edges (Water.sealed) let nothing
 * through. The bottom of the map lets nothing through.
 *
 * Deterministic: tiles are worked out in the order of their indices, each
 * from the water as it is then.
 */
#ifndef COLONY_CORE_GROUNDWATER_H
#define COLONY_CORE_GROUNDWATER_H

#include "core/serialize.h"

#include "core/map.h"
#include "core/soil.h"
#include "core/water.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Heads are in 1/MAP_PORE_FINE mm over the bottom of the map. */
#define GROUNDWATER_LEVEL ((int64_t)WATER_CELL_FULL * MAP_PORE_FINE)
/* No head: not porous, or no water table (an edge column of solid rock). */
#define GROUNDWATER_NO_HEAD INT64_MIN
/* A tile this close to its room (share of the room) is full. */
#define GROUNDWATER_FULL_SHARE 64 /* Within 1/64 of its room. */

typedef struct GroundWater {
    int width, height;
    /* The water table beyond each column of the edge (in the order of
     * water_edge_slot(): north row, south row, west column, east column),
     * as it was when the game began; GROUNDWATER_NO_HEAD where the column
     * had none. */
    int64_t *edge_head;
    /* Water that came in through the edges so far, less what went out,
     * 1/MAP_PORE_FINE mm. */
    int64_t edge_in;
    /* Per block of the flow (water_block_of()): what the ground gave to the
     * open water of a sleeping block over the hour (taken: negative),
     * 1/MAP_PORE_FINE mm, spread over that open water at the end of the
     * hour (groundwater_end_hour()). */
    int64_t *pending;
    uint32_t block_count;
    /* Statistics, not in the hash: water moved in the ground over the last
     * hour and in the hour before it, and out to free water (mm). */
    uint64_t moved, moved_last_hour;
    uint64_t seeped, seeped_last_hour;
} GroundWater;

/* The water table beyond the edges from the map as it is. */
bool groundwater_init(GroundWater *ground, const Map *map);
void groundwater_free(GroundWater *ground);

/* Whether a tile is porous rock (it holds pore water). */
static inline bool groundwater_porous_at(const Map *map, size_t index)
{
    return map_pore_room_at(map, index) > 0;
}

/* Whether a porous tile is full (within 1/GROUNDWATER_FULL_SHARE of its
 * room). */
bool groundwater_full_at(const Map *map, size_t index);

/* The head of a porous tile (see above), GROUNDWATER_NO_HEAD where it is not
 * porous. */
int64_t groundwater_head(const Map *map, int x, int y, int z);

/* The head of the water table of a column: that of its lowest porous tile
 * (the main table, under any perched one), GROUNDWATER_NO_HEAD without
 * porous tiles. */
int64_t groundwater_column_head(const Map *map, int x, int y);

/* Lays a water table at `head` (1/MAP_PORE_FINE mm over the bottom of the
 * map) in a column: porous tiles under it full, the one it crosses filled
 * in proportion above its field capacity, those over it at their field
 * capacity (world generation, tests). */
void groundwater_lay_table(Map *map, int x, int y, int64_t head);

/* One hour of the porous tiles first .. last - 1 (indices in the map). The
 * free water it gives or takes goes through the flow (`flow`,
 * water_exchange_here(); NULL: set on the map), which also tells whether the
 * edges are sealed. Water seeping out onto a floor of rooting ground goes
 * into its root zone (`soil`, NULL: none) as far as it has room, then lies
 * on it. */
void groundwater_update(GroundWater *ground, Map *map, Water *flow, Soil *soil, size_t first,
                        size_t last);

/* Ends the hour: what the ground gave to or took from the open water of
 * sleeping blocks is spread over it, alike on each of its tiles, and the
 * statistics of the hour become those of the last. */
void groundwater_end_hour(GroundWater *ground, Map *map, Water *flow);

/* Whether digging out the solid tile (x, y, z) lets water in (docs/WATER.md
 * §5.5): the water table stands GROUNDWATER_FLOOD_MM or more over its floor
 * (the gallery fills up to it), or open water beside it on its level stands
 * that deep (a lake or the river pours in). */
#define GROUNDWATER_FLOOD_MM WATER_SHALLOWS_MM
bool groundwater_dig_floods(const Map *map, int x, int y, int z);

/* How wet a solid tile is: the share of its room filled (0 .. 1), and in
 * words, a translation key (dry, damp, wet, soaked), NULL where it is not
 * porous. */
float groundwater_share(const Map *map, size_t index);
const char *groundwater_dampness_key(const Map *map, size_t index);
/* The same on the soil's scale (soil_moisture_of(), for the moisture view):
 * 0 dry to 1 at its field capacity, 2 full; -1 where it is not porous. */
float groundwater_moisture(const Map *map, size_t index);

uint64_t groundwater_hash(const GroundWater *ground, uint64_t hash);

/* Saves or loads (docs/SAVE.md) the table beyond the edges; already
 * allocated at the size of the map. */
void groundwater_serialize(Serializer *s, GroundWater *ground);

#endif /* COLONY_CORE_GROUNDWATER_H */
