#include "core/sun.h"

#include "core/game_clock.h"

#include <math.h>

#define PI 3.14159265358979323846
#define DEGREES (PI / 180.0)

float sun_declination(int day_of_year)
{
    /* At latitude φ the sun stays up for 2·h0 / 15° hours, with
     * cos h0 = -tan φ tan δ: the declination that gives the game's day. */
    double half_day = 0.5 * 15.0 * DEGREES * (double)game_day_length(day_of_year);
    double declination = atan(-cos(half_day) / tan((double)SUN_LATITUDE_DEFAULT * DEGREES));
    return (float)(declination / DEGREES);
}

SunPosition sun_position(float latitude, int day_of_year, float hour)
{
    double phi = (double)latitude * DEGREES;
    double delta = (double)sun_declination(day_of_year) * DEGREES;
    double h = 15.0 * DEGREES * ((double)hour - 12.0); /* The hour angle: 0 at noon. */

    /* The direction of the sun in the local frame (east, north, up), worked
     * out as vectors so that nothing breaks at the poles or at the zenith. */
    double east = -cos(delta) * sin(h);
    double north = cos(phi) * sin(delta) - sin(phi) * cos(delta) * cos(h);
    double up = sin(phi) * sin(delta) + cos(phi) * cos(delta) * cos(h);

    SunPosition sun;
    sun.elevation = (float)(asin(fmax(-1.0, fmin(1.0, up))) / DEGREES);
    double azimuth = atan2(east, north) / DEGREES;
    sun.azimuth = (float)(azimuth < 0.0 ? azimuth + 360.0 : azimuth);
    sun.east = (float)east;
    sun.south = (float)-north;
    sun.up = (float)up;
    return sun;
}

SunPosition sun_at_tick(uint64_t tick, float latitude)
{
    float hour = (float)(tick % TICKS_PER_DAY) / (float)TICKS_PER_HOUR;
    return sun_position(latitude, game_day_of_year(tick), hour);
}
