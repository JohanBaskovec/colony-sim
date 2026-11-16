#include "game/stockpile_panel.h"

#include "core/i18n.h"

#define GAP 8.0f
#define ITEM_INDENT 20.0f
#define ITEM_COLUMNS 2
#define TEXT_SIZE 128
#define SLIDER_MIN_WIDTH 200.0f
#define HIT_POINTS_STEP 5 /* Percent per slider step. */
#define QUALITY_SLIDER_ID 1
#define HIT_POINTS_SLIDER_ID 2

static int category_type_count(ItemCategory category)
{
    int count = 0;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        count += item_def((ItemType)type)->category == category;
    }
    return count;
}

static UiCheck check_state(ItemTypeMask allowed, ItemTypeMask mask)
{
    ItemTypeMask on = allowed & mask;
    return on == mask ? UI_CHECKED : (on != 0 ? UI_PARTIAL : UI_UNCHECKED);
}

void stockpile_panel_draw(App *app)
{
    Stockpiles *stockpiles = &app->game.stockpiles;
    int index = app->selected_stockpile;
    const Stockpile *stockpile = stockpile_get(stockpiles, index);
    if (stockpile == NULL) {
        return;
    }
    Ui *ui = &app->ui;
    char number[16], title[TEXT_SIZE], tiles[TEXT_SIZE], priority[TEXT_SIZE];
    tr_integer(number, sizeof(number), stockpile->number);
    TR_FORMAT(title, sizeof(title), tr("stockpile.name"), number);
    tr_integer(number, sizeof(number), stockpile->cell_count);
    TR_FORMAT(tiles, sizeof(tiles), tr_plural("stockpile.tiles", stockpile->cell_count), number);
    tr_integer(number, sizeof(number), stockpile->priority);
    TR_FORMAT(priority, sizeof(priority), tr("stockpile.priority"), number);
    const char *hint = tr("stockpile.priority_hint");
    const char *allowed_label = tr("stockpile.allowed");
    const char *all = tr("stockpile.allow_all");
    const char *none = tr("stockpile.allow_none");
    const char *delete_label = tr("stockpile.delete");
    char low[32], high[32], quality[TEXT_SIZE], hit_points[TEXT_SIZE];
    TR_FORMAT(quality, sizeof(quality), tr("stockpile.quality"),
              tr(item_quality_name_key((ItemQuality)stockpile->quality.min)),
              tr(item_quality_name_key((ItemQuality)stockpile->quality.max)));
    tr_integer(number, sizeof(number), stockpile->hit_points.min);
    TR_FORMAT(low, sizeof(low), tr("unit.percent"), number);
    tr_integer(number, sizeof(number), stockpile->hit_points.max);
    TR_FORMAT(high, sizeof(high), tr("unit.percent"), number);
    TR_FORMAT(hit_points, sizeof(hit_points), tr("stockpile.hit_points"), low, high);
    /* Symbols, not words: the same in every language. */
    const char *minus = "−", *plus = "+";

    /* Measure. */
    float row = ui_row_height(ui);
    float small = SDL_max(ui_button_width(ui, minus), ui_button_width(ui, plus));
    float width = ui_text_width(ui, tiles) + GAP + ui_button_width(ui, delete_label);
    width = SDL_max(width, ui_text_width(ui, priority) + GAP + 2.0f * small + GAP +
                               ui_text_width(ui, hint));
    width = SDL_max(width, ui_text_width(ui, allowed_label) + GAP + ui_button_width(ui, all) + GAP +
                               ui_button_width(ui, none));
    width = SDL_max(width, SDL_max(SLIDER_MIN_WIDTH, ui_text_width(ui, quality)));
    width = SDL_max(width, ui_text_width(ui, hit_points));
    /* Item types of a category in ITEM_COLUMNS columns, under the category. */
    int rows = 7;
    float column = 0.0f;
    for (int category = 0; category < ITEM_CATEGORY_COUNT; category++) {
        width = SDL_max(width,
                        ui_checkbox_width(ui, tr(item_category_name_key((ItemCategory)category))));
        rows += 1 + (category_type_count((ItemCategory)category) + ITEM_COLUMNS - 1) / ITEM_COLUMNS;
    }
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        column = SDL_max(column, ui_checkbox_width(ui, tr(item_def((ItemType)type)->name_key)));
    }
    column = SDL_ceilf(column) + GAP;
    width = SDL_ceilf(SDL_max(width, ITEM_INDENT + (float)ITEM_COLUMNS * column));
    bool open = true;
    SDL_FPoint origin = ui_window_begin(ui, APP_WINDOW_STOCKPILE, title, width, (float)rows * row,
                                        UI_PLACE_BOTTOM_RIGHT, &open);
    width = SDL_max(width, ui_window_content_width(ui));
    float x = origin.x, y = origin.y;
    float line = (float)text_line_height(app->text);
    float text_offset = SDL_floorf((row - line) * 0.5f);
    float button_height = row - 2.0f;

    /* Tiles and delete button. */
    ui_label(ui, tiles, x, y + text_offset);
    float delete_width = ui_button_width(ui, delete_label);
    bool deleted = ui_button(
        ui, (SDL_FRect){x + width - delete_width, y, delete_width, button_height}, delete_label);
    y += row;

    /* Priority: 1 is the highest. */
    ui_label(ui, priority, x, y + text_offset);
    float bx = x + ui_text_width(ui, priority) + GAP;
    if (ui_button(ui, (SDL_FRect){bx, y, small, button_height}, minus)) {
        stockpile_set_priority(stockpiles, index, stockpile->priority - 1);
    }
    if (ui_button(ui, (SDL_FRect){bx + small + 2.0f, y, small, button_height}, plus)) {
        stockpile_set_priority(stockpiles, index, stockpile->priority + 1);
    }
    ui_label(ui, hint, bx + 2.0f * small + GAP, y + text_offset);
    y += row;

    /* Condition filters: quality grades, hit points in steps of HIT_POINTS_STEP %. */
    ui_label(ui, quality, x, y + text_offset);
    y += row;
    int quality_low = stockpile->quality.min, quality_high = stockpile->quality.max;
    if (ui_range_slider(ui, QUALITY_SLIDER_ID, (SDL_FRect){x, y, width, row}, ITEM_QUALITY_COUNT,
                        &quality_low, &quality_high)) {
        stockpile_set_quality_range(stockpiles, index, (IntRange){quality_low, quality_high});
    }
    y += row;
    ui_label(ui, hit_points, x, y + text_offset);
    y += row;
    int hp_low = stockpile->hit_points.min / HIT_POINTS_STEP;
    int hp_high = (stockpile->hit_points.max + HIT_POINTS_STEP - 1) / HIT_POINTS_STEP;
    if (ui_range_slider(ui, HIT_POINTS_SLIDER_ID, (SDL_FRect){x, y, width, row},
                        100 / HIT_POINTS_STEP + 1, &hp_low, &hp_high)) {
        stockpile_set_hit_points_range(
            stockpiles, index, (IntRange){hp_low * HIT_POINTS_STEP, hp_high * HIT_POINTS_STEP});
    }
    y += row;

    /* Filters. */
    ItemTypeMask allowed = stockpile->allowed;
    ItemTypeMask new_allowed = allowed;
    ui_label(ui, allowed_label, x, y + text_offset);
    float none_width = ui_button_width(ui, none);
    float all_width = ui_button_width(ui, all);
    if (ui_button(ui, (SDL_FRect){x + width - none_width, y, none_width, button_height}, none)) {
        new_allowed = 0;
    }
    if (ui_button(
            ui, (SDL_FRect){x + width - none_width - GAP - all_width, y, all_width, button_height},
            all)) {
        new_allowed = item_types_all();
    }
    y += row;
    for (int category = 0; category < ITEM_CATEGORY_COUNT; category++) {
        ItemTypeMask mask = item_types_in_category((ItemCategory)category);
        UiCheck state = check_state(allowed, mask);
        if (ui_checkbox(ui, x, y, width, state,
                        tr(item_category_name_key((ItemCategory)category)))) {
            new_allowed = state == UI_CHECKED ? new_allowed & ~mask : new_allowed | mask;
        }
        y += row;
        int shown = 0;
        for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
            const ItemDef *def = item_def((ItemType)type);
            if ((int)def->category != category) {
                continue;
            }
            ItemTypeMask bit = ITEM_TYPE_BIT(type);
            float cx = x + ITEM_INDENT + (float)(shown % ITEM_COLUMNS) * column;
            if (ui_checkbox(ui, cx, y, column - GAP, (allowed & bit) ? UI_CHECKED : UI_UNCHECKED,
                            tr(def->name_key))) {
                new_allowed ^= bit;
            }
            if (++shown % ITEM_COLUMNS == 0) {
                y += row;
            }
        }
        if (shown % ITEM_COLUMNS != 0) {
            y += row;
        }
    }
    ui_window_end(ui);
    if (new_allowed != allowed) {
        stockpile_set_allowed(stockpiles, index, new_allowed);
    }
    if (deleted || !open) {
        app->selected_stockpile = STOCKPILE_NONE;
    }
    if (deleted) {
        stockpile_delete(stockpiles, index);
        app->selected_stockpile = STOCKPILE_NONE;
    }
}
