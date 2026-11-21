/*
 * Weather: the sky over the whole map (clear, cloudy, fog, rain, storm),
 * warm and cold spells, and the snow lying on open ground.
 *
 * The sky changes every WEATHER_PERIOD_HOURS: it stays as it is with
 * WEATHER_PERSISTENCE, else a new one is drawn from the season's odds
 * (WEATHER_SEASON_ODDS), one step at most along clear → cloudy → rain →
 * storm, so that a sunny sky never turns into a storm at once; fog only
 * forms at night and in the morning. Rain and storms fall as snow when the
 * air is at or below WEATHER_SNOW_CELSIUS. Each day a warm or cold spell
 * drifts (an anomaly added to the season's temperature, WEATHER_SPELL_*),
 * so that cold snaps and heat waves come and go over a few days.
 *
 * The sky changes the outdoor temperature (core/temperature.h): a clear sky
 * makes the days warmer and the nights colder, clouds even them out, rain
 * and storms cool the air. Every draw comes from a hash of the game's seed
 * and the period or day (weather_roll()), not from the game's random stream:
 * the weather does not depend on what the settlers do.
 *
 * What the weather does elsewhere: rain wets settlers out in it (core/
 * colonist.h), puts out fires under the open sky (core/hearth.h), stops
 * fresh grass from drying and makes food rot faster outdoors (core/item.h);
 * mushrooms come after rain (core/game.h).
 */
#ifndef COLONY_CORE_WEATHER_H
#define COLONY_CORE_WEATHER_H

#include "core/serialize.h"

#include "core/game_clock.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum WeatherType {
    WEATHER_CLEAR,
    WEATHER_CLOUDY,
    WEATHER_FOG,
    WEATHER_RAIN,
    WEATHER_STORM, /* Heavy rain and wind. */
    WEATHER_TYPE_COUNT
} WeatherType;

/* What falls from the sky. */
typedef enum Precipitation {
    PRECIPITATION_NONE,
    PRECIPITATION_RAIN,
    PRECIPITATION_SNOW,
} Precipitation;

#define WEATHER_PERIOD_HOURS 6
#define WEATHER_PERIOD_TICKS (WEATHER_PERIOD_HOURS * TICKS_PER_HOUR)
/* Chance that the sky stays as it is for the next period. */
#define WEATHER_PERSISTENCE 0.4f
/* Odds of each sky (clear, cloudy, fog, rain, storm) per season (spring,
 * summer, autumn, winter), in percent (tuning). */
#define WEATHER_SEASON_ODDS                                                                        \
    {                                                                                              \
        {35, 30, 5, 25, 5}, {55, 20, 0, 13, 12}, {25, 30, 15, 25, 5},                              \
        {                                                                                          \
            25, 35, 10, 25, 5                                                                      \
        }                                                                                          \
    }
/* Fog forms only in periods starting before this hour. */
#define WEATHER_FOG_BEFORE_HOUR 12
/* Rain and storms fall as snow at or below this outdoor temperature. */
#define WEATHER_SNOW_CELSIUS 1.0f
/* Warm and cold spells: each day the anomaly keeps WEATHER_SPELL_KEEP of
 * itself and gets a random change of standard deviation
 * WEATHER_SPELL_CHANGE (about ±3 °C, now and then ±6), within
 * ±WEATHER_SPELL_MAX. */
#define WEATHER_SPELL_KEEP 0.75f
#define WEATHER_SPELL_CHANGE 2.0f
#define WEATHER_SPELL_MAX 8.0f
/* Snow cover (0..1) on open ground: grows by WEATHER_SNOW_PER_HOUR while it
 * snows (twice that in a storm), melts by WEATHER_MELT_PER_DEGREE_HOUR per
 * degree above 0 °C an hour, WEATHER_RAIN_MELT more in the rain. */
#define WEATHER_SNOW_PER_HOUR 0.08f
#define WEATHER_MELT_PER_DEGREE_HOUR 0.015f
#define WEATHER_RAIN_MELT 0.05f
/* Recent rain: hours of rain, keeping WEATHER_RAIN_MEMORY of itself each
 * hour (about half after 17 hours). */
#define WEATHER_RAIN_MEMORY 0.96f
/* How fast what is out in the rain gets wet, per hour (1: soaked in an
 * hour), by what falls. */
#define WEATHER_WETTING_RAIN 1.0f
#define WEATHER_WETTING_STORM 2.0f
#define WEATHER_WETTING_SNOW 0.25f

typedef struct WeatherDef {
    const char *id;       /* Stable identifier (saves), never shown. */
    const char *name_key; /* Its name; snow_key when it snows (NULL: the same). */
    const char *snow_key;
    /* Daily swing of the outdoor temperature (×), and degrees added. */
    float swing_factor;
    float offset;
    float light; /* Daylight let through (1: all). */
    /* Step along clear (0) → cloudy, fog (1) → rain (2) → storm (3). */
    int cloudiness;
    bool falls; /* Rain or snow falls. */
    /* Water that falls, in mm an hour (docs/CLIMATE.md §3.1); how far the
     * dew point stays below the day's mean temperature (0: the air is
     * saturated, §4); how much of the sky is clear for the ground to cool
     * at night (§5.1). */
    float rain_mm;
    float dew_gap;
    float clearness;
} WeatherDef;

typedef struct Weather {
    uint64_t seed;
    uint8_t type;      /* WeatherType of the current period. */
    uint64_t period;   /* The period it is for (tick / WEATHER_PERIOD_TICKS). */
    uint32_t day;      /* The day the spell was last drawn for. */
    float spell;       /* Degrees added to the season's temperature today. */
    float snow;        /* Snow cover on open ground, 0..1. */
    float recent_rain; /* Hours of rain lately (see WEATHER_RAIN_MEMORY). */
    /* Debug: degrees added to the air all along (the debug menu's + and -);
     * 0 in a game as played. */
    float debug_offset;
} Weather;

/* NULL when out of range. */
const WeatherDef *weather_def(WeatherType type);

/* A clear sky with no spell nor snow at `tick`, the weather of `seed`. */
void weather_init(Weather *weather, uint64_t seed, uint64_t tick);

/* The sky of the next period after `current`, in `season`, for a period
 * starting at `hour`, from two rolls in [0, 1). */
WeatherType weather_next(WeatherType current, Season season, int hour, float keep_roll,
                         float draw_roll);

/* The share of the hours each sky holds over a long run of `season`, as
 * weather_next() goes (its odds, its persistence, fog only in the morning,
 * one step at most along the clouds): what a plant meets on most days.
 * Worked out over WEATHER_SHARE_DAYS days, the first half left out. */
#define WEATHER_SHARE_DAYS 20
void weather_season_shares(Season season, float shares[WEATHER_TYPE_COUNT]);

/* Roll in [0, 1) from the seed, a number (period, day) and a salt. */
float weather_roll(uint64_t seed, uint64_t number, uint32_t salt);

/* One tick: a new sky at the start of each period, a new spell at the start
 * of each day, snow and recent rain every hour. */
void weather_tick(Weather *weather, uint64_t tick);

/* Outdoor air temperature at `tick` with this weather. */
float weather_outdoor(const Weather *weather, uint64_t tick);

/* What falls at `tick`: rain, or snow when the air is cold enough. */
Precipitation weather_precipitation(const Weather *weather, uint64_t tick);

/* How fast what is out under the open sky gets wet at `tick`, per hour
 * (0 when nothing falls). */
float weather_wetting(const Weather *weather, uint64_t tick);

/* Translation key of the sky now (snow and snowstorm when it snows, a
 * downpour in a storm's heaviest hours). */
const char *weather_name_key(const Weather *weather, uint64_t tick);

/* Daylight let through by the sky (1: all of it). */
float weather_light(const Weather *weather);

/* The rain of a storm (docs/WATER.md §4.3), drawn per period: `mm` an hour,
 * from WEATHER_STORM_MM_MIN to WEATHER_STORM_MM_MAX; now and then (the
 * season's WEATHER_DOWNPOUR_ODDS of storms, most in summer) a downpour of
 * `downpour_mm` an hour (WEATHER_DOWNPOUR_MM_MIN to _MAX) for
 * `downpour_hours` (1 to WEATHER_DOWNPOUR_HOURS_MAX, 0: none) from
 * `downpour_start`, hours in the period. A downpour fills the hollows: far
 * more than any ground takes in. The storms are lighter than they were
 * (1.2 mm an hour) and the rains too (0.25 mm), so that what falls in a
 * season stays about what it was (tuning). Snowstorms snow as before. */
#define WEATHER_STORM_MM_MIN 0.5f
#define WEATHER_STORM_MM_MAX 1.1f
#define WEATHER_DOWNPOUR_MM_MIN 10.0f
#define WEATHER_DOWNPOUR_MM_MAX 20.0f
#define WEATHER_DOWNPOUR_HOURS_MAX 3
/* Spring, summer, autumn, winter. */
#define WEATHER_DOWNPOUR_ODDS                                                                      \
    {                                                                                              \
        0.06f, 0.10f, 0.06f, 0.02f                                                                 \
    }

typedef struct StormRain {
    float mm;
    float downpour_mm;
    int downpour_start;
    int downpour_hours;
} StormRain;

/* The storm of `period` (tick / WEATHER_PERIOD_TICKS) in `season`, from the
 * seed alone (weather_roll()), and its rain in an hour of the period. */
StormRain weather_storm_rain(uint64_t seed, uint64_t period, Season season);
float weather_storm_rain_at(const StormRain *rain, int hour);

/* Whether a downpour falls at `tick` (a storm's heaviest hours). */
bool weather_downpour(const Weather *weather, uint64_t tick);

/* Water falling at `tick` as rain and as snow, in mm (of water) an hour
 * (docs/CLIMATE.md §3.1). */
float weather_rainfall(const Weather *weather, uint64_t tick);
float weather_snowfall(const Weather *weather, uint64_t tick);

/* The air's dew point at `tick` (docs/CLIMATE.md §4): the outdoor air's
 * temperature when it is saturated (rain, storm, fog), else the day's mean
 * temperature less the sky's `dew_gap`, never above the air's. */
float weather_dew_point(const Weather *weather, uint64_t tick);

/* Relative humidity (0 .. 1) of air at `celsius` holding the vapour of air
 * saturated at `dew_point` (the Magnus formula); 1 at or below the dew point. */
float air_relative_humidity(float dew_point, float celsius);

/* The outdoor air's relative humidity at `tick`, 0 .. 1: saturated in rain,
 * storm and fog, driest in the afternoon of a clear day. */
float weather_outdoor_humidity(const Weather *weather, uint64_t tick);

uint64_t weather_hash(const Weather *weather, uint64_t hash);

/* Saves or loads (docs/SAVE.md) the weather. */
void weather_serialize(Serializer *s, Weather *weather);

#endif /* COLONY_CORE_WEATHER_H */
