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
 * temperature of the ground, TEMPERATURE_GROUND. Burning hearths add heat
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

#include "core/map.h"

#include <stdbool.h>
#include <stdint.h>

/* Outdoor temperature in the middle of spring, summer, autumn and winter
 * (tuning). */
#define TEMPERATURE_SEASON_MEANS {12.0f, 22.0f, 10.0f, -2.0f}
#define TEMPERATURE_DAILY_SWING 5.0f
#define TEMPERATURE_COLDEST_HOUR 5
#define TEMPERATURE_WARMEST_HOUR 15
/* Rock, and what is deep under a natural roof. */
#define TEMPERATURE_GROUND 10.0f

/* Ticks for a full pass over the layer (2.4 in-game minutes). Stable as long
 * as a tile's rates add up to less than TICKS_PER_HOUR / this (25 an hour):
 * open air has 4 × TEMPERATURE_CONDUCTANCE_AIR + TEMPERATURE_EXPOSURE_OPEN. */
#define TEMPERATURE_SWEEP_TICKS 100

/* Heat exchange rates, per hour (tuning): between neighbors (the lower of
 * the two conductances), with the outdoor air, with the ground. */
#define TEMPERATURE_CONDUCTANCE_AIR 4.0f
#define TEMPERATURE_CONDUCTANCE_WALL 0.5f
#define TEMPERATURE_CONDUCTANCE_DOOR 1.5f
#define TEMPERATURE_CONDUCTANCE_ROCK 0.5f
#define TEMPERATURE_EXPOSURE_OPEN 6.0f
#define TEMPERATURE_EXPOSURE_ROOFED 0.3f
#define TEMPERATURE_GROUND_RATE_NATURAL_ROOF 0.3f

/* What a tile is made of, for heat. */
typedef enum TemperatureMaterial {
    TEMPERATURE_MATERIAL_AIR,
    TEMPERATURE_MATERIAL_WALL,
    TEMPERATURE_MATERIAL_DOOR,
    TEMPERATURE_MATERIAL_ROCK,
    TEMPERATURE_MATERIAL_COUNT
} TemperatureMaterial;

/* What covers a tile. */
typedef enum TemperatureCover {
    TEMPERATURE_COVER_OPEN,
    TEMPERATURE_COVER_ROOFED,
    TEMPERATURE_COVER_NATURAL,
    TEMPERATURE_COVER_COUNT
} TemperatureCover;

typedef struct Temperatures {
    int width;
    int height;
    float *celsius; /* Per tile (padded by a row and a tile on each side). */
    /* Caches of the map: per tile, material × TEMPERATURE_COVER_COUNT +
     * cover; the rate between a tile and its east and south neighbors (0 at
     * the edges of the map, and padded so that tiles on the edges need no
     * test). */
    uint8_t *kind;
    float *edge_east;
    float *edge_south;
    float *buffers[3]; /* Allocations behind celsius, edge_east, edge_south. */
    uint32_t topology_revision; /* The map revisions the caches were built for. */
    uint32_t build_revision;
} Temperatures;

/* Every tile at the outdoor temperature of `tick` (rock at the ground's). */
bool temperatures_init(Temperatures *temperatures, const Map *map, uint64_t tick);
void temperatures_free(Temperatures *temperatures);

/* Outdoor air temperature at a tick. */
float temperature_outdoor(uint64_t tick);

/* Temperature of a tile; TEMPERATURE_GROUND out of bounds. */
float temperature_at(const Temperatures *temperatures, int x, int y);

/* Heats a tile by `degrees` (a burning hearth, every tick). */
void temperatures_add_heat(Temperatures *temperatures, int x, int y, float degrees);

/* What a tile is made of and covered by, as the layer sees the map now. */
TemperatureMaterial temperature_material(const Map *map, int x, int y);
TemperatureCover temperature_cover(const Map *map, int x, int y);

/* Speed of rot of food lying on a tile: item_rot_rate() at the tile's
 * temperature (ITEM_ROT_REFERENCE_CELSIUS when `temperatures` is NULL),
 * under its roof if any; 0 out of bounds. */
float temperature_rot_rate(const Temperatures *temperatures, const Map *map, int x, int y);

/* One tick: the band of rows due at `tick` exchanges heat. */
void temperatures_tick(Temperatures *temperatures, const Map *map, uint64_t tick);

uint64_t temperatures_hash(const Temperatures *temperatures, uint64_t hash);

#endif /* COLONY_CORE_TEMPERATURE_H */
