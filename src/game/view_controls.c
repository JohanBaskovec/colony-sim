#include "game/view_controls.h"

#include "core/i18n.h"
#include "game/layout.h"

#define TEXT_SIZE 256
#define LEVEL_SLIDER_ID 100
#define SLIDER_HEIGHT 200.0f /* At scale 1. */
#define ICON_TRIANGLES 3

typedef struct CornerLayout {
    SDL_FRect row;    /* Overlay toggles and speed buttons. */
    SDL_FRect column; /* Level slider, its buttons and its number. */
    float button;     /* Side of the square buttons. */
    float small;      /* Side of the level buttons (half of the others). */
} CornerLayout;

static float level_label_width(const Ui *ui)
{
    char widest[32];
    TR_FORMAT(widest, sizeof(widest), tr("controls.level_count"), "00", "00");
    int width = 0, height = 0;
    text_measure(ui->text, widest, &width, &height);
    return (float)width;
}

static void corner_layout(const App *app, CornerLayout *out)
{
    const Ui *ui = &app->ui;
    const Theme *theme = &ui->theme;
    ScreenLayout layout;
    layout_screen(ui, &layout);
    float button = layout.button_height, gap = theme->gap;
    /* Overlays (1), a wider gap, then the speeds (pause and the running ones). */
    float row_width = (1.0f + GAME_SPEED_COUNT) * button + (float)GAME_SPEED_COUNT * gap +
                      3.0f * gap + 2.0f * gap;
    out->button = button;
    out->row = (SDL_FRect){layout.width - theme->margin - row_width,
                           layout.bar_top - theme->margin - button - 2.0f * gap, row_width,
                           button + 2.0f * gap};
    float line = (float)text_line_height(ui->text);
    float slider = SDL_roundf(SLIDER_HEIGHT * theme->scale);
    float small = SDL_roundf(button * 0.5f);
    out->small = small;
    float width = SDL_max(small, level_label_width(ui)) + gap;
    float height = 2.0f * small + slider + line + 5.0f * gap;
    out->column = (SDL_FRect){layout.width - theme->margin - width,
                              out->row.y - theme->margin - height, width, height};
}

void view_controls_info_corner(const App *app, float *right, float *bottom)
{
    CornerLayout layout;
    corner_layout(app, &layout);
    *right = layout.column.x - app->ui.theme.margin;
    *bottom = layout.row.y - app->ui.theme.margin;
}

/* ---- Icons ------------------------------------------------------------------ */

static void triangle(SDL_Renderer *renderer, float x, float y, float w, float h, SDL_Color color)
{
    SDL_FColor c = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
    SDL_Vertex vertices[3] = {
        {{x, y}, c, {0.0f, 0.0f}},
        {{x + w, y + h * 0.5f}, c, {0.0f, 0.0f}},
        {{x, y + h}, c, {0.0f, 0.0f}},
    };
    SDL_RenderGeometry(renderer, NULL, vertices, 3, NULL, 0);
}

/* Two bars (pause), a small triangle (×0.3), a larger one (×0.5), a full one
 * (×1), or two or three side by side (the fast speeds). */
static void speed_icon(Ui *ui, SDL_FRect rect, GameSpeed speed)
{
    SDL_Color color = ui->theme.colors[THEME_TEXT];
    int count = speed == GAME_SPEED_FAST ? 2 : (speed == GAME_SPEED_FASTEST ? 3 : 1);
    float scale = speed == GAME_SPEED_SLOWEST ? 0.55f : (speed == GAME_SPEED_SLOW ? 0.78f : 1.0f);
    float h = SDL_floorf(rect.h * 0.45f * scale);
    float y = SDL_floorf(rect.y + (rect.h - h) * 0.5f);
    if (speed == GAME_SPEED_PAUSED) {
        float bar = SDL_max(2.0f, SDL_floorf(rect.w * 0.12f));
        float cx = rect.x + rect.w * 0.5f;
        ui_fill(ui, (SDL_FRect){SDL_floorf(cx - bar * 1.5f), y, bar, h}, color);
        ui_fill(ui, (SDL_FRect){SDL_floorf(cx + bar * 0.5f), y, bar, h}, color);
        return;
    }
    float w = SDL_floorf(h * 0.6f);
    float x = SDL_floorf(rect.x + (rect.w - w * (float)count) * 0.5f);
    for (int i = 0; i < count; i++) {
        triangle(ui->renderer, x + w * (float)i, y, w, h, color);
    }
}

/* A thermometer: a stem and a bulb, half red. */
static void temperature_icon(Ui *ui, SDL_FRect rect)
{
    static const SDL_Color RED = {220, 70, 60, 255};
    SDL_Color glass = ui->theme.colors[THEME_TEXT];
    float stem_w = SDL_max(3.0f, SDL_floorf(rect.w * 0.16f));
    float bulb = SDL_floorf(stem_w * 2.2f);
    float cx = SDL_floorf(rect.x + rect.w * 0.5f);
    float top = SDL_floorf(rect.y + rect.h * 0.18f);
    float bulb_y = SDL_floorf(rect.y + rect.h * 0.82f - bulb);
    ui_fill(ui, (SDL_FRect){cx - stem_w * 0.5f, top, stem_w, bulb_y - top + 1.0f}, glass);
    ui_fill(ui, (SDL_FRect){cx - bulb * 0.5f, bulb_y, bulb, bulb}, RED);
    float mercury = SDL_floorf((bulb_y - top) * 0.5f);
    ui_fill(ui, (SDL_FRect){cx - stem_w * 0.5f + 1.0f, bulb_y - mercury, stem_w - 2.0f, mercury},
            RED);
}

/* ---- Widgets -------------------------------------------------------------- */

static void tooltip(App *app, const char *title, const char *detail, SDL_Scancode key)
{
    const char *lines[3] = {title};
    int count = 1;
    if (detail != NULL) {
        lines[count++] = detail;
    }
    char shortcut[TEXT_SIZE];
    if (key != SDL_SCANCODE_UNKNOWN) {
        TR_FORMAT(shortcut, sizeof(shortcut), tr("menu.shortcut"), app_key_name(key));
        lines[count++] = shortcut;
    }
    ui_tooltip(&app->ui, lines, count);
}

static void draw_row(App *app, const CornerLayout *layout)
{
    Ui *ui = &app->ui;
    float gap = ui->theme.gap, button = layout->button;
    ui_panel(ui, layout->row);
    float x = layout->row.x + gap, y = layout->row.y + gap;

    SDL_FRect rect = {x, y, button, button};
    if (ui_icon_button(ui, rect, app->show_temperature)) {
        app->show_temperature = !app->show_temperature;
    }
    temperature_icon(ui, rect);
    if (ui_hovered(ui, rect)) {
        tooltip(app, tr("controls.temperature"), tr("controls.temperature.description"),
                APP_KEY_TEMPERATURE);
    }
    x += button + 4.0f * gap;

    static const SDL_Scancode SPEED_KEYS[GAME_SPEED_COUNT] = {
        [GAME_SPEED_PAUSED] = APP_KEY_PAUSE,      [GAME_SPEED_SLOWEST] = SDL_SCANCODE_UNKNOWN,
        [GAME_SPEED_SLOW] = SDL_SCANCODE_UNKNOWN, [GAME_SPEED_NORMAL] = SDL_SCANCODE_1,
        [GAME_SPEED_FAST] = SDL_SCANCODE_2,       [GAME_SPEED_FASTEST] = SDL_SCANCODE_3};
    GameClock *clock = &app->game.clock;
    for (int speed = 0; speed < GAME_SPEED_COUNT; speed++) {
        rect = (SDL_FRect){x, y, button, button};
        if (ui_icon_button(ui, rect, (int)clock->speed == speed)) {
            if (speed == GAME_SPEED_PAUSED) {
                game_clock_toggle_pause(clock);
            } else {
                game_clock_set_speed(clock, (GameSpeed)speed);
            }
        }
        speed_icon(ui, rect, (GameSpeed)speed);
        if (ui_hovered(ui, rect)) {
            char title[TEXT_SIZE];
            if (speed == GAME_SPEED_PAUSED) {
                SDL_strlcpy(title, tr("controls.pause"), sizeof(title));
            } else {
                char multiplier[16];
                float factor = game_speed_multiplier((GameSpeed)speed);
                if (factor == SDL_floorf(factor)) {
                    tr_integer(multiplier, sizeof(multiplier), (long long)factor);
                } else {
                    tr_decimal(multiplier, sizeof(multiplier), factor, 1);
                }
                TR_FORMAT(title, sizeof(title), tr("controls.speed"), multiplier);
            }
            tooltip(app, title, NULL, SPEED_KEYS[speed]);
        }
        x += button + gap;
    }
}

static void level_tooltip(App *app)
{
    char detail[TEXT_SIZE];
    TR_FORMAT(detail, sizeof(detail), tr("controls.level.description"),
              app_key_name(APP_KEY_LEVEL_UP), app_key_name(APP_KEY_LEVEL_DOWN));
    tooltip(app, tr("controls.level"), detail, SDL_SCANCODE_UNKNOWN);
}

static void draw_level_column(App *app, const CornerLayout *layout)
{
    Ui *ui = &app->ui;
    float gap = ui->theme.gap, button = layout->small;
    const SDL_FRect column = layout->column;
    int depth = app->game.map.depth;
    ui_panel(ui, column);
    float bx = SDL_floorf(column.x + (column.w - button) * 0.5f);

    SDL_FRect up = {bx, column.y + gap, button, button};
    if (ui_toggle_button(ui, up, "+", false) && app->camera.level < depth - 1) {
        app->camera.level++;
    }
    if (ui_hovered(ui, up)) {
        tooltip(app, tr("controls.level_up"), NULL, APP_KEY_LEVEL_UP);
    }
    float line = (float)text_line_height(ui->text);
    float slider_height = column.h - 2.0f * button - line - 5.0f * gap;
    SDL_FRect slider = {bx, up.y + button + gap, button, slider_height};
    int level = app->camera.level;
    if (ui_vertical_slider(ui, LEVEL_SLIDER_ID, slider, depth, app->game.map.surface_level,
                           &level)) {
        app->camera.level = level;
    }
    if (ui_hovered(ui, slider)) {
        level_tooltip(app);
    }
    SDL_FRect down = {bx, slider.y + slider_height + gap, button, button};
    if (ui_toggle_button(ui, down, "−", false) && app->camera.level > 0) {
        app->camera.level--;
    }
    if (ui_hovered(ui, down)) {
        tooltip(app, tr("controls.level_down"), NULL, APP_KEY_LEVEL_DOWN);
    }

    char current[16], count[16], text[32];
    tr_integer(current, sizeof(current), app->camera.level + 1);
    tr_integer(count, sizeof(count), depth);
    TR_FORMAT(text, sizeof(text), tr("controls.level_count"), current, count);
    float width = ui_text_width(ui, text);
    SDL_FRect label = {column.x, down.y + button + gap, column.w, line};
    ui_label(ui, text, SDL_floorf(column.x + (column.w - width) * 0.5f), label.y);
    if (ui_hovered(ui, label)) {
        level_tooltip(app);
    }
}

void view_controls_draw(App *app)
{
    CornerLayout layout;
    corner_layout(app, &layout);
    draw_row(app, &layout);
    draw_level_column(app, &layout);
}
