/*
 * Settings window (docs/UI.md), opened from the game menu: the interface
 * size (75 to 200 %, the font included), the colors (standard or
 * colorblind-friendly) and the language, each a dropdown with a line telling
 * what it does. A choice applies at once and is saved for the next sessions
 * (core/settings.h). It replaces the game menu, which comes back when it
 * closes; the game stays paused meanwhile.
 */
#ifndef COLONY_GAME_SETTINGS_WINDOW_H
#define COLONY_GAME_SETTINGS_WINDOW_H

#include "game/app.h"

/* Draws the window if it is open (App.show_settings). */
void settings_window_draw(App *app);

#endif /* COLONY_GAME_SETTINGS_WINDOW_H */
