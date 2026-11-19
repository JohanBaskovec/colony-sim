/*
 * Shadows on screen (docs/SHADOWS.md §4): how much of the direct sun reaches
 * the surfaces drawn when a level is shown, at several points per tile, so
 * that the edge of a shadow crosses the tiles.
 *
 * The rays (sun_lit()) are cast ahead of time, by pieces of the world
 * (SHADOW_CHUNK tiles a side) and by half hours of game time: for each
 * point, how lit it is at the start, the middle and the end of the half
 * hour, and between two of them that differ, the moment the edge of a shadow
 * passes (found by halving). A frame casts no ray: it reads, for the game's
 * time now, the value before or after each point's moment, blended a little
 * around it. Each point changing at its own moment, the shadows glide at any
 * speed and stand still when the game is paused.
 *
 * The work is done within a budget of rays per call: the pieces in view that
 * have nothing to show first, then those out of date (the map changed within
 * their reach: Map.light_stamps), then the next half hour ahead. A piece
 * keeps the half hour it shows and the next one; pieces long out of view
 * are forgotten.
 *
 * Only derived from a map (the known world on screen) and the time: nothing
 * here is part of the game hash.
 */
#ifndef COLONY_CORE_SHADOW_H
#define COLONY_CORE_SHADOW_H

#include "core/game_clock.h"
#include "core/map.h"
#include "core/sun.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Tiles a side of a piece of the shadows. */
#define SHADOW_CHUNK 16
/* Points a side of a tile, at most (the frontend picks fewer as the view
 * zooms out). */
#define SHADOW_SAMPLES_MAX 8
/* Game time between two moments the rays are cast for: half an hour. */
#define SHADOW_STEP_TICKS (TICKS_PER_HOUR / 2)
/* Moments half a step is cut into, to find when an edge passes (a power of
 * two: as many rays as its logarithm). */
#define SHADOW_STEP_PARTS 32
/* Width of the blend around a point's moment, in halves of a step. */
#define SHADOW_BLEND 0.08f
/* Frames a piece may stay out of view before it is forgotten. */
#define SHADOW_FORGET_FRAMES 600
/* The share of the light that comes from the whole sky under a clear sky
 * (§2.4, tuning); the rest comes straight from the sun. Under clouds, fog,
 * rain or snow all of it comes from the sky. */
#define SKY_DIFFUSE_CLEAR 0.35f

#define SHADOW_NO_STEP UINT64_MAX

/* What a piece holds for one half hour. */
typedef struct ShadowSlot {
    uint64_t step; /* tick / SHADOW_STEP_TICKS; SHADOW_NO_STEP when empty. */
    int level;     /* The level shown it was worked out for. */
    int samples;   /* Points a side of a tile. */
    /* The light revision it was worked out at, and the tiles whose changes
     * reach it (the piece, and towards the sun as far as the rays go). */
    uint32_t revision;
    int reach_x0, reach_y0, reach_x1, reach_y1;
    size_t capacity;  /* Points the buffers hold. */
    uint8_t *lit;     /* Per point: start, middle, end (0 .. 255). */
    uint8_t *moments; /* Per point: when it changes in each half (0 .. 255). */
} ShadowSlot;

typedef struct ShadowChunk {
    ShadowSlot slots[2];
    uint32_t seen_frame; /* Last frame it was in view. */
} ShadowChunk;

/* The piece being worked out, a row of points at a time. */
typedef struct ShadowJob {
    bool active;
    int chunk;
    int row;
    ShadowSlot work;
    SunPosition suns[2 * SHADOW_STEP_PARTS + 1];
} ShadowJob;

typedef struct ShadowField {
    const Map *map; /* What the shadows were worked out from (not owned). */
    int width, height;
    int columns, rows; /* Pieces. */
    ShadowChunk *chunks;
    float latitude;
    /* What is wanted (shadow_field_view()): the level shown, the points a
     * side of a tile, the tiles in view (x1, y1 excluded). */
    int level, samples;
    int view_x0, view_y0, view_x1, view_y1;
    uint32_t frame;
    ShadowJob job;
    uint64_t rays;      /* Cast so far. */
    uint64_t completed; /* Pieces worked out so far. */
} ShadowField;

void shadow_field_init(ShadowField *field, float latitude);
void shadow_field_free(ShadowField *field);

/* Forgets everything worked out (the next calls start again). */
void shadow_field_clear(ShadowField *field);

/* What is shown this frame: the level, the points a side of a tile (1 ..
 * SHADOW_SAMPLES_MAX) and the tiles in view (x1, y1 excluded). */
void shadow_field_view(ShadowField *field, int level, int samples, int x0, int y0, int x1, int y1);

/* Works the shadows out from `map` for the game's time `tick`, casting about
 * `ray_budget` rays at most (a row of points at a time). Starts again when
 * the map is another one (the known world turned on or off) or of another
 * size. Returns false when memory ran out. */
bool shadow_field_work(ShadowField *field, const Map *map, uint64_t tick, uint64_t ray_budget);

/* Whether every piece in view has the half hour of `tick`, up to date. */
bool shadow_field_ready(const ShadowField *field, uint64_t tick);

/* How lit by the sun the point (x, y) of the surface drawn there is at
 * `tick`, 0 .. 1 (`tick_fraction`: how far into the next tick the frame
 * is, 0 .. 1); 1 where nothing is known yet. */
float shadow_field_lit(const ShadowField *field, uint64_t tick, float tick_fraction, float x,
                       float y);

/* shadow_field_lit() at the middle of the points of `columns` × `rows` tiles
 * from (x0, y0), `samples` a side of a tile, row by row into `out`. */
void shadow_field_fill(const ShadowField *field, uint64_t tick, float tick_fraction, int x0, int y0,
                       int columns, int rows, int samples, float *out);

/* The height (metres over the bottom of level 0) a ray starts from on the
 * surface drawn at the column (x, y) when level z is shown: the floor seen
 * there, or the top of a block (rock, a wall, dense foliage). False when
 * nothing is drawn there. */
bool shadow_surface_height(const Map *map, int x, int y, int z, float *height);

/* The share of the daylight that comes straight from the sun (§2.4): none
 * under an overcast sky (`clear` 0) and fading out under SUN_LOW_ELEVATION,
 * 1 - SKY_DIFFUSE_CLEAR with the sun high in a clear sky. */
float shadow_direct_share(float elevation, float clear);

/* The light of a point (§2.4), 0 .. 1: the night sky and the sky's diffuse
 * light through what lies over it (`cover`), the direct sun as far as it
 * reaches (`lit`). `night` and `day` are the night sky's and the day's
 * parts of the light (the weather included). */
static inline float shadow_light(float night, float day, float direct, float cover, float lit)
{
    return night * cover + day * ((1.0f - direct) * cover + direct * lit);
}

#endif /* COLONY_CORE_SHADOW_H */
