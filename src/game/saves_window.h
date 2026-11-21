/*
 * The saves window (docs/UI.md, docs/SAVE.md §4), in the middle of the
 * screen: to save (Save in the game menu: a new save, or one overwritten) or
 * to load (Load in the game menu, Load game on the title screen: load or
 * delete). The saves are listed newest first, each with its name and a line
 * telling the date in the game, the settlers and when it was written.
 * Overwriting, deleting and, in a game, loading ask first. It replaces the
 * game menu, which comes back when it closes; the game stays paused.
 */
#ifndef COLONY_GAME_SAVES_WINDOW_H
#define COLONY_GAME_SAVES_WINDOW_H

#include "game/app.h"

/* Opens it, to load or to save. */
void saves_window_open(App *app, bool loading);

/* Escape: drops the question asked, else closes the window (back to the game
 * menu in a game). */
void saves_window_step_back(App *app);

/* Draws the window if it is open (App.show_saves). */
void saves_window_draw(App *app);

#endif /* COLONY_GAME_SAVES_WINDOW_H */
