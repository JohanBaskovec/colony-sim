/*
 * The bottom bar (docs/UI.md): full-width, text buttons of the same width
 * from the left (Build, Work; later Schedule, Assign...), Menu at the right
 * end. A button stays lit while its menu or window is open; its tooltip
 * gives its name, a description and its shortcut. Build and the main
 * windows are exclusive: opening one closes the others.
 */
#ifndef COLONY_GAME_BOTTOM_BAR_H
#define COLONY_GAME_BOTTOM_BAR_H

#include "game/app.h"

void bottom_bar_draw(App *app);

#endif /* COLONY_GAME_BOTTOM_BAR_H */
