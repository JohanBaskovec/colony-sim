#include "game/ui.h"

#define UI_BUTTON_PADDING 6.0f
#define UI_CHECKBOX_GAP 6.0f
#define UI_ROW_SPACING 4.0f
#define UI_WINDOW_PADDING 8.0f
#define UI_WINDOW_MARGIN 8.0f /* Kept free around windows at the screen edges. */
#define UI_SCROLLBAR_WIDTH 8.0f
#define UI_SCROLL_STEP_ROWS 3.0f /* Rows scrolled per wheel step. */
#define UI_TOOLTIP_PADDING 6.0f
#define UI_TOOLTIP_OFFSET 16.0f
#define UI_TOP_CENTER_Y 72.0f /* Below the status and tool panels. */

static const SDL_Color PANEL_COLOR = {12, 14, 18, 200};
static const SDL_Color WINDOW_COLOR = {16, 18, 24, 242};
static const SDL_Color TITLE_COLOR = {40, 46, 58, 240};
static const SDL_Color TOOLTIP_COLOR = {8, 9, 12, 235};
static const SDL_Color TEXT_COLOR = {236, 236, 228, 255};
static const SDL_Color DIM_TEXT_COLOR = {176, 180, 188, 255};
static const SDL_Color BUTTON_COLOR = {54, 60, 72, 255};
static const SDL_Color BUTTON_HOVER_COLOR = {80, 88, 104, 255};
static const SDL_Color BUTTON_ACTIVE_COLOR = {62, 104, 70, 255};
static const SDL_Color BORDER_COLOR = {130, 136, 150, 255};
static const SDL_Color ACTIVE_BORDER_COLOR = {150, 230, 140, 255};
static const SDL_Color CHECK_COLOR = {120, 210, 110, 255};
static const SDL_Color TRACK_COLOR = {54, 60, 72, 255};
static const SDL_Color HANDLE_COLOR = {200, 204, 214, 255};
static const SDL_Color SCROLL_TRACK_COLOR = {255, 255, 255, 24};
static const SDL_Color SCROLL_THUMB_COLOR = {200, 204, 214, 200};

#define UI_SLIDER_HANDLE_WIDTH 8.0f
#define UI_SLIDER_TRACK_HEIGHT 4.0f

static bool contains(SDL_FRect rect, float x, float y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

static void fill(Ui *ui, SDL_FRect rect, SDL_Color color)
{
    SDL_SetRenderDrawColor(ui->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(ui->renderer, &rect);
}

static SDL_Color brighter(SDL_Color color, int amount)
{
    color.r = (Uint8)SDL_min(255, color.r + amount);
    color.g = (Uint8)SDL_min(255, color.g + amount);
    color.b = (Uint8)SDL_min(255, color.b + amount);
    return color;
}

static void centered_label(Ui *ui, SDL_FRect rect, const char *label)
{
    float text_width = ui_text_width(ui, label);
    float line = (float)text_line_height(ui->text);
    ui_label(ui, label, SDL_floorf(rect.x + (rect.w - text_width) * 0.5f),
             SDL_floorf(rect.y + (rect.h - line) * 0.5f));
}

static void border(Ui *ui, SDL_FRect rect, SDL_Color color)
{
    SDL_SetRenderDrawColor(ui->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderRect(ui->renderer, &rect);
}

/* Whether a point may reach the widgets being drawn: inside the content area
 * of the window being drawn, if any. */
static bool in_clip(const Ui *ui, float x, float y)
{
    return !ui->clipping || contains(ui->clip, x, y);
}

int ui_take_click(Ui *ui, SDL_FRect rect)
{
    if (ui->click_pending && ui->click_owner == ui->owner &&
        contains(rect, ui->click_x, ui->click_y) && in_clip(ui, ui->click_x, ui->click_y)) {
        ui->click_pending = false;
        return ui->click_button;
    }
    return 0;
}

void ui_fill(Ui *ui, SDL_FRect rect, SDL_Color color)
{
    fill(ui, rect, color);
}

bool ui_hovered(const Ui *ui, SDL_FRect rect)
{
    return ui->hover_owner == ui->owner && contains(rect, ui->mouse_x, ui->mouse_y) &&
           in_clip(ui, ui->mouse_x, ui->mouse_y);
}

int ui_cell(Ui *ui, SDL_FRect rect, const char *label, SDL_Color color)
{
    fill(ui, rect, ui_hovered(ui, rect) ? brighter(color, 40) : color);
    border(ui, rect, BORDER_COLOR);
    if (label[0] != '\0') {
        centered_label(ui, rect, label);
    }
    return ui_take_click(ui, rect);
}

void ui_init(Ui *ui, SDL_Renderer *renderer, TextRenderer *text)
{
    SDL_zerop(ui);
    ui->renderer = renderer;
    ui->text = text;
    ui->current_window = -1;
}

/* Owner of the topmost area drawn last frame under a point, 0 if none. */
static int owner_at(const Ui *ui, float x, float y)
{
    for (int i = ui->previous_area_count - 1; i >= 0; i--) {
        if (contains(ui->previous_areas[i].rect, x, y)) {
            return ui->previous_areas[i].owner;
        }
    }
    return 0;
}

static UiWindow *find_window(Ui *ui, int id)
{
    for (int i = 0; i < UI_MAX_WINDOWS; i++) {
        if (id != 0 && ui->windows[i].id == id) {
            return &ui->windows[i];
        }
    }
    return NULL;
}

unsigned ui_window_z(const Ui *ui, int id)
{
    for (int i = 0; i < UI_MAX_WINDOWS; i++) {
        if (id != 0 && ui->windows[i].id == id) {
            return ui->windows[i].was_drawn ? ui->windows[i].z : 0;
        }
    }
    return 0;
}

bool ui_is_over_panel(const Ui *ui, float x, float y)
{
    return owner_at(ui, x, y) != 0;
}

bool ui_capture_click(Ui *ui, float x, float y, int button)
{
    int owner = owner_at(ui, x, y);
    if (ui->popup_id != 0 && owner != UI_POPUP_OWNER) {
        /* A click outside the open list closes it. */
        ui->popup_closed_id = ui->popup_id;
        ui->popup_id = 0;
        if (owner == 0) {
            return true; /* Nothing else happens on the world. */
        }
    }
    if (owner == 0) {
        return false;
    }
    ui->click_pending = true;
    ui->click_button = button;
    ui->click_x = x;
    ui->click_y = y;
    ui->click_owner = owner;
    UiWindow *window = find_window(ui, owner);
    if (window != NULL) {
        window->z = ++ui->next_z; /* To the front. */
    }
    return true;
}

bool ui_scroll(Ui *ui, float x, float y, float steps)
{
    int owner = owner_at(ui, x, y);
    UiWindow *window = find_window(ui, owner);
    if (window != NULL && window->max_scroll > 0.0f) {
        window->scroll -= steps * UI_SCROLL_STEP_ROWS * ui_row_height(ui);
        window->scroll = SDL_clamp(window->scroll, 0.0f, window->max_scroll);
    }
    return owner != 0;
}

void ui_begin_frame(Ui *ui, float mouse_x, float mouse_y, bool left_button_down, float screen_width,
                    float screen_height)
{
    ui->mouse_x = mouse_x;
    ui->mouse_y = mouse_y;
    ui->mouse_down = left_button_down;
    ui->screen_width = screen_width;
    ui->screen_height = screen_height;
    if (!left_button_down) {
        /* Released: dragging is over. */
        ui->active_slider = 0;
        ui->drag_window = 0;
        ui->scroll_drag_window = 0;
    }
    ui->hover_owner = owner_at(ui, mouse_x, mouse_y);
    ui->area_count = 0;
    ui->anonymous_count = 0;
    ui->owner = 0;
    ui->tooltip_count = 0;
    ui->popup_seen = false;
}

void ui_end_frame(Ui *ui)
{
    SDL_memcpy(ui->previous_areas, ui->areas, sizeof(ui->areas));
    ui->previous_area_count = ui->area_count;
    ui->click_pending = false;
    ui->popup_closed_id = 0;
    for (int i = 0; i < UI_MAX_WINDOWS; i++) {
        ui->windows[i].was_drawn = ui->windows[i].drawn;
        ui->windows[i].drawn = false;
    }
}

static void add_area(Ui *ui, SDL_FRect rect, int owner)
{
    if (ui->area_count < UI_MAX_AREAS) {
        ui->areas[ui->area_count++] = (UiArea){rect, owner};
    }
}

void ui_panel(Ui *ui, SDL_FRect rect)
{
    ui->owner = UI_ANONYMOUS_OWNER + ui->anonymous_count++;
    add_area(ui, rect, ui->owner);
    fill(ui, rect, PANEL_COLOR);
}

/* ---- Windows ---------------------------------------------------------------- */

static float title_height(const Ui *ui)
{
    return ui_row_height(ui) + 4.0f;
}

static UiWindow *window_slot(Ui *ui, int id)
{
    UiWindow *window = find_window(ui, id);
    for (int i = 0; window == NULL && i < UI_MAX_WINDOWS; i++) {
        if (ui->windows[i].id == 0) {
            window = &ui->windows[i];
            *window = (UiWindow){.id = id, .x = -1.0f};
        }
    }
    return window;
}

static void place(const Ui *ui, UiWindow *window, UiPlacement placement)
{
    switch (placement) {
    case UI_PLACE_BOTTOM_RIGHT:
        window->x = ui->screen_width - window->w - UI_WINDOW_MARGIN;
        window->y = ui->screen_height - window->h - UI_WINDOW_MARGIN;
        break;
    case UI_PLACE_TOP_CENTER:
        window->x = SDL_floorf((ui->screen_width - window->w) * 0.5f);
        window->y = UI_TOP_CENTER_Y;
        break;
    case UI_PLACE_CENTER:
        window->x = SDL_floorf((ui->screen_width - window->w) * 0.5f);
        window->y = SDL_floorf((ui->screen_height - window->h) * 0.5f);
        break;
    }
}

/* Scroll bar: a click on the thumb drags it, elsewhere on the track jumps there. */
static void handle_scroll_bar(Ui *ui, UiWindow *window)
{
    if (window->max_scroll <= 0.0f) {
        window->scroll = 0.0f;
        window->track.w = 0.0f;
        return;
    }
    SDL_FRect track = window->track;
    float thumb_height = SDL_max(16.0f, SDL_floorf(track.h * window->visible_height /
                                                   (window->visible_height + window->max_scroll)));
    float travel = track.h - thumb_height;
    if (ui_take_click(ui, track) == SDL_BUTTON_LEFT) {
        float thumb_y = track.y + travel * window->scroll / window->max_scroll;
        bool on_thumb = ui->click_y >= thumb_y && ui->click_y < thumb_y + thumb_height;
        ui->scroll_drag_window = window->id;
        ui->scroll_drag_offset = on_thumb ? ui->click_y - thumb_y : thumb_height * 0.5f;
    }
    if (ui->scroll_drag_window == window->id && ui->mouse_down && travel > 0.0f) {
        float thumb_y = ui->mouse_y - ui->scroll_drag_offset;
        window->scroll = (thumb_y - track.y) / travel * window->max_scroll;
    }
    window->scroll = SDL_clamp(window->scroll, 0.0f, window->max_scroll);
    window->thumb =
        (SDL_FRect){track.x, SDL_floorf(track.y + travel * window->scroll / window->max_scroll),
                    track.w, thumb_height};
}

SDL_FPoint ui_window_begin(Ui *ui, int id, const char *title, float content_width,
                           float content_height, UiPlacement placement, bool *open)
{
    UiWindow *window = window_slot(ui, id);
    ui->owner = id;
    if (window == NULL) {
        return (SDL_FPoint){0.0f, 0.0f}; /* Too many windows: drawn, but not as a window. */
    }
    ui->current_window = (int)(window - ui->windows);
    /* Size: the content, unless it is taller than the screen allows. */
    float title_h = title_height(ui);
    float max_visible =
        ui->screen_height - 2.0f * UI_WINDOW_MARGIN - title_h - 2.0f * UI_WINDOW_PADDING;
    window->visible_height = SDL_max(ui_row_height(ui), SDL_min(content_height, max_visible));
    window->max_scroll = SDL_max(0.0f, content_height - window->visible_height);
    float scrollbar = window->max_scroll > 0.0f ? UI_SCROLLBAR_WIDTH + UI_WINDOW_PADDING : 0.0f;
    float close_width = ui_button_width(ui, "×");
    float title_width = ui_text_width(ui, title) + UI_WINDOW_PADDING + close_width;
    float width = SDL_max(content_width, title_width);
    window->w = SDL_ceilf(width + scrollbar + 2.0f * UI_WINDOW_PADDING);
    window->h = SDL_ceilf(title_h + window->visible_height + 2.0f * UI_WINDOW_PADDING);
    if (window->x < 0.0f) {
        place(ui, window, placement);
    }
    if (!window->was_drawn && !window->drawn) {
        window->z = ++ui->next_z; /* Opened (again): to the front. */
    }
    window->drawn = true;

    /* Title bar: × closes, dragging the rest moves the window. */
    SDL_FRect title_bar = {window->x, window->y, window->w, title_h};
    SDL_FRect close = {window->x + window->w - close_width - 2.0f, window->y + 2.0f, close_width,
                       title_h - 4.0f};
    if (ui_take_click(ui, close) == SDL_BUTTON_LEFT && open != NULL) {
        *open = false;
    } else if (ui_take_click(ui, title_bar) == SDL_BUTTON_LEFT) {
        ui->drag_window = id;
        ui->drag_dx = ui->click_x - window->x;
        ui->drag_dy = ui->click_y - window->y;
    }
    if (ui->drag_window == id && ui->mouse_down) {
        window->x = ui->mouse_x - ui->drag_dx;
        window->y = ui->mouse_y - ui->drag_dy;
    }
    window->x = SDL_floorf(SDL_clamp(window->x, 0.0f, SDL_max(0.0f, ui->screen_width - window->w)));
    float max_y = SDL_max(0.0f, ui->screen_height - window->h);
    window->y = SDL_floorf(SDL_clamp(window->y, 0.0f, max_y));

    SDL_FRect frame = {window->x, window->y, window->w, window->h};
    add_area(ui, frame, id);
    fill(ui, frame, WINDOW_COLOR);
    title_bar = (SDL_FRect){window->x, window->y, window->w, title_h};
    fill(ui, title_bar, TITLE_COLOR);
    float line = (float)text_line_height(ui->text);
    ui_label(ui, title, window->x + UI_WINDOW_PADDING,
             SDL_floorf(window->y + (title_h - line) * 0.5f));
    close = (SDL_FRect){window->x + window->w - close_width - 2.0f, window->y + 2.0f, close_width,
                        title_h - 4.0f};
    fill(ui, close, ui_hovered(ui, close) ? BUTTON_HOVER_COLOR : TITLE_COLOR);
    centered_label(ui, close, "×");

    SDL_FRect content = {window->x + UI_WINDOW_PADDING, window->y + title_h + UI_WINDOW_PADDING,
                         window->w - 2.0f * UI_WINDOW_PADDING - scrollbar, window->visible_height};
    window->track = (SDL_FRect){window->x + window->w - UI_WINDOW_PADDING - UI_SCROLLBAR_WIDTH,
                                content.y, UI_SCROLLBAR_WIDTH, content.h};
    handle_scroll_bar(ui, window);

    ui->clipping = true;
    ui->clip = content;
    SDL_Rect clip = {(int)content.x, (int)content.y, (int)SDL_ceilf(content.w),
                     (int)SDL_ceilf(content.h)};
    SDL_SetRenderClipRect(ui->renderer, &clip);
    return (SDL_FPoint){content.x, content.y - SDL_floorf(window->scroll)};
}

float ui_window_content_width(const Ui *ui)
{
    return ui->clipping ? ui->clip.w : 0.0f;
}

void ui_window_end(Ui *ui)
{
    if (ui->clipping) {
        SDL_SetRenderClipRect(ui->renderer, NULL);
        ui->clipping = false;
    }
    if (ui->current_window >= 0) {
        UiWindow *window = &ui->windows[ui->current_window];
        if (window->track.w > 0.0f) {
            fill(ui, window->track, SCROLL_TRACK_COLOR);
            fill(ui, window->thumb, SCROLL_THUMB_COLOR);
        }
    }
    ui->current_window = -1;
}

void ui_label(Ui *ui, const char *text, float x, float y)
{
    text_draw(ui->text, text, x, y, TEXT_COLOR);
}

void ui_label_dim(Ui *ui, const char *text, float x, float y)
{
    text_draw(ui->text, text, x, y, DIM_TEXT_COLOR);
}

float ui_text_width(Ui *ui, const char *text)
{
    int width = 0, height = 0;
    text_measure(ui->text, text, &width, &height);
    return (float)width;
}

float ui_row_height(const Ui *ui)
{
    return (float)text_line_height(ui->text) + UI_ROW_SPACING;
}

float ui_button_width(Ui *ui, const char *label)
{
    return ui_text_width(ui, label) + 2.0f * UI_BUTTON_PADDING;
}

/* Button background and border; true when it was left-clicked this frame. */
static bool button_frame(Ui *ui, SDL_FRect rect, bool active)
{
    bool hover = ui_hovered(ui, rect);
    SDL_Color color = active ? BUTTON_ACTIVE_COLOR : BUTTON_COLOR;
    fill(ui, rect, hover ? brighter(color, 26) : color);
    border(ui, rect, active ? ACTIVE_BORDER_COLOR : BORDER_COLOR);
    return ui_take_click(ui, rect) == SDL_BUTTON_LEFT;
}

bool ui_button(Ui *ui, SDL_FRect rect, const char *label)
{
    return ui_toggle_button(ui, rect, label, false);
}

bool ui_toggle_button(Ui *ui, SDL_FRect rect, const char *label, bool active)
{
    bool clicked = button_frame(ui, rect, active);
    centered_label(ui, rect, label);
    return clicked;
}

bool ui_icon_button(Ui *ui, SDL_FRect rect, bool active)
{
    return button_frame(ui, rect, active);
}

bool ui_disclosure(Ui *ui, SDL_FRect rect, bool open)
{
    bool clicked = button_frame(ui, rect, false);
    centered_label(ui, rect, open ? "▾" : "▸");
    return clicked;
}

/* Room for the arrow of a dropdown, after its label. */
static float dropdown_arrow_width(Ui *ui)
{
    return ui_text_width(ui, "▾") + UI_BUTTON_PADDING;
}

float ui_dropdown_width(Ui *ui, const char *const *options, int count)
{
    float widest = 0.0f;
    for (int i = 0; i < count; i++) {
        widest = SDL_max(widest, ui_text_width(ui, options[i]));
    }
    return SDL_ceilf(widest) + 2.0f * UI_BUTTON_PADDING + dropdown_arrow_width(ui);
}

int ui_dropdown(Ui *ui, int id, SDL_FRect rect, const char *const *options, int count, int selected)
{
    int picked = -1;
    if (ui->picked_id == id) {
        picked = ui->picked_option;
        ui->picked_id = 0;
    }
    count = SDL_clamp(count, 0, UI_POPUP_OPTIONS);
    bool open = ui->popup_id == id;
    bool clicked = button_frame(ui, rect, open);
    float line = (float)text_line_height(ui->text);
    float text_y = SDL_floorf(rect.y + (rect.h - line) * 0.5f);
    if (selected >= 0 && selected < count) {
        ui_label(ui, options[selected], rect.x + UI_BUTTON_PADDING, text_y);
    }
    ui_label_dim(ui, "▾", rect.x + rect.w - dropdown_arrow_width(ui), text_y);
    if (clicked && open) {
        ui->popup_id = 0;
    } else if (clicked && ui->popup_closed_id != id) {
        ui->popup_id = id;
        ui->popup_count = count;
        ui->popup_selected = selected;
        for (int i = 0; i < count; i++) {
            SDL_strlcpy(ui->popup_options[i], options[i], UI_POPUP_OPTION_SIZE);
        }
    }
    if (ui->popup_id == id) {
        ui->popup_seen = true;
        ui->popup_anchor = rect; /* Follows the window when it moves or scrolls. */
    }
    return picked;
}

void ui_draw_popup(Ui *ui)
{
    if (ui->popup_id == 0) {
        return;
    }
    if (!ui->popup_seen) {
        ui->popup_id = 0; /* Its window is gone. */
        return;
    }
    float row = ui_row_height(ui);
    float width = ui->popup_anchor.w;
    for (int i = 0; i < ui->popup_count; i++) {
        width = SDL_max(width, ui_text_width(ui, ui->popup_options[i]) + 2.0f * UI_BUTTON_PADDING);
    }
    width = SDL_ceilf(width);
    float height = row * (float)ui->popup_count;
    float x = SDL_min(ui->popup_anchor.x, ui->screen_width - width);
    float y = ui->popup_anchor.y + ui->popup_anchor.h;
    if (y + height > ui->screen_height) {
        y = ui->popup_anchor.y - height; /* Above the button. */
    }
    SDL_FRect box = {SDL_max(0.0f, x), SDL_max(0.0f, y), width, height};
    ui->owner = UI_POPUP_OWNER;
    add_area(ui, box, UI_POPUP_OWNER);
    fill(ui, box, WINDOW_COLOR);
    float line = (float)text_line_height(ui->text);
    int picked = -1;
    for (int i = 0; i < ui->popup_count; i++) {
        SDL_FRect item = {box.x, box.y + row * (float)i, box.w, row};
        if (ui_hovered(ui, item)) {
            fill(ui, item, BUTTON_HOVER_COLOR);
        } else if (i == ui->popup_selected) {
            fill(ui, item, BUTTON_ACTIVE_COLOR);
        }
        ui_label(ui, ui->popup_options[i], item.x + UI_BUTTON_PADDING,
                 SDL_floorf(item.y + (row - line) * 0.5f));
        if (ui_take_click(ui, item) == SDL_BUTTON_LEFT) {
            picked = i;
        }
    }
    border(ui, box, BORDER_COLOR);
    if (picked >= 0) {
        ui->picked_id = ui->popup_id;
        ui->picked_option = picked;
        ui->popup_id = 0;
    }
}

float ui_checkbox_width(Ui *ui, const char *label)
{
    return SDL_floorf(ui_row_height(ui) * 0.6f) + UI_CHECKBOX_GAP + ui_text_width(ui, label);
}

bool ui_checkbox(Ui *ui, float x, float y, float width, UiCheck state, const char *label)
{
    float row = ui_row_height(ui);
    float size = SDL_floorf(row * 0.6f);
    SDL_FRect area = {x, y, width, row};
    SDL_FRect box = {x, SDL_floorf(y + (row - size) * 0.5f), size, size};
    bool hover = ui_hovered(ui, area);
    fill(ui, box, hover ? BUTTON_HOVER_COLOR : BUTTON_COLOR);
    border(ui, box, BORDER_COLOR);
    if (state != UI_UNCHECKED) {
        float inset = state == UI_CHECKED ? 3.0f : SDL_floorf(size * 0.35f);
        SDL_FRect mark = {box.x + inset, box.y + inset, box.w - 2.0f * inset, box.h - 2.0f * inset};
        fill(ui, mark, CHECK_COLOR);
    }
    float line = (float)text_line_height(ui->text);
    ui_label(ui, label, x + size + UI_CHECKBOX_GAP, SDL_floorf(y + (row - line) * 0.5f));
    return ui_take_click(ui, area) == SDL_BUTTON_LEFT;
}

/* Step under a horizontal position, steps spread evenly between the two ends
 * of the track (inset by half a handle). */
static int slider_step_at(SDL_FRect rect, int steps, float x)
{
    float left = rect.x + UI_SLIDER_HANDLE_WIDTH * 0.5f;
    float span = rect.w - UI_SLIDER_HANDLE_WIDTH;
    if (steps <= 1 || span <= 0.0f) {
        return 0;
    }
    int step = (int)SDL_roundf((x - left) / span * (float)(steps - 1));
    return SDL_clamp(step, 0, steps - 1);
}

static float slider_x_of(SDL_FRect rect, int steps, int step)
{
    float span = rect.w - UI_SLIDER_HANDLE_WIDTH;
    float t = steps > 1 ? (float)step / (float)(steps - 1) : 0.0f;
    return SDL_floorf(rect.x + UI_SLIDER_HANDLE_WIDTH * 0.5f + span * t);
}

bool ui_range_slider(Ui *ui, int id, SDL_FRect rect, int steps, int *low, int *high)
{
    int old_low = *low, old_high = *high;
    float target_x = 0.0f;
    bool moving = false;
    if (ui_take_click(ui, rect) == SDL_BUTTON_LEFT) {
        int step = slider_step_at(rect, steps, ui->click_x);
        /* The nearest handle; between equal handles, the side clicked on. */
        int to_low = SDL_abs(step - *low), to_high = SDL_abs(step - *high);
        ui->active_slider = id;
        ui->active_handle = to_low < to_high || (to_low == to_high && step < *low) ? 0 : 1;
        target_x = ui->click_x;
        moving = true; /* Even if the button was already released. */
    }
    if (ui->active_slider == id && ui->mouse_down) {
        target_x = ui->mouse_x;
        moving = true;
    }
    if (moving) {
        int step = slider_step_at(rect, steps, target_x);
        if (ui->active_handle == 0) {
            *low = SDL_min(step, *high);
        } else {
            *high = SDL_max(step, *low);
        }
    }

    float center = SDL_floorf(rect.y + rect.h * 0.5f);
    fill(
        ui,
        (SDL_FRect){rect.x, center - UI_SLIDER_TRACK_HEIGHT * 0.5f, rect.w, UI_SLIDER_TRACK_HEIGHT},
        TRACK_COLOR);
    float low_x = slider_x_of(rect, steps, *low), high_x = slider_x_of(rect, steps, *high);
    fill(ui,
         (SDL_FRect){low_x, center - UI_SLIDER_TRACK_HEIGHT * 0.5f, high_x - low_x,
                     UI_SLIDER_TRACK_HEIGHT},
         CHECK_COLOR);
    bool hover = ui_hovered(ui, rect) || ui->active_slider == id;
    SDL_Color handle = hover ? brighter(HANDLE_COLOR, 40) : HANDLE_COLOR;
    float handle_height = SDL_floorf(rect.h * 0.8f);
    for (int i = 0; i < 2; i++) {
        float hx = i == 0 ? low_x : high_x;
        SDL_FRect knob = {hx - UI_SLIDER_HANDLE_WIDTH * 0.5f, center - handle_height * 0.5f,
                          UI_SLIDER_HANDLE_WIDTH, handle_height};
        fill(ui, knob, handle);
        border(ui, knob, BORDER_COLOR);
    }
    return *low != old_low || *high != old_high;
}

/* ---- Tooltips --------------------------------------------------------------- */

void ui_tooltip(Ui *ui, const char *const *lines, int count)
{
    ui->tooltip_count = SDL_min(count, UI_TOOLTIP_LINES);
    for (int i = 0; i < ui->tooltip_count; i++) {
        SDL_strlcpy(ui->tooltip[i], lines[i], UI_TOOLTIP_LINE_SIZE);
    }
}

void ui_draw_tooltip(Ui *ui)
{
    if (ui->tooltip_count == 0) {
        return;
    }
    float line = (float)text_line_height(ui->text);
    float width = 0.0f;
    for (int i = 0; i < ui->tooltip_count; i++) {
        width = SDL_max(width, ui_text_width(ui, ui->tooltip[i]));
    }
    width = SDL_ceilf(width) + 2.0f * UI_TOOLTIP_PADDING;
    float height = line * (float)ui->tooltip_count + 2.0f * UI_TOOLTIP_PADDING;
    /* Above and to the right of the mouse, kept on the screen. */
    float x = ui->mouse_x + UI_TOOLTIP_OFFSET;
    float y = ui->mouse_y - UI_TOOLTIP_OFFSET - height;
    if (x + width > ui->screen_width) {
        x = ui->mouse_x - UI_TOOLTIP_OFFSET - width;
    }
    if (y < 0.0f) {
        y = ui->mouse_y + UI_TOOLTIP_OFFSET;
    }
    SDL_FRect box = {SDL_floorf(SDL_max(0.0f, x)), SDL_floorf(y), width, height};
    fill(ui, box, TOOLTIP_COLOR);
    border(ui, box, BORDER_COLOR);
    for (int i = 0; i < ui->tooltip_count; i++) {
        /* The first line is the title, the others are details. */
        float ty = box.y + UI_TOOLTIP_PADDING + line * (float)i;
        if (i == 0) {
            ui_label(ui, ui->tooltip[i], box.x + UI_TOOLTIP_PADDING, ty);
        } else {
            ui_label_dim(ui, ui->tooltip[i], box.x + UI_TOOLTIP_PADDING, ty);
        }
    }
}
