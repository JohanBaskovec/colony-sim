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

Status: steps W1 (the water layer at rest), W1b (the moisture of
items, §7) and W2 (the surface flow, §3, as built in §3.7) are built (§10);
the rest is design. Values marked *(tuning)*
are first guesses.

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
searches for water prefer the Shallows *(tuning)*. As built (W2, §3.7),
there is no hysteresis: the classes are worked out from the depth alone,
and the flow tells the map of the changes at most once every
`WATER_CLASS_MIN_TICKS` steps instead.

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
- A block falls asleep after `WATER_SLEEP_STEPS` (60) steps in which
  nothing changed in it (its water and its flows) by more than ripples, a
  current holding steady included (as built: §3.7). A lake is then a set
  of sleeping blocks, and so is a river flowing steadily.
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

**As built (W3a).** `soil_update()` takes the flow (`Water`) and gives or
takes free water with `water_exchange()`, in fine units (rounded to the
nearest 1/256 mm, no dither):

- On rooting ground, the rain, the snowmelt and the free water lying there
  soak in up to the intake and the room left below saturation; frozen ground
  (below 0 °C) takes nothing. The free water left evaporates first; the
  ground under it then dries no more that hour.
- Foliage lets the rain through to the ground under the crown. A roof sheds
  its rain over the eaves (not followed). Rough stone drinks up to
  `SOIL_CRACK_INTAKE` (1.5 mm an hour) into its cracks, down to deep ground
  (not followed until W4), when not frozen: a storm soaks away, a downpour
  runs off. Without the cracks, the rain of a storm lay on every outcrop and
  ran off it, keeping hundreds of blocks awake (105 µs a tick instead of 27
  in `bench_simulation 12 4 1`).
- Open water (a lake on its bed of silt) takes the rain on its surface tile
  (`water_surface_index()`: up the column of full tiles) and loses what
  evaporates; a pond on rooting ground other than silt also soaks into it.
- **Waking.** An exchange wakes its block only when it sets the water
  moving: the water would fall through its tile, or flow to or from a
  neighbour by a film's difference at least, after the exchange and not
  before. Rain falling alike everywhere moves nothing; water asleep beside
  lower ground (a pond laid on a plain, a lake on the level of its banks)
  stays held as it was. The flow's own class reports do not wake it.
- Without the flow (`NULL`), the soil behaves as before W3a: what it does
  not take is lost.

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

### 3.7 As built (W2), and what is left

`core/water.c`, `Game.water`, stepped first thing every tick. What differs
from the design above:

- **Fine units.** The flow works in 1/256 mm (`WATER_FINE`): the
  millimetres stay in `Map.water`, the fraction of each tile is kept by the
  flow (`Water.fine`). In whole millimetres the gain of a pipe is 0 below a
  difference of 4 mm, so water stopped on slopes of 3 mm a tile: a channel
  of 40 tiles ended 12 cm below its lake. The pipes are `int32_t`.
- **Rest.** A block falls asleep after 60 steps in which nothing changed in
  it by more than ripples: no pipe's flow changed by more than
  `WATER_QUIET_FLOW` (1/16 mm a step), and no tile's water by more than 60
  such ripples since those steps began (the sun or rain on a tile, and the
  water around evening it out, come and go). A steady current is at rest as
  much as still water (W3c-2, a river: until then a block slept only when
  nothing flowed). Two more conditions: the block as a whole gained or lost
  no more than those ripples (a lake drained slowly through a long channel
  changes by little at each tile and its current hardly changes, but it
  loses water: it starts its quiet steps again), and the blocks it trades a
  current with (more than a ripple across their side) are ready to sleep
  too, directly or through other ready blocks: a current stopped at the
  edge of a sleeping block would jolt the water and send a wave down a
  river that keeps it all awake. A steady river so falls asleep along its
  whole length at once, its levels and slope kept, and costs nothing until
  something changes (the weather, a settler); woken, its flows start again
  from nothing. A sleeping neighbour is woken only for a flow larger than a
  ripple (else two blocks at rest would wake each other in turn).
- **Waking.** From the map's logs: a new `Map.water_log` (water set from
  outside the flow), the topology log and the build log, each change waking
  the blocks around it when water lies within a tile. A log overflowed in a
  tick, or a change "anywhere" (`map_topology_changed()`), wakes every block
  holding water: the lakes as laid then fall asleep again without a
  millimetre moved. An active block wakes the sleeping blocks its water
  would reach before each step, so the flow never writes into a sleeping
  one.
- **Classes.** No hysteresis (§2.3): each active block remembers what the
  map was told of its tiles (passable, open water) and tells it of the
  changes every `WATER_CLASS_MIN_TICKS` steps and when it falls asleep
  (`map_water_class_changed()`); the walking cost follows the depth at
  once. The topology log's entries the flow wrote wake nothing.
- **Past full.** A tile past `WATER_CELL_FULL` (a wave, inflow into a full
  tile) passes the rest to the open tile above it, else keeps it until it
  flows away. A wave reaching the brim of a pit may so splash onto the
  level above.
- **Slopes** are flat floors: water does not run down a ramp to the level
  below (follow-up).
- **Beings.** No path goes into Deep water; one may start from it (the
  pathfinder does not check the tile it starts on), and an idle settler on a
  tile turning Deep steps to a free neighbour (`step_out_of_blocked_tile()`).
  Further out of a flood waits for swimming (W6); animals do not step out.
- **Water laid by hand.** `water_settle()` takes the water as it lies for
  water at rest: tests that put a pond on a plain without banks.
- **Cost** (`bench_water`, 256 × 256): about 4 µs a block a step, so 1 ms a
  step for a whole level awake (a dam opened: a lake 1.5 m deep over half
  the map pouring through a 16-tile breach, still levelling after 20 000
  steps; a pond spreading over the plain, at rest after 2 400). Lakes at
  rest cost nothing: `bench_simulation` is unchanged in cost and state hash.
  Water levels slowly through a narrow breach or a long channel (the flow
  stays near the difference of the surfaces); fine for now.

Left: slopes; walking out of a wide flood; waking only around the changes of
an overflowed log; batching far blocks if floods cost too much (§9). Lakes
of the tests' flat world (`WorldGenParams.flat`, a single level) lie on
the level of their banks, their deep water (`WORLDGEN_FLAT_DEEP_MM`) beside
their shallows: nothing holds them once woken (a tree growing on the shore
is enough), and they spread over the plain; so that world has no willows
(they stand on shores). Players do not get that world:
a relief of 0 % in the New game window makes a plain with its lakes in
hollows (docs/UI.md), held as any other.

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

As built (W3b, `core/water.h`):

- Each column of the edge (north row, south row, west column, east column)
  keeps the **ground** of the land beyond (`Water.edge_ground`, solid under
  it) and the **surface** of its water (`Water.edge_surface`), both taken
  from the edge column when the flow begins (`water_init()`, and
  `water_settle()` for scenarios that change the ground). Beyond a column of
  rock up to the top, the land is solid all the way. Under the ground
  beyond, the edge is solid: a cave cut by the edge keeps its water.
- The virtual neighbour at level z holds the surface's water at that level.
  The edge tile and it share a pipe (`WaterBlock.west_edge`, `north_edge`,
  and the east and south pipes of the last column and row), which answers
  only to what goes past a **film's difference**: past it the current grows
  from nothing (from a ripple, `WATER_QUIET_FLOW`), so that it eases off
  smoothly as the levels near each other (W3c-2: switched on and off at the
  film, a lake slowly rising as a river fills it kept the water awake).
- The land beyond is a **basin** of `WATER_EDGE_BASIN` (256) tiles behind
  each edge tile *(tuning)*: what crosses moves its level by 1/256 of what
  it moves on the edge tile (its surface is kept in 1/256 of the fine unit,
  so nothing is lost to rounding).
- The land beyond **lives the weather of the edge**: every exchange on an
  edge tile (rain, the sun, the soil drinking, `water_exchange()`) is made
  beyond too, above its ground. Rain on a dry edge then lies alike on both
  sides and does not run out, and a lake lowered by the sun is not filled
  from beyond.
- `Water.edge_in` counts what came in less what went out: the total of the
  water on the map changes only by it.
- The tests' flat world seals its edges (`water_seal_edges()`): its lakes
  lie on flat ground, not at rest, and fed from beyond they would flood it.
  `bench_water` seals them too unless asked `open`, so that its cases stay
  closed boxes as they were made.

Why the basins, the film and the weather: with a fixed level beyond each
edge tile, the sun takes a few tenths of a millimetre more from one edge
tile than from the next, the levels beyond drift a few millimetres apart in
a day, and a lake cut by the edge then keeps a current running through it
for ever, in by one tile and out by another, never falling asleep. The film
stops the smallest differences, the weather keeps the land beyond with the
edge, and the finite basins even out what is left.
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

*As built (W3c-1, the bed):* `core/river.h` gives the course: the edge it
comes in by and the one it leaves by (the opposite one two times in three,
else a neighbouring one), where along them (within their middle 60 %), all
from the seed until the world map sets them (`RiverParams`, docs/WORLD.md
G0c); a cubic curve square to both edges, swung aside by a noise (at most
14 % of the map's smaller side, a swing every 70 tiles or so, none at its
ends), kept inside the map. World generation lays it from each column's
distance to the middle of the course: within half its width, the channel at
the lakes' level (deep water two levels down more than 1.5 tiles inside its
edge, shallows one level down: a stream of 3 tiles is all shallows), filled
at rest; a tile of sand along its banks; the walls of its valley rising a
level every `RIVER_VALLEY_TILES` (3) tiles from the banks, cut through the
hills (outcrops on the valley's floor become gravel). The New game window's
*River* is the channel's width (0 none, 3 to 12, 6 by default; `--river` on
the command line, none by default, so that tests and benchmarks are
unchanged). The flat world of the tests has none.

*As built (W3c-2, the current):* the columns of the edge across the channel
are a river's ends (`WaterEdgeKind`, `water_set_edge()`, set by
`game_init()`): beyond the edge it comes in by, the **source**, its level
held `RIVER_HEAD_MM` (100 mm) above the lakes'; beyond the edge it leaves
by, the **outlet**, held at the lakes' level. Neither moves with what
crosses, and neither has the film's dead band, so the river takes a gentle
slope from one to the other and flows on steadily (in a channel of 120
tiles, 500 mm at the source, 450 in the middle, 400 at the outlet). Water
comes in until the slope and the lakes it reaches are full, then the river
falls asleep along its whole length (§3.7, *Rest*): on a map of 200 tiles,
a day or two of the game with its lakes to fill, a few hours without; the
weather wakes it now and then, and it falls asleep again. Awake, a river
keeps 20 to 60 blocks working (about 5 µs each a step). The kinds of the
edge are saved.

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

*As built (W3c-3, the discharge over time):* the **stage**, how far above
the lakes' level the river stands beyond its edges (`RiverFlow`,
`Game.river`, `core/river.h`), moves its source and its outlet together;
the source stands above the outlet by its head (`river_head()`: 10 cm,
twice that a metre up). The stage is a seasonal part, ±20 cm around a
high in the middle of spring (the snowmelt) and a low in early autumn, plus
the rain over its catchment: the map's own rain (the same weather) fills a
quick store (the ground and the brooks, emptying in about 12 hours), which
fills a slow one (the river, about 60 hours), 15 mm of stage for each mm in
the slow store *(tuning)*. The river rises for about a day after a storm
and falls over days: a cloudburst of 60 mm raises it by 61 cm a day later,
half of that three days on; a rainy week by 10 to 15 cm; a wet spell of
storms by 1.6 m, to the brim of the shallows' banks (1.6 m above the
lakes' level); the average rain keeps it 6 cm up. Higher, it spills over
the plain at its level and drains back as it falls: 200 mm of rain over the
catchment raise a river of seed 11 by 2 m a day later and cover two thirds
of a plain of 96 tiles with 30 cm of water for a day, gone within twelve
hours once the river is back below its banks. Within −30 cm and +2.5 m. Both parts are times the river's
`RiverParams.discharge` (1 by default; the world map will set it, docs/
WORLD.md G0c). Every hour the stores take the hour's rain, and the ends
are set again (`water_set_edge()`) once the stage moved by 2 cm: the
water near the edges wakes, the river follows and falls asleep again
(about 10 ms of a game hour on a map of 64 tiles). Saved with the water.

### 4.3 Rain and snowmelt

- What reaches a floor (`docs/CLIMATE.md` §3.1: interception by crowns,
  nothing under roofs and in caves) goes into the soil up to its intake and
  saturation, as today. **What is left is free water on the tile** instead
  of being lost. On rock and on frozen ground, all of it.
- Rain on open water adds to it.
- Rain was gentle (0.25 mm an hour, storms 1.2), below every soil's
  intake. It never flooded anything. Storms get an intensity of their own,
  drawn per period: most of them 1 to 3 mm an hour, now and then a
  **downpour** of 10 to 30 mm an hour for one to three hours, more often in
  summer *(tuning)*. The rain of a season stays about what it is (the soil
  was tuned on it): the other rains get a little lighter.
- *As built (W3a):* rain 0.22 mm an hour; a storm 0.5 to 1.1 mm an hour,
  drawn per period (`weather_storm_rain()`); a downpour in 6 % of the storms
  of spring and autumn, 10 % in summer, 2 % in winter, of 10 to 20 mm an
  hour for 1 to 3 hours of the storm (`weather_downpour()`, named
  *Downpour* in the weather shown). What falls in a season stays within
  15 % of what it was (`test_season_rain_is_kept`). Lakes keep about their
  level over two years without inflow: about 40 mm lower each summer, most
  of it back by the end of autumn (−25 mm a year at seed 42).
- *As built (W3d, rain of every strength):* the rain is no longer a
  constant of the sky. Under a sky of rain or storm, each hour is wet or dry
  (showers and pauses: 35 % of the hours of rain are wet, 75 % of a
  storm's), and a wet hour's rain is drawn from a log-normal law, anywhere
  from 0.1 to 100 mm an hour: the rain of the period (an episode) times that
  of the hour, around a median of 0.33 mm an hour under a sky of rain and
  0.55 in a storm (×1.2 in summer, ×0.9 in winter) *(tuning)*. Most rain is
  light; 2 % of the wet hours of rain bring 4 mm an hour or more, 9 % of a
  storm's (11 % in summer); a storm reaches 30 mm an hour once in 250 wet
  hours, 90 once in 3 000. It rains in about 8 % of the hours (23 % before),
  harder, and a
  season gets about as much as before (`test_season_rain_is_kept`: within
  15 % of rain 0.25 mm an hour and storms 1.2 in every hour of their skies,
  on which the soil was tuned). From the seed and the hour alone
  (`weather_rain_at()`). The weather is named by how hard it falls under a
  sky of rain (*Drizzle* below 0.5 mm an hour, *Rain*, *Heavy rain* from 4,
  *Downpour* from 16, *Cloudy* in a dry hour), a storm is a storm but in
  its downpours, and the HUD shows the rain in mm an hour; the rain drawn
  over the view thickens with it. Settlers and fires get wet with the
  square root of the rain (soaked in an hour at 0.25 mm an hour, in 15
  minutes at 4, half as fast again in a storm's wind). `recent_rain` counts
  mm. The debug menu forces a rain (0.1 to 100 mm an hour, every hour).
  What it does: a rain of 5 mm an hour soaks into the soil (it takes 12 an
  hour) and raises a lake by its own rain; 20 mm an hour for three hours
  (60 mm) raises a lake of seed 42 by 17 cm (its banks' runoff), 10 cm
  once settled, and leaves puddles in the hollows of the land that soak in
  within hours. Its cost: rain heavier than the cracks of rough stone drink
  (1.5 mm an hour) lies on the stone floors of the hills and flows on as a
  sheet a few millimetres deep, which keeps up to 800 blocks awake during
  the heaviest hours (4 ms a tick; over a year of `bench_simulation 12 60
  1`, 48.6 → 54.7 µs a tick).
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
fixed set of tiles in the order of their indices, each from the water as it
is then (as built, §5.6: no cached heads; a flow never moves more than half
of what would even the two heads out, so the order does not make water
swing).

### 5.3 The water table

World generation lays a water table close to rest:

- under a lake or the river, at its surface;
- elsewhere flat, at the lakes' level (1.6 m under the plains), and never
  closer than that under the ground;
- blocks below it saturated, blocks above it at field capacity;
- a flat world has none: its single level holds field capacity.

*(As built: the first design raised the table under the hills at half their
rise. That table was far from rest, every hill drained at once and kept
300 blocks of surface water awake for the first days. The hills get their
own table from the rain instead, §5.4.)*

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

- **Digging.** A dug block leaves the pore water it held above its field
  capacity as free water in the new cell (what it held against gravity
  stays in the spoil: digging dry earth does not make a pond). A gallery dug below the water table fills at the seepage rate:
  slowly in granite (a drip), in hours in earth, very fast in gravel. A
  digging order on a block that would let water in shows a blue drop
  (`designation_lets_water_in()`, `groundwater_dig_floods()`): the water
  table stands 10 cm or more over the floor the gallery would have, or open
  water that deep lies beside it. The inspector tells how damp a block is
  (dry, damp, wet, soaked, from the share of its room).
- **Wells.** A shaft dug down to below the water table fills up to it: a
  well (drawing water from it needs a bucket, with containers, M4).
- **Cellars.** The water table lies at the lakes' level, 1.6 m under the
  plains (§5.3): a cellar dug one level down in the plains reaches 40 cm
  under it and floods to there in a day; one dug in a hill stays dry. *(A
  table falling away from open water was tried, so that cellars in the
  plains stay dry: it is not at rest. The beds of sand and gravel carry the
  lakes' level under the whole plain within days, at a high cost. Keeping
  water out of a cellar, lining it with clay or draining it, is for W5.)*
- **Mud.** A floor of soil, rich soil or silt (not sand nor gravel) whose
  root zone reaches moisture 1.8 turns to mud, firm again below 1.6, never
  while frozen (`Map.mud`, set by the soil each hour, `soil_settle_mud()` at
  the start): walked at ×1.3 *(tuning)*, drawn as a wet brown veil with a
  few glistening puddles, told by the inspector. A few hours of heavy rain
  turn most of the soil to mud; it firms up within half a day once it stops,
  but in the wet hollows.
- **Views.** The moisture view (key K) shows the blocks too: the walls of
  galleries on the level shown, and the inside of the ground when the
  view cuts through it.
- **Later**: soaked earth caving in more easily, deep roots of trees
  drawing from the blocks, frost heave.

### 5.6 As built (W4), and what is left

`core/groundwater.h` and `Map.pore` (a `uint16_t` per tile, in 1/64 mm,
`MAP_PORE_FINE`), saved with the map:

- **Head.** A tile at its field capacity or below has its bottom as head.
  Above, the water table runs up the column while the tile above holds more
  than its capacity; the head is the top of that run: the share of the top
  tile's drainable room filled, of its height, or when it is full its top
  plus the free water standing on it.
- **The hourly pass** (`groundwater_update()`, in the slices of the soil):
  a tile drains down at the lower conductivity; trades with its east and
  south neighbours (Darcy, the difference of the heads over the width of a
  tile, times the height of water in the face; the yields of the giver's
  top of table and of the receiver's tile turn the water moved into a
  change of head); with the open tiles beside it (not walls or closed
  doors); with the edge (`edge_head`, the table it had when the game began,
  per edge slot).
- **Taking and giving** follow the column: water is taken from the top of
  the table down and given into the tile then up its column, both limited
  by the vertical conductivity of the tiles crossed. Water does not jump
  through granite. A full column gives the rest out at its top.
- **Seeping out.** Into a tile without floor it runs down to the foot of
  the cliff (a spring on a hillside falls to its foot instead of waking a
  block of empty air). Onto rooting ground holding less than 3 mm of free
  water it wets the root zone first, up to saturation. The rest lies as
  free water.
- **Lakes asleep.** What seeps into or out of the open water of a sleeping
  block is kept per block (`pending`) and spread alike over its tiles of
  shallows or deeper at the end of the hour: a lake fed along its shores
  rises as a whole and stays asleep.
- **The root zone** (§5.4, `core/soil.h`) drains into the block under its
  floor, as far as that block has room, at the lower of the drainage and
  the block's conductivity. It is drawn up when the block's drainable share
  passes half (`SOIL_RISE_FROM`): its target moisture rises from field
  capacity to saturation as the table reaches the floor, by
  `SOIL_RISE_RATE` of the gap an hour. The banks of `docs/CLIMATE.md` §3.5
  are kept only where no porous block lies under the floor (a flat world).
  Rain soaking into the cracks of rough stone goes into the block under it.
- **The inside of the ground** (`worldgen`): earth near the surface, a bed
  of sand or gravel three levels down where a noise is high (an aquifer),
  gravel under gravel, sand under sand, lenses of clay two levels down,
  stone deeper. Sand, gravel and clay are dug like earth (`rock_is_soft()`),
  with sprites of their own; they give nothing yet.
- **Shown**: the inspector's ground water row on a solid tile (dry, damp,
  wet, soaked, with the share of its room).
- **Layers written directly** (tests, scenarios) move no water:
  `map_topology_changed()` brings every pore within the room of its rock.
- **The edges.** Free water the ground gives or takes on an edge tile goes
  through `water_exchange_here()`: the land beyond does not get it too, as
  it gets the weather (`water_exchange()`). It did at first: a spring on an
  edge tile was copied into the basin beyond, which poured it back, and
  water came in without end (the free water of a map doubled in ten days).
- **W4d**: mud (§5.5), the moisture view colouring the porous blocks on the
  level shown on the same scale as the soil (`groundwater_moisture()`: 0
  dry, 1 at field capacity, 2 full: the walls of galleries, the inside of
  the ground where the level cuts it), the blue drop on digging orders that
  let water in.

Cost (`bench_simulation 12 4 1`, against W3c-3 on the same machine): 8.4 s
of processor time instead of 7.25 s (+16 %), p50 24 µs a tick instead of
18. The hourly pass over every solid tile is most of it: tiles holding
nothing above their field capacity beside others alike are skipped, but a
saturated tile still works out its head by walking up to the top of its
table, for each of its neighbours. The surface water costs less than before
(932 blocks woken instead of 1 232): springs fall to the foot of their cliff
and lakes are fed per block. Left to try if it matters: a head per column
and hour, worked out once.

Left for later: drawing water from a well (a shaft filling is already
there; drawing needs containers, M4), items from clay, gravel and sand (with
pottery and paths), keeping water out of a cellar (W5), muddy feet carrying
dirt indoors.

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
- Debug tools (F4): take a block away and pour water on a tile (10 cm a
  click, Shift to take it away), both done; later a downpour and a flood of
  the river.

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

`Item.moisture`: a `uint16_t`, the share of the item's mass that is water,
in steps of 1/10 000 (`ITEM_MOISTURE_UNITS`; coarser steps would round a
log's hourly drying to nothing), stored + 1. Only items of materials that
take water have one. Stones, flint and bodies have none (always 0, never
updated). A stack built by hand with 0 gets the type's reference moisture:
fresh for food, else the equilibrium at 65 % of relative humidity, what the
type's weight is given at.

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
  later if needed. Recipes of fibres and containers (`docs/CONTAINERS.md`
  §9.2) want an ingredient **dry**, **green** (still near its fresh
  moisture: rods woven as cut) or **wet** (rods soaked, bark softened).
- **Retting** (`docs/CONTAINERS.md` §4.4): nettle stems and lime bark ret
  while wet and warm, from rain, from the dew of humid nights (a small
  wetting at night, to come) or lying in shallow water.
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

### 7.6 Types that went away

`ITEM_FRESH_GRASS` and `ITEM_WET_TWIGS` (and `ItemDef.transforms_into` and
`wets_into`, which only they used) are gone. Their uses (`gather_zone.c`,
`tree.c`, `map.c`'s twig counts, `render.c`, `sprites.c`) read grass and
twigs with their moisture. The stable ids `fresh_grass` and `wet_twigs`
disappeared (no saves yet).

### 7.7 As built, and what is left

- A change of moisture alone is no change of the tile (no new stamp): the
  picture of the known world keeps the moisture it saw until the tile
  changes otherwise. Carried and held items keep theirs (they do not take
  the air of their holder's tile yet).
- Not yet: the skip of items settled at their target (every item that takes
  water is visited each hour, as rotting food was), a lighting chance that
  falls with the kindling's moisture, sorting by moisture, a "dry only"
  choice for bills and stockpiles (no recipe takes grass yet), hauling
  things out to dry.

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

- The cost of a flood across the whole plains of a 256 × 256 map: measured
  at W2, 1 ms a step for a whole level awake (§3.7): a step every tick for
  now. Whether blocks far from view can step less often (they must keep the
  same results: same steps, only batched) if real floods prove costly.
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
2. **W1b, moisture of items** (built). §7: `Item.moisture`, `ItemWater`, the
   hourly drying and wetting, fire, building, rot, weight, the item window,
   the tinted sprites. `ITEM_FRESH_GRASS` and `ITEM_WET_TWIGS` go. Tests:
   grass drying into hay in the sun and not on wet ground, twigs soaked in
   the rain, tinder refused when wet, the weight.
3. **W2, surface flow** (built, §3.7). §3: falling, the pipes, films, slopes, active
   blocks, rest, the class changes logged with their minimum time, a being
   walking out of rising water. The debug tools to pour and empty water.
   Tests: mass kept, determinism, a channel dug from a lake filling it to
   the lake's level, a shaft filling from the bottom, water not crossing a
   wall or a closed door, a lake at rest staying asleep (no block active),
   a waterfall. A benchmark (`bench_water`: a dam opened, a channel, a
   flooded plain) and `bench_simulation` unchanged in cost with lakes at
   rest.
4. **W3, where water comes from.** §4, in three steps:
   - **W3a, exchanges (done).** Rain beyond the soil's intake as free
     water, evaporation, soaking in, frozen ground, cracks in rough stone,
     storms of various intensity and downpours, snowmelt running off (§3.5,
     §4.3). Tests: a downpour lying on the ground and soaking in the hour
     after, frozen ground, puddles on stone drying, rain on lakes and ponds,
     exchanges waking only water they set moving, a season's rain kept.
   - **W3b, the edges (done)** (§4.1): the land beyond, its basins and its
     weather, sealed edges for the tests' flat world. Tests: a lake cut by
     the edge keeping its level, a flood flowing out at a dry edge, no
     water crossing under the ground beyond or sealed edges, basins apart
     evening out, the weather shared, a save made while water flows out
     playing on alike.
   - **W3c, the river** laid by world generation (a New game option) with
     its discharge and its floods, in three steps:
     - **W3c-1, the bed (done)**: the course as parameters (`core/river.h`),
       the channel, banks and valley, the New game option. Tests: the edges
       from the seed, the course joining its edges, the channel at the
       lakes' level, the valley's floor, the river lying at rest in a game.
     - **W3c-2, the current (done)**: the level held higher beyond the
       upstream end, the downstream end an outlet, and blocks that fall
       asleep when nothing changes any more in them, not only when nothing
       flows (a steady river would keep its blocks awake for ever). Tests:
       a steady current taking its slope and falling asleep, a lake still
       draining through a slow current, a river in a game flowing then
       falling asleep, a save made while it flows playing on alike.
     - **W3c-3, the discharge over time (done)**: the seasons, the rain of
       the last days over the catchment, floods spilling over the banks.
       Tests: the seasons, rain raising it after a while and the fall after,
       a wet spell's flood, the slope with the stage, a storm upstream
       raising the river all along a map.
5. **W4, water in the ground.** §5: the pore water of every block, the
   new layers (sand, gravel, clay), the water table laid by world
   generation, drainage and Darcy flow, seepage, the root zone draining into
   the ground and drawing from it (the banks of the soil go), damp walls,
   wells, mud. Tests: a gallery under the water table filling, one in
   granite dripping, rain soaking down to the water table, a spring on a
   hillside, a well. In four steps:
   - **W4a, the ground (done)**: `Map.pore`, sand, gravel and clay (dug
     like earth, their sprites), the layers and the water table laid by
     world generation, digging releasing what is above field capacity.
     Tests: the rooms, the layers, a table laid and its heads, digging.
   - **W4b, the flow (done)**: drainage, Darcy flow, seepage, the edges,
     springs falling to the foot of their cliff, lakes fed per block.
     Tests: soaking down and stopping over clay, tables evening out, a
     gallery filling, one in granite dripping, a spring, a well, a soaked
     ceiling, no seepage into walls, the edges, determinism.
   - **W4c, the root zone (done)**: draining into the ground, capillary
     rise, the banks only over no ground, cracks into the block under them,
     the inspector's row. Tests: the root zone draining, a high table
     keeping the ground wet.
   - **W4d, what it changes (done)**: mud, the moisture view of the
     blocks, the diggers' warning; a spring on an edge tile no longer
     copied beyond the edge. Tests: soaked soil turning to mud and firming
     up, frost, sand; the moisture of a block; digging that lets water in
     and the block each order digs; the table at the lakes' level. Wells
     (drawing) and clay and gravel items later (§5.6).
6. **W5, water in play.** Floodgates, dams of earth, cisterns, irrigation
   ditches (with farming), floating items carried away, alerts (the river
   rising, a gallery flooding), settlers keeping out of rising water.
7. **W6, later.** Pressure, swimming and drowning, ice (§8).
