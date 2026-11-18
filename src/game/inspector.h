/*
 * The inspector (docs/UI.md), bottom left above the bottom bar: a box with
 * the summary of what is selected (its title closes the selection), tabs
 * above it that open their panel above them (one at a time), and action
 * buttons to the right of the box. Each kind of selection draws its own
 * content through these functions, in this order: inspector_box(), then
 * inspector_tabs() and inspector_tab_panel() for the open tab, then
 * inspector_action() for each action. Hidden while the build menu or a main
 * window (work) is open.
 */
#ifndef COLONY_GAME_INSPECTOR_H
#define COLONY_GAME_INSPECTOR_H

#include "game/app.h"
#include "game/render.h"
#include "game/ui_layout.h"

typedef enum InspectorKind {
    INSPECT_NONE,
    INSPECT_COLONIST,
    INSPECT_COLONISTS, /* Several. */
    INSPECT_STATION,
    INSPECT_STOCKPILE,
    INSPECT_GATHER_ZONE,
    INSPECT_TILE,
    INSPECT_ANIMAL,
} InspectorKind;

/* What the inspector shows now (INSPECT_NONE: nothing selected, or the
 * build menu is open). */
InspectorKind inspector_kind(const App *app);

/* Draws the inspector of the selection. */
void inspector_draw(App *app);

/* The summary box; false when its × was clicked (the caller drops the
 * selection). The open tab is forgotten when the kind changes. */
bool inspector_box(App *app, InspectorKind kind, const char *title, UiRowsFn rows, void *userdata);

/* Tab buttons above the box; returns the open tab (-1: none). */
int inspector_tabs(App *app, const char *const *labels, int count);

/* The open tab's panel, above the tabs: no title (the tab's button says it),
 * only a × to close it. */
void inspector_tab_panel(App *app, UiRowsFn rows, void *userdata);

/* A window docked to the right of the window `after` (the open tab's panel,
 * APP_WINDOW_INSPECTOR_TAB, or another such window drawn just before it),
 * with the same bottom and the same top: as tall as the screen allows, like the
 * panel (the room the content leaves is empty), as wide as its content. `title` has a × that sets
 * *open to false. Draw it after `after` in the same frame. */
void inspector_side_window(App *app, int id, int after, const char *title, UiRowsFn rows,
                           void *userdata, bool *open);

/* Action button `slot` (0, 1...) right of the box, lit while `lit`: true
 * when it was clicked. *icon is where to draw its icon; the tooltip shows
 * `lines` (the first one is its name). */
bool inspector_action(App *app, int slot, bool lit, SDL_FRect *icon, const char *const *lines,
                      int count);

/* The usual actions. Forbid or allow (lit while forbidden): true when
 * clicked. `forbid_hint` is the tooltip's second line while it is not
 * forbidden (NULL: the usual one). */
bool inspector_forbid_action(App *app, int slot, bool forbidden, const char *forbid_hint);

/* Move the selected settlers: lit while the Move tool is armed, a click arms
 * it (or puts it away); the next click on the map sends them there. */
void inspector_move_action(App *app, int slot);

/* Follow the selected settler or animal (`kind`, `id`): lit while the camera
 * follows it, a click starts or stops it (app_toggle_follow()). */
void inspector_follow_action(App *app, int slot, FollowKind kind, EntityId id);

/* Build another of the selection (app_build_like_selection(), done here);
 * false (nothing drawn, the slot stays free) when there is nothing to build
 * like it. */
bool inspector_build_like_action(App *app, int slot);

/* An order on what is selected (fell it, take it apart...), shown with
 * `icon`, lit while it is `ordered` (a click then cancels it). Veiled while
 * it cannot be given, `unavailable` saying why in the tooltip (NULL: it can).
 * True when clicked and it may be given or cancelled. */
bool inspector_order_action(App *app, int slot, RenderIcon icon, const char *name,
                            const char *description, bool ordered, const char *unavailable);

/* Delete what is selected: true when clicked. */
bool inspector_delete_action(App *app, int slot, const char *description);

#endif /* COLONY_GAME_INSPECTOR_H */
