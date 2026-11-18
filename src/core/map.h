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
    TERRAIN_FOLIAGE,    /* Dense branches of a tree around its trunk: walked on (core/tree.h). */
    TERRAIN_FOLIAGE_MEDIUM, /* Thinner branches further out: not walked on. */
    TERRAIN_FOLIAGE_SPARSE, /* The thinnest, at the edge of the crown: not walked on. */
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
    PLANT_HAZEL,           /* A small tree (a shrub as a seedling): hazelnuts fall in autumn. */
    PLANT_BRAMBLE,         /* Thorny: blackberries at the end of summer. */
    PLANT_WILD_GARLIC,     /* Leaves in spring, then withers under the trees. */
    PLANT_NETTLE,          /* Young shoots in spring and autumn. */
    PLANT_PLANTAIN,        /* Ribwort plantain: leaves from spring to autumn. */
    PLANT_WILD_CARROT,     /* Its root is dug up from autumn to early spring. */
    PLANT_CATTAIL,         /* By the water: starchy rhizomes dug up all year. */
    PLANT_MUSHROOMS,       /* A patch under trees in autumn, gone in days. */
    PLANT_MOSS,            /* A carpet on the shaded forest floor: bedding. */
    PLANT_WOOD_SORREL,     /* Sour clover-like leaves in deep shade. */
    PLANT_BRACKEN,         /* Tall fronds on poor ground, cut brown in autumn: bedding, thatch. */
    PLANT_WILD_STRAWBERRY, /* Small fruit at the start of summer, in clearings. */
    PLANT_BILBERRY,        /* A low shrub of poor ground: berries in summer. */
    PLANT_DOG_ROSE,        /* Thorny: its hips ripen in autumn and stay through winter. */
    PLANT_HEATHER,         /* Low and woody on sand and gravel: bedding, thatch, fuel. */
    PLANT_REED,            /* In the shallows: tall stems cut dry in winter, the best thatch. */
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

/* Most products (stocks) one plant bears: a plant holds one stock of each
 * (Plant.stock[slot], PlantDef.products[slot]). */
#define PLANT_PRODUCTS_MAX 2

/* A set of days of the year (bit d: day d, 0-based, see game_day_of_year()). */
typedef uint64_t PlantDays;
_Static_assert(DAYS_PER_YEAR <= 64, "a year must fit in PlantDays");

/* Days `first` to `last` of the year (0-based, first <= last), both included. */
#define PLANT_DAYS(first, last)                                                                    \
    ((PlantDays)((((uint64_t)2 << (last)) - 1) & ~(((uint64_t)1 << (first)) - 1)))
#define PLANT_ALL_YEAR PLANT_DAYS(0, DAYS_PER_YEAR - 1)

/* What taking a plant's products does to the plant. */
typedef enum PlantHarvest {
    PLANT_HARVEST_KEEPS,   /* It stays as it is and the stock grows back (fruit, leaves). */
    PLANT_HARVEST_RESETS,  /* Back to growth 0, growing again from what is left in the ground
                              (roots dug up, grass cut). */
    PLANT_HARVEST_REMOVES, /* The plant is gone with its product (a patch of mushrooms). */
} PlantHarvest;

/*
 * Something a plant offers without being cut down, and that grows back: fruit,
 * leaves, roots, an armful of grass... Its stock holds up to `capacity` items
 * at full growth (proportionally fewer when younger), one growing back every
 * `regrow_hours`; settlers gather it as the resource of its item
 * (core/gather_zone.h), and animals eat what is food.
 */
typedef struct PlantProduct {
    ItemType item; /* ITEM_NONE: no product in this slot. */
    int capacity;
    int regrow_hours;
    /* When it is on the plant (docs/PLANTS.md §7): the warmth the plant has
     * gathered since it woke, in degree-days above its `t_min` (Plant.warmth),
     * must lie from `warmth_from` to `warmth_until` (0: no end: fruit ripens,
     * young leaves turn tough), and its climate not have fallen below
     * `lost_below` degrees (0: never: the fruit rots in the autumn chill);
     * out of that the stock is gone. `min_growth` is the growth the plant
     * needs to bear any. */
    float warmth_from, warmth_until, lost_below;
    float min_growth;
    bool dug; /* Dug up (with a digging stick, best), never eaten in place. */
    /* Easily mistaken for a poisonous plant: only a skilled gatherer takes it
     * (see core/gather_zone.h), and nobody eats it in place. */
    bool confusable;
} PlantProduct;

/* How a plant lives through the cold (docs/PLANTS.md §4). */
typedef enum PlantRest {
    PLANT_REST_SLEEPS,    /* It stays as it is: nothing grows, no stock grows back. */
    PLANT_REST_DIES_BACK, /* What is above ground dies (growth 0, stocks gone, nothing
                             shown) and the roots wait for the warmth. */
} PlantRest;

typedef struct PlantDef {
    const char *id;
    const char *name_key;
    /* Growth gained per in-game day at its best: the temperature (and soon the
     * light) at their best, on fertility 1.0. */
    float growth_per_day;
    float min_fertility; /* Does not spawn nor grow below this fertility. */
    /* Grows in shallow water and along the shore whatever the ground: the
     * fertility counts as 1.0 (see plant_advance()). */
    bool wetland;
    /* A plant of poor ground (heather, bracken): it grows as well on any
     * ground it lives on, from `min_fertility` up (the fertility counts as
     * 1.0, see plant_advance()). */
    bool poor_soil;
    float move_cost; /* Movement cost multiplier while walking through. */
    /* Loses its leaves in the cold (docs/PLANTS.md §12): the crowns of the
     * trees of this kind let the light through in winter (Map.bare). */
    bool deciduous;
    /* Growth by the temperature of its tile (degrees Celsius): none below
     * `t_min` nor above `t_max`, full from `t_opt_low` to `t_opt_high`, in
     * proportion between. */
    float t_min, t_opt_low, t_opt_high, t_max;
    /* What harms it: frost below `t_frost_active` while it grows, below
     * `t_frost_hardened` when fully hardened by the cold (the limit goes from
     * one to the other over the days, see Plant.hardening), and heat above
     * `t_lethal_heat`. */
    float t_frost_active, t_frost_hardened, t_lethal_heat;
    /* How it rests when the climate (the mean temperature of the last days)
     * falls below `t_min`; `rests_in_heat`: also when it rises above `t_max`
     * (wild garlic, a plant of the cool spring), by dying back until the next
     * winter has passed. */
    PlantRest rest;
    bool rests_in_heat;
    /* Growth by the light on its tile (0 .. 1, docs/PLANTS.md §5): none below
     * `light_min`, full from `light_full`, in proportion between. While awake,
     * a plant whose best light of the day stays below `light_min` takes
     * damage and dies after `dark_days` days in the dark (0: lives without
     * light, as fungi do). */
    float light_min, light_full, dark_days;
    ItemType cut_item; /* Dropped when the plant is cut down (ITEM_NONE: nothing). */
    int cut_yield;     /* Items dropped at full growth, proportionally less when younger. */
    /* What it bears (a slot per product), and what harvesting does to it. A
     * plant that vanishes also goes once its products are gone (taken, or out
     * of season), and appears on its own (see core/game.c). */
    PlantProduct products[PLANT_PRODUCTS_MAX];
    PlantHarvest harvest;
    /* Grazing animals eat it down, bite by bite (Plant.growth, see
     * core/animal.h). */
    bool grazed;
    /* What a tree lets fall in a season (see core/tree.h): `fall_yield` items of
     * `fall_item` over `fall_days` for a fully grown tree (ITEM_NONE: none). They
     * lie under it like its twigs (Map.nuts). */
    ItemType fall_item;
    PlantDays fall_days;
    int fall_yield;
    /* How it spreads (docs/PLANTS.md §13): awake and grown to `spread_growth`,
     * it sends out a runner or a seed with `spread_chance` a day, to a tile up
     * to `spread_range` tiles away, where a young plant takes root if the tile
     * is free and suits it (plant_can_take_root()). 0: it does not spread
     * (mushrooms come up on their own). */
    float spread_chance;
    float spread_growth;
    int spread_range;
} PlantDef;

/* Whether a plant type is a tree (takes room once grown, see core/tree.h). */
static inline bool plant_is_tree(uint8_t type)
{
    return type == PLANT_TREE || type == PLANT_HAZEL;
}

/* What a plant is doing (Plant.state, low bits; the high bit says its
 * climate is known). */
typedef enum PlantState {
    PLANT_STATE_AWAKE,     /* Growing when the weather lets it. */
    PLANT_STATE_RESTING,   /* Rests through the cold (PLANT_REST_SLEEPS). */
    PLANT_STATE_DIED_BACK, /* Dead above ground, the roots wait (PLANT_REST_DIES_BACK). */
    /* The same in the heat (PlantDef.rests_in_heat): a plant of the cool spring is
     * gone for the summer, and waits for the cold of winter before it wakes. */
    PLANT_STATE_SUMMER_DORMANT,
} PlantState;

/* A plant's damage, up to which it is dead (0: unharmed). */
#define PLANT_DAMAGE_MAX 65535

/* A plant's life (tuning, docs/PLANTS.md): its climate is the mean of the last
 * PLANT_CLIMATE_HOURS hours; it goes to rest below `t_min` and wakes
 * PLANT_REST_HYSTERESIS degrees above it; its hardening moves by
 * PLANT_HARDEN_PER_HOUR in the cold and PLANT_SOFTEN_PER_HOUR otherwise (of
 * 255); frost and heat take PLANT_FROST_DAMAGE and PLANT_HEAT_DAMAGE of its
 * health per degree beyond the limit and hour; it mends PLANT_RECOVERY of its
 * health an hour when all is well. */
#define PLANT_CLIMATE_HOURS 72.0f
#define PLANT_REST_HYSTERESIS 1.0f
#define PLANT_HARDEN_PER_HOUR 3
#define PLANT_SOFTEN_PER_HOUR 5
#define PLANT_FROST_DAMAGE 0.02f
#define PLANT_HEAT_DAMAGE 0.02f
#define PLANT_RECOVERY (0.25f / 24.0f)
/* PlantDef.growth_per_day is what a plant grows in a day at its best: that of
 * this many hours of full light (the night makes none). */
#define PLANT_BEST_DAY_HOURS 12.0f
/* The health a tree with a trunk keeps at the least (it never dies of the weather). */
#define PLANT_TREE_HEALTH_FLOOR 0.05f

/* The climate (Plant.climate) is kept in 1/256 degrees. */
#define PLANT_CLIMATE_SCALE 256.0f

typedef struct Plant {
    uint8_t type; /* PlantType */
    /* A tree's trunk width in tiles (core/tree.h), 0 meaning 1; 0 for other
     * plants. */
    uint8_t size;
    /* Stocks, in hours of regrowth: one item per PlantProduct.regrow_hours. */
    uint16_t stock[PLANT_PRODUCTS_MAX];
    /* Worn down by frost, heat and (docs/PLANTS.md) darkness, 0 (unharmed) ..
     * PLANT_DAMAGE_MAX (dead). It slows growth and heals when conditions are good. */
    uint16_t damage;
    float growth; /* 0 (seedling) .. 1 (mature) */
    /* Degree-days gathered above its `t_min` since it woke (0 when it died back
     * and came back): what ripens its products (PlantProduct.warmth_from). */
    float warmth;
    /* The mean temperature of the last days at its tile, in 1/256 degrees:
     * what decides when it rests and wakes. */
    int16_t climate;
    /* How hardened by the cold it is: 0 (growing, tender) .. 255 (fully hardened). */
    uint8_t hardening;
    uint8_t state; /* PlantState, plus PLANT_CLIMATE_KNOWN. */
} Plant;
_Static_assert(sizeof(Plant) == 20, "a plant is five words");

/* Set in Plant.state once `climate` holds the tile's temperatures (a plant
 * made by hand takes its first from the tile). */
#define PLANT_CLIMATE_KNOWN 0x80

/* What a plant meets on its tile in an hour. */
typedef struct PlantConditions {
    float temperature; /* Of its tile, in degrees Celsius. */
    float fertility;   /* Of its ground (TerrainDef.fertility). */
    /* The light on its tile now, 0 .. 1 (the sun, the sky and what covers the
     * tile), and the best it gets in the day (the noon sun): it is dark when
     * even that is low. */
    float light;
    float light_peak;
} PlantConditions;

/* What a plant is up to, for the player (the plant window). */
typedef enum PlantStatus {
    PLANT_STATUS_GROWING,
    PLANT_STATUS_SLOWED_COLD, /* Cooler than it likes, growing slowly. */
    PLANT_STATUS_SLOWED_HOT,
    PLANT_STATUS_SLOWED_DARK, /* Little light. */
    PLANT_STATUS_TOO_COLD,    /* Too cold to grow, or too hot, or too dark. */
    PLANT_STATUS_TOO_HOT,
    PLANT_STATUS_TOO_DARK,
    PLANT_STATUS_FROST, /* Suffering from frost. */
    PLANT_STATUS_HEAT,  /* Suffering from heat. */
    PLANT_STATUS_RESTING,
    PLANT_STATUS_DIED_BACK,
    PLANT_STATUS_COUNT
} PlantStatus;

/* Plants are drawn in this many sizes as they grow (see plant_growth_stage()). */
#define PLANT_GROWTH_STAGES 5

/* Most stones (cobbles and flint nodules together) lying about one tile. */
#define MAP_STONES_MAX 20

/* Most dead branches, and most twigs, lying on one tile (Map.branches). */
#define MAP_BRANCHES_MAX 2
#define MAP_TWIGS_MAX 4

/* Most hazelnuts lying on one tile (Map.nuts). */
#define MAP_NUTS_MAX 8

/* How many recent topology changes the map remembers (see Map.topology_log). */
#define MAP_TOPOLOGY_LOG_SIZE 16

/* How many recent structure and roof changes the map remembers (see
 * Map.build_log). */
#define MAP_BUILD_LOG_SIZE 16

/* How many recent felled trees the map remembers (see Map.tree_fall_log). */
#define MAP_TREE_FALL_LOG_SIZE 16

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

/* A tree felled (tree_fell(), core/tree.h): where its foot stood, how wide
 * its trunk was, which way it fell (a Direction) and how many tiles it
 * covers; the trunk lies on the first `trunk` of them, the crown beyond. */
typedef struct MapTreeFall {
    int16_t x, y, z;
    uint8_t size;
    uint8_t direction;
    uint8_t length;
    uint8_t trunk;
} MapTreeFall;

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
     * by the trees onto the ground under their crown (see core/tree.h), up to MAP_BRANCHES_MAX and
     * MAP_TWIGS_MAX a tile. */
    uint8_t *branches;
    uint8_t *twigs;
    /* Hazelnuts lying on each tile, fallen from the trees in autumn like twigs
     * (up to MAP_NUTS_MAX). They rot away on average (see core/game.c). */
    uint8_t *nuts;
    /* Built structures (StructureType on every tile they take): beds,
     * walls, doors. `structure_part` holds the structure's Direction (bits
     * 0-1) and which of its tiles this is (bits 2-3, 0 for the head); the
     * quality (ItemQuality) and hit points are kept on the head (see
     * core/structure.h). */
    uint8_t *structure;
    uint8_t *structure_part;
    uint8_t *structure_quality;
    uint16_t *structure_hit_points;
    int structure_counts[STRUCTURE_TYPE_COUNT]; /* Tiles holding each type. */
    /* The quality of each built roof (a terrain, see map_roof()), 0 elsewhere. */
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
    /* Incremented whenever the terrain, the rock or a roof of a tile changes:
     * what lets the light through (map_light_cover()). A cache of it compares
     * this. Not part of map_hash(). */
    uint32_t light_revision;
    /* How bare the crowns of the deciduous trees are, 0 (in full leaf) to
     * MAP_BARE_STEPS (bare branches), set by the game from the climate (see
     * plant_leaf_target()); a layer of foliage lets more light through the
     * barer it is. Part of map_hash() once it is not 0. Use map_set_bare(). */
    uint8_t bare;
    /* The change that led to revision r is at index r % MAP_TOPOLOGY_LOG_SIZE. */
    MapTopologyChange topology_log[MAP_TOPOLOGY_LOG_SIZE];
    /* Incremented whenever a tree is felled: who stood where it came down is
     * the game's business. Fall number r is at index r % MAP_TREE_FALL_LOG_SIZE.
     * Not part of map_hash(). */
    uint32_t tree_fall_revision;
    MapTreeFall tree_fall_log[MAP_TREE_FALL_LOG_SIZE];
} Map;

const TerrainDef *terrain_def(Terrain terrain);
const RockDef *rock_def(RockType rock);
const PlantDef *plant_def(PlantType plant);

/* The product of a plant type in a slot (NULL: none there). */
const PlantProduct *plant_product(PlantType type, int slot);

/* Item a stock of a plant type gives (ITEM_NONE: none). */
ItemType plant_stock_item(PlantType type, int slot);

/* Most items of a stock the plant can hold at its current growth (none below
 * PlantProduct.min_growth, whatever the season), times the area of a wide
 * tree's trunk (Plant.size). */
int plant_stock_capacity(const Plant *plant, int slot);

/* Items of a stock that can be taken now. */
int plant_stock_available(const Plant *plant, int slot);

/* Takes up to `count` items of a stock; returns how many were taken. */
int plant_take_stock(Plant *plant, int slot, int count);

/* Whether a day of the year (0-based) is in a set. */
bool plant_days_contain(PlantDays days, int day_of_year);

/* Whether a product is in season on the plant: the warmth gathered and the
 * climate say it is on it (PlantProduct.warmth_from). */
bool plant_product_in_season(const Plant *plant, int slot);

/* Lets the stocks grow back for `hours`, up to the capacity, while in
 * season (the stock is gone out of it); `hours` 0: only drops what is above
 * the limit or out of season. */
void plant_regrow_stocks(Plant *plant, int hours);

/*
 * `hours` hours of a plant's life in `conditions`: its climate follows the
 * tile's temperature; it goes to rest or wakes (a few days of cold, or of
 * heat for a plant that rests in it), hardens or softens, takes damage from
 * frost, heat (and later darkness) and mends when all is well; it grows at
 * its best rate times the temperature's response, its health and the
 * fertility (not below its minimum); its stocks grow back while it is awake;
 * and it goes (type PLANT_NONE) when it dies, or when it vanishes and its
 * products are gone. A tree with a trunk (core/tree.h) never dies of it.
 */
void plant_advance(Plant *plant, const PlantConditions *conditions, int hours);

/* The response of a plant to a temperature, 0 .. 1 (docs/PLANTS.md §2). */
float plant_temperature_response(PlantType type, float celsius);

/* The response of a plant to the light, 0 .. 1 (docs/PLANTS.md §5). */
float plant_light_response(PlantType type, float light);

/* Frost limit of a plant now: from its tender to its hardened one by its
 * hardening. */
float plant_frost_limit(const Plant *plant);

/* How bare the crowns of deciduous trees are in a climate (the running mean
 * of the air's temperature, like Plant.climate): in full leaf from
 * PLANT_LEAF_FULL_ABOVE degrees, bare below PLANT_LEAF_BARE_BELOW, in
 * between losing or growing leaves; 0 (in leaf) .. 1 (bare). */
#define PLANT_LEAF_BARE_BELOW 5.0f
#define PLANT_LEAF_FULL_ABOVE 12.0f
float plant_bare_target(float climate);

/*
 * Spread (docs/PLANTS.md §13). A plant takes root on a tile that is free (no
 * plant, rock nor built structure) and suits it: its ground (a fertility of
 * at least its `min_fertility` on sand, soil or gravel; a plant of the wet by
 * the water or in the shallows at the shore), enough light under the crowns
 * in leaf (the cover times PLANT_SPREAD_SUN, at least its `light_min`; the
 * cover of the season for a plant of the spring, which rests in the heat),
 * and for a tree no other tree nor trunk within TREE_SPREAD_SPACING tiles.
 */
#define PLANT_SPREAD_SUN 0.85f
#define TREE_SPREAD_SPACING 2
/* A young plant's growth when it takes root. */
#define PLANT_SPREAD_GROWTH 0.05f
bool plant_can_take_root(const Map *map, PlantType type, int x, int y, int z);

/* The tile a plant on (x, y, z) sends a runner or seed to: one of those within
 * its `spread_range` (not its own), picked by `roll` (0 .. 1). False when the
 * plant does not spread or the tile is off the map. */
bool plant_spread_target(const Map *map, int x, int y, int z, float roll, int *out_x, int *out_y);

/* A young plant from `parent`: of its kind, at PLANT_SPREAD_GROWTH, awake in
 * the climate and hardening of its parent (the same air), with the warmth its
 * parent gathered this year, and no stock yet. */
Plant plant_offspring(const Plant *parent);

/* Growth per day the plant makes in `conditions` (0 when it rests or cannot). */
float plant_growth_rate(const Plant *plant, const PlantConditions *conditions);

/* What the plant is up to in `conditions`. */
PlantStatus plant_status(const Plant *plant, const PlantConditions *conditions);

/* Translation key of the name of a status. */
const char *plant_status_name_key(PlantStatus status);

/* A plant's health, 0 (dead) .. 1. */
float plant_health(const Plant *plant);

/* Size class of a plant, 0 (seedling) .. PLANT_GROWTH_STAGES - 1 (mature):
 * growth split in equal steps. */
int plant_growth_stage(const Plant *plant);

/* The slot of the product of a plant type that is food (a settler can eat it;
 * -1: none). */
int plant_food_slot(PlantType type);

/* Whether the plant bears food that can be taken now (ripe fruit, nuts...). */
bool plant_has_food(const Plant *plant);

/* What a harvest does to the plant (PlantDef.harvest), once its products are
 * taken: nothing, its growth back to 0 (and its stocks emptied), or the plant
 * gone (the tile is then bare; "gone" only when no product is left). */
void plant_harvested(Map *map, int x, int y, int z);

/* Whether nothing of the plant shows above ground (died back until the warmth
 * returns). */
bool plant_is_dormant(const Plant *plant);

/* Puts a new plant in the state of a day of the year (new plants of a
 * generated world): its climate the expected one of that day (so it rests,
 * hardened, in winter, and a plant that dies back has died back), every stock
 * full (on the days of its product only). */
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

/* Whether a terrain is a tree's foliage, of any density (the dense one is the
 * only one to walk on). */
static inline bool terrain_is_foliage(uint8_t terrain)
{
    return terrain >= TERRAIN_FOLIAGE && terrain <= TERRAIN_FOLIAGE_SPARSE;
}

/* Whether a tile index (in bounds) is open space: no rock and no floor. */
static inline bool map_is_open_at(const Map *map, size_t index)
{
    return map->rock[index] == 0 && map->terrain[index] == TERRAIN_OPEN;
}

/* Whether the eye (and whatever falls) goes through a tile index: open space,
 * or thin foliage (the medium and sparse densities: leaves and bare branches
 * over nothing). */
static inline bool map_sees_through_at(const Map *map, size_t index)
{
    return map->rock[index] == 0 &&
           (map->terrain[index] == TERRAIN_OPEN || map->terrain[index] == TERRAIN_FOLIAGE_MEDIUM ||
            map->terrain[index] == TERRAIN_FOLIAGE_SPARSE);
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
 * Builds a structure of a quality, in perfect condition, with its head at
 * (x, y) and its other tiles in `direction` (see structure_length()). Any
 * structure already on those tiles is removed whole. Fails (nothing changes)
 * when a tile is out of bounds. */
bool map_place_structure(Map *map, int x, int y, int z, StructureType type, Direction direction,
                         ItemQuality quality);

/* map_place_structure() of standard quality, facing south; STRUCTURE_NONE
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

/* Quality and hit points of the structure covering a tile (0 hit points
 * without one or out of bounds). */
ItemQuality map_structure_quality(const Map *map, int x, int y, int z);
int map_structure_hit_points(const Map *map, int x, int y, int z);

/* Sets the hit points of the structure covering a tile, clamped to 1..its
 * maximum (nothing without a structure). */
void map_set_structure_hit_points(Map *map, int x, int y, int z, int hit_points);

/* STRUCTURE_NONE out of bounds. */
StructureType map_structure(const Map *map, int x, int y, int z);

/* The roof built as the floor of a tile (its terrain; STRUCTURE_NONE
 * without one or out of bounds) and its quality. The roof of a hut is on the
 * level above its walls: it covers the tiles under it (map_roof_over()). */
StructureType map_roof(const Map *map, int x, int y, int z);
ItemQuality map_roof_quality(const Map *map, int x, int y, int z);

/* Builds a roof (a type for which structure_is_roof()) of a quality as the
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

/* How much of the light reaches the tile through what lies over it, 0 .. 1:
 * all of it in the open; each layer of foliage over it lets through its share
 * (MAP_LIGHT_FOLIAGE_*), so that a crown of several layers is darker than a
 * thin one and the layers multiply; a built roof MAP_LIGHT_ROOF; rock or the
 * ground of a level above nothing. The trunk of a tree over its own foot is
 * not a cover. */
float map_light_cover(const Map *map, int x, int y, int z);
/* The same with the crowns in full leaf, whatever the season: the shade of
 * summer. */
float map_light_cover_in_leaf(const Map *map, int x, int y, int z);
/* Steps from full leaf to bare branches, and what a layer of foliage lets
 * through when it is bare (instead of MAP_LIGHT_FOLIAGE_*). */
#define MAP_BARE_STEPS 16
#define MAP_LIGHT_BARE_DENSE 0.8f
#define MAP_LIGHT_BARE_MEDIUM 0.9f
#define MAP_LIGHT_BARE_SPARSE 0.95f
/* Sets how bare the crowns are (0 .. MAP_BARE_STEPS); what lets the light
 * through changed when it did (Map.light_revision). */
void map_set_bare(Map *map, int steps);
/* The same, 0 (in leaf) .. 1 (bare). */
float map_bare_fraction(const Map *map);
#define MAP_LIGHT_FOLIAGE_DENSE 0.35f
#define MAP_LIGHT_FOLIAGE_MEDIUM 0.6f
#define MAP_LIGHT_FOLIAGE_SPARSE 0.8f
#define MAP_LIGHT_ROOF 0.05f

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
 * unless it is open space or thin foliage (map_sees_through_at()), else the
 * first level down that is not, however deep. -1 when nothing is seen there
 * (open space all the way, or off the map). */
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

/* Copies every layer of the tile at `index` from `src` to `dst` (maps of the
 * same size), keeping dst's counters (slopes, structures, roofs, forbidden
 * tiles) right. Not the revisions nor the change logs: dst is a picture of
 * src, not a map in use (see core/known.h). */
void map_copy_tile(Map *dst, const Map *src, size_t index);

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

/* Branches (ITEM_BRANCHES), twigs (ITEM_TWIGS) or hazelnuts (ITEM_HAZELNUTS)
 * lying on a tile; 0 for other types and out of bounds. */
int map_litter(const Map *map, int x, int y, int z, ItemType type);

/* Lays one branch, twig or nut on a tile; false when the tile is full, out of
 * bounds or for another type. Does not look at what the tile is (see
 * tree_shed()). */
bool map_add_litter(Map *map, int x, int y, int z, ItemType type);

/* Takes all the branches, twigs or nuts of a tile; returns how many were there. */
int map_take_litter(Map *map, int x, int y, int z, ItemType type);

/* Takes up to `count` of them; returns how many were taken. */
int map_take_litter_count(Map *map, int x, int y, int z, ItemType type, int count);

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
