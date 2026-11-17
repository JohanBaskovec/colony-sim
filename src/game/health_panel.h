/*
 * The health of a settler or an animal (core/health.h, docs/HEALTH.md) as
 * table rows: its state in a word and, once hurt, its blood, pain and
 * bleeding, what its body can still do, and each injury (the part, the kind,
 * how bad), with the details in tooltips. Shared by the settler's Health tab
 * and the animal window.
 */
#ifndef COLONY_GAME_HEALTH_PANEL_H
#define COLONY_GAME_HEALTH_PANEL_H

#include "core/health.h"
#include "game/ui_layout.h"

/* The rows (a UiRowsFn's body): only the state for a healthy body. */
void health_panel_rows(UiLayout *layout, const Health *health);

#endif /* COLONY_GAME_HEALTH_PANEL_H */
