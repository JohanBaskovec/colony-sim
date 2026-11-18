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
    [THEME_ICON_WELL] = {150, 136, 112, 255},   [THEME_VALUE_LOW] = {232, 84, 74, 255},
    [THEME_VALUE_MID] = {236, 236, 228, 255},   [THEME_VALUE_HIGH] = {104, 208, 98, 255},
};

/* The colorblind palette changes the colors told apart by hue alone: the
 * greens of what is lit or checked become blues (against the oranges of
 * warnings), and the temperature view goes from blue to orange through grey
 * instead of through green and yellow. Scales of values go from orange to
 * blue instead of from red to green. */
static const struct {
    ThemeColor role;
    SDL_Color color;
} COLORBLIND_COLORS[] = {
    {THEME_BUTTON_LIT, {44, 84, 142, 255}}, {THEME_BORDER_LIT, {130, 190, 255, 255}},
    {THEME_CHECK, {96, 168, 240, 255}},     {THEME_WARNING, {245, 165, 60, 255}},
    {THEME_VALUE_LOW, {240, 140, 40, 255}}, {THEME_VALUE_HIGH, {90, 160, 245, 255}},
};

static const SDL_Color TEMPERATURE_COLORS[SETTINGS_PALETTE_COUNT][THEME_TEMPERATURE_STEPS] = {
    [SETTINGS_PALETTE_STANDARD] = {{40, 60, 200, 255},
                                   {60, 150, 235, 255},
                                   {90, 200, 150, 255},
                                   {240, 210, 70, 255},
                                   {225, 60, 40, 255}},
    [SETTINGS_PALETTE_COLORBLIND] = {{30, 50, 170, 255},
                                     {90, 150, 235, 255},
                                     {215, 215, 210, 255},
                                     {245, 165, 60, 255},
                                     {185, 70, 20, 255}},
};

/* The light view goes from deep blue through a warm orange to bright yellow;
 * for the colorblind palette from blue through grey to pale yellow. */
static const SDL_Color LIGHT_COLORS[SETTINGS_PALETTE_COUNT][THEME_LIGHT_STEPS] = {
    [SETTINGS_PALETTE_STANDARD] = {{10, 14, 70, 255}, {225, 130, 50, 255}, {255, 240, 110, 255}},
    [SETTINGS_PALETTE_COLORBLIND] = {{20, 30, 110, 255},
                                     {150, 150, 150, 255},
                                     {255, 245, 150, 255}},
};

/* The soil moisture view, at moisture 0 (wilting), 0.5, 1 (field capacity),
 * 1.5 and 2 (saturated): ochre through straw and green to deep blue; for the
 * colorblind palette from orange through grey to blue. */
static const SDL_Color MOISTURE_COLORS[SETTINGS_PALETTE_COUNT][THEME_MOISTURE_STEPS] = {
    [SETTINGS_PALETTE_STANDARD] = {{185, 120, 45, 255},
                                   {220, 200, 105, 255},
                                   {85, 175, 85, 255},
                                   {60, 150, 215, 255},
                                   {30, 55, 185, 255}},
    [SETTINGS_PALETTE_COLORBLIND] = {{185, 90, 20, 255},
                                     {240, 170, 70, 255},
                                     {215, 215, 210, 255},
                                     {100, 160, 235, 255},
                                     {30, 50, 170, 255}},
};

/* A size at the default scale, scaled and rounded to whole pixels (never 0). */
static float scaled(const Theme *theme, float pixels)
{
    return SDL_max(1.0f, SDL_roundf(pixels * theme->scale));
}

static SDL_Color mix(SDL_Color a, SDL_Color b, float t)
{
    return (SDL_Color){(Uint8)SDL_roundf((float)a.r + ((float)b.r - (float)a.r) * t),
                       (Uint8)SDL_roundf((float)a.g + ((float)b.g - (float)a.g) * t),
                       (Uint8)SDL_roundf((float)a.b + ((float)b.b - (float)a.b) * t), 255};
}

SDL_Color theme_scale_color(const Theme *theme, float t)
{
    t = SDL_clamp(t, 0.0f, 1.0f);
    const SDL_Color *colors = theme->colors;
    if (t < 0.5f) {
        return mix(colors[THEME_VALUE_LOW], colors[THEME_VALUE_MID], t * 2.0f);
    }
    return mix(colors[THEME_VALUE_MID], colors[THEME_VALUE_HIGH], (t - 0.5f) * 2.0f);
}

SDL_Color theme_sign_color(const Theme *theme, float value)
{
    if (value < 0.0f) {
        return theme->colors[THEME_VALUE_LOW];
    }
    return theme->colors[value > 0.0f ? THEME_VALUE_HIGH : THEME_TEXT];
}

void theme_init(Theme *theme, float scale, SettingsPalette palette)
{
    SDL_zerop(theme);
    theme->scale = scale > 0.0f ? scale : 1.0f;
    theme->palette =
        (unsigned)palette < SETTINGS_PALETTE_COUNT ? palette : SETTINGS_PALETTE_STANDARD;
    SDL_memcpy(theme->colors, DEFAULT_COLORS, sizeof(DEFAULT_COLORS));
    if (theme->palette == SETTINGS_PALETTE_COLORBLIND) {
        for (int i = 0; i < (int)SDL_arraysize(COLORBLIND_COLORS); i++) {
            theme->colors[COLORBLIND_COLORS[i].role] = COLORBLIND_COLORS[i].color;
        }
    }
    SDL_memcpy(theme->temperature, TEMPERATURE_COLORS[theme->palette], sizeof(theme->temperature));
    SDL_memcpy(theme->light, LIGHT_COLORS[theme->palette], sizeof(theme->light));
    SDL_memcpy(theme->moisture, MOISTURE_COLORS[theme->palette], sizeof(theme->moisture));
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
    theme->build_menu_width = scaled(theme, 320.0f);
    theme->inspector_width = scaled(theme, 400.0f);
}
