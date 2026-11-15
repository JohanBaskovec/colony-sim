/*
 * Inspector of the selected gathering zone: its priority, then a row per
 * group of resources (foldable) and per resource: a check box, how many the
 * zone holds, the full harvest option (a plant with rays, tri-state for a
 * group and for the whole zone, at the top of its column), the mode (a
 * dropdown: forever, gather a number, until a number in the colony or days
 * of food) with its target and progress; deletion. Changes are applied to
 * the game immediately.
 */
#ifndef COLONY_GAME_GATHER_ZONE_PANEL_H
#define COLONY_GAME_GATHER_ZONE_PANEL_H

#include "game/app.h"

/* Draws the window (APP_WINDOW_GATHER_ZONE) if a gathering zone is selected;
 * closing it deselects the zone. */
void gather_zone_panel_draw(App *app);

#endif /* COLONY_GAME_GATHER_ZONE_PANEL_H */
