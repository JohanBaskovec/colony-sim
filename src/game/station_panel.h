/*
 * Inspector of the selected workstation: construction state of a blueprint,
 * or the bills of a built station (repeat mode, target, allowed inputs,
 * warnings when nobody can do one) and buttons to add bills. Changes are
 * applied to the game immediately.
 */
#ifndef COLONY_GAME_STATION_PANEL_H
#define COLONY_GAME_STATION_PANEL_H

#include "game/app.h"

/* Draws the panel with its bottom right corner at (right, bottom), if a
 * workstation is selected. */
void station_panel_draw(App *app, float right, float bottom);

#endif /* COLONY_GAME_STATION_PANEL_H */
