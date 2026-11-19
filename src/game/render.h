/*
 * World rendering. Plants, colonists and most of the ground (tile textures)
 * are sprites (see game/sprites.h); zones, items and the other terrains are
 * still drawn as colored shapes.
 */
#ifndef COLONY_GAME_RENDER_H
#define COLONY_GAME_RENDER_H

#include "core/camera.h"
#include "core/game.h"
#include "core/map.h"
#include "game/sprites.h"
#include "game/theme.h"

#include <SDL3/SDL.h>

/* The ground of the level shown (Camera.level): terrain seen from above with
 * its grass, the cut face of rock walls, slopes, the edges of drops. Open space is left as it is:
 * the levels below show there (render_below_begin()). */
void render_map(SDL_Renderer *renderer, Sprites *sprites, const Map *map, const Camera *camera);

/*
 * The levels below the one shown, seen through open space however deep:
 * blurred and veiled a little more with each level, never all dark.
 * render_below_begin() works out the level seen on each visible tile
 * (map_view_level()) and sets `levels` to those seen somewhere (bit z for
 * level z); it returns false when no level below is seen (nothing to draw).
 * Otherwise what is drawn until render_below_end() goes to a smaller
 * offscreen image, which render_below_end() stretches over the screen, and
 * the render functions given a camera on one of these levels only draw the
 * tiles where that level is the one seen: each tile is drawn once, whatever
 * the depth. Draw the levels of `levels` deepest first, and after each call
 * render_below_shade() with that level and the next one drawn (the level
 * shown after the last): it veils what is already drawn.
 */
bool render_below_begin(SDL_Renderer *renderer, const Map *map, const Camera *camera,
                        uint32_t *levels);
void render_below_shade(SDL_Renderer *renderer, int level, int next_level);
void render_below_end(SDL_Renderer *renderer);

/* A triangle on every ramp of the level shown: pointing up on a ramp that
 * leads up from it, down over one that leads down from it (map_ramp_at()).
 * See-through, drawn over what stands there (items, plants, settlers). */
void render_ramp_markers(SDL_Renderer *renderer, const Map *map, const Camera *camera);

/* A "no entry" sign in the corner of what is forbidden to the settlers on
 * the level shown (game_forbidden()): stacks, plants (a tree on its foot and
 * its trunk), structures (on their head tile), roofs, stations. */
void render_forbidden_markers(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* The item moves ordered: a line from each stack to the tile it goes to, which
 * is outlined. */
void render_item_moves(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* Rectangle of tiles being forbidden (red) or allowed (green) with the tool. */
void render_forbid_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0, int x1,
                        int y1, bool forbid);

/* Dark spots on dirty tiles. */
void render_dirt(SDL_Renderer *renderer, const Dirt *dirt, const Camera *camera);

/* Snow lying on open ground (Weather.snow), under everything on it. */
void render_snow_cover(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* Blood on the ground (core/blood.h): drops and pools on the level shown,
 * drying and fading. */
void render_blood(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* What falls from the sky (rain, snow, lightning in a storm) or fog, over
 * the whole view; `now_ms`: real time, which animates it. */
void render_weather(SDL_Renderer *renderer, const Game *game, const Camera *camera, Uint64 now_ms);

/* Darkens the screen according to the light level of the hour and the
 * clouds, less around burning hearths, which also glow warm. */
void render_daylight(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* The temperature felt on each visible tile (the warmth of fires
 * included), from blue (cold) to red or orange (hot): the theme's colors. */
void render_temperature(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                        const Theme *theme);
/* The light on each visible tile now, from dark blue to yellow. */
void render_light(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                  const Theme *theme);
/* The water in the ground of each visible tile of rooting ground
 * (core/soil.h), from ochre (dry) to deep blue (soaked); nothing on water,
 * stone and floors. */
void render_moisture(SDL_Renderer *renderer, const Game *game, const Camera *camera,
                     const Theme *theme);

/* The fog of war on the level shown: tiles that were seen but are not in view
 * now are veiled (what nobody has seen is not drawn: the known world has
 * nothing there). */
void render_fog(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* Debug: veils the tiles of the level shown that the settler in `slot` does
 * not see (slot -1: that nobody sees), as the vision tracks them
 * (core/vision.h). */
void render_vision(SDL_Renderer *renderer, const Game *game, const Camera *camera, int slot);

/* Items lying on the ground (placeholder shapes per item type). */
void render_items(SDL_Renderer *renderer, Sprites *sprites, const Game *game, const Camera *camera);

/* Marks designated tiles (chop, mine...) and shows work progress. */
void render_designations(SDL_Renderer *renderer, const Game *game, const Camera *camera);

/* Rectangle of tiles being designated with a tool (corners in any order). */
void render_designation_area(SDL_Renderer *renderer, const Camera *camera, int x0, int y0, int x1,
                             int y1, DesignationType type);

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

/* Plants (trees, shrubs, low plants), colonists (with what they carry) and
 * the animals the colony sees (game_animal_seen()), from the back to the
 * front so that tall sprites overlap correctly; selected colonists get a
 * ring and their current path, the selected animal (ENTITY_NONE: none) a
 * ring. */
void render_world_sprites(SDL_Renderer *renderer, Sprites *sprites, const Game *game,
                          const Camera *camera, const EntityId *selected, int selected_count,
                          EntityId selected_animal);

/* Rectangle shown while box-selecting, in screen coordinates. */
void render_selection_box(SDL_Renderer *renderer, float x0, float y0, float x1, float y1);

void render_tile_outline(SDL_Renderer *renderer, const Camera *camera, int x, int y,
                         SDL_Color color);

/* Thick outline of a selected tile (a structure). */
void render_tile_selection(SDL_Renderer *renderer, const Camera *camera, int x, int y);

/* A selected item lying on the ground: drawn again over whatever covers it,
 * with a ring around it (where a click picks it). */
void render_item_selection(SDL_Renderer *renderer, Sprites *sprites, const Camera *camera, int x,
                           int y, const Item *item);

/* Releases the buffers the renderer allocated on first use. */
/* Icons of the build menu, drawn inside `rect`. */
typedef enum RenderIcon {
    RENDER_ICON_GATHER_ZONE,
    RENDER_ICON_GATHER_ZONE_REMOVE, /* The zone icon with a "minus" sign. */
    RENDER_ICON_STOCKPILE,
    RENDER_ICON_STOCKPILE_REMOVE,
    RENDER_ICON_MINE,
    RENDER_ICON_DIG_RAMP,
    RENDER_ICON_DIG_DOWN,
    RENDER_ICON_CHOP,
    RENDER_ICON_DECONSTRUCT,
    RENDER_ICON_HARVEST,
    RENDER_ICON_UPROOT,
    RENDER_ICON_FORBID, /* The "no entry" sign. */
    RENDER_ICON_ALLOW,
    RENDER_ICON_MOVE,   /* An arrow toward a spot. */
    RENDER_ICON_FOLLOW, /* A sight: a ring with ticks, a dot in the middle. */
    RENDER_ICON_CANCEL,
    RENDER_ICON_KEEP_OPEN, /* An open door. */
} RenderIcon;

void render_workstation_icon(SDL_Renderer *renderer, SDL_FRect rect, WorkstationType type);
void render_tool_icon(SDL_Renderer *renderer, SDL_FRect rect, RenderIcon icon);
void render_structure_icon(SDL_Renderer *renderer, SDL_FRect rect, StructureType type);

/* A stack of items as it lies on the ground, filling `rect` (the resources list). */
void render_item_icon(SDL_Renderer *renderer, Sprites *sprites, SDL_FRect rect, ItemType type);

/* A colonist's head and shoulders filling `rect` (the colonist bar). */
void render_colonist_portrait(SDL_Renderer *renderer, Sprites *sprites, const Colonist *colonist,
                              SDL_FRect rect);

void render_shutdown(void);

#endif /* COLONY_GAME_RENDER_H */
