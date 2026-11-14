#include "game/work_panel.h"

#include "core/i18n.h"

#define PANEL_PADDING 10.0f
#define PANEL_TOP 72.0f    /* Below the status and tool panels. */
#define PANEL_BOTTOM 24.0f /* Space kept free under the panel. */
#define COLUMN_GAP 6.0f
#define MIN_CELL_WIDTH 44.0f
#define SCROLLBAR_WIDTH 6.0f
#define TEXT_SIZE 256
#define LEVEL_LABEL_INSET 4.0f

/* From WORK_PRIORITY_DISABLED (dark) to the highest priority (bright green). */
static const SDL_Color PRIORITY_COLORS[WORK_PRIORITY_LOWEST + 1] = {
    {34, 38, 46, 255}, {52, 150, 74, 255}, {108, 140, 58, 255},
    {142, 118, 54, 255}, {112, 84, 70, 255},
};
static const SDL_Color ROW_COLOR = {255, 255, 255, 14};
static const SDL_Color ROW_HOVER_COLOR = {255, 255, 255, 30};
static const SDL_Color ROW_SELECTED_COLOR = {120, 170, 255, 60};
static const SDL_Color SCROLL_TRACK_COLOR = {255, 255, 255, 24};
static const SDL_Color SCROLL_THUMB_COLOR = {200, 204, 214, 200};

static bool is_selected(const App *app, EntityId id)
{
    for (int i = 0; i < app->selected_count; i++) {
        if (entity_id_equal(app->selected[i], id)) {
            return true;
        }
    }
    return false;
}

/* Selects one colonist and centers the view on them. */
static void focus_colonist(App *app, EntityId id)
{
    const Colonist *colonist = game_colonist_const(&app->game, id);
    if (colonist == NULL) {
        return;
    }
    app->selected[0] = id;
    app->selected_count = 1;
    app->selected_stockpile = STOCKPILE_NONE;
    colonist_position(colonist, &app->camera.x, &app->camera.y);
}

void work_panel_scroll(App *app, float steps)
{
    app->work_panel_scroll -= (int)SDL_roundf(steps);
    /* Clamped to the row count when drawing. */
}

void work_panel_draw(App *app)
{
    if (!app->show_work_panel) {
        return;
    }
    Ui *ui = &app->ui;
    Game *game = &app->game;
    const char *title = tr("work_panel.title");
    const char *close = tr("ui.close");
    const char *name_header = tr("work_panel.colonist");
    const char *hint = tr("work_panel.hint");
    char legend[TEXT_SIZE], highest[8], lowest[8];
    tr_integer(highest, sizeof(highest), WORK_PRIORITY_HIGHEST);
    tr_integer(lowest, sizeof(lowest), WORK_PRIORITY_LOWEST);
    TR_FORMAT(legend, sizeof(legend), tr("work_panel.legend"), highest, lowest);
    char level_legend[TEXT_SIZE], mastery[8], max_level[8];
    tr_integer(mastery, sizeof(mastery), (long long)KNOWLEDGE_MASTERY_LEVEL);
    tr_integer(max_level, sizeof(max_level), (long long)KNOWLEDGE_LEVEL_MAX);
    TR_FORMAT(level_legend, sizeof(level_legend), tr("work_panel.knowledge_legend"), mastery,
              max_level);

    /* Rows: living colonists, in slot order. */
    EntityId ids[COLONIST_MAX];
    int count = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (!entity_id_is_none(id)) {
            ids[count++] = id;
        }
    }

    /* Measure: columns are as wide as their header, the panel as its widest line. */
    float row = ui_row_height(ui);
    float name_width = ui_text_width(ui, name_header);
    for (int i = 0; i < count; i++) {
        name_width = SDL_max(name_width, ui_text_width(ui, game->colonists[ids[i].index].name));
    }
    name_width = SDL_ceilf(name_width) + COLUMN_GAP;
    float column_width[WORK_TYPE_COUNT];
    float grid_width = name_width;
    for (int type = 0; type < WORK_TYPE_COUNT; type++) {
        float header = ui_text_width(ui, tr(work_type_def((WorkType)type)->name_key));
        column_width[type] = SDL_ceilf(SDL_max(header, MIN_CELL_WIDTH)) + COLUMN_GAP;
        grid_width += column_width[type];
    }
    grid_width += SCROLLBAR_WIDTH;
    float width = SDL_max(grid_width, ui_text_width(ui, title) + COLUMN_GAP * 2.0f +
                                          ui_button_width(ui, close));
    width = SDL_max(width, SDL_max(ui_text_width(ui, hint), ui_text_width(ui, legend)));
    width = SDL_ceilf(SDL_max(width, ui_text_width(ui, level_legend)));
    /* The work columns share whatever width the text lines add. */
    float extra = SDL_floorf((width - grid_width) / (float)WORK_TYPE_COUNT);
    for (int type = 0; type < WORK_TYPE_COUNT; type++) {
        column_width[type] += extra;
    }
    grid_width += extra * (float)WORK_TYPE_COUNT;

    /* Title, header, rows, hint, two legends. As many rows as fit on screen. */
    float fixed_height = 5.0f * row + 2.0f * PANEL_PADDING;
    float available = app->camera.viewport_height - PANEL_TOP - PANEL_BOTTOM - fixed_height;
    int rows_fit = SDL_max(1, (int)(available / row));
    int shown = SDL_max(1, SDL_min(count, rows_fit)); /* One line for "no colonists". */
    int max_scroll = SDL_max(0, count - shown);
    app->work_panel_scroll = SDL_clamp(app->work_panel_scroll, 0, max_scroll);
    int first = app->work_panel_scroll;

    SDL_FRect panel = {SDL_floorf((app->camera.viewport_width - width) * 0.5f) - PANEL_PADDING,
                       PANEL_TOP, width + 2.0f * PANEL_PADDING,
                       fixed_height + (float)shown * row};
    ui_panel(ui, panel);
    float x = panel.x + PANEL_PADDING, y = panel.y + PANEL_PADDING;
    float text_offset = SDL_floorf((row - (float)text_line_height(app->text)) * 0.5f);

    /* Title and close button. */
    ui_label(ui, title, x, y + text_offset);
    float close_width = ui_button_width(ui, close);
    if (ui_button(ui, (SDL_FRect){x + width - close_width, y, close_width, row - 2.0f}, close)) {
        app->show_work_panel = false;
    }
    y += row;

    /* Column headers, centered over their column. */
    ui_label(ui, name_header, x, y + text_offset);
    float column_x = x + name_width;
    for (int type = 0; type < WORK_TYPE_COUNT; type++) {
        const char *header = tr(work_type_def((WorkType)type)->name_key);
        float header_width = ui_text_width(ui, header);
        ui_label(ui, header,
                 SDL_floorf(column_x + (column_width[type] - COLUMN_GAP - header_width) * 0.5f),
                 y + text_offset);
        column_x += column_width[type];
    }
    y += row;

    /* Rows. */
    float rows_top = y;
    if (count == 0) {
        ui_label(ui, tr("work_panel.empty"), x, y + text_offset);
    }
    for (int i = first; i < first + shown && i < count; i++) {
        EntityId id = ids[i];
        const Colonist *colonist = &game->colonists[id.index];
        SDL_FRect band = {x - 4.0f, y, grid_width - SCROLLBAR_WIDTH + 4.0f, row};
        bool hovered = ui_hovered(ui, band);
        ui_fill(ui, band, is_selected(app, id) ? ROW_SELECTED_COLOR
                                               : (hovered ? ROW_HOVER_COLOR : ROW_COLOR));
        ui_label(ui, colonist->name, x, y + text_offset);
        if (ui_take_click(ui, (SDL_FRect){band.x, y, name_width + 4.0f, row}) ==
            SDL_BUTTON_LEFT) {
            focus_colonist(app, id);
        }

        column_x = x + name_width;
        for (int type = 0; type < WORK_TYPE_COUNT; type++) {
            int priority = colonist->work_priorities[type];
            char label[8] = "";
            if (priority != WORK_PRIORITY_DISABLED) {
                tr_integer(label, sizeof(label), priority);
            }
            SDL_FRect cell = {column_x, y + 1.0f, column_width[type] - COLUMN_GAP, row - 2.0f};
            SDL_Color color = PRIORITY_COLORS[work_priority_is_valid(priority) ? priority : 0];
            int button = ui_cell(ui, cell, label, color);
            /* Knowledge level in the corner (who is best at what), greyed
             * when it is too low to do the work at all. */
            KnowledgeType knowledge = work_type_def((WorkType)type)->knowledge;
            if (knowledge != KNOWLEDGE_NONE) {
                double value = colonist_knowledge_level(colonist, knowledge);
                char level[8];
                tr_integer(level, sizeof(level), (long long)value);
                float label_x = cell.x + cell.w - LEVEL_LABEL_INSET - ui_text_width(ui, level);
                if (knowledge_is_mastered(value)) {
                    ui_label(ui, level, label_x, y + text_offset);
                } else {
                    ui_label_dim(ui, level, label_x, y + text_offset);
                }
            }
            if (button == SDL_BUTTON_LEFT) {
                game_set_work_priority(game, id, (WorkType)type, work_priority_raised(priority));
            } else if (button == SDL_BUTTON_RIGHT) {
                game_set_work_priority(game, id, (WorkType)type,
                                       work_priority_lowered(priority));
            }
            column_x += column_width[type];
        }
        y += row;
    }
    y = rows_top + (float)shown * row;

    /* Scroll bar when some rows are out of view. */
    if (max_scroll > 0) {
        SDL_FRect track = {x + grid_width - SCROLLBAR_WIDTH, rows_top, SCROLLBAR_WIDTH,
                           (float)shown * row};
        ui_fill(ui, track, SCROLL_TRACK_COLOR);
        SDL_FRect thumb = track;
        thumb.h = SDL_max(8.0f, SDL_floorf(track.h * (float)shown / (float)count));
        thumb.y += SDL_floorf((track.h - thumb.h) * (float)first / (float)max_scroll);
        ui_fill(ui, thumb, SCROLL_THUMB_COLOR);
    }

    ui_label(ui, hint, x, y + text_offset);
    y += row;
    ui_label(ui, legend, x, y + text_offset);
    y += row;
    ui_label(ui, level_legend, x, y + text_offset);
}
