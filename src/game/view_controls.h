/*
 * The bottom right corner (docs/UI.md), above the bottom bar: the overlay
 * toggles (temperature) and the speed buttons (pause, ×1, ×2, ×3) on a row
 * at the bottom; at the right edge, the level slider (drag its handle, or
 * the + and − buttons at its ends; a tick marks the ground level) with
 * "level / levels" under it, counted from 1, the deepest level. The date and
 * weather (drawn by the HUD) go to the left of the slider, above the row.
 */
#ifndef COLONY_GAME_VIEW_CONTROLS_H
#define COLONY_GAME_VIEW_CONTROLS_H

#include "game/app.h"

void view_controls_draw(App *app);

/* Width taken at the right edge of the screen. */
float view_controls_width(const App *app);

/* Where the date and weather go: their bottom right corner. */
void view_controls_info_corner(const App *app, float *right, float *bottom);

#endif /* COLONY_GAME_VIEW_CONTROLS_H */
