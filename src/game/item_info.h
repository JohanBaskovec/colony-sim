/*
 * What there is to know of an item, in rows: its fixed traits (weight, size,
 * form, wood, food, tool and weapon figures), shared by the recipe details of a
 * workstation (game/station_panel.h) and by the information window of the item
 * selected (a button of its inspector), which adds its own condition (quality,
 * hit points, rot, moisture), where it lies and where it comes from.
 */
#ifndef COLONY_GAME_ITEM_INFO_H
#define COLONY_GAME_ITEM_INFO_H

#include "game/app.h"
#include "game/ui_layout.h"

/* A table row: what it is, then its value. Returns the row's rectangle. */
SDL_FRect item_info_row(UiLayout *layout, const char *label_key, const char *value, bool hoverable);

/* A row whose tooltip is the given lines (none when `count` is 0). */
void item_info_row_tip(UiLayout *layout, const char *label_key, const char *value,
                       const char *const *lines, int count);

/* A dim title over a group of rows. */
void item_info_heading(UiLayout *layout, const char *key);

/* "1.5 kg": `value` with as many decimals as it needs to be told from zero, and
 * the unit (a translation key, NULL: none). */
void item_info_quantity_text(char *out, size_t size, float value, const char *unit_key);

/* "75%" for a fraction (0..1). */
void item_info_percent_text(char *out, size_t size, float fraction);

/* "62% (wet)": a moisture and how wet that is for the type (docs/WATER.md
 * §7.5). */
void item_info_moisture_text(char *out, size_t size, ItemType type, float moisture);

/* An item picture on its well. */
void item_info_picture(App *app, SDL_FRect rect, ItemType type);

/* What one item (`one`: its type, wood and weight) is: weight, room, size,
 * form, hit points of a perfect one (`hit_points`), wood, food and how long
 * it keeps. */
void item_info_traits(UiLayout *layout, ItemStack one, bool hit_points);

/* A row per kind of work the item speeds up as a tool (with a heading); none for
 * what is no tool. */
void item_info_tool_rows(UiLayout *layout, ItemType type);

/* Its attack, hands, damage, pace, accuracy and damage per second as a weapon
 * (with a heading); none for what is no weapon. */
void item_info_weapon_rows(UiLayout *layout, ItemType type);

/* The window of the item selected (App.show_item_info), at the center of the
 * screen; it closes itself when no item is selected or the item is gone. */
void item_info_draw(App *app);

#endif /* COLONY_GAME_ITEM_INFO_H */
