#include "game/gather_zone_panel.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

#define GAP UI_LAYOUT_GAP
#define INDENT UI_LAYOUT_INDENT
#define TEXT_SIZE 160
#define TARGET_BIG_STEP 10 /* Shift+click on -/+. */
#define RIPE_RAYS 7

/* What the player clicked, applied after the rows. */
typedef enum ActionType {
    ACTION_NONE,
    ACTION_DELETE,
    ACTION_TOGGLE,      /* A resource or a group on or off. */
    ACTION_TOGGLE_RIPE, /* Of a resource, a group, or the whole zone. */
    ACTION_SET_MODE,
    ACTION_SET_UNIT,
    ACTION_ADD_TARGET,
    ACTION_FOLD,
    ACTION_ADD_PRIORITY,
} ActionType;

/* What an action or a row is about: a resource, a group, or the whole zone
 * (resource GATHER_RESOURCE_COUNT and group GATHER_GROUP_NONE). */
typedef struct Subject {
    GatherResource resource;
    GatherGroup group;
} Subject;

static const Subject WHOLE_ZONE = {GATHER_RESOURCE_COUNT, GATHER_GROUP_NONE};

typedef struct Action {
    ActionType type;
    Subject subject;
    int value; /* New mode or unit, target or priority change. */
} Action;

/* What the rows of the zone show, in order: the groups and grass. */
typedef struct Entry {
    bool is_group;
    int value; /* GatherGroup or GatherResource. */
} Entry;

static const Entry ENTRIES[] = {
    {true, GATHER_GROUP_WOOD},
    {true, GATHER_GROUP_FOOD},
    {false, GATHER_GRASS},
    {true, GATHER_GROUP_STONES},
};

/* Dropdown options, in GatherMode and GatherUnit order. */
static const char *const MODE_KEYS[GATHER_MODE_COUNT] = {
    [GATHER_DO_COUNT] = N_("gather_zone.mode.do_count"),
    [GATHER_UNTIL] = N_("gather_zone.mode.until"),
    [GATHER_FOREVER] = N_("gather_zone.mode.forever"),
};

static const char *const UNIT_KEYS[GATHER_UNIT_COUNT] = {
    [GATHER_UNIT_ITEMS] = N_("gather_zone.unit.items"),
    [GATHER_UNIT_FOOD_DAYS] = N_("gather_zone.unit.food_days"),
};

/* Where the columns of a row start, from the left of the content, and how
 * wide the controls are. */
typedef struct Columns {
    float icon;      /* Side of the square icon buttons. */
    float available; /* Start of the available count. */
    float ripe;      /* Start of the full harvest icon. */
    float mode;      /* Start of the mode dropdown. */
    float target;    /* Start of -, the target, +. */
    float unit;      /* Start of the unit (dropdown or label). */
    float progress;  /* Start of the progress. */
    float mode_width, small, number, unit_width;
    const char *modes[GATHER_MODE_COUNT];
    const char *units[GATHER_UNIT_COUNT];
} Columns;

/* The settings of an order, a resource's or a group's. */
typedef struct OrderView {
    GatherMode mode;
    GatherUnit unit;
    int target;
    bool food;     /* The unit may be days of food. */
    long done;     /* Gathered so far. */
    long in_items; /* Lying in the colony. */
    long in_days;  /* Days of food in the colony. */
} OrderView;

typedef struct ZoneRows {
    App *app;
    int index;
    long available[GATHER_RESOURCE_COUNT]; /* Counted once per frame. */
    float daily_food;
    Columns columns;
    Action action;
} ZoneRows;

static UiCheck check_of(GatherResourceMask set, GatherResourceMask among)
{
    set &= among;
    return set == 0 ? UI_UNCHECKED : set == among ? UI_CHECKED : UI_PARTIAL;
}

/* Resources of a subject. */
static GatherResourceMask members_of(Subject subject)
{
    if ((unsigned)subject.resource < GATHER_RESOURCE_COUNT) {
        return GATHER_RESOURCE_BIT(subject.resource);
    }
    const GatherGroupDef *group = gather_group_def(subject.group);
    return group != NULL ? group->members : ~(GatherResourceMask)0;
}

/* Id of a dropdown of a subject (`which`: 0 mode, 1 unit). */
static int dropdown_id(Subject subject, int which)
{
    int base = (unsigned)subject.resource < GATHER_RESOURCE_COUNT
                   ? (int)subject.resource
                   : GATHER_RESOURCE_COUNT + (int)subject.group;
    return 1 + base * 2 + which;
}

/* ---- Full harvest icon ------------------------------------------------------ */

static void triangle(SDL_Renderer *renderer, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c,
                     SDL_Color color)
{
    SDL_FColor fcolor = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
    SDL_Vertex vertices[3] = {
        {a, fcolor, {0.0f, 0.0f}}, {b, fcolor, {0.0f, 0.0f}}, {c, fcolor, {0.0f, 0.0f}}};
    SDL_RenderGeometry(renderer, NULL, vertices, 3, NULL, 0);
}

/* A leaf from the stem at (x, y), towards `side` (-1 left, 1 right). */
static void leaf(SDL_Renderer *renderer, float x, float y, float size, float side,
                 SDL_Color color)
{
    SDL_FPoint base = {x, y};
    SDL_FPoint tip = {x + side * 0.26f * size, y - 0.16f * size};
    SDL_FPoint upper = {x + side * 0.10f * size, y - 0.15f * size};
    SDL_FPoint lower = {x + side * 0.17f * size, y + 0.01f * size};
    triangle(renderer, base, lower, tip, color);
    triangle(renderer, base, tip, upper, color);
}

/* A young plant with rays around it like a sun: lit when on, half the rays
 * when on for some resources only, grey when off. */
static void draw_ripe_icon(Ui *ui, SDL_FRect rect, UiCheck state)
{
    static const SDL_Color PLANT = {120, 200, 90, 255};
    static const SDL_Color RAY = {240, 200, 70, 255};
    static const SDL_Color OFF = {120, 124, 132, 255};
    SDL_Renderer *renderer = ui->renderer;
    float size = SDL_min(rect.w, rect.h);
    float cx = SDL_floorf(rect.x + rect.w * 0.5f) + 0.5f;
    float cy = rect.y + rect.h * 0.56f;
    float bottom = rect.y + rect.h * 0.84f;
    SDL_Color plant = state == UI_UNCHECKED ? OFF : PLANT;
    /* Rays over the upper half, from the left to the right. */
    for (int i = 0; i < RIPE_RAYS; i++) {
        float angle = SDL_PI_F * (1.0f + (float)i / (float)(RIPE_RAYS - 1));
        float dx = SDL_cosf(angle), dy = SDL_sinf(angle);
        bool lit = state == UI_CHECKED || (state == UI_PARTIAL && i % 2 == 0);
        SDL_Color color = lit ? RAY : OFF;
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        float inner = 0.32f * size, outer = 0.48f * size;
        for (int side = 0; side < 2; side++) { /* Two pixels thick. */
            float ox = side == 0 ? 0.0f : -dy, oy = side == 0 ? 0.0f : dx;
            SDL_RenderLine(renderer, cx + dx * inner + ox, cy + dy * inner + oy,
                           cx + dx * outer + ox, cy + dy * outer + oy);
        }
    }
    SDL_SetRenderDrawColor(renderer, plant.r, plant.g, plant.b, plant.a);
    float top = cy - 0.12f * size;
    SDL_RenderLine(renderer, cx, bottom, cx, top);
    SDL_RenderLine(renderer, cx - 0.5f, bottom, cx - 0.5f, top);
    leaf(renderer, cx, cy + 0.12f * size, size, -1.0f, plant);
    leaf(renderer, cx, cy + 0.02f * size, size, 1.0f, plant);
    float bud = SDL_floorf(0.12f * size);
    SDL_RenderFillRect(renderer, &(SDL_FRect){cx - bud * 0.5f, top - bud * 0.5f, bud, bud});
}

/* The full harvest button of a subject, with its tooltip; true when clicked. */
static bool ripe_button(UiLayout *layout, float x, float size, UiCheck state, bool whole_zone)
{
    if (!layout->draw) {
        return false;
    }
    Ui *ui = layout->ui;
    SDL_FRect rect = {x, layout->y, size, layout->button_height};
    bool clicked = ui_icon_button(ui, rect, state == UI_CHECKED);
    draw_ripe_icon(ui, rect, state);
    if (ui_hovered(ui, rect)) {
        static const char *const STATE_KEYS[] = {
            [UI_UNCHECKED] = N_("gather_zone.ripe.off"),
            [UI_PARTIAL] = N_("gather_zone.ripe.partial"),
            [UI_CHECKED] = N_("gather_zone.ripe.on"),
        };
        const char *lines[5] = {tr("gather_zone.ripe.title"), tr(STATE_KEYS[state]),
                                tr("gather_zone.ripe.fruit"), tr("gather_zone.ripe.grown"),
                                tr("gather_zone.ripe.all")};
        ui_tooltip(ui, lines, whole_zone ? 5 : 4);
    }
    return clicked;
}

/* ---- Rows ------------------------------------------------------------------- */

/* How many items a subject holds in the zone now. */
static const char *available_text(char *out, size_t size, long available)
{
    char number[16];
    tr_integer(number, sizeof(number), available);
    TR_FORMAT(out, size, tr_plural("gather_zone.available", available), number);
    return out;
}

/* How far an order has got: gathered so far, or what lies in the colony (in
 * its unit when it gathers until a number). */
static const char *progress_text(char *out, size_t size, const OrderView *order)
{
    char number[16];
    switch (order->mode) {
    case GATHER_DO_COUNT:
        tr_integer(number, sizeof(number), order->done);
        TR_FORMAT(out, size, tr("gather_zone.progress.done"), number);
        break;
    case GATHER_UNTIL:
        tr_integer(number, sizeof(number),
                   order->unit == GATHER_UNIT_FOOD_DAYS ? order->in_days : order->in_items);
        TR_FORMAT(out, size, tr("gather_zone.progress.now"), number);
        break;
    default:
        tr_integer(number, sizeof(number), order->in_items);
        TR_FORMAT(out, size, tr("bill.progress.in_colony"), number);
        break;
    }
    return out;
}

/* The mode dropdown, the target with -/+ and the unit, then the progress, on
 * the current row (it does not advance). */
static void order_controls(UiLayout *layout, const Columns *columns, Subject subject,
                           const OrderView *order, Action *action)
{
    Ui *ui = layout->ui;
    float left = layout->x;
    char progress[TEXT_SIZE];
    progress_text(progress, sizeof(progress), order);
    float progress_x = order->mode == GATHER_FOREVER ? columns->target : columns->progress;
    ui_layout_measure(layout, progress_x + ui_text_width(ui, progress));
    if (!layout->draw) {
        return;
    }
    float height = layout->button_height;
    int picked = ui_dropdown(ui, dropdown_id(subject, 0),
                             (SDL_FRect){left + columns->mode, layout->y, columns->mode_width,
                                         height},
                             columns->modes, GATHER_MODE_COUNT, (int)order->mode);
    if (picked >= 0) {
        *action = (Action){ACTION_SET_MODE, subject, picked};
    }
    if (order->mode != GATHER_FOREVER) {
        /* Symbols, not words: the same in every language. */
        int step = (SDL_GetModState() & SDL_KMOD_SHIFT) ? TARGET_BIG_STEP : 1;
        float x = left + columns->target;
        if (ui_layout_button_sized(layout, x, columns->small, "−")) {
            *action = (Action){ACTION_ADD_TARGET, subject, -step};
        }
        x += columns->small + 2.0f;
        char target[16];
        tr_integer(target, sizeof(target), order->target);
        ui_cell(ui, (SDL_FRect){x, layout->y, columns->number, height}, target,
                (SDL_Color){24, 27, 34, 255});
        x += columns->number + 2.0f;
        if (ui_layout_button_sized(layout, x, columns->small, "+")) {
            *action = (Action){ACTION_ADD_TARGET, subject, step};
        }
    }
    if (order->mode == GATHER_UNTIL) {
        float x = left + columns->unit;
        if (order->food) {
            picked = ui_dropdown(ui, dropdown_id(subject, 1),
                                 (SDL_FRect){x, layout->y, columns->unit_width, height},
                                 columns->units, GATHER_UNIT_COUNT, (int)order->unit);
            if (picked >= 0) {
                *action = (Action){ACTION_SET_UNIT, subject, picked};
            }
        } else {
            ui_label(ui, columns->units[GATHER_UNIT_ITEMS], x, layout->y + layout->text_offset);
        }
    }
    ui_label_dim(ui, progress, left + progress_x, layout->y + layout->text_offset);
}

static OrderView resource_view(const ZoneRows *rows, const GatherOrder *order,
                               GatherResource resource)
{
    const long *totals = rows->app->game.items.totals;
    return (OrderView){
        .mode = (GatherMode)order->mode,
        .unit = (GatherUnit)order->unit,
        .target = order->target,
        .food = gather_resource_is_food(resource),
        .done = order->done,
        .in_items = gather_resource_count(resource, totals),
        .in_days = gather_resource_food_days(resource, totals, rows->daily_food),
    };
}

static OrderView group_view(const ZoneRows *rows, const GatherGroupOrder *order,
                            GatherGroup group)
{
    const long *totals = rows->app->game.items.totals;
    return (OrderView){
        .mode = (GatherMode)order->mode,
        .unit = (GatherUnit)order->unit,
        .target = order->target,
        .food = gather_group_def(group)->food,
        .done = order->done,
        .in_items = gather_group_count(group, totals),
        .in_days = gather_group_food_days(group, totals, rows->daily_food),
    };
}

/* A resource: check box and name (`indent` from the name column's start),
 * how many the zone holds, the full harvest option, and when on, its order;
 * warnings below. */
static void resource_rows(UiLayout *layout, ZoneRows *rows, GatherResource resource,
                          float indent)
{
    Ui *ui = layout->ui;
    const Columns *columns = &rows->columns;
    const GatherZone *zone = gather_zone_get(&rows->app->game.gather_zones, rows->index);
    const GatherResourceDef *def = gather_resource_def(resource);
    const GatherOrder *order = &zone->orders[resource];
    Subject subject = {resource, GATHER_GROUP_NONE};
    const char *name = tr(def->name_key);
    float x = layout->x + columns->icon + GAP + indent;
    if (layout->draw && ui_checkbox(ui, x, layout->y, ui_checkbox_width(ui, name),
                                    order->enabled ? UI_CHECKED : UI_UNCHECKED, name)) {
        rows->action = (Action){ACTION_TOGGLE, subject, 0};
    }
    char count[TEXT_SIZE];
    available_text(count, sizeof(count), rows->available[resource]);
    ui_layout_measure(layout, columns->ripe);
    if (layout->draw) {
        ui_label_dim(ui, count, layout->x + columns->available, layout->y + layout->text_offset);
    }
    if (def->ripe_option &&
        ripe_button(layout, layout->x + columns->ripe, columns->icon,
                    order->ripe_only ? UI_CHECKED : UI_UNCHECKED, false)) {
        rows->action = (Action){ACTION_TOGGLE_RIPE, subject, 0};
    }
    if (order->enabled) {
        OrderView view = resource_view(rows, order, resource);
        order_controls(layout, columns, subject, &view, &rows->action);
    }
    layout->y += layout->row;
    if (!order->enabled) {
        return;
    }
    float warning_indent = columns->icon + GAP + indent + INDENT;
    if (!game_gather_job_known(&rows->app->game, def->job)) {
        ui_layout_label(layout, tr("hud.tool.unavailable"), warning_indent, true);
    }
    if (rows->available[resource] == 0) {
        ui_layout_label(layout, tr("gather_zone.warning.none_left"), warning_indent, true);
    }
}

/* A group: fold button, tri-state check box and name, what its resources hold,
 * the full harvest option of its resources, and its order while any of them
 * is on; then its resources unless folded. */
static void group_rows(UiLayout *layout, ZoneRows *rows, GatherGroup group)
{
    Ui *ui = layout->ui;
    const Columns *columns = &rows->columns;
    const GatherZone *zone = gather_zone_get(&rows->app->game.gather_zones, rows->index);
    const GatherGroupDef *def = gather_group_def(group);
    Subject subject = {GATHER_RESOURCE_COUNT, group};
    bool folded = rows->app->gather_group_folded[group];
    if (layout->draw &&
        ui_disclosure(ui, (SDL_FRect){layout->x, layout->y, columns->icon, layout->button_height},
                      !folded)) {
        rows->action = (Action){ACTION_FOLD, subject, 0};
    }
    const char *name = tr(def->name_key);
    UiCheck enabled = check_of(gather_zone_enabled_mask(zone), def->members);
    if (layout->draw && ui_checkbox(ui, layout->x + columns->icon + GAP, layout->y,
                                    ui_checkbox_width(ui, name), enabled, name)) {
        rows->action = (Action){ACTION_TOGGLE, subject, 0};
    }
    long available = 0;
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        if (def->members & GATHER_RESOURCE_BIT(r)) {
            available += rows->available[r];
        }
    }
    char count[TEXT_SIZE];
    available_text(count, sizeof(count), available);
    ui_layout_measure(layout, columns->ripe);
    if (layout->draw) {
        ui_label_dim(ui, count, layout->x + columns->available, layout->y + layout->text_offset);
    }
    GatherResourceMask options = gather_ripe_options() & def->members;
    if (options != 0 &&
        ripe_button(layout, layout->x + columns->ripe, columns->icon,
                    check_of(gather_zone_ripe_mask(zone), options), false)) {
        rows->action = (Action){ACTION_TOGGLE_RIPE, subject, 0};
    }
    if (enabled != UI_UNCHECKED) {
        OrderView view = group_view(rows, &zone->groups[group], group);
        order_controls(layout, columns, subject, &view, &rows->action);
    }
    layout->y += layout->row;
    for (int r = 0; !folded && r < GATHER_RESOURCE_COUNT; r++) {
        if (def->members & GATHER_RESOURCE_BIT(r)) {
            resource_rows(layout, rows, (GatherResource)r, INDENT);
        }
    }
}

/* Priority: 1 is the highest; -/+ and a hint. */
static void priority_row(UiLayout *layout, const GatherZone *zone, Action *action)
{
    Ui *ui = layout->ui;
    char number[16], priority[TEXT_SIZE];
    tr_integer(number, sizeof(number), zone->priority);
    TR_FORMAT(priority, sizeof(priority), tr("gather_zone.priority"), number);
    const char *hint = tr("gather_zone.priority_hint");
    /* Symbols, not words: the same in every language. */
    const char *minus = "−", *plus = "+";
    float small = SDL_max(ui_button_width(ui, minus), ui_button_width(ui, plus));
    float label_width = ui_text_width(ui, priority);
    ui_layout_measure(layout, label_width + GAP + 2.0f * small + 2.0f + GAP +
                                  ui_text_width(ui, hint));
    float x = layout->x + label_width + GAP;
    if (ui_layout_button_sized(layout, x, small, minus)) {
        *action = (Action){ACTION_ADD_PRIORITY, WHOLE_ZONE, -1};
    }
    if (ui_layout_button_sized(layout, x + small + 2.0f, small, plus)) {
        *action = (Action){ACTION_ADD_PRIORITY, WHOLE_ZONE, 1};
    }
    if (layout->draw) {
        ui_label(ui, priority, layout->x, layout->y + layout->text_offset);
        ui_label_dim(ui, hint, x + 2.0f * small + 2.0f + GAP, layout->y + layout->text_offset);
    }
    layout->y += layout->row;
}

/* Where the columns start: after the widest name (groups and grass at the
 * left, the resources of groups indented) and the widest count. */
static void measure_columns(Ui *ui, const UiLayout *layout, const long *available,
                            Columns *columns)
{
    columns->icon = layout->button_height;
    float names = 0.0f;
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        const char *name = tr(gather_resource_def((GatherResource)r)->name_key);
        float indent = gather_resource_group((GatherResource)r) != GATHER_GROUP_NONE ? INDENT : 0;
        names = SDL_max(names, indent + ui_checkbox_width(ui, name));
    }
    long group_available[GATHER_GROUP_COUNT] = {0};
    for (int g = 0; g < GATHER_GROUP_COUNT; g++) {
        const GatherGroupDef *def = gather_group_def((GatherGroup)g);
        names = SDL_max(names, ui_checkbox_width(ui, tr(def->name_key)));
        for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
            if (def->members & GATHER_RESOURCE_BIT(r)) {
                group_available[g] += available[r];
            }
        }
    }
    float counts = 0.0f;
    char count[TEXT_SIZE];
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        counts = SDL_max(counts, ui_text_width(ui, available_text(count, sizeof(count),
                                                                  available[r])));
    }
    for (int g = 0; g < GATHER_GROUP_COUNT; g++) {
        counts = SDL_max(counts, ui_text_width(ui, available_text(count, sizeof(count),
                                                                  group_available[g])));
    }
    for (int m = 0; m < GATHER_MODE_COUNT; m++) {
        columns->modes[m] = tr(MODE_KEYS[m]);
    }
    for (int u = 0; u < GATHER_UNIT_COUNT; u++) {
        columns->units[u] = tr(UNIT_KEYS[u]);
    }
    columns->mode_width = ui_dropdown_width(ui, columns->modes, GATHER_MODE_COUNT);
    columns->unit_width = ui_dropdown_width(ui, columns->units, GATHER_UNIT_COUNT);
    columns->small = SDL_max(ui_button_width(ui, "−"), ui_button_width(ui, "+"));
    char widest[16];
    tr_integer(widest, sizeof(widest), GATHER_TARGET_MAX);
    columns->number = SDL_ceilf(ui_button_width(ui, widest));

    columns->available = columns->icon + GAP + SDL_ceilf(names) + GAP;
    columns->ripe = columns->available + SDL_ceilf(counts) + GAP;
    columns->mode = columns->ripe + columns->icon + GAP;
    columns->target = columns->mode + columns->mode_width + GAP;
    columns->unit = columns->target + 2.0f * columns->small + columns->number + 4.0f + GAP;
    columns->progress = columns->unit + columns->unit_width + GAP;
}

static void zone_rows(UiLayout *layout, void *userdata)
{
    ZoneRows *rows = userdata;
    Ui *ui = layout->ui;
    const GatherZone *zone = gather_zone_get(&rows->app->game.gather_zones, rows->index);
    char number[16], tiles[TEXT_SIZE];
    tr_integer(number, sizeof(number), zone->cell_count);
    TR_FORMAT(tiles, sizeof(tiles), tr_plural("gather_zone.tiles", zone->cell_count), number);
    if (ui_layout_label_with_button(layout, tiles, tr("gather_zone.delete"))) {
        rows->action = (Action){ACTION_DELETE, WHOLE_ZONE, 0};
    }
    priority_row(layout, zone, &rows->action);
    ui_layout_label(layout, tr("gather_zone.hint"), 0.0f, true);
    ui_layout_label(layout, tr("gather_zone.hint_group"), 0.0f, true);

    measure_columns(ui, layout, rows->available, &rows->columns);
    /* The whole zone's full harvest option heads its column. */
    if (ripe_button(layout, layout->x + rows->columns.ripe, rows->columns.icon,
                    check_of(gather_zone_ripe_mask(zone), gather_ripe_options()), true)) {
        rows->action = (Action){ACTION_TOGGLE_RIPE, WHOLE_ZONE, 0};
    }
    layout->y += layout->row;
    for (size_t i = 0; i < SDL_arraysize(ENTRIES); i++) {
        if (ENTRIES[i].is_group) {
            group_rows(layout, rows, (GatherGroup)ENTRIES[i].value);
        } else {
            resource_rows(layout, rows, (GatherResource)ENTRIES[i].value, 0.0f);
        }
    }
}

static void apply(App *app, int index, const Action *action)
{
    GatherZones *zones = &app->game.gather_zones;
    const GatherZone *zone = gather_zone_get(zones, index);
    if (zone == NULL) {
        return;
    }
    Subject subject = action->subject;
    bool is_resource = (unsigned)subject.resource < GATHER_RESOURCE_COUNT;
    bool is_group = !is_resource && (unsigned)subject.group < GATHER_GROUP_COUNT;
    GatherResourceMask members = members_of(subject);
    switch (action->type) {
    case ACTION_DELETE:
        gather_zone_delete(zones, index);
        app->selected_gather_zone = GATHER_ZONE_NONE;
        break;
    case ACTION_TOGGLE: {
        /* Partly on: all on. */
        bool on = check_of(gather_zone_enabled_mask(zone), members) != UI_CHECKED;
        if (is_resource) {
            gather_zone_set_enabled(zones, index, subject.resource, on);
        } else if (is_group) {
            gather_zone_set_group_enabled(zones, index, subject.group, on);
        }
        break;
    }
    case ACTION_TOGGLE_RIPE: {
        GatherResourceMask options = gather_ripe_options() & members;
        bool on = check_of(gather_zone_ripe_mask(zone), options) != UI_CHECKED;
        if (is_resource) {
            gather_zone_set_ripe_only(zones, index, subject.resource, on);
        } else if (is_group) {
            gather_zone_set_group_ripe_only(zones, index, subject.group, on);
        } else {
            gather_zone_set_all_ripe_only(zones, index, on);
        }
        break;
    }
    case ACTION_SET_MODE:
        if (is_resource) {
            gather_zone_set_mode(zones, index, subject.resource, (GatherMode)action->value);
        } else if (is_group) {
            gather_zone_set_group_mode(zones, index, subject.group, (GatherMode)action->value);
        }
        break;
    case ACTION_SET_UNIT:
        if (is_resource) {
            gather_zone_set_unit(zones, index, subject.resource, (GatherUnit)action->value);
        } else if (is_group) {
            gather_zone_set_group_unit(zones, index, subject.group, (GatherUnit)action->value);
        }
        break;
    case ACTION_ADD_TARGET:
        if (is_resource) {
            gather_zone_set_target(zones, index, subject.resource,
                                   zone->orders[subject.resource].target + action->value);
        } else if (is_group) {
            gather_zone_set_group_target(zones, index, subject.group,
                                         zone->groups[subject.group].target + action->value);
        }
        break;
    case ACTION_FOLD:
        if (is_group) {
            app->gather_group_folded[subject.group] = !app->gather_group_folded[subject.group];
        }
        break;
    case ACTION_ADD_PRIORITY:
        gather_zone_set_priority(zones, index, zone->priority + action->value);
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
    ZoneRows rows = {.app = app,
                     .index = index,
                     .daily_food = game_daily_food(&app->game),
                     .action = {ACTION_NONE, WHOLE_ZONE, 0}};
    gather_zone_available(&app->game.gather_zones, &app->game.map, index, rows.available);
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
