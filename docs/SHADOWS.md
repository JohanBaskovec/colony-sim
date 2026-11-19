# The sun and cast shadows

Today the sun has a strength but no direction: the light of a tile is the
sky times what lies straight above it (`map_light_cover()`), as if the sun
always stood overhead. A wall shades nothing beside it, the north side of a
cliff is as bright as its south side, and the shade on screen is one value
per tile corner, smoothed.

This document is the design of a sun that stands somewhere in the sky and of
the shadows the solid blocks of the world cast, first on screen, then in the
simulation (plants, soil, temperature).

Status: step 1 is implemented (`core/sun.h`: the sun's direction); the
rest is design. Values marked *(tuning)* are first guesses.

## 1. Principles

- **A real sun.** It rises in the east, stands in the south at noon and sets
  in the west; it is high in summer and low in winter. Its direction comes
  from the hour, the day of the year and the latitude of the map, nothing
  else.
- **Shadows worked out in the volume.** A point is in the sun when a straight
  line from it to the sun leaves the map without meeting anything solid. A
  hill shades the valley behind it, a cliff the ground at its foot, a wall
  the tiles beside it, across levels.
- **Only the blocks of the map cast shadows.** Rock and ground, walls, doors,
  roofs, trunks, and the foliage of crowns for its share (it is in the map's
  grid, and trees shade today). Settlers, animals, items, low plants and
  furniture cast none: they are not blocks. They receive shade like the
  ground they stand on.
- **Finer than a tile.** The edge of a shadow crosses tiles: a tile may be
  half in the sun and half in the shade, and the edge moves through the day.
- **Shadows glide.** On screen a shadow never jumps: its edge moves
  continuously with the sun, at any game speed (§4.1).
- **Shade is not darkness, but a closed room is dark.** The sky lights what
  the sun does not reach: a shadow takes the direct sun away and leaves the
  light of the sky. That light comes in through openings only: a hut with
  its roof, its walls and its door shut is dark at noon, lit by its fire; a
  shelter open on one side is bright; a cave darkens from its mouth inwards.
- **A tile partly in the sun is partly lit.** For the simulation too: a
  plant on a tile a fifth in the sun gets a fifth of the direct sun, not
  none.
- **On screen first, in the simulation later.** The screen needs fine shadows
  that move smoothly; the simulation needs one value per tile and hour (the
  share of the tile in the sun), which must be deterministic. They share the
  same sun and the same rays.

## 2. The sun in the sky

### 2.1 Where the map is

- **North is the top of the screen** (as the settlers' headings already say:
  `HEADING_SOUTH` faces the bottom).
- **Latitude 45.6° north, for now.** It is not a new choice: it is the
  latitude at which the days already last what `core/game_clock.h` says, 8.5
  hours at midwinter and 15.5 at midsummer (`GAME_DAY_LENGTH_WINTER`,
  `GAME_DAY_LENGTH_SUMMER`).
- **The latitude belongs to the world, not to the code.** It is a value of
  the game (set when the world is made, saved with it), and everything about
  the sun is worked out from it: its course, and the day lengths, which stop
  being two constants. Later the player chooses where to settle on a map of
  the world when starting a game (§6, step 7): far north, long shadows,
  short winter days and long summer evenings; near the equator, a sun nearly
  overhead and days of twelve hours all year; south of it, the sun in the
  north and the seasons the other way round; beyond the polar circles, a
  sun that does not set for weeks in summer (circling the sky, the shadows
  turning all the way round) and does not rise in winter, which sunrise,
  sunset, the day on screen and the plants' light must allow for (a day of
  0 or 24 hours). The settlers keep to their schedule and their needs
  whatever the light. The climate of the place
  (temperatures, rain, the plants that live there) goes with that choice and
  is a design of its own.

### 2.2 Its course

The sun's declination goes from about -23.4° at midwinter to +23.4° at
midsummer over the year (60 days), 0 at the equinoxes. It is the one that
gives the day lengths of `core/game_clock.h` at the default latitude
(`cos h0 = -tan φ tan δ`, the sun up for `2 h0 / 15°` hours), so that
sunrise and sunset stay exactly where `game_sunrise()` puts them. With the
hour angle `h = 15° × (hour - 12)`, the latitude `φ` and the declination
`δ`, the direction towards the sun in the local frame is

    east  = -cos δ sin h
    north =  cos φ sin δ - sin φ cos δ cos h
    up    =  sin φ sin δ + cos φ cos δ cos h

with `sin(elevation) = up` and the azimuth `atan2(east, north)` (0 north, 90
east, 180 south): as vectors, nothing breaks at the poles or at the zenith.

What it gives at noon, for a wall 2 m high (one level):

| Day        | Elevation at noon | Shadow of a 2 m wall |
|------------|------------------:|---------------------:|
| Midsummer  | 67.8°             | 0.8 m, to the north  |
| Equinoxes  | 44.4°             | 2.0 m                |
| Midwinter  | 21.0°             | 5.2 m                |

In the morning shadows point west and are long, shorten until noon, then
turn east and lengthen. In summer the sun rises north of east: at dawn and
dusk the north faces of walls are lit for a while.

### 2.3 A low sun

At sunrise and sunset shadows are endless, and the direct sun is weak
(`game_sunlight()` already follows the sine of the sun's course).

- Rays are followed over at most `SUN_SHADOW_REACH` tiles *(tuning: 40)*,
  or until they leave the top of the map.
- Under an elevation of `SUN_LOW_ELEVATION` *(tuning: 8°)* the cast shadows
  fade out with the direct light: at dawn and dusk the world is lit by the
  sky alone, evenly, as today.

### 2.4 The sky

    light = sky × (diffuse × open + direct × lit)

- `sky` is what the weather lets through (`weather_light()`), as today.
- `diffuse` is the share of the light that comes from the whole sky,
  `direct` the share that comes straight from the sun (`1 - diffuse`).
  Under a clear sky `diffuse` is about 0.35 *(tuning)*; under clouds, fog,
  rain or snow it goes to 1: an overcast day has no shadows.
- `lit` is how much of the direct sun reaches the point (§3.1), 0 .. 1.
- `open` is how much of the sky the tile sees (§3.2), 0 .. 1.

At night there is no direct light: the night sky (`NIGHT_SKY_LIT`) times
`open`, and the fires, whose light stops at walls too (§3.3). Moonlight as a
second sun is not planned (§7).

## 3. What stops the light

Light travels in straight lines through the grid of the map (tiles 1 m wide,
levels 2 m high, as the lines of sight of `core/vision.h`: the same walk
through the volume). Each tile a ray crosses takes its share:

| What the ray crosses                              | Lets through           |
|---------------------------------------------------|------------------------|
| Rock, the ground of a level, a trunk (`ROCK_WOOD`) | nothing                |
| A wall, a closed door                              | nothing                |
| An open door; a window (later)                     | everything             |
| A built roof (thatch, the only one so far)         | nothing                |
| Foliage, by its density and the season             | `MAP_LIGHT_FOLIAGE_*`, the bare-branch shares in winter |
| Open space, low plants, items, furniture, beings   | everything             |

The shares multiply along the ray, as the layers of a crown do today; the
ray stops once nothing is left.

What a block lets through is a property of the block: foliage is the only
one that lets some light through today. A thatched roof lets none through: a
roof that keeps the rain out keeps the light out. `MAP_LIGHT_ROOF` (the 5 %
of today, which stood for the daylight coming in under a roof from its
sides) goes: that daylight is now worked out (§3.2). A roof of another kind
(loosely laid branches, later) would have its own share, in its
`StructureDef`.

**Doors.** A door lets the light through when it is open and stops it when
it is closed, as a real one does. Doors have no such state today: a door is
a screen of branches that slows whoever goes through, and it always stops
sight (`core/vision.h`). They get one: open while somebody stands in the
doorway, closed behind them, and a setting to keep a door open (to light and
air a hut by day). An open door is an opening for everything, not for the
light only: one sees through it (`core/vision.h` uses the same state), the
warmth of the room goes out by it, and animals walk in, which a closed door
keeps out.

**Windows** (later, a structure of their own): an opening in a wall that
lets the light and the sight through, not the people.

### 3.1 The sun's rays

`lit` at a point is what is left of a ray from that point towards the sun.
What follows by itself:

- A roof shades what is under it, but a low sun comes in under its edge and
  through an open side: a patch of sun on the floor of a shelter open to the
  south.
- A tree casts the shadow of its crown beside it, not only under it, longer
  in winter though thinner (bare branches).
- A pit or a narrow yard only sees the sun around noon.

A ray starts a little above the surface it lights: the floor of a tile, or
the top of a wall or of a rock, lit like any ground at its height.

### 3.2 The light of the sky

`open` is how much of the sky a tile sees: from the middle of its floor,
`SKY_RAYS` rays *(tuning: 16)* go in fixed directions spread over the dome of
the sky, none lower than 20° over the horizon, each weighted by the light a
flat ground takes from its part of the sky (more from above than from the
sides). `open` is the weighted mean of what the rays let through, followed
over `SKY_REACH` tiles at most *(tuning: 24)*.

It replaces today's `map_light_cover()` (what lies straight above the tile)
in the light of a tile. What follows by itself:

- **A closed hut is dark**: no ray leaves it. Its fire lights it.
- **An opening lets the day in**: an open side, a door that is open, a hole
  in the roof light the tiles that see the sky through it, less and less
  away from it.
- **A roof without walls is bright under it**, a little less than the open
  ground: only the sky straight above is hidden.
- **A cave darkens from its mouth inwards**, over a few tiles, instead of
  being black from its first tile.
- **The foot of a cliff, a narrow yard, the edge of a wood** are dimmer than
  the open ground even out of any shadow: half their sky is hidden.
- **Under a crown** the light is what the foliage lets through from above
  and what comes in from the sides: brighter at the edge of a wood than at
  its heart, brighter in winter.

`open` does not depend on the sun: it only changes when the map does
(`Map.light_revision`: terrain, rock, roofs, walls, crowns and their leaves).
It is kept per tile and worked out again around what changed, within
`SKY_REACH`, in slices; the leaves coming and going over the whole map are
spread over a day *(tuning)*. One value per tile is enough, also on screen:
the light of the sky is soft, it has no edges.

### 3.3 The light of fires

Today the light of a burning hearth only depends on the distance
(`hearths_light_at()`, fading over `HEARTH_LIGHT_RADIUS`, 6 tiles): it goes
through walls. A fire inside a closed hut glows on the ground outside, and a
fire outside lights the inside of the hut. With huts that are dark by day
this would show at once.

The light of a fire follows the same rule as the sun's: a point is lit by a
fire when a straight line from the point to the fire meets nothing that
stops the light (the table above). What follows by itself:

- a fire lights its room and nothing behind the walls;
- through an open door or a window, its light falls outside as a wedge;
- a wall, a trunk or a rock near a fire outdoors has a shadow behind it,
  pointing away from the fire;
- the light goes up and down where nothing is in the way (a fire at the foot
  of a drop lights the edge above), instead of staying on its level.

It is worked out at the points of the fine texture (§4), since these
shadows have sharp edges, for the points within the fire's radius only, and
kept until the fire is lit or goes out or the map changes within its radius
(a door opening): fires do not move. Rays of 6 tiles at most.

The warmth a fire radiates already stops at walls (`hearths_radiant_warmth()`).

## 4. On screen

The veil of `render_daylight()` stays (its colors by day, dusk and night,
the fires, the smoothing from tile to tile, the floor that keeps the darkest
places readable): what changes is what it is made from. The light of the sky
(`open`) is a value per tile, smoothed as the shade is today; the sun's
shadows are finer.

- **A shadow texture finer than the tiles.** For the tiles in view, `lit` is
  worked out at several points per tile, `SHADOW_SAMPLES` a side *(tuning: 8
  when zoomed in, 4, 2 then 1 as the view zooms out and a tile becomes a few
  pixels)*, and drawn smoothed: the edge of a wall's shadow is a straight
  line across the tiles, softened over a fraction of a tile, like a real
  penumbra.
- **Each surface seen at its own height.** On the level shown: the floor of
  open tiles, the top of walls and rock. Through open space, the ground seen
  below (`map_view_level()`), in the shadow of the cliff it lies under.
- **What stands on a tile is shaded where it stands**: a settler, an animal,
  an item or a plant takes the shade of its own spot, not of its tile, so
  somebody walking out of a shadow comes into the light as they cross its
  edge.
- **What the colony knows.** Shadows are worked out from the known world
  (`game_view_map()`), so that a wall nobody has seen does not give itself
  away by its shadow.

### 4.1 Shadows that glide

Working every shadow out again for every frame would be wasteful; working
them out now and then would make them jump. Instead, the rays are cast
ahead of time and the frames only read the result:

- **Two moments, half an hour of game time apart.** For each point of the
  texture, `lit` is worked out for the sun where it stands at the start of
  the half hour and where it will stand at its end (`SHADOW_STEP_MINUTES`
  *(tuning: 30; less when the sun is low and shadows run fast)*).
- **When the edge will pass.** A point that is in the sun at one moment and
  in the shade at the other is one the edge of a shadow crosses during that
  half hour: a few more rays, halving the time between the two, find the
  moment it does. The point keeps that moment (and what it is lit as before
  and after: foliage gives values between 0 and 1). The middle of the half
  hour is tested for every point too, so that a narrow shadow (a trunk's)
  passing wholly over a point within the half hour is not missed.
- **Every frame, no ray:** a point is lit as before or as after according to
  the game's time now, with a short blend around its moment (the width of
  the penumbra). Each point changing at its own moment, the edge moves
  across the tiles continuously, at ×0.3 as at the fastest speed, and stands
  still when the game is paused.
- **The real work again every half hour of game time**, a little ahead of
  its end, for the half hour that follows, a few pieces per frame.
- **At once where the map changes.** A wall built, a tree felled, a door
  opened: the pieces within reach of the change (`SUN_SHADOW_REACH`) are
  worked out again without waiting (`Map.light_revision` tells when), so a
  new wall has its shadow as soon as it stands.

The texture is kept per piece of the world (`SHADOW_CHUNK` tiles a side
*(tuning: 16)*); a piece that comes into view is worked out when it does.

### 4.2 Cost

- Rays: a view of 30 × 20 tiles at 8 points a side is about 40 000 points;
  each takes three rays per half hour of game time (21 seconds at ×1), a few
  more for those an edge crosses. Spread over the frames, the ones in view
  first, within a budget per frame.
- Frames: a comparison and a blend per point in view.
- At the fastest speeds the half hours follow one another quickly: the
  budget per frame holds, and the step is worked out further ahead.
- To be measured with `--bench-render` before and after; the frame time of
  the fixed views must not suffer.

## 5. In the simulation

The simulation needs values per tile, not a picture: how much of the sky the
tile sees, and how much of the tile is in the sun during this hour.

### 5.1 What is stored

- `open`, a byte per tile (0 .. 255): the light of the sky (§3.2), from the
  real map.
- `direct`, a byte per tile (0 .. 255): the share of the tile in the sun
  now, the mean of `lit` (§3.1) over points spread across the tile's floor,
  `SUN_TILE_SAMPLES` a side *(tuning: 4, sixteen points)*: a tile whose
  fifth is in the sun gets 51, not 0. Worked out from the real map once an
  hour of game time (decided; three times a day, morning, midday and
  afternoon, if the hourly pass turns out to cost too much), in slices over
  the ticks like the soil's pass (`core/soil.h`). A tile whose `open` is 0 is not worked out: no sky, no
  sun.
- `sun_hours`, a byte per tile: the hours of direct sun of the day so far
  (an hour half in the sun counts half), kept for the day before too: what
  the player reads ("6 h of sun yesterday") and what slow rules use (§5.2).
- All are derived from the map and the clock, by a schedule that depends on
  the tick only: deterministic, rebuilt after loading, and not hashed
  themselves (what they change is).

Cost. Most tiles are wholly in the sun or wholly out of it; only the tiles a
shadow's edge crosses need all their points:

- a tile first asks five points (near its corners and its middle); when they
  agree, that is its answer; when they do not, the sixteen;
- a map of the highest block of each piece of the world lets a ray stop as
  soon as it is above everything ahead of it: over open ground most rays end
  at once.

As an order of magnitude: 40 000 tiles under the sky on a 200 × 200 map, a
few short rays each, once every 2 500 ticks: one to a few microseconds per
tick, against about 20 for the whole simulation today. To be measured with
`bench_simulation`; if it is too much, fewer points first, then three
passes a day instead of one an hour.

### 5.2 Who reads it

The light of a tile becomes the formula of §2.4 everywhere
(`game_light_at()`): `sun × sky × (diffuse × open + direct × lit)`, `lit`
being the tile's `direct`, instead of `sun × sky × cover`.

- **Plants** (`docs/PLANTS.md` §5): growth and the darkness that kills follow
  the new light. A plant is not a point: it takes the light of its whole
  tile. On the edge of a shadow it gets the sun for the share of its tile
  that is in it, and grows more slowly rather than not at all; a plant that
  needs light survives on a tile a fifth in the sun if that fifth, with the
  sky's light, is enough for it. Wholly in the shade under an open sky, the
  sky's light remains: about a third of the full light on a clear day, all
  of it on an overcast one. The north side of a wall or of a cliff grows
  moss and ferns rather than grass; a glade is brighter in its north half; a
  field wants to be south of the wood, not north of it. Taking root
  (`plant_can_take_root()`, "light in summer's shade") reads the summer
  `sun_hours` and `open` instead of the cover in leaf.
- **Soil water** (`docs/CLIMATE.md` §3.4): the `sun` of the evaporation is
  the tile's own: the foot of a north wall stays damp, a south bank dries.
- **Temperature** (`docs/CLIMATE.md` §5.1): `S`, what the sun adds to bare
  ground, is the tile's own too: warm at the foot of a south wall, cool in
  the shadow of a cliff, a few degrees apart at noon. East sides warm in the
  morning, west sides in the afternoon. This step only feeds the tile's own
  sun to the model as it is. The temperature model is to be reworked as a
  whole later, around the sun's rays and the shadows (what the sun heats is
  a surface it reaches: the ground, a wall, a roof; a room warmed through
  its open door or its window; the warmth leaving by an open door): a design
  of its own, in `docs/CLIMATE.md`.
- **Later, with what is already on the roadmap:** the direct sun felt on a
  body (shade worth seeking in a heat wave), snow melting first in the sun,
  wet things drying faster in the sun.

### 5.3 What it costs in tuning

This changes the state hash, the light most plants get (less beside
anything tall and under roofs, more at the edge of a wood, the same in the
open) and so the balance of world generation
(the understory is rolled from the light under the crowns), of the plants'
light needs (`PlantDef`) and of the microclimate's tests
(`test_microclimate`). It is a step of its own, measured on seed 42 before
and after: how many plants of each kind after a year, the temperatures of
§5.4 of `docs/CLIMATE.md`.

## 6. Steps

Each step is tested and leaves the game playable.

1. **The sun's direction** (core, `core/sun.h`, done): latitude,
   declination, elevation and azimuth from the tick, and the direction
   towards the sun along the map's axes. Tests: the noon elevations of
   §2.2, south at noon, east in the morning, sunrise and sunset where
   `game_sunrise()` puts them, the south and the polar circles.
2. **Rays to the sun** (core): `lit` at a point of the volume, through
   rock, walls, roofs and foliage. Tests: the length of a wall's shadow at
   noon by season, a cliff shading the level below, the sun under the edge
   of a roof, nothing through thatch, a crown beside its tree, nothing cast
   by an item or a settler.
3. **Shadows on screen**: the fine texture, shadows that glide (§4.1), the
   sky's share, the low sun, overcast days, the pieces and their budget; the
   light view (key G) shows the new light. The light of the sky is still
   what lies straight above (`map_light_cover()`). Measured with
   `--bench-render`.
4. **The light of the sky and doors** (core, then on screen): `open` per
   tile from the rays of §3.2, kept and worked out again around what
   changes; `MAP_LIGHT_ROOF` goes; doors open and close (while somebody
   passes, or kept open) and let the light and the sight through when open;
   the light of fires stops at walls (§3.3). On screen only at first: closed
   huts dark, the sun and the day coming in by the open door, caves
   darkening from their mouth, a fire lighting its room only. Tests: a
   closed hut at 0, the same with its door open, with a side open, a roof on
   posts, a cave three tiles in, open ground at 1, the foot of a cliff near
   a half; sight through an open door and not through a closed one; no light
   from a fire behind a wall, some through the open door.
5. **Light in the simulation**: `open`, `direct` (the share of the tile in
   the sun) and `sun_hours`, read by plants, soil and temperature; the
   inspector's line; the retuning of §5.3. Tests: a tile a fifth in the sun
   gets a fifth of the direct sun; a plant that needs light lives on it and
   dies in a closed hut. Measured with `bench_simulation`.
6. **What follows** (roadmap items of their own): the sun felt on a body,
   snow and drying; windows.
7. **A latitude per game** (later): the latitude a value of the world, the
   day lengths worked out from it, the polar day and night included; then
   the place chosen on a map of the world when starting a game, with its
   climate.
8. **The temperature model reworked** (later, a design of its own): heat
   from the sun where its rays land, shade, openings.

## 7. Not planned

- Shadows cast by settlers, animals, items and low plants.
- Moonlight as a second sun, with shadows at night: the night sky lights
  evenly what sees it (`open`).
- Light bouncing off walls, floors and water: a place is lit by the sky it
  sees, so the far corner of a room with one door is dark.
