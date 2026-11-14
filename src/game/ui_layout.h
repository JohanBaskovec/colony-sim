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

/* A button at `x` on the current row (the row does not advance). */
bool ui_layout_button(UiLayout *layout, float x, const char *label);

/* A button of a given width at `x` on the current row. */
bool ui_layout_button_sized(UiLayout *layout, float x, float width, const char *label);

/* Label on the left, button on the right of the same row (advances). */
bool ui_layout_label_with_button(UiLayout *layout, const char *text, const char *label);

typedef void (*UiRowsFn)(UiLayout *layout, void *userdata);

/* Measures `rows`, opens a window that fits them (ui_window_begin()), draws
 * them in it and closes it. */
void ui_layout_window(Ui *ui, int id, const char *title, UiPlacement placement, UiRowsFn rows,
                      void *userdata, bool *open);

#endif /* COLONY_GAME_UI_LAYOUT_H */
