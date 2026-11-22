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

/* The Carry action of one settler (docs/CARRYING.md §7.2): arms
 * APP_TOOL_CARRY, a click on an item then sends the settler to fetch it and
 * keep it, locked. */
void inspector_carry_action(App *app, int slot);

/* The allowed area of the selected settlers (core/allowed_area.h): a list of the areas, the
 * one picked given to all of them; pointing at it shows the area of the first one. */
void inspector_area_action(App *app, int slot);

/* Mobilize, or Demobilize when every selected settler is mobilized (lit
 * then): app_toggle_draft(). */
void inspector_draft_action(App *app, int slot);

/* Change weapon, for one mobilized settler: a list upward of its fists, the
 * weapons it keeps and those lying within a tile (game_weapon_choices());
 * the one picked is wielded. */
void inspector_change_weapon_action(App *app, int slot);

/* Attack, for mobilized settlers: arms APP_TOOL_ATTACK (lit while armed). */
void inspector_attack_action(App *app, int slot);

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
 * `shortcut`: the tooltip tells that APP_KEY_CANCEL does the same.
 * True when clicked and it may be given or cancelled. */
bool inspector_order_action(App *app, int slot, RenderIcon icon, const char *name,
                            const char *description, bool ordered, const char *unavailable,
                            bool shortcut);

/* Extend and shrink the selected zone alone (slots `slot` and `slot` + 1):
 * each arms `tool` (APP_TOOL_STOCKPILE or APP_TOOL_GATHER) in that mode, lit
 * while it is, or turns it off. `extend_icon` and `shrink_icon` draw them. */
void inspector_zone_edit_actions(App *app, int slot, AppTool tool, RenderIcon extend_icon,
                                 RenderIcon shrink_icon);

/* The priority of a station or site (JOB_PRIORITY_HIGHEST to _LOWEST), its
 * number on the button: a left click raises it, a right click lowers it.
 * True when it changes, with the new one in *changed. */
bool inspector_priority_action(App *app, int slot, int priority, int *changed);

/* The species of wood a site is to be built of (docs/TREES.md §6.4): its
 * `type` of wood on the button; a click opens a list upward of "Any wood"
 * and each species (with how many of `type` lie about). True when one is
 * picked, its species in *picked. */
bool inspector_wood_action(App *app, int slot, ItemType type, WoodMask current, WoodMask *picked);

/* Delete what is selected: true when clicked. `shortcut`: the tooltip tells
 * that APP_KEY_CANCEL does the same (app_cancel_selection()). */
bool inspector_delete_action(App *app, int slot, const char *description, bool shortcut);

#endif /* COLONY_GAME_INSPECTOR_H */
