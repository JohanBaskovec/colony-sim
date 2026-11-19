#include "game/attributes_panel.h"

#include "core/i18n.h"

/* A whole number with its sign ("+6", "-8"). */
static void signed_text(char *out, size_t size, long long value)
{
    char number[16];
    tr_integer(number, sizeof(number), value);
    SDL_snprintf(out, size, "%s%s", value > 0 ? "+" : "", number);
}

int attributes_panel_rows(UiLayout *layout, const Attributes *attributes, bool animal,
                          bool editable, AttributeType *edited)
{
    int step = 0;
    ui_layout_set_stepper(layout, editable);
    static const char *const GROUP_KEYS[ATTRIBUTE_GROUP_COUNT] = {
        [ATTRIBUTE_GROUP_BODY] = N_("attribute.group.body"),
        [ATTRIBUTE_GROUP_MIND] = N_("attribute.group.mind"),
        [ATTRIBUTE_GROUP_SOCIAL] = N_("attribute.group.social"),
    };
    for (int group = 0; group < ATTRIBUTE_GROUP_COUNT; group++) {
        ui_layout_label(layout, tr(GROUP_KEYS[group]), 0.0f, true);
        for (int type = 0; type < ATTRIBUTE_COUNT; type++) {
            if (attribute_group((AttributeType)type) != (AttributeGroup)group ||
                (animal && !attribute_affects_animals((AttributeType)type))) {
                continue;
            }
            int position = attributes->position[type];
            /* Red at the lowest position, white at the ordinary, green at the highest. */
            SDL_Color color =
                theme_scale_color(&layout->ui->theme,
                                  0.5f + (float)position / (2.0f * (float)ATTRIBUTE_POSITION_MAX));
            /* The exact position, as a percentage of the way to either end. */
            char number[16], percent[32];
            signed_text(number, sizeof(number), SDL_lround((double)position / 10.0));
            TR_FORMAT(percent, sizeof(percent), tr("unit.percent"), number);
            SDL_FRect row = ui_layout_pair(layout, tr(attribute_name_key((AttributeType)type)),
                                           percent, color, true);
            if (layout->draw && ui_hovered(layout->ui, row)) {
                const char *lines[1] = {tr(attribute_description_key((AttributeType)type))};
                ui_tooltip(layout->ui, lines, 1);
            }
            int clicked = editable ? ui_layout_stepper(layout, row) : 0;
            if (clicked != 0) {
                step = clicked;
                *edited = (AttributeType)type;
            }
        }
    }
    return step;
}
