#include "game/game_menu.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

typedef enum MenuChoice {
    MENU_NONE,
    MENU_RESUME,
    MENU_CONTROLS,
    MENU_SETTINGS,
    MENU_MAIN_MENU, /* Asks first (App.leave_confirm). */
    MENU_LEAVE,     /* Asked: back to the title screen. */
    MENU_STAY,
    MENU_QUIT,
} MenuChoice;

typedef struct MenuRows {
    App *app;
    MenuChoice picked;
} MenuRows;

static void menu_rows(UiLayout *layout, void *userdata)
{
    MenuRows *rows = userdata;
    ui_layout_measure(layout, layout->ui->theme.card_width);
    if (rows->app->leave_confirm) {
        ui_layout_label(layout, tr("game_menu.leave_question"), 0.0f, false);
        if (ui_layout_wide_button(layout, tr("game_menu.leave"))) {
            rows->picked = MENU_LEAVE;
        }
        if (ui_layout_wide_button(layout, tr("game_menu.stay"))) {
            rows->picked = MENU_STAY;
        }
        return;
    }
    if (ui_layout_wide_button(layout, tr("game_menu.resume"))) {
        rows->picked = MENU_RESUME;
    }
    if (ui_layout_wide_button(layout, tr("help.title"))) {
        rows->picked = MENU_CONTROLS;
    }
    if (ui_layout_wide_button(layout, tr("settings.title"))) {
        rows->picked = MENU_SETTINGS;
    }
    if (ui_layout_wide_button(layout, tr("game_menu.main_menu"))) {
        rows->picked = MENU_MAIN_MENU;
    }
    if (ui_layout_wide_button(layout, tr("game_menu.quit"))) {
        rows->picked = MENU_QUIT;
    }
}

void game_menu_draw(App *app)
{
    MenuRows rows = {app, MENU_NONE};
    bool open = true;
    /* The paused game behind it is veiled. */
    ui_fill(&app->ui, (SDL_FRect){0.0f, 0.0f, app->ui.screen_width, app->ui.screen_height},
            app->ui.theme.colors[THEME_DISABLED_VEIL]);
    ui_layout_window(&app->ui, APP_WINDOW_GAME_MENU, tr("menu.game"), UI_PLACE_CENTER, menu_rows,
                     &rows, &open);
    switch (rows.picked) {
    case MENU_RESUME: open = false; break;
    case MENU_CONTROLS:
        open = false;
        app->show_help = true;
        break;
    case MENU_SETTINGS:
        open = false; /* Back to the menu when the settings close. */
        app->show_settings = true;
        break;
    case MENU_MAIN_MENU: app->leave_confirm = true; break;
    case MENU_STAY: app->leave_confirm = false; break;
    case MENU_LEAVE: app_request_title(app); break;
    case MENU_QUIT: app->running = false; break;
    default: break;
    }
    if (!open) {
        app->show_game_menu = false;
        app->leave_confirm = false;
    }
}
