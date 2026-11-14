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
#include "game/text.h"
#include "game/ui.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

/* What a left click/drag on the map does. */
typedef enum AppTool {
    APP_TOOL_NONE,      /* Select colonists or a stockpile. */
    APP_TOOL_DESIGNATE, /* Place orders of `tool_type` (DESIGNATION_NONE: remove orders). */
    APP_TOOL_STOCKPILE, /* Create/extend stockpile zones; with Shift, remove zone tiles. */
} AppTool;

typedef struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TextRenderer *text;
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

    /* Work priorities grid (game/work_panel.h). */
    bool show_work_panel;
    int work_panel_scroll; /* First row shown. */
    /* A middle/right button press that started on a panel does not pan the view. */
    bool pan_blocked;

    AppTool tool;
    DesignationType tool_type; /* For APP_TOOL_DESIGNATE. */

    bool hover_valid; /* Whether the mouse is over a map tile. */
    int hover_x;
    int hover_y;

    float fps;
    double fps_elapsed;
    int fps_frames;
} App;

/* On failure, app_shutdown() must still be called to release what was created. */
bool app_init(App *app, const CliOptions *options);
void app_run(App *app);
void app_shutdown(App *app);

#endif /* COLONY_GAME_APP_H */
