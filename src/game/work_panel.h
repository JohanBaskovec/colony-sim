/*
 * Work priorities grid: one row per colonist, one column per work type. Each
 * cell shows the priority (1 = done first, empty = never); left click raises
 * it, right click lowers it. Clicking a name selects the colonist and centers
 * the view on them.
 */
#ifndef COLONY_GAME_WORK_PANEL_H
#define COLONY_GAME_WORK_PANEL_H

#include "game/app.h"

/* Draws the grid if it is open (App.show_work_panel). */
void work_panel_draw(App *app);

/* Scrolls the rows by a number of mouse wheel steps (positive: towards the top). */
void work_panel_scroll(App *app, float steps);

#endif /* COLONY_GAME_WORK_PANEL_H */
