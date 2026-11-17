/*
 * Inspector (game/inspector.h) of the selected stockpile zone: size and
 * priority (1-10) in the summary; a Storage tab with the accepted quality
 * grades and hit points (range sliders) and the allowed items by category;
 * actions: build another, delete. Changes are applied to the game
 * immediately.
 */
#ifndef COLONY_GAME_STOCKPILE_PANEL_H
#define COLONY_GAME_STOCKPILE_PANEL_H

#include "game/app.h"

/* Draws the inspector if a stockpile is selected; closing it deselects the
 * stockpile. */
void stockpile_panel_draw(App *app);

#endif /* COLONY_GAME_STOCKPILE_PANEL_H */
