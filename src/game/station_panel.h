/*
 * Inspector (game/inspector.h) of the selected workstation: in the summary,
 * whether the settlers leave it alone, a hearth's fuel and fire, what is
 * being made and how many bills; a Bills tab (repeat mode, target, allowed
 * inputs, warnings when nobody can do one, buttons to add bills), a Fire tab
 * for hearths (keep burning, fuels, what is taught by it); actions: forbid,
 * build another, delete. Changes are applied to the game immediately.
 */
#ifndef COLONY_GAME_STATION_PANEL_H
#define COLONY_GAME_STATION_PANEL_H

#include "game/app.h"

/* Draws the inspector if a workstation is selected; closing it deselects
 * the station. */
void station_panel_draw(App *app);

#endif /* COLONY_GAME_STATION_PANEL_H */
