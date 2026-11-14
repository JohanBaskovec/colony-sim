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
#include "core/pathfinding.h"
#include "core/pool.h"
#include "core/reservation.h"
#include "core/region.h"
#include "core/room.h"
#include "core/rng.h"
#include "core/stockpile.h"
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
/* The first settlers bring nothing: world generation makes sure there are at
 * least this many ripe berry bushes within GAME_SPAWN_FOOD_MAX_COST of travel
 * from where they arrive, planting more when it has to. */
#define GAME_SPAWN_RIPE_BUSHES 6
#define GAME_SPAWN_FOOD_MAX_COST 25.0f
/* Likewise, a tile with at least this many flint nodules where stones can be
 * gathered, within the same distance: knapping needs flint. */
#define GAME_SPAWN_FLINT_STONES 5

/* Radius (in tiles) around a world point within which a colonist can be picked. */
#define GAME_PICK_RADIUS 0.6f

typedef struct NewGameParams {
    WorldGenParams world;
    int colonist_count; /* Starting colonists, placed near the map center. */
} NewGameParams;

typedef struct Game {
    uint64_t seed;
    Map map;
    Designations designations; /* Player orders (chop, mine...), same size as the map. */
    Dirt dirt; /* How dirty each tile is. */
    GatherZones gather_zones; /* Where wood, berries, grass, stones... are gathered. */
    Items items; /* Items lying on the ground, same size as the map. */
    Stockpiles stockpiles; /* Storage zones drawn by the player. */
    Workstations workstations; /* Crafting spots, knapping spots... and their bills. */
    Constructions constructions; /* Blueprints of beds, walls, doors... being built. */
    Beds beds; /* Who owns which bed. */
    Reservations reservations; /* Who is busy with which job or food. */
    GameClock clock;
    Rng rng; /* Simulation randomness (names, AI...), seeded from `seed`. */
    Pathfinder pathfinder;
    /* Connectivity regions of the map: a cache, refreshed at the start of
     * each tick, not part of game_hash(). */
    Regions regions;
    /* Rooms and whether they are roofed: a cache, brought up to date when
     * asked for (game_rooms()), not part of game_hash(). */
    Rooms rooms;
    Pool colonist_pool;
    Colonist colonists[COLONIST_MAX]; /* Indexed by EntityId.index. */
    /* Items in stock per type, what "until N in stock" bills count: lying in a
     * stockpile that accepts them, or on their way there (lying on a
     * workstation, where crafts drop their products, or carried by a hauler),
     * so that crafters do not make more while the last ones wait for a
     * hauler. Refreshed at the start of each tick while some bill counts it
     * (see game_update_stock()), not part of game_hash(). */
    long stock[ITEM_TYPE_COUNT];
} Game;

void game_default_params(NewGameParams *params);

/* Generates a new world. On failure the game is left empty (safe to shut down). */
bool game_init(Game *game, const NewGameParams *params);

/* Recomputes Game.stock (game_tick() does it first thing when a bill needs
 * it; the frontend calls it before showing the stock). */
void game_update_stock(Game *game);
void game_shutdown(Game *game);

/* Advances the simulation by `real_seconds` of real time. Returns ticks run. */
/* The rooms of the map as it is now (rebuilt if it changed). Nothing in the
 * simulation depends on them yet, so they are only computed when asked for
 * (the tile inspector). */
const Rooms *game_rooms(Game *game);

int game_update(Game *game, double real_seconds);

/* Runs exactly one simulation tick. */
void game_tick(Game *game);

/* Content hash of the whole simulation state (determinism checks). */
uint64_t game_hash(const Game *game);

/* ---- Colonists ----------------------------------------------------------- */

/*
 * Creates a colonist on a tile with a random unused name, age and needs.
 * Returns ENTITY_NONE if the tile is not passable or the colony is full.
 */
EntityId game_spawn_colonist(Game *game, int x, int y);
bool game_remove_colonist(Game *game, EntityId id);

/* NULL when the handle is stale. */
Colonist *game_colonist(Game *game, EntityId id);
const Colonist *game_colonist_const(const Game *game, EntityId id);

/* Iteration: for (uint32_t i = 0; i < game_colonist_slot_count(g); i++), skipping ENTITY_NONE. */
uint32_t game_colonist_slot_count(const Game *game);
EntityId game_colonist_id_at(const Game *game, uint32_t slot);

/* Colonist closest to a world point, within GAME_PICK_RADIUS, or ENTITY_NONE. */
EntityId game_colonist_at(const Game *game, float world_x, float world_y);

/* Colonists whose center lies in the rectangle (corners in any order). Returns the count. */
int game_colonists_in_rect(const Game *game, float x0, float y0, float x1, float y1,
                           EntityId *out, int max_count);

/* Whether a colonist currently stands on the tile. */
bool game_tile_has_colonist(const Game *game, int x, int y);

/* ---- Designations -------------------------------------------------------- */

/* Sets a colonist's priority for a work type (see colonist_set_work_priority()).
 * Fails on a stale handle or invalid values. */
bool game_set_work_priority(Game *game, EntityId id, WorkType type, int priority);

/* Whether some living colonist knows enough to carry out this kind of order
 * (the player may only place orders someone can do). */
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
bool game_gather_work_at(const Game *game, int x, int y, GatherWorkInfo *info);

/* Places (or with DESIGNATION_NONE, removes) an order on every valid tile of a
 * rectangle (corners in any order). Returns the number of tiles changed. */
int game_designate_rect(Game *game, int x0, int y0, int x1, int y1, DesignationType type);

/* ---- Zones --------------------------------------------------------------- */

/* Zones of different kinds never overlap: these add a rectangle (corners in
 * any order) to the stockpiles or to the gathering zones as the zone tools do
 * (see stockpiles_add_rect() and gather_zones_add_rect(): zones of the same
 * kind it overlaps, and `preferred`, merge), leaving out the tiles of the
 * other kind of zone and of workstations (built or not). Return the zone's
 * index, or STOCKPILE_NONE / GATHER_ZONE_NONE when nothing could be added. */
int game_add_stockpile_rect(Game *game, int x0, int y0, int x1, int y1, int preferred);
int game_add_gather_zone_rect(Game *game, int x0, int y0, int x1, int y1, int preferred);

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
 * nowhere in the colony (0 when every tool can be had): one mask per
 * missing slot, joined. */
ItemTypeMask game_recipe_missing_tools(const Game *game, RecipeType recipe);

/* Whether enough of some input type a bill allows lies in the colony, once
 * the tools nobody carries have been taken from it (see bill_has_input()). */
bool game_bill_has_input(const Game *game, const Bill *bill);

/* Whether a workstation may be placed on a tile: not on a structure, where
 * construction_can_place() and workstation_can_place() say yes, with room
 * left in the station list once every station site is finished. */
bool game_can_place_workstation(const Game *game, int x, int y);

/* Places a workstation where game_can_place_workstation() allows: ready at
 * once when it needs no construction (workstation_needs_construction()) and
 * no plant is in the way, else its construction site. A zone (stockpile,
 * gathering zone) on the tile loses it: no zone lies under a station.
 * Returns whether something was placed. */
bool game_place_workstation(Game *game, WorkstationType type, int x, int y);

/* Removes a station; the input of a craft in progress is dropped on its
 * tile. Colonists working there give up. */
bool game_remove_workstation(Game *game, int index);

/* Removes the construction sites (structures and workstations) in a
 * rectangle; their materials are dropped. Returns how many. */
int game_remove_blueprints_rect(Game *game, int x0, int y0, int x1, int y1);

/* Removes a bill; the input of a craft in progress that no other bill can
 * finish is dropped on the station's tile. */
bool game_remove_bill(Game *game, int station, int bill);

/* ---- Structures ---------------------------------------------------------- */

/* Whether some living colonist knows enough to build this kind of structure. */
bool game_structure_buildable(const Game *game, StructureType type);

/* Whether a structure may be planned with its head at (x, y), facing
 * `direction`: every tile it would take is off stockpiles and workstations,
 * and passes construction_can_place(); a roof goes over stockpiles and
 * workstations, where construction_can_place_roof() allows. */
bool game_can_place_structure(const Game *game, StructureType type, int x, int y,
                              Direction direction);

/* Plans a structure where game_can_place_structure() allows. Returns the
 * site's index or CONSTRUCTION_NONE. */
int game_place_structure(Game *game, StructureType type, int x, int y, Direction direction);

/* One structure of a plan: its head, and whether it can be placed. */
typedef void (*StructurePlanVisit)(int x, int y, bool allowed, void *userdata);

/* Where structures go on a rectangle (corners in any order), in a fixed
 * order: along its edges for types drawn as an outline (walls), on every
 * tile otherwise; long ones (beds) side by side, in rows of their length
 * along `direction`, the rectangle stretched that way to hold at least one. */
void game_plan_structure_rect(const Game *game, StructureType type, Direction direction,
                              int x0, int y0, int x1, int y1, StructurePlanVisit visit,
                              void *userdata);

/* Places what game_plan_structure_rect() plans where allowed. Returns how
 * many sites were placed. */
int game_place_structure_rect(Game *game, StructureType type, Direction direction, int x0,
                              int y0, int x1, int y1);

/* Removes a construction site; its delivered materials are dropped on its
 * tile. */
bool game_remove_construction(Game *game, int index);

#endif /* COLONY_CORE_GAME_H */
