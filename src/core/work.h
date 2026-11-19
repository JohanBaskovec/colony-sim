/*
 * Work types (mining, woodcutting...) and work priorities.
 *
 * Every job belongs to a work type. Each settler has a priority per work type:
 * 0 means the settler never does that work, otherwise 1 (highest) to
 * WORK_PRIORITY_LOWEST. Settlers pick jobs by priority first, then by work
 * type in the order below, then distance, among the jobs their knowledge
 * allows (core/knowledge.h).
 */
#ifndef COLONY_CORE_WORK_H
#define COLONY_CORE_WORK_H

#include "core/knowledge.h"

#include <stdbool.h>
#include <stdint.h>

/* In the order of the columns of the priorities grid: at the same priority,
 * a settler does the work of the leftmost column first. Gathering comes after
 * construction and crafting: a zone gathering forever would keep settlers
 * from them otherwise. Lighting a hearth is short and comes first. */
typedef enum WorkType {
    WORK_MINING,
    WORK_FIRE,         /* Lighting hearths (refuelling them is hauling). */
    WORK_CONSTRUCTION, /* Construction sites, workstations, taking structures apart. */
    WORK_CRAFTING,     /* Working through the bills of workstations. */
    WORK_WOODCUTTING,
    WORK_GATHERING,
    WORK_HAULING,
    WORK_CLEANING,
    WORK_TYPE_COUNT
} WorkType;

#define WORK_PRIORITY_DISABLED 0
#define WORK_PRIORITY_HIGHEST 1
#define WORK_PRIORITY_LOWEST 4
#define WORK_PRIORITY_DEFAULT 3
/* New settlers clean at the lowest priority. */
#define WORK_PRIORITY_DEFAULT_CLEANING WORK_PRIORITY_LOWEST

typedef struct WorkTypeDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    /* Knowledge most of its jobs rely on, shown in the work priorities grid;
     * KNOWLEDGE_NONE when it needs none (hauling) or none exists yet. */
    KnowledgeType knowledge;
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

/* Priority of a workstation or a construction site (core/workstation.h,
 * core/construction.h): settlers take the jobs at those of the highest
 * priority (JOB_PRIORITY_HIGHEST) first, whatever the distance, then the
 * nearest one among equals. */
#define JOB_PRIORITY_HIGHEST 1
#define JOB_PRIORITY_LOWEST 10
#define JOB_PRIORITY_DEFAULT 5

/* A priority within JOB_PRIORITY_HIGHEST..JOB_PRIORITY_LOWEST. */
int job_priority_clamped(int priority);

/* Bit mask with one bit per WorkType. */
typedef uint32_t WorkTypeMask;

#define WORK_TYPE_BIT(type) ((WorkTypeMask)1u << (unsigned)(type))

/* Work types whose priority equals `priority` (1..WORK_PRIORITY_LOWEST). */
WorkTypeMask work_types_at_priority(const uint8_t priorities[WORK_TYPE_COUNT], int priority);

#endif /* COLONY_CORE_WORK_H */
