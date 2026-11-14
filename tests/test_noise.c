#include "core/noise.h"

#include "test_framework.h"

static void test_value_noise_in_unit_range(void)
{
    for (int y = -50; y < 50; y++) {
        for (int x = -50; x < 50; x++) {
            float value = noise_value2d(7u, (float)x * 0.37f, (float)y * 0.41f);
            CHECK(value >= 0.0f && value <= 1.0f);
        }
    }
}

static void test_noise_is_deterministic(void)
{
    CHECK(noise_value2d(3u, 1.5f, 2.25f) == noise_value2d(3u, 1.5f, 2.25f));
    CHECK(noise_fbm2d(3u, 1.5f, 2.25f, 5, 2.0f, 0.5f) ==
          noise_fbm2d(3u, 1.5f, 2.25f, 5, 2.0f, 0.5f));
}

static void test_noise_is_continuous(void)
{
    for (int i = 0; i < 1000; i++) {
        float x = (float)i * 0.013f;
        float a = noise_value2d(11u, x, 4.2f);
        float b = noise_value2d(11u, x + 0.001f, 4.2f);
        CHECK(fabsf(a - b) < 0.01f);
    }
}

static void test_seeds_give_different_noise(void)
{
    int different = 0;
    for (int i = 0; i < 100; i++) {
        float x = (float)i * 0.7f;
        different += noise_value2d(1u, x, x) != noise_value2d(2u, x, x);
    }
    CHECK(different > 90);
}

static void test_fbm_in_unit_range_and_varied(void)
{
    float min = 1.0f, max = 0.0f;
    for (int y = 0; y < 100; y++) {
        for (int x = 0; x < 100; x++) {
            float value = noise_fbm2d(42u, (float)x * 0.05f, (float)y * 0.05f, 5, 2.0f, 0.5f);
            CHECK(value >= 0.0f && value <= 1.0f);
            min = value < min ? value : min;
            max = value > max ? value : max;
        }
    }
    CHECK(max - min > 0.3f);
}

static void test_fbm_with_zero_octaves_is_zero(void)
{
    CHECK(noise_fbm2d(1u, 0.5f, 0.5f, 0, 2.0f, 0.5f) == 0.0f);
}

int main(void)
{
    RUN_TEST(test_value_noise_in_unit_range);
    RUN_TEST(test_noise_is_deterministic);
    RUN_TEST(test_noise_is_continuous);
    RUN_TEST(test_seeds_give_different_noise);
    RUN_TEST(test_fbm_in_unit_range_and_varied);
    RUN_TEST(test_fbm_with_zero_octaves_is_zero);
    return test_summary();
}
