#include "game/stockpile_panel.h"

#include "core/i18n.h"
#include "game/inspector.h"

#define GAP UI_LAYOUT_GAP
#define ITEM_INDENT UI_LAYOUT_INDENT
#define ITEM_COLUMNS 2
#define TEXT_SIZE 128
#define SLIDER_MIN_WIDTH 200.0f
#define HIT_POINTS_STEP 5 /* Percent per slider step. */
#define QUALITY_SLIDER_ID 1
#define HIT_POINTS_SLIDER_ID 2

typedef struct StockpileRows {
    Stockpiles *stockpiles;
    int index;
    const Stockpile *stockpile;
    ItemTypeMask allowed; /* Changed by the check boxes, applied after the rows. */
} StockpileRows;

static UiCheck check_state(ItemTypeMask allowed, ItemTypeMask mask)
{
    ItemTypeMask on = allowed & mask;
    return on == mask ? UI_CHECKED : (on != 0 ? UI_PARTIAL : UI_UNCHECKED);
}

/* Size and priority (1 is the highest), with − and + buttons. */
static void summary_rows(UiLayout *layout, void *userdata)
{
    StockpileRows *rows = userdata;
    const Stockpile *stockpile = rows->stockpile;
    Ui *ui = layout->ui;
    char number[16], text[TEXT_SIZE];
    tr_integer(number, sizeof(number), stockpile->cell_count);
    TR_FORMAT(text, sizeof(text), tr_plural("stockpile.tiles", stockpile->cell_count), number);
    ui_layout_label(layout, text, 0.0f, false);

    tr_integer(number, sizeof(number), stockpile->priority);
    TR_FORMAT(text, sizeof(text), tr("stockpile.priority"), number);
    /* Symbols, not words: the same in every language. */
    const char *minus = "−", *plus = "+", *hint = tr("stockpile.priority_hint");
    float small = SDL_max(ui_button_width(ui, minus), ui_button_width(ui, plus));
    float bx = layout->x + ui_text_width(ui, text) + GAP;
    ui_layout_measure(layout, bx - layout->x + 2.0f * small + GAP + ui_text_width(ui, hint));
    if (ui_layout_button_sized(layout, bx, small, minus)) {
        stockpile_set_priority(rows->stockpiles, rows->index, stockpile->priority - 1);
    }
    if (ui_layout_button_sized(layout, bx + small + 2.0f, small, plus)) {
        stockpile_set_priority(rows->stockpiles, rows->index, stockpile->priority + 1);
    }
    if (layout->draw) {
        ui_label(ui, text, layout->x, layout->y + layout->text_offset);
        ui_label_dim(ui, hint, bx + 2.0f * small + GAP, layout->y + layout->text_offset);
    }
    layout->y += layout->row;
}

/* Accepted quality grades and hit points (steps of HIT_POINTS_STEP %). */
static void condition_rows(UiLayout *layout, StockpileRows *rows)
{
    const Stockpile *stockpile = rows->stockpile;
    Ui *ui = layout->ui;
    char text[TEXT_SIZE], number[16], low[32], high[32];
    TR_FORMAT(text, sizeof(text), tr("stockpile.quality"),
              tr(item_quality_name_key((ItemQuality)stockpile->quality.min)),
              tr(item_quality_name_key((ItemQuality)stockpile->quality.max)));
    ui_layout_label(layout, text, 0.0f, false);
    ui_layout_measure(layout, SLIDER_MIN_WIDTH);
    int quality_low = stockpile->quality.min, quality_high = stockpile->quality.max;
    if (layout->draw &&
        ui_range_slider(ui, QUALITY_SLIDER_ID,
                        (SDL_FRect){layout->x, layout->y, layout->width, layout->row},
                        ITEM_QUALITY_COUNT, &quality_low, &quality_high)) {
        stockpile_set_quality_range(rows->stockpiles, rows->index,
                                    (IntRange){quality_low, quality_high});
    }
    layout->y += layout->row;

    tr_integer(number, sizeof(number), stockpile->hit_points.min);
    TR_FORMAT(low, sizeof(low), tr("unit.percent"), number);
    tr_integer(number, sizeof(number), stockpile->hit_points.max);
    TR_FORMAT(high, sizeof(high), tr("unit.percent"), number);
    TR_FORMAT(text, sizeof(text), tr("stockpile.hit_points"), low, high);
    ui_layout_label(layout, text, 0.0f, false);
    int hp_low = stockpile->hit_points.min / HIT_POINTS_STEP;
    int hp_high = (stockpile->hit_points.max + HIT_POINTS_STEP - 1) / HIT_POINTS_STEP;
    if (layout->draw &&
        ui_range_slider(ui, HIT_POINTS_SLIDER_ID,
                        (SDL_FRect){layout->x, layout->y, layout->width, layout->row},
                        100 / HIT_POINTS_STEP + 1, &hp_low, &hp_high)) {
        stockpile_set_hit_points_range(
            rows->stockpiles, rows->index,
            (IntRange){hp_low * HIT_POINTS_STEP, hp_high * HIT_POINTS_STEP});
    }
    layout->y += layout->row;
}

/* "Allowed items" with All and None, then each category (tri-state) with its
 * item types in ITEM_COLUMNS columns under it. */
static void filter_rows(UiLayout *layout, StockpileRows *rows)
{
    Ui *ui = layout->ui;
    ItemTypeMask allowed = rows->stockpile->allowed;
    const char *label = tr("stockpile.allowed"), *all = tr("stockpile.allow_all"),
               *none = tr("stockpile.allow_none");
    float all_width = ui_button_width(ui, all), none_width = ui_button_width(ui, none);
    ui_layout_measure(layout, ui_text_width(ui, label) + GAP + all_width + GAP + none_width);
    float right = layout->x + layout->width;
    if (ui_layout_button(layout, right - none_width, none)) {
        rows->allowed = 0;
    }
    if (ui_layout_button(layout, right - none_width - GAP - all_width, all)) {
        rows->allowed = item_types_all();
    }
    if (layout->draw) {
        ui_label(ui, label, layout->x, layout->y + layout->text_offset);
    }
    layout->y += layout->row;

    float column = 0.0f;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        column = SDL_max(column, ui_checkbox_width(ui, tr(item_def((ItemType)type)->name_key)));
    }
    column = SDL_ceilf(column) + GAP;
    ui_layout_measure(layout, ITEM_INDENT + (float)ITEM_COLUMNS * column);
    for (int category = 0; category < ITEM_CATEGORY_COUNT; category++) {
        ItemTypeMask mask = item_types_in_category((ItemCategory)category);
        UiCheck state = check_state(allowed, mask);
        const char *name = tr(item_category_name_key((ItemCategory)category));
        ui_layout_measure(layout, ui_checkbox_width(ui, name));
        if (layout->draw && ui_checkbox(ui, layout->x, layout->y, layout->width, state, name)) {
            rows->allowed = state == UI_CHECKED ? rows->allowed & ~mask : rows->allowed | mask;
        }
        layout->y += layout->row;
        int shown = 0;
        for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
            const ItemDef *def = item_def((ItemType)type);
            if ((int)def->category != category) {
                continue;
            }
            ItemTypeMask bit = ITEM_TYPE_BIT(type);
            float cx = layout->x + ITEM_INDENT + (float)(shown % ITEM_COLUMNS) * column;
            if (layout->draw &&
                ui_checkbox(ui, cx, layout->y, column - GAP,
                            (allowed & bit) ? UI_CHECKED : UI_UNCHECKED, tr(def->name_key))) {
                rows->allowed ^= bit;
            }
            if (++shown % ITEM_COLUMNS == 0) {
                layout->y += layout->row;
            }
        }
        if (shown % ITEM_COLUMNS != 0) {
            layout->y += layout->row;
        }
    }
}

static void storage_rows(UiLayout *layout, void *userdata)
{
    StockpileRows *rows = userdata;
    condition_rows(layout, rows);
    filter_rows(layout, rows);
}

void stockpile_panel_draw(App *app)
{
    Stockpiles *stockpiles = &app->game.stockpiles;
    int index = app->selected_stockpile;
    const Stockpile *stockpile = stockpile_get(stockpiles, index);
    if (stockpile == NULL) {
        return;
    }
    char number[16], title[TEXT_SIZE];
    tr_integer(number, sizeof(number), stockpile->number);
    TR_FORMAT(title, sizeof(title), tr("stockpile.name"), number);
    StockpileRows rows = {stockpiles, index, stockpile, stockpile->allowed};
    bool open = inspector_box(app, INSPECT_STOCKPILE, title, summary_rows, &rows);
    const char *tabs[1] = {tr("inspector.tab.storage")};
    if (inspector_tabs(app, tabs, 1) == 0) {
        inspector_tab_panel(app, storage_rows, &rows);
    }
    bool deleted = inspector_delete_action(app, 1, tr("stockpile.delete.description"));
    inspector_build_like_action(app, 0);
    if (app->selected_stockpile != index) {
        return; /* Building another: the zone is no longer selected. */
    }
    if (rows.allowed != stockpile->allowed) {
        stockpile_set_allowed(stockpiles, index, rows.allowed);
    }
    if (deleted) {
        stockpile_delete(stockpiles, index);
    }
    if (deleted || !open) {
        app->selected_stockpile = STOCKPILE_NONE;
    }
}
