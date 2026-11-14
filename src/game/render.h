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

/* Items lying on the ground (placeholder shapes per item type). */
void render_items(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* Marks designated tiles (chop, mine...) and shows work progress. */
void render_designations(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* Rectangle of tiles being designated with a tool (corners in any order). */
void render_designation_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0,
                             int x1, int y1, DesignationType type);

/* Stockpile zones as a translucent overlay; the selected one (index, or
 * STOCKPILE_NONE) is brighter and outlined. */
void render_stockpiles(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                       int selected);

/* Workstations and blueprints (with their progress); the selected one (index,
 * or WORKSTATION_NONE) is outlined. */
void render_workstations(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                         int selected);

/* Ghost of a blueprint under the cursor, red where it cannot be placed. */
void render_blueprint_preview(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                              WorkstationType type, int x, int y);

/* Rectangle of tiles being added to (or removed from) stockpile zones. */
void render_stockpile_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0, int x1,
                           int y1, bool remove);

/* Gathering zones (and the progress of the stone being looked for); the
 * selected one (index, or GATHER_ZONE_NONE) is brighter. */
void render_gather_zones(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                         int selected);

/* Rectangle of tiles being added to (or removed from) gathering zones. */
void render_gather_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0, int x1,
                        int y1, bool remove);

/* Draws every colonist (and what they carry); selected ones get a ring and their current path. */
void render_colonists(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                      const EntityId *selected, int selected_count);

/* Rectangle shown while box-selecting, in screen coordinates. */
void render_selection_box(SDL_Renderer *renderer, float x0, float y0, float x1, float y1);

void render_tile_outline(SDL_Renderer *renderer, const Camera *camera, int x, int y,
                         SDL_Color color);

/* Releases the buffers the renderer allocated on first use. */
void render_shutdown(void);

#endif /* COLONY_GAME_RENDER_H */
