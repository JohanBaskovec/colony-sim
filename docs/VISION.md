# Vision and fog of war

Settlers see what lies around them, walls and floors block their sight, and
the colony only knows what its settlers see or have seen. This document is
the design of milestone M5's first part (field of view and fog of war);
wildlife and hunting come after it.

Status: steps 1 (sight) and 2 (the colony's picture and the fog of war on
screen) are implemented: what nobody has seen is not shown, the tools refuse
it, and the start explores the arrival point and the way in. The settlers
themselves still know everything (step 3); the rest is design. Values marked
*(tuning)* are first guesses.

## 1. Principles

- **Seen in three dimensions.** Sight goes in straight lines through the
  volume of the map, across levels: rock, walls and floors stop it. From a
  tower or a hill one sees further than from the ground, because what stands
  in the way down there is below the line of sight.
- **A real constraint.** Settlers know nothing they do not see or have not
  seen: they never walk into the unknown, never look for food or water there,
  and act on what they last saw of a place out of view, even when it has
  changed since.
- **One picture for the colony.** The settlers share what they know (they talk
  to each other): the colony has one picture of the world, the union of what
  every settler has seen. The player sees that picture, nothing more.
- **Simulated everywhere, shown as last seen.** The whole map keeps being
  simulated as today; what nobody sees right now is shown, and acted upon, as
  it was last seen.
- **Deterministic.** Who sees what depends only on the game state: it is part
  of `game_hash()`, the only draw is the direction the settlers came from
  (from the seeded `Rng`).

## 2. What a settler sees

### 2.1 Range

- **No limit**: in daylight one makes things out several hundred metres
  away, more than the map (256 tiles of 1 m at most). Only what stands in the
  way limits sight: the forest, hills, walls, the ground. On open plains a
  settler sees across the map; in a forest the trunks soon hide the rest.
- Levels are 2 m high and tiles 1 m wide (`GAME_DESIGN.md`): lines of sight
  are worked out in metres, so a slope of sight over the ground is right.
- The same by day and by night for now. Darkness will limit it once light
  exists (levels step 4), and an eyesight attribute may change it later.
- A settler asleep sees only its own tile and the ones touching it (it would
  wake to what happens there) *(tuning)*. A settler knocked out (with health)
  sees nothing.

### 2.2 Eyes and peeking

- The eyes are 1.6 m above the floor of the settler's tile, in the middle of
  it. The settler's own tile never blocks its sight (a settler in a doorway
  sees both ways).
- With a wall in one of the eight tiles around (rock, a wall or a door),
  the settler also leans: from points 0.4 tile from the middle towards each
  open side. What any of the eyes sees is seen. Leaning moves along a wall
  face, which is what shows what lies past a corner. The leaning eyes only look
  as far as 8 tiles *(tuning)*: what a lean shows is mostly what is near the
  corner, and each eye costs a cast.

### 2.3 What blocks sight

| Thing | Sight |
|-------|-------|
| Rock, earth, the trunk of a tree, walls | Blocked. The face of the wall is seen (the tile is known), not what lies behind |
| Doors (a screen of branches) | Blocked, like a wall |
| A floor (ground, a built floor, a roof, dense foliage, deep or shallow water) | A plane at the bottom of its tile: blocks sight between its level and the one below, from above and from below |
| Open space | Seen through |
| Thin and sparse foliage | Seen through (as drawn: `map_sees_through_at()`) |
| Plants on the ground, items, settlers, ladders, props, workstations, beds | Seen through |
| A slope | Its tile has a floor, so it blocks like one |

- **What "seen" means.** A tile with rock or a wall is seen when sight
  reaches its box. Any other tile is seen when sight reaches its floor, from
  above or from below (a ceiling): a tile over the edge of a cliff is not seen
  from the foot of the cliff, whose eyes are below the edge, though the ray
  that would pass over the edge flies over it. Whatever lies on a floor (a
  plant, items, a bed) is known when the floor is seen.
- Open space is not looked at: only tiles with something in them (rock, a
  floor, a structure) are made of, so the field of view of a settler on open
  plains is its ground, not the thin air over it.
- Foliage does not block sight sideways for now (a settler in a tree sees
  between the branches) *(tuning)*.
- A sleeping settler sees its own tile, the eight around it and the ones over
  and under it. A settler knocked out (with health) will see nothing.

### 2.4 Refreshing

- A viewer's **near field** (the square of 16 tiles around it, on every level)
  is cast again whenever the settler changes tile, goes to sleep or wakes up,
  and when the map changes there (a wall built, a tree fallen, a tile dug:
  the map's change logs).
- Its **whole field** is cast again every 300 ticks (5 s at ×1, staggered
  from one settler to the next so that they do not all cast at once), and
  when the settler has walked 16 tiles or 3 levels from where it was last
  cast. A change far away is seen at the next whole cast. Between two
  casts what is far away is as it was, which is the point of a far view that
  changes slowly.
- Fields are kept as one bit per tile and per viewer; a count per tile
  (`Vision.seen`) says how many viewers see it now.

## 3. The colony's picture

### 3.1 Three states of a tile

| State | Meaning | Shown | Settlers |
|-------|---------|-------|----------|
| **Unexplored** | Never seen by anyone | Black | Know nothing: as if impassable, nothing to find there |
| **Remembered** | Seen before, nobody sees it now | As last seen, under a grey veil | Act on what they last saw |
| **Seen** | A settler sees it now | As it is | Know it as it is |

### 3.2 What is remembered

Everything a tile shows: terrain, rock, slope, the plant with its growth,
stocks and food, litter (branches, twigs, nuts), items, a structure or a
construction site, dirt. Creatures (wild animals, `core/animal.h`; settlers
of other groups later) are only shown where they are seen, since they move:
`game_animal_seen()`.

The colony's own settlers are always known (where they are, what they do):
they belong to it. So are its **plans**, which are the player's intent and
cannot be destroyed: zones, orders, blueprints and the bills of workstations,
wherever they are. The **things** are remembered like the rest, since they
can be destroyed (by enemies, later): structures and roofs are layers of the
map, and workstations (what they are, their fire, what lies on them to be
worked) are remembered per tile too, so that one destroyed out of view is
still believed to be there until someone sees it gone. What lies on a zone's
tiles is remembered like the rest: a stockpile out of view shows the items
last seen in it. Whatever is not in view is dim, the same by day and by
night, and no fire lights it: nobody knows whether something does.

### 3.3 Acting on the picture

Every choice a settler makes reads the colony's picture, never the real map:

- **Paths** go only through known tiles, and through remembered ones as they
  were. A settler meeting a change on its way (a wall built across a path it
  remembered open, a floor gone) sees it before stepping there (the next tile
  is always in view), and plans again.
- **Searches** (food, water, hauling, materials, work, gathering zones, beds,
  wandering) look only at known tiles. A settler walking to remembered food
  that is gone gives up when it sees it gone, and looks again.
- **Orders and zones** can only be placed on explored tiles. **Anything the
  colony remembers can be selected and given orders**, in view or not (a
  workstation to add a bill to, a tree to fell, stones to cut at a station out
  of sight): making the player walk a settler there first to see it would make
  the game a chore. The order is on the picture; the settler goes, and on
  arrival, when it sees the thing, it finds out whether it is still there: if
  not, the order falls and it chooses something else.
- **What the colony has** (the resources list, the food alert, bills "until N
  lie in the colony", the targets of gathering zones) counts what the colony
  knows: items last seen in a stockpile out of view still count (decided:
  the known world's items, `game_view_items()`, even where the settlers have
  not looked for a while). Implemented with the fog.
- **Messages** about the map (a roof fallen in, a gallery caved in) come when
  a settler sees it happen, or when the change is first seen. Messages about
  the colony's settlers (a breakdown, a knowledge learned) come at once.
  Hearing (a crash out of view) may come later.

The simulation itself (plants growing, items rotting, fires, falls,
temperature) uses the real map everywhere.

### 3.4 Exploring

Since settlers never walk into the unknown, the colony grows its picture by
going to the edge of what it knows. There is no Explore order: the player
explores by hand, by sending settlers where they want (the Move action of the settlers' window, then a click on the map:
`TASK_MOVE`, `game_order_move()`). A spot not explored yet is
refused, and a group aimed at it is sent to the explored tiles round it: the
fog is crossed a few tiles at a time, from its edge.

- Settlers see while doing everything else: a gatherer at the edge of a zone,
  a hauler, a wanderer all add to the picture.
- Wandering prefers nothing: an idle settler does not explore on its own
  (it only walks over what is known).

## 4. The start of a game

- At the start the colony knows only what its settlers see from where they
  stand, plus the land around the camp and **the way they came**: a path from
  a map edge to the arrival point, as if they had walked to it before the game
  began.
- **The camp**: every tile within 14 steps of the arrival point (and its
  neighbors) is explored, and so is everything seen from the arrival point.
  Food, water and flint are placed within reach of it (`GAME_SPAWN_*`), so
  they lie in the picture without a special rule.
- **The way in**: an edge tile of the map is drawn at random (a `Rng` of its
  own, seeded from the game's seed, so that the game's stream is left alone;
  up to 64 tries, the first one with a walkable path to the arrival point),
  and the cheapest walk from it is made: everything seen from its tiles (the
  whole field every 8th tile, the near one on every tile) is explored.
  `core/exploration.h`.
- Everything explored at the start is remembered as it is then.

## 5. Shown to the player

- **Unexplored** tiles are not drawn (the known world has nothing there: the
  dark background shows), **remembered** ones are drawn from the known world
  under a bluish veil, **seen** ones as they are. The view of the lower levels
  through open space shows the same.
- The tile readout shows the known world: an unexplored tile only says
  "Unexplored", a remembered one says "Out of sight: as last seen". The tile
  inspector and the resources list (items counted in the known world) follow.
- Whatever is not in view is dim, the same by day and by night, and the
  glow of a fire only lights what is in view. The temperature view only covers
  tiles in view, and the readout of a tile out of view says "Temperature:
  unknown". A workstation out of view shows its fuel and its fire as they were
  last seen.
- Orders, zones, buildings and forbidding are refused on unexplored tiles
  (in the core: `game_tile_explored()`); zones go through the known world, so
  an unexplored tile is simply not there.
- Debug (F4): **See everything** (no fog: the game as before it, the default
  of tests and benchmarks; `--debug see_everything`), **Reveal the map**
  (every tile explored, as it is now) and the overlay of what the selected
  settler sees.

## 6. In the code

`core/vision.h`, `core/known.h` and `core/exploration.h`
(`docs/ARCHITECTURE.md`, *Vision*), then:

- **The known world** is not a second simulation: pictures of the map, the
  items and the dirt (same types, same sizes, so code made for the real ones
  reads them unchanged), written by copying tiles from the real world: when a
  tile comes into view, a slice of the tiles in view every tick (a twelfth of
  them: they change), and a last time when it goes out of view, so that what
  is held of a tile out of view is what was seen last. Nothing is copied from
  the real world to anything but these pictures, and the simulation never
  reads them yet (step 3: paths, regions and searches will).
- **See everything** (fog off, the default of the core): the pictures are not
  kept, `game_view_map()` is the real map, behavior and state hashes are those
  of today; with the fog the hash also covers what is explored.
- **Cost**: measured by `bench_vision` (a cast) and `bench_simulation` (the
  whole game, 64 settlers). Target: about half a millisecond per tick on
  average and no tick above 2 ms because of vision *(tuning)*: at ×3 (180
  ticks a second) that is under a tenth of the processor. Saving the picture
  comes with saves (M7).

## 7. Steps

1. [x] **Sight** (`core/vision.h`): blockers, eye points, fields of view in 3D
   across the whole map, near and whole refreshes, seen counts; a debug
   overlay of what the selected settler sees (F4: "Show what the selected
   settler sees", or `--debug vision`). Tested: walls and floors block,
   peeking round a corner, a tower sees further than the ground, thin foliage
   seen through, the far side of open plains seen, counts equal to the
   fields' sums, and an agreement with a brute-force line of sight on
   generated worlds. Nothing hidden yet.
2. [x] **Picture and fog**: the known world (pictures of the map, items and
   dirt, copied from the real ones as tiles come into and go out of view), the
   explored tiles, the start (the camp, the way in from a random edge),
   drawing (unexplored not drawn, remembered veiled, seen as it is), the tile
   readout, orders, zones and buildings refused on unexplored tiles, the
   Reveal map and See everything cheats. Tested in `tests/test_fog.c`.
3. [x] **Acting on the picture**: every path and search plans on the known
   map and items (food, things to fetch and haul, beds, work spots, gathering
   and construction jobs, whether materials exist); what a settler does is
   done on the real world, so it finds out on arrival what changed; a walk's
   target is only checked against the real world while it is in view; the
   resources list, the food alert, bills and gathering targets count the known
   items; the messages about the map (a roof fallen in, a cave-in) come when
   someone sees it. Connectivity regions stay the real ones (they only decide
   where a pre-check looks, and may say "unreachable" too early). Orders on
   things the colony remembers are allowed; a destroyed thing's order is only
   given up on arrival (keeping a destroyed workstation's bills waits for
   enemies, `docs/ROADMAP.md`). Tested in `tests/test_fog.c` and `test_roof.c`.
4. ~~Exploring: the Explore order~~: dropped, the player explores by hand.
5. **Measure and tune**: benchmarks with vision on, the render benchmark,
   checks on screen.

## 8. Later

- Darkness and light shorten sight (levels step 4); a hearth lights the camp.
- An eyesight attribute (`docs/ATTRIBUTES.md`).
- Climbing a tree or a tower as a lookout (falls out of the 3D sight).
- Hearing: noises out of view (a cave-in, a fight) known without being seen.
- Combat uses the same lines of sight (M6). Wild animals have simpler senses
  of their own for now (a straight line on their level, and hearing close
  by: `animal_notices()`), not the settlers' 3D sight.
- Saving the picture (M7).

## 9. Open questions

None at the moment.

Settled: field of view and fog of war together, wildlife apart; no range
limit (only what is in the way), in 3D, blocked by walls and floors; the fog is a real
constraint for the settlers; at the start, only what is in view plus the way
from a random edge; everything is remembered; Reveal map and See everything
cheats.
