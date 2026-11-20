/*
 * The application: owns the SDL window, the renderer, the localization state
 * and the running Game, and drives the main loop.
 */
#ifndef COLONY_GAME_APP_H
#define COLONY_GAME_APP_H

#include "core/camera.h"
#include "core/cli.h"
#include "core/game.h"
#include "core/new_game.h"
#include "core/settings.h"
#include "core/test_scenario.h"
#include "game/localization.h"
#include "game/render.h"
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
     * or in the removal mode (`zone_mode`, tool type APP_ZONE_REMOVE); extend
     * or shrink the selected zone alone (APP_ZONE_EXTEND, APP_ZONE_SHRINK,
     * Shift shrinking too). */
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
    /* Move items: a click sends the items at `move_item_source` there
     * (game_order_item_move()), or the one item `move_item_id` when it is set
     * (game_order_single_item_move()); armed by the Move action of a tile's
     * items or of one item. */
    APP_TOOL_MOVE_ITEM,
    /* Carry: a click on an item sends the one selected settler to fetch it
     * and keep it, locked (game_order_carry()); armed by the Carry action of
     * the settler's window. */
    APP_TOOL_CARRY,
    /* Attack: a click on an animal sends the mobilized settlers selected
     * after it (game_order_attack()); armed by their Attack action. */
    APP_TOOL_ATTACK,
} AppTool;

/* The branches of the Gear tab's tree (game/hud.c): a place of the body
 * (CarryPlace), or the items that found no place. Later, containers. */
#define GEAR_BRANCH_NO_PLACE CARRY_PLACE_COUNT
#define GEAR_BRANCH_COUNT (CARRY_PLACE_COUNT + 1)

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
#define APP_KEY_GATHER SDL_SCANCODE_R
#define APP_KEY_WORK SDL_SCANCODE_T
#define APP_KEY_LOG SDL_SCANCODE_J    /* The colony log. */
#define APP_KEY_ROTATE SDL_SCANCODE_F /* Turns the structure being planned (beds). */
#define APP_KEY_FOLLOW SDL_SCANCODE_C /* The camera follows the selected settler or animal. */
#define APP_KEY_DRAFT SDL_SCANCODE_B  /* Mobilizes or demobilizes the selected settlers. */
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
#define APP_KEY_VISUAL_MENU SDL_SCANCODE_F7    /* Visual debug (game/visual_panel.h). */
#define APP_KEY_TEMPERATURE SDL_SCANCODE_H     /* Temperature view on or off. */
#define APP_KEY_LIGHT SDL_SCANCODE_G           /* Light view on or off. */
#define APP_KEY_MOISTURE SDL_SCANCODE_K        /* Soil moisture view on or off. */
#define APP_KEY_LEVEL_UP SDL_SCANCODE_PAGEUP   /* The level above (also Shift + wheel). */
#define APP_KEY_LEVEL_DOWN SDL_SCANCODE_PAGEDOWN

/* The font of the interface, in the assets folder. */
#define APP_FONT_FILE "fonts/DejaVuSans.ttf"

/* Who the camera follows (App.follow_kind). */
typedef enum FollowKind { FOLLOW_NONE, FOLLOW_COLONIST, FOLLOW_ANIMAL } FollowKind;

/* Types of the debug tool (see app_toggle_tool()). */
#define APP_DEBUG_SPAWN 0
#define APP_DEBUG_TELEPORT 1
#define APP_DEBUG_PLANT_TREE 2
#define APP_DEBUG_GROW_TREE 3
#define APP_DEBUG_SPAWN_ANIMAL 4 /* App.debug_animal. */
#define APP_DEBUG_HURT 5         /* A blow on whoever is on the tile. */
#define APP_DEBUG_FIGHT 6        /* Click an attacker, then its target (App.debug_attacker). */
#define APP_DEBUG_MOVE_ITEM 7    /* Drag an item anywhere, the wheel turns it (App.debug_held). */

/* Types of the forbid tool (see app_toggle_tool()). */
#define APP_FORBID 0
#define APP_ALLOW 1

/* Types of the zone tools (see app_toggle_tool()). */
#define APP_ZONE_ADD 0
#define APP_ZONE_REMOVE 1
#define APP_ZONE_EXTEND 2 /* The selected zone alone. */
#define APP_ZONE_SHRINK 3

/* Windows (game/ui.h ids). */
/* Two left clicks this close (milliseconds) on the same thing make a double click. */
#define APP_DOUBLE_CLICK_MS 400

typedef enum AppWindow {
    APP_WINDOW_NONE,
    APP_WINDOW_INSPECTOR,     /* Summary of the selection (game/inspector.h). */
    APP_WINDOW_INSPECTOR_TAB, /* The inspector's open tab. */
    APP_WINDOW_HEALTH_BODY,   /* The body's parts, right of the Health tab (game/health_panel.h). */
    APP_WINDOW_STATION_ADD,   /* Recipes to add to a workstation's bills (game/station_panel.h). */
    APP_WINDOW_STATION_RECIPE, /* What the recipe picked in that list needs and makes. */
    APP_WINDOW_STATION_BILL,   /* The details of the bill picked in the Bills tab. */
    APP_WINDOW_WORK,           /* Work priorities grid. */
    APP_WINDOW_LOG,            /* Colony log (game/log_window.h). */
    APP_WINDOW_HELP,           /* Controls. */
    APP_WINDOW_DEBUG,          /* Debug menu (cheats). */
    APP_WINDOW_ITEMS,          /* Items menu (debug). */
    APP_WINDOW_ANIMALS,        /* Animals menu (debug). */
    APP_WINDOW_VISUAL,         /* Visual debug menu. */
    APP_WINDOW_SCENARIOS,      /* Test scenarios menu (debug). */
    APP_WINDOW_GAME_MENU,
    APP_WINDOW_SETTINGS, /* Settings (game/settings_window.h), from the game menu. */
    APP_WINDOW_NEW_GAME, /* The title screen's New game window (game/title_screen.h). */
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
/* Items a box selects at most. */
#define APP_BOX_ITEMS_MAX 256
/* Plants a double click selects at most (all of one kind on the screen). */
#define APP_BOX_PLANTS_MAX 2048
/* Pixels the cursor may move between the two clicks of a double click. */
#define APP_DOUBLE_CLICK_SLOP 6.0f

typedef enum TileThing {
    THING_STRUCTURE, /* A wall, a door, a bed... */
    THING_ROOF,      /* The roof the tile is. */
    THING_SITE,      /* A construction site (a structure's or a workstation's). */
    THING_ITEMS,     /* The stack lying there. */
    THING_PLANT,     /* The plant (grass too), or the tree whose trunk or foliage it is. */
    THING_ITEM,      /* One item lying there (App.selected_item; the tile follows it). */
    THING_ITEM_BOX,  /* Several items lying about, picked by a box (App.selected_box_items). */
    THING_PLANT_BOX, /* Plants of one kind, picked by a double click (App.selected_plants). */
    THING_COUNT
} TileThing;

/* What the window shows. */
typedef enum AppScreen {
    APP_SCREEN_TITLE, /* The title screen (game/title_screen.h), a world drifting behind it. */
    APP_SCREEN_GAME,
} AppScreen;

/* Where starting a new game is (App.new_game_stage). */
typedef enum NewGameStage {
    NEW_GAME_STAGE_NONE,
    NEW_GAME_STAGE_ASKED,    /* Start clicked: the next frame tells the world is being made... */
    NEW_GAME_STAGE_GENERATE, /* ...it was shown: the world is made before the next frame. */
} NewGameStage;

/* What the outline of an area setting is about. */
typedef enum AreaPreviewKind {
    AREA_PREVIEW_NONE,
    AREA_PREVIEW_STOCKPILE, /* The tiles a stockpile takes items from (its radius). */
    AREA_PREVIEW_REACH,     /* The tiles within a walking cost of a workstation (a bill's range). */
} AreaPreviewKind;

typedef struct AreaPreview {
    AreaPreviewKind kind;
    int stockpile; /* AREA_PREVIEW_STOCKPILE: its index. */
    TilePos from;  /* AREA_PREVIEW_REACH: where the walk starts. */
    int cost;      /* AREA_PREVIEW_REACH: how far. */
} AreaPreview;

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

    AppScreen screen;
    /* The title screen (game/title_screen.h): its fonts (the big title, the
     * tagline and buttons) and the interface size they were made for; how
     * long (real seconds) the world behind has drifted; the New game window,
     * what it sets and the seed typed (empty: a random one). */
    TextRenderer *title_text;
    TextRenderer *title_subtext;
    float title_text_scale;
    double title_time;
    bool new_game_open;
    NewGameParams new_game;
    char new_game_seed[NEW_GAME_SEED_TEXT_SIZE];
    NewGameStage new_game_stage;
    bool pending_title; /* Back to the title screen before the next update (the game ends). */
    bool leave_confirm; /* The game menu asks before leaving for the title screen. */

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
    /* Branches folded in a settler's Gear tab (UI state): the places of the
     * body (CarryPlace), then the items with no place. */
    bool gear_branch_folded[GEAR_BRANCH_COUNT];
    /* Selected thing on a tile (a structure, a roof, a construction site,
     * items or a plant: game/tile_panel.h), same rules. */
    bool tile_selected;
    int selected_tile_x, selected_tile_y, selected_tile_z;
    TileThing selected_thing;
    ItemPick selected_item; /* THING_ITEM: the item, picked under the cursor. */
    /* THING_ITEM_BOX: the items inside the box drawn, each followed where it
     * lies; those gone are dropped. */
    ItemPick selected_box_items[APP_BOX_ITEMS_MAX];
    int selected_box_item_count;
    /* THING_PLANT_BOX: the tiles the plants grow on (a tree's: its foot),
     * all of kind selected_plant_type; those gone are dropped. */
    TilePos selected_plants[APP_BOX_PLANTS_MAX];
    int selected_plant_count;
    int selected_plant_type;
    /* The last click on the map (SDL ticks, screen position): a second one
     * close in time and space is a double click. */
    Uint64 map_click_ms;
    float map_click_x, map_click_y;
    /* Selected wild animal (ENTITY_NONE if none), same rules: dropped once
     * the colony no longer sees it (game/animal_panel.h). */
    EntityId selected_animal;
    /* The being the camera follows (app_follow()): the view stays on it, on its
     * level too, until the player moves the view or it is gone. */
    FollowKind follow_kind;
    EntityId follow_id;
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
    bool show_visual_menu;    /* Visual debug menu (game/visual_panel.h). */
    RenderTuning tuning;      /* What it sets: the fog, the levels below. */
    bool show_scenarios_menu; /* Test scenarios menu (game/scenarios_panel.h). */
    int pending_scenario;  /* A test scenario to load before the next update (TestScenario + 1, 0:
                              none). */
    bool show_temperature; /* The temperature of each tile over the map. */
    bool show_light;       /* The light on each tile over the map (not with the temperature). */
    bool show_moisture;    /* The water in the ground of each tile (one view at a time). */
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
    /* The stock counted afresh each frame (game_count_stock()): what the resources list, the
     * bills and the gathering zones show. */
    long stock[ITEM_TYPE_COUNT];
    /* The area a panel setting reaches, outlined in white on the map while the pointer is on
     * (or drags) that setting: the stockpile radius slider, a bill's ingredient range. Set by the
     * panel while it draws, shown by the next frame's world, then forgotten. */
    AreaPreview area_preview;
    /* The tiles a settler reaches on the level shown within the preview's cost (kept while
     * it is the same, see area_outline_update()). */
    uint8_t *reach_mask;
    size_t reach_capacity;
    AreaPreview reach_key;
    int reach_level;
    uint32_t reach_revision;
    bool reach_valid;
    /* Resources list (game/resources_list.h): the groups folded. */
    bool resources_folded[ITEM_CATEGORY_COUNT];
    /* A middle/right button press that started on a panel does not pan the view. */
    bool pan_blocked;

    AppTool tool;
    DesignationType tool_type;     /* For APP_TOOL_DESIGNATE. */
    int zone_mode;                 /* For the zone tools: APP_ZONE_ADD... */
    WorkstationType build_type;    /* For APP_TOOL_BUILD. */
    StructureType structure_type;  /* For APP_TOOL_STRUCTURE. */
    Direction structure_direction; /* For APP_TOOL_STRUCTURE: where a bed's foot goes. */
    ItemType debug_item;           /* For APP_TOOL_DEBUG: what is spawned. */
    bool debug_item_max;           /* ... a full stack of it, else one item. */
    bool move_item_box;            /* For APP_TOOL_MOVE_ITEM: the items of the box selected... */
    TilePos move_item_source;      /* ...or the items to carry... */
    ItemId move_item_id;           /* ...or this one alone (ITEM_ID_NONE: all of them). */
    int debug_mode;                /* For APP_TOOL_DEBUG: APP_DEBUG_SPAWN... */
    AnimalSpecies debug_animal;    /* For APP_DEBUG_SPAWN_ANIMAL: what is spawned... */
    Combatant debug_attacker;      /* For APP_DEBUG_FIGHT: who was clicked first (or none). */
    ItemId debug_held;             /* For APP_DEBUG_MOVE_ITEM: the item dragged (or none). */
    ItemOrder items_order;         /* How the inspector lists the items of a tile. */
    /* The character editor's Social tab: the settler the selected one acts
     * with (a click on its row; the best liked one when none). */
    EntityId debug_social_partner;
    AnimalSex debug_animal_sex; /* ... and its sex. */
    bool forbid_allows;         /* For APP_TOOL_FORBID: allows instead. */

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

/* A seed from the clock, for a world nobody chose. */
uint64_t app_random_seed(void);

/* Ends the game and goes back to the title screen at the start of the next
 * frame (the game is not kept). */
void app_request_title(App *app);

/* Asks for a test scenario (core/test_scenario.h) to replace the game at the
 * start of the next frame: never in the middle of drawing windows. */
void app_request_test_scenario(App *app, TestScenario scenario);

/* Selects a tool (`type`: DesignationType for APP_TOOL_DESIGNATE,
 * WorkstationType for APP_TOOL_BUILD, StructureType for APP_TOOL_STRUCTURE,
 * APP_FORBID or APP_ALLOW for APP_TOOL_FORBID),
 * or goes back to selection if it is already active. */
void app_toggle_tool(App *app, AppTool tool, int type);

/* APP_KEY_CANCEL with no tool in use: deletes the selected zone, cancels the
 * selected site, orders the selected structure or roof taken apart (or takes
 * the order back), takes back the order on the selected plant or items
 * (tile_panel_cancel()). False when the selection takes none of these: the
 * key then arms the tool that cancels orders. */
bool app_cancel_selection(App *app);

/* Whether this tool (same `type` rules) is the one in use. */
bool app_tool_active(const App *app, AppTool tool, int type);

/* Selects a colonist (the other selections are dropped); `additive` adds it
 * to the colonists selected, or takes it out when it already is. */
void app_select_colonist(App *app, EntityId id, bool additive);

/* What building another of the selection takes (a zone, a workstation or
 * its site, a structure): the tool and its type (see app_toggle_tool());
 * false when there is nothing to build like it. */
bool app_build_like_target(const App *app, AppTool *tool, int *type);

/* Selects the items lying on a tile together (the tile's window with the
 * list of them), or one of them by its pick (the window of that item). */
void app_select_tile_items(App *app, int x, int y, int z);
void app_select_item(App *app, ItemPick pick);

/* Whether the thing is on the tile, as the colony knows it (game_view_map()). */
bool app_thing_at(const App *app, TileThing thing, int x, int y, int z);

/* The plant on a tile as the colony knows it; on a tile of trunk or foliage,
 * its tree's (PLANT_NONE: none). */
PlantType app_plant_type_at(const App *app, int x, int y, int z);

/* Turns the temperature, light or soil moisture view on or off: they are
 * drawn over the same tiles, so showing one hides the others. */
void app_toggle_temperature_view(App *app);
void app_toggle_light_view(App *app);
void app_toggle_moisture_view(App *app);

/* Starts building another of what is selected (APP_KEY_BUILD_LIKE). */
void app_build_like_selection(App *app);

/* Centers the view on a colonist, on its level (once: it stops following). */
void app_focus_colonist(App *app, EntityId id);

/* The camera follows a settler or an animal: it is centered on it at once and
 * stays on it, level included, as it walks. The player moving the view (keys,
 * dragging, another level) ends it, as does the being's death or, for an
 * animal, being out of the colony's sight. Zooming keeps it. */
void app_follow(App *app, FollowKind kind, EntityId id);
void app_stop_following(App *app);
bool app_is_following(const App *app, FollowKind kind, EntityId id);

/* Follows the selected settler or animal, or stops if it is the one followed
 * (APP_KEY_FOLLOW, the Follow action). */
void app_toggle_follow(App *app, FollowKind kind, EntityId id);

/* Mobilization of the selected settlers (docs/COMBAT.md §8):
 * app_selection_drafted() tells whether all of them are mobilized,
 * app_selection_draftable() whether any could be mobilized or demobilized.
 * app_toggle_draft() demobilizes them when all are mobilized, else mobilizes
 * those who can be (APP_KEY_DRAFT, the Mobilize action). */
bool app_selection_drafted(const App *app);
bool app_selection_draftable(const App *app);
void app_toggle_draft(App *app);

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
