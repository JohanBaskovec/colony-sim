# The world map

Design of the world a game takes place in (long term, nothing built yet).
Today a new game generates a single map. Later, a new game generates a
whole **world**: poles in the north and the south, a hot middle, mountain
ranges, seas, rivers. The player picks one square of it, and that square
becomes the map played, as today.

The world comes late: the rest of the game comes first. What this document
fixes now is the **contract** between a square of the world and the map
generated from it, so that the work done on the map in the meantime (the
river of W3c, the latitude, the climate) takes the shape the world will
feed later, and nothing is thrown away.

## 1. Decisions

- **Square cells**, one per possible map (the map is square).
- **No wrap-around**: the world has edges; it is surrounded by ocean
  (§3.1). The poles are its north and south rows.
- **The cell decides** the map: relief, water, sea, river, climate. Until
  the world exists, the New game window keeps its manual settings (relief,
  water...), which become the inputs of the map's generation that a cell
  sets later.
- **Every climate is playable** from the start, even where nothing the
  game knows yet can live (a settler at the pole may die at once). Content
  for the cold, the dry and the hot comes over time.
- **Determinism**: the world comes from its seed alone, a map from the
  world's seed and its cell's coordinates. Same seed, same world.
- Later, the world is played on: travelling, settling elsewhere, villages
  of other people, traders and raiders (§7). The design leaves room for it.

## 2. Scale: real size, compressed latitude

A tile is 1 m (`GAME_DESIGN.md`), a map up to 256 × 256 m. A real planet
from pole to pole would take tens of thousands of such maps. The world is
therefore **a small world**:

- **Cells join at real scale.** Each cell is a map's worth of land, and
  neighbouring cells meet edge to edge: the relief, the water and the
  rivers of a map go on seamlessly into the next one (§4.3). Walking off
  the east edge of a map (later) leads onto the west edge of the next.
- **The latitude is a rule of the game, not a distance.** It goes from
  90° south on the bottom row to 90° north on the top row, evenly by row,
  whatever the metres. With 32 rows that is about 5.6° a row; the
  temperature changes by a degree or two from a cell to the next, which
  nobody notices on foot, and the whole range from the polar night to the
  tropics fits in 32 maps (8 km).
- The sun's course, the day lengths and the seasons of a map follow its
  cell's latitude (`core/sun.h` already takes a latitude; SHADOWS.md step
  7), beyond the polar circles too (a sun that does not set in summer nor
  rise in winter).
- **Travel times on the world map are a rule too** (later, §7): crossing a
  cell can take a day of the game on the world map, without anyone
  walking 256 m in a day.

Size: about 64 × 32 cells by default *(tuning)*, maybe a New game choice
(small, medium, large world). The world is never generated tile by tile:
only its cells' summaries (§3), and the one map played (or a few, later).

## 3. Generating the world

Each step works on the grid of cells, coarse and fast (a fraction of a
second for the whole world):

1. **Land and sea**: continents from noise (or a few plates), the share of
   sea a world option; ocean all around the edges.
2. **Elevation**: mountain ranges along the plates' meeting lines (or ridges
   of noise), hills, plains, lowlands; the height of each cell and its
   roughness.
3. **Temperature**: from the latitude (cold poles, hot middle), lowered by
   height, softened near the sea (milder winters, cooler summers); the mean
   of the year and the swing between summer and winter. A world option
   makes the world warmer or colder.
4. **Winds and rain**: prevailing winds by latitude band (westerlies in the
   middle latitudes, trade winds near the equator), carrying the sea's
   moisture inland, rain falling on the windward side of mountains and a
   dry rain shadow behind them; deserts where it is hot and dry.
5. **Rivers**: water runs down the elevation from cell to cell to the sea
   (pits filled into lakes first); each cell's discharge sums the rain of
   all the cells upstream; the larger flows become rivers, the others
   streams.
6. **Biomes**: from temperature and rain (ice cap, tundra, boreal forest,
   temperate forest, grassland, steppe, desert, savanna, tropical forest,
   wetland), named for the player.
7. **Sites** (later, §7): villages, ruins, the lairs of dangerous animals.

### 3.1 The edges of the world

The world is an island (or islands) in an ocean that covers its border:
its edge cells are sea, and nobody needs to know what lies beyond.

## 4. From a cell to a map

### 4.1 What a cell gives the map

A summary per cell, which the map's generator takes instead of the New
game sliders (the contract):

| Value | Used for |
|---|---|
| Latitude | The sun, the day lengths, the seasons |
| Mean temperature and seasonal swing | The weather, the soil, the plants |
| Rain over the year | The weather's rain, the soil's water |
| Height and roughness | The relief (today's Relief slider) |
| Relief at each edge | So that the land rises towards a neighbouring range |
| Lakes | Today's Water slider |
| Sea, on which edges | A coast along one or two sides (§4.4) |
| River: edge in, edge out, where along them, discharge | The river (W3c) |
| Biome | Which plants and animals, the ground |

### 4.2 The map's generator

The map is generated as today, from parameters. Each value of §4.1 is, or
becomes, a parameter of `WorldGenParams` that the New game window can set
by hand in the meantime; the world will only fill them in. That is the rule
for the work done before the world: **any new map feature that the world
will one day decide is built as a parameter first** (the river's edges and
discharge in W3c, the latitude, the climate's means).

### 4.3 Neighbours join

For a map to meet its neighbours seamlessly (later, when walking from one
to the next), its generation uses world coordinates: the noise of the
relief is sampled at `cell × map size + tile`, so that two neighbouring
maps see the same land along their shared edge, and the cell's summary
only steers it (the mean height, the roughness). A river leaves a map at
the point where it enters the next one: each edge crossing is a value of
the world (which edge, how far along), not of either map.

### 4.4 The sea

A coast cell gets the sea along one or more edges: the land slopes down to
a beach and the seabed, the sea's level held at the map's edge (the edges
of W3b, a basin whose level does not move: the sea). Tides later, maybe.
Salt water (not drinkable) is new to the game.

## 5. The New game screens

1. **World**: the world's options (seed, size, sea, mountains, warmer or
   colder), *Generate*, and the world drawn as a map (biomes, relief
   shaded, rivers, seas, the poles' ice), with an overlay for temperature
   and rain. A click on a cell shows its card: biome, summer and winter
   temperatures, rain, relief, lakes, coast, river and its size, the length
   of the shortest and longest day.
2. **The map**: the settings of the map as today (colonists, wild animals,
   the map's size...), the relief and water set by the cell, and a preview
   of the map before starting (already planned in the ROADMAP).

## 6. Saving

The world is regenerated from its seed: a save keeps the seed, the world's
options, the cell played and, later, what changes on the world (§7: the
villages, the cells explored, other colonies). The map played is saved as
today.

## 7. Later: the world played on

- **Travelling**: a group leaving the map for another cell, days on the
  world map (§2), arriving on that cell's map.
- **Settling elsewhere**: a second colony on another cell; only the map in
  view is simulated in full, the others summed up while away *(to design)*.
- **Villages of other people**, traders and raiders coming from a
  direction (the cells around: a river valley, a pass), with what their
  cell grows and makes.
- Events of the cells nearby (a fire in the forest upwind, a flood coming
  down the river from upstream: the river's discharge already follows the
  rain over its catchment, WATER.md §4.2).

## 8. Steps

Before the world (any time, each useful on its own):

- **G0a, latitude per game** (SHADOWS.md step 7): the latitude a parameter
  of the map, the sun and the day lengths worked out from it; a New game
  option.
- **G0b, climate per game**: the mean temperature, the seasonal swing and
  the rain parameters of the weather instead of constants; New game
  options (later the cell's values).
- **G0c, the river as parameters** (W3c): edge in, edge out, where along
  them, discharge.
- **G0d, a coast**: the sea along chosen edges of the map (W3b's edges with
  a fixed level), salt water.

The world itself (late, once the rest of the game stands):

- **G1, the world in the core**: generation steps 1 to 6 (§3), deterministic
  and tested (rivers reach the sea, the poles are cold, rain shadows), a
  cell's summary (§4.1), a benchmark.
- **G2, the World screen** (§5): the world drawn, overlays, the cell's card,
  the choice of a cell.
- **G3, the map from its cell**: the summary fed into the map's generator,
  neighbours joining (§4.3), saving (§6).
- **G4, content for every climate**: snow and ice (WATER.md W6), plants and
  animals of the cold, the dry and the hot.
- **G5, the world played on** (§7).

## 9. Open questions

- **Mountains**: levels are 2 m and maps about 32 levels high ("hills but
  no mountains", ROADMAP): a mountain cell gives the most rugged hills the
  levels allow, or the cap on levels is raised for it.
- The world's size and the number of rows from pole to pole *(tuning)*.
- Islands and seas inside the world (a strait between two continents).
- What a map is made of at the edge of a biome (a forest thinning into
  steppe across neighbouring cells): blend with the neighbours' biomes.
