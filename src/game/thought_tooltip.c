#include "game/thought_tooltip.h"

#include "core/i18n.h"

#include <SDL3/SDL.h>

#define LINE_SIZE 256

typedef struct Lines {
    char text[UI_TOOLTIP_LINES][LINE_SIZE];
    int count;
} Lines;

static char *add_line(Lines *lines)
{
    if (lines->count < UI_TOOLTIP_LINES) {
        lines->count++;
    }
    char *line = lines->text[lines->count - 1];
    line[0] = '\0';
    return line;
}

/* A whole number with its sign ("+6", "-8"). */
static void format_signed(char *out, size_t size, long long value)
{
    char number[16];
    tr_integer(number, sizeof(number), value);
    SDL_snprintf(out, size, "%s%s", value > 0 ? "+" : "", number);
}

void thought_tooltip(Ui *ui, const Mood *mood, ThoughtType type)
{
    const ThoughtDef *def = thought_def(type);
    if (def == NULL) {
        return;
    }
    Lines lines = {0};
    char a[16], b[16];
    SDL_strlcpy(add_line(&lines), tr(def->name_key), LINE_SIZE);
    SDL_strlcpy(add_line(&lines), tr(def->description_key), LINE_SIZE);
    bool held = mood_has(mood, type);
    float original = held ? mood_thought_peak(mood, type) : def->value * mood->sensitivity;
    format_signed(a, sizeof(a), SDL_lroundf(original));
    TR_FORMAT(add_line(&lines), LINE_SIZE, tr("hud.thought.original"), a);
    if (def->state) {
        tr_integer(a, sizeof(a), SDL_lroundf(def->half_life_hours));
        TR_FORMAT(add_line(&lines), LINE_SIZE, tr("hud.thought.state"), a);
        if (def->grow_max > 1.0f) {
            format_signed(a, sizeof(a),
                          SDL_lroundf(def->value * mood->sensitivity * def->grow_max));
            tr_integer(b, sizeof(b), SDL_lroundf(def->grow_hours));
            TR_FORMAT(add_line(&lines), LINE_SIZE, tr("hud.thought.grows"), a, b);
        }
    } else {
        tr_integer(a, sizeof(a), SDL_lroundf(def->plateau_hours));
        tr_integer(b, sizeof(b), SDL_lroundf(def->half_life_hours));
        TR_FORMAT(add_line(&lines), LINE_SIZE, tr("hud.thought.memory"), a, b);
    }
    if (def->max_stack > 1) {
        tr_integer(a, sizeof(a), def->max_stack);
        TR_FORMAT(add_line(&lines), LINE_SIZE, tr("hud.thought.stacks"), a);
    }
    if (held && mood_fading(mood, type)) {
        SDL_strlcpy(add_line(&lines), tr("hud.thought.fading"), LINE_SIZE);
    }
    const char *pointers[UI_TOOLTIP_LINES];
    for (int i = 0; i < lines.count; i++) {
        pointers[i] = lines.text[i];
    }
    ui_tooltip(ui, pointers, lines.count);
}
