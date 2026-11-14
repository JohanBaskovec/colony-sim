/*
 * Work priorities grid: one row per colonist, one column per work type. Each
 * cell shows the priority (1 = done first, empty = never); left click raises
 * it, right click lowers it. Clicking a name selects the colonist and centers
 * the view on them.
 */
#ifndef COLONY_GAME_WORK_PANEL_H
#define COLONY_GAME_WORK_PANEL_H

#include "game/app.h"

/* Draws the grid in a window (APP_WINDOW_WORK) if it is open (App.show_work_panel). */
void work_panel_draw(App *app);

#endif /* COLONY_GAME_WORK_PANEL_H */
