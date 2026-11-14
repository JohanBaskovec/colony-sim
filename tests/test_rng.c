#include "core/rng.h"

#include "test_framework.h"

static void test_matches_pcg32_reference_output(void)
{
    /* Reference values from the official pcg32 demo (seed 42, stream 54). */
    Rng rng;
    rng_seed(&rng, 42u, 54u);
    const uint32_t expected[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u,
                                 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        CHECK_EQ_UINT(rng_next_u32(&rng), expected[i]);
    }
}

static void test_same_seed_gives_same_sequence(void)
{
    Rng a, b;
    rng_seed(&a, 1234u, 7u);
    rng_seed(&b, 1234u, 7u);
    for (int i = 0; i < 1000; i++) {
        CHECK_EQ_UINT(rng_next_u32(&a), rng_next_u32(&b));
    }
}

static void test_different_streams_differ(void)
{
    Rng a, b;
    rng_seed(&a, 1234u, 1u);
    rng_seed(&b, 1234u, 2u);
    int equal = 0;
    for (int i = 0; i < 100; i++) {
        equal += rng_next_u32(&a) == rng_next_u32(&b);
    }
    CHECK(equal < 5);
}

static void test_range_u32_stays_in_bounds(void)
{
    Rng rng;
    rng_seed(&rng, 99u, 0u);
    CHECK_EQ_UINT(rng_range_u32(&rng, 0), 0);
    CHECK_EQ_UINT(rng_range_u32(&rng, 1), 0);
    for (int i = 0; i < 10000; i++) {
        CHECK(rng_range_u32(&rng, 7) < 7);
    }
}

static void test_range_i32_is_inclusive(void)
{
    Rng rng;
    rng_seed(&rng, 5u, 0u);
    int seen_min = 0, seen_max = 0;
    for (int i = 0; i < 10000; i++) {
        int32_t value = rng_range_i32(&rng, -3, 3);
        CHECK(value >= -3 && value <= 3);
        seen_min |= value == -3;
        seen_max |= value == 3;
    }
    CHECK(seen_min);
    CHECK(seen_max);
    CHECK_EQ_INT(rng_range_i32(&rng, 4, 4), 4);
    CHECK_EQ_INT(rng_range_i32(&rng, 10, 2), 10);
}

static void test_float_in_unit_interval_and_roughly_uniform(void)
{
    Rng rng;
    rng_seed(&rng, 2024u, 0u);
    int buckets[10] = {0};
    const int samples = 100000;
    for (int i = 0; i < samples; i++) {
        float value = rng_next_float(&rng);
        CHECK(value >= 0.0f && value < 1.0f);
        buckets[(int)(value * 10.0f)]++;
    }
    for (int i = 0; i < 10; i++) {
        CHECK(buckets[i] > samples / 10 * 9 / 10);
        CHECK(buckets[i] < samples / 10 * 11 / 10);
    }
}

static void test_chance_extremes(void)
{
    Rng rng;
    rng_seed(&rng, 1u, 0u);
    for (int i = 0; i < 1000; i++) {
        CHECK(!rng_chance(&rng, 0.0f));
        CHECK(rng_chance(&rng, 1.0f));
    }
}

int main(void)
{
    RUN_TEST(test_matches_pcg32_reference_output);
    RUN_TEST(test_same_seed_gives_same_sequence);
    RUN_TEST(test_different_streams_differ);
    RUN_TEST(test_range_u32_stays_in_bounds);
    RUN_TEST(test_range_i32_is_inclusive);
    RUN_TEST(test_float_in_unit_interval_and_roughly_uniform);
    RUN_TEST(test_chance_extremes);
    return test_summary();
}
