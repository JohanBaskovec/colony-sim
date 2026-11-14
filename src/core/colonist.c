#include "core/colonist.h"

#include "core/i18n.h"
#include "core/util.h"

#include <stdio.h>
#include <stdlib.h>
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
    for (int need = 0; need < NEED_COUNT; need++) {
        colonist->needs[need] = 1.0f;
    }
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
        [TASK_DRINK] = N_("colonist.task.drink"),
        [TASK_SLEEP] = N_("colonist.task.sleep"),
        [TASK_WORK] = N_("colonist.task.work"),
    };
    return (unsigned)task < TASK_COUNT ? KEYS[task] : KEYS[TASK_IDLE];
}

const char *colonist_activity_key(const Colonist *colonist)
{
    if (colonist->task == TASK_WORK) {
        const DesignationDef *def = designation_def((DesignationType)colonist->work_type);
        if (def != NULL && def->activity_key != NULL) {
            return def->activity_key;
        }
    }
    return colonist_task_name_key(colonist->task);
}

const char *need_name_key(NeedType need)
{
    static const char *const KEYS[NEED_COUNT] = {
        [NEED_FOOD] = N_("need.food"),
        [NEED_WATER] = N_("need.water"),
        [NEED_REST] = N_("need.rest"),
    };
    return (unsigned)need < NEED_COUNT ? KEYS[need] : KEYS[NEED_FOOD];
}

const char *need_level_key(NeedType need, NeedLevel level)
{
    static const char *const KEYS[NEED_COUNT][3] = {
        [NEED_FOOD] = {NULL, N_("need.food.low"), N_("need.food.critical")},
        [NEED_WATER] = {NULL, N_("need.water.low"), N_("need.water.critical")},
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

/* Every task change goes through here, so that a reserved designation is
 * always released when the colonist stops working on it. The path is kept:
 * callers set it before or clear it after. */
static void begin_task(Colonist *colonist, const ColonistContext *context, ColonistTask task)
{
    if (colonist->task == TASK_WORK) {
        designation_release(context->designations, colonist->target.x, colonist->target.y,
                            context->worker_id);
    }
    colonist->task = task;
    colonist->timer = 0;
    colonist->path_index = 0;
}

static void set_idle(Colonist *colonist, const ColonistContext *context, int ticks)
{
    begin_task(colonist, context, TASK_IDLE);
    colonist->timer = ticks;
    path_clear(&colonist->path);
}

void colonist_abandon_task(Colonist *colonist, const ColonistContext *context)
{
    set_idle(colonist, context, 0);
}

static bool search_allowed(const Colonist *colonist, SearchKind kind)
{
    return colonist->search_cooldown[kind] <= 0;
}

static void search_failed(Colonist *colonist, SearchKind kind)
{
    int backoff = colonist->search_failures[kind];
    colonist->search_cooldown[kind] = COLONIST_SEARCH_RETRY_TICKS << backoff;
    if (backoff < COLONIST_SEARCH_MAX_BACKOFF) {
        colonist->search_failures[kind]++;
    }
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

static bool is_drinking_spot(const Map *map, int x, int y, void *userdata)
{
    (void)userdata;
    return map_is_drinking_spot(map, x, y);
}

/* Walks to the nearest food (NEED_FOOD) or drinking spot (NEED_WATER). */
static bool try_satisfy(Colonist *colonist, const ColonistContext *context, NeedType need)
{
    bool drink = need == NEED_WATER;
    SearchKind kind = drink ? SEARCH_WATER : SEARCH_FOOD;
    if (!search_allowed(colonist, kind)) {
        return false;
    }
    TilePos found;
    if (!pathfinder_find_nearest(context->pathfinder, context->map, current_tile(colonist),
                                 drink ? is_drinking_spot : is_edible, NULL,
                                 drink ? COLONIST_WATER_SEARCH_MAX_COST
                                       : COLONIST_FOOD_SEARCH_MAX_COST,
                                 &colonist->path, &found)) {
        search_failed(colonist, kind);
        return false;
    }
    colonist->search_failures[kind] = 0;
    begin_task(colonist, context, drink ? TASK_DRINK : TASK_EAT);
    colonist->target = found;
    return true;
}

/* Tries to satisfy food and water when below `threshold`, most urgent first. */
static bool try_satisfy_urgent(Colonist *colonist, const ColonistContext *context,
                               float threshold)
{
    float food = colonist->needs[NEED_FOOD];
    float water = colonist->needs[NEED_WATER];
    NeedType order[2] = {NEED_WATER, NEED_FOOD};
    if (food < water) {
        order[0] = NEED_FOOD;
        order[1] = NEED_WATER;
    }
    for (int i = 0; i < 2; i++) {
        if (colonist->needs[order[i]] < threshold && try_satisfy(colonist, context, order[i])) {
            return true;
        }
    }
    return false;
}

static void start_sleeping(Colonist *colonist, const ColonistContext *context)
{
    begin_task(colonist, context, TASK_SLEEP);
    path_clear(&colonist->path);
}

/* ---- Work ------------------------------------------------------------------ */

static const int16_t NEIGHBOR_DX[4] = {0, 1, 0, -1};
static const int16_t NEIGHBOR_DY[4] = {-1, 0, 1, 0};

/* First available designation next to (x, y), in a fixed order (deterministic). */
static bool find_adjacent_work(const Designations *designations, int x, int y, TilePos *out)
{
    for (int i = 0; i < 4; i++) {
        int nx = x + NEIGHBOR_DX[i];
        int ny = y + NEIGHBOR_DY[i];
        if (designation_is_available(designations, nx, ny)) {
            *out = (TilePos){(int16_t)nx, (int16_t)ny};
            return true;
        }
    }
    return false;
}

/* A tile to stand on while working: passable, next to available work. */
static bool is_work_spot(const Map *map, int x, int y, void *userdata)
{
    TilePos unused;
    return map_is_passable(map, x, y) && find_adjacent_work(userdata, x, y, &unused);
}

static bool try_start_work(Colonist *colonist, const ColonistContext *context)
{
    Designations *designations = context->designations;
    if (designations == NULL || designations->available <= 0) {
        return false;
    }
    /* After a failure, new or freed work (a new revision) allows an early retry. */
    if (!search_allowed(colonist, SEARCH_WORK) &&
        colonist->work_search_revision == designations->revision) {
        return false;
    }
    TilePos spot, target;
    if (!pathfinder_find_nearest(context->pathfinder, context->map, current_tile(colonist),
                                 is_work_spot, designations, COLONIST_WORK_SEARCH_MAX_COST,
                                 &colonist->path, &spot)) {
        /* The path is untouched on failure: a wandering colonist keeps walking. */
        search_failed(colonist, SEARCH_WORK);
        colonist->work_search_revision = designations->revision;
        return false;
    }
    /* The search only stops on a spot next to available work. */
    find_adjacent_work(designations, spot.x, spot.y, &target);
    designation_reserve(designations, target.x, target.y, context->worker_id);
    colonist->search_failures[SEARCH_WORK] = 0;
    colonist->search_cooldown[SEARCH_WORK] = 0;
    begin_task(colonist, context, TASK_WORK);
    colonist->target = target;
    colonist->work_type = (uint8_t)designation_get(designations, target.x, target.y);
    return true;
}

static void work(Colonist *colonist, const ColonistContext *context)
{
    Designations *designations = context->designations;
    TilePos target = colonist->target;
    int distance = abs(colonist->x - target.x) + abs(colonist->y - target.y);
    if (distance != 1 ||
        designation_owner(designations, target.x, target.y) != context->worker_id) {
        set_idle(colonist, context, 0); /* Not there, or the order was cancelled. */
        return;
    }
    DesignationType type = designation_get(designations, target.x, target.y);
    if (!designation_can_apply(context->map, target.x, target.y, type)) {
        designation_set(designations, context->map, target.x, target.y, DESIGNATION_NONE);
        set_idle(colonist, context, 0);
        return;
    }
    if (designation_add_work(designations, context->map, target.x, target.y,
                             COLONIST_WORK_SPEED)) {
        set_idle(colonist, context, 0); /* Done: look for the next job right away. */
    }
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
            begin_task(colonist, context, TASK_WANDER);
            return;
        }
    }
    set_idle(colonist, context, random_idle_ticks(context->rng));
}

static bool is_night(uint64_t tick)
{
    int hour = game_date_from_tick(tick).hour;
    return hour >= 22 || hour < 6;
}

static void update_needs(Colonist *colonist)
{
    float *food = &colonist->needs[NEED_FOOD];
    float *water = &colonist->needs[NEED_WATER];
    float *rest = &colonist->needs[NEED_REST];
    *food = clampf(*food - NEED_FOOD_DECAY_PER_TICK, 0.0f, 1.0f);
    *water = clampf(*water - NEED_WATER_DECAY_PER_TICK, 0.0f, 1.0f);
    if (colonist->task == TASK_SLEEP && !colonist_is_moving(colonist)) {
        *rest = clampf(*rest + NEED_REST_GAIN_PER_TICK, 0.0f, 1.0f);
    } else {
        *rest = clampf(*rest - NEED_REST_DECAY_PER_TICK, 0.0f, 1.0f);
    }
    for (int kind = 0; kind < SEARCH_COUNT; kind++) {
        if (colonist->search_cooldown[kind] > 0) {
            colonist->search_cooldown[kind]--;
        }
    }
}

/* Chooses a new task when needs require it, or picks up work when idle.
 * Only called on tile centers. */
static void decide(Colonist *colonist, const ColonistContext *context)
{
    float rest = colonist->needs[NEED_REST];

    switch (colonist->task) {
    case TASK_SLEEP:
        if (rest >= 1.0f) {
            set_idle(colonist, context, random_idle_ticks(context->rng));
        } else {
            /* Severe hunger or thirst wakes colonists up. */
            try_satisfy_urgent(colonist, context, NEED_CRITICAL_THRESHOLD);
        }
        return;
    case TASK_EAT:
    case TASK_DRINK:
        return; /* Carried on until done. */
    default:
        break;
    }

    if (try_satisfy_urgent(colonist, context, NEED_LOW_THRESHOLD)) {
        return;
    }
    if (rest < NEED_LOW_THRESHOLD || (is_night(context->tick) && rest < COLONIST_NIGHT_SLEEP_REST)) {
        if (!try_satisfy_urgent(colonist, context, COLONIST_BEDTIME_NEED)) {
            start_sleeping(colonist, context);
        }
        return;
    }
    if (colonist->task == TASK_IDLE || colonist->task == TASK_WANDER) {
        try_start_work(colonist, context);
    }
}

/* Moves one tick along the path. */
static void walk(Colonist *colonist, const ColonistContext *context)
{
    TilePos next = colonist->path.steps[colonist->path_index];
    float cost = map_move_cost(context->map, next.x, next.y);
    if (cost <= 0.0f) {
        set_idle(colonist, context, random_idle_ticks(context->rng)); /* Path became blocked. */
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
        set_idle(colonist, context, 0);
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

    if (colonist->needs[NEED_FOOD] < COLONIST_SATED_FOOD &&
        try_satisfy(colonist, context, NEED_FOOD)) {
        return;
    }
    set_idle(colonist, context, random_idle_ticks(context->rng));
}

static void drink(Colonist *colonist, const ColonistContext *context)
{
    if (colonist->x != colonist->target.x || colonist->y != colonist->target.y ||
        !map_is_drinking_spot(context->map, colonist->x, colonist->y)) {
        set_idle(colonist, context, 0); /* The water is gone: look again soon. */
        return;
    }
    if (++colonist->timer < COLONIST_DRINK_TICKS) {
        return;
    }
    colonist->needs[NEED_WATER] = 1.0f;
    set_idle(colonist, context, random_idle_ticks(context->rng));
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
    case TASK_WANDER: set_idle(colonist, context, random_idle_ticks(context->rng)); break;
    case TASK_EAT: eat(colonist, context); break;
    case TASK_DRINK: drink(colonist, context); break;
    case TASK_WORK: work(colonist, context); break;
    case TASK_SLEEP:
    case TASK_COUNT: break;
    }
}
