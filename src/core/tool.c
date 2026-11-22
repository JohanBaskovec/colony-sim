#include "core/tool.h"

#include "core/i18n.h"
#include "core/util.h"
#include "core/wood.h"

/* Speed factors by job and tool (docs/KNOWLEDGE.md §2.3). */
static const ToolUseDef TOOL_USE_DEFS[TOOL_USE_COUNT] = {
    [TOOL_USE_CUT_GRASS] = {"cut_grass",
                            N_("tool_use.cut_grass"),
                            0.6f,
                            {[ITEM_CHOPPER] = 0.8f, [ITEM_FLAKE] = 1.0f, [ITEM_HAND_AXE] = 0.9f}},
    [TOOL_USE_KNAPPING] = {"knapping", N_("tool_use.knapping"), 0.0f, {[ITEM_COBBLE] = 1.0f}},
    [TOOL_USE_WOODWORKING] = {"woodworking",
                              N_("tool_use.woodworking"),
                              0.0f,
                              {[ITEM_CHOPPER] = 0.6f, [ITEM_FLAKE] = 1.0f, [ITEM_HAND_AXE] = 1.0f}},
    [TOOL_USE_BUILDING] = {"building",
                           N_("tool_use.building"),
                           0.8f,
                           {[ITEM_COBBLE] = 0.9f,
                            [ITEM_CHOPPER] = 1.0f,
                            [ITEM_FLAKE] = 0.9f,
                            [ITEM_HAND_AXE] = 1.0f}},
    [TOOL_USE_DIGGING] = {"digging",
                          N_("tool_use.digging"),
                          0.3f,
                          {[ITEM_HAND_AXE] = 0.5f,
                           [ITEM_DIGGING_STICK] = 1.0f,
                           [ITEM_DIGGING_STICK_HARDENED] = 1.0f}},
    [TOOL_USE_CUTTING] = {"cutting",
                          N_("tool_use.cutting"),
                          0.5f,
                          {[ITEM_CHOPPER] = 0.7f, [ITEM_FLAKE] = 1.0f, [ITEM_HAND_AXE] = 0.9f}},
    /* A flake cuts the fibre it should only clean (fingernails do it slowly). */
    [TOOL_USE_SCRAPING] = {"scraping",
                           N_("tool_use.scraping"),
                           0.3f,
                           {[ITEM_FLAKE] = 0.6f, [ITEM_WOODEN_SCRAPER] = 1.0f}},
    [TOOL_USE_PIERCING] =
        {"piercing",
         N_("tool_use.piercing"),
         0.0f,
         {[ITEM_FLAKE] = 0.4f, [ITEM_FLINT_BORER] = 1.0f, [ITEM_BONE_AWL] = 1.2f}},
    /* One cut down the trunk with a sharp edge, then the sheet lifted off. */
    [TOOL_USE_PEELING] = {"peeling",
                          N_("tool_use.peeling"),
                          0.3f,
                          {[ITEM_CHOPPER] = 0.6f, [ITEM_FLAKE] = 1.0f, [ITEM_HAND_AXE] = 0.8f}},
    /* A blow low on the shoot with a heavy edge; a flake saws through, slowly. */
    [TOOL_USE_CUTTING_RODS] =
        {"cutting_rods",
         N_("tool_use.cutting_rods"),
         0.0f,
         {[ITEM_CHOPPER] = 0.9f, [ITEM_FLAKE] = 0.6f, [ITEM_HAND_AXE] = 1.0f}},
    /* A sharp flake skins and cuts best; the axe and the chopper break the joints. */
    [TOOL_USE_BUTCHERING] = {"butchering",
                             N_("tool_use.butchering"),
                             0.0f,
                             {[ITEM_CHOPPER] = 0.8f, [ITEM_FLAKE] = 1.0f, [ITEM_HAND_AXE] = 0.9f}},
    /* The eyed needle carries the thread through; with an awl the holes are pierced
     * first and the thread pushed through each, slower. */
    [TOOL_USE_SEWING] =
        {"sewing",
         N_("tool_use.sewing"),
         0.0f,
         {[ITEM_BONE_NEEDLE] = 1.0f, [ITEM_BONE_AWL] = 0.6f, [ITEM_FLINT_BORER] = 0.3f}},
    /* A tree is felled with a hafted axe; a hand axe held in the fist only gnaws at it. */
    [TOOL_USE_FELLING] =
        {"felling",
         N_("tool_use.felling"),
         0.0f,
         {[ITEM_HAFTED_AXE] = 1.0f, [ITEM_HAND_AXE] = 0.2f, [ITEM_CHOPPER] = 0.15f}},
};

/* Speed factor of a tool of the worst and of the best quality, linear in
 * between (×1 at ITEM_QUALITY_STANDARD). */
#define TOOL_QUALITY_FACTOR_MIN 0.8f
#define TOOL_QUALITY_FACTOR_MAX 1.2f

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

float tool_quality_factor(ItemStack tool)
{
    const ItemDef *def = item_def((ItemType)tool.type);
    if (def == NULL || !def->has_quality) {
        return 1.0f;
    }
    return lerpf(TOOL_QUALITY_FACTOR_MIN, TOOL_QUALITY_FACTOR_MAX,
                 (float)clampi(tool.quality, ITEM_QUALITY_WORST, ITEM_QUALITY_BEST) /
                     (float)ITEM_QUALITY_BEST);
}

float tool_speed_factor(ToolUse use, ItemStack tool)
{
    if (tool.count == 0 || tool.type == ITEM_NONE) {
        return tool_type_factor(use, ITEM_NONE);
    }
    return tool_type_factor(use, (ItemType)tool.type) * tool_quality_factor(tool);
}

int tool_best(const ItemStack *tools, int count, ToolUse use, ItemTypeMask allowed,
              bool must_beat_hands)
{
    int best = -1;
    float best_factor = 0.0f;
    float floor = must_beat_hands ? tool_type_factor(use, ITEM_NONE) : 0.0f;
    for (int i = 0; i < count; i++) {
        ItemStack tool = tools[i];
        if (tool.count == 0 || !(item_types_has(allowed, tool.type))) {
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

int tool_wear_ticks(ItemStack tool)
{
    const ItemDef *def = item_def((ItemType)tool.type);
    float toughness = def != NULL && def->wooden ? wood_toughness(tool.woods) : 1.0f;
    return (int)((float)TOOL_WEAR_TICKS * toughness + 0.5f);
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
