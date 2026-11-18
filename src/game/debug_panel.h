/*
 * Debug menu: cheats for testing (GameDebug in core/game.h), each a check
 * box. Opened and closed with APP_KEY_DEBUG_MENU. Spawning items has its own
 * menu (game/items_panel.h). "God mode" turns on the first six cheats (up to
 * seeing everything) and the character editor, or all off; the character
 * editor (App.character_editor) puts - and + buttons at the right of the needs
 * of settlers and animals and of the characters and knowledge of settlers
 * (game/hud.c, game/animal_panel.c); the temperature row raises and lowers the
 * air's by steps (Weather.debug_offset).
 */
#ifndef COLONY_GAME_DEBUG_PANEL_H
#define COLONY_GAME_DEBUG_PANEL_H

#include "game/app.h"

/* Draws the menu in a window (APP_WINDOW_DEBUG) if it is open (App.show_debug_menu). */
void debug_panel_draw(App *app);

#endif /* COLONY_GAME_DEBUG_PANEL_H */
