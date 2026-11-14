/*
 * World rendering. Placeholder graphics: tiles are drawn as colored
 * rectangles until sprites are introduced.
 */
#ifndef COLONY_GAME_RENDER_H
#define COLONY_GAME_RENDER_H

#include "core/camera.h"
#include "core/map.h"

#include <SDL3/SDL.h>

void render_map(SDL_Renderer *renderer, const Map *map, const Camera *camera);

/* Darkens the whole screen according to the light level (1 = full daylight). */
void render_daylight(SDL_Renderer *renderer, float daylight, float width, float height);

void render_tile_outline(SDL_Renderer *renderer, const Camera *camera, int x, int y,
                         SDL_Color color);

#endif /* COLONY_GAME_RENDER_H */
