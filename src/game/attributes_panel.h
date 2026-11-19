/*
 * The character of a settler or an animal (core/attributes.h,
 * docs/ATTRIBUTES.md) as table rows, by group (body, then mind): the name of
 * each attribute and its position in percent of the way to either end,
 * colored by that position (red at the lowest, white at the ordinary, green at
 * the highest); a tooltip says what it does. Shared by the
 * settler's Character tab and the animal's.
 */
#ifndef COLONY_GAME_ATTRIBUTES_PANEL_H
#define COLONY_GAME_ATTRIBUTES_PANEL_H

#include "core/attributes.h"
#include "game/ui_layout.h"

/* With `editable`, each row ends with the - and + buttons of the character
 * editor (game/debug_panel.h): returns -1 or 1 for the one that was clicked
 * and sets `*edited` to its attribute, else 0. With `animal`, only the
 * attributes that change anything for an animal (attribute_affects_animals()). */
int attributes_panel_rows(UiLayout *layout, const Attributes *attributes, bool animal,
                          bool editable, AttributeType *edited);

#endif /* COLONY_GAME_ATTRIBUTES_PANEL_H */
