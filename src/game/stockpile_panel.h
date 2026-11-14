/*
 * Inspector of the selected stockpile zone: priority (1-10), accepted quality
 * grades and hit points (range sliders), allowed items by category, deletion.
 * Changes are applied to the game immediately.
 */
#ifndef COLONY_GAME_STOCKPILE_PANEL_H
#define COLONY_GAME_STOCKPILE_PANEL_H

#include "game/app.h"

/* Draws the window (APP_WINDOW_STOCKPILE) if a stockpile is selected;
 * closing it deselects the stockpile. */
void stockpile_panel_draw(App *app);

#endif /* COLONY_GAME_STOCKPILE_PANEL_H */
