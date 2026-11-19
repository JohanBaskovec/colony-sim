/*
 * Inspector (game/inspector.h) of the selected thing on a tile (a structure,
 * a roof, a construction site, the items, the plant: App.selected_thing),
 * like a workstation's: its name as the title, what there is to know of it in
 * the box, and the action menu right of it: forbid or allow
 * (game_set_forbidden()), the orders it takes (fell a tree, take a structure
 * or roof apart: a click again takes the order back; cancel a site) and
 * build another. Changes are applied to the game immediately.
 */
#ifndef COLONY_GAME_TILE_PANEL_H
#define COLONY_GAME_TILE_PANEL_H

#include "game/app.h"

/* Draws the inspector if a thing on a tile is selected; closing it
 * deselects it. */
void tile_panel_draw(App *app);

/* APP_KEY_CANCEL on the selected thing: cancels a site, orders a structure
 * or roof taken apart (or takes the order back), takes back an order on a
 * plant or a move of the items. False when there is nothing to do. */
bool tile_panel_cancel(App *app);

#endif /* COLONY_GAME_TILE_PANEL_H */
