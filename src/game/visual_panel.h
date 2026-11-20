/*
 * Visual debug menu: sliders for how the view looks (RenderTuning in
 * game/render.h), for playtests: how dark what is out of view gets (the fog of
 * war), and how blurred and how dark the levels below the one shown are, each
 * from 0 to 100 percent, and a button back to the game's own look. Opened and
 * closed with APP_KEY_VISUAL_MENU. The values last for the session (they are
 * not saved).
 */
#ifndef COLONY_GAME_VISUAL_PANEL_H
#define COLONY_GAME_VISUAL_PANEL_H

#include "game/app.h"

/* Draws the menu in a window (APP_WINDOW_VISUAL) if it is open (App.show_visual_menu). */
void visual_panel_draw(App *app);

#endif /* COLONY_GAME_VISUAL_PANEL_H */
