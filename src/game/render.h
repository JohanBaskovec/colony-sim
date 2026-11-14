/*
 * World rendering. Placeholder graphics: tiles are drawn as colored
 * rectangles until sprites are introduced.
 */
#ifndef COLONY_GAME_RENDER_H
#define COLONY_GAME_RENDER_H

#include "core/camera.h"
#include "core/game.h"
#include "core/map.h"

#include <SDL3/SDL.h>

void render_map(SDL_Renderer *renderer, const Map *map, const Camera *camera);

/* Darkens the whole screen according to the light level (1 = full daylight). */
void render_daylight(SDL_Renderer *renderer, float daylight, float width, float height);

/* Marks designated tiles (chop, mine...) and shows work progress. */
void render_designations(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* Rectangle of tiles being designated with a tool (corners in any order). */
void render_designation_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0,
                             int x1, int y1, DesignationType type);

/* Draws every colonist; selected ones get a ring and their current path. */
void render_colonists(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                      const EntityId *selected, int selected_count);

/* Rectangle shown while box-selecting, in screen coordinates. */
void render_selection_box(SDL_Renderer *renderer, float x0, float y0, float x1, float y1);

void render_tile_outline(SDL_Renderer *renderer, const Camera *camera, int x, int y,
                         SDL_Color color);

#endif /* COLONY_GAME_RENDER_H */
