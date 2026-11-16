#include "game/menu_bar.h"

#include "core/i18n.h"
#include "game/render.h"

#define BAR_MARGIN 8.0f
#define BAR_PADDING 4.0f
#define BAR_GAP 4.0f /* Between buttons, and between rows. */
#define ICON_SIZE 48.0f
#define ICON_INSET 6.0f
#define TEXT_SIZE 256
#define MAX_ROW_BUTTONS 12

/* Zones and orders of the build menu (workstations come from their table). */
typedef struct MenuTool {
    BuildCategory category;
    AppTool tool;
    int type; /* See app_toggle_tool(). */
    RenderIcon icon;
    const char *name_key;
    const char *description_key;
    SDL_Scancode key; /* Its shortcut (SDL_SCANCODE_UNKNOWN: none). */
} MenuTool;

static const MenuTool TOOLS[] = {
    {BUILD_CATEGORY_ZONES, APP_TOOL_GATHER, APP_ZONE_ADD, RENDER_ICON_GATHER_ZONE,
     N_("tool.gather_zone"), N_("tool.gather_zone.description"), APP_KEY_GATHER},
    {BUILD_CATEGORY_ZONES, APP_TOOL_GATHER, APP_ZONE_REMOVE, RENDER_ICON_GATHER_ZONE_REMOVE,
     N_("tool.gather_remove"), N_("tool.zone_remove.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ZONES, APP_TOOL_STOCKPILE, APP_ZONE_ADD, RENDER_ICON_STOCKPILE,
     N_("tool.stockpile"), N_("tool.stockpile.description"), APP_KEY_STOCKPILE},
    {BUILD_CATEGORY_ZONES, APP_TOOL_STOCKPILE, APP_ZONE_REMOVE, RENDER_ICON_STOCKPILE_REMOVE,
     N_("tool.stockpile_remove"), N_("tool.zone_remove.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_DESIGNATE, DESIGNATION_MINE, RENDER_ICON_MINE,
     N_("designation.mine"), N_("tool.mine.description"), APP_KEY_MINE},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_DESIGNATE, DESIGNATION_DIG, RENDER_ICON_DIG,
     N_("designation.dig"), N_("tool.dig.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_DESIGNATE, DESIGNATION_DIG_RAMP, RENDER_ICON_DIG_RAMP,
     N_("designation.dig_ramp"), N_("tool.dig_ramp.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_DESIGNATE, DESIGNATION_DIG_DOWN, RENDER_ICON_DIG_DOWN,
     N_("designation.dig_down"), N_("tool.dig_down.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_DESIGNATE, DESIGNATION_CHOP, RENDER_ICON_CHOP,
     N_("designation.chop"), N_("tool.chop.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_DESIGNATE, DESIGNATION_DECONSTRUCT, RENDER_ICON_DECONSTRUCT,
     N_("designation.deconstruct"), N_("tool.deconstruct.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_FORBID, APP_FORBID, RENDER_ICON_FORBID, N_("tool.forbid"),
     N_("tool.forbid.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_FORBID, APP_ALLOW, RENDER_ICON_ALLOW, N_("tool.allow"),
     N_("tool.allow.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_DESIGNATE, DESIGNATION_NONE, RENDER_ICON_CANCEL,
     N_("tool.cancel"), N_("tool.cancel.description"), APP_KEY_CANCEL},
};

static const char *const CATEGORY_KEYS[BUILD_CATEGORY_COUNT] = {
    [BUILD_CATEGORY_NONE] = NULL,
    [BUILD_CATEGORY_STATIONS] = N_("menu.stations"),
    [BUILD_CATEGORY_STRUCTURES] = N_("menu.structures"),
    [BUILD_CATEGORY_ZONES] = N_("menu.zones"),
    [BUILD_CATEGORY_ORDERS] = N_("menu.orders"),
};

static float button_height(const Ui *ui)
{
    return ui_row_height(ui) + 6.0f;
}

/* A panel holding a row of text buttons at (x, y); returns the index of the
 * button clicked (-1: none) and sets *hovered to the one under the mouse. */
static int text_row(Ui *ui, float x, float y, const char *const *labels, const bool *active,
                    int count, int *hovered)
{
    float height = button_height(ui);
    float width = 2.0f * BAR_PADDING + BAR_GAP * (float)(count - 1);
    for (int i = 0; i < count; i++) {
        width += SDL_ceilf(ui_button_width(ui, labels[i])) + 8.0f;
    }
    ui_panel(ui, (SDL_FRect){x, y, width, height + 2.0f * BAR_PADDING});
    int clicked = -1;
    *hovered = -1;
    float bx = x + BAR_PADDING;
    for (int i = 0; i < count; i++) {
        SDL_FRect rect = {bx, y + BAR_PADDING, SDL_ceilf(ui_button_width(ui, labels[i])) + 8.0f,
                          height};
        if (ui_toggle_button(ui, rect, labels[i], active[i])) {
            clicked = i;
        }
        if (ui_hovered(ui, rect)) {
            *hovered = i;
        }
        bx += rect.w + BAR_GAP;
    }
    return clicked;
}

/* "Shortcut: R". */
static void shortcut_line(char *out, size_t size, SDL_Scancode key)
{
    TR_FORMAT(out, size, tr("menu.shortcut"), app_key_name(key));
}

static void station_tooltip(App *app, WorkstationType type)
{
    const WorkstationDef *def = workstation_def(type);
    char cost[TEXT_SIZE];
    menu_bar_materials_cost(cost, sizeof(cost), def->materials);
    const char *lines[4] = {tr(def->name_key), tr(def->description_key), cost,
                            tr("hud.tool.unavailable")};
    ui_tooltip(&app->ui, lines, game_workstation_buildable(&app->game, type) ? 3 : 4);
}

void menu_bar_materials_cost(char *out, size_t size,
                             const StructureMaterial materials[STRUCTURE_MATERIAL_SLOTS])
{
    char parts[STRUCTURE_MATERIAL_SLOTS][TEXT_SIZE];
    int count = 0;
    float weight = 0.0f;
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        StructureMaterial material = materials[slot];
        if (material.type == ITEM_NONE) {
            continue;
        }
        char number[16];
        tr_integer(number, sizeof(number), material.count);
        TR_FORMAT(parts[count++], TEXT_SIZE, tr("recipe.ingredient"),
                  tr(item_def(material.type)->name_key), number);
        weight += item_stack_weight(material.type, material.count);
    }
    if (count == 0) {
        SDL_strlcpy(out, tr("menu.no_cost"), size);
        return;
    }
    char list[TEXT_SIZE], number[16], kilograms[32];
    if (count == 2) {
        TR_FORMAT(list, sizeof(list), tr("recipe.both"), parts[0], parts[1]);
    } else {
        SDL_strlcpy(list, count == 1 ? parts[0] : "", sizeof(list));
    }
    int decimals = SDL_fabsf(weight - SDL_roundf(weight)) > 0.05f ? 1 : 0;
    tr_decimal(number, sizeof(number), decimals ? weight : SDL_roundf(weight), decimals);
    TR_FORMAT(kilograms, sizeof(kilograms), tr("unit.kg"), number);
    TR_FORMAT(out, size, tr("menu.structure_cost"), list, kilograms);
}

void menu_bar_structure_cost(char *out, size_t size, StructureType type)
{
    menu_bar_materials_cost(out, size, structure_def(type)->materials);
}

static void structure_tooltip(App *app, StructureType type)
{
    const StructureDef *def = structure_def(type);
    char cost[TEXT_SIZE];
    menu_bar_structure_cost(cost, sizeof(cost), type);
    const char *lines[4] = {tr(def->name_key), tr(def->description_key), cost,
                            tr("hud.tool.unavailable")};
    ui_tooltip(&app->ui, lines, game_structure_buildable(&app->game, type) ? 3 : 4);
}

static void tool_tooltip(App *app, const MenuTool *tool)
{
    const char *lines[4] = {tr(tool->name_key), tr(tool->description_key)};
    int count = 2;
    char shortcut[TEXT_SIZE];
    if (tool->key != SDL_SCANCODE_UNKNOWN) {
        shortcut_line(shortcut, sizeof(shortcut), tool->key);
        lines[count++] = shortcut;
    }
    if (tool->tool == APP_TOOL_DESIGNATE && tool->type != DESIGNATION_NONE &&
        !game_designation_available(&app->game, (DesignationType)tool->type)) {
        lines[count++] = tr("hud.tool.unavailable");
    }
    ui_tooltip(&app->ui, lines, count);
}

/* One icon button per thing to place in the chosen category. */
static void icon_row(App *app, float x, float y)
{
    Ui *ui = &app->ui;
    int count = 0;
    const MenuTool *tools[MAX_ROW_BUTTONS];
    if (app->build_category == BUILD_CATEGORY_STATIONS) {
        count = WORKSTATION_TYPE_COUNT;
    } else if (app->build_category == BUILD_CATEGORY_STRUCTURES) {
        count = STRUCTURE_TYPE_COUNT - 1; /* STRUCTURE_NONE is not built. */
    } else {
        for (size_t i = 0; i < SDL_arraysize(TOOLS) && count < MAX_ROW_BUTTONS; i++) {
            if (TOOLS[i].category == app->build_category) {
                tools[count++] = &TOOLS[i];
            }
        }
    }
    if (count == 0) {
        return;
    }
    float width = 2.0f * BAR_PADDING + (float)count * ICON_SIZE + (float)(count - 1) * BAR_GAP;
    ui_panel(ui, (SDL_FRect){x, y, width, ICON_SIZE + 2.0f * BAR_PADDING});
    for (int i = 0; i < count; i++) {
        SDL_FRect rect = {x + BAR_PADDING + (float)i * (ICON_SIZE + BAR_GAP), y + BAR_PADDING,
                          ICON_SIZE, ICON_SIZE};
        SDL_FRect inner = {rect.x + ICON_INSET, rect.y + ICON_INSET, rect.w - 2.0f * ICON_INSET,
                           rect.h - 2.0f * ICON_INSET};
        if (app->build_category == BUILD_CATEGORY_STATIONS) {
            WorkstationType type = (WorkstationType)i;
            if (ui_icon_button(ui, rect, app_tool_active(app, APP_TOOL_BUILD, i))) {
                app_toggle_tool(app, APP_TOOL_BUILD, i);
            }
            render_workstation_icon(app->renderer, inner, type);
            if (ui_hovered(ui, rect)) {
                station_tooltip(app, type);
            }
        } else if (app->build_category == BUILD_CATEGORY_STRUCTURES) {
            StructureType type = (StructureType)(i + 1);
            if (ui_icon_button(ui, rect, app_tool_active(app, APP_TOOL_STRUCTURE, (int)type))) {
                app_toggle_tool(app, APP_TOOL_STRUCTURE, (int)type);
            }
            render_structure_icon(app->renderer, inner, type);
            if (ui_hovered(ui, rect)) {
                structure_tooltip(app, type);
            }
        } else {
            const MenuTool *tool = tools[i];
            if (ui_icon_button(ui, rect, app_tool_active(app, tool->tool, tool->type))) {
                app_toggle_tool(app, tool->tool, tool->type);
            }
            render_tool_icon(app->renderer, inner, tool->icon);
            if (ui_hovered(ui, rect)) {
                tool_tooltip(app, tool);
            }
        }
    }
}

float menu_bar_top(const App *app)
{
    float row_height = button_height(&app->ui) + 2.0f * BAR_PADDING;
    float y = app->camera.viewport_height - BAR_MARGIN - row_height;
    if (app->build_menu_open) {
        y -= row_height + BAR_GAP;
        if (app->build_category != BUILD_CATEGORY_NONE) {
            y -= ICON_SIZE + 2.0f * BAR_PADDING + BAR_GAP;
        }
    }
    return y;
}

void menu_bar_draw(App *app)
{
    Ui *ui = &app->ui;
    float row_height = button_height(ui) + 2.0f * BAR_PADDING;
    float y = app->camera.viewport_height - BAR_MARGIN - row_height;

    /* The bar. */
    const char *labels[3] = {tr("menu.build"), tr("menu.work"), "?"};
    bool active[3] = {app->build_menu_open, app->show_work_panel, app->show_help};
    int hovered;
    switch (text_row(ui, BAR_MARGIN, y, labels, active, 3, &hovered)) {
    case 0:
        app->build_menu_open = !app->build_menu_open;
        break;
    case 1: app->show_work_panel = !app->show_work_panel; break;
    case 2: app->show_help = !app->show_help; break;
    default: break;
    }
    char shortcut[TEXT_SIZE];
    if (hovered == 1) {
        shortcut_line(shortcut, sizeof(shortcut), APP_KEY_WORK);
        const char *lines[3] = {tr("work_panel.title"), tr("menu.work.description"), shortcut};
        ui_tooltip(ui, lines, 3);
    } else if (hovered == 2) {
        shortcut_line(shortcut, sizeof(shortcut), APP_KEY_HELP);
        const char *lines[2] = {tr("help.title"), shortcut};
        ui_tooltip(ui, lines, 2);
    }
    if (!app->build_menu_open) {
        return;
    }

    /* Categories. */
    y -= row_height + BAR_GAP;
    const char *categories[BUILD_CATEGORY_COUNT - 1];
    bool open[BUILD_CATEGORY_COUNT - 1];
    for (int i = 0; i < BUILD_CATEGORY_COUNT - 1; i++) {
        categories[i] = tr(CATEGORY_KEYS[i + 1]);
        open[i] = (int)app->build_category == i + 1;
    }
    int clicked = text_row(ui, BAR_MARGIN, y, categories, open, BUILD_CATEGORY_COUNT - 1,
                           &hovered);
    if (clicked >= 0) {
        BuildCategory category = (BuildCategory)(clicked + 1);
        app->build_category = app->build_category == category ? BUILD_CATEGORY_NONE : category;
    }
    if (app->build_category == BUILD_CATEGORY_NONE) {
        return;
    }

    /* What the category places. */
    y -= ICON_SIZE + 2.0f * BAR_PADDING + BAR_GAP;
    icon_row(app, BAR_MARGIN, y);
}
