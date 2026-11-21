# Water

Today open water is a terrain: `TERRAIN_SHALLOW_WATER` and
`TERRAIN_DEEP_WATER`, laid by world generation in the hollows of the plains
and never changing. The ground holds water in a bucket per tile for the
roots (`core/soil.h`, `docs/CLIMATE.md` §3), and what runs off is lost.
Items are dry or wet by type (fresh grass and grass, wet twigs and twigs).

This document is the design of water as a quantity: a depth of free water
in every cell of the volume, flowing by gravity (a channel dug from a lake
fills, a downpour floods the hollows, a river rises and breaks its banks);
water held in every block of the ground, soaking in, seeping through and
coming out again (a water table, springs, wells, damp galleries); and a
moisture of their own for items (grass that dries into hay, wood that
seasons, a bundle of twigs that gets soaked in the rain).

Status: step W1 is built (the water layer at rest, §10); the rest is
design. Values marked *(tuning)* are first guesses.

## 1. Principles

- **Water is a quantity, never a terrain.** Every cell of the map holds an
  amount of water. A lake is a hollow full of it, a puddle a few
  millimetres of it on a floor, a river water moving from one edge of the
  map to another. The floor under water is ordinary ground (silt, sand,
  gravel).
- **Mass is kept.** Water comes in by rain, snowmelt, springs and the edges
  of the map, and goes out by evaporation, the edges of the map and what
  plants drink. In between it is only moved: amounts are integers, every
  transfer takes from one cell what it gives to another, and a test sums
  the water of the map.
- **Physical, simplified.** As in `docs/CLIMATE.md`, each process has the
  shape of the real one with as few numbers as possible: water on the
  surface flows down its slope with some momentum, water in the ground
  drains down and then moves sideways towards a lower water table (Darcy's
  law), items dry towards what the air around them holds.
- **Credible everywhere, all the time.** The ground of the whole map holds
  water, from the topsoil to the deepest rock, and it keeps changing:
  granite is nearly watertight, earth and sandstone let water through,
  gravel and sand carry it fast.
- **Cheap where nothing moves.** A lake at rest, the dry rock of a hill and
  a stone lying in the sun cost nothing. Surface water is worked out only in
  the parts of the map where it moves (§3.4). Ground water and items are
  worked out once an hour, in slices over the ticks, like the soil today.
- **Deterministic and in the core.** Integer amounts, a fixed order, no
  random draws in the flow. The same seed and the same orders give the same
  water, which is part of the state hash.
- **Scale.** A tile is 1 m × 1 m, a level 2 m (`GAME_DESIGN.md`). A tick is
  1.44 s of game time (`TICKS_PER_HOUR` 2 500): water crossing a tile in a
  tick or two flows at a credible speed. A year has 60 days, so a
  season's rain falls in 15 days (`docs/CLIMATE.md` §1).

## 2. What is stored

### 2.1 One layer of water

`Map.water`: a `uint16_t` per cell of the volume, in **millimetres of water
over the tile's square metre** (1 mm is one litre).

- **In an open cell** (no rock, no wall), it is free water, from 0 to
  `WATER_CELL_FULL` = 2 000 (the cell is full: 2 m of water).
- **In a solid cell** (rock, earth, sand), it is the water in the block's
  pores, from 0 to the block's room for water: its porosity times 2 000
  (§5.1). A block of earth holds up to about 900 mm, a block of granite 20.

One layer for both means that digging a soaked block out leaves its water
where it was, now free water in the new cell (§5.5), and that a wall built
on a flooded tile cannot be placed while the water is there (§6.4).

At the largest map (256 × 256 × 48 levels) the layer takes 6 MB.

### 2.2 Depth on a floor

What a floor at level `z` has over it is the free water of its cell, plus
the cells above as long as the one under is full:

    depth(x, y, z) = water(z) + (water(z) == FULL ? water(z + 1) + ... : 0)

A floor at the bottom of a lake two levels deep has 2 000 + 600 mm over it,
so it is deep water. The free water of an open cell with no floor (over a
drop) rests on the cell below only once that one is full. Otherwise it
falls (§3.2).

### 2.3 Depth classes

The rest of the simulation never reads millimetres. It reads a few classes,
with a gap between going up and coming back down (hysteresis), so that a
depth wavering at a boundary does not change the paths every tick
*(tuning)*:

| Class | Depth on the floor | Walking | Drinking | What it does |
|---|---|---|---|---|
| Dry | < 3 mm | as the ground | no | |
| Wet | 3 mm to 2 cm | ×1.2 | no | a puddle's film: the ground is soaked, items lying there get wet |
| Puddle | 2 to 10 cm | ×1.5 | yes | |
| Shallows | 10 to 50 cm | ×3 (today's shallow water) | yes | settlers and items get wet |
| Wading | 50 to 120 cm | ×6 | yes | wet to the waist, nothing carried gets dry |
| Deep | ≥ 120 cm (leaves at < 100 from W2) | impassable (swimming later, §8) | from the bank | |

`map_is_water()` becomes "Shallows or deeper": what animals and settlers
drink, what the temperature counts as open water, what the banks of the soil
look for until step W4 replaces them (§5.4). From W3, when rain leaves
puddles, puddles can be drunk too (a thirsty settler in a storm), but
searches for water prefer the Shallows *(tuning)*. The hysteresis of the
classes comes with W2: in W1 water does not move, so the classes are worked
out from the depth alone.

The classes and the walking costs are the only things the pathfinder and
the connectivity regions see. `Map.topology_revision` is bumped only when a
cell enters or leaves Deep, and `Map.water_revision` only when a cell enters
or leaves Shallows. A flood front crossing a meadow changes the regions
where it makes tiles impassable, not at every millimetre (§3.6).

### 2.4 The rest

- **Active blocks** of 16 × 16 tiles on one level, where the surface water
  is moving (§3.4). Each holds its flows (§3.3) and a countdown to sleep.
  Blocks at rest hold nothing.
- **The map's edges**: for each edge cell, the level of the water beyond it
  (§4.1).
- **Items**: a `moisture` per item (§7).
- **The known world** (`core/known.h`) keeps the depth class of the water
  seen on each tile, as last seen. A lake out of view keeps the level it had
  when someone last looked.

## 3. Water on the surface

### 3.1 What is simulated

The free water of each open cell, as a height field per level (the
surface of the water is `z × 2 000 + water`) with vertical moves between
levels. This is the "virtual pipes" model often used for water and terrain
in games (a pipe between each pair of neighbouring cells, carrying a flow
that the difference of the surfaces speeds up or slows down), done with
integers:

- **Falling first.** Free water in an open cell with no floor goes into the
  cell below as far as it has room. A waterfall pours down a cliff, a pit
  fills from the bottom up.
- **Then flowing.** Between each cell and its four neighbours on the same
  level (not across a wall, a closed door, rock or the side of a slope,
  §6.4), the flow in the pipe keeps part of itself (momentum) and gains in
  proportion to the difference of their surfaces. What would leave a cell
  is scaled down to the water it has. All flows are worked out from the
  state at the start of the step, then applied: the order of the cells does
  not matter, nothing drifts to one side.
- **A film does not flow.** Water less than `WATER_FILM_MM` (3 mm) deep
  does not leave its cell: it soaks in or dries where it is (§3.5). Without
  it, a storm would set the whole map in motion in films of 1 mm.
- **Slopes.** A slope (`Map.slope`) joins two levels. Water on its tile
  flows down it to the lower neighbour as on flat ground, and only up from
  it when the water is deep enough to reach the upper level. Water runs
  down a ramp into a cellar.

A step is one tick *(tuning: every second tick if it costs too much)*. A
step moves water at most one tile, which gives fronts of 0.5 to 1 m/s, as
for a shallow stream.

### 3.2 Falling between levels

A cell's floor holds its water. An open cell without a floor
(`TERRAIN_OPEN`) passes its water down: the cell below takes what it has
room for. A full cell carries the water of the cell above it, and their
surfaces join (§2.2). That is enough for lakes deeper than a level, for
water pouring off a cliff into the level below, and for a dug shaft that
fills.

Water never goes up by pressure in steps W2 to W5: a full cell receiving
more from above does not push it out of a lower opening (a U-bend, a gallery
under a lake surfacing at a lower spring). That is step W6 (§8).

### 3.3 Flows

For each pipe (east and south of each cell, inside an active block), an
`int16_t` flow in mm per step *(tuning)*:

    flow = flow × 15 / 16 + (surface_a - surface_b) / 4

Then the total out of a cell is scaled down to what it holds (integer, by
the same factor for each pipe). The `15/16` damps the sloshing. The `1/4`
sets how fast differences are evened out. Rounding is towards zero, so that
flows die out at rest.

### 3.4 Waking and sleeping

The map is split into blocks of 16 × 16 tiles on each level. Only **active**
blocks are worked out:

- A block wakes when water is added to it (rain beyond the soil's intake,
  a spring, a river edge), when a neighbouring active block sends it water,
  or when the map changes in it (dug, built, a door opened or shut).
- A block falls asleep after `WATER_SLEEP_STEPS` (60) steps in which no flow
  exceeded 1 mm and no two neighbouring surfaces differed by more than 2
  mm. A lake is then a set of sleeping blocks.
- Sleeping blocks keep their water and nothing else. Their flows are freed.

A lake at rest costs nothing. A channel dug from it wakes the block it
opens into and those the water then reaches, and they fall asleep again
once the channel is full.

### 3.5 Exchanges with the ground and the air

Worked out once an hour per tile, in slices with the soil (`soil_update()`):

- **Soaking in.** Free water on a floor of rooting ground goes into the
  soil's bucket up to its intake (`SoilDef.intake`) and saturation
  (`docs/CLIMATE.md` §3.2). From W4, the bucket then drains into the block
  under it (§5). On bare rock it soaks in at the rock's own rate (§5.1).
  On a built floor (none yet) nothing goes in.
- **Evaporation.** The free water of an open cell under the sky
  evaporates at the potential rate of the tile (`PET`, `docs/CLIMATE.md`
  §3.4, about 4 mm on a summer day). A cell under a roof or in a cave
  evaporates at `SOIL_COVERED_EVAPORATION` of it.
- **Rounding.** The soil's bucket is a float. What goes between it and the
  integer layer is rounded by a deterministic dither (a hash of the tile and
  the hour), so that nothing is lost on average.

Changing a cell's water from these hourly passes wakes its block only when
the change is larger than a film (a puddle drying does not need the flow).

### 3.6 Paths, regions and searches

- The pathfinder reads the class of a tile's water (§2.3) for its cost and
  for whether it can be walked.
- When a cell enters or leaves Deep, the change is logged as a topology
  change like any (`map_set_terrain()` today). The regions are updated
  incrementally. A cell may change class at most once every
  `WATER_CLASS_MIN_TICKS` (60) *(tuning)*, so that a wave at the edge of a
  lake does not log changes every tick.
- A being whose tile turns Deep under it is not stranded: it may still walk
  out of it, at the Wading cost, towards the nearest tile that is not Deep.
  No path ever goes into Deep water.
- Searches for water (thirst, animals) look for Shallows or deeper, then for
  Puddles, as they look for water tiles today.

## 4. Where water comes from and where it goes

### 4.1 The edges of the map

The world goes on beyond the map. Each edge cell has a level of water
beyond it, as if the land outside held what it held when the world was
made (its surface at the start, or nothing if the cell started dry). The
edge cell and that virtual neighbour exchange water through a pipe like any
other:

- A lake cut by the edge keeps its level: what rain adds flows out, and what
  evaporates comes back in, slowly.
- A flood reaching a dry edge on the plains flows out of the map.
- A **river** enters at one edge and leaves at another (§4.2). The level
  beyond its upstream end follows the river's discharge, so it rises and
  falls. The level beyond its downstream end stays where it started.

### 4.2 The river

World generation (step W3) can lay a river across the map: a meandering
channel from one edge to the opposite one or to a neighbouring one, one
level down on its sides and two in its middle, with its banks sloping down
to it. Lakes from the elevation noise stay as they are today. The river is
an option of the New game menu ("River": none, stream, river, for a
channel 3, 6 or 12 tiles wide) *(tuning)*.

The level upstream follows the river's discharge, which comes from the
weather over its catchment, outside the map:

- a seasonal part: high after the snowmelt in spring, low at the end of
  summer;
- the rain of the last days (`Weather.recent_rain` over a longer memory):
  a rainy week raises the river by tens of centimetres;
- rare floods: after the storms of a wet spell, the river can rise by 1 to 2
  m for a day or two, more than its channel holds, and spill over the
  plains.

Like the weather, the discharge comes from the seed and the clock, not from
the game's random stream (`weather_roll()`). A flood is announced by the
river rising for several hours: an alert when it gets within 50 cm of its
banks (step W5).

### 4.3 Rain and snowmelt

- What reaches a floor (`docs/CLIMATE.md` §3.1: interception by crowns,
  nothing under roofs and in caves) goes into the soil up to its intake and
  saturation, as today. **What is left is free water on the tile** instead
  of being lost. On rock and on frozen ground, all of it.
- Rain on open water adds to it.
- Rain today is gentle (0.25 mm an hour, storms 1.2), below every soil's
  intake. It never floods anything. Storms get an intensity of their own
  (step W3), drawn per period: most of them 1 to 3 mm an hour, now and then
  a **downpour** of 10 to 30 mm an hour for one to three hours, more often in
  summer *(tuning)*. The rain of a season stays about what it is (the soil
  was tuned on it): the other rains get a little lighter.
- A downpour on soaked ground fills the hollows. Water runs down the hills
  in sheets that gather in the valleys and into dug cellars and galleries
  whose openings face uphill.
- Snowmelt (`SOIL_MELT_PER_DEGREE`) does the same in spring: on saturated
  ground, it runs off.

### 4.4 Springs

There are no placed sources. A spring is where the water table (§5.3)
reaches the surface on a hillside: the soaked block beside an open cell
seeps into it (§5.2), and its water runs down the slope. World generation
does not place them. They come from the relief and the rock.

## 5. Water in the ground

### 5.1 Materials

Each solid cell holds pore water up to its room (porosity × 2 000 mm).
Above its field capacity, water drains out of it (gravity). Below, it is
held. How fast water goes through it is its conductivity *(tuning)*:

| Block | Porosity | Field capacity | Conductivity (mm/h) | Room (mm) |
|---|---|---|---|---|
| Earth (`ROCK_EARTH`) | 0.45 | 0.30 | 10 | 900 |
| Sandstone | 0.15 | 0.05 | 2 | 300 |
| Granite | 0.01 | 0.005 | 0.05 | 20 |
| Sand (new, W4) | 0.35 | 0.08 | 300 | 700 |
| Gravel (new, W4) | 0.30 | 0.05 | 1 000 | 600 |
| Clay (new, W4) | 0.45 | 0.40 | 0.01 | 900 |

The trunks of trees (`ROCK_WOOD`), walls and other structures hold no pore
water. Water does not go through them. A wattle screen or a fence lets free
water through (§6.4).

World generation (W4) lays the inside of the ground in layers: earth near
the surface; here and there a bed of gravel or sand (an aquifer, which
carries water far and fast); lenses of clay (water stops over them: a
perched water table, a damp layer in a gallery); rock deeper down. The
clay and the gravel are dug like earth and give their own material, for
later uses (pottery, paths).

### 5.2 How water moves in the ground

Worked out once an hour per column of the map, in slices over the ticks
like the soil:

- **Unsaturated blocks** pass what they hold above their field capacity
  down to the block under them, at their conductivity or the one under
  them, whichever is lower, as far as that block has room. Water soaks down
  from the topsoil. It stops over clay and granite and builds up there.
- **Saturated blocks** form the water table. The **head** of a saturated
  block is the height of the top of the saturated run it belongs to in its
  column (plus what fills the block above it). Water moves sideways between
  neighbouring saturated blocks, from the higher head to the lower, at the
  lower of their conductivities times the difference (Darcy). A hill's
  water table slopes down to the valley, and water flows under the ground
  towards the lakes and the river.
- **Seepage.** A block beside an open cell gives the cell water when its
  head is above the cell's floor and its own water surface. Water gathers at
  the bottom of a gallery dug below the water table, and drips from the
  walls of one dug through a perched layer. A spring comes out on a
  hillside.
- **Soaking in from the surface** comes from the soil's bucket (§5.4) or
  straight from free water on bare rock.
- **The bottom of the map** holds water like a floor: nothing goes through
  it. The edges keep the water table they started with (§4.1).

All amounts are integers moved from one block to another. A slice does a
fixed set of columns in a fixed order. Lateral flows use the neighbours'
heads of the last hour, so the order of the slices does not matter
*(design detail to settle at W4: one cached head per column and saturated
run)*.

### 5.3 The water table

World generation lays a water table close to rest:

- under a lake or the river, at its surface;
- elsewhere a smoothed copy of the ground's height, rising under the hills
  at about half their rise and staying a level or more below their ground;
- blocks below it saturated, blocks above it at field capacity.

From there it lives: it rises after a wet spell and falls in a drought, it
is higher next to the water and on clay, it feeds the lakes and the river
from below in summer.

### 5.4 The root zone

The soil's bucket (`core/soil.h`) stays as it is: the top 50 cm of the
ground under a floor, what plants live on. It becomes the top of the column:

- what drains out of it goes into the block under it (W4) instead of being
  lost, as far as the block has room. Over a soaked block it does not drain;
- it draws water up from the block under it when that block is near
  saturation (capillary rise) at a rate of its own *(tuning)*. A floor over
  a high water table stays wet.

The banks of `docs/CLIMATE.md` §3.5 (`SOIL_BANK_RATE`, `SOIL_WET_FAR`, the
distance to open water) then go away. Banks are wet because the water table
is high next to the water, and a hollow without a lake is wet when the
water table comes close to it.

### 5.5 What it changes

- **Digging.** A dug block leaves its pore water as free water in the new
  cell. A gallery dug below the water table fills at the seepage rate:
  slowly in granite (a drip), in hours in earth, very fast in gravel. The
  diggers (`core/designation.h`) are warned before they dig into a soaked
  block next to a lake: the walls of a gallery show how damp they are
  (dry, damp, wet, dripping, from the share of their room).
- **Wells.** A shaft dug down to below the water table fills up to it: a
  well (drawing water from it needs a bucket, with containers, M4).
- **Cellars.** A cellar dug in a clay hillside stays dry, one dug under a
  meadow by the river floods in spring.
- **Mud.** A floor of soil whose bucket is saturated is mud: walked at ×1.3
  *(tuning)*.
- **Views.** The moisture view (key K) shows the blocks too: the walls of
  galleries on the level shown, and the inside of the ground when the
  view cuts through it.
- **Later**: soaked earth caving in more easily, deep roots of trees
  drawing from the blocks, frost heave.

## 6. What water changes

### 6.1 Settlers and animals

- Walking costs and impassable water (§2.3).
- Getting wet (`colonist.h` already wets settlers in the rain): standing in
  Shallows or deeper soaks a settler like a storm does. Wading soaks what
  they carry too (§7).
- Drinking from Puddles and deeper. Animals look for Shallows or deeper, as
  today.
- A rising flood: a being in water turning Deep walks out (§3.6). Fleeing
  water and taking shelter come with alerts (W5).

### 6.2 Plants

- A plant on a floor with Puddles or deeper has the moisture of open water
  (`PLANT_MOISTURE_STANDING`). Its water response decides whether it bears
  it (cattails, reeds) or its roots drown (`PLANT_STATUS_WATERLOGGED`,
  existing).
- A plant under water deeper than its own height for more than a day dies,
  unless it lives in water *(tuning)*. A flood kills the crops it lies on
  for long.
- World generation places the shore plants (cattails and reeds) by the
  depth: in Shallows near the land, as today in the shallow water.

### 6.3 Items and fire

- Items lying in Wet or deeper water get wet (§7.3). Items that float
  (wood, grass, twigs) are carried by the flow of a river or a flood and
  stranded where it slows down (W5). Stones and flint stay where they lie.
- A hearth in water deeper than Wet goes out and cannot be lit.
- Fire never burns on water. Spreading fire later (M4) stops at it.

### 6.4 What stops water

- Rock, earth and other solid blocks (their pore water moves, §5).
- Walls. A closed door stops free water, an open one lets it through
  (a closed wooden door leaks a little, later).
- Wattle screens and fences let it through.
- Roofs stop rain (as today), not water from the side.
- Building: no wall or hearth is placed on a tile with more than Wet water.
  Stockpiles and workstations can be, and their items get wet.

Later (W5): a floodgate (a door that holds water back, opened and closed by
order), a dam of earth (a built wall of earth), a cistern (a dug pit lined
with clay), irrigation ditches leading water to fields.

### 6.5 Temperature

Open water today keeps the temperature of the ground under it
(`Temperatures.water`). That flag becomes "Shallows or deeper". A puddle
does not change the air over it. Freezing is W6.

### 6.6 On screen

- Water is drawn from the layer: its colour and opacity by depth (the bed
  seen through shallow water, darker over deep water), its surface on the
  level of its top cell, the levels below seen through it as through open
  space, darker.
- Its flow is drawn as faint lines moving with it (from the flows of the
  active blocks, and the river's direction where it sleeps).
- Puddles are drawn as darker, shining patches on the floor.
- The tile inspector shows the water: "Water: 63 cm (wading)", and for a
  block "Damp (40 %)".
- Debug tools (F4): pour water on a tile, empty a tile, start a downpour, a
  flood of the river.

## 7. Moisture of items

### 7.1 What changes for the player

- There is no more fresh grass and grass, wet twigs and twigs: there is
  grass, twigs, branches, logs, each with its **moisture**, shown in the item
  window (`APP_WINDOW_ITEM_INFO`) and the tile's item list: "Moisture: 62 %
  (drying)", how long it should take to dry out of the rain in a tooltip.
- Grass cut fresh weighs about four times its dry weight and dries in a
  few days into hay. Wood from a felled tree is green: it burns badly
  until it has seasoned for weeks. Twigs left in a storm are soaked.
- What takes a spark (grass, bracken, moss) takes one only when dry.
  Building with grass (bedding, thatch) asks for dry grass. Fuel burns worse
  the wetter it is.
- Food that keeps its water rots as today. Food that has dried (jerky,
  dried berries: drying racks, later) keeps much longer.

### 7.2 What is stored

`Item.moisture`: a `uint8_t`, the share of the item's mass that is water,
in steps of 0.5 % (0 to 200). Only items of materials that take water have
one. Stones, flint and bone have none (always 0, never updated).

Each item type that takes water gets an `ItemWater` in its `ItemDef`
*(tuning)*:

| Type | Fresh | Room | Equilibrium (×) | Dries in (h) | Dry below |
|---|---|---|---|---|---|
| Grass | 75 % | 80 % | 1.0 | 60 | 20 % |
| Bracken | 80 % | 85 % | 1.0 | 70 | 20 % |
| Moss | 85 % | 95 % | 1.4 | 120 | 25 % |
| Twigs | 30 % | 60 % | 1.0 | 24 | 20 % |
| Branches | 35 % (dead) / 45 % (felled) | 60 % | 1.0 | 240 | 22 % |
| Logs | 45 % | 60 % | 1.0 | 1 000 | 22 % |
| Wooden tools and spears | 15 % | 50 % | 1.0 | 120 | |
| Meat | 70 % | 75 % | 1.2 | 2 000 | 25 % |
| Berries, fruit | 85 % | 88 % | 1.2 | 1 500 | 20 % |
| Roots | 75 % | 80 % | 1.2 | 1 500 | 15 % |
| Hazelnuts / chestnuts | 20 % / 50 % | 30 % / 60 % | 1.0 | 300 | 12 % |
| Mushrooms | 90 % | 92 % | 1.2 | 400 | 15 % |
| Hides | 60 % | 70 % | 1.2 | 200 | 20 % |

- **Fresh**: its moisture when it comes into the world (cut, felled,
  picked, butchered). Twigs and branches shed by a tree come dead, with the
  ground's moisture taken in as they lie (§7.3).
- **Room**: the most it takes, soaked.
- **Equilibrium**: what it tends to in still air, a curve shared by every
  material (from the sorption of wood: 6 % at 30 % of relative humidity,
  12 % at 65 %, 16 % at 80 %, 20 % at 90 %, 28 % saturated), times the
  material's factor.
- **Dries in**: the time it takes to go most of the way (two thirds) from
  fresh to equilibrium at 20 °C, out of the sun and the rain *(tuning)*.
  Food dries slowly when it only lies about: it rots first. Drying it is a
  work of its own (later).
- **Dry below**: the moisture under which it counts as dry for the rules
  (§7.4) and the word shown (§7.5).

The weight of an item is its dry weight plus its water: `dry / (1 -
moisture)`. Today's weights become the dry weights of the materials (or the
weights at equilibrium for those weighed dry today). Carrying fresh grass
is heavier than carrying hay.

### 7.3 How it changes

Once an hour, with the items that change today (the list of items that
rot or dry, `docs/ITEMS.md` §3), every item that takes water moves towards
a target:

- **The air.** The target is its equilibrium for the relative humidity of
  the air on its tile: the outdoor air's dew point (`weather_dew_point()`)
  at the tile's own temperature (`air_relative_humidity()`). The air by a
  fire is warmer, so it is drier and items dry faster there. A damp
  cave is near saturation, so nothing dries in it.
- **The ground.** An item lying on soaked ground (the soil's moisture above
  field capacity) takes the dampness of the ground: the humidity it feels
  is raised towards saturation by how wet the ground is *(tuning)*. Hay
  left on a wet meadow does not dry.
- **The rain.** An item under the sky in the rain goes towards its room at
  the weather's wetting rate (`WEATHER_WETTING_RAIN`...), times its
  material's take-up.
- **Water.** An item in Wet water or deeper goes to its room within an hour.
- **Speed.** It covers `1 - exp(-1 / τ)` of the way an hour, with τ the
  material's time, shorter in the sun (the tile's sun share times
  `game_sunlight()`: up to three times faster in full sun) and in the heat,
  longer in the cold *(tuning)*.
- **Carried and held items** have the air of their holder's tile.
  Settlers wading carry wet things.

An item within 0.5 % of its target, with no rain and no water on its tile,
is skipped until the weather or its tile changes *(design detail: a
day stamp of the humidity it settled at)*, so that thousands of twigs lying
in a wood do not cost every hour.

### 7.4 What reads it

- **Fire** (`core/hearth.h`): tinder (`ITEM_TYPE_BIT(ITEM_GRASS)`...) takes a
  spark only below its "dry" moisture. Fuel gives its heat less the heat
  needed to boil its water: a fuel at 45 % gives about half of what dry
  wood gives, and is harder to light (the chance of the kindling taking
  falls with its moisture).
- **Building and bills** (`core/structure.c`, `core/workstation.h`): a
  material or ingredient may require "dry" (bedding and thatch of grass,
  tinder bundles). The search for ingredients skips the items that are not.
  Bills keep their quality range. A moisture range for the player comes
  later if needed.
- **Rot**: the rot of food goes by its moisture relative to its fresh one.
  Below its "dry" moisture it rots ten times slower *(tuning)*. Food out in
  the rain rots faster because it gets wetter (it replaces today's ×
  factor in the rain).
- **Gathering zones**: cutting grass gives grass (fresh). Gathering zones
  that collect grass (`GATHER_GRASS`) take it at any moisture.
- **Stockpiles** take grass at any moisture. Settlers do not yet carry wet
  things to a dry place to dry them: a "dry items only" option for
  stockpiles, and hauling things out to dry, come later.

### 7.5 Shown

- In the item window, a row "Moisture: 62 %", with the item's state:
  soaked, wet, drying, dry (`item.moisture.state.*`). The state is a
  phrase of its own, never an adjective of the item: adjectives agree with
  the item in some languages (French "herbe sèche" but "brindilles
  sèches"), so each translation words the states so that they fit any item
  (French: "gorgé d'eau", "en cours de séchage", rather than adjectives).
- In the tooltip, the time it should take to become dry where it lies now.
- On screen, grass goes from green to straw, wood darkens when wet
  (the sprite tinted by its moisture, `render.c`).
- The item list of a tile and the bills' ingredients can sort by moisture.

### 7.6 Types that go away

`ITEM_FRESH_GRASS` and `ITEM_WET_TWIGS` (and `ItemDef.transforms_into` and
`wets_into`, which only they use) go. Their uses (`gather_zone.c`,
`tree.c`, `map.c`'s twig counts, `render.c`, `sprites.c`) read grass and
twigs with their moisture. The stable ids `fresh_grass` and `wet_twigs`
disappear (no saves yet).

## 8. Later (not in these steps)

- **Pressure** (W6): water in full cells pushing out of a lower opening
  (U-bends, a gallery under a lake coming out lower down, artesian wells).
  A search through the connected full cells for an exit lower than the
  surface feeding them, limited in size.
- **Swimming** (W6): Deep water crossed by settlers who can swim, slowly,
  tiring; drowning for those who cannot or are exhausted.
- **Ice** (W6): water freezing from its surface under 0 °C, thick enough to
  walk on after some days of frost, melting in spring (a flood when the
  river's ice breaks).
- Water quality: stagnant puddles, disease from bad water, boiling it.
- Erosion and sediment: rivers moving sand, a flood leaving silt on the
  fields.
- Waterwheels and mills (mechanical power).
- Rain running off roofs to their eaves, gutters, rain barrels.
- Salt water, tides.

## 9. Open questions

- The cost of a flood across the whole plains of a 256 × 256 map: tens of
  thousands of active cells. Measure at W2 and decide between a step every
  tick and every second tick, and whether blocks far from view can step
  less often (they must keep the same results: same steps, only batched).
- Whether the hourly ground-water pass needs to look at every column, or
  only those that changed (W4 measure).

## 10. Steps

Each step is tested, measured against the baseline (`bench_simulation`,
`--bench-render`), and leaves the game playable.

1. **W1, the water layer, at rest** (done). `Map.water` replaces
   `TERRAIN_SHALLOW_WATER` and `TERRAIN_DEEP_WATER`, which go (a silt
   terrain for the beds, sand in the shallows). World generation fills the
   hollows to a level (a lake's surface 40 cm over its shallows'
   floor, deep water 2.4 m) with water at rest. The beaches move up to the
   plains' level: today they lie on level −1 with the shallows, and water
   at rest has one surface over a whole level, so it would cover them; the
   sand ring of the shore is now on level 0, its slopes leading down into
   the water.
   The depth classes and their costs, `map_is_water()` from them, the
   revisions bumped by class (§2.3). Drawing water from the layer (depth
   shades), the known world copying it, the inspector's water row. Nothing
   flows yet: the water stays as laid. The state hash changes; tests: lakes
   at rest, the classes, paths through shallows and not deep water, animals
   and settlers drinking.
2. **W1b, moisture of items.** §7: `Item.moisture`, `ItemWater`, the
   hourly drying and wetting, fire, building, rot, weight, the item window,
   the tinted sprites. `ITEM_FRESH_GRASS` and `ITEM_WET_TWIGS` go. Tests:
   grass drying into hay in the sun and not on wet ground, twigs soaked in
   the rain, tinder refused when wet, the weight.
3. **W2, surface flow.** §3: falling, the pipes, films, slopes, active
   blocks, rest, the class changes logged with their minimum time, a being
   walking out of rising water. The debug tools to pour and empty water.
   Tests: mass kept, determinism, a channel dug from a lake filling it to
   the lake's level, a shaft filling from the bottom, water not crossing a
   wall or a closed door, a lake at rest staying asleep (no block active),
   a waterfall. A benchmark (`bench_water`: a dam opened, a channel, a
   flooded plain) and `bench_simulation` unchanged in cost with lakes at
   rest.
4. **W3, where water comes from.** §4: the edges, rain beyond the
   soil's intake as free water, evaporation, soaking in, storms of
   various intensity and downpours, snowmelt running off, the river laid by
   world generation (a New game option) with its discharge and its floods.
   Tests: the river's steady flow, a downpour filling a hollow and the
   hollow drying in the sun, a flood spilling over the banks.
5. **W4, water in the ground.** §5: the pore water of every block, the
   new layers (sand, gravel, clay), the water table laid by world
   generation, drainage and Darcy flow, seepage, the root zone draining into
   the ground and drawing from it (the banks of the soil go), damp walls,
   wells, mud. Tests: a gallery under the water table filling, one in
   granite dripping, rain soaking down to the water table, a spring on a
   hillside, a well.
6. **W5, water in play.** Floodgates, dams of earth, cisterns, irrigation
   ditches (with farming), floating items carried away, alerts (the river
   rising, a gallery flooding), settlers keeping out of rising water.
7. **W6, later.** Pressure, swimming and drowning, ice (§8).
