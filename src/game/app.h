/*
 * The application: owns the SDL window, the renderer, the localization state
 * and the running Game, and drives the main loop.
 */
#ifndef COLONY_GAME_APP_H
#define COLONY_GAME_APP_H

#include "core/camera.h"
#include "core/cli.h"
#include "core/game.h"
#include "core/settings.h"
#include "core/test_scenario.h"
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
    APP_TOOL_BUILD, /* Place a blueprint of `build_type` on the clicked tile. */
    APP_TOOL_GATHER,
    APP_TOOL_STRUCTURE, /* Plan structures of `structure_type` (click, or drag a rectangle). */
    /* Debug (game/debug_panel.h, game/items_panel.h, game/animals_panel.h): `debug_item` (one, or a
     * full stack) on the clicked tile,
     * or, by `debug_mode` (tool type), the selected colonists moved there, a
     * tree seedling planted, or the tree grown a step. */
    APP_TOOL_DEBUG,
    /* Forbid (type APP_FORBID) or allow (APP_ALLOW) everything in a rectangle
     * (game_set_forbidden_rect()). */
    APP_TOOL_FORBID,
    /* Move: a click sends the selected settlers there (game_order_move()); armed
     * by the Move action of the settlers' window. */
    APP_TOOL_MOVE,
    /* Move items: a click sends the stack at `move_item_source` there
     * (game_order_item_move()); armed by the Move action of its tile. */
    APP_TOOL_MOVE_ITEM,
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
#define APP_KEY_LOG SDL_SCANCODE_J    /* The colony log. */
#define APP_KEY_ROTATE SDL_SCANCODE_F /* Turns the structure being planned (beds). */
#define APP_KEY_STOCKPILE SDL_SCANCODE_P
#define APP_KEY_LANGUAGE SDL_SCANCODE_L
#define APP_KEY_MINE SDL_SCANCODE_M
#define APP_KEY_CANCEL SDL_SCANCODE_X
#define APP_KEY_PAUSE SDL_SCANCODE_SPACE
#define APP_KEY_SLOWER SDL_SCANCODE_MINUS  /* One speed down (keys 1-3 pick ×1, ×3, ×6). */
#define APP_KEY_FASTER SDL_SCANCODE_EQUALS /* One speed up. */
#define APP_KEY_BACK SDL_SCANCODE_ESCAPE
#define APP_KEY_HELP SDL_SCANCODE_F1
#define APP_KEY_SCENARIOS_MENU SDL_SCANCODE_F2 /* Test scenarios (game/scenarios_panel.h). */
#define APP_KEY_DEBUG SDL_SCANCODE_F3          /* Debug information. */
#define APP_KEY_DEBUG_MENU SDL_SCANCODE_F4     /* Cheats (game/debug_panel.h). */
#define APP_KEY_ITEMS_MENU SDL_SCANCODE_F5     /* Spawning items (game/items_panel.h). */
#define APP_KEY_ANIMALS_MENU SDL_SCANCODE_F6   /* Spawning animals (game/animals_panel.h). */
#define APP_KEY_TEMPERATURE SDL_SCANCODE_H     /* Temperature view on or off. */
#define APP_KEY_LEVEL_UP SDL_SCANCODE_PAGEUP   /* The level above (also Shift + wheel). */
#define APP_KEY_LEVEL_DOWN SDL_SCANCODE_PAGEDOWN

/* Types of the debug tool (see app_toggle_tool()). */
#define APP_DEBUG_SPAWN 0
#define APP_DEBUG_TELEPORT 1
#define APP_DEBUG_PLANT_TREE 2
#define APP_DEBUG_GROW_TREE 3
#define APP_DEBUG_SPAWN_ANIMAL 4 /* App.debug_animal. */
#define APP_DEBUG_HURT 5         /* A blow on whoever is on the tile. */
#define APP_DEBUG_FIGHT 6        /* Click an attacker, then its target (App.debug_attacker). */

/* Types of the forbid tool (see app_toggle_tool()). */
#define APP_FORBID 0
#define APP_ALLOW 1

/* Types of the zone tools (see app_toggle_tool()). */
#define APP_ZONE_ADD 0
#define APP_ZONE_REMOVE 1

/* Windows (game/ui.h ids). */
/* Two left clicks this close (milliseconds) on the same thing make a double click. */
#define APP_DOUBLE_CLICK_MS 400

typedef enum AppWindow {
    APP_WINDOW_NONE,
    APP_WINDOW_INSPECTOR,      /* Summary of the selection (game/inspector.h). */
    APP_WINDOW_INSPECTOR_TAB,  /* The inspector's open tab. */
    APP_WINDOW_STATION_ADD,    /* Recipes to add to a workstation's bills (game/station_panel.h). */
    APP_WINDOW_STATION_RECIPE, /* What the recipe picked in that list needs and makes. */
    APP_WINDOW_STATION_BILL,   /* The details of the bill picked in the Bills tab. */
    APP_WINDOW_WORK,           /* Work priorities grid. */
    APP_WINDOW_LOG,            /* Colony log (game/log_window.h). */
    APP_WINDOW_HELP,           /* Controls. */
    APP_WINDOW_DEBUG,          /* Debug menu (cheats). */
    APP_WINDOW_ITEMS,          /* Items menu (debug). */
    APP_WINDOW_ANIMALS,        /* Animals menu (debug). */
    APP_WINDOW_SCENARIOS,      /* Test scenarios menu (debug). */
    APP_WINDOW_GAME_MENU,
    APP_WINDOW_SETTINGS, /* Settings (game/settings_window.h), from the game menu. */
    APP_WINDOW_COUNT
} AppWindow;

/* Second level of the build menu (game/build_menu.h). */
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

/* What can be selected on a tile besides a workstation and the zones: each
 * has a window of its own (game/tile_panel.h). */
typedef enum TileThing {
    THING_STRUCTURE, /* A wall, a door, a bed... */
    THING_ROOF,      /* The roof the tile is. */
    THING_SITE,      /* A construction site (a structure's or a workstation's). */
    THING_ITEMS,     /* The stack lying there. */
    THING_PLANT,     /* The plant (grass too), or the tree whose trunk or foliage it is. */
    THING_COUNT
} TileThing;

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
    /* The player's settings as saved (the command line may override some
     * for the session), and the file they are saved in (empty: none). */
    Settings settings;
    char settings_path[1100];

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
    float drag_start_x; /* Screen coordinates where the left button was pressed (drag threshold). */
    float drag_start_y;
    /* The same point in world coordinates: the fixed corner of a drag rectangle
     * or selection box. The camera may move under a held button; only the
     * corner at the cursor follows it. No level is kept: the level shown when
     * the button is released is the one used. */
    float drag_world_x;
    float drag_world_y;

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
    /* Groups of resources folded in the gathering zone window (UI state). */
    bool gather_group_folded[GATHER_GROUP_COUNT];
    /* Selected thing on a tile (a structure, a roof, a construction site,
     * items or a plant: game/tile_panel.h), same rules. */
    bool tile_selected;
    int selected_tile_x, selected_tile_y, selected_tile_z;
    TileThing selected_thing;
    /* Selected wild animal (ENTITY_NONE if none), same rules: dropped once
     * the colony no longer sees it (game/animal_panel.h). */
    EntityId selected_animal;
    /* The kinds of entries the Journal tab hides (a bit per JournalKind; 0: all shown). */
    JournalFilter journal_hidden;
    /* A right click on the map (not dragged to pan) steps back like Escape. */
    bool right_click_pending;
    float right_down_x, right_down_y;

    /* Work priorities grid (game/work_panel.h) and controls help. */
    bool show_work_panel;
    bool show_log; /* Colony log (game/log_window.h): a main window like the work grid. */
    bool show_help;
    bool show_debug_menu;     /* Debug menu (game/debug_panel.h). */
    bool show_items_menu;     /* Items menu (game/items_panel.h). */
    bool show_animals_menu;   /* Animals menu (game/animals_panel.h). */
    bool show_scenarios_menu; /* Test scenarios menu (game/scenarios_panel.h). */
    int pending_scenario;  /* A test scenario to load before the next update (TestScenario + 1, 0:
                              none). */
    bool show_temperature; /* The temperature of each tile over the map. */
    bool character_editor; /* Debug: - and + buttons on needs, characters and knowledge. */
    bool show_vision;      /* Debug: what the selected settler sees, the rest veiled. */
    float level_wheel;     /* Wheel steps with Shift held, towards the next level. */
    /* Build menu (game/build_menu.h): open, which category is shown, and the
     * icon under the mouse (APP_TOOL_NONE: none) whose card is shown. */
    bool build_menu_open;
    BuildCategory build_category;
    AppTool build_hover_tool;
    int build_hover_type;
    bool show_game_menu; /* Game menu (game/game_menu.h): the game is paused meanwhile. */
    bool show_settings;  /* Settings window, opened from the game menu (paused too). */
    /* Inspector (game/inspector.h): the kind of selection shown last frame
     * (an InspectorKind), its open tab (-1: none) and how many tabs it has. */
    int inspector_kind;
    int inspector_tab;
    int inspector_tab_count;
    float inspector_tabs_width; /* What the tab buttons need, drawn last frame (0: none). */
    /* Workstation panel (game/station_panel.h): the list of recipes to add as
     * a bill is open, the recipe picked in it (station_recipe, when
     * station_recipe_picked) and the last click on a recipe, to tell a double
     * click (adds it). */
    bool station_add_open;
    /* The bill picked in the Bills tab (station_bill, when station_bill_picked):
     * its details open instead of that list. The number of the station all
     * this is about: the state is forgotten for another. */
    bool station_bill_picked;
    int station_bill;
    int station_panel_number;
    bool station_recipe_picked;
    int station_recipe;
    int station_click_recipe;
    Uint64 station_click_ms;
    /* Colonist bar (game/colonist_bar.h): the last click on an entry, to
     * tell a double click (centers the view on the colonist). */
    EntityId bar_last_click;
    Uint64 bar_last_click_ms;
    /* Resources list (game/resources_list.h): the groups folded. */
    bool resources_folded[ITEM_CATEGORY_COUNT];
    /* A middle/right button press that started on a panel does not pan the view. */
    bool pan_blocked;

    AppTool tool;
    DesignationType tool_type;     /* For APP_TOOL_DESIGNATE. */
    bool zone_remove;              /* For the zone tools: drags remove tiles from zones. */
    WorkstationType build_type;    /* For APP_TOOL_BUILD. */
    StructureType structure_type;  /* For APP_TOOL_STRUCTURE. */
    Direction structure_direction; /* For APP_TOOL_STRUCTURE: where a bed's foot goes. */
    ItemType debug_item;           /* For APP_TOOL_DEBUG: what is spawned. */
    bool debug_item_max;           /* ... a full stack of it, else one item. */
    TilePos move_item_source;      /* For APP_TOOL_MOVE_ITEM: the stack to carry. */
    int debug_mode;                /* For APP_TOOL_DEBUG: APP_DEBUG_SPAWN... */
    AnimalSpecies debug_animal;    /* For APP_DEBUG_SPAWN_ANIMAL: what is spawned... */
    Combatant debug_attacker;      /* For APP_DEBUG_FIGHT: who was clicked first (or none). */
    AnimalSex debug_animal_sex;    /* ... and its sex. */
    bool forbid_allows;            /* For APP_TOOL_FORBID: allows instead. */

    bool hover_valid; /* Whether the mouse is over a map tile. */
    int hover_x;
    int hover_y;

    /* Notices (core/notice.h) below the colonist bar: the number of the
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
    /* Time spent drawing a frame (the wait for the screen left out), averaged
     * like the frame rate: what the frame rate would allow without vsync. */
    float render_ms;
    double render_elapsed;
    int bench_render_frames; /* --bench-render: frames per view, 0 for a normal game. */
} App;

/* On failure, app_shutdown() must still be called to release what was created. */
bool app_init(App *app, const CliOptions *options);
/* The game loop, or the render benchmark with --bench-render. */
void app_run(App *app);
void app_shutdown(App *app);

/* Asks for a test scenario (core/test_scenario.h) to replace the game at the
 * start of the next frame: never in the middle of drawing windows. */
void app_request_test_scenario(App *app, TestScenario scenario);

/* Selects a tool (`type`: DesignationType for APP_TOOL_DESIGNATE,
 * WorkstationType for APP_TOOL_BUILD, StructureType for APP_TOOL_STRUCTURE,
 * APP_FORBID or APP_ALLOW for APP_TOOL_FORBID),
 * or goes back to selection if it is already active. */
void app_toggle_tool(App *app, AppTool tool, int type);

/* Whether this tool (same `type` rules) is the one in use. */
bool app_tool_active(const App *app, AppTool tool, int type);

/* Selects a colonist (the other selections are dropped); `additive` adds it
 * to the colonists selected, or takes it out when it already is. */
void app_select_colonist(App *app, EntityId id, bool additive);

/* What building another of the selection takes (a zone, a workstation or
 * its site, a structure): the tool and its type (see app_toggle_tool());
 * false when there is nothing to build like it. */
bool app_build_like_target(const App *app, AppTool *tool, int *type);

/* Whether the thing is on the tile, as the colony knows it (game_view_map()). */
bool app_thing_at(const App *app, TileThing thing, int x, int y, int z);

/* The plant on a tile as the colony knows it; on a tile of trunk or foliage,
 * its tree's (PLANT_NONE: none). */
PlantType app_plant_type_at(const App *app, int x, int y, int z);

/* Starts building another of what is selected (APP_KEY_BUILD_LIKE). */
void app_build_like_selection(App *app);

/* Centers the view on a colonist, on its level. */
void app_focus_colonist(App *app, EntityId id);

/* Switches to the next language, and remembers it (settings). */
void app_cycle_language(App *app);

/* Switches to a language (an index in Localization.languages) and remembers it. */
void app_select_language(App *app, int index);

/* Applies an interface size (percent, clamped to CLI_UI_SCALE_MIN..MAX) at
 * once, font included, and remembers it. */
void app_set_ui_scale(App *app, int percent);

/* Applies a palette at once and remembers it. */
void app_set_palette(App *app, SettingsPalette palette);

/* The interface size in use, in percent. */
int app_ui_scale(const App *app);

/* What the key at a physical position is labeled on the active layout. */
const char *app_key_name(SDL_Scancode key);

#endif /* COLONY_GAME_APP_H */
