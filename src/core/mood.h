/*
 * Moods: how a settler feels, from 0 (miserable) to 100 (happy):
 * MOOD_NEUTRAL plus what each of its thoughts weighs now.
 *
 * Nothing is all or nothing: every thought weighs more or less over time.
 * A settler's character (docs/ATTRIBUTES.md) sets how the mood is summed up:
 * its outlook adds points, its temperament scales what every thought weighs
 * (mood_set_character()).
 * A state is on while something lasts (hungry, cold, soaked, out in the
 * rain...: the colonist module sets the states every MOOD_UPDATE_TICKS): it
 * sets in over MOOD_STATE_RISE_HOURS, some grow worse the longer they last
 * (hunger, thirst: ThoughtDef.grow_hours, grow_max), and once it is over it
 * fades. A memory of something that happened (slept on the ground, ate the
 * same food again, heard a fireside story...) weighs its full value for
 * ThoughtDef.plateau_hours, then fades. Fading halves a thought every
 * ThoughtDef.half_life_hours, until it is forgotten below
 * MOOD_FORGET_BELOW. A memory that comes back is renewed to its full value,
 * or adds up to max_stack times its value (chats).
 *
 * Mood changes everything a settler does: work speed
 * (mood_work_factor()), the quality of what it makes, as if its level were
 * higher or lower (mood_level_offset()), and how fast it learns
 * (mood_learn_factor()). Below MOOD_MINOR_BREAK_BELOW a settler may break
 * down (mood_break_chance()): it sulks and does no work for a few hours;
 * below MOOD_MAJOR_BREAK_BELOW, it may go off alone far away, or eat far more
 * than it needs from the colony's food. A break ends with relief (a good
 * memory) and no other break for MOOD_BREAK_COOLDOWN_HOURS.
 */
#ifndef COLONY_CORE_MOOD_H
#define COLONY_CORE_MOOD_H

#include "core/game_clock.h"
#include "core/item.h"

#include <stdbool.h>
#include <stdint.h>

#define MOOD_MIN 0.0f
#define MOOD_MAX 100.0f
#define MOOD_NEUTRAL 50.0f
/* The colonist module updates states, mood and breaks this often. */
#define MOOD_UPDATE_TICKS (TICKS_PER_HOUR / 10)
/* A state reaches what it weighs in this long. */
#define MOOD_STATE_RISE_HOURS 1.0f
/* A thought weighing less than this (in points) is forgotten. */
#define MOOD_FORGET_BELOW 0.5f
/* At MOOD_MAX (MOOD_MIN), work goes MOOD_WORK_SPAN faster (slower), the
 * level counts MOOD_LEVEL_SPAN points more (less) for the quality of what is
 * made, and learning goes MOOD_LEARN_SPAN faster (slower); in between, in
 * proportion to the distance from MOOD_NEUTRAL. */
#define MOOD_WORK_SPAN 0.2f
#define MOOD_LEVEL_SPAN 10.0f
#define MOOD_LEARN_SPAN 0.25f
/* Breaks: below these moods, the chance per hour grows from 0 to
 * MOOD_BREAK_CHANCE_PER_HOUR at mood 0 (tuning). */
#define MOOD_MINOR_BREAK_BELOW 25.0f
#define MOOD_MAJOR_BREAK_BELOW 10.0f
#define MOOD_BREAK_CHANCE_PER_HOUR 0.25f
#define MOOD_BREAK_COOLDOWN_HOURS 12
/* A binge ends once this much food need is eaten beyond what is needed (or
 * when the break's time is up). */
#define MOOD_BINGE_NUTRITION 0.6f
/* A settler going off alone walks this far (travel cost) from where it is. */
#define MOOD_WANDER_OFF_MIN_COST 30.0f
#define MOOD_WANDER_OFF_MAX_COST 90.0f
/* Meals remembered for variety: MOOD_VARIED_KINDS kinds or more among them
 * is a varied diet, a single kind in all of them a monotonous one. */
#define MOOD_MEALS 5
#define MOOD_VARIED_KINDS 3
/* Food eaten below this condition (percent of hit points) was spoiled. */
#define MOOD_SPOILED_PERCENT 50
/* States: awake, feeling colder than MOOD_COLD_CELSIUS or hotter than
 * MOOD_HOT_CELSIUS; wet beyond MOOD_SOAKED_WETNESS. A fire warming a settler
 * by at least MOOD_FIRE_WARMTH degrees where the air is below
 * MOOD_FIRE_COLD_AIR is a good memory. */
#define MOOD_COLD_CELSIUS 5.0f
#define MOOD_HOT_CELSIUS 32.0f
#define MOOD_SOAKED_WETNESS 0.5f
#define MOOD_FIRE_WARMTH 4.0f
#define MOOD_FIRE_COLD_AIR 15.0f
/* A sleep counts (memories of how it was) after this long; the night was
 * cold (hot, wet: soaked) when it felt so for more than MOOD_SLEEP_BAD_SHARE
 * of it. */
#define MOOD_SLEEP_MIN_TICKS (2 * TICKS_PER_HOUR)
#define MOOD_SLEEP_BAD_SHARE 0.25f

typedef enum ThoughtType {
    /* States. */
    THOUGHT_HUNGRY,
    THOUGHT_STARVING,
    THOUGHT_THIRSTY,
    THOUGHT_PARCHED,
    THOUGHT_TIRED,
    THOUGHT_EXHAUSTED,
    THOUGHT_COLD,
    THOUGHT_HOT,
    THOUGHT_SOAKED,
    THOUGHT_IN_THE_RAIN,
    /* Memories. */
    THOUGHT_SLEPT_WELL,
    THOUGHT_SLEPT_ON_GROUND,
    THOUGHT_SLEPT_WET,
    THOUGHT_SLEPT_COLD,
    THOUGHT_SLEPT_HOT,
    THOUGHT_VARIED_MEALS,
    THOUGHT_SAME_FOOD,
    THOUGHT_COOKED_MEAL,
    THOUGHT_SPOILED_FOOD,
    THOUGHT_WARMED_BY_FIRE,
    THOUGHT_FIRESIDE_STORY,
    THOUGHT_CHAT,
    THOUGHT_RELIEF,      /* After a break. */
    THOUGHT_DEATH,       /* A settler of the colony died. */
    THOUGHT_SAW_DEATH,   /* ...before its eyes. */
    THOUGHT_DEBUG_GLOOM, /* The debug cheat (game_debug_sadden()). */
    THOUGHT_COUNT
} ThoughtType;

typedef uint32_t ThoughtMask;
#define THOUGHT_BIT(type) ((ThoughtMask)1u << (unsigned)(type))

typedef struct ThoughtDef {
    const char *id;              /* Stable identifier (saves), never shown. */
    const char *name_key;        /* What the settler thinks, shown to the player. */
    const char *description_key; /* What it is about, in a sentence or two. */
    bool state;                  /* A state (set while it lasts), else a memory. */
    float value;                 /* What it weighs in full (points of mood). */
    float plateau_hours;         /* Memories: full value for this long, then fading. */
    float half_life_hours;       /* Fading: half of it gone in this long. */
    /* Memories: renewed (1), or adding up to this many times the value. */
    int max_stack;
    /* States: growing worse the longer they last, up to grow_max times the
     * value after grow_hours (1: never worse). */
    float grow_hours;
    float grow_max;
} ThoughtDef;

typedef enum MoodBreak {
    MOOD_BREAK_NONE,
    MOOD_BREAK_SULK,       /* Does no work, wanders about. */
    MOOD_BREAK_WANDER_OFF, /* Goes off alone, far away. */
    MOOD_BREAK_BINGE,      /* Eats far more than it needs. */
    MOOD_BREAK_COUNT
} MoodBreak;

typedef struct Mood {
    float value;        /* MOOD_NEUTRAL, the outlook and every thought, MOOD_MIN..MOOD_MAX. */
    float outlook;      /* Points added to the mood (0: ordinary). */
    float sensitivity;  /* What every thought weighs, as a factor (1: ordinary). */
    ThoughtMask states; /* States on now. */
    /* Thoughts that came since the journal last took them (a memory renewed, a
     * state that began): see mood_take_news(). Not part of the hash. */
    ThoughtMask news;
    float weight[THOUGHT_COUNT]; /* What each thought weighs now (0: none). */
    float peak[THOUGHT_COUNT];   /* The most it weighed since it was last renewed. */
    /* Memories: ticks since renewed; states: ticks on (0 when off). */
    int32_t age[THOUGHT_COUNT];
    /* Last meals (food item types), the newest at meals[(meal_next + MOOD_MEALS - 1) % ...]. */
    uint8_t meals[MOOD_MEALS];
    uint8_t meal_count;
    uint8_t meal_next;
    /* The break under way: its kind, ticks left, and what is left to do. */
    uint8_t break_type; /* MoodBreak */
    int32_t break_ticks;
    float binge_left;       /* MOOD_BREAK_BINGE: food need still to eat. */
    bool went_off;          /* MOOD_BREAK_WANDER_OFF: walked away already. */
    bool break_started;     /* A break started (the game raises a notice, then clears it). */
    int32_t break_cooldown; /* Ticks before another break may start. */
} Mood;

/* NULL when out of range. */
const ThoughtDef *thought_def(ThoughtType type);

/* Neutral, no thought, an ordinary character. */
void mood_init(Mood *mood);

/* Sets the outlook (points added to the mood) and the sensitivity (the
 * factor every thought weighs by), and sums the mood up again. */
void mood_set_character(Mood *mood, float outlook, float sensitivity);

/* Remembers something that happened: renewed to its full value, or added
 * (up to max_stack times the value); its plateau starts over. Ignored for
 * states and invalid types. */
void mood_remember(Mood *mood, ThoughtType type);

/* The thoughts that came since the last call (memories remembered, states
 * that began), a bit each: the journal notes them down. */
ThoughtMask mood_take_news(Mood *mood);

/* Replaces the states on now (memory bits are ignored). */
void mood_set_states(Mood *mood, ThoughtMask states);

/* Whether a thought weighs anything now (a state over may still fade). */
bool mood_has(const Mood *mood, ThoughtType type);

/* What a thought weighs on the mood now, its sensitivity counted (0 when it
 * does not). */
float mood_thought_value(const Mood *mood, ThoughtType type);

/* The most it weighed since it was last renewed, sensitivity counted. */
float mood_thought_peak(const Mood *mood, ThoughtType type);

/* Whether a thought is fading: a memory past its plateau, a state over. */
bool mood_fading(const Mood *mood, ThoughtType type);

/* What a state on for `ticks` weighs in full (its value, growing worse). */
float mood_state_value(ThoughtType type, int ticks);

/* `ticks` go by: states set in, grow and fade, memories fade, and the mood
 * is summed up again. A break under way is not counted down here (see
 * mood_break_tick()). */
void mood_advance(Mood *mood, int ticks);

/* A meal of `type` at `hit_points_percent` condition: remembers it, and the
 * memories it brings (cooked, spoiled, varied or monotonous diet). */
void mood_meal(Mood *mood, ItemType type, int hit_points_percent);

/* Whether a food is cooked (roasted roots...). */
bool mood_food_cooked(ItemType type);

/* Effects of a mood value. */
float mood_work_factor(float value);
double mood_level_offset(float value);
double mood_learn_factor(float value);

/* Chance per hour that a settler in this mood breaks down (0 at or above
 * MOOD_MINOR_BREAK_BELOW). */
float mood_break_chance(float value);

/* The kind of break at this mood from a roll in [0, 1): a sulk, or below
 * MOOD_MAJOR_BREAK_BELOW going off alone (rolls below 0.5) or a binge. */
MoodBreak mood_break_kind(float value, float roll);

/* Starts a break of `ticks` (sets break_started). */
void mood_start_break(Mood *mood, MoodBreak kind, int ticks);

/* Ends the break under way: relief, and the cooldown. */
void mood_end_break(Mood *mood);

/* `ticks` of a break go by, and of the cooldown after it; the break ends
 * when its time is up. Returns true when it ended now. */
bool mood_break_tick(Mood *mood, int ticks);

/* Whether a break may start now (none under way, no cooldown). */
bool mood_can_break(const Mood *mood);

/* Translation keys: of a break (what the settler is doing), and of how a
 * mood value feels ("Content"...). */
const char *mood_break_key(MoodBreak kind);
const char *mood_level_key(float value);

uint64_t mood_hash(const Mood *mood, uint64_t hash);

#endif /* COLONY_CORE_MOOD_H */
