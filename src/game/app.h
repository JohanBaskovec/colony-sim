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

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TextRenderer *text;
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

    /* Designation tool: while active, left click/drag places orders of
     * `tool_type` (DESIGNATION_NONE: removes orders) instead of selecting. */
    bool tool_active;
    DesignationType tool_type;

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
