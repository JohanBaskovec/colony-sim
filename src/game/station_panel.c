#include "game/station_panel.h"

#include "core/hearth.h"
#include "core/i18n.h"
#include "game/ui_layout.h"

#define GAP UI_LAYOUT_GAP
#define INDENT UI_LAYOUT_INDENT
#define TEXT_SIZE 160
#define TARGET_BIG_STEP 10 /* Shift+click on -/+. */
#define LEVEL_STEP 5
#define LEVEL_BIG_STEP 25
#define RADIUS_STEP 10
#define RADIUS_BIG_STEP 50

/* What the player clicked, applied after the rows (deleting shifts bills). */
typedef enum ActionType {
    ACTION_NONE,
    ACTION_DELETE_STATION,
    ACTION_DELETE_BILL,
    ACTION_CYCLE_MODE,
    ACTION_ADD_TARGET,
    ACTION_TOGGLE_INPUT,
    ACTION_ADD_MIN_LEVEL,
    ACTION_ADD_MAX_LEVEL,
    ACTION_TOGGLE_PAUSE,
    ACTION_ADD_RADIUS,
    ACTION_ADD_BILL,
    ACTION_TOGGLE_KEEP_BURNING,
    ACTION_TOGGLE_FUEL,
    ACTION_TOGGLE_TEACH,
    ACTION_TOGGLE_FORBIDDEN,
} ActionType;

typedef struct Action {
    ActionType type;
    int bill;
    int value; /* Target, level or radius change, item type, recipe or knowledge. */
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
        tr_integer(number, sizeof(number), game->stock[def->product]);
        TR_FORMAT(progress, sizeof(progress), tr("bill.progress.in_stock"), number);
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

/* Pause check box, and -/+ for how far inputs are looked for. */
static void bill_pause_row(UiLayout *layout, const Bill *bill, int index, Action *action)
{
    Ui *ui = layout->ui;
    char text[TEXT_SIZE], radius[16];
    tr_integer(radius, sizeof(radius), bill->input_radius);
    TR_FORMAT(text, sizeof(text), tr("bill.input_radius"), radius);
    const char *pause = tr("bill.paused"), *minus = "−", *plus = "+";
    float box_width = ui_checkbox_width(ui, pause);
    float small = SDL_max(ui_button_width(ui, minus), ui_button_width(ui, plus));
    float pair = 2.0f * small + 2.0f;
    ui_layout_measure(layout, INDENT + box_width + GAP + pair + GAP + ui_text_width(ui, text));
    float x = layout->x + INDENT;
    if (layout->draw &&
        ui_checkbox(ui, x, layout->y, box_width, bill->paused ? UI_CHECKED : UI_UNCHECKED, pause)) {
        *action = (Action){ACTION_TOGGLE_PAUSE, index, 0};
    }
    x += box_width + GAP;
    int step = (SDL_GetModState() & SDL_KMOD_SHIFT) ? RADIUS_BIG_STEP : RADIUS_STEP;
    if (ui_layout_button_sized(layout, x, small, minus)) {
        *action = (Action){ACTION_ADD_RADIUS, index, -step};
    }
    if (ui_layout_button_sized(layout, x + small + 2.0f, small, plus)) {
        *action = (Action){ACTION_ADD_RADIUS, index, step};
    }
    if (layout->draw) {
        ui_label(ui, text, x + pair + GAP, layout->y + layout->text_offset);
    }
    layout->y += layout->row;
}

/* Worker level range, for recipes that need a knowledge: -/+ for the lowest
 * level, the range, -/+ for the highest. */
static void bill_levels_row(UiLayout *layout, const Bill *bill, int index, Action *action)
{
    if (recipe_def((RecipeType)bill->recipe)->knowledge == KNOWLEDGE_NONE) {
        return;
    }
    Ui *ui = layout->ui;
    char text[TEXT_SIZE], low[16], high[16];
    tr_integer(low, sizeof(low), bill->min_level);
    tr_integer(high, sizeof(high), bill->max_level);
    TR_FORMAT(text, sizeof(text), tr("bill.levels"), low, high);
    const char *minus = "−", *plus = "+";
    float small = SDL_max(ui_button_width(ui, minus), ui_button_width(ui, plus));
    float pair = 2.0f * small + 2.0f;
    float text_width = ui_text_width(ui, text);
    ui_layout_measure(layout, INDENT + 2.0f * (pair + GAP) + text_width);
    int step = (SDL_GetModState() & SDL_KMOD_SHIFT) ? LEVEL_BIG_STEP : LEVEL_STEP;
    float x = layout->x + INDENT;
    for (int bound = 0; bound < 2; bound++) {
        ActionType type = bound == 0 ? ACTION_ADD_MIN_LEVEL : ACTION_ADD_MAX_LEVEL;
        if (ui_layout_button_sized(layout, x, small, minus)) {
            *action = (Action){type, index, -step};
        }
        if (ui_layout_button_sized(layout, x + small + 2.0f, small, plus)) {
            *action = (Action){type, index, step};
        }
        x += pair + GAP;
        if (bound == 0) {
            if (layout->draw) {
                ui_label(ui, text, x, layout->y + layout->text_offset);
            }
            x += text_width + GAP;
        }
    }
    layout->y += layout->row;
}

/* Why nobody works on a bill, if it can be told cheaply. */
static void bill_warning_rows(UiLayout *layout, const Game *game, const Workstation *station,
                              const Bill *bill)
{
    RecipeType recipe = (RecipeType)bill->recipe;
    if (!game_recipe_known(game, recipe)) {
        ui_layout_label(layout, tr("station.warning.unknown_recipe"), INDENT, true);
        return;
    }
    if (recipe_needs_fire(recipe) && !station->burning) {
        ui_layout_label(layout, tr("station.warning.fire_out"), INDENT, true);
    }
    if (!game_bill_has_worker(game, bill)) {
        ui_layout_label(layout, tr("station.warning.no_worker_level"), INDENT, true);
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
    if (bill_wants_more(bill, (int)game->stock[def->product]) && !game_bill_has_input(game, bill)) {
        ui_layout_label(layout, tr("station.warning.no_input"), INDENT, true);
    }
}

/* A check box on its own row. Returns whether it was clicked. */
static bool checkbox_row(UiLayout *layout, float indent, const char *label, bool checked)
{
    float width = ui_checkbox_width(layout->ui, label);
    ui_layout_measure(layout, indent + width);
    bool clicked = layout->draw && ui_checkbox(layout->ui, layout->x + indent, layout->y, width,
                                               checked ? UI_CHECKED : UI_UNCHECKED, label);
    layout->y += layout->row;
    return clicked;
}

static void kilograms_text(char *out, size_t size, float kilograms)
{
    char number[16];
    tr_decimal(number, sizeof(number), kilograms, 1);
    TR_FORMAT(out, size, tr("unit.kg"), number);
}

/* "Burn:" and one check box per fuel type. */
static void hearth_fuel_row(UiLayout *layout, const Workstation *station, Action *action)
{
    Ui *ui = layout->ui;
    const char *label = tr("station.hearth.fuel_types");
    float width = ui_text_width(ui, label);
    for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
        if (HEARTH_FUEL_TYPES & ITEM_TYPE_BIT(type)) {
            width += GAP + ui_checkbox_width(ui, tr(item_def((ItemType)type)->name_key));
        }
    }
    ui_layout_measure(layout, width);
    float x = layout->x;
    if (layout->draw) {
        ui_label(ui, label, x, layout->y + layout->text_offset);
    }
    x += ui_text_width(ui, label) + GAP;
    for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
        if (!(HEARTH_FUEL_TYPES & ITEM_TYPE_BIT(type))) {
            continue;
        }
        const char *name = tr(item_def((ItemType)type)->name_key);
        float box_width = ui_checkbox_width(ui, name);
        bool allowed = (station->fuel_types & ITEM_TYPE_BIT(type)) != 0;
        if (layout->draw &&
            ui_checkbox(ui, x, layout->y, box_width, allowed ? UI_CHECKED : UI_UNCHECKED, name)) {
            *action = (Action){ACTION_TOGGLE_FUEL, 0, type};
        }
        x += box_width + GAP;
    }
    layout->y += layout->row;
}

/* The fire: fuel, burning or out, what the player wants of it, and which
 * knowledge is told at fireside stories (a colony setting, shown here). */
static void hearth_rows(UiLayout *layout, const Game *game, const Workstation *station,
                        Action *action)
{
    char text[TEXT_SIZE], fuel[32], capacity[32], number[16];
    kilograms_text(fuel, sizeof(fuel), station->fuel);
    kilograms_text(capacity, sizeof(capacity), HEARTH_FUEL_CAPACITY);
    TR_FORMAT(text, sizeof(text), tr("station.hearth.fuel"), fuel, capacity);
    ui_layout_label(layout, text, 0.0f, false);
    bool rained_on =
        hearth_rained_on(station, &game->map, weather_wetting(&game->weather, game->clock.tick));
    if (station->burning) {
        tr_decimal(number, sizeof(number), station->fuel / HEARTH_BURN_PER_HOUR, 1);
        TR_FORMAT(text, sizeof(text), tr("station.hearth.burning"), number);
        ui_layout_label(layout, text, 0.0f, false);
        if (station->soaked > 0.0f && rained_on) {
            tr_integer(number, sizeof(number),
                       (long long)(station->soaked / HEARTH_SOAKED_OUT * 100.0f));
            TR_FORMAT(text, sizeof(text), tr("station.hearth.soaking"), number);
            ui_layout_label(layout, text, 0.0f, true);
        }
    } else {
        ui_layout_label(layout, tr("station.hearth.out"), 0.0f, true);
        if (rained_on) {
            ui_layout_label(layout, tr("station.hearth.rained_on"), 0.0f, true);
        }
    }
    if (checkbox_row(layout, 0.0f, tr("station.hearth.keep_burning"), station->keep_burning)) {
        *action = (Action){ACTION_TOGGLE_KEEP_BURNING, 0, 0};
    }
    hearth_fuel_row(layout, station, action);
    ui_layout_label(layout, tr("station.hearth.teach"), 0.0f, false);
    ui_layout_label(layout, tr("station.hearth.teach_hint"), INDENT, true);
    bool any = false;
    for (int knowledge = 0; knowledge < KNOWLEDGE_COUNT; knowledge++) {
        if (!game_knowledge_discovered(game, (KnowledgeType)knowledge)) {
            continue;
        }
        any = true;
        const char *name = tr(knowledge_def((KnowledgeType)knowledge)->name_key);
        if (checkbox_row(layout, INDENT, name, game->teach[knowledge])) {
            *action = (Action){ACTION_TOGGLE_TEACH, 0, knowledge};
        }
    }
    if (!any) {
        ui_layout_label(layout, tr("station.hearth.nothing_to_teach"), INDENT, true);
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
    ui_layout_label(layout, tr(station->bill_count > 0 ? "station.bills" : "station.no_bills"),
                    0.0f, station->bill_count == 0);
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
        bill_levels_row(layout, bill, i, action);
        bill_pause_row(layout, bill, i, action);
        bill_warning_rows(layout, game, station, bill);
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
    /* Forbid or allow it, delete it; whether the settlers leave it alone. */
    const char *forbid = tr(station->forbidden ? "forbid.allow" : "forbid.forbid");
    const char *remove = tr("station.delete");
    const char *state = station->forbidden ? tr("tile_panel.left_alone") : "";
    float forbid_width = ui_button_width(layout->ui, forbid);
    float remove_width = ui_button_width(layout->ui, remove);
    ui_layout_measure(layout,
                      ui_text_width(layout->ui, state) + GAP + forbid_width + GAP + remove_width);
    float right = layout->x + layout->width;
    if (ui_layout_button(layout, right - remove_width - GAP - forbid_width, forbid)) {
        *action = (Action){ACTION_TOGGLE_FORBIDDEN, 0, 0};
    }
    if (ui_layout_button(layout, right - remove_width, remove)) {
        *action = (Action){ACTION_DELETE_STATION, 0, 0};
    }
    if (layout->draw && station->forbidden) {
        ui_label_dim(layout->ui, state, layout->x, layout->y + layout->text_offset);
    }
    layout->y += layout->row;
    if (hearth_is(station)) {
        hearth_rows(layout, game, station, action);
    }
    built_rows(layout, game, station, action);
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
            workstation_set_bill_input(workstations, index, action->bill, (ItemType)action->value,
                                       !(bill->inputs & ITEM_TYPE_BIT(action->value)));
        }
        break;
    case ACTION_ADD_MIN_LEVEL:
        if (bill != NULL) {
            workstation_set_bill_min_level(workstations, index, action->bill,
                                           bill->min_level + action->value);
        }
        break;
    case ACTION_ADD_MAX_LEVEL:
        if (bill != NULL) {
            workstation_set_bill_max_level(workstations, index, action->bill,
                                           bill->max_level + action->value);
        }
        break;
    case ACTION_TOGGLE_PAUSE:
        if (bill != NULL) {
            workstation_set_bill_paused(workstations, index, action->bill, !bill->paused);
        }
        break;
    case ACTION_ADD_RADIUS:
        if (bill != NULL) {
            workstation_set_bill_input_radius(workstations, index, action->bill,
                                              bill->input_radius + action->value);
        }
        break;
    case ACTION_ADD_BILL:
        workstation_add_bill(workstations, index, (RecipeType)action->value);
        break;
    case ACTION_TOGGLE_KEEP_BURNING:
        hearth_set_keep_burning(workstations, index, !station->keep_burning);
        break;
    case ACTION_TOGGLE_FUEL:
        hearth_set_fuel_type(workstations, index, (ItemType)action->value,
                             !(station->fuel_types & ITEM_TYPE_BIT(action->value)));
        break;
    case ACTION_TOGGLE_TEACH:
        game_set_teach(&app->game, (KnowledgeType)action->value, !app->game.teach[action->value]);
        break;
    case ACTION_TOGGLE_FORBIDDEN:
        game_set_forbidden(&app->game, station->x, station->y, station->z, FORBID_STATION,
                           !station->forbidden);
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
    game_update_stock(&app->game); /* Up to date even while paused. */
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
