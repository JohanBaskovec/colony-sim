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
};

static const WeatherDef WEATHER_DEFS[WEATHER_TYPE_COUNT] = {
    /* A clear sky lets the day heat up and the night cool down. */
    [WEATHER_CLEAR] = {"clear", N_("weather.clear"), NULL, 1.3f, 0.0f, 1.0f, 0, false},
    [WEATHER_CLOUDY] = {"cloudy", N_("weather.cloudy"), NULL, 0.6f, 0.0f, 0.85f, 1, false},
    [WEATHER_FOG] = {"fog", N_("weather.fog"), NULL, 0.4f, -1.0f, 0.8f, 1, false},
    [WEATHER_RAIN] = {"rain", N_("weather.rain"), N_("weather.snow"), 0.4f, -2.0f, 0.7f, 2, true},
    /* Wind and heavy rain chill the air. */
    [WEATHER_STORM] = {"storm", N_("weather.storm"), N_("weather.snowstorm"), 0.3f, -4.0f, 0.55f, 3,
                       true},
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

WeatherType weather_next(WeatherType current, Season season, int hour, float keep_roll,
                         float draw_roll)
{
    static const int ODDS[SEASON_COUNT][WEATHER_TYPE_COUNT] = WEATHER_SEASON_ODDS;
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
    return temperature_season_mean(tick) + weather->spell + offset +
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
    return def->name_key;
}

float weather_light(const Weather *weather)
{
    const WeatherDef *def = weather_def((WeatherType)weather->type);
    return def != NULL ? def->light : 1.0f;
}

uint64_t weather_hash(const Weather *weather, uint64_t hash)
{
    hash = hash_fnv1a64(hash, &weather->type, sizeof(weather->type));
    hash = hash_fnv1a64(hash, &weather->period, sizeof(weather->period));
    hash = hash_fnv1a64(hash, &weather->day, sizeof(weather->day));
    float values[3] = {weather->spell, weather->snow, weather->recent_rain};
    return hash_fnv1a64(hash, values, sizeof(values));
}
