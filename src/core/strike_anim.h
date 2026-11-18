/*
 * How a blow is played on screen (docs/COMBAT.md §5), for the eye only: the
 * attacker draws back, then lunges; when the blow lands, the one struck
 * flinches, flashes and shows the attack's mark (StrikeMark) for a moment.
 *
 * Everything is a function of the ticks since the blow was resolved
 * (CombatEvent.tick): the simulation resolves a blow at once, the screen
 * plays it over the next STRIKE_ANIM_TICKS, so it follows the game's speed
 * and stops while paused. Nothing here touches the simulation.
 */
#ifndef COLONY_CORE_STRIKE_ANIM_H
#define COLONY_CORE_STRIKE_ANIM_H

#include <stdbool.h>

/* Drawing back, then the lunge; the blow lands at their end. */
#define STRIKE_ANIM_WIND_TICKS 18
#define STRIKE_ANIM_LUNGE_TICKS 7
#define STRIKE_ANIM_HIT_TICKS (STRIKE_ANIM_WIND_TICKS + STRIKE_ANIM_LUNGE_TICKS)
/* The attacker goes back to its place. */
#define STRIKE_ANIM_RETURN_TICKS 30
/* The mark: drawn, held, then fading. */
#define STRIKE_ANIM_MARK_DRAW_TICKS 7
#define STRIKE_ANIM_MARK_HOLD_TICKS 24
#define STRIKE_ANIM_MARK_FADE_TICKS 22
/* The whole of it: nothing is left to play after this many ticks. */
#define STRIKE_ANIM_TICKS                                                                          \
    (STRIKE_ANIM_HIT_TICKS + STRIKE_ANIM_MARK_HOLD_TICKS + STRIKE_ANIM_MARK_FADE_TICKS)

/* How far the attacker draws back and lunges, in tiles toward its target. */
#define STRIKE_ANIM_BACK 0.20f
#define STRIKE_ANIM_FORWARD 0.32f

/* The attacker's pose: moved `advance` tiles toward its target (negative:
 * drawn back), leaning `lean` degrees forward, head toward it (negative:
 * rearing). */
typedef struct StrikePose {
    float advance;
    float lean;
} StrikePose;

StrikePose strike_anim_attacker(int ticks);

/* The one struck by a blow that landed: pushed back this many tiles (away
 * from the attacker), and how much it flashes (0..1). */
float strike_anim_recoil(int ticks);
float strike_anim_flash(int ticks);

/* The mark of a blow that landed: whether it shows, how much of it is drawn
 * (0..1) and its opacity (0..1). */
bool strike_anim_mark(int ticks, float *drawn, float *opacity);

#endif /* COLONY_CORE_STRIKE_ANIM_H */
