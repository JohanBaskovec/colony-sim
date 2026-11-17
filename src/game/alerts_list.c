#include "game/alerts_list.h"

#include "core/alert.h"
#include "core/i18n.h"

#define TEXT_SIZE 160

static void line_text(const Alert *alert, char *out, size_t size)
{
    char count[16];
    tr_integer(count, sizeof(count), alert->count);
    TR_FORMAT(out, size, tr_plural(alert_name_key(alert->type), alert->count), count);
}

static void line_tooltip(App *app, const Alert *alert, const char *text)
{
    const char *lines[3] = {text, tr(alert_description_key(alert->type))};
    int count = 2;
    char jump[TEXT_SIZE];
    const Colonist *first = game_colonist_const(&app->game, alert->first);
    if (first != NULL) {
        TR_FORMAT(jump, sizeof(jump), tr("alert.jump"), first->name);
        lines[count++] = jump;
    }
    ui_tooltip(&app->ui, lines, count);
}

void alerts_list_draw(App *app, float right, float bottom)
{
    Ui *ui = &app->ui;
    const Theme *theme = &ui->theme;
    Alerts alerts;
    alerts_compute(&app->game, &alerts);
    float line = (float)text_line_height(ui->text);
    float mark_width = ui_text_width(ui, "!") + theme->gap;
    float height = line + theme->padding;
    float y = bottom;
    /* Stacked upwards: the most pressing one at the bottom, next to the date. */
    for (int i = 0; i < alerts.count; i++) {
        const Alert *alert = &alerts.list[i];
        char text[TEXT_SIZE];
        line_text(alert, text, sizeof(text));
        float width = mark_width + ui_text_width(ui, text) + 2.0f * theme->padding;
        y -= height;
        SDL_FRect rect = {right - width, y, width, height};
        ui_panel_colored(ui, rect, THEME_HUD_PANEL);
        float text_y = rect.y + SDL_floorf(theme->padding * 0.5f);
        ui_label_colored(ui, "!", rect.x + theme->padding, text_y, THEME_WARNING);
        ui_label(ui, text, rect.x + theme->padding + mark_width, text_y);
        if (ui_hovered(ui, rect)) {
            line_tooltip(app, alert, text);
        }
        if (ui_take_click(ui, rect) == SDL_BUTTON_LEFT && !entity_id_is_none(alert->first)) {
            app_select_colonist(app, alert->first, false);
            app_focus_colonist(app, alert->first);
        }
        y -= theme->gap;
    }
}
