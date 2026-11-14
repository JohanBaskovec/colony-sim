#include "core/map.h"

#include "core/i18n.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

static const TerrainDef TERRAIN_DEFS[TERRAIN_COUNT] = {
    [TERRAIN_DEEP_WATER] = {"deep_water", N_("terrain.deep_water"), 0.0f, 0.0f, false},
    [TERRAIN_SHALLOW_WATER] = {"shallow_water", N_("terrain.shallow_water"), 3.0f, 0.0f, false},
    [TERRAIN_SAND] = {"sand", N_("terrain.sand"), 1.4f, 0.1f, true},
    [TERRAIN_SOIL] = {"soil", N_("terrain.soil"), 1.0f, 1.0f, true},
    [TERRAIN_RICH_SOIL] = {"rich_soil", N_("terrain.rich_soil"), 1.0f, 1.4f, true},
    [TERRAIN_GRAVEL] = {"gravel", N_("terrain.gravel"), 1.1f, 0.5f, true},
    [TERRAIN_ROUGH_STONE] = {"rough_stone", N_("terrain.rough_stone"), 1.2f, 0.0f, true},
};

static const RockDef ROCK_DEFS[ROCK_COUNT] = {
    [ROCK_NONE] = {"none", NULL, 0.0f, ITEM_NONE, 0},
    [ROCK_GRANITE] = {"granite", N_("rock.granite"), 3500.0f, ITEM_STONE, 10},
    [ROCK_SANDSTONE] = {"sandstone", N_("rock.sandstone"), 2500.0f, ITEM_STONE, 12},
};

static const PlantDef PLANT_DEFS[PLANT_COUNT] = {
    [PLANT_NONE] = {"none", NULL, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, ITEM_NONE, 0},
    [PLANT_GRASS] = {"grass", N_("plant.grass"), 0.35f, 0.3f, 1.0f, 0.0f, 0.0f, ITEM_NONE, 0},
    [PLANT_BUSH] = {"berry_bush", N_("plant.bush"), 0.12f, 0.5f, 1.3f, 0.45f, 0.5f, ITEM_NONE,
                    0},
    [PLANT_TREE] = {"tree", N_("plant.tree"), 0.03f, 0.6f, 1.6f, 0.0f, 0.0f, ITEM_WOOD, 25},
};

const TerrainDef *terrain_def(Terrain terrain)
{
    return ((unsigned)terrain < TERRAIN_COUNT) ? &TERRAIN_DEFS[terrain] : NULL;
}

const RockDef *rock_def(RockType rock)
{
    return ((unsigned)rock < ROCK_COUNT) ? &ROCK_DEFS[rock] : NULL;
}

const PlantDef *plant_def(PlantType plant)
{
    return ((unsigned)plant < PLANT_COUNT) ? &PLANT_DEFS[plant] : NULL;
}

bool map_init(Map *map, int width, int height)
{
    memset(map, 0, sizeof(*map));
    if (width < MAP_MIN_SIZE || height < MAP_MIN_SIZE || width > MAP_MAX_SIZE ||
        height > MAP_MAX_SIZE) {
        return false;
    }

    size_t count = (size_t)width * (size_t)height;
    map->terrain = malloc(count);
    map->rock = calloc(count, 1);
    map->plants = calloc(count, sizeof(Plant));
    if (map->terrain == NULL || map->rock == NULL || map->plants == NULL) {
        map_free(map);
        return false;
    }
    memset(map->terrain, TERRAIN_SOIL, count);
    map->width = width;
    map->height = height;
    return true;
}

void map_free(Map *map)
{
    free(map->terrain);
    free(map->rock);
    free(map->plants);
    memset(map, 0, sizeof(*map));
}

float map_move_cost(const Map *map, int x, int y)
{
    if (!map_in_bounds(map, x, y)) {
        return 0.0f;
    }
    size_t index = map_index(map, x, y);
    if (map->rock[index] != ROCK_NONE) {
        return 0.0f;
    }
    float cost = TERRAIN_DEFS[map->terrain[index]].move_cost;
    return cost * PLANT_DEFS[map->plants[index].type].move_cost;
}

bool map_is_passable(const Map *map, int x, int y)
{
    return map_move_cost(map, x, y) > 0.0f;
}

uint64_t map_hash(const Map *map)
{
    uint64_t hash = HASH_FNV1A64_INIT;
    int32_t size[2] = {map->width, map->height};
    hash = hash_fnv1a64(hash, size, sizeof(size));

    size_t count = map_tile_count(map);
    hash = hash_fnv1a64(hash, map->terrain, count);
    hash = hash_fnv1a64(hash, map->rock, count);
    /* Hash plant fields one by one: struct padding bytes are unspecified. */
    for (size_t i = 0; i < count; i++) {
        const Plant *plant = &map->plants[i];
        hash = hash_fnv1a64(hash, &plant->type, sizeof(plant->type));
        hash = hash_fnv1a64(hash, &plant->growth, sizeof(plant->growth));
    }
    return hash;
}

size_t map_count_plants(const Map *map, PlantType type)
{
    size_t total = 0;
    size_t count = map_tile_count(map);
    for (size_t i = 0; i < count; i++) {
        if (map->plants[i].type == type) {
            total++;
        }
    }
    return total;
}

bool map_has_edible_plant(const Map *map, int x, int y)
{
    if (!map_in_bounds(map, x, y)) {
        return false;
    }
    const Plant *plant = &map->plants[map_index(map, x, y)];
    const PlantDef *def = &PLANT_DEFS[plant->type];
    return def->nutrition > 0.0f && plant->growth >= def->min_edible_growth;
}

bool map_is_water(const Map *map, int x, int y)
{
    if (!map_in_bounds(map, x, y)) {
        return false;
    }
    uint8_t terrain = map->terrain[map_index(map, x, y)];
    return terrain == TERRAIN_SHALLOW_WATER || terrain == TERRAIN_DEEP_WATER;
}

bool map_is_drinking_spot(const Map *map, int x, int y)
{
    if (!map_is_passable(map, x, y)) {
        return false;
    }
    return map_is_water(map, x, y) || map_is_water(map, x - 1, y) ||
           map_is_water(map, x + 1, y) || map_is_water(map, x, y - 1) ||
           map_is_water(map, x, y + 1);
}
