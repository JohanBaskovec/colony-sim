/*
 * World sprites: the PNG files of assets/sprites/ (made by
 * tools/generate_sprites.py) packed into one texture atlas, with copies shrunk
 * by halves for zoomed-out views (SDL has no mipmaps). Sprites are drawn
 * through a vertex batch, so thousands of trees cost a few draw calls.
 *
 * Scale: SPRITE_PIXELS_PER_TILE sprite pixels per tile. A sprite is anchored by
 * the middle of its bottom edge and may overflow its tile upward and sideways.
 */
#ifndef COLONY_GAME_SPRITES_H
#define COLONY_GAME_SPRITES_H

#include "core/appearance.h"
#include "core/colonist.h"
#include "core/map.h"

#include <SDL3/SDL.h>

#define SPRITE_PIXELS_PER_TILE 128

typedef int SpriteId;

SpriteId sprite_tree(int stage);
SpriteId sprite_bush(int stage, bool fruit);
/* Character layers, drawn in this order. The body is naked, without arms nor
 * legs: clothes will be layers drawn over it. */
SpriteId sprite_body(int body, int skin);
SpriteId sprite_head(int skin, ColonistExpression expression);
SpriteId sprite_beard(int style); /* Style 1 .. APPEARANCE_BEARD_COUNT - 1. */
SpriteId sprite_hair(int style);  /* Style 1 .. APPEARANCE_HAIR_STYLE_COUNT - 1. */
/* A fist, and a tool held in it (-1 for items without a sprite). Both are
 * drawn centered on the hand: the grip is the middle of the sprite. */
SpriteId sprite_fist(int skin);
SpriteId sprite_tool(ItemType type);

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

/* Size of a sprite on screen at the zoom of the current batch, in pixels. */
void sprites_size(const Sprites *sprites, SpriteId id, float *width, float *height);

/* Draws what was queued: call it before drawing anything else over sprites. */
void sprites_flush(Sprites *sprites);

#endif /* COLONY_GAME_SPRITES_H */
