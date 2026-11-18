#include "core/strike_anim.h"

#include "core/util.h"

#include <math.h>

#define LEAN_BACK 5.0f
#define LEAN_FORWARD 6.0f
#define RECOIL 0.07f
#define RECOIL_TICKS 14
#define FLASH 0.4f
#define FLASH_TICKS 5

static float ease_in_out(float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return 0.5f - 0.5f * cosf(3.14159265f * t);
}

static float ease_in(float t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t;
}

static float ease_out(float t)
{
    t = 1.0f - clampf(t, 0.0f, 1.0f);
    return 1.0f - t * t * t;
}

StrikePose strike_anim_attacker(int ticks)
{
    if (ticks < 0 || ticks >= STRIKE_ANIM_HIT_TICKS + STRIKE_ANIM_RETURN_TICKS) {
        return (StrikePose){0.0f, 0.0f};
    }
    if (ticks < STRIKE_ANIM_WIND_TICKS) {
        float t = ease_in_out((float)ticks / STRIKE_ANIM_WIND_TICKS);
        return (StrikePose){-STRIKE_ANIM_BACK * t, -LEAN_BACK * t};
    }
    if (ticks < STRIKE_ANIM_HIT_TICKS) {
        float t = ease_in((float)(ticks - STRIKE_ANIM_WIND_TICKS + 1) / STRIKE_ANIM_LUNGE_TICKS);
        return (StrikePose){-STRIKE_ANIM_BACK + (STRIKE_ANIM_BACK + STRIKE_ANIM_FORWARD) * t,
                            -LEAN_BACK + (LEAN_BACK + LEAN_FORWARD) * t};
    }
    float left = 1.0f - ease_out((float)(ticks - STRIKE_ANIM_HIT_TICKS) / STRIKE_ANIM_RETURN_TICKS);
    return (StrikePose){STRIKE_ANIM_FORWARD * left, LEAN_FORWARD * left};
}

float strike_anim_recoil(int ticks)
{
    int since = ticks - STRIKE_ANIM_HIT_TICKS;
    if (since < 0 || since >= RECOIL_TICKS) {
        return 0.0f;
    }
    return RECOIL * (1.0f - ease_out((float)since / RECOIL_TICKS));
}

float strike_anim_flash(int ticks)
{
    int since = ticks - STRIKE_ANIM_HIT_TICKS;
    return since >= 0 && since < FLASH_TICKS ? FLASH : 0.0f;
}

bool strike_anim_mark(int ticks, float *drawn, float *opacity)
{
    int since = ticks - STRIKE_ANIM_HIT_TICKS;
    if (since < 0 || ticks >= STRIKE_ANIM_TICKS) {
        return false;
    }
    *drawn = clampf((float)(since + 1) / STRIKE_ANIM_MARK_DRAW_TICKS, 0.0f, 1.0f);
    int fading = since - STRIKE_ANIM_MARK_HOLD_TICKS;
    *opacity = fading < 0 ? 1.0f : 1.0f - (float)fading / STRIKE_ANIM_MARK_FADE_TICKS;
    return true;
}
