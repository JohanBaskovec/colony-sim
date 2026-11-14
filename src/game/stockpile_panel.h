/*
 * Inspector of the selected stockpile zone: priority (1-10), accepted quality
 * grades and hit points (range sliders), allowed items by category, deletion.
 * Changes are applied to the game immediately.
 */
#ifndef COLONY_GAME_STOCKPILE_PANEL_H
#define COLONY_GAME_STOCKPILE_PANEL_H

#include "game/app.h"

/* Draws the panel with its bottom right corner at (right, bottom), if a
 * stockpile is selected. */
void stockpile_panel_draw(App *app, float right, float bottom);

#endif /* COLONY_GAME_STOCKPILE_PANEL_H */
