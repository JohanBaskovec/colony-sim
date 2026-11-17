#include "game/theme.h"

static const SDL_Color DEFAULT_COLORS[THEME_COLOR_COUNT] = {
    [THEME_PANEL] = {12, 14, 18, 200},          [THEME_HUD_PANEL] = {12, 14, 18, 170},
    [THEME_READOUT] = {12, 14, 18, 120},        [THEME_BAR] = {20, 23, 30, 245},
    [THEME_WINDOW] = {16, 18, 24, 242},         [THEME_TITLE] = {40, 46, 58, 240},
    [THEME_TOOLTIP] = {8, 9, 12, 235},          [THEME_TEXT] = {236, 236, 228, 255},
    [THEME_TEXT_DIM] = {176, 180, 188, 255},    [THEME_TEXT_SHADOW] = {0, 0, 0, 200},
    [THEME_BUTTON] = {54, 60, 72, 255},         [THEME_BUTTON_HOVER] = {80, 88, 104, 255},
    [THEME_BUTTON_LIT] = {62, 104, 70, 255},    [THEME_BORDER] = {130, 136, 150, 255},
    [THEME_BORDER_LIT] = {150, 230, 140, 255},  [THEME_CHECK] = {120, 210, 110, 255},
    [THEME_TRACK] = {54, 60, 72, 255},          [THEME_HANDLE] = {200, 204, 214, 255},
    [THEME_SCROLL_TRACK] = {255, 255, 255, 24}, [THEME_SCROLL_THUMB] = {200, 204, 214, 200},
    [THEME_WARNING] = {240, 180, 90, 255},      [THEME_DISABLED_VEIL] = {10, 12, 16, 150},
};

/* A size at the default scale, scaled and rounded to whole pixels (never 0). */
static float scaled(const Theme *theme, float pixels)
{
    return SDL_max(1.0f, SDL_roundf(pixels * theme->scale));
}

void theme_init(Theme *theme, float scale)
{
    SDL_zerop(theme);
    theme->scale = scale > 0.0f ? scale : 1.0f;
    SDL_memcpy(theme->colors, DEFAULT_COLORS, sizeof(DEFAULT_COLORS));
    theme->font_size = 15.0f * theme->scale;
    theme->margin = scaled(theme, 8.0f);
    theme->gap = scaled(theme, 4.0f);
    theme->padding = scaled(theme, 6.0f);
    theme->button_padding = scaled(theme, 6.0f);
    theme->checkbox_gap = scaled(theme, 6.0f);
    theme->row_spacing = scaled(theme, 4.0f);
    theme->window_padding = scaled(theme, 8.0f);
    theme->scrollbar_width = scaled(theme, 8.0f);
    theme->tooltip_padding = scaled(theme, 6.0f);
    theme->tooltip_offset = scaled(theme, 16.0f);
    theme->tooltip_width = scaled(theme, 420.0f);
    theme->slider_handle_width = scaled(theme, 8.0f);
    theme->slider_track_height = scaled(theme, 4.0f);
    theme->bar_button_width = scaled(theme, 136.0f);
    theme->icon_size = scaled(theme, 56.0f);
    theme->card_width = scaled(theme, 240.0f);
}
