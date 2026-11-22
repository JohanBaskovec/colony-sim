#include "game/areas_window.h"

#include "core/i18n.h"
#include "game/colonist_bar.h"
#include "game/layout.h"

#define TEXT_SIZE 256

typedef enum AreaAction {
    ACTION_RENAME,
    ACTION_INVERT,
    ACTION_EXTEND,
    ACTION_SHRINK,
    ACTION_DELETE,
    ACTION_COUNT
} AreaAction;

static const char *const ACTION_KEYS[ACTION_COUNT] = {
    [ACTION_RENAME] = N_("areas.rename"), [ACTION_INVERT] = N_("areas.invert"),
    [ACTION_EXTEND] = N_("areas.extend"), [ACTION_SHRINK] = N_("areas.shrink"),
    [ACTION_DELETE] = N_("areas.delete"),
};

static const char *const ACTION_DESCRIPTION_KEYS[ACTION_COUNT] = {
    [ACTION_RENAME] = N_("areas.rename.description"),
    [ACTION_INVERT] = N_("areas.invert.description"),
    [ACTION_EXTEND] = N_("areas.extend.description"),
    [ACTION_SHRINK] = N_("areas.shrink.description"),
    [ACTION_DELETE] = N_("areas.delete.description"),
};

void areas_window_area_name(const App *app, int index, char *out, size_t size)
{
    const AllowedArea *area = allowed_areas_get(&app->game.areas, index);
    if (area == NULL) {
        out[0] = '\0';
        return;
    }
    SDL_strlcpy(out, area->name[0] != '\0' ? area->name : tr("areas.default_name"), size);
}

/* ---- Naming ----------------------------------------------------------------- */

static void start_naming(App *app, int index, const char *text)
{
    if (app->area_naming == ALLOWED_AREA_NONE) {
        SDL_StartTextInput(app->window);
    }
    app->area_naming = index;
    SDL_strlcpy(app->area_name, text, sizeof(app->area_name));
    app->area_name_fresh = true;
}

static void stop_naming(App *app)
{
    if (app->area_naming != ALLOWED_AREA_NONE) {
        app->area_naming = ALLOWED_AREA_NONE;
        SDL_StopTextInput(app->window);
    }
}

/* The name proposed for a new area: "Area 2", "Area 3"... */
static void new_area_name(const App *app, char *out, size_t size)
{
    char number[16];
    tr_integer(number, sizeof(number), app->game.areas.count + 1);
    TR_FORMAT(out, size, tr("areas.new_name"), number);
}

/* Paints the area: the tool that adds tiles to it (or takes them away). */
static void paint(App *app, int index, int mode)
{
    app->area_selected = index;
    if (!app_tool_active(app, APP_TOOL_AREA, mode)) {
        app_toggle_tool(app, APP_TOOL_AREA, mode);
    }
}

/* Enter: the name typed is kept; a new area is made, to be painted. An empty name changes
 * nothing (a new area takes the name proposed). */
static void finish_naming(App *app)
{
    int index = app->area_naming;
    char name[ALLOWED_AREA_NAME_SIZE];
    SDL_strlcpy(name, app->area_name, sizeof(name));
    stop_naming(app);
    bool blank = true;
    for (const char *c = name; *c != '\0' && blank; c++) {
        blank = *c == ' ';
    }
    if (index == ALLOWED_AREA_MAX) {
        if (blank) {
            new_area_name(app, name, sizeof(name));
        }
        int added = game_add_area(&app->game, name);
        if (added != ALLOWED_AREA_NONE) {
            paint(app, added, APP_ZONE_EXTEND);
        }
    } else if (!blank) {
        game_rename_area(&app->game, index, name);
    }
}

static void name_append(App *app, const char *text)
{
    for (const char *c = text; *c != '\0'; c++) {
        if ((unsigned char)*c < 0x20 || *c == 0x7f) {
            return; /* No control characters. */
        }
    }
    if (app->area_name_fresh) { /* What was proposed gives way to what is typed. */
        app->area_name[0] = '\0';
        app->area_name_fresh = false;
    }
    size_t length = strlen(app->area_name), added = strlen(text);
    if (length + added < sizeof(app->area_name)) {
        memcpy(app->area_name + length, text, added + 1);
    }
}

/* Takes the last character off (all the bytes of it). */
static void name_erase(App *app)
{
    if (app->area_name_fresh) {
        app->area_name[0] = '\0';
        app->area_name_fresh = false;
        return;
    }
    size_t length = strlen(app->area_name);
    while (length > 0 && ((unsigned char)app->area_name[length - 1] & 0xC0) == 0x80) {
        length--;
    }
    if (length > 0) {
        length--;
    }
    app->area_name[length] = '\0';
}

bool areas_window_handle_event(App *app, const SDL_Event *event)
{
    if (app->area_naming == ALLOWED_AREA_NONE) {
        return false;
    }
    switch (event->type) {
    case SDL_EVENT_TEXT_INPUT: name_append(app, event->text.text); return true;
    case SDL_EVENT_KEY_DOWN:
        switch (event->key.scancode) {
        case SDL_SCANCODE_BACKSPACE: name_erase(app); break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
            if (!event->key.repeat) {
                finish_naming(app);
            }
            break;
        case SDL_SCANCODE_ESCAPE: stop_naming(app); break;
        case SDL_SCANCODE_V:
            if ((event->key.mod & SDL_KMOD_CTRL) != 0) {
                char *text = SDL_GetClipboardText();
                if (text != NULL) {
                    name_append(app, text);
                    SDL_free(text);
                }
            }
            break;
        default: break; /* Typing: no shortcut acts on the game. */
        }
        return true;
    default: return false;
    }
}

void areas_window_set_open(App *app, bool open)
{
    if (open) {
        app->show_work_panel = false; /* One main window at a time. */
        app->show_log = false;
        app->build_menu_open = false;
    } else {
        stop_naming(app);
        if (app->tool == APP_TOOL_AREA) {
            app->tool = APP_TOOL_NONE;
        }
    }
    app->show_areas = open;
}

/* ---- Drawing ---------------------------------------------------------------- */

/* The name being typed (its end, if too long) and a blinking caret. */
static void draw_name_field(App *app, SDL_FRect rect)
{
    Ui *ui = &app->ui;
    ui_fill(ui, rect, (SDL_Color){0, 0, 0, 110});
    SDL_Color border = ui->theme.colors[THEME_BORDER_LIT];
    SDL_SetRenderDrawColor(app->renderer, border.r, border.g, border.b, border.a);
    SDL_RenderRect(app->renderer, &rect);
    float pad = ui->theme.button_padding;
    float line = (float)text_line_height(ui->text);
    float text_y = SDL_floorf(rect.y + (rect.h - line) * 0.5f);
    const char *shown = app->area_name;
    while (*shown != '\0' && ui_text_width(ui, shown) > rect.w - pad * 2.0f) {
        SDL_StepUTF8(&shown, NULL); /* Drops the first characters until the end fits. */
    }
    if (app->area_name_fresh && *shown != '\0') { /* Selected: typing replaces it. */
        ui_fill(ui, (SDL_FRect){rect.x + pad, text_y, ui_text_width(ui, shown), line},
                ui->theme.colors[THEME_BUTTON_LIT]);
    }
    ui_label(ui, shown, rect.x + pad, text_y);
    if ((SDL_GetTicks() / 530) % 2 == 0) {
        float caret_x = rect.x + pad + ui_text_width(ui, shown);
        ui_fill(ui, (SDL_FRect){caret_x + 1.0f, text_y, SDL_max(1.0f, ui->theme.scale), line},
                ui->theme.colors[THEME_TEXT]);
    }
}

static void tooltip_if_hovered(Ui *ui, SDL_FRect rect, const char *title, const char *text)
{
    if (ui_hovered(ui, rect)) {
        const char *lines[2] = {title, text};
        ui_tooltip(ui, lines, text != NULL ? 2 : 1);
    }
}

/* "Ana, Bram, Cole" (cut to the width), or "none". */
static void settler_names(const App *app, int index, float width, char *out, size_t size)
{
    char names[TEXT_SIZE * 2] = "";
    const Game *game = &app->game;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        const Colonist *colonist = game_colonist_const(game, game_colonist_id_at(game, slot));
        if (colonist == NULL || colonist->area != index) {
            continue;
        }
        if (names[0] == '\0') {
            SDL_strlcpy(names, colonist->name, sizeof(names));
        } else {
            char longer[sizeof(names)];
            TR_FORMAT(longer, sizeof(longer), tr("areas.settler_list"), names, colonist->name);
            SDL_strlcpy(names, longer, sizeof(names));
        }
    }
    char line[TEXT_SIZE * 2];
    TR_FORMAT(line, sizeof(line), tr("areas.settlers"),
              names[0] != '\0' ? names : tr("areas.settlers.none"));
    ui_fit_text((Ui *)&app->ui, line, width, out, size);
}

/* The block of one area: its name, its actions, its size and its settlers. Returns where the
 * next one goes. */
static float draw_area(App *app, int index, float x, float y, float width,
                       const float action_width[ACTION_COUNT])
{
    Ui *ui = &app->ui;
    const AllowedAreas *areas = &app->game.areas;
    const AllowedArea *area = allowed_areas_get(areas, index);
    float row = ui_row_height(ui), gap = ui->theme.gap;
    float text_offset = SDL_floorf((row - (float)text_line_height(app->text)) * 0.5f);

    /* The name: a click shows the area on the map. */
    SDL_FRect name_rect = {x, y, width, row};
    if (app->area_naming == index) {
        draw_name_field(app, name_rect);
    } else {
        if (index == app->area_selected) {
            ui_fill(ui, name_rect, ui->theme.colors[THEME_BUTTON_LIT]);
        }
        char name[TEXT_SIZE], fitted[TEXT_SIZE];
        areas_window_area_name(app, index, name, sizeof(name));
        if (index == 0) {
            char with_default[TEXT_SIZE];
            TR_FORMAT(with_default, sizeof(with_default), tr("areas.default_mark"), name);
            SDL_strlcpy(name, with_default, sizeof(name));
        }
        ui_fit_text(ui, name, width - ui->theme.button_padding, fitted, sizeof(fitted));
        ui_label(ui, fitted, x + ui->theme.button_padding * 0.5f, y + text_offset);
        if (ui_take_click(ui, name_rect) == SDL_BUTTON_LEFT) {
            app->area_selected = index;
        }
        tooltip_if_hovered(ui, name_rect, tr("areas.show"), NULL);
        if (ui_hovered(ui, name_rect)) {
            app->area_preview = (AreaPreview){.kind = AREA_PREVIEW_ALLOWED, .stockpile = index};
        }
    }
    y += row + gap * 0.5f;

    /* Its actions. */
    float bx = x;
    for (int i = 0; i < ACTION_COUNT; i++) {
        SDL_FRect rect = {bx, y, action_width[i], row};
        bx += action_width[i] + gap;
        bool lit = index == app->area_selected &&
                   ((i == ACTION_EXTEND && app_tool_active(app, APP_TOOL_AREA, APP_ZONE_EXTEND)) ||
                    (i == ACTION_SHRINK && app_tool_active(app, APP_TOOL_AREA, APP_ZONE_SHRINK)) ||
                    (i == ACTION_RENAME && app->area_naming == index));
        bool veiled = i == ACTION_DELETE && areas->count <= 1;
        bool clicked = ui_toggle_button(ui, rect, tr(ACTION_KEYS[i]), lit);
        if (veiled) {
            ui_fill(ui, rect, ui->theme.colors[THEME_DISABLED_VEIL]);
        }
        tooltip_if_hovered(ui, rect, tr(ACTION_KEYS[i]),
                           veiled ? tr("areas.delete.last") : tr(ACTION_DESCRIPTION_KEYS[i]));
        if (!clicked || veiled) {
            continue;
        }
        switch ((AreaAction)i) {
        case ACTION_RENAME: {
            char name[TEXT_SIZE];
            areas_window_area_name(app, index, name, sizeof(name));
            start_naming(app, index, name);
            break;
        }
        case ACTION_INVERT: game_invert_area(&app->game, index); break;
        case ACTION_EXTEND:
        case ACTION_SHRINK: {
            int mode = i == ACTION_EXTEND ? APP_ZONE_EXTEND : APP_ZONE_SHRINK;
            if (lit) {
                app->tool = APP_TOOL_NONE; /* Put away. */
            } else {
                paint(app, index, mode);
            }
            break;
        }
        case ACTION_DELETE:
            if (game_remove_area(&app->game, index)) {
                stop_naming(app);
                if (app->area_selected == index) {
                    app->area_selected = 0;
                    if (app->tool == APP_TOOL_AREA) {
                        app->tool = APP_TOOL_NONE;
                    }
                } else if (app->area_selected > index) {
                    app->area_selected--; /* The same area, one place up. */
                }
            }
            break;
        case ACTION_COUNT: break;
        }
    }
    y += row + gap * 0.5f;

    /* Its size and its settlers. */
    char count[32], line[TEXT_SIZE];
    long tiles = area != NULL ? area->tiles : 0;
    tr_integer(count, sizeof(count), tiles);
    TR_FORMAT(line, sizeof(line), tr_plural("areas.size", tiles), count);
    ui_label_dim(ui, line, x, y + text_offset);
    y += row;
    settler_names(app, index, width, line, sizeof(line));
    ui_label_dim(ui, line, x, y + text_offset);
    return y + row + gap * 2.0f;
}

void areas_window_draw(App *app)
{
    if (!app->show_areas) {
        return;
    }
    Ui *ui = &app->ui;
    const AllowedAreas *areas = &app->game.areas;
    if (!allowed_areas_valid(areas, app->area_selected)) {
        app->area_selected = 0;
    }
    float row = ui_row_height(ui), gap = ui->theme.gap;

    /* Measure: as wide as the row of actions, or the New area button. */
    float action_width[ACTION_COUNT], width = 0.0f;
    for (int i = 0; i < ACTION_COUNT; i++) {
        action_width[i] = SDL_ceilf(ui_button_width(ui, tr(ACTION_KEYS[i])));
        width += action_width[i] + (i > 0 ? gap : 0.0f);
    }
    width = SDL_max(width, SDL_ceilf(ui_button_width(ui, tr("areas.new"))));
    float block = row * 4.0f + gap * 3.0f;
    bool naming_new = app->area_naming == ALLOWED_AREA_MAX;
    bool naming = app->area_naming != ALLOWED_AREA_NONE;
    float height = (float)areas->count * block + row + (naming ? row + gap : 0.0f);

    ScreenLayout layout;
    layout_screen(ui, &layout);
    UiDock dock = {0.0f, layout.bar_top, colonist_bar_bottom(app) + ui->theme.margin, 0.0f, 0.0f};
    bool open = true;
    SDL_FPoint origin = ui_window_begin_docked(ui, APP_WINDOW_AREAS, tr("areas.title"), width,
                                               height, &dock, &open);
    float x = origin.x, y = origin.y;
    width = SDL_max(width, ui_window_content_width(ui));
    for (int i = 0; i < areas->count; i++) {
        y = draw_area(app, i, x, y, width, action_width);
    }

    /* New area: a name first, then it is painted. */
    SDL_FRect bottom = {x, y, width, row};
    if (naming_new) {
        draw_name_field(app, bottom);
    } else {
        bool full = areas->count >= ALLOWED_AREA_MAX;
        bool clicked = ui_button(ui, bottom, tr("areas.new"));
        if (full) {
            ui_fill(ui, bottom, ui->theme.colors[THEME_DISABLED_VEIL]);
        }
        tooltip_if_hovered(ui, bottom, tr("areas.new"),
                           full ? tr("areas.new.full") : tr("areas.new.description"));
        if (clicked && !full) {
            char name[ALLOWED_AREA_NAME_SIZE];
            new_area_name(app, name, sizeof(name));
            start_naming(app, ALLOWED_AREA_MAX, name);
        }
    }
    if (naming) {
        float text_offset = SDL_floorf((row - (float)text_line_height(app->text)) * 0.5f);
        ui_label_dim(ui, tr("areas.naming_hint"), x, y + row + gap + text_offset);
    }
    ui_window_end(ui);

    /* The area chosen stays shown on the map (unless another one is pointed at). */
    if (app->area_preview.kind == AREA_PREVIEW_NONE) {
        app->area_preview =
            (AreaPreview){.kind = AREA_PREVIEW_ALLOWED, .stockpile = app->area_selected};
    }
    if (!open) {
        areas_window_set_open(app, false);
    }
}
