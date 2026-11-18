/*
 * The marks blows leave for a moment on the one struck (StrikeMark,
 * core/combat.h), drawn as plain shapes: three claw scratches, two toothed
 * jaws closing, a slash, a burst of rays. How long they last and how they
 * fade is core/strike_anim.h's; tools/preview_attacks.py draws the same
 * shapes into an animated preview.
 */
#ifndef COLONY_GAME_STRIKE_MARKS_H
#define COLONY_GAME_STRIKE_MARKS_H

#include "core/combat.h"

#include <SDL3/SDL.h>

/*
 * Draws a mark centered on (cx, cy) on screen. `size`: screen pixels for a
 * tile, times the size of the one struck. `side`: +1 when the blow goes left
 * to right on screen (the attacker on the left), -1 the other way. `drawn`
 * and `opacity` (0..1) come from strike_anim_mark().
 */
void strike_mark_draw(SDL_Renderer *renderer, StrikeMark mark, float cx, float cy, float size,
                      float side, float drawn, float opacity);

#endif /* COLONY_GAME_STRIKE_MARKS_H */
