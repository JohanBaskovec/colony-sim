# Plants

How wild plants grow, rest, suffer and die. They react to the temperature,
the light and, later, the water of their tile, instead of following a
calendar.

Status: steps **P1** (temperature, health, rest; §2 to §4 and §7) and **P2**
(light; §5) are implemented; P3 and P4 (§9) are design. Before it a plant grew
on its growth days (spring to autumn) at a rate set by the fertility of the
ground. Values marked *(tuning)* are first guesses.

## 1. Principles

- **Conditions, not dates.** A plant grows when its tile is warm and lit
  enough, slows away from what suits it, stops when it is too cold, too hot
  or too dark, and is harmed beyond that. The seasons come out of the
  climate: a hard winter, a cold snap in spring, a cave or a roofed room
  change what grows, with no special case.
- **Each plant its own needs.** Temperatures, light and hardiness are data
  in `PlantDef`, as the products are (§7).
- **Damage builds up and heals.** A plant has a health that bad conditions
  wear down, in proportion to how bad and for how long, and that good ones
  restore. A short frost marks a plant, a long hard one kills it.
- **Rest is how plants live through winter.** They harden as the cold sets
  in, and stand far more cold once dormant than when growing. So the first
  cold night of autumn kills nothing, and a late frost in spring does harm.
- **Deterministic.** Everything comes from the tile's temperature
  (`core/temperature.h`), the clock, the weather and the map. Nothing is
  drawn at random.

## 2. Growth

Every hour, when the plant's slice of the map is updated (`update_plants()`):

    growth gained = growth_per_day / 24
                    × f_temperature(T) × f_light(L) × fertility (× f_water, P4)

- `growth_per_day` keeps its meaning as the rate at its best (today it is
  the rate on fertility 1), so the present values carry over.
- **T** is the temperature of the plant's own tile (`temperatures` layer),
  sampled at that hour. Under a roof, in a cave or beside a fire, a plant
  lives in that climate. The daily swing (`TEMPERATURE_DAILY_SWING`) is part
  of it: frost on a spring night, heat on a summer afternoon.
- **f_temperature** is a plateau, per plant:

        0 below t_min · rising to 1 at t_opt_low · 1 up to t_opt_high
        · falling to 0 at t_max · 0 above

- **f_light** saturates, per plant: 0 below the compensation point
  `light_min` (it makes no more than it burns), rising to 1 at `light_full`.
  A shade plant (wild garlic) is fully fed in the light of a forest floor, a
  sun plant (grass) only in the open. L is defined in §5.
- **Fertility** stays the terrain's (`TerrainDef.fertility`) and its minimum
  (`PlantDef.min_fertility`). It will move with the soil later
  (`docs/GAME_DESIGN.md`, *Soil*).
- No growth while dormant (§4). A tree still waits for room as today
  (`core/tree.h`).

## 3. Health and death

A plant has a **health**, 0 to 1, full for a new plant.

Each hour, what harms it takes health off:

- **Frost:** below its frost limit (§4: lower once hardened), in proportion
  to the degrees below, `PLANT_FROST_DAMAGE` per degree and hour *(tuning,
  0.02)*. Three degrees too cold for five hours takes 0.3; ten degrees too
  cold kills within five hours.
- **Heat:** above its heat limit `t_lethal_heat`, the same way
  (`PLANT_HEAT_DAMAGE`).
- **Darkness:** while its light stays under `light_min`, `1 / (dark_days ×
  24)` per hour. A plant kept in the dark for `dark_days` days dies (a few
  days for grass, weeks for a tree seedling). Nights do not count: the light
  is taken over the day (§5).

When none of them does, health comes back by `PLANT_RECOVERY` per hour
*(tuning: a quarter a day)*, only while the plant is growing.

A sick plant also grows more slowly: growth is multiplied by its health.

**At 0 the plant dies.** It is removed and the tile is bare
(`map_remove_plant()`). A place left bare stays bare until plants spread
(`docs/ROADMAP.md`). What it bore goes with it.

**Grown trees do not die** in this step. A dead tree (trunk and foliage over
several levels to take down, deadwood standing) is a feature of its own
(§8). A tree whose trunk has risen (`core/tree.h`, from stage 2) takes
damage and stops growing, but its health never goes below a small floor.
A seedling, with no trunk yet, dies like any plant.

## 4. Dormancy and hardening

### 4.1 Two ways to rest

- **Rest** (`PLANT_REST_SLEEPS`): the plant stays as it is: same growth,
  nothing gained, no stock growing back (what is there stays). Trees, hazels,
  brambles, grass, plantain and wild carrot (rosettes) rest, and the cattail,
  whose rhizomes stay in the ground to be dug up all year.
- **Dying back** (`PLANT_REST_DIES_BACK`): what is above ground dies (growth
  back to 0, stocks gone, nothing shown: `plant_is_dormant()`), and the roots
  wait in the ground. Nettle and wild garlic die back.

### 4.2 When it starts and ends

- A plant goes dormant after `PLANT_DORMANCY_DAYS` days *(tuning: 3)* whose
  mean temperature stays below its `t_min`, and wakes after the same number
  of days above it. A single warm day in winter wakes nothing, a single cold
  night in autumn puts nothing to sleep. The count is kept per plant: so a
  plant in a heated room or a cave keeps its own pace.
- **Wild garlic** is a spring plant of the forest floor. It also dies back
  when it is too *warm*: after the same number of days above its `t_max`
  (`PlantDef.rests_in_heat`, state `PLANT_STATE_SUMMER_DORMANT`). It does not
  come back with the cooler autumn: it waits for the winter's cold
  (the climate below its `t_min`, when it becomes an ordinary died-back plant)
  and wakes with the spring.
- A plant that dies back cannot be harmed by the frost underground (its
  roots are not on the tile's air). A resting plant can, but hardened.

### 4.3 Hardening

Each plant has two frost limits: `t_frost_active` (growing, soft tissue:
−2 to −5 °C) and `t_frost_hardened` (fully hardened: −15 to −30 °C). Its
limit moves from the first to the second over `PLANT_HARDENING_DAYS`
*(tuning: 4)* of cold (mean below `t_min`), and back to the active one twice
as fast once it is warm. A plant that wakes on a mild spell and meets a hard
frost a few days later is hurt: real late frosts.

## 5. Light

Today daylight is only drawn (`game_daylight()`: full day from 7 to 18 all
year, dawn from 5 and dusk until 20, `GAME_DAYLIGHT_MIN` at night, times
`weather_light()`). The plants need a
light of their own, per tile:

    L = sun(hour, day of the year) × sky(weather) × cover(tile)

- **The sun** rises and sets with the season: a day of
  `PLANT_DAY_LENGTH_WINTER` hours at midwinter to
  `PLANT_DAY_LENGTH_SUMMER` at midsummer *(tuning: 8.5 and 15.5, a
  temperate latitude)*, 12 at the equinoxes, smoothly over the year. Its
  strength follows its height: 0 at night, a twilight around sunrise and
  sunset, full near noon (strongest in summer, weaker in winter).
  - **The day on screen follows it too**: `game_daylight()` uses the same
    sunrise and sunset, so winter nights are long and summer nights short.
    The settlers' hours (sleep, work) stay as they are. The hearths' light
    matters more in winter.
- **The sky:** clouds, fog and rain dim it, as on screen (`weather_light()`).
- **The cover over the tile**, cached per ground tile like the temperature's
  cover, and updated around what changes (`Map.build_log`, trees growing and
  felled):
  - open sky: 1;
  - foliage over it (a tree's crown, `core/tree.h`): dense ~0.2, medium
    ~0.4, sparse ~0.7 *(tuning)*, the densest layer over the tile;
  - a built roof: ~0.05 (light through the gaps);
  - a natural roof (rock, the ground of a level above): 0. Underground
    and in caves, plants starve.
- A hearth's light lets one see at night, but feeds no plant.

A plant judges its light over the day: the hours of the day it grows (L
above `light_min`) feed it, the night is neither good nor bad, and the
**darkness** of §3 counts days whose best light stays under `light_min`.
**Mushrooms** are fungi and need no light at all.

## 6. What it means in play

- Spring warms the soil: the grass greens, nettles come back from their
  roots, wild garlic carpets the woods before the trees close over it (with
  deciduous trees, §8), then withers when it gets warm.
- Summer: everything grows. On a hot afternoon the plants slow down.
- Autumn: days get shorter and colder, the plants slow, harden, go to rest
  or die back.
- Winter: nothing grows. Resting plants stand the cold, the roots of those
  that died back wait. A colder winter than usual (a cold spell,
  `core/weather.h`) can kill what was not hardy enough.
- A roofed room or a cave: plants starve in the dark. A warm room keeps a
  plant growing in winter if it has light (a later greenhouse).

## 7. Data per plant *(tuning)*

`PlantDef` gains: `t_min`, `t_opt_low`, `t_opt_high`, `t_max` (°C),
`t_frost_active`, `t_frost_hardened`, `t_lethal_heat`, `light_min`,
`light_full`, `dark_days`, and the way it rests (`PLANT_REST` or
`PLANT_DIE_BACK`, plus "dies back when too warm" for wild garlic).
`growth_days` and `dies_back` go.

| Plant | Grows (min / best / max °C) | Frost: active / hardened | Heat kills | Light (min / full) | In the dark | Rests by |
|---|---|---|---|---|---|---|
| Grass | 5 / 15-25 / 35 | −5 / −25 | 45 | 0.2 / 0.8 (sun) | 10 days | resting |
| Tree (seedling) | 5 / 18-25 / 35 | −3 / −30 | 45 | 0.1 / 0.6 | 30 days | resting |
| Hazel | 4 / 15-22 / 32 | −3 / −30 | 42 | 0.1 / 0.5 (half shade) | 30 days | resting |
| Bramble | 5 / 18-25 / 35 | −5 / −20 | 45 | 0.1 / 0.6 | 20 days | resting |
| Wild garlic | 2 / 8-15 / 20 | −5 / −20 | 30 | 0.05 / 0.3 (shade) | 15 days | dying back (cold and warmth) |
| Nettle | 5 / 15-25 / 32 | −2 / −25 | 40 | 0.1 / 0.5 | 15 days | dying back |
| Plantain | 5 / 15-25 / 35 | −5 / −20 | 45 | 0.2 / 0.8 (sun) | 10 days | resting (rosette) |
| Wild carrot | 4 / 15-22 / 32 | −5 / −15 | 40 | 0.2 / 0.8 (sun) | 10 days | resting (rosette) |
| Cattail | 10 / 20-30 / 38 | −5 / −25 | 45 | 0.2 / 0.8 (sun) | 10 days | resting (the rhizomes stay) |
| Mushrooms | see below | | | none needed | | |

With the present seasons (12, 22, 10 and −2 °C at mid-spring, summer,
autumn and winter, `TEMPERATURE_SEASON_MEANS`) this gives roughly today's
calendar: growth from spring to autumn, wild garlic in spring only, nettles
gone in winter. **Mushrooms** keep their present rule in P1 and P2 (autumn,
after rain, near mature trees, gone within days); in P3 they come up
between 8 and 20 °C after rain, and go at the first frost.

**Products** keep their days of the year (`PlantProduct.days`) until P3,
when ripening follows the warmth gathered since spring (degree-days): a
cold summer brings the blackberries late, a warm one early. A plant that
dies back loses its stocks, as today; a resting plant grows none back.

## 8. Later

- **Ripening by degree-days** (P3) and mushrooms by temperature (P3).
- **Water** (P4): soil moisture per tile (rain, shade, nearby water,
  drying in the heat and the wind), `f_water` in the growth, drought that
  harms and kills, waterlogging for the plants that hate it, cattails in
  the wet. `docs/GAME_DESIGN.md`, *Soil*.
- **Deciduous trees:** their crowns bare while they rest, so that light
  reaches the ground in winter and early spring (what wild garlic lives
  on), with the drawing that goes with it.
- **Death of grown trees:** standing deadwood, a source of dry wood, that
  falls in time.
- **Spread** (seeds, runners) and competition for light: the shade of a
  bramble or a tree slowing what is under it.
- Crops, greenhouses, frost protection: with farming.

## 8b. As built (P1)

- `Plant` is 16 bytes: `damage` (0 = unharmed, so a plant made by hand is
  healthy; `PLANT_DAMAGE_MAX` = dead), `climate` (the mean temperature, 1/256
  degree), `hardening` (0 tender .. 255 hardened) and `state`
  (`PlantState`, plus a flag: a plant made by hand takes the temperature of its
  tile as its first climate, `plant_settle()` the expected one of the day).
- The **climate** is a running mean over `PLANT_CLIMATE_HOURS` (72) hours
  (about three days): a plant rests when it falls below `t_min` and wakes one
  degree above it (`PLANT_REST_HYSTERESIS`). It also sets the hardening
  (`PLANT_HARDEN_PER_HOUR`, `PLANT_SOFTEN_PER_HOUR`).
- `plant_advance(plant, &PlantConditions, hours)` does it all, with the
  temperature of the plant's tile (`temperature_for_plant()`), so a plant in a
  roofed room or a cave lives in that climate (a tree, standing on the tile of
  its trunk, in the outdoor air). Frost and heat take health, an awake plant mends
  (`PLANT_RECOVERY`), a plant at 0 is removed. A tree with a trunk
  (`TREE_CROWN_FROM`) keeps `PLANT_TREE_HEALTH_FLOOR` of its health at least.
- `plant_growth_rate()`, `plant_status()` and `plant_health()` feed the plant
  window (condition, rate per day, health when harmed) and the tile
  information.
- Products keep their days of the year (`PlantProduct.days`); a resting plant
  grows no stock back, a plant that died back has none.

## 8c. As built (P2)

- The sun (`core/game_clock.h`): `game_day_length()`, `game_sunrise()`,
  `game_sun_noon()` and `game_sunlight()` (the noon sun times the sine of the
  day's progress, 0 at night). The day on screen, `game_daylight()`, has its
  dawn and dusk (two hours each) around the same sunrise and sunset: winter
  nights are long and summer ones short.
- The cover of a tile is worked out when a plant is updated
  (`map_light_cover()`: nothing is cached, a plant looks up through the levels
  above it once an hour): open sky 1, foliage by density
  (`MAP_LIGHT_FOLIAGE_*`, the densest layer counts), a built roof
  `MAP_LIGHT_ROOF`, rock or the ground of a level above 0. A trunk over its
  own foot is no roof; a tree, standing on the tile of its trunk, gets the open
  light.
- `game_plant_conditions()` gathers what a plant meets (temperature, fertility,
  `light` now, `light_peak` the noon sun times the sky and the cover); the
  plant window uses it too.
- `PlantDef.growth_per_day` is what a plant grows in a day at its best: twelve
  hours of full light (`PLANT_BEST_DAY_HOURS`), so a long clear summer day
  gives about that and a short autumn one much less, with the temperature
  factor on top. `plant_light_response()`: none below `light_min`, full from
  `light_full`.
- Darkness is judged on the day's best light (`light_peak`), so nights do not
  count: an awake plant whose best light stays under `light_min` loses
  `1 / (dark_days × 24)` of its health an hour. A plant that rests needs no
  light; a fungus (`dark_days` 0) none at all. On a world's first days the
  grass under the densest crowns dies (about one in six on seed 42), and a
  tree without a trunk shaded by others' crowns can go the same way.
- The plant window says "little light" or "starving in the dark", and the
  rate it shows is the most it can make in a day ("Grows up to 11% a day").
- Cost: `bench_simulation` 4 days 35 → 45 µs a tick, almost all of it settlers
  searching wider for grass to cut (what grass grows in a short autumn day is
  a fraction of what it did), not the plant update.

## 9. Steps

1. **P1: temperature, health, rest** *(done)*. Growth from the temperature (§2,
   light taken as 1), health and death by frost and heat (§3), resting,
   dying back and hardening (§4); `growth_days` and `dies_back` removed. New
   plants of a generated world (`plant_settle()`) take the state of their
   day from the expected climate of that day (dormant and hardened in
   winter). The plant window shows its health, its state ("growing",
   "slowed: too cold", "resting", "died back", "suffering from frost",
   "too dark") and how fast it grows. The tests that rely on the seasons are
   written in terms of temperature.
2. **P2: light** *(done)*. The sun's day by season (also on screen), the sky,
   the cover of each tile (open, foliage, roof, rock), `f_light` and death in
   the dark; mushrooms need none.
3. **P3: ripening by degree-days**, mushrooms by temperature.
4. **P4: water** (soil moisture), with `docs/GAME_DESIGN.md`'s *Soil*.

Each step changes the state hash of the benchmarks.

## 10. Open questions

- Under foliage a plant lives in the outdoor air (foliage is open to the air
  for the temperature layer); only a tree lived at the ground's temperature,
  because its foot is the tile of its trunk (solid wood): it now lives in the
  outdoor air (`temperature_for_plant()`). A milder, damped temperature on
  the forest floor would be truer; P2's light is the other half of it.
