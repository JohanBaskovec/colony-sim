/*
 * Simulation time: fixed-rate ticks, game speed and the in-game calendar.
 *
 * The simulation always advances in whole ticks of fixed duration, whatever
 * the frame rate: the frontend reports elapsed real time and runs as many
 * ticks as are due. This keeps the simulation deterministic.
 */
#ifndef COLONY_CORE_GAME_CLOCK_H
#define COLONY_CORE_GAME_CLOCK_H

#include <stdint.h>

#define TICKS_PER_SECOND 60 /* At normal speed. */
#define TICKS_PER_HOUR 2500
#define HOURS_PER_DAY 24
#define TICKS_PER_DAY (TICKS_PER_HOUR * HOURS_PER_DAY)
#define DAYS_PER_SEASON 15
#define SEASONS_PER_YEAR 4
#define DAYS_PER_YEAR (DAYS_PER_SEASON * SEASONS_PER_YEAR)
#define TICKS_PER_YEAR ((uint64_t)TICKS_PER_DAY * DAYS_PER_YEAR)

/* Day of the year (0-based) of a season's day (1-based). */
#define SEASON_DAY(season, day) ((int)(season) * DAYS_PER_SEASON + (day) - 1)

/* A new game starts at the end of summer of year 1 (brambles bear fruit,
 * hazelnuts and roots follow in autumn), on this day of the year, at this
 * hour. */
#define GAME_START_DAY SEASON_DAY(SEASON_SUMMER, 11)
#define GAME_START_HOUR 6
#define GAME_START_TICK                                                                            \
    (((uint64_t)GAME_START_DAY * HOURS_PER_DAY + GAME_START_HOUR) * TICKS_PER_HOUR)

/* Upper bound of ticks run per update, so a slow frame cannot snowball. */
#define GAME_CLOCK_MAX_TICKS_PER_UPDATE 30

/* Minimum light level at night (1.0 = full daylight). */
#define GAME_DAYLIGHT_MIN 0.3f

typedef enum GameSpeed {
    GAME_SPEED_PAUSED,
    GAME_SPEED_SLOWEST, /* ×0.3 */
    GAME_SPEED_SLOW,    /* ×0.5 */
    GAME_SPEED_NORMAL,
    GAME_SPEED_FAST,    /* ×3 */
    GAME_SPEED_FASTEST, /* ×6 */
    GAME_SPEED_BLAZING, /* ×12 */
    GAME_SPEED_MAXIMUM, /* ×24 */
    GAME_SPEED_COUNT
} GameSpeed;

typedef enum Season {
    SEASON_SPRING,
    SEASON_SUMMER,
    SEASON_AUTUMN,
    SEASON_WINTER,
    SEASON_COUNT
} Season;

typedef struct GameClock {
    uint64_t tick; /* Ticks since the start of year 1. */
    GameSpeed speed;
    GameSpeed resume_speed; /* Speed restored when unpausing. */
    double pending_ticks;   /* Fractional ticks carried over between updates. */
} GameClock;

typedef struct GameDate {
    int year; /* 1-based */
    Season season;
    int day;    /* 1-based, within the season */
    int hour;   /* 0..23 */
    int minute; /* 0..59 */
} GameDate;

void game_clock_init(GameClock *clock);

/* Simulation speed multiplier: 0 when paused, from 0.3 to 24. */
float game_speed_multiplier(GameSpeed speed);

/* The next speed up (`direction` > 0) or down (< 0) from a running speed,
 * the same at either end; normal speed from a paused or invalid one. */
GameSpeed game_speed_step(GameSpeed speed, int direction);

void game_clock_set_speed(GameClock *clock, GameSpeed speed);
void game_clock_toggle_pause(GameClock *clock);

/* Steps to the next speed up or down (unpausing a paused clock). */
void game_clock_step_speed(GameClock *clock, int direction);

/*
 * Accumulates `real_seconds` of elapsed real time and returns how many ticks
 * are due now (capped). The caller runs them; the clock's `tick` counter is
 * advanced by game_tick(), not here.
 */
int game_clock_consume(GameClock *clock, double real_seconds);

GameDate game_date_from_tick(uint64_t tick);

/* Hour of the day (0-23) only: cheaper than the whole date. */
int game_hour_from_tick(uint64_t tick);

/* Day of the year, 0 (spring, day 1) .. DAYS_PER_YEAR - 1. */
int game_day_of_year(uint64_t tick);

/* The sun (docs/PLANTS.md §5): the day lasts GAME_DAY_LENGTH_WINTER hours at
 * midwinter and GAME_DAY_LENGTH_SUMMER at midsummer (12 at the equinoxes, in
 * between smoothly), centered on noon; the sun is stronger in summer than in
 * winter (a noon of GAME_SUN_NOON_WINTER to 1). */
#define GAME_DAY_LENGTH_WINTER 8.5f
#define GAME_DAY_LENGTH_SUMMER 15.5f
#define GAME_SUN_NOON_WINTER 0.6f

/* Hours between sunrise and sunset on a day of the year. */
float game_day_length(int day_of_year);

/* Sunrise (hour of the day, fractional) on a day of the year; sunset is the
 * day's length later. */
float game_sunrise(int day_of_year);

/* How strong the sun stands at its height on a day of the year (the noon
 * sun), 0 .. 1. */
float game_sun_noon(int day_of_year);

/* The sun on the ground at a tick, before the sky and what covers the
 * ground: the noon sun times the sine of its progress between sunrise and
 * sunset; 0 at night. */
float game_sunlight(uint64_t tick);

/* Light level for the given tick as drawn: GAME_DAYLIGHT_MIN at night, 1.0 at
 * day, dawn and dusk (two hours each) around sunrise and sunset. */
float game_daylight(uint64_t tick);

/* Translation key of a season's display name. */
const char *season_name_key(Season season);

#endif /* COLONY_CORE_GAME_CLOCK_H */
