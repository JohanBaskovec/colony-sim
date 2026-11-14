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
    colonist->work_priorities[WORK_CLEANING] = WORK_PRIORITY_DEFAULT_CLEANING;
    colonist_set_strength(colonist, COLONIST_STRENGTH_STANDARD);
    colonist->max_carry_bulk = COLONIST_CARRY_BULK;
    colonist->station = WORKSTATION_NONE;
    colonist->site = CONSTRUCTION_NONE;
}

bool colonist_set_work_priority(Colonist *colonist, WorkType type, int priority)
{
    if ((unsigned)type >= WORK_TYPE_COUNT || !work_priority_is_valid(priority)) {
        return false;
    }
    if (colonist->work_priorities[type] != priority) {
        colonist->work_priorities[type] = (uint8_t)priority;
        /* Jobs skipped so far may now be acceptable: forget failed searches
         * (every kind of work, not food and water). */
        for (int kind = 0; kind < SEARCH_COUNT; kind++) {
            if (kind != SEARCH_FOOD && kind != SEARCH_WATER) {
                colonist->search_cooldown[kind] = 0;
                colonist->search_failures[kind] = 0;
                colonist->search_quiet[kind] = 0;
            }
        }
    }
    return true;
}

double colonist_knowledge_level(const Colonist *colonist, KnowledgeType knowledge)
{
    return (unsigned)knowledge < KNOWLEDGE_COUNT ? colonist->knowledge[knowledge]
                                                 : KNOWLEDGE_LEVEL_MIN;
}

bool colonist_set_knowledge_level(Colonist *colonist, KnowledgeType knowledge, double level)
{
    if ((unsigned)knowledge >= KNOWLEDGE_COUNT || !(level >= KNOWLEDGE_LEVEL_MIN) ||
        level > KNOWLEDGE_LEVEL_MAX) {
        return false;
    }
    colonist->knowledge[knowledge] = level;
    return true;
}

void colonist_learn_by_doing(Colonist *colonist, KnowledgeType knowledge, int ticks)
{
    if ((unsigned)knowledge >= KNOWLEDGE_COUNT || ticks <= 0) {
        return;
    }
    double *level = &colonist->knowledge[knowledge];
    *level = knowledge_learn(*level, KNOWLEDGE_DOING_RATE, (double)ticks / TICKS_PER_HOUR,
                             colonist->age);
}

bool colonist_can_do(const Colonist *colonist, DesignationType type)
{
    const DesignationDef *def = designation_def(type);
    return def != NULL &&
           designation_doable_at(type, colonist_knowledge_level(colonist, def->knowledge));
}

bool colonist_can_gather(const Colonist *colonist, GatherJob job)
{
    const GatherJobDef *def = gather_job_def(job);
    return def != NULL &&
           gather_job_doable_at(job, colonist_knowledge_level(colonist, def->knowledge));
}

/* KNOWLEDGE_NONE: anyone (workstations and recipes that need no knowledge). */
static bool knows_enough(const Colonist *colonist, KnowledgeType knowledge, float min_level)
{
    return knowledge == KNOWLEDGE_NONE ||
           colonist_knowledge_level(colonist, knowledge) >= min_level;
}

bool colonist_can_build(const Colonist *colonist, WorkstationType type)
{
    const WorkstationDef *def = workstation_def(type);
    return def != NULL && knows_enough(colonist, def->knowledge, def->min_level);
}

bool colonist_can_construct(const Colonist *colonist, StructureType type)
{
    const StructureDef *def = structure_def(type);
    return def != NULL && type != STRUCTURE_NONE &&
           knows_enough(colonist, def->knowledge, def->min_level);
}

bool colonist_can_build_site(const Colonist *colonist, const ConstructionSite *site)
{
    WorkstationType station = construction_workstation(site);
    return station != WORKSTATION_TYPE_COUNT
               ? colonist_can_build(colonist, station)
               : colonist_can_construct(colonist, (StructureType)site->type);
}

bool colonist_can_craft(const Colonist *colonist, RecipeType recipe)
{
    const RecipeDef *def = recipe_def(recipe);
    return def != NULL &&
           recipe_doable_at(recipe, colonist_knowledge_level(colonist, def->knowledge));
}

bool colonist_level_fits_bill(const Colonist *colonist, const Bill *bill)
{
    const RecipeDef *def = recipe_def((RecipeType)bill->recipe);
    return def != NULL &&
           bill_accepts_level(bill, colonist_knowledge_level(colonist, def->knowledge));
}

int colonist_tool_index(const Colonist *colonist, ItemTypeMask types)
{
    int best = -1;
    for (int i = 0; i < COLONIST_TOOL_MAX; i++) {
        ItemStack tool = colonist->tools[i];
        if (tool.count == 0 || !(types & ITEM_TYPE_BIT(tool.type))) {
            continue;
        }
        if (best < 0 || tool.quality > colonist->tools[best].quality ||
            (tool.quality == colonist->tools[best].quality &&
             tool.hit_points > colonist->tools[best].hit_points)) {
            best = i;
        }
    }
    return best;
}

bool colonist_add_tool(Colonist *colonist, ItemStack tool)
{
    if (tool.count == 0 || tool.type == ITEM_NONE ||
        colonist_tool_index(colonist, ITEM_TYPE_BIT(tool.type)) >= 0) {
        return false;
    }
    for (int i = 0; i < COLONIST_TOOL_MAX; i++) {
        if (colonist->tools[i].count == 0) {
            tool.count = 1;
            colonist->tools[i] = tool;
            colonist->tool_wear[i] = 0;
            return true;
        }
    }
    return false;
}

bool colonist_swap_tool(Colonist *colonist, ItemStack tool, ItemStack *replaced)
{
    *replaced = (ItemStack){.type = ITEM_NONE};
    int index = tool.count > 0 ? colonist_tool_index(colonist, ITEM_TYPE_BIT(tool.type)) : -1;
    if (index < 0) {
        return colonist_add_tool(colonist, tool);
    }
    *replaced = colonist->tools[index];
    tool.count = 1;
    colonist->tools[index] = tool;
    colonist->tool_wear[index] = 0;
    return true;
}

void colonist_drop_tools(Colonist *colonist, Items *items, const Map *map)
{
    for (int i = 0; i < COLONIST_TOOL_MAX; i++) {
        if (colonist->tools[i].count > 0) {
            items_drop(items, map, colonist->x, colonist->y, colonist->tools[i]);
            colonist->tools[i] = (ItemStack){.type = ITEM_NONE};
            colonist->tool_wear[i] = 0;
        }
    }
}

/* Index of the tool the colonist works with for a use among `allowed`, or -1
 * for bare hands. A required tool (recipes) is used even when no faster than
 * bare hands. */
static int working_tool(const Colonist *colonist, ToolUse use, ItemTypeMask allowed,
                        bool required)
{
    if (use == TOOL_USE_NONE) {
        return -1;
    }
    return tool_best(colonist->tools, COLONIST_TOOL_MAX, use, allowed, !required);
}

float colonist_tool_factor(const Colonist *colonist, ToolUse use, ItemTypeMask allowed)
{
    int index = working_tool(colonist, use, allowed, false);
    return index >= 0 ? tool_speed_factor(use, colonist->tools[index])
                      : tool_type_factor(use, ITEM_NONE);
}

/* One tick of work with a carried tool: it loses a hit point every
 * TOOL_WEAR_TICKS, and breaks at 0. */
static void wear_tool(Colonist *colonist, int index)
{
    if (index < 0 || colonist->tools[index].count == 0) {
        return;
    }
    if (++colonist->tool_wear[index] < TOOL_WEAR_TICKS) {
        return;
    }
    colonist->tool_wear[index] = 0;
    if (tool_wear(&colonist->tools[index])) {
        colonist->tools_broken++;
    }
}

/* Speed factor of one tick of a kind of work with the best carried tool for
 * it (bare hands when none helps), which wears. */
static float work_with_tool(Colonist *colonist, ToolUse use)
{
    int index = working_tool(colonist, use, ~(ItemTypeMask)0, false);
    float factor = index >= 0 ? tool_speed_factor(use, colonist->tools[index])
                              : tool_type_factor(use, ITEM_NONE);
    wear_tool(colonist, index);
    return factor;
}

/* Weight and bulk of the carried tools. */
static void tools_load(const Colonist *colonist, float *weight, float *bulk)
{
    *weight = 0.0f;
    *bulk = 0.0f;
    for (int i = 0; i < COLONIST_TOOL_MAX; i++) {
        ItemStack tool = colonist->tools[i];
        if (tool.count > 0) {
            *weight += item_stack_weight((ItemType)tool.type, tool.count);
            *bulk += item_def((ItemType)tool.type)->bulk * (float)tool.count;
        }
    }
}

float colonist_work_speed(const Colonist *colonist, KnowledgeType knowledge)
{
    float speed = COLONIST_WORK_SPEED;
    if ((unsigned)knowledge < KNOWLEDGE_COUNT) {
        speed *= knowledge_work_speed(colonist->knowledge[knowledge]);
    }
    if (colonist->ground_sleep_ticks > 0) {
        speed *= COLONIST_GROUND_SLEEP_WORK_FACTOR;
    }
    return speed;
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
    float tools_weight, tools_bulk;
    tools_load(colonist, &tools_weight, &tools_bulk);
    return item_stack_weight((ItemType)colonist->carried.type, colonist->carried.count) +
           tools_weight;
}

int colonist_carry_capacity(const Colonist *colonist, ItemType type)
{
    const ItemDef *def = item_def(type);
    if (def == NULL || type == ITEM_NONE) {
        return 0;
    }
    float tools_weight, tools_bulk;
    tools_load(colonist, &tools_weight, &tools_bulk);
    float by_weight = floorf((colonist->max_carry_weight - tools_weight) / def->weight);
    float by_bulk = floorf((colonist->max_carry_bulk - tools_bulk) / def->bulk);
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

static ColonistAction gather_action(GatherJob job)
{
    return job == GATHER_JOB_CHOP ? COLONIST_ACTION_STRIKE : COLONIST_ACTION_GATHER;
}

static ColonistAction craft_action(const Colonist *colonist, ItemType *tool)
{
    const RecipeDef *def = recipe_def((RecipeType)colonist->recipe);
    if (def == NULL) {
        return COLONIST_ACTION_NONE;
    }
    int index = colonist_tool_index(colonist, def->tools[0]);
    *tool = index >= 0 ? (ItemType)colonist->tools[index].type : ITEM_NONE;
    /* Stones are knapped with blows of a hammerstone; anything else is cut. */
    return (def->tools[0] & ITEM_TYPE_BIT(ITEM_COBBLE)) ? COLONIST_ACTION_STRIKE
                                                        : COLONIST_ACTION_CUT;
}

ColonistAction colonist_action(const Colonist *colonist, ItemType *tool)
{
    ItemType unused;
    if (tool == NULL) {
        tool = &unused;
    }
    *tool = ITEM_NONE;
    if (colonist_is_moving(colonist)) {
        return COLONIST_ACTION_NONE;
    }
    bool at_station = colonist->station_phase == STATION_JOB_WORK;
    switch (colonist->task) {
    case TASK_EAT:
    case TASK_DRINK: return COLONIST_ACTION_EAT;
    case TASK_WORK:
        if (abs(colonist->x - colonist->target.x) + abs(colonist->y - colonist->target.y) != 1) {
            return COLONIST_ACTION_NONE; /* Not at the job yet. */
        }
        return colonist->gathering ? gather_action((GatherJob)colonist->work_type)
                                   : COLONIST_ACTION_STRIKE;
    case TASK_CONSTRUCT: return at_station ? COLONIST_ACTION_STRIKE : COLONIST_ACTION_NONE;
    case TASK_CRAFT: return at_station ? craft_action(colonist, tool) : COLONIST_ACTION_NONE;
    case TASK_CLEAN: /* Picking up chips and shavings. */
        return colonist->x == colonist->target.x && colonist->y == colonist->target.y
                   ? COLONIST_ACTION_GATHER
                   : COLONIST_ACTION_NONE;
    case TASK_IDLE:
    case TASK_WANDER:
    case TASK_HAUL:
    case TASK_SUPPLY:
    case TASK_SLEEP:
    case TASK_COUNT: break;
    }
    return COLONIST_ACTION_NONE;
}

ColonistExpression colonist_expression(const Colonist *colonist)
{
    NeedLevel worst = NEED_LEVEL_OK;
    bool content = true;
    for (int need = 0; need < NEED_COUNT; need++) {
        NeedLevel level = need_level(colonist->needs[need]);
        if (level > worst) {
            worst = level;
        }
        if (colonist->needs[need] < NEED_CONTENT_THRESHOLD) {
            content = false;
        }
    }
    if (worst == NEED_LEVEL_CRITICAL) {
        return EXPRESSION_ANGRY;
    }
    if (worst == NEED_LEVEL_LOW) {
        return EXPRESSION_SAD;
    }
    return content ? EXPRESSION_SMILING : EXPRESSION_NEUTRAL;
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
        [TASK_CRAFT] = N_("colonist.task.craft"),
        [TASK_CONSTRUCT] = N_("colonist.task.construct"),
        [TASK_SUPPLY] = N_("colonist.task.supply"),
        [TASK_CLEAN] = N_("colonist.task.clean"),
    };
    return (unsigned)task < TASK_COUNT ? KEYS[task] : KEYS[TASK_IDLE];
}

const char *colonist_activity_key(const Colonist *colonist)
{
    if (colonist->task == TASK_WORK && colonist->gathering) {
        const GatherJobDef *def = gather_job_def((GatherJob)colonist->work_type);
        if (def != NULL) {
            return def->activity_key;
        }
    }
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

/* Gives up the materials a colonist promised to bring to a site. */
static void release_promise(Colonist *colonist, const ColonistContext *context)
{
    if (colonist->supply_promised > 0) {
        construction_release_promise(context->constructions, colonist->site,
                                     colonist->site_number, colonist->supply_slot,
                                     colonist->supply_promised);
        colonist->supply_promised = 0;
    }
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
    release_promise(colonist, context);
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

/* Whether a work search of this kind may run: its delay is over, or what it
 * looks for changed since it failed (`stamp` then, `now` now), once
 * COLONIST_SEARCH_MIN_RETRY_TICKS have gone by. */
static bool retry_allowed(const Colonist *colonist, SearchKind kind, uint32_t stamp,
                          uint32_t now)
{
    return search_allowed(colonist, kind) || (stamp != now && colonist->search_quiet[kind] <= 0);
}

static void search_failed(Colonist *colonist, SearchKind kind)
{
    int backoff = colonist->search_failures[kind];
    colonist->search_cooldown[kind] = COLONIST_SEARCH_RETRY_TICKS << backoff;
    colonist->search_quiet[kind] = COLONIST_SEARCH_MIN_RETRY_TICKS;
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

/*
 * Whether some tile of the region of `from` passes `predicate`: a scan of the
 * map before a search for the nearest such tile, much cheaper than a search
 * that explores the whole area and finds nothing (every stack of a material
 * taken, no free bed left). True without up-to-date regions: search anyway.
 */
static bool any_tile_reachable(const ColonistContext *context, TilePos from,
                               TilePredicate predicate, void *userdata)
{
    const Map *map = context->map;
    uint32_t region = regions_at(context->regions, map, from.x, from.y);
    if (region == REGION_NONE) {
        return true;
    }
    /* From the row of `from` outwards: what is near is found at once. */
    for (int step = 0; step < 2 * map->height; step++) {
        int y = row_outwards(from.y, step, map->height);
        if (y < 0) {
            continue;
        }
        const uint32_t *ids = &context->regions->id[(size_t)y * (size_t)map->width];
        for (int x = 0; x < map->width; x++) {
            if (ids[x] == region && predicate(map, x, y, userdata)) {
                return true;
            }
        }
    }
    return false;
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

/* Search filter: the head of a bed nobody else owns, sleeps in or walks to. */
static bool is_free_bed(const Map *map, int x, int y, void *userdata)
{
    const ColonistContext *context = userdata;
    uint8_t owner = beds_owner(context->beds, x, y);
    return map_structure_is_origin(map, x, y) && structure_is_bed(map_structure(map, x, y)) &&
           (owner == BED_NO_OWNER || owner == context->worker_id) &&
           !reservations_taken_by_other(context->reservations, RESERVATION_BED,
                                        (TilePos){(int16_t)x, (int16_t)y}, context->worker_id);
}

typedef struct OwnBedSearch {
    const ColonistContext *context;
    TilePos bed;
} OwnBedSearch;

static bool is_own_bed(const Map *map, int x, int y, void *userdata)
{
    const OwnBedSearch *search = userdata;
    return x == search->bed.x && y == search->bed.y &&
           is_free_bed(map, x, y, (void *)search->context);
}

/* Goes to sleep in its own bed, else in the nearest free one (which becomes
 * its own), else where the colonist stands. */
static void start_sleeping(Colonist *colonist, const ColonistContext *context)
{
    begin_task(colonist, context, TASK_SLEEP);
    path_clear(&colonist->path);
    if (context->map->structure_counts[STRUCTURE_GRASS_BED] == 0) {
        return;
    }
    TilePos bed;
    OwnBedSearch own = {context, {-1, -1}};
    bool found = false;
    if (beds_owned(context->beds, context->worker_id, &own.bed)) {
        found = pathfinder_find_nearest(context->pathfinder, context->map, current_tile(colonist),
                                        is_own_bed, &own, COLONIST_BED_SEARCH_MAX_COST,
                                        &colonist->path, &bed);
    }
    if (!found &&
        any_tile_reachable(context, current_tile(colonist), is_free_bed, (void *)context) &&
        pathfinder_find_nearest(context->pathfinder, context->map, current_tile(colonist),
                                is_free_bed, (void *)context, COLONIST_BED_SEARCH_MAX_COST,
                                &colonist->path, &bed)) {
        found = true;
        beds_assign(context->beds, context->worker_id, bed.x, bed.y);
    }
    if (found) {
        colonist->target = bed;
        reservations_add(context->reservations, RESERVATION_BED, bed, context->worker_id);
    }
}

/* Rest gain multiplier where the colonist lies. */
static float rest_factor(const Colonist *colonist, const Map *map)
{
    StructureType bed = map_structure(map, colonist->x, colonist->y);
    return structure_is_bed(bed) ? structure_def(bed)->rest_factor : COLONIST_GROUND_REST_FACTOR;
}

/* ---- Work ------------------------------------------------------------------ */

static const int16_t NEIGHBOR_DX[4] = {0, 1, 0, -1};
static const int16_t NEIGHBOR_DY[4] = {-1, 0, 1, 0};

/* Search filter for jobs: work types allowed at the priority being searched. */
typedef struct WorkSearch {
    const Designations *designations;
    /* Gathering zone tiles instead of designations, for these jobs. */
    bool zones;
    GatherJobMask jobs;
    int zone_priority; /* Only tiles of zones of this priority. */
    const GatherZones *gather_zones;
    const Map *map;
    const Reservations *reservations;
    const Colonist *worker; /* Only jobs its knowledge allows. */
    WorkTypeMask mask;
    uint8_t self;
} WorkSearch;

/* Gathering jobs a colonist may do: work type among `mask`, and knowledge. */
static GatherJobMask gather_jobs_for(const Colonist *colonist, WorkTypeMask mask)
{
    GatherJobMask jobs = 0;
    for (int job = 0; job < GATHER_JOB_COUNT; job++) {
        if ((mask & WORK_TYPE_BIT(gather_job_def((GatherJob)job)->work_type)) &&
            colonist_can_gather(colonist, (GatherJob)job)) {
            jobs |= GATHER_JOB_BIT(job);
        }
    }
    return jobs;
}

/* Work types the colonist does at all. */
static WorkTypeMask enabled_work_types(const Colonist *colonist)
{
    WorkTypeMask mask = 0;
    for (int type = 0; type < WORK_TYPE_COUNT; type++) {
        if (colonist->work_priorities[type] != WORK_PRIORITY_DISABLED) {
            mask |= WORK_TYPE_BIT(type);
        }
    }
    return mask;
}

static bool is_open_job(const WorkSearch *search, int x, int y)
{
    if (search->zones) {
        if (gather_zone_priority_at(search->gather_zones, x, y) != search->zone_priority ||
            gather_tile_job(search->gather_zones, search->map, x, y, search->jobs) ==
                GATHER_JOB_NONE) {
            return false;
        }
    } else {
        DesignationType type = designation_get(search->designations, x, y);
        if (type == DESIGNATION_NONE ||
            !(search->mask & WORK_TYPE_BIT(designation_def(type)->work_type)) ||
            !colonist_can_do(search->worker, type)) {
            return false;
        }
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
    /* Designated tiles, or tiles of the zones that want something. */
    int remaining = search->zones ? search->gather_zones->wanting_cells : designations->count;
    for (int y = 0; y < height && remaining > 0; y++) {
        const uint8_t *types = search->zones
                                   ? &search->gather_zones->zone[(size_t)y * (size_t)width]
                                   : &designations->type[(size_t)y * (size_t)width];
        const uint32_t *ids = &regions->id[(size_t)y * (size_t)width];
        for (int x = 0; x < width; x++) {
            if (types[x] == 0 || (search->zones && search->gather_zones->list[types[x] - 1]
                                                           .wanted == 0)) {
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
    return context->designations->revision +
           context->reservations->releases_by_kind[RESERVATION_WORK];
}

static uint32_t gather_stamp(const ColonistContext *context)
{
    return context->gather_zones->revision +
           context->reservations->releases_by_kind[RESERVATION_WORK];
}

static uint32_t clean_stamp(const ColonistContext *context)
{
    return context->dirt->revision + context->reservations->releases_by_kind[RESERVATION_CLEAN];
}

/* Search filter: a dirty tile nobody else is cleaning. */
typedef struct CleanSearch {
    const Dirt *dirt;
    const Reservations *reservations;
    uint8_t self;
} CleanSearch;

static bool is_dirty_tile(const Map *map, int x, int y, void *userdata)
{
    (void)map;
    const CleanSearch *search = userdata;
    return dirt_at(search->dirt, x, y) > 0 &&
           !reservations_taken_by_other(search->reservations, RESERVATION_CLEAN,
                                        (TilePos){(int16_t)x, (int16_t)y}, search->self);
}

/* Walks to the nearest dirty tile to clean it. */
static bool try_clean_job(Colonist *colonist, const ColonistContext *context)
{
    CleanSearch search = {context->dirt, context->reservations, context->worker_id};
    TilePos found;
    if (!pathfinder_find_nearest(context->pathfinder, context->map, current_tile(colonist),
                                 is_dirty_tile, &search, COLONIST_WORK_SEARCH_MAX_COST,
                                 &colonist->path, &found)) {
        return false;
    }
    begin_task(colonist, context, TASK_CLEAN);
    colonist->target = found;
    reservations_add(context->reservations, RESERVATION_CLEAN, found, context->worker_id);
    return true;
}

static bool clean_job_still_valid(const Colonist *colonist, const ColonistContext *context)
{
    return colonist->work_priorities[WORK_CLEANING] != WORK_PRIORITY_DISABLED &&
           dirt_at(context->dirt, colonist->target.x, colonist->target.y) > 0;
}

/* Cleans the tile it stands on, 1 % at a time. */
static void clean(Colonist *colonist, const ColonistContext *context)
{
    TilePos target = colonist->target;
    if (colonist->x != target.x || colonist->y != target.y ||
        !clean_job_still_valid(colonist, context)) {
        set_idle(colonist, context, 0);
        return;
    }
    if (++colonist->timer < COLONIST_CLEAN_TICKS_PER_PERCENT) {
        return;
    }
    colonist->timer = 0;
    if (dirt_clean(context->dirt, target.x, target.y, 1) == 0) {
        set_idle(colonist, context, 0); /* The next dirty tile is probably next to it. */
    }
}

static HaulWorld haul_world(const ColonistContext *context)
{
    HaulWorld world = {context->map,         context->items,      context->stockpiles,
                       context->reservations, context->pathfinder, context->worker_id,
                       context->regions,      REGION_NONE,         0};
    return world;
}

static uint32_t haul_stamp(const ColonistContext *context)
{
    const uint32_t *releases = context->reservations->releases_by_kind;
    return context->stockpiles->revision + context->items->additions +
           releases[RESERVATION_HAUL_SOURCE] + releases[RESERVATION_STORAGE];
}

static uint32_t station_stamp(const ColonistContext *context)
{
    return context->workstations->revision + context->items->additions +
           context->reservations->releases;
}

/* Designation jobs of the given work types, or gathering zone tiles
 * (`zones`): nearest one. */
static bool try_designation_job(Colonist *colonist, const ColonistContext *context,
                                WorkTypeMask mask, bool zones)
{
    const Designations *designations = context->designations;
    GatherJobMask jobs = zones ? gather_jobs_for(colonist, mask) : 0;
    if (zones ? jobs == 0 || context->gather_zones->wanting_cells == 0
              : designations_count_for_work(designations, mask) == 0) {
        return false;
    }
    WorkSearch search = {.designations = designations,
                         .zones = zones,
                         .jobs = jobs,
                         .gather_zones = context->gather_zones,
                         .map = context->map,
                         .reservations = context->reservations,
                         .worker = colonist,
                         .mask = mask,
                         .self = context->worker_id};
    /* Zones of the highest priority first, whatever the distance. */
    uint32_t priorities = zones ? gather_zones_wanting_priorities(context->gather_zones) : 1u;
    uint32_t region = current_region(colonist, context);
    TilePos spot, target;
    bool found = false;
    for (int priority = 0; priority < 32 && !found; priority++) {
        if (!(priorities & (1u << (unsigned)priority))) {
            continue;
        }
        search.zone_priority = priority;
        /* First make sure some open job can be reached at all: with orders deep
         * inside rock, across water, or all taken by others, the search would
         * explore the whole area for nothing. The path is untouched on failure:
         * a wanderer keeps walking. */
        found = open_job_reachable(&search, context, region) &&
                pathfinder_find_nearest(context->pathfinder, context->map,
                                        current_tile(colonist), is_work_spot, &search,
                                        COLONIST_WORK_SEARCH_MAX_COST, &colonist->path, &spot);
    }
    if (!found) {
        return false;
    }
    /* The search only stops on a spot next to an open job. */
    find_adjacent_job(&search, spot.x, spot.y, &target);
    begin_task(colonist, context, TASK_WORK);
    colonist->target = target;
    colonist->work_type =
        (uint8_t)(zones ? gather_tile_job(context->gather_zones, context->map, target.x,
                                          target.y, jobs)
                        : designation_get(designations, target.x, target.y));
    colonist->gathering = zones;
    reservations_add(context->reservations, RESERVATION_WORK, target, context->worker_id);
    return true;
}

static bool try_haul_job(Colonist *colonist, const ColonistContext *context)
{
    HaulWorld world = haul_world(context);
    world.region = current_region(colonist, context);
    world.from_y = colonist->y;
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

static bool try_station_job(Colonist *colonist, const ColonistContext *context);
static bool site_still_wanted(const ColonistContext *context, const ConstructionSite *site);
static bool try_construction_job(Colonist *colonist, const ColonistContext *context);
static bool try_supply_job(Colonist *colonist, const ColonistContext *context);
static uint32_t construction_stamp(const ColonistContext *context);

/*
 * Takes the nearest job of the highest priority level that has one. Within a
 * level, work types are tried in the order of the priorities grid's columns
 * (WorkType order): for each, its orders, then its other jobs (construction
 * sites, bills, gathering zones, hauling, cleaning). Each search kind
 * backs off on failure until its stamp shows that something changed.
 */
static bool try_start_work(Colonist *colonist, const ColonistContext *context)
{
    int open_jobs = context->designations->count -
                    context->reservations->count_by_kind[RESERVATION_WORK];
    bool designations_allowed =
        open_jobs > 0 && retry_allowed(colonist, SEARCH_WORK, colonist->work_search_stamp,
                                       work_stamp(context));
    bool hauling_allowed =
        retry_allowed(colonist, SEARCH_HAUL, colonist->haul_search_stamp, haul_stamp(context));
    bool stations_allowed = retry_allowed(colonist, SEARCH_STATION,
                                          colonist->station_search_stamp, station_stamp(context));
    bool constructions_allowed =
        context->constructions->used_count > 0 &&
        retry_allowed(colonist, SEARCH_CONSTRUCTION, colonist->construction_search_stamp,
                      construction_stamp(context));
    bool searched_constructions = false;
    bool supply_allowed =
        context->constructions->used_count > 0 &&
        retry_allowed(colonist, SEARCH_SUPPLY, colonist->supply_search_stamp,
                      construction_stamp(context));
    bool searched_supply = false;
    bool gathering_allowed =
        context->gather_zones->wanting_cells > 0 &&
        retry_allowed(colonist, SEARCH_GATHER, colonist->gather_search_stamp,
                      gather_stamp(context));
    bool searched_designations = false, searched_hauling = false, searched_stations = false;
    bool searched_gathering = false;
    int open_cleaning = context->dirt->dirty_count -
                        context->reservations->count_by_kind[RESERVATION_CLEAN];
    bool cleaning_allowed =
        open_cleaning > 0 && retry_allowed(colonist, SEARCH_CLEAN, colonist->clean_search_stamp,
                                           clean_stamp(context));
    bool searched_cleaning = false;
    /* Every searcher below returns true once the colonist has a job; it
     * resets the backoff of its kind. */
#define FOUND(kind)                                                                            \
    do {                                                                                       \
        colonist->search_failures[kind] = 0;                                                   \
        colonist->search_cooldown[kind] = 0;                                                   \
        colonist->search_quiet[kind] = 0;                                                      \
        return true;                                                                           \
    } while (0)

    for (int priority = WORK_PRIORITY_HIGHEST; priority <= WORK_PRIORITY_LOWEST; priority++) {
        WorkTypeMask mask = work_types_at_priority(colonist->work_priorities, priority);
        /* At the same priority, work types are tried in the order of the
         * columns of the priorities grid, from left to right. */
        for (int type = 0; type < WORK_TYPE_COUNT; type++) {
            if (!(mask & WORK_TYPE_BIT(type))) {
                continue;
            }
            WorkTypeMask bit = WORK_TYPE_BIT(type);
            /* Orders (mining, taking structures apart...) of this work type. */
            if (designations_allowed && designations_count_for_work(context->designations, bit)) {
                searched_designations = true;
                if (try_designation_job(colonist, context, bit, false)) {
                    FOUND(SEARCH_WORK);
                }
            }
            switch ((WorkType)type) {
            case WORK_CONSTRUCTION:
                if (constructions_allowed) {
                    searched_constructions = true;
                    if (try_construction_job(colonist, context)) {
                        FOUND(SEARCH_CONSTRUCTION);
                    }
                }
                break;
            case WORK_CRAFTING:
                if (stations_allowed) {
                    searched_stations = true;
                    if (try_station_job(colonist, context)) {
                        FOUND(SEARCH_STATION);
                    }
                }
                break;
            case WORK_HAULING:
                /* Construction sites waiting for materials first. */
                if (supply_allowed) {
                    searched_supply = true;
                    if (try_supply_job(colonist, context)) {
                        FOUND(SEARCH_SUPPLY);
                    }
                }
                if (hauling_allowed) {
                    searched_hauling = true;
                    if (try_haul_job(colonist, context)) {
                        FOUND(SEARCH_HAUL);
                    }
                }
                break;
            case WORK_CLEANING:
                if (cleaning_allowed) {
                    searched_cleaning = true;
                    if (try_clean_job(colonist, context)) {
                        FOUND(SEARCH_CLEAN);
                    }
                }
                break;
            default: /* Mining, woodcutting, gathering: jobs in gathering zones. */
                if (gathering_allowed && gather_jobs_for(colonist, bit) != 0) {
                    searched_gathering = true;
                    if (try_designation_job(colonist, context, bit, true)) {
                        FOUND(SEARCH_GATHER);
                    }
                }
                break;
            }
        }
    }
#undef FOUND
    if (searched_designations) {
        search_failed(colonist, SEARCH_WORK);
        colonist->work_search_stamp = work_stamp(context);
    }
    if (searched_hauling) {
        search_failed(colonist, SEARCH_HAUL);
        colonist->haul_search_stamp = haul_stamp(context);
    }
    if (searched_stations) {
        search_failed(colonist, SEARCH_STATION);
        colonist->station_search_stamp = station_stamp(context);
    }
    if (searched_constructions) {
        search_failed(colonist, SEARCH_CONSTRUCTION);
        colonist->construction_search_stamp = construction_stamp(context);
    }
    if (searched_supply) {
        search_failed(colonist, SEARCH_SUPPLY);
        colonist->supply_search_stamp = construction_stamp(context);
    }
    if (searched_gathering) {
        search_failed(colonist, SEARCH_GATHER);
        colonist->gather_search_stamp = gather_stamp(context);
    }
    if (searched_cleaning) {
        search_failed(colonist, SEARCH_CLEAN);
        colonist->clean_search_stamp = clean_stamp(context);
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
 * still its own, and is of a work type the colonist still does and knows. */
static bool job_still_valid(const Colonist *colonist, const ColonistContext *context)
{
    TilePos target = colonist->target;
    bool own = reservations_owner(context->reservations, RESERVATION_WORK, target) ==
               context->worker_id;
    if (colonist->gathering) {
        GatherJob job = (GatherJob)colonist->work_type;
        return own && (unsigned)job < GATHER_JOB_COUNT &&
               (gather_jobs_for(colonist, enabled_work_types(colonist)) & GATHER_JOB_BIT(job)) &&
               gather_job_available(context->gather_zones, context->map, target.x, target.y, job);
    }
    const DesignationDef *def = designation_def((DesignationType)colonist->work_type);
    return def != NULL &&
           colonist->work_priorities[def->work_type] != WORK_PRIORITY_DISABLED &&
           colonist_can_do(colonist, (DesignationType)colonist->work_type) &&
           designation_get(context->designations, target.x, target.y) == colonist->work_type &&
           reservations_owner(context->reservations, RESERVATION_WORK, target) ==
               context->worker_id;
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
    if (colonist->gathering) {
        GatherJob job = (GatherJob)colonist->work_type;
        KnowledgeType knowledge = gather_job_def(job)->knowledge;
        float speed = colonist_work_speed(colonist, knowledge) *
                      work_with_tool(colonist, gather_job_def(job)->tool_use);
        /* Products depend on the level before this tick's learning. */
        bool done = gather_zone_add_work(context->gather_zones, context->map, context->items,
                                         target.x, target.y, job, speed,
                                         colonist_knowledge_level(colonist, knowledge),
                                         context->rng);
        colonist_learn_by_doing(colonist, knowledge, 1);
        /* After each job, choose again: a zone may keep a settler busy
         * forever, and a more important job may be waiting. */
        if (done) {
            set_idle(colonist, context, 0);
        }
        return;
    }
    if (!designation_can_apply(context->map, target.x, target.y, type)) {
        designation_set(designations, context->map, target.x, target.y, DESIGNATION_NONE);
        set_idle(colonist, context, 0);
        return;
    }
    KnowledgeType knowledge = designation_def(type)->knowledge;
    /* Products depend on the level before this tick's learning. */
    bool done = designation_add_work(designations, context->map, context->items, target.x,
                                     target.y, colonist_work_speed(colonist, knowledge),
                                     colonist_knowledge_level(colonist, knowledge), context->rng);
    colonist_learn_by_doing(colonist, knowledge, 1);
    int site = construction_at(context->constructions, target.x, target.y);
    const ConstructionSite *refresh = construction_get(context->constructions, site);
    if (done && type == DESIGNATION_DECONSTRUCT && refresh != NULL && refresh->refresh &&
        !site_still_wanted(context, refresh)) {
        /* A refresh site on the structure taken apart: its materials back. */
        ItemStack returned[STRUCTURE_MATERIAL_SLOTS];
        int count = constructions_remove(context->constructions, site, returned);
        for (int i = 0; i < count; i++) {
            items_drop(context->items, context->map, target.x, target.y, returned[i]);
        }
    }
    if (done) {
        set_idle(colonist, context, 0); /* Look for the next job right away. */
    }
}

/* ---- Workstations: building and crafting ---------------------------------- */

/* The station of the current job, NULL when it is gone (or replaced). */
static Workstation *job_station(const Colonist *colonist, const ColonistContext *context)
{
    Workstation *station = workstation_get(context->workstations, colonist->station);
    return station != NULL && station->number == colonist->station_number ? station : NULL;
}

static TilePos station_tile(const Workstation *station)
{
    return (TilePos){station->x, station->y};
}

/* Whether some item of these types lies anywhere (a cheap first check). */
static bool items_exist(const Items *items, ItemTypeMask types, int count)
{
    for (int type = 1; type < ITEM_TYPE_COUNT; type++) {
        if ((types & ITEM_TYPE_BIT(type)) && items->totals[type] >= count) {
            return true;
        }
    }
    return false;
}

static int stored_count(const ColonistContext *context, const Bill *bill)
{
    if (bill->mode != BILL_UNTIL_STOCK) {
        return 0;
    }
    return (int)context->stock[recipe_def((RecipeType)bill->recipe)->product];
}

/* Whether a tool of each kind the recipe needs is carried or lies somewhere. */
static bool tools_obtainable(const Colonist *colonist, const ColonistContext *context,
                             const RecipeDef *def)
{
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        ItemTypeMask types = def->tools[slot];
        if (types != 0 && colonist_tool_index(colonist, types) < 0 &&
            !items_exist(context->items, types, 1)) {
            return false;
        }
    }
    return true;
}

/*
 * First bill of a built station the colonist could work through now: it
 * wants doing, the colonist knows the recipe and has a level the bill accepts,
 * the tools and an input can be found (cheap checks), it is not paused. While
 * a craft is in progress on the station, only bills of its recipe qualify,
 * paused or not: it is finished first. Returns -1 if none.
 */
static int choose_bill(const Colonist *colonist, const ColonistContext *context,
                       const Workstation *station)
{
    bool in_progress = station->work_input.count > 0;
    for (int index = 0; index < station->bill_count; index++) {
        const Bill *bill = &station->bills[index];
        RecipeType recipe = (RecipeType)bill->recipe;
        const RecipeDef *def = recipe_def(recipe);
        if ((in_progress && station->work_recipe != recipe) || (!in_progress && bill->paused) ||
            !colonist_can_craft(colonist, recipe) || !colonist_level_fits_bill(colonist, bill) ||
            !bill_wants_more(bill, stored_count(context, bill)) ||
            !tools_obtainable(colonist, context, def)) {
            continue;
        }
        /* The tools still to fetch may be of the input's type (a cobble to
         * knap with): they must leave enough to work on. */
        ItemTypeMask to_fetch[RECIPE_TOOL_MAX] = {0};
        for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
            if (def->tools[slot] != 0 && colonist_tool_index(colonist, def->tools[slot]) < 0) {
                to_fetch[slot] = def->tools[slot];
            }
        }
        if (!in_progress && !bill_has_input(bill, context->items->totals, to_fetch)) {
            continue;
        }
        return index;
    }
    return -1;
}

/* Search filter for stations with a bill the worker can take. */
typedef struct StationSearch {
    const ColonistContext *context;
    const Colonist *worker;
} StationSearch;

static bool is_station_job(const Map *map, int x, int y, void *userdata)
{
    (void)map;
    const StationSearch *search = userdata;
    const ColonistContext *context = search->context;
    const Workstation *station = workstation_get_const(
        context->workstations, workstation_at(context->workstations, x, y));
    if (station == NULL ||
        reservations_taken_by_other(context->reservations, RESERVATION_STATION,
                                    station_tile(station), context->worker_id)) {
        return false;
    }
    return choose_bill(search->worker, context, station) >= 0;
}

/* Cheap check before a search: is there any station with bills? */
static bool station_jobs_exist(const ColonistContext *context)
{
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        const Workstation *station = &context->workstations->list[index];
        if (station->used && station->bill_count > 0) {
            return true;
        }
    }
    return false;
}

/* Search filter for something to pick up: types, grade, count, matching the
 * load already in hand, and not about to be taken by someone else. */
typedef struct FetchSearch {
    const Items *items;
    const Reservations *reservations;
    uint8_t self;
    ItemTypeMask types;
    int min_quality;
    int min_count;
    ItemStack match; /* Count 0: anything. */
    /* Tools only better than this speed factor for `use` (0: any). */
    ToolUse use;
    float min_factor;
} FetchSearch;

static bool is_fetchable(const Map *map, int x, int y, void *userdata)
{
    (void)map;
    const FetchSearch *search = userdata;
    ItemStack stack = items_get(search->items, x, y);
    return stack.type != ITEM_NONE && (search->types & ITEM_TYPE_BIT(stack.type)) &&
           stack.quality >= search->min_quality && stack.count >= search->min_count &&
           (search->match.count == 0 || item_stacks_match(stack, search->match)) &&
           (search->min_factor <= 0.0f ||
            tool_speed_factor(search->use, stack) > search->min_factor) &&
           !reservations_taken_by_other(search->reservations, RESERVATION_HAUL_SOURCE,
                                        (TilePos){(int16_t)x, (int16_t)y}, search->self);
}

/* Heads for a stack found by `search`, looked for around `from` within
 * `max_cost`. */
static bool start_fetch(Colonist *colonist, const ColonistContext *context, FetchSearch *search,
                        TilePos from, float max_cost, bool tool)
{
    TilePos found;
    if (!any_tile_reachable(context, from, is_fetchable, search) ||
        !pathfinder_find_nearest(context->pathfinder, context->map, from, is_fetchable, search,
                                 max_cost, NULL, &found) ||
        !pathfinder_find(context->pathfinder, context->map, current_tile(colonist), found,
                         COLONIST_WORK_SEARCH_MAX_COST, &colonist->path)) {
        return false;
    }
    reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE,
                              context->worker_id);
    reservations_add(context->reservations, RESERVATION_HAUL_SOURCE, found, context->worker_id);
    colonist->fetch = found;
    colonist->fetch_tool = tool;
    colonist->station_phase = STATION_JOB_FETCH;
    colonist->path_index = 0;
    return true;
}

static FetchSearch fetch_search(const ColonistContext *context, ItemTypeMask types)
{
    FetchSearch search = {context->items,      context->reservations, context->worker_id,
                          types,               ITEM_QUALITY_CRUDE,    1,
                          {.type = ITEM_NONE}, TOOL_USE_NONE,         0.0f};
    return search;
}

/* Input of a craft: the recipe's input types in order of preference, among
 * those the bill allows, near the station. */
static bool fetch_input(Colonist *colonist, const ColonistContext *context,
                        const Workstation *station, const Bill *bill)
{
    const RecipeDef *def = recipe_def((RecipeType)bill->recipe);
    for (int i = 0; i < RECIPE_INPUT_ALTERNATIVES; i++) {
        ItemType type = def->inputs[i];
        if (type == ITEM_NONE || !(bill->inputs & ITEM_TYPE_BIT(type))) {
            continue;
        }
        FetchSearch search = fetch_search(context, ITEM_TYPE_BIT(type));
        search.min_quality = def->input_min_quality;
        search.min_count = def->input_count - colonist->carried.count;
        search.match = colonist->carried;
        if (start_fetch(colonist, context, &search, station_tile(station),
                        (float)bill->input_radius, false)) {
            return true;
        }
    }
    return false;
}

/* Heads for the station: the work starts on arrival. */
static bool go_to_station(Colonist *colonist, const ColonistContext *context,
                          const Workstation *station)
{
    reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE,
                              context->worker_id);
    colonist->station_phase = STATION_JOB_GO;
    colonist->path_index = 0;
    TilePos tile = station_tile(station);
    if (colonist->x == tile.x && colonist->y == tile.y) {
        path_clear(&colonist->path);
        return true;
    }
    return pathfinder_find(context->pathfinder, context->map, current_tile(colonist), tile,
                           COLONIST_WORK_SEARCH_MAX_COST, &colonist->path);
}

/* Before a craft, a tool better than the carried one lying close by: worth a
 * short detour (the carried one is left where the better one lay). */
static bool fetch_better_tool(Colonist *colonist, const ColonistContext *context,
                              const RecipeDef *def)
{
    float best_possible = tool_best_possible_factor(def->tool_use);
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        if (def->tools[slot] == 0) {
            continue;
        }
        int index = working_tool(colonist, def->tool_use, def->tools[slot], true);
        if (index < 0) {
            continue;
        }
        float factor = tool_speed_factor(def->tool_use, colonist->tools[index]);
        if (factor >= best_possible) {
            continue;
        }
        FetchSearch search = fetch_search(context, def->tools[slot]);
        search.use = def->tool_use;
        search.min_factor = factor;
        if (start_fetch(colonist, context, &search, current_tile(colonist),
                        COLONIST_TOOL_SWAP_MAX_COST, true)) {
            return true;
        }
    }
    return false;
}

/* Chooses the next step of a craft: fetch what is still missing (tools
 * first, then the input), else go to the station. Returns false when
 * something is missing and cannot be found. */
static bool plan_station_job(Colonist *colonist, const ColonistContext *context)
{
    const Workstation *station = job_station(colonist, context);
    if (station == NULL) {
        return false;
    }
    const Bill *bill = &station->bills[colonist->bill];
    const RecipeDef *def = recipe_def((RecipeType)colonist->recipe);
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        if (def->tools[slot] != 0 && colonist_tool_index(colonist, def->tools[slot]) < 0) {
            FetchSearch search = fetch_search(context, def->tools[slot]);
            return start_fetch(colonist, context, &search, current_tile(colonist),
                               COLONIST_WORK_SEARCH_MAX_COST, true);
        }
    }
    if (station->work_input.count == 0 && fetch_better_tool(colonist, context, def)) {
        return true;
    }
    bool in_progress = station->work_input.count > 0;
    if (!in_progress && colonist->carried.count < def->input_count) {
        return fetch_input(colonist, context, station, bill);
    }
    return go_to_station(colonist, context, station);
}

/* Whether the craft is still worth doing: the station is there and
 * reserved by the colonist, the work type is still allowed, the colonist
 * still knows enough and, before the work starts, the bill still wants
 * doing. */
static bool station_job_still_valid(const Colonist *colonist, const ColonistContext *context)
{
    const Workstation *station = job_station(colonist, context);
    if (station == NULL ||
        reservations_owner(context->reservations, RESERVATION_STATION, station_tile(station)) !=
            context->worker_id) {
        return false;
    }
    if (colonist->work_priorities[WORK_CRAFTING] == WORK_PRIORITY_DISABLED ||
        colonist->bill >= station->bill_count ||
        station->bills[colonist->bill].recipe != colonist->recipe ||
        !colonist_can_craft(colonist, (RecipeType)colonist->recipe)) {
        return false;
    }
    const Bill *bill = &station->bills[colonist->bill];
    /* A craft under way is finished, even if the bill changed meanwhile. */
    return colonist->station_phase == STATION_JOB_WORK ||
           (bill_wants_more(bill, stored_count(context, bill)) &&
            colonist_level_fits_bill(colonist, bill) &&
            (!bill->paused || station->work_input.count > 0));
}

static bool try_station_job(Colonist *colonist, const ColonistContext *context)
{
    if (!station_jobs_exist(context)) {
        return false;
    }
    StationSearch search = {context, colonist};
    TilePos tile;
    if (!pathfinder_find_nearest(context->pathfinder, context->map, current_tile(colonist),
                                 is_station_job, &search, COLONIST_WORK_SEARCH_MAX_COST, NULL,
                                 &tile)) {
        return false;
    }
    int index = workstation_at(context->workstations, tile.x, tile.y);
    const Workstation *station = workstation_get_const(context->workstations, index);
    int bill = choose_bill(colonist, context, station);
    begin_task(colonist, context, TASK_CRAFT);
    colonist->station = index;
    colonist->station_number = station->number;
    colonist->target = tile;
    colonist->bill = (uint8_t)bill;
    colonist->recipe = station->bills[bill].recipe;
    reservations_add(context->reservations, RESERVATION_STATION, tile, context->worker_id);
    if (!plan_station_job(colonist, context)) {
        set_idle(colonist, context, 0);
        path_clear(&colonist->path);
        return false;
    }
    return true;
}

/* At the stack being fetched: takes a tool, or the input of the craft. */
static bool pick_up_fetched(Colonist *colonist, const ColonistContext *context)
{
    ItemStack stack = items_get(context->items, colonist->x, colonist->y);
    if (stack.type == ITEM_NONE) {
        return false;
    }
    if (colonist->fetch_tool) {
        const RecipeDef *def = recipe_def((RecipeType)colonist->recipe);
        ItemTypeMask tools = 0;
        for (int slot = 0; def != NULL && slot < RECIPE_TOOL_MAX; slot++) {
            tools |= def->tools[slot];
        }
        int carried = colonist_tool_index(colonist, ITEM_TYPE_BIT(stack.type));
        ToolUse use = def != NULL ? def->tool_use : TOOL_USE_NONE;
        ItemStack replaced;
        if (!(tools & ITEM_TYPE_BIT(stack.type)) ||
            (carried >= 0 &&
             tool_speed_factor(use, stack) <= tool_speed_factor(use, colonist->tools[carried])) ||
            !colonist_swap_tool(colonist, stack, &replaced)) {
            return false;
        }
        items_remove(context->items, colonist->x, colonist->y, 1);
        if (replaced.count > 0) {
            items_drop(context->items, context->map, colonist->x, colonist->y, replaced);
        }
        return true;
    }
    int wanted =
        recipe_def((RecipeType)colonist->recipe)->input_count - (int)colonist->carried.count;
    int capacity = colonist_carry_capacity(colonist, (ItemType)stack.type) -
                   (int)colonist->carried.count;
    wanted = wanted < capacity ? wanted : capacity;
    if (wanted <= 0 ||
        (colonist->carried.count > 0 && !item_stacks_match(stack, colonist->carried))) {
        return false;
    }
    int taken = items_remove(context->items, colonist->x, colonist->y,
                             stack.count < wanted ? stack.count : wanted);
    ItemStack picked = stack;
    picked.count = (uint16_t)taken;
    item_stack_merge(&colonist->carried, picked);
    return taken > 0;
}

/* Arrived at the station: puts the input down, then starts working. */
static bool arrive_at_station(Colonist *colonist, const ColonistContext *context)
{
    Workstations *workstations = context->workstations;
    Workstation *station = job_station(colonist, context);
    if (station->work_input.count == 0) {
        if (!workstation_start_craft(workstations, colonist->station,
                                     (RecipeType)colonist->recipe, colonist->carried)) {
            return false;
        }
        colonist->carried = (ItemStack){.type = ITEM_NONE};
    }
    colonist->station_phase = STATION_JOB_WORK;
    return true;
}

/*
 * Clearing the plant on a blueprint's tile before building there, one tick of
 * work into `progress`. Returns true while there is something to clear (the
 * tick was spent on it).
 */
static bool clear_plant_first(Colonist *colonist, const ColonistContext *context, TilePos tile,
                              float *progress)
{
    float needed = gather_clear_work(context->map, tile.x, tile.y);
    if (needed <= 0.0f) {
        return false;
    }
    *progress += colonist_work_speed(colonist, KNOWLEDGE_NONE) *
                 work_with_tool(colonist, gather_clear_tool_use(context->map, tile.x, tile.y));
    if (*progress >= needed) {
        gather_clear_plant(context->map, context->items, tile.x, tile.y,
                           colonist_knowledge_level(colonist, KNOWLEDGE_GATHERING), context->rng);
        *progress = 0.0f;
    }
    return true;
}

/*
 * Speed factor of one tick of a craft: the recipe's tool of the first set
 * (core/tool.h). Every tool of the recipe wears. 0 when one of them is not
 * carried (it broke).
 */
static float craft_tool_factor(Colonist *colonist, const RecipeDef *recipe)
{
    int used[RECIPE_TOOL_MAX];
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        used[slot] = -1;
        if (recipe->tools[slot] != 0) {
            used[slot] = slot == 0 ? working_tool(colonist, recipe->tool_use,
                                                  recipe->tools[slot], true)
                                   : colonist_tool_index(colonist, recipe->tools[slot]);
            if (used[slot] < 0) {
                return 0.0f;
            }
        }
    }
    float factor = used[0] >= 0 ? tool_speed_factor(recipe->tool_use, colonist->tools[used[0]])
                                : tool_type_factor(recipe->tool_use, ITEM_NONE);
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        if (slot == 0 || used[slot] != used[0]) {
            wear_tool(colonist, used[slot]);
        }
    }
    return factor;
}

static void station_job(Colonist *colonist, const ColonistContext *context)
{
    Workstation *station = job_station(colonist, context);
    if (!station_job_still_valid(colonist, context)) {
        set_idle(colonist, context, 0);
        return;
    }
    TilePos tile = station_tile(station);
    switch ((StationJobPhase)colonist->station_phase) {
    case STATION_JOB_FETCH:
        if (colonist->x != colonist->fetch.x || colonist->y != colonist->fetch.y ||
            !pick_up_fetched(colonist, context) ||
            !plan_station_job(colonist, context)) {
            set_idle(colonist, context, 0); /* Drops what it holds. */
        }
        return;
    case STATION_JOB_GO:
        if (colonist->x != tile.x || colonist->y != tile.y ||
            !arrive_at_station(colonist, context)) {
            set_idle(colonist, context, 0);
        }
        return;
    case STATION_JOB_WORK: break;
    }
    if (station->work_input.count == 0) {
        set_idle(colonist, context, 0); /* The input went away (bill removed). */
        return;
    }
    const RecipeDef *recipe = recipe_def((RecipeType)colonist->recipe);
    float tool_factor = craft_tool_factor(colonist, recipe);
    if (tool_factor <= 0.0f) {
        set_idle(colonist, context, 0); /* A tool broke: fetch another one first. */
        return;
    }
    KnowledgeType knowledge = recipe->knowledge;
    /* Products depend on the level before this tick's learning. */
    bool done = workstation_add_craft_work(context->workstations, colonist->station,
                                           colonist->bill, context->items, context->map,
                                           colonist_work_speed(colonist, knowledge) * tool_factor,
                                           colonist_knowledge_level(colonist, knowledge),
                                           context->rng);
    colonist_learn_by_doing(colonist, knowledge, 1);
    if (done) {
        dirt_spread(context->dirt, context->map, tile.x, tile.y,
                    recipe_def((RecipeType)colonist->recipe)->dirt);
        set_idle(colonist, context, 0);
    }
}

/* ---- Construction sites ----------------------------------------------------- */

/* The site of the current job, NULL when it is gone (built or removed). */
static const ConstructionSite *job_site(const Colonist *colonist, const ColonistContext *context)
{
    const ConstructionSite *site = construction_get(context->constructions, colonist->site);
    return site != NULL && site->number == colonist->site_number ? site : NULL;
}

static bool next_to(int x, int y, TilePos tile)
{
    return abs(x - tile.x) + abs(y - tile.y) == 1;
}

/* A site is reserved on its head, whichever of its tiles a builder works by. */
static TilePos site_origin(const ConstructionSite *site)
{
    return (TilePos){site->x, site->y};
}

/* A refresh site is only worth it while its structure stands. */
static bool site_still_wanted(const ColonistContext *context, const ConstructionSite *site)
{
    return !site->refresh || (map_structure_is_origin(context->map, site->x, site->y) &&
                              map_structure(context->map, site->x, site->y) ==
                                  (StructureType)site->type);
}

/* Whether some material of a site is still to bring, nobody brings it yet,
 * and some lies about. */
static bool site_wants_material(const ColonistContext *context, const ConstructionSite *site)
{
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        if (construction_unpromised(site, slot) > 0 &&
            items_exist(context->items, ITEM_TYPE_BIT(construction_material(site, slot)), 1)) {
            return true;
        }
    }
    return false;
}

/* Whether a site offers work to this colonist: free, buildable by it, and
 * either ready for work or missing a material it can go for. */
static bool site_has_job(const ColonistContext *context, const Colonist *worker, int x, int y)
{
    const ConstructionSite *site =
        construction_get(context->constructions, construction_at(context->constructions, x, y));
    if (site == NULL || !colonist_can_build_site(worker, site) ||
        reservations_taken_by_other(context->reservations, RESERVATION_CONSTRUCTION,
                                    site_origin(site), context->worker_id)) {
        return false;
    }
    return construction_can_work(site, context->map) || site_wants_material(context, site);
}

/* Cheap check before searching the map: whether some site offers a job,
 * to a builder (`worker`: ready for work and free, or missing a material it
 * can go for) or to a hauler (NULL: missing a material). */
static bool any_site_job(const ColonistContext *context, const Colonist *worker)
{
    for (int index = 0; index < CONSTRUCTION_MAX; index++) {
        const ConstructionSite *site = construction_get(context->constructions, index);
        if (site == NULL || !site_still_wanted(context, site)) {
            continue;
        }
        if (worker == NULL) {
            if (site_wants_material(context, site)) {
                return true;
            }
        } else if (site_has_job(context, worker, site->x, site->y)) {
            return true;
        }
    }
    return false;
}

typedef struct SiteSearch {
    const ColonistContext *context;
    const Colonist *worker;
} SiteSearch;

/* First site with a job next to (x, y), in a fixed order. */
static bool find_adjacent_site(const SiteSearch *search, int x, int y, TilePos *out)
{
    for (int i = 0; i < 4; i++) {
        int nx = x + NEIGHBOR_DX[i], ny = y + NEIGHBOR_DY[i];
        if (site_has_job(search->context, search->worker, nx, ny)) {
            *out = (TilePos){(int16_t)nx, (int16_t)ny};
            return true;
        }
    }
    return false;
}

static bool is_site_spot(const Map *map, int x, int y, void *userdata)
{
    TilePos unused;
    return map_is_passable(map, x, y) && find_adjacent_site(userdata, x, y, &unused);
}

static bool is_next_to_target(const Map *map, int x, int y, void *userdata)
{
    return map_is_passable(map, x, y) && next_to(x, y, *(const TilePos *)userdata);
}

/* Heads for a tile next to the site: the delivery or the work happens there. */
static bool go_to_site(Colonist *colonist, const ColonistContext *context)
{
    reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE,
                              context->worker_id);
    colonist->station_phase = STATION_JOB_GO;
    colonist->path_index = 0;
    if (next_to(colonist->x, colonist->y, colonist->target)) {
        path_clear(&colonist->path);
        return true;
    }
    TilePos spot;
    return pathfinder_find_nearest(context->pathfinder, context->map, current_tile(colonist),
                                   is_next_to_target, &colonist->target,
                                   COLONIST_WORK_SEARCH_MAX_COST, &colonist->path, &spot);
}

/* Next step of a trip, or the next trip: fetch more for the trip under way,
 * else deliver what is in hand, else go for the first material nobody brings
 * yet (promising it); once every material is in, a builder goes to work.
 * False when there is nothing left to do. */
static bool plan_construction(Colonist *colonist, const ColonistContext *context)
{
    const ConstructionSite *site = job_site(colonist, context);
    if (site == NULL) {
        return false;
    }
    if (colonist->carried.count > 0) {
        int wanted = colonist->supply_promised - colonist->carried.count;
        int room = colonist_carry_capacity(colonist, (ItemType)colonist->carried.type) -
                   colonist->carried.count;
        if (wanted > 0 && room > 0) {
            FetchSearch search = fetch_search(context, ITEM_TYPE_BIT(colonist->carried.type));
            search.match = colonist->carried;
            if (start_fetch(colonist, context, &search, current_tile(colonist),
                            COLONIST_WORK_SEARCH_MAX_COST, false)) {
                return true;
            }
        }
        return go_to_site(colonist, context); /* Deliver what is in hand. */
    }
    release_promise(colonist, context); /* Nothing in hand: a new trip. */
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        int left = construction_unpromised(site, slot);
        if (left <= 0) {
            continue;
        }
        ItemType type = construction_material(site, slot);
        FetchSearch search = fetch_search(context, ITEM_TYPE_BIT(type));
        if (start_fetch(colonist, context, &search, current_tile(colonist),
                        COLONIST_WORK_SEARCH_MAX_COST, false)) {
            int load = colonist_carry_capacity(colonist, type);
            colonist->supply_slot = slot;
            colonist->supply_promised = construction_promise(
                context->constructions, colonist->site, slot, left < load ? left : load);
            return true;
        }
    }
    return colonist->task == TASK_CONSTRUCT && construction_can_work(site, context->map) &&
           go_to_site(colonist, context);
}


static bool supply_job_still_valid(const Colonist *colonist, const ColonistContext *context)
{
    const ConstructionSite *site = job_site(colonist, context);
    return site != NULL && site_still_wanted(context, site) &&
           colonist->work_priorities[WORK_HAULING] != WORK_PRIORITY_DISABLED;
}

static bool construction_job_still_valid(const Colonist *colonist,
                                         const ColonistContext *context)
{
    const ConstructionSite *site = job_site(colonist, context);
    return site != NULL && site_still_wanted(context, site) &&
           reservations_owner(context->reservations, RESERVATION_CONSTRUCTION,
                              site_origin(site)) == context->worker_id &&
           colonist->work_priorities[WORK_CONSTRUCTION] != WORK_PRIORITY_DISABLED &&
           colonist_can_build_site(colonist, site);
}

static uint32_t construction_stamp(const ColonistContext *context)
{
    return context->constructions->revision + context->items->additions +
           context->reservations->releases;
}

static bool try_construction_job(Colonist *colonist, const ColonistContext *context)
{
    if (context->constructions->used_count == 0 || !any_site_job(context, colonist)) {
        return false;
    }
    SiteSearch search = {context, colonist};
    TilePos spot, target;
    if (!pathfinder_find_nearest(context->pathfinder, context->map, current_tile(colonist),
                                 is_site_spot, &search, COLONIST_WORK_SEARCH_MAX_COST, NULL,
                                 &spot)) {
        return false;
    }
    find_adjacent_site(&search, spot.x, spot.y, &target);
    int index = construction_at(context->constructions, target.x, target.y);
    const ConstructionSite *site = construction_get(context->constructions, index);
    begin_task(colonist, context, TASK_CONSTRUCT);
    colonist->site = index;
    colonist->site_number = site->number;
    colonist->target = target; /* The tile of the site worked by, its head or another. */
    reservations_add(context->reservations, RESERVATION_CONSTRUCTION, site_origin(site),
                     context->worker_id);
    if (!plan_construction(colonist, context)) {
        set_idle(colonist, context, 0);
        path_clear(&colonist->path);
        return false;
    }
    return true;
}

/* First site next to (x, y), in a fixed order, missing a material nobody
 * brings yet that lies about. */
static bool find_supply_site(const ColonistContext *context, int x, int y, TilePos *out)
{
    for (int i = 0; i < 4; i++) {
        int nx = x + NEIGHBOR_DX[i], ny = y + NEIGHBOR_DY[i];
        const ConstructionSite *site = construction_get(
            context->constructions, construction_at(context->constructions, nx, ny));
        if (site != NULL && site_still_wanted(context, site) &&
            site_wants_material(context, site)) {
            *out = (TilePos){(int16_t)nx, (int16_t)ny};
            return true;
        }
    }
    return false;
}

static bool is_supply_spot(const Map *map, int x, int y, void *userdata)
{
    TilePos unused;
    return map_is_passable(map, x, y) && find_supply_site(userdata, x, y, &unused);
}

/* A hauler brings materials to the nearest site that waits for some. */
static bool try_supply_job(Colonist *colonist, const ColonistContext *context)
{
    if (context->constructions->used_count == 0 || !any_site_job(context, NULL)) {
        return false;
    }
    TilePos spot, target;
    if (!pathfinder_find_nearest(context->pathfinder, context->map, current_tile(colonist),
                                 is_supply_spot, (void *)context, COLONIST_WORK_SEARCH_MAX_COST,
                                 NULL, &spot)) {
        return false;
    }
    find_supply_site(context, spot.x, spot.y, &target);
    int index = construction_at(context->constructions, target.x, target.y);
    begin_task(colonist, context, TASK_SUPPLY);
    colonist->site = index;
    colonist->site_number = construction_get(context->constructions, index)->number;
    colonist->target = target;
    if (!plan_construction(colonist, context)) {
        set_idle(colonist, context, 0);
        path_clear(&colonist->path);
        return false;
    }
    return true;
}

/* At the stack being fetched: takes what is left of the trip's promise. */
static bool pick_up_material(Colonist *colonist, const ColonistContext *context,
                             const ConstructionSite *site)
{
    ItemStack stack = items_get(context->items, colonist->x, colonist->y);
    if (colonist->supply_promised <= 0 ||
        construction_material(site, colonist->supply_slot) != stack.type ||
        (colonist->carried.count > 0 && !item_stacks_match(stack, colonist->carried))) {
        return false;
    }
    int wanted = colonist->supply_promised - colonist->carried.count;
    int room = colonist_carry_capacity(colonist, (ItemType)stack.type) - colonist->carried.count;
    wanted = wanted < room ? wanted : room;
    if (wanted <= 0) {
        return false;
    }
    int taken = items_remove(context->items, colonist->x, colonist->y,
                             stack.count < wanted ? stack.count : wanted);
    ItemStack picked = stack;
    picked.count = (uint16_t)taken;
    item_stack_merge(&colonist->carried, picked);
    return taken > 0;
}

/* A blocking structure or a workstation was built: zones lose its tiles
 * (one may have been laid on the site meanwhile). */
static void structure_built(const ColonistContext *context, const ConstructionSite *site)
{
    bool takes_tiles = construction_workstation(site) != WORKSTATION_TYPE_COUNT ||
                       structure_blocks((StructureType)site->type);
    for (int part = 0; takes_tiles && part < construction_length(site); part++) {
        int x, y;
        construction_tile(site, part, &x, &y);
        stockpiles_remove_rect(context->stockpiles, x, y, x, y);
        gather_zones_remove_rect(context->gather_zones, x, y, x, y);
    }
}

/* Clears the plants of a site's tiles, one after the other (see
 * clear_plant_first()). */
static bool clear_site_first(Colonist *colonist, const ColonistContext *context,
                             ConstructionSite *site)
{
    for (int part = 0; part < construction_length(site); part++) {
        int x, y;
        construction_tile(site, part, &x, &y);
        if (clear_plant_first(colonist, context, (TilePos){(int16_t)x, (int16_t)y},
                              &site->clear_progress)) {
            return true;
        }
    }
    return false;
}

/* TASK_CONSTRUCT and TASK_SUPPLY: fetching and delivering materials; a
 * builder then works on the site. */
static void construction_job(Colonist *colonist, const ColonistContext *context)
{
    bool supplying = colonist->task == TASK_SUPPLY;
    if (supplying ? !supply_job_still_valid(colonist, context)
                  : !construction_job_still_valid(colonist, context)) {
        set_idle(colonist, context, 0);
        return;
    }
    const ConstructionSite *site = job_site(colonist, context);
    switch ((StationJobPhase)colonist->station_phase) {
    case STATION_JOB_FETCH:
        if (colonist->x != colonist->fetch.x || colonist->y != colonist->fetch.y ||
            !pick_up_material(colonist, context, site) || !plan_construction(colonist, context)) {
            set_idle(colonist, context, 0); /* Drops what it holds. */
        }
        return;
    case STATION_JOB_GO:
        if (!next_to(colonist->x, colonist->y, colonist->target)) {
            set_idle(colonist, context, 0);
            return;
        }
        if (colonist->carried.count > 0) {
            int taken = construction_deliver(context->constructions, colonist->site,
                                             colonist->carried);
            colonist->carried.count = (uint16_t)(colonist->carried.count - taken);
            release_promise(colonist, context);
            if (colonist->carried.count > 0) {
                set_idle(colonist, context, 0); /* No longer wanted: put down here. */
                return;
            }
            colonist->carried = (ItemStack){.type = ITEM_NONE};
        }
        if (supplying || !construction_can_work(site, context->map)) {
            if (!plan_construction(colonist, context)) { /* More to bring. */
                set_idle(colonist, context, 0);
            }
            return;
        }
        colonist->station_phase = STATION_JOB_WORK;
        return;
    case STATION_JOB_WORK: break;
    }
    if (!next_to(colonist->x, colonist->y, colonist->target)) {
        set_idle(colonist, context, 0);
        return;
    }
    ConstructionSite *clearing = construction_get_mutable(context->constructions, colonist->site);
    /* A roof goes over the plants, as over anything else. */
    if (!structure_is_roof((StructureType)site->type) &&
        clear_site_first(colonist, context, clearing)) {
        return;
    }
    ConstructionSite done = *site; /* Gone from the list once built. */
    KnowledgeType knowledge = construction_knowledge(site);
    float speed = colonist_work_speed(colonist, knowledge) *
                  work_with_tool(colonist, TOOL_USE_BUILDING);
    /* The grade depends on the level before this tick's learning. */
    bool built = construction_add_work(context->constructions, context->map, context->items,
                                       context->workstations, colonist->site, speed,
                                       colonist_knowledge_level(colonist, knowledge),
                                       context->rng);
    colonist_learn_by_doing(colonist, knowledge, 1);
    if (built) {
        structure_built(context, &done);
        set_idle(colonist, context, 0);
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

static void update_needs(Colonist *colonist, const ColonistContext *context)
{
    float *food = &colonist->needs[NEED_FOOD];
    float *water = &colonist->needs[NEED_WATER];
    float *rest = &colonist->needs[NEED_REST];
    *food = clampf(*food - NEED_FOOD_DECAY_PER_TICK, 0.0f, 1.0f);
    *water = clampf(*water - NEED_WATER_DECAY_PER_TICK, 0.0f, 1.0f);
    if (colonist->ground_sleep_ticks > 0) {
        colonist->ground_sleep_ticks--;
    }
    if (colonist->task == TASK_SLEEP && !colonist_is_moving(colonist)) {
        float gain = NEED_REST_GAIN_PER_TICK * rest_factor(colonist, context->map);
        *rest = clampf(*rest + gain, 0.0f, 1.0f);
    } else {
        *rest = clampf(*rest - NEED_REST_DECAY_PER_TICK, 0.0f, 1.0f);
    }
    for (int kind = 0; kind < SEARCH_COUNT; kind++) {
        if (colonist->search_cooldown[kind] > 0) {
            colonist->search_cooldown[kind]--;
        }
        if (colonist->search_quiet[kind] > 0) {
            colonist->search_quiet[kind]--;
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
            /* Rested, but stiff after a night on the bare ground. */
            if (!structure_is_bed(map_structure(context->map, colonist->x, colonist->y))) {
                colonist->ground_sleep_ticks = COLONIST_GROUND_SLEEP_PENALTY_TICKS;
            }
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
    case TASK_CRAFT:
        if (!station_job_still_valid(colonist, context)) {
            set_idle(colonist, context, 0); /* Cancelled on the way: decide again below. */
        }
        break;
    case TASK_CONSTRUCT:
        if (!construction_job_still_valid(colonist, context)) {
            set_idle(colonist, context, 0);
        }
        break;
    case TASK_SUPPLY:
        if (!supply_job_still_valid(colonist, context)) {
            set_idle(colonist, context, 0);
        }
        break;
    case TASK_CLEAN:
        if (!clean_job_still_valid(colonist, context)) {
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

/* A structure built where the colonist stands (a wall finished by someone
 * else): it steps to a free neighboring tile and decides again. */
static void step_out_of_blocked_tile(Colonist *colonist, const ColonistContext *context)
{
    static const int DX[8] = {0, 1, 0, -1, 1, 1, -1, -1};
    static const int DY[8] = {-1, 0, 1, 0, -1, 1, 1, -1};
    if (map_is_passable(context->map, colonist->x, colonist->y)) {
        return;
    }
    for (int i = 0; i < 8; i++) {
        int x = colonist->x + DX[i], y = colonist->y + DY[i];
        if (map_is_passable(context->map, x, y)) {
            colonist->x = x;
            colonist->y = y;
            colonist->move_progress = 0.0f;
            set_idle(colonist, context, 0);
            return;
        }
    }
}

void colonist_tick(Colonist *colonist, const ColonistContext *context)
{
    step_out_of_blocked_tile(colonist, context);
    update_needs(colonist, context);
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
    case TASK_CRAFT: station_job(colonist, context); break;
    case TASK_CONSTRUCT:
    case TASK_SUPPLY: construction_job(colonist, context); break;
    case TASK_CLEAN: clean(colonist, context); break;
    case TASK_SLEEP:
    case TASK_COUNT: break;
    }
}
