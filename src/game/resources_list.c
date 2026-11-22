#include "game/resources_list.h"

#include "core/gather_zone.h"
#include "core/i18n.h"
#include "game/layout.h"
#include "game/render.h"

#define TEXT_SIZE 128
#define MAX_ROWS (ITEM_CATEGORY_COUNT + ITEM_TYPE_COUNT)

typedef struct Row {
    bool header;
    ItemCategory category;
    ItemType type; /* Item lines. */
} Row;

typedef struct ListLayout {
    float x, y, width;
    float row_height;
    float padding;
    float indent; /* Item lines, under the header's arrow. */
} ListLayout;

static long category_total(const App *app, ItemCategory category)
{
    long total = 0;
    for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
        if (item_def((ItemType)type)->category == category) {
            total += app->stock[type];
        }
    }
    return total;
}

/* The groups with something in storage, each followed by its items unless folded. What lies
 * on a workstation, in a hauler's hands or on the ground is not listed: only the items in the
 * stockpiles (and, one day, in containers). */
static int list_rows(const App *app, Row rows[MAX_ROWS])
{
    int count = 0;
    for (int category = 0; category < ITEM_CATEGORY_COUNT; category++) {
        if (category_total(app, (ItemCategory)category) == 0) {
            continue;
        }
        rows[count++] = (Row){true, (ItemCategory)category, ITEM_NONE};
        if (app->resources_folded[category]) {
            continue;
        }
        for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
            if (item_def((ItemType)type)->category == (ItemCategory)category &&
                app->stock[type] > 0) {
                rows[count++] = (Row){false, (ItemCategory)category, (ItemType)type};
            }
        }
    }
    return count;
}

/* `count` items, as the player reads numbers. */
static void number(long count, char *out, size_t size)
{
    tr_integer(out, size, count);
}

/* A label on the left (cut short to leave room for the count) and a count
 * right aligned, on the row at `y`. */
static void row_text(Ui *ui, const ListLayout *list, float left, float y, const char *label,
                     long count, bool dim)
{
    char count_text[32], fitted[TEXT_SIZE];
    number(count, count_text, sizeof(count_text));
    float line = (float)text_line_height(ui->text);
    float text_y = y + SDL_floorf((list->row_height - line) * 0.5f);
    float count_width = ui_text_width(ui, count_text);
    float right = list->x + list->width - list->padding;
    ui_fit_text(ui, label, right - count_width - ui->theme.gap - left, fitted, sizeof(fitted));
    if (dim) {
        ui_label_dim(ui, fitted, left, text_y);
    } else {
        ui_label(ui, fitted, left, text_y);
    }
    ui_label(ui, count_text, right - count_width, text_y);
}

/* "Food for 3 days" for the food of the `counted` types in stock. */
static void food_days_text(const App *app, ItemTypeMask counted, char *out, size_t size)
{
    long days = gather_food_days(counted, app->stock, game_daily_food(&app->game));
    if (days == 0) {
        SDL_strlcpy(out, tr("resources.food_days.less"), size);
        return;
    }
    char days_text[32];
    number(days, days_text, sizeof(days_text));
    TR_FORMAT(out, size, tr_plural("resources.food_days", days), days_text);
}

static void header_tooltip(App *app, ItemCategory category)
{
    char food[TEXT_SIZE];
    const char *lines[3];
    int count = 0;
    lines[count++] = tr(item_category_name_key(category));
    if (category == ITEM_CATEGORY_FOOD) {
        food_days_text(app, item_types_in_category(ITEM_CATEGORY_FOOD), food, sizeof(food));
        lines[count++] = food;
    }
    lines[count++] = tr(app->resources_folded[category] ? "resources.unfold" : "resources.fold");
    ui_tooltip(&app->ui, lines, count);
}

/* The weight of the items of a type stored, and the days of food they make. */
static void item_tooltip(App *app, ItemType type)
{
    char texts[3][TEXT_SIZE], value[32];
    const char *lines[3];
    int count = 0;
    lines[count++] = tr(item_def(type)->name_key);
    tr_decimal(value, sizeof(value), item_stack_weight(type, (int)app->stock[type]), 1);
    TR_FORMAT(texts[count], sizeof(texts[count]), tr("resources.weight"), value);
    lines[count] = texts[count];
    count++;
    if (item_def(type)->nutrition > 0.0f) {
        food_days_text(app, item_types_of(type), texts[count], sizeof(texts[count]));
        lines[count] = texts[count];
        count++;
    }
    ui_tooltip(&app->ui, lines, count);
}

/* Draws a row; returns the group to fold or unfold, or -1. */
static int draw_row(App *app, const ListLayout *list, const Row *row, float y)
{
    Ui *ui = &app->ui;
    SDL_FRect rect = {list->x, y, list->width, list->row_height};
    bool hovered = ui_hovered(ui, rect);
    if (row->header) {
        if (hovered) {
            ui_fill(ui, rect, ui->theme.colors[THEME_BUTTON_HOVER]);
        }
        bool folded = app->resources_folded[row->category];
        float line = (float)text_line_height(ui->text);
        float text_y = y + SDL_floorf((list->row_height - line) * 0.5f);
        ui_label_dim(ui, folded ? "▸" : "▾", list->x + list->padding, text_y);
        row_text(ui, list, list->x + list->padding + list->indent, y,
                 tr(item_category_name_key(row->category)), category_total(app, row->category),
                 false);
        if (hovered) {
            header_tooltip(app, row->category);
        }
        return ui_take_click(ui, rect) == SDL_BUTTON_LEFT ? (int)row->category : -1;
    }
    float icon = list->row_height;
    float left = list->x + list->padding + list->indent;
    SDL_FRect well = {left, y + 1.0f, icon - 2.0f, icon - 2.0f};
    ui_fill(ui, well, ui->theme.colors[THEME_ICON_WELL]);
    render_item_icon(app->renderer, app->sprites, well, row->type);
    row_text(ui, list, left + icon + ui->theme.gap, y, tr(item_def(row->type)->name_key),
             app->stock[row->type], true);
    if (hovered) {
        item_tooltip(app, row->type);
    }
    return -1;
}

void resources_list_draw(App *app, float bottom)
{
    Ui *ui = &app->ui;
    const Theme *theme = &ui->theme;
    ScreenLayout screen;
    layout_screen(ui, &screen);

    float line = (float)text_line_height(ui->text);
    ListLayout list = {
        .x = theme->margin,
        .y = theme->margin,
        .width = screen.corner_width - theme->margin - theme->gap,
        .row_height = SDL_floorf(line + theme->gap),
        .padding = theme->padding,
        .indent = SDL_ceilf(ui_text_width(ui, "▾")) + theme->gap,
    };
    Row rows[MAX_ROWS + 1];
    int count = list_rows(app, rows);
    bool empty = count == 0;
    if (empty) {
        count = 1; /* "Nothing in stock". */
    }
    int room = (int)SDL_floorf((bottom - list.y - 2.0f * list.padding) / list.row_height);
    if (room < 2) {
        return;
    }
    int shown = count <= room ? count : room - 1; /* The last line says how many are left. */
    int lines = count <= room ? count : room;
    SDL_FRect panel = {list.x, list.y, list.width,
                       (float)lines * list.row_height + 2.0f * list.padding};
    ui_panel_colored(ui, panel, THEME_HUD_PANEL);

    float y = list.y + list.padding;
    if (empty) {
        float text_y = y + SDL_floorf((list.row_height - line) * 0.5f);
        char fitted[TEXT_SIZE];
        ui_fit_text(ui, tr("resources.none"), list.width - 2.0f * list.padding, fitted,
                    sizeof(fitted));
        ui_label_dim(ui, fitted, list.x + list.padding, text_y);
        if (ui_hovered(ui, (SDL_FRect){list.x, y, list.width, list.row_height})) {
            const char *tip[2] = {tr("resources.none"), tr("resources.none.description")};
            ui_tooltip(ui, tip, 2);
        }
        return;
    }
    int toggled = -1;
    for (int i = 0; i < shown; i++) {
        int clicked = draw_row(app, &list, &rows[i], y);
        if (clicked >= 0) {
            toggled = clicked;
        }
        y += list.row_height;
    }
    if (shown < count) {
        char left[32], text[TEXT_SIZE];
        number(count - shown, left, sizeof(left));
        TR_FORMAT(text, sizeof(text), tr_plural("resources.more", count - shown), left);
        float text_y = y + SDL_floorf((list.row_height - line) * 0.5f);
        ui_label_dim(ui, text, list.x + list.padding, text_y);
        if (ui_hovered(ui, (SDL_FRect){list.x, y, list.width, list.row_height})) {
            const char *tip[1] = {tr("resources.more.description")};
            ui_tooltip(ui, tip, 1);
        }
    }
    if (toggled >= 0) {
        app->resources_folded[toggled] = !app->resources_folded[toggled];
    }
}
