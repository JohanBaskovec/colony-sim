#include "game/title_screen.h"

#include "core/i18n.h"
#include "core/log.h"
#include "game/hud.h"
#include "game/saves_window.h"
#include "game/settings_window.h"

#include <math.h>
#include <string.h>

/* The world behind: always the same one, so that the screen looks the same. */
#define BACKDROP_SEED 5
#define BACKDROP_SIZE 128
#define BACKDROP_ZOOM 40.0f
/* Games begin at dawn: the world behind is shown in the morning light. */
#define BACKDROP_MORNING_TICKS (3 * TICKS_PER_HOUR)
/* The view drifts along a figure of eight around the middle of the world
 * (tiles), a lap in DRIFT_PERIOD real seconds. */
#define DRIFT_RADIUS_X 24.0f
#define DRIFT_RADIUS_Y 14.0f
#define DRIFT_PERIOD 480.0

/* Sizes at the default interface size (theme scale 1). */
#define TITLE_POINTS 80.0f
#define SUBTITLE_POINTS 20.0f
#define TITLE_LETTER_SPACING 0.34f /* Of the title's size, between two letters. */
#define BUTTON_WIDTH 300.0f
#define BUTTON_HEIGHT 50.0f
#define BUTTON_GAP 14.0f
#define TITLE_BUTTONS 4 /* New game, Load game, Settings, Quit. */
#define ORNAMENT_LENGTH 70.0f
#define COLUMN_MIN_WIDTH 280.0f
#define COLUMN_GAP 32.0f

/* Slider ids (Ui.active_slider): unique among the windows open together. */
#define SLIDER_ID_BASE 8100

#define TEXT_SIZE 192
#define TITLE_TEXT_SIZE 64

/* The colors of the screen: ice, and the warmth of the thaw. */
static const SDL_Color ICE = {236, 242, 246, 255};
static const SDL_Color MIST = {196, 208, 218, 255};
static const SDL_Color AMBER = {232, 196, 140, 255};
static const SDL_Color SHADOW = {0, 0, 0, 150};

/* ---- The world behind ------------------------------------------------------ */

void title_screen_backdrop_params(NewGameParams *params)
{
    new_game_default_params(params);
    params->world.seed = BACKDROP_SEED;
    params->world.width = BACKDROP_SIZE;
    params->world.height = BACKDROP_SIZE;
    params->colonist_count = 0;
}

/* The level the view shows: the ground's most common one where it drifts
 * (the plains, most often). */
static int backdrop_level(const Map *map)
{
    int counts[MAP_MAX_LEVELS] = {0};
    int x0 = (int)((float)map->width * 0.5f - DRIFT_RADIUS_X) - 16;
    int y0 = (int)((float)map->height * 0.5f - DRIFT_RADIUS_Y) - 10;
    int x1 = (int)((float)map->width * 0.5f + DRIFT_RADIUS_X) + 16;
    int y1 = (int)((float)map->height * 0.5f + DRIFT_RADIUS_Y) + 10;
    for (int y = SDL_max(y0, 0); y < SDL_min(y1, map->height); y++) {
        for (int x = SDL_max(x0, 0); x < SDL_min(x1, map->width); x++) {
            int level = map_ground_level(map, x, y);
            if (level >= 0 && level < MAP_MAX_LEVELS) {
                counts[level]++;
            }
        }
    }
    int best = map->surface_level;
    for (int level = 0; level < MAP_MAX_LEVELS; level++) {
        if (counts[level] > counts[best]) {
            best = level;
        }
    }
    return best;
}

/* Where the view is after `seconds` of drifting. */
static void drift_position(const App *app, double seconds, float *x, float *y)
{
    double angle = seconds / DRIFT_PERIOD * 2.0 * SDL_PI_D;
    *x = (float)app->game.map.width * 0.5f + DRIFT_RADIUS_X * (float)sin(angle);
    *y = (float)app->game.map.height * 0.5f + DRIFT_RADIUS_Y * (float)sin(2.0 * angle);
}

void title_screen_begin(App *app)
{
    app->screen = APP_SCREEN_TITLE;
    app->title_time = 0.0;
    app->new_game_open = false;
    app->new_game_stage = NEW_GAME_STAGE_NONE;
    app->show_settings = false;
    app->show_saves = false;
    if (app->new_game.world.width == 0) {
        new_game_default_params(&app->new_game); /* The first visit. */
    }
    float x, y;
    drift_position(app, 0.0, &x, &y);
    camera_init(&app->camera, x, y, BACKDROP_ZOOM * app->ui.theme.scale);
    app->camera.level = backdrop_level(&app->game.map);
    game_debug_skip(&app->game, BACKDROP_MORNING_TICKS);
}

static void close_new_game(App *app)
{
    if (app->new_game_open) {
        app->new_game_open = false;
        SDL_StopTextInput(app->window);
    }
}

static void open_new_game(App *app)
{
    if (!app->new_game_open) {
        app->new_game_open = true;
        SDL_StartTextInput(app->window);
    }
}

void title_screen_end(App *app)
{
    close_new_game(app);
    app->new_game_stage = NEW_GAME_STAGE_NONE;
}

void title_screen_update(App *app, double dt)
{
    app->title_time += dt;
    int width = 0, height = 0;
    SDL_GetRenderOutputSize(app->renderer, &width, &height);
    camera_set_viewport(&app->camera, (float)width, (float)height);
    drift_position(app, app->title_time, &app->camera.x, &app->camera.y);
    app->hover_valid = false;
    if (app->new_game_stage == NEW_GAME_STAGE_NONE) {
        game_update(&app->game, dt);
    }
}

/* ---- The seed typed -------------------------------------------------------- */

/* The menu of a right click on the field. */
#define SEED_MENU_ID 1
enum { SEED_MENU_COPY, SEED_MENU_PASTE, SEED_MENU_COUNT };

static void seed_append(App *app, const char *text)
{
    size_t length = strlen(app->new_game_seed);
    for (const char *c = text; *c != '\0'; c++) {
        if ((unsigned char)*c < 0x20 || *c == 0x7f) {
            return; /* No control characters (a pasted line break ends it). */
        }
    }
    size_t added = strlen(text);
    if (length + added < sizeof(app->new_game_seed)) {
        memcpy(app->new_game_seed + length, text, added + 1);
    }
}

/* Takes the last character off (all the bytes of it). */
static void seed_erase(App *app)
{
    size_t length = strlen(app->new_game_seed);
    while (length > 0 && ((unsigned char)app->new_game_seed[length - 1] & 0xC0) == 0x80) {
        length--;
    }
    if (length > 0) {
        length--;
    }
    app->new_game_seed[length] = '\0';
}

/* Replaces the seed by the first line of the clipboard (the field has no selection: a seed
 * pasted over another is meant to take its place). */
static void seed_paste(App *app)
{
    char *text = SDL_GetClipboardText();
    if (text == NULL) {
        return;
    }
    text[strcspn(text, "\r\n")] = '\0'; /* The first line. */
    char before[sizeof(app->new_game_seed)];
    memcpy(before, app->new_game_seed, sizeof(before));
    app->new_game_seed[0] = '\0';
    seed_append(app, text);
    if (text[0] != '\0' && app->new_game_seed[0] == '\0') {
        memcpy(app->new_game_seed, before, sizeof(before)); /* Refused: too long, or not text. */
    }
    SDL_free(text);
}

static void seed_copy(const App *app)
{
    if (app->new_game_seed[0] != '\0') {
        SDL_SetClipboardText(app->new_game_seed);
    }
}

static void ask_start(App *app)
{
    close_new_game(app);
    app->new_game_stage = NEW_GAME_STAGE_ASKED;
}

static void handle_key_down(App *app, const SDL_KeyboardEvent *key)
{
    if (app->new_game_stage != NEW_GAME_STAGE_NONE) {
        return;
    }
    switch (key->scancode) {
    case SDL_SCANCODE_ESCAPE:
        if (key->repeat) {
            break;
        }
        if (app->show_settings) {
            app->show_settings = false;
        } else if (app->show_saves) {
            saves_window_step_back(app);
        } else {
            close_new_game(app);
        }
        break;
    case SDL_SCANCODE_BACKSPACE:
        if (app->new_game_open) {
            seed_erase(app);
        }
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        if (app->new_game_open && !key->repeat) {
            ask_start(app);
        }
        break;
    case SDL_SCANCODE_V:
        if (app->new_game_open && (key->mod & SDL_KMOD_CTRL) != 0) {
            seed_paste(app);
        }
        break;
    case SDL_SCANCODE_C:
        if (app->new_game_open && (key->mod & SDL_KMOD_CTRL) != 0) {
            seed_copy(app);
        }
        break;
    default: break;
    }
}

void title_screen_handle_event(App *app, const SDL_Event *event)
{
    switch (event->type) {
    case SDL_EVENT_QUIT: app->running = false; break;
    case SDL_EVENT_KEY_DOWN: handle_key_down(app, &event->key); break;
    case SDL_EVENT_TEXT_INPUT:
        if (app->new_game_open && app->new_game_stage == NEW_GAME_STAGE_NONE) {
            seed_append(app, event->text.text);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT || event->button.button == SDL_BUTTON_RIGHT) {
            ui_capture_click(&app->ui, event->button.x, event->button.y, event->button.button);
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        float steps = event->wheel.y;
        if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            steps = -steps;
        }
        ui_scroll(&app->ui, event->wheel.mouse_x, event->wheel.mouse_y, steps);
        break;
    }
    default: break;
    }
}

/* ---- Drawing helpers ------------------------------------------------------- */

static SDL_FColor fcolor(SDL_Color color, float alpha)
{
    return (SDL_FColor){color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, alpha};
}

/* A rectangle shaded from one color to another, top to bottom or left to right. */
static void gradient(SDL_Renderer *renderer, SDL_FRect rect, SDL_FColor from, SDL_FColor to,
                     bool vertical)
{
    SDL_FColor corners[4] = {from, vertical ? from : to, to, vertical ? to : from};
    SDL_FPoint points[4] = {{rect.x, rect.y},
                            {rect.x + rect.w, rect.y},
                            {rect.x + rect.w, rect.y + rect.h},
                            {rect.x, rect.y + rect.h}};
    SDL_Vertex vertices[4];
    for (int i = 0; i < 4; i++) {
        vertices[i] = (SDL_Vertex){points[i], corners[i], {0.0f, 0.0f}};
    }
    static const int INDICES[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, NULL, vertices, 4, INDICES, 6);
}

/* The veil over the world: dark and cold at the top and the bottom, lighter
 * in the middle, the sides a little darker. */
static void draw_veil(App *app)
{
    SDL_Renderer *renderer = app->renderer;
    float w = app->ui.screen_width, h = app->ui.screen_height;
    const SDL_Color night = {8, 12, 20, 255};
    gradient(renderer, (SDL_FRect){0.0f, 0.0f, w, h * 0.45f}, fcolor(night, 0.88f),
             fcolor(night, 0.38f), true);
    gradient(renderer, (SDL_FRect){0.0f, h * 0.45f, w, h * 0.2f}, fcolor(night, 0.38f),
             fcolor(night, 0.38f), true);
    gradient(renderer, (SDL_FRect){0.0f, h * 0.65f, w, h * 0.35f}, fcolor(night, 0.38f),
             fcolor(night, 0.85f), true);
    gradient(renderer, (SDL_FRect){0.0f, 0.0f, w * 0.22f, h}, fcolor(night, 0.45f),
             fcolor(night, 0.0f), false);
    gradient(renderer, (SDL_FRect){w * 0.78f, 0.0f, w * 0.22f, h}, fcolor(night, 0.0f),
             fcolor(night, 0.45f), false);
}

/* The title's fonts, (re)made for the interface size in use. */
static bool title_fonts(App *app)
{
    float scale = app->ui.theme.scale;
    if (app->title_text != NULL && app->title_subtext != NULL && app->title_text_scale == scale) {
        return true;
    }
    if (app->title_text == NULL || app->title_subtext == NULL) {
        char path[1200];
        SDL_snprintf(path, sizeof(path), "%s%s", app->assets_dir, APP_FONT_FILE);
        text_renderer_destroy(app->title_text);
        text_renderer_destroy(app->title_subtext);
        app->title_text = text_renderer_create(app->renderer, path, TITLE_POINTS * scale);
        app->title_subtext = text_renderer_create(app->renderer, path, SUBTITLE_POINTS * scale);
        if (app->title_text == NULL || app->title_subtext == NULL) {
            return false;
        }
    } else if (!text_renderer_set_size(app->title_text, TITLE_POINTS * scale) ||
               !text_renderer_set_size(app->title_subtext, SUBTITLE_POINTS * scale)) {
        return false;
    }
    app->title_text_scale = scale;
    return true;
}

/* Text with letters set apart: each character drawn by itself. Returns the
 * width (nothing drawn when `draw` is false). */
static float spaced_text(TextRenderer *text, const char *utf8, float x, float y, float spacing,
                         SDL_Color color, bool draw)
{
    float start = x;
    const char *next = utf8;
    while (*next != '\0') {
        const char *begin = next;
        SDL_StepUTF8(&next, NULL);
        char glyph[8] = {0};
        size_t length = (size_t)(next - begin);
        if (length == 0 || length >= sizeof(glyph)) {
            break;
        }
        memcpy(glyph, begin, length);
        int width = 0, height = 0;
        text_measure(text, glyph, &width, &height);
        if (draw) {
            text_draw(text, glyph, x, y, color);
        }
        x += (float)width + (*next != '\0' ? spacing : 0.0f);
    }
    return x - start;
}

static float text_width_of(TextRenderer *text, const char *utf8)
{
    int width = 0, height = 0;
    text_measure(text, utf8, &width, &height);
    return (float)width;
}

/* The name of the game, its tagline under it between two strokes; returns
 * the bottom of what was drawn. */
static float draw_title(App *app, float top)
{
    float scale = app->ui.theme.scale;
    float center = app->ui.screen_width * 0.5f;
    char name[TITLE_TEXT_SIZE];
    SDL_strlcpy(name, tr("game.title"), sizeof(name));
    for (char *c = name; *c != '\0'; c++) {
        if (*c >= 'a' && *c <= 'z') {
            *c = (char)(*c - 'a' + 'A'); /* Capitals (the name is ASCII). */
        }
    }
    float spacing = SDL_floorf(TITLE_POINTS * scale * TITLE_LETTER_SPACING);
    float width = spaced_text(app->title_text, name, 0.0f, 0.0f, spacing, ICE, false);
    float room = app->ui.screen_width * 0.92f;
    size_t letters = (size_t)SDL_utf8strlen(name);
    if (width > room && letters > 1) {
        /* A narrow window: the letters come closer (not below touching). */
        float tighter = spacing - (width - room) / (float)(letters - 1);
        spacing = SDL_max(0.0f, SDL_floorf(tighter));
        width = spaced_text(app->title_text, name, 0.0f, 0.0f, spacing, ICE, false);
    }
    float x = SDL_floorf(center - width * 0.5f);
    float shadow = SDL_ceilf(3.0f * scale);
    spaced_text(app->title_text, name, x + shadow, top + shadow * 1.3f, spacing, SHADOW, true);
    spaced_text(app->title_text, name, x, top, spacing, ICE, true);
    float y = top + (float)text_line_height(app->title_text) + SDL_floorf(10.0f * scale);

    const char *tagline = tr("title.tagline");
    float tag_width = text_width_of(app->title_subtext, tagline);
    float line = (float)text_line_height(app->title_subtext);
    float tag_x = SDL_floorf(center - tag_width * 0.5f);
    text_draw(app->title_subtext, tagline, tag_x + 1.0f, y + 1.0f, SHADOW);
    text_draw(app->title_subtext, tagline, tag_x, y, MIST);
    float stroke_y = SDL_floorf(y + line * 0.55f);
    float gap = SDL_floorf(18.0f * scale), length = SDL_floorf(ORNAMENT_LENGTH * scale);
    float thickness = SDL_max(1.0f, SDL_floorf(scale));
    SDL_SetRenderDrawColor(app->renderer, AMBER.r, AMBER.g, AMBER.b, 200);
    SDL_RenderFillRect(app->renderer,
                       &(SDL_FRect){tag_x - gap - length, stroke_y, length, thickness});
    SDL_RenderFillRect(app->renderer,
                       &(SDL_FRect){tag_x + tag_width + gap, stroke_y, length, thickness});
    /* A small diamond at the inner end of each stroke. */
    float d = SDL_floorf(3.0f * scale) + 1.0f;
    float ends[2] = {tag_x - gap + d, tag_x + tag_width + gap - d};
    for (int i = 0; i < 2; i++) {
        SDL_FColor amber = fcolor(AMBER, 0.9f);
        SDL_Vertex v[4] = {{{ends[i], stroke_y - d}, amber, {0, 0}},
                           {{ends[i] + d, stroke_y}, amber, {0, 0}},
                           {{ends[i], stroke_y + d}, amber, {0, 0}},
                           {{ends[i] - d, stroke_y}, amber, {0, 0}}};
        static const int INDICES[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(app->renderer, NULL, v, 4, INDICES, 6);
    }
    return y + line;
}

/* A large button of the title screen; true when clicked. */
static bool title_button(App *app, SDL_FRect rect, const char *label)
{
    Ui *ui = &app->ui;
    bool hover = ui_hovered(ui, rect);
    SDL_Renderer *renderer = app->renderer;
    if (hover) {
        gradient(renderer, rect, fcolor(AMBER, 0.28f), fcolor(AMBER, 0.12f), true);
    } else {
        gradient(renderer, rect, (SDL_FColor){0.05f, 0.07f, 0.11f, 0.72f},
                 (SDL_FColor){0.03f, 0.05f, 0.08f, 0.82f}, true);
    }
    SDL_Color edge = hover ? AMBER : ICE;
    SDL_SetRenderDrawColor(renderer, edge.r, edge.g, edge.b, hover ? 230 : 70);
    SDL_RenderRect(renderer, &rect);
    if (hover) {
        float bar = SDL_max(2.0f, SDL_floorf(3.0f * ui->theme.scale));
        SDL_SetRenderDrawColor(renderer, AMBER.r, AMBER.g, AMBER.b, 255);
        SDL_RenderFillRect(renderer, &(SDL_FRect){rect.x, rect.y, bar, rect.h});
    }
    float width = spaced_text(app->title_subtext, label, 0.0f, 0.0f, 1.0f, ICE, false);
    float line = (float)text_line_height(app->title_subtext);
    spaced_text(app->title_subtext, label, SDL_floorf(rect.x + (rect.w - width) * 0.5f),
                SDL_floorf(rect.y + (rect.h - line) * 0.5f), 1.0f, hover ? ICE : MIST, true);
    return ui_take_click(ui, rect) == SDL_BUTTON_LEFT;
}

/* ---- The New game window --------------------------------------------------- */

/* An option's label with its value. */
static void option_text(char *out, size_t size, NewGameOption option, int value)
{
    const NewGameOptionDef *def = new_game_option_def(option);
    if (value == 0 && def->zero_key != NULL) {
        SDL_strlcpy(out, tr(def->zero_key), size);
        return;
    }
    char number[32], shown[48];
    tr_integer(number, sizeof(number), value);
    if (def->unit == NEW_GAME_UNIT_PERCENT) {
        TR_FORMAT(shown, sizeof(shown), tr("unit.percent"), number);
    } else {
        SDL_strlcpy(shown, number, sizeof(shown));
    }
    TR_FORMAT(out, size, tr(def->name_key), shown);
}

static void tooltip_if_hovered(Ui *ui, SDL_FRect rect, const char *key)
{
    if (ui_hovered(ui, rect)) {
        const char *lines[1] = {tr(key)};
        ui_tooltip(ui, lines, 1);
    }
}

typedef struct WindowMetrics {
    float row, gap, text_offset, column, column_gap;
    float heading;        /* A section's heading and the space under it. */
    float option;         /* An option: its label, its slider, a gap. */
    float columns_height; /* The tallest column. */
    float width, height;  /* Of the whole content. */
    float random_width;   /* The Random button. */
} WindowMetrics;

static WindowMetrics window_metrics(App *app)
{
    Ui *ui = &app->ui;
    const Theme *theme = &ui->theme;
    WindowMetrics m;
    m.row = ui_row_height(ui);
    m.gap = theme->gap;
    m.text_offset = SDL_floorf((m.row - (float)text_line_height(ui->text)) * 0.5f);
    m.column_gap = SDL_floorf(COLUMN_GAP * theme->scale);
    m.heading = m.row + m.gap * 1.5f;
    m.option = m.row * 2.0f + m.gap;
    m.random_width = ui_button_width(ui, tr("new_game.seed.random"));
    /* Wide enough for every label at both ends of its range. */
    float column = COLUMN_MIN_WIDTH * theme->scale;
    char text[TEXT_SIZE];
    for (int i = 0; i < NEW_GAME_OPTION_COUNT; i++) {
        const NewGameOptionDef *def = new_game_option_def((NewGameOption)i);
        option_text(text, sizeof(text), (NewGameOption)i, def->min);
        column = SDL_max(column, ui_text_width(ui, text));
        option_text(text, sizeof(text), (NewGameOption)i, def->max);
        column = SDL_max(column, ui_text_width(ui, text));
    }
    column = SDL_max(column, m.row + m.gap + ui_text_width(ui, tr("new_game.advanced")));
    for (int s = 0; s < NEW_GAME_SECTION_COUNT; s++) {
        column = SDL_max(column, ui_text_width(ui, tr(new_game_section_key((NewGameSection)s))));
    }
    m.column = SDL_ceilf(column);
    /* Column heights: the World column has the seed, a column with advanced
     * options the row that shows them (and them when shown). */
    float heights[NEW_GAME_SECTION_COUNT] = {0};
    bool advanced[NEW_GAME_SECTION_COUNT] = {false};
    for (int i = 0; i < NEW_GAME_OPTION_COUNT; i++) {
        const NewGameOptionDef *def = new_game_option_def((NewGameOption)i);
        if (def->advanced) {
            advanced[def->section] = true;
        }
        if (!def->advanced || app->new_game_advanced) {
            heights[def->section] += m.option;
        }
    }
    m.columns_height = 0.0f;
    for (int s = 0; s < NEW_GAME_SECTION_COUNT; s++) {
        heights[s] += m.heading + (advanced[s] ? m.row + m.gap : 0.0f);
        if (s == NEW_GAME_SECTION_WORLD) {
            heights[s] += m.row * 2.0f + m.gap;
        }
        m.columns_height = SDL_max(m.columns_height, heights[s]);
    }
    m.width = m.column * NEW_GAME_SECTION_COUNT + m.column_gap * (NEW_GAME_SECTION_COUNT - 1);
    m.height = m.columns_height + m.gap * 2.0f + m.row;
    return m;
}

/* A section's heading, a thin amber stroke under it; returns the y below. */
static float draw_heading(App *app, const WindowMetrics *m, NewGameSection section, float x,
                          float y)
{
    Ui *ui = &app->ui;
    ui_label_rgba(ui, tr(new_game_section_key(section)), x, y + m->text_offset, AMBER);
    float stroke = SDL_max(1.0f, SDL_floorf(ui->theme.scale));
    ui_fill(ui, (SDL_FRect){x, y + m->row + SDL_floorf(m->gap * 0.25f), m->column, stroke},
            (SDL_Color){AMBER.r, AMBER.g, AMBER.b, 110});
    return y + m->heading;
}

/* An option: its label with the value, a slider under it (dimmed, without
 * slider, when it makes no difference); its note when hovered. */
static float draw_option(App *app, const WindowMetrics *m, NewGameOption option, float x, float y)
{
    Ui *ui = &app->ui;
    const NewGameOptionDef *def = new_game_option_def(option);
    int value = new_game_option_value(&app->new_game, option);
    bool applies = new_game_option_applies(&app->new_game, option);
    char text[TEXT_SIZE];
    option_text(text, sizeof(text), option, value);
    if (applies) {
        ui_label(ui, text, x, y + m->text_offset);
    } else {
        ui_label_dim(ui, text, x, y + m->text_offset);
    }
    SDL_FRect track = {x, y + m->row, m->column, m->row};
    if (applies) {
        int steps = (def->max - def->min) / def->step + 1;
        int index = (value - def->min) / def->step;
        if (ui_slider(ui, SLIDER_ID_BASE + (int)option, track, steps, &index)) {
            new_game_option_set(&app->new_game, option, def->min + index * def->step);
        }
    } else {
        float height = ui->theme.slider_track_height;
        ui_fill(ui,
                (SDL_FRect){x, SDL_floorf(track.y + (track.h - height) * 0.5f), track.w, height},
                ui->theme.colors[THEME_TRACK]);
    }
    tooltip_if_hovered(ui, (SDL_FRect){x, y, m->column, m->row * 2.0f}, def->note_key);
    return y + m->option;
}

/* The seed field: what was typed (its end, if too long), or a dim word when
 * empty, and a blinking caret. */
static void draw_seed_field(App *app, SDL_FRect rect)
{
    Ui *ui = &app->ui;
    ui_fill(ui, rect, (SDL_Color){0, 0, 0, 110});
    SDL_SetRenderDrawColor(app->renderer, AMBER.r, AMBER.g, AMBER.b, 170);
    SDL_RenderRect(app->renderer, &rect);
    float pad = ui->theme.button_padding;
    float room = rect.w - pad * 2.0f;
    float line = (float)text_line_height(ui->text);
    float text_y = SDL_floorf(rect.y + (rect.h - line) * 0.5f);
    const char *shown = app->new_game_seed;
    while (*shown != '\0' && ui_text_width(ui, shown) > room) {
        SDL_StepUTF8(&shown, NULL); /* Drops the first characters until the end fits. */
    }
    float caret_x = rect.x + pad;
    if (*shown == '\0' && app->new_game_seed[0] == '\0') {
        ui_label_dim(ui, tr("new_game.seed.empty"), rect.x + pad, text_y);
    } else {
        ui_label(ui, shown, rect.x + pad, text_y);
        caret_x += ui_text_width(ui, shown);
    }
    if ((SDL_GetTicks() / 530) % 2 == 0) {
        ui_fill(ui, (SDL_FRect){caret_x + 1.0f, text_y, SDL_max(1.0f, ui->theme.scale), line}, ICE);
    }
    /* A right click opens the menu: copy the seed, or paste another over it. */
    bool menu = ui_take_click(ui, rect) == SDL_BUTTON_RIGHT;
    const char *options[SEED_MENU_COUNT] = {tr("new_game.seed.copy"), tr("new_game.seed.paste")};
    int picked = ui_popup_list(ui, SEED_MENU_ID, rect, menu, false, options, SEED_MENU_COUNT, -1);
    if (picked == SEED_MENU_COPY) {
        seed_copy(app);
    } else if (picked == SEED_MENU_PASTE) {
        seed_paste(app);
    }
}

/* The row that shows or hides the advanced options of a column: a small
 * triangle and its label, both clickable. True when clicked. */
static bool draw_advanced_toggle(App *app, const WindowMetrics *m, float x, float y)
{
    Ui *ui = &app->ui;
    float box = SDL_floorf(m->row * 0.7f);
    SDL_FRect triangle = {x, y + SDL_floorf((m->row - box) * 0.5f), box, box};
    bool clicked = ui_disclosure(ui, triangle, app->new_game_advanced);
    SDL_FRect row = {x, y, m->column, m->row};
    ui_label_rgba(ui, tr("new_game.advanced"), x + box + m->gap, y + m->text_offset,
                  ui_hovered(ui, row) ? ICE : MIST);
    clicked = ui_take_click(ui, row) == SDL_BUTTON_LEFT || clicked;
    tooltip_if_hovered(ui, row, "new_game.advanced.note");
    return clicked;
}

typedef enum NewGameChoice {
    CHOICE_NONE,
    CHOICE_DEFAULTS,
    CHOICE_BACK,
    CHOICE_START,
} NewGameChoice;

static void draw_new_game(App *app)
{
    Ui *ui = &app->ui;
    WindowMetrics m = window_metrics(app);
    bool open = true;
    SDL_FPoint at = ui_window_begin(ui, APP_WINDOW_NEW_GAME, tr("title.new_game"), m.width,
                                    m.height, UI_PLACE_CENTER, &open);
    NewGameChoice choice = CHOICE_NONE;
    for (int s = 0; s < NEW_GAME_SECTION_COUNT; s++) {
        float x = at.x + (float)s * (m.column + m.column_gap);
        float y = draw_heading(app, &m, (NewGameSection)s,
                               at.x + (float)s * (m.column + m.column_gap), at.y);
        if (s == NEW_GAME_SECTION_WORLD) {
            SDL_FRect label = {x, y, m.column, m.row};
            ui_label(ui, tr("new_game.seed"), x, y + m.text_offset);
            y += m.row;
            SDL_FRect field = {x, y, m.column - m.random_width - m.gap, m.row};
            draw_seed_field(app, field);
            if (ui_button(ui, (SDL_FRect){x + m.column - m.random_width, y, m.random_width, m.row},
                          tr("new_game.seed.random"))) {
                SDL_snprintf(app->new_game_seed, sizeof(app->new_game_seed), "%" SDL_PRIu64,
                             app_random_seed() % 1000000000u);
            }
            label.h += m.row;
            label.w = field.w;
            tooltip_if_hovered(ui, label, "new_game.seed.note");
            y += m.row + m.gap;
        }
        bool has_advanced = false;
        for (int i = 0; i < NEW_GAME_OPTION_COUNT; i++) {
            const NewGameOptionDef *def = new_game_option_def((NewGameOption)i);
            if (def->section == (NewGameSection)s) {
                has_advanced = has_advanced || def->advanced;
                if (!def->advanced) {
                    y = draw_option(app, &m, (NewGameOption)i, x, y);
                }
            }
        }
        if (!has_advanced) {
            continue;
        }
        if (draw_advanced_toggle(app, &m, x, y)) {
            app->new_game_advanced = !app->new_game_advanced;
        }
        y += m.row + m.gap;
        for (int i = 0; app->new_game_advanced && i < NEW_GAME_OPTION_COUNT; i++) {
            const NewGameOptionDef *def = new_game_option_def((NewGameOption)i);
            if (def->section == (NewGameSection)s && def->advanced) {
                y = draw_option(app, &m, (NewGameOption)i, x, y);
            }
        }
    }
    /* The buttons along the bottom: Defaults on the left, Back and Start on the right. */
    float y = at.y + m.columns_height + m.gap;
    ui_fill(ui, (SDL_FRect){at.x, y, m.width, SDL_max(1.0f, SDL_floorf(ui->theme.scale))},
            ui->theme.colors[THEME_BORDER]);
    y += m.gap;
    const char *start = tr("new_game.start"), *back = tr("new_game.back");
    float start_width = SDL_max(ui_button_width(ui, start), 120.0f * ui->theme.scale);
    float back_width = ui_button_width(ui, back);
    float right = at.x + m.width;
    if (ui_button(ui, (SDL_FRect){at.x, y, ui_button_width(ui, tr("new_game.defaults")), m.row},
                  tr("new_game.defaults"))) {
        choice = CHOICE_DEFAULTS;
    }
    if (ui_button(ui, (SDL_FRect){right - start_width - m.gap - back_width, y, back_width, m.row},
                  back)) {
        choice = CHOICE_BACK;
    }
    if (ui_toggle_button(ui, (SDL_FRect){right - start_width, y, start_width, m.row}, start,
                         true)) {
        choice = CHOICE_START;
    }
    ui_window_end(ui);

    switch (choice) {
    case CHOICE_DEFAULTS: new_game_default_params(&app->new_game); break;
    case CHOICE_BACK: close_new_game(app); break;
    case CHOICE_START: ask_start(app); break;
    default: break;
    }
    if (!open) {
        close_new_game(app);
    }
}

/* ---- The screen ------------------------------------------------------------- */

void title_screen_draw(App *app)
{
    Ui *ui = &app->ui;
    float scale = ui->theme.scale;
    draw_veil(app);
    if (!title_fonts(app)) {
        return;
    }
    bool window = app->new_game_open || app->show_settings || app->show_saves;
    float h = ui->screen_height, w = ui->screen_width;
    float bottom = 0.0f;
    if (!window) {
        bottom = draw_title(app, SDL_floorf(h * 0.17f));
    }
    if (app->new_game_stage != NEW_GAME_STAGE_NONE) {
        /* The world is being made: a line where the buttons were, drawn once
         * before the work (it takes a moment). */
        const char *text = tr("title.generating");
        float width = spaced_text(app->title_subtext, text, 0.0f, 0.0f, 1.0f, ICE, false);
        float y = SDL_floorf(SDL_max(bottom + 60.0f * scale, h * 0.55f));
        spaced_text(app->title_subtext, text, SDL_floorf((w - width) * 0.5f), y, 1.0f, ICE, true);
        if (app->new_game_stage == NEW_GAME_STAGE_ASKED) {
            app->new_game_stage = NEW_GAME_STAGE_GENERATE;
        }
    } else if (app->new_game_open) {
        draw_new_game(app);
    } else if (app->show_settings) {
        settings_window_draw(app);
    } else if (app->show_saves) {
        saves_window_draw(app);
    } else {
        float button_w = SDL_floorf(BUTTON_WIDTH * scale);
        float button_h = SDL_floorf(BUTTON_HEIGHT * scale);
        float gap = SDL_floorf(BUTTON_GAP * scale);
        const char *labels[TITLE_BUTTONS] = {tr("title.new_game"), tr("title.load_game"),
                                             tr("settings.title"), tr("title.quit")};
        for (int i = 0; i < TITLE_BUTTONS; i++) {
            button_w = SDL_max(button_w, text_width_of(app->title_subtext, labels[i]) +
                                             4.0f * ui->theme.button_padding);
        }
        float x = SDL_floorf((w - button_w) * 0.5f);
        float y = SDL_floorf(SDL_max(bottom + 60.0f * scale, h * 0.52f));
        ui_region(
            ui, (SDL_FRect){x, y, button_w, button_h * TITLE_BUTTONS + gap * (TITLE_BUTTONS - 1)});
        for (int i = 0; i < TITLE_BUTTONS; i++) {
            SDL_FRect rect = {x, y + (float)i * (button_h + gap), button_w, button_h};
            if (title_button(app, rect, labels[i])) {
                switch (i) {
                case 0: open_new_game(app); break;
                case 1: saves_window_open(app, true); break;
                case 2: app->show_settings = true; break;
                default: app->running = false; break;
                }
            }
        }
    }
    char version[64];
    TR_FORMAT(version, sizeof(version), tr("title.version"), COLONY_VERSION);
    float margin = ui->theme.margin;
    ui_label_rgba(ui, version, w - margin - ui_text_width(ui, version),
                  h - margin - (float)text_line_height(ui->text),
                  (SDL_Color){MIST.r, MIST.g, MIST.b, 150});
    hud_draw_message(app, margin * 2.0f);
    ui_draw_popup(ui);
    ui_draw_tooltip(ui);
}
