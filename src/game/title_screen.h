/*
 * The title screen (docs/UI.md), shown when the game starts without a game
 * option on the command line (cli_starts_game()) and when a game is left
 * from the game menu: a world of its own drifts slowly under a veil, with the
 * name of the game in large spaced letters, a tagline, and New game,
 * Settings and Quit.
 *
 * New game opens a window with the options of the world to make
 * (core/new_game.h) in three columns (World, Relief, Life): sliders, the flat
 * world, and the seed typed on the keyboard (any text; empty: a random one).
 * Start (or Enter) shows that the world is being made for a frame, then
 * app.c makes it and the game begins.
 */
#ifndef COLONY_GAME_TITLE_SCREEN_H
#define COLONY_GAME_TITLE_SCREEN_H

#include "game/app.h"

/* The params of the world drifting behind the title screen (no settlers). */
void title_screen_backdrop_params(NewGameParams *params);

/* Shows the title screen over the backdrop world just made (App.game): the
 * view, the state of the window. The options set in the New game window are
 * kept from one visit to the next. */
void title_screen_begin(App *app);

/* Leaves it for the game: the text input stops. */
void title_screen_end(App *app);

void title_screen_handle_event(App *app, const SDL_Event *event);

/* The world behind moves on, and the view drifts over it. */
void title_screen_update(App *app, double dt);

/* Draws it (between ui_begin_frame() and ui_end_frame(), over the world). */
void title_screen_draw(App *app);

#endif /* COLONY_GAME_TITLE_SCREEN_H */
