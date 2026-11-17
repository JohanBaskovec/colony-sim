/*
 * The alerts column (docs/UI.md), right aligned above the date and weather:
 * one line per alert in progress (core/alert.h): a mark, what is wrong, and
 * how many settlers. A tooltip explains it and tells where a click leads: to
 * the first settler concerned (selected, the view centered on it).
 */
#ifndef COLONY_GAME_ALERTS_LIST_H
#define COLONY_GAME_ALERTS_LIST_H

#include "game/app.h"

/* Draws the lines stacked upwards from `bottom`, their right edges at `right`
 * (screen pixels). */
void alerts_list_draw(App *app, float right, float bottom);

#endif /* COLONY_GAME_ALERTS_LIST_H */
