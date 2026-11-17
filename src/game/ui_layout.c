#include "game/ui_layout.h"

void ui_layout_measure(UiLayout *layout, float width)
{
    if (!layout->draw) {
        layout->width = SDL_max(layout->width, width);
    }
}

void ui_layout_label(UiLayout *layout, const char *text, float indent, bool dim)
{
    ui_layout_measure(layout, indent + ui_text_width(layout->ui, text));
    if (layout->draw) {
        float y = layout->y + layout->text_offset;
        if (dim) {
            ui_label_dim(layout->ui, text, layout->x + indent, y);
        } else {
            ui_label(layout->ui, text, layout->x + indent, y);
        }
    }
    layout->y += layout->row;
}

SDL_FRect ui_layout_meter(UiLayout *layout, const char *label, float fraction)
{
    const Theme *theme = &layout->ui->theme;
    float bar_height = SDL_max(4.0f, SDL_roundf(layout->row * 0.3f));
    float height = (label != NULL ? layout->row - layout->text_offset : 0.0f) + bar_height;
    ui_layout_measure(layout, SDL_max(label != NULL ? ui_text_width(layout->ui, label) : 0.0f,
                                      theme->card_width));
    SDL_FRect block = {layout->x, layout->y, layout->width, height};
    if (layout->draw) {
        if (label != NULL) {
            ui_label(layout->ui, label, layout->x, layout->y + layout->text_offset);
        }
        SDL_FRect track = {layout->x, layout->y + height - bar_height, layout->width, bar_height};
        ui_fill(layout->ui, track, theme->colors[THEME_TRACK]);
        float filled = SDL_roundf(track.w * SDL_clamp(fraction, 0.0f, 1.0f));
        ui_fill(layout->ui, (SDL_FRect){track.x, track.y, filled, track.h},
                theme_scale_color(theme, fraction));
    }
    layout->y += height + theme->gap * 1.5f;
    return block;
}

SDL_FRect ui_layout_bar_row(UiLayout *layout, const char *label, float fraction, bool hoverable)
{
    const Theme *theme = &layout->ui->theme;
    float bar_width = SDL_roundf(theme->inspector_width * 0.5f);
    ui_layout_measure(layout, ui_text_width(layout->ui, label) + UI_LAYOUT_GAP * 2.0f + bar_width);
    SDL_FRect rect = {layout->x, layout->y, layout->width, layout->row};
    if (layout->draw) {
        if (hoverable && ui_hovered(layout->ui, rect)) {
            ui_fill(layout->ui, rect, theme->colors[THEME_SCROLL_TRACK]);
        }
        ui_label(layout->ui, label, layout->x, layout->y + layout->text_offset);
        float height = SDL_max(4.0f, SDL_roundf(layout->row * 0.4f));
        SDL_FRect track = {layout->x + layout->width - bar_width,
                           SDL_roundf(layout->y + (layout->row - height) * 0.5f), bar_width,
                           height};
        ui_fill(layout->ui, track, theme->colors[THEME_TRACK]);
        float filled = SDL_roundf(track.w * SDL_clamp(fraction, 0.0f, 1.0f));
        ui_fill(layout->ui, (SDL_FRect){track.x, track.y, filled, track.h},
                theme_scale_color(theme, fraction));
    }
    layout->y += layout->row;
    return rect;
}

SDL_FRect ui_layout_pair(UiLayout *layout, const char *left, const char *right, SDL_Color color,
                         bool hoverable)
{
    float right_width = ui_text_width(layout->ui, right);
    ui_layout_measure(layout, ui_text_width(layout->ui, left) + UI_LAYOUT_GAP * 2.0f + right_width);
    SDL_FRect rect = {layout->x, layout->y, layout->width, layout->row};
    if (layout->draw) {
        if (hoverable && ui_hovered(layout->ui, rect)) {
            ui_fill(layout->ui, rect, layout->ui->theme.colors[THEME_SCROLL_TRACK]);
        }
        float y = layout->y + layout->text_offset;
        ui_label(layout->ui, left, layout->x, y);
        ui_label_rgba(layout->ui, right, layout->x + layout->width - right_width, y, color);
    }
    layout->y += layout->row;
    return rect;
}

SDL_FRect ui_layout_triple(UiLayout *layout, const char *left, const char *middle,
                           const char *right, const char *slot, SDL_Color color, bool hoverable)
{
    float slot_width = ui_text_width(layout->ui, slot);
    ui_layout_measure(layout, ui_text_width(layout->ui, left) + ui_text_width(layout->ui, middle) +
                                  slot_width + UI_LAYOUT_GAP * 3.0f);
    SDL_FRect rect = {layout->x, layout->y, layout->width, layout->row};
    if (layout->draw) {
        if (hoverable && ui_hovered(layout->ui, rect)) {
            ui_fill(layout->ui, rect, layout->ui->theme.colors[THEME_SCROLL_TRACK]);
        }
        float y = layout->y + layout->text_offset;
        float right_edge = layout->x + layout->width;
        ui_label(layout->ui, left, layout->x, y);
        ui_label_rgba(layout->ui, right, right_edge - ui_text_width(layout->ui, right), y, color);
        right_edge -= slot_width + UI_LAYOUT_GAP;
        ui_label_rgba(layout->ui, middle, right_edge - ui_text_width(layout->ui, middle), y, color);
    }
    layout->y += layout->row;
    return rect;
}

bool ui_layout_button_sized(UiLayout *layout, float x, float width, const char *label)
{
    return layout->draw &&
           ui_button(layout->ui, (SDL_FRect){x, layout->y, width, layout->button_height}, label);
}

bool ui_layout_button(UiLayout *layout, float x, const char *label)
{
    return ui_layout_button_sized(layout, x, ui_button_width(layout->ui, label), label);
}

bool ui_layout_wide_button(UiLayout *layout, const char *label)
{
    ui_layout_measure(layout, ui_button_width(layout->ui, label));
    bool clicked = ui_layout_button_sized(layout, layout->x, layout->width, label);
    layout->y += layout->row + layout->ui->theme.gap;
    return clicked;
}

bool ui_layout_label_with_button(UiLayout *layout, const char *text, const char *label)
{
    float button_width = ui_button_width(layout->ui, label);
    ui_layout_measure(layout, ui_text_width(layout->ui, text) + UI_LAYOUT_GAP + button_width);
    bool clicked = ui_layout_button(layout, layout->x + layout->width - button_width, label);
    if (layout->draw) {
        ui_label(layout->ui, text, layout->x, layout->y + layout->text_offset);
    }
    layout->y += layout->row;
    return clicked;
}

/* ui_layout_window(), docked when `dock` is not NULL. */
static void layout_window(Ui *ui, int id, const char *title, UiPlacement placement,
                          const UiDock *dock, UiRowsFn rows, void *userdata, bool *open)
{
    float row = ui_row_height(ui);
    float line = (float)text_line_height(ui->text);
    UiLayout layout = {ui,        false, 0.0f, 0.0f, 0.0f, row, SDL_floorf((row - line) * 0.5f),
                       row - 2.0f};
    rows(&layout, userdata);
    float width = SDL_ceilf(layout.width);
    SDL_FPoint origin = dock != NULL
                            ? ui_window_begin_docked(ui, id, title, width, layout.y, dock, open)
                            : ui_window_begin(ui, id, title, width, layout.y, placement, open);
    layout.draw = true;
    layout.x = origin.x;
    layout.y = origin.y;
    layout.width = SDL_max(width, ui_window_content_width(ui));
    rows(&layout, userdata);
    ui_window_end(ui);
}

void ui_layout_window(Ui *ui, int id, const char *title, UiPlacement placement, UiRowsFn rows,
                      void *userdata, bool *open)
{
    layout_window(ui, id, title, placement, NULL, rows, userdata, open);
}

void ui_layout_docked(Ui *ui, int id, const char *title, const UiDock *dock, UiRowsFn rows,
                      void *userdata, bool *open)
{
    layout_window(ui, id, title, UI_PLACE_CENTER, dock, rows, userdata, open);
}
