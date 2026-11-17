/*
 * Rows laid out twice: once to measure the content, then to draw it in a
 * window sized to fit (game/ui.h). Widgets only exist (and react) in the
 * drawing pass, so a rows function must lay out the same rows both times:
 * apply what the player clicked after the second pass.
 */
#ifndef COLONY_GAME_UI_LAYOUT_H
#define COLONY_GAME_UI_LAYOUT_H

#include "game/ui.h"

#define UI_LAYOUT_GAP 8.0f
#define UI_LAYOUT_INDENT 20.0f

typedef struct UiLayout {
    Ui *ui;
    bool draw;
    float x, y;
    float width; /* Measuring: widest row so far. Drawing: content width. */
    float row, text_offset, button_height;
} UiLayout;

/* Measuring pass: the content is at least this wide. */
void ui_layout_measure(UiLayout *layout, float width);

/* A row of text (dimmer when `dim`), indented. */
void ui_layout_label(UiLayout *layout, const char *text, float indent, bool dim);

/* A meter: the label (when not NULL), then a bar under it, as wide as the
 * content, filled up to `fraction` (0..1) in the color of that point of the
 * scale (red at 0, white in the middle, green at 1) (advances). Returns the
 * rectangle of both, to show a tooltip on. */
SDL_FRect ui_layout_meter(UiLayout *layout, const char *label, float fraction);

/* A table row with a bar: the label on the left and, on the right, a bar of
 * a fixed width (so that the bars line up) filled up to `fraction` (0..1) in
 * the color of that point of the scale (advances). Returns the row's
 * rectangle; it lights up under the mouse when `hoverable`. */
SDL_FRect ui_layout_bar_row(UiLayout *layout, const char *label, float fraction, bool hoverable);

/* A table row: `left` on the left, `right` on the right in `color`
 * (advances). Returns the row's rectangle; when `hoverable`, it lights up
 * under the mouse (the caller shows a tooltip). */
SDL_FRect ui_layout_pair(UiLayout *layout, const char *left, const char *right, SDL_Color color,
                         bool hoverable);

/* ui_layout_pair() with a narrow column of text on the far right, in the same
 * color, `slot` wide (the width of the widest text it may hold) so that the
 * columns line up from row to row. */
SDL_FRect ui_layout_triple(UiLayout *layout, const char *left, const char *middle,
                           const char *right, const char *slot, SDL_Color color, bool hoverable);

/* A button at `x` on the current row (the row does not advance). */
bool ui_layout_button(UiLayout *layout, float x, const char *label);

/* A button of a given width at `x` on the current row. */
bool ui_layout_button_sized(UiLayout *layout, float x, float width, const char *label);

/* A button as wide as the content, on a row of its own with a gap under it
 * (advances). */
bool ui_layout_wide_button(UiLayout *layout, const char *label);

/* Label on the left, button on the right of the same row (advances). */
bool ui_layout_label_with_button(UiLayout *layout, const char *text, const char *label);

typedef void (*UiRowsFn)(UiLayout *layout, void *userdata);

/* Measures `rows`, opens a window that fits them (ui_window_begin()), draws
 * them in it and closes it. */
void ui_layout_window(Ui *ui, int id, const char *title, UiPlacement placement, UiRowsFn rows,
                      void *userdata, bool *open);

/* ui_layout_window() for a docked window (game/ui.h). */
void ui_layout_docked(Ui *ui, int id, const char *title, const UiDock *dock, UiRowsFn rows,
                      void *userdata, bool *open);

#endif /* COLONY_GAME_UI_LAYOUT_H */
