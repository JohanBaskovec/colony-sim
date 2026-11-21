/*
 * Soil: the water in the ground of every tile plants can live on, as a bucket
 * per tile filled by rain and snowmelt and drawn up by the water table,
 * emptied by drainage into the ground under it (core/groundwater.h) and by
 * what evaporates and what plants transpire (docs/CLIMATE.md §3,
 * docs/WATER.md §5.4).
 *
 * Each tile of rooting ground (sand, soil, rich soil, gravel, no rock) holds
 * `water` in mm in its root zone. Its terrain sets the bucket (SoilDef): the
 * wilting point below which plants cannot draw water, the field capacity it
 * holds against gravity, the saturation where it is waterlogged. The
 * moisture plants feel (soil_moisture_of()) is the same for every terrain: 0
 * at the wilting point, 1 at field capacity, 2 at saturation, in proportion
 * between; PLANT_MOISTURE_STANDING in open water.
 *
 * Tiles are worked out once an hour, in slices over the ticks like the plants
 * (soil_update()). What the weather brings that hour comes in a SoilHour
 * worked out once for the map. Besides the water, each update keeps what the
 * microclimate needs (docs/CLIMATE.md §5): the light the crowns let through
 * (`canopy`, what the rain gets through too), the tile's own share of the
 * sun (`sun`: the foot of a north wall gets less of it, docs/SHADOWS.md
 * §5.2) and the share of the sun's energy that went into evaporation
 * (`evap`).
 */
#ifndef COLONY_CORE_SOIL_H
#define COLONY_CORE_SOIL_H

#include "core/serialize.h"

#include "core/map.h"
#include "core/tile_light.h"
#include "core/water.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The bucket of a terrain, in mm of water in the root zone, and how fast it
 * drains above its capacity (share an hour) and takes rain in (mm an hour). */
typedef struct SoilDef {
    float wilting;
    float capacity;
    float saturation;
    float drainage;
    float intake;
} SoilDef;

/* NULL for what is not rooting ground (water, stone, open space, roofs,
 * foliage). */
const SoilDef *soil_def(Terrain terrain);

/* The moisture of `water` mm in a bucket (0 wilting, 1 field capacity, 2
 * saturation; below 0 when drier than the wilting point), and the water
 * that holds a moisture. */
#define SOIL_MOISTURE_SATURATED 2.0f
float soil_moisture_of(const SoilDef *def, float water);
float soil_water_for(const SoilDef *def, float moisture);

/* Rain reaching the ground under crowns: SOIL_THROUGHFALL_DENSE of it under
 * crowns letting no light through, all of it in the open, in proportion to the
 * light between (the leaves hold the rest, and it evaporates). */
#define SOIL_THROUGHFALL_DENSE 0.55f
/* Snowmelt: mm of water an hour per degree above 0 °C. */
#define SOIL_MELT_PER_DEGREE 0.12f
/* Banks (docs/CLIMATE.md §3.5), where no ground is followed under the floor
 * (a flat world's single level): the water table draws a tile's water up
 * towards its target by SOIL_BANK_RATE of the gap an hour, up to
 * SOIL_WET_FAR tiles from open water; water does not drain below it. */
#define SOIL_BANK_RATE 0.08f
#define SOIL_WET_FAR 7
/* Over porous rock (docs/WATER.md §5.4), the root zone drains into the block
 * under it, and the water table in that block draws it up (capillary rise)
 * once it stands within this share of the block's height from the floor:
 * from field capacity up to saturation when it reaches the floor,
 * SOIL_RISE_RATE of the gap an hour. */
#define SOIL_RISE_FROM 0.5f
#define SOIL_RISE_RATE 0.08f
/* Plants close their pores below this moisture: what they transpire falls
 * in proportion below it. */
#define SOIL_DRY_BELOW 0.5f
/* Bare soil evaporates this share of the potential, from its wet top only. */
#define SOIL_BARE_EVAPORATION 0.4f
/* Under a roof or in a cave: no sun, still air; the soil only dries this
 * share of the potential. */
#define SOIL_COVERED_EVAPORATION 0.05f
/* Dew condensing on an open tile colder than the dew point, mm an hour. */
#define SOIL_DEW_MM 0.02f
/* Rough stone drinks the rain into its cracks, down into the pores of the
 * rock under it as far as they have room (docs/WATER.md §5.2), up to this
 * many mm an hour while not frozen: a storm soaks away, a downpour runs off
 * (docs/WATER.md §3.5). */
#define SOIL_CRACK_INTAKE 1.5f

/* What the hour brings to the map (worked out once, soil_hour()). */
typedef struct SoilHour {
    float rain;      /* mm reaching open ground (before the crowns). */
    float melt;      /* Snowmelt, mm. */
    float pet;       /* Potential evapotranspiration of an open tile, mm. */
    float dew_point; /* Of the air, °C. */
    bool night;
    /* Look again at what covers each tile (the crowns grow and lose their
     * leaves slowly: every SOIL_CANOPY_HOURS); otherwise `canopy` is kept. */
    bool look_up;
    /* The light of each tile (docs/SHADOWS.md §5.2), and the share of the
     * daylight coming straight from the sun this hour: a tile's own sun
     * (`sun`). NULL: what covers it stands for it (`canopy`). */
    const TileLight *light;
    float direct_share;
} SoilHour;

/* Hours between two looks at what covers each tile. */
#define SOIL_CANOPY_HOURS 6

typedef struct Soil {
    int width;
    int height;
    int depth;
    /* mm per tile; 0 where it is not rooting ground, or not followed yet
     * (ground opened since, rock dug out: soil_update() starts it at field
     * capacity, and it is unknown until then). Never 0 once followed. */
    float *water;
    uint8_t *evap;   /* Share of the potential that evaporated last hour, 0 .. 255. */
    uint8_t *canopy; /* Light the crowns let through last hour, 0 .. 255. */
    /* The tile's share of the sun of open ground last hour (its sky and its
     * share in the sun, SoilHour.light), 0 .. 255: what dries it and warms
     * its air. */
    uint8_t *sun;
    uint8_t *wet;          /* Tiles to open water (SOIL_WET_FAR: farther). */
    float snowpack;        /* Snow lying on open ground, mm of water. */
    float melt;            /* Snowmelt of the hour, mm (soil_snow_hour()). */
    uint32_t wet_revision; /* Map.water_revision `wet` was worked out for. */
} Soil;

/* Every tile of rooting ground at `share` of its useful water (0: wilting,
 * 1: field capacity), the banks at their target. */
bool soil_init(Soil *soil, const Map *map, float share);
/* Allocated for the map, nothing worked out (for loading a save). */
bool soil_init_empty(Soil *soil, const Map *map);
void soil_free(Soil *soil);

/* The potential evapotranspiration of an open tile in an hour, in mm
 * (docs/CLIMATE.md §3.4): the sun (`sunlight`, 0 .. 1, game_sunlight()) let
 * through by the sky (`sky`, weather_light()) at `celsius`, plus the drying of
 * air at relative humidity `humidity`. */
float soil_potential_evaporation(float sunlight, float sky, float celsius, float humidity);

/* Snowpack for the hour: adds what fell (`snowfall` mm), melts by the
 * outdoor air (more in the rain), and returns the melt to spread this hour. */
float soil_snow_hour(Soil *soil, float snowfall, float celsius, bool raining);

/* Works out the distance to open water again if water came or went since
 * (Map.water_revision). */
void soil_refresh_banks(Soil *soil, const Map *map);

/* Looks at what covers tiles first .. last - 1 now (soil_init(), and a
 * cache rebuilt after the map was written directly). */
void soil_look_up(Soil *soil, const Map *map, size_t first, size_t last);

/* One hour of tiles first .. last - 1 (indices in the map), with
 * `temperature` the temperature of each tile (NULL: the dew point never
 * reached, nor frost). With the flow of free water (`flow`, else NULL:
 * docs/WATER.md §3.5): what the ground does not take of the rain and
 * snowmelt lies on it as free water, frozen ground takes none; free water
 * soaks into rooting ground (a pond's bed too, not a lake's bed of silt) and
 * evaporates first (the ground under it does not); rain on stone, roofs and
 * open water adds to their free water. Changes the map's free water only
 * (water_exchange()). */
void soil_update(Soil *soil, Map *map, Water *flow, size_t first, size_t last, const SoilHour *hour,
                 const float *temperature);

/* The moisture of a tile (soil_moisture_of()), PLANT_MOISTURE_STANDING on
 * open water, that of the ground beside its foot on a tree's trunk (where
 * its roots are), -1 where it is not rooting ground. */
float soil_moisture(const Soil *soil, const Map *map, int x, int y, int z);

/* How wet a tile's ground is in words, a translation key (dry, fairly dry,
 * moist, wet, soaked), NULL where it is not rooting ground. */
#define SOIL_DRY_UNDER 0.2f
#define SOIL_FAIRLY_DRY_UNDER 0.5f
#define SOIL_MOIST_UP_TO 1.1f
#define SOIL_SOAKED_FROM 1.8f
const char *soil_moisture_key(const Soil *soil, const Map *map, int x, int y, int z);

/* The moisture a tile's bank gives it at `distance` tiles from open water
 * (0: no water near), and that target in mm of a bucket. */
float soil_bank_moisture(int distance);
float soil_bank_target(const SoilDef *def, int distance);

uint64_t soil_hash(const Soil *soil, uint64_t hash);

/* Saves or loads (docs/SAVE.md) the water in the ground and what the last
 * hour left; already allocated at the size of the map. */
void soil_serialize(Serializer *s, Soil *soil);

#endif /* COLONY_CORE_SOIL_H */
