/*
 * Knowledge: what a settler knows how to do, and how well.
 *
 * Each settler has a level from KNOWLEDGE_LEVEL_MIN to KNOWLEDGE_LEVEL_MAX in
 * every knowledge (fire, stone knapping, gathering...). There is no tree: no
 * knowledge requires another. Below KNOWLEDGE_MASTERY_LEVEL a knowledge
 * unlocks nothing; from there, the settler can carry out the orders and
 * recipes that belong to it, each from its own minimum level. The level sets
 * how fast the work goes and how good its products are. Levels rise by
 * doing the work, by assisting a master (apprenticeship), by listening to
 * fireside stories and, a little, by chatting (core/conversation.h); each way
 * of learning stops at its own cap.
 * Unpractised knowledges fade, down to a share of the best level reached.
 * Design: docs/KNOWLEDGE.md.
 */
#ifndef COLONY_CORE_KNOWLEDGE_H
#define COLONY_CORE_KNOWLEDGE_H

#include "core/item.h"
#include "core/rng.h"

#include <stdbool.h>

typedef enum KnowledgeType {
    KNOWLEDGE_FIRE,
    KNOWLEDGE_STONE_KNAPPING,
    KNOWLEDGE_GATHERING,
    KNOWLEDGE_SPEAR_MAKING,
    KNOWLEDGE_SPEAR_HUNTING,
    KNOWLEDGE_BUTCHERING,
    KNOWLEDGE_BASIC_SHELTERS,
    KNOWLEDGE_CORDAGE,      /* Fibre and cord (docs/CONTAINERS.md §8). */
    KNOWLEDGE_BASKETRY,     /* Trays, baskets, bags, bark boxes. */
    KNOWLEDGE_SKIN_WORKING, /* Dressing hides, sewing them (docs/KNOWLEDGE.md §9). */
    KNOWLEDGE_BONE_WORKING, /* Awls and needles of bone. */
    KNOWLEDGE_HAFTING,      /* Birch tar, stone points and blades fixed on shafts (§13). */
    KNOWLEDGE_FELLING,      /* Felling trees, with an axe. */
    KNOWLEDGE_COUNT
} KnowledgeType;

/* Work that needs no knowledge (hauling), or that no known knowledge allows. */
#define KNOWLEDGE_NONE KNOWLEDGE_COUNT

#define KNOWLEDGE_LEVEL_MIN 0.0f
#define KNOWLEDGE_LEVEL_MAX 100.0f
/* Lowest level at which a knowledge unlocks anything. */
#define KNOWLEDGE_MASTERY_LEVEL 50.0f

/* Work speed multiplier at KNOWLEDGE_MASTERY_LEVEL and KNOWLEDGE_LEVEL_MAX,
 * linear in between (×1 at 75). */
#define KNOWLEDGE_SPEED_AT_MASTERY 0.5f
#define KNOWLEDGE_SPEED_AT_MAX 1.5f

/*
 * Products of work. The quality (0..100) is around KNOWLEDGE_QUALITY_BASE +
 * KNOWLEDGE_QUALITY_PER_LEVEL × (level − mastery). The condition (percent of the
 * item's hit points) is around KNOWLEDGE_CONDITION_BASE +
 * KNOWLEDGE_CONDITION_PER_LEVEL × (level − mastery), capped at 100. Both vary
 * by up to ±KNOWLEDGE_PRODUCT_SPREAD.
 */
#define KNOWLEDGE_QUALITY_BASE 25.0f
#define KNOWLEDGE_QUALITY_PER_LEVEL 1.4f
#define KNOWLEDGE_CONDITION_BASE 50.0f
#define KNOWLEDGE_CONDITION_PER_LEVEL 1.6f
#define KNOWLEDGE_PRODUCT_SPREAD 10

/*
 * Learning. A gain in points is rate × hours × age factor × difficulty, where
 * difficulty = (KNOWLEDGE_DIFFICULTY_CEILING − level) /
 * KNOWLEDGE_DIFFICULTY_SCALE: the basics come quickly, mastery slowly (×1.83
 * at 0, ×1 at 50, ×0.17 at 100). Doing the work of a knowledge gives
 * KNOWLEDGE_DOING_RATE points per hour at difficulty 1: about 110 hours of
 * work from 50 to 100.
 */
#define KNOWLEDGE_DIFFICULTY_CEILING 110.0
#define KNOWLEDGE_DIFFICULTY_SCALE 60.0
#define KNOWLEDGE_DOING_RATE 1.0

/*
 * Learning from someone else: apprentices get KNOWLEDGE_APPRENTICE_RATE and
 * listeners KNOWLEDGE_CONVERSATION_RATE points per hour, scaled by the
 * teacher's level / 100. Nobody learns past the teacher's level, nor past
 * KNOWLEDGE_CONVERSATION_CAP by chatting.
 */
#define KNOWLEDGE_APPRENTICE_RATE 1.5
#define KNOWLEDGE_CONVERSATION_RATE 0.3
#define KNOWLEDGE_CONVERSATION_CAP 30.0
/* Fireside stories: KNOWLEDGE_STORY_RATE × the storyteller's level / 100,
 * up to KNOWLEDGE_STORY_CAP and the storyteller's level. */
#define KNOWLEDGE_STORY_RATE 1.0
#define KNOWLEDGE_STORY_CAP 60.0

/*
 * Fading: a knowledge not practised for KNOWLEDGE_FADE_AFTER_DAYS loses one
 * point every KNOWLEDGE_FADE_DAYS_PER_POINT days, never below
 * KNOWLEDGE_FADE_FLOOR of the highest level ever reached.
 */
#define KNOWLEDGE_FADE_AFTER_DAYS 15
#define KNOWLEDGE_FADE_DAYS_PER_POINT 15
#define KNOWLEDGE_FADE_FLOOR 0.8

/* A knowledge is discovered (shown at all) while some living settler has at
 * least this level in it. */
#define KNOWLEDGE_DISCOVERED_LEVEL 1.0

/* Ways of learning, for the gains shown to the player. */
typedef enum KnowledgeSource {
    KNOWLEDGE_SOURCE_DOING,
    KNOWLEDGE_SOURCE_APPRENTICESHIP,
    KNOWLEDGE_SOURCE_CONVERSATION,
    KNOWLEDGE_SOURCE_STORIES,
    KNOWLEDGE_SOURCE_COUNT
} KnowledgeSource;

/* Learning speed by age: children learn twice as fast, elders half as fast. */
#define KNOWLEDGE_CHILD_AGE_LIMIT 14 /* Children are younger than this. */
#define KNOWLEDGE_ELDER_AGE 60
#define KNOWLEDGE_CHILD_FACTOR 2.0
#define KNOWLEDGE_ELDER_FACTOR 0.5

/* Starting levels in the starting knowledges: KNOWLEDGE_START_MIN plus
 * KNOWLEDGE_START_SPAN × (two dice of 0..KNOWLEDGE_START_DIE) /
 * (2 × KNOWLEDGE_START_DIE), i.e. 50 to 80, mostly around 65. */
#define KNOWLEDGE_START_MIN 50.0f
#define KNOWLEDGE_START_SPAN 30.0f
#define KNOWLEDGE_START_DIE 5

typedef struct KnowledgeDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    bool starting; /* Every first settler starts with it. */
} KnowledgeDef;

/* NULL when out of range (KNOWLEDGE_NONE included). */
const KnowledgeDef *knowledge_def(KnowledgeType knowledge);

/* Whether a level is enough to use a knowledge at all. */
bool knowledge_is_mastered(double level);

/* Work speed multiplier at a level: KNOWLEDGE_SPEED_AT_MASTERY at the mastery
 * level (and below: callers check the level first), up to
 * KNOWLEDGE_SPEED_AT_MAX. */
float knowledge_work_speed(double level);

/* Learning multiplier at a level (see KNOWLEDGE_DIFFICULTY_CEILING). */
double knowledge_difficulty(double level);

/* Learning multiplier at an age. */
double knowledge_age_factor(int age);

/* Level after learning for `hours` at `rate` points per hour (at difficulty 1)
 * by a settler of this age, capped at KNOWLEDGE_LEVEL_MAX. */
double knowledge_learn(double level, double rate, double hours, int age);

/* Like knowledge_learn(), never past `cap`: a level already at or above it
 * stays as it is. */
double knowledge_learn_capped(double level, double rate, double hours, int age, double cap);

/* Rate of learning from a teacher at `teacher_level`: `base_rate` × level / 100. */
double knowledge_taught_rate(double base_rate, double teacher_level);

/* Lowest level fading can bring a knowledge down to, from its peak. */
double knowledge_fade_floor(double peak);

/* Level after one more day of fading, `idle_days` days after the last
 * practice (unchanged before KNOWLEDGE_FADE_AFTER_DAYS). */
double knowledge_fade_day(double level, double peak, int idle_days);

/* Random starting level in a starting knowledge. */
float knowledge_random_starting_level(Rng *rng);

/* Quality (0..100) of something produced at this level; `rng` may be NULL
 * for the average result. */
int knowledge_product_score(double level, Rng *rng);

/* Condition (1..100, percent of the hit points) of something produced at
 * this level; `rng` may be NULL for the average result. */
int knowledge_product_condition(double level, Rng *rng);

/*
 * `count` items of `type` produced by a worker at this level: quality and
 * condition are rolled for the item types that have them (randomness is only
 * drawn for those), others come out perfect. Fresh (item_stack_set_fresh()):
 * cut, picked or dug up from what grows.
 */
ItemStack knowledge_product(ItemType type, int count, double level, Rng *rng);

#endif /* COLONY_CORE_KNOWLEDGE_H */
