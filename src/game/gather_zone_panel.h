/*
 * Inspector of the selected gathering zone: mode (until the colony has
 * enough, or forever), a target per stone type with how many lie in the
 * colony, what is being gathered now, deletion. Changes are applied to the
 * game immediately.
 */
#ifndef COLONY_GAME_GATHER_ZONE_PANEL_H
#define COLONY_GAME_GATHER_ZONE_PANEL_H

#include "game/app.h"

/* Draws the panel with its bottom right corner at (right, bottom), if a
 * gathering zone is selected. */
void gather_zone_panel_draw(App *app, float right, float bottom);

#endif /* COLONY_GAME_GATHER_ZONE_PANEL_H */
