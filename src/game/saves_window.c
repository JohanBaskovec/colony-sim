#include "game/saves_window.h"

#include "core/i18n.h"
#include "game/saves.h"
#include "game/ui_layout.h"

#define TEXT_SIZE 256
#define QUESTION_LINES 4

typedef enum SavesAction {
    ACTION_NONE,
    ACTION_NEW,
    ACTION_OVERWRITE,
    ACTION_LOAD,
    ACTION_DELETE,
    ACTION_YES, /* The question asked is answered yes. */
    ACTION_CANCEL,
} SavesAction;

typedef struct SavesRows {
    App *app;
    SavesAction action;
    int entry; /* The save the action is about. */
} SavesRows;

void saves_window_open(App *app, bool loading)
{
    app->show_saves = true;
    app->saves_loading = loading;
    app->saves_stale = true;
    app->saves_confirm = SAVES_CONFIRM_NONE;
}

static void close_window(App *app)
{
    app->show_saves = false;
    app->saves_confirm = SAVES_CONFIRM_NONE;
    /* Back where it was opened from (the title screen draws itself). */
    app->show_game_menu = app->screen == APP_SCREEN_GAME;
}

void saves_window_step_back(App *app)
{
    if (app->saves_confirm != SAVES_CONFIRM_NONE) {
        app->saves_confirm = SAVES_CONFIRM_NONE;
    } else {
        close_window(app);
    }
}

/* The saves window's question about the save waiting, with its yes. */
static void question_rows(UiLayout *layout, SavesRows *rows)
{
    App *app = rows->app;
    const SaveEntry *entry = NULL;
    for (int i = 0; i < app->saves.count; i++) {
        if (SDL_strcmp(app->saves.entries[i].file, app->saves_confirm_file) == 0) {
            entry = &app->saves.entries[i];
        }
    }
    char name[TEXT_SIZE];
    if (entry != NULL) {
        saves_entry_name(entry, name, sizeof(name));
    } else {
        SDL_strlcpy(name, app->saves_confirm_file, sizeof(name));
    }
    const char *key = "saves.confirm.overwrite", *yes = "saves.overwrite";
    if (app->saves_confirm == SAVES_CONFIRM_DELETE) {
        key = "saves.confirm.delete";
        yes = "saves.delete";
    } else if (app->saves_confirm == SAVES_CONFIRM_LOAD) {
        key = "saves.confirm.load";
        yes = "saves.load";
    }
    char question[TEXT_SIZE];
    TR_FORMAT(question, sizeof(question), tr(key), name);
    float width = layout->ui->theme.inspector_width;
    ui_layout_measure(layout, width);
    char lines[QUESTION_LINES][UI_WRAP_LINE_SIZE];
    int count = ui_wrap_text(layout->ui, question, width, lines, QUESTION_LINES);
    for (int line = 0; line < count; line++) {
        ui_layout_label(layout, lines[line], 0.0f, false);
    }
    layout->y += layout->ui->theme.gap;
    if (ui_layout_wide_button(layout, tr(yes))) {
        rows->action = ACTION_YES;
    }
    if (ui_layout_wide_button(layout, tr("saves.cancel"))) {
        rows->action = ACTION_CANCEL;
    }
}

/* A save: its name and its buttons on a row, its details under it. */
static void entry_rows(UiLayout *layout, SavesRows *rows, int index)
{
    App *app = rows->app;
    Ui *ui = layout->ui;
    const SaveEntry *entry = &app->saves.entries[index];
    char name[TEXT_SIZE], details[TEXT_SIZE];
    saves_entry_name(entry, name, sizeof(name));
    saves_entry_details(entry, details, sizeof(details));
    const char *labels[2];
    SavesAction actions[2];
    int count = 0;
    if (!app->saves_loading) {
        labels[count] = tr("saves.overwrite");
        actions[count++] = ACTION_OVERWRITE;
    } else {
        if (entry->has_info) {
            labels[count] = tr("saves.load");
            actions[count++] = ACTION_LOAD;
        }
        labels[count] = tr("saves.delete");
        actions[count++] = ACTION_DELETE;
    }
    float buttons = 0.0f;
    for (int i = 0; i < count; i++) {
        buttons += ui_button_width(ui, labels[i]) + (i > 0 ? ui->theme.gap : 0.0f);
    }
    ui_layout_measure(layout, ui_text_width(ui, name) + UI_LAYOUT_GAP * 2.0f + buttons);
    float x = layout->x + layout->width;
    for (int i = count - 1; i >= 0; i--) {
        x -= ui_button_width(ui, labels[i]);
        if (ui_layout_button(layout, x, labels[i])) {
            rows->action = actions[i];
            rows->entry = index;
        }
        x -= ui->theme.gap;
    }
    ui_layout_label(layout, name, 0.0f, false);
    ui_layout_label(layout, details, 0.0f, true);
    layout->y += ui->theme.gap * 2.0f;
}

static void saves_rows(UiLayout *layout, void *userdata)
{
    SavesRows *rows = userdata;
    App *app = rows->app;
    if (app->saves_confirm != SAVES_CONFIRM_NONE) {
        question_rows(layout, rows);
        return;
    }
    ui_layout_measure(layout, layout->ui->theme.inspector_width);
    if (!app->saves_loading) {
        if (ui_layout_wide_button(layout, tr("saves.new"))) {
            rows->action = ACTION_NEW;
        }
        layout->y += layout->ui->theme.gap;
    }
    if (app->saves.count == 0) {
        ui_layout_label(layout, tr("saves.none"), 0.0f, true);
        return;
    }
    for (int i = 0; i < app->saves.count; i++) {
        entry_rows(layout, rows, i);
    }
}

static void ask(App *app, SavesConfirm confirm, const char *file)
{
    app->saves_confirm = confirm;
    SDL_strlcpy(app->saves_confirm_file, file, sizeof(app->saves_confirm_file));
}

/* The answer to the question asked. */
static void confirmed(App *app)
{
    SavesConfirm confirm = app->saves_confirm;
    app->saves_confirm = SAVES_CONFIRM_NONE;
    switch (confirm) {
    case SAVES_CONFIRM_OVERWRITE:
        if (saves_write(app, app->saves_confirm_file, true)) {
            close_window(app);
        }
        break;
    case SAVES_CONFIRM_DELETE: saves_delete(app, app->saves_confirm_file); break;
    case SAVES_CONFIRM_LOAD: app_request_load(app, app->saves_confirm_file); break;
    default: break;
    }
}

static void apply(App *app, const SavesRows *rows)
{
    const char *file = NULL;
    if (rows->entry >= 0 && rows->entry < app->saves.count) {
        file = app->saves.entries[rows->entry].file;
    }
    switch (rows->action) {
    case ACTION_NEW:
        if (saves_write_new(app)) {
            close_window(app);
        }
        break;
    case ACTION_OVERWRITE: ask(app, SAVES_CONFIRM_OVERWRITE, file); break;
    case ACTION_DELETE: ask(app, SAVES_CONFIRM_DELETE, file); break;
    case ACTION_LOAD:
        if (app->screen == APP_SCREEN_GAME) {
            ask(app, SAVES_CONFIRM_LOAD, file); /* What is not saved would be lost. */
        } else {
            app_request_load(app, file);
        }
        break;
    case ACTION_YES: confirmed(app); break;
    case ACTION_CANCEL: app->saves_confirm = SAVES_CONFIRM_NONE; break;
    default: break;
    }
}

void saves_window_draw(App *app)
{
    if (!app->show_saves) {
        return;
    }
    if (app->saves_stale) {
        saves_list(app, &app->saves);
        app->saves_stale = false;
    }
    SavesRows rows = {.app = app, .action = ACTION_NONE, .entry = -1};
    bool open = true;
    /* The paused game behind it is veiled, as behind the game menu. */
    ui_fill(&app->ui, (SDL_FRect){0.0f, 0.0f, app->ui.screen_width, app->ui.screen_height},
            app->ui.theme.colors[THEME_DISABLED_VEIL]);
    const char *title = tr(app->saves_loading ? "saves.title.load" : "saves.title.save");
    ui_layout_window(&app->ui, APP_WINDOW_SAVES, title, UI_PLACE_CENTER, saves_rows, &rows, &open);
    if (rows.action == ACTION_OVERWRITE || rows.action == ACTION_DELETE ||
        rows.action == ACTION_LOAD) {
        if (rows.entry < 0) {
            rows.action = ACTION_NONE;
        }
    }
    apply(app, &rows);
    if (!open) {
        close_window(app);
    }
}
