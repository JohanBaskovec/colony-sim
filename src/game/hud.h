/*
 * Heads-up display: date and speed, language, tile inspector, controls help
 * and the debug overlay. Every visible string goes through tr().
 */
#ifndef COLONY_GAME_HUD_H
#define COLONY_GAME_HUD_H

#include "game/app.h"

void hud_draw(App *app);

/* Names under the colonists, drawn in the world (before the HUD panels). */
void hud_draw_world_labels(App *app);

#endif /* COLONY_GAME_HUD_H */
