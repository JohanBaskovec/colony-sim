/*
 * Test scenarios menu (debug): one button per scenario (core/test_scenario.h).
 * A click replaces the game being played by the scenario, at once and without
 * asking: nothing is saved. Opened and closed with APP_KEY_SCENARIOS_MENU.
 */
#ifndef COLONY_GAME_SCENARIOS_PANEL_H
#define COLONY_GAME_SCENARIOS_PANEL_H

#include "game/app.h"

/* Draws the menu in a window (APP_WINDOW_SCENARIOS) if it is open (App.show_scenarios_menu). */
void scenarios_panel_draw(App *app);

#endif /* COLONY_GAME_SCENARIOS_PANEL_H */
