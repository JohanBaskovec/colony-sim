#include "core/map.h"

#include "core/i18n.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

static const TerrainDef TERRAIN_DEFS[TERRAIN_COUNT] = {
    [TERRAIN_DEEP_WATER] = {"deep_water", N_("terrain.deep_water"), 0.0f, 0.0f, false, false},
    [TERRAIN_SHALLOW_WATER] = {"shallow_water", N_("terrain.shallow_water"), 3.0f, 0.0f, false,
                               false},
    [TERRAIN_SAND] = {"sand", N_("terrain.sand"), 1.4f, 0.1f, true, false},
    [TERRAIN_SOIL] = {"soil", N_("terrain.soil"), 1.0f, 1.0f, true, true},
    [TERRAIN_RICH_SOIL] = {"rich_soil", N_("terrain.rich_soil"), 1.0f, 1.4f, true, true},
    [TERRAIN_GRAVEL] = {"gravel", N_("terrain.gravel"), 1.1f, 0.5f, true, true},
    [TERRAIN_ROUGH_STONE] = {"rough_stone", N_("terrain.rough_stone"), 1.2f, 0.0f, true, true},
};

static const RockDef ROCK_DEFS[ROCK_COUNT] = {
    [ROCK_NONE] = {"none", NULL, 0.0f, ITEM_NONE, 0},
    [ROCK_GRANITE] = {"granite", N_("rock.granite"), 3500.0f, ITEM_STONE_BLOCK, 10},
    [ROCK_SANDSTONE] = {"sandstone", N_("rock.sandstone"), 2500.0f, ITEM_STONE_BLOCK, 12},
};

static const PlantDef PLANT_DEFS[PLANT_COUNT] = {
    /* id, name, growth/day, min fertility, move cost, nutrition, min edible growth,
     * cut item, cut yield, harvest item, harvest yield, stock capacity (branches, twigs),
     * stock regrowth hours (branches, twigs) */
    [PLANT_NONE] = {"none", NULL, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, ITEM_NONE, 0, ITEM_NONE, 0,
                    {0, 0}, {0, 0}},
    [PLANT_GRASS] = {"grass", N_("plant.grass"), 0.35f, 0.3f, 1.0f, 0.0f, 0.0f, ITEM_NONE, 0,
                     ITEM_NONE, 0, {0, 0}, {0, 0}},
    /* 9 berries of 0.05 nutrition: the same food as eating the ripe bush directly. */
    [PLANT_BUSH] = {"berry_bush", N_("plant.bush"), 0.12f, 0.5f, 1.3f, 0.45f, 0.5f, ITEM_NONE, 0,
                    ITEM_BERRIES, 9, {0, 4}, {0, 24}},
    /* 60 logs of 7 kg: the usable wood of a mature tree (see item.c). Dead
     * branches can be gathered without felling it. */
    [PLANT_TREE] = {"tree", N_("plant.tree"), 0.03f, 0.6f, 1.6f, 0.0f, 0.0f, ITEM_WOOD, 60,
                    ITEM_NONE, 0, {6, 10}, {48, 24}},
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

ItemType plant_stock_item(PlantStock stock)
{
    switch (stock) {
    case PLANT_STOCK_BRANCHES: return ITEM_BRANCHES;
    case PLANT_STOCK_TWIGS: return ITEM_TWIGS;
    default: return ITEM_NONE;
    }
}

int plant_stock_capacity(const Plant *plant, PlantStock stock)
{
    if ((unsigned)stock >= PLANT_STOCK_COUNT || (unsigned)plant->type >= PLANT_COUNT) {
        return 0;
    }
    float growth = clampf(plant->growth, 0.0f, 1.0f);
    return (int)((float)PLANT_DEFS[plant->type].stock_capacity[stock] * growth + 0.5f);
}

/* Regrowth hours a full stock holds. */
static int stock_limit(const Plant *plant, PlantStock stock)
{
    return plant_stock_capacity(plant, stock) * PLANT_DEFS[plant->type].stock_regrow_hours[stock];
}

int plant_stock_available(const Plant *plant, PlantStock stock)
{
    int capacity = plant_stock_capacity(plant, stock);
    if (capacity <= 0) {
        return 0;
    }
    int available = plant->stock[stock] / PLANT_DEFS[plant->type].stock_regrow_hours[stock];
    return available < capacity ? available : capacity;
}

int plant_take_stock(Plant *plant, PlantStock stock, int count)
{
    int available = plant_stock_available(plant, stock);
    int taken = count < available ? count : available;
    if (taken <= 0) {
        return 0;
    }
    int hours = taken * PLANT_DEFS[plant->type].stock_regrow_hours[stock];
    plant->stock[stock] = (uint16_t)(plant->stock[stock] - hours);
    return taken;
}

void plant_regrow_stocks(Plant *plant, int hours)
{
    if (hours <= 0 || (unsigned)plant->type >= PLANT_COUNT) {
        return;
    }
    for (int stock = 0; stock < PLANT_STOCK_COUNT; stock++) {
        /* A stock above the limit (the plant was eaten down) drops to it. */
        int value = (int)plant->stock[stock] + hours;
        plant->stock[stock] = (uint16_t)clampi(value, 0, stock_limit(plant, (PlantStock)stock));
    }
}

int plant_growth_stage(const Plant *plant)
{
    int stage = (int)(clampf(plant->growth, 0.0f, 1.0f) * (float)PLANT_GROWTH_STAGES);
    return clampi(stage, 0, PLANT_GROWTH_STAGES - 1);
}

bool plant_has_fruit(const Plant *plant)
{
    if ((unsigned)plant->type >= PLANT_COUNT) {
        return false;
    }
    const PlantDef *def = &PLANT_DEFS[plant->type];
    return def->harvest_item != ITEM_NONE && plant->growth >= def->min_edible_growth;
}

void plant_fill_stocks(Plant *plant)
{
    for (int stock = 0; stock < PLANT_STOCK_COUNT; stock++) {
        int limit = (unsigned)plant->type < PLANT_COUNT ? stock_limit(plant, (PlantStock)stock) : 0;
        plant->stock[stock] = (uint16_t)clampi(limit, 0, UINT16_MAX);
    }
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
    map->cobbles = calloc(count, 1);
    map->flint = calloc(count, 1);
    map->structure = calloc(count, 1);
    map->structure_part = calloc(count, 1);
    map->structure_quality = calloc(count, 1);
    map->structure_hit_points = calloc(count, sizeof(*map->structure_hit_points));
    if (map->terrain == NULL || map->rock == NULL || map->plants == NULL ||
        map->cobbles == NULL || map->flint == NULL || map->structure == NULL ||
        map->structure_part == NULL || map->structure_quality == NULL ||
        map->structure_hit_points == NULL) {
        map_free(map);
        return false;
    }
    memset(map->terrain, TERRAIN_SOIL, count);
    map->structure_counts[STRUCTURE_NONE] = (int)count;
    map->width = width;
    map->height = height;
    return true;
}

void map_free(Map *map)
{
    free(map->terrain);
    free(map->rock);
    free(map->plants);
    free(map->cobbles);
    free(map->flint);
    free(map->structure);
    free(map->structure_part);
    free(map->structure_quality);
    free(map->structure_hit_points);
    memset(map, 0, sizeof(*map));
}

static void log_topology_change(Map *map, int x, int y, bool opened)
{
    map->topology_revision++;
    map->topology_log[map->topology_revision % MAP_TOPOLOGY_LOG_SIZE] =
        (MapTopologyChange){(int16_t)x, (int16_t)y, opened};
}

void map_set_terrain(Map *map, int x, int y, Terrain terrain)
{
    if (!map_in_bounds(map, x, y) || (unsigned)terrain >= TERRAIN_COUNT) {
        return;
    }
    size_t index = map_index(map, x, y);
    if (map->terrain[index] != terrain) {
        map->terrain[index] = (uint8_t)terrain;
        /* May also turn neighbors into (or out of) drinking spots. */
        log_topology_change(map, x, y, false);
    }
}

void map_set_rock(Map *map, int x, int y, RockType rock)
{
    if (!map_in_bounds(map, x, y) || (unsigned)rock >= ROCK_COUNT) {
        return;
    }
    size_t index = map_index(map, x, y);
    bool was_passable = map_move_cost_at(map, index) > 0.0f;
    map->rock[index] = (uint8_t)rock;
    bool is_passable = map_move_cost_at(map, index) > 0.0f;
    if (was_passable != is_passable) {
        log_topology_change(map, x, y, is_passable);
    }
}

#define PART_DIRECTION_MASK 0x3u
#define PART_INDEX_SHIFT 2

/* Puts a structure tile (or none) on a tile: counts, plant, topology. */
static void set_tile_structure(Map *map, int x, int y, StructureType type, uint8_t part,
                               ItemQuality quality, int hit_points)
{
    size_t index = map_index(map, x, y);
    StructureType old = (StructureType)map->structure[index];
    bool was_passable = map_move_cost_at(map, index) > 0.0f;
    map->structure_counts[old]--;
    map->structure_counts[type]++;
    map->structure[index] = (uint8_t)type;
    map->structure_part[index] = part;
    map->structure_quality[index] = (uint8_t)quality;
    map->structure_hit_points[index] = (uint16_t)hit_points;
    if (type != STRUCTURE_NONE) {
        map->plants[index] = (Plant){0}; /* Trampled, cut or dug out by the builders. */
    }
    bool is_passable = map_move_cost_at(map, index) > 0.0f;
    if (was_passable != is_passable) {
        log_topology_change(map, x, y, is_passable);
    }
}

bool map_structure_origin(const Map *map, int x, int y, int *origin_x, int *origin_y)
{
    if (!map_in_bounds(map, x, y)) {
        return false;
    }
    size_t index = map_index(map, x, y);
    if (map->structure[index] == STRUCTURE_NONE) {
        return false;
    }
    uint8_t part = map->structure_part[index];
    Direction direction = (Direction)(part & PART_DIRECTION_MASK);
    int offset = part >> PART_INDEX_SHIFT;
    *origin_x = x - offset * direction_dx(direction);
    *origin_y = y - offset * direction_dy(direction);
    return true;
}

bool map_structure_is_origin(const Map *map, int x, int y)
{
    return map_in_bounds(map, x, y) && map->structure[map_index(map, x, y)] != STRUCTURE_NONE &&
           (map->structure_part[map_index(map, x, y)] >> PART_INDEX_SHIFT) == 0;
}

Direction map_structure_direction(const Map *map, int x, int y)
{
    if (!map_in_bounds(map, x, y) || map->structure[map_index(map, x, y)] == STRUCTURE_NONE) {
        return DIRECTION_SOUTH;
    }
    return (Direction)(map->structure_part[map_index(map, x, y)] & PART_DIRECTION_MASK);
}

void map_remove_structure(Map *map, int x, int y)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, &origin_x, &origin_y)) {
        return;
    }
    StructureType type = map_structure(map, x, y);
    Direction direction = map_structure_direction(map, x, y);
    for (int part = 0; part < structure_length(type); part++) {
        int px = origin_x + part * direction_dx(direction);
        int py = origin_y + part * direction_dy(direction);
        if (map_structure(map, px, py) == type) {
            set_tile_structure(map, px, py, STRUCTURE_NONE, 0, ITEM_QUALITY_CRUDE, 0);
        }
    }
}

bool map_place_structure(Map *map, int x, int y, StructureType type, Direction direction,
                         ItemQuality quality)
{
    if (type == STRUCTURE_NONE || (unsigned)type >= STRUCTURE_TYPE_COUNT ||
        (unsigned)direction >= DIRECTION_COUNT) {
        return false;
    }
    int length = structure_length(type);
    int dx = direction_dx(direction), dy = direction_dy(direction);
    for (int part = 0; part < length; part++) {
        if (!map_in_bounds(map, x + part * dx, y + part * dy)) {
            return false;
        }
    }
    quality = (ItemQuality)clampi((int)quality, ITEM_QUALITY_CRUDE, ITEM_QUALITY_BEST);
    bool same = true; /* The same structure already there: only its condition changes. */
    for (int part = 0; part < length; part++) {
        size_t index = map_index(map, x + part * dx, y + part * dy);
        same = same && map->structure[index] == (uint8_t)type &&
               map->structure_part[index] ==
                   (uint8_t)((unsigned)direction | ((unsigned)part << PART_INDEX_SHIFT));
    }
    if (same) {
        size_t head = map_index(map, x, y);
        map->structure_quality[head] = (uint8_t)quality;
        map->structure_hit_points[head] = (uint16_t)structure_max_hit_points(type, quality);
        return true;
    }
    for (int part = 0; part < length; part++) {
        map_remove_structure(map, x + part * dx, y + part * dy);
    }
    for (int part = 0; part < length; part++) {
        bool head = part == 0;
        set_tile_structure(map, x + part * dx, y + part * dy, type,
                           (uint8_t)((unsigned)direction | ((unsigned)part << PART_INDEX_SHIFT)),
                           head ? quality : ITEM_QUALITY_CRUDE,
                           head ? structure_max_hit_points(type, quality) : 0);
    }
    return true;
}

void map_set_structure(Map *map, int x, int y, StructureType type)
{
    if (type == STRUCTURE_NONE) {
        map_remove_structure(map, x, y);
    } else {
        map_place_structure(map, x, y, type, DIRECTION_SOUTH, ITEM_QUALITY_STANDARD);
    }
}

ItemQuality map_structure_quality(const Map *map, int x, int y)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, &origin_x, &origin_y)) {
        return ITEM_QUALITY_CRUDE;
    }
    return (ItemQuality)map->structure_quality[map_index(map, origin_x, origin_y)];
}

int map_structure_hit_points(const Map *map, int x, int y)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, &origin_x, &origin_y)) {
        return 0;
    }
    return map->structure_hit_points[map_index(map, origin_x, origin_y)];
}

void map_set_structure_hit_points(Map *map, int x, int y, int hit_points)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, &origin_x, &origin_y)) {
        return;
    }
    size_t index = map_index(map, origin_x, origin_y);
    StructureType type = (StructureType)map->structure[index];
    int max = structure_max_hit_points(type, (ItemQuality)map->structure_quality[index]);
    map->structure_hit_points[index] = (uint16_t)clampi(hit_points, 1, max);
}

StructureType map_structure(const Map *map, int x, int y)
{
    return map_in_bounds(map, x, y) ? (StructureType)map->structure[map_index(map, x, y)]
                                    : STRUCTURE_NONE;
}

void map_topology_changed(Map *map)
{
    log_topology_change(map, 0, 0, false);
}

float map_move_cost_at(const Map *map, size_t index)
{
    if (map->rock[index] != ROCK_NONE) {
        return 0.0f;
    }
    float cost = TERRAIN_DEFS[map->terrain[index]].move_cost;
    return cost * PLANT_DEFS[map->plants[index].type].move_cost *
           structure_def((StructureType)map->structure[index])->move_cost;
}

float map_move_cost(const Map *map, int x, int y)
{
    return map_in_bounds(map, x, y) ? map_move_cost_at(map, map_index(map, x, y)) : 0.0f;
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
    hash = hash_fnv1a64(hash, map->cobbles, count);
    hash = hash_fnv1a64(hash, map->flint, count);
    hash = hash_fnv1a64(hash, map->structure, count);
    hash = hash_fnv1a64(hash, map->structure_part, count);
    hash = hash_fnv1a64(hash, map->structure_quality, count);
    hash = hash_fnv1a64(hash, map->structure_hit_points,
                        count * sizeof(*map->structure_hit_points));
    /* Hash plant fields one by one: struct padding bytes are unspecified. */
    for (size_t i = 0; i < count; i++) {
        const Plant *plant = &map->plants[i];
        hash = hash_fnv1a64(hash, &plant->type, sizeof(plant->type));
        hash = hash_fnv1a64(hash, plant->stock, sizeof(plant->stock));
        hash = hash_fnv1a64(hash, &plant->growth, sizeof(plant->growth));
    }
    return hash;
}

/* The layer holding stones of a type, or NULL. */
static uint8_t *stone_layer(const Map *map, ItemType type)
{
    switch (type) {
    case ITEM_COBBLE: return map->cobbles;
    case ITEM_FLINT: return map->flint;
    default: return NULL;
    }
}

int map_stones(const Map *map, int x, int y, ItemType type)
{
    const uint8_t *layer = stone_layer(map, type);
    return layer != NULL && map_in_bounds(map, x, y) ? layer[map_index(map, x, y)] : 0;
}

void map_set_stones(Map *map, int x, int y, int cobbles, int flint)
{
    if (!map_in_bounds(map, x, y)) {
        return;
    }
    size_t index = map_index(map, x, y);
    flint = clampi(flint, 0, MAP_STONES_MAX);
    map->flint[index] = (uint8_t)flint;
    map->cobbles[index] = (uint8_t)clampi(cobbles, 0, MAP_STONES_MAX - flint);
}

bool map_take_stone(Map *map, int x, int y, ItemType type)
{
    uint8_t *layer = stone_layer(map, type);
    if (layer == NULL || !map_in_bounds(map, x, y) || layer[map_index(map, x, y)] == 0) {
        return false;
    }
    layer[map_index(map, x, y)]--;
    return true;
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

bool map_plant_blocks_building(const Map *map, int x, int y)
{
    return map_in_bounds(map, x, y) && map->plants[map_index(map, x, y)].type == PLANT_TREE;
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
