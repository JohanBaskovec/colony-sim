#include "core/colonist.h"

#include "core/hauling.h"
#include "core/i18n.h"
#include "core/tree.h"
#include "core/util.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SQRT2 1.41421356f

/* First names from many cultures. Names are proper nouns and are not translated. */
static const char *const NAMES[] = {
    "Ada",   "Aiko",    "Amara",   "Anouk", "Arjun",  "Astrid", "Bao",   "Bastien",
    "Bjorn", "Camille", "Chidi",   "Dalia", "Dmitri", "Elif",   "Emeka", "Esme",
    "Farah", "Felix",   "Freya",   "Gael",  "Hana",   "Hugo",   "Idris", "Ines",
    "Ivo",   "Jun",     "Kai",     "Kaveh", "Keziah", "Lars",   "Leila", "Lena",
    "Luca",  "Maeve",   "Malik",   "Mateo", "Mei",    "Milan",  "Nadia", "Nils",
    "Noor",  "Oren",    "Oskar",   "Priya", "Quinn",  "Rafael", "Ravi",  "Rosa",
    "Sami",  "Sanne",   "Selin",   "Soren", "Tariq",  "Thea",   "Tomas", "Uma",
    "Vera",  "Wren",    "Xiadani", "Yara",  "Yusuf",  "Zainab", "Zofia", "Zuri",
};

size_t colonist_name_count(void)
{
    return COLONY_ARRAY_COUNT(NAMES);
}

const char *colonist_name_at(size_t index)
{
    return index < COLONY_ARRAY_COUNT(NAMES) ? NAMES[index] : NULL;
}

void colonist_init(Colonist *colonist, const char *name, int age, int x, int y, int z)
{
    memset(colonist, 0, sizeof(*colonist));
    snprintf(colonist->name, sizeof(colonist->name), "%s", name);
    colonist->age = age;
    colonist->x = x;
    colonist->y = y;
    colonist->z = z;
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
    colonist->chat_topic = KNOWLEDGE_NONE;
    mood_init(&colonist->mood);
    attributes_init(&colonist->attributes);
}

void colonist_set_attributes(Colonist *colonist, const Attributes *attributes)
{
    colonist->attributes = *attributes;
    mood_set_character(&colonist->mood, attribute_outlook_points(attributes),
                       attribute_thought_weight(attributes));
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
    if (level > colonist->knowledge_peak[knowledge]) {
        colonist->knowledge_peak[knowledge] = level;
    }
    return true;
}

static uint32_t day_of_tick(uint64_t tick)
{
    return (uint32_t)(tick / TICKS_PER_DAY);
}

double colonist_learn(Colonist *colonist, KnowledgeType knowledge, KnowledgeSource source,
                      double rate, double hours, double cap, uint64_t tick)
{
    if ((unsigned)knowledge >= KNOWLEDGE_COUNT || (unsigned)source >= KNOWLEDGE_SOURCE_COUNT) {
        return 0.0;
    }
    double *level = &colonist->knowledge[knowledge];
    double before = *level;
    rate *= mood_learn_factor(colonist->mood.value) *
            attribute_factor(&colonist->attributes, ATTRIBUTE_LEARNING);
    *level = knowledge_learn_capped(before, rate, hours, colonist->age, cap);
    double gain = *level - before;
    if (!(gain > 0.0)) {
        return 0.0;
    }
    if (*level > colonist->knowledge_peak[knowledge]) {
        colonist->knowledge_peak[knowledge] = *level;
    }
    uint32_t day = day_of_tick(tick);
    if (colonist->knowledge_gains_day != day) {
        memset(colonist->knowledge_gains, 0, sizeof(colonist->knowledge_gains));
        colonist->knowledge_gains_day = day;
    }
    colonist->knowledge_gains[knowledge][source] += (float)gain;
    return gain;
}

void colonist_learn_by_doing(Colonist *colonist, KnowledgeType knowledge, int ticks, uint64_t tick)
{
    if ((unsigned)knowledge >= KNOWLEDGE_COUNT || ticks <= 0) {
        return;
    }
    colonist->knowledge_practice[knowledge] = tick;
    colonist_learn(colonist, knowledge, KNOWLEDGE_SOURCE_DOING, KNOWLEDGE_DOING_RATE,
                   (double)ticks / TICKS_PER_HOUR, KNOWLEDGE_LEVEL_MAX, tick);
}

double colonist_knowledge_gain_today(const Colonist *colonist, KnowledgeType knowledge,
                                     KnowledgeSource source, uint64_t tick)
{
    if ((unsigned)knowledge >= KNOWLEDGE_COUNT || (unsigned)source >= KNOWLEDGE_SOURCE_COUNT ||
        colonist->knowledge_gains_day != day_of_tick(tick)) {
        return 0.0;
    }
    return colonist->knowledge_gains[knowledge][source];
}

int colonist_days_unpractised(const Colonist *colonist, KnowledgeType knowledge, uint64_t tick)
{
    if ((unsigned)knowledge >= KNOWLEDGE_COUNT || tick < colonist->knowledge_practice[knowledge]) {
        return 0;
    }
    return (int)((tick - colonist->knowledge_practice[knowledge]) / TICKS_PER_DAY);
}

bool colonist_knowledge_fading(const Colonist *colonist, KnowledgeType knowledge, uint64_t tick)
{
    if ((unsigned)knowledge >= KNOWLEDGE_COUNT) {
        return false;
    }
    return colonist_days_unpractised(colonist, knowledge, tick) >= KNOWLEDGE_FADE_AFTER_DAYS &&
           colonist->knowledge[knowledge] >
               knowledge_fade_floor(colonist->knowledge_peak[knowledge]);
}

void colonist_fade_knowledge(Colonist *colonist, uint64_t tick)
{
    for (int knowledge = 0; knowledge < KNOWLEDGE_COUNT; knowledge++) {
        int idle = colonist_days_unpractised(colonist, (KnowledgeType)knowledge, tick);
        colonist->knowledge[knowledge] = knowledge_fade_day(
            colonist->knowledge[knowledge], colonist->knowledge_peak[knowledge], idle);
    }
}

void colonist_reset_practice(Colonist *colonist, uint64_t tick)
{
    for (int knowledge = 0; knowledge < KNOWLEDGE_COUNT; knowledge++) {
        colonist->knowledge_practice[knowledge] = tick;
    }
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
            items_drop(items, map, colonist->x, colonist->y, colonist->z, colonist->tools[i]);
            colonist->tools[i] = (ItemStack){.type = ITEM_NONE};
            colonist->tool_wear[i] = 0;
        }
    }
}

/* Index of the tool the colonist works with for a use among `allowed`, or -1
 * for bare hands. A required tool (recipes) is used even when no faster than
 * bare hands. */
static int working_tool(const Colonist *colonist, ToolUse use, ItemTypeMask allowed, bool required)
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

/* Speed of work without a tool under the debug cheat GameDebug.no_tools: a
 * standard tool's. */
#define NO_TOOLS_FACTOR 1.0f

/* Speed factor of one tick of a kind of work with the best carried tool for
 * it (bare hands when none helps), which wears. */
static float work_with_tool(Colonist *colonist, const ColonistContext *context, ToolUse use)
{
    int index = working_tool(colonist, use, ~(ItemTypeMask)0, false);
    float factor = index >= 0 ? tool_speed_factor(use, colonist->tools[index])
                              : tool_type_factor(use, ITEM_NONE);
    wear_tool(colonist, index);
    return context->no_tools && use != TOOL_USE_NONE && factor < NO_TOOLS_FACTOR ? NO_TOOLS_FACTOR
                                                                                 : factor;
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
    return speed * mood_work_factor(colonist->mood.value) *
           attribute_factor(&colonist->attributes, ATTRIBUTE_WORK_PACE);
}

double colonist_work_level(const Colonist *colonist, KnowledgeType knowledge)
{
    double level =
        colonist_knowledge_level(colonist, knowledge) + mood_level_offset(colonist->mood.value);
    return level < KNOWLEDGE_LEVEL_MIN   ? KNOWLEDGE_LEVEL_MIN
           : level > KNOWLEDGE_LEVEL_MAX ? KNOWLEDGE_LEVEL_MAX
                                         : level;
}

bool colonist_in_break(const Colonist *colonist)
{
    return colonist->mood.break_type != MOOD_BREAK_NONE;
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
        return colonist->x == colonist->target.x && colonist->y == colonist->target.y &&
                       colonist->z == colonist->target.z
                   ? COLONIST_ACTION_GATHER
                   : COLONIST_ACTION_NONE;
    case TASK_LIGHT: /* Wrapping embers, or twirling a stick on a board. */
        if (colonist->light_step == LIGHT_EMBERS) {
            return COLONIST_ACTION_GATHER;
        }
        return colonist->light_step == LIGHT_FRICTION ? COLONIST_ACTION_CUT : COLONIST_ACTION_NONE;
    case TASK_IDLE:
    case TASK_WANDER:
    case TASK_MOVE:
    case TASK_ASSIST:
    case TASK_REFUEL:
    case TASK_STORY:
    case TASK_LISTEN:
    case TASK_HAUL:
    case TASK_SUPPLY:
    case TASK_SLEEP:
    case TASK_COUNT: break;
    }
    return COLONIST_ACTION_NONE;
}

ColonistExpression colonist_expression(const Colonist *colonist)
{
    float mood = colonist->mood.value;
    if (colonist_in_break(colonist) || mood < COLONIST_ANGRY_BELOW) {
        return EXPRESSION_ANGRY;
    }
    if (mood < COLONIST_SAD_BELOW) {
        return EXPRESSION_SAD;
    }
    return mood >= COLONIST_SMILING_FROM ? EXPRESSION_SMILING : EXPRESSION_NEUTRAL;
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
        [TASK_ASSIST] = N_("colonist.task.assist"),
        [TASK_REFUEL] = N_("colonist.task.refuel"),
        [TASK_LIGHT] = N_("colonist.task.light"),
        [TASK_STORY] = N_("colonist.task.story"),
        [TASK_LISTEN] = N_("colonist.task.listen"),
        [TASK_MOVE] = N_("colonist.task.move"),
    };
    return (unsigned)task < TASK_COUNT ? KEYS[task] : KEYS[TASK_IDLE];
}

const char *colonist_activity_key(const Colonist *colonist)
{
    const char *break_key = mood_break_key((MoodBreak)colonist->mood.break_type);
    if (break_key != NULL && colonist->task != TASK_SLEEP && colonist->task != TASK_EAT &&
        colonist->task != TASK_DRINK) {
        return break_key;
    }
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
    return (TilePos){(int16_t)colonist->x, (int16_t)colonist->y, (int16_t)colonist->z};
}

/* Gives up the materials a colonist promised to bring to a site. */
static void release_promise(Colonist *colonist, const ColonistContext *context)
{
    if (colonist->supply_promised > 0) {
        construction_release_promise(context->constructions, colonist->site, colonist->site_number,
                                     colonist->supply_slot, colonist->supply_promised);
        colonist->supply_promised = 0;
    }
}

/* Gives up the fuel a colonist promised to bring to a hearth. */
static void release_fuel(Colonist *colonist, const ColonistContext *context)
{
    if (colonist->fuel_promised > 0.0f) {
        hearth_release_fuel(context->workstations, colonist->station, colonist->station_number,
                            colonist->fuel_promised);
        colonist->fuel_promised = 0.0f;
    }
}

/* Every task change goes through here: whatever the colonist had reserved
 * (job, food) is released, so reservations never outlive the task that made
 * them. The path is kept: callers set it before or clear it after. */
static void remember_sleep(Colonist *colonist, const ColonistContext *context);

static void begin_task(Colonist *colonist, const ColonistContext *context, ColonistTask task)
{
    if (colonist->task == TASK_SLEEP && task != TASK_SLEEP) {
        remember_sleep(colonist, context); /* Woke up. */
    } else if (colonist->task != TASK_SLEEP && task == TASK_SLEEP) {
        colonist->sleep_ticks = 0;
        colonist->sleep_cold_ticks = 0;
        colonist->sleep_hot_ticks = 0;
        colonist->sleep_wet_ticks = 0;
    }
    if (colonist->carried.count > 0) {
        /* Whatever is in hand is put down where the colonist stands. */
        items_drop(context->items, context->map, colonist->x, colonist->y, colonist->z,
                   colonist->carried);
        colonist->carried = (ItemStack){.type = ITEM_NONE};
    }
    reservations_release_all(context->reservations, context->worker_id);
    release_promise(colonist, context);
    release_fuel(colonist, context);
    colonist->ember_ticks = 0; /* Embers carried go out. */
    colonist->light_progress = 0.0f;
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

/* Starts walking to `goal` for a player's order (at the middle of a tile). */
static bool start_move(Colonist *colonist, const ColonistContext *context, TilePos goal)
{
    Path path = {0};
    TilePos here = current_tile(colonist);
    if (here.x != goal.x || here.y != goal.y || here.z != goal.z) {
        if (!pathfinder_find(context->pathfinder, context->plan_map, here, goal,
                             COLONIST_MOVE_MAX_COST, &path)) {
            path_free(&path);
            return false;
        }
    }
    begin_task(colonist, context, TASK_MOVE);
    path_free(&colonist->path);
    colonist->path = path; /* Empty when already there. */
    colonist->target = goal;
    return true;
}

bool colonist_order_move(Colonist *colonist, const ColonistContext *context, TilePos goal)
{
    if (!map_is_passable(context->plan_map, goal.x, goal.y, goal.z) ||
        !regions_connected(context->regions, context->map, current_tile(colonist), goal)) {
        return false;
    }
    if (colonist->move_progress > 0.0f) {
        /* Between two tiles: on the next one (the search is made then). */
        colonist->move_pending = true;
        colonist->move_goal = goal;
        return true;
    }
    colonist->move_pending = false;
    return start_move(colonist, context, goal);
}

static bool search_allowed(const Colonist *colonist, SearchKind kind)
{
    return colonist->search_cooldown[kind] <= 0;
}

/* Whether a work search of this kind may run: its delay is over, or what it
 * looks for changed since it failed (`stamp` then, `now` now), once
 * COLONIST_SEARCH_MIN_RETRY_TICKS have gone by, on the settler's own tick
 * out of COLONIST_SEARCH_STAGGER_TICKS. */
static bool retry_allowed(const Colonist *colonist, const ColonistContext *context, SearchKind kind,
                          uint32_t stamp, uint32_t now)
{
    return search_allowed(colonist, kind) ||
           (stamp != now && colonist->search_quiet[kind] <= 0 &&
            (colonist->search_phase + context->worker_id) % COLONIST_SEARCH_STAGGER_TICKS == 0);
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

/* Food on a tile: edible items on the ground, nuts fallen from a tree, or a
 * ripe plant. */
static bool tile_has_food(const Map *map, const Items *items, int x, int y, int z)
{
    return item_is_food((ItemType)items_get_allowed(items, x, y, z).type) ||
           map_litter(map, x, y, z, ITEM_HAZELNUTS) > 0 || map_has_edible_plant(map, x, y, z);
}

/* Search filter: food that nobody else is about to eat. */
typedef struct FoodSearch {
    const Items *items;
    const Reservations *reservations;
    const Temperatures *temperatures;
    bool rains; /* Rain or snow falls (food out in it rots faster). */
    uint8_t self;
} FoodSearch;

static bool is_free_food(const Map *map, int x, int y, int z, void *userdata)
{
    const FoodSearch *search = userdata;
    return tile_has_food(map, search->items, x, y, z) &&
           !reservations_taken_by_other(search->reservations, RESERVATION_FOOD,
                                        (TilePos){(int16_t)x, (int16_t)y, (int16_t)z},
                                        search->self);
}

/* Search filter: free food lying on the ground that rots away soon. */
static bool is_free_spoiling_food(const Map *map, int x, int y, int z, void *userdata)
{
    const FoodSearch *search = userdata;
    ItemStack stack = items_get_allowed(search->items, x, y, z);
    if (!item_is_food((ItemType)stack.type) || !item_rots((ItemType)stack.type)) {
        return false;
    }
    float hours = item_stack_rot_hours(
        stack, temperature_rot_rate(search->temperatures, map, x, y, z, search->rains));
    return hours >= 0.0f && hours < COLONIST_SPOILING_FOOD_HOURS &&
           !reservations_taken_by_other(search->reservations, RESERVATION_FOOD,
                                        (TilePos){(int16_t)x, (int16_t)y, (int16_t)z},
                                        search->self);
}

static bool is_drinking_spot(const Map *map, int x, int y, int z, void *userdata)
{
    (void)userdata;
    return map_is_drinking_spot(map, x, y, z);
}

/* Region the colonist stands in (REGION_NONE when unknown). */
static uint32_t current_region(const Colonist *colonist, const ColonistContext *context)
{
    return regions_at(context->regions, context->map, colonist->x, colonist->y, colonist->z);
}

/*
 * Whether some tile of the region of `from` within reach of a search of
 * `max_cost` passes `predicate`: a scan of the map before a search for the
 * nearest such tile, much cheaper than a search that explores the whole area
 * and finds nothing (every stack of a material taken, no free bed left, no
 * food nearby). Within reach: a step costs at least PATH_MIN_TILE_COST, so
 * nothing farther than max_cost / PATH_MIN_TILE_COST tiles in a straight or
 * diagonal line can be reached, nor more than max_cost / (MAP_CLIMB_COST *
 * PATH_MIN_TILE_COST) levels up or down. True without up-to-date regions:
 * search anyway.
 */
static bool any_tile_reachable(const ColonistContext *context, TilePos from,
                               TilePredicate predicate, void *userdata, float max_cost)
{
    const Map *map = context->map;
    uint32_t region = regions_at(context->regions, map, from.x, from.y, from.z);
    if (region == REGION_NONE) {
        return true;
    }
    float tiles = floorf(max_cost / PATH_MIN_TILE_COST);
    int reach = tiles < (float)(map->width + map->height) ? (int)tiles : map->width + map->height;
    /* Within reach and within the region's box. */
    TileBox box = regions_box(context->regions, map, region);
    int x0 = from.x - reach > box.x0 ? from.x - reach : box.x0;
    int x1 = from.x + reach < box.x1 ? from.x + reach : box.x1;
    int rows = 2 * (reach < map->height ? reach : map->height) + 1;
    float climbs = floorf(max_cost / (MAP_CLIMB_COST * PATH_MIN_TILE_COST));
    int levels = climbs < (float)map->depth ? (int)climbs : map->depth;
    /* The level of `from` first, then the others nearest first; from the row
     * of `from` outwards: what is near is found at once. */
    for (int level = 0; level < 2 * levels + 1; level++) {
        int z = from.z + (level % 2 == 0 ? level / 2 : -(level + 1) / 2);
        if (z < box.z0 || z > box.z1) {
            continue;
        }
        for (int step = 0; step < rows; step++) {
            int y = row_outwards(from.y, step, map->height);
            if (y < box.y0 || y > box.y1) {
                continue; /* Out of the map (-1) or of the region. */
            }
            const uint32_t *ids = &context->regions->id[map_index(map, 0, y, z)];
            for (int x = x0; x <= x1; x++) {
                if (ids[x] == region && predicate(context->plan_map, x, y, z, userdata)) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* Nearest free food lying on the ground that rots away soon, within a short
 * walk; the colonist's path leads there. */
static bool find_spoiling_food(Colonist *colonist, const ColonistContext *context, FoodSearch *food,
                               TilePos *found)
{
    return any_tile_reachable(context, current_tile(colonist), is_free_spoiling_food, food,
                              COLONIST_SPOILING_FOOD_MAX_COST) &&
           pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
                                   is_free_spoiling_food, food, COLONIST_SPOILING_FOOD_MAX_COST,
                                   &colonist->path, found);
}

/* Walks to the nearest food (NEED_FOOD: food about to rot first) or
 * drinking spot (NEED_WATER). */
static bool try_satisfy(Colonist *colonist, const ColonistContext *context, NeedType need)
{
    bool drink = need == NEED_WATER;
    SearchKind kind = drink ? SEARCH_WATER : SEARCH_FOOD;
    if (!search_allowed(colonist, kind)) {
        return false;
    }
    TilePos found;
    FoodSearch food = {context->plan_items, context->reservations, context->temperatures,
                       context->wetting > 0.0f, context->worker_id};
    /* No water anywhere the colonist can walk to, no food within reach: the
     * search would explore everything within its cost limit for nothing. */
    bool hopeless = drink ? !regions_has_drinking_spot(context->regions, context->map,
                                                       current_region(colonist, context))
                          : !any_tile_reachable(context, current_tile(colonist), is_free_food,
                                                &food, COLONIST_FOOD_SEARCH_MAX_COST);
    bool spoiling = !drink && !hopeless && find_spoiling_food(colonist, context, &food, &found);
    if (hopeless ||
        (!spoiling && !pathfinder_find_nearest(
                          context->pathfinder, context->plan_map, current_tile(colonist),
                          drink ? is_drinking_spot : is_free_food, &food,
                          drink ? COLONIST_WATER_SEARCH_MAX_COST : COLONIST_FOOD_SEARCH_MAX_COST,
                          &colonist->path, &found))) {
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
static bool try_satisfy_urgent(Colonist *colonist, const ColonistContext *context, float threshold)
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
static bool is_free_bed(const Map *map, int x, int y, int z, void *userdata)
{
    const ColonistContext *context = userdata;
    uint8_t owner = beds_owner(context->beds, x, y, z);
    return map_structure_is_origin(map, x, y, z) && structure_is_bed(map_structure(map, x, y, z)) &&
           !map_forbidden(map, x, y, z, MAP_FORBID_STRUCTURE) &&
           (owner == BED_NO_OWNER || owner == context->worker_id) &&
           !reservations_taken_by_other(context->reservations, RESERVATION_BED,
                                        (TilePos){(int16_t)x, (int16_t)y, (int16_t)z},
                                        context->worker_id);
}

static bool go_to_warm_spot(Colonist *colonist, const ColonistContext *context);
static void stay_warm_asleep(Colonist *colonist, const ColonistContext *context);

/* Whether some bed (any structure settlers sleep on) stands on the map. */
static bool any_bed(const Map *map)
{
    for (int type = STRUCTURE_NONE + 1; type < STRUCTURE_TYPE_COUNT; type++) {
        if (structure_is_bed((StructureType)type) && map->structure_counts[type] > 0) {
            return true;
        }
    }
    return false;
}

/* Goes to sleep in its own bed, else in the nearest free one (which becomes
 * its own), else at a spot beside a fire when too cold or hot where the
 * colonist stands, else there. */
static void start_sleeping(Colonist *colonist, const ColonistContext *context)
{
    begin_task(colonist, context, TASK_SLEEP);
    path_clear(&colonist->path);
    if (!any_bed(context->plan_map)) {
        go_to_warm_spot(colonist, context);
        return;
    }
    TilePos bed;
    /* Its own bed: straight there. */
    bool found = beds_owned(context->beds, context->worker_id, &bed) &&
                 is_free_bed(context->plan_map, bed.x, bed.y, bed.z, (void *)context) &&
                 pathfinder_find(context->pathfinder, context->plan_map, current_tile(colonist),
                                 bed, COLONIST_BED_SEARCH_MAX_COST, &colonist->path);
    if (!found &&
        any_tile_reachable(context, current_tile(colonist), is_free_bed, (void *)context,
                           COLONIST_BED_SEARCH_MAX_COST) &&
        pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
                                is_free_bed, (void *)context, COLONIST_BED_SEARCH_MAX_COST,
                                &colonist->path, &bed)) {
        found = true;
        beds_assign(context->beds, context->worker_id, bed.x, bed.y, bed.z);
    }
    if (found) {
        colonist->target = bed;
        reservations_add(context->reservations, RESERVATION_BED, bed, context->worker_id);
    } else {
        go_to_warm_spot(colonist, context);
    }
}

float colonist_cold_limit(const Colonist *colonist)
{
    return COLONIST_COLD_SLEEP_BELOW - attribute_cold_shift(&colonist->attributes);
}

float colonist_heat_limit(const Colonist *colonist)
{
    return COLONIST_HOT_SLEEP_ABOVE + attribute_heat_shift(&colonist->attributes);
}

float colonist_sleep_temperature_factor(const Colonist *colonist, float celsius)
{
    float beyond =
        fmaxf(colonist_cold_limit(colonist) - celsius, celsius - colonist_heat_limit(colonist));
    if (beyond <= 0.0f) {
        return 1.0f;
    }
    return fmaxf(COLONIST_SLEEP_TEMPERATURE_FACTOR_MIN,
                 1.0f - COLONIST_SLEEP_LOSS_PER_DEGREE * beyond);
}

/* Temperature the colonist feels on a tile (mild without temperatures). */
static float felt_temperature(const ColonistContext *context, int x, int y, int z)
{
    if (context->temperatures == NULL) {
        return (COLONIST_COLD_SLEEP_BELOW + COLONIST_HOT_SLEEP_ABOVE) * 0.5f;
    }
    return hearths_felt_temperature(context->workstations, context->map, context->temperatures, x,
                                    y, z);
}

/* Degrees colder a settler feels for being wet. */
static float wet_chill(const Colonist *colonist)
{
    return COLONIST_WET_CHILL * colonist->wetness;
}

/* Temperature the colonist feels where it is, wet or not. */
static float felt_here(const Colonist *colonist, const ColonistContext *context)
{
    return felt_temperature(context, colonist->x, colonist->y, colonist->z) - wet_chill(colonist);
}

/* Out in the rain or snow, the colonist gets wet; elsewhere it dries, fast
 * beside a fire. */
static void update_wetness(Colonist *colonist, const ColonistContext *context)
{
    bool exposed = context->wetting > 0.0f &&
                   temperature_cover(context->map, colonist->x, colonist->y, colonist->z) ==
                       TEMPERATURE_COVER_OPEN;
    if (exposed) {
        colonist->wetness = fminf(1.0f, colonist->wetness + context->wetting / TICKS_PER_HOUR);
    } else if (colonist->wetness > 0.0f) {
        bool warm = felt_temperature(context, colonist->x, colonist->y, colonist->z) >=
                    COLONIST_DRY_WARM_CELSIUS;
        float rate = warm ? COLONIST_DRY_WARM_PER_HOUR : COLONIST_DRY_PER_HOUR;
        colonist->wetness = fmaxf(0.0f, colonist->wetness - rate / TICKS_PER_HOUR);
    }
}

static bool comfortable_to_sleep(const Colonist *colonist, float celsius)
{
    return celsius >= colonist_cold_limit(colonist) && celsius <= colonist_heat_limit(colonist);
}

typedef struct WarmSpotSearch {
    const Colonist *colonist;
    const ColonistContext *context;
} WarmSpotSearch;

/* Search filter: a free tile beside a fire where it feels right to sleep. */
static bool is_warm_spot(const Map *map, int x, int y, int z, void *userdata)
{
    const WarmSpotSearch *search = userdata;
    const ColonistContext *context = search->context;
    TilePos tile = {(int16_t)x, (int16_t)y, (int16_t)z};
    return workstation_at(context->workstations, x, y, z) == WORKSTATION_NONE &&
           map_structure(map, x, y, z) == STRUCTURE_NONE &&
           !reservations_taken_by_other(context->reservations, RESERVATION_BED, tile,
                                        context->worker_id) &&
           hearths_radiant_warmth(context->workstations, map, x, y, z) > 0.0f &&
           comfortable_to_sleep(search->colonist, felt_temperature(context, x, y, z));
}

static bool any_fire_burning(const ColonistContext *context)
{
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        const Workstation *station = &context->workstations->list[index];
        if (station->used && station->burning) {
            return true;
        }
    }
    return false;
}

/* With no bed to go to and too cold or hot here: walks to a spot beside a
 * burning fire that feels right, if there is one. Returns whether it goes. */
static bool go_to_warm_spot(Colonist *colonist, const ColonistContext *context)
{
    if (context->temperatures == NULL ||
        comfortable_to_sleep(colonist, felt_here(colonist, context)) ||
        !any_fire_burning(context)) {
        return false;
    }
    TilePos spot;
    WarmSpotSearch search = {colonist, context};
    if (!pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
                                 is_warm_spot, &search, COLONIST_WARM_SPOT_MAX_COST,
                                 &colonist->path, &spot)) {
        return false;
    }
    colonist->target = spot;
    reservations_add(context->reservations, RESERVATION_BED, spot, context->worker_id);
    return true;
}

/* Asleep on the ground, now and then: moves to a spot beside a fire when it
 * feels too cold or hot where it lies. */
static void stay_warm_asleep(Colonist *colonist, const ColonistContext *context)
{
    if (colonist_is_moving(colonist) || context->temperatures == NULL ||
        !search_allowed(colonist, SEARCH_WARM_SPOT) ||
        structure_is_bed(map_structure(context->map, colonist->x, colonist->y, colonist->z)) ||
        comfortable_to_sleep(colonist, felt_here(colonist, context)) ||
        !any_fire_burning(context)) {
        return;
    }
    colonist->search_cooldown[SEARCH_WARM_SPOT] = COLONIST_WARM_SPOT_CHECK_TICKS;
    TilePos here = current_tile(colonist);
    /* Its own spot, if it had one, is free again: it may stay there. */
    reservations_release_kind(context->reservations, RESERVATION_BED, context->worker_id);
    if (!go_to_warm_spot(colonist, context)) {
        reservations_add(context->reservations, RESERVATION_BED, here, context->worker_id);
    }
}

/* Rest gain multiplier where the colonist lies: its bed (or the ground),
 * and the temperature it feels there. */
/* Rest gain multiplier asleep where the colonist lies, feeling `felt`. */
static float rest_factor(const Colonist *colonist, const ColonistContext *context, float felt)
{
    StructureType bed = map_structure(context->map, colonist->x, colonist->y, colonist->z);
    float factor =
        structure_is_bed(bed) ? structure_def(bed)->rest_factor : COLONIST_GROUND_REST_FACTOR;
    return factor * colonist_sleep_temperature_factor(colonist, felt);
}

/* A sleep ends: after a real one, the colonist remembers how it slept. */
static void remember_sleep(Colonist *colonist, const ColonistContext *context)
{
    int ticks = colonist->sleep_ticks;
    if (ticks >= MOOD_SLEEP_MIN_TICKS) {
        Mood *mood = &colonist->mood;
        /* A bed rests better than the bare ground; a ground bedding does not. */
        StructureType lain_on = map_structure(context->map, colonist->x, colonist->y, colonist->z);
        bool bed = structure_is_bed(lain_on) &&
                   structure_def(lain_on)->rest_factor > COLONIST_GROUND_REST_FACTOR;
        float bad = MOOD_SLEEP_BAD_SHARE * (float)ticks;
        bool cold = (float)colonist->sleep_cold_ticks > bad;
        bool hot = (float)colonist->sleep_hot_ticks > bad;
        bool wet = (float)colonist->sleep_wet_ticks > bad;
        if (!bed) {
            mood_remember(mood, THOUGHT_SLEPT_ON_GROUND);
        }
        if (wet) {
            mood_remember(mood, THOUGHT_SLEPT_WET);
        }
        if (cold) {
            mood_remember(mood, THOUGHT_SLEPT_COLD);
        }
        if (hot) {
            mood_remember(mood, THOUGHT_SLEPT_HOT);
        }
        if (bed && !wet && !cold && !hot) {
            mood_remember(mood, THOUGHT_SLEPT_WELL);
        }
    }
    colonist->sleep_ticks = 0;
    colonist->sleep_cold_ticks = 0;
    colonist->sleep_hot_ticks = 0;
    colonist->sleep_wet_ticks = 0;
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
    /* Whether orders on roofs (taken down from below) may be open: the
     * search's hot path skips them otherwise. */
    bool roof_jobs;
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

static bool is_open_job(const WorkSearch *search, int x, int y, int z)
{
    if (search->zones) {
        if (gather_tile_job_in(search->gather_zones, search->map, x, y, z, search->jobs,
                               search->zone_priority) == GATHER_JOB_NONE) {
            return false;
        }
    } else {
        DesignationType type = designation_get(search->designations, x, y, z);
        if (type == DESIGNATION_NONE ||
            !(search->mask & WORK_TYPE_BIT(designation_def(type)->work_type)) ||
            !colonist_can_do(search->worker, type) ||
            designation_waits_for_support(search->designations, search->map, x, y, z) ||
            designation_forbidden(search->designations, search->map, x, y, z)) {
            return false;
        }
    }
    return !reservations_taken_by_other(search->reservations, RESERVATION_WORK,
                                        (TilePos){(int16_t)x, (int16_t)y, (int16_t)z},
                                        search->self);
}

/* Whether an order on a tile is worked on from the level below: a roof's
 * (taken down from under it, as it is built). */
static bool worked_from_below(const Map *map, int x, int y, int z)
{
    return map->roof_count > 0 && map_roof(map, x, y, z) != STRUCTURE_NONE;
}

/* The tile beside which one stands to work on `target`: on the level below
 * for a roof. */
static TilePos work_reach(const Map *map, TilePos target)
{
    if (worked_from_below(map, target.x, target.y, target.z)) {
        target.z--;
    }
    return target;
}

/* First open order on a roof over a tile next to (x, y): worked from below. */
static bool find_adjacent_roof_job(const WorkSearch *search, int x, int y, int z, TilePos *out)
{
    for (int i = 0; i < 4; i++) {
        int nx = x + NEIGHBOR_DX[i];
        int ny = y + NEIGHBOR_DY[i];
        if (worked_from_below(search->map, nx, ny, z + 1) && is_open_job(search, nx, ny, z + 1)) {
            *out = (TilePos){(int16_t)nx, (int16_t)ny, (int16_t)(z + 1)};
            return true;
        }
    }
    return false;
}

/* First open job next to (x, y), in a fixed order (deterministic): the
 * orders of the tiles, then the orders on the roofs over them. */
static bool find_adjacent_job(const WorkSearch *search, int x, int y, int z, TilePos *out)
{
    for (int i = 0; i < 4; i++) {
        int nx = x + NEIGHBOR_DX[i];
        int ny = y + NEIGHBOR_DY[i];
        if (is_open_job(search, nx, ny, z)) {
            *out = (TilePos){(int16_t)nx, (int16_t)ny, (int16_t)z};
            return true;
        }
    }
    return search->roof_jobs && find_adjacent_roof_job(search, x, y, z, out);
}

/* A tile to stand on while working: passable, next to an open job. */
static bool is_work_spot(const Map *map, int x, int y, int z, void *userdata)
{
    TilePos unused;
    return map_is_passable(map, x, y, z) && find_adjacent_job(userdata, x, y, z, &unused);
}

/* Whether a tile of `ids` (a row of the regions layer) next to x belongs to
 * `region`. */
static bool beside_region(const uint32_t *ids, int x, int y, int width, int height, uint32_t region)
{
    return (x > 0 && ids[x - 1] == region) || (x + 1 < width && ids[x + 1] == region) ||
           (y > 0 && ids[x - width] == region) || (y + 1 < height && ids[x + width] == region);
}

/* Whether an order on a roof tile is worked on from `region` (the level
 * below). Kept out of open_job_reachable()'s scan, which stays tight. */
static __attribute__((noinline)) bool roof_beside_region(const ColonistContext *context,
                                                         const uint32_t *row_ids, int x, int y,
                                                         int z, uint32_t region)
{
    const Map *map = context->map;
    return z > 0 && worked_from_below(map, x, y, z) &&
           beside_region(row_ids - (size_t)map->width * (size_t)map->height, x, y, map->width,
                         map->height, region);
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
    const int depth = designations->depth;
    const bool roofs = search->roof_jobs;
    if (region == REGION_NONE || !regions_current(regions, context->map) ||
        regions->width != width || regions->height != height || regions->depth != depth) {
        return true;
    }
    /* Designated tiles, or tiles of the zones that want something; all inside
     * the box of the layer. Only those next to the region count (on the level
     * over it for a roof): within its box grown by a tile. */
    int remaining = search->zones ? search->gather_zones->wanting_cells : designations->count;
    TileBox box = search->zones ? search->gather_zones->box : designations->box;
    TileBox near = regions_box(regions, context->map, region);
    box.x0 = box.x0 > near.x0 - 1 ? box.x0 : near.x0 - 1;
    box.y0 = box.y0 > near.y0 - 1 ? box.y0 : near.y0 - 1;
    box.z0 = box.z0 > near.z0 ? box.z0 : near.z0;
    box.x1 = box.x1 < near.x1 + 1 ? box.x1 : near.x1 + 1;
    box.y1 = box.y1 < near.y1 + 1 ? box.y1 : near.y1 + 1;
    box.z1 = box.z1 < near.z1 + 1 ? box.z1 : near.z1 + 1;
    for (int z = box.z0; z <= box.z1 && remaining > 0; z++) {
        for (int y = box.y0; y <= box.y1 && remaining > 0; y++) {
            size_t row = ((size_t)z * (size_t)height + (size_t)y) * (size_t)width;
            const uint8_t *types =
                search->zones ? &search->gather_zones->zone[row] : &designations->type[row];
            const uint32_t *ids = &regions->id[row];
            for (int x = box.x0; x <= box.x1; x++) {
                if (types[x] == 0 ||
                    (search->zones && search->gather_zones->list[types[x] - 1].wanted == 0)) {
                    continue;
                }
                remaining--; /* The scan stops once every order has been seen. */
                /* A roof is worked on from the level below. */
                bool next_to_region = beside_region(ids, x, y, width, height, region) ||
                                      (roofs && roof_beside_region(context, ids, x, y, z, region));
                if (next_to_region && is_open_job(search, x, y, z)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static uint32_t work_stamp(const ColonistContext *context)
{
    /* Built props and walls: digging orders waiting for support may go on. */
    return context->designations->revision + context->map->build_revision +
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

static bool is_dirty_tile(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const CleanSearch *search = userdata;
    return dirt_at(search->dirt, x, y, z) > 0 &&
           !reservations_taken_by_other(search->reservations, RESERVATION_CLEAN,
                                        (TilePos){(int16_t)x, (int16_t)y, (int16_t)z},
                                        search->self);
}

/* Walks to the nearest dirty tile to clean it. */
static bool try_clean_job(Colonist *colonist, const ColonistContext *context)
{
    CleanSearch search = {context->dirt, context->reservations, context->worker_id};
    TilePos found;
    if (!pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
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
           dirt_at(context->dirt, colonist->target.x, colonist->target.y, colonist->target.z) > 0;
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
    if (dirt_clean(context->dirt, target.x, target.y, target.z, 1) == 0) {
        set_idle(colonist, context, 0); /* The next dirty tile is probably next to it. */
    }
}

static HaulWorld haul_world(const ColonistContext *context)
{
    HaulWorld world = {context->plan_map,
                       context->plan_items,
                       context->stockpiles,
                       context->reservations,
                       context->pathfinder,
                       context->worker_id,
                       context->regions,
                       REGION_NONE,
                       0,
                       0};
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

static bool fetch_job_tool(Colonist *colonist, const ColonistContext *context);
static void take_job_tool(Colonist *colonist, const ColonistContext *context);

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
                         .map = context->plan_map,
                         .reservations = context->reservations,
                         .worker = colonist,
                         .mask = mask,
                         .self = context->worker_id};
    /* Only taking a roof apart is ordered on a roof tile (a roof site is
     * found among the construction sites). */
    search.roof_jobs = !zones && context->plan_map->roof_count > 0 &&
                       (mask & WORK_TYPE_BIT(WORK_CONSTRUCTION)) &&
                       designations->count_by_work[WORK_CONSTRUCTION] > 0;
    /* Zones of the highest priority first, whatever the distance. */
    uint32_t priorities = zones ? gather_zones_wanting_priorities(context->gather_zones) : 1u;
    uint32_t region = current_region(colonist, context);
    /* Every tile to work from is next to an order or a zone tile (under it for
     * a roof): the search heads for their box. */
    TileBox spots = zones ? context->gather_zones->box : designations->box;
    if (!tile_box_is_empty(&spots)) {
        spots.x0--;
        spots.y0--;
        spots.x1++;
        spots.y1++;
        spots.z0 -= search.roof_jobs ? 1 : 0;
    }
    TilePos spot, target = {0, 0, 0};
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
                pathfinder_find_nearest_in_box(
                    context->pathfinder, context->plan_map, current_tile(colonist), &spots,
                    is_work_spot, &search, COLONIST_WORK_SEARCH_MAX_COST, &colonist->path, &spot);
    }
    if (!found) {
        return false;
    }
    /* The search only stops on a spot next to an open job. */
    find_adjacent_job(&search, spot.x, spot.y, spot.z, &target);
    begin_task(colonist, context, TASK_WORK);
    colonist->target = target;
    colonist->work_type =
        (uint8_t)(zones ? gather_tile_job(context->gather_zones, context->plan_map, target.x,
                                          target.y, target.z, jobs)
                        : designation_get(designations, target.x, target.y, target.z));
    colonist->gathering = zones;
    colonist->station_phase = STATION_JOB_GO;
    reservations_add(context->reservations, RESERVATION_WORK, target, context->worker_id);
    fetch_job_tool(colonist, context); /* A detour for a digging stick, maybe. */
    return true;
}

static bool try_haul_job(Colonist *colonist, const ColonistContext *context)
{
    HaulWorld world = haul_world(context);
    world.region = current_region(colonist, context);
    world.from_y = colonist->y;
    world.from_z = colonist->z;
    if (!haul_any_job_possible(&world)) {
        return false;
    }
    HaulJob job;
    if (!haul_find_job(&world, current_tile(colonist), COLONIST_WORK_SEARCH_MAX_COST, &job) ||
        !pathfinder_find(context->pathfinder, context->plan_map, current_tile(colonist), job.source,
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
static bool try_assist(Colonist *colonist, const ColonistContext *context, WorkType type);
static bool try_refuel_job(Colonist *colonist, const ColonistContext *context);
static bool try_light_job(Colonist *colonist, const ColonistContext *context);
static bool next_to(int x, int y, int z, TilePos tile);
static bool is_next_to_target(const Map *map, int x, int y, int z, void *userdata);

/*
 * Takes the nearest job of the highest priority level that has one. Within a
 * level, work types are tried in the order of the priorities grid's columns
 * (WorkType order): for each, its orders, then its other jobs (construction
 * sites, bills, gathering zones, hauling, cleaning). Each search kind
 * backs off on failure until its stamp shows that something changed.
 */
/* Looks for work at priorities from the highest down to `lowest_priority`. */
static bool try_start_work(Colonist *colonist, const ColonistContext *context, int lowest_priority)
{
    int open_jobs =
        context->designations->count - context->reservations->count_by_kind[RESERVATION_WORK];
    bool designations_allowed =
        open_jobs > 0 && retry_allowed(colonist, context, SEARCH_WORK, colonist->work_search_stamp,
                                       work_stamp(context));
    bool hauling_allowed = retry_allowed(colonist, context, SEARCH_HAUL,
                                         colonist->haul_search_stamp, haul_stamp(context));
    bool stations_allowed = retry_allowed(colonist, context, SEARCH_STATION,
                                          colonist->station_search_stamp, station_stamp(context));
    bool constructions_allowed =
        context->constructions->used_count > 0 &&
        retry_allowed(colonist, context, SEARCH_CONSTRUCTION, colonist->construction_search_stamp,
                      construction_stamp(context));
    bool searched_constructions = false;
    bool supply_allowed = context->constructions->used_count > 0 &&
                          retry_allowed(colonist, context, SEARCH_SUPPLY,
                                        colonist->supply_search_stamp, construction_stamp(context));
    bool searched_supply = false;
    bool gathering_allowed = context->gather_zones->wanting_cells > 0 &&
                             retry_allowed(colonist, context, SEARCH_GATHER,
                                           colonist->gather_search_stamp, gather_stamp(context));
    bool searched_designations = false, searched_hauling = false, searched_stations = false;
    bool searched_gathering = false;
    int open_cleaning =
        context->dirt->dirty_count - context->reservations->count_by_kind[RESERVATION_CLEAN];
    bool cleaning_allowed =
        open_cleaning > 0 && retry_allowed(colonist, context, SEARCH_CLEAN,
                                           colonist->clean_search_stamp, clean_stamp(context));
    bool searched_cleaning = false;
    uint32_t hearth_stamp = station_stamp(context);
    bool refuel_allowed = retry_allowed(colonist, context, SEARCH_REFUEL,
                                        colonist->refuel_search_stamp, hearth_stamp);
    bool light_allowed =
        retry_allowed(colonist, context, SEARCH_LIGHT, colonist->light_search_stamp, hearth_stamp);
    bool searched_refuel = false, searched_light = false;
    /* Apprenticeship: a master doing a job of the work type that the colonist
     * lacks the level for, once its own jobs of that type are all out of
     * reach. */
    bool assist_allowed = context->colonists != NULL && search_allowed(colonist, SEARCH_ASSIST);
    bool searched_assist = false;
    /* Every searcher below returns true once the colonist has a job; it
     * resets the backoff of its kind. */
#define FOUND(kind)                                                                                \
    do {                                                                                           \
        colonist->search_failures[kind] = 0;                                                       \
        colonist->search_cooldown[kind] = 0;                                                       \
        colonist->search_quiet[kind] = 0;                                                          \
        return true;                                                                               \
    } while (0)

    for (int priority = WORK_PRIORITY_HIGHEST; priority <= lowest_priority; priority++) {
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
            case WORK_FIRE:
                if (light_allowed) {
                    searched_light = true;
                    if (try_light_job(colonist, context)) {
                        FOUND(SEARCH_LIGHT);
                    }
                }
                break;
            case WORK_HAULING:
                /* Hearths running low first, then construction sites
                 * waiting for materials. */
                if (refuel_allowed) {
                    searched_refuel = true;
                    if (try_refuel_job(colonist, context)) {
                        FOUND(SEARCH_REFUEL);
                    }
                }
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
            if (assist_allowed) {
                searched_assist = true;
                if (try_assist(colonist, context, (WorkType)type)) {
                    FOUND(SEARCH_ASSIST);
                }
            }
        }
    }
#undef FOUND
    if (searched_assist) {
        colonist->search_cooldown[SEARCH_ASSIST] = COLONIST_ASSIST_SEARCH_TICKS;
    }
    if (searched_refuel) {
        search_failed(colonist, SEARCH_REFUEL);
        colonist->refuel_search_stamp = hearth_stamp;
    }
    if (searched_light) {
        search_failed(colonist, SEARCH_LIGHT);
        colonist->light_search_stamp = hearth_stamp;
    }
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
    return haul_destination_room(context->stockpiles, context->items, dest.x, dest.y, dest.z,
                                 stack) > 0;
}

/* Items of the load in hand (or of `stack` when empty) that can still be
 * added: limited by what the colonist can carry and the room left on the
 * destination tile. */
static int haul_room_left(const Colonist *colonist, const ColonistContext *context, ItemStack stack)
{
    TilePos dest = colonist->haul_destination;
    ItemStack load = colonist->carried.count > 0 ? colonist->carried : stack;
    int room =
        haul_destination_room(context->stockpiles, context->items, dest.x, dest.y, dest.z, load);
    int capacity = colonist_carry_capacity(colonist, (ItemType)load.type);
    int left = (room < capacity ? room : capacity) - (int)colonist->carried.count;
    return left > 0 ? left : 0;
}

/* Stops collecting and heads for the destination with the load in hand. */
static bool start_delivery(Colonist *colonist, const ColonistContext *context)
{
    reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE, context->worker_id);
    colonist->haul_delivering = true;
    colonist->path_index = 0;
    return pathfinder_find(context->pathfinder, context->plan_map, current_tile(colonist),
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
                                colonist->haul_destination, COLONIST_HAUL_COLLECT_MAX_COST, &extra,
                                &colonist->path)) {
        return false;
    }
    reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE, context->worker_id);
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
/* The items a settler checks on a tile: the real ones when it can see the
 * tile, else what it remembers of it (it finds out what changed there when it
 * gets there). */
static const Items *items_checked(const ColonistContext *context, int x, int y, int z)
{
    return context->vision == NULL || vision_seen(context->vision, x, y, z) ? context->items
                                                                            : context->plan_items;
}

static const Map *map_checked(const ColonistContext *context, int x, int y, int z)
{
    return context->vision == NULL || vision_seen(context->vision, x, y, z) ? context->map
                                                                            : context->plan_map;
}

static bool haul_still_valid(Colonist *colonist, const ColonistContext *context)
{
    if (!colonist->haul_delivering) {
        ItemStack stack = items_get_allowed(
            items_checked(context, colonist->target.x, colonist->target.y, colonist->target.z),
            colonist->target.x, colonist->target.y, colonist->target.z);
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
        ItemStack stack = items_get_allowed(context->items, colonist->x, colonist->y, colonist->z);
        int wanted = haul_room_left(colonist, context, stack);
        int taken = items_remove(context->items, colonist->x, colonist->y, colonist->z,
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
    int added = items_forbidden(context->items, dest.x, dest.y, dest.z) /* Left alone. */
                    ? 0
                    : items_add(context->items, dest.x, dest.y, dest.z, colonist->carried);
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
    bool own =
        reservations_owner(context->reservations, RESERVATION_WORK, target) == context->worker_id;
    if (colonist->gathering) {
        GatherJob job = (GatherJob)colonist->work_type;
        return own && (unsigned)job < GATHER_JOB_COUNT &&
               (gather_jobs_for(colonist, enabled_work_types(colonist)) & GATHER_JOB_BIT(job)) &&
               gather_job_available(context->gather_zones,
                                    map_checked(context, target.x, target.y, target.z), target.x,
                                    target.y, target.z, job);
    }
    const DesignationDef *def = designation_def((DesignationType)colonist->work_type);
    return def != NULL && colonist->work_priorities[def->work_type] != WORK_PRIORITY_DISABLED &&
           colonist_can_do(colonist, (DesignationType)colonist->work_type) &&
           designation_get(context->designations, target.x, target.y, target.z) ==
               colonist->work_type &&
           !designation_forbidden(context->designations, context->map, target.x, target.y,
                                  target.z) &&
           reservations_owner(context->reservations, RESERVATION_WORK, target) ==
               context->worker_id;
}

/* Work speed factor from an apprentice helping (see COLONIST_APPRENTICE_HELP). */
static float assistance(const Colonist *colonist, const ColonistContext *context)
{
    return colonist->assisted_until >= context->tick ? 1.0f + COLONIST_APPRENTICE_HELP : 1.0f;
}

static void work(Colonist *colonist, const ColonistContext *context)
{
    Designations *designations = context->designations;
    TilePos target = colonist->target;
    int distance = abs(colonist->x - target.x) + abs(colonist->y - target.y);
    DesignationType type = designation_get(designations, target.x, target.y, target.z);
    if (!job_still_valid(colonist, context)) {
        set_idle(colonist, context, 0); /* The order was cancelled. */
        return;
    }
    if (colonist->station_phase == STATION_JOB_FETCH) {
        take_job_tool(colonist, context);
        return;
    }
    if (distance != 1 || colonist->z != work_reach(context->map, target).z) {
        set_idle(colonist, context, 0); /* Not there. */
        return;
    }
    if (colonist->gathering) {
        GatherJob job = (GatherJob)colonist->work_type;
        KnowledgeType knowledge = gather_job_def(job)->knowledge;
        float speed = colonist_work_speed(colonist, knowledge) *
                      work_with_tool(colonist, context, gather_job_def(job)->tool_use) *
                      assistance(colonist, context);
        /* Products depend on the level before this tick's learning. */
        bool done = gather_zone_add_work(context->gather_zones, context->map, context->items,
                                         target.x, target.y, target.z, job, speed,
                                         colonist_work_level(colonist, knowledge), context->rng);
        colonist_learn_by_doing(colonist, knowledge, 1, context->tick);
        /* After each job, choose again: a zone may keep a settler busy
         * forever, and a more important job may be waiting. */
        if (done) {
            set_idle(colonist, context, 0);
        }
        return;
    }
    if (designation_waits_for_support(designations, context->map, target.x, target.y, target.z)) {
        set_idle(colonist, context, 0); /* A support went: the order waits. */
        return;
    }
    if (!designation_can_apply(context->map, target.x, target.y, target.z, type) ||
        !designation_ground_free(context->constructions, context->workstations, context->stockpiles,
                                 type, target.x, target.y, target.z)) {
        designation_set(designations, context->map, target.x, target.y, target.z, DESIGNATION_NONE);
        set_idle(colonist, context, 0);
        return;
    }
    KnowledgeType knowledge = designation_def(type)->knowledge;
    ToolUse use = designation_def(type)->tool_use;
    float speed = colonist_work_speed(colonist, knowledge) * assistance(colonist, context);
    if (use != TOOL_USE_NONE) {
        speed *= work_with_tool(colonist, context, use);
    }
    /* Products depend on the level before this tick's learning. */
    bool done = designation_add_work(designations, context->map, context->items, target.x, target.y,
                                     target.z, speed, colonist_work_level(colonist, knowledge),
                                     context->rng);
    colonist_learn_by_doing(colonist, knowledge, 1, context->tick);
    int site = construction_at(context->constructions, target.x, target.y, target.z);
    const ConstructionSite *refresh = construction_get(context->constructions, site);
    if (done && type == DESIGNATION_DECONSTRUCT && refresh != NULL && refresh->refresh &&
        !site_still_wanted(context, refresh)) {
        /* A refresh site on the structure taken apart: its materials back. */
        ItemStack returned[STRUCTURE_MATERIAL_SLOTS];
        int count = constructions_remove(context->constructions, site, returned);
        for (int i = 0; i < count; i++) {
            items_drop(context->items, context->map, target.x, target.y, target.z, returned[i]);
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
    /* A forbidden station is as good as gone: the job stops. */
    return station != NULL && station->number == colonist->station_number && !station->forbidden
               ? station
               : NULL;
}

static TilePos station_tile(const Workstation *station)
{
    return (TilePos){station->x, station->y, station->z};
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
            !items_exist(context->plan_items, types, 1)) {
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
            (recipe_needs_fire(recipe) && !station->burning) ||
            !colonist_can_craft(colonist, recipe) || !colonist_level_fits_bill(colonist, bill) ||
            !bill_wants_more(bill, stored_count(context, bill)) ||
            (!context->no_tools && !tools_obtainable(colonist, context, def))) {
            continue;
        }
        /* The tools still to fetch may be of the input's type (a cobble to
         * knap with): they must leave enough to work on. */
        ItemTypeMask to_fetch[RECIPE_TOOL_MAX] = {0};
        for (int slot = 0; !context->no_tools && slot < RECIPE_TOOL_MAX; slot++) {
            if (def->tools[slot] != 0 && colonist_tool_index(colonist, def->tools[slot]) < 0) {
                to_fetch[slot] = def->tools[slot];
            }
        }
        if (!in_progress &&
            (context->free_crafts ? bill_free_input(bill).count == 0
                                  : !bill_has_input(bill, context->plan_items->totals, to_fetch))) {
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

static bool is_station_job(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const StationSearch *search = userdata;
    const ColonistContext *context = search->context;
    const Workstation *station = workstation_get_const(
        context->workstations, workstation_at(context->workstations, x, y, z));
    if (station == NULL || station->forbidden ||
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

static bool is_fetchable(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const FetchSearch *search = userdata;
    ItemStack stack = items_get_allowed(search->items, x, y, z);
    return stack.type != ITEM_NONE && (search->types & ITEM_TYPE_BIT(stack.type)) &&
           stack.quality >= search->min_quality && stack.count >= search->min_count &&
           (search->match.count == 0 || item_stacks_match(stack, search->match)) &&
           (search->min_factor <= 0.0f ||
            tool_speed_factor(search->use, stack) > search->min_factor) &&
           !reservations_taken_by_other(search->reservations, RESERVATION_HAUL_SOURCE,
                                        (TilePos){(int16_t)x, (int16_t)y, (int16_t)z},
                                        search->self);
}

/* Heads for a stack found by `search`, looked for around `from` within
 * `max_cost`. */
static bool start_fetch(Colonist *colonist, const ColonistContext *context, FetchSearch *search,
                        TilePos from, float max_cost, bool tool)
{
    TilePos found;
    if (!any_tile_reachable(context, from, is_fetchable, search, max_cost) ||
        !pathfinder_find_nearest(context->pathfinder, context->plan_map, from, is_fetchable, search,
                                 max_cost, NULL, &found) ||
        !pathfinder_find(context->pathfinder, context->plan_map, current_tile(colonist), found,
                         COLONIST_WORK_SEARCH_MAX_COST, &colonist->path)) {
        return false;
    }
    reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE, context->worker_id);
    reservations_add(context->reservations, RESERVATION_HAUL_SOURCE, found, context->worker_id);
    colonist->fetch = found;
    colonist->fetch_tool = tool;
    colonist->station_phase = STATION_JOB_FETCH;
    colonist->path_index = 0;
    return true;
}

static FetchSearch fetch_search(const ColonistContext *context, ItemTypeMask types)
{
    FetchSearch search = {context->plan_items,
                          context->reservations,
                          context->worker_id,
                          types,
                          ITEM_QUALITY_CRUDE,
                          1,
                          {.type = ITEM_NONE},
                          TOOL_USE_NONE,
                          0.0f};
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
    reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE, context->worker_id);
    colonist->station_phase = STATION_JOB_GO;
    colonist->path_index = 0;
    TilePos tile = station_tile(station);
    if (hearth_is(station)) { /* Worked from beside, not from inside the fire. */
        if (next_to(colonist->x, colonist->y, colonist->z, tile)) {
            path_clear(&colonist->path);
            return true;
        }
        TilePos spot;
        return pathfinder_find_nearest(context->pathfinder, context->plan_map,
                                       current_tile(colonist), is_next_to_target, &tile,
                                       COLONIST_WORK_SEARCH_MAX_COST, &colonist->path, &spot);
    }
    if (colonist->x == tile.x && colonist->y == tile.y && colonist->z == tile.z) {
        path_clear(&colonist->path);
        return true;
    }
    return pathfinder_find(context->pathfinder, context->plan_map, current_tile(colonist), tile,
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

/* The tool use of the current work job (gathering or order), and whether it
 * is worth a detour for a better tool (digging). */
static ToolUse job_tool_use(const Colonist *colonist, bool *fetches)
{
    if (colonist->gathering) {
        const GatherJobDef *def = gather_job_def((GatherJob)colonist->work_type);
        *fetches = def != NULL && def->fetches_tool;
        return def != NULL ? def->tool_use : TOOL_USE_NONE;
    }
    const DesignationDef *def = designation_def((DesignationType)colonist->work_type);
    ToolUse use = def != NULL ? def->tool_use : TOOL_USE_NONE;
    *fetches = use != TOOL_USE_NONE;
    return use;
}

/* On the way to a work job that a tool speeds up and that is worth a detour
 * (digging), a better tool than the carried one lying close by: the colonist
 * heads for it first (see take_job_tool()). */
static bool fetch_job_tool(Colonist *colonist, const ColonistContext *context)
{
    bool fetches;
    ToolUse use = job_tool_use(colonist, &fetches);
    if (!fetches || context->no_tools) {
        return false;
    }
    float factor = colonist_tool_factor(colonist, use, ~(ItemTypeMask)0);
    if (factor >= tool_best_possible_factor(use)) {
        return false;
    }
    ItemTypeMask types = 0;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        if (tool_type_factor(use, (ItemType)type) > factor) {
            types |= ITEM_TYPE_BIT(type);
        }
    }
    FetchSearch search = fetch_search(context, types);
    search.use = use;
    search.min_factor = factor;
    return start_fetch(colonist, context, &search, current_tile(colonist),
                       COLONIST_TOOL_SWAP_MAX_COST, true);
}

/* At the tool fetched on the way to a work job: takes it (a carried tool of
 * the same type is left in its place), then heads for the job. */
static void take_job_tool(Colonist *colonist, const ColonistContext *context)
{
    bool fetches;
    ToolUse use = job_tool_use(colonist, &fetches);
    ItemStack stack = items_get_allowed(context->items, colonist->x, colonist->y, colonist->z);
    ItemStack replaced;
    if (colonist->x == colonist->fetch.x && colonist->y == colonist->fetch.y &&
        colonist->z == colonist->fetch.z && stack.type != ITEM_NONE &&
        tool_speed_factor(use, stack) > colonist_tool_factor(colonist, use, ~(ItemTypeMask)0) &&
        colonist_swap_tool(colonist, stack, &replaced)) {
        items_remove(context->items, colonist->x, colonist->y, colonist->z, 1);
        if (replaced.count > 0) {
            items_drop(context->items, context->map, colonist->x, colonist->y, colonist->z,
                       replaced);
        }
    }
    reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE, context->worker_id);
    colonist->station_phase = STATION_JOB_GO;
    colonist->path_index = 0;
    TilePos spot, reach = work_reach(context->map, colonist->target);
    if (next_to(colonist->x, colonist->y, colonist->z, reach)) {
        path_clear(&colonist->path);
    } else if (!pathfinder_find_nearest(context->pathfinder, context->plan_map,
                                        current_tile(colonist), is_next_to_target, &reach,
                                        COLONIST_WORK_SEARCH_MAX_COST, &colonist->path, &spot)) {
        set_idle(colonist, context, 0);
    }
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
    for (int slot = 0; !context->no_tools && slot < RECIPE_TOOL_MAX; slot++) {
        if (def->tools[slot] != 0 && colonist_tool_index(colonist, def->tools[slot]) < 0) {
            FetchSearch search = fetch_search(context, def->tools[slot]);
            return start_fetch(colonist, context, &search, current_tile(colonist),
                               COLONIST_WORK_SEARCH_MAX_COST, true);
        }
    }
    if (station->work_input.count == 0 && !context->no_tools &&
        fetch_better_tool(colonist, context, def)) {
        return true;
    }
    bool in_progress = station->work_input.count > 0;
    if (!in_progress && !context->free_crafts && colonist->carried.count < def->input_count) {
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
    if (station == NULL || reservations_owner(context->reservations, RESERVATION_STATION,
                                              station_tile(station)) != context->worker_id) {
        return false;
    }
    if (colonist->work_priorities[WORK_CRAFTING] == WORK_PRIORITY_DISABLED ||
        colonist->bill >= station->bill_count ||
        station->bills[colonist->bill].recipe != colonist->recipe ||
        !colonist_can_craft(colonist, (RecipeType)colonist->recipe) ||
        (recipe_needs_fire((RecipeType)colonist->recipe) && !station->burning)) {
        return false; /* (A craft whose fire went out waits on the station.) */
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
    if (!pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
                                 is_station_job, &search, COLONIST_WORK_SEARCH_MAX_COST, NULL,
                                 &tile)) {
        return false;
    }
    int index = workstation_at(context->workstations, tile.x, tile.y, tile.z);
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
    ItemStack stack = items_get_allowed(context->items, colonist->x, colonist->y, colonist->z);
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
        items_remove(context->items, colonist->x, colonist->y, colonist->z, 1);
        if (replaced.count > 0) {
            items_drop(context->items, context->map, colonist->x, colonist->y, colonist->z,
                       replaced);
        }
        return true;
    }
    int wanted =
        recipe_def((RecipeType)colonist->recipe)->input_count - (int)colonist->carried.count;
    int capacity =
        colonist_carry_capacity(colonist, (ItemType)stack.type) - (int)colonist->carried.count;
    wanted = wanted < capacity ? wanted : capacity;
    if (wanted <= 0 ||
        (colonist->carried.count > 0 && !item_stacks_match(stack, colonist->carried))) {
        return false;
    }
    int taken = items_remove(context->items, colonist->x, colonist->y, colonist->z,
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
        if (context->free_crafts &&
            colonist->carried.count < recipe_def((RecipeType)colonist->recipe)->input_count) {
            /* Out of nothing (what it holds, if anything, stays in hand). */
            if (!workstation_start_free_craft(workstations, colonist->station, colonist->bill)) {
                return false;
            }
        } else if (!workstation_start_craft(workstations, colonist->station,
                                            (RecipeType)colonist->recipe, colonist->carried)) {
            return false;
        } else {
            colonist->carried = (ItemStack){.type = ITEM_NONE};
        }
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
    float needed = gather_clear_work(context->map, tile.x, tile.y, tile.z);
    if (needed <= 0.0f) {
        return false;
    }
    *progress += colonist_work_speed(colonist, KNOWLEDGE_NONE) *
                 work_with_tool(colonist, context,
                                gather_clear_tool_use(context->map, tile.x, tile.y, tile.z));
    if (*progress >= needed) {
        gather_clear_plant(context->map, context->items, tile.x, tile.y, tile.z,
                           colonist_work_level(colonist, KNOWLEDGE_GATHERING), context->rng);
        *progress = 0.0f;
    }
    return true;
}

/*
 * Speed factor of one tick of a craft: the recipe's tool of the first set
 * (core/tool.h). Every tool of the recipe wears. 0 when one of them is not
 * carried (it broke); with the cheat GameDebug.no_tools, a missing tool is
 * no matter and the first one's speed is a standard tool's.
 */
static float craft_tool_factor(Colonist *colonist, const ColonistContext *context,
                               const RecipeDef *recipe)
{
    int used[RECIPE_TOOL_MAX];
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        used[slot] = -1;
        if (recipe->tools[slot] != 0) {
            used[slot] = slot == 0
                             ? working_tool(colonist, recipe->tool_use, recipe->tools[slot], true)
                             : colonist_tool_index(colonist, recipe->tools[slot]);
            if (used[slot] < 0 && !context->no_tools) {
                return 0.0f;
            }
        }
    }
    float bare =
        context->no_tools ? NO_TOOLS_FACTOR : tool_type_factor(recipe->tool_use, ITEM_NONE);
    float factor =
        used[0] >= 0 ? tool_speed_factor(recipe->tool_use, colonist->tools[used[0]]) : bare;
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
            !pick_up_fetched(colonist, context) || !plan_station_job(colonist, context)) {
            set_idle(colonist, context, 0); /* Drops what it holds. */
        }
        return;
    case STATION_JOB_GO:
        if (!(hearth_is(station)
                  ? next_to(colonist->x, colonist->y, colonist->z, tile)
                  : colonist->x == tile.x && colonist->y == tile.y && colonist->z == tile.z) ||
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
    float tool_factor = craft_tool_factor(colonist, context, recipe);
    if (tool_factor <= 0.0f) {
        set_idle(colonist, context, 0); /* A tool broke: fetch another one first. */
        return;
    }
    KnowledgeType knowledge = recipe->knowledge;
    /* Products depend on the level before this tick's learning. */
    bool done = workstation_add_craft_work(
        context->workstations, colonist->station, colonist->bill, context->items, context->map,
        colonist_work_speed(colonist, knowledge) * tool_factor * assistance(colonist, context),
        colonist_work_level(colonist, knowledge), context->rng);
    colonist_learn_by_doing(colonist, knowledge, 1, context->tick);
    if (done) {
        dirt_spread(context->dirt, context->map, tile.x, tile.y, tile.z,
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

static bool next_to(int x, int y, int z, TilePos tile)
{
    return z == tile.z && abs(x - tile.x) + abs(y - tile.y) == 1;
}

/* A site is reserved on its head, whichever of its tiles a builder works by. */
static TilePos site_origin(const ConstructionSite *site)
{
    return (TilePos){site->x, site->y, site->z};
}

/* A refresh site is only worth it while its structure stands. */
static bool site_still_wanted(const ColonistContext *context, const ConstructionSite *site)
{
    return !site->refresh ||
           (map_structure_is_origin(map_checked(context, site->x, site->y, site->z), site->x,
                                    site->y, site->z) &&
            map_structure(map_checked(context, site->x, site->y, site->z), site->x, site->y,
                          site->z) == (StructureType)site->type);
}

/* Whether some material of a site is still to bring, nobody brings it yet,
 * and some lies about. */
static bool site_wants_material(const ColonistContext *context, const ConstructionSite *site)
{
    if (construction_forbidden(site, context->plan_map)) {
        return false;
    }
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        if (construction_unpromised(site, slot) > 0 &&
            items_exist(context->plan_items, ITEM_TYPE_BIT(construction_material(site, slot)), 1)) {
            return true;
        }
    }
    return false;
}

/* Whether a site offers work to this colonist: free, buildable by it, and
 * either ready for work or missing a material it can go for. */
static bool site_has_job(const ColonistContext *context, const Colonist *worker, int index)
{
    const ConstructionSite *site = construction_get(context->constructions, index);
    if (site == NULL || !colonist_can_build_site(worker, site) ||
        reservations_taken_by_other(context->reservations, RESERVATION_CONSTRUCTION,
                                    site_origin(site), context->worker_id)) {
        return false;
    }
    return construction_can_work(site, context->plan_map) || site_wants_material(context, site);
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
        } else if (site_has_job(context, worker, index)) {
            return true;
        }
    }
    return false;
}

typedef struct SiteSearch {
    const ColonistContext *context;
    const Colonist *worker;
} SiteSearch;

/* First site with a job next to (x, y), in a fixed order (on each tile, the
 * structure's site before the roof's over it): the tile it is worked by (on
 * level z) and its index. */
static bool find_adjacent_site(const SiteSearch *search, int x, int y, int z, TilePos *out,
                               int *index)
{
    for (int i = 0; i < 4; i++) {
        int nx = x + NEIGHBOR_DX[i], ny = y + NEIGHBOR_DY[i];
        for (int roof = 0; roof < 2; roof++) {
            int site = construction_worked_from(search->context->constructions, nx, ny, z, roof);
            if (site_has_job(search->context, search->worker, site)) {
                *out = (TilePos){(int16_t)nx, (int16_t)ny, (int16_t)z};
                *index = site;
                return true;
            }
        }
    }
    return false;
}

static bool is_site_spot(const Map *map, int x, int y, int z, void *userdata)
{
    TilePos unused;
    int index;
    return map_is_passable(map, x, y, z) && find_adjacent_site(userdata, x, y, z, &unused, &index);
}

static bool is_next_to_target(const Map *map, int x, int y, int z, void *userdata)
{
    return map_is_passable(map, x, y, z) && next_to(x, y, z, *(const TilePos *)userdata);
}

/* Heads for a tile next to the site: the delivery or the work happens there. */
static bool go_to_site(Colonist *colonist, const ColonistContext *context)
{
    reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE, context->worker_id);
    colonist->station_phase = STATION_JOB_GO;
    colonist->path_index = 0;
    if (next_to(colonist->x, colonist->y, colonist->z, colonist->target)) {
        path_clear(&colonist->path);
        return true;
    }
    TilePos spot;
    return pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
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
            colonist->supply_promised = construction_promise(context->constructions, colonist->site,
                                                             slot, left < load ? left : load);
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

static bool construction_job_still_valid(const Colonist *colonist, const ColonistContext *context)
{
    const ConstructionSite *site = job_site(colonist, context);
    return site != NULL && site_still_wanted(context, site) &&
           reservations_owner(context->reservations, RESERVATION_CONSTRUCTION, site_origin(site)) ==
               context->worker_id &&
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
    if (!pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
                                 is_site_spot, &search, COLONIST_WORK_SEARCH_MAX_COST, NULL,
                                 &spot)) {
        return false;
    }
    int index = CONSTRUCTION_NONE;
    find_adjacent_site(&search, spot.x, spot.y, spot.z, &target, &index);
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

/* First site next to (x, y), in a fixed order (as find_adjacent_site()),
 * missing a material nobody brings yet that lies about. */
static bool find_supply_site(const ColonistContext *context, int x, int y, int z, TilePos *out,
                             int *index)
{
    for (int i = 0; i < 4; i++) {
        int nx = x + NEIGHBOR_DX[i], ny = y + NEIGHBOR_DY[i];
        for (int roof = 0; roof < 2; roof++) {
            int found = construction_worked_from(context->constructions, nx, ny, z, roof);
            const ConstructionSite *site = construction_get(context->constructions, found);
            if (site != NULL && site_still_wanted(context, site) &&
                site_wants_material(context, site)) {
                *out = (TilePos){(int16_t)nx, (int16_t)ny, (int16_t)z};
                *index = found;
                return true;
            }
        }
    }
    return false;
}

static bool is_supply_spot(const Map *map, int x, int y, int z, void *userdata)
{
    TilePos unused;
    int index;
    return map_is_passable(map, x, y, z) && find_supply_site(userdata, x, y, z, &unused, &index);
}

/* A hauler brings materials to the nearest site that waits for some. */
static bool try_supply_job(Colonist *colonist, const ColonistContext *context)
{
    if (context->constructions->used_count == 0 || !any_site_job(context, NULL)) {
        return false;
    }
    TilePos spot, target;
    if (!pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
                                 is_supply_spot, (void *)context, COLONIST_WORK_SEARCH_MAX_COST,
                                 NULL, &spot)) {
        return false;
    }
    int index = CONSTRUCTION_NONE;
    find_supply_site(context, spot.x, spot.y, spot.z, &target, &index);
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
    ItemStack stack = items_get_allowed(context->items, colonist->x, colonist->y, colonist->z);
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
    int taken = items_remove(context->items, colonist->x, colonist->y, colonist->z,
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
        int x, y, z;
        construction_tile(site, part, &x, &y, &z);
        stockpiles_remove_rect(context->stockpiles, x, y, x, y, z);
        gather_zones_remove_rect(context->gather_zones, x, y, x, y, z);
    }
}

/* Clears the plants of a site's tiles, one after the other (see
 * clear_plant_first()). */
static bool clear_site_first(Colonist *colonist, const ColonistContext *context,
                             ConstructionSite *site)
{
    for (int part = 0; part < construction_length(site); part++) {
        int x, y, z;
        construction_tile(site, part, &x, &y, &z);
        if (clear_plant_first(colonist, context, (TilePos){(int16_t)x, (int16_t)y, (int16_t)z},
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
        if (!next_to(colonist->x, colonist->y, colonist->z, colonist->target)) {
            set_idle(colonist, context, 0);
            return;
        }
        if (colonist->carried.count > 0) {
            int taken =
                construction_deliver(context->constructions, colonist->site, colonist->carried);
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
    if (!next_to(colonist->x, colonist->y, colonist->z, colonist->target)) {
        set_idle(colonist, context, 0);
        return;
    }
    ConstructionSite *clearing = construction_get_mutable(context->constructions, colonist->site);
    if (clear_site_first(colonist, context, clearing)) {
        return;
    }
    ConstructionSite done = *site; /* Gone from the list once built. */
    KnowledgeType knowledge = construction_knowledge(site);
    float speed = colonist_work_speed(colonist, knowledge) *
                  work_with_tool(colonist, context, TOOL_USE_BUILDING) *
                  assistance(colonist, context);
    /* The grade depends on the level before this tick's learning. */
    bool built = construction_add_work(context->constructions, context->map, context->items,
                                       context->workstations, colonist->site, speed,
                                       colonist_work_level(colonist, knowledge), context->rng);
    colonist_learn_by_doing(colonist, knowledge, 1, context->tick);
    if (built) {
        structure_built(context, &done);
        set_idle(colonist, context, 0);
    }
}

/* ---- Apprenticeship and conversation ------------------------------------- */

bool colonist_can_chat(const Colonist *colonist)
{
    if (colonist_in_break(colonist)) {
        return false; /* Wants to be left alone. */
    }
    switch (colonist->task) {
    case TASK_IDLE:
    case TASK_WANDER: return true;
    case TASK_EAT:
    case TASK_DRINK:
    case TASK_WORK:
    case TASK_CRAFT:
    case TASK_CONSTRUCT:
    case TASK_CLEAN:
    case TASK_LIGHT:
    case TASK_ASSIST: return !colonist_is_moving(colonist);
    case TASK_REFUEL:
    case TASK_MOVE:
    case TASK_STORY:
    case TASK_LISTEN:
    case TASK_SLEEP:
    case TASK_HAUL:
    case TASK_SUPPLY:
    case TASK_COUNT: break;
    }
    return false;
}

static void set_need(ColonistJobNeed *need, KnowledgeType knowledge, float min_level,
                     WorkType work_type)
{
    need->knowledge = knowledge;
    need->min_level = min_level;
    need->work_type = work_type;
}

bool colonist_job_need(const Colonist *colonist, const ColonistContext *context,
                       ColonistJobNeed *need)
{
    set_need(need, KNOWLEDGE_NONE, 0.0f, WORK_TYPE_COUNT);
    switch (colonist->task) {
    case TASK_CRAFT: {
        const RecipeDef *def = recipe_def((RecipeType)colonist->recipe);
        if (def != NULL) {
            set_need(need, def->knowledge, def->min_level, WORK_CRAFTING);
        }
        break;
    }
    case TASK_CONSTRUCT: {
        const ConstructionSite *site = job_site(colonist, context);
        if (site != NULL) {
            set_need(need, construction_knowledge(site), construction_min_level(site),
                     WORK_CONSTRUCTION);
        }
        break;
    }
    case TASK_WORK:
        if (colonist->gathering) {
            const GatherJobDef *def = gather_job_def((GatherJob)colonist->work_type);
            if (def != NULL) {
                set_need(need, def->knowledge, def->min_level, def->work_type);
            }
        } else {
            const DesignationDef *def = designation_def((DesignationType)colonist->work_type);
            if (def != NULL) {
                set_need(need, def->knowledge, def->min_level, def->work_type);
            }
        }
        break;
    default: break;
    }
    return (unsigned)need->knowledge < KNOWLEDGE_COUNT &&
           (unsigned)need->work_type < WORK_TYPE_COUNT;
}

bool colonist_at_work(const Colonist *colonist)
{
    if (colonist_is_moving(colonist)) {
        return false;
    }
    switch (colonist->task) {
    case TASK_CRAFT:
    case TASK_CONSTRUCT: return colonist->station_phase == STATION_JOB_WORK;
    case TASK_WORK: /* Beside its target, or under the roof's edge. */
        return next_to(colonist->x, colonist->y, colonist->z, colonist->target) ||
               next_to(colonist->x, colonist->y, colonist->z + 1, colonist->target);
    default: return false;
    }
}

/* A living colonist, NULL when the handle is stale or nobody assists anyone
 * in this context. */
static Colonist *colonist_by_id(const ColonistContext *context, EntityId id)
{
    if (context->colonists == NULL || !pool_is_alive(context->colonist_pool, id)) {
        return NULL;
    }
    return &context->colonists[id.index];
}

/* Whether `apprentice` can learn the job `need` from `master`: it does that
 * kind of work, lacks the level for the job, and the master knows more. */
static bool can_learn_from(const Colonist *apprentice, const Colonist *master,
                           const ColonistJobNeed *need)
{
    double own = apprentice->knowledge[need->knowledge];
    return apprentice->work_priorities[need->work_type] != WORK_PRIORITY_DISABLED &&
           own < need->min_level && master->knowledge[need->knowledge] > own;
}

/* Whether someone other than `self` assists this master. */
static bool has_apprentice(const ColonistContext *context, EntityId master, EntityId self)
{
    for (uint32_t slot = 0; slot < context->colonist_pool->high_water; slot++) {
        EntityId id = pool_id_at(context->colonist_pool, slot);
        if (entity_id_is_none(id) || entity_id_equal(id, self)) {
            continue;
        }
        const Colonist *other = &context->colonists[slot];
        if (other->task == TASK_ASSIST && entity_id_equal(other->mentor, master)) {
            return true;
        }
    }
    return false;
}

/* One of the 8 tiles around (x, y), not (x, y) itself. */
static bool beside(int x, int y, int z, int to_x, int to_y, int to_z)
{
    int dx = abs(x - to_x), dy = abs(y - to_y);
    return z == to_z && dx <= 1 && dy <= 1 && (dx | dy) != 0;
}

static bool is_beside_target(const Map *map, int x, int y, int z, void *userdata)
{
    const TilePos *target = userdata;
    return map_is_passable(map, x, y, z) && beside(x, y, z, target->x, target->y, target->z);
}

/* Heads for one of the 8 tiles around `at`. */
static bool go_beside_tile(Colonist *colonist, const ColonistContext *context, TilePos at)
{
    colonist->path_index = 0;
    if (beside(colonist->x, colonist->y, colonist->z, at.x, at.y, at.z)) {
        path_clear(&colonist->path);
        return true;
    }
    TilePos spot;
    return pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
                                   is_beside_target, &at, COLONIST_WORK_SEARCH_MAX_COST,
                                   &colonist->path, &spot);
}

/* Heads for a tile beside the master. */
static bool go_beside(Colonist *colonist, const ColonistContext *context, const Colonist *master)
{
    return go_beside_tile(colonist, context,
                          (TilePos){(int16_t)master->x, (int16_t)master->y, (int16_t)master->z});
}

/* Looks for the nearest master doing a job of this work type the colonist
 * can learn, with no apprentice yet, and goes to assist them. */
static bool try_assist(Colonist *colonist, const ColonistContext *context, WorkType type)
{
    EntityId self = pool_id_at(context->colonist_pool, (uint32_t)(context->worker_id - 1));
    TilePos here = current_tile(colonist);
    EntityId best = ENTITY_NONE;
    int best_distance = INT_MAX;
    for (uint32_t slot = 0; slot < context->colonist_pool->high_water; slot++) {
        EntityId id = pool_id_at(context->colonist_pool, slot);
        if (entity_id_is_none(id) || entity_id_equal(id, self)) {
            continue;
        }
        const Colonist *master = &context->colonists[slot];
        ColonistJobNeed need;
        if (!colonist_job_need(master, context, &need) || need.work_type != type ||
            !can_learn_from(colonist, master, &need)) {
            continue;
        }
        int distance = abs(master->x - colonist->x) + abs(master->y - colonist->y);
        TilePos at = {(int16_t)master->x, (int16_t)master->y, (int16_t)master->z};
        if (distance >= best_distance ||
            !regions_connected(context->regions, context->map, here, at) ||
            has_apprentice(context, id, self)) {
            continue;
        }
        best = id;
        best_distance = distance;
    }
    Colonist *master = colonist_by_id(context, best);
    if (master == NULL || !go_beside(colonist, context, master)) {
        return false;
    }
    ColonistJobNeed need;
    colonist_job_need(master, context, &need);
    begin_task(colonist, context, TASK_ASSIST);
    colonist->mentor = best;
    colonist->assist_work = (uint8_t)type;
    colonist->assist_knowledge = (uint8_t)need.knowledge;
    colonist->assist_wait = 0;
    return true;
}

/* Follows the master and helps while they work, learning their job. */
static void assist(Colonist *colonist, const ColonistContext *context)
{
    Colonist *master = colonist_by_id(context, colonist->mentor);
    if (master == NULL) {
        set_idle(colonist, context, 0);
        return;
    }
    ColonistJobNeed need;
    if (!colonist_job_need(master, context, &need) || !can_learn_from(colonist, master, &need)) {
        /* Between two jobs, busy with something else, or learned enough. */
        if (++colonist->assist_wait >= COLONIST_ASSIST_WAIT_TICKS) {
            set_idle(colonist, context, 0);
        }
        return;
    }
    colonist->assist_wait = 0;
    colonist->assist_work = (uint8_t)need.work_type;
    colonist->assist_knowledge = (uint8_t)need.knowledge;
    if (!beside(colonist->x, colonist->y, colonist->z, master->x, master->y, master->z)) {
        /* Catches up once the master stands still. */
        if (!colonist_is_moving(master) && !go_beside(colonist, context, master)) {
            set_idle(colonist, context, 0);
        }
        return;
    }
    if (!colonist_at_work(master)) {
        return; /* Waits while the master fetches what the job needs. */
    }
    master->assisted_until = context->tick + 1;
    master->knowledge_practice[need.knowledge] = context->tick;
    double teacher = master->knowledge[need.knowledge];
    colonist_learn(colonist, need.knowledge, KNOWLEDGE_SOURCE_APPRENTICESHIP,
                   knowledge_taught_rate(KNOWLEDGE_APPRENTICE_RATE, teacher), 1.0 / TICKS_PER_HOUR,
                   teacher, context->tick);
}

/* ---- Hearths: fuel, lighting, fireside stories ------------------------------ */

/* Heads for a tile next to `tile` (4 neighbors). */
static bool go_next_to(Colonist *colonist, const ColonistContext *context, TilePos tile)
{
    colonist->path_index = 0;
    if (next_to(colonist->x, colonist->y, colonist->z, tile)) {
        path_clear(&colonist->path);
        return true;
    }
    TilePos spot;
    return pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
                                   is_next_to_target, &tile, COLONIST_WORK_SEARCH_MAX_COST,
                                   &colonist->path, &spot);
}

/* Whether some station passes `wants` (a cheap check before a search). */
static bool any_station(const ColonistContext *context, bool (*wants)(const Workstation *))
{
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        const Workstation *station = &context->workstations->list[index];
        if (station->used && wants(station)) {
            return true;
        }
    }
    return false;
}

/* The station on a tile that settlers may use (none when forbidden). */
static const Workstation *station_on(const ColonistContext *context, int x, int y, int z)
{
    const Workstation *station = workstation_get_const(
        context->workstations, workstation_at(context->workstations, x, y, z));
    return station != NULL && !station->forbidden ? station : NULL;
}

static bool wants_fuel(const Workstation *station)
{
    return hearth_fuel_wanted(station) > 0.0f;
}

/* Fuel types the hearth takes now. */
static ItemTypeMask fuel_wanted_types(const Workstation *station)
{
    ItemTypeMask types = 0;
    for (int type = 1; type < ITEM_TYPE_COUNT; type++) {
        if (hearth_fuel_items_wanted(station, (ItemType)type) > 0) {
            types |= ITEM_TYPE_BIT(type);
        }
    }
    return types;
}

/* Search filter: a hearth wanting fuel of a type that lies somewhere. */
static bool is_hearth_wanting_fuel(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const ColonistContext *context = userdata;
    const Workstation *station = station_on(context, x, y, z);
    return station != NULL && wants_fuel(station) &&
           items_exist(context->plan_items, fuel_wanted_types(station), 1);
}

static bool try_refuel_job(Colonist *colonist, const ColonistContext *context)
{
    if (!any_station(context, wants_fuel)) {
        return false;
    }
    TilePos tile;
    if (!pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
                                 is_hearth_wanting_fuel, (void *)context,
                                 COLONIST_WORK_SEARCH_MAX_COST, NULL, &tile)) {
        return false;
    }
    int index = workstation_at(context->workstations, tile.x, tile.y, tile.z);
    const Workstation *station = workstation_get_const(context->workstations, index);
    begin_task(colonist, context, TASK_REFUEL);
    colonist->station = index;
    colonist->station_number = station->number;
    colonist->target = tile;
    FetchSearch search = fetch_search(context, fuel_wanted_types(station));
    if (!start_fetch(colonist, context, &search, tile, COLONIST_WORK_SEARCH_MAX_COST, false)) {
        set_idle(colonist, context, 0);
        path_clear(&colonist->path);
        return false;
    }
    /* All it wants, until the load in hand is known. */
    colonist->fuel_promised = hearth_fuel_wanted(station);
    hearth_promise_fuel(context->workstations, index, colonist->fuel_promised);
    return true;
}

static bool refuel_still_valid(const Colonist *colonist, const ColonistContext *context)
{
    const Workstation *station = job_station(colonist, context);
    return hearth_is(station) && station->keep_burning &&
           colonist->work_priorities[WORK_HAULING] != WORK_PRIORITY_DISABLED;
}

/* Picks up fuel, then puts it in the hearth from a neighboring tile. */
static void refuel(Colonist *colonist, const ColonistContext *context)
{
    Workstation *station = job_station(colonist, context);
    if (!refuel_still_valid(colonist, context)) {
        set_idle(colonist, context, 0);
        return;
    }
    TilePos hearth = station_tile(station);
    if (colonist->station_phase == STATION_JOB_FETCH) {
        ItemStack stack = items_get_allowed(context->items, colonist->x, colonist->y, colonist->z);
        float value = hearth_fuel_value((ItemType)stack.type);
        if (colonist->x != colonist->fetch.x || colonist->y != colonist->fetch.y || value <= 0.0f ||
            !(station->fuel_types & ITEM_TYPE_BIT(stack.type))) {
            set_idle(colonist, context, 0);
            return;
        }
        float others = station->fuel_promised - colonist->fuel_promised;
        int count = (int)floorf((HEARTH_FUEL_CAPACITY - station->fuel - others) / value);
        int capacity = colonist_carry_capacity(colonist, (ItemType)stack.type);
        count = count < capacity ? count : capacity;
        count = count < stack.count ? count : stack.count;
        if (count <= 0) {
            set_idle(colonist, context, 0);
            return;
        }
        items_remove(context->items, colonist->x, colonist->y, colonist->z, count);
        colonist->carried = stack;
        colonist->carried.count = (uint16_t)count;
        /* What is promised now is what is in hand. */
        release_fuel(colonist, context);
        colonist->fuel_promised = value * (float)count;
        hearth_promise_fuel(context->workstations, colonist->station, colonist->fuel_promised);
        reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE,
                                  context->worker_id);
        colonist->station_phase = STATION_JOB_GO;
        if (!go_next_to(colonist, context, hearth)) {
            set_idle(colonist, context, 0);
        }
        return;
    }
    if (!next_to(colonist->x, colonist->y, colonist->z, hearth)) {
        set_idle(colonist, context, 0);
        return;
    }
    int taken = hearth_add_fuel(context->workstations, colonist->station, colonist->carried);
    colonist->carried.count = (uint16_t)(colonist->carried.count - taken);
    if (colonist->carried.count == 0) {
        colonist->carried = (ItemStack){.type = ITEM_NONE};
    }
    set_idle(colonist, context, 0); /* Puts down what did not fit. */
}

bool colonist_can_light(const Colonist *colonist)
{
    return colonist->knowledge[KNOWLEDGE_FIRE] >= HEARTH_LIGHT_MIN_LEVEL;
}

/* A hearth the lighter has work at: with fuel to light, or with none and
 * some lying somewhere to bring it first. */
static bool light_wanted(const ColonistContext *context, const Workstation *station)
{
    return hearth_needs_lighting(station) ||
           (hearth_needs_kindling(station) &&
            items_exist(context->plan_items, fuel_wanted_types(station), 1));
}

static bool hearth_to_light(const ColonistContext *context, const Workstation *station)
{
    return station != NULL && light_wanted(context, station) &&
           !hearth_rained_on(station, context->map, context->wetting) &&
           !reservations_taken_by_other(context->reservations, RESERVATION_FIRE,
                                        station_tile(station), context->worker_id);
}

static bool needs_lighting(const Workstation *station)
{
    return hearth_needs_lighting(station) || hearth_needs_kindling(station);
}

static bool is_hearth_to_light(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const ColonistContext *context = userdata;
    return hearth_to_light(context, station_on(context, x, y, z));
}

typedef struct FireSourceSearch {
    const ColonistContext *context;
    TilePos hearth; /* The one to light: not a source. */
} FireSourceSearch;

static bool is_fire_source(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const FireSourceSearch *search = userdata;
    const Workstation *station = station_on(search->context, x, y, z);
    return station != NULL && station->burning &&
           (station->x != search->hearth.x || station->y != search->hearth.y ||
            station->z != search->hearth.z);
}

static bool carries_tinder(const Colonist *colonist)
{
    return colonist->carried.type == ITEM_GRASS && colonist->carried.count > 0;
}

/* Whether the colonist holds fuel the hearth takes. */
static bool carries_fuel(const Colonist *colonist, const Workstation *station)
{
    return colonist->carried.count > 0 &&
           hearth_fuel_value((ItemType)colonist->carried.type) > 0.0f &&
           (station->fuel_types & ITEM_TYPE_BIT(colonist->carried.type)) != 0;
}

/* Next step of lighting: fuel for a hearth with none, then to the hearth with embers or tinder in
 * hand, else embers from a burning fire close to it, else dry grass for friction. False when none
 * can be had. */
static bool plan_lighting(Colonist *colonist, const ColonistContext *context)
{
    const Workstation *station = job_station(colonist, context);
    if (station == NULL) {
        return false;
    }
    TilePos hearth = station_tile(station);
    colonist->light_progress = 0.0f;
    if (station->fuel <= 0.0f) { /* Nothing to burn: fuel first. */
        if (carries_fuel(colonist, station)) {
            colonist->light_step = LIGHT_BRING_FUEL;
            return go_next_to(colonist, context, hearth);
        }
        FetchSearch fuel = fetch_search(context, fuel_wanted_types(station));
        colonist->light_step = LIGHT_FETCH_FUEL;
        return start_fetch(colonist, context, &fuel, hearth, COLONIST_WORK_SEARCH_MAX_COST, false);
    }
    if (colonist->ember_ticks > 0 || carries_tinder(colonist)) {
        colonist->light_step = LIGHT_GO;
        return go_next_to(colonist, context, hearth);
    }
    FireSourceSearch sources = {context, hearth};
    TilePos source;
    if (pathfinder_find_nearest(context->pathfinder, context->plan_map, hearth, is_fire_source,
                                &sources, HEARTH_FIRE_CARRY_MAX_COST, NULL, &source)) {
        const Workstation *fire = station_on(context, source.x, source.y, source.z);
        colonist->fire_source = workstation_at(context->workstations, source.x, source.y, source.z);
        colonist->fire_source_number = fire->number;
        colonist->light_step = LIGHT_TO_SOURCE;
        return go_next_to(colonist, context, source);
    }
    FetchSearch tinder = fetch_search(context, ITEM_TYPE_BIT(ITEM_GRASS));
    colonist->light_step = LIGHT_FETCH_TINDER;
    return start_fetch(colonist, context, &tinder, hearth, COLONIST_WORK_SEARCH_MAX_COST, false);
}

static bool try_light_job(Colonist *colonist, const ColonistContext *context)
{
    if (!colonist_can_light(colonist) || !any_station(context, needs_lighting)) {
        return false;
    }
    TilePos tile;
    if (!pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
                                 is_hearth_to_light, (void *)context, COLONIST_WORK_SEARCH_MAX_COST,
                                 NULL, &tile)) {
        return false;
    }
    int index = workstation_at(context->workstations, tile.x, tile.y, tile.z);
    begin_task(colonist, context, TASK_LIGHT);
    colonist->station = index;
    colonist->station_number = workstation_get_const(context->workstations, index)->number;
    colonist->target = tile;
    reservations_add(context->reservations, RESERVATION_FIRE, tile, context->worker_id);
    if (!plan_lighting(colonist, context)) {
        set_idle(colonist, context, 0);
        path_clear(&colonist->path);
        return false;
    }
    return true;
}

static bool light_still_valid(const Colonist *colonist, const ColonistContext *context)
{
    const Workstation *station = job_station(colonist, context);
    return station != NULL && (hearth_needs_lighting(station) || hearth_needs_kindling(station)) &&
           !hearth_rained_on(station, context->map, context->wetting) &&
           reservations_owner(context->reservations, RESERVATION_FIRE, station_tile(station)) ==
               context->worker_id &&
           colonist->work_priorities[WORK_FIRE] != WORK_PRIORITY_DISABLED &&
           colonist_can_light(colonist);
}

/* The burning hearth embers are taken from, NULL when gone or out. */
static const Workstation *fire_source(const Colonist *colonist, const ColonistContext *context)
{
    const Workstation *source = workstation_get_const(context->workstations, colonist->fire_source);
    return source != NULL && source->number == colonist->fire_source_number && source->burning &&
                   !source->forbidden
               ? source
               : NULL;
}

static void light_job(Colonist *colonist, const ColonistContext *context)
{
    if (!light_still_valid(colonist, context)) {
        set_idle(colonist, context, 0);
        return;
    }
    TilePos hearth = colonist->target;
    float speed = colonist_work_speed(colonist, KNOWLEDGE_FIRE);
    Workstation *station = job_station(colonist, context);
    switch ((LightStep)colonist->light_step) {
    case LIGHT_FETCH_FUEL: {
        ItemStack stack = items_get_allowed(context->items, colonist->x, colonist->y, colonist->z);
        float value = hearth_fuel_value((ItemType)stack.type);
        if (colonist->x != colonist->fetch.x || colonist->y != colonist->fetch.y || value <= 0.0f ||
            !(station->fuel_types & ITEM_TYPE_BIT(stack.type))) {
            set_idle(colonist, context, 0);
            return;
        }
        int count = (int)floorf((HEARTH_FUEL_CAPACITY - station->fuel) / value);
        int capacity = colonist_carry_capacity(colonist, (ItemType)stack.type);
        count = count < capacity ? count : capacity;
        count = count < stack.count ? count : stack.count;
        if (count <= 0) {
            set_idle(colonist, context, 0);
            return;
        }
        items_remove(context->items, colonist->x, colonist->y, colonist->z, count);
        colonist->carried = stack;
        colonist->carried.count = (uint16_t)count;
        reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE,
                                  context->worker_id);
        break; /* Then to the hearth. */
    }
    case LIGHT_BRING_FUEL: {
        if (!carries_fuel(colonist, station) ||
            !next_to(colonist->x, colonist->y, colonist->z, hearth)) {
            break; /* Plan again. */
        }
        int taken = hearth_add_fuel(context->workstations, colonist->station, colonist->carried);
        colonist->carried.count = (uint16_t)(colonist->carried.count - taken);
        if (colonist->carried.count > 0) {
            set_idle(colonist, context, 0); /* Puts down what did not fit. */
            return;
        }
        colonist->carried = (ItemStack){.type = ITEM_NONE};
        break; /* Fuel in: on to the fire. */
    }
    case LIGHT_FETCH_TINDER: {
        ItemStack stack = items_get_allowed(context->items, colonist->x, colonist->y, colonist->z);
        if (colonist->x != colonist->fetch.x || colonist->y != colonist->fetch.y ||
            stack.type != ITEM_GRASS) {
            set_idle(colonist, context, 0);
            return;
        }
        items_remove(context->items, colonist->x, colonist->y, colonist->z, 1);
        colonist->carried = stack;
        colonist->carried.count = 1;
        reservations_release_kind(context->reservations, RESERVATION_HAUL_SOURCE,
                                  context->worker_id);
        break; /* Then to the hearth. */
    }
    case LIGHT_TO_SOURCE: {
        const Workstation *source = fire_source(colonist, context);
        if (source != NULL &&
            next_to(colonist->x, colonist->y, colonist->z, station_tile(source))) {
            colonist->light_step = LIGHT_EMBERS;
            colonist->light_progress = 0.0f;
            return;
        }
        break; /* Gone out, or not there: plan again. */
    }
    case LIGHT_EMBERS:
        if (fire_source(colonist, context) == NULL) {
            break;
        }
        colonist->light_progress += speed;
        colonist_learn_by_doing(colonist, KNOWLEDGE_FIRE, 1, context->tick);
        if (colonist->light_progress < HEARTH_EMBER_WORK) {
            return;
        }
        colonist->ember_ticks = HEARTH_EMBER_TICKS;
        break;
    case LIGHT_GO:
        if (!next_to(colonist->x, colonist->y, colonist->z, hearth)) {
            break;
        }
        if (colonist->ember_ticks > 0) {
            hearth_light(context->workstations, colonist->station);
            colonist_learn_by_doing(colonist, KNOWLEDGE_FIRE, 1, context->tick);
            set_idle(colonist, context, 0);
            return;
        }
        if (carries_tinder(colonist)) {
            colonist->light_step = LIGHT_FRICTION;
            colonist->light_progress = 0.0f;
            return;
        }
        break;
    case LIGHT_FRICTION: {
        if (!carries_tinder(colonist) || !next_to(colonist->x, colonist->y, colonist->z, hearth)) {
            break;
        }
        /* The chance depends on the level before this attempt's learning. */
        double level = colonist_work_level(colonist, KNOWLEDGE_FIRE);
        colonist->light_progress += speed;
        colonist_learn_by_doing(colonist, KNOWLEDGE_FIRE, 1, context->tick);
        if (colonist->light_progress < HEARTH_FRICTION_WORK) {
            return;
        }
        /* The tinder burns, whether the fire takes or not. */
        colonist->carried.count--;
        if (colonist->carried.count == 0) {
            colonist->carried = (ItemStack){.type = ITEM_NONE};
        }
        if (rng_next_float(context->rng) < hearth_friction_chance(level)) {
            hearth_light(context->workstations, colonist->station);
            set_idle(colonist, context, 0);
            return;
        }
        break; /* Failed: more tinder. */
    }
    }
    if (!plan_lighting(colonist, context)) {
        set_idle(colonist, context, 0);
    }
}

bool colonist_telling(const Colonist *colonist, const Workstations *workstations)
{
    if (colonist->task != TASK_STORY || colonist_is_moving(colonist)) {
        return false;
    }
    const Workstation *station = workstation_get_const(workstations, colonist->station);
    return station != NULL && station->number == colonist->station_number && station->burning &&
           beside(colonist->x, colonist->y, colonist->z, station->x, station->y, station->z);
}

KnowledgeType colonist_story_topic(const Colonist *colonist, const bool *teach)
{
    KnowledgeType best = KNOWLEDGE_NONE;
    for (int knowledge = 0; teach != NULL && knowledge < KNOWLEDGE_COUNT; knowledge++) {
        double level = colonist->knowledge[knowledge];
        if (teach[knowledge] && knowledge_is_mastered(level) &&
            (best == KNOWLEDGE_NONE || level > colonist->knowledge[best])) {
            best = (KnowledgeType)knowledge;
        }
    }
    return best;
}

typedef struct FiresideSearch {
    const ColonistContext *context;
    EntityId self;
    TilePos hearth;
} FiresideSearch;

/* Whether someone other than `self` tells stories at this hearth. */
static bool hearth_has_storyteller(const ColonistContext *context, const Workstation *station,
                                   EntityId self)
{
    for (uint32_t slot = 0; slot < context->colonist_pool->high_water; slot++) {
        EntityId id = pool_id_at(context->colonist_pool, slot);
        const Colonist *other = &context->colonists[slot];
        if (!entity_id_is_none(id) && !entity_id_equal(id, self) && other->task == TASK_STORY &&
            other->station_number == station->number) {
            return true;
        }
    }
    return false;
}

static bool is_story_hearth(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const FiresideSearch *search = userdata;
    const Workstation *station = station_on(search->context, x, y, z);
    return station != NULL && station->burning &&
           !hearth_has_storyteller(search->context, station, search->self);
}

/* Goes to tell about its best taught knowledge at a burning hearth nobody
 * tells stories at. */
static bool try_tell(Colonist *colonist, const ColonistContext *context, EntityId self)
{
    KnowledgeType topic = colonist_story_topic(colonist, context->teach);
    FiresideSearch search = {context, self, {0, 0, 0}};
    TilePos tile;
    if (topic == KNOWLEDGE_NONE ||
        !pathfinder_find_nearest(context->pathfinder, context->plan_map, current_tile(colonist),
                                 is_story_hearth, &search, COLONIST_WORK_SEARCH_MAX_COST, NULL,
                                 &tile) ||
        !go_beside_tile(colonist, context, tile)) {
        return false;
    }
    const Workstation *station = station_on(context, tile.x, tile.y, tile.z);
    begin_task(colonist, context, TASK_STORY);
    colonist->station = workstation_at(context->workstations, tile.x, tile.y, tile.z);
    colonist->station_number = station->number;
    colonist->target = tile;
    colonist->story_topic = (uint8_t)topic;
    return true;
}

static int listener_count(const ColonistContext *context, EntityId teller)
{
    int count = 0;
    for (uint32_t slot = 0; slot < context->colonist_pool->high_water; slot++) {
        const Colonist *other = &context->colonists[slot];
        if (!entity_id_is_none(pool_id_at(context->colonist_pool, slot)) &&
            other->task == TASK_LISTEN && entity_id_equal(other->mentor, teller)) {
            count++;
        }
    }
    return count;
}

/* A free place to sit around the hearth: not the hearth, nobody else's. */
static bool is_listen_spot(const Map *map, int x, int y, int z, void *userdata)
{
    const FiresideSearch *search = userdata;
    int dx = abs(x - search->hearth.x), dy = abs(y - search->hearth.y);
    int distance = dx > dy ? dx : dy;
    if (distance < 1 || distance > HEARTH_LISTEN_SPOT_RANGE || !map_is_passable(map, x, y, z)) {
        return false;
    }
    const ColonistContext *context = search->context;
    for (uint32_t slot = 0; slot < context->colonist_pool->high_water; slot++) {
        EntityId id = pool_id_at(context->colonist_pool, slot);
        const Colonist *other = &context->colonists[slot];
        if (entity_id_is_none(id) || entity_id_equal(id, search->self)) {
            continue;
        }
        if ((other->task == TASK_LISTEN && other->target.x == x && other->target.y == y) ||
            (other->task == TASK_STORY && other->x == x && other->y == y)) {
            return false;
        }
    }
    return true;
}

/* Goes to sit near the closest storyteller with room for one more. */
static bool try_listen(Colonist *colonist, const ColonistContext *context, EntityId self)
{
    TilePos here = current_tile(colonist);
    EntityId best = ENTITY_NONE;
    int best_distance = INT_MAX;
    for (uint32_t slot = 0; slot < context->colonist_pool->high_water; slot++) {
        EntityId id = pool_id_at(context->colonist_pool, slot);
        const Colonist *teller = &context->colonists[slot];
        if (entity_id_is_none(id) || entity_id_equal(id, self) || teller->task != TASK_STORY) {
            continue;
        }
        int distance = abs(teller->target.x - colonist->x) + abs(teller->target.y - colonist->y);
        if (distance >= best_distance ||
            !regions_connected(context->regions, context->map, here, teller->target) ||
            listener_count(context, id) >= HEARTH_STORY_LISTENERS) {
            continue;
        }
        best = id;
        best_distance = distance;
    }
    Colonist *teller = colonist_by_id(context, best);
    if (teller == NULL) {
        return false;
    }
    FiresideSearch search = {context, self, teller->target};
    TilePos spot;
    if (!pathfinder_find_nearest(context->pathfinder, context->plan_map, here, is_listen_spot,
                                 &search, COLONIST_WORK_SEARCH_MAX_COST, &colonist->path, &spot)) {
        return false;
    }
    begin_task(colonist, context, TASK_LISTEN);
    colonist->mentor = best;
    colonist->station = teller->station;
    colonist->station_number = teller->station_number;
    colonist->target = spot;
    return true;
}

/* In the evening, with knowledges marked to teach: tell, or listen. */
static bool try_fireside(Colonist *colonist, const ColonistContext *context)
{
    if (context->colonists == NULL || context->teach == NULL || !hearth_story_time(context->tick) ||
        !search_allowed(colonist, SEARCH_STORY)) {
        return false;
    }
    bool taught = false;
    for (int knowledge = 0; knowledge < KNOWLEDGE_COUNT; knowledge++) {
        taught = taught || context->teach[knowledge];
    }
    EntityId self = pool_id_at(context->colonist_pool, (uint32_t)(context->worker_id - 1));
    if (taught && (try_tell(colonist, context, self) || try_listen(colonist, context, self))) {
        return true;
    }
    colonist->search_cooldown[SEARCH_STORY] = COLONIST_ASSIST_SEARCH_TICKS;
    return false;
}

/* Beside the hearth, telling (listeners learn on their side). */
static void story(Colonist *colonist, const ColonistContext *context)
{
    const Workstation *station = job_station(colonist, context);
    if (station == NULL || !station->burning || !hearth_story_time(context->tick)) {
        set_idle(colonist, context, 0);
        return;
    }
    if (!beside(colonist->x, colonist->y, colonist->z, station->x, station->y, station->z)) {
        if (!go_beside_tile(colonist, context, station_tile(station))) {
            set_idle(colonist, context, 0);
        }
        return;
    }
    colonist->knowledge_practice[colonist->story_topic] = context->tick; /* Teaching. */
    mood_remember(&colonist->mood, THOUGHT_FIRESIDE_STORY);
}

/* Sitting near the hearth, learning while the storyteller tells. */
static void listen_to_story(Colonist *colonist, const ColonistContext *context)
{
    const Colonist *teller = colonist_by_id(context, colonist->mentor);
    const Workstation *station = job_station(colonist, context);
    if (teller == NULL || station == NULL || teller->task != TASK_STORY ||
        teller->station_number != colonist->station_number) {
        set_idle(colonist, context, 0);
        return;
    }
    int dx = abs(colonist->x - station->x), dy = abs(colonist->y - station->y);
    if ((dx > dy ? dx : dy) > HEARTH_STORY_RANGE || colonist->z != station->z) {
        set_idle(colonist, context, 0); /* Did not get there. */
        return;
    }
    if (!colonist_telling(teller, context->workstations)) {
        return; /* Waits for the story to start. */
    }
    mood_remember(&colonist->mood, THOUGHT_FIRESIDE_STORY);
    KnowledgeType topic = (KnowledgeType)teller->story_topic;
    double level = teller->knowledge[topic];
    colonist_learn(colonist, topic, KNOWLEDGE_SOURCE_STORIES,
                   knowledge_taught_rate(KNOWLEDGE_STORY_RATE, level), 1.0 / TICKS_PER_HOUR,
                   level < KNOWLEDGE_STORY_CAP ? level : KNOWLEDGE_STORY_CAP, context->tick);
}

static void start_wandering(Colonist *colonist, const ColonistContext *context)
{
    const int radius = COLONIST_WANDER_RADIUS;
    for (int attempt = 0; attempt < 8; attempt++) {
        int dx = rng_range_i32(context->rng, -radius, radius);
        int dy = rng_range_i32(context->rng, -radius, radius);
        TilePos goal = {(int16_t)(colonist->x + dx), (int16_t)(colonist->y + dy),
                        (int16_t)colonist->z};
        if ((dx == 0 && dy == 0) || !map_is_passable(context->map, goal.x, goal.y, goal.z) ||
            !regions_connected(context->regions, context->map, current_tile(colonist), goal)) {
            continue; /* Blocked, or on the other side of a wall or of water. */
        }
        if (pathfinder_find(context->pathfinder, context->plan_map, current_tile(colonist), goal,
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

bool colonist_is_night_for(uint64_t tick, uint8_t worker_id)
{
    /* Coprime with the spread: settlers of nearby slots are far apart. */
    uint64_t delay = (uint64_t)worker_id * 97u % (uint64_t)COLONIST_BEDTIME_SPREAD_TICKS;
    /* The same time of day, the day before when too early. */
    return colonist_is_night(tick >= delay ? tick - delay : tick + TICKS_PER_DAY - delay);
}

/* States thought now (hungry, cold, out in the rain...). */
static ThoughtMask mood_states(Colonist *colonist, const ColonistContext *context, bool asleep)
{
    static const ThoughtType LOW[NEED_COUNT] = {THOUGHT_HUNGRY, THOUGHT_THIRSTY, THOUGHT_TIRED};
    static const ThoughtType CRITICAL[NEED_COUNT] = {THOUGHT_STARVING, THOUGHT_PARCHED,
                                                     THOUGHT_EXHAUSTED};
    ThoughtMask states = 0;
    for (int need = 0; need < NEED_COUNT; need++) {
        if (need == NEED_REST && asleep) {
            continue; /* Resting already. */
        }
        NeedLevel level = need_level(colonist->needs[need]);
        if (level == NEED_LEVEL_CRITICAL) {
            states |= THOUGHT_BIT(CRITICAL[need]);
        } else if (level == NEED_LEVEL_LOW) {
            states |= THOUGHT_BIT(LOW[need]);
        }
    }
    if (context->temperatures != NULL) {
        float felt = felt_here(colonist, context);
        if (!asleep && felt < MOOD_COLD_CELSIUS - attribute_cold_shift(&colonist->attributes)) {
            states |= THOUGHT_BIT(THOUGHT_COLD);
        } else if (!asleep &&
                   felt > MOOD_HOT_CELSIUS + attribute_heat_shift(&colonist->attributes)) {
            states |= THOUGHT_BIT(THOUGHT_HOT);
        }
        if (temperature_at(context->temperatures, colonist->x, colonist->y, colonist->z) <
                MOOD_FIRE_COLD_AIR &&
            hearths_radiant_warmth(context->workstations, context->map, colonist->x, colonist->y,
                                   colonist->z) >= MOOD_FIRE_WARMTH) {
            mood_remember(&colonist->mood, THOUGHT_WARMED_BY_FIRE);
        }
    }
    if (colonist->wetness >= MOOD_SOAKED_WETNESS) {
        states |= THOUGHT_BIT(THOUGHT_SOAKED);
    }
    if (context->wetting > 0.0f && temperature_cover(context->map, colonist->x, colonist->y,
                                                     colonist->z) == TEMPERATURE_COVER_OPEN) {
        states |= THOUGHT_BIT(THOUGHT_IN_THE_RAIN);
    }
    return states;
}

/* The colonist takes stock of how it is, its mood drifts, and in a very bad
 * mood it may break down (the next decision acts it out). */
static void update_mood(Colonist *colonist, const ColonistContext *context)
{
    Mood *mood = &colonist->mood;
    bool asleep = colonist->task == TASK_SLEEP && !colonist_is_moving(colonist);
    mood_set_states(mood, mood_states(colonist, context, asleep));
    mood_advance(mood, MOOD_UPDATE_TICKS);
    if (asleep || !mood_can_break(mood)) {
        return;
    }
    float chance = mood_break_chance(mood->value) * (float)MOOD_UPDATE_TICKS / TICKS_PER_HOUR;
    if (chance <= 0.0f || !rng_chance(context->rng, chance)) {
        return;
    }
    MoodBreak kind = mood_break_kind(mood->value, rng_next_float(context->rng));
    int hours = kind == MOOD_BREAK_SULK ? rng_range_i32(context->rng, 2, 4)
                                        : rng_range_i32(context->rng, 4, 8);
    mood_start_break(mood, kind, hours * TICKS_PER_HOUR);
}

static void update_needs(Colonist *colonist, const ColonistContext *context)
{
    float *food = &colonist->needs[NEED_FOOD];
    float *water = &colonist->needs[NEED_WATER];
    float *rest = &colonist->needs[NEED_REST];
    *food = clampf(*food - NEED_FOOD_DECAY_PER_TICK *
                               attribute_factor(&colonist->attributes, ATTRIBUTE_APPETITE),
                   0.0f, 1.0f);
    *water = clampf(*water - NEED_WATER_DECAY_PER_TICK, 0.0f, 1.0f);
    if (colonist->ember_ticks > 0) {
        colonist->ember_ticks--;
    }
    update_wetness(colonist, context);
    if (colonist->chat_ticks > 0) {
        /* A chat ends early when the colonist gets busy. */
        colonist->chat_ticks = colonist_can_chat(colonist) ? colonist->chat_ticks - 1 : 0;
    }
    if (colonist->task == TASK_SLEEP && !colonist_is_moving(colonist)) {
        float felt = context->temperatures != NULL ? felt_here(colonist, context)
                                                   : felt_temperature(context, 0, 0, 0);
        float gain = NEED_REST_GAIN_PER_TICK * rest_factor(colonist, context, felt) *
                     attribute_factor(&colonist->attributes, ATTRIBUTE_SLEEP);
        *rest = clampf(*rest + gain, 0.0f, 1.0f);
        colonist->sleep_ticks++;
        if (felt < colonist_cold_limit(colonist)) {
            colonist->sleep_cold_ticks++;
        } else if (felt > colonist_heat_limit(colonist)) {
            colonist->sleep_hot_ticks++;
        }
        if (colonist->wetness >= MOOD_SOAKED_WETNESS) {
            colonist->sleep_wet_ticks++;
        }
    } else {
        *rest = clampf(*rest - NEED_REST_DECAY_PER_TICK, 0.0f, 1.0f);
    }
    colonist->search_phase++;
    for (int kind = 0; kind < SEARCH_COUNT; kind++) {
        if (colonist->search_cooldown[kind] > 0) {
            colonist->search_cooldown[kind]--;
        }
        if (colonist->search_quiet[kind] > 0) {
            colonist->search_quiet[kind]--;
        }
    }
    mood_break_tick(&colonist->mood, 1);
    /* Spread over the ticks: not every colonist at once. */
    if ((context->tick + context->worker_id) % MOOD_UPDATE_TICKS == 0) {
        update_mood(colonist, context);
    }
}

/* Walks off alone to a spot far away (MOOD_WANDER_OFF_*). */
static bool go_off_alone(Colonist *colonist, const ColonistContext *context)
{
    int far = (int)(MOOD_WANDER_OFF_MAX_COST / 3.0f), near = (int)(MOOD_WANDER_OFF_MIN_COST / 2.0f);
    for (int attempt = 0; attempt < 8; attempt++) {
        int dx = rng_range_i32(context->rng, -far, far);
        int dy = rng_range_i32(context->rng, -far, far);
        if (abs(dx) < near && abs(dy) < near) {
            continue; /* Not far enough. */
        }
        TilePos goal = {(int16_t)(colonist->x + dx), (int16_t)(colonist->y + dy),
                        (int16_t)colonist->z};
        if (!map_is_passable(context->map, goal.x, goal.y, goal.z) ||
            !regions_connected(context->regions, context->map, current_tile(colonist), goal)) {
            continue;
        }
        if (pathfinder_find(context->pathfinder, context->plan_map, current_tile(colonist), goal,
                            MOOD_WANDER_OFF_MAX_COST, &colonist->path)) {
            begin_task(colonist, context, TASK_WANDER);
            return true;
        }
    }
    return false;
}

/* A break: no work; the colonist still eats, drinks and sleeps when it must,
 * sulks about, goes off alone, or eats far more than it needs. */
static void act_out_break(Colonist *colonist, const ColonistContext *context)
{
    Mood *mood = &colonist->mood;
    float rest = colonist->needs[NEED_REST];
    if (try_satisfy_urgent(colonist, context, NEED_CRITICAL_THRESHOLD)) {
        return;
    }
    if (rest < NEED_CRITICAL_THRESHOLD || (context->night && rest < COLONIST_NIGHT_SLEEP_REST)) {
        start_sleeping(colonist, context);
        return;
    }
    switch ((MoodBreak)mood->break_type) {
    case MOOD_BREAK_BINGE:
        if (mood->binge_left <= 0.0f) {
            mood_end_break(mood); /* Ate it all. */
            set_idle(colonist, context, random_idle_ticks(context->rng));
            return;
        }
        if (try_satisfy(colonist, context, NEED_FOOD)) {
            return;
        }
        break; /* No food: sulks. */
    case MOOD_BREAK_WANDER_OFF:
        if (!mood->went_off) {
            mood->went_off = true;
            if (go_off_alone(colonist, context)) {
                return;
            }
        }
        break;
    default: break;
    }
    if (colonist->task != TASK_IDLE && colonist->task != TASK_WANDER) {
        set_idle(colonist, context, random_idle_ticks(context->rng));
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
        } else if (colonist_is_moving(colonist) &&
                   map_forbidden(context->map, colonist->target.x, colonist->target.y,
                                 colonist->target.z, MAP_FORBID_STRUCTURE)) {
            /* The bed it walks to was forbidden meanwhile: another one, or the
             * ground (asleep in it, it sleeps on). */
            reservations_release_kind(context->reservations, RESERVATION_BED, context->worker_id);
            start_sleeping(colonist, context);
        } else if (!try_satisfy_urgent(colonist, context, NEED_CRITICAL_THRESHOLD)) {
            /* Severe hunger or thirst wakes colonists up; the cold or the heat
             * of the night moves them. */
            stay_warm_asleep(colonist, context);
        }
        return;
    case TASK_EAT:
        if (!tile_has_food(
                map_checked(context, colonist->target.x, colonist->target.y, colonist->target.z),
                items_checked(context, colonist->target.x, colonist->target.y, colonist->target.z),
                colonist->target.x, colonist->target.y, colonist->target.z)) {
            set_idle(colonist, context, 0); /* The fruit is gone: decide again below. */
            break;
        }
        return; /* Carried on until done. */
    case TASK_DRINK: return;
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
    case TASK_ASSIST:
        if (colonist_by_id(context, colonist->mentor) == NULL) {
            set_idle(colonist, context, 0);
        }
        break;
    case TASK_REFUEL:
        if (!refuel_still_valid(colonist, context)) {
            set_idle(colonist, context, 0);
        }
        break;
    case TASK_LIGHT:
        if (!light_still_valid(colonist, context)) {
            set_idle(colonist, context, 0);
        }
        break;
    case TASK_STORY:
    case TASK_LISTEN:
        if (!hearth_story_time(context->tick)) {
            set_idle(colonist, context, 0); /* Bedtime. */
        }
        break;
    case TASK_MOVE:
        /* An order comes before everything but a break and dire needs. */
        if (!colonist_in_break(colonist) &&
            (try_satisfy_urgent(colonist, context, NEED_CRITICAL_THRESHOLD) ||
             rest >= NEED_CRITICAL_THRESHOLD)) {
            return;
        }
        break; /* Dropped for a break, or sleep (below). */
    default: break;
    }

    if (colonist_in_break(colonist)) {
        act_out_break(colonist, context);
        return;
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
        /* In the evening, the fireside before new work. */
        if (!try_fireside(colonist, context)) {
            try_start_work(colonist, context, WORK_PRIORITY_LOWEST);
        }
    } else if (colonist->task == TASK_ASSIST && search_allowed(colonist, SEARCH_ASSIST)) {
        /* Work of a higher priority than the apprenticeship comes first. */
        colonist->search_cooldown[SEARCH_ASSIST] = COLONIST_ASSIST_SEARCH_TICKS;
        int priority = colonist->work_priorities[colonist->assist_work];
        if (priority > WORK_PRIORITY_HIGHEST) {
            try_start_work(colonist, context, priority - 1);
        }
    }
}

/* Moves one tick along the path. */
static void walk(Colonist *colonist, const ColonistContext *context)
{
    TilePos next = colonist->path.steps[colonist->path_index];
    float cost = map_move_cost(context->map, next.x, next.y, next.z);
    if (next.z != colonist->z && cost > 0.0f) { /* Up or down a slope, a ladder, a tree. */
        bool up = next.z > colonist->z;
        TilePos low = up ? current_tile(colonist) : next;
        TilePos high = up ? next : current_tile(colonist);
        if (low.x == high.x && low.y == high.y) {
            cost = map_can_climb_tree(context->map, low.x, low.y, low.z)
                       ? cost * MAP_TREE_CLIMB_COST
                       : 0.0f;
        } else {
            cost = map_can_climb(context->map, low.x, low.y, low.z, high.x, high.y)
                       ? cost * map_climb_cost(context->map, low.x, low.y, low.z)
                       : 0.0f;
        }
    }
    if (cost <= 0.0f) {
        set_idle(colonist, context, random_idle_ticks(context->rng)); /* Path became blocked. */
        return;
    }
    if (next.x != colonist->x && next.y != colonist->y) {
        cost *= SQRT2;
    }
    colonist->move_progress += COLONIST_WALK_SPEED * colonist_load_speed_factor(colonist) *
                               attribute_factor(&colonist->attributes, ATTRIBUTE_WALKING) /
                               (float)TICKS_PER_SECOND / cost;
    if (colonist->move_progress >= 1.0f) {
        colonist->x = next.x;
        colonist->y = next.y;
        colonist->z = next.z;
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
        !tile_has_food(map, context->items, colonist->x, colonist->y, colonist->z)) {
        /* Not there, or the food is gone: look again soon. */
        set_idle(colonist, context, 0);
        return;
    }
    if (++colonist->timer < COLONIST_EAT_TICKS) {
        return;
    }

    float *food = &colonist->needs[NEED_FOOD];
    Mood *mood = &colonist->mood;
    /* What it takes to be sated, and on a binge far more. */
    float appetite = fmaxf(COLONIST_SATED_FOOD - *food, 0.0f);
    if (mood->break_type == MOOD_BREAK_BINGE) {
        appetite += fmaxf(mood->binge_left, 0.0f);
    }
    ItemStack stack = items_get_allowed(context->items, colonist->x, colonist->y, colonist->z);
    ItemType type;
    int eaten;
    if (item_is_food((ItemType)stack.type)) {
        /* Items on the ground first. */
        type = (ItemType)stack.type;
        int wanted = (int)ceilf(appetite / item_def(type)->nutrition);
        eaten = items_remove(context->items, colonist->x, colonist->y, colonist->z,
                             wanted > 1 ? wanted : 1);
        mood_meal(mood, type, item_stack_hit_points_percent(stack));
    } else if (map_litter(map, colonist->x, colonist->y, colonist->z, ITEM_HAZELNUTS) > 0) {
        /* Then the nuts lying there. */
        type = ITEM_HAZELNUTS;
        int wanted = (int)ceilf(appetite / item_def(type)->nutrition);
        eaten = map_take_litter_count(map, colonist->x, colonist->y, colonist->z, type,
                                      wanted > 1 ? wanted : 1);
        mood_meal(mood, type, 100);
    } else {
        /* Then the plant's fruit or leaves, fresh. */
        Plant *plant = &map->plants[map_index(map, colonist->x, colonist->y, colonist->z)];
        type = plant_stock_item((PlantType)plant->type, PLANT_STOCK_FOOD);
        int wanted = (int)ceilf(appetite / item_def(type)->nutrition);
        eaten = plant_take_stock(plant, PLANT_STOCK_FOOD, wanted > 1 ? wanted : 1);
        mood_meal(mood, type, 100);
    }
    float nutrition = item_def(type)->nutrition * (float)eaten;
    if (mood->break_type == MOOD_BREAK_BINGE) {
        /* Beyond what it took to be sated. */
        mood->binge_left -= fmaxf(nutrition - fmaxf(COLONIST_SATED_FOOD - *food, 0.0f), 0.0f);
    }
    *food = clampf(*food + nutrition, 0.0f, 1.0f);

    if (colonist->needs[NEED_FOOD] < COLONIST_SATED_FOOD &&
        try_satisfy(colonist, context, NEED_FOOD)) {
        return;
    }
    set_idle(colonist, context, random_idle_ticks(context->rng));
}

static void drink(Colonist *colonist, const ColonistContext *context)
{
    if (colonist->x != colonist->target.x || colonist->y != colonist->target.y ||
        !map_is_drinking_spot(context->map, colonist->x, colonist->y, colonist->z)) {
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
 * else, a gallery filled up by a cave-in): it steps to a free neighboring
 * tile and decides again. The ground gone under its feet (a ramp cut): it
 * falls to the ground below. */
static void step_out_of_blocked_tile(Colonist *colonist, const ColonistContext *context)
{
    static const int DX[8] = {0, 1, 0, -1, 1, 1, -1, -1};
    static const int DY[8] = {-1, 0, 1, 0, -1, 1, 1, -1};
    const Map *map = context->map;
    if (map_is_passable(map, colonist->x, colonist->y, colonist->z)) {
        return;
    }
    int ground = colonist->z;
    while (ground > 0 && map_is_open(map, colonist->x, colonist->y, ground)) {
        ground--;
    }
    if (ground != colonist->z && map_is_passable(map, colonist->x, colonist->y, ground)) {
        colonist->z = ground;
        colonist->move_progress = 0.0f;
        set_idle(colonist, context, 0);
        return;
    }
    for (int i = 0; i < 8; i++) {
        int x = colonist->x + DX[i], y = colonist->y + DY[i];
        if (map_is_passable(context->map, x, y, colonist->z)) {
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
        if (colonist->move_pending) {
            colonist->move_pending = false;
            start_move(colonist, context, colonist->move_goal);
        }
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
    case TASK_ASSIST: assist(colonist, context); break;
    case TASK_REFUEL: refuel(colonist, context); break;
    case TASK_LIGHT: light_job(colonist, context); break;
    case TASK_STORY: story(colonist, context); break;
    case TASK_LISTEN: listen_to_story(colonist, context); break;
    case TASK_MOVE: /* There: stands a while, then takes up its life again. */
        if (++colonist->timer >= COLONIST_MOVE_HOLD_TICKS) {
            set_idle(colonist, context, 0);
        }
        break;
    case TASK_SLEEP:
    case TASK_COUNT: break;
    }
}
