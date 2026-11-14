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

static void test_priority_stepping(void)
{
    CHECK(work_priority_is_valid(WORK_PRIORITY_DISABLED));
    CHECK(work_priority_is_valid(WORK_PRIORITY_LOWEST));
    CHECK(!work_priority_is_valid(-1));
    CHECK(!work_priority_is_valid(WORK_PRIORITY_LOWEST + 1));

    /* Raising: disabled, then from the lowest up to the highest, then around. */
    CHECK_EQ_INT(work_priority_raised(WORK_PRIORITY_DISABLED), WORK_PRIORITY_LOWEST);
    CHECK_EQ_INT(work_priority_raised(WORK_PRIORITY_LOWEST), WORK_PRIORITY_LOWEST - 1);
    CHECK_EQ_INT(work_priority_raised(WORK_PRIORITY_HIGHEST + 1), WORK_PRIORITY_HIGHEST);
    CHECK_EQ_INT(work_priority_raised(WORK_PRIORITY_HIGHEST), WORK_PRIORITY_DISABLED);

    CHECK_EQ_INT(work_priority_lowered(WORK_PRIORITY_DISABLED), WORK_PRIORITY_HIGHEST);
    CHECK_EQ_INT(work_priority_lowered(WORK_PRIORITY_HIGHEST), WORK_PRIORITY_HIGHEST + 1);
    CHECK_EQ_INT(work_priority_lowered(WORK_PRIORITY_LOWEST), WORK_PRIORITY_DISABLED);

    /* Each is the inverse of the other, and a full cycle visits every value. */
    int priority = WORK_PRIORITY_DEFAULT;
    for (int step = 0; step <= WORK_PRIORITY_LOWEST; step++) {
        CHECK_EQ_INT(work_priority_lowered(work_priority_raised(priority)), priority);
        CHECK_EQ_INT(work_priority_raised(work_priority_lowered(priority)), priority);
        priority = work_priority_raised(priority);
        CHECK(work_priority_is_valid(priority));
        CHECK((priority == WORK_PRIORITY_DEFAULT) == (step == WORK_PRIORITY_LOWEST));
    }

    CHECK_EQ_INT(work_priority_raised(99), WORK_PRIORITY_DEFAULT);
    CHECK_EQ_INT(work_priority_lowered(-3), WORK_PRIORITY_DEFAULT);
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_types_at_priority);
    RUN_TEST(test_priority_stepping);
    return test_summary();
}
