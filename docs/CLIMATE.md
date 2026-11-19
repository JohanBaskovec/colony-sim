# Microclimate and water

The water in the ground and in the air, and the temperature close to the
ground, tile by tile: the shade of a wood and the heat of a bare slope, the
dew of a clear night, the soil soaked after a storm and dried by a week of
sun, the wet banks of a river, what plants drink.

Status: built, steps C1 to C4 (§9): soil water (`core/soil.h`),
microclimate (`core/temperature.h`), plants and water (`PlantWater` in
`core/map.h`, `docs/PLANTS.md` P4) and the views. Values marked *(tuning)*
are first guesses.

## 1. Principles

- **Physical, simplified.** Each process is the shape of the real one (a
  bucket of water in the roots, the energy of the sun turned into heat or
  into evaporation, heat stored in the ground) with as few numbers as
  possible. Nothing is a special case of a place: a wood is cool because its
  crowns take the sun, its leaves turn water into vapour and its air mixes
  slowly with the outside.
- **Cheap.** Water changes over hours: it is worked out once an hour per
  tile, in slices over the ticks like the plants (`update_plants()`). The
  temperature keeps its passes (`core/temperature.h`) with a few more numbers
  per tile, read from caches. No new search, nothing per tick per tile.
- **Deterministic and in the core.** Everything comes from the map, the
  weather, the clock and the plants; nothing is drawn at random.
- **Scale.** A tile is 1 m × 1 m (`GAME_DESIGN.md`). A day of the game is a
  day: fluxes are real ones per hour (a summer day evaporates 3 to 5 mm), but
  a year has 60 days, so the rain of a season falls in 15 days (§3.1).

## 2. What is stored

Per tile of the ground levels (the tiles plants can live on, up to
`Map.top_ground_level`):

| Field | Type | What |
|---|---|---|
| `water` | float, mm | Water in the root zone (about the top 50 cm) |
| `evap` | uint8, 0..255 | The share of the sun's energy that went into evaporation last hour (for the microclimate) |
| `canopy` | uint8, 0..255 | The light let through by what lies above (`map_light_cover()`), last hour |
| `wet` | uint8 | Distance to open water, capped (§3.5): a cache rebuilt when the terrain changes |

Map-wide: the snowpack (mm of water), the air's dew point (§4).

Per tile in the temperature layer: `soil` (°C), the heat stored in the
ground under it (§5.3).

## 3. Water in the ground (C1)

A bucket per tile, by its terrain *(tuning)*:

| Terrain | Wilting point `WP` | Field capacity `FC` | Saturation `SAT` | Drains (/h above FC) | Takes in (mm/h) |
|---|---|---|---|---|---|
| Sand | 8 | 40 | 90 | 0.25 | 40 |
| Gravel | 12 | 55 | 100 | 0.18 | 25 |
| Soil | 40 | 140 | 200 | 0.06 | 12 |
| Rich soil | 55 | 180 | 250 | 0.05 | 15 |

Below `WP` plants cannot draw water; between `WP` and `FC` is what they
live on; above `FC` the soil is wet and drains; at `SAT` it is waterlogged
and takes no more. The **moisture** shown and used by plants
(`soil_moisture_of()`) is one scale for every terrain: `m = (water - WP) /
(FC - WP)` up to field capacity (0 dry at the wilting point, 1 at field
capacity, below 0 drier still), then `1 + (water - FC) / (SAT - FC)` above
it, 2 saturated. Open water counts as 3 (`PLANT_MOISTURE_STANDING`), and a
tree's trunk has the moisture of the ground beside its foot.

Every hour, for each tile of rooting ground (sand, soil, rich soil, gravel,
no rock):

### 3.1 Rain and snow

- What falls, in mm an hour *(tuning)*: rain 0.25, storm 1.2; nothing from
  fog and clouds (dew, §4, adds a little at night). With the odds of
  `WEATHER_SEASON_ODDS` it gives about 4 mm a day in summer (mostly storms)
  and 3 in spring and autumn: a little more than plants and sun take in a
  long dry spell, so the ground stays moist on average, dries in a week of
  sun and floods in a stormy one.
- **Interception.** Crowns hold part of it on their leaves, which then
  evaporates: what reaches the ground is `rain × (0.55 + 0.45 × canopy)`
  (half under a dense crown in leaf, more when bare).
- **Roofs and caves.** Nothing falls on a tile covered by a built roof or by
  the ground of a level above (`map_cover_level()`); a tile beside open
  space under the sky is not covered.
- **Snow** does not reach the soil: it adds to the snowpack (mm of water,
  map-wide like `Weather.snow`), which melts at 0.12 mm per degree above 0 °C
  an hour (more in the rain) into every open tile.

### 3.2 Taking it in

All that reaches the ground goes in, up to the terrain's rate and to `SAT`;
the rest runs off (lost for now: puddles and streams later).

### 3.3 Drainage

Above `FC`, water drains down out of the root zone:
`(water - FC) × drains` an hour. Sand is dry a day after rain, rich soil
stays wet for days. Near open water it drains only down to the water table
(the bank's target, §3.5), so a bank stays soaked.

### 3.4 Evaporation and transpiration

The **potential** evapotranspiration of an open tile is the energy that
could go into evaporation, in mm an hour *(tuning)*:

    PET = 0.55 × sun × sky × f(T) + 0.03 × (1 - RH)

- `sun` is `game_sunlight()` (the sun's height, 0 at night), `sky` the
  light the weather lets through (`weather_light()`).
- `f(T) = clamp((T + 5) / 25, 0, 1.2)`: little in the cold.
- The second term is the drying of dry air, day and night (`RH` the air's
  relative humidity, §4).

A clear summer day then gives about 4.5 mm, a cloudy one 2, a winter day
less than 1: the right orders.

What a tile actually loses comes from what is on it and over it:

- **Crowns over it** take the light first: the crown's share is
  `1 - canopy`, transpired by the tree from the soil under the crown (its
  roots are there): `PET × (1 - canopy) × f_dry(m)`.
- **The plant on the tile** gets what the crowns let through:
  `PET × canopy × leaf × f_dry(m)`, with `leaf` its leaf cover: its growth
  times its kind's `PlantDef.transpiration` (grass 1, moss 0.4, heather 0.6,
  a bramble 1, a resting or died-back plant 0).
- **Bare soil** evaporates what is left of the light from its top layer
  only: `PET × canopy × (1 - leaf) × 0.4 × min(1, water / FC)²`.
- `f_dry(m) = clamp(m / 0.5, 0, 1)`: plants close their pores below half of
  the useful water.

A tile under a crown in leaf therefore loses water as fast as a meadow,
through the tree; its own floor plants and soil lose little. `evap` stores
`AET / (PET of the open)` for the microclimate (§5).

The crown's foliage, in the sun at the top of the tree, is where that
transpiration happens: a foliage tile's `evap` is `f_dry(m)` of the ground
under it (the first ground down its column, or beside the foot of the trunk),
so a crown is as cool as a meadow while its roots find water and heats like
bare dry ground in a drought. Bare in winter, it is as the ground under it
(the sun goes through to it). Without this the tops of the crowns, counted
as dry floors, were up to 2 °C over the air on summer afternoons.

### 3.5 Banks and hollows

Near open water the ground draws water up from below: each hour a tile
gains `(target - water) × 0.08` when below its target, with the target `SAT`
next to the water (1 tile), `FC + (SAT - FC) / 2` at 2, `FC` at 3 and 4,
`0.75 FC` at 5, `0.5 FC` at 6, nothing beyond (`soil_bank_target()`). The distance (`wet`) is a
distance transform over each level (water tiles and the tiles beside them,
one level down too: a lake in a hollow seen from its banks), rebuilt when
water comes or goes (`Map.water_revision`). So
beaches and banks are wet, a meadow by the river stays green in a drought,
and a dune is dry.

### 3.6 A new world

The water of each tile starts at moisture 0.8 (`GAME_SOIL_START_SHARE`), or
the target of §3.5 near water when higher. Ground opened later (rock dug
out) starts at field capacity on its first hour, unknown until then.

World generation places plants by the moisture their ground is expected to
hold (`expected_moisture()` in `worldgen.c`): `0.45 + 0.6 ×` the moisture
noise of the region, or the bank's target by the distance to open water when
wetter (a distance transform over the columns), and each plant's chance is
multiplied by its water response (§6). So no heather nor bracken on the
banks, fewer trees right at the water's edge.

## 4. Water in the air

One air for the map, as the temperature: its **dew point** `Td` comes from
the weather, and its **relative humidity** at a tile follows from that
tile's temperature (the Magnus formula):

    RH(T) = exp(17.27 Td / (Td + 237.3) - 17.27 T / (T + 237.3))

- `Td` *(tuning)*: in rain, storm and fog the air is saturated (`Td` = the
  air's temperature); otherwise `Td` = the day's mean temperature minus 4
  (clear) or minus 2 (cloudy), never above the air's.
- So the air is driest in the afternoon of a clear day (RH about 50 %) and
  near saturation before dawn; a cooler tile (a wood, §5) has a higher RH
  with the same vapour, as in a real wood.
- **Dew.** When an open tile's temperature falls below `Td` at night, 0.02 mm
  an hour condense on it (into its water).
- Used for: the dry-air term of `PET`, the drying of fresh grass (later),
  comfort (later), fog (later: forms when RH reaches 1 at dawn).

## 5. Temperature near the ground (C2)

Today every tile under the sky is pulled towards the same outdoor air
(`core/temperature.h`). The outdoor air stays what it is (the air above the
woods and fields); what changes is what each tile is pulled towards, how
fast, and the heat of the ground under it.

### 5.1 What a tile is pulled towards

The outdoor air is what a weather station measures: the air over a
well-watered meadow. A tile under the sky is pulled towards that air plus
what its surface does differently from such a meadow:

    target = outdoor + S × (canopy × (1 - 0.7 × evap) - 0.3)
                     + N × (1 - canopy)

- `S = sun_heat × sun × sky`, `sun_heat` *(tuning)* 5 °C
  (`TEMPERATURE_SUN_HEAT`): what the sun on bare dry ground adds to the air.
  A meadow (`canopy` 1, `evap` about 1) gives `S × 0.3`, which the `- 0.3`
  takes back: it stays at the outdoor air. Bare dry sand is `S × 0.7` warmer
  than the meadow, the shade of a dense wood `S × 0.3` cooler.
- Evaporation cools: the `(1 - 0.7 × evap)` (`TEMPERATURE_EVAP_COOLING`):
  the energy that went into vapour does not heat the air.
- `N = night_cooling × clearness` once the sun is down, `night_cooling`
  *(tuning)* 3 °C (`TEMPERATURE_NIGHT_COOLING`), `clearness` 1 clear, 0.3
  cloudy, 0 in rain, fog or storm. The open ground radiates to the sky as the
  station's meadow does, so the outdoor air already holds it; crowns hide the
  sky, so a wood is up to `N` milder (fewer frosts in a wood).
- `canopy` and `evap` (0 .. 255) come from the soil's hourly pass (§3.4);
  the sun, night and sky factors are worked out once per pass
  (`SurfacePass` in `temperature.c`), so a tile costs a few multiplications.

### 5.2 How fast

A wood's air mixes slowly with the open air: the outdoor air's share of a
tile's pull is multiplied by `0.25 + 0.75 × canopy`
(`TEMPERATURE_CANOPY_STILL`: a quarter under a dense crown); the pull of
rock beside it is not. Its neighbours still exchange heat with it as before,
so the edge of a wood is mild and its heart cool, by itself.

### 5.3 Heat stored in the ground

After its pull, each tile under the sky exchanges heat with the ground under
it (`Temperatures.soil`): `T += (soil - T) × 0.7` an hour
(`TEMPERATURE_SOIL_RATE`), and the ground follows the tile slowly,
`soil += (T - soil) × 0.03` an hour (`TEMPERATURE_SOIL_FOLLOW`, a day or so
behind). Open water holds far more heat: 3 an hour and 0.01
(`TEMPERATURE_WATER_*`), so a lake is cool on summer afternoons and mild at
night. The ground starts at the day's mean temperature
(`temperatures_settle_ground()`); `game_settle_climate()` resets air and
ground after the clock jumps (debug season change, tests).

### 5.4 What it gives

Measured on seed 42 in summer, relative to the outdoor air: at 14:00 the
heart of a wood -3.6 °C, a meadow about -1 (its stored ground lags the
afternoon), bare sand +0.4, open water -3.8, the tops of the crowns -0.6; before dawn a
wood +1.8. The
unit test `test_microclimate` checks the signs on a strip map (a wood cooler
than the meadow and the air in the afternoon, milder at dawn, the meadow
near the air). In winter, bare crowns let the sun and the sky through: the
difference mostly goes.

### 5.5 Cost

Per tile and pass: `canopy` and `evap` (two bytes), `soil` (a float), a
water flag (a byte), a few multiplications. The soil's hourly pass runs in
slices over the ticks like the plants; the crowns over each tile are looked
up every `SOIL_CANOPY_HOURS` (6) only, and the distance to water when water
changes. Together with the temperature pass computed half as often
(`TEMPERATURE_SWEEP_TICKS` 200), `bench_simulation 12 4 1` went from 10.9 to
13.3 µs per tick (+22 %) and scenario 3 from 14.6 to 18.1 (+24 %) against
the same pass without soil and microclimate.

## 6. What it changes

- **Plants (C3, `docs/PLANTS.md` P4):** each plant has a water range like its
  temperature range, `PlantDef.water` (`PlantWater`: `min`, `opt_low`,
  `opt_high`, `max` in moisture, `drought_days`, `flood_days`):
  - **growth** times `f_water`, a plateau: 0 at `min`, 1 from `opt_low` to
    `opt_high`, 0 at `max`;
  - **drought:** below `min` an awake plant loses `1 / (drought_days × 24)`
    of its health an hour (resting plants wait), and dies after
    `drought_days`; **drowning roots:** above `max` the same with
    `flood_days`, resting or awake; they come after frost and heat, before
    darkness, and health mends as before once the water is right;
  - the plant window says "growing slowly (dry ground)", "(ground too wet)",
    "drying out" or "roots drowning";
  - **spreading** (§13 of PLANTS): no young plant where its water response is
    0 now;
  - without a known moisture (a plant made in a test, ground not followed)
    water plays no part.

  | Plant | min | best | max | Drought / flood days |
  |---|---|---|---|---|
  | Grass | 0.05 | 0.5 - 1.4 | 2.2 | 20 / 10 |
  | Tree | 0 | 0.3 - 1.3 | 1.9 | 60 / 30 |
  | Hazel | 0.05 | 0.4 - 1.3 | 1.9 | 40 / 20 |
  | Bramble | 0.05 | 0.4 - 1.4 | 1.9 | 30 / 15 |
  | Wild garlic | 0.1 | 0.7 - 1.6 | 2.1 | 10 / 20 |
  | Nettle | 0.1 | 0.7 - 1.6 | 2.1 | 15 / 15 |
  | Plantain | 0.05 | 0.4 - 1.4 | 2.1 | 25 / 15 |
  | Wild carrot | 0 | 0.3 - 1.2 | 1.8 | 30 / 8 |
  | Cattail | 1.3 | 1.8 - 3 | 3.5 | 15 / - |
  | Reed | 1.2 | 1.7 - 3 | 3.5 | 20 / - |
  | Moss | 0 | 0.6 - 1.9 | 2.5 | 60 / 20 |
  | Wood sorrel | 0.1 | 0.6 - 1.5 | 2.0 | 10 / 15 |
  | Bracken | 0.1 | 0.4 - 1.2 | 1.6 | 25 / 6 |
  | Wild strawberry | 0.05 | 0.5 - 1.3 | 1.8 | 15 / 10 |
  | Bilberry | 0.1 | 0.6 - 1.4 | 1.7 | 15 / 8 |
  | Dog rose | 0 | 0.3 - 1.2 | 1.8 | 40 / 10 |
  | Heather | 0 | 0.2 - 1.1 | 1.5 | 40 / 6 |
  | Mushrooms | needs nothing | | | |

  Measured on seed 42 without settlers over two years: the ground averages
  0.9 in autumn and winter and falls to 0.5 in a dry summer (half the tiles
  under 0.3); then growth slows and grass stops spreading, but nothing dies
  of it (a first try with the damage of the moisture lovers from 0.3 halved
  the nettles, wood sorrel and bilberries in one dry summer: the damage now
  starts near the wilting point, as for real plants). No heather, bracken or
  bilberry on the beaches (there were on 10 to 14 % of the beach sand). No
  measurable cost (`bench_simulation`).
- **Food and items:** rot by the tile's temperature (cooler in the wood's
  shade) and wetness (later).
- **Animals:** they already go to a cooler spot in the heat: the woods.
- **Settlers:** the shade of a tree in a heat wave (felt temperature), later
  thirst by the heat.
- **Fire (later):** dry ground and dry plants burn.

## 7. Not done (later)

Water flowing on the ground (runoff into hollows, puddles, streams), rivers
rising and falling, irrigation and digging ditches, wind (drying, wind
chill), clouds of fog on cold wet mornings, snow per tile (drifts, melting
first in the sun), frost heave, soil types under the root zone.

The sun reaching each tile by its own (the shadow of a cliff, of a wall, of
a wood beside it, instead of what lies straight above only) is designed in
`docs/SHADOWS.md` (§5: the `sun` of §3.4 and the `S` of §5.1 become the
tile's own, by the share of the tile in the sun). The temperature model is
to be reworked as a whole after that, around the sun's rays and the shadows:
heat where the rays land (the ground, a wall, a roof), rooms warmed through
their openings, the warmth leaving by an open door.

## 8. Open questions

- Should the moisture noise of world generation become a soil depth (a
  bigger or smaller bucket per tile), so that the pattern of dry and wet
  places persists beyond the first weeks?
- Interception by the floor plants (moss holds water).

## 9. Steps

1. *(done)* **C1 soil water:** the bucket (§3), the air (§4), the inspector line
   ("Soil: moist"). Tests: a storm fills, sun empties, roofs keep dry,
   banks stay wet, sand drains faster than soil.
2. *(done)* **C2 microclimate:** targets, pulls and stored heat (§5). Tests: a wood
   cooler at 15:00 and milder at 5:00 than the open, a wet tile cooler than
   a dry one in the sun, no change at night under clouds.
3. *(done)* **C3 plants and water:** §6, with the beach question of the
   roadmap.
4. *(done)* **C4 views:** a soil moisture view next to the temperature and
   light ones (key K, a drop button: ochre dry, green moist, blue soaked; the
   tile under the mouse in words), the air's humidity in the status panel
   (`weather_outdoor_humidity()`).
