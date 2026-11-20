#include "game/visual_panel.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

/* Slider ids (see Ui.active_slider): unique among the windows open together. */
#define FOG_SLIDER_ID 7001
#define BLUR_SLIDER_ID 7002
#define DARK_SLIDER_ID 7003
#define SLIDER_MIN_WIDTH 260.0f
#define TEXT_SIZE 128
/* A step of a slider: 5 percent. */
#define STEP_PERCENT 5
#define STEP_COUNT (100 / STEP_PERCENT + 1)

typedef struct VisualRows {
    RenderTuning *tuning;
    bool reset;
} VisualRows;

/* The label of a slider with `percent` in it. */
static void percent_text(char *out, size_t size, const char *key, int percent)
{
    char number[16], value[32];
    tr_integer(number, sizeof(number), percent);
    TR_FORMAT(value, sizeof(value), tr("unit.percent"), number);
    TR_FORMAT(out, size, tr(key), value);
}

/* A label with the value in percent, and a slider under it. */
static void percent_slider(UiLayout *layout, const char *key, int id, int *percent)
{
    char text[TEXT_SIZE], widest[TEXT_SIZE];
    percent_text(text, sizeof(text), key, *percent);
    /* The window keeps its width as the value changes (the slider under the mouse stays). */
    percent_text(widest, sizeof(widest), key, 100);
    ui_layout_measure(layout, ui_text_width(layout->ui, widest));
    ui_layout_label(layout, text, 0.0f, false);
    ui_layout_measure(layout, SLIDER_MIN_WIDTH);
    int step = *percent / STEP_PERCENT;
    if (layout->draw &&
        ui_slider(layout->ui, id, (SDL_FRect){layout->x, layout->y, layout->width, layout->row},
                  STEP_COUNT, &step)) {
        *percent = step * STEP_PERCENT;
    }
    layout->y += layout->row;
}

static void visual_rows(UiLayout *layout, void *userdata)
{
    VisualRows *rows = userdata;
    percent_slider(layout, "debug.visual.fog_dark", FOG_SLIDER_ID, &rows->tuning->fog_dark);
    percent_slider(layout, "debug.visual.below_blur", BLUR_SLIDER_ID, &rows->tuning->below_blur);
    percent_slider(layout, "debug.visual.below_dark", DARK_SLIDER_ID, &rows->tuning->below_dark);
    rows->reset |= ui_layout_wide_button(layout, tr("debug.visual.reset"));
}

void visual_panel_draw(App *app)
{
    if (!app->show_visual_menu) {
        return;
    }
    VisualRows rows = {.tuning = &app->tuning};
    bool open = true;
    ui_layout_window(&app->ui, APP_WINDOW_VISUAL, tr("debug.visual.title"), UI_PLACE_TOP_CENTER,
                     visual_rows, &rows, &open);
    if (rows.reset) {
        app->tuning = render_tuning_default();
    }
    if (!open) {
        app->show_visual_menu = false;
    }
}
