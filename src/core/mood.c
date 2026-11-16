#include "core/mood.h"

#include "core/i18n.h"
#include "core/util.h"

#include <math.h>
#include <string.h>

/* A state: value, half-life once over, growing worse up to `grow_max` times
 * after `grow` hours (tuning). */
#define STATE(id, key, value, half_life, grow, grow_max)                                           \
    {                                                                                              \
        id, key, true, value, 0.0f, half_life, 1, grow, grow_max                                   \
    }
/* A memory: value, full for `plateau` hours, then halving every `half_life`
 * hours; renewed (stack 1) or adding up to `stack` times (tuning). */
#define MEMORY(id, key, value, plateau, half_life, stack)                                          \
    {                                                                                              \
        id, key, false, value, plateau, half_life, stack, 0.0f, 1.0f                               \
    }

static const ThoughtDef THOUGHT_DEFS[THOUGHT_COUNT] = {
    [THOUGHT_HUNGRY] = STATE("hungry", N_("thought.hungry"), -6.0f, 1.0f, 24.0f, 2.0f),
    [THOUGHT_STARVING] = STATE("starving", N_("thought.starving"), -15.0f, 2.0f, 48.0f, 2.0f),
    [THOUGHT_THIRSTY] = STATE("thirsty", N_("thought.thirsty"), -6.0f, 1.0f, 12.0f, 2.0f),
    [THOUGHT_PARCHED] = STATE("parched", N_("thought.parched"), -15.0f, 2.0f, 24.0f, 2.0f),
    [THOUGHT_TIRED] = STATE("tired", N_("thought.tired"), -5.0f, 1.0f, 0.0f, 1.0f),
    [THOUGHT_EXHAUSTED] = STATE("exhausted", N_("thought.exhausted"), -12.0f, 2.0f, 24.0f, 1.5f),
    [THOUGHT_COLD] = STATE("cold", N_("thought.cold"), -8.0f, 1.0f, 0.0f, 1.0f),
    [THOUGHT_HOT] = STATE("hot", N_("thought.hot"), -6.0f, 1.0f, 0.0f, 1.0f),
    [THOUGHT_SOAKED] = STATE("soaked", N_("thought.soaked"), -6.0f, 1.0f, 0.0f, 1.0f),
    [THOUGHT_IN_THE_RAIN] =
        STATE("in_the_rain", N_("thought.in_the_rain"), -4.0f, 1.0f, 0.0f, 1.0f),
    [THOUGHT_SLEPT_WELL] = MEMORY("slept_well", N_("thought.slept_well"), 6.0f, 4.0f, 6.0f, 1),
    [THOUGHT_SLEPT_ON_GROUND] =
        MEMORY("slept_on_ground", N_("thought.slept_on_ground"), -5.0f, 2.0f, 4.0f, 1),
    [THOUGHT_SLEPT_OUTSIDE] =
        MEMORY("slept_outside", N_("thought.slept_outside"), -4.0f, 2.0f, 4.0f, 1),
    [THOUGHT_SLEPT_COLD] = MEMORY("slept_cold", N_("thought.slept_cold"), -6.0f, 2.0f, 4.0f, 1),
    [THOUGHT_SLEPT_HOT] = MEMORY("slept_hot", N_("thought.slept_hot"), -4.0f, 2.0f, 4.0f, 1),
    [THOUGHT_VARIED_MEALS] =
        MEMORY("varied_meals", N_("thought.varied_meals"), 5.0f, 12.0f, 12.0f, 1),
    [THOUGHT_SAME_FOOD] = MEMORY("same_food", N_("thought.same_food"), -6.0f, 12.0f, 12.0f, 1),
    [THOUGHT_COOKED_MEAL] = MEMORY("cooked_meal", N_("thought.cooked_meal"), 4.0f, 2.0f, 4.0f, 1),
    [THOUGHT_SPOILED_FOOD] =
        MEMORY("spoiled_food", N_("thought.spoiled_food"), -6.0f, 2.0f, 4.0f, 1),
    [THOUGHT_WARMED_BY_FIRE] =
        MEMORY("warmed_by_fire", N_("thought.warmed_by_fire"), 4.0f, 1.0f, 2.0f, 1),
    [THOUGHT_FIRESIDE_STORY] =
        MEMORY("fireside_story", N_("thought.fireside_story"), 6.0f, 6.0f, 8.0f, 1),
    [THOUGHT_CHAT] = MEMORY("chat", N_("thought.chat"), 2.0f, 1.0f, 3.0f, 3),
    [THOUGHT_RELIEF] = MEMORY("relief", N_("thought.relief"), 10.0f, 6.0f, 12.0f, 1),
    [THOUGHT_DEBUG_GLOOM] = MEMORY("debug_gloom", N_("thought.debug_gloom"), -45.0f, 2.0f, 2.0f, 1),
};

#undef STATE
#undef MEMORY

_Static_assert(THOUGHT_COUNT <= 32, "thought masks");

const ThoughtDef *thought_def(ThoughtType type)
{
    return (unsigned)type < THOUGHT_COUNT ? &THOUGHT_DEFS[type] : NULL;
}

static bool is_memory(ThoughtType type)
{
    return (unsigned)type < THOUGHT_COUNT && !THOUGHT_DEFS[type].state;
}

/* Sums the thoughts up into the mood. */
static void sum_up(Mood *mood)
{
    float value = MOOD_NEUTRAL;
    for (int type = 0; type < THOUGHT_COUNT; type++) {
        value += mood->weight[type];
    }
    mood->value = clampf(value, MOOD_MIN, MOOD_MAX);
}

static void forget(Mood *mood, ThoughtType type)
{
    mood->weight[type] = 0.0f;
    mood->peak[type] = 0.0f;
    mood->age[type] = 0;
}

void mood_init(Mood *mood)
{
    memset(mood, 0, sizeof(*mood));
    mood->value = MOOD_NEUTRAL;
}

void mood_remember(Mood *mood, ThoughtType type)
{
    if (!is_memory(type)) {
        return;
    }
    const ThoughtDef *def = &THOUGHT_DEFS[type];
    float weight = def->max_stack > 1 ? mood->weight[type] + def->value : def->value;
    float most = def->value * (float)def->max_stack;
    if (fabsf(weight) > fabsf(most)) {
        weight = most;
    }
    mood->weight[type] = weight;
    mood->peak[type] = weight;
    mood->age[type] = 0;
    sum_up(mood);
}

void mood_set_states(Mood *mood, ThoughtMask states)
{
    for (int type = 0; type < THOUGHT_COUNT; type++) {
        if (is_memory((ThoughtType)type)) {
            states &= ~THOUGHT_BIT(type);
            continue;
        }
        bool was = (mood->states & THOUGHT_BIT(type)) != 0;
        bool now = (states & THOUGHT_BIT(type)) != 0;
        if (was != now) {
            mood->age[type] = 0; /* On from now, or over. */
            if (now && mood->weight[type] == 0.0f) {
                mood->peak[type] = 0.0f;
            }
        }
    }
    mood->states = states;
}

bool mood_has(const Mood *mood, ThoughtType type)
{
    return (unsigned)type < THOUGHT_COUNT &&
           (mood->weight[type] != 0.0f || (mood->states & THOUGHT_BIT(type)) != 0);
}

float mood_thought_value(const Mood *mood, ThoughtType type)
{
    return (unsigned)type < THOUGHT_COUNT ? mood->weight[type] : 0.0f;
}

bool mood_fading(const Mood *mood, ThoughtType type)
{
    if ((unsigned)type >= THOUGHT_COUNT || mood->weight[type] == 0.0f) {
        return false;
    }
    if (!is_memory(type)) {
        return (mood->states & THOUGHT_BIT(type)) == 0;
    }
    return (float)mood->age[type] > THOUGHT_DEFS[type].plateau_hours * (float)TICKS_PER_HOUR;
}

float mood_state_value(ThoughtType type, int ticks)
{
    if ((unsigned)type >= THOUGHT_COUNT) {
        return 0.0f;
    }
    const ThoughtDef *def = &THOUGHT_DEFS[type];
    if (def->grow_hours <= 0.0f || def->grow_max <= 1.0f) {
        return def->value;
    }
    float share = clampf((float)ticks / (def->grow_hours * (float)TICKS_PER_HOUR), 0.0f, 1.0f);
    return def->value * (1.0f + (def->grow_max - 1.0f) * share);
}

/* `ticks` of fading: halves every half_life_hours, forgotten when it weighs
 * too little. */
static void fade(Mood *mood, ThoughtType type, int ticks)
{
    if (ticks <= 0 || mood->weight[type] == 0.0f) {
        return;
    }
    float half_life = THOUGHT_DEFS[type].half_life_hours * (float)TICKS_PER_HOUR;
    mood->weight[type] *= exp2f(-(float)ticks / half_life);
    if (fabsf(mood->weight[type]) < MOOD_FORGET_BELOW) {
        forget(mood, type);
    }
}

/* A state on for `ticks` more: sets in towards what it weighs now. */
static void set_in(Mood *mood, ThoughtType type, int ticks)
{
    mood->age[type] += ticks;
    float full = mood_state_value(type, mood->age[type]);
    float step = fabsf(full) * (float)ticks / (MOOD_STATE_RISE_HOURS * (float)TICKS_PER_HOUR);
    float weight = mood->weight[type];
    if (fabsf(full - weight) <= step) {
        weight = full;
    } else {
        weight += full > weight ? step : -step;
    }
    mood->weight[type] = weight;
    if (fabsf(weight) > fabsf(mood->peak[type])) {
        mood->peak[type] = weight;
    }
}

void mood_advance(Mood *mood, int ticks)
{
    if (ticks <= 0) {
        return;
    }
    for (int type = 0; type < THOUGHT_COUNT; type++) {
        ThoughtType thought = (ThoughtType)type;
        if (!is_memory(thought)) {
            if (mood->states & THOUGHT_BIT(type)) {
                set_in(mood, thought, ticks);
            } else {
                fade(mood, thought, ticks);
            }
        } else if (mood->weight[type] != 0.0f) {
            int plateau = (int)(THOUGHT_DEFS[type].plateau_hours * (float)TICKS_PER_HOUR);
            int before = mood->age[type];
            mood->age[type] += ticks;
            /* Only the ticks past the plateau fade it. */
            int past = mood->age[type] - (before > plateau ? before : plateau);
            fade(mood, thought, past);
        }
    }
    sum_up(mood);
}

bool mood_food_cooked(ItemType type)
{
    return type == ITEM_ROASTED_RHIZOMES || type == ITEM_ROASTED_CARROTS;
}

void mood_meal(Mood *mood, ItemType type, int hit_points_percent)
{
    if (!item_is_food(type)) {
        return;
    }
    mood->meals[mood->meal_next] = (uint8_t)type;
    mood->meal_next = (uint8_t)((mood->meal_next + 1) % MOOD_MEALS);
    if (mood->meal_count < MOOD_MEALS) {
        mood->meal_count++;
    }
    if (mood_food_cooked(type)) {
        mood_remember(mood, THOUGHT_COOKED_MEAL);
    }
    if (hit_points_percent < MOOD_SPOILED_PERCENT) {
        mood_remember(mood, THOUGHT_SPOILED_FOOD);
    }
    /* Kinds among the meals remembered. */
    int kinds = 0;
    for (int i = 0; i < mood->meal_count; i++) {
        bool seen = false;
        for (int j = 0; j < i; j++) {
            seen = seen || mood->meals[j] == mood->meals[i];
        }
        kinds += seen ? 0 : 1;
    }
    if (kinds >= MOOD_VARIED_KINDS) {
        forget(mood, THOUGHT_SAME_FOOD);
        mood_remember(mood, THOUGHT_VARIED_MEALS);
    } else if (kinds == 1 && mood->meal_count == MOOD_MEALS) {
        forget(mood, THOUGHT_VARIED_MEALS);
        mood_remember(mood, THOUGHT_SAME_FOOD);
    }
}

/* -1 at MOOD_MIN, 0 at MOOD_NEUTRAL, 1 at MOOD_MAX. */
static float signed_mood(float value)
{
    return (clampf(value, MOOD_MIN, MOOD_MAX) - MOOD_NEUTRAL) / (MOOD_MAX - MOOD_NEUTRAL);
}

float mood_work_factor(float value)
{
    return 1.0f + MOOD_WORK_SPAN * signed_mood(value);
}

double mood_level_offset(float value)
{
    return (double)(MOOD_LEVEL_SPAN * signed_mood(value));
}

double mood_learn_factor(float value)
{
    return 1.0 + (double)(MOOD_LEARN_SPAN * signed_mood(value));
}

float mood_break_chance(float value)
{
    if (!(value < MOOD_MINOR_BREAK_BELOW)) {
        return 0.0f;
    }
    float depth =
        (MOOD_MINOR_BREAK_BELOW - clampf(value, MOOD_MIN, MOOD_MAX)) / MOOD_MINOR_BREAK_BELOW;
    return MOOD_BREAK_CHANCE_PER_HOUR * depth;
}

MoodBreak mood_break_kind(float value, float roll)
{
    if (!(value < MOOD_MAJOR_BREAK_BELOW)) {
        return MOOD_BREAK_SULK;
    }
    return roll < 0.5f ? MOOD_BREAK_WANDER_OFF : MOOD_BREAK_BINGE;
}

void mood_start_break(Mood *mood, MoodBreak kind, int ticks)
{
    if ((unsigned)kind >= MOOD_BREAK_COUNT || kind == MOOD_BREAK_NONE || ticks <= 0) {
        return;
    }
    mood->break_type = (uint8_t)kind;
    mood->break_ticks = ticks;
    mood->binge_left = kind == MOOD_BREAK_BINGE ? MOOD_BINGE_NUTRITION : 0.0f;
    mood->went_off = false;
    mood->break_started = true;
}

void mood_end_break(Mood *mood)
{
    if (mood->break_type == MOOD_BREAK_NONE) {
        return;
    }
    mood->break_type = MOOD_BREAK_NONE;
    mood->break_ticks = 0;
    mood->binge_left = 0.0f;
    mood->went_off = false;
    mood->break_cooldown = MOOD_BREAK_COOLDOWN_HOURS * TICKS_PER_HOUR;
    mood_remember(mood, THOUGHT_RELIEF);
}

bool mood_break_tick(Mood *mood, int ticks)
{
    if (mood->break_type == MOOD_BREAK_NONE) {
        mood->break_cooldown = mood->break_cooldown > ticks ? mood->break_cooldown - ticks : 0;
        return false;
    }
    mood->break_ticks -= ticks;
    if (mood->break_ticks <= 0) {
        mood_end_break(mood);
        return true;
    }
    return false;
}

bool mood_can_break(const Mood *mood)
{
    return mood->break_type == MOOD_BREAK_NONE && mood->break_cooldown <= 0;
}

const char *mood_break_key(MoodBreak kind)
{
    static const char *const KEYS[MOOD_BREAK_COUNT] = {
        [MOOD_BREAK_NONE] = NULL,
        [MOOD_BREAK_SULK] = N_("mood.break.sulk"),
        [MOOD_BREAK_WANDER_OFF] = N_("mood.break.wander_off"),
        [MOOD_BREAK_BINGE] = N_("mood.break.binge"),
    };
    return (unsigned)kind < MOOD_BREAK_COUNT ? KEYS[kind] : NULL;
}

const char *mood_level_key(float value)
{
    if (value < MOOD_MAJOR_BREAK_BELOW) {
        return N_("mood.level.breaking");
    }
    if (value < MOOD_MINOR_BREAK_BELOW) {
        return N_("mood.level.miserable");
    }
    if (value < 40.0f) {
        return N_("mood.level.upset");
    }
    if (value < 60.0f) {
        return N_("mood.level.neutral");
    }
    if (value < 80.0f) {
        return N_("mood.level.content");
    }
    return N_("mood.level.happy");
}

uint64_t mood_hash(const Mood *mood, uint64_t hash)
{
    float values[2] = {mood->value, mood->binge_left};
    hash = hash_fnv1a64(hash, values, sizeof(values));
    hash = hash_fnv1a64(hash, &mood->states, sizeof(mood->states));
    hash = hash_fnv1a64(hash, mood->weight, sizeof(mood->weight));
    hash = hash_fnv1a64(hash, mood->peak, sizeof(mood->peak));
    hash = hash_fnv1a64(hash, mood->age, sizeof(mood->age));
    hash = hash_fnv1a64(hash, mood->meals, sizeof(mood->meals));
    int32_t fields[6] = {mood->meal_count,  mood->meal_next, mood->break_type,
                         mood->break_ticks, mood->went_off,  mood->break_cooldown};
    return hash_fnv1a64(hash, fields, sizeof(fields));
}
