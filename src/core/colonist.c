#include "core/colonist.h"

#include "core/i18n.h"
#include "core/util.h"

#include <stdio.h>
#include <string.h>

#define SQRT2 1.41421356f

/* First names from many cultures. Names are proper nouns and are not translated. */
static const char *const NAMES[] = {
    "Ada",    "Aiko",   "Amara",  "Anouk",  "Arjun",  "Astrid", "Bao",    "Bastien",
    "Bjorn",  "Camille", "Chidi", "Dalia",  "Dmitri", "Elif",   "Emeka",  "Esme",
    "Farah",  "Felix",  "Freya",  "Gael",   "Hana",   "Hugo",   "Idris",  "Ines",
    "Ivo",    "Jun",    "Kai",    "Kaveh",  "Keziah", "Lars",   "Leila",  "Lena",
    "Luca",   "Maeve",  "Malik",  "Mateo",  "Mei",    "Milan",  "Nadia",  "Nils",
    "Noor",   "Oren",   "Oskar",  "Priya",  "Quinn",  "Rafael", "Ravi",   "Rosa",
    "Sami",   "Sanne",  "Selin",  "Soren",  "Tariq",  "Thea",   "Tomas",  "Uma",
    "Vera",   "Wren",   "Xiadani", "Yara",  "Yusuf",  "Zainab", "Zofia",  "Zuri",
};

size_t colonist_name_count(void)
{
    return COLONY_ARRAY_COUNT(NAMES);
}

const char *colonist_name_at(size_t index)
{
    return index < COLONY_ARRAY_COUNT(NAMES) ? NAMES[index] : NULL;
}

void colonist_init(Colonist *colonist, const char *name, int age, int x, int y)
{
    memset(colonist, 0, sizeof(*colonist));
    snprintf(colonist->name, sizeof(colonist->name), "%s", name);
    colonist->age = age;
    colonist->x = x;
    colonist->y = y;
    colonist->task = TASK_IDLE;
    colonist->needs[NEED_FOOD] = 1.0f;
    colonist->needs[NEED_REST] = 1.0f;
}

void colonist_free(Colonist *colonist)
{
    path_free(&colonist->path);
}

bool colonist_is_moving(const Colonist *colonist)
{
    return colonist->path_index < colonist->path.count;
}

void colonist_position(const Colonist *colonist, float *x, float *y)
{
    float px = (float)colonist->x;
    float py = (float)colonist->y;
    if (colonist_is_moving(colonist)) {
        TilePos next = colonist->path.steps[colonist->path_index];
        px = lerpf(px, (float)next.x, colonist->move_progress);
        py = lerpf(py, (float)next.y, colonist->move_progress);
    }
    *x = px + 0.5f;
    *y = py + 0.5f;
}

NeedLevel need_level(float value)
{
    if (value < NEED_CRITICAL_THRESHOLD) {
        return NEED_LEVEL_CRITICAL;
    }
    return value < NEED_LOW_THRESHOLD ? NEED_LEVEL_LOW : NEED_LEVEL_OK;
}

const char *colonist_task_name_key(ColonistTask task)
{
    static const char *const KEYS[TASK_COUNT] = {
        [TASK_IDLE] = N_("colonist.task.idle"),
        [TASK_WANDER] = N_("colonist.task.wander"),
        [TASK_EAT] = N_("colonist.task.eat"),
        [TASK_SLEEP] = N_("colonist.task.sleep"),
    };
    return (unsigned)task < TASK_COUNT ? KEYS[task] : KEYS[TASK_IDLE];
}

const char *need_name_key(NeedType need)
{
    return need == NEED_FOOD ? N_("need.food") : N_("need.rest");
}

const char *need_level_key(NeedType need, NeedLevel level)
{
    static const char *const KEYS[NEED_COUNT][3] = {
        [NEED_FOOD] = {NULL, N_("need.food.low"), N_("need.food.critical")},
        [NEED_REST] = {NULL, N_("need.rest.low"), N_("need.rest.critical")},
    };
    if ((unsigned)need >= NEED_COUNT || (unsigned)level > NEED_LEVEL_CRITICAL) {
        return NULL;
    }
    return KEYS[need][level];
}

/* ---- Behavior ------------------------------------------------------------ */

static TilePos current_tile(const Colonist *colonist)
{
    return (TilePos){(int16_t)colonist->x, (int16_t)colonist->y};
}

static void set_idle(Colonist *colonist, int ticks)
{
    colonist->task = TASK_IDLE;
    colonist->timer = ticks;
    colonist->path_index = 0;
    path_clear(&colonist->path);
}

static int random_idle_ticks(Rng *rng)
{
    return rng_range_i32(rng, COLONIST_IDLE_MIN_TICKS, COLONIST_IDLE_MAX_TICKS);
}

static bool is_edible(const Map *map, int x, int y, void *userdata)
{
    (void)userdata;
    return map_has_edible_plant(map, x, y);
}

static bool try_start_eating(Colonist *colonist, const ColonistContext *context)
{
    if (colonist->food_search_cooldown > 0) {
        return false;
    }
    TilePos found;
    if (!pathfinder_find_nearest(context->pathfinder, context->map, current_tile(colonist),
                                 is_edible, NULL, COLONIST_FOOD_SEARCH_MAX_COST,
                                 &colonist->path, &found)) {
        colonist->food_search_cooldown = COLONIST_FOOD_SEARCH_RETRY_TICKS;
        return false;
    }
    colonist->task = TASK_EAT;
    colonist->target = found;
    colonist->timer = 0;
    colonist->path_index = 0;
    return true;
}

static void start_sleeping(Colonist *colonist)
{
    colonist->task = TASK_SLEEP;
    colonist->path_index = 0;
    path_clear(&colonist->path);
}

static void start_wandering(Colonist *colonist, const ColonistContext *context)
{
    const int radius = COLONIST_WANDER_RADIUS;
    for (int attempt = 0; attempt < 8; attempt++) {
        int dx = rng_range_i32(context->rng, -radius, radius);
        int dy = rng_range_i32(context->rng, -radius, radius);
        TilePos goal = {(int16_t)(colonist->x + dx), (int16_t)(colonist->y + dy)};
        if ((dx == 0 && dy == 0) || !map_is_passable(context->map, goal.x, goal.y)) {
            continue;
        }
        if (pathfinder_find(context->pathfinder, context->map, current_tile(colonist), goal,
                            (float)radius * 3.0f, &colonist->path)) {
            colonist->task = TASK_WANDER;
            colonist->path_index = 0;
            return;
        }
    }
    set_idle(colonist, random_idle_ticks(context->rng));
}

static bool is_night(uint64_t tick)
{
    int hour = game_date_from_tick(tick).hour;
    return hour >= 22 || hour < 6;
}

static void update_needs(Colonist *colonist)
{
    float *food = &colonist->needs[NEED_FOOD];
    float *rest = &colonist->needs[NEED_REST];
    *food = clampf(*food - NEED_FOOD_DECAY_PER_TICK, 0.0f, 1.0f);
    if (colonist->task == TASK_SLEEP && !colonist_is_moving(colonist)) {
        *rest = clampf(*rest + NEED_REST_GAIN_PER_TICK, 0.0f, 1.0f);
    } else {
        *rest = clampf(*rest - NEED_REST_DECAY_PER_TICK, 0.0f, 1.0f);
    }
    if (colonist->food_search_cooldown > 0) {
        colonist->food_search_cooldown--;
    }
}

/* Chooses a new task when needs require it. Only called on tile centers. */
static void decide(Colonist *colonist, const ColonistContext *context)
{
    float food = colonist->needs[NEED_FOOD];
    float rest = colonist->needs[NEED_REST];

    switch (colonist->task) {
    case TASK_SLEEP:
        if (rest >= 1.0f) {
            set_idle(colonist, random_idle_ticks(context->rng));
        } else if (food < NEED_CRITICAL_THRESHOLD) {
            try_start_eating(colonist, context); /* Hunger wakes colonists up. */
        }
        return;
    case TASK_EAT:
        return; /* Carried on by act(). */
    default:
        break;
    }

    if (food < NEED_LOW_THRESHOLD && try_start_eating(colonist, context)) {
        return;
    }
    if (rest < NEED_LOW_THRESHOLD || (is_night(context->tick) && rest < COLONIST_NIGHT_SLEEP_REST)) {
        start_sleeping(colonist);
    }
}

/* Moves one tick along the path. */
static void walk(Colonist *colonist, const ColonistContext *context)
{
    TilePos next = colonist->path.steps[colonist->path_index];
    float cost = map_move_cost(context->map, next.x, next.y);
    if (cost <= 0.0f) {
        set_idle(colonist, random_idle_ticks(context->rng)); /* Path became blocked. */
        return;
    }
    if (next.x != colonist->x && next.y != colonist->y) {
        cost *= SQRT2;
    }
    colonist->move_progress += COLONIST_WALK_SPEED / (float)TICKS_PER_SECOND / cost;
    if (colonist->move_progress >= 1.0f) {
        colonist->x = next.x;
        colonist->y = next.y;
        colonist->move_progress = 0.0f;
        colonist->path_index++;
        if (colonist->path_index >= colonist->path.count) {
            colonist->path_index = 0;
            path_clear(&colonist->path);
        }
    }
}

static void eat(Colonist *colonist, const ColonistContext *context)
{
    Map *map = context->map;
    if (colonist->x != colonist->target.x || colonist->y != colonist->target.y ||
        !map_has_edible_plant(map, colonist->x, colonist->y)) {
        /* Not there, or someone else ate it first: look again soon. */
        set_idle(colonist, 0);
        return;
    }
    if (++colonist->timer < COLONIST_EAT_TICKS) {
        return;
    }

    Plant *plant = &map->plants[map_index(map, colonist->x, colonist->y)];
    const PlantDef *def = plant_def((PlantType)plant->type);
    colonist->needs[NEED_FOOD] =
        clampf(colonist->needs[NEED_FOOD] + def->nutrition * plant->growth, 0.0f, 1.0f);
    plant->growth = PLANT_GROWTH_AFTER_EATEN;

    if (colonist->needs[NEED_FOOD] < COLONIST_SATED_FOOD && try_start_eating(colonist, context)) {
        return;
    }
    set_idle(colonist, random_idle_ticks(context->rng));
}

void colonist_tick(Colonist *colonist, const ColonistContext *context)
{
    update_needs(colonist);
    if (colonist->move_progress == 0.0f) {
        decide(colonist, context);
    }

    if (colonist_is_moving(colonist)) {
        walk(colonist, context);
        return;
    }

    switch (colonist->task) {
    case TASK_IDLE:
        if (colonist->timer > 0) {
            colonist->timer--;
        } else {
            start_wandering(colonist, context);
        }
        break;
    case TASK_WANDER: set_idle(colonist, random_idle_ticks(context->rng)); break;
    case TASK_EAT: eat(colonist, context); break;
    case TASK_SLEEP:
    case TASK_COUNT: break;
    }
}
