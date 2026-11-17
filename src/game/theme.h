/*
 * The look of the interface (docs/UI.md): a color for each role and every
 * size, with the UI scale applied. Widgets never use literal colors or pixel
 * sizes: they take them from here, so that a larger interface is a scale and
 * a palette for colorblind players is another set of colors.
 */
#ifndef COLONY_GAME_THEME_H
#define COLONY_GAME_THEME_H

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
    THEME_COLOR_COUNT
} ThemeColor;

typedef struct Theme {
    float scale; /* 1: the default size. */
    SDL_Color colors[THEME_COLOR_COUNT];
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
} Theme;

/* The default palette at a scale (1: default size; 1.5: half as large again). */
void theme_init(Theme *theme, float scale);

#endif /* COLONY_GAME_THEME_H */
