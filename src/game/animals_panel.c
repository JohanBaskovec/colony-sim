#include "game/animals_panel.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

/* What the player clicked in the drawing pass, applied after it. */
typedef struct AnimalRows {
    int species; /* -1: none. */
    AnimalSex sex;
} AnimalRows;

static void animal_rows(UiLayout *layout, void *userdata)
{
    AnimalRows *rows = userdata;
    const char *labels[ANIMAL_SEX_COUNT] = {tr("animal.sex.female"), tr("animal.sex.male")};
    float widths[ANIMAL_SEX_COUNT];
    float buttons_width = 0.0f;
    for (int sex = 0; sex < ANIMAL_SEX_COUNT; sex++) {
        widths[sex] = ui_button_width(layout->ui, labels[sex]);
        buttons_width += widths[sex] + UI_LAYOUT_GAP * 0.5f;
    }
    for (int species = 0; species < ANIMAL_SPECIES_COUNT; species++) {
        const char *name = tr(animal_def((AnimalSpecies)species)->name_key);
        ui_layout_measure(layout,
                          ui_text_width(layout->ui, name) + UI_LAYOUT_GAP * 2.0f + buttons_width);
        /* Right to left: the last sex is the rightmost button. */
        float x = layout->x + layout->width;
        for (int sex = ANIMAL_SEX_COUNT - 1; sex >= 0; sex--) {
            x -= widths[sex];
            if (ui_layout_button_sized(layout, x, widths[sex], labels[sex])) {
                rows->species = species;
                rows->sex = (AnimalSex)sex;
            }
            x -= UI_LAYOUT_GAP * 0.5f;
        }
        if (layout->draw) {
            ui_label(layout->ui, name, layout->x, layout->y + layout->text_offset);
        }
        layout->y += layout->row;
    }
}

void animals_panel_draw(App *app)
{
    if (!app->show_animals_menu) {
        return;
    }
    AnimalRows rows = {.species = -1};
    bool open = true;
    ui_layout_window(&app->ui, APP_WINDOW_ANIMALS, tr("debug.animals.title"), UI_PLACE_TOP_CENTER,
                     animal_rows, &rows, &open);
    if (rows.species >= 0) {
        app->debug_animal = (AnimalSpecies)rows.species;
        app->debug_animal_sex = rows.sex;
        app->debug_mode = APP_DEBUG_SPAWN_ANIMAL;
        app->tool = APP_TOOL_DEBUG;
        app->left_button_down = false;
        app->box_selecting = false;
    }
    if (!open) {
        app->show_animals_menu = false;
    }
}
