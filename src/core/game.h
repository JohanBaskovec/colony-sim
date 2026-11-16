/*
 * The simulation root: owns the world state and advances it tick by tick.
 *
 * Everything reachable from Game is pure, deterministic C: no SDL, no I/O, no
 * wall-clock time, no global random state.
 */
#ifndef COLONY_CORE_GAME_H
#define COLONY_CORE_GAME_H

#include "core/bed.h"
#include "core/colonist.h"
#include "core/construction.h"
#include "core/designation.h"
#include "core/dirt.h"
#include "core/game_clock.h"
#include "core/gather_zone.h"
#include "core/map.h"
#include "core/notice.h"
#include "core/pathfinding.h"
#include "core/pool.h"
#include "core/reservation.h"
#include "core/region.h"
#include "core/rng.h"
#include "core/roof.h"
#include "core/stockpile.h"
#include "core/temperature.h"
#include "core/weather.h"
#include "core/workstation.h"
#include "core/worldgen.h"

#include <stdbool.h>
#include <stdint.h>

/* Each plant grows once per interval; the map is covered slice by slice, a
 * little every tick. */
#define GAME_PLANT_UPDATE_INTERVAL TICKS_PER_HOUR
/* Same for items lying on the ground (transformations such as drying). */
#define GAME_ITEM_UPDATE_INTERVAL TICKS_PER_HOUR

#define GAME_DEFAULT_COLONIST_COUNT 3
/* The first settlers bring nothing and arrive at the end of summer: world
 * generation makes sure there are at least this many grown brambles (bearing
 * fruit) and hazels (nuts in autumn) within GAME_SPAWN_FOOD_MAX_COST of travel
 * from where they arrive, planting more when it has to. */
#define GAME_SPAWN_BRAMBLES 6
#define GAME_SPAWN_HAZELS 3
#define GAME_SPAWN_FOOD_MAX_COST 25.0f
/* Likewise, a tile with at least this many flint nodules where stones can be
 * gathered, within the same distance: knapping needs flint. */
#define GAME_SPAWN_FLINT_STONES 5

/* How often the game counts who knows what, to raise notices (a knowledge
 * only one settler still masters...). */
#define GAME_KNOWLEDGE_CHECK_TICKS 250

/* Radius (in tiles) around a world point within which a colonist can be picked. */
#define GAME_PICK_RADIUS 0.6f

/* Level of the builder behind debug instant construction (a standard grade,
 * on average). */
#define GAME_DEBUG_BUILD_LEVEL 65.0
/* Level of every knowledge after game_debug_learn_all(): work at a standard
 * speed (×1). */
#define GAME_DEBUG_KNOWLEDGE_LEVEL 75.0

/* Cheats for testing, set from the debug menu. They change the simulation,
 * so game_hash() includes them (only when one is on, so that hashes stay as
 * they were without them). */
typedef struct GameDebug {
    /* Every construction site is finished at the start of each tick, for
     * free: no materials, no work, no builder. Plants in the way are cleared;
     * a roof without support and a workstation that cannot be placed wait. */
    bool instant_build;
    /* Crafts need no input: crafters make it out of nothing (tools, the
     * recipe's knowledge and a burning fire are still needed). */
    bool free_crafts;
    /* No tool is needed: crafts go without theirs, and work done without a
     * tool goes at least at a standard tool's speed (×1). Carried tools are
     * still used (and wear) when they do better. */
    bool no_tools;
    /* Orders to take structures apart and roofs down are carried out at the
     * start of each tick, with nobody there (half the materials back, as
     * usual). */
    bool instant_deconstruct;
    /* Orders to fell trees and to mine rock are carried out at the start of
     * each tick, with nobody there (what they give is dropped as usual; rock
     * that holds a ceiling up waits, as for a settler). Nobody can do either
     * yet: with this on, they can be ordered. */
    bool instant_fell_and_mine;
} GameDebug;

typedef struct NewGameParams {
    WorldGenParams world;
    int colonist_count; /* Starting colonists, placed near the map center. */
} NewGameParams;

typedef struct Game {
    uint64_t seed;
    Map map;
    Designations designations;   /* Player orders (chop, mine...), same size as the map. */
    Dirt dirt;                   /* How dirty each tile is. */
    GatherZones gather_zones;    /* Where wood, wild food, grass, stones... are gathered. */
    Items items;                 /* Items lying on the ground, same size as the map. */
    Stockpiles stockpiles;       /* Storage zones drawn by the player. */
    Workstations workstations;   /* Crafting spots, knapping spots... and their bills. */
    Constructions constructions; /* Blueprints of beds, walls, doors... being built. */
    Beds beds;                   /* Who owns which bed. */
    Reservations reservations;   /* Who is busy with which job or food. */
    GameClock clock;
    Rng rng; /* Simulation randomness (names, AI...), seeded from `seed`. */
    Pathfinder pathfinder;
    /* Connectivity regions of the map: a cache, refreshed at the start of
     * each tick, not part of game_hash(). */
    Regions regions;
    Temperatures temperatures; /* Per tile, warmed by burning hearths. */
    Weather weather;           /* The sky, warm and cold spells, snow on the ground. */
    /* Where roofs were last checked for support (core/roof.h): derived from
     * the map, not part of game_hash(). */
    RoofWatch roof_watch;
    Pool colonist_pool;
    Colonist colonists[COLONIST_MAX]; /* Indexed by EntityId.index. */
    /* Items in stock per type, what "until N in stock" bills count: lying in a
     * stockpile that accepts them, or on their way there (lying on a
     * workstation, where crafts drop their products, or carried by a hauler),
     * so that crafters do not make more while the last ones wait for a
     * hauler. Refreshed at the start of each tick while some bill counts it
     * (see game_update_stock()), not part of game_hash(). */
    long stock[ITEM_TYPE_COUNT];
    Notices notices; /* Messages for the player, not part of game_hash(). */
    /* Who knew what at the last count (see GAME_KNOWLEDGE_CHECK_TICKS):
     * settlers mastering each knowledge, and whether anyone had it at
     * KNOWLEDGE_DISCOVERED_LEVEL. Only used for notices. */
    uint8_t knowledge_masters[KNOWLEDGE_COUNT];
    bool knowledge_known[KNOWLEDGE_COUNT];
    bool knowledge_counted;
    /* Knowledges the player wants told about at fireside stories. */
    bool teach[KNOWLEDGE_COUNT];
    GameDebug debug;
} Game;

void game_default_params(NewGameParams *params);

/* Generates a new world. On failure the game is left empty (safe to shut down). */
bool game_init(Game *game, const NewGameParams *params);

/* Recomputes Game.stock (game_tick() does it first thing when a bill needs
 * it; the frontend calls it before showing the stock). */
void game_update_stock(Game *game);
void game_shutdown(Game *game);

/* Advances the simulation by `real_seconds` of real time. Returns ticks run. */
int game_update(Game *game, double real_seconds);

/* Runs exactly one simulation tick. */
void game_tick(Game *game);

/* Content hash of the whole simulation state (determinism checks). */
uint64_t game_hash(const Game *game);

/* Turns a cheat of GameDebug on or off. */
void game_set_instant_build(Game *game, bool on);
void game_set_free_crafts(Game *game, bool on);
void game_set_no_tools(Game *game, bool on);
void game_set_instant_deconstruct(Game *game, bool on);
void game_set_instant_fell_and_mine(Game *game, bool on);
/* Debug: the next sky at once (clear → cloudy → fog → rain → storm), and a
 * cold snap (the coldest spell, until the next day's draw). */
void game_debug_next_weather(Game *game);
void game_debug_cold_snap(Game *game);

/* Debug: every colonist gets a gloomy memory (THOUGHT_DEBUG_GLOOM, enough to
 * break down soon, fading within hours), and nothing holds a break back. */
void game_debug_sadden(Game *game);

/* Debug: every living colonist knows everything at least at
 * GAME_DEBUG_KNOWLEDGE_LEVEL (practised now: nothing fades yet). */
void game_debug_learn_all(Game *game);

/* Debug: runs `ticks` simulation ticks at once (time goes by as usual). */
void game_debug_skip(Game *game, uint64_t ticks);

/* Debug: jumps the calendar to the first day of the next season, at the
 * same time of day, without simulating the days between (plants do not
 * grow, nobody gets hungry). */
void game_debug_next_season(Game *game);

/* Debug: `count` new items of a type (standard grade, undamaged) dropped on
 * a tile as items_drop() would. Returns how many found room (0 for an
 * invalid type or tile). */
int game_debug_spawn_items(Game *game, int x, int y, int z, ItemType type, int count);

/* Debug: moves a colonist to a passable tile at once; it gives up its task
 * and looks around afresh. Fails on a stale handle or a tile nobody can
 * stand on. */
bool game_debug_teleport(Game *game, EntityId id, int x, int y, int z);

/* ---- Colonists ----------------------------------------------------------- */

/*
 * Creates a colonist on a tile with a random unused name, age and needs.
 * Returns ENTITY_NONE if the tile is not passable or the colony is full.
 */
EntityId game_spawn_colonist(Game *game, int x, int y, int z);
bool game_remove_colonist(Game *game, EntityId id);

/* NULL when the handle is stale. */
Colonist *game_colonist(Game *game, EntityId id);
const Colonist *game_colonist_const(const Game *game, EntityId id);

/* Iteration: for (uint32_t i = 0; i < game_colonist_slot_count(g); i++), skipping ENTITY_NONE. */
uint32_t game_colonist_slot_count(const Game *game);
EntityId game_colonist_id_at(const Game *game, uint32_t slot);

/* Colonist of level z closest to a world point, within GAME_PICK_RADIUS, or
 * ENTITY_NONE. */
EntityId game_colonist_at(const Game *game, float world_x, float world_y, int z);

/* Colonists of level z whose center lies in the rectangle (corners in any
 * order). Returns the count. */
int game_colonists_in_rect(const Game *game, float x0, float y0, float x1, float y1, int z,
                           EntityId *out, int max_count);

/* Whether a colonist currently stands on the tile. */
bool game_tile_has_colonist(const Game *game, int x, int y, int z);

/* Speed of rot of food lying on a tile (item_rot_rate() at the tile's
 * temperature, under its roof if any, faster out in the rain); 0 out of
 * bounds. */
float game_item_rot_rate(const Game *game, int x, int y, int z);

/* Food need all the colonists lose in a day (gather_daily_food()). */
float game_daily_food(const Game *game);

/* Outdoor air temperature now, with the weather. */
float game_outdoor_temperature(const Game *game);

/* ---- Knowledge ----------------------------------------------------------- */

/* Whether some living colonist has at least KNOWLEDGE_DISCOVERED_LEVEL in a
 * knowledge: only those are shown. */
bool game_knowledge_discovered(const Game *game, KnowledgeType knowledge);

/* Living colonists who master a knowledge (knowledge_is_mastered()). */
int game_knowledge_masters(const Game *game, KnowledgeType knowledge);

/* The colonist assisting a master as an apprentice, or ENTITY_NONE. */
EntityId game_apprentice_of(const Game *game, EntityId master);

/* Marks a knowledge to be taught (or not) at fireside stories. Fails on an
 * invalid knowledge. */
bool game_set_teach(Game *game, KnowledgeType knowledge, bool teach);

/* Counts who knows what and raises notices for what changed since the last
 * count: a knowledge discovered, mastered again, mastered by a single
 * settler, no longer mastered, forgotten. The first count after game_init()
 * raises none. game_tick() runs it every GAME_KNOWLEDGE_CHECK_TICKS. */
void game_count_knowledge(Game *game);

/* ---- Designations -------------------------------------------------------- */

/* Sets a colonist's priority for a work type (see colonist_set_work_priority()).
 * Fails on a stale handle or invalid values. */
bool game_set_work_priority(Game *game, EntityId id, WorkType type, int priority);

/* Whether some living colonist knows enough to carry out this kind of order,
 * or a cheat carries it out (GameDebug.instant_fell_and_mine): the player
 * may only place orders someone can do. */
bool game_designation_available(const Game *game, DesignationType type);

/* Whether some living colonist knows enough to do this gathering job. */
bool game_gather_job_known(const Game *game, GatherJob job);

/* The gathering job under way on a tile of a zone. */
typedef struct GatherWorkInfo {
    GatherJob job;   /* GATHER_JOB_NONE: nothing under way. */
    float fraction;  /* Work done on it, 0..1. */
    EntityId worker; /* The settler who took it (walking there or working), or ENTITY_NONE
                        when the work was left off. */
} GatherWorkInfo;

/* What is under way on a tile: the job a settler took there, else the job
 * some work was done on and left off. False when nothing is (the info then
 * says GATHER_JOB_NONE). */
bool game_gather_work_at(const Game *game, int x, int y, int z, GatherWorkInfo *info);

/* Places (or with DESIGNATION_NONE, removes) an order on every valid tile of a
 * rectangle (corners in any order), in place of the order already there.
 * Returns the number of tiles changed. */
int game_designate_rect(Game *game, int x0, int y0, int x1, int y1, int z, DesignationType type);

/* The cancel tool: removes the orders and the construction sites in a
 * rectangle (corners in any order). Returns how many orders and sites were
 * removed. */
int game_cancel_rect(Game *game, int x0, int y0, int x1, int y1, int z);

/* ---- Forbidding ---------------------------------------------------------- */

/* What on a tile the player may forbid the settlers: they neither pick up,
 * eat, use, take apart, cut nor fell it, nor go through a forbidden door or
 * ladder (see map_forbidden(), items_forbidden(), Workstation.forbidden). */
typedef enum ForbidTarget {
    FORBID_ITEMS,     /* The stack lying there. */
    FORBID_PLANT,     /* The plant; on a tile of trunk or foliage, its tree. */
    FORBID_STRUCTURE, /* The structure (bed, wall, door, ladder...), given any of its tiles. */
    FORBID_ROOF,      /* The built roof. */
    FORBID_STATION,   /* The built workstation. */
    FORBID_TARGET_COUNT
} ForbidTarget;

/* Whether there is such a thing on the tile to forbid or allow. */
bool game_can_forbid(const Game *game, int x, int y, int z, ForbidTarget target);

/* Whether the thing on the tile is forbidden (false when there is none). */
bool game_forbidden(const Game *game, int x, int y, int z, ForbidTarget target);

/* Forbids or allows the thing on the tile. Allowing it makes the settlers
 * who found nothing to do look again. Returns whether it changed. */
bool game_set_forbidden(Game *game, int x, int y, int z, ForbidTarget target, bool forbidden);

/* The forbid and allow tools: forbids or allows everything on the tiles of a
 * rectangle (corners in any order) but grass, the ground cover nearly
 * everywhere (game_set_forbidden() takes it one tile at a time). Returns how
 * many things changed. */
int game_set_forbidden_rect(Game *game, int x0, int y0, int x1, int y1, int z, bool forbidden);

/* ---- Zones --------------------------------------------------------------- */

/* Zones of different kinds never overlap: these add a rectangle (corners in
 * any order) to the stockpiles or to the gathering zones as the zone tools do
 * (see stockpiles_add_rect() and gather_zones_add_rect(): zones of the same
 * kind it overlaps, and `preferred`, merge), leaving out the tiles of the
 * other kind of zone and of workstations (built or not). Return the zone's
 * index, or STOCKPILE_NONE / GATHER_ZONE_NONE when nothing could be added. */
int game_add_stockpile_rect(Game *game, int x0, int y0, int x1, int y1, int z, int preferred);
int game_add_gather_zone_rect(Game *game, int x0, int y0, int x1, int y1, int z, int preferred);

/* ---- Workstations -------------------------------------------------------- */

/* Whether some living colonist knows enough to build this kind of station
 * (the player may only place blueprints someone can build). */
bool game_workstation_buildable(const Game *game, WorkstationType type);

/* Whether some living colonist knows enough to make this recipe. */
bool game_recipe_known(const Game *game, RecipeType recipe);

/* Whether some living colonist knows the bill's recipe at a level the bill
 * accepts. */
bool game_bill_has_worker(const Game *game, const Bill *bill);

/* Tool kinds a recipe needs that no living colonist carries and that lie
 * nowhere in the colony (0 when every tool can be had, or none is needed:
 * GameDebug.no_tools): one mask per missing slot, joined. */
ItemTypeMask game_recipe_missing_tools(const Game *game, RecipeType recipe);

/* Whether enough of some input type a bill allows lies in the colony, once
 * the tools nobody carries have been taken from it (see bill_has_input()).
 * With GameDebug.free_crafts: whether the bill allows some input; with
 * GameDebug.no_tools, no tool is taken from it. */
bool game_bill_has_input(const Game *game, const Bill *bill);

/* Whether a workstation may be placed on a tile: not on a structure, where
 * construction_can_replace() and workstation_can_place() say yes (a site
 * there is replaced), with room left in the station list once every station
 * site is finished. */
bool game_can_place_workstation(const Game *game, int x, int y, int z);

/* Places a workstation where game_can_place_workstation() allows: ready at
 * once when it needs no construction (workstation_needs_construction()) and
 * no plant is in the way, else its construction site. A site already on the
 * tile is cancelled (materials dropped) and replaced, unless it is the same
 * workstation (nothing is done then). A zone (stockpile,
 * gathering zone) on the tile loses it: no zone lies under a station.
 * Returns whether something was placed. */
bool game_place_workstation(Game *game, WorkstationType type, int x, int y, int z);

/* Removes a station; the input of a craft in progress is dropped on its
 * tile. Colonists working there give up. */
bool game_remove_workstation(Game *game, int index);

/* Removes the construction sites in a rectangle; their materials are
 * dropped. Returns how many. */
int game_remove_blueprints_rect(Game *game, int x0, int y0, int x1, int y1, int z);

/* Removes a bill; the input of a craft in progress that no other bill can
 * finish is dropped on the station's tile. */
bool game_remove_bill(Game *game, int station, int bill);

/* ---- Structures ---------------------------------------------------------- */

/* Whether some living colonist knows enough to build this kind of structure. */
bool game_structure_buildable(const Game *game, StructureType type);

/* Whether a structure may be planned with its head at (x, y), facing
 * `direction`: every tile it would take is off stockpiles and workstations,
 * and passes construction_can_replace_structure() (sites in the way do not
 * count, but the very same plan already there does), roofs on the open tile
 * over the walls. */
bool game_can_place_structure(const Game *game, StructureType type, int x, int y, int z,
                              Direction direction);

/* Plans a structure where game_can_place_structure() allows. The sites on
 * its tiles are cancelled (their materials dropped): a new order replaces
 * one in progress. Returns the site's index or CONSTRUCTION_NONE. */
int game_place_structure(Game *game, StructureType type, int x, int y, int z, Direction direction);

/* One structure of a plan: its head, and whether it can be placed. */
typedef void (*StructurePlanVisit)(int x, int y, int z, bool allowed, void *userdata);

/* Where structures go on a rectangle (corners in any order), in a fixed
 * order: along its edges for types drawn as an outline (walls), on every
 * tile otherwise; long ones (beds) side by side, in rows of their length
 * along `direction`, the rectangle stretched that way to hold at least one. */
void game_plan_structure_rect(const Game *game, StructureType type, Direction direction, int x0,
                              int y0, int x1, int y1, int z, StructurePlanVisit visit,
                              void *userdata);

/* Places what game_plan_structure_rect() plans where allowed. Returns how
 * many sites were placed. */
int game_place_structure_rect(Game *game, StructureType type, Direction direction, int x0, int y0,
                              int x1, int y1, int z);

/* Removes a construction site; its delivered materials are dropped on its
 * tile. */
bool game_remove_construction(Game *game, int index);

#endif /* COLONY_CORE_GAME_H */
