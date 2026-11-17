#include "game/items_panel.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

/* What the player clicked in the drawing pass, applied after it. */
typedef struct ItemRows {
    ItemType clicked; /* ITEM_NONE: none. */
    bool max;         /* A full stack rather than one item. */
} ItemRows;

static void item_rows(UiLayout *layout, void *userdata)
{
    ItemRows *rows = userdata;
    const char *one_label = tr("debug.items.one");
    const char *max_label = tr("debug.items.max");
    float one_width = ui_button_width(layout->ui, one_label);
    float max_width = ui_button_width(layout->ui, max_label);
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        const char *name = tr(item_def((ItemType)type)->name_key);
        ui_layout_measure(layout, ui_text_width(layout->ui, name) + UI_LAYOUT_GAP * 2.0f +
                                      one_width + max_width);
        float max_x = layout->x + layout->width - max_width;
        if (ui_layout_button_sized(layout, max_x, max_width, max_label)) {
            rows->clicked = (ItemType)type;
            rows->max = true;
        }
        if (ui_layout_button_sized(layout, max_x - UI_LAYOUT_GAP * 0.5f - one_width, one_width,
                                   one_label)) {
            rows->clicked = (ItemType)type;
            rows->max = false;
        }
        if (layout->draw) {
            ui_label(layout->ui, name, layout->x, layout->y + layout->text_offset);
        }
        layout->y += layout->row;
    }
}

void items_panel_draw(App *app)
{
    if (!app->show_items_menu) {
        return;
    }
    ItemRows rows = {.clicked = ITEM_NONE};
    bool open = true;
    ui_layout_window(&app->ui, APP_WINDOW_ITEMS, tr("debug.items.title"), UI_PLACE_TOP_CENTER,
                     item_rows, &rows, &open);
    if (rows.clicked != ITEM_NONE) {
        app->debug_item = rows.clicked;
        app->debug_item_max = rows.max;
        app->debug_mode = APP_DEBUG_SPAWN;
        app->tool = APP_TOOL_DEBUG;
        app->left_button_down = false;
        app->box_selecting = false;
    }
    if (!open) {
        app->show_items_menu = false;
    }
}
