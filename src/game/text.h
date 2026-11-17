/*
 * UTF-8 text rendering with SDL3_ttf, with a small cache of laid-out strings
 * so that static HUD text is not re-shaped every frame.
 */
#ifndef COLONY_GAME_TEXT_H
#define COLONY_GAME_TEXT_H

#include <SDL3/SDL.h>

typedef struct TextRenderer TextRenderer;

TextRenderer *text_renderer_create(SDL_Renderer *renderer, const char *font_path, float point_size);
void text_renderer_destroy(TextRenderer *text);

/* Changes the font size (the UI scale changed): the cached strings are laid
 * out again. Returns false (the size unchanged) on failure. */
bool text_renderer_set_size(TextRenderer *text, float point_size);

/* Draws a single line of UTF-8 text with its top-left corner at (x, y). */
void text_draw(TextRenderer *text, const char *utf8, float x, float y, SDL_Color color);

/* Size in pixels of a single line of text. */
void text_measure(TextRenderer *text, const char *utf8, int *width, int *height);

int text_line_height(const TextRenderer *text);

/* Call once per frame: evicts cached strings that were not used recently. */
void text_renderer_end_frame(TextRenderer *text);

#endif /* COLONY_GAME_TEXT_H */
