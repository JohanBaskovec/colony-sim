#include "game/app.h"

#include "core/i18n.h"
#include "core/log.h"
#include "game/hud.h"
#include "game/render.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <math.h>

#define APP_WINDOW_WIDTH 1280
#define APP_WINDOW_HEIGHT 720
#define APP_FONT_FILE "fonts/DejaVuSans.ttf"
#define APP_FONT_SIZE 15.0f
#define APP_INITIAL_ZOOM 16.0f
#define APP_PAN_SPEED 900.0f /* Pixels per second. */
#define APP_PAN_FAST_FACTOR 2.5f
#define APP_ZOOM_STEP 1.15f

static const SDL_Color HOVER_COLOR = {255, 255, 255, 200};

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
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);

    char font_path[1200];
    SDL_snprintf(font_path, sizeof(font_path), "%s%s", app->assets_dir, APP_FONT_FILE);
    app->text = text_renderer_create(app->renderer, font_path, APP_FONT_SIZE);
    if (app->text == NULL) {
        return false;
    }

    WorldGenParams params;
    worldgen_default_params(&params);
    params.seed = options->has_seed ? options->seed : random_seed();
    if (options->map_size > 0) {
        params.width = options->map_size;
        params.height = options->map_size;
    }
    if (!game_init(&app->game, &params)) {
        LOG_ERROR("World generation failed");
        return false;
    }
    app->game_ready = true;
    LOG_INFO("World generated: seed %" SDL_PRIu64 ", %dx%d tiles", params.seed, params.width,
             params.height);

    camera_init(&app->camera, (float)params.width * 0.5f, (float)params.height * 0.5f,
                APP_INITIAL_ZOOM);
    app->running = true;
    return true;
}

void app_shutdown(App *app)
{
    if (app->game_ready) {
        game_shutdown(&app->game);
    }
    text_renderer_destroy(app->text);
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

static void handle_key_down(App *app, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    /* Speed keys use physical positions (scancodes): they work on any layout. */
    switch (key->scancode) {
    case SDL_SCANCODE_SPACE: game_clock_toggle_pause(&app->game.clock); return;
    case SDL_SCANCODE_1:
    case SDL_SCANCODE_KP_1: game_clock_set_speed(&app->game.clock, GAME_SPEED_NORMAL); return;
    case SDL_SCANCODE_2:
    case SDL_SCANCODE_KP_2: game_clock_set_speed(&app->game.clock, GAME_SPEED_FAST); return;
    case SDL_SCANCODE_3:
    case SDL_SCANCODE_KP_3: game_clock_set_speed(&app->game.clock, GAME_SPEED_FASTEST); return;
    default: break;
    }
    /* Mnemonic keys use the key labels (keycodes). */
    switch (key->key) {
    case SDLK_ESCAPE: app->running = false; break;
    case SDLK_F3: app->show_debug = !app->show_debug; break;
    case SDLK_L:
        localization_cycle(&app->localization);
        update_window_title(app);
        break;
    default: break;
    }
}

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
        if (steps != 0.0f) {
            camera_zoom_at(&app->camera, powf(APP_ZOOM_STEP, steps), event->wheel.mouse_x,
                           event->wheel.mouse_y);
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (event->motion.state & (SDL_BUTTON_MMASK | SDL_BUTTON_RMASK)) {
            camera_pan_pixels(&app->camera, -event->motion.xrel, -event->motion.yrel);
        }
        break;
    default: break;
    }
}

static void update_camera(App *app, float dt)
{
    int width = 0, height = 0;
    SDL_GetRenderOutputSize(app->renderer, &width, &height);
    camera_set_viewport(&app->camera, (float)width, (float)height);

    const bool *keys = SDL_GetKeyboardState(NULL);
    /* Physical W/A/S/D positions (scancodes): ZQSD on AZERTY keyboards. */
    float dx = (float)(keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) -
               (float)(keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]);
    float dy = (float)(keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) -
               (float)(keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]);
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
                       map_in_bounds(&app->game.map, app->hover_x, app->hover_y);
}

static void update_fps(App *app, double dt)
{
    app->fps_elapsed += dt;
    app->fps_frames++;
    if (app->fps_elapsed >= 0.5) {
        app->fps = (float)(app->fps_frames / app->fps_elapsed);
        app->fps_elapsed = 0.0;
        app->fps_frames = 0;
    }
}

static void render_frame(App *app)
{
    SDL_SetRenderDrawColor(app->renderer, 10, 10, 12, 255);
    SDL_RenderClear(app->renderer);

    render_map(app->renderer, &app->game.map, &app->camera);
    render_daylight(app->renderer, game_daylight(app->game.clock.tick),
                    app->camera.viewport_width, app->camera.viewport_height);
    if (app->hover_valid) {
        render_tile_outline(app->renderer, &app->camera, app->hover_x, app->hover_y,
                            HOVER_COLOR);
    }
    hud_draw(app);

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
        update_fps(app, dt);
        render_frame(app);
    }
}
