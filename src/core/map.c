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
    [TERRAIN_OPEN] = {"open", N_("terrain.open"), 0.0f, 0.0f, false, false},
};

static const RockDef ROCK_DEFS[ROCK_COUNT] = {
    [ROCK_NONE] = {"none", NULL, 0.0f, ITEM_NONE, 0},
    [ROCK_GRANITE] = {"granite", N_("rock.granite"), 3500.0f, ITEM_STONE_BLOCK, 10},
    [ROCK_SANDSTONE] = {"sandstone", N_("rock.sandstone"), 2500.0f, ITEM_STONE_BLOCK, 12},
    [ROCK_EARTH] = {"earth", N_("rock.earth"), 1200.0f, ITEM_NONE, 0},
};

/* Seasons' days, for the plant tables. */
#define SPRING(day) SEASON_DAY(SEASON_SPRING, day)
#define SUMMER(day) SEASON_DAY(SEASON_SUMMER, day)
#define AUTUMN(day) SEASON_DAY(SEASON_AUTUMN, day)
#define WINTER(day) SEASON_DAY(SEASON_WINTER, day)
/* Plants grow from spring to autumn and rest in winter. */
#define GROWING_SEASON PLANT_DAYS(SPRING(1), AUTUMN(DAYS_PER_SEASON))
#define STOCKS(branches, twigs, food) {branches, twigs, food}

/*
 * Wild plants and their seasons in a temperate climate (docs/KNOWLEDGE.md §7).
 * Food values: see the items (core/item.c). A stock grows back one item per
 * `stock_regrow_hours`: a bramble ripens two handfuls a day at the end of
 * summer, a hazel a little over one nut a day in autumn.
 */
static const PlantDef PLANT_DEFS[PLANT_COUNT] = {
    [PLANT_NONE] = {.id = "none", .move_cost = 1.0f},
    [PLANT_GRASS] = {.id = "grass", .name_key = N_("plant.grass"), .growth_per_day = 0.35f,
                     .min_fertility = 0.3f, .move_cost = 1.0f, .growth_days = GROWING_SEASON},
    /* 60 logs of 7 kg: the usable wood of a mature tree (see item.c). Dead
     * branches can be gathered without felling it. */
    [PLANT_TREE] = {.id = "tree", .name_key = N_("plant.tree"), .growth_per_day = 0.03f,
                    .min_fertility = 0.6f, .move_cost = 1.6f, .growth_days = GROWING_SEASON,
                    .cut_item = ITEM_WOOD, .cut_yield = 60, .stock_capacity = STOCKS(6, 10, 0),
                    .stock_regrow_hours = STOCKS(48, 24, 0)},
    [PLANT_HAZEL] = {.id = "hazel", .name_key = N_("plant.hazel"), .growth_per_day = 0.04f,
                     .min_fertility = 0.6f, .move_cost = 1.4f, .growth_days = GROWING_SEASON,
                     .stock_capacity = STOCKS(3, 6, 12), .stock_regrow_hours = STOCKS(72, 24, 20),
                     .food_item = ITEM_HAZELNUTS,
                     .food_days = PLANT_DAYS(AUTUMN(1), AUTUMN(DAYS_PER_SEASON)),
                     .min_food_growth = 0.6f},
    /* Thorny canes: slow to walk through. */
    [PLANT_BRAMBLE] = {.id = "bramble", .name_key = N_("plant.bramble"), .growth_per_day = 0.12f,
                       .min_fertility = 0.5f, .move_cost = 2.0f, .growth_days = GROWING_SEASON,
                       .stock_capacity = STOCKS(0, 0, 9), .stock_regrow_hours = STOCKS(0, 0, 12),
                       .food_item = ITEM_BLACKBERRIES,
                       .food_days = PLANT_DAYS(SUMMER(8), AUTUMN(7)), .min_food_growth = 0.5f},
    /* Easily mistaken for lily of the valley or autumn crocus. */
    [PLANT_WILD_GARLIC] = {.id = "wild_garlic", .name_key = N_("plant.wild_garlic"),
                           .growth_per_day = 0.25f, .min_fertility = 1.2f, .move_cost = 1.0f,
                           .growth_days = PLANT_DAYS(SPRING(1), SUMMER(3)), .dies_back = true,
                           .stock_capacity = STOCKS(0, 0, 3),
                           .stock_regrow_hours = STOCKS(0, 0, 24), .food_item = ITEM_WILD_GARLIC,
                           .food_days = PLANT_DAYS(SPRING(1), SPRING(DAYS_PER_SEASON)),
                           .min_food_growth = 0.3f, .food_confusable = true},
    /* Young shoots in spring, and again when it grows back in autumn. */
    [PLANT_NETTLE] = {.id = "nettle", .name_key = N_("plant.nettle"), .growth_per_day = 0.2f,
                      .min_fertility = 1.2f, .move_cost = 1.2f, .growth_days = GROWING_SEASON,
                      .dies_back = true, .stock_capacity = STOCKS(0, 0, 3),
                      .stock_regrow_hours = STOCKS(0, 0, 24), .food_item = ITEM_NETTLES,
                      .food_days = PLANT_DAYS(SPRING(1), SPRING(DAYS_PER_SEASON)) |
                                   PLANT_DAYS(AUTUMN(3), AUTUMN(DAYS_PER_SEASON)),
                      .min_food_growth = 0.2f},
    [PLANT_PLANTAIN] = {.id = "plantain", .name_key = N_("plant.plantain"),
                        .growth_per_day = 0.15f, .min_fertility = 0.6f, .move_cost = 1.0f,
                        .growth_days = GROWING_SEASON, .stock_capacity = STOCKS(0, 0, 2),
                        .stock_regrow_hours = STOCKS(0, 0, 24), .food_item = ITEM_PLANTAIN,
                        .food_days = GROWING_SEASON, .min_food_growth = 0.3f},
    /* Its root is tender from autumn until early spring, woody once it
     * flowers; easily mistaken for hemlock. */
    [PLANT_WILD_CARROT] = {.id = "wild_carrot", .name_key = N_("plant.wild_carrot"),
                           .growth_per_day = 0.08f, .min_fertility = 0.6f, .move_cost = 1.0f,
                           .growth_days = GROWING_SEASON, .stock_capacity = STOCKS(0, 0, 3),
                           .stock_regrow_hours = STOCKS(0, 0, 24),
                           .food_item = ITEM_WILD_CARROTS,
                           .food_days = PLANT_DAYS(AUTUMN(1), WINTER(DAYS_PER_SEASON)) |
                                        PLANT_DAYS(SPRING(1), SPRING(6)),
                           .min_food_growth = 0.6f, .food_dug = true, .food_confusable = true,
                           .dug_whole = true},
    /* In the shallows and on the shore. A spreading rootstock: digging part
     * of it up leaves the rest growing. */
    [PLANT_CATTAIL] = {.id = "cattail", .name_key = N_("plant.cattail"), .growth_per_day = 0.1f,
                       .wetland = true, .move_cost = 1.3f, .growth_days = GROWING_SEASON,
                       .stock_capacity = STOCKS(0, 0, 4), .stock_regrow_hours = STOCKS(0, 0, 48),
                       .food_item = ITEM_CATTAIL_RHIZOMES, .food_days = PLANT_ALL_YEAR,
                       .min_food_growth = 0.5f, .food_dug = true},
    /* Appear fully grown, picked or gone within days (see core/game.c). Some
     * are deadly: a matter for skilled gatherers. */
    [PLANT_MUSHROOMS] = {.id = "mushrooms", .name_key = N_("plant.mushrooms"),
                         .move_cost = 1.0f, .stock_capacity = STOCKS(0, 0, 3),
                         .stock_regrow_hours = STOCKS(0, 0, 24), .food_item = ITEM_MUSHROOMS,
                         .food_days = PLANT_DAYS(AUTUMN(1), AUTUMN(DAYS_PER_SEASON)),
                         .food_confusable = true, .vanishes = true},
};

#undef SPRING
#undef SUMMER
#undef AUTUMN
#undef WINTER
#undef GROWING_SEASON
#undef STOCKS

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

ItemType plant_stock_item(PlantType type, PlantStock stock)
{
    switch (stock) {
    case PLANT_STOCK_BRANCHES: return ITEM_BRANCHES;
    case PLANT_STOCK_TWIGS: return ITEM_TWIGS;
    case PLANT_STOCK_FOOD:
        return (unsigned)type < PLANT_COUNT ? PLANT_DEFS[type].food_item : ITEM_NONE;
    default: return ITEM_NONE;
    }
}

int plant_stock_capacity(const Plant *plant, PlantStock stock)
{
    if ((unsigned)stock >= PLANT_STOCK_COUNT || (unsigned)plant->type >= PLANT_COUNT) {
        return 0;
    }
    const PlantDef *def = &PLANT_DEFS[plant->type];
    float growth = clampf(plant->growth, 0.0f, 1.0f);
    if (stock == PLANT_STOCK_FOOD &&
        (def->food_item == ITEM_NONE || growth < def->min_food_growth)) {
        return 0;
    }
    return (int)((float)def->stock_capacity[stock] * growth + 0.5f);
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

bool plant_days_contain(PlantDays days, int day_of_year)
{
    return day_of_year >= 0 && day_of_year < DAYS_PER_YEAR && ((days >> day_of_year) & 1u);
}

/* Whether a stock of a plant grows on a day of the year. */
static bool stock_in_season(const PlantDef *def, PlantStock stock, int day_of_year)
{
    return plant_days_contain(stock == PLANT_STOCK_FOOD ? def->food_days : def->growth_days,
                              day_of_year);
}

void plant_regrow_stocks(Plant *plant, int hours, int day_of_year)
{
    if (hours <= 0 || (unsigned)plant->type >= PLANT_COUNT) {
        return;
    }
    const PlantDef *def = &PLANT_DEFS[plant->type];
    for (int stock = 0; stock < PLANT_STOCK_COUNT; stock++) {
        bool in_season = stock_in_season(def, (PlantStock)stock, day_of_year);
        if (!in_season && stock == PLANT_STOCK_FOOD) {
            plant->stock[stock] = 0; /* Fallen, rotten or woody. */
            continue;
        }
        /* A stock above the limit (the plant was eaten down) drops to it. */
        int value = (int)plant->stock[stock] + (in_season ? hours : 0);
        plant->stock[stock] = (uint16_t)clampi(value, 0, stock_limit(plant, (PlantStock)stock));
    }
}

void plant_advance(Plant *plant, float fertility, int hours, int day_of_year)
{
    if (plant->type == PLANT_NONE || (unsigned)plant->type >= PLANT_COUNT || hours <= 0) {
        return;
    }
    const PlantDef *def = &PLANT_DEFS[plant->type];
    if (def->wetland) {
        fertility = 1.0f;
    }
    bool growing = plant_days_contain(def->growth_days, day_of_year);
    if (!growing && def->dies_back) {
        plant->growth = 0.0f;
        memset(plant->stock, 0, sizeof(plant->stock));
    } else {
        plant_regrow_stocks(plant, hours, day_of_year);
        if (growing && plant->growth < 1.0f && fertility >= def->min_fertility) {
            float growth = plant->growth + def->growth_per_day * fertility * (float)hours /
                                               (float)HOURS_PER_DAY;
            plant->growth = growth < 1.0f ? growth : 1.0f;
        }
    }
    if (def->vanishes && plant_stock_available(plant, PLANT_STOCK_FOOD) == 0) {
        memset(plant, 0, sizeof(*plant));
    }
}

int plant_growth_stage(const Plant *plant)
{
    int stage = (int)(clampf(plant->growth, 0.0f, 1.0f) * (float)PLANT_GROWTH_STAGES);
    return clampi(stage, 0, PLANT_GROWTH_STAGES - 1);
}

bool plant_has_food(const Plant *plant)
{
    return plant_stock_available(plant, PLANT_STOCK_FOOD) > 0;
}

bool plant_is_dormant(const Plant *plant)
{
    return (unsigned)plant->type < PLANT_COUNT && PLANT_DEFS[plant->type].dies_back &&
           plant->growth <= 0.0f;
}

void plant_settle(Plant *plant, int day_of_year)
{
    if ((unsigned)plant->type >= PLANT_COUNT) {
        return;
    }
    const PlantDef *def = &PLANT_DEFS[plant->type];
    if (def->dies_back && !plant_days_contain(def->growth_days, day_of_year)) {
        plant->growth = 0.0f;
    }
    for (int stock = 0; stock < PLANT_STOCK_COUNT; stock++) {
        bool full =
            stock != PLANT_STOCK_FOOD || stock_in_season(def, PLANT_STOCK_FOOD, day_of_year);
        int limit = full ? stock_limit(plant, (PlantStock)stock) : 0;
        plant->stock[stock] = (uint16_t)clampi(limit, 0, UINT16_MAX);
    }
}

bool map_init(Map *map, int width, int height, int depth)
{
    memset(map, 0, sizeof(*map));
    if (width < MAP_MIN_SIZE || height < MAP_MIN_SIZE || width > MAP_MAX_SIZE ||
        height > MAP_MAX_SIZE || depth < 1 || depth > MAP_MAX_LEVELS) {
        return false;
    }

    size_t count = (size_t)width * (size_t)height * (size_t)depth;
    map->terrain = malloc(count);
    map->rock = calloc(count, 1);
    map->slope = calloc(count, 1);
    map->plants = calloc(count, sizeof(Plant));
    map->cobbles = calloc(count, 1);
    map->flint = calloc(count, 1);
    map->structure = calloc(count, 1);
    map->structure_part = calloc(count, 1);
    map->structure_quality = calloc(count, 1);
    map->structure_hit_points = calloc(count, sizeof(*map->structure_hit_points));
    map->roof = calloc(count, 1);
    map->roof_quality = calloc(count, 1);
    if (map->terrain == NULL || map->rock == NULL || map->slope == NULL || map->plants == NULL ||
        map->cobbles == NULL || map->flint == NULL || map->structure == NULL ||
        map->structure_part == NULL || map->structure_quality == NULL ||
        map->structure_hit_points == NULL || map->roof == NULL || map->roof_quality == NULL) {
        map_free(map);
        return false;
    }
    size_t level = (size_t)width * (size_t)height;
    memset(map->terrain, TERRAIN_SOIL, level);
    memset(map->terrain + level, TERRAIN_OPEN, count - level);
    map->structure_counts[STRUCTURE_NONE] = (int)count;
    map->width = width;
    map->height = height;
    map->depth = depth;
    return true;
}

void map_free(Map *map)
{
    free(map->terrain);
    free(map->rock);
    free(map->slope);
    free(map->plants);
    free(map->cobbles);
    free(map->flint);
    free(map->structure);
    free(map->structure_part);
    free(map->structure_quality);
    free(map->structure_hit_points);
    free(map->roof);
    free(map->roof_quality);
    memset(map, 0, sizeof(*map));
}

static void log_topology_change(Map *map, int x, int y, int z, bool opened)
{
    map->topology_revision++;
    map->topology_log[map->topology_revision % MAP_TOPOLOGY_LOG_SIZE] =
        (MapTopologyChange){(int16_t)x, (int16_t)y, (int16_t)z, opened, false};
}

static void log_build_change(Map *map, int x, int y, int z)
{
    map->build_revision++;
    map->build_log[map->build_revision % MAP_BUILD_LOG_SIZE] =
        (MapBuildChange){(int16_t)x, (int16_t)y, (int16_t)z};
}

void map_set_terrain(Map *map, int x, int y, int z, Terrain terrain)
{
    if (!map_in_bounds(map, x, y, z) || (unsigned)terrain >= TERRAIN_COUNT) {
        return;
    }
    size_t index = map_index(map, x, y, z);
    if (map->terrain[index] != terrain) {
        map->terrain[index] = (uint8_t)terrain;
        /* May also turn neighbors into (or out of) drinking spots. */
        log_topology_change(map, x, y, z, false);
    }
}

void map_set_rock(Map *map, int x, int y, int z, RockType rock)
{
    if (!map_in_bounds(map, x, y, z) || (unsigned)rock >= ROCK_COUNT) {
        return;
    }
    size_t index = map_index(map, x, y, z);
    bool was_passable = map_move_cost_at(map, index) > 0.0f;
    bool was_rock = map->rock[index] != ROCK_NONE;
    bool was_open = map_is_open_at(map, index);
    map->rock[index] = (uint8_t)rock;
    bool is_passable = map_move_cost_at(map, index) > 0.0f;
    if (was_passable != is_passable) {
        log_topology_change(map, x, y, z, is_passable);
    } else if (was_open != map_is_open_at(map, index)) {
        log_topology_change(map, x, y, z, false); /* Over a slope: closes or opens it. */
    } else if (was_rock != (rock != ROCK_NONE)) {
        log_build_change(map, x, y, z); /* Rock where a wall stood: rooms, warmth. */
    }
}

void map_set_slope(Map *map, int x, int y, int z, bool slope)
{
    if (!map_in_bounds(map, x, y, z)) {
        return;
    }
    size_t index = map_index(map, x, y, z);
    if ((map->slope[index] != 0) != slope) {
        map->slope[index] = slope;
        map->slope_count += slope ? 1 : -1;
        log_topology_change(map, x, y, z, false);
    }
}

#define PART_DIRECTION_MASK 0x3u
#define PART_INDEX_SHIFT 2

/* Puts a structure tile (or none) on a tile: counts, plant, topology. */
static void set_tile_structure(Map *map, int x, int y, int z, StructureType type, uint8_t part,
                               ItemQuality quality, int hit_points)
{
    size_t index = map_index(map, x, y, z);
    StructureType old = (StructureType)map->structure[index];
    bool was_passable = map_move_cost_at(map, index) > 0.0f;
    map->structure_counts[old]--;
    map->structure_counts[type]++;
    map->structure[index] = (uint8_t)type;
    map->structure_part[index] = part;
    map->structure_quality[index] = (uint8_t)quality;
    map->structure_hit_points[index] = (uint16_t)hit_points;
    log_build_change(map, x, y, z);
    if (type != STRUCTURE_NONE) {
        map->plants[index] = (Plant){0}; /* Trampled, cut or dug out by the builders. */
    }
    bool is_passable = map_move_cost_at(map, index) > 0.0f;
    if (was_passable != is_passable) {
        log_topology_change(map, x, y, z, is_passable);
    }
}

bool map_structure_origin(const Map *map, int x, int y, int z, int *origin_x, int *origin_y)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
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

bool map_structure_is_origin(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) &&
           map->structure[map_index(map, x, y, z)] != STRUCTURE_NONE &&
           (map->structure_part[map_index(map, x, y, z)] >> PART_INDEX_SHIFT) == 0;
}

Direction map_structure_direction(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z) || map->structure[map_index(map, x, y, z)] == STRUCTURE_NONE) {
        return DIRECTION_SOUTH;
    }
    return (Direction)(map->structure_part[map_index(map, x, y, z)] & PART_DIRECTION_MASK);
}

void map_remove_structure(Map *map, int x, int y, int z)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, z, &origin_x, &origin_y)) {
        return;
    }
    StructureType type = map_structure(map, x, y, z);
    Direction direction = map_structure_direction(map, x, y, z);
    for (int part = 0; part < structure_length(type); part++) {
        int px = origin_x + part * direction_dx(direction);
        int py = origin_y + part * direction_dy(direction);
        if (map_structure(map, px, py, z) == type) {
            set_tile_structure(map, px, py, z, STRUCTURE_NONE, 0, ITEM_QUALITY_CRUDE, 0);
        }
    }
}

bool map_place_structure(Map *map, int x, int y, int z, StructureType type, Direction direction,
                         ItemQuality quality)
{
    if (type == STRUCTURE_NONE || (unsigned)type >= STRUCTURE_TYPE_COUNT ||
        structure_is_roof(type) || /* See map_set_roof(). */
        (unsigned)direction >= DIRECTION_COUNT) {
        return false;
    }
    int length = structure_length(type);
    int dx = direction_dx(direction), dy = direction_dy(direction);
    for (int part = 0; part < length; part++) {
        if (!map_in_bounds(map, x + part * dx, y + part * dy, z)) {
            return false;
        }
    }
    quality = (ItemQuality)clampi((int)quality, ITEM_QUALITY_CRUDE, ITEM_QUALITY_BEST);
    bool same = true; /* The same structure already there: only its condition changes. */
    for (int part = 0; part < length; part++) {
        size_t index = map_index(map, x + part * dx, y + part * dy, z);
        same = same && map->structure[index] == (uint8_t)type &&
               map->structure_part[index] ==
                   (uint8_t)((unsigned)direction | ((unsigned)part << PART_INDEX_SHIFT));
    }
    if (same) {
        size_t head = map_index(map, x, y, z);
        map->structure_quality[head] = (uint8_t)quality;
        map->structure_hit_points[head] = (uint16_t)structure_max_hit_points(type, quality);
        return true;
    }
    for (int part = 0; part < length; part++) {
        map_remove_structure(map, x + part * dx, y + part * dy, z);
    }
    for (int part = 0; part < length; part++) {
        bool head = part == 0;
        set_tile_structure(map, x + part * dx, y + part * dy, z, type,
                           (uint8_t)((unsigned)direction | ((unsigned)part << PART_INDEX_SHIFT)),
                           head ? quality : ITEM_QUALITY_CRUDE,
                           head ? structure_max_hit_points(type, quality) : 0);
    }
    return true;
}

void map_set_structure(Map *map, int x, int y, int z, StructureType type)
{
    if (type == STRUCTURE_NONE) {
        map_remove_structure(map, x, y, z);
    } else {
        map_place_structure(map, x, y, z, type, DIRECTION_SOUTH, ITEM_QUALITY_STANDARD);
    }
}

ItemQuality map_structure_quality(const Map *map, int x, int y, int z)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, z, &origin_x, &origin_y)) {
        return ITEM_QUALITY_CRUDE;
    }
    return (ItemQuality)map->structure_quality[map_index(map, origin_x, origin_y, z)];
}

int map_structure_hit_points(const Map *map, int x, int y, int z)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, z, &origin_x, &origin_y)) {
        return 0;
    }
    return map->structure_hit_points[map_index(map, origin_x, origin_y, z)];
}

void map_set_structure_hit_points(Map *map, int x, int y, int z, int hit_points)
{
    int origin_x, origin_y;
    if (!map_structure_origin(map, x, y, z, &origin_x, &origin_y)) {
        return;
    }
    size_t index = map_index(map, origin_x, origin_y, z);
    StructureType type = (StructureType)map->structure[index];
    int max = structure_max_hit_points(type, (ItemQuality)map->structure_quality[index]);
    map->structure_hit_points[index] = (uint16_t)clampi(hit_points, 1, max);
}

StructureType map_structure(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) ? (StructureType)map->structure[map_index(map, x, y, z)]
                                    : STRUCTURE_NONE;
}

void map_topology_changed(Map *map)
{
    log_topology_change(map, 0, 0, 0, false);
    map->topology_log[map->topology_revision % MAP_TOPOLOGY_LOG_SIZE].anywhere = true;
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

float map_move_cost(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) ? map_move_cost_at(map, map_index(map, x, y, z)) : 0.0f;
}

bool map_is_passable(const Map *map, int x, int y, int z)
{
    return map_move_cost(map, x, y, z) > 0.0f;
}

bool map_has_floor(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) && map->terrain[map_index(map, x, y, z)] != TERRAIN_OPEN;
}

bool map_is_open(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) && map_is_open_at(map, map_index(map, x, y, z));
}

bool map_is_slope(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) && map->slope[map_index(map, x, y, z)] != 0;
}

bool map_can_climb(const Map *map, int x, int y, int z, int nx, int ny)
{
    return abs(nx - x) + abs(ny - y) == 1 && map_is_slope(map, x, y, z) &&
           map_is_passable(map, x, y, z) && map_is_open(map, x, y, z + 1) &&
           map_is_passable(map, nx, ny, z + 1);
}

int map_level_links(const Map *map, int x, int y, int z, TilePos out[MAP_MAX_LEVEL_LINKS])
{
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    int count = 0;
    if (!map_is_passable(map, x, y, z)) {
        return 0;
    }
    for (int d = 0; d < 4; d++) {
        int nx = x + DX[d], ny = y + DY[d];
        if (map_can_climb(map, x, y, z, nx, ny)) {
            out[count++] = (TilePos){(int16_t)nx, (int16_t)ny, (int16_t)(z + 1)};
        }
        if (map_can_climb(map, nx, ny, z - 1, x, y)) {
            out[count++] = (TilePos){(int16_t)nx, (int16_t)ny, (int16_t)(z - 1)};
        }
    }
    return count;
}

bool map_is_buildable(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    return map->rock[index] == ROCK_NONE && map->slope[index] == 0 &&
           TERRAIN_DEFS[map->terrain[index]].buildable;
}

StructureType map_roof(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) ? (StructureType)map->roof[map_index(map, x, y, z)]
                                    : STRUCTURE_NONE;
}

ItemQuality map_roof_quality(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) ? (ItemQuality)map->roof_quality[map_index(map, x, y, z)]
                                    : ITEM_QUALITY_CRUDE;
}

void map_set_roof(Map *map, int x, int y, int z, StructureType roof, ItemQuality quality)
{
    if (!map_in_bounds(map, x, y, z) || (roof != STRUCTURE_NONE && !structure_is_roof(roof))) {
        return;
    }
    size_t index = map_index(map, x, y, z);
    map->roof_count += (roof != STRUCTURE_NONE) - (map->roof[index] != STRUCTURE_NONE);
    map->roof[index] = (uint8_t)roof;
    map->roof_quality[index] =
        roof != STRUCTURE_NONE
            ? (uint8_t)clampi((int)quality, ITEM_QUALITY_CRUDE, ITEM_QUALITY_BEST)
            : 0;
    log_build_change(map, x, y, z);
}

/* Rock, or a structure that blocks movement: what a roof rests on. */
static bool holds_up_roof(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    return map->rock[index] != ROCK_NONE || structure_blocks((StructureType)map->structure[index]);
}

bool map_roof_supported(const Map *map, int x, int y, int z)
{
    for (int dy = -ROOF_SUPPORT_RANGE; dy <= ROOF_SUPPORT_RANGE; dy++) {
        for (int dx = -ROOF_SUPPORT_RANGE; dx <= ROOF_SUPPORT_RANGE; dx++) {
            if (holds_up_roof(map, x + dx, y + dy, z)) {
                return true;
            }
        }
    }
    return false;
}

bool map_natural_roof(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z) || map->rock[map_index(map, x, y, z)] != ROCK_NONE) {
        return false;
    }
    for (int above = z + 1; above < map->depth; above++) {
        if (!map_is_open_at(map, map_index(map, x, y, above))) {
            return true;
        }
    }
    return false;
}

bool map_is_roofed(const Map *map, int x, int y, int z)
{
    return map_roof(map, x, y, z) != STRUCTURE_NONE || map_natural_roof(map, x, y, z);
}

uint64_t map_hash(const Map *map)
{
    uint64_t hash = HASH_FNV1A64_INIT;
    int32_t size[2] = {map->width, map->height};
    hash = hash_fnv1a64(hash, size, sizeof(size));
    if (map->depth != 1) { /* Hashes of one-level maps stay as they were. */
        hash = hash_fnv1a64(hash, &map->depth, sizeof(map->depth));
    }
    if (map->surface_level != 0) {
        hash = hash_fnv1a64(hash, &map->surface_level, sizeof(map->surface_level));
    }

    size_t count = map_tile_count(map);
    hash = hash_fnv1a64(hash, map->terrain, count);
    hash = hash_fnv1a64(hash, map->rock, count);
    if (map->slope_count > 0) { /* Hashes of maps without slopes stay as they were. */
        hash = hash_fnv1a64(hash, map->slope, count);
    }
    hash = hash_fnv1a64(hash, map->cobbles, count);
    hash = hash_fnv1a64(hash, map->flint, count);
    hash = hash_fnv1a64(hash, map->structure, count);
    hash = hash_fnv1a64(hash, map->structure_part, count);
    hash = hash_fnv1a64(hash, map->structure_quality, count);
    hash = hash_fnv1a64(hash, map->structure_hit_points,
                        count * sizeof(*map->structure_hit_points));
    if (map->roof_count > 0) { /* Hashes of maps without roofs stay as they were. */
        hash = hash_fnv1a64(hash, map->roof, count);
        hash = hash_fnv1a64(hash, map->roof_quality, count);
    }
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

int map_stones(const Map *map, int x, int y, int z, ItemType type)
{
    const uint8_t *layer = stone_layer(map, type);
    return layer != NULL && map_in_bounds(map, x, y, z) ? layer[map_index(map, x, y, z)] : 0;
}

void map_set_stones(Map *map, int x, int y, int z, int cobbles, int flint)
{
    if (!map_in_bounds(map, x, y, z)) {
        return;
    }
    size_t index = map_index(map, x, y, z);
    flint = clampi(flint, 0, MAP_STONES_MAX);
    map->flint[index] = (uint8_t)flint;
    map->cobbles[index] = (uint8_t)clampi(cobbles, 0, MAP_STONES_MAX - flint);
}

bool map_take_stone(Map *map, int x, int y, int z, ItemType type)
{
    uint8_t *layer = stone_layer(map, type);
    if (layer == NULL || !map_in_bounds(map, x, y, z) || layer[map_index(map, x, y, z)] == 0) {
        return false;
    }
    layer[map_index(map, x, y, z)]--;
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

bool map_plant_blocks_building(const Map *map, int x, int y, int z)
{
    return map_in_bounds(map, x, y, z) && map->plants[map_index(map, x, y, z)].type == PLANT_TREE;
}

bool map_has_edible_plant(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    const PlantDef *def = &PLANT_DEFS[plant->type];
    return !def->food_dug && !def->food_confusable && plant_has_food(plant);
}

bool map_tree_near(const Map *map, int x, int y, int z, int range)
{
    for (int ty = y - range; ty <= y + range; ty++) {
        for (int tx = x - range; tx <= x + range; tx++) {
            if (map_in_bounds(map, tx, ty, z)) {
                const Plant *plant = &map->plants[map_index(map, tx, ty, z)];
                if (plant->type == PLANT_TREE && plant->growth >= 1.0f) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool map_is_water(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    uint8_t terrain = map->terrain[map_index(map, x, y, z)];
    return terrain == TERRAIN_SHALLOW_WATER || terrain == TERRAIN_DEEP_WATER;
}

bool map_is_drinking_spot(const Map *map, int x, int y, int z)
{
    if (!map_is_passable(map, x, y, z)) {
        return false;
    }
    return map_is_water(map, x, y, z) || map_is_water(map, x - 1, y, z) ||
           map_is_water(map, x + 1, y, z) || map_is_water(map, x, y - 1, z) ||
           map_is_water(map, x, y + 1, z);
}

int map_ground_level(const Map *map, int x, int y)
{
    if (!map_in_bounds(map, x, y, 0)) {
        return 0;
    }
    for (int z = map->depth - 1; z >= 0; z--) {
        size_t index = map_index(map, x, y, z);
        if (map->rock[index] != ROCK_NONE) {
            return z + 1 < map->depth ? z + 1 : z;
        }
        if (map->terrain[index] != TERRAIN_OPEN) {
            return z;
        }
    }
    return 0;
}
