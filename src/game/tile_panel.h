/*
 * Inspector (game/inspector.h) of the selected tile: what lies there that
 * the settlers may be forbidden (the structure, the roof, the items, the
 * plant), each with a button to forbid or allow it (game_set_forbidden()),
 * and the construction site there; action: build another (a structure, a
 * workstation's site). Changes are applied to the game immediately.
 */
#ifndef COLONY_GAME_TILE_PANEL_H
#define COLONY_GAME_TILE_PANEL_H

#include "game/app.h"

/* Draws the inspector if a tile is selected; closing it
 * deselects the tile. */
void tile_panel_draw(App *app);

#endif /* COLONY_GAME_TILE_PANEL_H */
