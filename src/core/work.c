#include "core/work.h"

#include "core/i18n.h"

static const WorkTypeDef WORK_TYPE_DEFS[WORK_TYPE_COUNT] = {
    /* Mining and woodcutting need knowledges that do not exist yet. */
    [WORK_MINING] = {"mining", N_("work.mining"), KNOWLEDGE_NONE},
    [WORK_WOODCUTTING] = {"woodcutting", N_("work.woodcutting"), KNOWLEDGE_NONE},
    [WORK_GATHERING] = {"gathering", N_("work.gathering"), KNOWLEDGE_GATHERING},
    [WORK_HAULING] = {"hauling", N_("work.hauling"), KNOWLEDGE_NONE},
};

const WorkTypeDef *work_type_def(WorkType type)
{
    return (unsigned)type < WORK_TYPE_COUNT ? &WORK_TYPE_DEFS[type] : NULL;
}

bool work_priority_is_valid(int priority)
{
    return priority >= WORK_PRIORITY_DISABLED && priority <= WORK_PRIORITY_LOWEST;
}

int work_priority_raised(int priority)
{
    if (!work_priority_is_valid(priority)) {
        return WORK_PRIORITY_DEFAULT;
    }
    if (priority == WORK_PRIORITY_DISABLED) {
        return WORK_PRIORITY_LOWEST;
    }
    return priority == WORK_PRIORITY_HIGHEST ? WORK_PRIORITY_DISABLED : priority - 1;
}

int work_priority_lowered(int priority)
{
    if (!work_priority_is_valid(priority)) {
        return WORK_PRIORITY_DEFAULT;
    }
    if (priority == WORK_PRIORITY_DISABLED) {
        return WORK_PRIORITY_HIGHEST;
    }
    return priority == WORK_PRIORITY_LOWEST ? WORK_PRIORITY_DISABLED : priority + 1;
}

WorkTypeMask work_types_at_priority(const uint8_t priorities[WORK_TYPE_COUNT], int priority)
{
    WorkTypeMask mask = 0;
    if (priority < WORK_PRIORITY_HIGHEST || priority > WORK_PRIORITY_LOWEST) {
        return 0;
    }
    for (int type = 0; type < WORK_TYPE_COUNT; type++) {
        if (priorities[type] == priority) {
            mask |= WORK_TYPE_BIT(type);
        }
    }
    return mask;
}
