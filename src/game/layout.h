/*
 * Zones of the screen (docs/UI.md): where the bottom bar lies and where the
 * zones above it end, worked out in one place from the screen size and the
 * theme, so that the parts of the interface never overlap.
 */
#ifndef COLONY_GAME_LAYOUT_H
#define COLONY_GAME_LAYOUT_H

#include "game/ui.h"

typedef struct ScreenLayout {
    float width, height;  /* The screen. */
    float button_height;  /* Text buttons of the menus. */
    SDL_FRect bottom_bar; /* Full width, along the bottom edge. */
    float bar_top;        /* The zones above the bar end here. */
    /* Width kept free at each top corner, beside the colonist bar: the
     * resources list (left), the debug readout (right). */
    float corner_width;
} ScreenLayout;

void layout_screen(const Ui *ui, ScreenLayout *layout);

#endif /* COLONY_GAME_LAYOUT_H */
