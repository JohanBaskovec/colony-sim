# Vision and fog of war

Settlers see what lies around them, walls and floors block their sight, and
the colony only knows what its settlers see or have seen. This document is
the design of milestone M5's first part (field of view and fog of war);
wildlife and hunting come after it.

Status: step 1 (sight) is implemented (`core/vision.h`, shown by a debug
overlay), nothing is hidden yet; the rest is design. Values marked *(tuning)*
are first guesses.

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
construction site, dirt. Creatures (settlers of other groups, animals later)
are only shown where they are seen, since they move.

The colony's own settlers are always known (where they are, what they do):
they belong to it. The colony's plans are known too: its zones, orders,
bills and the settings of its workstations, wherever they are. What lies on
their tiles is remembered like the rest: a stockpile out of view shows the
items last seen in it.

### 3.3 Acting on the picture

Every choice a settler makes reads the colony's picture, never the real map:

- **Paths** go only through known tiles, and through remembered ones as they
  were. A settler meeting a change on its way (a wall built across a path it
  remembered open, a floor gone) sees it before stepping there (the next tile
  is always in view), and plans again.
- **Searches** (food, water, hauling, materials, work, gathering zones, beds,
  wandering) look only at known tiles. A settler walking to remembered food
  that is gone gives up when it sees it gone, and looks again.
- **Orders and zones** can only be placed on explored tiles.
- **What the colony has** (the resources list, the food alert, bills "until N
  lie in the colony") counts what the colony knows: items last seen in a
  stockpile out of view still count.
- **Messages** about the map (a roof fallen in, a gallery caved in) come when
  a settler sees it happen, or when the change is first seen. Messages about
  the colony's settlers (a breakdown, a knowledge learned) come at once.
  Hearing (a crash out of view) may come later.

The simulation itself (plants growing, items rotting, fires, falls,
temperature) uses the real map everywhere.

### 3.4 Exploring

Since settlers never walk into the unknown, the colony grows its picture by
going to the edge of what it knows:

- **Explore order** (Orders category of the build menu): drag a rectangle,
  over unexplored tiles too. A settler walks to the known tile nearest to the
  target that it can reach, and goes on as more comes into view, until the
  marked tiles are explored or nothing more can be reached. The order then
  goes. A new work type, **Exploring** *(tuning: its place in the grid)*.
- Settlers see while doing everything else: a gatherer at the edge of a zone,
  a hauler, a wanderer all add to the picture.
- Wandering prefers nothing for now: an idle settler does not explore on its
  own *(tuning)*.

## 4. The start of a game

- At the start the colony knows only what its settlers see from where they
  stand, plus **the way they came**: a path from a map edge to the arrival
  point, as if they had walked to it before the game began.
- The edge is drawn at random (seeded `Rng`, part of the world's draws). The
  path is the cheapest walk between them on the real map; everything seen
  along it is explored (seen from every tile of it, as if walked), and
  remembered as it is at the start.
- What the start guarantees (berry bushes, flint, drinkable water near the
  arrival point, `docs/KNOWLEDGE.md`) must lie in the picture: within sight of
  the arrival point, or along the path. When drinkable water is within reach
  but out of sight, the path passes by it (they followed the river).

## 5. Shown to the player

- **Unexplored** tiles are black (on every level), **remembered** ones are
  drawn from the picture under a grey veil, **seen** ones as they are. The
  view of the lower levels through open space shows the picture too.
- The tile readout and the inspector of a tile show the picture: an
  unexplored tile only says "Unexplored", a remembered one says that it is
  as last seen.
- The temperature view shows only seen tiles.
- Tools refuse unexplored tiles (except the Explore order), like any tile
  where an order cannot go, and say why on the build card.
- Debug (F4): **Reveal map** (everything explored, remembered as it is now)
  and **See everything** (every tile always seen: the game as before the fog,
  for tests and comparisons).

## 6. In the code

`core/vision.h` (step 1, see `docs/ARCHITECTURE.md`), then:

- **The known world**: a second copy of the map's layers and of the
  layers of items, structures and sites, of the same types (`Map`, `Items`...),
  where unexplored tiles have an unknown terrain (impassable, not buildable).
  Paths, regions and searches run on it unchanged; the simulation writes the
  real map as today, and every change to a tile that someone sees is copied
  to the known world, as is every tile coming into view.
- **See everything** (the debug cheat, and the default of tests and
  benchmarks that are not about vision): the known world is the real map,
  nothing is copied, behavior and state hashes are those of today.
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
2. **Picture and fog**: the known world, the start (view from the arrival
   point and the path from an edge), drawing (black, veil, seen), the tile
   readout, tools refusing unexplored tiles, the Reveal map and See
   everything cheats.
3. **Acting on the picture**: paths, regions and every search on the known
   world; plans redone on a change met on the way; targets gone; orders on
   explored tiles; resources list, alerts and bills on what is known;
   messages about the map when seen.
4. **Exploring**: the Explore order and work type.
5. **Measure and tune**: benchmarks with vision on, the render benchmark,
   checks on screen.

## 8. Later

- Darkness and light shorten sight (levels step 4); a hearth lights the camp.
- An eyesight attribute (`docs/ATTRIBUTES.md`).
- Climbing a tree or a tower as a lookout (falls out of the 3D sight).
- Hearing: noises out of view (a cave-in, a fight) known without being seen.
- Wildlife seen and not seen; combat uses the same lines of sight (M6).
- Saving the picture (M7).

## 9. Open questions

None at the moment.

Settled: field of view and fog of war together, wildlife apart; no range
limit (only what is in the way), in 3D, blocked by walls and floors; the fog is a real
constraint for the settlers; at the start, only what is in view plus the way
from a random edge; everything is remembered; Reveal map and See everything
cheats.
