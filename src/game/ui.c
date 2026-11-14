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

static bool contains(SDL_FRect rect, float x, float y)
{
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

static void fill(Ui *ui, SDL_FRect rect, SDL_Color color)
{
    SDL_SetRenderDrawColor(ui->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(ui->renderer, &rect);
}

static void border(Ui *ui, SDL_FRect rect, SDL_Color color)
{
    SDL_SetRenderDrawColor(ui->renderer, color.r, color.g, color.b, color.a);
    SDL_RenderRect(ui->renderer, &rect);
}

/* Consumes the pending click if it lies inside `rect`. */
static bool take_click(Ui *ui, SDL_FRect rect)
{
    if (ui->click_pending && contains(rect, ui->click_x, ui->click_y)) {
        ui->click_pending = false;
        return true;
    }
    return false;
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

bool ui_capture_click(Ui *ui, float x, float y)
{
    if (!ui_is_over_panel(ui, x, y)) {
        return false;
    }
    ui->click_pending = true;
    ui->click_x = x;
    ui->click_y = y;
    return true;
}

void ui_begin_frame(Ui *ui, float mouse_x, float mouse_y)
{
    ui->mouse_x = mouse_x;
    ui->mouse_y = mouse_y;
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
    float text_width = ui_text_width(ui, label);
    float line = (float)text_line_height(ui->text);
    ui_label(ui, label, SDL_floorf(rect.x + (rect.w - text_width) * 0.5f),
             SDL_floorf(rect.y + (rect.h - line) * 0.5f));
    return take_click(ui, rect);
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
    return take_click(ui, area);
}
