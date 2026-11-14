#include "core/skill.h"

#include "core/i18n.h"
#include "core/util.h"

static const SkillDef SKILL_DEFS[SKILL_COUNT] = {
    [SKILL_MINING] = {"mining", N_("skill.mining")},
    [SKILL_WOODCUTTING] = {"woodcutting", N_("skill.woodcutting")},
    [SKILL_GATHERING] = {"gathering", N_("skill.gathering")},
};

const SkillDef *skill_def(SkillType skill)
{
    return (unsigned)skill < SKILL_COUNT ? &SKILL_DEFS[skill] : NULL;
}

float skill_xp_to_next_level(int level)
{
    if (level >= SKILL_LEVEL_MAX) {
        return 0.0f;
    }
    level = clampi(level, SKILL_LEVEL_MIN, SKILL_LEVEL_MAX - 1);
    return SKILL_XP_PER_LEVEL * (float)(level + 1);
}

int skill_random_level(Rng *rng)
{
    int level = rng_range_i32(rng, 0, SKILL_START_DIE) + rng_range_i32(rng, 0, SKILL_START_DIE);
    return clampi(level, SKILL_LEVEL_MIN, SKILL_LEVEL_MAX);
}

static int spread(Rng *rng)
{
    return rng != NULL ? rng_range_i32(rng, -SKILL_PRODUCT_SPREAD, SKILL_PRODUCT_SPREAD) : 0;
}

int skill_product_score(int level, Rng *rng)
{
    level = clampi(level, SKILL_LEVEL_MIN, SKILL_LEVEL_MAX);
    int mean = SKILL_QUALITY_BASE + (int)(SKILL_QUALITY_PER_LEVEL * (float)level);
    return clampi(mean + spread(rng), 0, 100);
}

ItemQuality skill_quality_of_score(int score)
{
    return (ItemQuality)clampi(score / SKILL_QUALITY_GRADE_WIDTH, 0, ITEM_QUALITY_BEST);
}

int skill_product_condition(int level, Rng *rng)
{
    level = clampi(level, SKILL_LEVEL_MIN, SKILL_LEVEL_MAX);
    int mean = SKILL_CONDITION_BASE + SKILL_CONDITION_PER_LEVEL * level;
    return clampi(mean + spread(rng), 1, 100);
}

ItemStack skill_product(ItemType type, int count, int level, Rng *rng)
{
    const ItemDef *def = item_def(type);
    if (def == NULL || type == ITEM_NONE) {
        return item_stack_make(ITEM_NONE, 0, 0, 0);
    }
    int quality = def->has_quality ? (int)skill_quality_of_score(skill_product_score(level, rng))
                                   : ITEM_QUALITY_BEST;
    int condition = def->damageable ? skill_product_condition(level, rng) : 100;
    return item_stack_make(type, count, quality, item_hit_points_at_percent(type, condition));
}
