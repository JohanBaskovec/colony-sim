/*
 * Bottom left menus: a bar with "Build", "Work" (the work priorities grid)
 * and "?" (controls help). "Build" opens a row of categories (workstations,
 * zones, orders) above the bar; a category opens a row of icon buttons above
 * it, one per thing to place, with a tooltip (name, description, what it
 * needs, its shortcut). Clicking an icon picks the tool; the menus stay open.
 */
#ifndef COLONY_GAME_MENU_BAR_H
#define COLONY_GAME_MENU_BAR_H

#include "game/app.h"

/* Draws the bar and the open menus (after the windows: they stay on top). */
void menu_bar_draw(App *app);

/* What a structure needs, with the total weight: "Needs: Branches ×5 and Dry
 * grass ×24 (31.5 kg)". */
void menu_bar_structure_cost(char *out, size_t size, StructureType type);

/* Top of the bar and the open menus. */
float menu_bar_top(const App *app);

#endif /* COLONY_GAME_MENU_BAR_H */
