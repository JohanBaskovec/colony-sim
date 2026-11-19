/*
 * The simulation root: owns the world state and advances it tick by tick.
 *
 * Everything reachable from Game is pure, deterministic C: no SDL, no I/O, no
 * wall-clock time, no global random state.
 */
#ifndef COLONY_CORE_GAME_H
#define COLONY_CORE_GAME_H

#include "core/animal.h"
#include "core/bed.h"
#include "core/blood.h"
#include "core/colonist.h"
#include "core/corpse.h"
#include "core/construction.h"
#include "core/designation.h"
#include "core/dirt.h"
#include "core/game_clock.h"
#include "core/gather_zone.h"
#include "core/hearth.h"
#include "core/item_move.h"
#include "core/known.h"
#include "core/map.h"
#include "core/notice.h"
#include "core/occupancy.h"
#include "core/pathfinding.h"
#include "core/pool.h"
#include "core/reservation.h"
#include "core/region.h"
#include "core/rng.h"
#include "core/roof.h"
#include "core/soil.h"
#include "core/stockpile.h"
#include "core/temperature.h"
#include "core/vision.h"
#include "core/weather.h"
#include "core/workstation.h"
#include "core/worldgen.h"

#include <stdbool.h>
#include <stdint.h>

/* Each plant grows once per interval; the map is covered slice by slice, a
 * little every tick. */
#define GAME_PLANT_UPDATE_INTERVAL TICKS_PER_HOUR
/* The water a new world's ground starts with: this share of its useful water
 * (0 wilting .. 1 field capacity), the banks wetter (docs/CLIMATE.md §3.6). */
#define GAME_SOIL_START_SHARE 0.8f
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

/* Without vision (game_set_vision()), settlers within this many tiles on
 * the same level see a death (docs/HEALTH.md §8). */
#define GAME_WITNESS_RADIUS 12

/* Injuries from the world (docs/HEALTH.md §9), points (tuning): a gallery
 * caving in on someone crushes them, a felled tree crushes those under its
 * trunk and bruises those under its crown (the woodcutter, beside its foot,
 * steps back), a debug blow is of any kind. */
#define GAME_CAVE_IN_DAMAGE_MIN 15.0f
#define GAME_CAVE_IN_DAMAGE_MAX 30.0f
#define GAME_TRUNK_DAMAGE_MIN 10.0f
#define GAME_TRUNK_DAMAGE_MAX 25.0f
#define GAME_CROWN_DAMAGE_MIN 3.0f
#define GAME_CROWN_DAMAGE_MAX 8.0f
#define GAME_DEBUG_BLOW_MIN 6.0f
#define GAME_DEBUG_BLOW_MAX 16.0f

/* How often the game counts who knows what, to raise notices (a knowledge
 * only one settler still masters...). */
#define GAME_KNOWLEDGE_CHECK_TICKS 250

/* Radius (in tiles) around a world point within which a colonist can be picked. */
#define GAME_PICK_RADIUS 0.6f

/* Level of the builder behind debug instant construction (a standard quality,
 * on average). */
#define GAME_DEBUG_BUILD_LEVEL 68.0
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

/* The density of wild animals in a game as played (NewGameParams.wildlife). */
#define GAME_WILDLIFE_DENSITY 1.0f

typedef struct NewGameParams {
    WorldGenParams world;
    int colonist_count; /* Starting colonists, placed near the map center. */
    /* Wild animals living in the world (core/animal.h): how many of them,
     * as a multiple of AnimalDef.density (GAME_WILDLIFE_DENSITY in a game as
     * played). 0, the default: none, as for tests and benchmarks that are
     * not about them. */
    float wildlife;
} NewGameParams;

typedef struct Game {
    uint64_t seed;
    Map map;
    Designations designations;   /* Player orders (chop, mine...), same size as the map. */
    Dirt dirt;                   /* How dirty each tile is. */
    GatherZones gather_zones;    /* Where wood, wild food, grass, stones... are gathered. */
    Items items;                 /* Items lying on the ground, same size as the map. */
    Corpses corpses;             /* Records of the settlers who died (bodies point at them). */
    Stockpiles stockpiles;       /* Storage zones drawn by the player. */
    Workstations workstations;   /* Crafting spots, knapping spots... and their bills. */
    Constructions constructions; /* Blueprints of beds, walls, doors... being built. */
    Beds beds;                   /* Who owns which bed. */
    Reservations reservations;   /* Who is busy with which job or food. */
    ItemMoves item_moves;        /* Stacks the player ordered carried to a chosen tile. */
    GameClock clock;
    Rng rng; /* Simulation randomness (names, AI...), seeded from `seed`. */
    /* The crowns' leaves (Map.bare): the climates of the deciduous trees met
     * during the hourly pass over the plants, and how many, for their mean. */
    double leaf_climate_sum;
    uint32_t leaf_tree_count;
    Pathfinder pathfinder;
    /* Connectivity regions of the map: a cache, refreshed at the start of
     * each tick, not part of game_hash(). */
    Regions regions;
    Temperatures temperatures; /* Per tile, warmed by burning hearths. */
    Soil soil;                 /* Water in the ground of each tile (docs/CLIMATE.md). */
    /* Who sees what (core/vision.h): derived from the map and where the
     * settlers stand, not part of game_hash(). Only kept up to date while
     * `vision_on` (see game_set_vision()). */
    Vision vision;
    bool vision_on;
    /* The fog of war (docs/VISION.md): what the colony has seen. While
     * `fog_on` (which needs `vision_on`), `known` holds pictures of the map,
     * the items and the dirt, kept as game_set_fog() says, and what is
     * unexplored is hidden: the frontend draws the known world instead of the
     * real one (game_view_map()...). With it off, everything is seen. */
    KnownWorld known;
    bool known_ready; /* `known` is allocated. */
    bool fog_on;
    bool fog_started; /* The start was explored. */
    TilePos arrival;  /* Where the starting settlers arrived. */
    bool has_arrival;
    Weather weather; /* The sky, warm and cold spells, snow on the ground. */
    /* Where roofs were last checked for support (core/roof.h): derived from
     * the map, not part of game_hash(). */
    RoofWatch roof_watch;
    Pool colonist_pool;
    Colonist colonists[COLONIST_MAX]; /* Indexed by EntityId.index. */
    /* The last settler died (docs/HEALTH.md §8): the game paused itself
     * then. Part of game_hash() once set. */
    bool colony_lost;
    /* Blows asked for by settlers and animals during a tick, resolved at its
     * end (core/combat.h), with a random stream of their own, and how many
     * were resolved so far (part of game_hash() once there was one). */
    CombatQueue combat;
    Rng combat_rng;
    uint32_t strikes_resolved;
    /* The last blows resolved, for the frontend to play (core/strike_anim.h):
     * not part of game_hash(). */
    CombatLog combat_log;
    /* Blood on the ground (core/blood.h), with a random stream of its own:
     * for the eye only, not part of game_hash(). */
    BloodStains blood;
    Rng blood_rng;
    /* Felled trees dealt with (Map.tree_fall_revision): those under them
     * were hurt. */
    uint32_t tree_falls_seen;
    /* Wild animals (core/animal.h), ticked after the settlers, and their own
     * randomness: what the animals do does not shift the settlers' draws.
     * Part of game_hash() once an animal has lived in the game. */
    Animals animals;
    Rng wildlife_rng;
    /* Who stops on which tile, settlers and animals alike (core/occupancy.h):
     * refreshed by each being on its turn, not part of game_hash(). */
    Occupancy occupancy;
    /* Items in stock per type, what "until N in stock" bills count: lying in a
     * stockpile that accepts them, or on their way there (lying on a
     * workstation, where crafts drop their products, or carried by a hauler),
     * so that crafters do not make more while the last ones wait for a
     * hauler. Refreshed at the start of each tick while some bill counts it
     * (see game_update_stock()), not part of game_hash(). */
    long stock[ITEM_TYPE_COUNT];
    /* Where the stock lies (the three add up to `stock`), refreshed with it:
     * for the player only. */
    long stock_stored[ITEM_TYPE_COUNT];      /* In stockpiles that accept them. */
    long stock_on_stations[ITEM_TYPE_COUNT]; /* On workstations, out of storage. */
    long stock_hauled[ITEM_TYPE_COUNT];      /* Carried by haulers. */
    Notices notices;                         /* Messages for the player, not part of game_hash(). */
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

/* Recomputes Game.stock and where it lies (game_tick() does it first thing when a bill needs
 * it; the frontend calls it before showing the stock). */
void game_update_stock(Game *game);
void game_shutdown(Game *game);

/* Advances the simulation by `real_seconds` of real time. Returns ticks run. */
int game_update(Game *game, double real_seconds);

/* Runs exactly one simulation tick. */
void game_tick(Game *game);

/* Content hash of the whole simulation state (determinism checks). */
uint64_t game_hash(const Game *game);

/* Turns the tracking of what the settlers see on or off (it costs time, and
 * nothing depends on it yet: for the debug overlay). Off frees it. Returns
 * false when it cannot be allocated. */
bool game_set_vision(Game *game, bool on);

/* Turns the fog of war on or off (off: "see everything", as before the fog;
 * the known world is not kept up to date meanwhile). The first time it comes
 * on, the colony explores the start: around the arrival point and along the
 * way in from a map edge (core/exploration.h). It needs the vision, which it
 * turns on. Returns false when something cannot be allocated. */
bool game_set_fog(Game *game, bool on);

/* Debug: every tile of the map becomes explored, as it is now. */
void game_reveal_map(Game *game);

/* What to draw and to read of the world: the known world under the fog, the
 * real one without. */
static inline const Map *game_view_map(const Game *game)
{
    return game->fog_on ? &game->known.map : &game->map;
}
static inline const Items *game_view_items(const Game *game)
{
    return game->fog_on ? &game->known.items : &game->items;
}
static inline const Dirt *game_view_dirt(const Game *game)
{
    return game->fog_on ? &game->known.dirt : &game->dirt;
}

/* The items to show on a tile: the real ones where someone sees it now (what
 * moves is shown as it moves), the known world's elsewhere. */
const Items *game_items_at(const Game *game, int x, int y, int z);

/* Whether a tile has been seen (always true without the fog), and whether
 * someone sees it now (likewise). False out of bounds. */
bool game_tile_explored(const Game *game, int x, int y, int z);
bool game_tile_seen(const Game *game, int x, int y, int z);

/* Turns a cheat of GameDebug on or off. */
void game_set_instant_build(Game *game, bool on);
void game_set_free_crafts(Game *game, bool on);
void game_set_no_tools(Game *game, bool on);
void game_set_instant_deconstruct(Game *game, bool on);
void game_set_instant_fell_and_mine(Game *game, bool on);
/* Debug: the character editor's steps (a click of its - or + button): a need
 * moves by a tenth, an attribute by 5 percent of the way to either end, a
 * knowledge by 5 levels; the air by 5 degrees. */
#define GAME_DEBUG_NEED_STEP 0.1f
#define GAME_DEBUG_ATTRIBUTE_STEP 50
#define GAME_DEBUG_KNOWLEDGE_STEP 5.0
#define GAME_DEBUG_TEMPERATURE_STEP 5.0f
#define GAME_DEBUG_TEMPERATURE_LIMIT 60.0f /* The offset stays within -/+ this. */

/* Debug (the character editor): moves a need of a colonist or an animal up
 * (`direction` > 0) or down by a step, within 0..1; an attribute of a
 * colonist, within the whole range, by a step (the character that follows
 * from it is worked out again); a colonist's knowledge, within 0 to
 * KNOWLEDGE_LEVEL_MAX (its peak follows it down: nothing is forgotten, so
 * nothing is relearnt). False for a stale handle or an invalid type. */
bool game_debug_colonist_need(Game *game, EntityId id, NeedType need, int direction);
bool game_debug_animal_need(Game *game, EntityId id, AnimalNeed need, int direction);
bool game_debug_colonist_attribute(Game *game, EntityId id, AttributeType type, int direction);
bool game_debug_colonist_knowledge(Game *game, EntityId id, KnowledgeType knowledge, int direction);

/* Debug: raises (`direction` > 0) or lowers the air's temperature by a step,
 * all along (Weather.debug_offset), within GAME_DEBUG_TEMPERATURE_LIMIT. */
void game_debug_temperature(Game *game, int direction);

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
/* The air of every tile at the outdoor air's and the ground under it at the
 * day's mean temperature: what the weather of the day would have left them,
 * after the clock jumped (game_debug_next_season(), tests). */
void game_settle_climate(Game *game);
void game_debug_next_season(Game *game);

/* Debug: `count` new items of a type (standard quality, undamaged) dropped on
 * a tile as items_drop() would. Returns how many found room (0 for an
 * invalid type or tile). */
int game_debug_spawn_items(Game *game, int x, int y, int z, ItemType type, int count);

/* Growth a click of the debug "grow a tree" tool adds (a tenth: the trunk
 * rises at 0.4, 0.6 and 0.8, then the crown spreads until 1). */
#define GAME_DEBUG_TREE_GROWTH_STEP 0.1f

/* Debug: a tree seedling (growth 0) on a tile, replacing the plant there.
 * Fails (false) where no tree grows: out of the map, rock, a structure, a
 * slope, ground less fertile than trees need, or touching another tree. */
bool game_debug_plant_tree(Game *game, int x, int y, int z);

/* Debug: the tree on (x, y, z), from any tile of its trunk, foliage or foot,
 * grows by GAME_DEBUG_TREE_GROWTH_STEP (to full growth with `fully`) and is
 * written into the map at once. Returns whether it grew: false without a
 * tree, for one fully grown already, or one that waits for room (something
 * where its trunk would rise stops it, as in play). */
bool game_debug_grow_tree(Game *game, int x, int y, int z, bool fully);

/* Who stands on the tile: a settler first, else an animal (COMBATANT_NONE
 * when nobody; the dead are not counted). */
Combatant game_combatant_at(const Game *game, int x, int y, int z);

/* Debug: makes `attacker` (a settler or an animal) attack `target` (an
 * animal, or a settler when the attacker is an animal), whatever it was doing.
 * Fails on stale handles, a dead being, an animal without a weapon, or a
 * settler against a settler. */
bool game_debug_attack(Game *game, Combatant attacker, Combatant target);

/* Debug: a blow of a random kind (GAME_DEBUG_BLOW_*, times the body's size)
 * on every settler and animal on the tile. Returns how many were hurt. */
int game_debug_hurt(Game *game, int x, int y, int z);

/* Debug: moves a colonist to a passable tile at once; it gives up its task
 * and looks around afresh. Fails on a stale handle or a tile nobody can
 * stand on. */
bool game_debug_teleport(Game *game, EntityId id, int x, int y, int z);

/* ---- Colonists ----------------------------------------------------------- */

/* The player's order to move: each of the `count` colonists given gives up
 * what it does and walks to a tile of its own near (x, y, z) (the tile itself
 * for the first that can stand on it: they do not pile up on one tile).
 * Tiles nobody has explored, nobody can stand on or reach on what the colony
 * knows are left out. Returns how many were sent. */
int game_order_move(Game *game, const EntityId *ids, int count, int x, int y, int z);

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

/* Colonist seen from level z closest to a world point, within GAME_PICK_RADIUS,
 * or ENTITY_NONE. Seen: on level z, or on a lower level visible through the
 * open space over its tile (map_view_level(), as for the zone tools). */
EntityId game_colonist_at(const Game *game, float world_x, float world_y, int z);

/* Colonists seen from level z (as game_colonist_at()) whose center lies in the
 * rectangle (corners in any order). Returns the count. */
int game_colonists_in_rect(const Game *game, float x0, float y0, float x1, float y1, int z,
                           EntityId *out, int max_count);

/* Whether a colonist currently stands on the tile. */
bool game_tile_has_colonist(const Game *game, int x, int y, int z);

/* ---- Animals ------------------------------------------------------------- */

/* Creates a wild animal on a tile, alone (animals_spawn(), with the game's
 * wildlife randomness): for tests and the debug tool. ENTITY_NONE when it
 * cannot stand there or there is no room left. */
EntityId game_spawn_animal(Game *game, AnimalSpecies species, AnimalSex sex, int x, int y, int z);

/* Debug: game_spawn_animal(), and the newcomer joins the herd of the nearest
 * animal of its species and sex within ANIMAL_HERD_RADIUS tiles, if any. */
EntityId game_debug_spawn_animal(Game *game, AnimalSpecies species, AnimalSex sex, int x, int y,
                                 int z);

/* NULL when the handle is stale. */
const Animal *game_animal_const(const Game *game, EntityId id);

/* Whether the colony sees the animal now: always without the fog of war,
 * under it when a settler sees the tile it stands on (animals out of view
 * are neither drawn nor picked: nobody knows where they are). False for a
 * stale handle. */
bool game_animal_seen(const Game *game, EntityId id);

/* The animal the colony sees closest to a world point, seen from level z as
 * game_colonist_at(), or ENTITY_NONE. */
EntityId game_animal_at(const Game *game, float world_x, float world_y, int z);

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

/* The same for the order on a given tile, as the colony knows it: some need
 * more there (colonist_can_do_at()). */
bool game_designation_available_at(const Game *game, DesignationType type, int x, int y, int z);

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
 * ladder (see map_forbidden(), items_forbidden(), Workstation.forbidden). Plans
 * and zones may be forbidden too: a forbidden plan gets no material and no work,
 * a forbidden stockpile no hauling in nor out, a forbidden gathering zone no
 * gathering. */
typedef enum ForbidTarget {
    FORBID_ITEMS,       /* The stack lying there. */
    FORBID_PLANT,       /* The plant; on a tile of trunk or foliage, its tree. */
    FORBID_STRUCTURE,   /* The structure (bed, wall, door, ladder...), given any of its tiles. */
    FORBID_ROOF,        /* The built roof. */
    FORBID_STATION,     /* The built workstation. */
    FORBID_SITE,        /* The blueprint (construction site): no material brought, no work done. */
    FORBID_STOCKPILE,   /* The whole stockpile the tile is in: nothing hauled onto it nor off it. */
    FORBID_GATHER_ZONE, /* The whole gathering zone the tile is in: nothing gathered in it. */
    FORBID_TARGET_COUNT
} ForbidTarget;

/* Whether there is such a thing on the tile to forbid or allow. */
bool game_can_forbid(const Game *game, int x, int y, int z, ForbidTarget target);

/* Whether the thing on the tile is forbidden (false when there is none). */
bool game_forbidden(const Game *game, int x, int y, int z, ForbidTarget target);

/* Forbids or allows the thing on the tile. Allowing it makes the settlers
 * who found nothing to do look again. Returns whether it changed. */
bool game_set_forbidden(Game *game, int x, int y, int z, ForbidTarget target, bool forbidden);

/* The light on a tile now (0 .. 1): the sun (game_sunlight()), the sky
 * (weather_light()) and what covers the tile (map_light_cover(); a tile of a
 * tree's trunk gets the open light). `game_light_peak_at()` is the best it
 * gets in the day (the noon sun). */
float game_light_at(const Game *game, int x, int y, int z);
float game_light_peak_at(const Game *game, int x, int y, int z);

/* What a plant on a tile meets now: the temperature of its tile
 * (temperature_for_plant()), its ground's fertility, and the light: the sun
 * (game_sunlight()), the sky (weather_light()) and what covers the tile
 * (map_light_cover(); a tree, which stands on the tile of its trunk and
 * above what lies on the ground, gets the open light). */
PlantConditions game_plant_conditions(const Game *game, int x, int y, int z);

/* The forbid and allow tools: forbids or allows everything on the tiles of a
 * rectangle (corners in any order), plans and plants included (grass too), but
 * not zones, which are forbidden as a whole. Returns how many things changed. */
int game_set_forbidden_rect(Game *game, int x0, int y0, int x1, int y1, int z, bool forbidden);

/*
 * Orders the stack of items on (x, y, z) carried to the tile (dx, dy, dz),
 * which may be anywhere a settler can stand and has room (a stockpile or
 * not): hauling settlers do it before their ordinary hauls (core/item_move.h).
 * The stack is forbidden at once so that nobody else takes it, and so is
 * what lands on the destination, until the player allows it. An order on a
 * stack that already has one replaces it. Refused (false) when the source
 * holds nothing or has not been seen, when the destination is unexplored, not
 * standable, the same tile or has no room, or when there are too many orders.
 */
bool game_order_item_move(Game *game, int x, int y, int z, int dx, int dy, int dz);

/* Takes the order on the stack at (x, y, z) back (what was forbidden stays so).
 * Returns whether there was one. */
bool game_cancel_item_move(Game *game, int x, int y, int z);

/* Where the stack at (x, y, z) is ordered to go; false when it is not. */
bool game_item_move_at(const Game *game, int x, int y, int z, TilePos *destination);

/* ---- Zones --------------------------------------------------------------- */

/* Zones of different kinds never overlap: these add a rectangle (corners in
 * any order) to the stockpiles or to the gathering zones as the zone tools do
 * (see stockpiles_add_view_rect() and gather_zones_add_view_rect(): zones of
 * the same kind it overlaps, and `preferred`, merge), leaving out the tiles
 * of the other kind of zone and of workstations (built or not). `z` is the
 * level shown: the zone covers what is seen in the rectangle, so the levels
 * below too where the level shown is open space (map_view_level()). Return
 * the zone's index, or STOCKPILE_NONE / GATHER_ZONE_NONE when nothing could
 * be added. */
int game_add_stockpile_rect(Game *game, int x0, int y0, int x1, int y1, int z, int preferred);
int game_add_gather_zone_rect(Game *game, int x0, int y0, int x1, int y1, int z, int preferred);

/* Takes what is seen in a rectangle (level z shown) out of the stockpiles, or
 * of the gathering zones. Returns the number of tiles removed. */
int game_remove_stockpile_rect(Game *game, int x0, int y0, int x1, int y1, int z);
int game_remove_gather_zone_rect(Game *game, int x0, int y0, int x1, int y1, int z);

/* What is seen in a rectangle (level z shown) added to one stockpile or
 * gathering zone alone (`index`: no merging; the same tiles left out as by
 * game_add_stockpile_rect()), or taken out of it alone. Return the number of
 * tiles added or removed. */
int game_extend_stockpile_rect(Game *game, int index, int x0, int y0, int x1, int y1, int z);
int game_shrink_stockpile_rect(Game *game, int index, int x0, int y0, int x1, int y1, int z);
int game_extend_gather_zone_rect(Game *game, int index, int x0, int y0, int x1, int y1, int z);
int game_shrink_gather_zone_rect(Game *game, int index, int x0, int y0, int x1, int y1, int z);

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

/* Whether what lighting a hearth takes is lacking: a kindling or branch not
 * laid in it of which none lies in the colony, tinder of which none lies in
 * the colony while no other fire burns to take embers from. */
bool game_hearth_lacks(const Game *game, const Workstation *station, HearthIngredient ingredient);

/* Whether a workstation may be placed on a tile: not on a structure, where
 * construction_can_replace() and workstation_can_place() say yes (a site
 * there is replaced), with room left in the station list once every station
 * site is finished. */
bool game_can_place_workstation(const Game *game, int x, int y, int z);

/* Places a workstation where game_can_place_workstation() allows: ready at
 * once when it needs no construction (workstation_needs_construction(); the
 * plant under it is ordered uprooted, not destroyed), else its construction
 * site. A site already on the
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

/* The level to build on at (x, y) when level `shown` is the one on screen: the
 * ground seen there, the level below when the level shown is open space
 * (map_view_level(), as for the zone tools), so that one builds on whatever one
 * sees as if it were the level shown. A roof goes on the open level over it
 * (`roof`). `shown` itself when nothing is seen. */
int game_build_level(const Game *game, int x, int y, int shown, bool roof);

/* Whether a structure may be planned with its head at (x, y), facing
 * `direction`: every tile it would take is off stockpiles and workstations,
 * and passes construction_can_replace_structure() (sites in the way do not
 * count, but the very same plan already there does), roofs on the open tile
 * over the walls. */
bool game_can_place_structure(const Game *game, StructureType type, int x, int y, int z,
                              Direction direction);

/* Plans a structure where game_can_place_structure() allows. The sites on
 * its tiles are cancelled (their materials dropped): a new order replaces
 * one in progress. Returns the site's index, CONSTRUCTION_BUILT_AT_ONCE when
 * it needs no construction (structure_needs_construction(): it stands there
 * at once, the plants under it ordered uprooted) or CONSTRUCTION_NONE. */
int game_place_structure(Game *game, StructureType type, int x, int y, int z, Direction direction);

/* One structure of a plan: its head, and whether it can be placed. */
typedef void (*StructurePlanVisit)(int x, int y, int z, bool allowed, void *userdata);

/* Where structures go on a rectangle (corners in any order) drawn on level
 * `z` shown (each structure on its own tile's level, game_build_level()), in a fixed
 * order: along its edges for types drawn as an outline (walls), on every
 * tile otherwise; long ones (beds) side by side, in rows of their length
 * along `direction`, the rectangle stretched that way to hold at least one. */
void game_plan_structure_rect(const Game *game, StructureType type, Direction direction, int x0,
                              int y0, int x1, int y1, int z, StructurePlanVisit visit,
                              void *userdata);

/* Places what game_plan_structure_rect() plans where allowed (`z`: the level
 * shown). Returns how many sites were placed. */
int game_place_structure_rect(Game *game, StructureType type, Direction direction, int x0, int y0,
                              int x1, int y1, int z);

/* Removes a construction site; its delivered materials are dropped on its
 * tile. */
bool game_remove_construction(Game *game, int index);

#endif /* COLONY_CORE_GAME_H */
