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
#include "core/reservation.h"
#include "core/rng.h"
#include "core/work.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COLONIST_MAX 64
#define COLONIST_NAME_SIZE 32

/* Walking speed on terrain of cost 1, in tiles per second at normal game speed. */
#define COLONIST_WALK_SPEED 3.5f

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
/* Work done per tick on a designation. */
#define COLONIST_WORK_SPEED 1.0f
#define COLONIST_WORK_SEARCH_MAX_COST 250.0f
/* Delay before searching again after a failed food, water or work search, doubled
 * after each consecutive failure up to 2^COLONIST_SEARCH_MAX_BACKOFF times. A
 * failed search floods everything within its cost limit, so it must stay rare. */
#define COLONIST_SEARCH_RETRY_TICKS (TICKS_PER_HOUR / 2)
#define COLONIST_SEARCH_MAX_BACKOFF 3
#define COLONIST_WANDER_RADIUS 6
#define COLONIST_IDLE_MIN_TICKS 120
#define COLONIST_IDLE_MAX_TICKS 360
/* Growth left on a plant after its fruit has been eaten. */
#define PLANT_GROWTH_AFTER_EATEN 0.05f

typedef enum ColonistTask {
    TASK_IDLE,
    TASK_WANDER,
    TASK_EAT, /* Walking to food, then eating. */
    TASK_DRINK, /* Walking to a drinking spot, then drinking. */
    TASK_SLEEP,
    TASK_WORK, /* Walking next to a designated tile, then working on it. */
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
    uint8_t work_priorities[WORK_TYPE_COUNT]; /* 0 = never, else 1 (highest) .. 4. */
    float needs[NEED_COUNT];
} Colonist;

/* What a colonist needs to see of the world to make decisions. */
typedef struct ColonistContext {
    Map *map;
    Designations *designations;
    Items *items;
    Reservations *reservations;
    Pathfinder *pathfinder;
    Rng *rng;
    uint64_t tick;
    uint8_t worker_id; /* Reservation owner for this colonist (slot + 1), never 0. */
} ColonistContext;

void colonist_init(Colonist *colonist, const char *name, int age, int x, int y);
void colonist_free(Colonist *colonist);

void colonist_tick(Colonist *colonist, const ColonistContext *context);

/* Gives up the current task, releasing any reservation (e.g. before removal). */
void colonist_abandon_task(Colonist *colonist, const ColonistContext *context);

bool colonist_is_moving(const Colonist *colonist);

/* Interpolated position of the colonist's center, in world (tile) coordinates. */
void colonist_position(const Colonist *colonist, float *x, float *y);

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
