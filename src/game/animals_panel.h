/*
 * Animals menu (debug): every species of wild animal, each with two buttons, a
 * female or a male. A button arms the debug tool (APP_TOOL_DEBUG): each click on
 * the map then puts one such animal on the tile, until the tool is put away.
 * Opened and closed with APP_KEY_ANIMALS_MENU.
 */
#ifndef COLONY_GAME_ANIMALS_PANEL_H
#define COLONY_GAME_ANIMALS_PANEL_H

#include "game/app.h"

/* Draws the menu in a window (APP_WINDOW_ANIMALS) if it is open (App.show_animals_menu). */
void animals_panel_draw(App *app);

#endif /* COLONY_GAME_ANIMALS_PANEL_H */
