/*
 * Temperature: the outdoor air through the day and the year, and a layer of
 * temperatures, one per tile, in degrees Celsius.
 *
 * The outdoor temperature follows the seasons (TEMPERATURE_SEASON_MEANS at
 * the middle of each, smoothly in between) and the day (coldest at
 * TEMPERATURE_COLDEST_HOUR, warmest at TEMPERATURE_WARMEST_HOUR, a swing of
 * TEMPERATURE_DAILY_SWING either way). Weather will add to it.
 *
 * Each tile exchanges heat with its four neighbors, at the rate of the
 * worse conductor of the two (air passes heat well, a wall poorly, a door
 * in between), and with the outdoor air at a rate set by what covers it:
 * open sky (fast), a built roof (slow), a natural roof (none: it slowly
 * takes the temperature of the ground instead). Rock stays at the
 * temperature of the ground, TEMPERATURE_GROUND; open space under the sky
 * (the air over lower ground, see core/map.h) is the outdoor air. Burning hearths add heat
 * (temperatures_add_heat()). So a walled and roofed hut keeps the warmth of
 * its fire, a walled yard open to the sky does not, and a cave stays mild
 * all year.
 *
 * Rates are per hour; the layer is worked through in bands of rows, a full
 * pass every TEMPERATURE_SWEEP_TICKS, each tile updated in place. What each
 * tile is made of and covered by is a cache, updated around the tiles the
 * map logged as changed (Map.build_log, Map.topology_log), or rebuilt when
 * too much changed at once.
 */
#ifndef COLONY_CORE_TEMPERATURE_H
#define COLONY_CORE_TEMPERATURE_H

#include "core/serialize.h"

#include "core/map.h"

#include <stdbool.h>
#include <stdint.h>

/* Outdoor temperature in the middle of spring, summer, autumn and winter
 * (tuning). */
#define TEMPERATURE_SEASON_MEANS                                                                   \
    {                                                                                              \
        12.0f, 22.0f, 10.0f, -2.0f                                                                 \
    }
#define TEMPERATURE_DAILY_SWING 5.0f
#define TEMPERATURE_COLDEST_HOUR 5
#define TEMPERATURE_WARMEST_HOUR 15
/* Rock, and what is deep under a natural roof. */
#define TEMPERATURE_GROUND 10.0f

/* Ticks for a full pass over the layer (4.8 in-game minutes). A pass works a
 * tile out edge by edge with its neighbors (what one gains the other loses:
 * the heat is kept), then pulls it towards the outdoor air and the ground. It
 * stays stable while one pass closes at most half the gap between two tiles
 * (TEMPERATURE_CONDUCTANCE_AIR times the pass: 0.32 an edge) and the pull is
 * capped at the gap (a tile at a cliff's edge, open on several sides); the
 * steady states are those of the heat equation. */
#define TEMPERATURE_SWEEP_TICKS 200

/* Heat exchange rates, per hour (tuning): between neighbors (the lower of
 * the two conductances), with the outdoor air, with the ground. */
#define TEMPERATURE_CONDUCTANCE_AIR 4.0f
#define TEMPERATURE_CONDUCTANCE_WALL 0.5f
#define TEMPERATURE_CONDUCTANCE_DOOR 1.5f
#define TEMPERATURE_CONDUCTANCE_ROCK 0.5f
#define TEMPERATURE_EXPOSURE_OPEN 6.0f
#define TEMPERATURE_EXPOSURE_ROOFED 0.3f
#define TEMPERATURE_GROUND_RATE_NATURAL_ROOF 0.3f

/* Near the ground (docs/CLIMATE.md §5). The outdoor air is that of a
 * well-watered meadow (where the weather is measured); other surfaces differ
 * from it. In the sun a tile warms the air over it by TEMPERATURE_SUN_HEAT
 * (full sun) times the sun it gets (the light its crowns let through) less
 * what goes into evaporation (TEMPERATURE_EVAP_COOLING of it), counted from
 * what a meadow gets: dry bare ground is warmer, the shade of a wood cooler.
 * On a clear night the open ground cools by TEMPERATURE_NIGHT_COOLING (already
 * in the outdoor air) where crowns hide the sky, a wood is milder by that
 * much. Under crowns the air mixes more slowly with the open air
 * (TEMPERATURE_CANOPY_STILL of the pull under crowns letting no light through).
 * Each tile under the sky exchanges heat with the ground under it at
 * TEMPERATURE_SOIL_RATE an hour, the ground following it at
 * TEMPERATURE_SOIL_FOLLOW an hour (a day or so behind); water stores far more
 * heat (TEMPERATURE_WATER_RATE, TEMPERATURE_WATER_FOLLOW). */
#define TEMPERATURE_SUN_HEAT 5.0f
#define TEMPERATURE_EVAP_COOLING 0.7f
#define TEMPERATURE_NIGHT_COOLING 3.0f
#define TEMPERATURE_CANOPY_STILL 0.25f
#define TEMPERATURE_SOIL_RATE 0.7f
#define TEMPERATURE_SOIL_FOLLOW 0.03f
#define TEMPERATURE_WATER_RATE 3.0f
#define TEMPERATURE_WATER_FOLLOW 0.01f

/* What the surface of each tile does this tick (docs/CLIMATE.md §5), from
 * the soil's hourly pass (core/soil.h): the light the crowns let through and
 * the share of the sun gone into evaporation, per tile (0 .. 255); the sun's
 * warming now (TEMPERATURE_SUN_HEAT × the sun's height × the sky) and the
 * night's cooling now (TEMPERATURE_NIGHT_COOLING × night × clear sky). */
typedef struct TemperatureSurface {
    const uint8_t *canopy;
    const uint8_t *evap;
    const uint8_t *sun; /* The tile's own share of the sun (Soil.sun); NULL: `canopy`. */
    float sun_heat;
    float night_cooling;
} TemperatureSurface;

/* What a tile is made of, for heat. */
typedef enum TemperatureMaterial {
    TEMPERATURE_MATERIAL_AIR,
    TEMPERATURE_MATERIAL_WALL,
    TEMPERATURE_MATERIAL_DOOR,
    TEMPERATURE_MATERIAL_ROCK,
    /* Open space under the sky (core/map.h), and roofs out in it: the
     * outdoor air, as it is. */
    TEMPERATURE_MATERIAL_OPEN,
    TEMPERATURE_MATERIAL_COUNT
} TemperatureMaterial;

/* What covers a tile. */
typedef enum TemperatureCover {
    TEMPERATURE_COVER_OPEN,
    TEMPERATURE_COVER_ROOFED,
    TEMPERATURE_COVER_NATURAL,
    TEMPERATURE_COVER_COUNT
} TemperatureCover;

/* Heat put in by burning hearths, waiting for the next pass over its tile
 * (temperatures_heat_source()): the tile's index and the degrees. */
#define TEMPERATURE_SOURCES_MAX 64
typedef struct TemperatureSource {
    uint32_t index;
    float degrees;
} TemperatureSource;

typedef struct Temperatures {
    int width;
    int height;
    int depth;
    /* Per tile, the levels one after the other as in the map (padded by a
     * row and a tile on each side). No heat flows between levels yet: the
     * edges between the last row of a level and the first of the next have a
     * rate of 0. */
    float *celsius;
    /* Caches of the map: per tile, material × TEMPERATURE_COVER_COUNT +
     * cover; the rate between a tile and its east and south neighbors times
     * the pass (0 at the edges of the map and next to rock or open space, and
     * padded so that tiles on the edges need no test). */
    uint8_t *kind;
    float *edge_east;
    float *edge_south;
    /* Per tile, from its rates of exchange with the outdoor air (by its
     * cover, plus each side open to open space) and with the ground (under
     * a natural roof, plus each side against rock): the share of its gap to
     * them a pass closes (the rates times the pass, at most 1; 0 when it
     * exchanges with neither, rock and open space), and the ground's share
     * of what pulls it (0 out in the air, 1 deep in a cave). */
    float *pull;
    float *ground_share;
    /* One bit per tile: whether the sweep works it out (all but rock and
     * open space). */
    uint64_t *active;
    /* Per tile, the heat stored in the ground (or the water) under it (°C),
     * for the tiles under the sky, and whether it is water (a cache of the
     * map, as `kind`). */
    float *soil;
    uint8_t *water;
    /* The surface this tick (temperatures_set_surface(); none: the open air
     * only, no ground under the tiles). */
    TemperatureSurface surface;
    /* Heat waiting for the pass over its tile, at most one entry per tile. */
    TemperatureSource sources[TEMPERATURE_SOURCES_MAX];
    int source_count;
    float outdoor;              /* The outdoor air at the last tick: open space's. */
    float *buffers[3];          /* Allocations behind celsius, edge_east, edge_south. */
    uint32_t topology_revision; /* The map revisions the caches were built for. */
    uint32_t build_revision;
} Temperatures;

/* Every tile at the `outdoor` temperature (rock at the ground's). */
bool temperatures_init(Temperatures *temperatures, const Map *map, float outdoor);
/* Allocated for the map, nothing worked out (for loading a save). */
bool temperatures_init_empty(Temperatures *temperatures, const Map *map);
void temperatures_free(Temperatures *temperatures);

/* Outdoor air temperature at a tick, before the weather
 * (core/weather.h): the season's mean plus the daily swing. */
float temperature_outdoor(uint64_t tick);

/* The season's mean temperature at a tick, and where the day stands
 * between its coldest (-1) and warmest (1) hour. */
float temperature_season_mean(uint64_t tick);
float temperature_daily_swing(uint64_t tick);

/* Temperature of a tile; TEMPERATURE_GROUND out of bounds. */
float temperature_at(const Temperatures *temperatures, int x, int y, int z);

/* The temperature a plant lives in on the tile it grows on: that of the
 * tile, except for a tree, whose foot is the tile of its trunk (solid wood:
 * rock, at the ground's temperature) and whose crown is in the open air: the
 * outdoor air then. */
float temperature_for_plant(const Temperatures *temperatures, const Map *map, int x, int y, int z);

/* Heats a tile by `degrees` at once. */
void temperatures_add_heat(Temperatures *temperatures, int x, int y, int z, float degrees);
/* Heat put into a tile little by little (a burning hearth, every tick): it
 * waits for the next pass over the tile, which adds half of it before working
 * the tile out (it came in all along the pass) and the rest after, so that the
 * tile does not pile it up in a saw-tooth between passes. Added at once when
 * too many tiles wait (TEMPERATURE_SOURCES_MAX). */
void temperatures_heat_source(Temperatures *temperatures, int x, int y, int z, float degrees);

/* What a tile is made of and covered by, as the layer sees the map now. */
TemperatureMaterial temperature_material(const Map *map, int x, int y, int z);
TemperatureCover temperature_cover(const Map *map, int x, int y, int z);

/* Speed of rot of food lying on a tile: item_rot_rate() at the tile's
 * temperature (ITEM_ROT_REFERENCE_CELSIUS when `temperatures` is NULL),
 * under its roof if any, ×ITEM_ROT_RAINED_ON under the open sky while it
 * `rains`; 0 out of bounds. */
float temperature_rot_rate(const Temperatures *temperatures, const Map *map, int x, int y, int z,
                           bool rains);

/* The heat stored in the ground and water under every tile set to
 * `celsius` (a new world: the day's mean temperature). */
void temperatures_settle_ground(Temperatures *temperatures, float celsius);

/* What the surface does from now on (copied; the arrays must outlive the
 * ticks), or nothing (NULL). */
void temperatures_set_surface(Temperatures *temperatures, const TemperatureSurface *surface);

/* One tick: the band of rows due at `tick` exchanges heat, with the
 * outdoor air at `outdoor` (core/weather.h). */
void temperatures_tick(Temperatures *temperatures, const Map *map, uint64_t tick, float outdoor);

uint64_t temperatures_hash(const Temperatures *temperatures, uint64_t hash);

/* The temperatures a being is at ease in and those its body bears
 * (colonist_temperature_ranges(), animal_temperature_ranges()): pleasant
 * from `pleasant_low` to `pleasant_high`; below `bearable_low` it freezes,
 * above `bearable_high` it overheats (docs/HEALTH.md §10). */
typedef struct TemperatureRanges {
    float pleasant_low;
    float pleasant_high;
    float bearable_low;
    float bearable_high;
} TemperatureRanges;

/* How a temperature feels to it: freezing (below what it bears), cold
 * (below what is pleasant), pleasant, hot, scorching. */
typedef enum TemperatureFeel {
    TEMPERATURE_FREEZING,
    TEMPERATURE_COLD,
    TEMPERATURE_PLEASANT,
    TEMPERATURE_HOT,
    TEMPERATURE_SCORCHING,
    TEMPERATURE_FEEL_COUNT
} TemperatureFeel;

TemperatureFeel temperature_feel(const TemperatureRanges *ranges, float celsius);

/* Its translation key ("Pleasant"...). */
const char *temperature_feel_key(TemperatureFeel feel);

/* Saves or loads (docs/SAVE.md) the temperatures, the heat in the ground,
 * the caches of the map and the heat waiting; already allocated at the size
 * of the map. Not the surface (set every tick). */
void temperatures_serialize(Serializer *s, Temperatures *temperatures);

#endif /* COLONY_CORE_TEMPERATURE_H */
