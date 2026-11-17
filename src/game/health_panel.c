#include "game/health_panel.h"

#include "core/i18n.h"

#define TEXT_SIZE 192

/* Some 0..1 as a whole percentage ("42 %"). */
static void percent_text(char *out, size_t size, float fraction)
{
    char number[16];
    tr_integer(number, sizeof(number), (long long)SDL_lroundf(fraction * 100.0f));
    TR_FORMAT(out, size, tr("unit.percent"), number);
}

/* Blood lost an hour as a percentage with a decimal ("4.2 % of the blood an hour"). */
static void bleeding_text(char *out, size_t size, float per_hour)
{
    char number[16];
    tr_decimal(number, sizeof(number), per_hour * 100.0f, 1);
    TR_FORMAT(out, size, tr("health.bleeding.value"), number);
}

/* Bad news in red, nothing wrong in white: `badness` 0..1. */
static SDL_Color bad_color(const UiLayout *layout, float badness)
{
    return theme_scale_color(&layout->ui->theme, 0.5f - 0.5f * badness);
}

static void tooltip_lines(UiLayout *layout, const char *first, const char *second)
{
    const char *lines[2] = {first, second};
    ui_tooltip(layout->ui, lines, second != NULL ? 2 : 1);
}

/* An injury: the part, the kind, how bad; bleeding, healing in the tooltip. */
static void injury_row(UiLayout *layout, const Health *health, const Injury *injury)
{
    float severity = health_injury_severity(health, injury);
    char percent[32];
    percent_text(percent, sizeof(percent), severity);
    SDL_FRect row =
        ui_layout_triple(layout, tr(body_part_def((BodyPlan)health->plan, injury->part)->name_key),
                         tr(injury_def((InjuryType)injury->type)->name_key), percent, "100 %",
                         bad_color(layout, severity), true);
    if (!layout->draw || !ui_hovered(layout->ui, row)) {
        return;
    }
    char first[TEXT_SIZE], second[TEXT_SIZE];
    TR_FORMAT(first, sizeof(first), tr("health.injury.severity"), percent);
    float bleeding = health_injury_bleeding(health, injury);
    if (health_injury_serious(health, injury) && injury_def(injury->type)->bleeding > 0.0f) {
        char rate[64];
        bleeding_text(rate, sizeof(rate), bleeding);
        TR_FORMAT(second, sizeof(second), tr("health.injury.serious"), rate);
    } else if (bleeding > 0.0f) {
        char rate[64];
        bleeding_text(rate, sizeof(rate), bleeding);
        TR_FORMAT(second, sizeof(second), tr("health.injury.clotting"), rate);
    } else {
        SDL_strlcpy(second, tr("health.injury.healing"), sizeof(second));
    }
    tooltip_lines(layout, first, second);
}

void health_panel_rows(UiLayout *layout, const Health *health)
{
    bool hurt = health_is_hurt(health);
    SDL_Color text = layout->ui->theme.colors[THEME_TEXT];
    ui_layout_pair(layout, tr("health.status"), tr(health_status_key(health)),
                   hurt ? bad_color(layout, 0.5f) : text, false);
    if (!hurt || health_is_dead(health)) {
        return;
    }
    char value[TEXT_SIZE];

    /* Blood: the bar empties down to where it kills. */
    float blood = (health->blood - HEALTH_BLOOD_DEATH) / (1.0f - HEALTH_BLOOD_DEATH);
    SDL_FRect row = ui_layout_bar_row(layout, tr("health.blood"), blood, true);
    if (layout->draw && ui_hovered(layout->ui, row)) {
        char percent[32];
        percent_text(percent, sizeof(percent), health->blood);
        TR_FORMAT(value, sizeof(value), tr("health.blood.value"), percent);
        tooltip_lines(layout, value, tr("health.blood.description"));
    }
    float bleeding = health_bleeding(health);
    if (bleeding > 0.0f) {
        bleeding_text(value, sizeof(value), bleeding);
        ui_layout_pair(layout, tr("health.bleeding"), value, bad_color(layout, 1.0f), false);
    }
    float pain = health_pain(health);
    percent_text(value, sizeof(value), pain);
    row = ui_layout_pair(layout, tr("health.pain"), value, bad_color(layout, pain), true);
    if (layout->draw && ui_hovered(layout->ui, row)) {
        tooltip_lines(layout, tr("health.pain.description"), NULL);
    }

    ui_layout_label(layout, tr("health.abilities"), 0.0f, true);
    for (int ability = 0; ability < HEALTH_ABILITY_COUNT; ability++) {
        if (ability == HEALTH_HANDS && (BodyPlan)health->plan != BODY_HUMAN) {
            continue; /* Animals have none. */
        }
        float level = health_ability(health, (HealthAbility)ability);
        percent_text(value, sizeof(value), level);
        ui_layout_triple(layout, tr(health_ability_name_key((HealthAbility)ability)), "", value,
                         "100 %", bad_color(layout, 1.0f - level), false);
    }

    ui_layout_label(layout, tr("health.injuries"), 0.0f, true);
    for (int i = 0; i < health->injury_count; i++) {
        injury_row(layout, health, &health->injuries[i]);
    }
}
