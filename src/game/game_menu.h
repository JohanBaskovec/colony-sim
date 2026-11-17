/*
 * The game menu (docs/UI.md): a window in the middle of the screen, opened
 * by Menu in the bottom bar or by Escape when there is nothing else to close:
 * resume, controls, settings (game/settings_window.h), quit. The game is paused while it is open.
 */
#ifndef COLONY_GAME_GAME_MENU_H
#define COLONY_GAME_GAME_MENU_H

#include "game/app.h"

void game_menu_draw(App *app);

#endif /* COLONY_GAME_GAME_MENU_H */
