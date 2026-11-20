/*
 * Resources list (docs/UI.md), in the top left corner: what the colony has in
 * stock (App.stock: what lies in stockpiles), by item category. A group's header shows its total
 * and folds it (a click); each item has a line with its icon, its name and its count, and a tooltip
 * telling their weight (and how many days of food they make). Groups
 * with nothing in stock are left out. The list stops above what the left
 * side of the screen shows below it (inspector, work grid, build menu, tile
 * readout): the lines that do not fit are left out, and the last line says
 * how many.
 */
#ifndef COLONY_GAME_RESOURCES_LIST_H
#define COLONY_GAME_RESOURCES_LIST_H

#include "game/app.h"

/* Draws the list from the top left corner down to `bottom` at most (screen
 * pixels). */
void resources_list_draw(App *app, float bottom);

#endif /* COLONY_GAME_RESOURCES_LIST_H */
