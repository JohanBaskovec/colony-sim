/*
 * The tile map: terrain, natural rock walls and plants, stored as dense layers
 * (one array per layer, indexed by y * width + x).
 */
#ifndef COLONY_CORE_MAP_H
#define COLONY_CORE_MAP_H

#include "core/item.h"
#include "core/structure.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAP_MIN_SIZE 16
#define MAP_MAX_SIZE 1024

typedef enum Terrain {
    TERRAIN_DEEP_WATER,
    TERRAIN_SHALLOW_WATER,
    TERRAIN_SAND,
    TERRAIN_SOIL,
    TERRAIN_RICH_SOIL,
    TERRAIN_GRAVEL,
    TERRAIN_ROUGH_STONE,
    TERRAIN_COUNT
} Terrain;

typedef enum RockType {
    ROCK_NONE = 0,
    ROCK_GRANITE,
    ROCK_SANDSTONE,
    ROCK_COUNT
} RockType;

typedef enum PlantType {
    PLANT_NONE = 0,
    PLANT_GRASS,
    PLANT_BUSH,
    PLANT_TREE,
    PLANT_COUNT
} PlantType;

/*
 * Static definitions. `id` is a stable identifier (save files, data files) and
 * is never shown to the player; `name_key` is the translation key of the
 * display name.
 */
typedef struct TerrainDef {
    const char *id;
    const char *name_key;
    float move_cost; /* Movement cost multiplier; 0 means impassable. */
    float fertility; /* Plant growth multiplier (1.0 = normal soil). */
    bool buildable; /* Whether structures can be built on it. */
    bool stony; /* Whether stones lie about on it (see Map.cobbles). */
} TerrainDef;

typedef struct RockDef {
    const char *id;
    const char *name_key;
    float mine_work; /* Work (in ticks of a settler working) needed to mine one tile. */
    ItemType mined_item; /* Dropped when a tile is mined. */
    int mined_yield;
} RockDef;

/* Things a plant offers without being cut down, and that grow back. */
typedef enum PlantStock {
    PLANT_STOCK_BRANCHES,
    PLANT_STOCK_TWIGS,
    PLANT_STOCK_COUNT
} PlantStock;

typedef struct PlantDef {
    const char *id;
    const char *name_key;
    float growth_per_day; /* Growth gained per in-game day on fertility 1.0. */
    float min_fertility; /* Does not spawn nor grow below this fertility. */
    float move_cost; /* Movement cost multiplier while walking through. */
    float nutrition; /* Food gained by eating it at full growth; 0 = not edible. */
    float min_edible_growth; /* Cannot be eaten below this growth. */
    ItemType cut_item; /* Dropped when the plant is cut down (ITEM_NONE: nothing). */
    int cut_yield; /* Items dropped at full growth, proportionally less when younger. */
    ItemType harvest_item; /* Picked when harvested, the plant stays (ITEM_NONE: nothing). */
    int harvest_yield; /* Items picked at full growth; harvesting needs min_edible_growth. */
    /* Stocks: at most `stock_capacity` items at full growth (proportionally
     * fewer when younger), one growing back every `stock_regrow_hours`. */
    int stock_capacity[PLANT_STOCK_COUNT];
    int stock_regrow_hours[PLANT_STOCK_COUNT];
} PlantDef;

/* Growth left on a plant after its fruit has been eaten or harvested. */
#define PLANT_GROWTH_AFTER_PICKING 0.05f

typedef struct Plant {
    uint8_t type; /* PlantType */
    /* Stocks, in hours of regrowth: one item per stock_regrow_hours. */
    uint16_t stock[PLANT_STOCK_COUNT];
    float growth; /* 0 (seedling) .. 1 (mature) */
} Plant;

/* Plants are drawn in this many sizes as they grow (see plant_growth_stage()). */
#define PLANT_GROWTH_STAGES 5

/* Most stones (cobbles and flint nodules together) lying about one tile. */
#define MAP_STONES_MAX 20

/* How many recent topology changes the map remembers (see Map.topology_log). */
#define MAP_TOPOLOGY_LOG_SIZE 16

/* How many recent structure and roof changes the map remembers (see
 * Map.build_log). */
#define MAP_BUILD_LOG_SIZE 16

/* A natural roof needs rock within this many tiles (see map_natural_roof()). */
#define MAP_NATURAL_ROOF_REACH 3

/* One change of where one can walk. */
typedef struct MapTopologyChange {
    int16_t x, y;
    /* True when all that happened is that this tile became passable (a rock
     * wall was dug out): the common change, which caches can apply without
     * starting over. False: anything else, on this tile or (`anywhere`)
     * anywhere. */
    bool opened;
    bool anywhere; /* The layers were written directly (map_topology_changed()). */
} MapTopologyChange;

/* One change of a structure or a roof, or of rock where one could not walk
 * anyway (a tile; see Map.build_log). */
typedef struct MapBuildChange {
    int16_t x, y;
} MapBuildChange;

typedef struct Map {
    int width;
    int height;
    uint8_t *terrain; /* Terrain per tile. */
    uint8_t *rock; /* RockType per tile; non-zero means a solid rock wall. */
    Plant *plants;
    /* Stones lying about each tile, that gathering takes one by one: cobbles
     * and flint nodules (on flint patches), MAP_STONES_MAX at most together.
     * World generation puts none on sand (see gather_stone_ground()). */
    uint8_t *cobbles;
    uint8_t *flint;
    /* Built structures (StructureType on every tile they take): beds,
     * walls, doors. `structure_part` holds the structure's Direction (bits
     * 0-1) and which of its tiles this is (bits 2-3, 0 for the head); the
     * grade (ItemQuality) and hit points are kept on the head (see
     * core/structure.h). */
    uint8_t *structure;
    uint8_t *structure_part;
    uint8_t *structure_quality;
    uint16_t *structure_hit_points;
    int structure_counts[STRUCTURE_TYPE_COUNT]; /* Tiles holding each type. */
    /* Built roofs: the roof's StructureType over each tile (0: none) and its
     * grade. A layer of its own: a roof covers a bed, a wall or open ground. */
    uint8_t *roof;
    uint8_t *roof_quality;
    int roof_count; /* Roofed tiles. */
    /* Incremented whenever a structure or a roof changes. Data derived from
     * it (core/room.h) compares it. Not part of map_hash(). */
    uint32_t build_revision;
    /* The change that led to build revision r is at index r % MAP_BUILD_LOG_SIZE. */
    MapBuildChange build_log[MAP_BUILD_LOG_SIZE];
    /* Incremented whenever where one can walk, or where water is, changes
     * (plants never block). Data derived from it (core/region.h) compares it
     * to know when it is out of date. Not part of map_hash(). */
    uint32_t topology_revision;
    /* The change that led to revision r is at index r % MAP_TOPOLOGY_LOG_SIZE. */
    MapTopologyChange topology_log[MAP_TOPOLOGY_LOG_SIZE];
} Map;

const TerrainDef *terrain_def(Terrain terrain);
const RockDef *rock_def(RockType rock);
const PlantDef *plant_def(PlantType plant);

/* Item a stock gives (branches, twigs). */
ItemType plant_stock_item(PlantStock stock);

/* Most items of a stock the plant can hold at its current growth. */
int plant_stock_capacity(const Plant *plant, PlantStock stock);

/* Items of a stock that can be taken now. */
int plant_stock_available(const Plant *plant, PlantStock stock);

/* Takes up to `count` items of a stock; returns how many were taken. */
int plant_take_stock(Plant *plant, PlantStock stock, int count);

/* Lets the stocks grow back for `hours`, up to the capacity. */
void plant_regrow_stocks(Plant *plant, int hours);

/* Size class of a plant, 0 (seedling) .. PLANT_GROWTH_STAGES - 1 (mature):
 * growth split in equal steps. */
int plant_growth_stage(const Plant *plant);

/* Whether the plant carries fruit that can be picked or eaten (ripe berries). */
bool plant_has_fruit(const Plant *plant);

/* Fills every stock to the capacity (new plants of a generated world). */
void plant_fill_stocks(Plant *plant);

/* Allocates a map filled with soil, without rock, plants nor stones. */
bool map_init(Map *map, int width, int height);
void map_free(Map *map);

static inline bool map_in_bounds(const Map *map, int x, int y)
{
    return x >= 0 && y >= 0 && x < map->width && y < map->height;
}

static inline size_t map_index(const Map *map, int x, int y)
{
    return (size_t)y * (size_t)map->width + (size_t)x;
}

static inline size_t map_tile_count(const Map *map)
{
    return (size_t)map->width * (size_t)map->height;
}

/*
 * Terrain and rock must be changed through these once the map is in use (or
 * map_topology_changed() must be called after writing the layers directly),
 * so that topology_revision follows. Out-of-bounds tiles are ignored. A rock
 * change that leaves the tile as passable as before (granite to sandstone)
 * is not a topology change.
 */
void map_set_terrain(Map *map, int x, int y, Terrain terrain);
void map_set_rock(Map *map, int x, int y, RockType rock);
void map_topology_changed(Map *map);

/* Same rule for structures (a wall blocks movement, a door slows it). The
 * plants of its tiles are cleared when a structure is built.
 *
 * Builds a structure of a grade, in perfect condition, with its head at
 * (x, y) and its other tiles in `direction` (see structure_length()). Any
 * structure already on those tiles is removed whole. Fails (nothing changes)
 * when a tile is out of bounds. */
bool map_place_structure(Map *map, int x, int y, StructureType type, Direction direction,
                         ItemQuality quality);

/* map_place_structure() of standard grade, facing south; STRUCTURE_NONE
 * removes the structure covering the tile (map_remove_structure()). */
void map_set_structure(Map *map, int x, int y, StructureType type);

/* Removes the whole structure covering a tile, all its tiles. */
void map_remove_structure(Map *map, int x, int y);

/* Head of the structure covering a tile (the tile itself for a one-tile
 * structure); false without a structure. */
bool map_structure_origin(const Map *map, int x, int y, int *origin_x, int *origin_y);

/* Whether the tile holds the head of a structure. */
bool map_structure_is_origin(const Map *map, int x, int y);

/* Direction of the structure covering a tile (DIRECTION_SOUTH without one). */
Direction map_structure_direction(const Map *map, int x, int y);

/* Grade and hit points of the structure covering a tile (0 hit points
 * without one or out of bounds). */
ItemQuality map_structure_quality(const Map *map, int x, int y);
int map_structure_hit_points(const Map *map, int x, int y);

/* Sets the hit points of the structure covering a tile, clamped to 1..its
 * maximum (nothing without a structure). */
void map_set_structure_hit_points(Map *map, int x, int y, int hit_points);

/* STRUCTURE_NONE out of bounds. */
StructureType map_structure(const Map *map, int x, int y);

/* The roof built over a tile (STRUCTURE_NONE without one or out of bounds)
 * and its grade. */
StructureType map_roof(const Map *map, int x, int y);
ItemQuality map_roof_quality(const Map *map, int x, int y);

/* Builds a roof (a type for which structure_is_roof()) of a grade over a
 * tile, or removes it (STRUCTURE_NONE). Other types and out-of-bounds tiles
 * are ignored. */
void map_set_roof(Map *map, int x, int y, StructureType roof, ItemQuality quality);

/* Whether a roof over the tile would rest on something: a wall (a structure
 * that blocks movement) or rock within ROOF_SUPPORT_RANGE tiles. */
bool map_roof_supported(const Map *map, int x, int y);

/*
 * Whether rock covers the tile: an open tile with rock at most 3 tiles away
 * in at least 6 of the 8 directions (inside a cave, under an overhang).
 * docs/KNOWLEDGE.md §10.
 */
bool map_natural_roof(const Map *map, int x, int y);

/* A built roof or a natural one. */
bool map_is_roofed(const Map *map, int x, int y);

/* False when out of bounds, on impassable terrain, on a rock wall or on a
 * structure that blocks movement. */
bool map_is_passable(const Map *map, int x, int y);

/* Movement cost multiplier of a tile, or 0 when impassable. */
float map_move_cost(const Map *map, int x, int y);

/* Same for a tile index known to be in bounds (hot loops: pathfinding). */
float map_move_cost_at(const Map *map, size_t index);

/* Content hash of all layers; equal maps give equal hashes. */
uint64_t map_hash(const Map *map);

size_t map_count_plants(const Map *map, PlantType type);

/* Whether the plant on a tile keeps anything from being built there: a tree
 * (felling needs an axe). Grass and bushes are cleared by hand first (see
 * gather_clear_plant()). */
bool map_plant_blocks_building(const Map *map, int x, int y);

/* Whether the plant on this tile can be eaten right now. */
bool map_has_edible_plant(const Map *map, int x, int y);

/* Stones of a type (ITEM_COBBLE or ITEM_FLINT) lying about a tile; 0 for other
 * types and out of bounds. */
int map_stones(const Map *map, int x, int y, ItemType type);

/* Sets the stones of a tile: each count is clamped to 0..MAP_STONES_MAX, then
 * cobbles to what flint leaves of MAP_STONES_MAX. Out-of-bounds tiles are
 * ignored. */
void map_set_stones(Map *map, int x, int y, int cobbles, int flint);

/* Takes one stone of a type from a tile; false when there is none. */
bool map_take_stone(Map *map, int x, int y, ItemType type);

/* Shallow or deep water (false out of bounds). */
bool map_is_water(const Map *map, int x, int y);

/* Whether a settler standing on this tile can drink: the tile is passable and
 * is shallow water or touches water (4-neighborhood). */
bool map_is_drinking_spot(const Map *map, int x, int y);

#endif /* COLONY_CORE_MAP_H */
