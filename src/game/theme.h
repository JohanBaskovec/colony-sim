/*
 * The look of the interface (docs/UI.md): a color for each role and every
 * size, with the UI scale applied. Widgets never use literal colors or pixel
 * sizes: they take them from here, so that a larger interface is a scale and
 * a palette for colorblind players is another set of colors.
 */
#ifndef COLONY_GAME_THEME_H
#define COLONY_GAME_THEME_H

#include "core/settings.h"

#include <SDL3/SDL.h>

typedef enum ThemeColor {
    THEME_PANEL,       /* Plain panels (menus). */
    THEME_HUD_PANEL,   /* Text panels over the map. */
    THEME_READOUT,     /* What lies under the mouse: lets the map show through. */
    THEME_BAR,         /* The bottom bar. */
    THEME_WINDOW,      /* Window background. */
    THEME_TITLE,       /* Window title bar. */
    THEME_TOOLTIP,     /* Tooltip background. */
    THEME_TEXT,        /* Text. */
    THEME_TEXT_DIM,    /* Secondary text. */
    THEME_TEXT_SHADOW, /* Under text drawn over the map. */
    THEME_BUTTON,      /* Button background. */
    THEME_BUTTON_HOVER,
    THEME_BUTTON_LIT, /* A button whose menu, window or tool is in use. */
    THEME_BORDER,
    THEME_BORDER_LIT, /* Around a lit button: never shown by color alone. */
    THEME_CHECK,      /* Check marks, the filled part of a slider. */
    THEME_TRACK,      /* Slider track. */
    THEME_HANDLE,     /* Slider handle. */
    THEME_SCROLL_TRACK,
    THEME_SCROLL_THUMB,
    THEME_WARNING,       /* Something stands in the way (a tool unavailable). */
    THEME_DISABLED_VEIL, /* Over a button that cannot be used yet (a text says why). */
    THEME_ICON_WELL,     /* Behind the small item icons, like bare ground. */
    THEME_VALUE_LOW,     /* Worst end of a scale (meters, signed numbers): red. */
    THEME_VALUE_MID,     /* Middle of a scale: white. */
    THEME_VALUE_HIGH,    /* Best end of a scale: green. */
    THEME_COLOR_COUNT
} ThemeColor;

/* Colors of the temperature view, coldest to hottest (render_temperature()). */
#define THEME_TEMPERATURE_STEPS 5

typedef struct Theme {
    float scale; /* 1: the default size. */
    SettingsPalette palette;
    SDL_Color colors[THEME_COLOR_COUNT];
    SDL_Color temperature[THEME_TEMPERATURE_STEPS];
    /* Sizes in pixels, the scale applied. */
    float font_size;           /* Points. */
    float margin;              /* Kept free at the screen edges. */
    float gap;                 /* Between buttons, between rows of buttons. */
    float padding;             /* Inside text panels and cards. */
    float button_padding;      /* Around a button's label. */
    float checkbox_gap;        /* Between a check box and its label. */
    float row_spacing;         /* Added to the text line height for a row of widgets. */
    float window_padding;      /* Inside windows. */
    float scrollbar_width;     /* Window scroll bars. */
    float tooltip_padding;     /* Inside tooltips. */
    float tooltip_offset;      /* From the mouse. */
    float tooltip_width;       /* Longer tooltip lines wrap. */
    float slider_handle_width; /* Range sliders. */
    float slider_track_height;
    float bar_button_width; /* Buttons of the bottom bar (wider if a label needs it). */
    float icon_size;        /* Icon buttons of the build menu. */
    float card_width;       /* Build menu: the category column and the card above it. */
    float inspector_width;  /* The inspector's box (and its tab panels, at least). */
} Theme;

/* Color along a scale from 0 (THEME_VALUE_LOW) through 0.5 (THEME_VALUE_MID) to 1
 * (THEME_VALUE_HIGH); `t` is clamped. */
SDL_Color theme_scale_color(const Theme *theme, float t);

/* THEME_VALUE_LOW below zero, THEME_VALUE_HIGH above, THEME_TEXT at zero. */
SDL_Color theme_sign_color(const Theme *theme, float value);

/* A palette at a scale (1: default size; 1.5: half as large again). */
void theme_init(Theme *theme, float scale, SettingsPalette palette);

#endif /* COLONY_GAME_THEME_H */
