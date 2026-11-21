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
#include "core/crown.h"
#include "core/construction.h"
#include "core/conversation.h"
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
#include "core/social.h"
#include "core/soil.h"
#include "core/stockpile.h"
#include "core/temperature.h"
#include "core/tile_light.h"
#include "core/vision.h"
#include "core/weather.h"
#include "core/workstation.h"
#include "core/worldgen.h"

#include <stdbool.h>
#include <stdint.h>

/* Each plant grows once per interval; the map is covered slice by slice, a
 * little every tick. */
#define GAME_PLANT_UPDATE_INTERVAL TICKS_PER_HOUR
/* Ticks between counts of the stock when no bill needs it every tick (gathering zones' targets,
 * the food alert). */
#define GAME_STOCK_INTERVAL 30
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
    Crowns crowns;               /* What hangs in the trees: fruit, dead wood (Map.crowns). */
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
    TileLight
        light; /* Sky, sun and sun hours of each tile (docs/SHADOWS.md §5): read by nobody yet. */
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
    /* What the settlers think of each other (core/social.h), with a random
     * stream of its own: chats, families. */
    Social social;
    ConversationCounts chat_counts; /* For the curious: not part of game_hash(). */
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
    /* The doors somebody stands in (map indices), open for the light and the
     * sight while they do (MAP_DOOR_PASSING), not part of game_hash(). */
    uint32_t passing_doors[OCCUPANCY_BEINGS];
    int passing_door_count;
    /* Items in stock per type: what lies in a stockpile that accepts them (what
     * "until N in stock" bills, gathering zones that gather until N and the food
     * alert count; the resources list shows it too). Items on a workstation, carried
     * or on the ground are not in stock. Refreshed by game_update_stock() at
     * the start of each tick while some bill counts it, every GAME_STOCK_INTERVAL
     * ticks otherwise; not part of game_hash(). */
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

/* Counts the stock (see Game.stock) into `stock` without touching the game: what the
 * frontend shows, up to date every frame, whatever the simulation last counted. */
void game_count_stock(const Game *game, long stock[ITEM_TYPE_COUNT]);
/* Recomputes Game.stock (for the simulation: game_tick() does it, on a schedule of the tick
 * only). */
void game_update_stock(Game *game);
void game_shutdown(Game *game);

/* Advances the simulation by `real_seconds` of real time. Returns ticks run. */
int game_update(Game *game, double real_seconds);

/* Runs exactly one simulation tick. */
void game_tick(Game *game);

/* Content hash of the whole simulation state (determinism checks). */
uint64_t game_hash(const Game *game);

/*
 * Checks that the items are consistent (tests and benchmarks): items_check()
 * on the world and on the known world's picture, every settler's load
 * matching its items (items_check_load()), and the items in loads adding up
 * to what the item pool counts as held. Visits every tile.
 */
ItemsCheck game_check_items(const Game *game);

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

/* Debug tools of social life (docs/SOCIAL.md §10): the Social tab with the
 * character editor on, and the debug menu. Opinions are changed through the
 * bond's debug memory (social_debug_nudge()), and relationships brought up
 * to date at once, noted in the journals. */
#define GAME_DEBUG_OPINION_STEP 10.0f
/* game_debug_chat(): a chat drawn as usual, and an argument that comes to
 * blows, next to the ChatKind values. */
#define GAME_DEBUG_CHAT_ANY (-1)
#define GAME_DEBUG_CHAT_BRAWL CHAT_KIND_COUNT
/* The opinion game_debug_relation() sets for each relationship. */
#define GAME_DEBUG_ENEMY_OPINION -80.0f
#define GAME_DEBUG_RIVAL_OPINION -42.0f
#define GAME_DEBUG_FRIEND_OPINION 42.0f
#define GAME_DEBUG_CLOSE_FRIEND_OPINION 80.0f

/* Debug: `a` leads a chat with `b` now, wherever they are, turned into
 * `kind` (a ChatKind, GAME_DEBUG_CHAT_ANY or GAME_DEBUG_CHAT_BRAWL). What it
 * needs is made up when missing: someone to gossip about (`a` then loathes a
 * third settler), a tale to pass on (a third settler spoke ill of `b`), a
 * story to tell (`a` killed a wolf). `out` (may be NULL) gets how it went.
 * False when either is not a living settler, or they are the same. */
bool game_debug_chat(Game *game, EntityId a, EntityId b, int kind, ChatResult *out);

/* Debug: what `who` thinks of `whom`, GAME_DEBUG_OPINION_STEP points up
 * (`direction` > 0) or down. */
bool game_debug_opinion(Game *game, EntityId who, EntityId whom, int direction);

/* Debug: `a` and `b` think of each other what makes `level` (both ways). */
bool game_debug_relation(Game *game, EntityId a, EntityId b, RelationLevel level);

/* Debug: the next family tie of `b` to `a` (none, parent, child, sibling,
 * grandparent, grandchild, none...), and the reverse. */
bool game_debug_family(Game *game, EntityId a, EntityId b);

/* Debug: every settler takes against every other: a rival, one time in
 * GAME_DEBUG_DISCORD_ENEMIES an enemy (one way each, from the social stream). */
#define GAME_DEBUG_DISCORD_ENEMIES 3
void game_debug_discord(Game *game);

/* Debug: the living settlers split in two clans, taken in turn by slot:
 * friends within (GAME_DEBUG_FRIEND_OPINION), enemies across; in each clan,
 * every other settler abrasive and bold, the rest kind (who apologize).
 * Without journal notes: a situation to start from (--scenario 4). */
void game_debug_feud(Game *game);

/* Debug: what the debug tools did to opinions is forgotten. */
void game_debug_forget_social(Game *game);

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

/* The item drawn at a point of level z, in tiles (items_at_point()), among
 * the items really there (the debug tools see everything). */
ItemId game_item_at_point(const Game *game, float x, float y, int z);

/* An item lying on the ground as the player sees it (a selection): its
 * tile, and its id among the items drawn there (game_items_at(): the real
 * ones in view, the picture of them out of view). */
typedef struct ItemPick {
    ItemId id;
    int16_t x, y, z;
} ItemPick;

/* The items drawn under the point (x, y) of level z, in tiles, the topmost
 * first: a tile's newest ITEM_DRAWN_PER_TILE, the newest on top, the tiles
 * drawn row by row over those before them; an item is hit within its
 * picture (item_drawn_size(), turned any way: a circle). Up to `capacity`
 * into `out`; returns how many. */
int game_items_under_point(const Game *game, float x, float y, int z, ItemPick *out, int capacity);

/* The items drawn within the rectangle (corners in any order, in tiles) of
 * level z: those whose center lies inside it, a tile's newest
 * ITEM_DRAWN_PER_TILE, as game_items_under_point() sees them (the real
 * items where someone sees, the known world's elsewhere), row by row. Up to
 * `capacity` into `out`; returns how many. */
int game_items_in_rect(const Game *game, float x0, float y0, float x1, float y1, int z,
                       ItemPick *out, int capacity);

/* game_items_in_rect() of the items of one type alone (the capacity counts
 * those only: a crowd of other things does not hide them). */
int game_items_of_type_in_rect(const Game *game, float x0, float y0, float x1, float y1, int z,
                               ItemType type, ItemPick *out, int capacity);

/* The item a pick names now, NULL when it is gone (picked up, eaten, rotted
 * away). The pick follows it: onto its
 * new tile when moved, into the picture when its tile leaves the view and
 * back into the real items when it comes into view. */
const Item *game_pick_item(const Game *game, ItemPick *pick);

/* Debug: moves an item lying on the ground to the point (x, y) of level z,
 * in tiles: onto that tile, at that very spot, whatever lies there, if
 * someone could stand there. Returns whether it moved. */
bool game_debug_move_item(Game *game, ItemId id, float x, float y, int z);

/* Debug: turns an item lying on the ground by `steps` of 1/256 of a turn. */
bool game_debug_turn_item(Game *game, ItemId id, int steps);

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

/* Debug: takes a block away at once, for tests of water and digging: a wall
 * of rock or earth on (x, y, z) goes, its floor left; else the floor there
 * goes with the rock under it, a hole down to the level below (what lay on
 * it and its water fall in). Trunks, structures and the lowest level stay.
 * Returns whether something went. */
bool game_debug_remove_block(Game *game, int x, int y, int z);

/* Debug: pours `mm` of water onto the floor (x, y, z) (takes it away when
 * negative): what a full tile cannot hold goes into the open tile above it,
 * and water is taken from the top. Returns the water poured (or taken, as a
 * negative number). */
int game_debug_pour_water(Game *game, int x, int y, int z, int mm);

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

/* Speed of rot of an item of a type lying on a tile (item_rot_rate() at the
 * tile's temperature, under its roof if any; out in the rain faster for what
 * takes no water: the rain soaks the others, item_stack_rot_hours()); 0 out
 * of bounds. */
float game_item_rot_rate(const Game *game, ItemType type, int x, int y, int z);

/* What items lying on a tile go through now (docs/WATER.md §7.3): the air's
 * humidity at the tile's temperature, raised by soaked ground, the rain under
 * the open sky, water on the tile, the sun and the heat that dry them. */
ItemConditions game_item_conditions(const Game *game, int x, int y, int z);

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

/* Whether a door stands on the tile and is kept open (Map.doors). */
bool game_door_kept_open(const Game *game, int x, int y, int z);

/* Keeps the door on the tile open (the light, the sight, the warmth of its
 * room and the animals go through) or lets it close behind whoever passes.
 * False when no door the colony knows of stands there. */
bool game_set_door_kept_open(Game *game, int x, int y, int z, bool open);

/* The light on a tile now (0 .. 1, docs/SHADOWS.md §5.2): the sun
 * (game_sunlight()) and the sky (weather_light()) times the tile's own share
 * of them (game_tile_light()). `game_light_peak_at()` is the best it gets in
 * the day (the noon sun, the tile's share in the sun around noon). */
float game_light_at(const Game *game, int x, int y, int z);
float game_light_peak_at(const Game *game, int x, int y, int z);
/* Works out now the light of the tiles (Game.light, tile_light_refresh())
 * after the map or the clock was changed by hand. */
void game_refresh_light(Game *game);
/* The share of the daylight coming straight from the sun at an elevation
 * (degrees) under today's sky (shadow_direct_share(): none unless clear). */
float game_direct_share(const Game *game, float elevation);
/* How much of the light of open ground a tile in the map gets, 0 .. 1: the
 * sky it sees (Game.light) and, for `direct_share` of the light, its share in
 * the sun now (or around noon); a tile of a tree's trunk gets it all (the
 * crown is in the light); tiles over the plant levels what covers them. */
float game_tile_light(const Game *game, int x, int y, int z, float direct_share, bool noon);

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
 * Orders the items on (x, y, z) carried to the tile (dx, dy, dz),
 * which may be anywhere a settler can stand and has room (a stockpile or
 * not): hauling settlers do it before their ordinary hauls (core/item_move.h).
 * They are forbidden at once so that nobody else takes them, and stay so
 * where they land, until the player allows them. An order on a tile that
 * already has one replaces it, and so it does the orders on single items
 * lying there. Refused (false) when the source holds nothing or has not been
 * seen, when the destination is unexplored, not standable, the same tile or
 * has no room, or when there are too many orders.
 */
bool game_order_item_move(Game *game, int x, int y, int z, int dx, int dy, int dz);

/* Takes the order on the stack at (x, y, z) back (what was forbidden stays so).
 * Returns whether there was one. */
bool game_cancel_item_move(Game *game, int x, int y, int z);

/* Where the items at (x, y, z) are ordered to go; false when they are not. */
bool game_item_move_at(const Game *game, int x, int y, int z, TilePos *destination);

/* The item a pick names in the real items (game->items), even when the pick
 * shows the picture of a tile out of view (see game_pick_item()):
 * ITEM_ID_NONE when it is gone. */
ItemId game_pick_real_item(const Game *game, ItemPick *pick);

/* Whether the player forbade an item lying on the ground. */
bool game_item_forbidden(const Game *game, ItemId id);

/* Forbids or allows one item lying on the ground (on an explored tile).
 * Returns whether it changed. */
bool game_set_item_forbidden(Game *game, ItemId id, bool forbidden);

/* game_order_item_move() for one item lying on the ground: only it is
 * carried, and forbidden. */
bool game_order_single_item_move(Game *game, ItemId id, int dx, int dy, int dz);

/* Takes the order on one item back (it stays forbidden). Returns whether
 * there was one. */
bool game_cancel_single_item_move(Game *game, ItemId id);

/* The player's Carry order (docs/CARRYING.md §7.2, colonist_order_carry()):
 * the colonist fetches the item lying on an explored tile and keeps it,
 * locked. game_carry_refusal() tells why it would be refused
 * (colonist_carry_refusal(); an item on a tile not explored cannot be
 * reached), game_can_carry() whether it would not. */
bool game_order_carry(Game *game, EntityId colonist, ItemId item);
CarryRefusal game_carry_refusal(const Game *game, EntityId colonist, ItemId item);
bool game_can_carry(const Game *game, EntityId colonist, ItemId item);

/* Sets down where the colonist stands the item it keeps at `index` of its
 * tools, forbidden (colonist_put_down()); locks it (colonist_set_lock()). */
bool game_put_down(Game *game, EntityId colonist, int index);
bool game_set_carry_lock(Game *game, EntityId colonist, int index, CarryLock lock);

/* Marks the weapon the colonist keeps at `index` of its tools primary, or
 * takes the mark away (colonist_set_primary_weapon()). */
bool game_set_primary_weapon(Game *game, EntityId colonist, int index, bool primary);

/* Mobilization (docs/COMBAT.md §8, colonist_set_drafted()):
 * game_can_draft() tells whether the colonist could be mobilized (not down,
 * in a mental break or fleeing), game_is_drafted() whether it is.
 * game_set_drafted() mobilizes (or demobilizes) every colonist of `ids`
 * that can be; returns how many changed. A colonist demobilized by itself
 * raises a notice (NOTICE_UNDRAFTED_*). */
bool game_can_draft(const Game *game, EntityId colonist);
bool game_is_drafted(const Game *game, EntityId colonist);
int game_set_drafted(Game *game, const EntityId *ids, int count, bool drafted);

/* The Attack order (colonist_order_attack()): every mobilized colonist of
 * `ids` goes after the animal, which the colony must see. Returns how many
 * were sent. */
int game_order_attack(Game *game, const EntityId *ids, int count, Combatant target);

/* A weapon a mobilized colonist may wield (docs/COMBAT.md §8): its fists
 * (slot -1, no item), one it keeps (its slot in the tools) or one lying
 * within a tile of it (its item). */
typedef struct WeaponChoice {
    int slot;
    ItemId item;
    ItemType type; /* ITEM_NONE for its fists. */
} WeaponChoice;

/* What the colonist may wield: its fists, the weapons it keeps that it can
 * wield, those lying on an explored tile within a tile of it (one of items
 * alike: same type, quality and condition). Returns how
 * many were written (at most `capacity`). game_wielded_choice() is the slot
 * of the weapon it wields, -1 for its fists or none; game_wield() wields
 * one (colonist_wield(), colonist_wield_item()). */
int game_weapon_choices(const Game *game, EntityId colonist, WeaponChoice *out, int capacity);
int game_wielded_choice(const Game *game, EntityId colonist);
bool game_wield(Game *game, EntityId colonist, WeaponChoice choice);

/* The same for the item at `index` of the colonist's load: kept, locked
 * (colonist_keep_load_item(): its slot in the tools, -1 when refused;
 * game_can_keep_load_item() tells whether one could be kept so), or dropped,
 * forbidden (colonist_drop_load_item()). */
bool game_can_keep_load_item(const Game *game, EntityId colonist, CarryLock lock);
int game_keep_load_item(Game *game, EntityId colonist, int index, CarryLock lock);
bool game_drop_load_item(Game *game, EntityId colonist, int index);

/* Where one item is ordered to go; false when it is not. */
bool game_single_item_move_at(const Game *game, ItemId id, TilePos *destination);

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

/* What a bill's craft makes on average (the bills' window): among the
 * living settlers who may take it (game_bill_has_worker()'s), the product of
 * the least and of the most skilled at their working level, from the first
 * input type the bill allows at the best quality it allows, of its species
 * (recipe_average_product()). */
typedef struct BillEstimate {
    int crafters;                 /* Settlers who may take it; 0: the rest is unset. */
    double low_level, high_level; /* Their working levels (colonist_work_level()). */
    EntityId low_crafter, high_crafter;
    ItemStack low, high; /* What each makes on average. */
} BillEstimate;
BillEstimate game_bill_estimate(const Game *game, const Bill *bill);

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
/* game_place_structure_rect() of sites to be built of the species `woods`
 * (ConstructionSite.wood_choice, 0: any). */
int game_place_structure_rect_wood(Game *game, StructureType type, Direction direction, int x0,
                                   int y0, int x1, int y1, int z, WoodMask woods);

/* Removes a construction site; its delivered materials are dropped on its
 * tile. */
bool game_remove_construction(Game *game, int index);

#endif /* COLONY_CORE_GAME_H */
