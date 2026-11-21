#include "core/tile_light.h"

#include "core/game_clock.h"
#include "core/sky.h"
#include "core/sun.h"
#include "core/util.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LEVEL_HEIGHT 2.0f
/* The five first points of a tile agree when they differ by no more. */
#define TILE_LIGHT_AGREE 0.05f

static int mini(int a, int b)
{
    return a < b ? a : b;
}

static bool floor_stops_light(uint8_t terrain)
{
    return terrain != TERRAIN_OPEN && !terrain_is_foliage(terrain);
}

bool tile_light_has_surface(const Map *map, size_t index)
{
    return floor_stops_light(map->terrain[index]) && !map_stops_light_at(map, index);
}

static uint8_t to_byte(float value)
{
    return (uint8_t)lroundf(clampf(value, 0.0f, 1.0f) * 255.0f);
}

/* ------------------------------------------------------------------------ */
/* The tops                                                                 */
/* ------------------------------------------------------------------------ */

#define CELL (1 << TILE_LIGHT_CELL_SHIFT)

static size_t cells_of(const TileLight *light)
{
    return (size_t)light->cells_x * (size_t)light->cells_y;
}

/* The lowest level from which nothing in the cell stops the light. */
static int cell_top_of(const Map *map, int cx, int cy)
{
    int x0 = cx * CELL, y0 = cy * CELL;
    int x1 = mini(x0 + CELL, map->width), y1 = mini(y0 + CELL, map->height);
    for (int z = map->depth - 1; z >= 0; z--) {
        for (int y = y0; y < y1; y++) {
            size_t index = map_index(map, x0, y, z);
            for (int x = x0; x < x1; x++, index++) {
                if (map->terrain[index] != TERRAIN_OPEN || map->rock[index] != ROCK_NONE ||
                    structure_stops_light(map->structure[index])) {
                    return z + 1;
                }
            }
        }
    }
    return 0;
}

/* The cells (offsets) a ray going along (east, south) may cross from any
 * point of a cell within `reach` tiles along the ground: those under lines
 * drawn from a grid of points of the cell (its corners, the middles of its
 * sides and its middle), which bound what the cell sweeps. -1 when there are
 * too many. */
static int cells_ahead(float east, float south, int reach, int8_t offsets[][2])
{
    int count = 0;
    offsets[count][0] = 0;
    offsets[count][1] = 0;
    count++;
    float flat = sqrtf(east * east + south * south);
    if (flat < 1.0e-6f) {
        return count; /* Straight up: its own column. */
    }
    float ux = east / flat, uy = south / flat;
    const float points[3] = {0.001f, CELL * 0.5f, CELL - 0.001f};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            for (float t = 0.0f; t <= (float)reach + 0.5f; t += 0.25f) {
                int ox = (int)floorf((points[i] + ux * t) / CELL);
                int oy = (int)floorf((points[j] + uy * t) / CELL);
                bool known = false;
                for (int k = 0; k < count && !known; k++) {
                    known = offsets[k][0] == ox && offsets[k][1] == oy;
                }
                if (!known) {
                    if (count == TILE_LIGHT_AHEAD_MAX) {
                        return -1;
                    }
                    offsets[count][0] = (int8_t)ox;
                    offsets[count][1] = (int8_t)oy;
                    count++;
                }
            }
        }
    }
    return count;
}

/* Per cell of (x0, y0) .. (x1, y1) (inclusive, in cells, clipped), the
 * highest top of the cells ahead (offsets; all levels when unknown). */
static void tops_ahead(TileLight *light, int8_t offsets[][2], int count, uint8_t *tops, int x0,
                       int y0, int x1, int y1)
{
    x0 = clampi(x0, 0, light->cells_x - 1);
    y0 = clampi(y0, 0, light->cells_y - 1);
    x1 = clampi(x1, 0, light->cells_x - 1);
    y1 = clampi(y1, 0, light->cells_y - 1);
    for (int cy = y0; cy <= y1; cy++) {
        for (int cx = x0; cx <= x1; cx++) {
            uint8_t top = count < 0 ? UINT8_MAX : 0;
            for (int k = 0; k < count; k++) {
                int x = cx + offsets[k][0], y = cy + offsets[k][1];
                if (x >= 0 && y >= 0 && x < light->cells_x && y < light->cells_y) {
                    uint8_t cell = light->cell_top[y * light->cells_x + x];
                    top = cell > top ? cell : top;
                }
            }
            tops[cy * light->cells_x + cx] = top;
        }
    }
}

/* The sky's tops ahead of the cells within reach of the cells (x0, y0) ..
 * (x1, y1) (inclusive), whose tops changed. */
static void sky_tops_around(TileLight *light, int x0, int y0, int x1, int y1)
{
    int reach = SKY_REACH / CELL + 1;
    for (int ray = 0; ray < SKY_RAYS; ray++) {
        tops_ahead(light, light->sky_ahead[ray], light->sky_ahead_count[ray],
                   &light->sky_tops[(size_t)ray * cells_of(light)], x0 - reach, y0 - reach,
                   x1 + reach, y1 + reach);
    }
}

/* Brings the tops up to date with the map (all of them when `all`). */
static void refresh_tops(TileLight *light, const Map *map, bool all)
{
    if (!all && light->tops_revision == map->light_revision) {
        return;
    }
    const int per_block = MAP_LIGHT_BLOCK / CELL;
    for (int by = 0; by < light->blocks_y; by++) {
        for (int bx = 0; bx < light->blocks_x; bx++) {
            int tx = bx * MAP_LIGHT_BLOCK, ty = by * MAP_LIGHT_BLOCK;
            if (!all && map_light_stamp(map, tx, ty, tx, ty) <= light->tops_revision) {
                continue;
            }
            int cx0 = bx * per_block, cy0 = by * per_block;
            int cx1 = mini(cx0 + per_block, light->cells_x) - 1;
            int cy1 = mini(cy0 + per_block, light->cells_y) - 1;
            uint8_t block = 0;
            for (int cy = cy0; cy <= cy1; cy++) {
                for (int cx = cx0; cx <= cx1; cx++) {
                    uint8_t top = (uint8_t)cell_top_of(map, cx, cy);
                    light->cell_top[cy * light->cells_x + cx] = top;
                    block = top > block ? top : block;
                }
            }
            light->block_top[by * light->blocks_x + bx] = block;
            if (!all) {
                sky_tops_around(light, cx0, cy0, cx1, cy1);
            }
        }
    }
    if (all) {
        sky_tops_around(light, 0, 0, light->cells_x - 1, light->cells_y - 1);
    }
    light->sun_ready = false;
    light->tops_revision = map->light_revision;
}

/* ------------------------------------------------------------------------ */
/* The light of the sky                                                     */
/* ------------------------------------------------------------------------ */

/* The latest light change within SKY_REACH of a block (`solid`: to what
 * stops the light outright). */
static uint32_t reach_stamp(const TileLight *light, const Map *map, int block, bool solid)
{
    int x0 = (block % light->blocks_x) * MAP_LIGHT_BLOCK - SKY_REACH;
    int y0 = (block / light->blocks_x) * MAP_LIGHT_BLOCK - SKY_REACH;
    int x1 = x0 + MAP_LIGHT_BLOCK - 1 + 2 * SKY_REACH,
        y1 = y0 + MAP_LIGHT_BLOCK - 1 + 2 * SKY_REACH;
    return solid ? map_light_solid_stamp(map, x0, y0, x1, y1)
                 : map_light_stamp(map, x0, y0, x1, y1);
}

static bool block_current(const TileLight *light, const Map *map, int block, bool solid)
{
    uint32_t revision = light->open_revisions[block];
    return revision != 0 && revision - 1 >= reach_stamp(light, map, block, solid);
}

/* The revision a sweep follows: the latest change it looks for. */
static uint32_t sweep_revision(const TileLightSweep *sweep, const Map *map)
{
    return sweep->solid ? map->light_solid_revision : map->light_revision;
}

/* The highest of the sky's tops ahead of a column's cell. */
static int sky_top_over(const TileLight *light, int x, int y)
{
    size_t cell = (size_t)(y >> TILE_LIGHT_CELL_SHIFT) * (size_t)light->cells_x +
                  (size_t)(x >> TILE_LIGHT_CELL_SHIFT);
    int top = 0;
    for (int ray = 0; ray < SKY_RAYS; ray++) {
        int ahead = light->sky_tops[(size_t)ray * cells_of(light) + cell];
        top = ahead > top ? ahead : top;
    }
    return top;
}

/* Works out the block's cells from `*cell` on while the budget lasts; true
 * when the block is done. */
static bool work_block(TileLight *light, const Map *map, int block, size_t *cell, int *budget)
{
    int x0 = (block % light->blocks_x) * MAP_LIGHT_BLOCK;
    int y0 = (block / light->blocks_x) * MAP_LIGHT_BLOCK;
    int columns = mini(MAP_LIGHT_BLOCK, light->width - x0);
    int rows = mini(MAP_LIGHT_BLOCK, light->height - y0);
    LightAhead aheads[SKY_RAYS];
    for (int ray = 0; ray < SKY_RAYS; ray++) {
        aheads[ray] = (LightAhead){&light->sky_tops[(size_t)ray * cells_of(light)], light->cells_x,
                                   TILE_LIGHT_CELL_SHIFT};
    }
    /* Over its own top a block has nothing but open space: no surface. */
    int levels = mini(light->levels, light->block_top[block]);
    size_t per_level = (size_t)columns * (size_t)rows;
    size_t cells = per_level * (size_t)levels;
    while (*cell < cells) {
        if (*budget <= 0) {
            return false;
        }
        int z = (int)(*cell / per_level);
        int k = (int)(*cell % per_level);
        int x = x0 + k % columns, y = y0 + k / columns;
        size_t index = map_index(map, x, y, z);
        if (tile_light_has_surface(map, index) && z >= sky_top_over(light, x, y)) {
            light->open[index] = 255; /* Over everything around: the whole sky. */
            *budget -= 1;
        } else if (tile_light_has_surface(map, index)) {
            light->open[index] =
                to_byte(sky_open_ahead(map, aheads, (float)x + 0.5f, (float)y + 0.5f,
                                       (float)z * LEVEL_HEIGHT + SUN_RAY_LIFT));
            light->rays += SKY_RAYS;
            *budget -= TILE_LIGHT_SURFACE_COST;
        } else {
            light->open[index] = 0;
            *budget -= 1;
        }
        ++*cell;
    }
    for (int z = levels; z < light->levels; z++) {
        for (int y = y0; y < y0 + rows; y++) {
            memset(&light->open[map_index(map, x0, y, z)], 0, (size_t)columns);
        }
    }
    return true;
}

static void next_block(TileLight *light, TileLightSweep *sweep, const Map *map)
{
    sweep->cell = 0;
    if (++sweep->block < light->blocks_x * light->blocks_y) {
        return;
    }
    sweep->block = 0;
    uint32_t revision = sweep_revision(sweep, map);
    if (!sweep->round_worked && sweep->round_started == revision) {
        sweep->clean = revision + 1;
    }
    sweep->round_started = revision;
    sweep->round_worked = false;
}

static bool sweep_clean(const TileLightSweep *sweep, const Map *map)
{
    return sweep->clean == sweep_revision(sweep, map) + 1;
}

/* Works out the blocks out of date for the sweep within its budget. */
static void sweep_open(TileLight *light, TileLightSweep *sweep, const Map *map)
{
    int budget = sweep->budget;
    while (budget > 0) {
        int block = sweep->block;
        if (sweep->cell == 0) {
            if (block_current(light, map, block, sweep->solid)) {
                budget--;
                next_block(light, sweep, map);
                continue;
            }
            sweep->started = map->light_revision;
            sweep->round_worked = true;
        }
        if (!work_block(light, map, block, &sweep->cell, &budget)) {
            return;
        }
        light->open_revisions[block] = sweep->started + 1;
        next_block(light, sweep, map);
    }
}

bool tile_light_open_settled(const TileLight *light, const Map *map)
{
    if (sweep_clean(&light->slow, map)) {
        return true;
    }
    for (int block = 0; block < light->blocks_x * light->blocks_y; block++) {
        if (!block_current(light, map, block, false)) {
            return false;
        }
    }
    return true;
}

bool tile_light_open_firm(const TileLight *light, const Map *map)
{
    if (sweep_clean(&light->fast, map)) {
        return true;
    }
    for (int block = 0; block < light->blocks_x * light->blocks_y; block++) {
        if (!block_current(light, map, block, true)) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* The sun                                                                  */
/* ------------------------------------------------------------------------ */

/* The tops ahead of the blocks towards the sun. */
static void prepare_sun(TileLight *light, SunPosition sun)
{
    if (light->sun_ready && light->sun_east == sun.east && light->sun_south == sun.south) {
        return;
    }
    int8_t offsets[TILE_LIGHT_AHEAD_MAX][2];
    int count = cells_ahead(sun.east, sun.south, SUN_SHADOW_REACH, offsets);
    tops_ahead(light, offsets, count, light->sun_tops, 0, 0, light->cells_x - 1,
               light->cells_y - 1);
    light->sun_east = sun.east;
    light->sun_south = sun.south;
    light->sun_ready = true;
}

float tile_light_sun_lit(TileLight *light, const Map *map, SunPosition sun, float x, float y,
                         float height)
{
    refresh_tops(light, map, false);
    prepare_sun(light, sun);
    LightAhead ahead = {light->sun_tops, light->cells_x, TILE_LIGHT_CELL_SHIFT};
    return sun_lit_ahead(map, &ahead, sun, x, y, height);
}

/* The share of the tile's floor in the sun (prepare_sun() done). */
static float tile_direct(TileLight *light, const Map *map, SunPosition sun, size_t index)
{
    int x, y, z;
    map_tile_of(map, index, &x, &y, &z);
    LightAhead ahead = {light->sun_tops, light->cells_x, TILE_LIGHT_CELL_SHIFT};
    if (z >= ahead.tops[(y >> ahead.shift) * ahead.cells_x + (x >> ahead.shift)]) {
        return 1.0f; /* Nothing towards the sun is as high. */
    }
    float height = (float)z * LEVEL_HEIGHT + SUN_RAY_LIFT;
    const int last = TILE_LIGHT_SAMPLES - 1;
    float lit[TILE_LIGHT_SAMPLES][TILE_LIGHT_SAMPLES];
    const int corners[4][2] = {{0, 0}, {last, 0}, {0, last}, {last, last}};
    float middle = sun_lit_ahead(map, &ahead, sun, (float)x + 0.5f, (float)y + 0.5f, height);
    float low = middle, high = middle, sum = middle;
    for (int c = 0; c < 4; c++) {
        int i = corners[c][0], j = corners[c][1];
        float value =
            sun_lit_ahead(map, &ahead, sun, (float)x + ((float)i + 0.5f) / TILE_LIGHT_SAMPLES,
                          (float)y + ((float)j + 0.5f) / TILE_LIGHT_SAMPLES, height);
        lit[j][i] = value;
        low = value < low ? value : low;
        high = value > high ? value : high;
        sum += value;
    }
    light->rays += 5;
    if (high - low <= TILE_LIGHT_AGREE) {
        return sum / 5.0f;
    }
    sum = 0.0f;
    for (int j = 0; j < TILE_LIGHT_SAMPLES; j++) {
        for (int i = 0; i < TILE_LIGHT_SAMPLES; i++) {
            bool corner = (i == 0 || i == last) && (j == 0 || j == last);
            bool centre = TILE_LIGHT_SAMPLES % 2 == 1 && i == last / 2 && j == last / 2;
            if (centre) {
                lit[j][i] = middle;
            } else if (!corner) {
                lit[j][i] = sun_lit_ahead(
                    map, &ahead, sun, (float)x + ((float)i + 0.5f) / TILE_LIGHT_SAMPLES,
                    (float)y + ((float)j + 0.5f) / TILE_LIGHT_SAMPLES, height);
                light->rays++;
            }
            sum += lit[j][i];
        }
    }
    return sum / (float)(TILE_LIGHT_SAMPLES * TILE_LIGHT_SAMPLES);
}

/* The suns `noon` is made of on `day`: those at the middle of the periods
 * from TILE_LIGHT_NOON_FROM to TILE_LIGHT_NOON_TO, as the day goes. */
#define NOON_SUNS ((TILE_LIGHT_NOON_TO - TILE_LIGHT_NOON_FROM) / TILE_LIGHT_DIRECT_HOURS)

static SunPosition noon_sun(int day, int which)
{
    float hour =
        (float)TILE_LIGHT_NOON_FROM + (float)TILE_LIGHT_DIRECT_HOURS * ((float)which + 0.5f);
    return sun_position(SUN_LATITUDE_DEFAULT, day, hour);
}

void tile_light_noon_on(TileLight *light, const Map *map, int day, const size_t *tiles,
                        size_t count, float *out)
{
    refresh_tops(light, map, false);
    for (size_t i = 0; i < count; i++) {
        out[i] = 0.0f;
    }
    for (int which = 0; which < NOON_SUNS; which++) {
        SunPosition sun = noon_sun(day, which);
        if (!sun_is_up(sun)) {
            continue;
        }
        prepare_sun(light, sun);
        for (size_t i = 0; i < count; i++) {
            size_t index = tiles[i];
            if (index < light->count && light->open[index] > 0 &&
                tile_light_has_surface(map, index)) {
                out[i] = fmaxf(out[i], tile_direct(light, map, sun, index));
            }
        }
    }
}

/* What a period does to `noon`: nothing, starts it again, or adds to it. */
typedef enum NoonPass { NOON_NONE, NOON_FIRST, NOON_MORE } NoonPass;

/* The sun of the tiles [begin, end) for the sun at the middle of a period;
 * `midnight`: the first period of a day (the day's hours roll over first);
 * `hours`: the hours of the period the sun is up, added to the sun hours
 * as far as the tile is in the sun. */
static void work_direct(TileLight *light, const Map *map, SunPosition sun, size_t begin, size_t end,
                        bool midnight, float hours, NoonPass noon)
{
    bool up = sun_is_up(sun);
    if (up) {
        prepare_sun(light, sun);
    }
    int steps = (int)lroundf(hours * TILE_LIGHT_SUN_HOUR_STEPS);
    for (size_t i = begin; i < end; i++) {
        if (midnight) {
            light->sun_yesterday[i] = light->sun_today[i];
            light->sun_today[i] = 0;
        }
        uint8_t direct = 0;
        if (up && light->open[i] > 0 && tile_light_has_surface(map, i)) {
            direct = to_byte(tile_direct(light, map, sun, i));
        }
        light->direct[i] = direct;
        if (noon == NOON_FIRST || (noon == NOON_MORE && direct > light->noon[i])) {
            light->noon[i] = direct;
        }
        if (steps > 0 && direct > 0) {
            int total = light->sun_today[i] + (direct * steps + 127) / 255; /* Rounded. */
            light->sun_today[i] = (uint8_t)mini(total, UINT8_MAX);
        }
    }
}

#define PERIOD_TICKS ((uint64_t)TILE_LIGHT_DIRECT_HOURS * TICKS_PER_HOUR)

static SunPosition sun_of_period(uint64_t period)
{
    return sun_at_tick(period * PERIOD_TICKS + PERIOD_TICKS / 2, SUN_LATITUDE_DEFAULT);
}

/* The hours of a period the sun is up (between game_sunrise() and a day
 * length later). */
static float sun_up_hours(uint64_t period)
{
    uint64_t start = period * PERIOD_TICKS;
    int day = game_day_of_year(start);
    float from = (float)game_hour_from_tick(start);
    float rise = game_sunrise(day), set = rise + game_day_length(day);
    float to = from + (float)TILE_LIGHT_DIRECT_HOURS;
    float up = (to < set ? to : set) - (from > rise ? from : rise);
    return up > 0.0f ? up : 0.0f;
}

/* ------------------------------------------------------------------------ */

/* Works out now `open` where it is out of date (everywhere when `all`),
 * `direct` for the period of `tick` and `noon` for the noon sun. */
static void work_everything(TileLight *light, const Map *map, uint64_t tick, bool all)
{
    refresh_tops(light, map, all);
    for (int block = 0; block < light->blocks_x * light->blocks_y; block++) {
        if (all || !block_current(light, map, block, false)) {
            size_t cell = 0;
            int budget = INT_MAX;
            work_block(light, map, block, &cell, &budget);
            light->open_revisions[block] = map->light_revision + 1;
        }
    }
    light->fast = (TileLightSweep){.solid = true,
                                   .budget = TILE_LIGHT_OPEN_BUDGET,
                                   .round_started = map->light_solid_revision,
                                   .clean = map->light_solid_revision + 1};
    light->slow = (TileLightSweep){.budget = TILE_LIGHT_FOLIAGE_BUDGET,
                                   .round_started = map->light_revision,
                                   .clean = map->light_revision + 1};
    light->first_period = tick / PERIOD_TICKS;
    int day = game_day_of_year(tick);
    for (int which = 0; which < NOON_SUNS; which++) {
        work_direct(light, map, noon_sun(day, which), 0, light->count, false, 0.0f,
                    which == 0 ? NOON_FIRST : NOON_MORE);
    }
    work_direct(light, map, sun_of_period(light->first_period), 0, light->count, false, 0.0f,
                NOON_NONE);
}

bool tile_light_init(TileLight *light, const Map *map, uint64_t tick)
{
    if (!tile_light_init_empty(light, map)) {
        return false;
    }
    light->start_tick = tick;
    work_everything(light, map, tick, true);
    return true;
}

bool tile_light_init_empty(TileLight *light, const Map *map)
{
    memset(light, 0, sizeof(*light));
    light->width = map->width;
    light->height = map->height;
    light->levels = mini(map->top_ground_level + 1, map->depth);
    light->count = map_level_tile_count(map) * (size_t)light->levels;
    light->blocks_x = (map->width + MAP_LIGHT_BLOCK - 1) / MAP_LIGHT_BLOCK;
    light->blocks_y = (map->height + MAP_LIGHT_BLOCK - 1) / MAP_LIGHT_BLOCK;
    size_t blocks = (size_t)light->blocks_x * (size_t)light->blocks_y;
    light->cells_x = (map->width + CELL - 1) / CELL;
    light->cells_y = (map->height + CELL - 1) / CELL;
    size_t cells = cells_of(light);
    light->open = calloc(light->count, 1);
    light->direct = calloc(light->count, 1);
    light->noon = calloc(light->count, 1);
    light->sun_today = calloc(light->count, 1);
    light->sun_yesterday = calloc(light->count, 1);
    light->block_top = calloc(blocks, 1);
    light->cell_top = calloc(cells, 1);
    light->sky_tops = calloc(cells * SKY_RAYS, 1);
    light->sun_tops = calloc(cells, 1);
    light->open_revisions = calloc(blocks, sizeof(*light->open_revisions));
    if (light->open == NULL || light->direct == NULL || light->noon == NULL ||
        light->sun_today == NULL || light->sun_yesterday == NULL || light->block_top == NULL ||
        light->cell_top == NULL || light->sky_tops == NULL || light->sun_tops == NULL ||
        light->open_revisions == NULL) {
        tile_light_free(light);
        return false;
    }
    for (int ray = 0; ray < SKY_RAYS; ray++) {
        float east, south, up;
        sky_ray_direction(ray, &east, &south, &up);
        light->sky_ahead_count[ray] = cells_ahead(east, south, SKY_REACH, light->sky_ahead[ray]);
    }
    return true;
}

void tile_light_refresh(TileLight *light, const Map *map, uint64_t tick)
{
    if (light->count > 0) {
        work_everything(light, map, tick, false);
    }
}

void tile_light_free(TileLight *light)
{
    free(light->open);
    free(light->direct);
    free(light->noon);
    free(light->sun_today);
    free(light->sun_yesterday);
    free(light->block_top);
    free(light->cell_top);
    free(light->sky_tops);
    free(light->sun_tops);
    free(light->open_revisions);
    memset(light, 0, sizeof(*light));
}

void tile_light_tick(TileLight *light, const Map *map, uint64_t tick)
{
    if (light->count == 0) {
        return;
    }
    refresh_tops(light, map, false);
    if (!sweep_clean(&light->fast, map)) {
        sweep_open(light, &light->fast, map);
    } else if (!sweep_clean(&light->slow, map)) {
        sweep_open(light, &light->slow, map);
    }
    uint64_t period = tick / PERIOD_TICKS;
    if (period == light->first_period) {
        return; /* Worked out by tile_light_init(). */
    }
    uint64_t phase = tick % PERIOD_TICKS;
    size_t begin = (size_t)(phase * light->count / PERIOD_TICKS);
    size_t end = (size_t)((phase + 1) * light->count / PERIOD_TICKS);
    int hour = game_hour_from_tick(period * PERIOD_TICKS);
    NoonPass noon = hour == TILE_LIGHT_NOON_FROM                               ? NOON_FIRST
                    : hour > TILE_LIGHT_NOON_FROM && hour < TILE_LIGHT_NOON_TO ? NOON_MORE
                                                                               : NOON_NONE;
    work_direct(light, map, sun_of_period(period), begin, end, hour == 0, sun_up_hours(period),
                noon);
}

/* ------------------------------------------------------------------------ */

static bool stored_index(const TileLight *light, int x, int y, int z, size_t *index)
{
    if (x < 0 || y < 0 || z < 0 || x >= light->width || y >= light->height || z >= light->levels) {
        return false;
    }
    *index = ((size_t)z * (size_t)light->height + (size_t)y) * (size_t)light->width + (size_t)x;
    return true;
}

float tile_light_open(const TileLight *light, int x, int y, int z)
{
    size_t index;
    return stored_index(light, x, y, z, &index) ? (float)light->open[index] / 255.0f : 0.0f;
}

float tile_light_direct(const TileLight *light, int x, int y, int z)
{
    size_t index;
    return stored_index(light, x, y, z, &index) ? (float)light->direct[index] / 255.0f : 0.0f;
}

float tile_light_noon(const TileLight *light, int x, int y, int z)
{
    size_t index;
    return stored_index(light, x, y, z, &index) ? (float)light->noon[index] / 255.0f : 0.0f;
}

float tile_light_sun_hours(const TileLight *light, int x, int y, int z, bool yesterday)
{
    size_t index;
    if (!stored_index(light, x, y, z, &index)) {
        return 0.0f;
    }
    uint8_t steps = yesterday ? light->sun_yesterday[index] : light->sun_today[index];
    return (float)steps / (float)TILE_LIGHT_SUN_HOUR_STEPS;
}

bool tile_light_knows_yesterday(const TileLight *light, uint64_t tick)
{
    /* The day after the first one, once each tile rolled over (in the first period). */
    uint64_t next_day = (light->start_tick / TICKS_PER_DAY + 1) * TICKS_PER_DAY;
    return tick >= next_day + PERIOD_TICKS;
}

static void sweep_serialize(Serializer *s, TileLightSweep *sweep, int blocks, size_t count)
{
    ser_bool(s, &sweep->solid);
    ser_int(s, &sweep->budget);
    ser_int_in(s, &sweep->block, 0, blocks);
    ser_size(s, &sweep->cell);
    ser_check(s, sweep->cell <= count); /* A tile of the block, on some level. */
    ser_u32(s, &sweep->started);
    ser_u32(s, &sweep->round_started);
    ser_bool(s, &sweep->round_worked);
    ser_u32(s, &sweep->clean);
}

void tile_light_serialize(Serializer *s, TileLight *light)
{
    size_t count = light->count;
    ser_size(s, &count);
    if (!ser_check(s, count == light->count)) {
        return;
    }
    int blocks = light->blocks_x * light->blocks_y;
    size_t cells = cells_of(light);
    ser_layer_u8(s, light->open, count);
    ser_layer_u8(s, light->direct, count);
    ser_layer_u8(s, light->noon, count);
    ser_layer_u8(s, light->sun_today, count);
    ser_layer_u8(s, light->sun_yesterday, count);
    ser_layer_u8(s, light->block_top, (size_t)blocks);
    ser_layer_u8(s, light->cell_top, cells);
    ser_layer_u8(s, light->sky_tops, cells * SKY_RAYS);
    ser_layer_u8(s, light->sun_tops, cells);
    ser_float(s, &light->sun_east);
    ser_float(s, &light->sun_south);
    ser_bool(s, &light->sun_ready);
    ser_u32(s, &light->tops_revision);
    ser_u32s(s, light->open_revisions, (size_t)blocks);
    sweep_serialize(s, &light->fast, blocks, count);
    sweep_serialize(s, &light->slow, blocks, count);
    ser_u64(s, &light->first_period);
    ser_u64(s, &light->start_tick);
    ser_u64(s, &light->rays);
}
