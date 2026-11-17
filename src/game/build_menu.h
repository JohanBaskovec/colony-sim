/*
 * The build menu (docs/UI.md), opened by "Build" in the bottom bar: a column
 * of categories (workstations, constructions, zones, orders) above the bar
 * at the left edge; a category opens a row of icon buttons to the right of
 * the column, one per thing to place, its shortcut in a corner. Clicking an
 * icon picks the tool; the menus stay open. A card above the column
 * describes the icon under the mouse, else the tool in use (name, cost,
 * description, how to use it, shortcut, why it is unavailable); with the
 * menu closed, the card of the tool in use sits right above the bar.
 */
#ifndef COLONY_GAME_BUILD_MENU_H
#define COLONY_GAME_BUILD_MENU_H

#include "game/app.h"

/* Draws the menu and the card (after the windows: they stay on top). */
void build_menu_draw(App *app);

/* Top of what the menu and the card take up in the bottom left corner (the
 * top of the bottom bar when they show nothing). */
float build_menu_top(App *app);

/* What a structure or a workstation needs (its material slots), with the
 * total weight: "Needs: Branches ×5 and Dry grass ×24 (31.5 kg)"; "Needs no
 * resources" when every slot is empty. */
void build_menu_materials_cost(char *out, size_t size,
                               const StructureMaterial materials[STRUCTURE_MATERIAL_SLOTS]);

#endif /* COLONY_GAME_BUILD_MENU_H */
