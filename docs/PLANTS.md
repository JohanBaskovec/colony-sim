# Plants

How wild plants grow, rest, suffer and die. They react to the temperature,
the light and, later, the water of their tile, instead of following a
calendar.

Status: steps **P1** (temperature, health, rest; §2 to §4 and §7), **P2**
(light; §5) and **P3** (ripening by warmth; §7) are implemented; P4 (§9) is
design. Before it a plant grew
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
  - foliage over it (a tree's crown, `core/tree.h`): each layer lets through
    dense 0.35, medium 0.6, sparse 0.8 *(tuning)*, the layers multiply: a
    crown of several layers is darker than a thin one;
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
gone in winter. **Mushrooms** come up on bare ground near mature trees on
the cool nights (4 to 16 °C at midnight, `MUSHROOM_MIN_CELSIUS`) after rain,
in autumn and in spring, are gone within days, are lost when the climate falls
below 3 °C and killed by a hard frost.

**Products** ripen with the warmth the plant has gathered since it woke (its
degree-days above its `t_min`, `Plant.warmth`): a cold summer brings the
blackberries late, a warm one early. Each has a window
(`PlantProduct.warmth_from`, `warmth_until`) and a chill that loses it
(`lost_below`, the plant's climate):

| Product | From | Until | Lost below | In this climate |
|---|---|---|---|---|
| Blackberries | 220 | - | 10 °C | end of summer to mid autumn |
| Wild garlic leaves | 0 | 145 | - | spring |
| Nettle shoots | 0 | 100 | - | spring (the autumn shoots are gone) |
| Plantain leaves | 17 | - | 5 °C | spring to autumn |
| Wild carrot roots | 360 | - | - | autumn to the next spring's wake |
| Cattail rhizomes, grass | - | - | - | all year |

A plant that dies back loses its stocks and its warmth; a resting plant keeps
its warmth (and its stocks, which grow no more) until it wakes, when the warmth
starts over. A plant of a generated world (`plant_settle()`) starts with the
expected warmth of the day.

## 8. Later

- **Water** (P4): soil moisture per tile (rain, shade, nearby water,
  drying in the heat and the wind), `f_water` in the growth, drought that
  harms and kills, waterlogging for the plants that hate it, cattails in
  the wet. `docs/GAME_DESIGN.md`, *Soil*.
- **Evergreens** (pines): crowns that stay in leaf all winter (§12 treats all
  trees as deciduous).
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
- Products: see P3 below.

## 8c. As built (P2)

- The sun (`core/game_clock.h`): `game_day_length()`, `game_sunrise()`,
  `game_sun_noon()` and `game_sunlight()` (the noon sun times the sine of the
  day's progress, 0 at night). The day on screen, `game_daylight()`, has its
  dawn and dusk (two hours each) around the same sunrise and sunset: winter
  nights are long and summer ones short.
- The cover of a tile is worked out when a plant is updated
  (`map_light_cover()`: nothing is cached, a plant looks up through the levels
  above it once an hour; the light view and the shade drawn on screen keep
  it per tile until `Map.light_revision` changes): open sky 1, foliage by
  density (`MAP_LIGHT_FOLIAGE_*`, each layer in turn: they multiply), a built roof
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

## 8d. As built (P3)

- `Plant` is 20 bytes (`warmth`, a float of degree-days). An awake plant adds
  `(T - t_min) / 24` an hour when it is warmer than its `t_min`; it starts over
  when the plant wakes from rest, and is 0 for what died back
  (`plant_product_in_season()` says whether a product is on the plant).
- `PlantProduct.days` and `PlantConditions.day_of_year` are gone; the
  calendar stays only for what a tree lets fall (`PlantDef.fall_days`).
- The expected warmth of a plant settled on a day is the year's degree-days
  up to it in the mean climate (`expected_warmth()`, a table made once).
- On seed 42 the blackberries are on the plants from the end of summer to
  mid autumn, garlic and nettle shoots in spring only, carrots from autumn to
  spring, mushrooms on cool damp nights of autumn and spring.
- Not done: ripening of what trees shed (hazelnuts, `fall_days`) by warmth.

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
3. **P3: ripening by degree-days** *(done)*, mushrooms by temperature.
4. **P4: water** (soil moisture), with `docs/GAME_DESIGN.md`'s *Soil*.

Each step changes the state hash of the benchmarks.

## 10. A forest floor with its plants

Grass does not grow in the shade, and a wood is not a lawn with trees in it.
World generation now places the woods first and what lives under them after,
from the light each tile gets (`map_light_cover()`, §5) and from the ground:

1. **Trees and hazels** (`place_woody()`): scattered, dense where the forest
   noise is high, and with **glades** where a second noise opens the woods.
   Most start grown (15 % young): the shade they cast, which decides what
   lives under them, would change within days otherwise.
2. **The crowns** are built (`core/tree.h`) and the litter spread.
3. **The understory** (`place_understory()`): for each free tile of ground a
   chance per plant, from the light (a window: not below what the plant
   needs, not above what it bears), the fertility and terrain, the moisture
   of the region, how forested the place is and a noise of its own per
   plant, so that bracken, nettles or heather stand in **patches**. Grass,
   last, fills what is left. Where nothing fits the ground stays bare, under
   its litter.
4. **The shores** (`place_shore_plants()`): cattails, and reeds in beds of
   their own.

Nothing is generated where it would starve (a test checks it); the few
deaths left in the first weeks are saplings whose crowns thicken.

### The plants

*Light* is the best a tile gets in a year (cover × 0.85).

| Plant | Where | Season | Gives | Use |
|---|---|---|---|---|
| Moss | any ground, damp, deepest shade up to half light | grows from the first mild days to the frosts; slow | moss (cut, comes back slowly) | bedding (beds), tinder |
| Wood sorrel | fertile ground, damp, deep shade | leaves from spring | sour leaves | food |
| Wild garlic *(existing)* | rich soil, woods, shade | spring, dies back in summer | leaves | food |
| Bracken | poor to plain ground, half shade to light, in patches | fronds die down in winter; brown and dry from the start of autumn | bracken (taken whole, cut brown) | bedding, thatch, tinder, fuel |
| Nettle, plantain, wild carrot *(existing)* | as before, now in patches and by light | | | food |
| Wild strawberry | fertile ground, glades and edges, half shade to light | fruit in the first half of summer | strawberries | food |
| Bilberry | poor ground (sand, gravel), thin woods | berries in the middle of summer | bilberries | food |
| Dog rose | fertile ground, edges and open, thorny | hips from the start of autumn, **on the bush all winter** | rose hips | food when there is little else |
| Heather | sand and gravel, full sun, in patches | evergreen; cut, it takes weeks to grow back | heather | bedding, thatch, fuel |
| Reed | in the shallows and along the shore, in beds | stems dry from autumn, standing all winter | reeds | the best thatch |
| Bramble, hazel, cattail, mushrooms *(existing)* | | | | food |

Plants of poor ground (`PlantDef.poor_soil`: moss, bracken, bilberry,
heather) grow as well on any ground from their `min_fertility` up: poor
ground is where they are not outcompeted, not where they thrive.

### What the fibres are for

Every plant has a use. The fibres cut from plants (`GATHER_GROUP_FIBRES`,
with grass: one job, cutting) are:

- **building**: a slot that takes dry grass (a bed, a wall, a roof, a
  ladder) takes bracken, heather or reeds too (and a bed moss), the nearest
  first (`structure_material_types()`); once one kind is in a slot it takes
  only that kind, so that a structure is never half one thing and half
  another;
- **fire**: dry bracken and heather are fuel (`HEARTH_FUEL_TYPES`, not
  brought by default: the player allows them in the hearth's window), and
  dry grass, bracken and moss are tinder (`ITEM_TINDER_TYPES`);
- **food**: the berries, hips and leaves, with the food zones.

### Who eats them

Wild animals eat the new food plants where they bear (core/animal.c):
roe deer bilberries, strawberries, rose hips and wood sorrel; red deer
bilberries and hips; boar the same as roe deer; hares strawberries and wood
sorrel. Fibres are not eaten.

### Not done

- Poisonous look-alikes of the new food plants (lily of the valley already
  stands for wild garlic's); bracken as poison to eat, moss as dressing
  (with health, D2 to D4).
- Spread (§8): a patch cut bare stays bare, and a glade does not close.
- A site that has taken some reeds and then runs out of reeds waits for more
  rather than switching to grass.

## 11. Open questions

- Under foliage a plant lives in the outdoor air (foliage is open to the air
  for the temperature layer); only a tree lived at the ground's temperature,
  because its foot is the tile of its trunk (solid wood): it now lives in the
  outdoor air (`temperature_for_plant()`). A milder, damped temperature on
  the forest floor would be truer; P2's light is the other half of it.

## 12. Deciduous trees

Trees and hazels lose their leaves in the cold (`PlantDef.deciduous`), so that
light reaches the ground in winter and early spring: what wild garlic lives
on.

- **When.** The leaves follow the climate the trees live in, the running mean
  of the air (`Plant.climate`, §4): in full leaf from `PLANT_LEAF_FULL_ABOVE`
  (12 °C), bare below `PLANT_LEAF_BARE_BELOW` (5 °C), and in between losing or
  growing leaves with the climate (`plant_bare_target()`). In this climate
  the crowns start to thin in the second half of autumn, are bare from the
  first days of winter to the first days of spring, and are in leaf again by
  the middle of spring.
- **How.** Not per tree: the foliage is terrain, which belongs to no plant,
  and every tree meets the same air. Each hourly pass over the plants adds up
  the climates of the deciduous trees with a crown and sets `Map.bare`
  (0 in leaf .. `MAP_BARE_STEPS` bare, in sixteenths) from their mean
  (`game.c`, `leaf_conclude()`); a new world takes it from the trees as
  generated. It is part of `map_hash()` once it is not 0.
- **Light.** `map_light_cover()` lets a layer of foliage through the barer it
  is, from `MAP_LIGHT_FOLIAGE_*` (leaf) to `MAP_LIGHT_BARE_*` (branches only:
  0.8 for a dense layer, 0.9, 0.95; they still shade a little, and layers
  multiply). Changing `Map.bare` bumps `Map.light_revision`, so the light
  cache of the drawing follows. Plants under the crowns therefore get more
  light in winter and early spring (the P2 growth sees it).
- **Drawing.** Each crown tile has a bare texture (`ground_foliage*_bare`):
  the branches of the leafy one without the leaves, on dark earth for a
  dense crown, on nothing for the thin ones. In winter it is drawn with the
  leaves fading over it as the crown comes into leaf (alpha = leaf share); the
  young trees' sprites go browner.
- **Not done.** Evergreens (a per-tree state: the foliage tile would have to
  know its tree); leaf colours in autumn; leaves falling as litter.

