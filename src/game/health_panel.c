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

/* How an injury is going: serious (it keeps bleeding), clotting, or healing. */
static void injury_status(char *out, size_t size, const Health *health, const Injury *injury)
{
    float bleeding = health_injury_bleeding(health, injury);
    char rate[64];
    bleeding_text(rate, sizeof(rate), bleeding);
    if (health_injury_serious(health, injury) && injury_def(injury->type)->bleeding > 0.0f) {
        TR_FORMAT(out, size, tr("health.injury.serious"), rate);
    } else if (bleeding > 0.0f) {
        TR_FORMAT(out, size, tr("health.injury.clotting"), rate);
    } else {
        SDL_strlcpy(out, tr("health.injury.healing"), size);
    }
}

/* What a part is needed for, in a line (NULL: nothing in particular). */
static const char *role_key(BodyRole role)
{
    switch (role) {
    case BODY_ROLE_MIND: return "health.part.role.mind";
    case BODY_ROLE_WALK: return "health.part.role.walk";
    case BODY_ROLE_HANDS: return "health.part.role.hands";
    case BODY_ROLE_NONE: break;
    }
    return NULL;
}

/* The tooltip of a part: what it is for, then each injury on it. */
static void part_tooltip(UiLayout *layout, const Health *health, int part, float efficiency)
{
    const BodyPartDef *def = body_part_def((BodyPlan)health->plan, part);
    char lines[UI_TOOLTIP_LINES][TEXT_SIZE];
    const char *shown[UI_TOOLTIP_LINES];
    int count = 0;
    char value[32];
    percent_text(value, sizeof(value), efficiency);
    TR_FORMAT(lines[count++], TEXT_SIZE, tr("health.part.efficiency"), tr(def->name_key), value);
    if (def->vital) {
        SDL_strlcpy(lines[count++], tr("health.part.vital"), TEXT_SIZE);
    }
    if (role_key(def->role) != NULL) {
        SDL_strlcpy(lines[count++], tr(role_key(def->role)), TEXT_SIZE);
    }
    if (def->limb != BODY_LIMB_NONE) {
        TR_FORMAT(lines[count++], TEXT_SIZE, tr("health.part.limb"),
                  tr(body_limb_name_key(def->limb)));
    }
    for (int i = 0; i < health->injury_count && count < UI_TOOLTIP_LINES; i++) {
        const Injury *injury = &health->injuries[i];
        if (injury->part != part) {
            continue;
        }
        char severity[32], status[TEXT_SIZE];
        percent_text(severity, sizeof(severity), health_injury_severity(health, injury));
        injury_status(status, sizeof(status), health, injury);
        TR_FORMAT(lines[count++], TEXT_SIZE, tr("health.part.injury"),
                  tr(injury_def((InjuryType)injury->type)->name_key), severity, status);
    }
    for (int i = 0; i < count; i++) {
        shown[i] = lines[i];
    }
    ui_tooltip(layout->ui, shown, count);
}

/* Blood the injuries of a part lose an hour. */
static float part_bleeding(const Health *health, int part)
{
    float sum = 0.0f;
    for (int i = 0; i < health->injury_count; i++) {
        if (health->injuries[i].part == part) {
            sum += health_injury_bleeding(health, &health->injuries[i]);
        }
    }
    return sum;
}

/* The kinds of injury on a part, each once ("Cut, Bruise"); "Intact" if none. */
static bool part_injury_text(char *out, size_t size, const Health *health, int part)
{
    bool seen[INJURY_TYPE_COUNT] = {false};
    out[0] = '\0';
    bool any = false;
    for (int i = 0; i < health->injury_count; i++) {
        const Injury *injury = &health->injuries[i];
        if (injury->part != part || seen[injury->type]) {
            continue;
        }
        seen[injury->type] = true;
        const char *name = tr(injury_def((InjuryType)injury->type)->name_key);
        if (any) {
            SDL_strlcat(out, ", ", size);
        }
        SDL_strlcat(out, name, size);
        any = true;
    }
    if (!any) {
        SDL_strlcpy(out, tr("health.part.intact"), size);
    }
    return any;
}

/* A table of the body: one row per part with its injuries, how much of it
 * works and the blood it loses (details in the tooltip). */
static void parts_table(UiLayout *layout, const Health *health)
{
    Ui *ui = layout->ui;
    const Theme *theme = &ui->theme;
    int count = body_part_count((BodyPlan)health->plan);
    char injuries[HEALTH_MAX_PARTS][TEXT_SIZE], state[HEALTH_MAX_PARTS][32],
        bleed[HEALTH_MAX_PARTS][48];
    float efficiency[HEALTH_MAX_PARTS];
    bool hurt[HEALTH_MAX_PARTS];
    const char *heads[4] = {tr("health.part"), tr("health.part.injuries"), tr("health.part.state"),
                            tr("health.part.bleeding")};
    float width[4];
    for (int column = 0; column < 4; column++) {
        width[column] = ui_text_width(ui, heads[column]);
    }
    for (int part = 0; part < count && part < HEALTH_MAX_PARTS; part++) {
        hurt[part] = part_injury_text(injuries[part], sizeof(injuries[part]), health, part);
        efficiency[part] = health_part_efficiency(health, part);
        percent_text(state[part], sizeof(state[part]), efficiency[part]);
        float lost = part_bleeding(health, part);
        bleed[part][0] = '\0';
        if (lost > 0.0f) {
            char number[16];
            tr_decimal(number, sizeof(number), lost * 100.0f, 1);
            TR_FORMAT(bleed[part], sizeof(bleed[part]), tr("health.part.bleeding.value"), number);
        }
        const char *name = tr(body_part_def((BodyPlan)health->plan, part)->name_key);
        width[0] = SDL_max(width[0], ui_text_width(ui, name));
        width[1] = SDL_max(width[1], ui_text_width(ui, injuries[part]));
        width[2] = SDL_max(width[2], ui_text_width(ui, state[part]));
        width[3] = SDL_max(width[3], ui_text_width(ui, bleed[part]));
    }
    float total = width[0] + width[1] + width[2] + width[3] + 3.0f * UI_LAYOUT_GAP;
    ui_layout_measure(layout, total);

    float right = layout->x + layout->width; /* The last two columns are right-aligned. */
    float x_bleed = right - width[3];
    float x_injuries = layout->x + width[0] + UI_LAYOUT_GAP;
    if (layout->draw) {
        float y = layout->y + layout->text_offset;
        SDL_Color dim = theme->colors[THEME_TEXT_DIM];
        ui_label_rgba(ui, heads[0], layout->x, y, dim);
        ui_label_rgba(ui, heads[1], x_injuries, y, dim);
        ui_label_rgba(ui, heads[2], x_bleed - UI_LAYOUT_GAP - ui_text_width(ui, heads[2]), y, dim);
        ui_label_rgba(ui, heads[3], right - ui_text_width(ui, heads[3]), y, dim);
    }
    layout->y += layout->row;
    for (int part = 0; part < count && part < HEALTH_MAX_PARTS; part++) {
        SDL_FRect row = {layout->x, layout->y, layout->width, layout->row};
        if (layout->draw) {
            if (ui_hovered(ui, row)) {
                ui_fill(ui, row, theme->colors[THEME_SCROLL_TRACK]);
            }
            float y = layout->y + layout->text_offset;
            SDL_Color color = hurt[part] || efficiency[part] < 1.0f
                                  ? bad_color(layout, 1.0f - efficiency[part])
                                  : theme->colors[THEME_TEXT];
            ui_label(ui, tr(body_part_def((BodyPlan)health->plan, part)->name_key), layout->x, y);
            ui_label_rgba(ui, injuries[part], x_injuries, y,
                          hurt[part] ? color : theme->colors[THEME_TEXT_DIM]);
            ui_label_rgba(ui, state[part], x_bleed - UI_LAYOUT_GAP - ui_text_width(ui, state[part]),
                          y, color);
            ui_label_rgba(ui, bleed[part], right - ui_text_width(ui, bleed[part]), y,
                          bad_color(layout, 1.0f));
            if (ui_hovered(ui, row)) {
                part_tooltip(layout, health, part, efficiency[part]);
            }
        }
        layout->y += layout->row;
    }
}

void health_panel_rows(UiLayout *layout, const Health *health)
{
    bool hurt = health_is_hurt(health);
    SDL_Color text = layout->ui->theme.colors[THEME_TEXT];
    ui_layout_pair(layout, tr("health.status"), tr(health_status_key(health)),
                   hurt ? bad_color(layout, 0.5f) : text, false);
    if (!hurt || health_is_dead(health)) {
        layout->y += layout->row * 0.5f;
        parts_table(layout, health);
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

    for (int kind = 0; kind < DEPRIVATION_COUNT; kind++) {
        float severity = health_deprivation(health, (Deprivation)kind);
        if (severity <= 0.0f) {
            continue;
        }
        percent_text(value, sizeof(value), severity);
        row = ui_layout_pair(layout, tr(deprivation_name_key((Deprivation)kind)), value,
                             bad_color(layout, severity), true);
        if (layout->draw && ui_hovered(layout->ui, row)) {
            tooltip_lines(layout, tr(deprivation_description_key((Deprivation)kind)), NULL);
        }
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

    layout->y += layout->row * 0.5f;
    parts_table(layout, health);
}
