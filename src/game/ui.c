#include "game/ui.h"

#define UI_BUTTON_PADDING 6.0f
#define UI_CHECKBOX_GAP 6.0f
#define UI_ROW_SPACING 4.0f

static const SDL_Color PANEL_COLOR = {12, 14, 18, 200};
static const SDL_Color TEXT_COLOR = {236, 236, 228, 255};
static const SDL_Color BUTTON_COLOR = {54, 60, 72, 255};
static const SDL_Color BUTTON_HOVER_COLOR = {80, 88, 104, 255};
static const SDL_Color BORDER_COLOR = {130, 136, 150, 255};
static const SDL_Color CHECK_COLOR = {120, 210, 110, 255};
static const SDL_Color TRACK_COLOR = {54, 60, 72, 255};
static const SDL_Color HANDLE_COLOR = {200, 204, 214, 255};

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

int ui_take_click(Ui *ui, SDL_FRect rect)
{
    if (ui->click_pending && contains(rect, ui->click_x, ui->click_y)) {
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
    return contains(rect, ui->mouse_x, ui->mouse_y);
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
}

bool ui_is_over_panel(const Ui *ui, float x, float y)
{
    for (int i = 0; i < ui->previous_area_count; i++) {
        if (contains(ui->previous_areas[i], x, y)) {
            return true;
        }
    }
    return false;
}

bool ui_capture_click(Ui *ui, float x, float y, int button)
{
    if (!ui_is_over_panel(ui, x, y)) {
        return false;
    }
    ui->click_pending = true;
    ui->click_button = button;
    ui->click_x = x;
    ui->click_y = y;
    return true;
}

void ui_begin_frame(Ui *ui, float mouse_x, float mouse_y, bool left_button_down)
{
    ui->mouse_x = mouse_x;
    ui->mouse_y = mouse_y;
    ui->mouse_down = left_button_down;
    if (!left_button_down) {
        ui->active_slider = 0; /* Released: dragging is over. */
    }
    ui->area_count = 0;
}

void ui_end_frame(Ui *ui)
{
    SDL_memcpy(ui->previous_areas, ui->areas, sizeof(ui->areas));
    ui->previous_area_count = ui->area_count;
    ui->click_pending = false;
}

void ui_panel(Ui *ui, SDL_FRect rect)
{
    if (ui->area_count < UI_MAX_AREAS) {
        ui->areas[ui->area_count++] = rect;
    }
    fill(ui, rect, PANEL_COLOR);
}

void ui_label(Ui *ui, const char *text, float x, float y)
{
    text_draw(ui->text, text, x, y, TEXT_COLOR);
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

bool ui_button(Ui *ui, SDL_FRect rect, const char *label)
{
    bool hover = contains(rect, ui->mouse_x, ui->mouse_y);
    fill(ui, rect, hover ? BUTTON_HOVER_COLOR : BUTTON_COLOR);
    border(ui, rect, BORDER_COLOR);
    centered_label(ui, rect, label);
    return ui_take_click(ui, rect) == SDL_BUTTON_LEFT;
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
    bool hover = contains(area, ui->mouse_x, ui->mouse_y);
    fill(ui, box, hover ? BUTTON_HOVER_COLOR : BUTTON_COLOR);
    border(ui, box, BORDER_COLOR);
    if (state != UI_UNCHECKED) {
        float inset = state == UI_CHECKED ? 3.0f : SDL_floorf(size * 0.35f);
        SDL_FRect mark = {box.x + inset, box.y + inset, box.w - 2.0f * inset,
                          box.h - 2.0f * inset};
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
    fill(ui, (SDL_FRect){rect.x, center - UI_SLIDER_TRACK_HEIGHT * 0.5f, rect.w,
                         UI_SLIDER_TRACK_HEIGHT},
         TRACK_COLOR);
    float low_x = slider_x_of(rect, steps, *low), high_x = slider_x_of(rect, steps, *high);
    fill(ui, (SDL_FRect){low_x, center - UI_SLIDER_TRACK_HEIGHT * 0.5f, high_x - low_x,
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
