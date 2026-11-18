/*
 * The character of a settler or an animal (core/attributes.h,
 * docs/ATTRIBUTES.md) as table rows, by group (body, then mind): the name of
 * each attribute, the word for its position and its exact position in
 * percent, all colored by that position (red at the lowest, white at the
 * ordinary, green at the highest); a tooltip says what it does. Shared by the
 * settler's Character tab and the animal's.
 */
#ifndef COLONY_GAME_ATTRIBUTES_PANEL_H
#define COLONY_GAME_ATTRIBUTES_PANEL_H

#include "core/attributes.h"
#include "game/ui_layout.h"

void attributes_panel_rows(UiLayout *layout, const Attributes *attributes);

#endif /* COLONY_GAME_ATTRIBUTES_PANEL_H */
