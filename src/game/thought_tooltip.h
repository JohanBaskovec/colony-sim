/*
 * The tooltip of a thought (core/mood.h): what it is about, what it weighed
 * at first, how it fades (or grows worse while it lasts), how it adds up, and
 * whether it is fading now. Shared by the Mood tab and the journal entries of
 * thoughts.
 */
#ifndef COLONY_GAME_THOUGHT_TOOLTIP_H
#define COLONY_GAME_THOUGHT_TOOLTIP_H

#include "core/mood.h"
#include "game/ui.h"

/* Shows it for `type`, as it stands in `mood` (a thought the mood no longer
 * holds shows its full value, with the settler's sensitivity). */
void thought_tooltip(Ui *ui, const Mood *mood, ThoughtType type);

#endif /* COLONY_GAME_THOUGHT_TOOLTIP_H */
