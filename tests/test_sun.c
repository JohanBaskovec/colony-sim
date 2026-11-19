#include "core/game_clock.h"
#include "core/sun.h"

#include "test_framework.h"

#include <math.h>

#define MIDSUMMER SEASON_DAY(SEASON_SUMMER, 8)
#define MIDWINTER SEASON_DAY(SEASON_WINTER, 8)
#define SPRING_EQUINOX SEASON_DAY(SEASON_SPRING, 8)
#define AUTUMN_EQUINOX SEASON_DAY(SEASON_AUTUMN, 8)

/* Length of the shadow a wall of one level (2 m) casts on flat ground. */
static float wall_shadow(SunPosition sun)
{
    return 2.0f * hypotf(sun.east, sun.south) / sun.up;
}

/* About ±23.4° at the solstices, 0 at the equinoxes, rising from midwinter
 * to midsummer and falling back. */
static void test_declination_follows_the_year(void)
{
    CHECK_NEAR(sun_declination(MIDSUMMER), 23.44, 0.1);
    CHECK_NEAR(sun_declination(MIDWINTER), -23.44, 0.1);
    CHECK_NEAR(sun_declination(SPRING_EQUINOX), 0.0, 1e-3);
    CHECK_NEAR(sun_declination(AUTUMN_EQUINOX), 0.0, 1e-3);
    for (int day = SPRING_EQUINOX; day < MIDSUMMER; day++) {
        CHECK(sun_declination(day + 1) > sun_declination(day));
    }
    for (int day = MIDSUMMER; day < MIDWINTER; day++) {
        CHECK(sun_declination(day + 1) < sun_declination(day));
    }
}

/* docs/SHADOWS.md §2.2: the noon sun by season, in the south, and the shadow
 * of a 2 m wall pointing north. */
static void test_noon_by_season(void)
{
    SunPosition summer = sun_position(SUN_LATITUDE_DEFAULT, MIDSUMMER, 12.0f);
    SunPosition equinox = sun_position(SUN_LATITUDE_DEFAULT, SPRING_EQUINOX, 12.0f);
    SunPosition winter = sun_position(SUN_LATITUDE_DEFAULT, MIDWINTER, 12.0f);
    CHECK_NEAR(summer.elevation, 67.8, 0.1);
    CHECK_NEAR(equinox.elevation, 44.4, 0.1);
    CHECK_NEAR(winter.elevation, 21.0, 0.1);
    CHECK_NEAR(wall_shadow(summer), 0.8, 0.05);
    CHECK_NEAR(wall_shadow(equinox), 2.0, 0.05);
    CHECK_NEAR(wall_shadow(winter), 5.2, 0.05);
    SunPosition noons[] = {summer, equinox, winter};
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(noons[i].azimuth, 180.0, 1e-3);
        CHECK_NEAR(noons[i].east, 0.0, 1e-6);
        CHECK(noons[i].south > 0.0f); /* The sun in the south: shadows to the north. */
    }
}

/* East in the morning, west in the afternoon, the same height at the same
 * distance from noon, highest at noon; in summer it rises north of east, in
 * winter south of it. */
static void test_course_of_a_day(void)
{
    for (int day = 0; day < DAYS_PER_YEAR; day += 5) {
        float noon = sun_position(SUN_LATITUDE_DEFAULT, day, 12.0f).elevation;
        for (float before = 0.5f; before < 12.0f; before += 0.5f) {
            SunPosition morning = sun_position(SUN_LATITUDE_DEFAULT, day, 12.0f - before);
            SunPosition afternoon = sun_position(SUN_LATITUDE_DEFAULT, day, 12.0f + before);
            CHECK(morning.east > 0.0f && morning.azimuth > 0.0f && morning.azimuth < 180.0f);
            CHECK(afternoon.east < 0.0f && afternoon.azimuth > 180.0f);
            CHECK_NEAR(morning.elevation, afternoon.elevation, 1e-3);
            CHECK_NEAR(morning.azimuth, 360.0f - afternoon.azimuth, 1e-2);
            CHECK(morning.elevation < noon);
        }
    }
    float summer_rise = game_sunrise(MIDSUMMER), winter_rise = game_sunrise(MIDWINTER);
    CHECK(sun_position(SUN_LATITUDE_DEFAULT, MIDSUMMER, summer_rise).azimuth < 90.0f);
    CHECK(sun_position(SUN_LATITUDE_DEFAULT, MIDWINTER, winter_rise).azimuth > 90.0f);
    /* Long in the morning, shorter until noon. */
    CHECK(wall_shadow(sun_position(SUN_LATITUDE_DEFAULT, SPRING_EQUINOX, 8.0f)) >
          wall_shadow(sun_position(SUN_LATITUDE_DEFAULT, SPRING_EQUINOX, 10.0f)));
}

/* At the default latitude the sun crosses the horizon where the clock puts
 * sunrise and sunset, every day of the year. */
static void test_sunrise_and_sunset_match_the_clock(void)
{
    for (int day = 0; day < DAYS_PER_YEAR; day++) {
        float rise = game_sunrise(day), set = rise + game_day_length(day);
        CHECK_NEAR(sun_position(SUN_LATITUDE_DEFAULT, day, rise).elevation, 0.0, 0.01);
        CHECK_NEAR(sun_position(SUN_LATITUDE_DEFAULT, day, set).elevation, 0.0, 0.01);
        CHECK(!sun_is_up(sun_position(SUN_LATITUDE_DEFAULT, day, rise - 0.25f)));
        CHECK(sun_is_up(sun_position(SUN_LATITUDE_DEFAULT, day, rise + 0.25f)));
        CHECK(sun_is_up(sun_position(SUN_LATITUDE_DEFAULT, day, set - 0.25f)));
        CHECK(!sun_is_up(sun_position(SUN_LATITUDE_DEFAULT, day, set + 0.25f)));
        CHECK(!sun_is_up(sun_position(SUN_LATITUDE_DEFAULT, day, 0.0f)));
    }
}

/* The direction is a unit vector that agrees with the elevation and the
 * azimuth, at any latitude. */
static void test_direction_agrees_with_the_angles(void)
{
    const float latitudes[] = {-90.0f, -60.0f, -10.0f, 0.0f, 23.4f, SUN_LATITUDE_DEFAULT,
                               80.0f,  90.0f};
    for (int l = 0; l < (int)(sizeof latitudes / sizeof latitudes[0]); l++) {
        for (int day = 0; day < DAYS_PER_YEAR; day += 7) {
            for (float hour = 0.0f; hour < 24.0f; hour += 1.25f) {
                SunPosition sun = sun_position(latitudes[l], day, hour);
                float length = sqrtf(sun.east * sun.east + sun.south * sun.south + sun.up * sun.up);
                CHECK_NEAR(length, 1.0, 1e-5);
                CHECK_NEAR(sun.up, sinf(sun.elevation * 3.14159265f / 180.0f), 1e-5);
                CHECK(sun.azimuth >= 0.0f && sun.azimuth < 360.0f);
                float flat = hypotf(sun.east, sun.south);
                if (flat > 1e-3f) {
                    float azimuth = sun.azimuth * 3.14159265f / 180.0f;
                    CHECK_NEAR(sun.east / flat, sinf(azimuth), 1e-3);
                    CHECK_NEAR(-sun.south / flat, cosf(azimuth), 1e-3);
                }
            }
        }
    }
}

/* Other places: the sun overhead at the equator at the equinox, in the north
 * at noon in the south with the seasons the other way round, a sun that does
 * not set in summer nor rise in winter beyond the polar circle, circling at
 * one height at the pole. */
static void test_other_latitudes(void)
{
    SunPosition equator = sun_position(0.0f, SPRING_EQUINOX, 12.0f);
    CHECK_NEAR(equator.elevation, 90.0, 0.01);
    CHECK_NEAR(equator.up, 1.0, 1e-6);

    SunPosition south_summer = sun_position(-SUN_LATITUDE_DEFAULT, MIDWINTER, 12.0f);
    SunPosition south_winter = sun_position(-SUN_LATITUDE_DEFAULT, MIDSUMMER, 12.0f);
    CHECK_NEAR(south_summer.elevation, 67.8, 0.1);
    CHECK_NEAR(south_winter.elevation, 21.0, 0.1);
    CHECK(south_summer.south < 0.0f && fabsf(south_summer.east) < 1e-6f); /* In the north. */

    for (float hour = 0.0f; hour < 24.0f; hour += 0.5f) {
        CHECK(sun_is_up(sun_position(75.0f, MIDSUMMER, hour)));
        CHECK(!sun_is_up(sun_position(75.0f, MIDWINTER, hour)));
        CHECK_NEAR(sun_position(90.0f, MIDSUMMER, hour).elevation, sun_declination(MIDSUMMER),
                   0.01);
    }
    /* The midnight sun stands in the north. */
    CHECK_NEAR(sun_position(75.0f, MIDSUMMER, 0.0f).azimuth, 0.0, 0.01);
}

/* The sun at a tick moves with the minutes, not by whole hours. */
static void test_sun_at_tick(void)
{
    uint64_t day = (uint64_t)(DAYS_PER_YEAR + AUTUMN_EQUINOX) * TICKS_PER_DAY; /* Year 2. */
    uint64_t tick = day + 9 * TICKS_PER_HOUR + TICKS_PER_HOUR / 2;
    SunPosition at_tick = sun_at_tick(tick, SUN_LATITUDE_DEFAULT);
    SunPosition expected = sun_position(SUN_LATITUDE_DEFAULT, AUTUMN_EQUINOX, 9.5f);
    CHECK_NEAR(at_tick.elevation, expected.elevation, 1e-4);
    CHECK_NEAR(at_tick.azimuth, expected.azimuth, 1e-4);
    SunPosition next = sun_at_tick(tick + 1, SUN_LATITUDE_DEFAULT);
    CHECK(next.elevation > at_tick.elevation);
    CHECK(next.elevation - at_tick.elevation < 0.01f);
    CHECK(next.azimuth > at_tick.azimuth);
}

int main(void)
{
    RUN_TEST(test_declination_follows_the_year);
    RUN_TEST(test_noon_by_season);
    RUN_TEST(test_course_of_a_day);
    RUN_TEST(test_sunrise_and_sunset_match_the_clock);
    RUN_TEST(test_direction_agrees_with_the_angles);
    RUN_TEST(test_other_latitudes);
    RUN_TEST(test_sun_at_tick);
    return test_summary();
}
