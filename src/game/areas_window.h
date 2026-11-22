/*
 * The allowed areas window (core/allowed_area.h): a main window docked at the bottom left above
 * the bottom bar, opened by its button. One block per area, the default one first: its name
 * (a click shows it on the map), Rename, Invert, Extend, Shrink and Delete (not for the last
 * area left), its size and the settlers bound to it; New area at the bottom asks for a name,
 * then the area is painted (APP_TOOL_AREA). Names are typed in the window, the name proposed
 * selected (the first key typed replaces it): Enter keeps the name, Escape gives it up.
 */
#ifndef COLONY_GAME_AREAS_WINDOW_H
#define COLONY_GAME_AREAS_WINDOW_H

#include "game/app.h"

/* Draws the window if it is open (App.show_areas). */
void areas_window_draw(App *app);

/* Opens or closes it (the bottom bar's button); closing it ends the naming and the painting. */
void areas_window_set_open(App *app, bool open);

/* While a name is typed (App.area_naming): the text typed and the keys that edit it (Backspace,
 * Enter, Escape; the others do nothing, so as not to act on the game). True when the event was
 * taken. */
bool areas_window_handle_event(App *app, const SDL_Event *event);

/* The name shown for an area: the one the player gave it, else the default name. */
void areas_window_area_name(const App *app, int index, char *out, size_t size);

#endif /* COLONY_GAME_AREAS_WINDOW_H */
