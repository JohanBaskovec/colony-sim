/* Weather: the sky and its changes, spells, snow, rain. */
#include "core/i18n.h"
#include "core/temperature.h"
#include "core/weather.h"

#include "test_framework.h"

#include <string.h>

#include <math.h>
#include <stdlib.h>

/* First tick of the middle day of a season (year 0). */
static uint64_t mid_season(Season season)
{
    return (uint64_t)SEASON_DAY(season, DAYS_PER_SEASON / 2 + 1) * TICKS_PER_DAY;
}

/* The first tick at or after `from` of an hour whose rain under the sky
 * `type` (seed 77) is within [low, high) mm an hour, with the air warm
 * enough for rain; 0 when none in a year. */
static uint64_t rain_hour(WeatherType type, uint64_t from, float low, float high)
{
    Weather weather = {.seed = 77, .type = (uint8_t)type};
    for (uint64_t hour = from / TICKS_PER_HOUR; hour < from / TICKS_PER_HOUR + 24 * 60; hour++) {
        uint64_t tick = hour * TICKS_PER_HOUR;
        float rain = weather_rain_at(77, type, hour);
        if (rain >= low && rain < high &&
            weather_outdoor(&weather, tick) > WEATHER_SNOW_CELSIUS + 1.0f) {
            return tick;
        }
    }
    return 0;
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
                    WeatherType next =
                        weather_next((WeatherType)current, (Season)season, hour, 0.99f, draw);
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

/* The share of the hours of each sky, worked out, is what the skies drawn
 * period after period come to over the years. */
static void test_season_shares_match_the_draws(void)
{
    int counts[SEASON_COUNT][WEATHER_TYPE_COUNT] = {{0}};
    count_skies(7, 20, counts);
    int periods = 20 * DAYS_PER_SEASON * HOURS_PER_DAY / WEATHER_PERIOD_HOURS;
    for (int season = 0; season < SEASON_COUNT; season++) {
        float shares[WEATHER_TYPE_COUNT];
        weather_season_shares((Season)season, shares);
        float sum = 0.0f;
        for (int type = 0; type < WEATHER_TYPE_COUNT; type++) {
            sum += shares[type];
            CHECK(fabsf(shares[type] - (float)counts[season][type] / (float)periods) < 0.04f);
        }
        CHECK(fabsf(sum - 1.0f) < 0.001f);
    }
    float summer[WEATHER_TYPE_COUNT], autumn[WEATHER_TYPE_COUNT];
    weather_season_shares(SEASON_SUMMER, summer);
    weather_season_shares(SEASON_AUTUMN, autumn);
    CHECK(summer[WEATHER_CLEAR] > autumn[WEATHER_CLEAR]);
    CHECK(summer[WEATHER_FOG] == 0.0f); /* No fog in the summer's odds. */
}

static void test_same_seed_same_weather(void)
{
    Weather a, b, c;
    weather_init(&a, 5, GAME_START_TICK);
    weather_init(&b, 5, GAME_START_TICK);
    weather_init(&c, 6, GAME_START_TICK);
    CHECK_EQ_INT(a.type, WEATHER_CLEAR); /* The settlers arrive on a fine day. */
    int differences = 0;
    for (uint64_t tick = GAME_START_TICK + 1; tick < GAME_START_TICK + 10 * TICKS_PER_DAY; tick++) {
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
    Weather weather = {.type = WEATHER_RAIN,
                       .spell = -8.0f,
                       .period = noon / WEATHER_PERIOD_TICKS,
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
    uint64_t spring = rain_hour(WEATHER_RAIN, mid_season(SEASON_SPRING), 0.5f, 2.0f);
    REQUIRE(spring > 0);
    weather.seed = 77;
    weather.type = WEATHER_RAIN;
    weather.spell = 0.0f;
    weather.period = spring / WEATHER_PERIOD_TICKS;
    weather.day = (uint32_t)(spring / TICKS_PER_DAY);
    weather.recent_rain = 0.0f;
    CHECK_EQ_INT(weather_precipitation(&weather, spring), PRECIPITATION_RAIN);
    CHECK_STR_EQ(weather_name_key(&weather, spring), "weather.rain");
    float rain = weather_rainfall(&weather, spring);
    CHECK_NEAR(weather_wetting(&weather, spring), sqrtf(rain / WEATHER_WETTING_RAIN_MM), 1e-5f);
    float snow = weather.snow;
    weather_tick(&weather, spring);
    CHECK(weather.snow < snow - WEATHER_RAIN_MELT);
    CHECK_NEAR(weather.recent_rain, rain, 1e-5f); /* In mm. */
    weather.type = WEATHER_STORM;
    if (weather_rainfall(&weather, spring) > 0.0f) { /* The wind drives it. */
        CHECK(weather_wetting(&weather, spring) >
              sqrtf(weather_rainfall(&weather, spring) / WEATHER_WETTING_RAIN_MM));
    }
    /* Rain is remembered a while. */
    weather.type = WEATHER_CLEAR;
    CHECK_EQ_INT(weather_precipitation(&weather, spring), PRECIPITATION_NONE);
    CHECK_NEAR(weather_wetting(&weather, spring), 0.0f, 1e-6f);
    uint64_t start = weather.period * WEATHER_PERIOD_TICKS;
    int ticked = 0;
    for (uint64_t tick = spring + TICKS_PER_HOUR; tick < start + WEATHER_PERIOD_TICKS && ticked < 4;
         tick += TICKS_PER_HOUR, ticked++) { /* Within the period: the sky stays clear. */
        weather_tick(&weather, tick);
    }
    CHECK_NEAR(weather.recent_rain, rain * powf(WEATHER_RAIN_MEMORY, (float)ticked), 1e-4f);
}

/* The outdoor air's humidity (docs/CLIMATE.md §4): saturated in the rain and
 * the fog, driest on a clear afternoon, damper at dawn. */
static void test_outdoor_humidity(void)
{
    uint64_t day = mid_season(SEASON_SUMMER);
    uint64_t warmest = day + TEMPERATURE_WARMEST_HOUR * TICKS_PER_HOUR;
    uint64_t coldest = day + TEMPERATURE_COLDEST_HOUR * TICKS_PER_HOUR;
    Weather clear = {.type = WEATHER_CLEAR};
    Weather rain = {.type = WEATHER_RAIN};
    Weather fog = {.type = WEATHER_FOG};
    CHECK_NEAR(weather_outdoor_humidity(&rain, warmest), 1.0, 1e-6);
    CHECK_NEAR(weather_outdoor_humidity(&fog, coldest), 1.0, 1e-6);
    float afternoon = weather_outdoor_humidity(&clear, warmest);
    float dawn = weather_outdoor_humidity(&clear, coldest);
    CHECK(afternoon > 0.3f && afternoon < 0.7f);
    CHECK(dawn > afternoon + 0.2f);
    CHECK(dawn <= 1.0f);
}

static void test_light(void)
{
    Weather weather = {.type = WEATHER_STORM};
    CHECK_NEAR(weather_light(&weather), weather_def(WEATHER_STORM)->light, 1e-6f);
    weather.type = WEATHER_TYPE_COUNT;
    CHECK_NEAR(weather_light(&weather), 1.0f, 1e-6f);
    CHECK_STR_EQ(weather_name_key(&weather, 0), "weather.clear");
}

/* The rain of a wet hour: from the seed and the hour alone, anywhere from
 * WEATHER_RAIN_MM_MIN to WEATHER_RAIN_MM_MAX and any amount in between; the
 * share of wet hours that of the sky; mostly light, heavy now and then, and
 * the storms of summer the heaviest. */
static void test_the_rain_falls_harder_or_lighter(void)
{
    const uint64_t hours = (uint64_t)HOURS_PER_DAY * DAYS_PER_YEAR * 40;
    for (int type = WEATHER_RAIN; type <= WEATHER_STORM; type++) {
        double sum[SEASON_COUNT] = {0};
        uint64_t count[SEASON_COUNT] = {0}, wet[SEASON_COUNT] = {0};
        uint64_t light = 0, heavy = 0, downpour = 0;
        bool between[10] = {false}; /* A rain of 1 to 10 mm an hour, by whole mm. */
        float lowest = 1e9f, highest = 0.0f;
        for (uint64_t hour = 0; hour < hours; hour++) {
            Season season = game_date_from_tick(hour * TICKS_PER_HOUR).season;
            float rain = weather_rain_at(77, (WeatherType)type, hour);
            CHECK(rain == weather_rain_at(77, (WeatherType)type, hour));
            sum[season] += rain;
            count[season]++;
            if (rain <= 0.0f) {
                continue;
            }
            wet[season]++;
            lowest = fminf(lowest, rain);
            highest = fmaxf(highest, rain);
            light += rain < 1.0f;
            heavy += rain >= WEATHER_HEAVY_RAIN_FROM;
            downpour += rain >= WEATHER_DOWNPOUR_FROM;
            if (rain >= 1.0f && rain < 11.0f) {
                between[(int)rain - 1] = true;
            }
        }
        CHECK(lowest >= WEATHER_RAIN_MM_MIN && highest <= WEATHER_RAIN_MM_MAX);
        uint64_t wet_hours = 0;
        for (int season = 0; season < SEASON_COUNT; season++) {
            RainLaw law = weather_rain_law((WeatherType)type, (Season)season);
            CHECK_NEAR((double)wet[season] / (double)count[season], law.wet_share, 0.01);
            wet_hours += wet[season];
        }
        for (int mm = 0; mm < 10; mm++) {
            CHECK(between[mm]);
        }
        CHECK(light > wet_hours / 2); /* Mostly light. */
        CHECK(heavy > 0 && heavy < wet_hours / 10);
        CHECK(downpour > 0 && downpour < heavy);
        if (type == WEATHER_STORM) {
            CHECK(highest > 50.0f); /* A cloudburst, once in a long while. */
            CHECK(sum[SEASON_SUMMER] > sum[SEASON_WINTER] * 1.2);
        }
    }
    CHECK_NEAR(weather_rain_at(77, WEATHER_CLEAR, 5), 0.0, 1e-9);
    CHECK_NEAR(weather_rain_law(WEATHER_FOG, SEASON_SPRING).wet_share, 0.0, 1e-9);
    CHECK(weather_rain_at(77, WEATHER_RAIN, 1000) == weather_rain_at(77, WEATHER_RAIN, 1000));
}

/* What falls in a season stays about what it was when rain fell lightly in
 * every hour of its skies (rain 0.25 mm an hour, storms 1.2): the soil was
 * tuned on it. */
static void test_season_rain_is_kept(void)
{
    const uint64_t days = 40ull * DAYS_PER_YEAR;
    for (int season = 0; season < SEASON_COUNT; season++) {
        float shares[WEATHER_TYPE_COUNT];
        weather_season_shares((Season)season, shares);
        double rain = 0.0, storm = 0.0;
        uint64_t hours = 0;
        for (uint64_t day = 0; day < days; day++) {
            if (game_date_from_tick(day * TICKS_PER_DAY).season != (Season)season) {
                continue;
            }
            for (uint64_t hour = day * HOURS_PER_DAY; hour < (day + 1) * HOURS_PER_DAY; hour++) {
                rain += weather_rain_at(77, WEATHER_RAIN, hour);
                storm += weather_rain_at(77, WEATHER_STORM, hour);
                hours++;
            }
        }
        double before = shares[WEATHER_RAIN] * 0.25 + shares[WEATHER_STORM] * 1.2;
        double now = (shares[WEATHER_RAIN] * rain + shares[WEATHER_STORM] * storm) / (double)hours;
        CHECK(now > 0.85 * before && now < 1.15 * before);
    }
}

/* The weather says how hard it rains: drizzle, rain, heavy rain, a
 * downpour; cloudy in a dry hour under a sky of rain; a storm but in its
 * downpours. */
static void test_the_weather_names_the_rain(void)
{
    static const struct {
        WeatherType type;
        float low, high;
        const char *key;
    } CASES[] = {
        {WEATHER_RAIN, 0.0f, 1e-9f, "weather.cloudy"},
        {WEATHER_RAIN, WEATHER_RAIN_MM_MIN, WEATHER_DRIZZLE_BELOW, "weather.drizzle"},
        {WEATHER_RAIN, WEATHER_DRIZZLE_BELOW, WEATHER_HEAVY_RAIN_FROM, "weather.rain"},
        {WEATHER_RAIN, WEATHER_HEAVY_RAIN_FROM, WEATHER_DOWNPOUR_FROM, "weather.heavy_rain"},
        {WEATHER_STORM, WEATHER_RAIN_MM_MIN, WEATHER_DOWNPOUR_FROM, "weather.storm"},
        {WEATHER_STORM, WEATHER_DOWNPOUR_FROM, WEATHER_RAIN_MM_MAX + 1.0f, "weather.downpour"},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        uint64_t tick =
            rain_hour(CASES[i].type, mid_season(SEASON_SUMMER), CASES[i].low, CASES[i].high);
        REQUIRE(tick > 0);
        Weather weather = {.seed = 77, .type = (uint8_t)CASES[i].type};
        float rain = weather_rainfall(&weather, tick);
        CHECK(rain >= CASES[i].low && rain < CASES[i].high);
        CHECK_STR_EQ(weather_name_key(&weather, tick), CASES[i].key);
        CHECK(weather_downpour(&weather, tick) == (rain >= WEATHER_DOWNPOUR_FROM));
        CHECK_EQ_INT(weather_precipitation(&weather, tick),
                     rain > 0.0f ? PRECIPITATION_RAIN : PRECIPITATION_NONE);
        CHECK(weather_wetting(&weather, tick) <= WEATHER_WETTING_MAX);
    }
    /* The harder it rains, the faster one gets wet. */
    uint64_t drizzle = rain_hour(WEATHER_RAIN, 0, 0.1f, 0.3f);
    uint64_t heavy = rain_hour(WEATHER_RAIN, 0, 5.0f, 15.0f);
    Weather weather = {.seed = 77, .type = WEATHER_RAIN};
    CHECK(weather_wetting(&weather, drizzle) < 1.0f);
    CHECK(weather_wetting(&weather, heavy) > 4.0f);
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_next_moves_one_step_at_most);
    RUN_TEST(test_rolls);
    RUN_TEST(test_seasons_have_their_weather);
    RUN_TEST(test_season_shares_match_the_draws);
    RUN_TEST(test_same_seed_same_weather);
    RUN_TEST(test_sky_changes_the_temperature);
    RUN_TEST(test_spells_drift_within_bounds);
    RUN_TEST(test_snow_falls_and_melts);
    RUN_TEST(test_outdoor_humidity);
    RUN_TEST(test_light);
    RUN_TEST(test_the_rain_falls_harder_or_lighter);
    RUN_TEST(test_season_rain_is_kept);
    RUN_TEST(test_the_weather_names_the_rain);
    return test_summary();
}
