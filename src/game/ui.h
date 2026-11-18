/*
 * Minimal immediate-mode UI: panels, windows, buttons, check boxes, sliders,
 * dropdowns and tooltips, drawn every frame.
 *
 * Everything drawn belongs to an owner: a window (its id) or a plain panel.
 * A mouse click (left or right) that lands on a panel or window drawn during
 * the previous frame is captured (the world does not see it) and handed to
 * the widgets of the topmost owner under it: the first of its widgets
 * containing the click consumes it. Only that owner's widgets light up under
 * the mouse. Labels are passed already translated.
 *
 * Windows have a title bar (drag it to move the window, × to close it), are
 * sized to their content, and scroll (mouse wheel or scroll bar) when the
 * content is taller than the screen allows. The UI remembers their position
 * and stacking order from one frame to the next: a window clicked comes to
 * the front, and windows must be drawn in their stacking order (see
 * ui_window_z()). Sliders and scroll bars keep following the mouse while the
 * left button stays down after a click on them (dragging).
 *
 * A dropdown opens a list of options over everything else, drawn at the end
 * of the frame (ui_draw_popup()); the list belongs to an owner of its own,
 * so that it captures the clicks on it. An option picked is returned by the
 * dropdown at the next frame; a list too long for the screen scrolls with the
 * mouse wheel. A click anywhere else closes the list (a click on the world
 * does nothing else then).
 */
#ifndef COLONY_GAME_UI_H
#define COLONY_GAME_UI_H

#include "game/text.h"
#include "game/theme.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

#define UI_MAX_AREAS 32
#define UI_MAX_WINDOWS 16
#define UI_TOOLTIP_LINES 10
/* A tooltip line holds a whole translated text (a description is a few
 * sentences); tests/test_i18n.c keeps every translation within TRANSLATION_MAX_BYTES. */
#define UI_TOOLTIP_LINE_SIZE 512
#define UI_TOOLTIP_WRAPPED_LINES 24
#define UI_WRAP_LINE_SIZE 256
/* Window ids are 1 .. UI_POPUP_OWNER - 1. */
#define UI_ANONYMOUS_OWNER 1000
#define UI_POPUP_OWNER (UI_ANONYMOUS_OWNER - 1) /* The open dropdown list. */
#define UI_POPUP_OPTIONS 80 /* A longer list scrolls (wheel) when it does not fit the screen. */
#define UI_POPUP_OPTION_SIZE 96

typedef enum UiCheck { UI_UNCHECKED, UI_PARTIAL, UI_CHECKED } UiCheck;

/* Where a window first appears (the player may move it afterwards). */
typedef enum UiPlacement {
    UI_PLACE_TOP_CENTER,
    UI_PLACE_CENTER,
} UiPlacement;

/* Where a docked window goes: its bottom left corner at (x, bottom), as tall
 * as its content up to `top` (it scrolls beyond), at least `width` wide and
 * `height` tall (0: no minimum; the room is left empty, so a `height` of
 * `bottom - top` makes the window as tall as it can be). It cannot be
 * dragged. */
typedef struct UiDock {
    float x, bottom, top, width, height;
} UiDock;

typedef struct UiArea {
    SDL_FRect rect;
    int owner;
} UiArea;

typedef struct UiWindow {
    int id;         /* 0: free slot. */
    float x, y;     /* Top left corner. */
    float w, h;     /* Size at the last frame. */
    unsigned z;     /* Higher: in front. */
    bool drawn;     /* This frame. */
    bool was_drawn; /* Last frame: a window opened again comes to the front. */
    float scroll;   /* Content pixels scrolled out at the top. */
    float max_scroll;
    float visible_height; /* Of the content area. */
    SDL_FRect track;      /* Scroll bar (w = 0: none). */
    SDL_FRect thumb;
} UiWindow;

typedef struct Ui {
    SDL_Renderer *renderer;
    TextRenderer *text;
    Theme theme;
    float screen_width;
    float screen_height;
    float reserved_bottom; /* Kept free of windows at the bottom (the bottom bar). */
    float mouse_x;
    float mouse_y;
    bool mouse_down;   /* Left button held. */
    int active_slider; /* Id of the slider being dragged, 0 if none. */
    int active_handle; /* 0: low handle, 1: high handle. */
    bool click_pending;
    int click_button; /* SDL_BUTTON_LEFT or SDL_BUTTON_RIGHT. */
    float click_x;
    float click_y;
    int click_owner;     /* Topmost owner under the pending click. */
    int hover_owner;     /* Topmost owner under the mouse (last frame's areas), 0: none. */
    int owner;           /* Owner of what is being drawn now. */
    int anonymous_count; /* Plain panels drawn this frame. */
    bool clipping;
    SDL_FRect clip;             /* Content area of the window being drawn. */
    UiArea areas[UI_MAX_AREAS]; /* Drawn this frame, back to front. */
    int area_count;
    UiArea previous_areas[UI_MAX_AREAS]; /* Drawn last frame: they capture clicks. */
    int previous_area_count;
    UiWindow windows[UI_MAX_WINDOWS];
    unsigned next_z;
    int current_window; /* Index in `windows` between begin and end, -1 otherwise. */
    int drag_window;    /* Id of the window moved by its title bar, 0 if none. */
    float drag_dx, drag_dy;
    int scroll_drag_window; /* Id of the window whose scroll thumb is dragged, 0 if none. */
    float scroll_drag_offset;
    char tooltip[UI_TOOLTIP_LINES][UI_TOOLTIP_LINE_SIZE];
    int tooltip_count;
    /* The open dropdown list (popup_id 0: none), below its button. */
    int popup_id;
    SDL_FRect popup_anchor;
    char popup_options[UI_POPUP_OPTIONS][UI_POPUP_OPTION_SIZE];
    int popup_count;
    int popup_selected;
    int popup_scroll;    /* First option shown (-1: keep the selected one in view). */
    int popup_visible;   /* Options shown at the last frame. */
    bool popup_seen;     /* Its dropdown was drawn this frame (else it closes). */
    int popup_closed_id; /* Closed by this frame's click: that click does not open it again. */
    int picked_id;       /* Dropdown an option was picked from, returned the next frame. */
    int picked_option;
} Ui;

void ui_init(Ui *ui, SDL_Renderer *renderer, TextRenderer *text, const Theme *theme);

/* Another scale or palette (the font size changed with it, see
 * text_renderer_set_size()); at another scale, windows are placed again
 * (centered...). */
void ui_set_theme(Ui *ui, const Theme *theme);

/* Whether a point is over a panel or window drawn last frame. */
bool ui_is_over_panel(const Ui *ui, float x, float y);

/* Offers a mouse click to the UI: returns true (and keeps it for the widgets)
 * when it lands on a panel or window, false when it belongs to the world. A
 * window clicked comes to the front. */
bool ui_capture_click(Ui *ui, float x, float y, int button);

/* Offers mouse wheel steps (positive: up) to the UI: scrolls the window
 * under the point if it can scroll. Returns true when the point is over a
 * panel or window (the world does not see the wheel then). */
bool ui_scroll(Ui *ui, float x, float y, float steps);

void ui_begin_frame(Ui *ui, float mouse_x, float mouse_y, bool left_button_down, float screen_width,
                    float screen_height);
void ui_end_frame(Ui *ui); /* Unconsumed clicks are dropped. */

/* Panel background; also blocks clicks to the world from the next frame on.
 * Widgets drawn after it belong to it. */
void ui_panel(Ui *ui, SDL_FRect rect);

/* ui_panel() in another color of the theme. */
void ui_panel_colored(Ui *ui, SDL_FRect rect, ThemeColor color);

/*
 * (A NULL `title` opens a window without a title bar: its × sits at the top
 * right, with a strip of its own at the right of the content.)
 *
 * Opens a window of `content_width` x `content_height` pixels of content:
 * draws its frame and title bar, handles moving, closing (sets *open to false
 * when × is clicked) and scrolling, and clips what follows to the content
 * area until ui_window_end(). Returns the top left corner of the content,
 * scrolling applied: draw the content from there.
 */
SDL_FPoint ui_window_begin(Ui *ui, int id, const char *title, float content_width,
                           float content_height, UiPlacement placement, bool *open);
void ui_window_end(Ui *ui);

/* ui_window_begin() for a docked window. */
SDL_FPoint ui_window_begin_docked(Ui *ui, int id, const char *title, float content_width,
                                  float content_height, const UiDock *dock, bool *open);

/* Frame of a window as last drawn (empty if it never was). */
SDL_FRect ui_window_rect(const Ui *ui, int id);

/* Width of the content area of the window being drawn (at least the width
 * asked for: the title may make the window wider). */
float ui_window_content_width(const Ui *ui);

/* Stacking order of a window (higher: in front), 0 if it was not shown last
 * frame (it will come to the front when drawn). */
unsigned ui_window_z(const Ui *ui, int id);

void ui_label(Ui *ui, const char *text, float x, float y);

/* Label in a dimmer color, for secondary information. */
void ui_label_dim(Ui *ui, const char *text, float x, float y);

/* Label in a color of the theme. */
void ui_label_colored(Ui *ui, const char *text, float x, float y, ThemeColor color);

/* Label in any color (a value on a scale, theme_scale_color()). */
void ui_label_rgba(Ui *ui, const char *text, float x, float y, SDL_Color color);
float ui_text_width(Ui *ui, const char *text);

/* Plain rectangle (row backgrounds...). */
void ui_fill(Ui *ui, SDL_FRect rect, SDL_Color color);

/* Whether the mouse is over a rectangle of the current owner (and inside the
 * window's content area when drawing a window). */
bool ui_hovered(const Ui *ui, SDL_FRect rect);

/* Consumes a pending click inside `rect`, for the current owner: returns its
 * button (SDL_BUTTON_LEFT or SDL_BUTTON_RIGHT), or 0 when there is none. */
int ui_take_click(Ui *ui, SDL_FRect rect);

/* Colored box with a centered label (may be empty), brighter under the mouse.
 * Returns the button it was clicked with this frame, or 0. */
int ui_cell(Ui *ui, SDL_FRect rect, const char *label, SDL_Color color);

/* Width of a button showing `label`. */
float ui_button_width(Ui *ui, const char *label);

/* Draws a button; true when it was left-clicked this frame. */
bool ui_button(Ui *ui, SDL_FRect rect, const char *label);

/* A button that stays lit while `active` (open menu, tool in use). */
bool ui_toggle_button(Ui *ui, SDL_FRect rect, const char *label, bool active);

/* An empty button frame for an icon the caller draws inside; lit while
 * `active`. True when it was left-clicked this frame. */
bool ui_icon_button(Ui *ui, SDL_FRect rect, bool active);

/* A small triangle button, pointing down when `open` (a section shown), right
 * otherwise; true when it was left-clicked this frame. */
bool ui_disclosure(Ui *ui, SDL_FRect rect, bool open);

/* Width of a dropdown showing the widest of its options. */
float ui_dropdown_width(Ui *ui, const char *const *options, int count);

/*
 * Dropdown showing options[selected]: a click opens the list of the `count`
 * options (at most UI_POPUP_OPTIONS) below it, or closes it. Returns the
 * option picked in the list (at the frame after the click), -1 otherwise.
 * `id` (non-zero) tells the dropdowns drawn in a frame apart.
 */
int ui_dropdown(Ui *ui, int id, SDL_FRect rect, const char *const *options, int count,
                int selected);

/* Draws the open dropdown list, if any, over everything else but the
 * tooltip; call it at the end of the frame, before ui_draw_tooltip(). */
void ui_draw_popup(Ui *ui);

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

/*
 * Slider choosing a step 0..steps-1 along a vertical track, 0 at the bottom.
 * A click moves the handle there, and it follows the mouse while the left
 * button is held. A tick marks step `mark` (-1: none). `id` (non-zero)
 * identifies the slider from one frame to the next. Returns true when
 * *value changed.
 */
bool ui_vertical_slider(Ui *ui, int id, SDL_FRect rect, int steps, int mark, int *value);

/* Height of one widget row. */
float ui_row_height(const Ui *ui);

/* Shows these lines next to the mouse at the end of the frame (call it while
 * a widget is hovered; the last call of the frame wins). */
void ui_tooltip(Ui *ui, const char *const *lines, int count);

/* `text` cut short with an ellipsis to be no wider than `width` (whole
 * when it fits). */
void ui_fit_text(Ui *ui, const char *text, float width, char *out, size_t size);

/* Splits `text` at spaces into lines no wider than `width` (a word wider
 * than that keeps a line of its own), at most `max_lines`. Non-breaking
 * spaces do not split. Returns the number of lines. */
int ui_wrap_text(Ui *ui, const char *text, float width, char (*lines)[UI_WRAP_LINE_SIZE],
                 int max_lines);

/* Draws the tooltip asked for this frame (long lines wrapped), over
 * everything else. */
void ui_draw_tooltip(Ui *ui);

#endif /* COLONY_GAME_UI_H */
