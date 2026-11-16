/*
 * World rendering. The ground, zones and items are still drawn as colored
 * shapes; plants and colonists are sprites (see game/sprites.h).
 */
#ifndef COLONY_GAME_RENDER_H
#define COLONY_GAME_RENDER_H

#include "core/camera.h"
#include "core/game.h"
#include "core/map.h"
#include "game/sprites.h"

#include <SDL3/SDL.h>

/* The ground of the level shown (Camera.level): terrain, rock, slopes, the
 * edges of drops. Open space is left as it is: the levels below show there
 * (render_below_begin()). */
void render_map(SDL_Renderer *renderer, const Map *map, const Camera *camera);

/*
 * The levels below the one shown, seen through open space: blurred and
 * darker with depth. render_below_begin() returns false when there is no
 * open space in view (nothing to draw); otherwise what is drawn until
 * render_below_end() goes to a smaller offscreen image, which
 * render_below_end() stretches over the screen. Draw the deepest level first
 * and call render_below_shade() after each level: it darkens what is already
 * drawn.
 */
bool render_below_begin(SDL_Renderer *renderer, const Map *map, const Camera *camera);
void render_below_shade(SDL_Renderer *renderer);
void render_below_end(SDL_Renderer *renderer);

/* A triangle on every ramp of the level shown: pointing up on a ramp that
 * leads up from it, down over one that leads down from it (map_ramp_at()).
 * See-through, drawn over what stands there (items, plants, settlers). */
void render_ramp_markers(SDL_Renderer *renderer, const Map *map, const Camera *camera);

/* A "no entry" sign in the corner of what is forbidden to the settlers on
 * the level shown (game_forbidden()): stacks, plants (a tree on its foot and
 * its trunk), structures (on their head tile), roofs, stations. */
void render_forbidden_markers(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* Rectangle of tiles being forbidden (red) or allowed (green) with the tool. */
void render_forbid_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0, int x1,
                        int y1, bool forbid);

/* Dark spots on dirty tiles. */
void render_dirt(SDL_Renderer *renderer, const Dirt *dirt, const Camera *camera);

/* Snow lying on open ground (Weather.snow), under everything on it. */
void render_snow_cover(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* What falls from the sky (rain, snow, lightning in a storm) or fog, over
 * the whole view; `now_ms`: real time, which animates it. */
void render_weather(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                    Uint64 now_ms);

/* Darkens the screen according to the light level of the hour and the
 * clouds, less around burning hearths, which also glow warm. */
void render_daylight(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* The temperature felt on each visible tile (the warmth of fires
 * included), from blue (cold) to red (hot). */
void render_temperature(SDL_Renderer *renderer, const Game *game, const Camera *camera);

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

/* Built structures (beds, walls, doors), built roofs (the floor of their
 * level, over the walls of the level below) and construction sites. */
void render_structures(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* Open tiles a roof could rest on (construction_roof_held()), shown while
 * planning roofs. */
void render_roof_support(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* Ghosts of the structures a rectangle would plan (game_plan_structure_rect():
 * its edges for walls, beds side by side facing `direction`), red where they
 * cannot be placed. */
void render_structure_preview(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                              StructureType type, Direction direction, int x0, int y0, int x1,
                              int y1);

/* Rectangle of tiles being added to (or removed from) stockpile zones. */
void render_stockpile_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0, int x1,
                           int y1, bool remove);

/* Gathering zones (and the progress of the job under way on each tile); the
 * selected one (index, or GATHER_ZONE_NONE) is brighter. */
void render_gather_zones(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                         int selected);

/* Rectangle of tiles being added to (or removed from) gathering zones. */
void render_gather_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0, int x1,
                        int y1, bool remove);

/* Plants (trees, shrubs, low plants) and colonists (with what they carry),
 * from the back to the front so that tall sprites overlap correctly; selected
 * colonists get a ring and their current path. */
void render_world_sprites(SDL_Renderer *renderer, Sprites *sprites, const Game *game,
                          const Camera *camera, const EntityId *selected, int selected_count);

/* Rectangle shown while box-selecting, in screen coordinates. */
void render_selection_box(SDL_Renderer *renderer, float x0, float y0, float x1, float y1);

void render_tile_outline(SDL_Renderer *renderer, const Camera *camera, int x, int y,
                         SDL_Color color);

/* Thick outline of a selected tile (a structure). */
void render_tile_selection(SDL_Renderer *renderer, const Camera *camera, int x, int y);

/* Releases the buffers the renderer allocated on first use. */
/* Icons of the build menu, drawn inside `rect`. */
typedef enum RenderIcon {
    RENDER_ICON_GATHER_ZONE,
    RENDER_ICON_GATHER_ZONE_REMOVE, /* The zone icon with a "minus" sign. */
    RENDER_ICON_STOCKPILE,
    RENDER_ICON_STOCKPILE_REMOVE,
    RENDER_ICON_MINE,
    RENDER_ICON_DIG,
    RENDER_ICON_DIG_RAMP,
    RENDER_ICON_DIG_DOWN,
    RENDER_ICON_CHOP,
    RENDER_ICON_DECONSTRUCT,
    RENDER_ICON_FORBID, /* The "no entry" sign. */
    RENDER_ICON_ALLOW,
    RENDER_ICON_CANCEL,
} RenderIcon;

void render_workstation_icon(SDL_Renderer *renderer, SDL_FRect rect, WorkstationType type);
void render_tool_icon(SDL_Renderer *renderer, SDL_FRect rect, RenderIcon icon);
void render_structure_icon(SDL_Renderer *renderer, SDL_FRect rect, StructureType type);

void render_shutdown(void);

#endif /* COLONY_GAME_RENDER_H */
