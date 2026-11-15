#include "game/debug_panel.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

/* What the player clicked in the drawing pass, applied after it. */
typedef struct DebugRows {
    const GameDebug *debug;
    bool toggle_instant_build;
    bool toggle_free_crafts;
    bool toggle_no_tools;
    bool toggle_instant_deconstruct;
    const char *weather; /* Name of the sky now. */
    bool next_weather;
    bool cold_snap;
} DebugRows;

/* A button on its own row. Returns whether it was clicked. */
static bool button_row(UiLayout *layout, const char *label)
{
    ui_layout_measure(layout, ui_button_width(layout->ui, label));
    bool clicked = ui_layout_button(layout, layout->x, label);
    layout->y += layout->row;
    return clicked;
}

/* A check box on its own row. Returns whether it was clicked. */
static bool checkbox_row(UiLayout *layout, const char *label, bool checked)
{
    float width = ui_checkbox_width(layout->ui, label);
    ui_layout_measure(layout, width);
    bool clicked = layout->draw && ui_checkbox(layout->ui, layout->x, layout->y, width,
                                               checked ? UI_CHECKED : UI_UNCHECKED, label);
    layout->y += layout->row;
    return clicked;
}

static void debug_rows(UiLayout *layout, void *userdata)
{
    DebugRows *rows = userdata;
    rows->toggle_instant_build |=
        checkbox_row(layout, tr("debug.instant_build"), rows->debug->instant_build);
    rows->toggle_free_crafts |=
        checkbox_row(layout, tr("debug.free_crafts"), rows->debug->free_crafts);
    rows->toggle_no_tools |= checkbox_row(layout, tr("debug.no_tools"), rows->debug->no_tools);
    rows->toggle_instant_deconstruct |= checkbox_row(layout, tr("debug.instant_deconstruct"),
                                                     rows->debug->instant_deconstruct);
    char label[128];
    TR_FORMAT(label, sizeof(label), tr("debug.next_weather"), rows->weather);
    rows->next_weather |= button_row(layout, label);
    rows->cold_snap |= button_row(layout, tr("debug.cold_snap"));
}

void debug_panel_draw(App *app)
{
    if (!app->show_debug_menu) {
        return;
    }
    Game *game = &app->game;
    DebugRows rows = {.debug = &game->debug,
                      .weather = tr(weather_name_key(&game->weather, game->clock.tick))};
    bool open = true;
    ui_layout_window(&app->ui, APP_WINDOW_DEBUG, tr("debug.title"), UI_PLACE_TOP_CENTER,
                     debug_rows, &rows, &open);
    if (rows.toggle_instant_build) {
        game_set_instant_build(game, !game->debug.instant_build);
    }
    if (rows.toggle_free_crafts) {
        game_set_free_crafts(game, !game->debug.free_crafts);
    }
    if (rows.toggle_no_tools) {
        game_set_no_tools(game, !game->debug.no_tools);
    }
    if (rows.toggle_instant_deconstruct) {
        game_set_instant_deconstruct(game, !game->debug.instant_deconstruct);
    }
    if (rows.next_weather) {
        game_debug_next_weather(game);
    }
    if (rows.cold_snap) {
        game_debug_cold_snap(game);
    }
    if (!open) {
        app->show_debug_menu = false;
    }
}
