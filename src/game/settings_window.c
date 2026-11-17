#include "game/settings_window.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

#define TEXT_SIZE 128
#define SCALE_COUNT ((CLI_UI_SCALE_MAX - CLI_UI_SCALE_MIN) / SETTINGS_UI_SCALE_STEP + 1)
#define NOTE_LINES 4
/* Dropdown ids, apart from those of the other windows (small numbers). */
#define DROPDOWN_BASE 0x5E7700

typedef enum Setting { SETTING_SCALE, SETTING_PALETTE, SETTING_LANGUAGE, SETTING_COUNT } Setting;

typedef struct SettingRow {
    const char *label;
    const char *note; /* What it does, under the row. */
    const char *options[UI_POPUP_OPTIONS];
    int count;
    int selected;
} SettingRow;

typedef struct SettingsRows {
    App *app;
    SettingRow rows[SETTING_COUNT];
    char scale_texts[SCALE_COUNT][32];
    char language_note[TEXT_SIZE];
    int picked[SETTING_COUNT]; /* Option picked in a dropdown, or -1. */
} SettingsRows;

static void fill_rows(SettingsRows *rows)
{
    App *app = rows->app;
    SettingRow *scale = &rows->rows[SETTING_SCALE];
    scale->label = tr("settings.ui_scale");
    scale->note = tr("settings.ui_scale.description");
    int current = app_ui_scale(app);
    int nearest = 0;
    for (int i = 0; i < SCALE_COUNT && i < UI_POPUP_OPTIONS; i++) {
        int percent = CLI_UI_SCALE_MIN + i * SETTINGS_UI_SCALE_STEP;
        char number[16];
        tr_integer(number, sizeof(number), percent);
        TR_FORMAT(rows->scale_texts[i], sizeof(rows->scale_texts[i]), tr("settings.percent"),
                  number);
        scale->options[scale->count++] = rows->scale_texts[i];
        int best = CLI_UI_SCALE_MIN + nearest * SETTINGS_UI_SCALE_STEP;
        if (SDL_abs(percent - current) < SDL_abs(best - current)) {
            nearest = i;
        }
    }
    scale->selected = nearest;

    SettingRow *palette = &rows->rows[SETTING_PALETTE];
    palette->label = tr("settings.palette");
    palette->note = tr("settings.palette.description");
    for (int i = 0; i < SETTINGS_PALETTE_COUNT; i++) {
        palette->options[palette->count++] = tr(settings_palette_name_key((SettingsPalette)i));
    }
    palette->selected = (int)app->ui.theme.palette;

    SettingRow *language = &rows->rows[SETTING_LANGUAGE];
    const Localization *loc = &app->localization;
    language->label = tr("settings.language");
    TR_FORMAT(rows->language_note, sizeof(rows->language_note), tr("settings.language.description"),
              app_key_name(APP_KEY_LANGUAGE));
    language->note = rows->language_note;
    for (int i = 0; i < loc->language_count && i < UI_POPUP_OPTIONS; i++) {
        language->options[language->count++] = loc->languages[i].name;
    }
    language->selected = loc->active_index;
}

/* Dim lines no wider than `width`. */
static void wrapped_note(UiLayout *layout, const char *text, float width)
{
    char lines[NOTE_LINES][UI_WRAP_LINE_SIZE];
    int count = ui_wrap_text(layout->ui, text, width, lines, NOTE_LINES);
    for (int line = 0; line < count; line++) {
        ui_layout_label(layout, lines[line], 0.0f, true);
    }
}

static void settings_rows(UiLayout *layout, void *userdata)
{
    SettingsRows *rows = userdata;
    Ui *ui = layout->ui;
    const Theme *theme = &ui->theme;
    float label_width = 0.0f, dropdown_width = 0.0f;
    for (int i = 0; i < SETTING_COUNT; i++) {
        const SettingRow *row = &rows->rows[i];
        label_width = SDL_max(label_width, SDL_ceilf(ui_text_width(ui, row->label)));
        dropdown_width = SDL_max(dropdown_width, ui_dropdown_width(ui, row->options, row->count));
    }
    float note_width = theme->inspector_width;
    ui_layout_measure(layout, SDL_max(note_width, label_width + theme->padding + dropdown_width));
    for (int i = 0; i < SETTING_COUNT; i++) {
        const SettingRow *row = &rows->rows[i];
        if (layout->draw) {
            ui_label(ui, row->label, layout->x, layout->y + layout->text_offset);
            SDL_FRect rect = {layout->x + layout->width - dropdown_width, layout->y, dropdown_width,
                              layout->button_height};
            rows->picked[i] =
                ui_dropdown(ui, DROPDOWN_BASE + i, rect, row->options, row->count, row->selected);
        }
        layout->y += layout->row;
        wrapped_note(layout, row->note, note_width);
        layout->y += theme->gap * 2.0f;
    }
    wrapped_note(layout, tr("settings.saved"), note_width);
}

void settings_window_draw(App *app)
{
    if (!app->show_settings) {
        return;
    }
    SettingsRows rows = {.app = app};
    fill_rows(&rows);
    for (int i = 0; i < SETTING_COUNT; i++) {
        rows.picked[i] = -1;
    }
    bool open = true;
    /* The paused game behind it is veiled, as behind the game menu. */
    ui_fill(&app->ui, (SDL_FRect){0.0f, 0.0f, app->ui.screen_width, app->ui.screen_height},
            app->ui.theme.colors[THEME_DISABLED_VEIL]);
    ui_layout_window(&app->ui, APP_WINDOW_SETTINGS, tr("settings.title"), UI_PLACE_CENTER,
                     settings_rows, &rows, &open);
    /* Applied once the window is drawn: the size changes every measure. */
    if (rows.picked[SETTING_SCALE] >= 0) {
        app_set_ui_scale(app,
                         CLI_UI_SCALE_MIN + rows.picked[SETTING_SCALE] * SETTINGS_UI_SCALE_STEP);
    }
    if (rows.picked[SETTING_PALETTE] >= 0) {
        app_set_palette(app, (SettingsPalette)rows.picked[SETTING_PALETTE]);
    }
    if (rows.picked[SETTING_LANGUAGE] >= 0) {
        app_select_language(app, rows.picked[SETTING_LANGUAGE]);
    }
    if (!open) {
        app->show_settings = false;
        app->show_game_menu = true; /* Back where they were opened from. */
    }
}
