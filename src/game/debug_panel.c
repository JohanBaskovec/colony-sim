#include "game/debug_panel.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

/* What the player clicked in the drawing pass, applied after it. */
typedef struct DebugRows {
    const GameDebug *debug;
    bool vision_on;
    bool toggle_instant_build;
    bool toggle_free_crafts;
    bool toggle_no_tools;
    bool toggle_instant_deconstruct;
    bool toggle_instant_fell_and_mine;
    bool toggle_vision;
    const char *weather; /* Name of the sky now. */
    bool next_weather;
    bool cold_snap;
    bool sadden;
    bool learn_all;
    bool skip_hour;
    bool skip_day;
    bool next_season;
    bool teleport_active; /* Whether the debug tool is on in teleport mode. */
    bool toggle_teleport;
    bool plant_tree_active, grow_tree_active; /* ... in the tree modes. */
    bool toggle_plant_tree, toggle_grow_tree;
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
    rows->toggle_instant_deconstruct |=
        checkbox_row(layout, tr("debug.instant_deconstruct"), rows->debug->instant_deconstruct);
    rows->toggle_instant_fell_and_mine |=
        checkbox_row(layout, tr("debug.instant_fell_and_mine"), rows->debug->instant_fell_and_mine);
    rows->toggle_vision |= checkbox_row(layout, tr("debug.vision"), rows->vision_on);
    char label[128];
    TR_FORMAT(label, sizeof(label), tr("debug.next_weather"), rows->weather);
    rows->next_weather |= button_row(layout, label);
    rows->cold_snap |= button_row(layout, tr("debug.cold_snap"));
    rows->sadden |= button_row(layout, tr("debug.sadden"));
    rows->learn_all |= button_row(layout, tr("debug.learn_all"));
    rows->skip_hour |= button_row(layout, tr("debug.skip_hour"));
    rows->skip_day |= button_row(layout, tr("debug.skip_day"));
    rows->next_season |= button_row(layout, tr("debug.next_season"));
    rows->toggle_teleport |= checkbox_row(layout, tr("debug.teleport_tool"), rows->teleport_active);
    rows->toggle_plant_tree |=
        checkbox_row(layout, tr("debug.plant_tree_tool"), rows->plant_tree_active);
    rows->toggle_grow_tree |=
        checkbox_row(layout, tr("debug.grow_tree_tool"), rows->grow_tree_active);
}

void debug_panel_draw(App *app)
{
    if (!app->show_debug_menu) {
        return;
    }
    Game *game = &app->game;
    DebugRows rows = {
        .debug = &game->debug,
        .vision_on = game->vision_on,
        .weather = tr(weather_name_key(&game->weather, game->clock.tick)),
        .teleport_active = app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_TELEPORT),
        .plant_tree_active = app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_PLANT_TREE),
        .grow_tree_active = app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_GROW_TREE)};
    bool open = true;
    ui_layout_window(&app->ui, APP_WINDOW_DEBUG, tr("debug.title"), UI_PLACE_TOP_CENTER, debug_rows,
                     &rows, &open);
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
    if (rows.toggle_instant_fell_and_mine) {
        game_set_instant_fell_and_mine(game, !game->debug.instant_fell_and_mine);
    }
    if (rows.toggle_vision) {
        game_set_vision(game, !game->vision_on);
    }
    if (rows.next_weather) {
        game_debug_next_weather(game);
    }
    if (rows.sadden) {
        game_debug_sadden(game);
    }
    if (rows.cold_snap) {
        game_debug_cold_snap(game);
    }
    if (rows.learn_all) {
        game_debug_learn_all(game);
    }
    if (rows.skip_hour || rows.skip_day) { /* Simulated at once: the frame waits. */
        game_debug_skip(game, rows.skip_day ? TICKS_PER_DAY : TICKS_PER_HOUR);
    }
    if (rows.next_season) {
        game_debug_next_season(game);
    }
    if (rows.toggle_teleport) {
        app_toggle_tool(app, APP_TOOL_DEBUG, APP_DEBUG_TELEPORT);
    }
    if (rows.toggle_plant_tree) {
        app_toggle_tool(app, APP_TOOL_DEBUG, APP_DEBUG_PLANT_TREE);
    }
    if (rows.toggle_grow_tree) {
        app_toggle_tool(app, APP_TOOL_DEBUG, APP_DEBUG_GROW_TREE);
    }
    if (!open) {
        app->show_debug_menu = false;
    }
}
