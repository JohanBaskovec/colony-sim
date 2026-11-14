/*
 * Tools: how much faster a carried item makes a kind of work, and how tools
 * wear out.
 *
 * Settlers carry their tools (no equipment slots, one tool per item type: see
 * core/colonist.h) and work with the best one they carry for the task. Each
 * kind of work (ToolUse) gives a speed factor per item type (bare hands
 * included; 0: impossible with it), multiplied by the tool's grade factor.
 * A tool loses one hit point per TOOL_WEAR_TICKS of work and breaks at 0.
 * Any item can be a tool (a plain cobble is the knapper's hammer).
 * Design: docs/KNOWLEDGE.md §2.3.
 */
#ifndef COLONY_CORE_TOOL_H
#define COLONY_CORE_TOOL_H

#include "core/game_clock.h"
#include "core/item.h"

#include <stdbool.h>

typedef enum ToolUse {
    TOOL_USE_CUT_GRASS,   /* Cutting grass: gathering, clearing a blueprint's tile. */
    TOOL_USE_KNAPPING,    /* Striking stone. */
    TOOL_USE_WOODWORKING, /* Shaping wood: spears. */
    TOOL_USE_BUILDING,    /* Construction sites and workstations. */
    TOOL_USE_COUNT
} ToolUse;

/* Work no tool helps with (gathering branches, picking berries...). */
#define TOOL_USE_NONE TOOL_USE_COUNT

/* Work done with a tool before it loses one hit point: an hour. */
#define TOOL_WEAR_TICKS TICKS_PER_HOUR

typedef struct ToolUseDef {
    const char *id;   /* Stable identifier, never shown. */
    float bare_hands; /* Speed factor without a tool; 0: impossible. */
    float factor[ITEM_TYPE_COUNT]; /* Per item type used as the tool; 0: useless. */
} ToolUseDef;

/* NULL when out of range (TOOL_USE_NONE included). */
const ToolUseDef *tool_use_def(ToolUse use);

/* Speed factor of an item type for a use (ITEM_NONE: bare hands); 1 for
 * TOOL_USE_NONE whatever the item, 0 when the type cannot do it. */
float tool_type_factor(ToolUse use, ItemType type);

/* Grade factor on speed, from ×0.8 (crude) to ×1.2 (superb); 1 for items
 * without quality. */
float tool_grade_factor(ItemStack tool);

/* Speed factor of a tool for a use: type factor × grade factor (bare hands
 * for an empty stack). */
float tool_speed_factor(ToolUse use, ItemStack tool);

/*
 * Index of the best of `count` carried tools for a use, among the types of
 * `allowed`: the highest speed factor (then the most hit points, then the
 * first). With `must_beat_hands`, only a tool faster than bare hands counts.
 * -1 when none qualifies.
 */
int tool_best(const ItemStack *tools, int count, ToolUse use, ItemTypeMask allowed,
              bool must_beat_hands);

/* The best speed factor any tool can give for a use (best type, best grade). */
float tool_best_possible_factor(ToolUse use);

/* Takes one hit point from a tool; returns true when it breaks (the stack is
 * then emptied). Items that cannot be damaged never wear. */
bool tool_wear(ItemStack *tool);

#endif /* COLONY_CORE_TOOL_H */
