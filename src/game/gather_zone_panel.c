#include "game/gather_zone_panel.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

#define GAP UI_LAYOUT_GAP
#define INDENT UI_LAYOUT_INDENT
#define TEXT_SIZE 160
#define TARGET_BIG_STEP 10 /* Shift+click on -/+. */

/* What the player clicked, applied after the rows. */
typedef enum ActionType {
    ACTION_NONE,
    ACTION_DELETE,
    ACTION_TOGGLE,
    ACTION_CYCLE_MODE,
    ACTION_ADD_TARGET,
    ACTION_TOGGLE_RIPE,
} ActionType;

typedef struct Action {
    ActionType type;
    GatherResource resource;
    int value; /* Target change. */
} Action;

static const char *mode_label(char *out, size_t size, const GatherOrder *order)
{
    char target[16];
    tr_integer(target, sizeof(target), order->target);
    switch ((GatherMode)order->mode) {
    case GATHER_DO_COUNT:
        TR_FORMAT(out, size, tr_plural("gather_zone.mode.do_count", order->target), target);
        break;
    case GATHER_UNTIL: TR_FORMAT(out, size, tr("gather_zone.mode.until"), target); break;
    default: SDL_strlcpy(out, tr("gather_zone.mode.forever"), size); break;
    }
    return out;
}

/* How far the order has got: gathered so far, or how many lie in the colony. */
static const char *progress_text(char *out, size_t size, const Game *game,
                                 GatherResource resource, const GatherOrder *order)
{
    char number[16];
    if (order->mode == GATHER_DO_COUNT) {
        tr_integer(number, sizeof(number), order->done);
        TR_FORMAT(out, size, tr("gather_zone.progress.done"), number);
    } else {
        tr_integer(number, sizeof(number), gather_resource_count(resource, game->items.totals));
        TR_FORMAT(out, size, tr("bill.progress.in_colony"), number);
    }
    return out;
}

/* Check box with the resource's name; when enabled, the mode button, -/+ for
 * the target and the progress on the same row, then the options and
 * warnings below. */
static void resource_rows(UiLayout *layout, const App *app, int index, GatherResource resource,
                          float name_column, Action *action)
{
    Ui *ui = layout->ui;
    const GatherResourceDef *def = gather_resource_def(resource);
    const GatherOrder *order = &gather_zone_get(&app->game.gather_zones, index)->orders[resource];
    const char *name = tr(def->name_key);
    float x = layout->x;
    if (layout->draw && ui_checkbox(ui, x, layout->y, ui_checkbox_width(ui, name),
                                    order->enabled ? UI_CHECKED : UI_UNCHECKED, name)) {
        *action = (Action){ACTION_TOGGLE, resource, 0};
    }
    if (!order->enabled) {
        ui_layout_measure(layout, ui_checkbox_width(ui, name));
        layout->y += layout->row;
        return;
    }
    char mode[TEXT_SIZE], progress[TEXT_SIZE];
    mode_label(mode, sizeof(mode), order);
    progress_text(progress, sizeof(progress), &app->game, resource, order);
    /* Symbols, not words: the same in every language. */
    const char *minus = "−", *plus = "+";
    float small = SDL_max(ui_button_width(ui, minus), ui_button_width(ui, plus));
    bool has_target = order->mode != GATHER_FOREVER;
    x += name_column;
    float mode_width = ui_button_width(ui, mode);
    float targets_width = has_target ? 2.0f * small + 2.0f + GAP : 0.0f;
    ui_layout_measure(layout, name_column + mode_width + GAP + targets_width +
                                  ui_text_width(ui, progress));
    if (ui_layout_button_sized(layout, x, mode_width, mode)) {
        *action = (Action){ACTION_CYCLE_MODE, resource, 0};
    }
    x += mode_width + GAP;
    if (has_target) {
        int step = (SDL_GetModState() & SDL_KMOD_SHIFT) ? TARGET_BIG_STEP : 1;
        if (ui_layout_button_sized(layout, x, small, minus)) {
            *action = (Action){ACTION_ADD_TARGET, resource, -step};
        }
        if (ui_layout_button_sized(layout, x + small + 2.0f, small, plus)) {
            *action = (Action){ACTION_ADD_TARGET, resource, step};
        }
        x += 2.0f * small + 2.0f + GAP;
    }
    if (layout->draw) {
        ui_label_dim(ui, progress, x, layout->y + layout->text_offset);
    }
    layout->y += layout->row;

    if (def->ripe_option) {
        const char *ripe = tr("gather_zone.ripe_only");
        float width = ui_checkbox_width(ui, ripe);
        ui_layout_measure(layout, INDENT + width);
        if (layout->draw && ui_checkbox(ui, layout->x + INDENT, layout->y, width,
                                        order->ripe_only ? UI_CHECKED : UI_UNCHECKED, ripe)) {
            *action = (Action){ACTION_TOGGLE_RIPE, resource, 0};
        }
        layout->y += layout->row;
    }
    if (!game_gather_job_known(&app->game, def->job)) {
        ui_layout_label(layout, tr("hud.tool.unavailable"), INDENT, true);
    }
    if (resource == GATHER_FLINT &&
        gather_zone_flint_cells(&app->game.gather_zones, &app->game.map, index) == 0) {
        ui_layout_label(layout, tr("gather_zone.warning.no_flint"), INDENT, true);
    }
}

typedef struct ZoneRows {
    const App *app;
    int index;
    Action action;
} ZoneRows;

static void zone_rows(UiLayout *layout, void *userdata)
{
    ZoneRows *rows = userdata;
    const App *app = rows->app;
    int index = rows->index;
    Action *action = &rows->action;
    Ui *ui = layout->ui;
    const GatherZone *zone = gather_zone_get(&app->game.gather_zones, index);
    char number[16], tiles[TEXT_SIZE];
    tr_integer(number, sizeof(number), zone->cell_count);
    TR_FORMAT(tiles, sizeof(tiles), tr_plural("gather_zone.tiles", zone->cell_count), number);
    if (ui_layout_label_with_button(layout, tiles, tr("gather_zone.delete"))) {
        *action = (Action){ACTION_DELETE, GATHER_RESOURCE_COUNT, 0};
    }
    ui_layout_label(layout, tr("gather_zone.hint"), 0.0f, true);

    float name_column = 0.0f;
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        const char *name = tr(gather_resource_def((GatherResource)r)->name_key);
        name_column = SDL_max(name_column, ui_checkbox_width(ui, name));
    }
    name_column = SDL_ceilf(name_column) + GAP;
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        resource_rows(layout, app, index, (GatherResource)r, name_column, action);
    }
}

static void apply(App *app, int index, const Action *action)
{
    GatherZones *zones = &app->game.gather_zones;
    const GatherZone *zone = gather_zone_get(zones, index);
    if (zone == NULL) {
        return;
    }
    GatherResource resource = action->resource;
    const GatherOrder *order = (unsigned)resource < GATHER_RESOURCE_COUNT
                                   ? &zone->orders[resource]
                                   : NULL;
    switch (action->type) {
    case ACTION_DELETE:
        gather_zone_delete(zones, index);
        app->selected_gather_zone = GATHER_ZONE_NONE;
        break;
    case ACTION_TOGGLE:
        if (order != NULL) {
            gather_zone_set_enabled(zones, index, resource, !order->enabled);
        }
        break;
    case ACTION_CYCLE_MODE:
        if (order != NULL) {
            gather_zone_set_mode(zones, index, resource,
                                 (GatherMode)((order->mode + 1) % GATHER_MODE_COUNT));
        }
        break;
    case ACTION_ADD_TARGET:
        if (order != NULL) {
            gather_zone_set_target(zones, index, resource, order->target + action->value);
        }
        break;
    case ACTION_TOGGLE_RIPE:
        if (order != NULL) {
            gather_zone_set_ripe_only(zones, index, resource, !order->ripe_only);
        }
        break;
    case ACTION_NONE: break;
    }
}

void gather_zone_panel_draw(App *app)
{
    int index = app->selected_gather_zone;
    const GatherZone *zone = gather_zone_get(&app->game.gather_zones, index);
    if (zone == NULL) {
        return;
    }
    char number[16], title[TEXT_SIZE];
    tr_integer(number, sizeof(number), zone->number);
    TR_FORMAT(title, sizeof(title), tr("gather_zone.name"), number);
    ZoneRows rows = {app, index, {ACTION_NONE, GATHER_RESOURCE_COUNT, 0}};
    bool open = true;
    ui_layout_window(&app->ui, APP_WINDOW_GATHER_ZONE, title, UI_PLACE_BOTTOM_RIGHT, zone_rows,
                     &rows, &open);
    if (rows.action.type != ACTION_NONE) {
        apply(app, index, &rows.action);
    }
    if (!open) {
        app->selected_gather_zone = GATHER_ZONE_NONE;
    }
}
