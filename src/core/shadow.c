#include "core/shadow.h"

#include "core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Metres in a level. */
#define LEVEL_HEIGHT 2.0f
/* Two values closer than this (of 255) are the same: no edge passes. */
#define EDGE_THRESHOLD 6
/* Tiles added around the reach of a piece. */
#define REACH_MARGIN 2

static int min_int(int a, int b)
{
    return a < b ? a : b;
}

static int max_int(int a, int b)
{
    return a > b ? a : b;
}

static size_t chunk_points(int samples)
{
    size_t side = (size_t)SHADOW_CHUNK * (size_t)samples;
    return side * side;
}

static void slot_free(ShadowSlot *slot)
{
    free(slot->lit);
    free(slot->moments);
    memset(slot, 0, sizeof(*slot));
    slot->step = SHADOW_NO_STEP;
}

static bool slot_reserve(ShadowSlot *slot, size_t points)
{
    if (slot->capacity >= points) {
        return true;
    }
    uint8_t *lit = realloc(slot->lit, points * 3);
    if (lit == NULL) {
        return false;
    }
    slot->lit = lit;
    uint8_t *moments = realloc(slot->moments, points * 2);
    if (moments == NULL) {
        return false;
    }
    slot->moments = moments;
    slot->capacity = points;
    return true;
}

void shadow_field_init(ShadowField *field, float latitude)
{
    memset(field, 0, sizeof(*field));
    field->latitude = latitude;
    field->samples = 1;
    field->job.work.step = SHADOW_NO_STEP;
}

static void free_chunks(ShadowField *field)
{
    size_t count = (size_t)field->columns * (size_t)field->rows;
    for (size_t i = 0; field->chunks != NULL && i < count; i++) {
        slot_free(&field->chunks[i].slots[0]);
        slot_free(&field->chunks[i].slots[1]);
    }
    free(field->chunks);
    field->chunks = NULL;
    field->columns = field->rows = 0;
}

void shadow_field_free(ShadowField *field)
{
    free_chunks(field);
    slot_free(&field->job.work);
    float latitude = field->latitude;
    shadow_field_init(field, latitude);
}

void shadow_field_clear(ShadowField *field)
{
    size_t count = (size_t)field->columns * (size_t)field->rows;
    for (size_t i = 0; field->chunks != NULL && i < count; i++) {
        field->chunks[i].slots[0].step = SHADOW_NO_STEP;
        field->chunks[i].slots[1].step = SHADOW_NO_STEP;
        field->chunks[i].wanted_level = -1;
    }
    field->job.active = false;
}

/* Gets ready for another map (or one of another size). */
static bool reset_for_map(ShadowField *field, const Map *map)
{
    if (map->width != field->width || map->height != field->height || field->chunks == NULL) {
        free_chunks(field);
        int columns = (map->width + SHADOW_CHUNK - 1) / SHADOW_CHUNK;
        int rows = (map->height + SHADOW_CHUNK - 1) / SHADOW_CHUNK;
        field->chunks = calloc((size_t)columns * (size_t)rows, sizeof(*field->chunks));
        if (field->chunks == NULL) {
            return false;
        }
        field->columns = columns;
        field->rows = rows;
        field->width = map->width;
        field->height = map->height;
        for (size_t i = 0; i < (size_t)columns * (size_t)rows; i++) {
            field->chunks[i].slots[0].step = SHADOW_NO_STEP;
            field->chunks[i].slots[1].step = SHADOW_NO_STEP;
            field->chunks[i].seen_frame = field->frame;
            field->chunks[i].wanted_level = -1;
        }
    }
    shadow_field_clear(field);
    field->map = map;
    return true;
}

/* The tiles in view, clipped to the map (x1, y1 excluded); false when none. */
static bool view_tiles(const ShadowField *field, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = clampi(field->view_x0, 0, field->width);
    *y0 = clampi(field->view_y0, 0, field->height);
    *x1 = clampi(field->view_x1, 0, field->width);
    *y1 = clampi(field->view_y1, 0, field->height);
    return field->chunks != NULL && *x1 > *x0 && *y1 > *y0;
}

/* The pieces the tiles in view fall on (x1, y1 included); false when none. */
static bool view_chunks(const ShadowField *field, int *x0, int *y0, int *x1, int *y1)
{
    int tx0, ty0, tx1, ty1;
    if (!view_tiles(field, &tx0, &ty0, &tx1, &ty1)) {
        return false;
    }
    *x0 = tx0 / SHADOW_CHUNK;
    *y0 = ty0 / SHADOW_CHUNK;
    *x1 = (tx1 - 1) / SHADOW_CHUNK;
    *y1 = (ty1 - 1) / SHADOW_CHUNK;
    return true;
}

void shadow_field_view(ShadowField *field, int level, int samples, int x0, int y0, int x1, int y1)
{
    field->frame++;
    field->level = level;
    field->samples = clampi(samples, 1, SHADOW_SAMPLES_MAX);
    field->view_x0 = x0;
    field->view_y0 = y0;
    field->view_x1 = x1;
    field->view_y1 = y1;
    int cx0, cy0, cx1, cy1;
    if (view_chunks(field, &cx0, &cy0, &cx1, &cy1)) {
        for (int cy = cy0; cy <= cy1; cy++) {
            for (int cx = cx0; cx <= cx1; cx++) {
                field->chunks[cy * field->columns + cx].seen_frame = field->frame;
            }
        }
    }
    /* Pieces long out of view are forgotten. */
    for (int i = 0; field->chunks != NULL && i < field->columns * field->rows; i++) {
        ShadowChunk *chunk = &field->chunks[i];
        if (field->frame - chunk->seen_frame > SHADOW_FORGET_FRAMES &&
            chunk->slots[0].capacity + chunk->slots[1].capacity > 0 &&
            (!field->job.active || field->job.chunk != i)) {
            slot_free(&chunk->slots[0]);
            slot_free(&chunk->slots[1]);
        }
    }
}

/* Whether a slot was worked out for the surfaces the level shown draws on
 * the piece (as last looked up). */
static bool slot_fits(const ShadowField *field, const ShadowChunk *chunk, const ShadowSlot *slot)
{
    return chunk->wanted_level == field->level &&
           memcmp(slot->surfaces, chunk->wanted, sizeof(chunk->wanted)) == 0;
}

/* The slot of a piece for a half hour and the surfaces shown, NULL if none. */
static const ShadowSlot *find_slot(const ShadowField *field, const ShadowChunk *chunk,
                                   uint64_t step)
{
    for (int k = 0; k < 2; k++) {
        if (chunk->slots[k].step == step && slot_fits(field, chunk, &chunk->slots[k])) {
            return &chunk->slots[k];
        }
    }
    return NULL;
}

/* Looks up the surfaces the level shown draws on the pieces in view, where
 * the level or the map changed since. */
static void look_up_surfaces(ShadowField *field, const Map *map)
{
    int cx0, cy0, cx1, cy1;
    if (!view_chunks(field, &cx0, &cy0, &cx1, &cy1)) {
        return;
    }
    for (int cy = cy0; cy <= cy1; cy++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            ShadowChunk *chunk = &field->chunks[cy * field->columns + cx];
            int x0 = cx * SHADOW_CHUNK, y0 = cy * SHADOW_CHUNK;
            if (chunk->wanted_level == field->level &&
                map_light_stamp(map, x0, y0, x0 + SHADOW_CHUNK - 1, y0 + SHADOW_CHUNK - 1) <=
                    chunk->wanted_revision) {
                continue;
            }
            for (int j = 0; j < SHADOW_CHUNK; j++) {
                for (int i = 0; i < SHADOW_CHUNK; i++) {
                    chunk->wanted[j * SHADOW_CHUNK + i] =
                        shadow_surface_code(map, x0 + i, y0 + j, field->level);
                }
            }
            chunk->wanted_level = field->level;
            chunk->wanted_revision = map->light_revision;
        }
    }
}

/* Whether a slot is as wanted now: the points asked for, nothing changed
 * within its reach since. */
static bool slot_current(const ShadowField *field, const ShadowSlot *slot)
{
    return slot->samples == field->samples &&
           map_light_stamp(field->map, slot->reach_x0, slot->reach_y0, slot->reach_x1,
                           slot->reach_y1) <= slot->revision;
}

/* How much a piece needs working out (0: nothing to show, 1: out of date, 2:
 * the next half hour missing, 3: nothing), and for which half hour. */
static int chunk_need(const ShadowField *field, int index, uint64_t step, uint64_t *wanted)
{
    const ShadowChunk *chunk = &field->chunks[index];
    const ShadowSlot *now = find_slot(field, chunk, step);
    const ShadowSlot *next = find_slot(field, chunk, step + 1);
    *wanted = step;
    if (now == NULL) {
        return 0;
    }
    if (!slot_current(field, now)) {
        return 1;
    }
    *wanted = step + 1;
    return next == NULL || !slot_current(field, next) ? 2 : 3;
}

/* The piece in view that needs it most (nearest the middle of the view
 * first); false when all are done. */
static bool pick_chunk(const ShadowField *field, uint64_t step, int *index, uint64_t *wanted,
                       int *need)
{
    int cx0, cy0, cx1, cy1;
    if (!view_chunks(field, &cx0, &cy0, &cx1, &cy1)) {
        return false;
    }
    int tx0, ty0, tx1, ty1;
    view_tiles(field, &tx0, &ty0, &tx1, &ty1);
    float middle_x = 0.5f * (float)(tx0 + tx1), middle_y = 0.5f * (float)(ty0 + ty1);
    int best_need = 3;
    float best_distance = 0.0f;
    for (int cy = cy0; cy <= cy1; cy++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            int i = cy * field->columns + cx;
            uint64_t step_wanted;
            int need_here = chunk_need(field, i, step, &step_wanted);
            float dx = ((float)cx + 0.5f) * SHADOW_CHUNK - middle_x;
            float dy = ((float)cy + 0.5f) * SHADOW_CHUNK - middle_y;
            float distance = dx * dx + dy * dy;
            if (need_here < best_need ||
                (need_here == best_need && need_here < 3 && distance < best_distance)) {
                best_need = need_here;
                best_distance = distance;
                *index = i;
                *wanted = step_wanted;
            }
        }
    }
    *need = best_need;
    return best_need < 3;
}

/* The sun at a moment of a half hour: part k of 2 × SHADOW_STEP_PARTS. */
static SunPosition sun_in_step(float latitude, uint64_t step, int part)
{
    double ticks = (double)step * SHADOW_STEP_TICKS +
                   (double)part * SHADOW_STEP_TICKS / (2.0 * SHADOW_STEP_PARTS);
    int day = (int)((uint64_t)(ticks / TICKS_PER_DAY) % DAYS_PER_YEAR);
    double hour = fmod(ticks, (double)TICKS_PER_DAY) / TICKS_PER_HOUR;
    return sun_position(latitude, day, (float)hour);
}

/* Widens a box of tiles by a piece moved towards the sun as far as its rays
 * go before leaving the map's top or their reach. */
static void reach_towards(const Map *map, SunPosition sun, int x0, int y0, int x1, int y1, int *rx0,
                          int *ry0, int *rx1, int *ry1)
{
    float flat = sqrtf(sun.east * sun.east + sun.south * sun.south);
    if (sun.up <= 0.0f || flat <= 0.0f) {
        return;
    }
    float reach = fminf((float)SUN_SHADOW_REACH, (float)map->depth * LEVEL_HEIGHT * flat / sun.up);
    int dx = (int)lroundf(sun.east / flat * reach), dy = (int)lroundf(sun.south / flat * reach);
    *rx0 = min_int(*rx0, x0 + dx);
    *ry0 = min_int(*ry0, y0 + dy);
    *rx1 = max_int(*rx1, x1 + dx);
    *ry1 = max_int(*ry1, y1 + dy);
}

static bool start_job(ShadowField *field, const Map *map, int index, uint64_t step)
{
    ShadowJob *job = &field->job;
    ShadowSlot *work = &job->work;
    const ShadowChunk *chunk = &field->chunks[index];
    size_t points = chunk_points(field->samples);
    if (!slot_reserve(work, points)) {
        return false;
    }
    /* What a slot of the same half hour has for the columns whose surface
     * is the same is kept: only the others are worked out. */
    const ShadowSlot *older = NULL;
    for (int k = 0; k < 2 && older == NULL; k++) {
        const ShadowSlot *slot = &chunk->slots[k];
        if (slot->step == step && slot_current(field, slot)) {
            older = slot;
        }
    }
    for (int c = 0; c < SHADOW_CHUNK_COLUMNS; c++) {
        job->todo[c] = older == NULL || older->surfaces[c] != chunk->wanted[c];
    }
    if (older != NULL) {
        memcpy(work->lit, older->lit, points * 3);
        memcpy(work->moments, older->moments, points * 2);
    }
    memcpy(work->surfaces, chunk->wanted, sizeof(work->surfaces));
    work->step = step;
    work->samples = field->samples;
    work->revision = map->light_revision;
    for (int k = 0; k <= 2 * SHADOW_STEP_PARTS; k++) {
        job->suns[k] = sun_in_step(field->latitude, step, k);
    }
    int x0 = index % field->columns * SHADOW_CHUNK, y0 = index / field->columns * SHADOW_CHUNK;
    int x1 = x0 + SHADOW_CHUNK - 1, y1 = y0 + SHADOW_CHUNK - 1;
    work->reach_x0 = x0;
    work->reach_y0 = y0;
    work->reach_x1 = x1;
    work->reach_y1 = y1;
    for (int k = 0; k <= 2; k++) {
        reach_towards(map, job->suns[k * SHADOW_STEP_PARTS], x0, y0, x1, y1, &work->reach_x0,
                      &work->reach_y0, &work->reach_x1, &work->reach_y1);
    }
    work->reach_x0 -= REACH_MARGIN;
    work->reach_y0 -= REACH_MARGIN;
    work->reach_x1 += REACH_MARGIN;
    work->reach_y1 += REACH_MARGIN;
    job->chunk = index;
    job->row = 0;
    job->active = true;
    return true;
}

static uint8_t to_byte(float value)
{
    return (uint8_t)lroundf(clampf(value, 0.0f, 1.0f) * 255.0f);
}

/* When, between parts a and b of the half hour (lit va and vb), the point
 * changes from one to the other, as a share of the way from a to b. */
static float edge_moment(const Map *map, const SunPosition *suns, int a, int b, float va, float vb,
                         float x, float y, float height, uint64_t *rays)
{
    int lo = a, hi = b;
    while (hi - lo > 1) {
        int middle = (lo + hi) / 2;
        float v = sun_lit(map, suns[middle], x, y, height);
        ++*rays;
        if (fabsf(v - va) <= fabsf(v - vb)) {
            lo = middle;
        } else {
            hi = middle;
        }
    }
    return ((float)(lo + hi) * 0.5f - (float)a) / (float)(b - a);
}

/* Works out one row of points of the piece; returns the rays cast. */
static uint64_t work_row(ShadowField *field, const Map *map)
{
    ShadowJob *job = &field->job;
    ShadowSlot *work = &job->work;
    const int samples = work->samples, side = SHADOW_CHUNK * samples;
    const int x0 = job->chunk % field->columns * SHADOW_CHUNK;
    const int ty = job->chunk / field->columns * SHADOW_CHUNK + job->row / samples;
    const float y = (float)ty + ((float)(job->row % samples) + 0.5f) / (float)samples;
    const SunPosition *suns = job->suns;
    uint8_t *lit = work->lit + (size_t)job->row * (size_t)side * 3;
    uint8_t *moments = work->moments + (size_t)job->row * (size_t)side * 2;
    uint64_t rays = 0;
    const int column_row = job->row / samples * SHADOW_CHUNK;
    for (int tile = 0; tile < SHADOW_CHUNK; tile++) {
        if (!job->todo[column_row + tile]) {
            lit += 3 * samples; /* Kept from an older slot. */
            moments += 2 * samples;
            continue;
        }
        int tx = x0 + tile;
        uint8_t code = work->surfaces[column_row + tile];
        bool drawn = code != 0;
        float height = (float)(code - 1) * LEVEL_HEIGHT + SUN_RAY_LIFT;
        for (int s = 0; s < samples; s++, lit += 3, moments += 2) {
            if (!drawn) {
                lit[0] = lit[1] = lit[2] = 255;
                moments[0] = moments[1] = 0;
                continue;
            }
            float x = (float)tx + ((float)s + 0.5f) / (float)samples;
            float v[3];
            for (int k = 0; k < 3; k++) {
                v[k] = sun_lit(map, suns[k * SHADOW_STEP_PARTS], x, y, height);
                lit[k] = to_byte(v[k]);
            }
            rays += 3;
            for (int half = 0; half < 2; half++) {
                float moment = 0.5f;
                if (abs((int)lit[half] - (int)lit[half + 1]) > EDGE_THRESHOLD) {
                    moment = edge_moment(map, suns, half * SHADOW_STEP_PARTS,
                                         (half + 1) * SHADOW_STEP_PARTS, v[half], v[half + 1], x, y,
                                         height, &rays);
                }
                moments[half] = to_byte(moment);
            }
        }
    }
    job->row++;
    return rays;
}

/* Puts the piece worked out in its place: over the slot of the same half
 * hour, else an empty one, else the one furthest from it in time. */
static void finish_job(ShadowField *field)
{
    ShadowJob *job = &field->job;
    ShadowChunk *chunk = &field->chunks[job->chunk];
    int k = -1;
    for (int i = 0; i < 2 && k < 0; i++) {
        if (chunk->slots[i].step == job->work.step) {
            k = i;
        }
    }
    for (int i = 0; i < 2 && k < 0; i++) {
        if (chunk->slots[i].step == SHADOW_NO_STEP) {
            k = i;
        }
    }
    if (k < 0) {
        uint64_t step = job->work.step;
        uint64_t far0 =
            chunk->slots[0].step > step ? chunk->slots[0].step - step : step - chunk->slots[0].step;
        uint64_t far1 =
            chunk->slots[1].step > step ? chunk->slots[1].step - step : step - chunk->slots[1].step;
        k = far0 >= far1 ? 0 : 1;
    }
    ShadowSlot old = chunk->slots[k];
    chunk->slots[k] = job->work;
    job->work = old;
    job->work.step = SHADOW_NO_STEP;
    job->active = false;
    field->completed++;
}

/* Whether the piece being worked out is still wanted. */
static bool job_wanted(const ShadowField *field)
{
    const ShadowJob *job = &field->job;
    int cx0, cy0, cx1, cy1;
    if (!view_chunks(field, &cx0, &cy0, &cx1, &cy1) || job->work.samples != field->samples) {
        return false;
    }
    int cx = job->chunk % field->columns, cy = job->chunk / field->columns;
    return cx >= cx0 && cx <= cx1 && cy >= cy0 && cy <= cy1 &&
           slot_fits(field, &field->chunks[job->chunk], &job->work);
}

bool shadow_field_work(ShadowField *field, const Map *map, uint64_t tick, uint64_t ray_budget)
{
    if ((map != field->map || map->width != field->width || map->height != field->height) &&
        !reset_for_map(field, map)) {
        return false;
    }
    uint64_t step = tick / SHADOW_STEP_TICKS;
    look_up_surfaces(field, map);
    if (field->job.active) {
        /* A piece for later gives way to one with nothing to show. */
        int index, need;
        uint64_t wanted;
        bool urgent = pick_chunk(field, step, &index, &wanted, &need) && need == 0 &&
                      find_slot(field, &field->chunks[field->job.chunk], step) != NULL;
        if (urgent || !job_wanted(field)) {
            field->job.active = false;
        }
    }
    uint64_t used = 0;
    while (used < ray_budget) {
        if (!field->job.active) {
            int index, need;
            uint64_t wanted;
            if (!pick_chunk(field, step, &index, &wanted, &need)) {
                break;
            }
            if (!start_job(field, map, index, wanted)) {
                field->rays += used;
                return false;
            }
        }
        used += work_row(field, map);
        if (field->job.row == SHADOW_CHUNK * field->job.work.samples) {
            finish_job(field);
        }
    }
    field->rays += used;
    return true;
}

bool shadow_field_ready(const ShadowField *field, uint64_t tick)
{
    int cx0, cy0, cx1, cy1;
    if (field->map == NULL || !view_chunks(field, &cx0, &cy0, &cx1, &cy1)) {
        return field->map != NULL;
    }
    uint64_t step = tick / SHADOW_STEP_TICKS;
    for (int cy = cy0; cy <= cy1; cy++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            const ShadowSlot *slot =
                find_slot(field, &field->chunks[cy * field->columns + cx], step);
            if (slot == NULL || !slot_current(field, slot)) {
                return false;
            }
        }
    }
    return true;
}

/* The slot a piece shows at a half hour, and how far into it (its end for
 * an older one, its start for a newer one when that half hour is missing):
 * one worked out for the surfaces shown first, else another (another level
 * was shown: right for the columns whose surface is the same, until the
 * others are worked out); NULL when it has none. */
static const ShadowSlot *shown_slot(const ShadowField *field, const ShadowChunk *chunk,
                                    uint64_t step, float *phase)
{
    const ShadowSlot *best = NULL;
    uint64_t best_distance = UINT64_MAX;
    bool best_fits = false;
    for (int k = 0; k < 2; k++) {
        const ShadowSlot *slot = &chunk->slots[k];
        if (slot->step == SHADOW_NO_STEP) {
            continue;
        }
        bool fits = slot_fits(field, chunk, slot);
        uint64_t distance = slot->step > step ? slot->step - step : step - slot->step;
        if (best == NULL || (fits && !best_fits) ||
            (fits == best_fits && distance < best_distance)) {
            best = slot;
            best_distance = distance;
            best_fits = fits;
        }
    }
    if (best != NULL && best->step != step) {
        *phase = best->step < step ? 1.0f : 0.0f;
    }
    return best;
}

/* The value of a point of a slot `phase` (0 .. 1) into its half hour. */
static float slot_value(const ShadowSlot *slot, size_t point, float phase)
{
    int half = phase < 0.5f ? 0 : 1;
    const uint8_t *lit = slot->lit + point * 3;
    int a = lit[half], b = lit[half + 1];
    if (a == b) {
        return (float)a / 255.0f;
    }
    float u = phase * 2.0f - (float)half;
    float moment = (float)slot->moments[point * 2 + (size_t)half] / 255.0f;
    float w = clampf((u - moment) / SHADOW_BLEND + 0.5f, 0.0f, 1.0f);
    w = w * w * (3.0f - 2.0f * w);
    return ((float)a + (float)(b - a) * w) / 255.0f;
}

/* The point of a slot under (x, y), inside the piece at (x0, y0). */
static size_t slot_point(const ShadowSlot *slot, int x0, int y0, float x, float y)
{
    int side = SHADOW_CHUNK * slot->samples;
    int i = clampi((int)((x - (float)x0) * (float)slot->samples), 0, side - 1);
    int j = clampi((int)((y - (float)y0) * (float)slot->samples), 0, side - 1);
    return (size_t)j * (size_t)side + (size_t)i;
}

static float step_phase(uint64_t tick, float tick_fraction)
{
    float phase = ((float)(tick % SHADOW_STEP_TICKS) + clampf(tick_fraction, 0.0f, 1.0f)) /
                  (float)SHADOW_STEP_TICKS;
    return phase < 1.0f ? phase : 1.0f;
}

float shadow_field_lit(const ShadowField *field, uint64_t tick, float tick_fraction, float x,
                       float y)
{
    if (field->chunks == NULL || x < 0.0f || y < 0.0f || x >= (float)field->width ||
        y >= (float)field->height) {
        return 1.0f;
    }
    int cx = (int)x / SHADOW_CHUNK, cy = (int)y / SHADOW_CHUNK;
    float phase = step_phase(tick, tick_fraction);
    const ShadowSlot *slot = shown_slot(field, &field->chunks[cy * field->columns + cx],
                                        tick / SHADOW_STEP_TICKS, &phase);
    if (slot == NULL) {
        return 1.0f;
    }
    return slot_value(slot, slot_point(slot, cx * SHADOW_CHUNK, cy * SHADOW_CHUNK, x, y), phase);
}

void shadow_field_fill(const ShadowField *field, uint64_t tick, float tick_fraction, int x0, int y0,
                       int columns, int rows, int samples, float *out)
{
    const int width = columns * samples;
    const uint64_t step = tick / SHADOW_STEP_TICKS;
    const float inverse = 1.0f / (float)samples;
    for (int j = 0; j < rows * samples; j++) {
        int ty = y0 + j / samples;
        float y = (float)ty + ((float)(j % samples) + 0.5f) * inverse;
        float *row = out + (size_t)j * (size_t)width;
        for (int tile = 0; tile < columns; tile++) {
            int tx = x0 + tile;
            float *point = row + tile * samples;
            const ShadowSlot *slot = NULL;
            float phase = step_phase(tick, tick_fraction);
            if (field->chunks != NULL && tx >= 0 && ty >= 0 && tx < field->width &&
                ty < field->height) {
                int cx = tx / SHADOW_CHUNK, cy = ty / SHADOW_CHUNK;
                slot = shown_slot(field, &field->chunks[cy * field->columns + cx], step, &phase);
            }
            for (int s = 0; s < samples; s++) {
                if (slot == NULL) {
                    point[s] = 1.0f;
                    continue;
                }
                float x = (float)tx + ((float)s + 0.5f) * inverse;
                size_t index = slot_point(slot, tx / SHADOW_CHUNK * SHADOW_CHUNK,
                                          ty / SHADOW_CHUNK * SHADOW_CHUNK, x, y);
                point[s] = slot_value(slot, index, phase);
            }
        }
    }
}

uint8_t shadow_surface_code(const Map *map, int x, int y, int z)
{
    return shadow_surface_code_of(map, x, y, map_view_level(map, x, y, z));
}

uint8_t shadow_surface_code_of(const Map *map, int x, int y, int level)
{
    if (level < 0) {
        return 0;
    }
    size_t index = map_index(map, x, y, level);
    bool block = map_stops_light_at(map, index) || map->terrain[index] == TERRAIN_FOLIAGE;
    return (uint8_t)((block ? level + 1 : level) + 1);
}

bool shadow_surface_height(const Map *map, int x, int y, int z, float *height)
{
    uint8_t code = shadow_surface_code(map, x, y, z);
    if (code == 0) {
        return false;
    }
    *height = (float)(code - 1) * LEVEL_HEIGHT + SUN_RAY_LIFT;
    return true;
}

float shadow_direct_share(float elevation, float clear)
{
    float rise = clampf(elevation / SUN_LOW_ELEVATION, 0.0f, 1.0f);
    rise = rise * rise * (3.0f - 2.0f * rise);
    return (1.0f - SKY_DIFFUSE_CLEAR) * clampf(clear, 0.0f, 1.0f) * rise;
}
