/*
 * Skills: how good a settler is at a kind of work, and what that changes.
 *
 * Each settler has a level per skill, from SKILL_LEVEL_MIN to SKILL_LEVEL_MAX,
 * and gains experience by working: one point per unit of work done on an
 * order of that skill. Each level takes longer to reach than the previous one.
 * A work type (core/work.h) may rely on a skill. The skill of the worker sets
 * the condition of what the work produces: a skilled woodcutter brings back
 * better and less damaged logs than a clumsy one (core/item.h).
 */
#ifndef COLONY_CORE_SKILL_H
#define COLONY_CORE_SKILL_H

#include "core/item.h"
#include "core/rng.h"

typedef enum SkillType {
    SKILL_MINING,
    SKILL_WOODCUTTING,
    SKILL_GATHERING,
    SKILL_COUNT
} SkillType;

#define SKILL_NONE SKILL_COUNT /* Work that needs no skill (hauling). */

#define SKILL_LEVEL_MIN 0
#define SKILL_LEVEL_MAX 20
/* Starting levels are the sum of two dice of 0..SKILL_START_DIE: mostly
 * average settlers, a few novices and a few experts. */
#define SKILL_START_DIE 6

/* Experience needed to go from level L to L + 1: SKILL_XP_PER_LEVEL × (L + 1).
 * With one point per tick of work, level 1 takes one in-game hour of work,
 * level 20 twenty hours: about 210 hours from novice to master. */
#define SKILL_XP_PER_LEVEL 2500.0f

/*
 * Products of skilled work. A quality score (0..100) around
 * SKILL_QUALITY_BASE + SKILL_QUALITY_PER_LEVEL × level gives the grade, one
 * grade per SKILL_QUALITY_GRADE_WIDTH points (novices make crude or rough
 * items, experts superb ones). The condition (percent of the item's hit
 * points) is around SKILL_CONDITION_BASE + SKILL_CONDITION_PER_LEVEL × level,
 * capped at 100: an expert never damages what it produces. Both vary by up to
 * ±SKILL_PRODUCT_SPREAD.
 */
#define SKILL_QUALITY_BASE 25
#define SKILL_QUALITY_PER_LEVEL 3.5f
#define SKILL_QUALITY_GRADE_WIDTH 20
#define SKILL_CONDITION_BASE 50
#define SKILL_CONDITION_PER_LEVEL 4
#define SKILL_PRODUCT_SPREAD 10

typedef struct SkillDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
} SkillDef;

/* NULL when out of range (SKILL_NONE included). */
const SkillDef *skill_def(SkillType skill);

/* Experience needed to reach the next level from `level`; 0 at the maximum
 * level (or above). */
float skill_xp_to_next_level(int level);

/* Random starting level for a new settler. */
int skill_random_level(Rng *rng);

/* Quality score (0..100) of something produced at this skill level; `rng`
 * may be NULL for the average result. */
int skill_product_score(int level, Rng *rng);

/* Grade of a quality score. */
ItemQuality skill_quality_of_score(int score);

/* Condition (1..100, percent of the hit points) of something produced at
 * this skill level; `rng` may be NULL for the average result. */
int skill_product_condition(int level, Rng *rng);

/*
 * `count` items of `type` produced by a worker of this skill level: quality
 * and condition are rolled for the item types that have them (randomness is
 * only drawn for those), others come out perfect.
 */
ItemStack skill_product(ItemType type, int count, int level, Rng *rng);

#endif /* COLONY_CORE_SKILL_H */
