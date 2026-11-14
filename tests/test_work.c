#include "core/i18n.h"
#include "core/work.h"

#include "test_framework.h"

static void test_definitions(void)
{
    for (int type = 0; type < WORK_TYPE_COUNT; type++) {
        const WorkTypeDef *def = work_type_def((WorkType)type);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL);
        CHECK(i18n_is_valid_key(def->name_key));
    }
    CHECK(work_type_def(WORK_TYPE_COUNT) == NULL);
    CHECK(WORK_TYPE_COUNT <= 32); /* Fits in a WorkTypeMask. */
    CHECK(WORK_PRIORITY_DEFAULT >= WORK_PRIORITY_HIGHEST &&
          WORK_PRIORITY_DEFAULT <= WORK_PRIORITY_LOWEST);
}

static void test_types_at_priority(void)
{
    uint8_t priorities[WORK_TYPE_COUNT] = {0};
    priorities[WORK_MINING] = 1;
    priorities[WORK_WOODCUTTING] = WORK_PRIORITY_DISABLED;
    CHECK_EQ_UINT(work_types_at_priority(priorities, 1), WORK_TYPE_BIT(WORK_MINING));
    CHECK_EQ_UINT(work_types_at_priority(priorities, 2), 0);

    priorities[WORK_WOODCUTTING] = 1;
    CHECK_EQ_UINT(work_types_at_priority(priorities, 1),
                  WORK_TYPE_BIT(WORK_MINING) | WORK_TYPE_BIT(WORK_WOODCUTTING));
    /* "Disabled" is not a priority level. */
    CHECK_EQ_UINT(work_types_at_priority(priorities, WORK_PRIORITY_DISABLED), 0);
    CHECK_EQ_UINT(work_types_at_priority(priorities, WORK_PRIORITY_LOWEST + 1), 0);
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_types_at_priority);
    return test_summary();
}
