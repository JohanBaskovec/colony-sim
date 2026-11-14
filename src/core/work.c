#include "core/work.h"

#include "core/i18n.h"

static const WorkTypeDef WORK_TYPE_DEFS[WORK_TYPE_COUNT] = {
    [WORK_MINING] = {"mining", N_("work.mining")},
    [WORK_WOODCUTTING] = {"woodcutting", N_("work.woodcutting")},
};

const WorkTypeDef *work_type_def(WorkType type)
{
    return (unsigned)type < WORK_TYPE_COUNT ? &WORK_TYPE_DEFS[type] : NULL;
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
