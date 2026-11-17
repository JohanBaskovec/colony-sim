#include "game/colonist_bar.h"

#include "core/entry_strip.h"
#include "core/i18n.h"
#include "game/layout.h"
#include "game/render.h"

#define TEXT_SIZE 128
#define ENTRY_WIDTH 100.0f  /* At scale 1. */
#define PORTRAIT_SIZE 44.0f /* At scale 1. */
#define MIN_SCALE 0.45f
#define NAME_MIN_SCALE 0.6f    /* Smaller entries show the portrait alone. */
#define ACTION_MIN_SCALE 0.85f /* Smaller entries leave the action to the tooltip. */
#define DOUBLE_CLICK_MS 400

typedef struct BarLayout {
    EntryStrip strip;
    int count;
    float x0, y0; /* Top left corner of the first row. */
    float entry_w, entry_h, gap;
    float portrait;
    bool name, action; /* Shown at this size. */
} BarLayout;

static int colonist_count(const App *app)
{
    int count = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(&app->game); slot++) {
        count += !entity_id_is_none(game_colonist_id_at(&app->game, slot));
    }
    return count;
}

static void bar_layout(const App *app, BarLayout *out)
{
    const Ui *ui = &app->ui;
    const Theme *theme = &ui->theme;
    float line = (float)text_line_height(ui->text);
    /* The corners stay free for the resources (left) and the debug readout (right). */
    ScreenLayout screen;
    layout_screen(ui, &screen);
    float width = ui->screen_width - 2.0f * screen.corner_width;
    float entry = SDL_roundf(ENTRY_WIDTH * theme->scale);
    out->count = colonist_count(app);
    out->gap = theme->gap;
    out->strip = entry_strip_fit(out->count, width, entry, out->gap, MIN_SCALE);
    float scale = out->strip.scale;
    out->entry_w = SDL_floorf(entry * scale);
    out->name = scale >= NAME_MIN_SCALE;
    out->action = scale >= ACTION_MIN_SCALE;
    out->portrait = SDL_floorf(SDL_roundf(PORTRAIT_SIZE * theme->scale) * SDL_max(scale, 0.8f));
    out->portrait = SDL_min(out->portrait, out->entry_w - 2.0f * out->gap);
    out->entry_h =
        out->portrait + 2.0f * out->gap + (out->name ? line : 0.0f) + (out->action ? line : 0.0f);
    int per_row = out->strip.per_row;
    float row_width = (float)per_row * out->entry_w + (float)(per_row - 1) * out->gap;
    out->x0 = SDL_floorf((ui->screen_width - row_width) * 0.5f);
    out->y0 = theme->margin;
}

float colonist_bar_bottom(const App *app)
{
    BarLayout layout;
    bar_layout(app, &layout);
    return layout.y0 + (float)layout.strip.rows * (layout.entry_h + layout.gap);
}

static bool is_selected(const App *app, EntityId id)
{
    for (int i = 0; i < app->selected_count; i++) {
        if (entity_id_equal(app->selected[i], id)) {
            return true;
        }
    }
    return false;
}

static void centered_text(Ui *ui, const char *text, SDL_FRect rect, float y, bool dim)
{
    char fitted[TEXT_SIZE];
    ui_fit_text(ui, text, rect.w - 4.0f, fitted, sizeof(fitted));
    float x = SDL_floorf(rect.x + (rect.w - ui_text_width(ui, fitted)) * 0.5f);
    if (dim) {
        ui_label_dim(ui, fitted, x, y);
    } else {
        ui_label(ui, fitted, x, y);
    }
}

static void click(App *app, EntityId id)
{
    bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    Uint64 now = SDL_GetTicks();
    bool twice =
        entity_id_equal(app->bar_last_click, id) && now - app->bar_last_click_ms <= DOUBLE_CLICK_MS;
    if (twice) {
        app_select_colonist(app, id, false);
        app_focus_colonist(app, id);
        app->bar_last_click = ENTITY_NONE;
        return;
    }
    app_select_colonist(app, id, shift);
    app->bar_last_click = id;
    app->bar_last_click_ms = now;
}

void colonist_bar_draw(App *app)
{
    Ui *ui = &app->ui;
    BarLayout layout;
    bar_layout(app, &layout);
    if (layout.count == 0) {
        return;
    }
    float line = (float)text_line_height(ui->text);
    /* A panel per row (they catch the clicks), then the entries. */
    for (int row = 0; row < layout.strip.rows; row++) {
        int in_row = SDL_min(layout.strip.per_row, layout.count - row * layout.strip.per_row);
        float y = layout.y0 + (float)row * (layout.entry_h + layout.gap);
        ui_panel(ui, (SDL_FRect){layout.x0 - layout.gap, y - layout.gap,
                                 (float)in_row * (layout.entry_w + layout.gap) + layout.gap,
                                 layout.entry_h + 2.0f * layout.gap});
        int first = row * layout.strip.per_row;
        int index = 0;
        for (uint32_t slot = 0; slot < game_colonist_slot_count(&app->game); slot++) {
            EntityId id = game_colonist_id_at(&app->game, slot);
            if (entity_id_is_none(id)) {
                continue;
            }
            int position = index++;
            if (position < first || position >= first + in_row) {
                continue;
            }
            const Colonist *colonist = &app->game.colonists[slot];
            int column = position - first;
            SDL_FRect rect = {layout.x0 + (float)column * (layout.entry_w + layout.gap), y,
                              layout.entry_w, layout.entry_h};
            if (ui_icon_button(ui, rect, is_selected(app, id))) {
                click(app, id);
            }
            SDL_FRect portrait = {SDL_floorf(rect.x + (rect.w - layout.portrait) * 0.5f),
                                  rect.y + layout.gap, layout.portrait, layout.portrait};
            render_colonist_portrait(app->renderer, app->sprites, colonist, portrait);
            float text_y = portrait.y + portrait.h + SDL_floorf(layout.gap * 0.5f);
            const char *activity = tr(colonist_activity_key(colonist));
            if (layout.name) {
                centered_text(ui, colonist->name, rect, text_y, false);
                text_y += line;
            }
            if (layout.action) {
                centered_text(ui, activity, rect, text_y, true);
            }
            if (ui_hovered(ui, rect)) {
                const char *lines[2] = {colonist->name, activity};
                ui_tooltip(ui, lines, 2);
            }
        }
    }
}
