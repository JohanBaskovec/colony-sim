/* Weather: the sky and its changes, spells, snow, rain. */
#include "core/i18n.h"
#include "core/temperature.h"
#include "core/weather.h"

#include "test_framework.h"

#include <math.h>
#include <stdlib.h>

/* First tick of the middle day of a season (year 0). */
static uint64_t mid_season(Season season)
{
    return (uint64_t)SEASON_DAY(season, DAYS_PER_SEASON / 2 + 1) * TICKS_PER_DAY;
}

static void test_definitions(void)
{
    for (int type = 0; type < WEATHER_TYPE_COUNT; type++) {
        const WeatherDef *def = weather_def((WeatherType)type);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL);
        CHECK(i18n_is_valid_key(def->name_key));
        CHECK(def->snow_key == NULL || i18n_is_valid_key(def->snow_key));
        CHECK(def->falls == (def->snow_key != NULL));
        CHECK(def->light > 0.0f && def->light <= 1.0f);
        CHECK(def->cloudiness >= 0 && def->cloudiness <= 3);
    }
    CHECK(weather_def(WEATHER_TYPE_COUNT) == NULL);
    CHECK(weather_def(WEATHER_RAIN)->falls && weather_def(WEATHER_STORM)->falls);
    CHECK(!weather_def(WEATHER_CLEAR)->falls && !weather_def(WEATHER_FOG)->falls);
    /* The darker the sky, the less light. */
    CHECK(weather_def(WEATHER_STORM)->light < weather_def(WEATHER_RAIN)->light);
    CHECK(weather_def(WEATHER_RAIN)->light < weather_def(WEATHER_CLEAR)->light);
}

static void test_next_moves_one_step_at_most(void)
{
    for (int current = 0; current < WEATHER_TYPE_COUNT; current++) {
        for (int season = 0; season < SEASON_COUNT; season++) {
            for (int hour = 0; hour < HOURS_PER_DAY; hour += WEATHER_PERIOD_HOURS) {
                for (int roll = 0; roll < 100; roll++) {
                    float draw = (float)roll / 100.0f;
                    WeatherType next = weather_next((WeatherType)current, (Season)season, hour,
                                                    0.99f, draw);
                    int from = weather_def((WeatherType)current)->cloudiness;
                    int to = weather_def(next)->cloudiness;
                    CHECK(abs(to - from) <= 1);
                    if (hour >= WEATHER_FOG_BEFORE_HOUR) {
                        CHECK(next != WEATHER_FOG); /* Fog lifts by noon. */
                    }
                }
            }
        }
    }
    /* Kept with the persistence roll, fog only in the morning. */
    CHECK_EQ_INT(weather_next(WEATHER_RAIN, SEASON_SUMMER, 12, 0.1f, 0.0f), WEATHER_RAIN);
    CHECK_EQ_INT(weather_next(WEATHER_FOG, SEASON_AUTUMN, 6, 0.1f, 0.0f), WEATHER_FOG);
    CHECK(weather_next(WEATHER_FOG, SEASON_AUTUMN, 12, 0.1f, 0.0f) != WEATHER_FOG);
    /* A clear sky never turns into a storm at once. */
    CHECK_EQ_INT(weather_next(WEATHER_CLEAR, SEASON_SUMMER, 12, 0.99f, 0.999f), WEATHER_CLOUDY);
    CHECK_EQ_INT(weather_next(WEATHER_STORM, SEASON_SUMMER, 12, 0.99f, 0.0f), WEATHER_RAIN);
    /* Out of range: treated as clear in spring. */
    CHECK(weather_next(WEATHER_TYPE_COUNT, SEASON_COUNT, 0, 0.1f, 0.0f) == WEATHER_CLEAR);
}

static void test_rolls(void)
{
    float sum = 0.0f;
    for (uint64_t i = 0; i < 1000; i++) {
        float roll = weather_roll(42, i, 0);
        CHECK(roll >= 0.0f && roll < 1.0f);
        sum += roll;
    }
    CHECK_NEAR(sum / 1000.0f, 0.5f, 0.05f);
    CHECK(weather_roll(42, 7, 0) != weather_roll(43, 7, 0));
    CHECK(weather_roll(42, 7, 0) != weather_roll(42, 7, 1));
    CHECK_NEAR(weather_roll(42, 7, 0), weather_roll(42, 7, 0), 0.0f);
}

/* Counts of each sky per season over `years` years of weather. */
static void count_skies(uint64_t seed, int years, int counts[SEASON_COUNT][WEATHER_TYPE_COUNT])
{
    Weather weather;
    weather_init(&weather, seed, 0);
    for (uint64_t tick = 1; tick <= (uint64_t)years * TICKS_PER_YEAR; tick++) {
        weather_tick(&weather, tick);
        if (tick % WEATHER_PERIOD_TICKS == 0) {
            counts[game_date_from_tick(tick).season][weather.type]++;
        }
    }
}

static void test_seasons_have_their_weather(void)
{
    int counts[SEASON_COUNT][WEATHER_TYPE_COUNT] = {{0}};
    count_skies(11, 20, counts);
    int periods = 20 * DAYS_PER_SEASON * HOURS_PER_DAY / WEATHER_PERIOD_HOURS;
    for (int season = 0; season < SEASON_COUNT; season++) {
        int total = 0;
        for (int type = 0; type < WEATHER_TYPE_COUNT; type++) {
            total += counts[season][type];
        }
        CHECK_EQ_INT(total, periods);
        CHECK(counts[season][WEATHER_RAIN] + counts[season][WEATHER_STORM] > periods / 10);
    }
    /* Summer is the sunniest, autumn the foggiest. */
    CHECK(counts[SEASON_SUMMER][WEATHER_CLEAR] > counts[SEASON_AUTUMN][WEATHER_CLEAR]);
    CHECK(counts[SEASON_AUTUMN][WEATHER_FOG] > counts[SEASON_SUMMER][WEATHER_FOG]);
    CHECK(counts[SEASON_SUMMER][WEATHER_STORM] > counts[SEASON_WINTER][WEATHER_STORM]);
}

static void test_same_seed_same_weather(void)
{
    Weather a, b, c;
    weather_init(&a, 5, GAME_START_TICK);
    weather_init(&b, 5, GAME_START_TICK);
    weather_init(&c, 6, GAME_START_TICK);
    CHECK_EQ_INT(a.type, WEATHER_CLEAR); /* The settlers arrive on a fine day. */
    int differences = 0;
    for (uint64_t tick = GAME_START_TICK + 1; tick < GAME_START_TICK + 10 * TICKS_PER_DAY;
         tick++) {
        weather_tick(&a, tick);
        weather_tick(&b, tick);
        weather_tick(&c, tick);
        differences += a.type != c.type;
    }
    CHECK_EQ_UINT(weather_hash(&a, 1), weather_hash(&b, 1));
    CHECK(differences > 0);
    CHECK(weather_hash(&a, 1) != weather_hash(&c, 1));
}

static void test_sky_changes_the_temperature(void)
{
    uint64_t day = mid_season(SEASON_SPRING);
    uint64_t warmest = day + TEMPERATURE_WARMEST_HOUR * TICKS_PER_HOUR;
    uint64_t coldest = day + TEMPERATURE_COLDEST_HOUR * TICKS_PER_HOUR;
    Weather clear = {.type = WEATHER_CLEAR};
    Weather cloudy = {.type = WEATHER_CLOUDY};
    Weather rain = {.type = WEATHER_RAIN};
    /* Clear: warmer days and colder nights than the plain climate. */
    CHECK(weather_outdoor(&clear, warmest) > temperature_outdoor(warmest));
    CHECK(weather_outdoor(&clear, coldest) < temperature_outdoor(coldest));
    /* Clouds even the day out. */
    CHECK(weather_outdoor(&cloudy, warmest) < weather_outdoor(&clear, warmest));
    CHECK(weather_outdoor(&cloudy, coldest) > weather_outdoor(&clear, coldest));
    CHECK(weather_outdoor(&rain, warmest) < weather_outdoor(&cloudy, warmest));
    /* A spell adds to everything. */
    Weather spell = {.type = WEATHER_CLOUDY, .spell = -5.0f};
    CHECK_NEAR(weather_outdoor(&spell, warmest), weather_outdoor(&cloudy, warmest) - 5.0f, 1e-4f);
}

static void test_spells_drift_within_bounds(void)
{
    Weather weather;
    weather_init(&weather, 3, 0);
    float sum = 0.0f, lowest = 0.0f, highest = 0.0f;
    int days = 0;
    for (uint64_t tick = 1; tick <= 20 * TICKS_PER_YEAR; tick += TICKS_PER_DAY / 4) {
        weather_tick(&weather, tick);
        if (tick % TICKS_PER_DAY < TICKS_PER_DAY / 4) {
            CHECK(fabsf(weather.spell) <= WEATHER_SPELL_MAX);
            sum += weather.spell;
            lowest = fminf(lowest, weather.spell);
            highest = fmaxf(highest, weather.spell);
            days++;
        }
    }
    CHECK(days > 1000);
    CHECK_NEAR(sum / (float)days, 0.0f, 1.0f);
    CHECK(lowest < -4.0f); /* Cold snaps... */
    CHECK(highest > 4.0f); /* ...and heat waves. */
}

static void test_snow_falls_and_melts(void)
{
    uint64_t noon = mid_season(SEASON_WINTER) + 12 * TICKS_PER_HOUR;
    Weather weather = {.type = WEATHER_RAIN, .spell = -8.0f, .period = noon / WEATHER_PERIOD_TICKS,
                       .day = (uint32_t)(noon / TICKS_PER_DAY)};
    REQUIRE(weather_outdoor(&weather, noon) <= WEATHER_SNOW_CELSIUS);
    CHECK_EQ_INT(weather_precipitation(&weather, noon), PRECIPITATION_SNOW);
    CHECK_STR_EQ(weather_name_key(&weather, noon), "weather.snow");
    CHECK_NEAR(weather_wetting(&weather, noon), WEATHER_WETTING_SNOW, 1e-6f);
    for (int hour = 1; hour <= 3; hour++) {
        weather_tick(&weather, noon + (uint64_t)hour * TICKS_PER_HOUR);
    }
    CHECK_NEAR(weather.snow, 3.0f * WEATHER_SNOW_PER_HOUR, 1e-4f);
    CHECK_NEAR(weather.recent_rain, 0.0f, 1e-6f); /* Snow is not rain. */
    weather.type = WEATHER_STORM;
    CHECK_STR_EQ(weather_name_key(&weather, noon), "weather.snowstorm");

    /* A mild spell: rain melts it. */
    uint64_t spring = mid_season(SEASON_SPRING) + 12 * TICKS_PER_HOUR;
    weather.type = WEATHER_RAIN;
    weather.spell = 0.0f;
    weather.period = spring / WEATHER_PERIOD_TICKS;
    weather.day = (uint32_t)(spring / TICKS_PER_DAY);
    CHECK_EQ_INT(weather_precipitation(&weather, spring), PRECIPITATION_RAIN);
    CHECK_STR_EQ(weather_name_key(&weather, spring), "weather.rain");
    CHECK_NEAR(weather_wetting(&weather, spring), WEATHER_WETTING_RAIN, 1e-6f);
    float snow = weather.snow;
    weather_tick(&weather, spring + TICKS_PER_HOUR);
    CHECK(weather.snow < snow - WEATHER_RAIN_MELT);
    CHECK_NEAR(weather.recent_rain, 1.0f, 1e-6f);
    weather.type = WEATHER_STORM;
    CHECK_NEAR(weather_wetting(&weather, spring), WEATHER_WETTING_STORM, 1e-6f);
    /* Rain is remembered a while. */
    weather.type = WEATHER_CLEAR;
    CHECK_EQ_INT(weather_precipitation(&weather, spring), PRECIPITATION_NONE);
    CHECK_NEAR(weather_wetting(&weather, spring), 0.0f, 1e-6f);
    for (int hour = 2; hour <= 5; hour++) { /* Within the period: the sky stays clear. */
        weather_tick(&weather, spring + (uint64_t)hour * TICKS_PER_HOUR);
    }
    CHECK_NEAR(weather.recent_rain, powf(WEATHER_RAIN_MEMORY, 4.0f), 1e-4f);
}

static void test_light(void)
{
    Weather weather = {.type = WEATHER_STORM};
    CHECK_NEAR(weather_light(&weather), weather_def(WEATHER_STORM)->light, 1e-6f);
    weather.type = WEATHER_TYPE_COUNT;
    CHECK_NEAR(weather_light(&weather), 1.0f, 1e-6f);
    CHECK_STR_EQ(weather_name_key(&weather, 0), "weather.clear");
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_next_moves_one_step_at_most);
    RUN_TEST(test_rolls);
    RUN_TEST(test_seasons_have_their_weather);
    RUN_TEST(test_same_seed_same_weather);
    RUN_TEST(test_sky_changes_the_temperature);
    RUN_TEST(test_spells_drift_within_bounds);
    RUN_TEST(test_snow_falls_and_melts);
    RUN_TEST(test_light);
    return test_summary();
}
