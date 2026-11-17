#include "game/game_menu.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

#define TEXT_SIZE 256

typedef enum MenuChoice {
    MENU_NONE,
    MENU_RESUME,
    MENU_CONTROLS,
    MENU_LANGUAGE,
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
    if (ui_layout_wide_button(layout, tr("game_menu.resume"))) {
        rows->picked = MENU_RESUME;
    }
    if (ui_layout_wide_button(layout, tr("help.title"))) {
        rows->picked = MENU_CONTROLS;
    }
    const LanguageInfo *info = localization_active_language(&rows->app->localization);
    char language[TEXT_SIZE];
    TR_FORMAT(language, sizeof(language), tr("game_menu.language"),
              info != NULL ? info->name : "?");
    if (ui_layout_wide_button(layout, language)) {
        rows->picked = MENU_LANGUAGE;
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
    case MENU_LANGUAGE: app_cycle_language(app); break;
    case MENU_QUIT: app->running = false; break;
    default: break;
    }
    if (!open) {
        app->show_game_menu = false;
    }
}
