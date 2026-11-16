#include "game/app.h"

#include "core/i18n.h"
#include "core/log.h"
#include "core/util.h"
#include "game/hud.h"
#include "game/render.h"
#include "game/stockpile_panel.h"
#include "game/work_panel.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <math.h>

#define APP_WINDOW_WIDTH 1280
#define APP_WINDOW_HEIGHT 720
#define APP_FONT_FILE "fonts/DejaVuSans.ttf"
#define APP_FONT_SIZE 15.0f
#define APP_INITIAL_ZOOM 48.0f
#define APP_PAN_SPEED 900.0f /* Pixels per second. */
#define APP_PAN_FAST_FACTOR 2.5f
#define APP_ZOOM_STEP 1.15f
/* Levels below the one shown that show through open space (deeper ones are
 * too dark to see). */
#define APP_LEVELS_SHOWN_BELOW 6
/* Height of a colonist's body above its position, in tiles (for clicks). */
#define APP_PICK_BODY_OFFSET 0.7f
#define APP_DRAG_THRESHOLD 5.0f /* Pixels before a click becomes a box selection. */

static const SDL_Color HOVER_COLOR = {255, 255, 255, 200};

const char *app_key_name(SDL_Scancode key)
{
    return SDL_GetKeyName(SDL_GetKeyFromScancode(key, SDL_KMOD_NONE, false));
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
    case APP_TOOL_GATHER: return app->zone_remove == (type == APP_ZONE_REMOVE);
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
            app->zone_remove = type == APP_ZONE_REMOVE;
        }
    }
    app->left_button_down = false;
    app->box_selecting = false;
}

static void toggle_tool(App *app, AppTool tool, DesignationType type)
{
    app_toggle_tool(app, tool, (int)type);
}

/* Build tool: crafting spot, then the next kinds of station, then off. */
static void cycle_build_tool(App *app)
{
    if (app->tool != APP_TOOL_BUILD) {
        app->tool = APP_TOOL_BUILD;
        app->build_type = (WorkstationType)0;
    } else if (app->build_type + 1 < WORKSTATION_TYPE_COUNT) {
        app->build_type = (WorkstationType)(app->build_type + 1);
    } else {
        app->tool = APP_TOOL_NONE;
    }
    app->left_button_down = false;
    app->box_selecting = false;
}

/* Colonists, a station or a zone: one kind of selection at a time. */
static void clear_selection(App *app)
{
    app->selected_count = 0;
    app->selected_stockpile = STOCKPILE_NONE;
    app->selected_station = WORKSTATION_NONE;
    app->selected_gather_zone = GATHER_ZONE_NONE;
    app->structure_selected = false;
}

/* The construction site on a tile. */
static const ConstructionSite *site_at(const App *app, int x, int y, int z)
{
    const Constructions *sites = &app->game.constructions;
    return construction_get(sites, construction_at(sites, x, y, z));
}

/* Type of what is built (a structure, or the roof the tile is), else of the
 * construction site, on a tile (STRUCTURE_NONE for a workstation's site). */
static StructureType structure_type_at(const App *app, int x, int y, int z)
{
    StructureType built = map_structure(&app->game.map, x, y, z);
    if (built == STRUCTURE_NONE) {
        built = map_roof(&app->game.map, x, y, z);
    }
    if (built != STRUCTURE_NONE) {
        return built;
    }
    const ConstructionSite *site = site_at(app, x, y, z);
    return site != NULL ? (StructureType)site->type : STRUCTURE_NONE;
}

/* Whether a structure, a roof or a construction site (a workstation's too)
 * lies on a tile: what a click selects as a structure. */
static bool structure_or_site_at(const App *app, int x, int y, int z)
{
    return structure_type_at(app, x, y, z) != STRUCTURE_NONE || site_at(app, x, y, z) != NULL;
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

static void select_structure(App *app, int x, int y, int z)
{
    clear_selection(app);
    app->structure_selected = structure_or_site_at(app, x, y, z);
    app->selected_structure_x = x;
    app->selected_structure_y = y;
    app->selected_structure_z = z;
}

/* Forgets the selected structure once it is gone (built over, taken apart). */
static void prune_structure_selection(App *app)
{
    if (app->structure_selected &&
        !structure_or_site_at(app, app->selected_structure_x, app->selected_structure_y,
                              app->selected_structure_z)) {
        app->structure_selected = false;
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

static uint64_t random_seed(void)
{
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    return (uint64_t)now ^ SDL_GetPerformanceCounter();
}

static void update_window_title(App *app)
{
    SDL_SetWindowTitle(app->window, tr("game.title"));
}

bool app_init(App *app, const CliOptions *options)
{
    SDL_zerop(app);
    app->structure_direction = DIRECTION_SOUTH; /* Beds' heads at the top. */
    log_set_sink(sdl_log_sink, NULL);
    SDL_SetAppMetadata("Colony Sim", COLONY_VERSION, "io.github.colony-sim");

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

    if (!localization_init(&app->localization, app->assets_dir, options->language)) {
        return false;
    }
    app->localization_ready = true;

    if (!SDL_CreateWindowAndRenderer(tr("game.title"), APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT,
                                     SDL_WINDOW_RESIZABLE, &app->window, &app->renderer)) {
        LOG_ERROR("Cannot create the window: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(app->renderer, 1);
    LOG_INFO("Renderer: %s", SDL_GetRendererName(app->renderer));
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    char font_path[1200];
    SDL_snprintf(font_path, sizeof(font_path), "%s%s", app->assets_dir, APP_FONT_FILE);
    app->text = text_renderer_create(app->renderer, font_path, APP_FONT_SIZE);
    if (app->text == NULL) {
        return false;
    }
    app->sprites = sprites_create(app->renderer, app->assets_dir);
    if (app->sprites == NULL) {
        return false;
    }

    NewGameParams params;
    game_default_params(&params);
    params.world.seed = options->has_seed ? options->seed : random_seed();
    if (options->map_size > 0) {
        params.world.width = options->map_size;
        params.world.height = options->map_size;
    }
    if (!game_init(&app->game, &params)) {
        LOG_ERROR("World generation failed");
        return false;
    }
    app->game_ready = true;
    LOG_INFO("World generated: seed %" SDL_PRIu64 ", %dx%d tiles, %u colonists",
             params.world.seed, params.world.width, params.world.height,
             app->game.colonist_pool.count);

    /* Start centered on the first colonist (or the map center if there is none). */
    float start_x = (float)params.world.width * 0.5f;
    float start_y = (float)params.world.height * 0.5f;
    const Colonist *first = game_colonist_const(&app->game, game_colonist_id_at(&app->game, 0));
    if (first != NULL) {
        colonist_position(first, &start_x, &start_y);
    }
    camera_init(&app->camera, start_x, start_y, APP_INITIAL_ZOOM);
    app->camera.level = first != NULL ? first->z
                                      : map_ground_level(&app->game.map, (int)start_x,
                                                         (int)start_y);
    ui_init(&app->ui, app->renderer, app->text);
    app->selected_stockpile = STOCKPILE_NONE;
    app->selected_station = WORKSTATION_NONE;
    app->selected_gather_zone = GATHER_ZONE_NONE;
    app->running = true;
    return true;
}

void app_shutdown(App *app)
{
    if (app->game_ready) {
        game_shutdown(&app->game);
    }
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
    case APP_WINDOW_COLONIST: app->selected_count = 0; break;
    case APP_WINDOW_STATION: app->selected_station = WORKSTATION_NONE; break;
    case APP_WINDOW_STOCKPILE: app->selected_stockpile = STOCKPILE_NONE; break;
    case APP_WINDOW_GATHER_ZONE: app->selected_gather_zone = GATHER_ZONE_NONE; break;
    case APP_WINDOW_WORK: app->show_work_panel = false; break;
    case APP_WINDOW_HELP: app->show_help = false; break;
    case APP_WINDOW_DEBUG: app->show_debug_menu = false; break;
    default: break;
    }
}

/* Escape (or a right click) steps back: cancels the drag under way, leaves
 * the tool, closes the build menu, then the window in front (closing an
 * inspector clears the selection), then forgets a selection without window.
 * It never quits: the window's close button does. */
static void step_back(App *app)
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
    } else {
        app->structure_selected = false;
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
static void build_like_selection(App *app)
{
    const Workstation *station =
        workstation_get_const(&app->game.workstations, app->selected_station);
    StructureType structure =
        app->structure_selected
            ? structure_type_at(app, app->selected_structure_x, app->selected_structure_y,
                                app->selected_structure_z)
            : STRUCTURE_NONE;
    /* A workstation's site: another of that workstation. */
    const ConstructionSite *site =
        app->structure_selected ? site_at(app, app->selected_structure_x,
                                          app->selected_structure_y, app->selected_structure_z)
                                : NULL;
    WorkstationType site_station =
        site != NULL ? construction_workstation(site) : WORKSTATION_TYPE_COUNT;
    if (app->selected_stockpile != STOCKPILE_NONE) {
        clear_selection(app);
        set_tool(app, APP_TOOL_STOCKPILE, APP_ZONE_ADD);
    } else if (app->selected_gather_zone != GATHER_ZONE_NONE) {
        clear_selection(app);
        set_tool(app, APP_TOOL_GATHER, APP_ZONE_ADD);
    } else if (station != NULL || site_station != WORKSTATION_TYPE_COUNT) {
        WorkstationType type =
            station != NULL ? (WorkstationType)station->type : site_station;
        clear_selection(app);
        set_tool(app, APP_TOOL_BUILD, (int)type);
    } else if (structure != STRUCTURE_NONE) {
        app->structure_direction =
            structure_direction_at(app, app->selected_structure_x, app->selected_structure_y,
                                   app->selected_structure_z);
        clear_selection(app);
        set_tool(app, APP_TOOL_STRUCTURE, (int)structure);
    }
}

/* Shows another level (clamped to the map's). */
static void set_level(App *app, int level)
{
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
    case APP_KEY_BUILD_LIKE: build_like_selection(app); return;
    case SDL_SCANCODE_1:
    case SDL_SCANCODE_KP_1: game_clock_set_speed(&app->game.clock, GAME_SPEED_NORMAL); return;
    case SDL_SCANCODE_2:
    case SDL_SCANCODE_KP_2: game_clock_set_speed(&app->game.clock, GAME_SPEED_FAST); return;
    case SDL_SCANCODE_3:
    case SDL_SCANCODE_KP_3: game_clock_set_speed(&app->game.clock, GAME_SPEED_FASTEST); return;
    case APP_KEY_BACK: step_back(app); return;
    case APP_KEY_HELP: app->show_help = !app->show_help; return;
    case APP_KEY_DEBUG_MENU: app->show_debug_menu = !app->show_debug_menu; return;
    case APP_KEY_TEMPERATURE: app->show_temperature = !app->show_temperature; return;
    case APP_KEY_LEVEL_UP: set_level(app, app->camera.level + 1); return;
    case APP_KEY_LEVEL_DOWN: set_level(app, app->camera.level - 1); return;
    case APP_KEY_MINE: toggle_tool(app, APP_TOOL_DESIGNATE, DESIGNATION_MINE); return;
    case APP_KEY_GATHER: app_toggle_tool(app, APP_TOOL_GATHER, APP_ZONE_ADD); return;
    case APP_KEY_CANCEL: toggle_tool(app, APP_TOOL_DESIGNATE, DESIGNATION_NONE); return;
    case APP_KEY_STOCKPILE: app_toggle_tool(app, APP_TOOL_STOCKPILE, APP_ZONE_ADD); return;
    case APP_KEY_WORK: app->show_work_panel = !app->show_work_panel; return;
    case APP_KEY_BUILD: cycle_build_tool(app); return;
    case APP_KEY_ROTATE:
        app->structure_direction = direction_turn(app->structure_direction);
        return;
    case APP_KEY_DEBUG:
        app->show_debug = !app->show_debug;
        app->debug_tree_count = map_count_plants(&app->game.map, PLANT_TREE);
        return;
    case APP_KEY_LANGUAGE:
        localization_cycle(&app->localization);
        update_window_title(app);
        return;
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

/* Click: select the colonist under the cursor, or else the workstation, the
 * stockpile zone or the gathering zone there. Shift adds colonists to / removes them from the
 * selection. */
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
    if (!additive) {
        app->selected_count = 0;
    }
    app->selected_stockpile = STOCKPILE_NONE;
    app->selected_station = WORKSTATION_NONE;
    app->selected_gather_zone = GATHER_ZONE_NONE;
    app->structure_selected = false;
    if (entity_id_is_none(id)) {
        if (!additive) {
            int x = (int)floorf(world_x), y = (int)floorf(world_y);
            int station = workstation_at(&app->game.workstations, x, y, z);
            int stockpile = stockpile_at(&app->game.stockpiles, x, y, z);
            if (station != WORKSTATION_NONE) {
                select_station(app, station);
            } else if (structure_or_site_at(app, x, y, z)) {
                select_structure(app, x, y, z);
            } else if (stockpile != STOCKPILE_NONE) {
                select_stockpile(app, stockpile);
            } else {
                select_gather_zone(app, gather_zone_at(&app->game.gather_zones, x, y, z));
            }
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

static void select_in_box(App *app, float x0, float y0, float x1, float y1, bool additive)
{
    float wx0, wy0, wx1, wy1;
    camera_screen_to_world(&app->camera, x0, y0, &wx0, &wy0);
    camera_screen_to_world(&app->camera, x1, y1, &wx1, &wy1);
    EntityId found[COLONIST_MAX];
    int count = game_colonists_in_rect(&app->game, wx0, wy0, wx1, wy1, app->camera.level, found,
                                       COLONIST_MAX);
    if (!additive) {
        app->selected_count = 0;
    }
    app->selected_stockpile = STOCKPILE_NONE;
    app->selected_station = WORKSTATION_NONE;
    app->selected_gather_zone = GATHER_ZONE_NONE;
    for (int i = 0; i < count; i++) {
        select_add(app, found[i]);
    }
}

static void screen_to_tile(const App *app, float screen_x, float screen_y, int *x, int *y)
{
    float world_x, world_y;
    camera_screen_to_world(&app->camera, screen_x, screen_y, &world_x, &world_y);
    *x = (int)floorf(world_x);
    *y = (int)floorf(world_y);
}

/* Tool drag rectangle in tiles, from where the button was pressed to the cursor. */
static void tool_rect(const App *app, float screen_x, float screen_y, int *x0, int *y0, int *x1,
                      int *y1)
{
    screen_to_tile(app, app->drag_start_x, app->drag_start_y, x0, y0);
    screen_to_tile(app, screen_x, screen_y, x1, y1);
}

/* Whether a zone tool drag removes tiles: in the removal mode, or with Shift. */
static bool zone_tool_removes(const App *app)
{
    return app->zone_remove || (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
}

/* Stockpile tool release: adds the rectangle to the selected stockpile and
 * those it overlaps (merged into one), or creates a new one, which becomes
 * selected; or removes zone tiles. */
static void apply_stockpile_tool(App *app, float screen_x, float screen_y)
{
    const int z = app->camera.level; /* The level shown. */
    Stockpiles *stockpiles = &app->game.stockpiles;
    int x0, y0, x1, y1;
    tool_rect(app, screen_x, screen_y, &x0, &y0, &x1, &y1);
    if (zone_tool_removes(app)) {
        stockpiles_remove_rect(stockpiles, x0, y0, x1, y1, z);
        prune_stockpile_selection(app);
    } else {
        select_stockpile(app, game_add_stockpile_rect(&app->game, x0, y0, x1, y1, z,
                                                      app->selected_stockpile));
    }
}

/* Gathering zone tool release: like the stockpile tool. */
static void apply_gather_tool(App *app, float screen_x, float screen_y)
{
    const int z = app->camera.level; /* The level shown. */
    GatherZones *zones = &app->game.gather_zones;
    int x0, y0, x1, y1;
    tool_rect(app, screen_x, screen_y, &x0, &y0, &x1, &y1);
    if (zone_tool_removes(app)) {
        gather_zones_remove_rect(zones, x0, y0, x1, y1, z);
        prune_gather_zone_selection(app);
    } else {
        select_gather_zone(app, game_add_gather_zone_rect(&app->game, x0, y0, x1, y1, z,
                                                          app->selected_gather_zone));
    }
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
                step_back(app);
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
        return;
    }
    if (!app->left_button_down) {
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
            game_place_workstation(&app->game, app->build_type, x, y, z);
        }
    } else if (app->tool == APP_TOOL_STRUCTURE) {
        int x0, y0, x1, y1;
        tool_rect(app, button->x, button->y, &x0, &y0, &x1, &y1);
        if (game_structure_buildable(&app->game, app->structure_type)) {
            game_place_structure_rect(&app->game, app->structure_type, app->structure_direction,
                                      x0, y0, x1, y1, z);
        }
    } else if (app->tool == APP_TOOL_STOCKPILE) {
        apply_stockpile_tool(app, button->x, button->y);
    } else if (app->tool == APP_TOOL_GATHER) {
        apply_gather_tool(app, button->x, button->y);
    } else if (app->box_selecting) {
        select_in_box(app, app->drag_start_x, app->drag_start_y, button->x, button->y, additive);
    } else {
        select_at_point(app, button->x, button->y, additive);
    }
    app->left_button_down = false;
    app->box_selecting = false;
}

/* ---- Events ---------------------------------------------------------------- */

static void handle_event(App *app, const SDL_Event *event)
{
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
        if (SDL_GetModState() & SDL_KMOD_SHIFT) { /* Up and down the levels. */
            app->level_wheel += steps;
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
        }
        if (app->left_button_down && !app->box_selecting) {
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
        app->fps_elapsed = 0.0;
        app->fps_frames = 0;
    }
}

static void render_frame(App *app)
{
    SDL_SetRenderDrawColor(app->renderer, 10, 10, 12, 255);
    SDL_RenderClear(app->renderer);

    /* The levels below, through open space: the deepest first. */
    if (render_below_begin(app->renderer, &app->game.map, &app->camera)) {
        int lowest = app->camera.level - APP_LEVELS_SHOWN_BELOW;
        for (int z = lowest > 0 ? lowest : 0; z < app->camera.level; z++) {
            Camera below = app->camera;
            below.level = z;
            render_map(app->renderer, &app->game.map, &below);
            render_snow_cover(app->renderer, &app->game, &below);
            render_structures(app->renderer, &app->game, &below);
            render_workstations(app->renderer, &app->game, &below, WORKSTATION_NONE);
            render_items(app->renderer, &app->game, &below);
            render_world_sprites(app->renderer, app->sprites, &app->game, &below, NULL, 0);
            render_below_shade(app->renderer);
        }
        render_below_end(app->renderer);
    }
    render_map(app->renderer, &app->game.map, &app->camera);
    render_dirt(app->renderer, &app->game.dirt, &app->camera);
    render_snow_cover(app->renderer, &app->game, &app->camera);
    render_gather_zones(app->renderer, &app->game, &app->camera, app->selected_gather_zone);
    render_stockpiles(app->renderer, &app->game, &app->camera, app->selected_stockpile);
    render_structures(app->renderer, &app->game, &app->camera);
    render_workstations(app->renderer, &app->game, &app->camera, app->selected_station);
    render_items(app->renderer, &app->game, &app->camera);
    render_world_sprites(app->renderer, app->sprites, &app->game, &app->camera, app->selected,
                         app->selected_count);
    render_designations(app->renderer, &app->game, &app->camera);
    hud_draw_item_counts(app);
    render_daylight(app->renderer, &app->game, &app->camera);
    render_weather(app->renderer, &app->game, &app->camera, SDL_GetTicks());
    if (app->show_temperature) {
        render_temperature(app->renderer, &app->game, &app->camera);
    }
    if (app->hover_valid) {
        render_tile_outline(app->renderer, &app->camera, app->hover_x, app->hover_y,
                            HOVER_COLOR);
    }
    if (app->structure_selected) {
        render_tile_selection(app->renderer, &app->camera, app->selected_structure_x,
                              app->selected_structure_y);
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
            float mouse_x, mouse_y;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            int x0, y0, x1, y1;
            tool_rect(app, mouse_x, mouse_y, &x0, &y0, &x1, &y1);
            render_structure_preview(app->renderer, &app->game, &app->camera,
                                     app->structure_type, app->structure_direction, x0, y0, x1,
                                     y1);
        } else if (app->hover_valid) {
            render_structure_preview(app->renderer, &app->game, &app->camera,
                                     app->structure_type, app->structure_direction, app->hover_x,
                                     app->hover_y, app->hover_x, app->hover_y);
        }
    } else if (app->tool != APP_TOOL_NONE && app->left_button_down) {
        float mouse_x, mouse_y;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        int x0, y0, x1, y1;
        tool_rect(app, mouse_x, mouse_y, &x0, &y0, &x1, &y1);
        bool remove = zone_tool_removes(app);
        if (app->tool == APP_TOOL_STOCKPILE) {
            render_stockpile_area(app->renderer, &app->camera, x0, y0, x1, y1, remove);
        } else if (app->tool == APP_TOOL_GATHER) {
            render_gather_area(app->renderer, &app->camera, x0, y0, x1, y1, remove);
        } else {
            render_designation_area(app->renderer, &app->camera, x0, y0, x1, y1,
                                    app->tool_type);
        }
    } else if (app->box_selecting) {
        float mouse_x, mouse_y;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        render_selection_box(app->renderer, app->drag_start_x, app->drag_start_y, mouse_x,
                             mouse_y);
    }
    float mouse_x, mouse_y;
    SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    ui_begin_frame(&app->ui, mouse_x, mouse_y, (buttons & SDL_BUTTON_LMASK) != 0,
                   app->camera.viewport_width, app->camera.viewport_height);
    hud_draw(app);
    ui_end_frame(&app->ui);

    text_renderer_end_frame(app->text);
    SDL_RenderPresent(app->renderer);
}

void app_run(App *app)
{
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
        update_camera(app, (float)dt);
        game_update(&app->game, dt);
        hud_update_notices(app, dt);
        prune_selection(app);
        prune_stockpile_selection(app);
        prune_station_selection(app);
        prune_gather_zone_selection(app);
        prune_structure_selection(app);
        update_fps(app, dt);
        render_frame(app);
    }
}
