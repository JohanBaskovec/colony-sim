#include "game/build_menu.h"

#include "core/i18n.h"
#include "game/layout.h"
#include "game/render.h"

#define TEXT_SIZE 256
#define MAX_ROW_BUTTONS 16
#define CARD_LINES 10
#define CARD_WRAPPED_LINES 32

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
    {BUILD_CATEGORY_ORDERS, APP_TOOL_DESIGNATE, DESIGNATION_DIG_RAMP, RENDER_ICON_DIG_RAMP,
     N_("designation.dig_ramp"), N_("tool.dig_ramp.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_DESIGNATE, DESIGNATION_DIG_DOWN, RENDER_ICON_DIG_DOWN,
     N_("designation.dig_down"), N_("tool.dig_down.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_DESIGNATE, DESIGNATION_CHOP, RENDER_ICON_CHOP,
     N_("designation.chop"), N_("tool.chop.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_DESIGNATE, DESIGNATION_HARVEST, RENDER_ICON_HARVEST,
     N_("designation.harvest"), N_("tool.harvest.description"), SDL_SCANCODE_UNKNOWN},
    {BUILD_CATEGORY_ORDERS, APP_TOOL_DESIGNATE, DESIGNATION_UPROOT, RENDER_ICON_UPROOT,
     N_("designation.uproot"), N_("tool.uproot.description"), SDL_SCANCODE_UNKNOWN},
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

/* "Shortcut: R". */
static void shortcut_line(char *out, size_t size, SDL_Scancode key)
{
    TR_FORMAT(out, size, tr("menu.shortcut"), app_key_name(key));
}

void build_menu_materials_cost(char *out, size_t size,
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

/* ---- The card ------------------------------------------------------------ */

typedef enum CardStyle { CARD_TITLE, CARD_TEXT, CARD_DIM, CARD_WARNING } CardStyle;

typedef struct Card {
    char text[CARD_LINES][TEXT_SIZE];
    CardStyle style[CARD_LINES];
    int count;
} Card;

/* The buffer of a new line (the last one is reused when the card is full). */
static char *card_add(Card *card, CardStyle style)
{
    if (card->count < CARD_LINES) {
        card->count++;
    }
    card->style[card->count - 1] = style;
    card->text[card->count - 1][0] = '\0';
    return card->text[card->count - 1];
}

static void card_add_text(Card *card, CardStyle style, const char *text)
{
    SDL_strlcpy(card_add(card, style), text, TEXT_SIZE);
}

static const MenuTool *find_tool(AppTool tool, int type)
{
    for (size_t i = 0; i < SDL_arraysize(TOOLS); i++) {
        if (TOOLS[i].tool == tool && TOOLS[i].type == type) {
            return &TOOLS[i];
        }
    }
    return NULL;
}

static void station_card(App *app, WorkstationType type, Card *card)
{
    const WorkstationDef *def = workstation_def(type);
    card_add_text(card, CARD_TITLE, tr(def->name_key));
    build_menu_materials_cost(card_add(card, CARD_DIM), TEXT_SIZE, def->materials);
    card_add_text(card, CARD_TEXT, tr(def->description_key));
    TR_FORMAT(card_add(card, CARD_DIM), TEXT_SIZE, tr("hud.tool.build_help"),
              app_key_name(APP_KEY_BUILD));
    if (!game_workstation_buildable(&app->game, type)) {
        card_add_text(card, CARD_WARNING, tr("hud.tool.unavailable"));
    }
}

static void structure_card(App *app, StructureType type, Card *card)
{
    const StructureDef *def = structure_def(type);
    card_add_text(card, CARD_TITLE, tr(def->name_key));
    build_menu_materials_cost(card_add(card, CARD_DIM), TEXT_SIZE, def->materials);
    card_add_text(card, CARD_TEXT, tr(def->description_key));
    if (structure_is_roof(type)) {
        char range[16];
        tr_integer(range, sizeof(range), ROOF_SUPPORT_RANGE);
        TR_FORMAT(card_add(card, CARD_DIM), TEXT_SIZE, tr("hud.tool.roof_support"), range,
                  app_key_name(APP_KEY_LEVEL_UP));
    }
    card_add_text(card, CARD_DIM,
                  tr(def->drawn_as_outline ? "hud.tool.outline_help" : "hud.tool.structure_help"));
    if (def->length > 1) {
        TR_FORMAT(card_add(card, CARD_DIM), TEXT_SIZE, tr("hud.tool.rotate_help"),
                  app_key_name(APP_KEY_ROTATE));
    }
    if (!game_structure_buildable(&app->game, type)) {
        card_add_text(card, CARD_WARNING, tr("hud.tool.unavailable"));
    }
}

/* The debug tool (game/debug_panel.h, items_panel.h, animals_panel.h): what a click does. */
static void debug_card(App *app, int mode, Card *card)
{
    char count[16];
    if (mode == APP_DEBUG_PLANT_TREE) {
        card_add_text(card, CARD_TITLE, tr("hud.tool.plant_tree"));
    } else if (mode == APP_DEBUG_GROW_TREE) {
        card_add_text(card, CARD_TITLE, tr("hud.tool.grow_tree"));
    } else if (mode == APP_DEBUG_SPAWN_ANIMAL) {
        TR_FORMAT(card_add(card, CARD_TITLE), TEXT_SIZE, tr("hud.tool.spawn_animal"),
                  tr(animal_name_key(app->debug_animal, app->debug_animal_sex)));
    } else if (mode == APP_DEBUG_TELEPORT) {
        tr_integer(count, sizeof(count), app->selected_count);
        TR_FORMAT(card_add(card, CARD_TITLE), TEXT_SIZE,
                  tr_plural("hud.tool.teleport", app->selected_count), count);
    } else {
        tr_integer(count, sizeof(count),
                   app->debug_item_max ? item_stack_limit(app->debug_item) : 1);
        TR_FORMAT(card_add(card, CARD_TITLE), TEXT_SIZE, tr("hud.tool.spawn"),
                  tr(item_def(app->debug_item)->name_key), count);
    }
}

/* Zones and orders. */
static void tool_card(App *app, AppTool tool, int type, Card *card)
{
    const MenuTool *menu = find_tool(tool, type);
    if (menu == NULL) {
        return;
    }
    card_add_text(card, CARD_TITLE, tr(menu->name_key));
    card_add_text(card, CARD_TEXT, tr(menu->description_key));
    if (tool == APP_TOOL_STOCKPILE || tool == APP_TOOL_GATHER) {
        card_add_text(
            card, CARD_DIM,
            tr(type == APP_ZONE_REMOVE ? "hud.tool.zone_remove_help" : "hud.tool.stockpile_help"));
    } else if (tool == APP_TOOL_FORBID) {
        card_add_text(card, CARD_DIM,
                      tr(type == APP_ALLOW ? "hud.tool.allow_help" : "hud.tool.forbid_help"));
    }
    if (menu->key != SDL_SCANCODE_UNKNOWN) {
        shortcut_line(card_add(card, CARD_DIM), TEXT_SIZE, menu->key);
    }
    if (tool == APP_TOOL_DESIGNATE && type != DESIGNATION_NONE &&
        !game_designation_available(&app->game, (DesignationType)type)) {
        card_add_text(card, CARD_WARNING, tr("hud.tool.unavailable"));
    }
}

static void card_for(App *app, AppTool tool, int type, Card *card)
{
    card->count = 0;
    switch (tool) {
    case APP_TOOL_BUILD: station_card(app, (WorkstationType)type, card); break;
    case APP_TOOL_STRUCTURE: structure_card(app, (StructureType)type, card); break;
    case APP_TOOL_DEBUG: debug_card(app, type, card); break;
    case APP_TOOL_MOVE:
        card_add_text(card, CARD_TITLE, tr("inspector.move"));
        card_add_text(card, CARD_DIM, tr("hud.tool.move_help"));
        break;
    case APP_TOOL_MOVE_ITEM:
        card_add_text(card, CARD_TITLE, tr("tile_panel.move_items"));
        card_add_text(card, CARD_DIM, tr("hud.tool.move_items_help"));
        break;
    case APP_TOOL_NONE: break;
    default: tool_card(app, tool, type, card); break;
    }
}

/* The type of the tool in use (see app_toggle_tool()). */
static int active_type(const App *app)
{
    switch (app->tool) {
    case APP_TOOL_DESIGNATE: return (int)app->tool_type;
    case APP_TOOL_BUILD: return (int)app->build_type;
    case APP_TOOL_STRUCTURE: return (int)app->structure_type;
    case APP_TOOL_STOCKPILE:
    case APP_TOOL_GATHER: return app->zone_remove ? APP_ZONE_REMOVE : APP_ZONE_ADD;
    case APP_TOOL_DEBUG: return app->debug_mode;
    case APP_TOOL_FORBID: return app->forbid_allows ? APP_ALLOW : APP_FORBID;
    default: return 0;
    }
}

/* The card shown: the icon under the mouse (last frame), else the tool in use. */
static bool current_card(App *app, Card *card)
{
    if (app->build_menu_open && app->build_hover_tool != APP_TOOL_NONE) {
        card_for(app, app->build_hover_tool, app->build_hover_type, card);
    } else {
        card_for(app, app->tool, active_type(app), card);
    }
    return card->count > 0;
}

typedef struct WrappedCard {
    char lines[CARD_WRAPPED_LINES][UI_WRAP_LINE_SIZE];
    CardStyle style[CARD_WRAPPED_LINES];
    int count;
    float height;
} WrappedCard;

static void wrap_card(Ui *ui, const Card *card, WrappedCard *out)
{
    const Theme *theme = &ui->theme;
    float width = theme->card_width - 2.0f * theme->padding;
    out->count = 0;
    static const char SEPARATOR[] = " \u00b7 "; /* "Click: one · Drag: many": a line each. */
    for (int i = 0; i < card->count && out->count < CARD_WRAPPED_LINES; i++) {
        int first = out->count;
        const char *part = card->text[i];
        while (part != NULL && out->count < CARD_WRAPPED_LINES) {
            const char *end = SDL_strstr(part, SEPARATOR);
            char piece[TEXT_SIZE];
            size_t length = end != NULL ? (size_t)(end - part) : SDL_strlen(part);
            SDL_strlcpy(piece, part, SDL_min(length + 1, sizeof(piece)));
            out->count += ui_wrap_text(ui, piece, width, out->lines + out->count,
                                       CARD_WRAPPED_LINES - out->count);
            part = end != NULL ? end + SDL_strlen(SEPARATOR) : NULL;
        }
        for (int j = first; j < out->count; j++) {
            out->style[j] = card->style[i];
        }
    }
    float line = (float)text_line_height(ui->text);
    out->height = line * (float)out->count + 2.0f * theme->padding;
}

/* The card with its bottom at `bottom`, at the left edge. */
static void draw_card(Ui *ui, const WrappedCard *card, float bottom)
{
    const Theme *theme = &ui->theme;
    SDL_FRect rect = {0.0f, bottom - card->height, theme->card_width, card->height};
    ui_panel_colored(ui, rect, THEME_WINDOW);
    float line = (float)text_line_height(ui->text);
    static const ThemeColor COLORS[] = {
        [CARD_TITLE] = THEME_TEXT,
        [CARD_TEXT] = THEME_TEXT,
        [CARD_DIM] = THEME_TEXT_DIM,
        [CARD_WARNING] = THEME_WARNING,
    };
    for (int i = 0; i < card->count; i++) {
        ui_label_colored(ui, card->lines[i], rect.x + theme->padding,
                         rect.y + theme->padding + line * (float)i, COLORS[card->style[i]]);
    }
}

/* ---- Categories and icons ------------------------------------------------- */

/* A thing to place: a tool and its type (see app_toggle_tool()). */
typedef struct MenuEntry {
    AppTool tool;
    int type;
    const MenuTool *menu; /* Zones and orders, NULL otherwise. */
} MenuEntry;

static int category_entries(BuildCategory category, MenuEntry out[MAX_ROW_BUTTONS])
{
    int count = 0;
    if (category == BUILD_CATEGORY_STATIONS) {
        for (int i = 0; i < WORKSTATION_TYPE_COUNT && count < MAX_ROW_BUTTONS; i++) {
            out[count++] = (MenuEntry){APP_TOOL_BUILD, i, NULL};
        }
    } else if (category == BUILD_CATEGORY_STRUCTURES) {
        /* STRUCTURE_NONE is not built. */
        for (int i = 1; i < STRUCTURE_TYPE_COUNT && count < MAX_ROW_BUTTONS; i++) {
            out[count++] = (MenuEntry){APP_TOOL_STRUCTURE, i, NULL};
        }
    } else {
        for (size_t i = 0; i < SDL_arraysize(TOOLS) && count < MAX_ROW_BUTTONS; i++) {
            if (TOOLS[i].category == category) {
                out[count++] = (MenuEntry){TOOLS[i].tool, TOOLS[i].type, &TOOLS[i]};
            }
        }
    }
    return count;
}

static bool entry_available(App *app, const MenuEntry *entry)
{
    switch (entry->tool) {
    case APP_TOOL_BUILD:
        return game_workstation_buildable(&app->game, (WorkstationType)entry->type);
    case APP_TOOL_STRUCTURE:
        return game_structure_buildable(&app->game, (StructureType)entry->type);
    case APP_TOOL_DESIGNATE:
        return entry->type == DESIGNATION_NONE ||
               game_designation_available(&app->game, (DesignationType)entry->type);
    default: return true;
    }
}

static void entry_icon(App *app, const MenuEntry *entry, SDL_FRect rect)
{
    switch (entry->tool) {
    case APP_TOOL_BUILD:
        render_workstation_icon(app->renderer, rect, (WorkstationType)entry->type);
        break;
    case APP_TOOL_STRUCTURE:
        render_structure_icon(app->renderer, rect, (StructureType)entry->type);
        break;
    default: render_tool_icon(app->renderer, rect, entry->menu->icon); break;
    }
}

/* Height of the category column. */
static float column_height(const Theme *theme, const ScreenLayout *layout)
{
    int count = BUILD_CATEGORY_COUNT - 1;
    return (float)count * layout->button_height + (float)(count + 1) * theme->gap;
}

/* The column of categories at the left edge, its bottom on the bar. */
static void draw_categories(App *app, const ScreenLayout *layout)
{
    Ui *ui = &app->ui;
    const Theme *theme = &ui->theme;
    float height = column_height(theme, layout);
    SDL_FRect column = {0.0f, layout->bar_top - height, theme->card_width, height};
    ui_panel(ui, column);
    for (int i = 1; i < BUILD_CATEGORY_COUNT; i++) {
        SDL_FRect rect = {column.x + theme->gap,
                          column.y + theme->gap +
                              (float)(i - 1) * (layout->button_height + theme->gap),
                          column.w - 2.0f * theme->gap, layout->button_height};
        BuildCategory category = (BuildCategory)i;
        if (ui_toggle_button(ui, rect, tr(CATEGORY_KEYS[i]), app->build_category == category)) {
            app->build_category = app->build_category == category ? BUILD_CATEGORY_NONE : category;
        }
    }
}

/* The icons of the open category, right of the column, along the bar. */
static void draw_icons(App *app, const ScreenLayout *layout)
{
    Ui *ui = &app->ui;
    const Theme *theme = &ui->theme;
    MenuEntry entries[MAX_ROW_BUTTONS];
    int count = category_entries(app->build_category, entries);
    if (count == 0) {
        return;
    }
    float icon = theme->icon_size, gap = theme->gap;
    float inset = SDL_roundf(icon * 0.12f);
    SDL_FRect row = {theme->card_width + gap, layout->bar_top - icon - 2.0f * gap,
                     (float)count * (icon + gap) + gap, icon + 2.0f * gap};
    ui_panel(ui, row);
    for (int i = 0; i < count; i++) {
        const MenuEntry *entry = &entries[i];
        SDL_FRect rect = {row.x + gap + (float)i * (icon + gap), row.y + gap, icon, icon};
        if (ui_icon_button(ui, rect, app_tool_active(app, entry->tool, entry->type))) {
            app_toggle_tool(app, entry->tool, entry->type);
        }
        entry_icon(app, entry,
                   (SDL_FRect){rect.x + inset, rect.y + inset, rect.w - 2.0f * inset,
                               rect.h - 2.0f * inset});
        if (!entry_available(app, entry)) {
            ui_fill(ui, rect, theme->colors[THEME_DISABLED_VEIL]);
        }
        if (entry->menu != NULL && entry->menu->key != SDL_SCANCODE_UNKNOWN) {
            const char *key = app_key_name(entry->menu->key);
            float kx = rect.x + gap, ky = rect.y + 1.0f;
            ui_label_colored(ui, key, kx + 1.0f, ky + 1.0f, THEME_TEXT_SHADOW);
            ui_label(ui, key, kx, ky);
        }
        if (ui_hovered(ui, rect)) {
            app->build_hover_tool = entry->tool;
            app->build_hover_type = entry->type;
        }
    }
}

float build_menu_top(App *app)
{
    ScreenLayout layout;
    layout_screen(&app->ui, &layout);
    float top = layout.bar_top;
    if (app->build_menu_open) {
        top -= column_height(&app->ui.theme, &layout);
    }
    Card card;
    if (current_card(app, &card)) {
        WrappedCard wrapped;
        wrap_card(&app->ui, &card, &wrapped);
        top -= wrapped.height;
    }
    return top;
}

void build_menu_draw(App *app)
{
    ScreenLayout layout;
    layout_screen(&app->ui, &layout);
    float card_bottom = layout.bar_top;
    app->build_hover_tool = APP_TOOL_NONE;
    if (app->build_menu_open) {
        draw_categories(app, &layout);
        draw_icons(app, &layout);
        card_bottom -= column_height(&app->ui.theme, &layout);
    }
    Card card;
    if (current_card(app, &card)) {
        WrappedCard wrapped;
        wrap_card(&app->ui, &card, &wrapped);
        draw_card(&app->ui, &wrapped, card_bottom);
    }
}
