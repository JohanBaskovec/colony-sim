#include "core/river.h"

#include "core/noise.h"
#include "core/rng.h"
#include "core/game_clock.h"
#include "core/util.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

/* How far the course strays from its smooth line at most, as a share of the
 * map's smaller side, and the length of a meander's swing, in tiles. */
#define MEANDER_SHARE 0.14f
#define MEANDER_LENGTH 70.0f
/* Its points along the edges stay within this middle part of them. */
#define EDGE_MARGIN 0.2f
/* How much more often a river leaves by the opposite edge than by either
 * neighbouring one. */
#define OPPOSITE_WEIGHT 2
/* Exact distances out to this far beyond the channel's half width. */
#define EXACT_REACH 3.0f

typedef struct Point {
    float x, y;
} Point;

void river_default_params(RiverParams *river)
{
    river->width = 0;
    river->from = MAP_SIDE_COUNT;
    river->to = MAP_SIDE_COUNT;
    river->from_at = 0.5f;
    river->to_at = 0.5f;
    river->discharge = 1.0f;
}

void river_resolve(const RiverParams *river, uint64_t seed, RiverParams *out)
{
    *out = *river;
    out->width = clampi(river->width, 0, RIVER_MAX_WIDTH);
    out->discharge = river->discharge > 0.0f ? river->discharge : 0.0f;
    Rng rng;
    rng_seed(&rng, seed, 0x7269766572ULL /* "river" */);
    /* Always the same draws, whatever is already set. */
    MapSide from = (MapSide)rng_range_i32(&rng, 0, MAP_SIDE_COUNT - 1);
    int turn = rng_range_i32(&rng, 0, OPPOSITE_WEIGHT + 1); /* 0, 1: a neighbour; else opposite. */
    float from_at = rng_next_float(&rng), to_at = rng_next_float(&rng);
    if ((unsigned)out->from >= MAP_SIDE_COUNT) {
        out->from = from;
        out->from_at = from_at;
    }
    if ((unsigned)out->to >= MAP_SIDE_COUNT || out->to == out->from) {
        int offset = turn == 0 ? 1 : turn == 1 ? 3 : 2;
        out->to = (MapSide)((out->from + offset) % MAP_SIDE_COUNT);
        out->to_at = to_at;
    }
    out->from_at = lerpf(EDGE_MARGIN, 1.0f - EDGE_MARGIN, clampf(out->from_at, 0.0f, 1.0f));
    out->to_at = lerpf(EDGE_MARGIN, 1.0f - EDGE_MARGIN, clampf(out->to_at, 0.0f, 1.0f));
}

/* The point of an edge at `at` along it, a tile out of the map (so that the
 * channel reaches the edge's tiles whole), and the way into the map there. */
static Point edge_point(MapSide side, float at, int width, int height, Point *inward)
{
    float x = at * (float)(width - 1), y = at * (float)(height - 1);
    switch (side) {
    case MAP_SIDE_NORTH: *inward = (Point){0.0f, 1.0f}; return (Point){x, -1.0f};
    case MAP_SIDE_SOUTH: *inward = (Point){0.0f, -1.0f}; return (Point){x, (float)height};
    case MAP_SIDE_WEST: *inward = (Point){1.0f, 0.0f}; return (Point){-1.0f, y};
    default: *inward = (Point){-1.0f, 0.0f}; return (Point){(float)width, y};
    }
}

/* A cubic Bézier curve and its derivative at t. */
static Point bezier(const Point p[4], float t, Point *tangent)
{
    float u = 1.0f - t;
    float b0 = u * u * u, b1 = 3.0f * u * u * t, b2 = 3.0f * u * t * t, b3 = t * t * t;
    float d0 = -3.0f * u * u, d1 = 3.0f * u * u - 6.0f * u * t, d2 = 6.0f * u * t - 3.0f * t * t,
          d3 = 3.0f * t * t;
    tangent->x = d0 * p[0].x + d1 * p[1].x + d2 * p[2].x + d3 * p[3].x;
    tangent->y = d0 * p[0].y + d1 * p[1].y + d2 * p[2].y + d3 * p[3].y;
    return (Point){b0 * p[0].x + b1 * p[1].x + b2 * p[2].x + b3 * p[3].x,
                   b0 * p[0].y + b1 * p[1].y + b2 * p[2].y + b3 * p[3].y};
}

/* Lowers out[] around a point of the course, out to `reach` tiles. */
static void stamp(float *out, int width, int height, Point at, float reach)
{
    int x0 = (int)floorf(at.x - reach), x1 = (int)ceilf(at.x + reach);
    int y0 = (int)floorf(at.y - reach), y1 = (int)ceilf(at.y + reach);
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > width - 1 ? width - 1 : x1;
    y1 = y1 > height - 1 ? height - 1 : y1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x - at.x, dy = (float)y - at.y;
            float d = sqrtf(dx * dx + dy * dy);
            size_t column = (size_t)y * (size_t)width + (size_t)x;
            out[column] = d < out[column] ? d : out[column];
        }
    }
}

/* Carries the distances further out, over the eight neighbours (two
 * sweeps): approximate, enough for the walls of a valley. */
static void spread(float *out, int width, int height)
{
    const float diagonal = 1.41421356f;
    for (int pass = 0; pass < 2; pass++) {
        for (int step = 0; step < height; step++) {
            int y = pass == 0 ? step : height - 1 - step;
            for (int across = 0; across < width; across++) {
                int x = pass == 0 ? across : width - 1 - across;
                size_t column = (size_t)y * (size_t)width + (size_t)x;
                float best = out[column];
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = x + dx, ny = y + dy;
                        if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= width ||
                            ny >= height) {
                            continue;
                        }
                        float d = out[(size_t)ny * (size_t)width + (size_t)nx] +
                                  (dx != 0 && dy != 0 ? diagonal : 1.0f);
                        best = d < best ? d : best;
                    }
                }
                out[column] = best;
            }
        }
    }
}

bool river_distances(const RiverParams *river, uint64_t seed, int width, int height, float *out)
{
    if (river->width <= 0 || width <= 0 || height <= 0 || (unsigned)river->from >= MAP_SIDE_COUNT ||
        (unsigned)river->to >= MAP_SIDE_COUNT || river->from == river->to) {
        return false;
    }
    Point in_from, in_to;
    Point start = edge_point(river->from, river->from_at, width, height, &in_from);
    Point end = edge_point(river->to, river->to_at, width, height, &in_to);
    float length = hypotf(end.x - start.x, end.y - start.y);
    float pull = length * 0.4f;
    const Point curve[4] = {start,
                            {start.x + in_from.x * pull, start.y + in_from.y * pull},
                            {end.x + in_to.x * pull, end.y + in_to.y * pull},
                            end};
    float smaller = (float)(width < height ? width : height);
    float meander = smaller * MEANDER_SHARE;
    float half = (float)river->width * 0.5f;
    uint32_t noise_seed = (uint32_t)(seed ^ (seed >> 32)) ^ 0x6d65616eu /* "mean" */;
    float swings = length / MEANDER_LENGTH;
    size_t columns = (size_t)width * (size_t)height;
    for (size_t c = 0; c < columns; c++) {
        out[c] = FLT_MAX;
    }
    int samples = (int)(length * 4.0f) + 16;
    for (int i = 0; i <= samples; i++) {
        float t = (float)i / (float)samples;
        Point tangent;
        Point at = bezier(curve, t, &tangent);
        float norm = hypotf(tangent.x, tangent.y);
        if (norm > 0.0f) {
            /* Away from the smooth line, square to it, none at the edges. */
            float swing = (noise_fbm2d(noise_seed, t * swings, 0.37f, 3, 2.0f, 0.5f) - 0.5f) * 2.0f;
            float offset = meander * swing * sinf(3.14159265f * t);
            at.x += -tangent.y / norm * offset;
            at.y += tangent.x / norm * offset;
        }
        /* Kept within the map, but where it comes in and goes out. */
        bool inside = at.x >= 0.0f && at.y >= 0.0f && at.x <= (float)(width - 1) &&
                      at.y <= (float)(height - 1);
        if (inside) {
            at.x = clampf(at.x, half, (float)(width - 1) - half);
            at.y = clampf(at.y, half, (float)(height - 1) - half);
        }
        stamp(out, width, height, at, half + EXACT_REACH);
    }
    spread(out, width, height);
    return true;
}

void river_flow_hour(RiverFlow *flow, float rain_mm)
{
    float quick_out = flow->quick / RIVER_QUICK_HOURS;
    flow->quick += (rain_mm > 0.0f ? rain_mm : 0.0f) - quick_out;
    flow->slow += quick_out - flow->slow / RIVER_SLOW_HOURS;
}

int32_t river_stage(const RiverFlow *flow, int day_of_year)
{
    float angle = 6.2831853f * (float)(day_of_year - RIVER_SEASON_PEAK_DAY) / (float)DAYS_PER_YEAR;
    float stage = flow->discharge * (RIVER_SEASON_MM * cosf(angle) + RIVER_RAIN_RISE * flow->slow);
    return (int32_t)clampf(roundf(stage), (float)RIVER_STAGE_MIN_MM, (float)RIVER_STAGE_MAX_MM);
}

int32_t river_head(int32_t stage)
{
    float factor = clampf(1.0f + (float)stage / 1000.0f, 0.5f, 3.0f);
    return (int32_t)lroundf((float)RIVER_HEAD_MM * factor);
}

uint64_t river_flow_hash(const RiverFlow *flow, uint64_t hash)
{
    if (!flow->flowing) {
        return hash; /* No river: hashes stay as they were. */
    }
    hash = hash_fnv1a64(hash, &flow->rest, sizeof(flow->rest));
    float values[3] = {flow->discharge, flow->quick, flow->slow};
    hash = hash_fnv1a64(hash, values, sizeof(values));
    return hash_fnv1a64(hash, &flow->stage_set, sizeof(flow->stage_set));
}

void river_flow_serialize(Serializer *s, RiverFlow *flow)
{
    ser_bool(s, &flow->flowing);
    ser_float(s, &flow->discharge);
    ser_i64(s, &flow->rest);
    ser_float(s, &flow->quick);
    ser_float(s, &flow->slow);
    ser_i32(s, &flow->stage_set);
    ser_check(s, flow->stage_set >= RIVER_STAGE_MIN_MM && flow->stage_set <= RIVER_STAGE_MAX_MM);
}
