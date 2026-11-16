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

bool ui_layout_button_sized(UiLayout *layout, float x, float width, const char *label)
{
    return layout->draw &&
           ui_button(layout->ui, (SDL_FRect){x, layout->y, width, layout->button_height}, label);
}

bool ui_layout_button(UiLayout *layout, float x, const char *label)
{
    return ui_layout_button_sized(layout, x, ui_button_width(layout->ui, label), label);
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

void ui_layout_window(Ui *ui, int id, const char *title, UiPlacement placement, UiRowsFn rows,
                      void *userdata, bool *open)
{
    float row = ui_row_height(ui);
    float line = (float)text_line_height(ui->text);
    UiLayout layout = {ui,        false, 0.0f, 0.0f, 0.0f, row, SDL_floorf((row - line) * 0.5f),
                       row - 2.0f};
    rows(&layout, userdata);
    float width = SDL_ceilf(layout.width);
    SDL_FPoint origin = ui_window_begin(ui, id, title, width, layout.y, placement, open);
    layout.draw = true;
    layout.x = origin.x;
    layout.y = origin.y;
    layout.width = SDL_max(width, ui_window_content_width(ui));
    rows(&layout, userdata);
    ui_window_end(ui);
}
