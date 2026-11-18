#include "game/scenarios_panel.h"

#include "core/i18n.h"
#include "game/ui_layout.h"

static void scenario_rows(UiLayout *layout, void *userdata)
{
    int *picked = userdata;
    for (int scenario = 0; scenario < TEST_SCENARIO_COUNT; scenario++) {
        const char *name = tr(test_scenario_name_key((TestScenario)scenario));
        if (ui_layout_wide_button(layout, name)) {
            *picked = scenario;
        }
    }
}

void scenarios_panel_draw(App *app)
{
    if (!app->show_scenarios_menu) {
        return;
    }
    int picked = -1;
    bool open = true;
    ui_layout_window(&app->ui, APP_WINDOW_SCENARIOS, tr("debug.scenarios.title"),
                     UI_PLACE_TOP_CENTER, scenario_rows, &picked, &open);
    if (picked >= 0) {
        app_request_test_scenario(app, (TestScenario)picked);
    }
    if (!open) {
        app->show_scenarios_menu = false;
    }
}
