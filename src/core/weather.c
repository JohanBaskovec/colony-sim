#include "core/weather.h"

#include "core/i18n.h"
#include "core/temperature.h"
#include "core/util.h"

#include <math.h>

/* Salts of the rolls (weather_roll()). */
enum {
    ROLL_KEEP,
    ROLL_DRAW,
    ROLL_SPELL, /* Four of them, ROLL_SPELL to ROLL_SPELL + 3. */
    ROLL_STORM_RAIN = ROLL_SPELL + 4,
    ROLL_DOWNPOUR,
    ROLL_DOWNPOUR_RAIN,
    ROLL_DOWNPOUR_START,
    ROLL_DOWNPOUR_HOURS,
};

static const WeatherDef WEATHER_DEFS[WEATHER_TYPE_COUNT] = {
    /* A clear sky lets the day heat up and the night cool down. */
    [WEATHER_CLEAR] = {"clear", N_("weather.clear"), NULL, 1.3f, 0.0f, 1.0f, 0, false, 0.0f, 4.0f,
                       1.0f},
    [WEATHER_CLOUDY] = {"cloudy", N_("weather.cloudy"), NULL, 0.6f, 0.0f, 0.85f, 1, false, 0.0f,
                        2.0f, 0.3f},
    [WEATHER_FOG] = {"fog", N_("weather.fog"), NULL, 0.4f, -1.0f, 0.8f, 1, false, 0.0f, 0.0f, 0.0f},
    [WEATHER_RAIN] = {"rain", N_("weather.rain"), N_("weather.snow"), 0.4f, -2.0f, 0.7f, 2, true,
                      0.22f, 0.0f, 0.0f},
    /* Wind and heavy rain chill the air. */
    [WEATHER_STORM] = {"storm", N_("weather.storm"), N_("weather.snowstorm"), 0.3f, -4.0f, 0.55f, 3,
                       true, 1.2f, 0.0f, 0.0f},
};

/* The sky at each step of cloudiness (fog is drawn, never stepped to). */
static const WeatherType BY_CLOUDINESS[] = {WEATHER_CLEAR, WEATHER_CLOUDY, WEATHER_RAIN,
                                            WEATHER_STORM};

const WeatherDef *weather_def(WeatherType type)
{
    return (unsigned)type < WEATHER_TYPE_COUNT ? &WEATHER_DEFS[type] : NULL;
}

void weather_init(Weather *weather, uint64_t seed, uint64_t tick)
{
    *weather = (Weather){
        .seed = seed,
        .type = WEATHER_CLEAR,
        .period = tick / WEATHER_PERIOD_TICKS,
        .day = (uint32_t)(tick / TICKS_PER_DAY),
    };
}

float weather_roll(uint64_t seed, uint64_t number, uint32_t salt)
{
    uint64_t key[3] = {seed, number, (uint64_t)salt | 0x5745415400000000ULL}; /* "WEAT" */
    uint64_t hash = hash_fnv1a64(HASH_FNV1A64_INIT, key, sizeof(key));
    hash ^= hash >> 33; /* Mixes the last bytes into the high bits. */
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    return (float)(hash >> 40) / (float)(1u << 24);
}

static const int ODDS[SEASON_COUNT][WEATHER_TYPE_COUNT] = WEATHER_SEASON_ODDS;

WeatherType weather_next(WeatherType current, Season season, int hour, float keep_roll,
                         float draw_roll)
{
    if ((unsigned)current >= WEATHER_TYPE_COUNT) {
        current = WEATHER_CLEAR;
    }
    if ((unsigned)season >= SEASON_COUNT) {
        season = SEASON_SPRING;
    }
    bool fog_allowed = hour < WEATHER_FOG_BEFORE_HOUR;
    if (keep_roll < WEATHER_PERSISTENCE && (current != WEATHER_FOG || fog_allowed)) {
        return current;
    }
    int total = 0;
    for (int type = 0; type < WEATHER_TYPE_COUNT; type++) {
        total += ODDS[season][type];
    }
    int pick = (int)(draw_roll * (float)total);
    WeatherType drawn = WEATHER_TYPE_COUNT - 1;
    for (int type = 0; type < WEATHER_TYPE_COUNT; type++) {
        if (pick < ODDS[season][type]) {
            drawn = (WeatherType)type;
            break;
        }
        pick -= ODDS[season][type];
    }
    if (drawn == WEATHER_FOG && !fog_allowed) {
        drawn = WEATHER_CLOUDY;
    }
    /* One step at most along clear → cloudy → rain → storm. */
    int from = WEATHER_DEFS[current].cloudiness;
    int to = WEATHER_DEFS[drawn].cloudiness;
    if (to > from + 1) {
        return BY_CLOUDINESS[from + 1];
    }
    if (to < from - 1) {
        return BY_CLOUDINESS[from - 1];
    }
    return drawn;
}

void weather_season_shares(Season season, float shares[WEATHER_TYPE_COUNT])
{
    if ((unsigned)season >= SEASON_COUNT) {
        season = SEASON_SPRING;
    }
    int total = 0;
    for (int type = 0; type < WEATHER_TYPE_COUNT; type++) {
        total += ODDS[season][type];
        shares[type] = 0.0f;
    }
    /* The chances of each sky, period after period from a clear one, through
     * weather_next() itself (a draw in the middle of each sky's odds); the
     * first days are left out, the sky forgets where it began. */
    float chance[WEATHER_TYPE_COUNT] = {[WEATHER_CLEAR] = 1.0f};
    const int periods = HOURS_PER_DAY / WEATHER_PERIOD_HOURS;
    float counted = 0.0f;
    for (int day = 0; day < WEATHER_SHARE_DAYS; day++) {
        for (int period = 0; period < periods; period++) {
            int hour = period * WEATHER_PERIOD_HOURS;
            float next[WEATHER_TYPE_COUNT] = {0};
            for (int from = 0; from < WEATHER_TYPE_COUNT; from++) {
                bool keeps = from != WEATHER_FOG || hour < WEATHER_FOG_BEFORE_HOUR;
                float keep = keeps ? WEATHER_PERSISTENCE : 0.0f;
                next[from] += chance[from] * keep;
                int below = 0;
                for (int drawn = 0; drawn < WEATHER_TYPE_COUNT; drawn++) {
                    int odds = ODDS[season][drawn];
                    if (odds > 0) {
                        float roll = ((float)below + 0.5f * (float)odds) / (float)total;
                        WeatherType to = weather_next((WeatherType)from, season, hour, 1.0f, roll);
                        next[to] += chance[from] * (1.0f - keep) * (float)odds / (float)total;
                    }
                    below += odds;
                }
            }
            for (int type = 0; type < WEATHER_TYPE_COUNT; type++) {
                chance[type] = next[type];
                if (day >= WEATHER_SHARE_DAYS / 2) {
                    shares[type] += next[type];
                }
            }
            counted += day >= WEATHER_SHARE_DAYS / 2 ? 1.0f : 0.0f;
        }
    }
    for (int type = 0; type < WEATHER_TYPE_COUNT; type++) {
        shares[type] /= counted;
    }
}

/* A roll of about a standard normal distribution (sum of four uniforms). */
static float spell_change(uint64_t seed, uint32_t day)
{
    float sum = 0.0f;
    for (uint32_t i = 0; i < 4; i++) {
        sum += weather_roll(seed, day, ROLL_SPELL + i);
    }
    return (sum - 2.0f) * 1.7320508f; /* Variance 4/12 → 1. */
}

void weather_tick(Weather *weather, uint64_t tick)
{
    uint64_t period = tick / WEATHER_PERIOD_TICKS;
    if (period != weather->period) {
        weather->period = period;
        GameDate date = game_date_from_tick(tick);
        weather->type = (uint8_t)weather_next((WeatherType)weather->type, date.season, date.hour,
                                              weather_roll(weather->seed, period, ROLL_KEEP),
                                              weather_roll(weather->seed, period, ROLL_DRAW));
    }
    uint32_t day = (uint32_t)(tick / TICKS_PER_DAY);
    if (day != weather->day) {
        weather->day = day;
        weather->spell = clampf(WEATHER_SPELL_KEEP * weather->spell +
                                    WEATHER_SPELL_CHANGE * spell_change(weather->seed, day),
                                -WEATHER_SPELL_MAX, WEATHER_SPELL_MAX);
    }
    if (tick % TICKS_PER_HOUR != 0) {
        return;
    }
    Precipitation falling = weather_precipitation(weather, tick);
    bool storm = weather->type == WEATHER_STORM;
    if (falling == PRECIPITATION_SNOW) {
        weather->snow += WEATHER_SNOW_PER_HOUR * (storm ? 2.0f : 1.0f);
    } else {
        float outdoor = weather_outdoor(weather, tick);
        if (outdoor > 0.0f) {
            weather->snow -= WEATHER_MELT_PER_DEGREE_HOUR * outdoor;
        }
        if (falling == PRECIPITATION_RAIN) {
            weather->snow -= WEATHER_RAIN_MELT;
        }
    }
    weather->snow = clampf(weather->snow, 0.0f, 1.0f);
    weather->recent_rain =
        weather->recent_rain * WEATHER_RAIN_MEMORY + (falling == PRECIPITATION_RAIN ? 1.0f : 0.0f);
}

float weather_outdoor(const Weather *weather, uint64_t tick)
{
    const WeatherDef *def = weather_def((WeatherType)weather->type);
    float swing = def != NULL ? def->swing_factor : 1.0f;
    float offset = def != NULL ? def->offset : 0.0f;
    return temperature_season_mean(tick) + weather->spell + weather->debug_offset + offset +
           TEMPERATURE_DAILY_SWING * swing * temperature_daily_swing(tick);
}

Precipitation weather_precipitation(const Weather *weather, uint64_t tick)
{
    const WeatherDef *def = weather_def((WeatherType)weather->type);
    if (def == NULL || !def->falls) {
        return PRECIPITATION_NONE;
    }
    return weather_outdoor(weather, tick) <= WEATHER_SNOW_CELSIUS ? PRECIPITATION_SNOW
                                                                  : PRECIPITATION_RAIN;
}

float weather_wetting(const Weather *weather, uint64_t tick)
{
    switch (weather_precipitation(weather, tick)) {
    case PRECIPITATION_RAIN:
        return weather->type == WEATHER_STORM ? WEATHER_WETTING_STORM : WEATHER_WETTING_RAIN;
    case PRECIPITATION_SNOW: return WEATHER_WETTING_SNOW;
    default: return 0.0f;
    }
}

const char *weather_name_key(const Weather *weather, uint64_t tick)
{
    const WeatherDef *def = weather_def((WeatherType)weather->type);
    if (def == NULL) {
        return WEATHER_DEFS[WEATHER_CLEAR].name_key;
    }
    if (def->snow_key != NULL && weather_precipitation(weather, tick) == PRECIPITATION_SNOW) {
        return def->snow_key;
    }
    if (weather_downpour(weather, tick)) {
        return N_("weather.downpour");
    }
    return def->name_key;
}

float weather_light(const Weather *weather)
{
    const WeatherDef *def = weather_def((WeatherType)weather->type);
    return def != NULL ? def->light : 1.0f;
}

StormRain weather_storm_rain(uint64_t seed, uint64_t period, Season season)
{
    static const float DOWNPOUR_ODDS[SEASON_COUNT] = WEATHER_DOWNPOUR_ODDS;
    StormRain rain = {0};
    rain.mm = WEATHER_STORM_MM_MIN + (WEATHER_STORM_MM_MAX - WEATHER_STORM_MM_MIN) *
                                         weather_roll(seed, period, ROLL_STORM_RAIN);
    float odds = (unsigned)season < SEASON_COUNT ? DOWNPOUR_ODDS[season] : 0.0f;
    if (weather_roll(seed, period, ROLL_DOWNPOUR) < odds) {
        rain.downpour_hours =
            1 + (int)(weather_roll(seed, period, ROLL_DOWNPOUR_HOURS) * WEATHER_DOWNPOUR_HOURS_MAX);
        rain.downpour_start = (int)(weather_roll(seed, period, ROLL_DOWNPOUR_START) *
                                    (float)(WEATHER_PERIOD_HOURS - rain.downpour_hours + 1));
        rain.downpour_mm =
            WEATHER_DOWNPOUR_MM_MIN + (WEATHER_DOWNPOUR_MM_MAX - WEATHER_DOWNPOUR_MM_MIN) *
                                          weather_roll(seed, period, ROLL_DOWNPOUR_RAIN);
    }
    return rain;
}

float weather_storm_rain_at(const StormRain *rain, int hour)
{
    bool pouring = rain->downpour_hours > 0 && hour >= rain->downpour_start &&
                   hour < rain->downpour_start + rain->downpour_hours;
    return pouring ? rain->downpour_mm : rain->mm;
}

/* The storm of the current period. */
static StormRain current_storm(const Weather *weather, uint64_t tick)
{
    uint64_t period = tick / WEATHER_PERIOD_TICKS;
    Season season = game_date_from_tick(period * WEATHER_PERIOD_TICKS).season;
    return weather_storm_rain(weather->seed, period, season);
}

/* The hour of `tick` within its period. */
static int hour_in_period(uint64_t tick)
{
    return (int)(tick % WEATHER_PERIOD_TICKS / TICKS_PER_HOUR);
}

bool weather_downpour(const Weather *weather, uint64_t tick)
{
    if (weather->type != WEATHER_STORM ||
        weather_precipitation(weather, tick) != PRECIPITATION_RAIN) {
        return false;
    }
    StormRain rain = current_storm(weather, tick);
    int hour = hour_in_period(tick);
    return rain.downpour_hours > 0 && hour >= rain.downpour_start &&
           hour < rain.downpour_start + rain.downpour_hours;
}

float weather_rainfall(const Weather *weather, uint64_t tick)
{
    const WeatherDef *def = weather_def((WeatherType)weather->type);
    if (def == NULL || weather_precipitation(weather, tick) != PRECIPITATION_RAIN) {
        return 0.0f;
    }
    if (weather->type == WEATHER_STORM) {
        StormRain rain = current_storm(weather, tick);
        return weather_storm_rain_at(&rain, hour_in_period(tick));
    }
    return def->rain_mm;
}

float weather_snowfall(const Weather *weather, uint64_t tick)
{
    const WeatherDef *def = weather_def((WeatherType)weather->type);
    return def != NULL && weather_precipitation(weather, tick) == PRECIPITATION_SNOW ? def->rain_mm
                                                                                     : 0.0f;
}

float weather_dew_point(const Weather *weather, uint64_t tick)
{
    const WeatherDef *def = weather_def((WeatherType)weather->type);
    float air = weather_outdoor(weather, tick);
    float mean = temperature_season_mean(tick) + weather->spell + weather->debug_offset;
    float dew = mean - (def != NULL ? def->dew_gap : 0.0f);
    return dew < air && def != NULL && def->dew_gap > 0.0f ? dew : air;
}

float weather_outdoor_humidity(const Weather *weather, uint64_t tick)
{
    return air_relative_humidity(weather_dew_point(weather, tick), weather_outdoor(weather, tick));
}

float air_relative_humidity(float dew_point, float celsius)
{
    if (celsius <= dew_point) {
        return 1.0f;
    }
    return expf(17.27f * dew_point / (dew_point + 237.3f) - 17.27f * celsius / (celsius + 237.3f));
}

uint64_t weather_hash(const Weather *weather, uint64_t hash)
{
    hash = hash_fnv1a64(hash, &weather->type, sizeof(weather->type));
    hash = hash_fnv1a64(hash, &weather->period, sizeof(weather->period));
    hash = hash_fnv1a64(hash, &weather->day, sizeof(weather->day));
    float values[3] = {weather->spell, weather->snow, weather->recent_rain};
    hash = hash_fnv1a64(hash, values, sizeof(values));
    if (weather->debug_offset != 0.0f) { /* Apart: earlier hashes stay as they were. */
        hash = hash_fnv1a64(hash, &weather->debug_offset, sizeof(weather->debug_offset));
    }
    return hash;
}

void weather_serialize(Serializer *s, Weather *weather)
{
    ser_u64(s, &weather->seed);
    ser_u8_below(s, &weather->type, WEATHER_TYPE_COUNT);
    ser_u64(s, &weather->period);
    ser_u32(s, &weather->day);
    ser_float(s, &weather->spell);
    ser_float(s, &weather->snow);
    ser_float(s, &weather->recent_rain);
    ser_float(s, &weather->debug_offset);
}
