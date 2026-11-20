#include "game/app.h"

#include "core/i18n.h"
#include "core/log.h"
#include "core/pathfinding.h"
#include "core/scenario.h"
#include "core/tree.h"
#include "core/util.h"
#include "game/bottom_bar.h"
#include "game/hud.h"
#include "game/inspector.h"
#include "game/render.h"
#include "game/stockpile_panel.h"
#include "game/tile_panel.h"
#include "game/title_screen.h"
#include "game/work_panel.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_WINDOW_WIDTH 1280
#define APP_WINDOW_HEIGHT 720
#define APP_SETTINGS_FOLDER "colony-sim" /* In the user's data folder. */
#define APP_SETTINGS_FILE "settings.cfg"
#define APP_INITIAL_ZOOM 48.0f
/* Zoom (pixels per tile) a test scenario starts at: wide enough for a chase. */
#define APP_SCENARIO_ZOOM 32.0f
#define APP_PAN_SPEED 900.0f /* Pixels per second. */
#define APP_PAN_FAST_FACTOR 2.5f
#define APP_ZOOM_STEP 1.15f
/* Levels below the one shown that show through open space (deeper ones are
 * too dark to see). */
/* Height of a colonist's body above its position, in tiles (for clicks). */
#define APP_PICK_BODY_OFFSET 0.7f
/* The same for animals, lower on their legs. */
#define APP_PICK_ANIMAL_OFFSET 0.45f
#define APP_DRAG_THRESHOLD 5.0f /* Pixels before a click becomes a box selection. */

static const SDL_Color HOVER_COLOR = {255, 255, 255, 200};

const char *app_key_name(SDL_Scancode key)
{
    /* Keys without a character: SDL names them in English. */
    switch (key) {
    case SDL_SCANCODE_ESCAPE: return tr("key.escape");
    case SDL_SCANCODE_SPACE: return tr("key.space");
    case SDL_SCANCODE_PAGEUP: return tr("key.page_up");
    case SDL_SCANCODE_PAGEDOWN: return tr("key.page_down");
    default: return SDL_GetKeyName(SDL_GetKeyFromScancode(key, SDL_KMOD_NONE, false));
    }
}

bool app_tool_active(const App *app, AppTool tool, int type)
{
    if (app->tool != tool) {
        return false;
    }
    switch (tool) {
    case APP_TOOL_DESIGNATE: return (int)app->tool_type == type;
    case APP_TOOL_BUILD: return (int)app->build_type == type;
    case APP_TOOL_STRUCTURE: return (int)app->structure_type == type;
    case APP_TOOL_STOCKPILE:
    case APP_TOOL_GATHER: return app->zone_mode == type;
    case APP_TOOL_DEBUG: return app->debug_mode == type;
    case APP_TOOL_FORBID: return app->forbid_allows == (type == APP_ALLOW);
    default: return true;
    }
}

void app_toggle_tool(App *app, AppTool tool, int type)
{
    if (app_tool_active(app, tool, type)) {
        app->tool = APP_TOOL_NONE;
    } else {
        app->tool = tool;
        if (tool == APP_TOOL_DESIGNATE) {
            app->tool_type = (DesignationType)type;
        } else if (tool == APP_TOOL_BUILD) {
            app->build_type = (WorkstationType)type;
        } else if (tool == APP_TOOL_STRUCTURE) {
            app->structure_type = (StructureType)type;
        } else if (tool == APP_TOOL_STOCKPILE || tool == APP_TOOL_GATHER) {
            app->zone_mode = type;
        } else if (tool == APP_TOOL_DEBUG) {
            app->debug_mode = type;
        } else if (tool == APP_TOOL_FORBID) {
            app->forbid_allows = type == APP_ALLOW;
        }
    }
    app->left_button_down = false;
    app->box_selecting = false;
}

static void toggle_tool(App *app, AppTool tool, DesignationType type)
{
    app_toggle_tool(app, tool, (int)type);
}

bool app_cancel_selection(App *app)
{
    if (app->tool != APP_TOOL_NONE) {
        return false; /* The key turns the tool off. */
    }
    switch (inspector_kind(app)) {
    case INSPECT_GATHER_ZONE:
        gather_zone_delete(&app->game.gather_zones, app->selected_gather_zone);
        app->selected_gather_zone = GATHER_ZONE_NONE;
        return true;
    case INSPECT_STOCKPILE:
        stockpile_delete(&app->game.stockpiles, app->selected_stockpile);
        app->selected_stockpile = STOCKPILE_NONE;
        return true;
    case INSPECT_TILE: return tile_panel_cancel(app);
    default: return false;
    }
}

/* One overlay view at a time: turns `view` on or off, the others off. */
static void toggle_view(App *app, bool *view)
{
    bool on = !*view;
    app->show_temperature = false;
    app->show_light = false;
    app->show_moisture = false;
    *view = on;
}

void app_toggle_temperature_view(App *app)
{
    toggle_view(app, &app->show_temperature);
}

void app_toggle_light_view(App *app)
{
    toggle_view(app, &app->show_light);
}

void app_toggle_moisture_view(App *app)
{
    toggle_view(app, &app->show_moisture);
}

/* Colonists, a station or a zone: one kind of selection at a time. */
static bool has_selection(const App *app)
{
    return app->selected_count > 0 || app->selected_stockpile != STOCKPILE_NONE ||
           app->selected_station != WORKSTATION_NONE ||
           app->selected_gather_zone != GATHER_ZONE_NONE || app->tile_selected ||
           !entity_id_is_none(app->selected_animal);
}

static void clear_selection(App *app)
{
    app->selected_count = 0;
    app->selected_stockpile = STOCKPILE_NONE;
    app->selected_station = WORKSTATION_NONE;
    app->selected_gather_zone = GATHER_ZONE_NONE;
    app->tile_selected = false;
    app->selected_animal = ENTITY_NONE;
}

/* The construction site on a tile. */
static const ConstructionSite *site_at(const App *app, int x, int y, int z)
{
    const Constructions *sites = &app->game.constructions;
    return construction_get(sites, construction_at(sites, x, y, z));
}

/* What a click on a tile may select there, in the order repeated clicks go
 * through them. Grass comes after the zones (it grows nearly everywhere). */
typedef enum ClickTarget {
    CLICK_STATION,
    CLICK_STRUCTURE,
    CLICK_ROOF,
    CLICK_SITE,
    CLICK_ITEMS,
    CLICK_PLANT, /* Any plant or tree but grass. */
    CLICK_STOCKPILE,
    CLICK_GATHER_ZONE,
    CLICK_GRASS,
    CLICK_TARGET_COUNT
} ClickTarget;

PlantType app_plant_type_at(const App *app, int x, int y, int z)
{
    const Map *map = game_view_map(&app->game);
    if (!map_in_bounds(map, x, y, z)) {
        return PLANT_NONE;
    }
    PlantType type = (PlantType)map->plants[map_index(map, x, y, z)].type;
    TreeRef tree;
    if (type == PLANT_NONE && tree_at(map, x, y, z, &tree)) {
        type = (PlantType)tree_plant(map, &tree)->type;
    }
    return type;
}

bool app_thing_at(const App *app, TileThing thing, int x, int y, int z)
{
    const Map *map = game_view_map(&app->game);
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    switch (thing) {
    case THING_STRUCTURE: return map_structure(map, x, y, z) != STRUCTURE_NONE;
    case THING_ROOF: return map_roof(map, x, y, z) != STRUCTURE_NONE;
    case THING_SITE: return site_at(app, x, y, z) != NULL;
    case THING_ITEMS: /* A stack, or items lying where they fell. */
        return items_first(game_view_items(&app->game), map_index(map, x, y, z)) != NULL;
    case THING_PLANT: return app_plant_type_at(app, x, y, z) != PLANT_NONE;
    case THING_ITEM: { /* The item selected, if it lies there still. */
        ItemPick pick = app->selected_item;
        return game_pick_item(&app->game, &pick) != NULL && pick.x == x && pick.y == y &&
               pick.z == z;
    }
    case THING_ITEM_BOX:
        return app->selected_box_item_count > 0; /* (See prune_tile_selection().) */
    case THING_PLANT_BOX: return app->selected_plant_count > 0;
    case THING_COUNT: break;
    }
    return false;
}

/* The thing a click target selects on a tile (THING_COUNT: not a thing). */
static TileThing click_thing(ClickTarget target)
{
    switch (target) {
    case CLICK_STRUCTURE: return THING_STRUCTURE;
    case CLICK_ROOF: return THING_ROOF;
    case CLICK_SITE: return THING_SITE;
    case CLICK_ITEMS: return THING_ITEMS;
    case CLICK_PLANT:
    case CLICK_GRASS: return THING_PLANT;
    default: return THING_COUNT;
    }
}

static bool click_target_at(const App *app, ClickTarget target, int x, int y, int z)
{
    const Game *game = &app->game;
    switch (target) {
    case CLICK_STATION: return workstation_at(&game->workstations, x, y, z) != WORKSTATION_NONE;
    case CLICK_STRUCTURE:
    case CLICK_ROOF:
    case CLICK_SITE:
    case CLICK_ITEMS: return app_thing_at(app, click_thing(target), x, y, z);
    case CLICK_PLANT: {
        PlantType plant = app_plant_type_at(app, x, y, z);
        return plant != PLANT_NONE && plant != PLANT_GRASS;
    }
    case CLICK_STOCKPILE: return stockpile_at(&game->stockpiles, x, y, z) != STOCKPILE_NONE;
    case CLICK_GATHER_ZONE: return gather_zone_at(&game->gather_zones, x, y, z) != GATHER_ZONE_NONE;
    case CLICK_GRASS: return app_plant_type_at(app, x, y, z) == PLANT_GRASS;
    case CLICK_TARGET_COUNT: break;
    }
    return false;
}

/* What is selected now, if it is on the tile (CLICK_TARGET_COUNT otherwise). */
static ClickTarget click_target_selected(const App *app, int x, int y, int z)
{
    const Game *game = &app->game;
    if (app->selected_station != WORKSTATION_NONE &&
        workstation_at(&game->workstations, x, y, z) == app->selected_station) {
        return CLICK_STATION;
    }
    if (app->tile_selected && app->selected_tile_x == x && app->selected_tile_y == y &&
        app->selected_tile_z == z) {
        switch (app->selected_thing) {
        case THING_STRUCTURE: return CLICK_STRUCTURE;
        case THING_ROOF: return CLICK_ROOF;
        case THING_SITE: return CLICK_SITE;
        case THING_ITEMS: return CLICK_ITEMS;
        case THING_PLANT:
            return app_plant_type_at(app, x, y, z) == PLANT_GRASS ? CLICK_GRASS : CLICK_PLANT;
        case THING_ITEM: /* Not things of the tile's: see select_at_point(). */
        case THING_ITEM_BOX:
        case THING_PLANT_BOX:
        case THING_COUNT: break;
        }
    }
    if (app->selected_stockpile != STOCKPILE_NONE &&
        stockpile_at(&game->stockpiles, x, y, z) == app->selected_stockpile) {
        return CLICK_STOCKPILE;
    }
    if (app->selected_gather_zone != GATHER_ZONE_NONE &&
        gather_zone_at(&game->gather_zones, x, y, z) == app->selected_gather_zone) {
        return CLICK_GATHER_ZONE;
    }
    return CLICK_TARGET_COUNT;
}

/* Direction of the structure, or of the construction site, on a tile. */
static Direction structure_direction_at(const App *app, int x, int y, int z)
{
    if (map_structure(&app->game.map, x, y, z) != STRUCTURE_NONE) {
        return map_structure_direction(&app->game.map, x, y, z);
    }
    const Constructions *sites = &app->game.constructions;
    const ConstructionSite *site = construction_get(sites, construction_at(sites, x, y, z));
    return site != NULL ? (Direction)site->direction : DIRECTION_SOUTH;
}

static void select_thing(App *app, TileThing thing, int x, int y, int z)
{
    clear_selection(app);
    app->tile_selected = app_thing_at(app, thing, x, y, z);
    app->selected_thing = thing;
    app->selected_tile_x = x;
    app->selected_tile_y = y;
    app->selected_tile_z = z;
}

/* Selects one item lying on the ground. */
static void select_item(App *app, ItemPick pick)
{
    clear_selection(app);
    app->tile_selected = true;
    app->selected_thing = THING_ITEM;
    app->selected_item = pick;
    app->selected_tile_x = pick.x;
    app->selected_tile_y = pick.y;
    app->selected_tile_z = pick.z;
}

/* Selects the items inside a box. */
static void select_box_items(App *app, const ItemPick *picks, int count)
{
    clear_selection(app);
    for (int i = 0; i < count && i < APP_BOX_ITEMS_MAX; i++) {
        app->selected_box_items[i] = picks[i];
    }
    app->selected_box_item_count = count < APP_BOX_ITEMS_MAX ? count : APP_BOX_ITEMS_MAX;
    app->tile_selected = true;
    app->selected_thing = THING_ITEM_BOX;
    app->selected_tile_x = picks[0].x;
    app->selected_tile_y = picks[0].y;
    app->selected_tile_z = picks[0].z;
}

void app_select_item(App *app, ItemPick pick)
{
    select_item(app, pick);
}

void app_select_tile_items(App *app, int x, int y, int z)
{
    select_thing(app, THING_ITEMS, x, y, z);
}

/* The plant on a tile as the colony knows it, and the tile it grows on (a
 * tree's trunk and foliage tiles stand for its foot). */
static bool plant_foot(const App *app, int x, int y, int z, TilePos *foot, PlantType *type)
{
    const Map *map = game_view_map(&app->game);
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    TreeRef tree;
    if (tree_at(map, x, y, z, &tree)) {
        *foot = (TilePos){(int16_t)tree.x, (int16_t)tree.y, (int16_t)tree.z};
        *type = (PlantType)tree_plant(map, &tree)->type;
        return *type != PLANT_NONE;
    }
    *foot = (TilePos){(int16_t)x, (int16_t)y, (int16_t)z};
    *type = (PlantType)map->plants[map_index(map, x, y, z)].type;
    return *type != PLANT_NONE;
}

/* Selects the plants of one kind seen on the screen, on the level z seen. */
static bool select_plants_on_screen(App *app, PlantType type, int z)
{
    const Map *map = game_view_map(&app->game);
    TileRect view = camera_visible_tiles(&app->camera, map->width, map->height);
    static TilePos found[APP_BOX_PLANTS_MAX];
    int count = 0;
    for (int y = view.y0; y < view.y1; y++) {
        for (int x = view.x0; x < view.x1 && count < APP_BOX_PLANTS_MAX; x++) {
            if (map->plants[map_index(map, x, y, z)].type == type &&
                map_view_level(map, x, y, app->camera.level) == z) {
                found[count++] = (TilePos){(int16_t)x, (int16_t)y, (int16_t)z};
            }
        }
    }
    if (count == 0) {
        return false;
    }
    clear_selection(app);
    memcpy(app->selected_plants, found, (size_t)count * sizeof(found[0]));
    app->selected_plant_count = count;
    app->selected_plant_type = (int)type;
    app->tile_selected = true;
    app->selected_thing = THING_PLANT_BOX;
    app->selected_tile_x = found[0].x;
    app->selected_tile_y = found[0].y;
    app->selected_tile_z = found[0].z;
    return true;
}

/* A double click on an item or a plant: all those of the same kind on the
 * screen (as a box would select them). False when there is nothing selected
 * that has a kind. */
static bool select_same_on_screen(App *app)
{
    if (!app->tile_selected) {
        return false;
    }
    int x = app->selected_tile_x, y = app->selected_tile_y, z = app->selected_tile_z;
    const Map *map = game_view_map(&app->game);
    switch (app->selected_thing) {
    case THING_ITEM:
    case THING_ITEMS: {
        int type = ITEM_NONE;
        if (app->selected_thing == THING_ITEM) {
            ItemPick pick = app->selected_item;
            const Item *item = game_pick_item(&app->game, &pick);
            type = item != NULL ? item->type : ITEM_NONE;
            z = pick.z;
        } else {
            const Items *items = game_view_items(&app->game);
            type = items_get(items, x, y, z).type;
            const Item *first = items_first(items, map_index(map, x, y, z));
            if (type == ITEM_NONE && first != NULL) {
                type = first->type;
            }
        }
        TileRect view = camera_visible_tiles(&app->camera, map->width, map->height);
        ItemPick picks[APP_BOX_ITEMS_MAX];
        int count =
            game_items_of_type_in_rect(&app->game, (float)view.x0, (float)view.y0, (float)view.x1,
                                       (float)view.y1, z, (ItemType)type, picks, APP_BOX_ITEMS_MAX);
        if (count == 0) {
            return false;
        }
        if (count == 1) {
            select_item(app, picks[0]);
        } else {
            select_box_items(app, picks, count);
        }
        return true;
    }
    case THING_PLANT: {
        TilePos foot;
        PlantType type;
        return plant_foot(app, x, y, z, &foot, &type) && select_plants_on_screen(app, type, foot.z);
    }
    default: break;
    }
    return false;
}

/* Forgets the selected thing once it is gone (taken apart, picked up,
 * felled); the items selected are followed where they lie. */
static void prune_tile_selection(App *app)
{
    if (app->tile_selected && app->selected_thing == THING_ITEM_BOX) {
        int kept = 0;
        for (int i = 0; i < app->selected_box_item_count; i++) {
            ItemPick pick = app->selected_box_items[i];
            if (game_pick_item(&app->game, &pick) != NULL) {
                app->selected_box_items[kept++] = pick;
            }
        }
        app->selected_box_item_count = kept;
        app->tile_selected = kept > 0;
        if (kept > 0) {
            app->selected_tile_x = app->selected_box_items[0].x;
            app->selected_tile_y = app->selected_box_items[0].y;
            app->selected_tile_z = app->selected_box_items[0].z;
        }
        return;
    }
    if (app->tile_selected && app->selected_thing == THING_PLANT_BOX) {
        const Map *map = game_view_map(&app->game);
        int kept = 0;
        for (int i = 0; i < app->selected_plant_count; i++) {
            TilePos at = app->selected_plants[i];
            if (map_in_bounds(map, at.x, at.y, at.z) &&
                map->plants[map_index(map, at.x, at.y, at.z)].type == app->selected_plant_type) {
                app->selected_plants[kept++] = at;
            }
        }
        app->selected_plant_count = kept;
        app->tile_selected = kept > 0;
        if (kept > 0) {
            app->selected_tile_x = app->selected_plants[0].x;
            app->selected_tile_y = app->selected_plants[0].y;
            app->selected_tile_z = app->selected_plants[0].z;
        }
        return;
    }
    if (app->tile_selected && app->selected_thing == THING_ITEM) {
        ItemPick *pick = &app->selected_item;
        app->tile_selected = game_pick_item(&app->game, pick) != NULL;
        app->selected_tile_x = pick->x;
        app->selected_tile_y = pick->y;
        app->selected_tile_z = pick->z;
        return;
    }
    if (app->tile_selected && !app_thing_at(app, app->selected_thing, app->selected_tile_x,
                                            app->selected_tile_y, app->selected_tile_z)) {
        app->tile_selected = false;
    }
}

static void select_stockpile(App *app, int index)
{
    const Stockpile *stockpile = stockpile_get(&app->game.stockpiles, index);
    if (stockpile != NULL) {
        clear_selection(app);
    }
    app->selected_stockpile = stockpile != NULL ? index : STOCKPILE_NONE;
    app->selected_stockpile_number = stockpile != NULL ? stockpile->number : 0;
}

static void select_station(App *app, int index)
{
    const Workstation *station = workstation_get_const(&app->game.workstations, index);
    if (station != NULL) {
        clear_selection(app);
    }
    app->selected_station = station != NULL ? index : WORKSTATION_NONE;
    app->selected_station_number = station != NULL ? station->number : 0;
}

static void select_gather_zone(App *app, int index)
{
    const GatherZone *zone = gather_zone_get(&app->game.gather_zones, index);
    if (zone != NULL) {
        clear_selection(app);
    }
    app->selected_gather_zone = zone != NULL ? index : GATHER_ZONE_NONE;
    app->selected_gather_zone_number = zone != NULL ? zone->number : 0;
}

/* Forgets the selected animal once it is gone, or out of the colony's sight. */
static void prune_animal_selection(App *app)
{
    if (!entity_id_is_none(app->selected_animal) &&
        !game_animal_seen(&app->game, app->selected_animal)) {
        app->selected_animal = ENTITY_NONE;
    }
}

/* Forgets the selected gathering zone once it no longer exists. */
static void prune_gather_zone_selection(App *app)
{
    const GatherZone *zone = gather_zone_get(&app->game.gather_zones, app->selected_gather_zone);
    if (zone == NULL || zone->number != app->selected_gather_zone_number) {
        app->selected_gather_zone = GATHER_ZONE_NONE;
    }
}

/* Forgets the selected workstation once it no longer exists. */
static void prune_station_selection(App *app)
{
    const Workstation *station =
        workstation_get_const(&app->game.workstations, app->selected_station);
    if (station == NULL || station->number != app->selected_station_number) {
        app->selected_station = WORKSTATION_NONE;
    }
}

/* Extending or shrinking a zone ends with its selection (deleted, emptied,
 * something else selected). */
static void prune_zone_edit(App *app)
{
    bool edits = app->zone_mode == APP_ZONE_EXTEND || app->zone_mode == APP_ZONE_SHRINK;
    if (edits &&
        ((app->tool == APP_TOOL_STOCKPILE && app->selected_stockpile == STOCKPILE_NONE) ||
         (app->tool == APP_TOOL_GATHER && app->selected_gather_zone == GATHER_ZONE_NONE))) {
        app->tool = APP_TOOL_NONE;
    }
}

/* Forgets the selected stockpile once it no longer exists. */
static void prune_stockpile_selection(App *app)
{
    const Stockpile *stockpile = stockpile_get(&app->game.stockpiles, app->selected_stockpile);
    if (stockpile == NULL || stockpile->number != app->selected_stockpile_number) {
        app->selected_stockpile = STOCKPILE_NONE;
    }
}

/* Routes core log messages to SDL's logging (console, Android logcat...). */
static void sdl_log_sink(LogLevel level, const char *message, void *userdata)
{
    (void)userdata;
    SDL_LogPriority priority = SDL_LOG_PRIORITY_INFO;
    switch (level) {
    case LOG_LEVEL_DEBUG: priority = SDL_LOG_PRIORITY_DEBUG; break;
    case LOG_LEVEL_INFO: priority = SDL_LOG_PRIORITY_INFO; break;
    case LOG_LEVEL_WARN: priority = SDL_LOG_PRIORITY_WARN; break;
    case LOG_LEVEL_ERROR: priority = SDL_LOG_PRIORITY_ERROR; break;
    }
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, priority, "%s", message);
}

static bool file_exists(const char *path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

/* Assets are looked up next to the executable, then in the source tree (development). */
static bool resolve_assets_dir(char *out, size_t size)
{
    char probe[1200];
    const char *base = SDL_GetBasePath();
    if (base != NULL) {
        SDL_snprintf(out, size, "%sassets/", base);
        SDL_snprintf(probe, sizeof(probe), "%slang/en.lang", out);
        if (file_exists(probe)) {
            return true;
        }
    }
#ifdef COLONY_SOURCE_ASSETS_DIR
    SDL_snprintf(out, size, "%s/", COLONY_SOURCE_ASSETS_DIR);
    SDL_snprintf(probe, sizeof(probe), "%slang/en.lang", out);
    if (file_exists(probe)) {
        return true;
    }
#endif
    return false;
}

uint64_t app_random_seed(void)
{
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    return (uint64_t)now ^ SDL_GetPerformanceCounter();
}

static void update_window_title(App *app)
{
    SDL_SetWindowTitle(app->window, tr("game.title"));
}

/* The settings file: in the user's data folder (XDG_DATA_HOME on Linux). */
static void find_settings_path(App *app)
{
    app->settings_path[0] = '\0';
    char *folder = SDL_GetPrefPath("", APP_SETTINGS_FOLDER);
    if (folder == NULL) {
        LOG_WARN("No folder for the settings, they will not be kept: %s", SDL_GetError());
        return;
    }
    SDL_snprintf(app->settings_path, sizeof(app->settings_path), "%s%s", folder, APP_SETTINGS_FILE);
    SDL_free(folder);
}

static void load_settings(App *app)
{
    settings_default(&app->settings);
    if (app->settings_path[0] == '\0' || !file_exists(app->settings_path)) {
        return;
    }
    size_t length = 0;
    char *text = SDL_LoadFile(app->settings_path, &length);
    if (text == NULL) {
        LOG_WARN("Cannot read the settings '%s': %s", app->settings_path, SDL_GetError());
        return;
    }
    settings_parse(&app->settings, text, length);
    SDL_free(text);
}

static void save_settings(const App *app)
{
    if (app->settings_path[0] == '\0') {
        return;
    }
    char text[512];
    size_t length = settings_format(&app->settings, text, sizeof(text));
    if (length >= sizeof(text) || !SDL_SaveFile(app->settings_path, text, length)) {
        LOG_WARN("Cannot save the settings '%s': %s", app->settings_path, SDL_GetError());
    }
}

/* Remembers the language in use. */
static void language_changed(App *app)
{
    update_window_title(app);
    const LanguageInfo *info = localization_active_language(&app->localization);
    if (info != NULL) {
        SDL_strlcpy(app->settings.language, info->code, sizeof(app->settings.language));
        save_settings(app);
    }
}

void app_cycle_language(App *app)
{
    localization_cycle(&app->localization);
    language_changed(app);
}

void app_select_language(App *app, int index)
{
    if (localization_select(&app->localization, index)) {
        language_changed(app);
    }
}

int app_ui_scale(const App *app)
{
    return (int)SDL_lroundf(app->ui.theme.scale * 100.0f);
}

/* A new theme for this scale and palette, the font resized to it. */
static void apply_theme(App *app, int percent, SettingsPalette palette)
{
    Theme theme;
    theme_init(&theme, (float)percent / 100.0f, palette);
    if (theme.font_size != app->ui.theme.font_size &&
        !text_renderer_set_size(app->text, theme.font_size)) {
        return;
    }
    ui_set_theme(&app->ui, &theme);
}

void app_set_ui_scale(App *app, int percent)
{
    percent = SDL_clamp(percent, CLI_UI_SCALE_MIN, CLI_UI_SCALE_MAX);
    apply_theme(app, percent, app->ui.theme.palette);
    app->settings.ui_scale = percent;
    save_settings(app);
}

void app_set_palette(App *app, SettingsPalette palette)
{
    apply_theme(app, app_ui_scale(app), palette);
    app->settings.palette = app->ui.theme.palette;
    save_settings(app);
}

/* The test situation and the cheats asked for on the command line. */
static void apply_test_options(Game *game, const CliOptions *options)
{
    scenario_apply(game, (Scenario)options->scenario);
    game_set_instant_build(game, (options->debug & CLI_DEBUG_INSTANT_BUILD) != 0);
    game_set_free_crafts(game, (options->debug & CLI_DEBUG_FREE_CRAFTS) != 0);
    game_set_no_tools(game, (options->debug & CLI_DEBUG_NO_TOOLS) != 0);
    game_set_instant_deconstruct(game, (options->debug & CLI_DEBUG_INSTANT_DECONSTRUCT) != 0);
    game_set_instant_fell_and_mine(game, (options->debug & CLI_DEBUG_INSTANT_FELL_AND_MINE) != 0);
    /* The fog of war is on, unless everything is to be seen. */
    if ((options->debug & CLI_DEBUG_SEE_EVERYTHING) == 0 && !game_set_fog(game, true)) {
        LOG_ERROR("Cannot start the fog of war");
    }
    if (options->debug & CLI_DEBUG_VISION) {
        game_set_vision(game, true);
    }
    if (options->debug & CLI_DEBUG_LEARN_ALL) {
        game_debug_learn_all(game);
    }
    if (options->scenario != SCENARIO_NONE || options->debug != 0) {
        LOG_INFO("Scenario %d, debug flags 0x%x", options->scenario, options->debug);
    }
}

/* The view at the start of a game: on the first colonist (or the middle of
 * the map if there is none), on its level. */
static void view_start(App *app)
{
    const Map *map = &app->game.map;
    float start_x = (float)map->width * 0.5f;
    float start_y = (float)map->height * 0.5f;
    const Colonist *first = game_colonist_const(&app->game, game_colonist_id_at(&app->game, 0));
    if (first != NULL) {
        colonist_position(first, &start_x, &start_y);
    }
    camera_init(&app->camera, start_x, start_y, APP_INITIAL_ZOOM);
    app->camera.level =
        first != NULL ? first->z : map_ground_level(map, (int)start_x, (int)start_y);
}

/* Replaces the game by the world behind the title screen, and shows it. */
static bool open_title_screen(App *app)
{
    if (app->game_ready) {
        game_shutdown(&app->game);
        app->game_ready = false;
    }
    NewGameParams params;
    title_screen_backdrop_params(&params);
    if (!game_init(&app->game, &params)) {
        LOG_ERROR("Cannot make the world of the title screen");
        return false;
    }
    app->game_ready = true;
    title_screen_begin(app);
    return true;
}

bool app_init(App *app, const CliOptions *options)
{
    SDL_zerop(app);
    app->structure_direction = DIRECTION_SOUTH; /* Beds' heads at the top. */
    /* Zeroed is a valid item id (slot 0): these name no item. */
    app->debug_held = ITEM_ID_NONE;
    app->tuning = render_tuning_default();
    app->move_item_id = ITEM_ID_NONE;
    app->selected_item.id = ITEM_ID_NONE;
    log_set_sink(sdl_log_sink, NULL);
    SDL_SetAppMetadata("Thawmoor", COLONY_VERSION, "io.github.colony-sim");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    app->sdl_initialized = true;

    if (!TTF_Init()) {
        LOG_ERROR("TTF_Init failed: %s", SDL_GetError());
        return false;
    }
    app->ttf_initialized = true;

    if (!resolve_assets_dir(app->assets_dir, sizeof(app->assets_dir))) {
        LOG_ERROR("Cannot find the assets directory");
        return false;
    }

    /* The command line wins over the saved settings, for this session. */
    find_settings_path(app);
    load_settings(app);
    const char *language =
        options->language[0] != '\0' ? options->language : app->settings.language;
    if (!localization_init(&app->localization, app->assets_dir, language)) {
        return false;
    }
    app->localization_ready = true;

    if (!SDL_CreateWindowAndRenderer(tr("game.title"), APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT,
                                     SDL_WINDOW_RESIZABLE, &app->window, &app->renderer)) {
        LOG_ERROR("Cannot create the window: %s", SDL_GetError());
        return false;
    }
    app->bench_render_frames = options->bench_render_frames;
    bool vsync = !options->no_vsync && app->bench_render_frames == 0;
    SDL_SetRenderVSync(app->renderer, vsync ? 1 : 0);
    LOG_INFO("Renderer: %s", SDL_GetRendererName(app->renderer));
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    char font_path[1200];
    SDL_snprintf(font_path, sizeof(font_path), "%s%s", app->assets_dir, APP_FONT_FILE);
    Theme theme;
    int ui_scale = options->ui_scale > 0 ? options->ui_scale : app->settings.ui_scale;
    theme_init(&theme, (float)ui_scale / 100.0f, app->settings.palette);
    app->text = text_renderer_create(app->renderer, font_path, theme.font_size);
    if (app->text == NULL) {
        return false;
    }
    app->sprites = sprites_create(app->renderer, app->assets_dir);
    if (app->sprites == NULL) {
        return false;
    }

    ui_init(&app->ui, app->renderer, app->text, &theme);
    app->selected_stockpile = STOCKPILE_NONE;
    app->selected_station = WORKSTATION_NONE;
    app->selected_gather_zone = GATHER_ZONE_NONE;
    app->inspector_tab = -1;
    if (!cli_starts_game(options)) {
        /* No game asked for on the command line: the title screen. */
        if (!open_title_screen(app)) {
            return false;
        }
        app->running = true;
        return true;
    }
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = options->has_seed ? options->seed : app_random_seed();
    if (options->map_size > 0) {
        params.world.width = options->map_size;
        params.world.height = options->map_size;
    }
    if (options->colonist_count > 0) {
        params.colonist_count = options->colonist_count;
    }
    params.wildlife = options->has_wildlife
                          ? GAME_WILDLIFE_DENSITY * (float)options->wildlife / 100.0f
                          : GAME_WILDLIFE_DENSITY;
    if (!game_init(&app->game, &params)) {
        LOG_ERROR("World generation failed");
        return false;
    }
    app->game_ready = true;
    app->screen = APP_SCREEN_GAME;
    apply_test_options(&app->game, options);
    app->show_vision = (options->debug & CLI_DEBUG_VISION) != 0;
    app->character_editor = (options->debug & CLI_DEBUG_CHARACTER_EDITOR) != 0;
    LOG_INFO("World generated: seed %" SDL_PRIu64 ", %dx%d tiles, %u colonists, %u animals",
             params.world.seed, params.world.width, params.world.height,
             app->game.colonist_pool.count, app->game.animals.pool.count);
    view_start(app);
    app->running = true;
    return true;
}

void app_shutdown(App *app)
{
    if (app->game_ready) {
        game_shutdown(&app->game);
    }
    SDL_free(app->reach_mask);
    text_renderer_destroy(app->title_text);
    text_renderer_destroy(app->title_subtext);
    text_renderer_destroy(app->text);
    sprites_destroy(app->sprites);
    render_shutdown();
    if (app->renderer != NULL) {
        SDL_DestroyRenderer(app->renderer);
    }
    if (app->window != NULL) {
        SDL_DestroyWindow(app->window);
    }
    if (app->localization_ready) {
        localization_shutdown(&app->localization);
    }
    if (app->ttf_initialized) {
        TTF_Quit();
    }
    if (app->sdl_initialized) {
        SDL_Quit();
    }
    log_set_sink(NULL, NULL);
    SDL_zerop(app);
}

static void close_window(App *app, AppWindow window)
{
    switch (window) {
    case APP_WINDOW_INSPECTOR:
        /* Its open tab first. */
        if (app->inspector_tab >= 0) {
            app->inspector_tab = -1;
        } else {
            clear_selection(app);
        }
        break;
    case APP_WINDOW_INSPECTOR_TAB: app->inspector_tab = -1; break;
    case APP_WINDOW_WORK: app->show_work_panel = false; break;
    case APP_WINDOW_LOG: app->show_log = false; break;
    case APP_WINDOW_HELP: app->show_help = false; break;
    case APP_WINDOW_DEBUG: app->show_debug_menu = false; break;
    case APP_WINDOW_ITEMS: app->show_items_menu = false; break;
    case APP_WINDOW_ANIMALS: app->show_animals_menu = false; break;
    case APP_WINDOW_VISUAL: app->show_visual_menu = false; break;
    case APP_WINDOW_SCENARIOS: app->show_scenarios_menu = false; break;
    case APP_WINDOW_GAME_MENU:
        app->show_game_menu = false;
        app->leave_confirm = false;
        break;
    case APP_WINDOW_SETTINGS:
        app->show_settings = false;
        app->show_game_menu =
            app->screen == APP_SCREEN_GAME; /* Back where they were opened from. */
        break;
    default: break;
    }
}

/* Escape (or a right click) steps back: cancels the drag under way, leaves
 * the tool, closes the build menu, then the window in front (closing an
 * inspector clears the selection); with nothing left to close, Escape opens
 * the game menu (a right click does nothing more). */
static void step_back(App *app, bool escape)
{
    AppWindow open[APP_WINDOW_COUNT];
    int open_count = hud_open_windows(app, open);
    if (app->left_button_down) {
        app->left_button_down = false;
        app->box_selecting = false;
    } else if (app->tool != APP_TOOL_NONE) {
        app->tool = APP_TOOL_NONE;
    } else if (app->build_menu_open) {
        app->build_menu_open = false;
    } else if (open_count > 0) {
        close_window(app, open[open_count - 1]);
    } else if (escape) {
        app->show_game_menu = true;
    }
}

/* Turns a tool on (app_toggle_tool() would turn it off when it already is). */
static void set_tool(App *app, AppTool tool, int type)
{
    if (!app_tool_active(app, tool, type)) {
        app_toggle_tool(app, tool, type);
    }
}

/* Starts building another of what is selected: a zone of the same kind (a
 * new one: the selection is dropped, or drags would extend it), a
 * workstation or a structure of the same type. */
bool app_build_like_target(const App *app, AppTool *tool, int *type)
{
    if (app->selected_stockpile != STOCKPILE_NONE) {
        *tool = APP_TOOL_STOCKPILE;
        *type = APP_ZONE_ADD;
        return true;
    }
    if (app->selected_gather_zone != GATHER_ZONE_NONE) {
        *tool = APP_TOOL_GATHER;
        *type = APP_ZONE_ADD;
        return true;
    }
    const Workstation *station =
        workstation_get_const(&app->game.workstations, app->selected_station);
    if (station != NULL) {
        *tool = APP_TOOL_BUILD;
        *type = (int)station->type;
        return true;
    }
    if (!app->tile_selected) {
        return false;
    }
    int x = app->selected_tile_x, y = app->selected_tile_y, z = app->selected_tile_z;
    const Map *map = game_view_map(&app->game);
    StructureType structure = STRUCTURE_NONE;
    switch (app->selected_thing) {
    case THING_STRUCTURE: structure = map_structure(map, x, y, z); break;
    case THING_ROOF: structure = map_roof(map, x, y, z); break;
    case THING_SITE: {
        /* A workstation's site: another of that workstation. */
        const ConstructionSite *site = site_at(app, x, y, z);
        if (site != NULL && construction_workstation(site) != WORKSTATION_TYPE_COUNT) {
            *tool = APP_TOOL_BUILD;
            *type = (int)construction_workstation(site);
            return true;
        }
        structure = site != NULL ? (StructureType)site->type : STRUCTURE_NONE;
        break;
    }
    default: break;
    }
    if (structure != STRUCTURE_NONE) {
        *tool = APP_TOOL_STRUCTURE;
        *type = (int)structure;
        return true;
    }
    return false;
}

void app_build_like_selection(App *app)
{
    AppTool tool;
    int type;
    if (!app_build_like_target(app, &tool, &type)) {
        return;
    }
    if (tool == APP_TOOL_STRUCTURE) {
        app->structure_direction = structure_direction_at(
            app, app->selected_tile_x, app->selected_tile_y, app->selected_tile_z);
    }
    clear_selection(app);
    set_tool(app, tool, type);
}

/* Shows another level (clamped to the map's). */
static void set_level(App *app, int level)
{
    app_stop_following(app); /* The player takes the view back. */
    app->camera.level = clampi(level, 0, app->game.map.depth - 1);
}

static void handle_key_down(App *app, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    /* Physical positions (scancodes): the same controls on any layout (app.h). */
    switch (key->scancode) {
    case APP_KEY_PAUSE: game_clock_toggle_pause(&app->game.clock); return;
    case APP_KEY_BUILD_LIKE: app_build_like_selection(app); return;
    case APP_KEY_FOLLOW:
        if (!entity_id_is_none(app->selected_animal)) {
            app_toggle_follow(app, FOLLOW_ANIMAL, app->selected_animal);
        } else if (app->selected_count == 1) {
            app_toggle_follow(app, FOLLOW_COLONIST, app->selected[0]);
        }
        return;
    case SDL_SCANCODE_1:
    case SDL_SCANCODE_KP_1: game_clock_set_speed(&app->game.clock, GAME_SPEED_NORMAL); return;
    case SDL_SCANCODE_2:
    case SDL_SCANCODE_KP_2: game_clock_set_speed(&app->game.clock, GAME_SPEED_FAST); return;
    case SDL_SCANCODE_3:
    case SDL_SCANCODE_KP_3: game_clock_set_speed(&app->game.clock, GAME_SPEED_FASTEST); return;
    case APP_KEY_SLOWER:
    case SDL_SCANCODE_KP_MINUS: game_clock_step_speed(&app->game.clock, -1); return;
    case APP_KEY_FASTER:
    case SDL_SCANCODE_KP_PLUS: game_clock_step_speed(&app->game.clock, 1); return;
    case APP_KEY_BACK: step_back(app, true); return;
    case APP_KEY_HELP: app->show_help = !app->show_help; return;
    case APP_KEY_DEBUG_MENU: app->show_debug_menu = !app->show_debug_menu; return;
    case APP_KEY_ITEMS_MENU: app->show_items_menu = !app->show_items_menu; return;
    case APP_KEY_ANIMALS_MENU: app->show_animals_menu = !app->show_animals_menu; return;
    case APP_KEY_VISUAL_MENU: app->show_visual_menu = !app->show_visual_menu; return;
    case APP_KEY_SCENARIOS_MENU: app->show_scenarios_menu = !app->show_scenarios_menu; return;
    case APP_KEY_TEMPERATURE: app_toggle_temperature_view(app); return;
    case APP_KEY_LIGHT: app_toggle_light_view(app); return;
    case APP_KEY_MOISTURE: app_toggle_moisture_view(app); return;
    case APP_KEY_LEVEL_UP: set_level(app, app->camera.level + 1); return;
    case APP_KEY_LEVEL_DOWN: set_level(app, app->camera.level - 1); return;
    case APP_KEY_MINE: toggle_tool(app, APP_TOOL_DESIGNATE, DESIGNATION_MINE); return;
    case APP_KEY_GATHER: app_toggle_tool(app, APP_TOOL_GATHER, APP_ZONE_ADD); return;
    case APP_KEY_CANCEL:
        if (!app_cancel_selection(app)) {
            toggle_tool(app, APP_TOOL_DESIGNATE, DESIGNATION_NONE);
        }
        return;
    case APP_KEY_STOCKPILE: app_toggle_tool(app, APP_TOOL_STOCKPILE, APP_ZONE_ADD); return;
    case APP_KEY_WORK: bottom_bar_press_work(app); return;
    case APP_KEY_LOG: bottom_bar_press_log(app); return;
    case APP_KEY_ROTATE:
        app->structure_direction = direction_turn(app->structure_direction);
        return;
    case APP_KEY_DEBUG:
        app->show_debug = !app->show_debug;
        app->debug_tree_count = map_count_plants(&app->game.map, PLANT_TREE);
        return;
    case APP_KEY_LANGUAGE: app_cycle_language(app); return;
    default: return;
    }
}

/* ---- Selection ------------------------------------------------------------ */

static int find_selected(const App *app, EntityId id)
{
    for (int i = 0; i < app->selected_count; i++) {
        if (entity_id_equal(app->selected[i], id)) {
            return i;
        }
    }
    return -1;
}

static void select_add(App *app, EntityId id)
{
    if (find_selected(app, id) < 0 && app->selected_count < COLONIST_MAX) {
        app->selected[app->selected_count++] = id;
    }
}

static void select_remove_at(App *app, int index)
{
    app->selected[index] = app->selected[--app->selected_count];
}

/* Drops handles of colonists that no longer exist. */
static void prune_selection(App *app)
{
    for (int i = app->selected_count - 1; i >= 0; i--) {
        if (game_colonist_const(&app->game, app->selected[i]) == NULL) {
            select_remove_at(app, i);
        }
    }
}

/* What a click on a tile may select, and on which level. */
typedef struct ClickPick {
    ClickTarget target;
    int z;
} ClickPick;

/* Most levels a click reaches: the level shown, the level seen there, and the
 * open levels between (they only hold plants: foliage). */
#define CLICK_PICK_MAX (CLICK_TARGET_COUNT * MAP_MAX_LEVELS)

/* Whether a click on a tile reaches what lies on level z: on the level shown
 * and the level seen there (map_view_level(): the ground under open space),
 * and, for plants, the levels between (a tree's foliage over what is seen). */
static bool click_reaches(ClickTarget target, int z, int shown, int seen)
{
    return z == shown || z == seen || (target == CLICK_PLANT && z > seen && z < shown);
}

/* Everything a click on (x, y) from the level shown may select, in the order
 * repeated clicks go through it: level by level from the top, then in the
 * order of ClickTarget. Returns how many. */
static int click_picks(const App *app, int x, int y, ClickPick *out, int max)
{
    const int shown = app->camera.level;
    int seen = map_view_level(game_view_map(&app->game), x, y, shown);
    if (seen < 0 || seen > shown) {
        seen = shown;
    }
    int count = 0;
    for (int z = shown; z >= seen; z--) {
        for (int target = 0; target < CLICK_TARGET_COUNT && count < max; target++) {
            if (click_reaches((ClickTarget)target, z, shown, seen) &&
                click_target_at(app, (ClickTarget)target, x, y, z)) {
                out[count++] = (ClickPick){(ClickTarget)target, z};
            }
        }
    }
    return count;
}

/* Items a click may pick under the cursor at most. */
#define CLICK_ITEMS_MAX 32

/* The items drawn under the cursor, the topmost first: on the level shown,
 * or else on the ground seen below it. */
static int click_items(const App *app, float world_x, float world_y, int x, int y, ItemPick *out)
{
    const int shown = app->camera.level;
    int count = game_items_under_point(&app->game, world_x, world_y, shown, out, CLICK_ITEMS_MAX);
    int seen = map_view_level(game_view_map(&app->game), x, y, shown);
    if (count == 0 && seen >= 0 && seen < shown) {
        count = game_items_under_point(&app->game, world_x, world_y, seen, out, CLICK_ITEMS_MAX);
    }
    return count;
}

/* Click: select the colonist or the animal under the cursor, or else an
 * item drawn under it (the topmost), or else what is seen on the tile: the
 * workstation, a structure, the items, a plant, a stockpile zone or a
 * gathering zone, on the level shown or on the ground seen below it; a click
 * on what is selected goes to the next one of these (the items under the
 * cursor one by one, then the things of the tile). Shift adds colonists to /
 * removes them from the selection. */
static void select_at_point(App *app, float screen_x, float screen_y, bool additive)
{
    const int z = app->camera.level; /* The level shown. */
    float world_x, world_y;
    camera_screen_to_world(&app->camera, screen_x, screen_y, &world_x, &world_y);
    EntityId id = game_colonist_at(&app->game, world_x, world_y, z);
    if (entity_id_is_none(id)) {
        /* Colonist sprites stand above their position: a click on the head
         * or the body counts too. */
        id = game_colonist_at(&app->game, world_x, world_y + APP_PICK_BODY_OFFSET, z);
    }
    /* A second click close to the first on an item or a plant: all of its
     * kind on the screen, rather than the next thing under the cursor. */
    Uint64 now = SDL_GetTicks();
    bool twice = !additive && app->tile_selected && entity_id_is_none(id) &&
                 now - app->map_click_ms <= APP_DOUBLE_CLICK_MS &&
                 SDL_fabsf(screen_x - app->map_click_x) <= APP_DOUBLE_CLICK_SLOP &&
                 SDL_fabsf(screen_y - app->map_click_y) <= APP_DOUBLE_CLICK_SLOP;
    app->map_click_ms = now;
    app->map_click_x = screen_x;
    app->map_click_y = screen_y;
    if (twice && select_same_on_screen(app)) {
        app->map_click_ms = 0; /* A third click starts over. */
        return;
    }
    int x = (int)floorf(world_x), y = (int)floorf(world_y);
    ItemPick items[CLICK_ITEMS_MAX];
    int item_count = click_items(app, world_x, world_y, x, y, items);
    ClickPick picks[CLICK_PICK_MAX];
    int pick_count = click_picks(app, x, y, picks, CLICK_PICK_MAX);
    /* Where the selection stands among them, the items first (-1: not here). */
    int current = -1;
    for (int i = 0; i < item_count && current < 0; i++) {
        if (app->tile_selected && app->selected_thing == THING_ITEM &&
            app->selected_item.id.slot == items[i].id.slot &&
            app->selected_item.id.generation == items[i].id.generation) {
            current = i;
        }
    }
    for (int i = 0; i < pick_count && current < 0; i++) {
        if (click_target_selected(app, x, y, picks[i].z) == picks[i].target) {
            current = item_count + i;
        }
    }
    /* A click on the selected animal goes on to what is under it. */
    EntityId was_animal = app->selected_animal;
    if (!additive) {
        app->selected_count = 0;
    }
    app->selected_stockpile = STOCKPILE_NONE;
    app->selected_station = WORKSTATION_NONE;
    app->selected_gather_zone = GATHER_ZONE_NONE;
    app->tile_selected = false;
    app->selected_animal = ENTITY_NONE;
    if (entity_id_is_none(id)) {
        if (additive) {
            return;
        }
        /* An animal the colony sees, before what is on the ground. */
        EntityId animal = game_animal_at(&app->game, world_x, world_y, z);
        if (entity_id_is_none(animal)) {
            animal = game_animal_at(&app->game, world_x, world_y + APP_PICK_ANIMAL_OFFSET, z);
        }
        if (!entity_id_is_none(animal) && !entity_id_equal(animal, was_animal)) {
            app->selected_animal = animal;
            return;
        }
        if (item_count + pick_count == 0) {
            return;
        }
        int next = (current + 1) % (item_count + pick_count);
        if (next < item_count) {
            select_item(app, items[next]);
            return;
        }
        ClickPick pick = picks[next - item_count];
        switch (pick.target) {
        case CLICK_STATION:
            select_station(app, workstation_at(&app->game.workstations, x, y, pick.z));
            break;
        case CLICK_STRUCTURE:
        case CLICK_ROOF:
        case CLICK_SITE:
        case CLICK_ITEMS:
        case CLICK_PLANT:
        case CLICK_GRASS: select_thing(app, click_thing(pick.target), x, y, pick.z); break;
        case CLICK_STOCKPILE:
            select_stockpile(app, stockpile_at(&app->game.stockpiles, x, y, pick.z));
            break;
        case CLICK_GATHER_ZONE:
            select_gather_zone(app, gather_zone_at(&app->game.gather_zones, x, y, pick.z));
            break;
        case CLICK_TARGET_COUNT: break;
        }
        return;
    }
    int index = find_selected(app, id);
    if (additive && index >= 0) {
        select_remove_at(app, index);
    } else {
        select_add(app, id);
    }
}

void app_select_colonist(App *app, EntityId id, bool additive)
{
    if (!additive) {
        app->selected_count = 0;
    }
    app->selected_stockpile = STOCKPILE_NONE;
    app->selected_station = WORKSTATION_NONE;
    app->selected_gather_zone = GATHER_ZONE_NONE;
    app->tile_selected = false;
    app->selected_animal = ENTITY_NONE;
    int index = find_selected(app, id);
    if (additive && index >= 0) {
        select_remove_at(app, index);
    } else {
        select_add(app, id);
    }
    app->build_menu_open = false;
}

void app_request_test_scenario(App *app, TestScenario scenario)
{
    app->pending_scenario = (int)scenario + 1;
}

/* Forgets what pointed into a game that was replaced: selection, tools,
 * notices, the being followed. */
static void forget_game(App *app)
{
    clear_selection(app);
    app->tool = APP_TOOL_NONE;
    app->build_category = BUILD_CATEGORY_NONE;
    app->left_button_down = false;
    app->box_selecting = false;
    app->debug_attacker = combatant_none();
    app->debug_held = ITEM_ID_NONE;
    app->inspector_tab = -1;
    app->next_notice = 0;
    app->notices_shown_count = 0;
    app_stop_following(app);
}

/* Replaces the game by the test scenario asked for (core/test_scenario.h) and
 * forgets what pointed into the old one: selection, tools, notices. The view
 * goes to the middle of the world, on its only level. */
static void load_pending_scenario(App *app)
{
    TestScenario scenario = (TestScenario)(app->pending_scenario - 1);
    app->pending_scenario = 0;
    if (!test_scenario_load(&app->game, scenario)) {
        LOG_ERROR("Cannot load the test scenario: leaving");
        app->game_ready = false; /* Shut down by the failed load. */
        app->running = false;
        return;
    }
    forget_game(app);
    camera_init(&app->camera, (float)app->game.map.width * 0.5f, (float)app->game.map.height * 0.5f,
                app->camera.zoom);
    app->camera.level = 0;
    LOG_INFO("Test scenario %d loaded", (int)scenario);
}

/* Closes the windows about a game (the game menu included): a new one starts. */
static void close_game_windows(App *app)
{
    app->show_work_panel = false;
    app->show_log = false;
    app->show_help = false;
    app->show_debug_menu = false;
    app->show_items_menu = false;
    app->show_animals_menu = false;
    app->show_visual_menu = false;
    app->show_scenarios_menu = false;
    app->show_game_menu = false;
    app->show_settings = false;
    app->build_menu_open = false;
    app->leave_confirm = false;
    app->show_temperature = false;
    app->show_light = false;
    app->show_moisture = false;
}

/* The New game window's Start was shown working: makes the world asked for
 * and begins the game on it, with the fog of war. */
static void start_new_game(App *app)
{
    NewGameParams params = app->new_game;
    if (!new_game_seed_from_text(app->new_game_seed, &params.world.seed)) {
        params.world.seed = app_random_seed();
    }
    title_screen_end(app);
    if (app->game_ready) {
        game_shutdown(&app->game);
        app->game_ready = false;
    }
    if (!game_init(&app->game, &params)) {
        LOG_ERROR("World generation failed: leaving");
        app->running = false;
        return;
    }
    app->game_ready = true;
    if (!game_set_fog(&app->game, true)) {
        LOG_ERROR("Cannot start the fog of war");
    }
    LOG_INFO("New game: seed %" SDL_PRIu64 ", %dx%d tiles, %u colonists, %u animals",
             params.world.seed, params.world.width, params.world.height,
             app->game.colonist_pool.count, app->game.animals.pool.count);
    forget_game(app);
    close_game_windows(app);
    app->screen = APP_SCREEN_GAME;
    view_start(app);
}

void app_request_title(App *app)
{
    app->pending_title = true;
}

/* Leaves the game for the title screen (app_request_title()). */
static void return_to_title(App *app)
{
    app->pending_title = false;
    forget_game(app);
    close_game_windows(app);
    if (!open_title_screen(app)) {
        app->running = false;
    }
}

/* Where the followed being is and on which level; false when there is none
 * to follow (any more). */
static bool follow_position(const App *app, float *x, float *y, int *level)
{
    switch (app->follow_kind) {
    case FOLLOW_COLONIST: {
        const Colonist *colonist = game_colonist_const(&app->game, app->follow_id);
        if (colonist == NULL) {
            return false;
        }
        colonist_position(colonist, x, y);
        *level = colonist->z;
        return true;
    }
    case FOLLOW_ANIMAL: {
        const Animal *animal = game_animal_const(&app->game, app->follow_id);
        if (animal == NULL || !game_animal_seen(&app->game, app->follow_id)) {
            return false;
        }
        animal_position(animal, x, y);
        *level = animal->z;
        return true;
    }
    case FOLLOW_NONE: break;
    }
    return false;
}

/* Keeps the camera on the followed being (after the game's update, so that
 * the view and the sprite agree). */
static void update_follow(App *app)
{
    if (app->follow_kind == FOLLOW_NONE) {
        return;
    }
    float x, y;
    int level;
    if (!follow_position(app, &x, &y, &level)) {
        app_stop_following(app);
        return;
    }
    app->camera.x = x;
    app->camera.y = y;
    app->camera.level = clampi(level, 0, app->game.map.depth - 1);
    camera_clamp(&app->camera, (float)app->game.map.width, (float)app->game.map.height);
}

void app_follow(App *app, FollowKind kind, EntityId id)
{
    app->follow_kind = kind;
    app->follow_id = id;
    update_follow(app); /* Centers it at once, or stops when there is no one. */
}

void app_stop_following(App *app)
{
    app->follow_kind = FOLLOW_NONE;
    app->follow_id = ENTITY_NONE;
}

bool app_is_following(const App *app, FollowKind kind, EntityId id)
{
    return app->follow_kind == kind && kind != FOLLOW_NONE && entity_id_equal(app->follow_id, id);
}

void app_toggle_follow(App *app, FollowKind kind, EntityId id)
{
    if (app_is_following(app, kind, id)) {
        app_stop_following(app);
    } else {
        app_follow(app, kind, id);
    }
}

void app_focus_colonist(App *app, EntityId id)
{
    app_stop_following(app);
    const Colonist *colonist = game_colonist_const(&app->game, id);
    if (colonist != NULL) {
        colonist_position(colonist, &app->camera.x, &app->camera.y);
        app->camera.level = colonist->z;
    }
}

/* Selects the colonists in the box from the drag's world corner to the
 * cursor; with none in it, the items lying in it (one: as a click would
 * select it, several: all of them at once). */
static void select_in_box(App *app, float screen_x, float screen_y, bool additive)
{
    const float wx0 = app->drag_world_x, wy0 = app->drag_world_y;
    float wx1, wy1;
    camera_screen_to_world(&app->camera, screen_x, screen_y, &wx1, &wy1);
    EntityId found[COLONIST_MAX];
    int count = game_colonists_in_rect(&app->game, wx0, wy0, wx1, wy1, app->camera.level, found,
                                       COLONIST_MAX);
    if (!additive) {
        app->selected_count = 0;
    }
    app->selected_stockpile = STOCKPILE_NONE;
    app->selected_station = WORKSTATION_NONE;
    app->selected_gather_zone = GATHER_ZONE_NONE;
    app->tile_selected = false;
    app->selected_animal = ENTITY_NONE;
    for (int i = 0; i < count; i++) {
        select_add(app, found[i]);
    }
    if (count == 0 && app->selected_count == 0) {
        ItemPick picks[APP_BOX_ITEMS_MAX];
        int items = game_items_in_rect(&app->game, wx0, wy0, wx1, wy1, app->camera.level, picks,
                                       APP_BOX_ITEMS_MAX);
        if (items == 1) {
            select_item(app, picks[0]);
        } else if (items > 1) {
            select_box_items(app, picks, items);
        }
    }
}

static void screen_to_tile(const App *app, float screen_x, float screen_y, int *x, int *y)
{
    float world_x, world_y;
    camera_screen_to_world(&app->camera, screen_x, screen_y, &world_x, &world_y);
    *x = (int)floorf(world_x);
    *y = (int)floorf(world_y);
}

/* Tool drag rectangle in tiles, from where the button was pressed (a fixed
 * point of the world, even if the camera moved since) to the cursor. */
static void tool_rect(const App *app, float screen_x, float screen_y, int *x0, int *y0, int *x1,
                      int *y1)
{
    *x0 = (int)floorf(app->drag_world_x);
    *y0 = (int)floorf(app->drag_world_y);
    screen_to_tile(app, screen_x, screen_y, x1, y1);
}

/* Whether the tool is used by dragging out an area of tiles (the others act
 * where the button is pressed or released). */
static bool tool_drags_area(AppTool tool)
{
    return tool == APP_TOOL_DESIGNATE || tool == APP_TOOL_STOCKPILE || tool == APP_TOOL_GATHER ||
           tool == APP_TOOL_FORBID;
}

/* Whether a zone tool drag removes tiles: in the removal or shrinking mode,
 * or with Shift. */
static bool zone_tool_removes(const App *app)
{
    return app->zone_mode == APP_ZONE_REMOVE || app->zone_mode == APP_ZONE_SHRINK ||
           (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
}

/* Whether the zone tool works on the selected zone alone. */
static bool zone_tool_edits(const App *app)
{
    return app->zone_mode == APP_ZONE_EXTEND || app->zone_mode == APP_ZONE_SHRINK;
}

/* Stockpile tool release: adds the rectangle to the selected stockpile and
 * those it overlaps (merged into one), or creates a new one, which becomes
 * selected; or removes zone tiles. */
/* The debug tool: the chosen item (one, or a full stack) on the tile, the
 * selected colonists moved there, a tree planted or grown, an animal, or a
 * blow on whoever is there. */
static void apply_debug_tool(App *app, float screen_x, float screen_y)
{
    int x, y;
    screen_to_tile(app, screen_x, screen_y, &x, &y);
    const int z = app->camera.level;
    switch (app->debug_mode) {
    case APP_DEBUG_TELEPORT:
        for (int i = 0; i < app->selected_count; i++) {
            game_debug_teleport(&app->game, app->selected[i], x, y, z);
        }
        break;
    case APP_DEBUG_PLANT_TREE: game_debug_plant_tree(&app->game, x, y, z); break;
    case APP_DEBUG_SPAWN_ANIMAL: { /* On the ground seen there, however deep. */
        int level = map_view_level(&app->game.map, x, y, z);
        game_debug_spawn_animal(&app->game, app->debug_animal, app->debug_animal_sex, x, y,
                                level >= 0 ? level : z);
        break;
    }
    case APP_DEBUG_FIGHT: { /* First the attacker, then its target. */
        int level = map_view_level(&app->game.map, x, y, z);
        Combatant who = game_combatant_at(&app->game, x, y, level >= 0 ? level : z);
        if (combatant_is_none(app->debug_attacker) || combatant_is_none(who)) {
            app->debug_attacker = who;
        } else {
            game_debug_attack(&app->game, app->debug_attacker, who);
            app->debug_attacker = combatant_none();
        }
        break;
    }
    case APP_DEBUG_HURT: { /* Whoever stands on the ground seen there. */
        int level = map_view_level(&app->game.map, x, y, z);
        game_debug_hurt(&app->game, x, y, level >= 0 ? level : z);
        break;
    }
    case APP_DEBUG_GROW_TREE: /* Shift: all the way. */
        game_debug_grow_tree(&app->game, x, y, z, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
        break;
    default:
        game_debug_spawn_items(&app->game, x, y, z, app->debug_item,
                               app->debug_item_max ? item_stack_limit(app->debug_item) : 1);
        break;
    }
}

static void apply_stockpile_tool(App *app, float screen_x, float screen_y)
{
    const int z = app->camera.level; /* The level shown. */
    int x0, y0, x1, y1;
    tool_rect(app, screen_x, screen_y, &x0, &y0, &x1, &y1);
    if (zone_tool_edits(app)) {
        int index = app->selected_stockpile;
        if (zone_tool_removes(app)) {
            game_shrink_stockpile_rect(&app->game, index, x0, y0, x1, y1, z);
            prune_stockpile_selection(app);
        } else {
            game_extend_stockpile_rect(&app->game, index, x0, y0, x1, y1, z);
        }
    } else if (zone_tool_removes(app)) {
        game_remove_stockpile_rect(&app->game, x0, y0, x1, y1, z);
        prune_stockpile_selection(app);
    } else {
        select_stockpile(
            app, game_add_stockpile_rect(&app->game, x0, y0, x1, y1, z, app->selected_stockpile));
    }
}

/* Gathering zone tool release: like the stockpile tool. */
static void apply_gather_tool(App *app, float screen_x, float screen_y)
{
    const int z = app->camera.level; /* The level shown. */
    int x0, y0, x1, y1;
    tool_rect(app, screen_x, screen_y, &x0, &y0, &x1, &y1);
    if (zone_tool_edits(app)) {
        int index = app->selected_gather_zone;
        if (zone_tool_removes(app)) {
            game_shrink_gather_zone_rect(&app->game, index, x0, y0, x1, y1, z);
            prune_gather_zone_selection(app);
        } else {
            game_extend_gather_zone_rect(&app->game, index, x0, y0, x1, y1, z);
        }
    } else if (zone_tool_removes(app)) {
        game_remove_gather_zone_rect(&app->game, x0, y0, x1, y1, z);
        prune_gather_zone_selection(app);
    } else {
        select_gather_zone(app, game_add_gather_zone_rect(&app->game, x0, y0, x1, y1, z,
                                                          app->selected_gather_zone));
    }
}

static void drag_held_item(App *app, float screen_x, float screen_y);

/* The item under a point of the screen for the debug item tool: on the level
 * shown, or else on the ground seen below it (the level shown may be open
 * air). ITEM_ID_NONE when there is none. */
static ItemId debug_item_at(const App *app, float screen_x, float screen_y)
{
    float world_x, world_y;
    camera_screen_to_world(&app->camera, screen_x, screen_y, &world_x, &world_y);
    int shown = app->camera.level;
    ItemId id = game_item_at_point(&app->game, world_x, world_y, shown);
    if (id.slot == UINT32_MAX) {
        int seen =
            game_build_level(&app->game, (int)floorf(world_x), (int)floorf(world_y), shown, false);
        if (seen != shown) {
            id = game_item_at_point(&app->game, world_x, world_y, seen);
        }
    }
    return id;
}

static void handle_mouse_button(App *app, const SDL_MouseButtonEvent *button)
{
    if (button->button != SDL_BUTTON_LEFT) {
        /* Right clicks on a panel go to its widgets; dragging from a panel
         * with the middle or right button does not pan the view. */
        bool over_panel = ui_is_over_panel(&app->ui, button->x, button->y);
        app->pan_blocked = button->down && over_panel;
        if (button->button != SDL_BUTTON_RIGHT) {
            return;
        }
        if (button->down) {
            bool captured = ui_capture_click(&app->ui, button->x, button->y, SDL_BUTTON_RIGHT);
            /* On the map, a right click steps back, unless it was a drag to pan. */
            app->right_click_pending = !captured && !over_panel;
            app->right_down_x = button->x;
            app->right_down_y = button->y;
        } else if (app->right_click_pending) {
            app->right_click_pending = false;
            float dx = button->x - app->right_down_x, dy = button->y - app->right_down_y;
            if (dx * dx + dy * dy <= APP_DRAG_THRESHOLD * APP_DRAG_THRESHOLD) {
                step_back(app, false);
            }
        }
        return;
    }
    if (button->down) {
        if (ui_capture_click(&app->ui, button->x, button->y, SDL_BUTTON_LEFT)) {
            return; /* Clicked on a panel: the widgets get it. */
        }
        app->left_button_down = true;
        app->box_selecting = false;
        app->drag_start_x = button->x;
        app->drag_start_y = button->y;
        camera_screen_to_world(&app->camera, button->x, button->y, &app->drag_world_x,
                               &app->drag_world_y);
        if (app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_MOVE_ITEM)) {
            app->debug_held = debug_item_at(app, button->x, button->y);
        }
        return;
    }
    if (!app->left_button_down) {
        return;
    }
    if (app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_MOVE_ITEM)) {
        drag_held_item(app, button->x, button->y); /* Dropped there. */
        app->debug_held = ITEM_ID_NONE;
        app->left_button_down = false;
        app->box_selecting = false;
        return;
    }
    bool additive = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    const int z = app->camera.level; /* The level shown. */
    if (app->tool == APP_TOOL_DESIGNATE) {
        int x0, y0, x1, y1;
        tool_rect(app, button->x, button->y, &x0, &y0, &x1, &y1);
        /* Orders nobody can carry out are refused (the tool panel says why). */
        if (app->tool_type == DESIGNATION_NONE) {
            game_cancel_rect(&app->game, x0, y0, x1, y1, z);
            prune_station_selection(app);
        } else if (game_designation_available(&app->game, app->tool_type)) {
            game_designate_rect(&app->game, x0, y0, x1, y1, z, app->tool_type);
        }
    } else if (app->tool == APP_TOOL_BUILD) {
        /* Blueprints nobody can build are refused (the tool panel says why). */
        int x, y;
        screen_to_tile(app, button->x, button->y, &x, &y);
        if (game_workstation_buildable(&app->game, app->build_type)) {
            game_place_workstation(&app->game, app->build_type, x, y,
                                   game_build_level(&app->game, x, y, z, false));
        }
    } else if (app->tool == APP_TOOL_STRUCTURE) {
        int x0, y0, x1, y1;
        tool_rect(app, button->x, button->y, &x0, &y0, &x1, &y1);
        if (game_structure_buildable(&app->game, app->structure_type)) {
            game_place_structure_rect(&app->game, app->structure_type, app->structure_direction, x0,
                                      y0, x1, y1, z);
        }
    } else if (app->tool == APP_TOOL_FORBID) {
        int x0, y0, x1, y1;
        tool_rect(app, button->x, button->y, &x0, &y0, &x1, &y1);
        game_set_forbidden_rect(&app->game, x0, y0, x1, y1, z, !app->forbid_allows);
    } else if (app->tool == APP_TOOL_MOVE) {
        if (app->selected_count > 0) {
            int x, y;
            screen_to_tile(app, button->x, button->y, &x, &y);
            /* Where they go is what is seen there: the ground under open space. */
            int seen = map_view_level(game_view_map(&app->game), x, y, z);
            game_order_move(&app->game, app->selected, app->selected_count, x, y,
                            seen >= 0 && seen <= z ? seen : z);
        } else {
            app->tool = APP_TOOL_NONE; /* Nobody left to send. */
        }
    } else if (app->tool == APP_TOOL_MOVE_ITEM) {
        int x, y;
        screen_to_tile(app, button->x, button->y, &x, &y);
        TilePos from = app->move_item_source;
        /* Where they go is what is seen there: the ground under open space. */
        int seen = map_view_level(game_view_map(&app->game), x, y, z);
        const int to_z = seen >= 0 && seen <= z ? seen : z;
        bool ordered;
        if (app->move_item_box) { /* Each item of the box, as far as the orders and room go. */
            ordered = false;
            for (int i = 0; i < app->selected_box_item_count; i++) {
                ItemPick pick = app->selected_box_items[i];
                ItemId id = game_pick_real_item(&app->game, &pick);
                ordered |= game_order_single_item_move(&app->game, id, x, y, to_z);
            }
        } else if (app->move_item_id.slot != UINT32_MAX) {
            ordered = game_order_single_item_move(&app->game, app->move_item_id, x, y, to_z);
        } else {
            ordered = game_order_item_move(&app->game, from.x, from.y, from.z, x, y, to_z);
        }
        if (ordered) {
            app->tool = APP_TOOL_NONE; /* Done: the items are on their way. */
        } /* Else the tool stays for another try (the card says what to do). */
    } else if (app->tool == APP_TOOL_DEBUG) {
        apply_debug_tool(app, button->x, button->y);
    } else if (app->tool == APP_TOOL_STOCKPILE) {
        apply_stockpile_tool(app, button->x, button->y);
    } else if (app->tool == APP_TOOL_GATHER) {
        apply_gather_tool(app, button->x, button->y);
    } else {
        if (app->box_selecting) {
            select_in_box(app, button->x, button->y, additive);
        } else {
            select_at_point(app, button->x, button->y, additive);
        }
        if (has_selection(app)) {
            /* The inspector takes the place of the main menus and windows. */
            app->build_menu_open = false;
            app->show_work_panel = false;
            app->show_log = false;
        }
    }
    app->left_button_down = false;
    app->box_selecting = false;
}

/* ---- Events ---------------------------------------------------------------- */

/* The debug item tool: the item held follows the cursor, onto the very spot
 * (it stays where it was over a place nobody could stand). */
static void drag_held_item(App *app, float screen_x, float screen_y)
{
    if (app->debug_held.slot == UINT32_MAX) {
        return;
    }
    float world_x, world_y;
    camera_screen_to_world(&app->camera, screen_x, screen_y, &world_x, &world_y);
    /* Onto the ground seen under the cursor, as the other tools build on it. */
    int z = game_build_level(&app->game, (int)floorf(world_x), (int)floorf(world_y),
                             app->camera.level, false);
    game_debug_move_item(&app->game, app->debug_held, world_x, world_y, z);
}

/* Turns of an item per step of the wheel, in 1/256 of a turn. */
#define APP_ITEM_TURN_STEP 16

/* The debug item tool: the wheel turns the item held, or else the one under
 * the cursor. Returns whether it turned one (the wheel then does not zoom). */
static bool turn_item_under(App *app, float screen_x, float screen_y, float steps)
{
    if (!app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_MOVE_ITEM) || steps == 0.0f) {
        return false;
    }
    ItemId id = app->debug_held;
    if (id.slot == UINT32_MAX) {
        id = debug_item_at(app, screen_x, screen_y);
    }
    return game_debug_turn_item(&app->game, id, (int)lroundf(steps * APP_ITEM_TURN_STEP));
}

static void handle_event(App *app, const SDL_Event *event)
{
    if (app->screen == APP_SCREEN_TITLE) {
        title_screen_handle_event(app, event);
        return;
    }
    switch (event->type) {
    case SDL_EVENT_QUIT: app->running = false; break;
    case SDL_EVENT_KEY_DOWN: handle_key_down(app, &event->key); break;
    case SDL_EVENT_MOUSE_WHEEL: {
        float steps = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            steps = -steps;
        }
        if (ui_scroll(&app->ui, event->wheel.mouse_x, event->wheel.mouse_y, steps)) {
            break; /* The wheel does not zoom the map through a panel. */
        }
        if (turn_item_under(app, event->wheel.mouse_x, event->wheel.mouse_y, steps)) {
            break;
        }
        if (SDL_GetModState() & SDL_KMOD_SHIFT) {
            /* Up and down the levels: the wheel down goes up (as it zooms out). */
            app->level_wheel -= steps;
            while (app->level_wheel >= 1.0f) {
                set_level(app, app->camera.level + 1);
                app->level_wheel -= 1.0f;
            }
            while (app->level_wheel <= -1.0f) {
                set_level(app, app->camera.level - 1);
                app->level_wheel += 1.0f;
            }
            break;
        }
        if (steps != 0.0f) {
            camera_zoom_at(&app->camera, powf(APP_ZOOM_STEP, steps), event->wheel.mouse_x,
                           event->wheel.mouse_y);
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: handle_mouse_button(app, &event->button); break;
    case SDL_EVENT_MOUSE_MOTION:
        if ((event->motion.state & (SDL_BUTTON_MMASK | SDL_BUTTON_RMASK)) && !app->pan_blocked) {
            camera_pan_pixels(&app->camera, -event->motion.xrel, -event->motion.yrel);
            app_stop_following(app);
        }
        if (app->left_button_down && app_tool_active(app, APP_TOOL_DEBUG, APP_DEBUG_MOVE_ITEM) &&
            app->debug_held.slot != UINT32_MAX) {
            drag_held_item(app, event->motion.x, event->motion.y);
            break;
        }
        /* Only with no tool: a tool uses where the button is released. */
        if (app->left_button_down && !app->box_selecting && app->tool == APP_TOOL_NONE) {
            float dx = event->motion.x - app->drag_start_x;
            float dy = event->motion.y - app->drag_start_y;
            app->box_selecting = dx * dx + dy * dy > APP_DRAG_THRESHOLD * APP_DRAG_THRESHOLD;
        }
        break;
    default: break;
    }
}

static void update_camera(App *app, float dt)
{
    const int z = app->camera.level; /* The level shown. */
    int width = 0, height = 0;
    SDL_GetRenderOutputSize(app->renderer, &width, &height);
    camera_set_viewport(&app->camera, (float)width, (float)height);

    const bool *keys = SDL_GetKeyboardState(NULL);
    /* Physical W/A/S/D positions (scancodes): ZQSD on AZERTY keyboards. */
    float dx = (float)(keys[APP_KEY_RIGHT] || keys[SDL_SCANCODE_RIGHT]) -
               (float)(keys[APP_KEY_LEFT] || keys[SDL_SCANCODE_LEFT]);
    float dy = (float)(keys[APP_KEY_DOWN] || keys[SDL_SCANCODE_DOWN]) -
               (float)(keys[APP_KEY_UP] || keys[SDL_SCANCODE_UP]);
    if (dx != 0.0f || dy != 0.0f) {
        float speed = APP_PAN_SPEED * dt;
        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
            speed *= APP_PAN_FAST_FACTOR;
        }
        camera_pan_pixels(&app->camera, dx * speed, dy * speed);
        app_stop_following(app);
    }
    camera_clamp(&app->camera, (float)app->game.map.width, (float)app->game.map.height);

    float mouse_x, mouse_y, world_x, world_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    camera_screen_to_world(&app->camera, mouse_x, mouse_y, &world_x, &world_y);
    app->hover_x = (int)floorf(world_x);
    app->hover_y = (int)floorf(world_y);
    app->hover_valid = (SDL_GetMouseFocus() == app->window) &&
                       map_in_bounds(&app->game.map, app->hover_x, app->hover_y, z) &&
                       !ui_is_over_panel(&app->ui, mouse_x, mouse_y);
}

static void update_fps(App *app, double dt)
{
    app->fps_elapsed += dt;
    app->fps_frames++;
    if (app->fps_elapsed >= 0.5) {
        if (app->show_debug) {
            /* Counted here rather than every frame: it scans the whole map. */
            app->debug_tree_count = map_count_plants(&app->game.map, PLANT_TREE);
        }
        app->fps = (float)(app->fps_frames / app->fps_elapsed);
        app->render_ms = (float)(app->render_elapsed * 1000.0 / app->fps_frames);
        app->fps_elapsed = 0.0;
        app->render_elapsed = 0.0;
        app->fps_frames = 0;
    }
}

/* The world as the colony sees it: the levels below, the level shown and
 * everything on it, the fog, the markers (ramps, orders) if asked, the light
 * and the weather. */
static void render_world(App *app, bool markers)
{

    /* The levels below, through open space: those seen somewhere, the deepest first. */
    uint32_t levels;
    if (render_below_begin(app->renderer, game_view_map(&app->game), &app->camera, &app->tuning,
                           &levels)) {
        for (int z = 0; z < app->camera.level; z++) {
            if ((levels & (1u << z)) == 0) {
                continue;
            }
            Camera below = app->camera;
            below.level = z;
            render_map(app->renderer, app->sprites, game_view_map(&app->game), &below);
            render_snow_cover(app->renderer, &app->game, &below);
            render_gather_zones(app->renderer, &app->game, &below, GATHER_ZONE_NONE);
            render_stockpiles(app->renderer, &app->game, &below, STOCKPILE_NONE);
            render_structures(app->renderer, &app->game, &below);
            render_workstations(app->renderer, &app->game, &below, WORKSTATION_NONE);
            render_items(app->renderer, app->sprites, &app->game, &below);
            render_world_sprites(app->renderer, app->sprites, &app->game, &below, NULL, 0,
                                 app->selected_animal);
            int next = z + 1;
            while (next < app->camera.level && (levels & (1u << next)) == 0) {
                next++;
            }
            render_below_shade(app->renderer, &app->tuning, z, next);
        }
        render_below_end(app->renderer);
    }
    render_map(app->renderer, app->sprites, game_view_map(&app->game), &app->camera);
    render_dirt(app->renderer, game_view_dirt(&app->game), &app->camera);
    render_snow_cover(app->renderer, &app->game, &app->camera);
    render_blood(app->renderer, &app->game, &app->camera);
    render_gather_zones(app->renderer, &app->game, &app->camera, app->selected_gather_zone);
    render_stockpiles(app->renderer, &app->game, &app->camera, app->selected_stockpile);
    render_structures(app->renderer, &app->game, &app->camera);
    render_workstations(app->renderer, &app->game, &app->camera, app->selected_station);
    render_items(app->renderer, app->sprites, &app->game, &app->camera);
    render_world_sprites(app->renderer, app->sprites, &app->game, &app->camera, app->selected,
                         app->selected_count, app->selected_animal);
    if (app->game.fog_on) {
        render_fog(app->renderer, &app->game, &app->camera, &app->tuning);
    }
    if (markers) {
        render_ramp_markers(app->renderer, game_view_map(&app->game), &app->camera);
        render_forbidden_markers(app->renderer, &app->game, &app->camera);
        render_item_moves(app->renderer, &app->game, &app->camera);
        render_designations(app->renderer, &app->game, &app->camera);
    }
    render_daylight(app->renderer, &app->game, &app->camera);
    render_weather(app->renderer, &app->game, &app->camera, SDL_GetTicks());
}

/* ---- Outline of the area a setting reaches ------------------------------------ */

typedef struct StockpileArea {
    const Stockpile *stockpile;
    int z, width, height;
} StockpileArea;

static bool stockpile_area_contains(const void *area, int x, int y)
{
    const StockpileArea *a = area;
    return x >= 0 && y >= 0 && x < a->width && y < a->height &&
           stockpile_in_radius(a->stockpile, x, y, a->z);
}

typedef struct ReachArea {
    const uint8_t *mask;
    int width, height;
} ReachArea;

static bool reach_contains(const void *area, int x, int y)
{
    const ReachArea *a = area;
    return x >= 0 && y >= 0 && x < a->width && y < a->height && a->mask[y * a->width + x] != 0;
}

/* The tiles of the level shown a settler reaches on foot from the preview's workstation within
 * its cost, worked out again only when the start, the cost, the level or the map's ground
 * changed. NULL when there is no room. */
static const uint8_t *reach_mask(App *app, const Map *map, const AreaPreview *preview)
{
    const int level = app->camera.level;
    if (!app->reach_valid || app->reach_key.from.x != preview->from.x ||
        app->reach_key.from.y != preview->from.y || app->reach_key.from.z != preview->from.z ||
        app->reach_key.cost != preview->cost || app->reach_level != level ||
        app->reach_revision != map->topology_revision) {
        size_t size = (size_t)map->width * (size_t)map->height;
        if (app->reach_capacity < size) {
            uint8_t *grown = SDL_realloc(app->reach_mask, size);
            if (grown == NULL) {
                return NULL;
            }
            app->reach_mask = grown;
            app->reach_capacity = size;
        }
        pathfinder_reachable_on_level(&app->game.pathfinder, map, preview->from,
                                      (float)preview->cost, level, app->reach_mask);
        app->reach_key = *preview;
        app->reach_level = level;
        app->reach_revision = map->topology_revision;
        app->reach_valid = true;
    }
    return app->reach_mask;
}

/* Draws what a panel asked to be outlined on its last frame, then forgets it: the panel asks
 * again as long as the pointer is on its setting. */
static void draw_area_preview(App *app)
{
    const Map *map = &app->game.map;
    AreaPreview preview = app->area_preview;
    app->area_preview.kind = AREA_PREVIEW_NONE;
    if (preview.kind == AREA_PREVIEW_STOCKPILE) {
        const Stockpile *stockpile = stockpile_get(&app->game.stockpiles, preview.stockpile);
        if (stockpile != NULL) {
            StockpileArea area = {stockpile, app->camera.level, map->width, map->height};
            render_area_outline(app->renderer, &app->camera, map->width, map->height,
                                stockpile_area_contains, &area);
        }
    } else if (preview.kind == AREA_PREVIEW_REACH) {
        const uint8_t *mask = reach_mask(app, map, &preview);
        if (mask != NULL) {
            ReachArea area = {mask, map->width, map->height};
            render_area_outline(app->renderer, &app->camera, map->width, map->height,
                                reach_contains, &area);
        }
    }
}

static void render_frame(App *app)
{
    SDL_SetRenderDrawColor(app->renderer, 10, 10, 12, 255);
    SDL_RenderClear(app->renderer);
    render_world(app, app->screen == APP_SCREEN_GAME); /* No markers behind the title. */
    float mouse_x, mouse_y;
    SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    if (app->screen == APP_SCREEN_TITLE) {
        ui_begin_frame(&app->ui, mouse_x, mouse_y, (buttons & SDL_BUTTON_LMASK) != 0,
                       app->camera.viewport_width, app->camera.viewport_height);
        title_screen_draw(app);
        ui_end_frame(&app->ui);
        text_renderer_end_frame(app->text);
        if (app->title_text != NULL && app->title_subtext != NULL) {
            text_renderer_end_frame(app->title_text);
            text_renderer_end_frame(app->title_subtext);
        }
        return;
    }
    game_count_stock(&app->game, app->stock);
    if (app->show_temperature) {
        render_temperature(app->renderer, &app->game, &app->camera, &app->ui.theme);
    }
    if (app->show_light) {
        render_light(app->renderer, &app->game, &app->camera, &app->ui.theme);
    }
    if (app->show_moisture) {
        render_moisture(app->renderer, &app->game, &app->camera, &app->ui.theme);
    }
    if (app->show_vision && app->game.vision_on) {
        render_vision(app->renderer, &app->game, &app->camera,
                      app->selected_count > 0 ? (int)app->selected[0].index : -1);
    }
    draw_area_preview(app);
    if (app->hover_valid) {
        render_tile_outline(app->renderer, &app->camera, app->hover_x, app->hover_y, HOVER_COLOR);
    }
    if (app->tile_selected && app->selected_thing == THING_ITEM) {
        ItemPick pick = app->selected_item;
        const Item *item = game_pick_item(&app->game, &pick);
        if (item != NULL && pick.z == app->camera.level) {
            render_item_selection(app->renderer, app->sprites, &app->camera, pick.x, pick.y, item);
        }
    } else if (app->tile_selected && app->selected_thing == THING_ITEM_BOX) {
        for (int i = 0; i < app->selected_box_item_count; i++) {
            ItemPick pick = app->selected_box_items[i];
            const Item *item = game_pick_item(&app->game, &pick);
            if (item != NULL && pick.z == app->camera.level) {
                render_item_selection(app->renderer, app->sprites, &app->camera, pick.x, pick.y,
                                      item);
            }
        }
    } else if (app->tile_selected && app->selected_thing == THING_PLANT_BOX) {
        for (int i = 0; i < app->selected_plant_count; i++) {
            TilePos at = app->selected_plants[i];
            if (at.z == app->camera.level ||
                map_view_level(game_view_map(&app->game), at.x, at.y, app->camera.level) == at.z) {
                render_tile_selection(app->renderer, &app->camera, at.x, at.y);
            }
        }
    } else if (app->tile_selected) {
        render_tile_selection(app->renderer, &app->camera, app->selected_tile_x,
                              app->selected_tile_y);
    }
    hud_draw_world_labels(app);
    if (app->tool == APP_TOOL_BUILD) {
        if (app->hover_valid) {
            render_blueprint_preview(app->renderer, &app->game, &app->camera, app->build_type,
                                     app->hover_x, app->hover_y);
        }
    } else if (app->tool == APP_TOOL_STRUCTURE) {
        if (structure_is_roof(app->structure_type)) {
            render_roof_support(app->renderer, &app->game, &app->camera);
        }
        if (app->left_button_down) {
            int x0, y0, x1, y1;
            tool_rect(app, mouse_x, mouse_y, &x0, &y0, &x1, &y1);
            render_structure_preview(app->renderer, &app->game, &app->camera, app->structure_type,
                                     app->structure_direction, x0, y0, x1, y1);
        } else if (app->hover_valid) {
            render_structure_preview(app->renderer, &app->game, &app->camera, app->structure_type,
                                     app->structure_direction, app->hover_x, app->hover_y,
                                     app->hover_x, app->hover_y);
        }
    } else if (tool_drags_area(app->tool) && app->left_button_down) {
        int x0, y0, x1, y1;
        tool_rect(app, mouse_x, mouse_y, &x0, &y0, &x1, &y1);
        bool remove = zone_tool_removes(app);
        if (app->tool == APP_TOOL_STOCKPILE) {
            render_stockpile_area(app->renderer, &app->camera, x0, y0, x1, y1, remove);
        } else if (app->tool == APP_TOOL_GATHER) {
            render_gather_area(app->renderer, &app->camera, x0, y0, x1, y1, remove);
        } else if (app->tool == APP_TOOL_FORBID) {
            render_forbid_area(app->renderer, &app->camera, x0, y0, x1, y1, !app->forbid_allows);
        } else {
            render_designation_area(app->renderer, &app->camera, x0, y0, x1, y1, app->tool_type);
        }
    } else if (app->box_selecting) {
        float start_x, start_y;
        camera_world_to_screen(&app->camera, app->drag_world_x, app->drag_world_y, &start_x,
                               &start_y);
        render_selection_box(app->renderer, start_x, start_y, mouse_x, mouse_y);
    }
    ui_begin_frame(&app->ui, mouse_x, mouse_y, (buttons & SDL_BUTTON_LMASK) != 0,
                   app->camera.viewport_width, app->camera.viewport_height);
    hud_draw(app);
    ui_end_frame(&app->ui);

    text_renderer_end_frame(app->text);
}

/* Draws a frame and shows it; returns the seconds spent drawing (the draw
 * calls carried out: SDL queues them until a flush), the wait for the screen
 * left out. */
static double draw_frame(App *app)
{
    uint64_t begin = SDL_GetTicksNS();
    render_frame(app);
    SDL_FlushRenderer(app->renderer);
    double seconds = (double)(SDL_GetTicksNS() - begin) / 1e9;
    SDL_RenderPresent(app->renderer);
    return seconds;
}

static int compare_doubles(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* --bench-render: the simulation paused, a few fixed views over the first
 * colonist (the start level and higher up, where the levels below show
 * through, then zoomed out), each drawn as fast as possible; prints the time
 * of a frame (from one shown to the next) and of its drawing, then quits. */
static void bench_render(App *app)
{
    static const struct {
        int levels_up;
        float zoom;
        const char *name;
    } VIEWS[] = {
        {0, 1.0f, "start level"},   {1, 1.0f, "1 level up"},       {5, 1.0f, "5 levels up"},
        {10, 1.0f, "10 levels up"}, {0, 0.4f, "start level, far"}, {5, 0.4f, "5 levels up, far"},
    };
    const int frames = app->bench_render_frames;
    const int warmup = frames < 10 ? frames : 10;
    double *frame_times = SDL_malloc((size_t)frames * sizeof(double));
    double *draw_times = SDL_malloc((size_t)frames * sizeof(double));
    if (frame_times == NULL || draw_times == NULL) {
        LOG_ERROR("Out of memory for the render benchmark");
        SDL_free(frame_times);
        SDL_free(draw_times);
        return;
    }
    const Camera start = app->camera;
    printf("render benchmark: seed %" SDL_PRIu64 ", %d frames per view, renderer %s\n",
           app->game.seed, frames, SDL_GetRendererName(app->renderer));
    printf("  %-18s %10s %10s %10s %10s\n", "view", "frame avg", "frame p99", "draw avg",
           "draw p99");
    double total = 0.0;
    for (size_t view = 0; view < sizeof(VIEWS) / sizeof(VIEWS[0]) && app->running; view++) {
        app->camera = start;
        int level = start.level + VIEWS[view].levels_up;
        app->camera.level = level < app->game.map.depth ? level : app->game.map.depth - 1;
        update_camera(app, 0.0f); /* The viewport. */
        camera_zoom_at(&app->camera, VIEWS[view].zoom, app->camera.viewport_width * 0.5f,
                       app->camera.viewport_height * 0.5f);
        uint64_t previous = 0;
        for (int i = -warmup; i < frames && app->running; i++) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    app->running = false;
                }
            }
            double draw = draw_frame(app);
            uint64_t now = SDL_GetTicksNS();
            if (i >= 0) {
                frame_times[i] = (double)(now - previous) / 1e9;
                draw_times[i] = draw;
                total += frame_times[i];
            }
            previous = now;
        }
        if (!app->running) {
            break;
        }
        double frame_sum = 0.0, draw_sum = 0.0;
        for (int i = 0; i < frames; i++) {
            frame_sum += frame_times[i];
            draw_sum += draw_times[i];
        }
        qsort(frame_times, (size_t)frames, sizeof(double), compare_doubles);
        qsort(draw_times, (size_t)frames, sizeof(double), compare_doubles);
        int p99 = (int)((double)frames * 0.99);
        p99 = p99 < frames ? p99 : frames - 1;
        printf("  %-18s %7.2f ms %7.2f ms %7.2f ms %7.2f ms\n", VIEWS[view].name,
               frame_sum * 1e3 / frames, frame_times[p99] * 1e3, draw_sum * 1e3 / frames,
               draw_times[p99] * 1e3);
    }
    printf("  total %.2f s\n", total);
    fflush(stdout);
    SDL_free(frame_times);
    SDL_free(draw_times);
}

void app_run(App *app)
{
    if (app->bench_render_frames > 0) {
        bench_render(app);
        return;
    }
    uint64_t previous = SDL_GetTicksNS();
    while (app->running) {
        uint64_t now = SDL_GetTicksNS();
        double dt = (double)(now - previous) / 1e9;
        previous = now;
        if (dt > 0.25) {
            dt = 0.25; /* Debugger pause or window drag: do not jump ahead. */
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            handle_event(app, &event);
        }
        if (app->pending_title) {
            return_to_title(app);
        }
        if (app->screen == APP_SCREEN_TITLE && app->new_game_stage == NEW_GAME_STAGE_GENERATE) {
            start_new_game(app);
            previous = SDL_GetTicksNS(); /* Making the world is not time played. */
        }
        if (!app->running) {
            break;
        }
        if (app->screen == APP_SCREEN_TITLE) {
            title_screen_update(app, dt);
            update_fps(app, dt);
            app->render_elapsed += draw_frame(app);
            continue;
        }
        if (app->pending_scenario > 0) {
            load_pending_scenario(app);
            if (!app->running) {
                break;
            }
        }
        update_camera(app, (float)dt);
        if (!app->show_game_menu && !app->show_settings) {
            game_update(&app->game, dt); /* The game menu and the settings pause the game. */
        }
        hud_update_notices(app, dt);
        prune_selection(app);
        prune_stockpile_selection(app);
        prune_station_selection(app);
        prune_gather_zone_selection(app);
        prune_tile_selection(app);
        prune_animal_selection(app);
        prune_zone_edit(app);
        update_follow(app);
        update_fps(app, dt);
        app->render_elapsed += draw_frame(app);
    }
}
