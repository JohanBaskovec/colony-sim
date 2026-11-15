/*
 * Heads-up display: date and speed, language, active tool, the menus
 * (game/menu_bar.h) with the tile inspector above them, the windows
 * (inspectors, work priorities, controls help) in their stacking order,
 * tooltips and the debug overlay. Every visible string goes through tr().
 */
#ifndef COLONY_GAME_HUD_H
#define COLONY_GAME_HUD_H

#include "game/app.h"

void hud_draw(App *app);

/* Takes the notices the game raised since the last frame and ages those on
 * screen by `dt` real seconds. */
void hud_update_notices(App *app, double dt);

/* Windows open now, back to front (the last one is in front). Returns how many. */
int hud_open_windows(const App *app, AppWindow out[APP_WINDOW_COUNT]);

/* Stack sizes in the bottom right corner of item tiles, when zoomed in enough
 * (drawn in the world, under the colonists). */
void hud_draw_item_counts(App *app);

/* Names under the colonists, drawn in the world (before the HUD panels). */
void hud_draw_world_labels(App *app);

#endif /* COLONY_GAME_HUD_H */
