/*
 * The colony log (docs/UI.md): a main window docked at the bottom left above
 * the bottom bar, opened by its button or key. It lists the notices the game
 * kept (core/notice.h), the newest first, each with its date; the ones on
 * screen under the colonist bar are the latest of them.
 */
#ifndef COLONY_GAME_LOG_WINDOW_H
#define COLONY_GAME_LOG_WINDOW_H

#include "game/app.h"

/* Draws the window if it is open (App.show_log). */
void log_window_draw(App *app);

/* The text of a notice, translated. */
void log_notice_text(const Notice *notice, char *out, size_t size);

#endif /* COLONY_GAME_LOG_WINDOW_H */
