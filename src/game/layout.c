#include "game/layout.h"

#define CORNER_WIDTH 0.9f /* Of the card width. */

void layout_screen(const Ui *ui, ScreenLayout *layout)
{
    const Theme *theme = &ui->theme;
    layout->width = ui->screen_width;
    layout->height = ui->screen_height;
    float line = (float)text_line_height(ui->text);
    layout->button_height = line + 2.0f * theme->button_padding;
    float bar_height = layout->button_height + theme->gap;
    layout->bottom_bar = (SDL_FRect){0.0f, layout->height - bar_height, layout->width, bar_height};
    layout->bar_top = layout->bottom_bar.y;
    layout->corner_width = SDL_floorf(CORNER_WIDTH * theme->card_width);
}
