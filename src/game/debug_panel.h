/*
 * Debug menu: cheats for testing (GameDebug in core/game.h), each a check
 * box. Opened and closed with APP_KEY_DEBUG_MENU.
 */
#ifndef COLONY_GAME_DEBUG_PANEL_H
#define COLONY_GAME_DEBUG_PANEL_H

#include "game/app.h"

/* Draws the menu in a window (APP_WINDOW_DEBUG) if it is open (App.show_debug_menu). */
void debug_panel_draw(App *app);

#endif /* COLONY_GAME_DEBUG_PANEL_H */
