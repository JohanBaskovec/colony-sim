/*
 * Work types (mining, woodcutting...) and work priorities.
 *
 * Every job belongs to a work type. Each settler has a priority per work type:
 * 0 means the settler never does that work, otherwise 1 (highest) to
 * WORK_PRIORITY_LOWEST. Settlers pick jobs by priority first, then distance.
 */
#ifndef COLONY_CORE_WORK_H
#define COLONY_CORE_WORK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum WorkType {
    WORK_MINING,
    WORK_WOODCUTTING,
    WORK_GATHERING,
    WORK_HAULING,
    WORK_TYPE_COUNT
} WorkType;

#define WORK_PRIORITY_DISABLED 0
#define WORK_PRIORITY_HIGHEST 1
#define WORK_PRIORITY_LOWEST 4
#define WORK_PRIORITY_DEFAULT 3

typedef struct WorkTypeDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
} WorkTypeDef;

const WorkTypeDef *work_type_def(WorkType type);

/* Whether a value is WORK_PRIORITY_DISABLED or a priority level. */
bool work_priority_is_valid(int priority);

/*
 * Stepping through priorities, as the player does in the priorities grid.
 * Raising goes disabled → lowest → ... → highest, then wraps to disabled;
 * lowering goes the other way. Invalid values give the default priority.
 */
int work_priority_raised(int priority);
int work_priority_lowered(int priority);

/* Bit mask with one bit per WorkType. */
typedef uint32_t WorkTypeMask;

#define WORK_TYPE_BIT(type) ((WorkTypeMask)1u << (unsigned)(type))

/* Work types whose priority equals `priority` (1..WORK_PRIORITY_LOWEST). */
WorkTypeMask work_types_at_priority(const uint8_t priorities[WORK_TYPE_COUNT], int priority);

#endif /* COLONY_CORE_WORK_H */
