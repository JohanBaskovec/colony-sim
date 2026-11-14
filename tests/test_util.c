/* Small helpers (core/util.h). */
#include "core/util.h"

#include "test_framework.h"

static void test_row_outwards(void)
{
    /* Center 2 of 5 rows: 2, 3, 1, 4, 0, then out of range. */
    int expected[] = {2, 3, 1, 4, 0, -1, -1, -1, -1, -1};
    for (int step = 0; step < 10; step++) {
        CHECK_EQ_INT(row_outwards(2, step, 5), expected[step]);
    }
    /* From the first row, every row once. */
    int seen[4] = {0};
    for (int step = 0; step < 8; step++) {
        int row = row_outwards(0, step, 4);
        if (row >= 0) {
            seen[row]++;
        }
    }
    for (int row = 0; row < 4; row++) {
        CHECK_EQ_INT(seen[row], 1);
    }
}

int main(void)
{
    RUN_TEST(test_row_outwards);
    return test_summary();
}
