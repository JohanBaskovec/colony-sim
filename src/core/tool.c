#include "core/tool.h"

#include "core/util.h"

/* Speed factors by job and tool (docs/KNOWLEDGE.md §2.3). */
static const ToolUseDef TOOL_USE_DEFS[TOOL_USE_COUNT] = {
    [TOOL_USE_CUT_GRASS] = {"cut_grass",
                            0.6f,
                            {[ITEM_CHOPPER] = 0.8f, [ITEM_FLAKE] = 1.0f, [ITEM_HAND_AXE] = 0.9f}},
    [TOOL_USE_KNAPPING] = {"knapping", 0.0f, {[ITEM_COBBLE] = 1.0f}},
    [TOOL_USE_WOODWORKING] = {"woodworking",
                              0.0f,
                              {[ITEM_CHOPPER] = 0.6f, [ITEM_FLAKE] = 1.0f,
                               [ITEM_HAND_AXE] = 1.0f}},
    [TOOL_USE_BUILDING] = {"building",
                           0.8f,
                           {[ITEM_COBBLE] = 0.9f, [ITEM_CHOPPER] = 1.0f, [ITEM_FLAKE] = 0.9f,
                            [ITEM_HAND_AXE] = 1.0f}},
};

/* Crude to superb. */
static const float GRADE_FACTORS[ITEM_QUALITY_COUNT] = {0.8f, 0.9f, 1.0f, 1.1f, 1.2f};

const ToolUseDef *tool_use_def(ToolUse use)
{
    return (unsigned)use < TOOL_USE_COUNT ? &TOOL_USE_DEFS[use] : NULL;
}

float tool_type_factor(ToolUse use, ItemType type)
{
    const ToolUseDef *def = tool_use_def(use);
    if (def == NULL) {
        return 1.0f;
    }
    if (type == ITEM_NONE) {
        return def->bare_hands;
    }
    return (unsigned)type < ITEM_TYPE_COUNT ? def->factor[type] : 0.0f;
}

float tool_grade_factor(ItemStack tool)
{
    const ItemDef *def = item_def((ItemType)tool.type);
    if (def == NULL || !def->has_quality || tool.quality >= ITEM_QUALITY_COUNT) {
        return 1.0f;
    }
    return GRADE_FACTORS[tool.quality];
}

float tool_speed_factor(ToolUse use, ItemStack tool)
{
    if (tool.count == 0 || tool.type == ITEM_NONE) {
        return tool_type_factor(use, ITEM_NONE);
    }
    return tool_type_factor(use, (ItemType)tool.type) * tool_grade_factor(tool);
}

int tool_best(const ItemStack *tools, int count, ToolUse use, ItemTypeMask allowed,
              bool must_beat_hands)
{
    int best = -1;
    float best_factor = 0.0f;
    float floor = must_beat_hands ? tool_type_factor(use, ITEM_NONE) : 0.0f;
    for (int i = 0; i < count; i++) {
        ItemStack tool = tools[i];
        if (tool.count == 0 || !(allowed & ITEM_TYPE_BIT(tool.type))) {
            continue;
        }
        float factor = tool_speed_factor(use, tool);
        if (factor <= floor) {
            continue;
        }
        if (best < 0 || factor > best_factor ||
            (factor == best_factor && tool.hit_points > tools[best].hit_points)) {
            best = i;
            best_factor = factor;
        }
    }
    return best;
}

float tool_best_possible_factor(ToolUse use)
{
    const ToolUseDef *def = tool_use_def(use);
    if (def == NULL) {
        return 1.0f;
    }
    float best = def->bare_hands;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        ItemStack superb = item_stack_make((ItemType)type, 1, ITEM_QUALITY_BEST, 1);
        float factor = tool_speed_factor(use, superb);
        best = factor > best ? factor : best;
    }
    return best;
}

bool tool_wear(ItemStack *tool)
{
    const ItemDef *def = item_def((ItemType)tool->type);
    if (tool->count == 0 || def == NULL || !def->damageable) {
        return false;
    }
    if (tool->hit_points > 1) {
        tool->hit_points--;
        return false;
    }
    *tool = (ItemStack){.type = ITEM_NONE};
    return true;
}
