#include "core/colonist.h"

#include "core/hauling.h"
#include "core/i18n.h"
#include "core/util.h"

#include <math.h>
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
    for (int work = 0; work < WORK_TYPE_COUNT; work++) {
        colonist->work_priorities[work] = WORK_PRIORITY_DEFAULT;
    }
    colonist_set_strength(colonist, COLONIST_STRENGTH_STANDARD);
    colonist->max_carry_bulk = COLONIST_CARRY_BULK;
}

bool colonist_set_work_priority(Colonist *colonist, WorkType type, int priority)
{
    if ((unsigned)type >= WORK_TYPE_COUNT || !work_priority_is_valid(priority)) {
        return false;
    }
    if (colonist->work_priorities[type] != priority) {
        colonist->work_priorities[type] = (uint8_t)priority;
        /* Jobs skipped so far may now be acceptable: forget failed searches. */
        colonist->search_cooldown[SEARCH_WORK] = 0;
        colonist->search_failures[SEARCH_WORK] = 0;
        colonist->search_cooldown[SEARCH_HAUL] = 0;
        colonist->search_failures[SEARCH_HAUL] = 0;
    }
    return true;
}

int colonist_skill_level(const Colonist *colonist, SkillType skill)
{
    return (unsigned)skill < SKILL_COUNT ? colonist->skills[skill] : SKILL_LEVEL_MIN;
}

bool colonist_set_skill_level(Colonist *colonist, SkillType skill, int level)
{
    if ((unsigned)skill >= SKILL_COUNT || level < SKILL_LEVEL_MIN || level > SKILL_LEVEL_MAX) {
        return false;
    }
    colonist->skills[skill] = (uint8_t)level;
    colonist->skill_xp[skill] = 0.0f;
    return true;
}

int colonist_gain_skill_xp(Colonist *colonist, SkillType skill, float xp)
{
    if ((unsigned)skill >= SKILL_COUNT || !(xp > 0.0f)) {
        return 0;
    }
    int gained = 0;
    float *stored = &colonist->skill_xp[skill];
    *stored += xp;
    for (;;) {
        float needed = skill_xp_to_next_level(colonist->skills[skill]);
        if (needed <= 0.0f) {
            *stored = 0.0f; /* Mastered: nothing more to learn. */
            break;
        }
        if (*stored < needed) {
            break;
        }
        *stored -= needed;
        colonist->skills[skill]++;
        gained++;
    }
    return gained;
}

float colonist_work_speed(const Colonist *colonist, SkillType skill)
{
    if ((unsigned)skill >= SKILL_COUNT) {
        return COLONIST_WORK_SPEED;
    }
    return COLONIST_WORK_SPEED * skill_work_speed(colonist->skills[skill]);
}

float colonist_skill_progress(const Colonist *colonist, SkillType skill)
{
    if ((unsigned)skill >= SKILL_COUNT) {
        return 0.0f;
    }
    float needed = skill_xp_to_next_level(colonist->skills[skill]);
    return needed > 0.0f ? clampf(colonist->skill_xp[skill] / needed, 0.0f, 1.0f) : 0.0f;
}

float colonist_carry_weight_for_strength(int strength)
{
    strength = clampi(strength, COLONIST_STRENGTH_MIN, COLONIST_STRENGTH_MAX);
    return COLONIST_CARRY_WEIGHT +
           COLONIST_CARRY_WEIGHT_PER_STRENGTH * (float)(strength - COLONIST_STRENGTH_STANDARD);
}

int colonist_random_strength(Rng *rng, int age)
{
    int strength = COLONIST_STRENGTH_BASE + rng_range_i32(rng, 0, COLONIST_STRENGTH_DIE) +
                   rng_range_i32(rng, 0, COLONIST_STRENGTH_DIE);
    if (age > COLONIST_STRENGTH_PEAK_AGE) {
        strength -= (age - COLONIST_STRENGTH_PEAK_AGE) / COLONIST_STRENGTH_AGE_STEP;
    }
    return clampi(strength, COLONIST_STRENGTH_MIN, COLONIST_STRENGTH_MAX);
}

bool colonist_set_strength(Colonist *colonist, int strength)
{
    if (strength < COLONIST_STRENGTH_MIN || strength > COLONIST_STRENGTH_MAX) {
        return false;
    }
    colonist->strength = strength;
    colonist->max_carry_weight = colonist_carry_weight_for_strength(strength);
    return true;
}

float colonist_carried_weight(const Colonist *colonist)
{
    return item_stack_weight((ItemType)colonist->carried.type, colonist->carried.count);
}

int colonist_carry_capacity(const Colonist *colonist, ItemType type)
{
    const ItemDef *def = item_def(type);
    if (def == NULL || type == ITEM_NONE) {
        return 0;
    }
    float by_weight = floorf(colonist->max_carry_weight / def->weight);
    float by_bulk = floorf(colonist->max_carry_bulk / def->bulk);
    float capacity = by_weight < by_bulk ? by_weight : by_bulk;
    if (capacity < 1.0f) {
        return 1; /* Dragged. */
    }
    return capacity > (float)UINT16_MAX ? UINT16_MAX : (int)capacity;
}

float colonist_load_speed_factor(const Colonist *colonist)
{
    float load = colonist_carried_weight(colonist);
    if (load <= colonist->max_carry_weight) {
        return 1.0f;
    }
    float factor = colonist->max_carry_weight / load;
    return factor < COLONIST_MIN_LOAD_SPEED ? COLONIST_MIN_LOAD_SPEED : factor;
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
        [TASK_HAUL] = N_("colonist.task.haul"),
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

/* Every task change goes through here: whatever the colonist had reserved
 * (job, food) is released, so reservations never outlive the task that made
 * them. The path is kept: callers set it before or clear it after. */
static void begin_task(Colonist *colonist, const ColonistContext *context, ColonistTask task)
{
    if (colonist->carried.count > 0) {
        /* Whatever is in hand is put down where the colonist stands. */
        items_drop(context->items, context->map, colonist->x, colonist->y, colonist->carried);
        colonist->carried = (ItemStack){.type = ITEM_NONE};
    }
    reservations_release_all(context->reservations, context->worker_id);
    colonist->haul_delivering = false;
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

/* Food on a tile: edible items on the ground, or a ripe plant. */
static bool tile_has_food(const Map *map, const Items *items, int x, int y)
{
    return item_is_food((ItemType)items_get(items, x, y).type) || map_has_edible_plant(map, x, y);
}

/* Search filter: food that nobody else is about to eat. */
typedef struct FoodSearch {
    const Items *items;
    const Reservations *reservations;
    uint8_t self;
} FoodSearch;

static bool is_free_food(const Map *map, int x, int y, void *userdata)
{
    const FoodSearch *search = userdata;
    return tile_has_food(map, search->items, x, y) &&
           !reservations_taken_by_other(search->reservations, RESERVATION_FOOD,
                                        (TilePos){(int16_t)x, (int16_t)y}, search->self);
}

static bool is_drinking_spot(const Map *map, int x, int y, void *userdata)
{
    (void)userdata;
    return map_is_drinking_spot(map, x, y);
}

/* Region the colonist stands in (REGION_NONE when unknown). */
static uint32_t current_region(const Colonist *colonist, const ColonistContext *context)
{
    return regions_at(context->regions, context->map, colonist->x, colonist->y);
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
    FoodSearch food = {context->items, context->reservations, context->worker_id};
    /* No water anywhere the colonist can walk to: the search would explore
     * everything within its cost limit for nothing. */
    bool hopeless = drink && !regions_has_drinking_spot(context->regions, context->map,
                                                        current_region(colonist, context));
    if (hopeless ||
        !pathfinder_find_nearest(context->pathfinder, context->map, current_tile(colonist),
                                 drink ? is_drinking_spot : is_free_food, &food,
                                 drink ? COLONIST_WATER_SEARCH_MAX_COST
                                       : COLONIST_FOOD_SEARCH_MAX_COST,
                                 &colonist->path, &found)) {
        search_failed(colonist, kind);
        return false;
    }
    colonist->search_failures[kind] = 0;
    begin_task(colonist, context, drink ? TASK_DRINK : TASK_EAT);
    colonist->target = found;
    if (!drink) {
        reservations_add(context->reservations, RESERVATION_FOOD, found, context->worker_id);
    }
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

/* Search filter for jobs: work types allowed at the priority being searched. */
typedef struct WorkSearch {
    const Designations *designations;
    const Reservations *reservations;
    WorkTypeMask mask;
    uint8_t self;
} WorkSearch;

static bool is_open_job(const WorkSearch *search, int x, int y)
{
    DesignationType type = designation_get(search->designations, x, y);
    if (type == DESIGNATION_NONE ||
        !(search->mask & WORK_TYPE_BIT(designation_def(type)->work_type))) {
        return false;
    }
    return !reservations_taken_by_other(search->reservations, RESERVATION_WORK,
                                        (TilePos){(int16_t)x, (int16_t)y}, search->self);
}

/* First open job next to (x, y), in a fixed order (deterministic). */
static bool find_adjacent_job(const WorkSearch *search, int x, int y, TilePos *out)
{
    for (int i = 0; i < 4; i++) {
        int nx = x + NEIGHBOR_DX[i];
        int ny = y + NEIGHBOR_DY[i];
        if (is_open_job(search, nx, ny)) {
            *out = (TilePos){(int16_t)nx, (int16_t)ny};
            return true;
        }
    }
    return false;
}

/* A tile to stand on while working: passable, next to an open job. */
static bool is_work_spot(const Map *map, int x, int y, void *userdata)
{
    TilePos unused;
    return map_is_passable(map, x, y) && find_adjacent_job(userdata, x, y, &unused);
}

/*
 * Whether some open job has a tile to stand on in `region`. False only when
 * it is certain that none has (an unknown region gives true). Scans the
 * designation layer: far cheaper than a path search that fails, but not
 * meant for every tick.
 */
static bool open_job_reachable(const WorkSearch *search, const ColonistContext *context,
                               uint32_t region)
{
    const Designations *designations = search->designations;
    const Regions *regions = context->regions;
    const int width = designations->width, height = designations->height;
    if (region == REGION_NONE || !regions_current(regions, context->map) ||
        regions->width != width || regions->height != height) {
        return true;
    }
    int remaining = designations->count;
    for (int y = 0; y < height && remaining > 0; y++) {
        const uint8_t *types = &designations->type[(size_t)y * (size_t)width];
        const uint32_t *ids = &regions->id[(size_t)y * (size_t)width];
        for (int x = 0; x < width; x++) {
            if (types[x] == DESIGNATION_NONE) {
                continue;
            }
            remaining--; /* The scan stops once every order has been seen. */
            bool next_to_region =
                (x > 0 && ids[x - 1] == region) || (x + 1 < width && ids[x + 1] == region) ||
                (y > 0 && ids[x - width] == region) ||
                (y + 1 < height && ids[x + width] == region);
            if (next_to_region && is_open_job(search, x, y)) {
                return true;
            }
        }
    }
    return false;
}

static uint32_t work_stamp(const ColonistContext *context)
{
    return context->designations->revision + context->reservations->releases;
}

static HaulWorld haul_world(const ColonistContext *context)
{
    HaulWorld world = {context->map,         context->items,      context->stockpiles,
                       context->reservations, context->pathfinder, context->worker_id};
    return world;
}

static uint32_t haul_stamp(const ColonistContext *context)
{
    return context->stockpiles->revision + context->items->additions +
           context->reservations->releases;
}

/* Designation jobs of the given work types: nearest one. */
static bool try_designation_job(Colonist *colonist, const ColonistContext *context,
                                WorkTypeMask mask)
{
    const Designations *designations = context->designations;
    if (designations_count_for_work(designations, mask) == 0) {
        return false;
    }
    WorkSearch search = {designations, context->reservations, mask, context->worker_id};
    /* First make sure some open job can be reached at all: with orders deep
     * inside rock, across water, or all taken by others, the search would
     * explore the whole area for nothing. */
    if (!open_job_reachable(&search, context, current_region(colonist, context))) {
        return false;
    }
    TilePos spot, target;
    if (!pathfinder_find_nearest(context->pathfinder, context->map, current_tile(colonist),
                                 is_work_spot, &search, COLONIST_WORK_SEARCH_MAX_COST,
                                 &colonist->path, &spot)) {
        return false; /* The path is untouched on failure: a wanderer keeps walking. */
    }
    /* The search only stops on a spot next to an open job. */
    find_adjacent_job(&search, spot.x, spot.y, &target);
    begin_task(colonist, context, TASK_WORK);
    colonist->target = target;
    colonist->work_type = (uint8_t)designation_get(designations, target.x, target.y);
    reservations_add(context->reservations, RESERVATION_WORK, target, context->worker_id);
    return true;
}

static bool try_haul_job(Colonist *colonist, const ColonistContext *context)
{
    HaulWorld world = haul_world(context);
    if (!haul_any_job_possible(&world)) {
        return false;
    }
    HaulJob job;
    if (!haul_find_job(&world, current_tile(colonist), COLONIST_WORK_SEARCH_MAX_COST, &job) ||
        !pathfinder_find(context->pathfinder, context->map, current_tile(colonist), job.source,
                         COLONIST_WORK_SEARCH_MAX_COST, &colonist->path)) {
        return false;
    }
    begin_task(colonist, context, TASK_HAUL);
    colonist->target = job.source;
    colonist->haul_destination = job.destination;
    reservations_add(context->reservations, RESERVATION_HAUL_SOURCE, job.source,
                     context->worker_id);
    reservations_add(context->reservations, RESERVATION_STORAGE, job.destination,
                     context->worker_id);
    return true;
}

/*
 * Takes the nearest job of the highest priority level that has one. At each
 * level, designations (mining, woodcutting...) come before hauling. Each
 * search kind backs off on failure until its stamp shows that something
 * changed.
 */
static bool try_start_work(Colonist *colonist, const ColonistContext *context)
{
    int open_jobs = context->designations->count -
                    context->reservations->count_by_kind[RESERVATION_WORK];
    bool designations_allowed =
        open_jobs > 0 && (search_allowed(colonist, SEARCH_WORK) ||
                          colonist->work_search_stamp != work_stamp(context));
    bool hauling_allowed = search_allowed(colonist, SEARCH_HAUL) ||
                           colonist->haul_search_stamp != haul_stamp(context);
    bool searched_designations = false, searched_hauling = false;

    for (int priority = WORK_PRIORITY_HIGHEST; priority <= WORK_PRIORITY_LOWEST; priority++) {
        WorkTypeMask mask = work_types_at_priority(colonist->work_priorities, priority);
        WorkTypeMask designation_mask = mask & ~WORK_TYPE_BIT(WORK_HAULING);
        if (designations_allowed && designation_mask != 0) {
            searched_designations = true;
            if (try_designation_job(colonist, context, designation_mask)) {
                colonist->search_failures[SEARCH_WORK] = 0;
                colonist->search_cooldown[SEARCH_WORK] = 0;
                return true;
            }
        }
        if (hauling_allowed && (mask & WORK_TYPE_BIT(WORK_HAULING))) {
            searched_hauling = true;
            if (try_haul_job(colonist, context)) {
                colonist->search_failures[SEARCH_HAUL] = 0;
                colonist->search_cooldown[SEARCH_HAUL] = 0;
                return true;
            }
        }
    }
    if (searched_designations) {
        search_failed(colonist, SEARCH_WORK);
        colonist->work_search_stamp = work_stamp(context);
    }
    if (searched_hauling) {
        search_failed(colonist, SEARCH_HAUL);
        colonist->haul_search_stamp = haul_stamp(context);
    }
    return false;
}

/* Whether the haul destination still accepts these items and has room. */
static bool haul_destination_valid(const Colonist *colonist, const ColonistContext *context,
                                   ItemStack stack)
{
    TilePos dest = colonist->haul_destination;
    return haul_destination_room(context->stockpiles, context->items, dest.x, dest.y, stack) > 0;
}

/* Items of the load in hand (or of `stack` when empty) that can still be
 * added: limited by what the colonist can carry and the room left on the
 * destination tile. */
static int haul_room_left(const Colonist *colonist, const ColonistContext *context,
                          ItemStack stack)
{
    TilePos dest = colonist->haul_destination;
    ItemStack load = colonist->carried.count > 0 ? colonist->carried : stack;
    int room = haul_destination_room(context->stockpiles, context->items, dest.x, dest.y, load);
    int capacity = colonist_carry_capacity(colonist, (ItemType)load.type);
    int left = (room < capacity ? room : capacity) - (int)colonist->carried.count;
    return left > 0 ? left : 0;
}

/* Stops collecting and heads for the destination with the load in hand. */
static bool start_delivery(Colonist *colonist, const ColonistContext *context)
{
    reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE,
                              context->worker_id);
    colonist->haul_delivering = true;
    colonist->path_index = 0;
    return pathfinder_find(context->pathfinder, context->map, current_tile(colonist),
                           colonist->haul_destination, PATH_NO_COST_LIMIT, &colonist->path);
}

/* With room left in hand, heads for another matching stack nearby that the
 * destination accepts. Returns false when there is none. */
static bool collect_more(Colonist *colonist, const ColonistContext *context)
{
    if (haul_room_left(colonist, context, colonist->carried) <= 0) {
        return false;
    }
    HaulWorld world = haul_world(context);
    TilePos extra;
    if (!haul_find_extra_source(&world, current_tile(colonist), colonist->carried,
                                colonist->haul_destination, COLONIST_HAUL_COLLECT_MAX_COST,
                                &extra, &colonist->path)) {
        return false;
    }
    reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE,
                              context->worker_id);
    reservations_add(context->reservations, RESERVATION_HAUL_SOURCE, extra, context->worker_id);
    colonist->target = extra;
    colonist->path_index = 0;
    return true;
}

/*
 * Whether a haul is still worth doing. While collecting: the stack is still
 * there, matches what is in hand and the destination accepts it; when it is
 * gone, a hauler that already holds items delivers them. While delivering:
 * when the destination stopped accepting the items (filter changed, zone
 * removed, tile filled), the hauler looks for another storage tile and only
 * gives up (dropping its load) if there is none.
 */
static bool haul_still_valid(Colonist *colonist, const ColonistContext *context)
{
    if (!colonist->haul_delivering) {
        ItemStack stack = items_get(context->items, colonist->target.x, colonist->target.y);
        bool holding = colonist->carried.count > 0;
        if (colonist->work_priorities[WORK_HAULING] != WORK_PRIORITY_DISABLED &&
            stack.type != ITEM_NONE && (!holding || item_stacks_match(stack, colonist->carried)) &&
            haul_destination_valid(colonist, context, stack) &&
            (!holding || haul_destination_valid(colonist, context, colonist->carried))) {
            return true;
        }
        return holding && start_delivery(colonist, context);
    }
    if (haul_destination_valid(colonist, context, colonist->carried)) {
        return true;
    }
    HaulWorld world = haul_world(context);
    TilePos dest;
    /* Carried items count as not stored: any accepting stockpile will do. */
    if (!haul_find_destination(&world, current_tile(colonist), colonist->carried, 0,
                               COLONIST_WORK_SEARCH_MAX_COST, &dest, &colonist->path)) {
        return false;
    }
    reservations_release_kind(context->reservations, RESERVATION_STORAGE, context->worker_id);
    reservations_add(context->reservations, RESERVATION_STORAGE, dest, context->worker_id);
    colonist->haul_destination = dest;
    colonist->path_index = 0;
    return true;
}

static void haul(Colonist *colonist, const ColonistContext *context)
{
    if (!haul_still_valid(colonist, context)) {
        set_idle(colonist, context, 0); /* Drops what it carries. */
        return;
    }
    if (colonist_is_moving(colonist)) {
        return; /* On the way (possibly rerouted). */
    }
    if (!colonist->haul_delivering) {
        /* At a stack: pick up what fits, then collect more nearby or deliver. */
        if (colonist->x != colonist->target.x || colonist->y != colonist->target.y) {
            set_idle(colonist, context, 0);
            return;
        }
        ItemStack stack = items_get(context->items, colonist->x, colonist->y);
        int wanted = haul_room_left(colonist, context, stack);
        int taken = items_remove(context->items, colonist->x, colonist->y,
                                 stack.count < wanted ? stack.count : wanted);
        ItemStack picked = stack; /* Same condition as the stack it comes from. */
        picked.count = (uint16_t)taken;
        item_stack_merge(&colonist->carried, picked);
        if (colonist->carried.count == 0 ||
            (!collect_more(colonist, context) && !start_delivery(colonist, context))) {
            set_idle(colonist, context, 0);
        }
        return;
    }
    TilePos dest = colonist->haul_destination;
    if (colonist->x != dest.x || colonist->y != dest.y) {
        set_idle(colonist, context, 0);
        return;
    }
    int added = items_add(context->items, dest.x, dest.y, colonist->carried);
    colonist->carried.count = (uint16_t)(colonist->carried.count - added);
    if (colonist->carried.count == 0) {
        colonist->carried = (ItemStack){.type = ITEM_NONE};
    }
    set_idle(colonist, context, 0); /* Drops any leftover nearby, then looks for more work. */
}

/* Whether the job the colonist is heading to or working on still exists, is
 * still its own, and is of a work type the colonist still does. */
static bool job_still_valid(const Colonist *colonist, const ColonistContext *context)
{
    TilePos target = colonist->target;
    const DesignationDef *def = designation_def((DesignationType)colonist->work_type);
    return def != NULL &&
           colonist->work_priorities[def->work_type] != WORK_PRIORITY_DISABLED &&
           designation_get(context->designations, target.x, target.y) == colonist->work_type &&
           reservations_owner(context->reservations, RESERVATION_WORK, target) ==
               context->worker_id;
}

/* Skill used to carry out an order. */
static SkillType skill_of(DesignationType type)
{
    const DesignationDef *def = designation_def(type);
    const WorkTypeDef *work = def != NULL ? work_type_def(def->work_type) : NULL;
    return work != NULL ? work->skill : SKILL_NONE;
}

static void work(Colonist *colonist, const ColonistContext *context)
{
    Designations *designations = context->designations;
    TilePos target = colonist->target;
    int distance = abs(colonist->x - target.x) + abs(colonist->y - target.y);
    DesignationType type = designation_get(designations, target.x, target.y);
    if (distance != 1 || !job_still_valid(colonist, context)) {
        set_idle(colonist, context, 0); /* Not there, or the order was cancelled. */
        return;
    }
    if (!designation_can_apply(context->map, target.x, target.y, type)) {
        designation_set(designations, context->map, target.x, target.y, DESIGNATION_NONE);
        set_idle(colonist, context, 0);
        return;
    }
    SkillType skill = skill_of(type);
    /* Products depend on the level before this tick's experience. */
    bool done = designation_add_work(designations, context->map, context->items, target.x,
                                     target.y, colonist_work_speed(colonist, skill),
                                     colonist_skill_level(colonist, skill), context->rng);
    colonist_gain_skill_xp(colonist, skill, COLONIST_WORK_XP_PER_TICK); /* Learning by doing. */
    if (done) {
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
        if ((dx == 0 && dy == 0) || !map_is_passable(context->map, goal.x, goal.y) ||
            !regions_connected(context->regions, context->map, current_tile(colonist), goal)) {
            continue; /* Blocked, or on the other side of a wall or of water. */
        }
        if (pathfinder_find(context->pathfinder, context->map, current_tile(colonist), goal,
                            (float)radius * 3.0f, &colonist->path)) {
            begin_task(colonist, context, TASK_WANDER);
            return;
        }
    }
    set_idle(colonist, context, random_idle_ticks(context->rng));
}

bool colonist_is_night(uint64_t tick)
{
    int hour = game_hour_from_tick(tick);
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
        if (!tile_has_food(context->map, context->items, colonist->target.x,
                           colonist->target.y)) {
            set_idle(colonist, context, 0); /* The fruit is gone: decide again below. */
            break;
        }
        return; /* Carried on until done. */
    case TASK_DRINK:
        return;
    case TASK_WORK:
        if (!job_still_valid(colonist, context)) {
            set_idle(colonist, context, 0); /* Cancelled on the way: decide again below. */
        }
        break;
    case TASK_HAUL:
        if (!haul_still_valid(colonist, context)) {
            set_idle(colonist, context, 0);
        }
        break;
    default:
        break;
    }

    if (try_satisfy_urgent(colonist, context, NEED_LOW_THRESHOLD)) {
        return;
    }
    bool night_sleep = context->night && rest < COLONIST_NIGHT_SLEEP_REST;
    if (rest < NEED_LOW_THRESHOLD || night_sleep) {
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
    colonist->move_progress += COLONIST_WALK_SPEED * colonist_load_speed_factor(colonist) /
                               (float)TICKS_PER_SECOND / cost;
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
        !tile_has_food(map, context->items, colonist->x, colonist->y)) {
        /* Not there, or the food is gone: look again soon. */
        set_idle(colonist, context, 0);
        return;
    }
    if (++colonist->timer < COLONIST_EAT_TICKS) {
        return;
    }

    float *food = &colonist->needs[NEED_FOOD];
    ItemStack stack = items_get(context->items, colonist->x, colonist->y);
    if (item_is_food((ItemType)stack.type)) {
        /* Items on the ground first: eat what it takes to be sated. */
        float nutrition = item_def((ItemType)stack.type)->nutrition;
        int wanted = (int)ceilf((COLONIST_SATED_FOOD - *food) / nutrition);
        int eaten = items_remove(context->items, colonist->x, colonist->y, wanted > 1 ? wanted : 1);
        *food = clampf(*food + nutrition * (float)eaten, 0.0f, 1.0f);
    } else {
        Plant *plant = &map->plants[map_index(map, colonist->x, colonist->y)];
        const PlantDef *def = plant_def((PlantType)plant->type);
        *food = clampf(*food + def->nutrition * plant->growth, 0.0f, 1.0f);
        plant->growth = PLANT_GROWTH_AFTER_PICKING;
    }

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
    case TASK_HAUL: haul(colonist, context); break;
    case TASK_SLEEP:
    case TASK_COUNT: break;
    }
}
