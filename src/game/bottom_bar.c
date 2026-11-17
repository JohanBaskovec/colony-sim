#include "game/bottom_bar.h"

#include "core/i18n.h"
#include "game/layout.h"

#define TEXT_SIZE 256

typedef enum BarButton { BAR_BUILD, BAR_WORK, BAR_LOG, BAR_MENU, BAR_BUTTON_COUNT } BarButton;

static const char *const LABEL_KEYS[BAR_BUTTON_COUNT] = {
    [BAR_BUILD] = N_("menu.build"),
    [BAR_WORK] = N_("menu.work"),
    [BAR_LOG] = N_("menu.log"),
    [BAR_MENU] = N_("menu.game"),
};

static const char *const DESCRIPTION_KEYS[BAR_BUTTON_COUNT] = {
    [BAR_BUILD] = N_("menu.build.description"),
    [BAR_WORK] = N_("menu.work.description"),
    [BAR_LOG] = N_("menu.log.description"),
    [BAR_MENU] = N_("menu.game.description"),
};

/* Shortcuts (SDL_SCANCODE_UNKNOWN: none). */
static const SDL_Scancode KEYS[BAR_BUTTON_COUNT] = {
    [BAR_BUILD] = SDL_SCANCODE_UNKNOWN,
    [BAR_WORK] = APP_KEY_WORK,
    [BAR_LOG] = APP_KEY_LOG,
    [BAR_MENU] = APP_KEY_BACK,
};

static bool button_lit(const App *app, BarButton button)
{
    switch (button) {
    case BAR_BUILD: return app->build_menu_open;
    case BAR_WORK: return app->show_work_panel;
    case BAR_LOG: return app->show_log;
    case BAR_MENU: return app->show_game_menu;
    default: return false;
    }
}

/* Opens a main window (or closes it): one at a time, and not with the build menu. */
static void toggle_main_window(App *app, bool *window)
{
    bool open = !*window;
    app->show_work_panel = false;
    app->show_log = false;
    *window = open;
    if (open) {
        app->build_menu_open = false;
    }
}

void bottom_bar_press_work(App *app)
{
    toggle_main_window(app, &app->show_work_panel);
}

void bottom_bar_press_log(App *app)
{
    toggle_main_window(app, &app->show_log);
}

static void press(App *app, BarButton button)
{
    switch (button) {
    case BAR_BUILD:
        app->build_menu_open = !app->build_menu_open;
        if (app->build_menu_open) {
            app->show_work_panel = false;
            app->show_log = false;
        }
        break;
    case BAR_WORK: bottom_bar_press_work(app); break;
    case BAR_LOG: bottom_bar_press_log(app); break;
    case BAR_MENU: app->show_game_menu = !app->show_game_menu; break;
    default: break;
    }
}

static void tooltip(App *app, BarButton button)
{
    const char *lines[3] = {tr(LABEL_KEYS[button]), tr(DESCRIPTION_KEYS[button])};
    int count = 2;
    char shortcut[TEXT_SIZE];
    if (KEYS[button] != SDL_SCANCODE_UNKNOWN) {
        TR_FORMAT(shortcut, sizeof(shortcut), tr("menu.shortcut"), app_key_name(KEYS[button]));
        lines[count++] = shortcut;
    }
    ui_tooltip(&app->ui, lines, count);
}

void bottom_bar_draw(App *app)
{
    Ui *ui = &app->ui;
    ScreenLayout layout;
    layout_screen(ui, &layout);
    ui_panel_colored(ui, layout.bottom_bar, THEME_BAR);

    /* Every button as wide as the widest label needs, at least the theme's width. */
    float width = ui->theme.bar_button_width;
    for (int i = 0; i < BAR_BUTTON_COUNT; i++) {
        width = SDL_max(width, SDL_ceilf(ui_button_width(ui, tr(LABEL_KEYS[i]))));
    }
    float gap = ui->theme.gap;
    float y = layout.bottom_bar.y + SDL_floorf(gap * 0.5f);
    for (int i = 0; i < BAR_BUTTON_COUNT; i++) {
        BarButton button = (BarButton)i;
        float x = button == BAR_MENU ? layout.width - width : (float)i * (width + gap);
        SDL_FRect rect = {x, y, width, layout.button_height};
        if (ui_toggle_button(ui, rect, tr(LABEL_KEYS[i]), button_lit(app, button))) {
            press(app, button);
        }
        if (ui_hovered(ui, rect)) {
            tooltip(app, button);
        }
    }
}
