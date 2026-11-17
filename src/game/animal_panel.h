/*
 * Inspector (game/inspector.h) of the selected wild animal
 * (App.selected_animal): what it is called as the title (a hind, a stag), its
 * species, its sex, what it is doing, its needs as bars and its herd. No
 * action yet: nothing can be done to an animal.
 */
#ifndef COLONY_GAME_ANIMAL_PANEL_H
#define COLONY_GAME_ANIMAL_PANEL_H

#include "game/app.h"

/* Draws the inspector if an animal is selected; closing it deselects it. */
void animal_panel_draw(App *app);

#endif /* COLONY_GAME_ANIMAL_PANEL_H */
