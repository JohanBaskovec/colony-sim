/*
 * Colonists: needs, simple decision making and movement along paths.
 *
 * Each tick a colonist updates its needs, decides what to do when it stands
 * on a tile center (drink when thirsty, eat when hungry, sleep when tired or
 * at night, otherwise work on designations, or wander), then acts (walk one
 * tick along its path, eat, work, sleep...).
 * Colonists are stored in the Game (see core/game.h); this module only works
 * on one colonist at a time with explicit dependencies.
 */
#ifndef COLONY_CORE_COLONIST_H
#define COLONY_CORE_COLONIST_H

#include "core/designation.h"
#include "core/game_clock.h"
#include "core/map.h"
#include "core/pathfinding.h"
#include "core/region.h"
#include "core/reservation.h"
#include "core/rng.h"
#include "core/skill.h"
#include "core/stockpile.h"
#include "core/work.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COLONIST_MAX 64
#define COLONIST_NAME_SIZE 32

/* Walking speed on terrain of cost 1, in tiles per second at normal game speed. */
#define COLONIST_WALK_SPEED 3.5f

/*
 * Carrying, by hand (no container yet): a colonist picks up at most
 * COLONIST_CARRY_BULK of bulk and COLONIST_CARRY_WEIGHT kilograms. A single
 * item over either limit is still taken (dragged). Above the weight limit,
 * walking slows down in proportion (limit / load), never below
 * COLONIST_MIN_LOAD_SPEED.
 */
#define COLONIST_CARRY_WEIGHT 40.0f
#define COLONIST_CARRY_BULK 60.0f
#define COLONIST_MIN_LOAD_SPEED 0.1f

/* Needs go from 1 (fully satisfied) to 0. */
#define NEED_FOOD_DECAY_PER_TICK (1.0f / (36.0f * TICKS_PER_HOUR))
#define NEED_WATER_DECAY_PER_TICK (1.0f / (24.0f * TICKS_PER_HOUR))
#define NEED_REST_DECAY_PER_TICK (1.0f / (20.0f * TICKS_PER_HOUR))
#define NEED_REST_GAIN_PER_TICK (1.0f / (8.0f * TICKS_PER_HOUR))
#define NEED_LOW_THRESHOLD 0.3f
#define NEED_CRITICAL_THRESHOLD 0.1f

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
/* Work done per tick on a designation at the standard skill level
 * (see colonist_work_speed()). */
#define COLONIST_WORK_SPEED 1.0f
/* Experience gained per tick spent working on a designation. */
#define COLONIST_WORK_XP_PER_TICK 1.0f
#define COLONIST_WORK_SEARCH_MAX_COST 250.0f
/* After picking up a stack, a hauler with room left also collects matching
 * stacks within this travel cost before delivering. */
#define COLONIST_HAUL_COLLECT_MAX_COST 12.0f
/* Delay before searching again after a failed food, water or work search, doubled
 * after each consecutive failure up to 2^COLONIST_SEARCH_MAX_BACKOFF times. A
 * failed search floods everything within its cost limit, so it must stay rare. */
#define COLONIST_SEARCH_RETRY_TICKS (TICKS_PER_HOUR / 2)
#define COLONIST_SEARCH_MAX_BACKOFF 3
#define COLONIST_WANDER_RADIUS 6
#define COLONIST_IDLE_MIN_TICKS 120
#define COLONIST_IDLE_MAX_TICKS 360

typedef enum ColonistTask {
    TASK_IDLE,
    TASK_WANDER,
    TASK_EAT, /* Walking to food, then eating. */
    TASK_DRINK, /* Walking to a drinking spot, then drinking. */
    TASK_SLEEP,
    TASK_WORK, /* Walking next to a designated tile, then working on it. */
    TASK_HAUL, /* Walking to an item stack, picking it up, carrying it to storage. */
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
    SEARCH_COUNT
} SearchKind;

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
    /* Food tile (TASK_EAT), drinking spot (TASK_DRINK) or designated tile (TASK_WORK). */
    TilePos target;
    int timer; /* Idle countdown (TASK_IDLE), eating or drinking progress. */
    uint8_t work_type; /* DesignationType being worked on (TASK_WORK). */
    int search_cooldown[SEARCH_COUNT]; /* Ticks before searching again. */
    int search_failures[SEARCH_COUNT]; /* Consecutive failed searches. */
    /* Designations revision + reservation releases at the last failed work search. */
    uint32_t work_search_stamp;
    /* Stockpile revision + item additions + releases at the last failed haul search. */
    uint32_t haul_search_stamp;
    ItemStack carried; /* Items in hand (TASK_HAUL), dropped if the task is abandoned. */
    TilePos haul_destination; /* Storage tile the carried items go to (TASK_HAUL). */
    /* TASK_HAUL: false while collecting stacks (`target`), true once heading
     * for `haul_destination`. */
    bool haul_delivering;
    float max_carry_weight; /* Kilograms carried without slowing down. */
    float max_carry_bulk;   /* Bulk carried at once (see ITEM_TILE_BULK). */
    uint8_t work_priorities[WORK_TYPE_COUNT]; /* 0 = never, else 1 (highest) .. 4. */
    uint8_t skills[SKILL_COUNT]; /* Levels, SKILL_LEVEL_MIN..SKILL_LEVEL_MAX. */
    float skill_xp[SKILL_COUNT]; /* Experience toward the next level (0 at the maximum). */
    float needs[NEED_COUNT];
} Colonist;

/* What a colonist needs to see of the world to make decisions. */
typedef struct ColonistContext {
    Map *map;
    Designations *designations;
    Items *items;
    Stockpiles *stockpiles;
    Reservations *reservations;
    Pathfinder *pathfinder;
    /* Connectivity regions (may be out of date, never wrong: see core/region.h). */
    const Regions *regions;
    Rng *rng;
    uint64_t tick;
    bool night; /* colonist_is_night(tick), computed once per tick for everyone. */
    uint8_t worker_id; /* Reservation owner for this colonist (slot + 1), never 0. */
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

/* Level in a skill; SKILL_LEVEL_MIN for SKILL_NONE or an invalid skill. */
int colonist_skill_level(const Colonist *colonist, SkillType skill);

/* Fails on an invalid skill or a level out of range. Experience toward the
 * next level starts over. */
bool colonist_set_skill_level(Colonist *colonist, SkillType skill, int level);

/* Adds experience to a skill, raising the level (possibly several times) each
 * time enough is gathered; nothing more is gathered at the maximum level.
 * Ignored for SKILL_NONE, invalid skills and xp <= 0. Returns the number of
 * levels gained. */
int colonist_gain_skill_xp(Colonist *colonist, SkillType skill, float xp);

/* Work done per tick on an order relying on `skill`: COLONIST_WORK_SPEED
 * scaled by skill_work_speed() (unscaled for SKILL_NONE). */
float colonist_work_speed(const Colonist *colonist, SkillType skill);

/* Progress toward the next level, 0..1 (0 at the maximum level). */
float colonist_skill_progress(const Colonist *colonist, SkillType skill);

/* Gives up the current task, releasing any reservation (e.g. before removal). */
void colonist_abandon_task(Colonist *colonist, const ColonistContext *context);

bool colonist_is_moving(const Colonist *colonist);

/* Interpolated position of the colonist's center, in world (tile) coordinates. */
void colonist_position(const Colonist *colonist, float *x, float *y);

/* Kilograms the colonist is carrying. */
float colonist_carried_weight(const Colonist *colonist);

/* How many items of a type the colonist can pick up at once (at least 1:
 * an item too heavy or too bulky is dragged). */
int colonist_carry_capacity(const Colonist *colonist, ItemType type);

/* Walking speed multiplier from the load: 1 up to the weight limit, lower above. */
float colonist_load_speed_factor(const Colonist *colonist);

NeedLevel need_level(float value);

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
