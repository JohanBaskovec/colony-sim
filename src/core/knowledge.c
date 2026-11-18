#include "core/knowledge.h"

#include "core/i18n.h"
#include "core/util.h"

#include <math.h>

static const KnowledgeDef KNOWLEDGE_DEFS[KNOWLEDGE_COUNT] = {
    [KNOWLEDGE_FIRE] = {"fire", N_("knowledge.fire"), true},
    [KNOWLEDGE_STONE_KNAPPING] = {"stone_knapping", N_("knowledge.stone_knapping"), true},
    [KNOWLEDGE_GATHERING] = {"gathering", N_("knowledge.gathering"), true},
    [KNOWLEDGE_SPEAR_MAKING] = {"spear_making", N_("knowledge.spear_making"), true},
    [KNOWLEDGE_SPEAR_HUNTING] = {"spear_hunting", N_("knowledge.spear_hunting"), true},
    [KNOWLEDGE_BUTCHERING] = {"butchering", N_("knowledge.butchering"), true},
    [KNOWLEDGE_BASIC_SHELTERS] = {"basic_shelters", N_("knowledge.basic_shelters"), true},
};

const KnowledgeDef *knowledge_def(KnowledgeType knowledge)
{
    return (unsigned)knowledge < KNOWLEDGE_COUNT ? &KNOWLEDGE_DEFS[knowledge] : NULL;
}

bool knowledge_is_mastered(double level)
{
    return level >= KNOWLEDGE_MASTERY_LEVEL;
}

/* Position of a level between mastery and the maximum, 0..1. */
static float mastery_progress(double level)
{
    double t = (level - KNOWLEDGE_MASTERY_LEVEL) / (KNOWLEDGE_LEVEL_MAX - KNOWLEDGE_MASTERY_LEVEL);
    return clampf((float)t, 0.0f, 1.0f);
}

float knowledge_work_speed(double level)
{
    return lerpf(KNOWLEDGE_SPEED_AT_MASTERY, KNOWLEDGE_SPEED_AT_MAX, mastery_progress(level));
}

double knowledge_difficulty(double level)
{
    double difficulty = (KNOWLEDGE_DIFFICULTY_CEILING - level) / KNOWLEDGE_DIFFICULTY_SCALE;
    return difficulty > 0.0 ? difficulty : 0.0;
}

double knowledge_age_factor(int age)
{
    if (age < KNOWLEDGE_CHILD_AGE_LIMIT) {
        return KNOWLEDGE_CHILD_FACTOR;
    }
    return age >= KNOWLEDGE_ELDER_AGE ? KNOWLEDGE_ELDER_FACTOR : 1.0;
}

double knowledge_learn(double level, double rate, double hours, int age)
{
    if (!(rate > 0.0) || !(hours > 0.0)) {
        return level;
    }
    level += rate * hours * knowledge_age_factor(age) * knowledge_difficulty(level);
    return level > KNOWLEDGE_LEVEL_MAX ? KNOWLEDGE_LEVEL_MAX : level;
}

double knowledge_learn_capped(double level, double rate, double hours, int age, double cap)
{
    if (!(level < cap)) {
        return level;
    }
    double learned = knowledge_learn(level, rate, hours, age);
    return learned < cap ? learned : cap;
}

double knowledge_taught_rate(double base_rate, double teacher_level)
{
    return teacher_level > 0.0 ? base_rate * teacher_level / KNOWLEDGE_LEVEL_MAX : 0.0;
}

double knowledge_fade_floor(double peak)
{
    return peak * KNOWLEDGE_FADE_FLOOR;
}

double knowledge_fade_day(double level, double peak, int idle_days)
{
    if (idle_days < KNOWLEDGE_FADE_AFTER_DAYS) {
        return level;
    }
    double floor = knowledge_fade_floor(peak);
    if (!(level > floor)) {
        return level;
    }
    double faded = level - 1.0 / KNOWLEDGE_FADE_DAYS_PER_POINT;
    return faded > floor ? faded : floor;
}

float knowledge_random_starting_level(Rng *rng)
{
    int dice =
        rng_range_i32(rng, 0, KNOWLEDGE_START_DIE) + rng_range_i32(rng, 0, KNOWLEDGE_START_DIE);
    return KNOWLEDGE_START_MIN +
           KNOWLEDGE_START_SPAN * (float)dice / (float)(2 * KNOWLEDGE_START_DIE);
}

static int spread(Rng *rng)
{
    return rng != NULL ? rng_range_i32(rng, -KNOWLEDGE_PRODUCT_SPREAD, KNOWLEDGE_PRODUCT_SPREAD)
                       : 0;
}

/* Points above mastery, clamped to the useful range. */
static float points_above_mastery(double level)
{
    return mastery_progress(level) * (KNOWLEDGE_LEVEL_MAX - KNOWLEDGE_MASTERY_LEVEL);
}

int knowledge_product_score(double level, Rng *rng)
{
    float mean = KNOWLEDGE_QUALITY_BASE + KNOWLEDGE_QUALITY_PER_LEVEL * points_above_mastery(level);
    return clampi((int)floorf(mean) + spread(rng), 0, 100);
}

int knowledge_product_condition(double level, Rng *rng)
{
    float mean =
        KNOWLEDGE_CONDITION_BASE + KNOWLEDGE_CONDITION_PER_LEVEL * points_above_mastery(level);
    return clampi((int)floorf(mean) + spread(rng), 1, 100);
}

ItemStack knowledge_product(ItemType type, int count, double level, Rng *rng)
{
    const ItemDef *def = item_def(type);
    if (def == NULL || type == ITEM_NONE) {
        return item_stack_make(ITEM_NONE, 0, 0, 0);
    }
    int quality = def->has_quality ? knowledge_product_score(level, rng) : ITEM_QUALITY_BEST;
    int condition = def->damageable ? knowledge_product_condition(level, rng) : 100;
    return item_stack_make(type, count, quality, item_hit_points_at_percent(type, condition));
}
