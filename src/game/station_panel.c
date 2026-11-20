#include "game/station_panel.h"

#include "core/game_clock.h"
#include "core/hearth.h"
#include "core/i18n.h"
#include "core/stockpile.h"
#include "core/work.h"
#include "game/hud.h"
#include "game/inspector.h"
#include "game/render.h"

#define GAP UI_LAYOUT_GAP
#define INDENT UI_LAYOUT_INDENT
#define TEXT_SIZE 160
#define TARGET_BIG_STEP 10                                /* Shift+click on -/+. */
#define LEVEL_STEPS (BILL_LEVEL_MAX - BILL_LEVEL_MIN + 1) /* Notches of the level slider. */
#define LEVEL_SLIDER_ID 5000                              /* + the bill. */
#define LEVEL_SLIDER_WIDTH 200.0f
#define QUALITY_SLIDER_ID 6000 /* + the bill. */
#define QUALITY_STEP 5         /* Quality points per notch of the ingredient slider. */
#define DROPDOWN_ID 100        /* + 2 per bill, + 1 for the settler's. */
#define RADIUS_SLIDER_ID 7000  /* + the bill. */

/* What the player clicked, applied after the rows (deleting shifts bills). */
typedef enum ActionType {
    ACTION_NONE,
    ACTION_DELETE_STATION,
    ACTION_DELETE_BILL,
    ACTION_CYCLE_MODE,
    ACTION_ADD_TARGET,
    ACTION_TOGGLE_INPUT,
    ACTION_SET_WORKER,
    ACTION_SET_COLONIST,
    ACTION_SET_LEVELS,
    ACTION_SET_QUALITY,
    ACTION_TOGGLE_PAUSE,
    ACTION_SET_RADIUS,
    ACTION_ADD_BILL,
    ACTION_TOGGLE_ADD_LIST,
    ACTION_PICK_RECIPE,
    ACTION_PICK_BILL,
    ACTION_TOGGLE_KEEP_BURNING,
    ACTION_TOGGLE_FUEL,
    ACTION_TOGGLE_TEACH,
    ACTION_TOGGLE_FORBIDDEN,
} ActionType;

typedef struct Action {
    ActionType type;
    int bill;
    int value;  /* Target, level or radius change, item type, recipe, knowledge, worker mode. */
    int value2; /* ACTION_SET_LEVELS/QUALITY: the highest (value is the lowest). */
    EntityId colonist; /* ACTION_SET_COLONIST. */
} Action;

/* "Flint or cobble", "Hand axe and sharp flake": one or two names joined
 * (three: "Dry grass, moss or bracken", recipe.either_three). */
static void join_names(char *out, size_t size, const char *key, const char *names[3], int count)
{
    if (count >= 3) {
        TR_FORMAT(out, size, tr("recipe.either_three"), names[0], names[1], names[2]);
    } else if (count == 2) {
        TR_FORMAT(out, size, tr(key), names[0], names[1]);
    } else {
        SDL_strlcpy(out, count == 1 ? names[0] : "", size);
    }
}

/* Names of the item types of a mask (three at most), joined with "or". */
static void either_names(char *out, size_t size, ItemTypeMask types)
{
    const char *names[3] = {NULL, NULL, NULL};
    int count = 0;
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT && count < 3; type++) {
        if (types & ITEM_TYPE_BIT(type)) {
            names[count++] = tr(item_def((ItemType)type)->name_key);
        }
    }
    join_names(out, size, "recipe.either", names, count);
}

/* "Flint or cobble ×1 (quality 50 or better)": the recipe's ingredient, only
 * the input types of `allowed` (in the recipe's order of preference). */
static void ingredient_text(char *out, size_t size, RecipeType recipe, ItemTypeMask allowed)
{
    const RecipeDef *def = recipe_def(recipe);
    char names[TEXT_SIZE], count[16], text[TEXT_SIZE];
    const char *inputs[3] = {NULL, NULL, NULL};
    int input_count = 0;
    for (int i = 0; i < RECIPE_INPUT_ALTERNATIVES; i++) {
        if (def->inputs[i] != ITEM_NONE && (allowed & ITEM_TYPE_BIT(def->inputs[i]))) {
            inputs[input_count++] = tr(item_def(def->inputs[i])->name_key);
        }
    }
    join_names(names, sizeof(names), "recipe.either", inputs, input_count);
    tr_integer(count, sizeof(count), def->input_count);
    TR_FORMAT(out, size, tr("recipe.ingredient"), names, count);
    if (def->input_min_quality > ITEM_QUALITY_WORST) {
        char quality[16];
        hud_quality_text(quality, sizeof(quality), def->input_min_quality);
        SDL_strlcpy(text, out, sizeof(text));
        TR_FORMAT(out, size, tr("recipe.ingredient_quality"), text, quality);
    }
}

/* The tools a recipe needs, one text per set ("Hand axe or sharp flake");
 * returns how many. */
static int tool_texts(RecipeType recipe, char texts[RECIPE_TOOL_MAX][TEXT_SIZE])
{
    const RecipeDef *def = recipe_def(recipe);
    int count = 0;
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        if (def->tools[slot] != 0) {
            either_names(texts[count++], TEXT_SIZE, def->tools[slot]);
        }
    }
    return count;
}

/* All the tools, joined: "Cobble and hand axe". */
static void tools_text(char *out, size_t size, char texts[RECIPE_TOOL_MAX][TEXT_SIZE], int count)
{
    const char *slots[3] = {texts[0], texts[1], NULL};
    join_names(out, size, "recipe.both", slots, count);
}

/* A duration given in work ticks at speed ×1, in game time: "9 min", "1 h 30 min". */
static void duration_text(char *out, size_t size, float work)
{
    int minutes = SDL_max(1, (int)SDL_roundf(work * 60.0f / (float)TICKS_PER_HOUR));
    char hours_text[16], minutes_text[16];
    tr_integer(minutes_text, sizeof(minutes_text), minutes % 60);
    if (minutes < 60) {
        tr_integer(minutes_text, sizeof(minutes_text), minutes);
        TR_FORMAT(out, size, tr("unit.minutes"), minutes_text);
        return;
    }
    tr_integer(hours_text, sizeof(hours_text), minutes / 60);
    if (minutes % 60 == 0) {
        TR_FORMAT(out, size, tr("unit.hours"), hours_text);
        return;
    }
    TR_FORMAT(out, size, tr("unit.hours_minutes"), hours_text, minutes_text);
}

/* A weight or a bulk with as many decimals as it needs to be told from zero. */
static void quantity_text(char *out, size_t size, float value, const char *unit_key)
{
    int decimals = value < 0.1f ? 2 : (SDL_fabsf(value - SDL_roundf(value)) > 0.05f ? 1 : 0);
    char number[32];
    tr_decimal(number, sizeof(number), value, decimals);
    if (unit_key == NULL) {
        SDL_strlcpy(out, number, size);
    } else {
        TR_FORMAT(out, size, tr(unit_key), number);
    }
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
        *action = (Action){.type = ACTION_CYCLE_MODE, .bill = index};
    }
    x += ui_button_width(ui, mode) + GAP;
    if (has_target) {
        int step = (SDL_GetModState() & SDL_KMOD_SHIFT) ? TARGET_BIG_STEP : 1;
        if (ui_layout_button_sized(layout, x, small, minus)) {
            *action = (Action){.type = ACTION_ADD_TARGET, .bill = index, .value = -step};
        }
        if (ui_layout_button_sized(layout, x + small + 2.0f, small, plus)) {
            *action = (Action){.type = ACTION_ADD_TARGET, .bill = index, .value = step};
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
    float width = 0.0f;
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
    float x = layout->x;
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
            *action = (Action){.type = ACTION_TOGGLE_INPUT, .bill = index, .value = (int)type};
        }
        x += box_width + GAP;
    }
    layout->y += layout->row;
}

/* A row of text, `indent` from the left, in the color of a problem when
 * `bad`; the tooltip then tells what it is. */
static void problem_row_at(UiLayout *layout, float indent, const char *text, bool bad, bool dim,
                           const char *tooltip)
{
    Ui *ui = layout->ui;
    ui_layout_measure(layout, indent + ui_text_width(ui, text));
    if (layout->draw) {
        float x = layout->x + indent, y = layout->y + layout->text_offset;
        if (bad) {
            ui_label_colored(ui, text, x, y, THEME_VALUE_LOW);
        } else if (dim) {
            ui_label_dim(ui, text, x, y);
        } else {
            ui_label(ui, text, x, y);
        }
        SDL_FRect row = {layout->x, layout->y, layout->width, layout->row};
        if (bad && tooltip != NULL && ui_hovered(ui, row)) {
            ui_tooltip(ui, &tooltip, 1);
        }
    }
    layout->y += layout->row;
}

static void problem_row(UiLayout *layout, const char *text, bool bad, bool dim, const char *tooltip)
{
    problem_row_at(layout, 0.0f, text, bad, dim, tooltip);
}

/* Two bars: the pause button's icon. */
static void pause_icon(Ui *ui, SDL_FRect rect)
{
    float bar = SDL_max(2.0f, SDL_floorf(rect.w * 0.14f));
    float height = SDL_floorf(rect.h * 0.45f);
    float y = SDL_floorf(rect.y + (rect.h - height) * 0.5f);
    float center = rect.x + rect.w * 0.5f;
    SDL_Color color = ui->theme.colors[THEME_TEXT];
    ui_fill(ui, (SDL_FRect){SDL_floorf(center - bar * 1.5f), y, bar, height}, color);
    ui_fill(ui, (SDL_FRect){SDL_floorf(center + bar * 0.5f), y, bar, height}, color);
}

/* Whether no ingredient a bill allows lies in the colony (when it still wants any). */
static bool bill_lacks_input(const Game *game, const Bill *bill)
{
    const RecipeDef *def = recipe_def((RecipeType)bill->recipe);
    return bill_wants_more(bill, (int)game->stock[def->product]) &&
           !game_bill_has_input(game, bill);
}

/* The tools of a recipe that cannot be had, as text ("Cobble"); false when
 * none is missing. */
static bool recipe_lacks_tools(const Game *game, RecipeType recipe, char *names, size_t size)
{
    const RecipeDef *def = recipe_def(recipe);
    ItemTypeMask missing = game_recipe_missing_tools(game, recipe);
    char lacking[RECIPE_TOOL_MAX][TEXT_SIZE];
    int count = 0;
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        ItemTypeMask types = def->tools[slot];
        if (types != 0 && (missing & types) == types) {
            either_names(lacking[count++], TEXT_SIZE, types);
        }
    }
    if (count > 0) {
        tools_text(names, size, lacking, count);
    }
    return count > 0;
}

/* What keeps a bill from being worked on, if anything: the first problem, as
 * the text of a tooltip. */
static bool bill_problem(const Game *game, const Workstation *station, const Bill *bill, char *out,
                         size_t size)
{
    RecipeType recipe = (RecipeType)bill->recipe;
    char names[TEXT_SIZE];
    const char *key = NULL;
    if (!game_recipe_known(game, recipe)) {
        key = "station.warning.unknown_recipe";
    } else if (bill_lacks_input(game, bill)) {
        key = "station.warning.no_input";
    } else if (recipe_lacks_tools(game, recipe, names, sizeof(names))) {
        TR_FORMAT(out, size, tr("station.warning.tool"), names);
        return true;
    } else if (recipe_needs_fire(recipe) && !station->burning) {
        key = "station.warning.fire_out";
    } else if (!game_bill_has_worker(game, bill)) {
        key = "station.warning.no_worker";
    }
    if (key != NULL) {
        SDL_strlcpy(out, tr(key), size);
    }
    return key != NULL;
}

/* First row of a bill: its name (red, with the problem as a tooltip, when
 * nobody can work on it), and at the right end the pause and remove buttons. */
static void bill_header_row(UiLayout *layout, const Game *game, const Workstation *station,
                            const Bill *bill, int index, Action *action)
{
    Ui *ui = layout->ui;
    const char *name = tr(recipe_def((RecipeType)bill->recipe)->name_key);
    float size = layout->button_height;
    ui_layout_measure(layout, ui_text_width(ui, name) + GAP + 2.0f * size + 2.0f);
    if (layout->draw) {
        float right = layout->x + layout->width;
        SDL_FRect remove = {right - size, layout->y, size, size};
        SDL_FRect pause = {remove.x - 2.0f - size, layout->y, size, size};
        float y = layout->y + layout->text_offset;
        char problem[TEXT_SIZE];
        if (bill_problem(game, station, bill, problem, sizeof(problem))) {
            ui_label_colored(ui, name, layout->x, y, THEME_VALUE_LOW);
            const char *line = problem;
            if (ui_hovered(
                    ui, (SDL_FRect){layout->x, layout->y, ui_text_width(ui, name), layout->row})) {
                ui_tooltip(ui, &line, 1);
            }
        } else if (bill->paused) {
            ui_label_dim(ui, name, layout->x, y);
        } else {
            ui_label(ui, name, layout->x, y);
        }
        if (ui_toggle_button(ui, pause, "", bill->paused)) {
            *action = (Action){.type = ACTION_TOGGLE_PAUSE, .bill = index};
        }
        pause_icon(ui, pause);
        if (ui_hovered(ui, pause)) {
            const char *line = tr(bill->paused ? "bill.resume" : "bill.pause");
            ui_tooltip(ui, &line, 1);
        }
        if (ui_button(ui, remove, "×")) {
            *action = (Action){.type = ACTION_DELETE_BILL, .bill = index};
        }
        if (ui_hovered(ui, remove)) {
            const char *line = tr("station.bill.remove");
            ui_tooltip(ui, &line, 1);
        }
    }
    layout->y += layout->row;
}

/* The ingredients still allowed; red when nothing suitable lies in the colony. */
static void bill_ingredients_row(UiLayout *layout, const Game *game, const Bill *bill)
{
    char text[TEXT_SIZE];
    ingredient_text(text, sizeof(text), (RecipeType)bill->recipe, bill->inputs);
    problem_row(layout, text, bill_lacks_input(game, bill), false, tr("station.warning.no_input"));
}

/* The tools, red when one cannot be had. */
static void bill_tool_row(UiLayout *layout, const Game *game, const Bill *bill)
{
    RecipeType recipe = (RecipeType)bill->recipe;
    char texts[RECIPE_TOOL_MAX][TEXT_SIZE], text[TEXT_SIZE], names[TEXT_SIZE], warning[TEXT_SIZE];
    int count = tool_texts(recipe, texts);
    if (count == 0) {
        problem_row(layout, tr("bill.no_tool"), false, true, NULL);
        return;
    }
    tools_text(text, sizeof(text), texts, count);
    bool lacking = recipe_lacks_tools(game, recipe, names, sizeof(names));
    TR_FORMAT(warning, sizeof(warning), tr("station.warning.tool"), lacking ? names : "");
    problem_row(layout, text, lacking, false, warning);
}

/* Settlers alive, by slot: their names and handles. Returns how many. */
static int living_colonists(const Game *game, char names[COLONIST_MAX][COLONIST_NAME_SIZE],
                            EntityId ids[COLONIST_MAX])
{
    int count = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game) && count < COLONIST_MAX; slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (!entity_id_is_none(id)) {
            SDL_strlcpy(names[count], game->colonists[slot].name, COLONIST_NAME_SIZE);
            ids[count++] = id;
        }
    }
    return count;
}

/* Who works on it, a drop-down list. A level range adds a slider
 * with two handles under it, one settler a list of every settler. */
static void bill_worker_rows(UiLayout *layout, const Game *game, const Bill *bill, int index,
                             Action *action)
{
    Ui *ui = layout->ui;
    const RecipeDef *def = recipe_def((RecipeType)bill->recipe);
    char level_option[TEXT_SIZE];
    TR_FORMAT(level_option, sizeof(level_option), tr("bill.worker.level"),
              def->knowledge != KNOWLEDGE_NONE ? tr(knowledge_def(def->knowledge)->name_key) : "");
    /* A level range needs a knowledge. */
    BillWorker modes[BILL_WORKER_COUNT];
    const char *options[BILL_WORKER_COUNT];
    int count = 0, selected = 0;
    modes[count] = BILL_WORKER_ANYONE;
    options[count++] = tr("bill.worker.anyone");
    if (def->knowledge != KNOWLEDGE_NONE) {
        modes[count] = BILL_WORKER_LEVEL;
        options[count++] = level_option;
    }
    modes[count] = BILL_WORKER_SPECIFIC;
    options[count++] = tr("bill.worker.specific");
    for (int i = 0; i < count; i++) {
        if (modes[i] == (BillWorker)bill->worker) {
            selected = i;
        }
    }
    float width = ui_dropdown_width(ui, options, count);
    ui_layout_measure(layout, width);
    if (layout->draw) {
        int picked = ui_dropdown(ui, DROPDOWN_ID + 2 * index,
                                 (SDL_FRect){layout->x, layout->y, width, layout->button_height},
                                 options, count, selected);
        if (picked >= 0) {
            *action =
                (Action){.type = ACTION_SET_WORKER, .bill = index, .value = (int)modes[picked]};
        }
    }
    layout->y += layout->row;

    if (bill->worker == BILL_WORKER_LEVEL) {
        char text[TEXT_SIZE], low[16], high[16];
        tr_integer(low, sizeof(low), bill->min_level);
        tr_integer(high, sizeof(high), bill->max_level);
        TR_FORMAT(text, sizeof(text), tr("bill.levels"), low, high);
        ui_layout_label(layout, text, 0.0f, true);
        ui_layout_measure(layout, LEVEL_SLIDER_WIDTH);
        int slider_low = bill->min_level - BILL_LEVEL_MIN,
            slider_high = bill->max_level - BILL_LEVEL_MIN;
        if (layout->draw &&
            ui_range_slider(ui, LEVEL_SLIDER_ID + index,
                            (SDL_FRect){layout->x, layout->y, layout->width, layout->row},
                            LEVEL_STEPS, &slider_low, &slider_high)) {
            *action = (Action){.type = ACTION_SET_LEVELS,
                               .bill = index,
                               .value = slider_low + BILL_LEVEL_MIN,
                               .value2 = slider_high + BILL_LEVEL_MIN};
        }
        layout->y += layout->row;
    } else if (bill->worker == BILL_WORKER_SPECIFIC) {
        char names[COLONIST_MAX][COLONIST_NAME_SIZE];
        EntityId ids[COLONIST_MAX];
        int colonists = living_colonists(game, names, ids);
        /* A settler who is gone: "Nobody" comes first. */
        int first = 0, chosen = -1;
        for (int i = 0; i < colonists; i++) {
            if (entity_id_equal(ids[i], bill->colonist)) {
                chosen = i;
            }
        }
        const char *list[COLONIST_MAX + 1];
        if (chosen < 0) {
            list[first++] = tr("bill.worker.nobody");
        }
        for (int i = 0; i < colonists; i++) {
            list[first + i] = names[i];
        }
        float list_width = ui_dropdown_width(ui, list, first + colonists);
        ui_layout_measure(layout, list_width);
        if (layout->draw) {
            int picked =
                ui_dropdown(ui, DROPDOWN_ID + 2 * index + 1,
                            (SDL_FRect){layout->x, layout->y, list_width, layout->button_height},
                            list, first + colonists, chosen < 0 ? 0 : chosen + first);
            if (picked >= first) {
                *action = (Action){
                    .type = ACTION_SET_COLONIST, .bill = index, .colonist = ids[picked - first]};
            }
        }
        layout->y += layout->row;
    }
}

/* The quality range of the ingredients a bill takes (steps of QUALITY_STEP). */
static void bill_quality_row(UiLayout *layout, const Bill *bill, int index, Action *action)
{
    Ui *ui = layout->ui;
    char text[TEXT_SIZE], low[32], high[32];
    hud_quality_text(low, sizeof(low), (ItemQuality)bill->min_quality);
    hud_quality_text(high, sizeof(high), (ItemQuality)bill->max_quality);
    TR_FORMAT(text, sizeof(text), tr("bill.quality"), low, high);
    ui_layout_label(layout, text, 0.0f, true);
    ui_layout_measure(layout, LEVEL_SLIDER_WIDTH);
    int slider_low = bill->min_quality / QUALITY_STEP;
    int slider_high = (bill->max_quality + QUALITY_STEP - 1) / QUALITY_STEP;
    if (layout->draw &&
        ui_range_slider(ui, QUALITY_SLIDER_ID + index,
                        (SDL_FRect){layout->x, layout->y, layout->width, layout->row},
                        ITEM_QUALITY_BEST / QUALITY_STEP + 1, &slider_low, &slider_high)) {
        *action = (Action){.type = ACTION_SET_QUALITY,
                           .bill = index,
                           .value = slider_low * QUALITY_STEP,
                           .value2 = slider_high * QUALITY_STEP};
    }
    layout->y += layout->row;
}

/* A slider for how far inputs are looked for, in the notches of a stockpile's; the tiles the
 * settlers reach within it are outlined on the map while the pointer is on it. */
static void bill_radius_row(UiLayout *layout, App *app, const Workstation *station,
                            const Bill *bill, int index, Action *action)
{
    Ui *ui = layout->ui;
    char text[TEXT_SIZE], radius[16];
    if (bill->input_radius == 0) {
        SDL_strlcpy(text, tr("bill.input_radius_none"), sizeof(text));
    } else {
        tr_integer(radius, sizeof(radius), bill->input_radius);
        TR_FORMAT(text, sizeof(text), tr("bill.input_radius"), radius);
    }
    ui_layout_label(layout, text, 0.0f, true);
    ui_layout_measure(layout, LEVEL_SLIDER_WIDTH);
    /* The notches of a stockpile's radius slider, but the last (unlimited). */
    const int steps = stockpile_radius_steps() - 1;
    int step = SDL_min(stockpile_radius_step(bill->input_radius), steps - 1);
    SDL_FRect track = {layout->x, layout->y, layout->width, layout->row};
    if (layout->draw) {
        int id = RADIUS_SLIDER_ID + index;
        if (ui_slider(ui, id, track, steps, &step)) {
            *action = (Action){
                .type = ACTION_SET_RADIUS, .bill = index, .value = stockpile_radius_of_step(step)};
        }
        if (ui_slider_engaged(ui, id, track)) {
            app->area_preview = (AreaPreview){
                .kind = AREA_PREVIEW_REACH,
                .from = {(int16_t)station->x, (int16_t)station->y, (int16_t)station->z},
                .cost = stockpile_radius_of_step(step)};
        }
    }
    layout->y += layout->row;
}

/* Why nobody works on a bill, other than the ingredients and tools (shown
 * in red on their own rows). */
static void bill_warning_rows(UiLayout *layout, const Game *game, const Workstation *station,
                              const Bill *bill)
{
    RecipeType recipe = (RecipeType)bill->recipe;
    if (!game_recipe_known(game, recipe)) {
        ui_layout_label(layout, tr("station.warning.unknown_recipe"), 0.0f, true);
        return;
    }
    if (recipe_needs_fire(recipe) && !station->burning) {
        ui_layout_label(layout, tr("station.warning.fire_out"), 0.0f, true);
    }
    if (!game_bill_has_worker(game, bill)) {
        ui_layout_label(layout, tr("station.warning.no_worker"), 0.0f, true);
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
            *action = (Action){.type = ACTION_TOGGLE_FUEL, .value = type};
        }
        x += box_width + GAP;
    }
    layout->y += layout->row;
}

/* The fire: fuel, burning or out, what the player wants of it, and which
 * knowledge is told at fireside stories (a colony setting, shown here). */
/* The hearth's state: fuel, fire. */
static void hearth_status_rows(UiLayout *layout, const Game *game, const Workstation *station)
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
}

/* What lighting a hearth takes, one row per ingredient ("Kindling: dry
 * twigs ×1"): "in place" once laid, red with a tooltip when none lies in the
 * colony (as a bill's ingredients). */
static void hearth_lighting_rows(UiLayout *layout, const Game *game, const Workstation *station)
{
    static const char *const LABELS[HEARTH_INGREDIENT_COUNT] = {
        [HEARTH_TINDER] = N_("station.hearth.tinder"),
        [HEARTH_KINDLING] = N_("station.hearth.kindling"),
        [HEARTH_BRANCH] = N_("station.hearth.branch"),
    };
    ui_layout_label(layout, tr("station.hearth.to_light"), 0.0f, false);
    for (int i = 0; i < HEARTH_INGREDIENT_COUNT; i++) {
        HearthIngredient ingredient = (HearthIngredient)i;
        char names[TEXT_SIZE], count[16], items[TEXT_SIZE], text[TEXT_SIZE];
        either_names(names, sizeof(names), hearth_ingredient_types(ingredient));
        tr_integer(count, sizeof(count), 1);
        TR_FORMAT(items, sizeof(items), tr("recipe.ingredient"), names, count);
        TR_FORMAT(text, sizeof(text), tr(LABELS[i]), items);
        bool laid = hearth_laid(station, ingredient);
        if (laid) {
            SDL_strlcpy(items, text, sizeof(items));
            TR_FORMAT(text, sizeof(text), tr("station.hearth.in_place"), items);
        }
        problem_row_at(layout, INDENT, text, game_hearth_lacks(game, station, ingredient), laid,
                       tr("station.warning.no_input"));
    }
    ui_layout_label(layout, tr("station.hearth.embers_hint"), INDENT, true);
    layout->y += GAP;
}

/* The hearth's settings: what lighting it takes while it is out, keep it
 * burning, its fuels, what is taught by it. */
static void hearth_settings_rows(UiLayout *layout, const Game *game, const Workstation *station,
                                 Action *action)
{
    if (!station->burning) {
        hearth_lighting_rows(layout, game, station);
    }
    if (checkbox_row(layout, 0.0f, tr("station.hearth.keep_burning"), station->keep_burning)) {
        *action = (Action){.type = ACTION_TOGGLE_KEEP_BURNING};
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
            *action = (Action){.type = ACTION_TOGGLE_TEACH, .value = knowledge};
        }
    }
    if (!any) {
        ui_layout_label(layout, tr("station.hearth.nothing_to_teach"), INDENT, true);
    }
}

/* What is being made, if anything. */
static void progress_row(UiLayout *layout, const Workstation *station)
{
    char text[TEXT_SIZE], percent[32];
    const RecipeDef *current = recipe_def((RecipeType)station->work_recipe);
    if (current != NULL && station->work_input.count > 0) {
        percent_text(percent, sizeof(percent), station->work_progress / current->work);
        TR_FORMAT(text, sizeof(text), tr("station.in_progress"), tr(current->name_key), percent);
        ui_layout_label(layout, text, 0.0f, false);
    }
}

typedef struct StationRows {
    App *app;
    RecipeType recipe; /* The recipe the details window (recipe_rows()) is about. */
    const Game *game;
    const Workstation *station;
    Action action;
} StationRows;

/* The button that opens the list of bills to add (lit while it is open). */
static void add_button_row(UiLayout *layout, StationRows *rows)
{
    Ui *ui = layout->ui;
    const char *label = tr("station.add_bill");
    bool full = rows->station->bill_count >= WORKSTATION_BILL_MAX;
    ui_layout_measure(layout, ui_button_width(ui, label));
    if (layout->draw) {
        SDL_FRect rect = {layout->x, layout->y, layout->width, layout->button_height};
        if (ui_toggle_button(ui, rect, label, rows->app->station_add_open) && !full) {
            rows->action = (Action){.type = ACTION_TOGGLE_ADD_LIST};
        }
        if (full) {
            ui_fill(ui, rect, ui->theme.colors[THEME_DISABLED_VEIL]);
        }
        if (ui_hovered(ui, rect)) {
            const char *lines[2] = {
                label, tr(full ? "station.add_bill.full" : "station.add_bill.description")};
            ui_tooltip(ui, lines, 2);
        }
    }
    layout->y += layout->row + GAP;
}

/* The bills: each a name with its pause and remove buttons and how often it
 * is done; a click on one opens its details at the right. */
static void bills_rows_of(UiLayout *layout, StationRows *rows, const Workstation *station)
{
    const Game *game = rows->game;
    Action *action = &rows->action;
    Ui *ui = layout->ui;
    add_button_row(layout, rows);
    for (int i = 0; i < station->bill_count; i++) {
        const Bill *bill = &station->bills[i];
        if (i > 0) {
            ui_layout_measure(layout, 0.0f);
            if (layout->draw) {
                ui_fill(ui, (SDL_FRect){layout->x, layout->y + GAP * 0.5f, layout->width, 1.0f},
                        ui->theme.colors[THEME_BORDER]);
            }
            layout->y += GAP;
        }
        /* The two rows light up as one: lit when picked, under the mouse. */
        SDL_FRect block = {layout->x, layout->y, layout->width, 2.0f * layout->row};
        if (layout->draw) {
            bool picked = rows->app->station_bill_picked && rows->app->station_bill == i;
            if (picked) {
                ui_fill(ui, block, ui->theme.colors[THEME_BUTTON_LIT]);
            } else if (ui_hovered(ui, block)) {
                ui_fill(ui, block, ui->theme.colors[THEME_SCROLL_TRACK]);
            }
        }
        bill_header_row(layout, game, station, bill, i, action);
        bill_mode_row(layout, game, bill, i, action);
        /* After the buttons, which take the clicks that are theirs. */
        if (layout->draw && ui_take_click(ui, block) == SDL_BUTTON_LEFT) {
            *action = (Action){.type = ACTION_PICK_BILL, .bill = i};
        }
    }
}

static Workstation as_known(const Game *game, const Workstation *station);

/* A dim title over a group of rows. */
static void heading_row(UiLayout *layout, const char *key)
{
    ui_layout_label(layout, tr(key), 0.0f, true);
}

/* The details of the bill picked: what it takes (ingredients and tools,
 * red when they cannot be had), who works on it, how far ingredients are
 * looked for and which of them may be used. */
static void bill_rows(UiLayout *layout, void *userdata)
{
    StationRows *rows = userdata;
    Workstation known = as_known(rows->game, rows->station);
    int index = rows->app->station_bill;
    if (index < 0 || index >= known.bill_count) {
        return;
    }
    const Bill *bill = &known.bills[index];
    const RecipeDef *def = recipe_def((RecipeType)bill->recipe);
    heading_row(layout, "station.recipe.ingredients");
    bill_ingredients_row(layout, rows->game, bill);
    layout->y += GAP;
    heading_row(layout, "bill.heading.tools");
    bill_tool_row(layout, rows->game, bill);
    layout->y += GAP;
    heading_row(layout, "bill.heading.worker");
    bill_worker_rows(layout, rows->game, bill, index, &rows->action);
    layout->y += GAP;
    bill_radius_row(layout, rows->app, rows->station, bill, index, &rows->action);
    if (def->inputs[0] != ITEM_NONE) {
        bill_quality_row(layout, bill, index, &rows->action);
    }
    if (def->inputs[1] != ITEM_NONE) {
        layout->y += GAP;
        heading_row(layout, "bill.heading.choice");
        bill_inputs_row(layout, bill, index, &rows->action);
    }
    layout->y += GAP;
    bill_warning_rows(layout, rows->game, &known, bill);
}

/* The summary: whether the settlers leave it alone, its fire, what is being
 * made, how many bills. */
/* The station as the colony knows it: what the player decided is exact (bills,
 * settings), but what the station does of itself (its fuel, its fire, a craft
 * under way) is what was last seen while it is out of view. */
static Workstation as_known(const Game *game, const Workstation *station)
{
    Workstation known = *station;
    if (game->fog_on && !game_tile_seen(game, station->x, station->y, station->z)) {
        const KnownStation *picture =
            &game->known.stations[map_index(&game->known.map, station->x, station->y, station->z)];
        known.burning = picture->burning;
        known.fuel = (float)picture->fuel * 0.1f;
        known.soaked = 0.0f;
        known.work_recipe = RECIPE_COUNT;
        known.work_progress = 0.0f;
    }
    return known;
}

static void summary_rows(UiLayout *layout, void *userdata)
{
    StationRows *rows = userdata;
    Workstation known = as_known(rows->game, rows->station);
    const Workstation *station = &known;
    if (station->forbidden) {
        ui_layout_label(layout, tr("tile_panel.left_alone"), 0.0f, true);
    }
    if (hearth_is(station)) {
        hearth_status_rows(layout, rows->game, station);
    }
    progress_row(layout, station);
    char text[TEXT_SIZE], number[16];
    tr_integer(number, sizeof(number), station->bill_count);
    TR_FORMAT(text, sizeof(text), tr_plural("station.bill_count", station->bill_count), number);
    ui_layout_label(layout, text, 0.0f, station->bill_count == 0);
}

static void bills_rows(UiLayout *layout, void *userdata)
{
    StationRows *rows = userdata;
    Workstation known = as_known(rows->game, rows->station);
    bills_rows_of(layout, rows, &known);
}

static void fire_rows(UiLayout *layout, void *userdata)
{
    StationRows *rows = userdata;
    hearth_settings_rows(layout, rows->game, rows->station, &rows->action);
}

/* Recipes a station makes, in the table's order. */
static int station_recipes(const Workstation *station, RecipeType out[RECIPE_COUNT])
{
    int count = 0;
    for (int recipe = 0; recipe < RECIPE_COUNT; recipe++) {
        if (recipe_def((RecipeType)recipe)->stations & WORKSTATION_BIT(station->type)) {
            out[count++] = (RecipeType)recipe;
        }
    }
    return count;
}

/* Item names are the tooltip of the small pictures. */
static void item_picture_tooltip(Ui *ui, ItemType type)
{
    const char *name = tr(item_def(type)->name_key);
    ui_tooltip(ui, &name, 1);
}

/* An item picture on its well, `size` wide. */
static void item_picture(App *app, SDL_FRect rect, ItemType type)
{
    ui_fill(&app->ui, rect, app->ui.theme.colors[THEME_ICON_WELL]);
    render_item_icon(app->renderer, app->sprites, rect, type);
}

/* The window of recipes to add, one per row: the picture of what it makes,
 * its name, and its price: a picture per ingredient with the count under
 * it. A click picks the recipe (its details open at the right), a double
 * click adds it as a bill. */
static void add_rows(UiLayout *layout, void *userdata)
{
    StationRows *rows = userdata;
    App *app = rows->app;
    Ui *ui = layout->ui;
    RecipeType recipes[RECIPE_COUNT];
    int count = station_recipes(rows->station, recipes);
    float line = (float)text_line_height(ui->text);
    float product_size = SDL_roundf(layout->row * 1.3f);
    float cost_size = SDL_roundf(layout->row * 0.9f);
    float row_height = SDL_max(product_size, cost_size + line) + 4.0f;
    float slash = ui_text_width(ui, "/");
    /* Columns: the picture, the name, the price. */
    float name_width = 0.0f, cost_width = 0.0f;
    for (int i = 0; i < count; i++) {
        const RecipeDef *def = recipe_def(recipes[i]);
        name_width = SDL_max(name_width, ui_text_width(ui, tr(def->name_key)));
        float width = 0.0f;
        for (int input = 0; input < RECIPE_INPUT_ALTERNATIVES; input++) {
            if (def->inputs[input] != ITEM_NONE) {
                char number[16];
                tr_integer(number, sizeof(number), def->input_count);
                width += (width > 0.0f ? slash + GAP : 0.0f) +
                         SDL_max(cost_size, ui_text_width(ui, number));
            }
        }
        cost_width = SDL_max(cost_width, width);
    }
    float name_x = product_size + GAP, cost_x = name_x + name_width + 2.0f * GAP;
    ui_layout_measure(layout, cost_x + cost_width);
    for (int i = 0; i < count; i++) {
        RecipeType recipe = recipes[i];
        const RecipeDef *def = recipe_def(recipe);
        SDL_FRect row = {layout->x, layout->y, layout->width, row_height};
        bool picked = app->station_recipe_picked && app->station_recipe == (int)recipe;
        if (layout->draw) {
            if (picked) {
                ui_fill(ui, row, ui->theme.colors[THEME_BUTTON_LIT]);
            } else if (ui_hovered(ui, row)) {
                ui_fill(ui, row, ui->theme.colors[THEME_SCROLL_TRACK]);
            }
            float middle = row.y + row_height * 0.5f;
            item_picture(app,
                         (SDL_FRect){row.x, SDL_roundf(middle - product_size * 0.5f), product_size,
                                     product_size},
                         def->product);
            ui_label(ui, tr(def->name_key), row.x + name_x, SDL_floorf(middle - line * 0.5f));
            float x = row.x + cost_x;
            bool first = true;
            for (int input = 0; input < RECIPE_INPUT_ALTERNATIVES; input++) {
                ItemType type = def->inputs[input];
                if (type == ITEM_NONE) {
                    continue;
                }
                char number[16];
                tr_integer(number, sizeof(number), def->input_count);
                float cell = SDL_max(cost_size, ui_text_width(ui, number));
                if (!first) {
                    ui_label_dim(ui, "/", x, SDL_floorf(middle - line * 0.5f));
                    x += slash + GAP;
                }
                first = false;
                float top = SDL_roundf(middle - (cost_size + line) * 0.5f);
                SDL_FRect picture = {SDL_roundf(x + (cell - cost_size) * 0.5f), top, cost_size,
                                     cost_size};
                item_picture(app, picture, type);
                ui_label(ui, number, x + (cell - ui_text_width(ui, number)) * 0.5f,
                         top + cost_size);
                if (ui_hovered(ui, (SDL_FRect){x, top, cell, cost_size + line})) {
                    item_picture_tooltip(ui, type);
                }
                x += cell;
            }
            if (ui_take_click(ui, row) == SDL_BUTTON_LEFT) {
                Uint64 now = SDL_GetTicks();
                if (app->station_click_recipe == (int)recipe &&
                    now - app->station_click_ms <= APP_DOUBLE_CLICK_MS) {
                    rows->action = (Action){.type = ACTION_ADD_BILL, .value = (int)recipe};
                    app->station_click_recipe = -1;
                } else {
                    rows->action = (Action){.type = ACTION_PICK_RECIPE, .value = (int)recipe};
                    app->station_click_recipe = (int)recipe;
                    app->station_click_ms = now;
                }
            }
        }
        layout->y += row_height;
    }
}

/* A table row of the recipe's details: what it is, then its value. Returns
 * the row's rectangle. */
static SDL_FRect detail_row(UiLayout *layout, const char *label_key, const char *value,
                            bool hoverable)
{
    return ui_layout_pair(layout, tr(label_key), value, layout->ui->theme.colors[THEME_TEXT],
                          hoverable);
}

/* What the recipe's product is, made by a settler at `level` (the average
 * result: no luck, no failure). */
static ItemStack average_product(RecipeType recipe, double level)
{
    const RecipeDef *def = recipe_def(recipe);
    const ItemDef *material = item_def(def->inputs[0]);
    ItemStack input = item_stack_make(def->inputs[0], def->input_count, ITEM_QUALITY_BEST,
                                      material->max_hit_points);
    ItemStack made[RECIPE_PRODUCT_MAX];
    int count = recipe_craft(recipe, input, level, NULL, made);
    for (int i = 0; i < count; i++) {
        if (made[i].type == def->product) {
            return made[i];
        }
    }
    return item_stack_make(ITEM_NONE, 0, 0, 0);
}

/* The tooltip of a recipe's knowledge: what it does for this recipe, at the
 * lowest level that can make it and at the highest. Returns the line count. */
static int knowledge_tooltip(char lines[UI_TOOLTIP_LINES][TEXT_SIZE], RecipeType recipe)
{
    const RecipeDef *def = recipe_def(recipe);
    double low = def->min_level, high = KNOWLEDGE_LEVEL_MAX;
    char a[16], b[16], c[16], d[16], e[16];
    int count = 0;
    SDL_strlcpy(lines[count++], tr(knowledge_def(def->knowledge)->name_key), TEXT_SIZE);
    tr_integer(a, sizeof(a), (long long)low);
    tr_integer(b, sizeof(b), (long long)KNOWLEDGE_MASTERY_LEVEL);
    TR_FORMAT(lines[count++], TEXT_SIZE, tr("recipe.tip.unlock"), a, b);
    /* Speed. */
    tr_decimal(a, sizeof(a), knowledge_work_speed(low), 2);
    tr_integer(b, sizeof(b), (long long)low);
    tr_decimal(c, sizeof(c), knowledge_work_speed(high), 2);
    tr_integer(d, sizeof(d), (long long)high);
    TR_FORMAT(lines[count++], TEXT_SIZE, tr("recipe.tip.speed"), a, b, c, d);
    /* What it makes, when that depends on the level. */
    ItemStack at_low = average_product(recipe, low), at_high = average_product(recipe, high);
    tr_integer(b, sizeof(b), (long long)low);
    tr_integer(d, sizeof(d), (long long)high);
    if (at_low.count != at_high.count) {
        tr_integer(a, sizeof(a), at_low.count);
        tr_integer(c, sizeof(c), at_high.count);
        TR_FORMAT(lines[count++], TEXT_SIZE, tr("recipe.tip.count"), a, b, c, d);
    }
    if (at_low.quality != at_high.quality) {
        tr_integer(a, sizeof(a), at_low.quality);
        tr_integer(c, sizeof(c), at_high.quality);
        tr_integer(e, sizeof(e), ITEM_QUALITY_CAP_BONUS);
        TR_FORMAT(lines[count++], TEXT_SIZE, tr("recipe.tip.quality"), a, b, c, d, e);
    }
    int condition_low = item_stack_hit_points_percent(at_low);
    int condition_high = item_stack_hit_points_percent(at_high);
    if (condition_low != condition_high) {
        char percent_low[16], percent_high[16];
        tr_integer(a, sizeof(a), condition_low);
        TR_FORMAT(percent_low, sizeof(percent_low), tr("unit.percent"), a);
        tr_integer(c, sizeof(c), condition_high);
        TR_FORMAT(percent_high, sizeof(percent_high), tr("unit.percent"), c);
        TR_FORMAT(lines[count++], TEXT_SIZE, tr("recipe.tip.condition"), percent_low, b,
                  percent_high, d);
    }
    /* Failures, which stop at some level. */
    float failure = recipe_failure_chance(recipe, low);
    if (failure > 0.0f) {
        int safe = (int)high;
        for (int level = (int)low; level <= (int)high; level++) {
            if (recipe_failure_chance(recipe, level) < 0.005f) { /* Below half a percent. */
                safe = level;
                break;
            }
        }
        tr_integer(a, sizeof(a), (long long)SDL_roundf(failure * 100.0f));
        char percent[16];
        TR_FORMAT(percent, sizeof(percent), tr("unit.percent"), a);
        tr_integer(c, sizeof(c), safe);
        TR_FORMAT(lines[count++], TEXT_SIZE, tr("recipe.tip.failure"), percent, b, c);
    }
    return count;
}

/* The details of a recipe: what making it takes and gives. */
static void recipe_rows(UiLayout *layout, void *userdata)
{
    StationRows *rows = userdata;
    RecipeType recipe = rows->recipe;
    const RecipeDef *def = recipe_def(recipe);
    char text[TEXT_SIZE], number[16];
    detail_row(layout, "station.recipe.work", tr(work_type_def(WORK_CRAFTING)->name_key), false);
    if (def->knowledge != KNOWLEDGE_NONE) {
        tr_integer(number, sizeof(number), (long long)def->min_level);
        TR_FORMAT(text, sizeof(text), tr("recipe.knowledge_level"),
                  tr(knowledge_def(def->knowledge)->name_key), number);
        SDL_FRect row = detail_row(layout, "station.recipe.knowledge", text, true);
        if (layout->draw && ui_hovered(layout->ui, row)) {
            char lines[UI_TOOLTIP_LINES][TEXT_SIZE];
            const char *shown[UI_TOOLTIP_LINES];
            int count = knowledge_tooltip(lines, recipe);
            for (int i = 0; i < count; i++) {
                shown[i] = lines[i];
            }
            ui_tooltip(layout->ui, shown, count);
        }
    } else {
        detail_row(layout, "station.recipe.knowledge", tr("station.recipe.none"), false);
    }
    ingredient_text(text, sizeof(text), recipe, recipe_inputs(recipe));
    detail_row(layout, "station.recipe.ingredients", text, false);
    duration_text(text, sizeof(text), def->work);
    detail_row(layout, "station.recipe.time", text, false);
    layout->y += GAP;
    /* The thing made. */
    const ItemDef *product = item_def(def->product);
    detail_row(layout, "station.recipe.result", tr(product->name_key), false);
    quantity_text(text, sizeof(text), product->weight, "unit.kg");
    detail_row(layout, "station.recipe.weight", text, false);
    quantity_text(text, sizeof(text), product->bulk, NULL);
    detail_row(layout, "station.recipe.bulk", text, false);
}

/* The details of `recipe` in a window docked after the window `after`;
 * false when its × was clicked. */
static bool recipe_window(App *app, StationRows *rows, RecipeType recipe, int after)
{
    bool shown = true;
    rows->recipe = recipe;
    inspector_side_window(app, APP_WINDOW_STATION_RECIPE, after, tr(recipe_def(recipe)->name_key),
                          recipe_rows, rows, &shown);
    return shown;
}

/* The windows to the right of the bills: the recipes that can be added, and
 * the details of the one picked. */
static void add_windows(App *app, StationRows *rows)
{
    if (app->station_bill_picked) {
        if (app->station_bill >= rows->station->bill_count) {
            app->station_bill_picked = false;
            return;
        }
        RecipeType recipe = (RecipeType)rows->station->bills[app->station_bill].recipe;
        bool shown = true;
        inspector_side_window(app, APP_WINDOW_STATION_BILL, APP_WINDOW_INSPECTOR_TAB,
                              tr(recipe_def(recipe)->name_key), bill_rows, rows, &shown);
        /* The same details as when adding a bill, at its right. */
        app->station_bill_picked =
            shown && recipe_window(app, rows, recipe, APP_WINDOW_STATION_BILL);
        return;
    }
    if (!app->station_add_open) {
        return;
    }
    bool open = true;
    inspector_side_window(app, APP_WINDOW_STATION_ADD, APP_WINDOW_INSPECTOR_TAB,
                          tr("station.add_bill"), add_rows, rows, &open);
    if (!open) {
        app->station_add_open = false;
        app->station_recipe_picked = false;
        return;
    }
    RecipeType recipes[RECIPE_COUNT];
    int count = station_recipes(rows->station, recipes);
    bool known = false; /* A recipe of another kind of station is dropped. */
    for (int i = 0; i < count; i++) {
        known = known || (int)recipes[i] == app->station_recipe;
    }
    if (app->station_recipe_picked && known) {
        app->station_recipe_picked =
            recipe_window(app, rows, (RecipeType)app->station_recipe, APP_WINDOW_STATION_ADD);
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
    case ACTION_DELETE_BILL:
        game_remove_bill(&app->game, index, action->bill);
        if (app->station_bill_picked && app->station_bill == action->bill) {
            app->station_bill_picked = false;
        } else if (app->station_bill > action->bill) {
            app->station_bill--;
        }
        break;
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
    case ACTION_SET_WORKER:
        if (bill != NULL) {
            EntityId colonist = bill->colonist;
            if (action->value == BILL_WORKER_SPECIFIC && entity_id_is_none(colonist)) {
                /* The first settler alive, until the player picks another. */
                for (uint32_t slot = 0; slot < game_colonist_slot_count(&app->game); slot++) {
                    colonist = game_colonist_id_at(&app->game, slot);
                    if (!entity_id_is_none(colonist)) {
                        break;
                    }
                }
            }
            workstation_set_bill_worker(workstations, index, action->bill,
                                        (BillWorker)action->value, colonist);
        }
        break;
    case ACTION_SET_COLONIST:
        if (bill != NULL) {
            workstation_set_bill_worker(workstations, index, action->bill, BILL_WORKER_SPECIFIC,
                                        action->colonist);
        }
        break;
    case ACTION_SET_LEVELS:
        if (bill != NULL) {
            /* Each bound stays within the other: the one that moves away goes first. */
            if (action->value > bill->max_level) {
                workstation_set_bill_max_level(workstations, index, action->bill, action->value2);
                workstation_set_bill_min_level(workstations, index, action->bill, action->value);
            } else {
                workstation_set_bill_min_level(workstations, index, action->bill, action->value);
                workstation_set_bill_max_level(workstations, index, action->bill, action->value2);
            }
        }
        break;
    case ACTION_SET_QUALITY:
        if (bill != NULL) {
            workstation_set_bill_quality_range(workstations, index, action->bill, action->value,
                                               action->value2);
        }
        break;
    case ACTION_TOGGLE_PAUSE:
        if (bill != NULL) {
            workstation_set_bill_paused(workstations, index, action->bill, !bill->paused);
        }
        break;
    case ACTION_SET_RADIUS:
        if (bill != NULL) {
            workstation_set_bill_input_radius(workstations, index, action->bill, action->value);
        }
        break;
    case ACTION_ADD_BILL:
        workstation_add_bill(workstations, index, (RecipeType)action->value);
        break;
    case ACTION_TOGGLE_ADD_LIST:
        app->station_add_open = !app->station_add_open;
        app->station_recipe_picked = false;
        app->station_bill_picked = false;
        break;
    case ACTION_PICK_RECIPE:
        app->station_recipe = action->value;
        app->station_recipe_picked = true;
        break;
    case ACTION_PICK_BILL:
        /* Its details take the place of the list of recipes. */
        app->station_bill = action->bill;
        app->station_bill_picked = true;
        app->station_add_open = false;
        app->station_recipe_picked = false;
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
    const char *title = tr(workstation_def((WorkstationType)station->type)->name_key);
    StationRows rows = {app, RECIPE_COUNT, &app->game, station, {.type = ACTION_NONE, .bill = -1}};
    bool open = inspector_box(app, INSPECT_STATION, title, summary_rows, &rows);
    const char *tabs[2] = {tr("inspector.tab.bills"), tr("inspector.tab.fire")};
    int tab = inspector_tabs(app, tabs, hearth_is(station) ? 2 : 1);
    /* What was open for another station or tab is forgotten. */
    if (tab != 0 || app->station_panel_number != station->number) {
        app->station_add_open = false;
        app->station_recipe_picked = false;
        app->station_bill_picked = false;
    }
    app->station_panel_number = station->number;
    if (tab == 0) {
        inspector_tab_panel(app, bills_rows, &rows);
        add_windows(app, &rows);
    } else if (tab == 1) {
        inspector_tab_panel(app, fire_rows, &rows);
    }
    if (inspector_forbid_action(app, 0, station->forbidden, NULL)) {
        rows.action = (Action){.type = ACTION_TOGGLE_FORBIDDEN};
    }
    inspector_build_like_action(app, 1);
    if (inspector_delete_action(app, 2, tr("station.delete.description"), false)) {
        rows.action = (Action){.type = ACTION_DELETE_STATION};
    }
    int priority;
    if (inspector_priority_action(app, 3, station->priority, &priority)) {
        workstation_set_priority(&app->game.workstations, index, priority);
    }
    if (rows.action.type != ACTION_NONE && app->selected_station == index) {
        apply(app, index, &rows.action);
    }
    if (!open) {
        app->selected_station = WORKSTATION_NONE;
        app->station_add_open = false;
        app->station_recipe_picked = false;
        app->station_bill_picked = false;
    }
}
