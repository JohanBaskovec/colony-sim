/*
 * The tile map: terrain, natural rock walls and plants, stored as dense layers
 * (one array per layer, indexed by y * width + x).
 */
#ifndef COLONY_CORE_MAP_H
#define COLONY_CORE_MAP_H

#include "core/item.h"

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
} TerrainDef;

typedef struct RockDef {
    const char *id;
    const char *name_key;
    float mine_work; /* Work (in ticks of a settler working) needed to mine one tile. */
    ItemType mined_item; /* Dropped when a tile is mined. */
    int mined_yield;
} RockDef;

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
} PlantDef;

typedef struct Plant {
    uint8_t type; /* PlantType */
    float growth; /* 0 (seedling) .. 1 (mature) */
} Plant;

typedef struct Map {
    int width;
    int height;
    uint8_t *terrain; /* Terrain per tile. */
    uint8_t *rock; /* RockType per tile; non-zero means a solid rock wall. */
    Plant *plants;
} Map;

const TerrainDef *terrain_def(Terrain terrain);
const RockDef *rock_def(RockType rock);
const PlantDef *plant_def(PlantType plant);

/* Allocates a map filled with soil, without rock nor plants. */
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

/* False when out of bounds, on impassable terrain or on a rock wall. */
bool map_is_passable(const Map *map, int x, int y);

/* Movement cost multiplier of a tile, or 0 when impassable. */
float map_move_cost(const Map *map, int x, int y);

/* Content hash of all layers; equal maps give equal hashes. */
uint64_t map_hash(const Map *map);

size_t map_count_plants(const Map *map, PlantType type);

/* Whether the plant on this tile can be eaten right now. */
bool map_has_edible_plant(const Map *map, int x, int y);

/* Shallow or deep water (false out of bounds). */
bool map_is_water(const Map *map, int x, int y);

/* Whether a settler standing on this tile can drink: the tile is passable and
 * is shallow water or touches water (4-neighborhood). */
bool map_is_drinking_spot(const Map *map, int x, int y);

#endif /* COLONY_CORE_MAP_H */
