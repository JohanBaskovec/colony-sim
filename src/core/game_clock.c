#include "core/game_clock.h"

#include "core/i18n.h"
#include "core/util.h"

void game_clock_init(GameClock *clock)
{
    clock->tick = (uint64_t)GAME_START_HOUR * TICKS_PER_HOUR;
    clock->speed = GAME_SPEED_NORMAL;
    clock->resume_speed = GAME_SPEED_NORMAL;
    clock->pending_ticks = 0.0;
}

int game_speed_multiplier(GameSpeed speed)
{
    switch (speed) {
    case GAME_SPEED_PAUSED: return 0;
    case GAME_SPEED_NORMAL: return 1;
    case GAME_SPEED_FAST: return 3;
    case GAME_SPEED_FASTEST: return 6;
    case GAME_SPEED_COUNT: break;
    }
    return 0;
}

void game_clock_set_speed(GameClock *clock, GameSpeed speed)
{
    if ((unsigned)speed >= GAME_SPEED_COUNT) {
        return;
    }
    clock->speed = speed;
    if (speed != GAME_SPEED_PAUSED) {
        clock->resume_speed = speed;
    }
}

void game_clock_toggle_pause(GameClock *clock)
{
    if (clock->speed == GAME_SPEED_PAUSED) {
        clock->speed = clock->resume_speed;
    } else {
        clock->speed = GAME_SPEED_PAUSED;
    }
}

int game_clock_consume(GameClock *clock, double real_seconds)
{
    int multiplier = game_speed_multiplier(clock->speed);
    if (multiplier == 0 || real_seconds <= 0.0) {
        if (multiplier == 0) {
            clock->pending_ticks = 0.0;
        }
        return 0;
    }

    clock->pending_ticks += real_seconds * TICKS_PER_SECOND * multiplier;
    if (clock->pending_ticks >= GAME_CLOCK_MAX_TICKS_PER_UPDATE) {
        /* Too far behind: drop the backlog rather than trying to catch up. */
        clock->pending_ticks = 0.0;
        return GAME_CLOCK_MAX_TICKS_PER_UPDATE;
    }
    int due = (int)clock->pending_ticks;
    clock->pending_ticks -= due;
    return due;
}

GameDate game_date_from_tick(uint64_t tick)
{
    GameDate date;
    uint64_t day_index = tick / TICKS_PER_DAY;
    uint64_t tick_in_day = tick % TICKS_PER_DAY;

    date.year = (int)(day_index / DAYS_PER_YEAR) + 1;
    int day_in_year = (int)(day_index % DAYS_PER_YEAR);
    date.season = (Season)(day_in_year / DAYS_PER_SEASON);
    date.day = day_in_year % DAYS_PER_SEASON + 1;
    date.hour = (int)(tick_in_day / TICKS_PER_HOUR);
    date.minute = (int)((tick_in_day % TICKS_PER_HOUR) * 60 / TICKS_PER_HOUR);
    return date;
}

float game_daylight(uint64_t tick)
{
    float hour = (float)(tick % TICKS_PER_DAY) / (float)TICKS_PER_HOUR;
    const float dawn_start = 5.0f, dawn_end = 7.0f;
    const float dusk_start = 18.0f, dusk_end = 20.0f;

    if (hour < dawn_start || hour >= dusk_end) {
        return GAME_DAYLIGHT_MIN;
    }
    if (hour < dawn_end) {
        return lerpf(GAME_DAYLIGHT_MIN, 1.0f, (hour - dawn_start) / (dawn_end - dawn_start));
    }
    if (hour < dusk_start) {
        return 1.0f;
    }
    return lerpf(1.0f, GAME_DAYLIGHT_MIN, (hour - dusk_start) / (dusk_end - dusk_start));
}

const char *season_name_key(Season season)
{
    static const char *const KEYS[SEASON_COUNT] = {
        [SEASON_SPRING] = N_("season.spring"),
        [SEASON_SUMMER] = N_("season.summer"),
        [SEASON_AUTUMN] = N_("season.autumn"),
        [SEASON_WINTER] = N_("season.winter"),
    };
    return ((unsigned)season < SEASON_COUNT) ? KEYS[season] : KEYS[SEASON_SPRING];
}
