/*
 * Colonists: needs, simple decision making and movement along paths.
 *
 * Each tick a colonist updates its needs, decides what to do when it stands
 * on a tile center (drink when thirsty, eat when hungry, sleep when tired or
 * at night, otherwise work: designations, construction sites, building and
 * crafting at workstations, hauling; or wander), then acts (walk one tick
 * along its path, eat, work, sleep...).
 *
 * Settlers sleep in their own bed (core/bed.h); one without a bed takes the
 * nearest free one, and one whose bed cannot be reached takes another. With
 * no bed at all they sleep on the ground: rest comes back more slowly there,
 * and after such a night they work more slowly for a while.
 *
 * Tools are simply carried, at most one of each type, and used without
 * equipping anything; they count in the carried weight. A colonist fetches a
 * tool when a recipe needs one it does not carry, and keeps it; one that
 * carries a tool fetches a better one lying close by, leaving the old one
 * there. Work goes as fast as the best carried tool for it allows (see
 * core/tool.h), and wears that tool: it breaks in the end.
 *
 * Knowledge spreads (docs/KNOWLEDGE.md §1.5, §1.6): a settler who finds no
 * job of a work type, and lacks the level for the job of that type someone
 * else is doing, assists them as an apprentice (TASK_ASSIST): it stands at
 * the master's side, adds COLONIST_APPRENTICE_HELP of a worker to the work and
 * learns, up to the master's level. Masters take one apprentice each.
 * Settlers chat now and then (core/conversation.h). Knowledges not practised
 * for a while fade (colonist_fade_knowledge(), once a day).
 *
 * Hearths (core/hearth.h): haulers bring them fuel (TASK_REFUEL), settlers
 * who know fire light them (TASK_LIGHT, work type WORK_FIRE). In the evening
 * (hearth_story_time()), a settler with nothing to do who masters a
 * knowledge the player marked to teach tells about it beside a burning
 * hearth (TASK_STORY), and others with nothing to do sit around and listen
 * (TASK_LISTEN), learning up to KNOWLEDGE_STORY_CAP.
 * Colonists are stored in the Game (see core/game.h); this module only works
 * on one colonist at a time with explicit dependencies.
 */
#ifndef COLONY_CORE_COLONIST_H
#define COLONY_CORE_COLONIST_H

#include "core/appearance.h"
#include "core/bed.h"
#include "core/construction.h"
#include "core/designation.h"
#include "core/dirt.h"
#include "core/game_clock.h"
#include "core/gather_zone.h"
#include "core/hearth.h"
#include "core/knowledge.h"
#include "core/map.h"
#include "core/pathfinding.h"
#include "core/pool.h"
#include "core/region.h"
#include "core/reservation.h"
#include "core/rng.h"
#include "core/stockpile.h"
#include "core/tool.h"
#include "core/work.h"
#include "core/workstation.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COLONIST_MAX 64
#define COLONIST_NAME_SIZE 32

/* Walking speed on terrain of cost 1, in tiles per second at normal game speed. */
#define COLONIST_WALK_SPEED 3.5f

/*
 * Carrying, by hand (no container yet): a colonist picks up at most
 * COLONIST_CARRY_BULK of bulk and a weight set by its strength
 * (COLONIST_CARRY_WEIGHT kilograms at COLONIST_STRENGTH_STANDARD,
 * COLONIST_CARRY_WEIGHT_PER_STRENGTH more or less per point). A single item
 * over either limit is still taken (dragged). Above the weight limit, walking
 * slows down in proportion (limit / load), never below
 * COLONIST_MIN_LOAD_SPEED.
 */
#define COLONIST_CARRY_WEIGHT 40.0f
#define COLONIST_CARRY_WEIGHT_PER_STRENGTH 2.0f
#define COLONIST_CARRY_BULK 60.0f
#define COLONIST_MIN_LOAD_SPEED 0.1f

/*
 * Strength, from COLONIST_STRENGTH_MIN to COLONIST_STRENGTH_MAX. Rolled at
 * creation: COLONIST_STRENGTH_BASE plus two dice of 0..COLONIST_STRENGTH_DIE
 * (10 on average), minus one point per COLONIST_STRENGTH_AGE_STEP years past
 * COLONIST_STRENGTH_PEAK_AGE. Later, health (injuries, hunger) will lower
 * what a colonist can carry without changing its strength.
 */
#define COLONIST_STRENGTH_MIN 1
#define COLONIST_STRENGTH_MAX 20
#define COLONIST_STRENGTH_STANDARD 10
#define COLONIST_STRENGTH_BASE 4
#define COLONIST_STRENGTH_DIE 6
#define COLONIST_STRENGTH_PEAK_AGE 40
#define COLONIST_STRENGTH_AGE_STEP 6

/* Tools carried at once: one per tool type at most. */
#define COLONIST_TOOL_MAX 8

/* Needs go from 1 (fully satisfied) to 0. */
#define NEED_FOOD_DECAY_PER_TICK (1.0f / (36.0f * TICKS_PER_HOUR))
#define NEED_WATER_DECAY_PER_TICK (1.0f / (24.0f * TICKS_PER_HOUR))
#define NEED_REST_DECAY_PER_TICK (1.0f / (20.0f * TICKS_PER_HOUR))
/* Rest gained asleep in a bed of rest factor 1 (see StructureDef). */
#define NEED_REST_GAIN_PER_TICK (1.0f / (8.0f * TICKS_PER_HOUR))
/* Rest gain multiplier when sleeping on the bare ground. */
#define COLONIST_GROUND_REST_FACTOR 0.8f
/* After waking up from a night on the ground, work is slower for this long. */
#define COLONIST_GROUND_SLEEP_PENALTY_TICKS (12 * TICKS_PER_HOUR)
#define COLONIST_GROUND_SLEEP_WORK_FACTOR 0.9f
/* Settlers walk at most this far to their bed or a free one. */
#define COLONIST_BED_SEARCH_MAX_COST 150.0f
#define NEED_LOW_THRESHOLD 0.3f
#define NEED_CRITICAL_THRESHOLD 0.1f
/* Colonists smile when every need is at least this well satisfied. */
#define NEED_CONTENT_THRESHOLD 0.7f

/* A hungry colonist keeps eating until food reaches this level. */
#define COLONIST_SATED_FOOD 0.8f
/* At night, colonists go to sleep below this rest level. */
#define COLONIST_NIGHT_SLEEP_REST 0.9f
/* Before going to sleep, colonists eat and drink when below this level, so
 * that they do not wake up starving or parched. */
#define COLONIST_BEDTIME_NEED 0.5f
#define COLONIST_EAT_TICKS 500
#define COLONIST_FOOD_SEARCH_MAX_COST 120.0f
/* Drinking fills the water need completely. */
#define COLONIST_DRINK_TICKS 300
/* Water is scarcer than berries: colonists accept longer trips to drink. */
#define COLONIST_WATER_SEARCH_MAX_COST 200.0f
/* Work done per tick on a designation at speed ×1 (see colonist_work_speed()). */
#define COLONIST_WORK_SPEED 1.0f
#define COLONIST_WORK_SEARCH_MAX_COST 250.0f
/* How far (travel cost) a settler who carries a tool looks for a better one
 * before a craft: a short detour, no long trip. */
#define COLONIST_TOOL_SWAP_MAX_COST 30.0f
/* Cleaning removes 1 % of dirt every this many ticks (a filthy tile takes
 * 10 seconds at speed ×1). */
#define COLONIST_CLEAN_TICKS_PER_PERCENT 6
/* After picking up a stack, a hauler with room left also collects matching
 * stacks within this travel cost before delivering. */
#define COLONIST_HAUL_COLLECT_MAX_COST 12.0f
/* Delay before searching again after a failed food, water or work search, doubled
 * after each consecutive failure up to 2^COLONIST_SEARCH_MAX_BACKOFF times. A
 * failed search floods everything within its cost limit, so it must stay rare. */
#define COLONIST_SEARCH_RETRY_TICKS (TICKS_PER_HOUR / 2)
#define COLONIST_SEARCH_MAX_BACKOFF 3
/* A change in what a failed work search looks for (items added, a job let go)
 * cuts that delay short, but not sooner than this after the failure: with
 * many settlers something changes every tick, and idle ones would search
 * again and again for the few jobs left. */
#define COLONIST_SEARCH_MIN_RETRY_TICKS 60
#define COLONIST_WANDER_RADIUS 6
#define COLONIST_IDLE_MIN_TICKS 120
#define COLONIST_IDLE_MAX_TICKS 360
/* Apprentices add this share of a worker to their master's work. */
#define COLONIST_APPRENTICE_HELP 0.25f
/* Delay between two searches for a master to assist, and, while assisting,
 * for work of a higher priority. */
#define COLONIST_ASSIST_SEARCH_TICKS 250
/* An apprentice waits this long for a master between two jobs. */
#define COLONIST_ASSIST_WAIT_TICKS 250

typedef enum ColonistTask {
    TASK_IDLE,
    TASK_WANDER,
    TASK_EAT, /* Walking to food, then eating. */
    TASK_DRINK, /* Walking to a drinking spot, then drinking. */
    TASK_SLEEP,
    TASK_WORK, /* Walking next to a designated tile, then working on it. */
    TASK_HAUL, /* Walking to an item stack, picking it up, carrying it to storage. */
    TASK_CRAFT, /* Fetching tools and input, then working through a bill at a station. */
    /* Bringing materials to a construction site (a structure's or a
     * workstation's), then building it from a neighboring tile. */
    TASK_CONSTRUCT,
    TASK_SUPPLY, /* Bringing materials to a construction site (hauling work). */
    TASK_CLEAN, /* Walking to a dirty tile, then cleaning it. */
    /* Following a master (Colonist.mentor) and helping with their job. */
    TASK_ASSIST,
    TASK_REFUEL, /* Bringing fuel to a hearth (hauling work). */
    TASK_LIGHT,  /* Lighting a hearth: embers from a burning fire, or friction. */
    TASK_STORY,  /* Telling about a knowledge beside a burning hearth. */
    TASK_LISTEN, /* Sitting near a storyteller (Colonist.mentor) and listening. */
    TASK_COUNT
} ColonistTask;

typedef enum NeedType {
    NEED_FOOD,
    NEED_WATER,
    NEED_REST,
    NEED_COUNT
} NeedType;

/* Things a colonist searches the map for; failed searches back off per kind. */
typedef enum SearchKind {
    SEARCH_FOOD,
    SEARCH_WATER,
    SEARCH_WORK,
    SEARCH_HAUL,
    SEARCH_STATION, /* Crafting jobs at workstations. */
    SEARCH_GATHER,  /* Gathering zones. */
    SEARCH_CONSTRUCTION, /* Construction sites. */
    SEARCH_CLEAN,        /* Dirty tiles. */
    SEARCH_SUPPLY,       /* Construction sites missing materials, for haulers. */
    SEARCH_ASSIST,       /* Masters to assist, or better work while assisting. */
    SEARCH_REFUEL,       /* Hearths wanting fuel. */
    SEARCH_LIGHT,        /* Hearths to light. */
    SEARCH_STORY,        /* A hearth to tell stories at, or a story to listen to. */
    SEARCH_COUNT
} SearchKind;

/* Steps of a job at a workstation (TASK_CRAFT) or a construction site
 * (TASK_CONSTRUCT, TASK_SUPPLY). */
typedef enum StationJobPhase {
    STATION_JOB_FETCH, /* Walking to `fetch` to pick up a tool, an input or materials. */
    STATION_JOB_GO,    /* Walking to the station (next to the site). */
    STATION_JOB_WORK,  /* Working on it. */
} StationJobPhase;

/* Steps of lighting a hearth (TASK_LIGHT). */
typedef enum LightStep {
    LIGHT_FETCH_TINDER, /* Walking to dry grass (`fetch`) for lighting by friction. */
    LIGHT_TO_SOURCE,    /* Walking to a burning hearth (`fire_source`) for embers. */
    LIGHT_EMBERS,       /* Wrapping embers in grass beside it. */
    LIGHT_GO,           /* Walking to the hearth to light (`station`). */
    LIGHT_FRICTION,     /* Beside it, twirling a stick on a board. */
} LightStep;

/* Face shown by the colonist sprite, from how well its needs are met. */
typedef enum ColonistExpression {
    EXPRESSION_NEUTRAL,
    EXPRESSION_SMILING,
    EXPRESSION_SAD,
    EXPRESSION_ANGRY,
    EXPRESSION_COUNT
} ColonistExpression;

/* What a colonist's hands are doing, for the frontend to animate. */
typedef enum ColonistAction {
    COLONIST_ACTION_NONE,   /* Walking, idle, asleep, carrying. */
    COLONIST_ACTION_STRIKE, /* Blows: mining, felling, building, knapping. */
    COLONIST_ACTION_CUT,    /* Back and forth with a cutting tool: shaping a spear. */
    COLONIST_ACTION_GATHER, /* Picking or pulling: berries, branches, twigs, grass, stones. */
    COLONIST_ACTION_EAT,    /* The hand to the mouth: eating or drinking. */
    COLONIST_ACTION_COUNT
} ColonistAction;

typedef enum NeedLevel {
    NEED_LEVEL_OK,
    NEED_LEVEL_LOW,
    NEED_LEVEL_CRITICAL
} NeedLevel;

typedef struct Colonist {
    char name[COLONIST_NAME_SIZE]; /* Proper noun: never translated. */
    int age;
    int x; /* Current tile. */
    int y;
    Path path;
    int path_index; /* Next step of `path` to walk to. */
    float move_progress; /* 0..1 from the current tile to the next step. */
    ColonistTask task;
    /* Food tile (TASK_EAT), drinking spot (TASK_DRINK), designated tile
     * (TASK_WORK) or dirty tile (TASK_CLEAN). */
    TilePos target;
    int timer; /* Idle countdown (TASK_IDLE), eating or drinking progress. */
    /* TASK_WORK: the DesignationType being worked on, or the GatherJob when
     * `gathering` (in a gathering zone rather than on a designation). */
    uint8_t work_type;
    bool gathering;
    int search_cooldown[SEARCH_COUNT]; /* Ticks before searching again. */
    int search_failures[SEARCH_COUNT]; /* Consecutive failed searches. */
    int search_quiet[SEARCH_COUNT]; /* Ticks before a change may cut the delay short. */
    /* Designations revision + work reservation releases at the last failed
     * work search. */
    uint32_t work_search_stamp;
    /* Stockpile revision + item additions + releases of stacks and storage
     * tiles at the last failed haul search. */
    uint32_t haul_search_stamp;
    /* Workstations revision + item additions + releases at the last failed
     * crafting search. */
    uint32_t station_search_stamp;
    /* Gathering zones revision + work reservation releases at the last failed
     * gathering search. */
    uint32_t gather_search_stamp;
    /* Construction sites revision + item additions + releases at the last
     * failed construction search. */
    uint32_t construction_search_stamp;
    /* Dirt revision + cleaning reservation releases at the last failed
     * cleaning search. */
    uint32_t clean_search_stamp;
    /* Same as construction_search_stamp, for the last failed supply search. */
    uint32_t supply_search_stamp;
    /* Workstations revision + item additions + releases at the last failed
     * refuel and lighting searches. */
    uint32_t refuel_search_stamp;
    uint32_t light_search_stamp;
    ItemStack carried; /* Items in hand (TASK_HAUL), dropped if the task is abandoned. */
    TilePos haul_destination; /* Storage tile the carried items go to (TASK_HAUL). */
    /* TASK_HAUL: false while collecting stacks (`target`), true once heading
     * for `haul_destination`. */
    bool haul_delivering;
    /* TASK_CRAFT: the station (index, and number to notice a
     * replacement in the same slot), the bill being worked through and its
     * recipe, the current step, and the stack being fetched (a tool when
     * `fetch_tool`). `target` is the station's tile. */
    int station;
    int station_number;
    uint8_t bill;
    uint8_t recipe;
    uint8_t station_phase; /* StationJobPhase */
    TilePos fetch;
    bool fetch_tool;
    /* TASK_CONSTRUCT, TASK_SUPPLY: the site (index, and number to notice a
     * replacement); `target` is the tile of the site worked or delivered by,
     * the phase and fetch fields are shared with station jobs. The material
     * slot of the trip under way, and the items of it promised to the site
     * (construction_promise(), released when the task changes). */
    int site;
    int site_number;
    int supply_slot;
    int supply_promised;
    /* Ticks left of slower work after a night on the ground. */
    int ground_sleep_ticks;
    /* Tools carried (one per type, count 1; empty slots have type ITEM_NONE). */
    ItemStack tools[COLONIST_TOOL_MAX];
    /* Ticks of work done with each tool since it last lost a hit point. */
    uint16_t tool_wear[COLONIST_TOOL_MAX];
    int tools_broken; /* Tools worn out so far (for the player's information). */
    int strength; /* COLONIST_STRENGTH_MIN..COLONIST_STRENGTH_MAX. */
    float max_carry_weight; /* Kilograms carried without slowing down (from strength). */
    float max_carry_bulk;   /* Bulk carried at once (see ITEM_TILE_BULK). */
    uint8_t work_priorities[WORK_TYPE_COUNT]; /* 0 = never, else 1 (highest) .. 4. */
    /* Level in each knowledge, KNOWLEDGE_LEVEL_MIN..KNOWLEDGE_LEVEL_MAX. Double
     * precision: a tick of practice adds a tiny fraction of a point. */
    double knowledge[KNOWLEDGE_COUNT];
    /* Highest level ever reached in each knowledge: fading stops at
     * KNOWLEDGE_FADE_FLOOR of it. */
    double knowledge_peak[KNOWLEDGE_COUNT];
    /* Tick of the last practice of each knowledge: doing its work, or
     * having an apprentice in it. */
    uint64_t knowledge_practice[KNOWLEDGE_COUNT];
    /* Points gained per knowledge and way of learning on the day
     * `knowledge_gains_day` (for the player's information only). */
    float knowledge_gains[KNOWLEDGE_COUNT][KNOWLEDGE_SOURCE_COUNT];
    uint32_t knowledge_gains_day;
    /* TASK_ASSIST: the master, the work type and knowledge of their job when
     * last seen, and how long the apprentice has waited for them to get back
     * to work. */
    EntityId mentor;
    uint8_t assist_work;
    uint8_t assist_knowledge;
    int assist_wait;
    /* A master works faster up to this tick: an apprentice is helping. */
    uint64_t assisted_until;
    /* Conversation under way (see core/conversation.h): ticks left, with
     * whom, and the knowledge brought up (KNOWLEDGE_NONE: small talk). */
    int chat_ticks;
    EntityId chat_partner;
    uint8_t chat_topic;
    /* TASK_REFUEL: kilograms of fuel promised to the hearth (`station`),
     * released when the task changes. */
    float fuel_promised;
    /* TASK_LIGHT: the step (LightStep), the burning hearth embers come from
     * (index and number), and the work done on the step. The hearth to light
     * is `station`, reserved as RESERVATION_FIRE on `target`. */
    uint8_t light_step;
    int fire_source;
    int fire_source_number;
    float light_progress;
    int ember_ticks; /* Embers carried: ticks before they go out. */
    /* TASK_STORY: the knowledge told at the hearth `station` (`target`). */
    uint8_t story_topic;
    float needs[NEED_COUNT];
    ColonistAppearance appearance; /* Cosmetic only: never affects the simulation. */
} Colonist;

/* What a colonist needs to see of the world to make decisions. */
typedef struct ColonistContext {
    Map *map;
    Designations *designations;
    Dirt *dirt;
    GatherZones *gather_zones;
    Items *items;
    Stockpiles *stockpiles;
    Workstations *workstations;
    Constructions *constructions;
    Beds *beds;
    Reservations *reservations;
    Pathfinder *pathfinder;
    /* Connectivity regions (may be out of date, never wrong: see core/region.h). */
    const Regions *regions;
    const long *stock; /* Game.stock: items in stock per type. */
    Rng *rng;
    uint64_t tick;
    bool night; /* colonist_is_night(tick), computed once per tick for everyone. */
    uint8_t worker_id; /* Reservation owner for this colonist (slot + 1), never 0. */
    /* Every colonist (Game.colonists) and who is alive, to find a master to
     * assist; NULL: nobody assists anyone. */
    Colonist *colonists;
    const Pool *colonist_pool;
    /* Knowledges the player wants taught at fireside stories (Game.teach);
     * NULL: none. */
    const bool *teach;
} ColonistContext;

void colonist_init(Colonist *colonist, const char *name, int age, int x, int y);
void colonist_free(Colonist *colonist);

/* Whether it is time to be in bed (22:00 to 06:00). */
bool colonist_is_night(uint64_t tick);

void colonist_tick(Colonist *colonist, const ColonistContext *context);

/*
 * Changes how much a colonist favors a work type (WORK_PRIORITY_DISABLED:
 * never does it). Fails on an invalid type or priority. The colonist looks
 * for work again right away, and gives up a job whose work type got disabled
 * the next time it decides (a hauler finishes carrying what it holds).
 */
bool colonist_set_work_priority(Colonist *colonist, WorkType type, int priority);

/* Level in a knowledge; KNOWLEDGE_LEVEL_MIN for KNOWLEDGE_NONE or an invalid one. */
double colonist_knowledge_level(const Colonist *colonist, KnowledgeType knowledge);

/* Fails on an invalid knowledge or a level out of range. */
bool colonist_set_knowledge_level(Colonist *colonist, KnowledgeType knowledge, double level);

/*
 * Learning: `hours` at `rate` points per hour (see knowledge_learn()), never
 * past `cap`. The gain is counted in the gains of the day of `tick`, under
 * `source`, and raises the peak. Returns the points gained.
 */
double colonist_learn(Colonist *colonist, KnowledgeType knowledge, KnowledgeSource source,
                      double rate, double hours, double cap, uint64_t tick);

/* Learning by doing: `ticks` of work on something of this knowledge at
 * `tick`, which also counts as practice. Ignored for KNOWLEDGE_NONE and
 * invalid values. */
void colonist_learn_by_doing(Colonist *colonist, KnowledgeType knowledge, int ticks,
                             uint64_t tick);

/* Points gained today (the day of `tick`) in a knowledge by one way of
 * learning. */
double colonist_knowledge_gain_today(const Colonist *colonist, KnowledgeType knowledge,
                                     KnowledgeSource source, uint64_t tick);

/* Whole days since the knowledge was last practised. */
int colonist_days_unpractised(const Colonist *colonist, KnowledgeType knowledge, uint64_t tick);

/* Whether the knowledge is fading (unpractised for long enough, above its
 * floor). */
bool colonist_knowledge_fading(const Colonist *colonist, KnowledgeType knowledge, uint64_t tick);

/* A day goes by: knowledges unpractised for long enough fade a little (see
 * knowledge_fade_day()). */
void colonist_fade_knowledge(Colonist *colonist, uint64_t tick);

/* Marks every knowledge as practised at `tick` (a newcomer: what they knew
 * before is fresh). */
void colonist_reset_practice(Colonist *colonist, uint64_t tick);

/* Whether what the colonist is doing leaves it free to chat: idle or
 * wandering, or standing at a meal, a drink or a job. Asleep, walking to
 * something or carrying a load: no. */
bool colonist_can_chat(const Colonist *colonist);

/* What a job the colonist is busy with needs (the master's side of
 * apprenticeship). */
typedef struct ColonistJobNeed {
    KnowledgeType knowledge; /* Never KNOWLEDGE_NONE when the job counts. */
    float min_level;
    WorkType work_type;
} ColonistJobNeed;

/* What the colonist's job needs: false when it is not a job of a knowledge
 * (hauling, eating...). */
bool colonist_job_need(const Colonist *colonist, const ColonistContext *context,
                       ColonistJobNeed *need);

/* Whether the colonist is at the work itself (not walking to it nor
 * fetching what it needs). */
bool colonist_at_work(const Colonist *colonist);

/* Whether the colonist knows enough to carry out this kind of order. */
bool colonist_can_do(const Colonist *colonist, DesignationType type);

/* Whether the colonist knows enough to do this gathering job. */
bool colonist_can_gather(const Colonist *colonist, GatherJob job);

/* Work done per tick on something of `knowledge`: COLONIST_WORK_SPEED scaled
 * by knowledge_work_speed() (unscaled for KNOWLEDGE_NONE), and by
 * COLONIST_GROUND_SLEEP_WORK_FACTOR after a night on the ground. */
float colonist_work_speed(const Colonist *colonist, KnowledgeType knowledge);

/* Whether the colonist knows enough to build this kind of workstation. */
bool colonist_can_build(const Colonist *colonist, WorkstationType type);

/* Whether the colonist knows enough to build what a construction site
 * builds (a structure or a workstation). */
bool colonist_can_build_site(const Colonist *colonist, const ConstructionSite *site);

/* Whether the colonist knows enough to build this kind of structure. */
bool colonist_can_construct(const Colonist *colonist, StructureType type);

/* Whether the colonist knows fire well enough to light a hearth. */
bool colonist_can_light(const Colonist *colonist);

/* Whether the colonist tells a story now: beside the burning hearth of its
 * TASK_STORY, not walking. */
bool colonist_telling(const Colonist *colonist, const Workstations *workstations);

/* The knowledge the colonist would tell about at the fireside: among those
 * marked in `teach`, the one it knows best, at least at mastery;
 * KNOWLEDGE_NONE when there is none. */
KnowledgeType colonist_story_topic(const Colonist *colonist, const bool *teach);

/* Whether the colonist knows enough to make this recipe. */
bool colonist_can_craft(const Colonist *colonist, RecipeType recipe);

/* Whether the colonist's level in the recipe's knowledge lies in the bill's
 * worker level range (see bill_accepts_level()). */
bool colonist_level_fits_bill(const Colonist *colonist, const Bill *bill);

/* Index in `tools` of the best carried tool among `types` (grade, then hit
 * points), or -1 when it carries none of them. */
int colonist_tool_index(const Colonist *colonist, ItemTypeMask types);

/* Takes one item as a tool (any item a recipe uses as one, e.g. a cobble as
 * a hammer). Fails when one of the same type is already carried, or every
 * slot is used. */
bool colonist_add_tool(Colonist *colonist, ItemStack tool);

/* Takes one item as a tool in place of the carried one of the same type,
 * which is written to `replaced` (an empty stack when there was none: the
 * tool is then simply added). Fails like colonist_add_tool() otherwise. */
bool colonist_swap_tool(Colonist *colonist, ItemStack tool, ItemStack *replaced);

/* Speed factor the colonist works at for a kind of work: its best carried
 * tool among `allowed` (bare hands when none does better), see core/tool.h. */
float colonist_tool_factor(const Colonist *colonist, ToolUse use, ItemTypeMask allowed);

/* Drops every carried tool where the colonist stands (e.g. before removal). */
void colonist_drop_tools(Colonist *colonist, Items *items, const Map *map);

/* Gives up the current task, releasing any reservation (e.g. before removal). */
void colonist_abandon_task(Colonist *colonist, const ColonistContext *context);

bool colonist_is_moving(const Colonist *colonist);

/* Interpolated position of the colonist's center, in world (tile) coordinates. */
void colonist_position(const Colonist *colonist, float *x, float *y);

/* Kilograms carried without slowing down at a strength (clamped to the valid range). */
float colonist_carry_weight_for_strength(int strength);

/* Random strength for a new colonist of this age. */
int colonist_random_strength(Rng *rng, int age);

/* Sets the strength and the weight limit that follows from it. Fails on a
 * value out of range. */
bool colonist_set_strength(Colonist *colonist, int strength);

/* Kilograms the colonist is carrying: the load in hand and the tools. */
float colonist_carried_weight(const Colonist *colonist);

/* How many items of a type the colonist can pick up at once, in what the
 * weight and bulk limits leave once its tools are counted (at least 1: an
 * item too heavy or too bulky is dragged). */
int colonist_carry_capacity(const Colonist *colonist, ItemType type);

/* Walking speed multiplier from the load: 1 up to the weight limit, lower above. */
float colonist_load_speed_factor(const Colonist *colonist);

NeedLevel need_level(float value);

/* The action under way and, when it needs one, the tool in hand (written to
 * `tool` when not NULL; ITEM_NONE without a tool). Only a colonist standing at
 * its job and actually working on it acts; the tool is the one of the first
 * tool kind of the recipe being crafted. */
ColonistAction colonist_action(const Colonist *colonist, ItemType *tool);

/* Angry when a need is critical, sad when one is low, smiling when every need
 * reaches NEED_CONTENT_THRESHOLD, neutral otherwise. */
ColonistExpression colonist_expression(const Colonist *colonist);

/* Translation keys. need_level_key() returns NULL for NEED_LEVEL_OK. */
const char *colonist_task_name_key(ColonistTask task);
/* What the colonist is busy with: the task name, or the kind of work (e.g. "Chopping a tree"). */
const char *colonist_activity_key(const Colonist *colonist);
const char *need_name_key(NeedType need);
const char *need_level_key(NeedType need, NeedLevel level);

/* Built-in list of first names, picked from when colonists are created. */
size_t colonist_name_count(void);
const char *colonist_name_at(size_t index);

#endif /* COLONY_CORE_COLONIST_H */
