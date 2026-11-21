#include "game/debug_panel.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

/* What the player clicked in the drawing pass, applied after it. */
typedef struct DebugRows {
    const GameDebug *debug;
    bool vision_shown;
    bool fog_on;
    bool character_editor;
    bool god_mode; /* The first six cheats and the character editor are all on. */
    float temperature_offset;
    bool toggle_god_mode;
    bool toggle_character_editor;
    int temperature_step; /* -1, 0, 1: the buttons of the temperature row. */
    float rain;           /* Weather.debug_rain. */
    int rain_step;        /* -1, 0, 1: the buttons of the rain row. */
    bool toggle_instant_build;
    bool toggle_free_crafts;
    bool toggle_no_tools;
    bool toggle_instant_deconstruct;
    bool toggle_instant_fell_and_mine;
    bool toggle_vision;
    bool toggle_fog;
    bool reveal_map;
    const char *weather; /* Name of the sky now. */
    bool next_weather;
    bool cold_snap;
    bool sadden;
    bool discord;
    bool forget_social;
    const ConversationCounts *chats;
    bool learn_all;
    bool skip_hour;
    bool skip_day;
    bool next_season;
    bool teleport_active; /* Whether the debug tool is on in teleport mode. */
    bool toggle_teleport;
    bool plant_tree_active, grow_tree_active; /* ... in the tree modes. */
    bool toggle_plant_tree, toggle_grow_tree;
    bool hurt_active, toggle_hurt;           /* ... in the blow mode. */
    bool move_item_active, toggle_move_item; /* ... in the item dragging mode. */
    bool remove_block_active, toggle_remove_block;
    bool pour_water_active, toggle_pour_water;
    bool fight_active, toggle_fight; /* ... in the fight mode. */
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
    rows->toggle_god_mode |= checkbox_row(layout, tr("debug.god_mode"), rows->god_mode);
    rows->toggle_instant_build |=
        checkbox_row(layout, tr("debug.instant_build"), rows->debug->instant_build);
    rows->toggle_free_crafts |=
        checkbox_row(layout, tr("debug.free_crafts"), rows->debug->free_crafts);
    rows->toggle_no_tools |= checkbox_row(layout, tr("debug.no_tools"), rows->debug->no_tools);
    rows->toggle_instant_deconstruct |=
        checkbox_row(layout, tr("debug.instant_deconstruct"), rows->debug->instant_deconstruct);
    rows->toggle_instant_fell_and_mine |=
        checkbox_row(layout, tr("debug.instant_fell_and_mine"), rows->debug->instant_fell_and_mine);
    rows->toggle_fog |= checkbox_row(layout, tr("debug.see_everything"), !rows->fog_on);
    rows->reveal_map |= button_row(layout, tr("debug.reveal_map"));
    rows->toggle_vision |= checkbox_row(layout, tr("debug.vision"), rows->vision_shown);
    rows->toggle_character_editor |=
        checkbox_row(layout, tr("debug.character_editor"), rows->character_editor);
    char label[128];
    TR_FORMAT(label, sizeof(label), tr("debug.next_weather"), rows->weather);
    rows->next_weather |= button_row(layout, label);
    rows->cold_snap |= button_row(layout, tr("debug.cold_snap"));
    /* The air's temperature compared with the weather's, with its - and + buttons. */
    char degrees[16], offset[32];
    tr_integer(degrees, sizeof(degrees), SDL_lroundf(rows->temperature_offset));
    TR_FORMAT(offset, sizeof(offset), tr("unit.celsius"), degrees);
    if (rows->temperature_offset > 0.0f) {
        SDL_snprintf(degrees, sizeof(degrees), "+%s", offset);
        SDL_strlcpy(offset, degrees, sizeof(offset));
    }
    TR_FORMAT(label, sizeof(label), tr("debug.temperature"), offset);
    ui_layout_set_stepper(layout, true);
    SDL_FRect row = ui_layout_pair(layout, label, "", layout->ui->theme.colors[THEME_TEXT], false);
    int step = ui_layout_stepper(layout, row);
    ui_layout_set_stepper(layout, false);
    if (step != 0) {
        rows->temperature_step = step;
    }
    /* The rain forced in mm an hour (or the weather's own), with its - and + buttons. */
    char amount[16], rain[32];
    if (rows->rain > 0.0f) {
        tr_decimal(amount, sizeof(amount), rows->rain, rows->rain < 1.0f ? 1 : 0);
        TR_FORMAT(rain, sizeof(rain), tr("unit.mm_per_hour"), amount);
    } else {
        SDL_strlcpy(rain, tr("debug.rain_natural"), sizeof(rain));
    }
    TR_FORMAT(label, sizeof(label), tr("debug.rain"), rain);
    ui_layout_set_stepper(layout, true);
    row = ui_layout_pair(layout, label, "", layout->ui->theme.colors[THEME_TEXT], false);
    step = ui_layout_stepper(layout, row);
    ui_layout_set_stepper(layout, false);
    if (step != 0) {
        rows->rain_step = step;
    }
    rows->sadden |= button_row(layout, tr("debug.sadden"));
    rows->discord |= button_row(layout, tr("debug.discord"));
    rows->forget_social |= button_row(layout, tr("debug.forget_social"));
    /* What the chats turned into so far. */
    const ConversationCounts *chats = rows->chats;
    uint32_t total = 0;
    for (int kind = 0; kind < CHAT_KIND_COUNT; kind++) {
        total += chats->kinds[kind];
    }
    char numbers[5][16];
    tr_integer(numbers[0], sizeof(numbers[0]), (int)total);
    tr_integer(numbers[1], sizeof(numbers[1]),
               (int)(chats->kinds[CHAT_ARGUMENT] + chats->answered_back));
    tr_integer(numbers[2], sizeof(numbers[2]), (int)chats->brawls);
    tr_integer(numbers[3], sizeof(numbers[3]), (int)chats->kinds[CHAT_APOLOGY]);
    tr_integer(numbers[4], sizeof(numbers[4]), (int)chats->kinds[CHAT_REPORT]);
    TR_FORMAT(label, sizeof(label), tr("debug.chat_counts"), numbers[0], numbers[1], numbers[2],
              numbers[3], numbers[4]);
    ui_layout_label(layout, label, 0.0f, true);
    rows->learn_all |= button_row(layout, tr("debug.learn_all"));
    rows->skip_hour |= button_row(layout, tr("debug.skip_hour"));
    rows->skip_day |= button_row(layout, tr("debug.skip_day"));
    rows->next_season |= button_row(layout, tr("debug.next_season"));
    rows->toggle_teleport |= checkbox_row(layout, tr("debug.teleport_tool"), rows->teleport_active);
    rows->toggle_plant_tree |=
        checkbox_row(layout, tr("debug.plant_tree_tool"), rows->plant_tree_active);
    rows->toggle_grow_tree |=
        checkbox_row(layout, tr("debug.grow_tree_tool"), rows->grow_tree_active);
    rows->toggle_hurt |= checkbox_row(layout, tr("debug.hurt_tool"), rows->hurt_active);
    rows->toggle_fight |= checkbox_row(layout, tr("debug.fight_tool"), rows->fight_active);
    rows->toggle_move_item |=
        checkbox_row(layout, tr("debug.move_item_tool"), rows->move_item_active);
    rows->toggle_remove_block |=
        checkbox_row(layout, tr("debug.remove_block_tool"), rows->remove_block_active);
    rows->toggle_pour_water |=
        checkbox_row(layout, tr("debug.pour_water_tool"), rows->pour_water_active);
}

void debug_panel_draw(App *app)
{
    if (!app->show_debug_menu) {
        return;
    }
    Game *game = &app->game;
    const GameDebug *debug = &game->debug;
    bool god_mode = debug->instant_build && debug->free_crafts && debug->no_tools &&
                    debug->instant_deconstruct && debug->instant_fell_and_mine && !game->fog_on &&
                    app->character_editor;
    DebugRows rows = {
        .debug = debug,
        .vision_shown = app->show_vision,
        .fog_on = game->fog_on,
        .character_editor = app->character_editor,
        .god_mode = god_mode,
        .temperature_offset = game->weather.debug_offset,
        .rain = game->weather.debug_rain,
        .weather = tr(weather_name_key(&game->weather, game->clock.tick)),
        .chats = &game->chat_counts,
        .teleport_active = app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_TELEPORT),
        .plant_tree_active = app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_PLANT_TREE),
        .grow_tree_active = app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_GROW_TREE),
        .hurt_active = app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_HURT),
        .fight_active = app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_FIGHT),
        .move_item_active = app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_MOVE_ITEM),
        .remove_block_active = app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_REMOVE_BLOCK),
        .pour_water_active = app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_POUR_WATER)};
    bool open = true;
    ui_layout_window(&app->ui, APP_WINDOW_DEBUG, tr("debug.title"), UI_PLACE_TOP_CENTER, debug_rows,
                     &rows, &open);
    if (rows.toggle_god_mode) { /* All of them on, or (when all were) all off. */
        bool on = !god_mode;
        game_set_instant_build(game, on);
        game_set_free_crafts(game, on);
        game_set_no_tools(game, on);
        game_set_instant_deconstruct(game, on);
        game_set_instant_fell_and_mine(game, on);
        game_set_fog(game, !on);
        app->character_editor = on;
    }
    if (rows.toggle_character_editor) {
        app->character_editor = !app->character_editor;
    }
    if (rows.temperature_step != 0) {
        game_debug_temperature(game, rows.temperature_step);
    }
    if (rows.rain_step != 0) {
        game_debug_rain(game, rows.rain_step);
    }
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
    if (rows.toggle_fog) {
        game_set_fog(game, !game->fog_on);
    }
    if (rows.reveal_map) {
        game_set_vision(game, true); /* Revealing needs the tracking. */
        game_reveal_map(game);
    }
    if (rows.toggle_vision) {
        app->show_vision = !app->show_vision;
        if (app->show_vision) {
            game_set_vision(game, true);
        }
    }
    if (rows.next_weather) {
        game_debug_next_weather(game);
    }
    if (rows.sadden) {
        game_debug_sadden(game);
    }
    if (rows.discord) {
        game_debug_discord(game);
    }
    if (rows.forget_social) {
        game_debug_forget_social(game);
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
    if (rows.toggle_hurt) {
        app_toggle_tool(app, APP_TOOL_DEBUG, APP_DEBUG_HURT);
    }
    if (rows.toggle_move_item) {
        app->debug_held = ITEM_ID_NONE;
        app_toggle_tool(app, APP_TOOL_DEBUG, APP_DEBUG_MOVE_ITEM);
    }
    if (rows.toggle_remove_block) {
        app_toggle_tool(app, APP_TOOL_DEBUG, APP_DEBUG_REMOVE_BLOCK);
    }
    if (rows.toggle_pour_water) {
        app_toggle_tool(app, APP_TOOL_DEBUG, APP_DEBUG_POUR_WATER);
    }
    if (rows.toggle_fight) {
        app->debug_attacker = combatant_none();
        app_toggle_tool(app, APP_TOOL_DEBUG, APP_DEBUG_FIGHT);
    }
    if (!open) {
        app->show_debug_menu = false;
    }
}
