#include "game/inspector.h"

#include "core/i18n.h"
#include "core/work.h"
#include "game/animal_panel.h"
#include "game/colonist_bar.h"
#include "game/layout.h"
#include "game/render.h"
#include "game/station_panel.h"
#include "game/stockpile_panel.h"
#include "game/gather_zone_panel.h"
#include "game/tile_panel.h"
#include "game/hud.h"

#define BOX_MIN_ROWS 6

InspectorKind inspector_kind(const App *app)
{
    if (app->build_menu_open || app->show_work_panel || app->show_log) {
        return INSPECT_NONE; /* A main menu or window takes its place. */
    }
    if (app->selected_station != WORKSTATION_NONE) {
        return INSPECT_STATION;
    }
    if (app->selected_gather_zone != GATHER_ZONE_NONE) {
        return INSPECT_GATHER_ZONE;
    }
    if (app->selected_stockpile != STOCKPILE_NONE) {
        return INSPECT_STOCKPILE;
    }
    if (app->tile_selected) {
        return INSPECT_TILE;
    }
    if (!entity_id_is_none(app->selected_animal)) {
        return INSPECT_ANIMAL;
    }
    if (app->selected_count == 1) {
        return INSPECT_COLONIST;
    }
    return app->selected_count > 1 ? INSPECT_COLONISTS : INSPECT_NONE;
}

/* How high the docked windows may reach: under the colonist bar. */
static float dock_top(const App *app)
{
    return colonist_bar_bottom(app) + app->ui.theme.margin;
}

static float tab_row_height(const App *app)
{
    ScreenLayout layout;
    layout_screen(&app->ui, &layout);
    return layout.button_height + 2.0f * app->ui.theme.gap;
}

typedef struct BoxRows {
    UiRowsFn rows;
    void *userdata;
} BoxRows;

/* The summary, at least BOX_MIN_ROWS rows high: the box keeps its size. */
static void box_rows(UiLayout *layout, void *userdata)
{
    const BoxRows *box = userdata;
    float start = layout->y;
    box->rows(layout, box->userdata);
    layout->y = SDL_max(layout->y, start + (float)BOX_MIN_ROWS * layout->row);
}

bool inspector_box(App *app, InspectorKind kind, const char *title, UiRowsFn rows, void *userdata)
{
    if (app->inspector_kind != (int)kind) {
        app->inspector_kind = (int)kind;
        app->inspector_tab = -1;
    }
    app->inspector_tab_count = 0;
    /* The box is as wide as its row of tabs (drawn last frame). */
    float tabs_width = app->inspector_tabs_width;
    app->inspector_tabs_width = 0.0f;
    ScreenLayout layout;
    layout_screen(&app->ui, &layout);
    UiDock dock = {0.0f, layout.bar_top, dock_top(app) + tab_row_height(app),
                   SDL_max(app->ui.theme.inspector_width, tabs_width), 0.0f};
    BoxRows box = {rows, userdata};
    bool open = true;
    ui_layout_docked(&app->ui, APP_WINDOW_INSPECTOR, title, &dock, box_rows, &box, &open);
    return open;
}

int inspector_tabs(App *app, const char *const *labels, int count)
{
    Ui *ui = &app->ui;
    app->inspector_tab_count = count;
    if (app->inspector_tab >= count) {
        app->inspector_tab = -1;
    }
    if (count == 0) {
        return -1;
    }
    SDL_FRect box = ui_window_rect(ui, APP_WINDOW_INSPECTOR);
    float gap = ui->theme.gap, height = tab_row_height(app);
    float width = gap;
    for (int i = 0; i < count; i++) {
        width += SDL_ceilf(ui_button_width(ui, labels[i])) + gap;
    }
    app->inspector_tabs_width = width;
    SDL_FRect row = {box.x, box.y - height, SDL_max(width, box.w), height};
    ui_panel(ui, row);
    float x = row.x + gap;
    for (int i = 0; i < count; i++) {
        SDL_FRect rect = {x, row.y + gap, SDL_ceilf(ui_button_width(ui, labels[i])),
                          height - 2.0f * gap};
        if (ui_toggle_button(ui, rect, labels[i], app->inspector_tab == i)) {
            app->inspector_tab = app->inspector_tab == i ? -1 : i;
        }
        x += rect.w + gap;
    }
    return app->inspector_tab;
}

void inspector_tab_panel(App *app, UiRowsFn rows, void *userdata)
{
    SDL_FRect box = ui_window_rect(&app->ui, APP_WINDOW_INSPECTOR);
    float bottom = box.y - tab_row_height(app);
    /* As tall as the screen allows: the windows docked at its right are too. */
    UiDock dock = {box.x, bottom, dock_top(app), box.w, bottom - dock_top(app)};
    bool open = true;
    ui_layout_docked(&app->ui, APP_WINDOW_INSPECTOR_TAB, NULL, &dock, rows, userdata, &open);
    if (!open) {
        app->inspector_tab = -1;
    }
}

void inspector_side_window(App *app, int id, int after, const char *title, UiRowsFn rows,
                           void *userdata, bool *open)
{
    SDL_FRect previous = ui_window_rect(&app->ui, after);
    float bottom = previous.y + previous.h;
    UiDock dock = {previous.x + previous.w + app->ui.theme.gap, bottom, dock_top(app), 0.0f,
                   bottom - dock_top(app)};
    ui_layout_docked(&app->ui, id, title, &dock, rows, userdata, open);
}

/* An action button of the menu right of the box: its button clicked, or 0. */
static int action_click(App *app, int slot, bool lit, SDL_FRect *button, SDL_FRect *icon,
                        const char *const *lines, int count)
{
    Ui *ui = &app->ui;
    SDL_FRect box = ui_window_rect(ui, APP_WINDOW_INSPECTOR);
    float size = ui->theme.icon_size, gap = ui->theme.gap;
    SDL_FRect rect = {box.x + box.w + gap + (float)slot * (size + 2.0f * gap) + gap,
                      box.y + box.h - size - gap, size, size};
    ui_panel(ui, (SDL_FRect){rect.x - gap, rect.y - gap, size + 2.0f * gap, size + 2.0f * gap});
    int clicked = ui_icon_button_click(ui, rect, lit);
    float inset = SDL_roundf(size * 0.12f);
    *button = rect;
    *icon =
        (SDL_FRect){rect.x + inset, rect.y + inset, rect.w - 2.0f * inset, rect.h - 2.0f * inset};
    if (ui_hovered(ui, rect)) {
        ui_tooltip(ui, lines, count);
    }
    return clicked;
}

bool inspector_action(App *app, int slot, bool lit, SDL_FRect *icon, const char *const *lines,
                      int count)
{
    SDL_FRect button;
    return action_click(app, slot, lit, &button, icon, lines, count) == SDL_BUTTON_LEFT;
}

bool inspector_priority_action(App *app, int slot, int priority, int *changed)
{
    char title[128], number[16];
    tr_integer(number, sizeof(number), priority);
    TR_FORMAT(title, sizeof(title), tr("inspector.priority"), number);
    const char *lines[3] = {title, tr("inspector.priority.description"),
                            tr("inspector.priority.controls")};
    SDL_FRect button, icon;
    int clicked = action_click(app, slot, false, &button, &icon, lines, 3);
    ui_centered_label(&app->ui, button, number);
    /* Left: higher, so done sooner (a smaller number); right: lower. */
    int wanted = priority;
    if (clicked == SDL_BUTTON_LEFT) {
        wanted = job_priority_clamped(priority - 1);
    } else if (clicked == SDL_BUTTON_RIGHT) {
        wanted = job_priority_clamped(priority + 1);
    }
    *changed = wanted;
    return wanted != priority;
}

bool inspector_forbid_action(App *app, int slot, bool forbidden, const char *forbid_hint)
{
    const char *lines[2] = {
        tr(forbidden ? "forbid.allow" : "forbid.forbid"),
        forbidden ? tr("inspector.allow.description")
                  : (forbid_hint != NULL ? forbid_hint : tr("inspector.forbid.description"))};
    SDL_FRect icon;
    bool clicked = inspector_action(app, slot, forbidden, &icon, lines, 2);
    render_tool_icon(app->renderer, icon, forbidden ? RENDER_ICON_ALLOW : RENDER_ICON_FORBID);
    return clicked;
}

void inspector_move_action(App *app, int slot)
{
    const char *lines[2] = {tr("inspector.move"), tr("inspector.move.description")};
    SDL_FRect icon;
    bool lit = app->tool == APP_TOOL_MOVE;
    if (inspector_action(app, slot, lit, &icon, lines, 2)) {
        app_toggle_tool(app, APP_TOOL_MOVE, 0);
    }
    render_tool_icon(app->renderer, icon, RENDER_ICON_MOVE);
}

void inspector_carry_action(App *app, int slot)
{
    const char *lines[2] = {tr("inspector.carry"), tr("inspector.carry.description")};
    SDL_FRect icon;
    bool lit = app->tool == APP_TOOL_CARRY;
    if (inspector_action(app, slot, lit, &icon, lines, 2)) {
        app_toggle_tool(app, APP_TOOL_CARRY, 0);
    }
    render_tool_icon(app->renderer, icon, RENDER_ICON_CARRY);
}

void inspector_draft_action(App *app, int slot)
{
    bool lit = app_selection_drafted(app);
    char shortcut[128];
    TR_FORMAT(shortcut, sizeof(shortcut), tr("menu.shortcut"), app_key_name(APP_KEY_DRAFT));
    const char *lines[3] = {
        tr(lit ? "inspector.undraft" : "inspector.draft"),
        tr(lit ? "inspector.undraft.description" : "inspector.draft.description"), shortcut};
    SDL_FRect icon;
    if (inspector_action(app, slot, lit, &icon, lines, 3)) {
        app_toggle_draft(app);
    }
    render_tool_icon(app->renderer, icon, RENDER_ICON_DRAFT);
}

void inspector_follow_action(App *app, int slot, FollowKind kind, EntityId id)
{
    bool lit = app_is_following(app, kind, id);
    char shortcut[128];
    TR_FORMAT(shortcut, sizeof(shortcut), tr("menu.shortcut"), app_key_name(APP_KEY_FOLLOW));
    const char *lines[3] = {tr(lit ? "inspector.follow.stop" : "inspector.follow"),
                            tr("inspector.follow.description"), shortcut};
    SDL_FRect icon;
    if (inspector_action(app, slot, lit, &icon, lines, 3)) {
        app_toggle_follow(app, kind, id);
    }
    render_tool_icon(app->renderer, icon, RENDER_ICON_FOLLOW);
}

bool inspector_build_like_action(App *app, int slot)
{
    AppTool tool;
    int type;
    if (!app_build_like_target(app, &tool, &type)) {
        return false;
    }
    char shortcut[128];
    TR_FORMAT(shortcut, sizeof(shortcut), tr("menu.shortcut"), app_key_name(APP_KEY_BUILD_LIKE));
    const char *lines[3] = {tr("inspector.build_like"), tr("inspector.build_like.description"),
                            shortcut};
    SDL_FRect icon;
    if (inspector_action(app, slot, false, &icon, lines, 3)) {
        app_build_like_selection(app);
    }
    switch (tool) {
    case APP_TOOL_BUILD: render_workstation_icon(app->renderer, icon, (WorkstationType)type); break;
    case APP_TOOL_STRUCTURE: render_structure_icon(app->renderer, icon, (StructureType)type); break;
    case APP_TOOL_STOCKPILE: render_tool_icon(app->renderer, icon, RENDER_ICON_STOCKPILE); break;
    default: render_tool_icon(app->renderer, icon, RENDER_ICON_GATHER_ZONE); break;
    }
    return true;
}

/* "Shortcut: X" for what APP_KEY_CANCEL does on the selection. */
static void cancel_shortcut(char *out, size_t size)
{
    TR_FORMAT(out, size, tr("menu.shortcut"), app_key_name(APP_KEY_CANCEL));
}

bool inspector_order_action(App *app, int slot, RenderIcon icon, const char *name,
                            const char *description, bool ordered, const char *unavailable,
                            bool shortcut)
{
    bool veiled = unavailable != NULL && !ordered;
    char key[128];
    cancel_shortcut(key, sizeof(key));
    const char *lines[3] = {ordered ? tr("inspector.order.cancel") : name, description, NULL};
    int count = 2;
    if (veiled) {
        lines[count++] = unavailable;
    } else if (shortcut) {
        lines[count++] = key;
    }
    SDL_FRect rect;
    bool clicked = inspector_action(app, slot, ordered, &rect, lines, count);
    render_tool_icon(app->renderer, rect, icon);
    if (veiled) {
        ui_fill(&app->ui, rect, app->ui.theme.colors[THEME_DISABLED_VEIL]);
    }
    return clicked && !veiled;
}

void inspector_zone_edit_actions(App *app, int slot, AppTool tool, RenderIcon extend_icon,
                                 RenderIcon shrink_icon)
{
    static const int MODES[2] = {APP_ZONE_EXTEND, APP_ZONE_SHRINK};
    static const char *const NAMES[2] = {N_("inspector.zone.extend"), N_("inspector.zone.shrink")};
    static const char *const DESCRIPTIONS[2] = {N_("inspector.zone.extend.description"),
                                                N_("inspector.zone.shrink.description")};
    RenderIcon icons[2] = {extend_icon, shrink_icon};
    for (int i = 0; i < 2; i++) {
        bool lit = app_tool_active(app, tool, MODES[i]);
        const char *lines[2] = {tr(NAMES[i]), tr(DESCRIPTIONS[i])};
        SDL_FRect icon;
        if (inspector_action(app, slot + i, lit, &icon, lines, 2)) {
            app_toggle_tool(app, tool, MODES[i]);
        }
        render_tool_icon(app->renderer, icon, icons[i]);
    }
}

bool inspector_delete_action(App *app, int slot, const char *description, bool shortcut)
{
    char key[128];
    cancel_shortcut(key, sizeof(key));
    const char *lines[3] = {tr("inspector.delete"), description, key};
    SDL_FRect icon;
    bool clicked = inspector_action(app, slot, false, &icon, lines, shortcut ? 3 : 2);
    render_tool_icon(app->renderer, icon, RENDER_ICON_CANCEL);
    return clicked;
}

void inspector_draw(App *app)
{
    switch (inspector_kind(app)) {
    case INSPECT_COLONIST:
    case INSPECT_COLONISTS: hud_inspect_colonists(app); break;
    case INSPECT_STATION: station_panel_draw(app); break;
    case INSPECT_STOCKPILE: stockpile_panel_draw(app); break;
    case INSPECT_GATHER_ZONE: gather_zone_panel_draw(app); break;
    case INSPECT_TILE: tile_panel_draw(app); break;
    case INSPECT_ANIMAL: animal_panel_draw(app); break;
    case INSPECT_NONE: break;
    }
    if (app->inspector_tab >= app->inspector_tab_count) {
        app->inspector_tab = -1;
    }
}
