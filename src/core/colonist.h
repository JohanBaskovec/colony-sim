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
 * and they remember the night (core/mood.h).
 *
 * Moods (core/mood.h): every MOOD_UPDATE_TICKS a colonist takes stock of
 * how it is (hungry, cold, soaked, out in the rain...) and its mood drifts;
 * it remembers how it slept, what it ate, a fire that warmed it, stories and
 * chats. Its mood speeds up or slows down its work and learning, and makes
 * what it makes better or worse (colonist_work_level()). A colonist in a
 * very bad mood may break down: it then sulks and does no work, goes off
 * alone far away, or eats far more than it needs; it still eats, drinks and
 * sleeps when it must.
 *
 * Attributes (core/attributes.h, docs/ATTRIBUTES.md): innate values that
 * make a colonist walk, work, learn, sleep and eat faster or slower, find
 * cold or heat more or less bearable, and set its mood higher or lower and
 * how strongly its thoughts weigh.
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

#include "core/serialize.h"

#include "core/allowed_area.h"
#include "core/animal.h"
#include "core/appearance.h"
#include "core/attributes.h"
#include "core/experience.h"
#include "core/bed.h"
#include "core/carry.h"
#include "core/combat.h"
#include "core/construction.h"
#include "core/designation.h"
#include "core/health.h"
#include "core/journal.h"
#include "core/dirt.h"
#include "core/game_clock.h"
#include "core/gather_zone.h"
#include "core/hearth.h"
#include "core/item_move.h"
#include "core/knowledge.h"
#include "core/map.h"
#include "core/mood.h"
#include "core/occupancy.h"
#include "core/pathfinding.h"
#include "core/pool.h"
#include "core/region.h"
#include "core/reservation.h"
#include "core/rng.h"
#include "core/soil.h"
#include "core/stockpile.h"
#include "core/temperature.h"
#include "core/tool.h"
#include "core/vision.h"
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
 * Carrying, by hand (no container yet, docs/CARRYING.md, core/carry.h): a
 * colonist carries in its hands, both arms, under its arms, on its shoulders,
 * what each place takes by the item's form, length, room and weight, up to a
 * weight set by its strength (COLONIST_CARRY_WEIGHT kilograms at
 * COLONIST_STRENGTH_STANDARD, COLONIST_CARRY_WEIGHT_PER_STRENGTH more or less
 * per point) and its arms' health. Its tools take places too. A single item
 * over every place is still taken (on the shoulders or dragged). Above the
 * weight limit, walking slows down in proportion (limit / load), never below
 * COLONIST_MIN_LOAD_SPEED.
 */
#define COLONIST_CARRY_WEIGHT 40.0f
#define COLONIST_CARRY_WEIGHT_PER_STRENGTH 2.0f
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
/* Sleeping where it feels colder or hotter than this rests less: the rest
 * gain loses COLONIST_SLEEP_LOSS_PER_DEGREE of itself per degree beyond,
 * down to COLONIST_SLEEP_TEMPERATURE_FACTOR_MIN (tuning). The felt
 * temperature counts the warmth of fires (hearths_felt_temperature()). */
#define COLONIST_COLD_SLEEP_BELOW 10.0f
#define COLONIST_HOT_SLEEP_ABOVE 30.0f
#define COLONIST_SLEEP_LOSS_PER_DEGREE 0.025f
#define COLONIST_SLEEP_TEMPERATURE_FACTOR_MIN 0.5f
/* A settler with no bed to go to, who would sleep too cold or too hot where
 * it stands, walks at most this far to a spot that feels right beside a
 * fire. */
#define COLONIST_WARM_SPOT_MAX_COST 80.0f
/* Asleep out of a bed, a settler checks this often whether it feels too
 * cold or hot (the night gets colder, a fire burns low) and moves. */
#define COLONIST_WARM_SPOT_CHECK_TICKS (TICKS_PER_HOUR / 2)
/* Out under the open sky while rain or snow falls, settlers get wet at the
 * weather's rate (weather_wetting(): soaked in an hour of rain), and dry by
 * COLONIST_DRY_PER_HOUR, COLONIST_DRY_WARM_PER_HOUR where it feels at least
 * COLONIST_DRY_WARM_CELSIUS (beside a fire). Wet through, a settler feels
 * COLONIST_WET_CHILL degrees colder (tuning). They do not run for shelter:
 * that comes with the risk of hypothermia (health). */
#define COLONIST_DRY_PER_HOUR 0.25f
#define COLONIST_DRY_WARM_PER_HOUR 1.0f
#define COLONIST_DRY_WARM_CELSIUS 20.0f
#define COLONIST_WET_CHILL 8.0f
/* Settlers walk at most this far to their bed or a free one. */
#define COLONIST_BED_SEARCH_MAX_COST 150.0f
#define NEED_LOW_THRESHOLD 0.3f
#define NEED_CRITICAL_THRESHOLD 0.1f
/* Hunger and thirst only weigh on the mood below NEED_CRITICAL_THRESHOLD
 * (a settler has the time to go and eat before it feels it: it looks for
 * food from NEED_LOW_THRESHOLD), and for the worse below
 * NEED_EMPTY_THRESHOLD (starving, parched). Tiredness weighs from
 * NEED_LOW_THRESHOLD, and for the worse below NEED_CRITICAL_THRESHOLD. */
#define NEED_EMPTY_THRESHOLD 0.03f
/* Company (docs/SOCIAL.md §4), from 1 (plenty) to 0: awake, a settler misses
 * it at this rate times its sociability factor; listening to a story,
 * telling one or working beside a master or an apprentice fills it at
 * COLONIST_COMPANY_GAIN_PER_TICK, chats at once (core/conversation.h).
 * Below COLONIST_LONELY_BELOW it feels lonely. An idle settler about to
 * wander goes over to someone instead (or stays, when someone is already
 * within COLONIST_COMPANY_NEAR tiles) with a chance growing as company runs
 * low: none from COLONIST_SEEK_COMPANY_FROM up, certain once lonely. It
 * picks whom by liking (COLONIST_SEEK_*_WEIGHT, rivals and enemies never)
 * and nearness. */
#define COLONIST_COMPANY_DECAY_PER_TICK (1.0f / (30.0f * TICKS_PER_HOUR))
#define COLONIST_COMPANY_GAIN_PER_TICK (1.0f / (3.0f * TICKS_PER_HOUR))
#define COLONIST_LONELY_BELOW 0.25f
#define COLONIST_SEEK_COMPANY_FROM 0.9f
#define COLONIST_COMPANY_NEAR 3 /* CONVERSATION_RANGE: close enough to chat. */
#define COLONIST_SEEK_ACQUAINTANCE_WEIGHT 1.0f
#define COLONIST_SEEK_FRIEND_WEIGHT 2.5f
#define COLONIST_SEEK_CLOSE_FRIEND_WEIGHT 4.0f
/* Nearness: a settler this many tiles away counts half as much. */
#define COLONIST_SEEK_HALF_DISTANCE 10.0f
/* A settler looks for company this far around (tiles, straight line), and
 * walks at most this travel cost to it. */
#define COLONIST_COMPANY_SEARCH_RADIUS 40
#define COLONIST_COMPANY_MAX_COST 80.0f
/* Faces by mood (see colonist_expression()). */
#define COLONIST_ANGRY_BELOW 20.0f
#define COLONIST_SAD_BELOW 35.0f
#define COLONIST_SMILING_FROM 65.0f

/* A hungry colonist keeps eating until food reaches this level. */
#define COLONIST_SATED_FOOD 0.8f
/* At night, colonists go to sleep below this rest level. */
#define COLONIST_NIGHT_SLEEP_REST 0.9f
/* Before going to sleep, colonists eat and drink when below this level, so
 * that they do not wake up starving or parched. */
#define COLONIST_BEDTIME_NEED 0.5f
#define COLONIST_EAT_TICKS 500
#define COLONIST_FOOD_SEARCH_MAX_COST 120.0f
/* Food lying within this cost that rots away in less than
 * COLONIST_SPOILING_FOOD_HOURS (at its tile now) is eaten first, before
 * nearer food that keeps. */
#define COLONIST_SPOILING_FOOD_HOURS 24.0f
#define COLONIST_SPOILING_FOOD_MAX_COST 60.0f
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
/* Such early retries are spread over this many ticks, each settler on its
 * own: those that failed together (all idle when the last jobs went) would
 * otherwise all search again on the same tick when something changes. */
#define COLONIST_SEARCH_STAGGER_TICKS 8
#define COLONIST_WANDER_RADIUS 6
/* A settler ordered to a spot stays this long once there before it takes up
 * its life again (5 s): long enough to look around and give the next order. */
#define COLONIST_MOVE_HOLD_TICKS 300
/* Drawing a weapon into the hands (docs/CARRYING.md §6.4, tuning): from
 * under an arm or the shoulders, slung, or taken up from the ground. */
#define COLONIST_DRAW_TICKS 30
#define COLONIST_DRAW_SLUNG_TICKS 120
#define COLONIST_DRAW_GROUND_TICKS 60
/* No limit to how far an order may send a settler. */
#define COLONIST_MOVE_MAX_COST 1.0e9f
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
    TASK_EAT,   /* Walking to food, then eating. */
    TASK_DRINK, /* Walking to a drinking spot, then drinking. */
    TASK_SLEEP,
    TASK_WORK,  /* Walking next to a designated tile, then working on it. */
    TASK_HAUL,  /* Walking to an item stack, picking it up, carrying it to storage. */
    TASK_CRAFT, /* Fetching tools and input, then working through a bill at a station. */
    /* Bringing materials to a construction site (a structure's or a
     * workstation's), then building it from a neighboring tile. */
    TASK_CONSTRUCT,
    TASK_SUPPLY, /* Bringing materials to a construction site (hauling work). */
    TASK_CLEAN,  /* Walking to a dirty tile, then cleaning it. */
    /* Following a master (Colonist.mentor) and helping with their job. */
    TASK_ASSIST,
    TASK_REFUEL, /* Bringing fuel to a hearth (hauling work). */
    TASK_LIGHT,  /* Lighting a hearth: embers from a burning fire, or friction. */
    TASK_STORY,  /* Telling about a knowledge beside a burning hearth. */
    TASK_LISTEN, /* Sitting near a storyteller (Colonist.mentor) and listening. */
    /* Walking where the player ordered (`target`), then standing there a while
     * (COLONIST_MOVE_HOLD_TICKS). Only critical needs and breaks cut it short. */
    TASK_MOVE,
    /* Struck by an animal (Colonist.enemy, docs/COMBAT.md): fighting it (walks
     * up to it and strikes), or running from it when hurt past its nerve. */
    TASK_FIGHT,
    TASK_FLEE,
    /* Lying where it fell: unconscious, or unable to walk (core/health.h).
     * Gets up by itself once able to. */
    TASK_DOWN,
    TASK_COUNT
} ColonistTask;

typedef enum NeedType { NEED_FOOD, NEED_WATER, NEED_REST, NEED_COUNT } NeedType;

/* Things a colonist searches the map for; failed searches back off per kind. */
typedef enum SearchKind {
    SEARCH_FOOD,
    SEARCH_WATER,
    SEARCH_WORK,
    SEARCH_HAUL,
    SEARCH_STATION,      /* Crafting jobs at workstations. */
    SEARCH_GATHER,       /* Gathering zones. */
    SEARCH_CONSTRUCTION, /* Construction sites. */
    SEARCH_CLEAN,        /* Dirty tiles. */
    SEARCH_SUPPLY,       /* Construction sites missing materials, for haulers. */
    SEARCH_ASSIST,       /* Masters to assist, or better work while assisting. */
    SEARCH_REFUEL,       /* Hearths wanting fuel. */
    SEARCH_LIGHT,        /* Hearths to light. */
    SEARCH_STORY,        /* A hearth to tell stories at, or a story to listen to. */
    SEARCH_WARM_SPOT,    /* Asleep too cold or hot: a better spot beside a fire. */
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
    LIGHT_FETCH_FUEL,   /* Walking to fuel (`fetch`) for a hearth with none in it. */
    LIGHT_BRING_FUEL,   /* Walking to the hearth with the fuel to put in it. */
    LIGHT_FETCH_LAY,    /* Walking to dry twigs or a branch (`fetch`) to lay in the hearth. */
    LIGHT_BRING_LAY,    /* Walking to the hearth with it to lay it. */
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

typedef enum NeedLevel { NEED_LEVEL_OK, NEED_LEVEL_LOW, NEED_LEVEL_CRITICAL } NeedLevel;

/* The state a need at this value puts a settler in (hungry, starving,
 * thirsty, parched, tired, exhausted), or THOUGHT_COUNT for none. */
ThoughtType need_thought(NeedType need, float value);

typedef struct Colonist {
    char name[COLONIST_NAME_SIZE]; /* Proper noun: never translated. */
    int age;
    int x; /* Current tile. */
    int y;
    int z;
    Path path;
    int path_index;      /* Next step of `path` to walk to. */
    float move_progress; /* 0..1 from the current tile to the next step. */
    /* Ticks waited one step short of a tile someone else holds (core/occupancy.h). */
    uint8_t stop_wait;
    ColonistTask task;
    /* Food tile (TASK_EAT), drinking spot (TASK_DRINK), designated tile
     * (TASK_WORK) or dirty tile (TASK_CLEAN). */
    TilePos target;
    /* A move order given between two tiles: carried out on the next tile
     * (colonist_order_move()). */
    bool move_pending;
    TilePos move_goal;
    int timer; /* Idle countdown (TASK_IDLE), eating or drinking progress. */
    /* TASK_WORK: the DesignationType being worked on, or the GatherJob when
     * `gathering` (in a gathering zone rather than on a designation). */
    uint8_t work_type;
    bool gathering;
    int search_cooldown[SEARCH_COUNT]; /* Ticks before searching again. */
    int search_failures[SEARCH_COUNT]; /* Consecutive failed searches. */
    int search_quiet[SEARCH_COUNT];    /* Ticks before a change may cut the delay short. */
    /* Ticks lived (wrapping): when a change may cut a delay short, see
     * COLONIST_SEARCH_STAGGER_TICKS. Not the clock, which tests may hold still. */
    uint32_t search_phase;
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
    ItemStack carried;        /* Items in hand (TASK_HAUL), dropped if the task is abandoned. */
    TilePos haul_destination; /* Storage tile the carried items go to (TASK_HAUL). */
    /* TASK_HAUL: false while collecting stacks (`target`), true once heading
     * for `haul_destination`. */
    bool haul_delivering;
    /* TASK_HAUL: on a detour to `fetch` for a container first (a basket for
     * berries, docs/CONTAINERS.md §7.2), before the stacks. */
    bool haul_fetching;
    /* TASK_HAUL: carrying out an item move ordered by the player (core/item_move.h)
     * rather than hauling to a stockpile: `target` is the source,
     * `haul_destination` the tile chosen. */
    bool haul_forced;
    /* TASK_HAUL, forced: the one item ordered moved, ITEM_ID_NONE when the
     * order is about every item of `target`. */
    ItemId haul_item;
    /* TASK_HAUL: the type of the items collected (ItemType; a tile may hold
     * several). */
    uint8_t haul_type;
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
    /* The sleep under way (TASK_SLEEP): ticks asleep, and of them, ticks it
     * felt too cold or hot to sleep well, or was soaked. */
    int sleep_ticks;
    int sleep_cold_ticks;
    int sleep_hot_ticks;
    int sleep_wet_ticks;
    /* Tools and other kept items (count 1 each; one free item per type, any
     * number of locked ones; empty slots have type ITEM_NONE). */
    ItemStack tools[COLONIST_TOOL_MAX];
    /* Ticks of work done with each tool since it last lost a hit point. */
    uint16_t tool_wear[COLONIST_TOOL_MAX];
    /* What the player asked of each tool or other item it keeps (CarryLock,
     * docs/CARRYING.md §7.1): a locked one is never set down by the
     * colonist; one kept in hand stays in a hand, and the colonist then only
     * hauls (the other work needs both hands). */
    uint8_t tool_locks[COLONIST_TOOL_MAX];
    /* TASK_MOVE ordered to carry an item (colonist_order_carry()): taken on
     * arrival and kept, locked; ITEM_ID_NONE otherwise. */
    ItemId take_item;
    /* The slot in `tools` of its primary weapon (docs/COMBAT.md §7), -1 for
     * none; see colonist_primary_weapon(). */
    int8_t primary_weapon;
    /* A weapon in its hands, ready to strike (docs/COMBAT.md §7): while
     * `wielding` (mobilized, or defending itself), `wielded` is its slot in
     * `tools`, -1 for its fists; see colonist_wielded(). A state apart from
     * the locks: it goes back where it fits best when it ends. */
    bool wielding;
    int8_t wielded;
    /* While its action needs the hands (docs/CARRYING.md §6.3), worked out
     * on each of its ticks: how many (0: none, the rest is then 0), the
     * tools in use and the kept items set aside, a bit per slot of `tools`;
     * the load is set aside too. Set aside, an item lies at its feet and is
     * still its own: picked up again once the action is over. */
    uint8_t work_hands;
    uint8_t work_tools;
    uint8_t aside;
    int tools_broken;       /* Tools worn out so far (for the player's information). */
    int strength;           /* COLONIST_STRENGTH_MIN..COLONIST_STRENGTH_MAX. */
    float max_carry_weight; /* Kilograms carried without slowing down (from strength). */
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
    float company; /* 1 (plenty) .. 0 (lonely): see COLONIST_COMPANY_DECAY_PER_TICK. */
    /* The company it feels around it (a friend, a rival: mood states), set
     * by social_company_update() (core/social.h). */
    ThoughtMask social_states;
    /* The others by slot (bit `slot`), from the relationships last noted
     * (social_company_update()): friends, close friends, rivals or enemies.
     * Whom it seeks out or keeps away from. */
    uint64_t friend_slots;
    uint64_t close_friend_slots;
    uint64_t rival_slots;
    /* What it lived through that makes a story to tell (core/experience.h). */
    Experiences experiences;
    float wetness; /* 0 (dry) .. 1 (wet through). */
    Mood mood;
    Attributes attributes; /* Set with colonist_set_attributes(). */
    Health health;         /* Its factors follow the attributes. */
    Journal journal;       /* What happened to it (for the eye only: not hashed). */
    bool collapse_noticed; /* The player was told it collapsed (not hashed). */
    /* The animal that struck it and it fights or flees (TASK_FIGHT, TASK_FLEE);
     * NONE when nobody. `attack_cooldown`: ticks before its next blow. */
    Combatant enemy;
    int attack_cooldown;
    /* Mobilized by the player (docs/COMBAT.md §8, colonist_set_drafted()):
     * it ignores its needs and its work, and only follows orders.
     * `draft_end`: why it was last demobilized by itself (DraftEnd), for the
     * player's notice; the game reads it and sets it back to NONE (not
     * hashed). */
    bool drafted;
    uint8_t draft_end;
    /* Its allowed area (core/allowed_area.h): an index in Game.areas, 0 (the
     * default area) at first. */
    uint8_t area;
    /* The animal the player ordered it to attack while mobilized
     * (colonist_order_attack()): it goes after it; NONE otherwise. */
    Combatant attack_target;
    ColonistAppearance appearance; /* Cosmetic only: never affects the simulation. */
} Colonist;

/* What a colonist needs to see of the world to make decisions. */
typedef struct ColonistContext {
    Map *map;
    /* What a settler plans on (docs/VISION.md): the known world under the fog
     * (a tile nobody has seen is open space, a tile out of view is as it was
     * last seen), the real map and items without. Paths and every search
     * read these; what a settler does (picking up, mining...) is done on the
     * real ones, and finds out there what is no longer so. */
    const Map *plan_map;
    const Items *plan_items;
    /* Who sees what, under the fog (NULL without: everything is in view). */
    const Vision *vision;
    Designations *designations;
    Dirt *dirt;
    GatherZones *gather_zones;
    Items *items;
    Stockpiles *stockpiles;
    Workstations *workstations;
    Constructions *constructions;
    Beds *beds;
    Reservations *reservations;
    ItemMoves *item_moves; /* Stacks the player ordered carried somewhere. */
    Pathfinder *pathfinder;
    /* Connectivity regions (may be out of date, never wrong: see core/region.h). */
    const Regions *regions;
    const long *stock; /* Game.stock: items in stock per type. */
    Rng *rng;
    uint64_t tick;
    bool night;        /* colonist_is_night_for(tick, worker_id). */
    uint8_t worker_id; /* Reservation owner for this colonist (slot + 1), never 0. */
    EntityId self;     /* Its own handle (for the blows it deals). */
    /* Every colonist (Game.colonists) and who is alive, to find a master to
     * assist; NULL: nobody assists anyone. */
    Colonist *colonists;
    const Pool *colonist_pool;
    /* Knowledges the player wants taught at fireside stories (Game.teach);
     * NULL: none. */
    const bool *teach;
    /* Temperature per tile (Game.temperatures); NULL: always mild. */
    const Temperatures *temperatures;
    /* How fast settlers out under the open sky get wet now, per hour
     * (weather_wetting(), 0 when nothing falls). */
    float wetting;
    /* The water in the ground (Game.soil): twigs picked from wet ground come
     * wet. NULL: only the rain wets them. */
    const Soil *soil;
    /* Debug cheats (GameDebug): crafts need no input, no tool is needed. */
    bool free_crafts;
    bool no_tools;
    /* The animals, to fight them, and where blows are queued (core/combat.h);
     * NULL: no fights. */
    const Animals *animals;
    CombatQueue *combat;
    /* Who stops where (core/occupancy.h): a settler never stops on a tile
     * someone else holds. NULL: anyone stops anywhere. */
    Occupancy *occupancy;
    /* The allowed areas (core/allowed_area.h): its searches and walks keep to
     * its own (Colonist.area) on its turn. NULL: it goes anywhere. */
    const AllowedAreas *areas;
} ColonistContext;

/* Fighting (docs/COMBAT.md): an enemy further than this many tiles is forgotten,
 * a settler that flees runs to a tile this far from it, within this travel
 * cost, and one that fights walks up to it within this travel cost. */
#define COLONIST_ENEMY_FORGET_DISTANCE 24
#define COLONIST_FLEE_DISTANCE 18
#define COLONIST_FLEE_MAX_COST 70.0f
#define COLONIST_CHASE_MAX_COST 50.0f

/* How far a settler out of its allowed area with nothing to do looks for the
 * way back into it (travel cost). */
#define COLONIST_AREA_RETURN_MAX_COST 600.0f
/* Making way (core/occupancy.h): the nearest free tile within this travel cost. */
#define COLONIST_MAKE_WAY_MAX_COST 12.0f
/* Power of a blow by strength: ×(COLONIST_POWER_BASE + COLONIST_POWER_PER_STRENGTH
 * × strength), and the skill with a spear: ×COLONIST_SPEAR_SKILL_MIN at level 0
 * to ×COLONIST_SPEAR_SKILL_MAX at 100. */
#define COLONIST_POWER_BASE 0.6f
#define COLONIST_POWER_PER_STRENGTH 0.04f
#define COLONIST_SPEAR_SKILL_MIN 0.8f
#define COLONIST_SPEAR_SKILL_MAX 1.3f

/* Deprivation (docs/HEALTH.md §10): every COLONIST_STRAIN_TICKS the settler's
 * food and water at 0, and the temperature it feels, press on its body. It
 * freezes below COLONIST_HYPOTHERMIA_CELSIUS (lower with cold tolerance),
 * full pressure COLONIST_COLD_SPAN degrees below; it overheats above
 * COLONIST_HEATSTROKE_CELSIUS (higher with heat tolerance), full pressure
 * COLONIST_HEAT_SPAN degrees above. */
#define COLONIST_STRAIN_TICKS 25
#define COLONIST_HYPOTHERMIA_CELSIUS 0.0f
#define COLONIST_COLD_SPAN 15.0f
#define COLONIST_HEATSTROKE_CELSIUS 36.0f
#define COLONIST_HEAT_SPAN 10.0f

/* The pressure (0..1) a felt temperature puts on the settler's body. */
float colonist_cold_pressure(const Colonist *colonist, float felt_celsius);

/* The felt temperatures a settler finds pleasant (MOOD_COLD_CELSIUS to
 * MOOD_HOT_CELSIUS: no thought of cold or heat) and bears (above
 * COLONIST_HYPOTHERMIA_CELSIUS, below COLONIST_HEATSTROKE_CELSIUS), moved by
 * its cold and heat tolerance. */
TemperatureRanges colonist_temperature_ranges(const Colonist *colonist);

/* The temperature a settler feels where it stands: the air with the warmth
 * of fires (hearths_felt_temperature()), COLONIST_WET_CHILL colder soaked
 * through. */
float colonist_felt_temperature(const Colonist *colonist, const Workstations *workstations,
                                const Map *map, const Temperatures *temperatures);
float colonist_heat_pressure(const Colonist *colonist, float felt_celsius);

/* A settler was struck (or attacked) by `attacker`: it will fight or flee at its
 * next turn. Returns true when it had no enemy before. */
bool colonist_provoke(Colonist *colonist, Combatant attacker);

/* The attack a settler fights with: its weapon's (colonist_weapon()), else
 * its fists; `skill` and `power` are the multipliers its blows carry (the
 * spear hunting knowledge for spears; power follows its strength and the use
 * of its arms). ATTACK_NONE when its arms are of no use (both lost or broken,
 * or knocked out): everything it strikes with is held in the hands. */
AttackType colonist_attack(const Colonist *colonist, float *skill, float *power);

void colonist_init(Colonist *colonist, const char *name, int age, int x, int y, int z);
void colonist_free(Colonist *colonist);

/* Whether it is time to be in bed (22:00 to 06:00). */
bool colonist_is_night(uint64_t tick);

/* Settlers do not all go to bed, nor get up, on the same tick (each would
 * look for food or work at once): each one's night comes later by up to
 * this, after its slot. */
#define COLONIST_BEDTIME_SPREAD_TICKS (TICKS_PER_HOUR / 4)

/* Whether it is night for the settler of `worker_id` (slot + 1). */
bool colonist_is_night_for(uint64_t tick, uint8_t worker_id);

void colonist_tick(Colonist *colonist, const ColonistContext *context);

/* Its allowed area (or the area itself) changed: it looks for work, food...
 * again at once, and gives up what it was doing outside the area now (not
 * the player's orders, a fight, sleep). */
void colonist_area_changed(Colonist *colonist, const ColonistContext *context);

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
void colonist_learn_by_doing(Colonist *colonist, KnowledgeType knowledge, int ticks, uint64_t tick);

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

/* The same for the order on a given tile: some need more there (a plant easily
 * mistaken for a poisonous one, designation_min_level_at()). */
bool colonist_can_do_at(const Colonist *colonist, const Map *map, int x, int y, int z,
                        DesignationType type);

/* Sets the attributes, and the character of the mood that follows from them
 * (outlook, temperament). */
void colonist_set_attributes(Colonist *colonist, const Attributes *attributes);

/* The coldest and warmest felt temperatures the colonist sleeps well at:
 * COLONIST_COLD_SLEEP_BELOW and COLONIST_HOT_SLEEP_ABOVE, moved by its cold
 * and heat tolerance. The same shifts apply to when it feels cold or hot
 * (MOOD_COLD_CELSIUS, MOOD_HOT_CELSIUS). */
float colonist_cold_limit(const Colonist *colonist);
float colonist_heat_limit(const Colonist *colonist);

/* Rest gain multiplier of sleeping at a felt temperature: 1 between the
 * colonist's cold and heat limits, less beyond. */
float colonist_sleep_temperature_factor(const Colonist *colonist, float celsius);

/* Whether the colonist knows enough to do this gathering job. */
bool colonist_can_gather(const Colonist *colonist, GatherJob job);

/* Work done per tick on something of `knowledge`: COLONIST_WORK_SPEED scaled
 * by knowledge_work_speed() (unscaled for KNOWLEDGE_NONE), by the mood
 * (mood_work_factor()), by the colonist's work pace and by the use of its
 * hands (core/health.h). */
float colonist_work_speed(const Colonist *colonist, KnowledgeType knowledge);

/* The level what the colonist makes comes from (quality and condition of
 * products, quality of constructions): its level in the knowledge, more or
 * less with its mood (mood_level_offset()), within the valid levels. */
double colonist_work_level(const Colonist *colonist, KnowledgeType knowledge);

/* Whether the colonist is having a break (core/mood.h). */
bool colonist_in_break(const Colonist *colonist);

/* Whether the colonist is down (TASK_DOWN): collapsed, unconscious or unable
 * to walk. */
bool colonist_is_down(const Colonist *colonist);

/* Walking speed multiplier from the colonist's body: its walking attribute
 * and the walking its health leaves (core/health.h). */
float colonist_walk_factor(const Colonist *colonist);

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

/* Whether the player's choice of workers for the bill (anyone, a level
 * range, one settler) lets this colonist (whose handle is `id`) take it (see
 * bill_accepts_worker()). */
bool colonist_fits_bill(const Colonist *colonist, EntityId id, const Bill *bill);

/* Index in `tools` of the best carried tool among `types` (quality, then hit
 * points), or -1 when it carries none of them. */
int colonist_tool_index(const Colonist *colonist, ItemTypeMask types);

/* Takes one item as a tool (any item a recipe uses as one, e.g. a cobble as
 * a hammer), free. Fails when a free one of the same type is already
 * carried, or every slot is used. */
bool colonist_add_tool(Colonist *colonist, ItemStack tool);

/* Takes one item as a tool in place of the free carried one of the same
 * type (locked ones stay), which is written to `replaced` (an empty stack
 * when there was none: the tool is then simply added). Fails like
 * colonist_add_tool() otherwise. */
bool colonist_swap_tool(Colonist *colonist, ItemStack tool, ItemStack *replaced);

/* Speed factor the colonist works at for a kind of work: its best carried
 * tool among `allowed` (bare hands when none does better), see core/tool.h. */
float colonist_tool_factor(const Colonist *colonist, ToolUse use, ItemTypeMask allowed);

/*
 * Kept items: the tools, and any item the player made it carry or locked
 * (docs/CARRYING.md §7), in `tools`, one item each: one free item per type
 * at most, any number of locked ones.
 *
 * colonist_set_lock() locks the kept item at `index` (CarryLock). Fails on
 * an empty slot, a lock out of range, or CARRY_LOCK_IN_HAND when no hand can
 * hold it beside the others kept in hand.
 */
bool colonist_set_lock(Colonist *colonist, int index, CarryLock lock);

/* Hands taken by the items kept in hand (0 to 2): while any, the colonist
 * only hauls. */
int colonist_hands_locked(const Colonist *colonist);

/* Whether the colonist can take `count` items of `type` to keep with that
 * lock (not CARRY_LOCK_NONE): a slot and a place (a hand, kept in hand) for
 * each beside the items it keeps locked (the free ones would be set down). */
bool colonist_can_keep(const Colonist *colonist, ItemType type, int count, CarryLock lock);

/* Sets down where it stands the kept item at `index`, its lock cleared, and
 * forbidden so that nobody picks it up again at once. Fails on an empty
 * slot. */
bool colonist_put_down(Colonist *colonist, Items *items, const Map *map, int index);

/* The player locks the item at `index` of the load (newest first,
 * items_load_first(), docs/CARRYING.md §7.1): it becomes a kept item with
 * that lock. Returns its slot in `tools`, -1 when it cannot be kept so
 * (colonist_can_keep()), there is no such item, or when down. */
int colonist_keep_load_item(Colonist *colonist, const ColonistContext *context, int index,
                            CarryLock lock);

/* The player makes it drop the item at `index` of the load: put down where
 * it stands, forbidden. False when there is no such item. */
bool colonist_drop_load_item(Colonist *colonist, const ColonistContext *context, int index);

/* Both: once the load is empty, what it was carried for is given up. */

/* The player's Carry order (docs/CARRYING.md §7.2): walks to the item lying
 * on `tile` (as colonist_order_move()) and, there or next to it, takes it
 * and keeps it, locked on the colonist (a free kept item of its type is set
 * down). Refused (false) for any reason of colonist_carry_refusal(). */
bool colonist_order_carry(Colonist *colonist, const ColonistContext *context, ItemId item,
                          TilePos tile);

/* Why a Carry order would be refused, for the player (docs/CARRYING.md §7.2). */
typedef enum CarryRefusal {
    CARRY_REFUSAL_NONE,
    CARRY_REFUSAL_DOWN,        /* The settler cannot move (or is gone). */
    CARRY_REFUSAL_GONE,        /* No such item any more. */
    CARRY_REFUSAL_HELD,        /* Another settler holds it (handing over comes later). */
    CARRY_REFUSAL_NO_ROOM,     /* No slot or no place for it beside its locked items. */
    CARRY_REFUSAL_UNREACHABLE, /* The settler cannot get there. */
    CARRY_REFUSAL_COUNT
} CarryRefusal;

/* Why the colonist could not carry `item` lying on `tile`: CARRY_REFUSAL_NONE
 * when it can. `plan_map` is the map as the settlers know it (paths), `map`
 * the real one (connectivity regions, as colonist_order_move()). */
CarryRefusal colonist_carry_refusal(const Colonist *colonist, const Items *items, const Map *map,
                                    const Map *plan_map, const Regions *regions, ItemId item,
                                    TilePos tile);

/* The translation key of a refusal's reason, NULL for CARRY_REFUSAL_NONE. */
const char *carry_refusal_key(CarryRefusal refusal);

/*
 * Weapons (docs/COMBAT.md §7). colonist_primary_weapon() is the slot in
 * `tools` of its primary weapon, -1 for none (or when that item is gone).
 * colonist_set_primary_weapon() marks the weapon at `index` primary (and
 * locks it kept on the settler) or takes the mark away (the lock stays);
 * fails on an empty slot or what is no weapon. Freeing its lock takes the
 * mark away too.
 */
int colonist_primary_weapon(const Colonist *colonist);
bool colonist_set_primary_weapon(Colonist *colonist, int index, bool primary);

/* The weapon it fights with, a slot in `tools`: its primary weapon, else the
 * best it carries (weapon_better()), among those its arms can wield (both
 * for one taking two hands); -1 for none: its fists. */
int colonist_weapon(const Colonist *colonist);

/*
 * Wielding (docs/COMBAT.md §7). colonist_wielded() is the slot of the weapon
 * in its hands, -1 when none (its fists, or not wielding; or what its arms
 * can no longer wield). colonist_can_wield() tells whether it could wield
 * the weapon in `slot` (-1: its fists, always): a weapon its arms can hold,
 * and hands not all locked by other items. A mobilized colonist wields at
 * once colonist_weapon(), and one defending itself too; it waits the
 * drawing time before its first blow.
 *
 * colonist_wield() makes a mobilized colonist wield the weapon in `slot`
 * (-1: its fists); colonist_wield_item() takes up a weapon lying within a
 * tile of it (it becomes a kept item, locked) and wields it;
 * colonist_can_wield_item() tells whether it could. The load is put down,
 * the free kept items that no longer fit are set down.
 */
int colonist_wielded(const Colonist *colonist);
bool colonist_can_wield(const Colonist *colonist, int slot);
bool colonist_wield(Colonist *colonist, const ColonistContext *context, int slot);
bool colonist_can_wield_item(const Colonist *colonist, const Items *items, ItemId id);
bool colonist_wield_item(Colonist *colonist, const ColonistContext *context, ItemId id);

/*
 * Mobilization (docs/COMBAT.md §8). colonist_can_draft(): not dead, down, in
 * a mental break or fleeing. colonist_set_drafted() mobilizes it (what it
 * did is given up: asleep, it wakes up) or demobilizes it (it takes up its
 * life again); false when it cannot be mobilized. A mobilized settler is
 * demobilized by itself when it breaks, flees (it cannot fight any more) or
 * is knocked out: `draft_end` says why.
 */
typedef enum DraftEnd {
    DRAFT_END_NONE,
    DRAFT_END_BREAK,
    DRAFT_END_FLED,
    DRAFT_END_DOWN,
    DRAFT_END_COUNT
} DraftEnd;
bool colonist_can_draft(const Colonist *colonist);
bool colonist_set_drafted(Colonist *colonist, const ColonistContext *context, bool drafted);

/* The player's Attack order (docs/COMBAT.md §8): a mobilized colonist goes
 * after the animal and strikes it until it is dead or gone, or another
 * order comes. False when not mobilized, down, or the target is no living
 * animal. Without an order, a mobilized colonist stays where it is and
 * strikes the animals attacking settlers that come in its reach, without
 * going after them. */
bool colonist_order_attack(Colonist *colonist, const ColonistContext *context, Combatant target);

/* Drops every carried tool where the colonist stands (e.g. before removal). */
void colonist_drop_tools(Colonist *colonist, Items *items, const Map *map);

/* Gives up the current task, releasing any reservation (e.g. before removal). */
void colonist_abandon_task(Colonist *colonist, const ColonistContext *context);

/*
 * Sends the colonist to a tile, whatever it was doing (a player's order): the
 * job is given up, anything carried put down, a sleeper woken. Refused (false,
 * nothing changed) when the colonist is down, or the tile cannot be stood on,
 * or reached on what the colonist knows of the map (context->plan_map).
 * Between two tiles the order waits for the next one.
 */
bool colonist_order_move(Colonist *colonist, const ColonistContext *context, TilePos goal);

bool colonist_is_moving(const Colonist *colonist);

/* The chance that an idle settler about to wander seeks company instead,
 * at this level of company (see COLONIST_SEEK_COMPANY_FROM). */
float colonist_seek_company_chance(float company);

/* Asleep in place, or unconscious. */
bool colonist_is_asleep(const Colonist *colonist);

/* The way the colonist faces: where its step leads while it walks, south
 * (toward the viewer) when it stands, works or sleeps. */
Heading colonist_heading(const Colonist *colonist);

/* Interpolated position of the colonist's center, in world (tile) coordinates. */
void colonist_position(const Colonist *colonist, float *x, float *y);

/* Kilograms carried without slowing down at a strength (clamped to the valid range). */
float colonist_carry_weight_for_strength(int strength);

/* Random strength for a new colonist of this age. */
int colonist_random_strength(Rng *rng, int age);

/* Sets the strength and the weight limit that follows from it. Fails on a
 * value out of range. */
bool colonist_set_strength(Colonist *colonist, int strength);

/* Kilograms the colonist carries without slowing down: the limit of its
 * strength, times its vigor (core/health.h). The arms count place by place
 * (colonist_carry_body()). */
float colonist_max_carry_weight(const Colonist *colonist);

/* Kilograms the colonist is carrying: the load in hand and the tools. */
float colonist_carried_weight(const Colonist *colonist);

/* The colonist's body for carrying (core/carry.h): the weight it carries
 * without slowing down and each arm's efficiency. */
CarryBody colonist_carry_body(const Colonist *colonist);

/* How many items of a type the colonist can carry in all, in the places its
 * tools leave (core/carry.h, docs/CARRYING.md): at least 1 (a single item
 * over every place is carried on the shoulders or dragged), 0 for no type. */
int colonist_carry_capacity(const Colonist *colonist, ItemType type);

/* Where the colonist carries its tools and its load. The kept items of the
 * arrangement are its filled slots of `tools`, the weapon it wields first:
 * `slots` (may be NULL; COLONIST_TOOL_MAX entries) gets the slot of each,
 * -1 after the last. While its action needs the hands (`work_hands`), the
 * arrangement is the work's (carry_arrange_working()): no load, the tools
 * in use in hand, the hands of the work kept empty (`working`), the items
 * set aside left out. */
void colonist_carry_arrangement(const Colonist *colonist, CarryArrangement *out, int *slots);

/* Hands an action takes (docs/CARRYING.md §6.3): both for blows, cutting and
 * gathering, one for eating, none otherwise. */
int colonist_action_hands(ColonistAction action);

/*
 * Before picking up a load of `type` (ITEM_NONE: none) of `wanted` items in
 * all (what it already holds included): sets down where it stands the tools
 * that fit in no place, then, while the load does not fit, the tool whose
 * leaving lets the most of it in (docs/CARRYING.md §6.4). Never the tools of
 * `keep` (those of its job). Returns how many tools were set down.
 */
int colonist_make_room(Colonist *colonist, Items *items, const Map *map, ItemType type, int wanted,
                       ItemTypeMask keep);

/* Walking speed multiplier from the load: 1 up to the weight limit, lower above. */
float colonist_load_speed_factor(const Colonist *colonist);

NeedLevel need_level(float value);

/* Hands its action under way takes (colonist_action_hands()). */
int colonist_hands_needed(const Colonist *colonist);

/* The action under way and, when it needs one, the tool in hand (written to
 * `tool` when not NULL; ITEM_NONE without a tool). Only a colonist standing at
 * its job and actually working on it acts; the tool is the one of the first
 * tool kind of the recipe being crafted. */
ColonistAction colonist_action(const Colonist *colonist, ItemType *tool);

/* From the mood: angry during a break or below COLONIST_ANGRY_BELOW, sad
 * below COLONIST_SAD_BELOW, smiling from COLONIST_SMILING_FROM, neutral
 * otherwise. */
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

/* Saves or loads (docs/SAVE.md) a settler, all of it (its path allocated
 * when reading). */
void colonist_serialize(Serializer *s, Colonist *colonist);

#endif /* COLONY_CORE_COLONIST_H */
