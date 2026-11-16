/*
 * Items menu (debug): every item of the game, each with two buttons, one item
 * or a full stack of it. A button arms the debug tool (APP_TOOL_DEBUG): the
 * next click on the map drops that on the tile. Opened and closed with
 * APP_KEY_ITEMS_MENU.
 */
#ifndef COLONY_GAME_ITEMS_PANEL_H
#define COLONY_GAME_ITEMS_PANEL_H

#include "game/app.h"

/* Draws the menu in a window (APP_WINDOW_ITEMS) if it is open (App.show_items_menu). */
void items_panel_draw(App *app);

#endif /* COLONY_GAME_ITEMS_PANEL_H */
