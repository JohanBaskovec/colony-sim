#include "core/game_clock.h"
#include "core/i18n.h"

#include "test_framework.h"

/* At the end of summer: the brambles bear fruit, autumn's food follows. */
static void test_init_starts_in_the_morning_at_normal_speed(void)
{
    GameClock clock;
    game_clock_init(&clock);
    CHECK_EQ_INT(clock.speed, GAME_SPEED_NORMAL);
    GameDate date = game_date_from_tick(clock.tick);
    CHECK_EQ_INT(date.year, 1);
    CHECK_EQ_INT(date.season, SEASON_SUMMER);
    CHECK_EQ_INT(date.day, 11);
    CHECK_EQ_INT(game_day_of_year(clock.tick), GAME_START_DAY);
    CHECK_EQ_INT(date.hour, GAME_START_HOUR);
    CHECK_EQ_INT(date.minute, 0);
}

static void test_day_of_year(void)
{
    CHECK_EQ_INT(game_day_of_year(0), 0);
    CHECK_EQ_INT(game_day_of_year(TICKS_PER_DAY - 1), 0);
    CHECK_EQ_INT(game_day_of_year(TICKS_PER_DAY), 1);
    CHECK_EQ_INT(game_day_of_year(TICKS_PER_YEAR - 1), DAYS_PER_YEAR - 1);
    CHECK_EQ_INT(game_day_of_year(TICKS_PER_YEAR + 3 * TICKS_PER_DAY), 3);
    CHECK_EQ_INT(SEASON_DAY(SEASON_SPRING, 1), 0);
    CHECK_EQ_INT(SEASON_DAY(SEASON_AUTUMN, 1), 2 * DAYS_PER_SEASON);
    CHECK_EQ_INT(SEASON_DAY(SEASON_WINTER, DAYS_PER_SEASON), DAYS_PER_YEAR - 1);
}

static void test_speed_multipliers(void)
{
    CHECK_EQ_INT(game_speed_multiplier(GAME_SPEED_PAUSED), 0);
    CHECK_EQ_INT(game_speed_multiplier(GAME_SPEED_NORMAL), 1);
    CHECK(game_speed_multiplier(GAME_SPEED_FAST) > 1);
    CHECK(game_speed_multiplier(GAME_SPEED_FASTEST) > game_speed_multiplier(GAME_SPEED_FAST));
}

static void test_consume_produces_ticks_at_fixed_rate(void)
{
    GameClock clock;
    game_clock_init(&clock);
    int total = 0;
    /* 60 frames of 1/60 s at normal speed = 1 second = TICKS_PER_SECOND ticks. */
    for (int i = 0; i < 60; i++) {
        total += game_clock_consume(&clock, 1.0 / 60.0);
    }
    CHECK(total >= TICKS_PER_SECOND - 1 && total <= TICKS_PER_SECOND);

    /* Fractions accumulate: two half ticks make one. */
    game_clock_init(&clock);
    CHECK_EQ_INT(game_clock_consume(&clock, 0.5 / TICKS_PER_SECOND), 0);
    CHECK_EQ_INT(game_clock_consume(&clock, 0.5 / TICKS_PER_SECOND + 1e-9), 1);
}

static void test_consume_scales_with_speed(void)
{
    GameClock clock;
    game_clock_init(&clock);
    game_clock_set_speed(&clock, GAME_SPEED_FAST);
    int due = game_clock_consume(&clock, 0.1);
    CHECK_EQ_INT(due, TICKS_PER_SECOND / 10 * game_speed_multiplier(GAME_SPEED_FAST));
}

static void test_consume_is_capped(void)
{
    GameClock clock;
    game_clock_init(&clock);
    game_clock_set_speed(&clock, GAME_SPEED_FASTEST);
    CHECK_EQ_INT(game_clock_consume(&clock, 10.0), GAME_CLOCK_MAX_TICKS_PER_UPDATE);
    CHECK_NEAR(clock.pending_ticks, 0.0, 1e-9);
}

static void test_pause_and_resume(void)
{
    GameClock clock;
    game_clock_init(&clock);
    game_clock_set_speed(&clock, GAME_SPEED_FASTEST);
    game_clock_toggle_pause(&clock);
    CHECK_EQ_INT(clock.speed, GAME_SPEED_PAUSED);
    CHECK_EQ_INT(game_clock_consume(&clock, 1.0), 0);
    game_clock_toggle_pause(&clock);
    CHECK_EQ_INT(clock.speed, GAME_SPEED_FASTEST);

    game_clock_set_speed(&clock, GAME_SPEED_PAUSED);
    game_clock_toggle_pause(&clock);
    CHECK_EQ_INT(clock.speed, GAME_SPEED_FASTEST);

    game_clock_set_speed(&clock, GAME_SPEED_COUNT); /* Invalid: ignored. */
    CHECK_EQ_INT(clock.speed, GAME_SPEED_FASTEST);
}

static void test_date_rollover(void)
{
    GameDate date = game_date_from_tick(0);
    CHECK_EQ_INT(date.year, 1);
    CHECK_EQ_INT(date.day, 1);
    CHECK_EQ_INT(date.hour, 0);

    date = game_date_from_tick(TICKS_PER_HOUR / 2);
    CHECK_EQ_INT(date.minute, 30);

    date = game_date_from_tick((uint64_t)TICKS_PER_DAY - 1);
    CHECK_EQ_INT(date.day, 1);
    CHECK_EQ_INT(date.hour, 23);
    CHECK_EQ_INT(date.minute, 59);

    date = game_date_from_tick(TICKS_PER_DAY);
    CHECK_EQ_INT(date.day, 2);
    CHECK_EQ_INT(date.hour, 0);

    date = game_date_from_tick((uint64_t)TICKS_PER_DAY * DAYS_PER_SEASON);
    CHECK_EQ_INT(date.season, SEASON_SUMMER);
    CHECK_EQ_INT(date.day, 1);

    date = game_date_from_tick(TICKS_PER_YEAR - 1);
    CHECK_EQ_INT(date.year, 1);
    CHECK_EQ_INT(date.season, SEASON_WINTER);
    CHECK_EQ_INT(date.day, DAYS_PER_SEASON);

    date = game_date_from_tick(TICKS_PER_YEAR * 2);
    CHECK_EQ_INT(date.year, 3);
    CHECK_EQ_INT(date.season, SEASON_SPRING);

    /* The hour alone agrees with the full date. */
    for (uint64_t tick = 0; tick < (uint64_t)TICKS_PER_DAY * 3; tick += 617) {
        CHECK_EQ_INT(game_hour_from_tick(tick), game_date_from_tick(tick).hour);
    }
    CHECK_EQ_INT(game_hour_from_tick(TICKS_PER_YEAR * 5 + 13 * TICKS_PER_HOUR), 13);
}

static void test_daylight_cycle(void)
{
    const uint64_t hour = TICKS_PER_HOUR;
    CHECK_NEAR(game_daylight(0), GAME_DAYLIGHT_MIN, 1e-6);
    CHECK_NEAR(game_daylight(12 * hour), 1.0, 1e-6);
    CHECK_NEAR(game_daylight(23 * hour), GAME_DAYLIGHT_MIN, 1e-6);

    float dawn = game_daylight(6 * hour);
    CHECK(dawn > GAME_DAYLIGHT_MIN && dawn < 1.0f);
    float dusk = game_daylight(19 * hour);
    CHECK(dusk > GAME_DAYLIGHT_MIN && dusk < 1.0f);

    /* Light increases monotonically through dawn. */
    float previous = 0.0f;
    for (uint64_t t = 4 * hour; t <= 8 * hour; t += hour / 10) {
        float light = game_daylight(t);
        CHECK(light >= previous);
        previous = light;
    }
    /* Next day repeats. */
    CHECK_NEAR(game_daylight(TICKS_PER_DAY + 12 * hour), 1.0, 1e-6);
}

static void test_season_keys(void)
{
    for (int s = 0; s < SEASON_COUNT; s++) {
        CHECK(i18n_is_valid_key(season_name_key((Season)s)));
    }
    CHECK(strcmp(season_name_key(SEASON_SPRING), season_name_key(SEASON_WINTER)) != 0);
}

int main(void)
{
    RUN_TEST(test_init_starts_in_the_morning_at_normal_speed);
    RUN_TEST(test_day_of_year);
    RUN_TEST(test_speed_multipliers);
    RUN_TEST(test_consume_produces_ticks_at_fixed_rate);
    RUN_TEST(test_consume_scales_with_speed);
    RUN_TEST(test_consume_is_capped);
    RUN_TEST(test_pause_and_resume);
    RUN_TEST(test_date_rollover);
    RUN_TEST(test_daylight_cycle);
    RUN_TEST(test_season_keys);
    return test_summary();
}
