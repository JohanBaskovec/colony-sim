/*
 * The application: owns the SDL window, the renderer, the localization state
 * and the running Game, and drives the main loop.
 */
#ifndef COLONY_GAME_APP_H
#define COLONY_GAME_APP_H

#include "core/camera.h"
#include "core/cli.h"
#include "core/game.h"
#include "game/localization.h"
#include "game/sprites.h"
#include "game/text.h"
#include "game/ui.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

/* What a left click/drag on the map does. */
typedef enum AppTool {
    APP_TOOL_NONE,      /* Select colonists, a workstation or a stockpile. */
    APP_TOOL_DESIGNATE, /* Place orders of `tool_type` (DESIGNATION_NONE: remove orders). */
    /* Zone tools: create, extend or merge zones; remove zone tiles with Shift
     * or in the removal mode (`zone_remove`, tool type APP_ZONE_REMOVE). */
    APP_TOOL_STOCKPILE,
    APP_TOOL_BUILD,     /* Place a blueprint of `build_type` on the clicked tile. */
    APP_TOOL_GATHER,
    APP_TOOL_STRUCTURE, /* Plan structures of `structure_type` (click, or drag a rectangle). */
} AppTool;

/*
 * Keyboard controls. Every one is a physical position (scancode, named after
 * the QWERTY key there), so that the controls are laid out the same on any
 * keyboard: the camera moves with WASD on QWERTY, which is ZQSD on AZERTY.
 * Names shown to the player come from the active layout (app_key_name()).
 */
#define APP_KEY_UP SDL_SCANCODE_W
#define APP_KEY_LEFT SDL_SCANCODE_A
#define APP_KEY_DOWN SDL_SCANCODE_S
#define APP_KEY_RIGHT SDL_SCANCODE_D
#define APP_KEY_BUILD_LIKE SDL_SCANCODE_Q /* Build another of what is selected. */
#define APP_KEY_BUILD SDL_SCANCODE_E      /* Workstation blueprints, one kind after the other. */
#define APP_KEY_GATHER SDL_SCANCODE_R
#define APP_KEY_WORK SDL_SCANCODE_T
#define APP_KEY_ROTATE SDL_SCANCODE_F /* Turns the structure being planned (beds). */
#define APP_KEY_STOCKPILE SDL_SCANCODE_P
#define APP_KEY_LANGUAGE SDL_SCANCODE_L
#define APP_KEY_MINE SDL_SCANCODE_M
#define APP_KEY_CANCEL SDL_SCANCODE_X
#define APP_KEY_PAUSE SDL_SCANCODE_SPACE
#define APP_KEY_BACK SDL_SCANCODE_ESCAPE
#define APP_KEY_HELP SDL_SCANCODE_F1
#define APP_KEY_DEBUG SDL_SCANCODE_F3
#define APP_KEY_TEMPERATURE SDL_SCANCODE_H /* Temperature view on or off. */

/* Types of the zone tools (see app_toggle_tool()). */
#define APP_ZONE_ADD 0
#define APP_ZONE_REMOVE 1

/* Windows (game/ui.h ids). */
typedef enum AppWindow {
    APP_WINDOW_NONE,
    APP_WINDOW_COLONIST, /* Inspector of the selected colonist(s). */
    APP_WINDOW_STATION,
    APP_WINDOW_STOCKPILE,
    APP_WINDOW_GATHER_ZONE,
    APP_WINDOW_WORK, /* Work priorities grid. */
    APP_WINDOW_HELP, /* Controls. */
    APP_WINDOW_COUNT
} AppWindow;

/* Second level of the build menu (game/menu_bar.h). */
typedef enum BuildCategory {
    BUILD_CATEGORY_NONE,
    BUILD_CATEGORY_STATIONS,
    BUILD_CATEGORY_STRUCTURES,
    BUILD_CATEGORY_ZONES,
    BUILD_CATEGORY_ORDERS,
    BUILD_CATEGORY_COUNT
} BuildCategory;

/* Notices on screen at once, and how long each one stays (real seconds). */
#define APP_NOTICES_SHOWN 5
#define APP_NOTICE_SECONDS 15.0

typedef struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TextRenderer *text;
    Sprites *sprites;
    Ui ui;
    Localization localization;
    Game game;
    Camera camera;
    char assets_dir[1024]; /* Ends with a path separator. */

    bool sdl_initialized;
    bool ttf_initialized;
    bool localization_ready;
    bool game_ready;
    bool running;
    bool show_debug;

    /* Selection is UI state: it lives here, not in the simulation. */
    EntityId selected[COLONIST_MAX];
    int selected_count;
    bool left_button_down;
    bool box_selecting; /* Left button dragged far enough to become a box. */
    float drag_start_x; /* Screen coordinates where the left button was pressed. */
    float drag_start_y;

    /* Selected stockpile zone (STOCKPILE_NONE if none), exclusive with the
     * colonist selection. The number guards against a reused index. */
    int selected_stockpile;
    int selected_stockpile_number;
    /* Selected workstation (WORKSTATION_NONE if none), exclusive with the
     * other selections; same guard. */
    int selected_station;
    int selected_station_number;
    /* Selected gathering zone (GATHER_ZONE_NONE if none), same rules. */
    int selected_gather_zone;
    int selected_gather_zone_number;
    /* Selected structure or construction site (its tile), same rules. */
    bool structure_selected;
    int selected_structure_x, selected_structure_y;
    /* A right click on the map (not dragged to pan) steps back like Escape. */
    bool right_click_pending;
    float right_down_x, right_down_y;

    /* Work priorities grid (game/work_panel.h) and controls help. */
    bool show_work_panel;
    bool show_help;
    bool show_temperature; /* The temperature of each tile over the map. */
    /* Build menu (game/menu_bar.h): open, and which category is shown. */
    bool build_menu_open;
    BuildCategory build_category;
    /* A middle/right button press that started on a panel does not pan the view. */
    bool pan_blocked;

    AppTool tool;
    DesignationType tool_type; /* For APP_TOOL_DESIGNATE. */
    bool zone_remove;          /* For the zone tools: drags remove tiles from zones. */
    WorkstationType build_type; /* For APP_TOOL_BUILD. */
    StructureType structure_type; /* For APP_TOOL_STRUCTURE. */
    Direction structure_direction; /* For APP_TOOL_STRUCTURE: where a bed's foot goes. */

    bool hover_valid; /* Whether the mouse is over a map tile. */
    int hover_x;
    int hover_y;

    /* Notices (core/notice.h) in the top right corner: the number of the
     * next one to take from the game, and those on screen with how long (real
     * seconds) they have been there, oldest first. */
    uint32_t next_notice;
    uint32_t notices_shown[APP_NOTICES_SHOWN];
    double notices_age[APP_NOTICES_SHOWN];
    int notices_shown_count;

    size_t debug_tree_count; /* Shown in the debug overlay, refreshed twice a second. */
    float fps;
    double fps_elapsed;
    int fps_frames;
} App;

/* On failure, app_shutdown() must still be called to release what was created. */
bool app_init(App *app, const CliOptions *options);
void app_run(App *app);
void app_shutdown(App *app);

/* Selects a tool (`type`: DesignationType for APP_TOOL_DESIGNATE,
 * WorkstationType for APP_TOOL_BUILD, StructureType for APP_TOOL_STRUCTURE),
 * or goes back to selection if it is already active. */
void app_toggle_tool(App *app, AppTool tool, int type);

/* Whether this tool (same `type` rules) is the one in use. */
bool app_tool_active(const App *app, AppTool tool, int type);

/* What the key at a physical position is labeled on the active layout. */
const char *app_key_name(SDL_Scancode key);

#endif /* COLONY_GAME_APP_H */
