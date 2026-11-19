#include "game/animal_panel.h"

#include "core/i18n.h"
#include "game/attributes_panel.h"
#include "game/health_panel.h"
#include "game/inspector.h"
#include "game/journal_panel.h"
#include "game/ui_layout.h"

#define TEXT_SIZE 192

/* A need as a table row: its name and a bar; the tooltip has its exact
 * value and what it does. */
static void need_row(UiLayout *layout, App *app, const Animal *animal, AnimalNeed need)
{
    static const char *const DESCRIPTION_KEYS[ANIMAL_NEED_COUNT] = {
        [ANIMAL_NEED_FOOD] = N_("animal_panel.need.food"),
        [ANIMAL_NEED_WATER] = N_("animal_panel.need.water"),
        [ANIMAL_NEED_REST] = N_("animal_panel.need.rest"),
    };
    const char *name = tr(animal_need_name_key(need));
    SDL_FRect row = ui_layout_bar_row(layout, name, animal->needs[need], true);
    if (layout->draw && ui_hovered(layout->ui, row)) {
        char percent[16], value[TEXT_SIZE];
        tr_integer(percent, sizeof(percent), (long long)(animal->needs[need] * 100.0f + 0.5f));
        TR_FORMAT(value, sizeof(value), tr("hud.colonist.need"), name, percent);
        const char *lines[2] = {value, tr(DESCRIPTION_KEYS[need])};
        ui_tooltip(layout->ui, lines, 2);
    }
    int step = app->character_editor ? ui_layout_stepper(layout, row) : 0;
    if (step != 0) {
        game_debug_animal_need(&app->game, app->selected_animal, need, step);
    }
}

static void animal_rows(UiLayout *layout, void *userdata)
{
    App *app = userdata;
    const Animal *animal = game_animal_const(&app->game, app->selected_animal);
    if (animal == NULL) {
        return;
    }
    const SDL_Color text = layout->ui->theme.colors[THEME_TEXT];
    ui_layout_pair(layout, tr("animal_panel.species"),
                   tr(animal_def((AnimalSpecies)animal->species)->name_key), text, false);
    ui_layout_pair(layout, tr("animal_panel.sex"), tr(animal_sex_key((AnimalSex)animal->sex)), text,
                   false);
    ui_layout_pair(layout, tr("hud.summary.activity"), tr(animal_activity_key(animal)), text,
                   false);
    ui_layout_set_stepper(layout, app->character_editor);
    for (int need = 0; need < ANIMAL_NEED_COUNT; need++) {
        need_row(layout, app, animal, (AnimalNeed)need);
    }
    ui_layout_set_stepper(layout, false);
    int herd = animals_herd_size(&app->game.animals, app->selected_animal);
    char size[TEXT_SIZE];
    if (herd > 1) {
        char count[16];
        tr_integer(count, sizeof(count), herd);
        TR_FORMAT(size, sizeof(size), tr_plural("animal_panel.herd.count", herd), count);
    } else {
        SDL_strlcpy(size, tr("animal_panel.herd.alone"), sizeof(size));
    }
    ui_layout_pair(layout, tr("animal_panel.herd"), size, text, false);
}

/* The Health tab (game/health_panel.h). */
static void health_rows(UiLayout *layout, void *userdata)
{
    App *app = userdata;
    const Animal *animal = game_animal_const(&app->game, app->selected_animal);
    if (animal != NULL) {
        health_panel_rows(layout, &animal->health);
        TemperatureRanges ranges = animal_temperature_ranges(animal);
        float felt = temperature_at(&app->game.temperatures, animal->x, animal->y, animal->z);
        health_panel_temperature_rows(layout, felt, &ranges, false);
    }
}

/* The Character tab (game/attributes_panel.h). */
static void journal_rows(UiLayout *layout, void *userdata)
{
    App *app = userdata;
    const Animal *animal = game_animal_const(&app->game, app->selected_animal);
    if (animal != NULL) {
        journal_panel_rows(layout, &animal->journal,
                           (JournalOwner){NULL, animal->species, animal->sex},
                           &app->journal_hidden);
    }
}

static void character_rows(UiLayout *layout, void *userdata)
{
    App *app = userdata;
    const Animal *animal = game_animal_const(&app->game, app->selected_animal);
    if (animal != NULL) {
        attributes_panel_rows(layout, &animal->attributes, true, false, NULL);
    }
}

void animal_panel_draw(App *app)
{
    const Animal *animal = game_animal_const(&app->game, app->selected_animal);
    if (animal == NULL) {
        return;
    }
    const char *title = tr(animal_name_key((AnimalSpecies)animal->species, (AnimalSex)animal->sex));
    bool open = inspector_box(app, INSPECT_ANIMAL, title, animal_rows, app);
    inspector_follow_action(app, 0, FOLLOW_ANIMAL, app->selected_animal);
    const char *labels[3] = {tr("inspector.tab.health"), tr("inspector.tab.character"),
                             tr("inspector.tab.journal")};
    int tab = inspector_tabs(app, labels, 3);
    if (tab == 0) {
        inspector_tab_panel(app, health_rows, app);
        health_panel_body_window(app, &animal->health);
    } else if (tab == 1) {
        inspector_tab_panel(app, character_rows, app);
    } else if (tab == 2) {
        inspector_tab_panel(app, journal_rows, app);
    }
    if (!open) {
        app->selected_animal = ENTITY_NONE;
    }
}
