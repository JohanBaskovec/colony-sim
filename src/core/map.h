/*
 * The tile map: terrain, natural rock walls and plants, stored as dense layers
 * (one array per layer). The map is a volume: `depth` levels of width x height
 * tiles, each level 2 m high (a storey); a tile is (x, y, z), z the level, and
 * the layers hold the levels one after the other (index (z * height + y) *
 * width + x, see map_index()).
 *
 * A tile is solid (rock: a wall, or the inside of a hill), open with a floor
 * (its terrain: soil, sand, water...) or open without a floor
 * (TERRAIN_OPEN: the air above lower ground, where nobody can stand). One
 * walks on floors; natural slopes lead up to the level above (see
 * map_can_climb()). A floor or rock anywhere above a tile covers it (a
 * natural roof). A built roof is a floor too: laid on the open tile over a
 * hut's walls (the level above), nobody can stand on it (see map_roof()).
 *
 * Trees take room in the volume (see core/tree.h): their trunk is rock of
 * its own (ROCK_WOOD), their foliage a floor of branches (TERRAIN_FOLIAGE)
 * around the trunk, walked on, that one climbs up to along the trunk (see
 * map_can_climb_tree()).
 */
#ifndef COLONY_CORE_MAP_H
#define COLONY_CORE_MAP_H

#include "core/game_clock.h"
#include "core/item.h"
#include "core/structure.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAP_MIN_SIZE 16
#define MAP_MAX_SIZE 256
#define MAP_MAX_LEVELS 32

typedef enum Terrain {
    TERRAIN_DEEP_WATER,
    TERRAIN_SHALLOW_WATER,
    TERRAIN_SAND,
    TERRAIN_SOIL,
    TERRAIN_RICH_SOIL,
    TERRAIN_GRAVEL,
    TERRAIN_ROUGH_STONE,
    TERRAIN_OPEN,       /* No floor: open space over lower ground (impassable). */
    TERRAIN_GRASS_ROOF, /* A built roof (STRUCTURE_ROOF): too frail to stand on. */
    TERRAIN_FOLIAGE,    /* The branches of a tree around its trunk: walked on (core/tree.h). */
    TERRAIN_COUNT
} Terrain;

typedef enum RockType {
    ROCK_NONE = 0,
    ROCK_GRANITE,
    ROCK_SANDSTONE,
    ROCK_EARTH, /* The inside of soft hills: dug out, it gives nothing. */
    ROCK_WOOD,  /* The trunk of a tree (core/tree.h): felled, never mined. */
    ROCK_COUNT
} RockType;

typedef enum PlantType {
    PLANT_NONE = 0,
    PLANT_GRASS,
    PLANT_TREE,
    PLANT_HAZEL,       /* Shrub: hazelnuts in autumn, branches and twigs. */
    PLANT_BRAMBLE,     /* Thorny: blackberries at the end of summer. */
    PLANT_WILD_GARLIC, /* Leaves in spring, then withers under the trees. */
    PLANT_NETTLE,      /* Young shoots in spring and autumn. */
    PLANT_PLANTAIN,    /* Ribwort plantain: leaves from spring to autumn. */
    PLANT_WILD_CARROT, /* Its root is dug up from autumn to early spring. */
    PLANT_CATTAIL,     /* By the water: starchy rhizomes dug up all year. */
    PLANT_MUSHROOMS,   /* A patch under trees in autumn, gone in days. */
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
    bool buildable;  /* Whether structures can be built on it. */
    bool stony;      /* Whether stones lie about on it (see Map.cobbles). */
    /* The roof built as this floor (see map_set_roof()), STRUCTURE_NONE for
     * natural ground. */
    StructureType built;
} TerrainDef;

typedef struct RockDef {
    const char *id;
    const char *name_key;
    float mine_work;     /* Work (in ticks of a settler working) needed to mine one tile. */
    ItemType mined_item; /* Dropped when a tile is mined. */
    int mined_yield;
} RockDef;

/* Things a plant offers without being cut down, and that grow back. */
typedef enum PlantStock {
    PLANT_STOCK_BRANCHES,
    PLANT_STOCK_TWIGS,
    PLANT_STOCK_FOOD, /* Fruit, nuts, leaves or roots: PlantDef.food_item, in season. */
    PLANT_STOCK_COUNT
} PlantStock;

/* A set of days of the year (bit d: day d, 0-based, see game_day_of_year()). */
typedef uint64_t PlantDays;
_Static_assert(DAYS_PER_YEAR <= 64, "a year must fit in PlantDays");

/* Days `first` to `last` of the year (0-based, first <= last), both included. */
#define PLANT_DAYS(first, last)                                                                    \
    ((PlantDays)((((uint64_t)2 << (last)) - 1) & ~(((uint64_t)1 << (first)) - 1)))
#define PLANT_ALL_YEAR PLANT_DAYS(0, DAYS_PER_YEAR - 1)

typedef struct PlantDef {
    const char *id;
    const char *name_key;
    float growth_per_day; /* Growth gained per in-game day on fertility 1.0. */
    float min_fertility;  /* Does not spawn nor grow below this fertility. */
    /* Grows in shallow water and along the shore whatever the ground: the
     * fertility counts as 1.0 (see plant_advance()). */
    bool wetland;
    float move_cost;       /* Movement cost multiplier while walking through. */
    PlantDays growth_days; /* When it grows (and when its stocks grow back). */
    /* Withers on the other days: back to growth 0, nothing above ground, its
     * stocks gone, until it grows again. */
    bool dies_back;
    ItemType cut_item; /* Dropped when the plant is cut down (ITEM_NONE: nothing). */
    int cut_yield;     /* Items dropped at full growth, proportionally less when younger. */
    /* Stocks: at most `stock_capacity` items at full growth (proportionally
     * fewer when younger), one growing back every `stock_regrow_hours`. */
    int stock_capacity[PLANT_STOCK_COUNT];
    int stock_regrow_hours[PLANT_STOCK_COUNT];
    /* Food (PLANT_STOCK_FOOD): what it is, the days it ripens on (it falls,
     * rots or turns woody on the other days: the stock is gone) and the
     * growth the plant needs to bear any. */
    ItemType food_item; /* ITEM_NONE: none. */
    PlantDays food_days;
    float min_food_growth;
    bool food_dug; /* Roots: dug up (with a digging stick, best), never eaten in place. */
    /* Easily mistaken for a poisonous plant: only a skilled gatherer takes it
     * (see core/gather_zone.h), and nobody eats it in place. */
    bool food_confusable;
    bool dug_whole; /* Digging its food up takes the plant: back to growth 0. */
    /* Gone once its food is (taken, or out of season); appears on its own
     * (see core/game.c). */
    bool vanishes;
} PlantDef;

typedef struct Plant {
    uint8_t type; /* PlantType */
    /* A tree's trunk width in tiles (core/tree.h), 0 meaning 1; 0 for other
     * plants. */
    uint8_t size;
    /* Stocks, in hours of regrowth: one item per stock_regrow_hours. */
    uint16_t stock[PLANT_STOCK_COUNT];
    float growth; /* 0 (seedling) .. 1 (mature) */
} Plant;

/* Plants are drawn in this many sizes as they grow (see plant_growth_stage()). */
#define PLANT_GROWTH_STAGES 5

/* Most stones (cobbles and flint nodules together) lying about one tile. */
#define MAP_STONES_MAX 20

/* Most dead branches, and most twigs, lying on one tile (Map.branches). */
#define MAP_BRANCHES_MAX 2
#define MAP_TWIGS_MAX 4

/* How many recent topology changes the map remembers (see Map.topology_log). */
#define MAP_TOPOLOGY_LOG_SIZE 16

/* How many recent structure and roof changes the map remembers (see
 * Map.build_log). */
#define MAP_BUILD_LOG_SIZE 16

/* Movement cost multiplier of a step up or down a slope (see map_can_climb()),
 * of one up or down a ladder (slower, hands on the rungs), and of climbing
 * a tree's trunk to its foliage (map_can_climb_tree()). */
#define MAP_CLIMB_COST 2.0f
#define MAP_LADDER_CLIMB_COST 3.0f
#define MAP_TREE_CLIMB_COST 3.0f

/* A tile of the map. */
typedef struct TilePos {
    int16_t x;
    int16_t y;
    int16_t z; /* The level. */
} TilePos;

/* One change of where one can walk. */
typedef struct MapTopologyChange {
    int16_t x, y, z;
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
    int16_t x, y, z;
} MapBuildChange;

typedef struct Map {
    int width;
    int height;
    int depth; /* Levels, 1..MAP_MAX_LEVELS. */
    /* The level of the plains (shown to the player as level 0; the levels
     * below it are underground). Set by world generation, 0 by map_init(). */
    int surface_level;
    /* The highest level with ground at world generation: nothing above it
     * holds plants (open space where trees rise and roofs go, foliage where
     * nothing grows), so the game updates plants up to it only. depth - 1 by
     * map_init(). */
    int top_ground_level;
    uint8_t *terrain; /* Terrain per tile. */
    uint8_t *rock;    /* RockType per tile; non-zero means a solid rock wall. */
    /* Natural slopes: non-zero where the ground rises to the level above
     * (see map_can_climb()). */
    uint8_t *slope;
    int slope_count; /* Slope tiles. */
    Plant *plants;
    /* Stones lying about each tile, that gathering takes one by one: cobbles
     * and flint nodules (on flint patches), MAP_STONES_MAX at most together.
     * World generation puts none on sand (see gather_stone_ground()). */
    uint8_t *cobbles;
    uint8_t *flint;
    /* Dead branches and twigs lying on each tile that gathering takes: shed
     * by the trees onto the ground under their crown and onto their foliage
     * (see core/tree.h), up to MAP_BRANCHES_MAX and MAP_TWIGS_MAX a tile. */
    uint8_t *branches;
    uint8_t *twigs;
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
    /* The grade of each built roof (a terrain, see map_roof()), 0 elsewhere. */
    uint8_t *roof_quality;
    int roof_count; /* Built roof tiles. */
    /* What the player forbade the settlers on each tile (MapForbid bits, see
     * map_forbidden()): the plant, the structure (on its head tile), the
     * roof. */
    uint8_t *forbidden;
    int forbidden_count; /* Tiles with some bit set. */
    /* Incremented whenever a structure or a roof changes. Data derived from
     * it (core/roof.h, core/temperature.h) compares it. Not part of map_hash(). */
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

/* Item a stock of a plant type gives (branches, twigs, its food). */
ItemType plant_stock_item(PlantType type, PlantStock stock);

/* Most items of a stock the plant can hold at its current growth (food:
 * none below PlantDef.min_food_growth, whatever the season), times the area
 * of a wide tree's trunk (Plant.size). */
int plant_stock_capacity(const Plant *plant, PlantStock stock);

/* Items of a stock that can be taken now. */
int plant_stock_available(const Plant *plant, PlantStock stock);

/* Takes up to `count` items of a stock; returns how many were taken. */
int plant_take_stock(Plant *plant, PlantStock stock, int count);

/* Whether a day of the year (0-based) is in a set. */
bool plant_days_contain(PlantDays days, int day_of_year);

/* Lets the stocks grow back for `hours` on a day of the year, up to the
 * capacity: food only on its days (the stock is gone on the others), the
 * other stocks on the plant's growth days. */
void plant_regrow_stocks(Plant *plant, int hours, int day_of_year);

/*
 * One step of a plant's life on a tile of `fertility`, `hours` long on a day
 * of the year: its stocks grow back, it grows on its growth days (not below
 * its minimum fertility), withers on the others when it dies back, and goes
 * (type PLANT_NONE) when it vanishes and its food is gone.
 */
void plant_advance(Plant *plant, float fertility, int hours, int day_of_year);

/* Size class of a plant, 0 (seedling) .. PLANT_GROWTH_STAGES - 1 (mature):
 * growth split in equal steps. */
int plant_growth_stage(const Plant *plant);

/* Whether the plant bears food that can be taken now (ripe fruit, nuts...). */
bool plant_has_food(const Plant *plant);

/* Whether nothing of the plant shows above ground (withered until its growth
 * days come back). */
bool plant_is_dormant(const Plant *plant);

/* Puts a new plant in the state of a day of the year (new plants of a
 * generated world): withered when it dies back out of its growth days, every
 * stock full otherwise (food in season only). */
void plant_settle(Plant *plant, int day_of_year);

/* Allocates a map of `depth` levels, level 0 a floor of soil and the levels
 * above open (TERRAIN_OPEN), without rock, slopes, plants nor stones. */
bool map_init(Map *map, int width, int height, int depth);
void map_free(Map *map);

static inline bool map_in_bounds(const Map *map, int x, int y, int z)
{
    return x >= 0 && y >= 0 && z >= 0 && x < map->width && y < map->height && z < map->depth;
}

static inline size_t map_index(const Map *map, int x, int y, int z)
{
    return ((size_t)z * (size_t)map->height + (size_t)y) * (size_t)map->width + (size_t)x;
}

/* Whether a tile index (in bounds) is open space: no rock and no floor. */
static inline bool map_is_open_at(const Map *map, size_t index)
{
    return map->rock[index] == 0 && map->terrain[index] == TERRAIN_OPEN;
}

/* Things on a tile the player may forbid the settlers (bits of
 * Map.forbidden). Items and workstations keep theirs (core/item.h,
 * core/workstation.h). */
typedef enum MapForbid {
    MAP_FORBID_PLANT = 1 << 0,     /* Not picked, cut, felled nor climbed. */
    MAP_FORBID_STRUCTURE = 1 << 1, /* Not used, taken apart nor refreshed; a door or a
                                    * ladder is locked: nobody goes through. */
    MAP_FORBID_ROOF = 1 << 2,      /* Not taken down. */
} MapForbid;

/* Whether a structure forbidden on the tile (an index in bounds, the
 * structure's head) locks the way: a door or a ladder nobody may use. */
static inline bool map_locked_at(const Map *map, size_t index)
{
    return (map->structure[index] == STRUCTURE_DOOR || map->structure[index] == STRUCTURE_LADDER) &&
           (map->forbidden[index] & MAP_FORBID_STRUCTURE) != 0;
}

/* What a step up from the tile, or down onto it, costs (a multiplier of the
 * tile reached): MAP_CLIMB_COST on a slope, MAP_LADDER_CLIMB_COST on a
 * ladder (none when it is locked), 0 when the tile leads nowhere (see
 * map_can_climb()). */
static inline float map_climb_cost_at(const Map *map, size_t index)
{
    if (map->slope[index] != 0) {
        return MAP_CLIMB_COST;
    }
    return map->structure[index] == STRUCTURE_LADDER && !map_locked_at(map, index)
               ? MAP_LADDER_CLIMB_COST
               : 0.0f;
}

/* Tiles of one level. */
static inline size_t map_level_tile_count(const Map *map)
{
    return (size_t)map->width * (size_t)map->height;
}

/* Tiles of every level. */
static inline size_t map_tile_count(const Map *map)
{
    return map_level_tile_count(map) * (size_t)map->depth;
}

/* The tile of an index (the reverse of map_index()). */
static inline void map_tile_of(const Map *map, size_t index, int *x, int *y, int *z)
{
    size_t level = map_level_tile_count(map);
    *z = (int)(index / level);
    *y = (int)(index % level / (size_t)map->width);
    *x = (int)(index % (size_t)map->width);
}

/*
 * Terrain and rock must be changed through these once the map is in use (or
 * map_topology_changed() must be called after writing the layers directly),
 * so that topology_revision follows. Out-of-bounds tiles are ignored. A rock
 * change that leaves the tile as passable as before (granite to sandstone)
 * is not a topology change.
 */
void map_set_terrain(Map *map, int x, int y, int z, Terrain terrain);
void map_set_rock(Map *map, int x, int y, int z, RockType rock);
void map_set_slope(Map *map, int x, int y, int z, bool slope);
void map_topology_changed(Map *map);

/* Same rule for structures (a wall blocks movement, a door slows it). The
 * plants of its tiles are cleared when a structure is built.
 *
 * Builds a structure of a grade, in perfect condition, with its head at
 * (x, y) and its other tiles in `direction` (see structure_length()). Any
 * structure already on those tiles is removed whole. Fails (nothing changes)
 * when a tile is out of bounds. */
bool map_place_structure(Map *map, int x, int y, int z, StructureType type, Direction direction,
                         ItemQuality quality);

/* map_place_structure() of standard grade, facing south; STRUCTURE_NONE
 * removes the structure covering the tile (map_remove_structure()). */
void map_set_structure(Map *map, int x, int y, int z, StructureType type);

/* Removes the whole structure covering a tile, all its tiles. */
void map_remove_structure(Map *map, int x, int y, int z);

/* Head of the structure covering a tile (the tile itself for a one-tile
 * structure); false without a structure. */
bool map_structure_origin(const Map *map, int x, int y, int z, int *origin_x, int *origin_y);

/* Whether the tile holds the head of a structure. */
bool map_structure_is_origin(const Map *map, int x, int y, int z);

/* Direction of the structure covering a tile (DIRECTION_SOUTH without one). */
Direction map_structure_direction(const Map *map, int x, int y, int z);

/* Grade and hit points of the structure covering a tile (0 hit points
 * without one or out of bounds). */
ItemQuality map_structure_quality(const Map *map, int x, int y, int z);
int map_structure_hit_points(const Map *map, int x, int y, int z);

/* Sets the hit points of the structure covering a tile, clamped to 1..its
 * maximum (nothing without a structure). */
void map_set_structure_hit_points(Map *map, int x, int y, int z, int hit_points);

/* STRUCTURE_NONE out of bounds. */
StructureType map_structure(const Map *map, int x, int y, int z);

/* The roof built as the floor of a tile (its terrain; STRUCTURE_NONE
 * without one or out of bounds) and its grade. The roof of a hut is on the
 * level above its walls: it covers the tiles under it (map_roof_over()). */
StructureType map_roof(const Map *map, int x, int y, int z);
ItemQuality map_roof_quality(const Map *map, int x, int y, int z);

/* Builds a roof (a type for which structure_is_roof()) of a grade as the
 * floor of an open tile, or removes the roof of a tile (STRUCTURE_NONE: the
 * tile is open again). Other types, tiles with something else on them and
 * out-of-bounds tiles are ignored. */
void map_set_roof(Map *map, int x, int y, int z, StructureType roof, ItemQuality quality);

/* Whether a roof on the tile would rest on something: a wall (a structure
 * that blocks movement: walls, props) or rock within ROOF_SUPPORT_RANGE
 * tiles on the level below. */
bool map_roof_supported(const Map *map, int x, int y, int z);

/* The built roof that covers a tile: the first floor above it, when that is
 * a roof (STRUCTURE_NONE otherwise). */
StructureType map_roof_over(const Map *map, int x, int y, int z);

/* Whether the tile has a ceiling to hold up: no rock on it, and right above
 * it the ground of the level over it (a floor) or rock (a dug gallery). A
 * tree's foliage is no ceiling: its tree holds it. */
bool map_has_ceiling(const Map *map, int x, int y, int z);

/* Whether a ceiling over the tile is held up: rock or a wall (a prop...)
 * within ROOF_SUPPORT_RANGE tiles on its level. A built roof is the ceiling
 * of the tile under it (see map_roof_supported()). Tiles without a ceiling
 * need nothing. */
bool map_ceiling_supported(const Map *map, int x, int y, int z);

/* Whether digging the rock of (x, y, z) out would leave every ceiling around
 * it held up (its own included). `opens_above`: the ground over it goes too
 * (a ramp), leaving it no ceiling. */
bool map_dig_keeps_ceilings(const Map *map, int x, int y, int z, bool opens_above);

/* The level of what covers a tile without rock: the first floor or rock
 * above it (a built roof, a cave, a gallery dug into a hill); -1 when
 * nothing does (open sky), when the tile is rock or out of bounds. The
 * foliage of trees lets the sky through: it covers nothing. */
int map_cover_level(const Map *map, int x, int y, int z);

/* Whether something covers the tile (map_cover_level() is not -1). */
bool map_is_roofed(const Map *map, int x, int y, int z);

/* Whether it is covered by something natural, not a built roof (see
 * map_roof_over()). */
bool map_natural_roof(const Map *map, int x, int y, int z);

/* False when out of bounds, on impassable terrain (deep water, no floor), on
 * a rock wall or on a structure that blocks movement. */
bool map_is_passable(const Map *map, int x, int y, int z);

/* Whether the tile has a floor (its terrain is not TERRAIN_OPEN; false out
 * of bounds). Rock tiles have one under the rock. */
bool map_has_floor(const Map *map, int x, int y, int z);

/* Open space: in bounds, no rock and no floor. */
bool map_is_open(const Map *map, int x, int y, int z);

/* The level of what is seen on the tile (x, y) with level z shown: z itself
 * unless it is open space, else the first level down that is not, however
 * deep. -1 when nothing is seen there (open space all the way, or off the
 * map). */
int map_view_level(const Map *map, int x, int y, int z);

/* Whether a natural slope rises from the tile to the level above. */
bool map_is_slope(const Map *map, int x, int y, int z);

/* Whether one can walk from (x, y, z) up to the orthogonal neighbor
 * (nx, ny, z + 1): (x, y, z) is a slope or holds a ladder, with open space
 * above it, and one can stand on (nx, ny, z + 1). The same step taken
 * backwards leads down. Such steps cost map_climb_cost_at() times the tile
 * reached. */
bool map_can_climb(const Map *map, int x, int y, int z, int nx, int ny);

/* The way a ramp (a natural slope or a dug one) leads, as shown on a tile of
 * a level: up on a slope one can walk up from (map_can_climb()), down on
 * the open space over such a slope, where one walks down it from the level
 * above. Ladders are not ramps. */
typedef enum MapRamp {
    MAP_RAMP_NONE,
    MAP_RAMP_UP,
    MAP_RAMP_DOWN,
} MapRamp;
MapRamp map_ramp_at(const Map *map, int x, int y, int z);

/* What a step up from (x, y, z), or down onto it, costs: see
 * map_climb_cost_at(); 0 out of bounds. */
float map_climb_cost(const Map *map, int x, int y, int z);

/* Whether the tile is beside a tree's trunk (ROCK_WOOD on an orthogonal
 * neighbor, same level). */
bool map_beside_trunk(const Map *map, int x, int y, int z);

/* Whether one can climb from (x, y, z) straight up to (x, y, z + 1) along a
 * tree's trunk: both can be stood on, (x, y, z) is beside a trunk, and
 * (x, y, z + 1) is foliage. The same step taken backwards leads down. Such
 * steps cost MAP_TREE_CLIMB_COST times the tile reached. */
bool map_can_climb_tree(const Map *map, int x, int y, int z);

/* Whether a ladder on (x, y, z) would lead somewhere: open space above it
 * and the ground of an orthogonal neighbor on the level above. */
bool map_ladder_fits(const Map *map, int x, int y, int z);

/* The tiles of other levels one can step to from (x, y, z) (up and down
 * slopes and ladders, see map_can_climb(); up and down trees, see
 * map_can_climb_tree()): at most MAP_MAX_LEVEL_LINKS, stored in `out`;
 * returns how many. */
#define MAP_MAX_LEVEL_LINKS 10
int map_level_links(const Map *map, int x, int y, int z, TilePos out[MAP_MAX_LEVEL_LINKS]);

/* Whether the settlers are forbidden `what` (one MapForbid bit) on a tile:
 * only while there is such a thing there (a plant, a structure, given any of
 * its tiles, a roof). */
bool map_forbidden(const Map *map, int x, int y, int z, MapForbid what);

/* Removes the plant on a tile, and what the player forbade of it. */
void map_remove_plant(Map *map, int x, int y, int z);

/* Whether there is something to forbid on the tile (see map_forbidden()). */
bool map_can_forbid(const Map *map, int x, int y, int z, MapForbid what);

/* Forbids or allows `what` (one MapForbid bit) on a tile, when there is such
 * a thing there. Locking or unlocking a door or a ladder changes where one
 * can walk (topology_revision). Returns whether it changed. The bit goes
 * with what it is on: a structure taken apart, a roof taken down. */
bool map_set_forbidden(Map *map, int x, int y, int z, MapForbid what, bool forbidden);

/* Whether structures, stations and stockpiles can go on the tile: a floor of
 * buildable terrain, without rock, not a slope. */
bool map_is_buildable(const Map *map, int x, int y, int z);

/* Movement cost multiplier of a tile, or 0 when impassable. */
float map_move_cost(const Map *map, int x, int y, int z);

/* Same for a tile index known to be in bounds (hot loops: pathfinding). */
float map_move_cost_at(const Map *map, size_t index);

/* Content hash of all layers; equal maps give equal hashes. */
uint64_t map_hash(const Map *map);

size_t map_count_plants(const Map *map, PlantType type);

/* Whether the plant on a tile keeps anything from being built there: a tree,
 * even a sapling (felling needs an axe). Grass and bushes are cleared by
 * hand first (see gather_clear_plant()). */
bool map_plant_blocks_building(const Map *map, int x, int y, int z);

/* Whether the plant on this tile bears food a settler may eat in place right
 * now (not roots, nor anything easily mistaken for a poisonous plant). */
bool map_has_edible_plant(const Map *map, int x, int y, int z);

/* Whether a mature tree (fully grown) stands within `range` tiles (square). */
bool map_tree_near(const Map *map, int x, int y, int z, int range);

/* Stones of a type (ITEM_COBBLE or ITEM_FLINT) lying about a tile; 0 for other
 * types and out of bounds. */
int map_stones(const Map *map, int x, int y, int z, ItemType type);

/* Sets the stones of a tile: each count is clamped to 0..MAP_STONES_MAX, then
 * cobbles to what flint leaves of MAP_STONES_MAX. Out-of-bounds tiles are
 * ignored. */
void map_set_stones(Map *map, int x, int y, int z, int cobbles, int flint);

/* Takes one stone of a type from a tile; false when there is none. */
bool map_take_stone(Map *map, int x, int y, int z, ItemType type);

/* Branches (ITEM_BRANCHES) or twigs (ITEM_TWIGS) lying on a tile; 0 for other
 * types and out of bounds. */
int map_litter(const Map *map, int x, int y, int z, ItemType type);

/* Lays one branch or twig on a tile; false when the tile is full, out of
 * bounds or for another type. Does not look at what the tile is (see
 * tree_shed()). */
bool map_add_litter(Map *map, int x, int y, int z, ItemType type);

/* Takes all the branches or twigs of a tile; returns how many were there. */
int map_take_litter(Map *map, int x, int y, int z, ItemType type);

/* The level of the ground at (x, y): the highest tile of the column one
 * could stand on (the top of the highest rock or floor), trees left out
 * (their trunks and foliage). */
int map_ground_level(const Map *map, int x, int y);

/* Shallow or deep water (false out of bounds). */
bool map_is_water(const Map *map, int x, int y, int z);

/* Whether a settler standing on this tile can drink: the tile is passable and
 * is shallow water or touches water (4-neighborhood). */
bool map_is_drinking_spot(const Map *map, int x, int y, int z);

#endif /* COLONY_CORE_MAP_H */
