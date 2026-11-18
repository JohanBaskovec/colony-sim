/*
 * The health of a settler or an animal (core/health.h, docs/HEALTH.md): in
 * the Health tab, always its state in a word, its consciousness, blood,
 * bleeding and pain, the afflictions wearing it down (hunger, thirst, cold,
 * heat) and what its body can do; in a window to the right of the tab, the
 * table of its body, each part with its injuries, how much of it works and
 * the blood it loses, with the details in tooltips. Shared by the settler's
 * Health tab and the animal window.
 */
#ifndef COLONY_GAME_HEALTH_PANEL_H
#define COLONY_GAME_HEALTH_PANEL_H

#include "core/health.h"
#include "game/app.h"
#include "game/ui_layout.h"

/* The rows of the tab (a UiRowsFn's body). */
void health_panel_rows(UiLayout *layout, const Health *health);

/* The window of the body's parts, docked to the right of the open tab: call it
 * just after drawing the Health tab's panel (game/inspector.h). */
void health_panel_body_window(App *app, const Health *health);

#endif /* COLONY_GAME_HEALTH_PANEL_H */
