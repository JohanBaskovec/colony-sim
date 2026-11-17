/*
 * World sprites: the PNG files of assets/sprites/ (made by
 * tools/generate_sprites.py) packed into one texture atlas, with copies shrunk
 * by halves for zoomed-out views (SDL has no mipmaps). Sprites are drawn
 * through a vertex batch, so thousands of trees cost a few draw calls.
 *
 * Scale: SPRITE_PIXELS_PER_TILE sprite pixels per tile. A sprite is anchored by
 * the middle of its bottom edge and may overflow its tile upward and sideways;
 * tile textures (ground, walls, grass) cover exactly one tile.
 */
#ifndef COLONY_GAME_SPRITES_H
#define COLONY_GAME_SPRITES_H

#include "core/animal.h"
#include "core/appearance.h"
#include "core/colonist.h"
#include "core/map.h"

#include <SDL3/SDL.h>

#define SPRITE_PIXELS_PER_TILE 128

typedef int SpriteId;

/* A plant at a growth stage (0 .. PLANT_GROWTH_STAGES - 1), bearing food or
 * not; -1 for plants without a sprite (grass is part of the ground, a tree
 * past its sapling stages is trunk and foliage tiles). */
SpriteId sprite_plant(PlantType type, int stage, bool fruit);
/* How a character is seen: from the front, from the side (facing right:
 * mirror it when it faces left) or from behind. */
typedef enum SpriteView {
    SPRITE_VIEW_FRONT,
    SPRITE_VIEW_SIDE,
    SPRITE_VIEW_BACK,
    SPRITE_VIEW_COUNT
} SpriteView;
/* Character layers, drawn in this order. The body is naked, without arms nor
 * legs: clothes will be layers drawn over it. From behind a head shows no
 * expression, and a beard does not show at all (-1). */
SpriteId sprite_body(int body, int skin, SpriteView view);
SpriteId sprite_head(int skin, ColonistExpression expression, SpriteView view);
/* Style 1 .. APPEARANCE_BEARD_COUNT - 1. */
SpriteId sprite_beard(int style, SpriteView view);
/* Style 1 .. APPEARANCE_HAIR_STYLE_COUNT - 1. */
SpriteId sprite_hair(int style, SpriteView view);
/* A fist, and a tool held in it (-1 for items without a sprite). Both are
 * drawn centered on the hand: the grip is the middle of the sprite. */
SpriteId sprite_fist(int skin);
/* A layer over a head seen from the front (after the head, before beard and
 * hair): closed eyes, for a settler asleep. And the Z of sleep. */
SpriteId sprite_closed_eyes(int skin);
SpriteId sprite_sleep_z(void);
SpriteId sprite_tool(ItemType type);
/* An animal seen from the side, facing right (mirror it when it faces left):
 * standing, eating (head down) or asleep (lying); and standing seen from the
 * front and from behind, for one that walks down or up the screen. */
typedef enum AnimalPose {
    ANIMAL_POSE_STAND,
    ANIMAL_POSE_EAT,
    ANIMAL_POSE_SLEEP,
    ANIMAL_POSE_FRONT,
    ANIMAL_POSE_BACK,
    ANIMAL_POSE_COUNT
} AnimalPose;
SpriteId sprite_animal(AnimalSpecies species, AnimalSex sex, AnimalPose pose);
/* Tile textures, drawn with sprites_draw_tile(), the same on every tile of
 * their kind (they tile seamlessly): the ground of a terrain seen from above,
 * the cut face of a rock wall seen from inside it (dark and flat, unlike any
 * ground), and grass over the ground, taller with its growth (0 .. 1). -1 for
 * terrains and rocks without a texture. */
SpriteId sprite_ground(Terrain terrain);
SpriteId sprite_wall(RockType rock);
SpriteId sprite_grass(float growth);

typedef struct Sprites Sprites;

/* Loads every sprite from `assets_dir` (ending with a separator). Returns NULL
 * (after logging why) when a file is missing or the atlas does not fit. */
Sprites *sprites_create(SDL_Renderer *renderer, const char *assets_dir);
void sprites_destroy(Sprites *sprites);

/* Starts a batch at the given zoom (screen pixels per tile), which picks the
 * atlas copy closest to that size. */
void sprites_begin(Sprites *sprites, float pixels_per_tile);

/* Queues a sprite with its anchor (bottom middle) at a screen position. The
 * color multiplies the sprite (white: unchanged); `flip` mirrors it. */
void sprites_draw(Sprites *sprites, SpriteId id, float anchor_x, float anchor_y, SDL_FColor color,
                  bool flip);

/* Queues a sprite centered on a screen position, turned clockwise by `degrees`
 * around its center (after mirroring when `flip`). */
void sprites_draw_rotated(Sprites *sprites, SpriteId id, float center_x, float center_y,
                          float degrees, SDL_FColor color, bool flip);

/* Queues a sprite centered on a screen position, `scale` times its size. */
void sprites_draw_scaled(Sprites *sprites, SpriteId id, float center_x, float center_y, float scale,
                         SDL_FColor color);

/* Queues a tile texture stretched over a screen rectangle (a whole tile). */
void sprites_draw_tile(Sprites *sprites, SpriteId id, const SDL_FRect *rect, SDL_FColor color);

/* Size of a sprite on screen at the zoom of the current batch, in pixels. */
void sprites_size(const Sprites *sprites, SpriteId id, float *width, float *height);

/* Draws what was queued: call it before drawing anything else over sprites. */
void sprites_flush(Sprites *sprites);

#endif /* COLONY_GAME_SPRITES_H */
