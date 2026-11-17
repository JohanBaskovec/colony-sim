/*
 * The colonist bar (docs/UI.md), top center: an entry per colonist with a
 * portrait, the name and the current action (cut short, in full in the
 * tooltip). A click selects the colonist (Shift: adds it to the selection
 * or takes it out), a double click also centers the view on it. With many
 * colonists the entries shrink (the action, then the name, left to the
 * tooltip), then wrap onto more rows (core/entry_strip.h). A hurt colonist's
 * portrait bears a cross (red while it bleeds or is down), and its state is
 * in the tooltip.
 */
#ifndef COLONY_GAME_COLONIST_BAR_H
#define COLONY_GAME_COLONIST_BAR_H

#include "game/app.h"

void colonist_bar_draw(App *app);

/* Bottom of the bar (the top margin when there is no colonist). */
float colonist_bar_bottom(const App *app);

#endif /* COLONY_GAME_COLONIST_BAR_H */
