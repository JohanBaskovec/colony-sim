#include "core/temperature.h"

#include "core/i18n.h"

#include "core/game_clock.h"
#include "core/structure.h"
#include "core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265f

/* In-game hours between two passes over a tile. */
#define SWEEP_HOURS ((float)TEMPERATURE_SWEEP_TICKS / (float)TICKS_PER_HOUR)

static const float CONDUCTANCES[TEMPERATURE_MATERIAL_COUNT] = {
    [TEMPERATURE_MATERIAL_AIR] = TEMPERATURE_CONDUCTANCE_AIR,
    [TEMPERATURE_MATERIAL_WALL] = TEMPERATURE_CONDUCTANCE_WALL,
    [TEMPERATURE_MATERIAL_DOOR] = TEMPERATURE_CONDUCTANCE_DOOR,
    [TEMPERATURE_MATERIAL_ROCK] = TEMPERATURE_CONDUCTANCE_ROCK,
    [TEMPERATURE_MATERIAL_OPEN] = TEMPERATURE_CONDUCTANCE_AIR,
};

static const float EXPOSURES[TEMPERATURE_COVER_COUNT] = {
    [TEMPERATURE_COVER_OPEN] = TEMPERATURE_EXPOSURE_OPEN,
    [TEMPERATURE_COVER_ROOFED] = TEMPERATURE_EXPOSURE_ROOFED,
    [TEMPERATURE_COVER_NATURAL] = 0.0f,
};

static const float GROUND_RATES[TEMPERATURE_COVER_COUNT] = {
    [TEMPERATURE_COVER_NATURAL] = TEMPERATURE_GROUND_RATE_NATURAL_ROOF,
};

/* What a tile is, as the cache stores it. */
static uint8_t tile_kind(const Map *map, int x, int y, int z)
{
    TemperatureMaterial material = temperature_material(map, x, y, z);
    TemperatureCover cover = material == TEMPERATURE_MATERIAL_ROCK
                                 ? TEMPERATURE_COVER_NATURAL
                                 : temperature_cover(map, x, y, z);
    return (uint8_t)(material * TEMPERATURE_COVER_COUNT + cover);
}

static float kind_conductance(uint8_t kind)
{
    return CONDUCTANCES[kind / TEMPERATURE_COVER_COUNT];
}

static int kind_material(uint8_t kind)
{
    return kind / TEMPERATURE_COVER_COUNT;
}

/* Whether the sweep works out the temperature of a tile of this kind (rock
 * keeps the ground's, open space under the sky is the outdoor air). */
static bool kind_active(uint8_t kind)
{
    int material = kind_material(kind);
    return material != TEMPERATURE_MATERIAL_ROCK && material != TEMPERATURE_MATERIAL_OPEN;
}

/* Refreshes the caches over tiles x0..x1, y0..y1 of level z (clamped to the
 * map). */
static void refresh_area(Temperatures *temperatures, const Map *map, int x0, int y0, int x1, int y1,
                         int z)
{
    int width = temperatures->width, height = temperatures->height;
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 >= width ? width - 1 : x1;
    y1 = y1 >= height ? height - 1 : y1;
    uint8_t *kind = temperatures->kind;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            size_t i = map_index(map, x, y, z);
            kind[i] = tile_kind(map, x, y, z);
            temperatures->water[i] = map_is_water(map, x, y, z);
            if (kind_material(kind[i]) == TEMPERATURE_MATERIAL_ROCK) {
                temperatures->celsius[i] = TEMPERATURE_GROUND;
            }
            uint64_t bit = (uint64_t)1 << (i % 64);
            if (kind_active(kind[i])) {
                temperatures->active[i / 64] |= bit;
            } else {
                temperatures->active[i / 64] &= ~bit;
            }
        }
    }
    /* The edges of those tiles, those with their west and north neighbors
     * included: the rate between two tiles times the pass. Only between
     * tiles the sweep works out: open space is the outdoor air and rock the
     * ground's (see the pulls below). */
    for (int y = y0 > 0 ? y0 - 1 : 0; y <= y1; y++) {
        for (int x = x0 > 0 ? x0 - 1 : 0; x <= x1; x++) {
            size_t i = map_index(map, x, y, z);
            bool active = kind_active(kind[i]);
            float here = kind_conductance(kind[i]);
            float east = x + 1 < width && active && kind_active(kind[i + 1])
                             ? kind_conductance(kind[i + 1])
                             : 0.0f;
            size_t below = i + (size_t)width;
            float south = y + 1 < height && active && kind_active(kind[below])
                              ? kind_conductance(kind[below])
                              : 0.0f;
            temperatures->edge_east[i] = fminf(here, east) * SWEEP_HOURS;
            temperatures->edge_south[i] = fminf(here, south) * SWEEP_HOURS;
        }
    }
    /* The pulls of the outdoor air (what covers the tile, and its sides open
     * to open space) and of the ground (under a natural roof, and its sides
     * against rock); those of the tiles around too. */
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    for (int y = y0 > 0 ? y0 - 1 : 0; y <= y1 + 1 && y < height; y++) {
        for (int x = x0 > 0 ? x0 - 1 : 0; x <= x1 + 1 && x < width; x++) {
            size_t i = map_index(map, x, y, z);
            float exposure = 0.0f, ground = 0.0f;
            if (kind_active(kind[i])) {
                float here = kind_conductance(kind[i]);
                exposure = EXPOSURES[kind[i] % TEMPERATURE_COVER_COUNT];
                ground = GROUND_RATES[kind[i] % TEMPERATURE_COVER_COUNT];
                for (int d = 0; d < 4; d++) {
                    int nx = x + DX[d], ny = y + DY[d];
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                        continue;
                    }
                    uint8_t next = kind[map_index(map, nx, ny, z)];
                    if (kind_material(next) == TEMPERATURE_MATERIAL_OPEN) {
                        exposure += fminf(here, TEMPERATURE_CONDUCTANCE_AIR);
                    } else if (kind_material(next) == TEMPERATURE_MATERIAL_ROCK) {
                        ground += fminf(here, kind_conductance(next));
                    }
                }
            }
            float rate = exposure + ground;
            temperatures->pull[i] = fminf(rate * SWEEP_HOURS, 1.0f); /* Never past it. */
            temperatures->ground_share[i] = rate > 0.0f ? ground / rate : 0.0f;
        }
    }
}

/* Refreshes the caches after a change on a tile: the tile, and the tiles
 * below it, whose natural roof it may make or take away. */
static void refresh_column(Temperatures *temperatures, const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z)) {
        return;
    }
    for (int level = 0; level <= z; level++) {
        refresh_area(temperatures, map, x, y, x, y, level);
    }
}

/* Refreshes the caches over the whole map. */
static void refresh_all(Temperatures *temperatures, const Map *map)
{
    for (int z = 0; z < map->depth; z++) {
        refresh_area(temperatures, map, 0, 0, map->width - 1, map->height - 1, z);
    }
}

bool temperatures_init(Temperatures *temperatures, const Map *map, float outdoor)
{
    if (!temperatures_init_empty(temperatures, map)) {
        return false;
    }
    size_t count = map_tile_count(map);
    temperatures->outdoor = outdoor;
    for (size_t i = 0; i < count; i++) {
        temperatures->celsius[i] = outdoor;
        temperatures->soil[i] = outdoor;
    }
    refresh_all(temperatures, map);
    temperatures->topology_revision = map->topology_revision;
    temperatures->build_revision = map->build_revision;
    return true;
}

bool temperatures_init_empty(Temperatures *temperatures, const Map *map)
{
    memset(temperatures, 0, sizeof(*temperatures));
    size_t count = map_tile_count(map);
    if (count == 0) {
        return false;
    }
    size_t width = (size_t)map->width;
    /* Padding: tiles on the edges read past them, at rates of 0. */
    temperatures->buffers[0] = calloc(count + 2 * width + 2, sizeof(float));
    temperatures->buffers[1] = calloc(count + 1, sizeof(float));
    temperatures->buffers[2] = calloc(count + width, sizeof(float));
    temperatures->kind = calloc(count, sizeof(*temperatures->kind));
    temperatures->pull = calloc(count, sizeof(*temperatures->pull));
    temperatures->ground_share = calloc(count, sizeof(*temperatures->ground_share));
    temperatures->soil = calloc(count, sizeof(*temperatures->soil));
    temperatures->water = calloc(count, sizeof(*temperatures->water));
    temperatures->active = calloc((count + 63) / 64, sizeof(*temperatures->active));
    if (temperatures->buffers[0] == NULL || temperatures->buffers[1] == NULL ||
        temperatures->buffers[2] == NULL || temperatures->kind == NULL ||
        temperatures->pull == NULL || temperatures->ground_share == NULL ||
        temperatures->soil == NULL || temperatures->water == NULL || temperatures->active == NULL) {
        temperatures_free(temperatures);
        return false;
    }
    temperatures->celsius = temperatures->buffers[0] + width + 1;
    temperatures->edge_east = temperatures->buffers[1] + 1;
    temperatures->edge_south = temperatures->buffers[2] + width;
    temperatures->width = map->width;
    temperatures->height = map->height;
    temperatures->depth = map->depth;
    return true;
}

void temperatures_free(Temperatures *temperatures)
{
    for (int i = 0; i < 3; i++) {
        free(temperatures->buffers[i]);
    }
    free(temperatures->kind);
    free(temperatures->pull);
    free(temperatures->ground_share);
    free(temperatures->soil);
    free(temperatures->water);
    free(temperatures->active);
    memset(temperatures, 0, sizeof(*temperatures));
}

float temperature_season_mean(uint64_t tick)
{
    static const float MEANS[SEASON_COUNT] = TEMPERATURE_SEASON_MEANS;
    /* Seasons: from the middle of one to the middle of the next. */
    float year = (float)(tick % TICKS_PER_YEAR) / (float)TICKS_PER_YEAR;
    float position = year * (float)SEASON_COUNT - 0.5f;
    float first = floorf(position);
    int from = ((int)first % SEASON_COUNT + SEASON_COUNT) % SEASON_COUNT;
    int to = (from + 1) % SEASON_COUNT;
    float blend = (1.0f - cosf(PI_F * (position - first))) * 0.5f;
    return lerpf(MEANS[from], MEANS[to], blend);
}

float temperature_daily_swing(uint64_t tick)
{
    /* Warming up from the coldest hour to the warmest, cooling down for the
     * rest of the day. */
    float hour = (float)(tick % TICKS_PER_DAY) / (float)TICKS_PER_HOUR;
    const float coldest = (float)TEMPERATURE_COLDEST_HOUR;
    const float warmest = (float)TEMPERATURE_WARMEST_HOUR;
    float swing;
    if (hour >= coldest && hour < warmest) {
        swing = -cosf(PI_F * (hour - coldest) / (warmest - coldest));
    } else {
        float since = fmodf(hour - warmest + (float)HOURS_PER_DAY, (float)HOURS_PER_DAY);
        swing = cosf(PI_F * since / ((float)HOURS_PER_DAY - (warmest - coldest)));
    }
    return swing;
}

float temperature_outdoor(uint64_t tick)
{
    return temperature_season_mean(tick) + TEMPERATURE_DAILY_SWING * temperature_daily_swing(tick);
}

static bool in_bounds(const Temperatures *temperatures, int x, int y, int z)
{
    return temperatures->celsius != NULL && x >= 0 && y >= 0 && z >= 0 && x < temperatures->width &&
           y < temperatures->height && z < temperatures->depth;
}

static size_t index_of(const Temperatures *temperatures, int x, int y, int z)
{
    return ((size_t)z * (size_t)temperatures->height + (size_t)y) * (size_t)temperatures->width +
           (size_t)x;
}

TemperatureFeel temperature_feel(const TemperatureRanges *ranges, float celsius)
{
    if (celsius < ranges->bearable_low) {
        return TEMPERATURE_FREEZING;
    }
    if (celsius > ranges->bearable_high) {
        return TEMPERATURE_SCORCHING;
    }
    if (celsius < ranges->pleasant_low) {
        return TEMPERATURE_COLD;
    }
    return celsius > ranges->pleasant_high ? TEMPERATURE_HOT : TEMPERATURE_PLEASANT;
}

const char *temperature_feel_key(TemperatureFeel feel)
{
    static const char *const KEYS[TEMPERATURE_FEEL_COUNT] = {
        [TEMPERATURE_FREEZING] = N_("temperature.feel.freezing"),
        [TEMPERATURE_COLD] = N_("temperature.feel.cold"),
        [TEMPERATURE_PLEASANT] = N_("temperature.feel.pleasant"),
        [TEMPERATURE_HOT] = N_("temperature.feel.hot"),
        [TEMPERATURE_SCORCHING] = N_("temperature.feel.scorching"),
    };
    return (unsigned)feel < TEMPERATURE_FEEL_COUNT ? KEYS[feel] : KEYS[TEMPERATURE_PLEASANT];
}

float temperature_at(const Temperatures *temperatures, int x, int y, int z)
{
    if (!in_bounds(temperatures, x, y, z)) {
        return TEMPERATURE_GROUND;
    }
    size_t i = index_of(temperatures, x, y, z);
    return kind_material(temperatures->kind[i]) == TEMPERATURE_MATERIAL_OPEN
               ? temperatures->outdoor
               : temperatures->celsius[i];
}

void temperatures_add_heat(Temperatures *temperatures, int x, int y, int z, float degrees)
{
    if (in_bounds(temperatures, x, y, z)) {
        temperatures->celsius[index_of(temperatures, x, y, z)] += degrees;
    }
}

void temperatures_heat_source(Temperatures *temperatures, int x, int y, int z, float degrees)
{
    if (!in_bounds(temperatures, x, y, z)) {
        return;
    }
    uint32_t index = (uint32_t)index_of(temperatures, x, y, z);
    for (int s = 0; s < temperatures->source_count; s++) {
        if (temperatures->sources[s].index == index) {
            temperatures->sources[s].degrees += degrees;
            return;
        }
    }
    if (temperatures->source_count < TEMPERATURE_SOURCES_MAX) {
        temperatures->sources[temperatures->source_count++] = (TemperatureSource){index, degrees};
    } else {
        temperatures->celsius[index] += degrees;
    }
}

/* The heat that came into tiles first .. last - 1 since their last pass (a
 * band): it came in all along, so half of it is added before the pass (and
 * takes part in it) and the rest after (`after`), then forgotten. */
static void release_sources(Temperatures *temperatures, size_t first, size_t last, bool after)
{
    int kept = 0;
    for (int s = 0; s < temperatures->source_count; s++) {
        TemperatureSource source = temperatures->sources[s];
        if (source.index >= first && source.index < last) {
            float share = after ? source.degrees : 0.5f * source.degrees;
            temperatures->celsius[source.index] += share;
            source.degrees -= share;
            if (after) {
                continue;
            }
        }
        temperatures->sources[kept++] = source;
    }
    temperatures->source_count = kept;
}

TemperatureMaterial temperature_material(const Map *map, int x, int y, int z)
{
    if (!map_in_bounds(map, x, y, z) || map->rock[map_index(map, x, y, z)] != ROCK_NONE) {
        return TEMPERATURE_MATERIAL_ROCK;
    }
    /* Open space under the sky, and the roofs out in it, are outdoors. */
    if ((map_is_open(map, x, y, z) || map_roof(map, x, y, z) != STRUCTURE_NONE) &&
        !map_is_roofed(map, x, y, z)) {
        return TEMPERATURE_MATERIAL_OPEN;
    }
    StructureType structure = map_structure(map, x, y, z);
    if (structure == STRUCTURE_DOOR) { /* Kept open, it is an opening. */
        return map_door_kept_open_at(map, map_index(map, x, y, z)) ? TEMPERATURE_MATERIAL_AIR
                                                                   : TEMPERATURE_MATERIAL_DOOR;
    }
    return structure_blocks(structure) ? TEMPERATURE_MATERIAL_WALL : TEMPERATURE_MATERIAL_AIR;
}

TemperatureCover temperature_cover(const Map *map, int x, int y, int z)
{
    int cover = map_cover_level(map, x, y, z); /* The first floor above decides. */
    if (cover < 0) {
        return TEMPERATURE_COVER_OPEN;
    }
    return map_roof(map, x, y, cover) != STRUCTURE_NONE ? TEMPERATURE_COVER_ROOFED
                                                        : TEMPERATURE_COVER_NATURAL;
}

float temperature_rot_rate(const Temperatures *temperatures, const Map *map, int x, int y, int z,
                           bool rains)
{
    if (!map_in_bounds(map, x, y, z)) {
        return 0.0f;
    }
    float celsius =
        temperatures != NULL ? temperature_at(temperatures, x, y, z) : ITEM_ROT_REFERENCE_CELSIUS;
    bool sheltered = temperature_cover(map, x, y, z) != TEMPERATURE_COVER_OPEN;
    float rate = item_rot_rate(celsius, sheltered);
    return rains && !sheltered ? rate * ITEM_ROT_RAINED_ON : rate;
}

float temperature_for_plant(const Temperatures *temperatures, const Map *map, int x, int y, int z)
{
    if (map_in_bounds(map, x, y, z) && map->rock[map_index(map, x, y, z)] == ROCK_WOOD) {
        return temperatures->outdoor;
    }
    return temperature_at(temperatures, x, y, z);
}

/* Brings the caches up to date with the changes the map logged since. */
static void update_caches(Temperatures *temperatures, const Map *map)
{
    uint32_t topology = map->topology_revision - temperatures->topology_revision;
    uint32_t build = map->build_revision - temperatures->build_revision;
    if (topology == 0 && build == 0) {
        return;
    }
    bool everything = topology > MAP_TOPOLOGY_LOG_SIZE || build > MAP_BUILD_LOG_SIZE;
    for (uint32_t r = 1; r <= topology && !everything; r++) {
        uint32_t revision = temperatures->topology_revision + r;
        everything = map->topology_log[revision % MAP_TOPOLOGY_LOG_SIZE].anywhere;
    }
    if (everything) {
        refresh_all(temperatures, map);
    } else {
        for (uint32_t r = 1; r <= topology; r++) {
            uint32_t revision = temperatures->topology_revision + r;
            const MapTopologyChange *change = &map->topology_log[revision % MAP_TOPOLOGY_LOG_SIZE];
            refresh_column(temperatures, map, change->x, change->y, change->z);
        }
        for (uint32_t r = 1; r <= build; r++) {
            uint32_t revision = temperatures->build_revision + r;
            const MapBuildChange *change = &map->build_log[revision % MAP_BUILD_LOG_SIZE];
            refresh_column(temperatures, map, change->x, change->y, change->z);
        }
    }
    temperatures->topology_revision = map->topology_revision;
    temperatures->build_revision = map->build_revision;
}

void temperatures_settle_ground(Temperatures *temperatures, float celsius)
{
    size_t count =
        (size_t)temperatures->width * (size_t)temperatures->height * (size_t)temperatures->depth;
    for (size_t i = 0; temperatures->soil != NULL && i < count; i++) {
        temperatures->soil[i] = celsius;
    }
}

void temperatures_set_surface(Temperatures *temperatures, const TemperatureSurface *surface)
{
    temperatures->surface = surface != NULL ? *surface : (TemperatureSurface){0};
}

/* What the surface does this tick, worked out once for the pass
 * (docs/CLIMATE.md §5): a tile under the sky with crowns letting `c` of the
 * light through (0 .. 255), `s` of the sun of open ground reaching it (its
 * own: shade, docs/SHADOWS.md §5.2) and `e` of its sun gone into
 * evaporation is pulled towards `base + sun_part × s - night_part × c -
 * sun_evap × s × e`, at `still_base + still_canopy × c` of its pull, and
 * exchanges heat with the ground under it at `store[water]`, the ground
 * following at `follow[water]`. */
typedef struct SurfacePass {
    const uint8_t *canopy;
    const uint8_t *evap;
    const uint8_t *sun;
    float base;
    float sun_part;
    float night_part;
    float sun_evap;
    float still_base;
    float still_canopy;
    float store[2];
    float follow[2];
} SurfacePass;

static SurfacePass surface_pass(const TemperatureSurface *surface, float outdoor)
{
    const float meadow = 1.0f - TEMPERATURE_EVAP_COOLING; /* Its sun mostly evaporates. */
    const float scale = 1.0f / 255.0f;
    return (SurfacePass){
        .canopy = surface->canopy,
        .evap = surface->evap,
        .sun = surface->sun != NULL ? surface->sun : surface->canopy,
        .base = outdoor - surface->sun_heat * meadow + surface->night_cooling,
        .sun_part = surface->sun_heat * scale,
        .night_part = surface->night_cooling * scale,
        .sun_evap = surface->sun_heat * TEMPERATURE_EVAP_COOLING * scale * scale,
        .still_base = TEMPERATURE_CANOPY_STILL,
        .still_canopy = (1.0f - TEMPERATURE_CANOPY_STILL) * scale,
        .store = {TEMPERATURE_SOIL_RATE * SWEEP_HOURS, TEMPERATURE_WATER_RATE * SWEEP_HOURS},
        .follow = {TEMPERATURE_SOIL_FOLLOW * SWEEP_HOURS, TEMPERATURE_WATER_FOLLOW * SWEEP_HOURS},
    };
}

/* Pulls a tile under the sky towards what its surface makes of the outdoor
 * air, more slowly under crowns (the air's share of the pull; that of rock
 * beside it is not slowed), then exchanges heat with the ground under it.
 * `here` is the tile after its edges; returns its new temperature. */
static float surface_pull(const SurfacePass *pass, Temperatures *temperatures, size_t i, float here,
                          float pull, float ground)
{
    float canopy = (float)pass->canopy[i];
    float sun = (float)pass->sun[i];
    float air = pass->base + sun * (pass->sun_part - pass->sun_evap * (float)pass->evap[i]) -
                canopy * pass->night_part;
    float still = pass->still_base + pass->still_canopy * canopy;
    if (ground == 0.0f) {
        here += pull * still * (air - here);
    } else {
        float air_share = (1.0f - ground) * still;
        float reservoir = (air_share * air + ground * TEMPERATURE_GROUND) / (air_share + ground);
        here += pull * (air_share + ground) * (reservoir - here);
    }
    int water = temperatures->water[i];
    float *soil = &temperatures->soil[i];
    here += pass->store[water] * (*soil - here);
    *soil += pass->follow[water] * (here - *soil);
    return here;
}

void temperatures_tick(Temperatures *temperatures, const Map *map, uint64_t tick, float outdoor)
{
    if (temperatures->celsius == NULL || temperatures->width != map->width ||
        temperatures->height != map->height || temperatures->depth != map->depth) {
        return;
    }
    update_caches(temperatures, map);
    temperatures->outdoor = outdoor;
    int band = (int)(tick % TEMPERATURE_SWEEP_TICKS);
    /* Rows of every level, one after the other. */
    int width = temperatures->width, rows = temperatures->height * temperatures->depth;
    int y0 = rows * band / TEMPERATURE_SWEEP_TICKS;
    int y1 = rows * (band + 1) / TEMPERATURE_SWEEP_TICKS;
    float *celsius = temperatures->celsius;
    const float *east = temperatures->edge_east, *south = temperatures->edge_south;
    const float *pull = temperatures->pull, *ground_share = temperatures->ground_share;
    const uint8_t *kind = temperatures->kind;
    const uint8_t *open_sky =
        temperatures->surface.canopy != NULL && temperatures->surface.evap != NULL
            ? temperatures->surface.canopy
            : NULL;
    SurfacePass pass = surface_pass(&temperatures->surface, outdoor);
    const uint64_t *active = temperatures->active;
    size_t row = (size_t)width;
    /* The active tiles of the band, in order, 64 at a time. */
    size_t first = (size_t)y0 * row, last = (size_t)y1 * row;
    if (temperatures->source_count > 0) {
        release_sources(temperatures, first, last, false);
    }
    for (size_t word = first / 64; word * 64 < last; word++) {
        uint64_t bits = active[word];
        if (word * 64 < first) {
            bits &= ~(uint64_t)0 << (first % 64);
        }
        if ((word + 1) * 64 > last) {
            bits &= ~(~(uint64_t)0 << (last % 64));
        }
        while (bits != 0) {
            size_t i = word * 64 + (size_t)lowest_bit64(bits);
            bits &= bits - 1;
            /* Its edges with its east and south neighbors (0 past the map,
             * reading the padding; the west and north edges were those of the
             * tiles before it): what one gains the other loses, so the heat is
             * kept, and a pass never closes more than half their gap, so
             * nothing overshoots. Then the pull of the outdoor air and the
             * ground. */
            float here = celsius[i];
            float flow = east[i] * (celsius[i + 1] - here);
            here += flow;
            celsius[i + 1] -= flow;
            flow = south[i] * (celsius[i + row] - here);
            here += flow;
            celsius[i + row] -= flow;
            if (open_sky != NULL && kind[i] % TEMPERATURE_COVER_COUNT == TEMPERATURE_COVER_OPEN) {
                celsius[i] = surface_pull(&pass, temperatures, i, here, pull[i], ground_share[i]);
                continue;
            }
            float reservoir = outdoor + ground_share[i] * (TEMPERATURE_GROUND - outdoor);
            celsius[i] = here + pull[i] * (reservoir - here);
        }
    }
    if (temperatures->source_count > 0) {
        release_sources(temperatures, first, last, true);
    }
}

uint64_t temperatures_hash(const Temperatures *temperatures, uint64_t hash)
{
    if (temperatures->celsius != NULL) {
        hash = hash_fnv1a64(hash, temperatures->celsius,
                            (size_t)temperatures->width * (size_t)temperatures->height *
                                (size_t)temperatures->depth * sizeof(*temperatures->celsius));
        hash = hash_fnv1a64(hash, temperatures->soil,
                            (size_t)temperatures->width * (size_t)temperatures->height *
                                (size_t)temperatures->depth * sizeof(*temperatures->soil));
        hash = hash_fnv1a64(hash, temperatures->sources,
                            (size_t)temperatures->source_count * sizeof(*temperatures->sources));
    }
    return hash;
}

void temperatures_serialize(Serializer *s, Temperatures *temperatures)
{
    Temperatures *t = temperatures;
    size_t count = (size_t)t->width * (size_t)t->height * (size_t)t->depth;
    ser_layer_float(s, t->celsius, count);
    ser_layer_u8(s, t->kind, count);
    ser_layer_float(s, t->edge_east, count);
    ser_layer_float(s, t->edge_south, count);
    ser_layer_float(s, t->pull, count);
    ser_layer_float(s, t->ground_share, count);
    ser_layer_u64(s, t->active, (count + 63) / 64);
    ser_layer_float(s, t->soil, count);
    ser_layer_u8(s, t->water, count);
    ser_int_in(s, &t->source_count, 0, TEMPERATURE_SOURCES_MAX);
    for (int i = 0; i < t->source_count; i++) {
        ser_u32_upto(s, &t->sources[i].index, (uint32_t)count - 1);
        ser_float(s, &t->sources[i].degrees);
    }
    ser_float(s, &t->outdoor);
    ser_u32(s, &t->topology_revision);
    ser_u32(s, &t->build_revision);
}
