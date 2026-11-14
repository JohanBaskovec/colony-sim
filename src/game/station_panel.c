#include "game/station_panel.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

#define GAP UI_LAYOUT_GAP
#define INDENT UI_LAYOUT_INDENT
#define TEXT_SIZE 160
#define TARGET_BIG_STEP 10 /* Shift+click on -/+. */

/* What the player clicked, applied after the rows (deleting shifts bills). */
typedef enum ActionType {
    ACTION_NONE,
    ACTION_DELETE_STATION,
    ACTION_DELETE_BILL,
    ACTION_CYCLE_MODE,
    ACTION_ADD_TARGET,
    ACTION_TOGGLE_INPUT,
    ACTION_ADD_BILL,
} ActionType;

typedef struct Action {
    ActionType type;
    int bill;
    int value; /* Target change, input type or recipe. */
} Action;

/* "Flint or cobble", "Hand axe and sharp flake": one or two names joined. */
static void join_names(char *out, size_t size, const char *key, const char *names[2], int count)
{
    if (count >= 2) {
        TR_FORMAT(out, size, tr(key), names[0], names[1]);
    } else {
        SDL_strlcpy(out, count == 1 ? names[0] : "", size);
    }
}

/* Names of the item types of a mask, joined with "or". */
static void either_names(char *out, size_t size, ItemTypeMask types)
{
    const char *names[2] = {NULL, NULL};
    int count = 0;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT && count < 2; type++) {
        if (types & ITEM_TYPE_BIT(type)) {
            names[count++] = tr(item_def((ItemType)type)->name_key);
        }
    }
    join_names(out, size, "recipe.either", names, count);
}

/* "Needs Flint or cobble ×1 · Tools: Cobble". */
static void recipe_needs(char *out, size_t size, RecipeType recipe)
{
    const RecipeDef *def = recipe_def(recipe);
    char names[TEXT_SIZE], count[16], ingredient[TEXT_SIZE], text[TEXT_SIZE];
    /* Inputs in the recipe's order of preference. */
    const char *inputs[2] = {NULL, NULL};
    int input_count = 0;
    for (int i = 0; i < RECIPE_INPUT_ALTERNATIVES; i++) {
        if (def->inputs[i] != ITEM_NONE) {
            inputs[input_count++] = tr(item_def(def->inputs[i])->name_key);
        }
    }
    join_names(names, sizeof(names), "recipe.either", inputs, input_count);
    tr_integer(count, sizeof(count), def->input_count);
    TR_FORMAT(ingredient, sizeof(ingredient), tr("recipe.ingredient"), names, count);
    if (def->input_min_quality > ITEM_QUALITY_CRUDE) {
        SDL_strlcpy(text, ingredient, sizeof(text));
        TR_FORMAT(ingredient, sizeof(ingredient), tr("recipe.ingredient_quality"), text,
                  tr(item_quality_name_key(def->input_min_quality)));
    }
    char tools[RECIPE_TOOL_MAX][TEXT_SIZE];
    const char *slots[RECIPE_TOOL_MAX] = {NULL, NULL};
    int tool_count = 0;
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        if (def->tools[slot] != 0) {
            either_names(tools[tool_count], sizeof(tools[tool_count]), def->tools[slot]);
            slots[tool_count] = tools[tool_count];
            tool_count++;
        }
    }
    if (tool_count == 0) {
        TR_FORMAT(out, size, tr("recipe.needs_no_tool"), ingredient);
        return;
    }
    join_names(text, sizeof(text), "recipe.both", slots, tool_count);
    TR_FORMAT(out, size, tr("recipe.needs"), ingredient, text);
}

static void percent_text(char *out, size_t size, float fraction)
{
    char number[16];
    tr_integer(number, sizeof(number), (long long)(SDL_clamp(fraction, 0.0f, 1.0f) * 100.0f));
    TR_FORMAT(out, size, tr("unit.percent"), number);
}

static void blueprint_rows(UiLayout *layout, const Game *game, const Workstation *station)
{
    const WorkstationDef *def = workstation_def((WorkstationType)station->type);
    char text[TEXT_SIZE], a[16], b[16], percent[32];
    if (def->material != ITEM_NONE) {
        tr_integer(a, sizeof(a), station->materials);
        tr_integer(b, sizeof(b), def->material_count);
        TR_FORMAT(text, sizeof(text), tr("station.materials"),
                  tr(item_def(def->material)->name_key), a, b);
        ui_layout_label(layout, text, 0.0f, false);
    }
    percent_text(percent, sizeof(percent), workstation_build_fraction(station));
    TR_FORMAT(text, sizeof(text), tr("station.construction"), percent);
    ui_layout_label(layout, text, 0.0f, false);
    if (!game_workstation_buildable(game, (WorkstationType)station->type)) {
        ui_layout_label(layout, tr("station.warning.nobody_can_build"), 0.0f, true);
    }
}

static const char *mode_label(char *out, size_t size, const Bill *bill)
{
    char target[16];
    tr_integer(target, sizeof(target), bill->target);
    switch ((BillMode)bill->mode) {
    case BILL_DO_TIMES:
        TR_FORMAT(out, size, tr_plural("bill.mode.do_times", bill->target), target);
        break;
    case BILL_UNTIL_STOCK: TR_FORMAT(out, size, tr("bill.mode.until_stock"), target); break;
    default: SDL_strlcpy(out, tr("bill.mode.forever"), size); break;
    }
    return out;
}

/* Mode button, -/+ for the target, and how far the bill has got. */
static void bill_mode_row(UiLayout *layout, const Game *game, const Bill *bill, int index,
                          Action *action)
{
    Ui *ui = layout->ui;
    char mode[TEXT_SIZE], progress[TEXT_SIZE] = "", number[16];
    mode_label(mode, sizeof(mode), bill);
    /* Symbols, not words: the same in every language. */
    const char *minus = "−", *plus = "+";
    float small = SDL_max(ui_button_width(ui, minus), ui_button_width(ui, plus));
    bool has_target = bill->mode != BILL_FOREVER;
    if (bill->mode == BILL_DO_TIMES) {
        tr_integer(number, sizeof(number), bill->done);
        TR_FORMAT(progress, sizeof(progress), tr("bill.progress.done"), number);
    } else if (bill->mode == BILL_UNTIL_STOCK) {
        const RecipeDef *def = recipe_def((RecipeType)bill->recipe);
        tr_integer(number, sizeof(number), game->items.totals[def->product]);
        TR_FORMAT(progress, sizeof(progress), tr("bill.progress.in_colony"), number);
    }
    float x = layout->x + INDENT;
    float width = INDENT + ui_button_width(ui, mode) + GAP;
    if (has_target) {
        width += 2.0f * small + 2.0f + GAP;
    }
    ui_layout_measure(layout, width + ui_text_width(ui, progress));
    if (ui_layout_button(layout, x, mode)) {
        *action = (Action){ACTION_CYCLE_MODE, index, 0};
    }
    x += ui_button_width(ui, mode) + GAP;
    if (has_target) {
        int step = (SDL_GetModState() & SDL_KMOD_SHIFT) ? TARGET_BIG_STEP : 1;
        if (ui_layout_button_sized(layout, x, small, minus)) {
            *action = (Action){ACTION_ADD_TARGET, index, -step};
        }
        if (ui_layout_button_sized(layout, x + small + 2.0f, small, plus)) {
            *action = (Action){ACTION_ADD_TARGET, index, step};
        }
        x += 2.0f * small + 2.0f + GAP;
    }
    if (layout->draw && progress[0] != '\0') {
        ui_label_dim(ui, progress, x, layout->y + layout->text_offset);
    }
    layout->y += layout->row;
}

/* One check box per input type, when the recipe has a choice. */
static void bill_inputs_row(UiLayout *layout, const Bill *bill, int index, Action *action)
{
    const RecipeDef *def = recipe_def((RecipeType)bill->recipe);
    int alternatives = 0;
    float width = INDENT;
    for (int i = 0; i < RECIPE_INPUT_ALTERNATIVES; i++) {
        if (def->inputs[i] != ITEM_NONE) {
            alternatives++;
            width += ui_checkbox_width(layout->ui, tr(item_def(def->inputs[i])->name_key)) + GAP;
        }
    }
    if (alternatives < 2) {
        return;
    }
    ui_layout_measure(layout, width - GAP);
    float x = layout->x + INDENT;
    for (int i = 0; i < RECIPE_INPUT_ALTERNATIVES; i++) {
        ItemType type = def->inputs[i];
        if (type == ITEM_NONE) {
            continue;
        }
        const char *name = tr(item_def(type)->name_key);
        float box_width = ui_checkbox_width(layout->ui, name);
        bool allowed = (bill->inputs & ITEM_TYPE_BIT(type)) != 0;
        if (layout->draw && ui_checkbox(layout->ui, x, layout->y, box_width,
                                        allowed ? UI_CHECKED : UI_UNCHECKED, name)) {
            *action = (Action){ACTION_TOGGLE_INPUT, index, (int)type};
        }
        x += box_width + GAP;
    }
    layout->y += layout->row;
}

/* Why nobody works on a bill, if it can be told cheaply. */
static void bill_warning_rows(UiLayout *layout, const Game *game, const Bill *bill)
{
    RecipeType recipe = (RecipeType)bill->recipe;
    if (!game_recipe_known(game, recipe)) {
        ui_layout_label(layout, tr("station.warning.unknown_recipe"), INDENT, true);
        return;
    }
    const RecipeDef *def = recipe_def(recipe);
    ItemTypeMask missing = game_recipe_missing_tools(game, recipe);
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        ItemTypeMask types = def->tools[slot];
        if (types == 0 || (missing & types) != types) {
            continue;
        }
        char names[TEXT_SIZE], text[TEXT_SIZE];
        either_names(names, sizeof(names), types);
        TR_FORMAT(text, sizeof(text), tr("station.warning.tool"), names);
        ui_layout_label(layout, text, INDENT, true);
    }
    if (bill_wants_more(bill, (int)game->items.totals[def->product]) &&
        !game_bill_has_input(game, bill)) {
        ui_layout_label(layout, tr("station.warning.no_input"), INDENT, true);
    }
}

static void built_rows(UiLayout *layout, const Game *game, const Workstation *station,
                       Action *action)
{
    char text[TEXT_SIZE], number[16], percent[32];
    const RecipeDef *current = recipe_def((RecipeType)station->work_recipe);
    if (current != NULL && station->work_input.count > 0) {
        percent_text(percent, sizeof(percent), station->work_progress / current->work);
        TR_FORMAT(text, sizeof(text), tr("station.in_progress"), tr(current->name_key), percent);
        ui_layout_label(layout, text, 0.0f, false);
    }
    ui_layout_label(layout,
                    tr(station->bill_count > 0 ? "station.bills" : "station.no_bills"), 0.0f,
                    station->bill_count == 0);
    for (int i = 0; i < station->bill_count; i++) {
        const Bill *bill = &station->bills[i];
        tr_integer(number, sizeof(number), i + 1);
        TR_FORMAT(text, sizeof(text), tr("station.bill"), number,
                  tr(recipe_def((RecipeType)bill->recipe)->name_key));
        if (ui_layout_label_with_button(layout, text, tr("station.bill.remove"))) {
            *action = (Action){ACTION_DELETE_BILL, i, 0};
        }
        recipe_needs(text, sizeof(text), (RecipeType)bill->recipe);
        ui_layout_label(layout, text, INDENT, true);
        bill_mode_row(layout, game, bill, i, action);
        bill_inputs_row(layout, bill, i, action);
        bill_warning_rows(layout, game, bill);
    }
    if (station->bill_count >= WORKSTATION_BILL_MAX) {
        return;
    }
    ui_layout_label(layout, tr("station.add_bill"), 0.0f, false);
    /* Buttons in a column, what each recipe needs aligned after them. */
    float column = 0.0f;
    for (int recipe = 0; recipe < RECIPE_COUNT; recipe++) {
        const RecipeDef *def = recipe_def((RecipeType)recipe);
        if (def->stations & WORKSTATION_BIT(station->type)) {
            column = SDL_max(column, ui_button_width(layout->ui, tr(def->name_key)));
        }
    }
    for (int recipe = 0; recipe < RECIPE_COUNT; recipe++) {
        const RecipeDef *def = recipe_def((RecipeType)recipe);
        if (!(def->stations & WORKSTATION_BIT(station->type))) {
            continue;
        }
        const char *name = tr(def->name_key);
        recipe_needs(text, sizeof(text), (RecipeType)recipe);
        ui_layout_measure(layout, INDENT + column + GAP + ui_text_width(layout->ui, text));
        if (ui_layout_button(layout, layout->x + INDENT, name)) {
            *action = (Action){ACTION_ADD_BILL, 0, recipe};
        }
        if (layout->draw) {
            ui_label_dim(layout->ui, text, layout->x + INDENT + column + GAP,
                         layout->y + layout->text_offset);
        }
        layout->y += layout->row;
    }
}

typedef struct StationRows {
    const Game *game;
    const Workstation *station;
    Action action;
} StationRows;

static void station_rows(UiLayout *layout, void *userdata)
{
    StationRows *rows = userdata;
    const Game *game = rows->game;
    const Workstation *station = rows->station;
    Action *action = &rows->action;
    const char *state = station->built ? "" : tr("station.blueprint");
    if (ui_layout_label_with_button(layout, state, tr("station.delete"))) {
        *action = (Action){ACTION_DELETE_STATION, 0, 0};
    }
    if (station->built) {
        built_rows(layout, game, station, action);
    } else {
        blueprint_rows(layout, game, station);
    }
}

static void apply(App *app, int index, const Action *action)
{
    Workstations *workstations = &app->game.workstations;
    const Workstation *station = workstation_get_const(workstations, index);
    const Bill *bill = action->bill >= 0 && action->bill < station->bill_count
                           ? &station->bills[action->bill]
                           : NULL;
    switch (action->type) {
    case ACTION_DELETE_STATION:
        game_remove_workstation(&app->game, index);
        app->selected_station = WORKSTATION_NONE;
        break;
    case ACTION_DELETE_BILL: game_remove_bill(&app->game, index, action->bill); break;
    case ACTION_CYCLE_MODE:
        if (bill != NULL) {
            workstation_set_bill_mode(workstations, index, action->bill,
                                      (BillMode)((bill->mode + 1) % BILL_MODE_COUNT));
        }
        break;
    case ACTION_ADD_TARGET:
        if (bill != NULL) {
            workstation_set_bill_target(workstations, index, action->bill,
                                        bill->target + action->value);
        }
        break;
    case ACTION_TOGGLE_INPUT:
        if (bill != NULL) {
            workstation_set_bill_input(workstations, index, action->bill,
                                       (ItemType)action->value,
                                       !(bill->inputs & ITEM_TYPE_BIT(action->value)));
        }
        break;
    case ACTION_ADD_BILL:
        workstation_add_bill(workstations, index, (RecipeType)action->value);
        break;
    case ACTION_NONE: break;
    }
}

void station_panel_draw(App *app)
{
    int index = app->selected_station;
    const Workstation *station = workstation_get_const(&app->game.workstations, index);
    if (station == NULL) {
        return;
    }
    const char *title = tr(workstation_def((WorkstationType)station->type)->name_key);
    StationRows rows = {&app->game, station, {ACTION_NONE, -1, 0}};
    bool open = true;
    ui_layout_window(&app->ui, APP_WINDOW_STATION, title, UI_PLACE_BOTTOM_RIGHT, station_rows,
                     &rows, &open);
    if (rows.action.type != ACTION_NONE) {
        apply(app, index, &rows.action);
    }
    if (!open) {
        app->selected_station = WORKSTATION_NONE;
    }
}
