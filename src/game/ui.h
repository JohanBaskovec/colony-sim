/*
 * Minimal immediate-mode UI: panels, buttons and checkboxes drawn every frame.
 *
 * A mouse click (left or right) that lands on a panel drawn during the
 * previous frame is captured (the world does not see it) and handed to the
 * widgets drawn this frame: the first widget containing the click consumes
 * it. Labels are passed already translated. Sliders keep following the mouse
 * while the left button stays down after a click on them (dragging).
 */
#ifndef COLONY_GAME_UI_H
#define COLONY_GAME_UI_H

#include "game/text.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

#define UI_MAX_AREAS 16

typedef enum UiCheck { UI_UNCHECKED, UI_PARTIAL, UI_CHECKED } UiCheck;

typedef struct Ui {
    SDL_Renderer *renderer;
    TextRenderer *text;
    float mouse_x;
    float mouse_y;
    bool mouse_down; /* Left button held. */
    int active_slider; /* Id of the slider being dragged, 0 if none. */
    int active_handle; /* 0: low handle, 1: high handle. */
    bool click_pending;
    int click_button; /* SDL_BUTTON_LEFT or SDL_BUTTON_RIGHT. */
    float click_x;
    float click_y;
    SDL_FRect areas[UI_MAX_AREAS]; /* Panels drawn this frame. */
    int area_count;
    SDL_FRect previous_areas[UI_MAX_AREAS]; /* Panels drawn last frame: they capture clicks. */
    int previous_area_count;
} Ui;

void ui_init(Ui *ui, SDL_Renderer *renderer, TextRenderer *text);

/* Whether a point is over a panel drawn last frame. */
bool ui_is_over_panel(const Ui *ui, float x, float y);

/* Offers a mouse click to the UI: returns true (and keeps it for the widgets)
 * when it lands on a panel, false when it belongs to the world. */
bool ui_capture_click(Ui *ui, float x, float y, int button);

void ui_begin_frame(Ui *ui, float mouse_x, float mouse_y, bool left_button_down);
void ui_end_frame(Ui *ui); /* Unconsumed clicks are dropped. */

/* Panel background; also blocks clicks to the world from the next frame on. */
void ui_panel(Ui *ui, SDL_FRect rect);

void ui_label(Ui *ui, const char *text, float x, float y);
float ui_text_width(Ui *ui, const char *text);

/* Plain rectangle (row backgrounds, scroll bars...). */
void ui_fill(Ui *ui, SDL_FRect rect, SDL_Color color);

/* Whether the mouse is over a rectangle. */
bool ui_hovered(const Ui *ui, SDL_FRect rect);

/* Consumes a pending click inside `rect`: returns its button (SDL_BUTTON_LEFT
 * or SDL_BUTTON_RIGHT), or 0 when there is none. */
int ui_take_click(Ui *ui, SDL_FRect rect);

/* Colored box with a centered label (may be empty), brighter under the mouse.
 * Returns the button it was clicked with this frame, or 0. */
int ui_cell(Ui *ui, SDL_FRect rect, const char *label, SDL_Color color);

/* Width of a button showing `label`. */
float ui_button_width(Ui *ui, const char *label);

/* Draws a button; true when it was left-clicked this frame. */
bool ui_button(Ui *ui, SDL_FRect rect, const char *label);

/* Draws a check box followed by its label (the whole row is clickable); true
 * when it was clicked this frame. */
bool ui_checkbox(Ui *ui, float x, float y, float width, UiCheck state, const char *label);

/* Width of a check box row showing `label`. */
float ui_checkbox_width(Ui *ui, const char *label);

/*
 * Slider selecting a range of steps 0..steps-1 with two handles. A click
 * moves the nearest handle there, and it follows the mouse while the left
 * button is held; the low handle never passes the high one. `id` (non-zero)
 * identifies the slider from one frame to the next. Returns true when *low or
 * *high changed.
 */
bool ui_range_slider(Ui *ui, int id, SDL_FRect rect, int steps, int *low, int *high);

/* Height of one widget row. */
float ui_row_height(const Ui *ui);

#endif /* COLONY_GAME_UI_H */
