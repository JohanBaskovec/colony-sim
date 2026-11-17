#include "core/entry_strip.h"
#include "test_framework.h"

static void test_entries_that_fit_keep_full_size(void)
{
    EntryStrip strip = entry_strip_fit(3, 800.0f, 100.0f, 4.0f, 0.5f);
    CHECK_NEAR(strip.scale, 1.0f, 1e-6);
    CHECK_EQ_INT(strip.per_row, 3);
    CHECK_EQ_INT(strip.rows, 1);
}

static void test_entries_shrink_to_stay_on_one_row(void)
{
    /* 10 × 100 + 9 × 4 = 1036 > 800: shrunk to (800 - 36) / 1000. */
    EntryStrip strip = entry_strip_fit(10, 800.0f, 100.0f, 4.0f, 0.5f);
    CHECK_NEAR(strip.scale, 0.764f, 1e-4);
    CHECK_EQ_INT(strip.per_row, 10);
    CHECK_EQ_INT(strip.rows, 1);
}

static void test_many_entries_wrap_at_the_minimum_scale(void)
{
    /* 64 entries 50 wide at the minimum: 14 per row of 800, 5 rows. */
    EntryStrip strip = entry_strip_fit(64, 800.0f, 100.0f, 4.0f, 0.5f);
    CHECK_NEAR(strip.scale, 0.5f, 1e-6);
    CHECK_EQ_INT(strip.per_row, 14);
    CHECK_EQ_INT(strip.rows, 5);
    CHECK(strip.per_row * (100.0f * strip.scale + 4.0f) - 4.0f <= 800.0f);
}

static void test_degenerate_strips(void)
{
    EntryStrip none = entry_strip_fit(0, 800.0f, 100.0f, 4.0f, 0.5f);
    CHECK_EQ_INT(none.rows, 0);
    /* Narrower than one entry: still one per row. */
    EntryStrip narrow = entry_strip_fit(3, 20.0f, 100.0f, 4.0f, 0.5f);
    CHECK_EQ_INT(narrow.per_row, 1);
    CHECK_EQ_INT(narrow.rows, 3);
}

int main(void)
{
    RUN_TEST(test_entries_that_fit_keep_full_size);
    RUN_TEST(test_entries_shrink_to_stay_on_one_row);
    RUN_TEST(test_many_entries_wrap_at_the_minimum_scale);
    RUN_TEST(test_degenerate_strips);
    return test_summary();
}
