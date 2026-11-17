#include "game/log_window.h"

#include "core/i18n.h"
#include "game/colonist_bar.h"
#include "game/layout.h"
#include "game/ui_layout.h"

#define TEXT_SIZE 192

void log_notice_text(const Notice *notice, char *out, size_t size)
{
    /* Not every notice is about a knowledge (a roof falling in). */
    const KnowledgeDef *def = knowledge_def(notice->knowledge);
    TR_FORMAT(out, size, tr(notice_text_key((NoticeType)notice->type)),
              def != NULL ? tr(def->name_key) : "", notice->name);
}

/* "Summer 11, year 1 · 06:04". */
static void date_text(uint64_t tick, char *out, size_t size)
{
    GameDate date = game_date_from_tick(tick);
    char day[16], year[16], hours[8], minutes[8];
    tr_integer(day, sizeof(day), date.day);
    tr_integer(year, sizeof(year), date.year);
    SDL_snprintf(hours, sizeof(hours), "%02d", date.hour);
    SDL_snprintf(minutes, sizeof(minutes), "%02d", date.minute);
    TR_FORMAT(out, size, tr("hud.date"), day, tr(season_name_key(date.season)), year, hours,
              minutes);
}

typedef struct LogRows {
    const Game *game;
} LogRows;

static void log_rows(UiLayout *layout, void *userdata)
{
    const LogRows *rows = userdata;
    const Notices *notices = &rows->game->notices;
    Ui *ui = layout->ui;
    int shown = 0;
    /* The newest first; the ring holds the last NOTICE_MAX. */
    for (uint32_t i = notices->count; i > 0; i--) {
        const Notice *notice = notices_get(notices, i - 1);
        if (notice == NULL) {
            break; /* The older ones are forgotten. */
        }
        char date[TEXT_SIZE], text[TEXT_SIZE];
        date_text(notice->tick, date, sizeof(date));
        log_notice_text(notice, text, sizeof(text));
        ui_layout_label(layout, date, 0.0f, true);
        ui_layout_label(layout, text, ui->theme.window_padding, false);
        shown++;
    }
    if (shown == 0) {
        ui_layout_label(layout, tr("log.empty"), 0.0f, true);
    }
}

void log_window_draw(App *app)
{
    if (!app->show_log) {
        return;
    }
    ScreenLayout screen;
    layout_screen(&app->ui, &screen);
    UiDock dock = {0.0f, screen.bar_top, colonist_bar_bottom(app) + app->ui.theme.margin,
                   app->ui.theme.inspector_width};
    LogRows rows = {&app->game};
    bool open = true;
    ui_layout_docked(&app->ui, APP_WINDOW_LOG, tr("log.title"), &dock, log_rows, &rows, &open);
    if (!open) {
        app->show_log = false;
    }
}
