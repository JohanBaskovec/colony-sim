# Architecture

## Overview

```
┌──────────────────────────────────────────────────────────────┐
│ src/game/  (executable colony_sim, depends on SDL3/SDL3_ttf) │
│                                                              │
│  main.c ─► app.c ──► render.c   hud.c   text.c               │
│              │       localization.c (loads .lang via SDL)    │
└──────────────┼───────────────────────────────────────────────┘
               │ calls (one-way dependency)
┌──────────────▼───────────────────────────────────────────────┐
│ src/core/  (static library colony_core, pure C17, no SDL)    │
│                                                              │
│  game ─► colonist ─► pathfinding, region ─► map              │
│    │        ├─────► designation ─► map, work ─► skill ─► item │
│    │        ├─────► hauling ─► stockpile, item, reservation  │
│    │        └─────► reservation, item                        │
│    ├──► pool, game_clock, worldgen ─► noise, rng             │
│  i18n   camera   cli   log   util                            │
└──────────────────────────────────────────────────────────────┘
               ▲
               │ links
┌──────────────┴───────────────────────────────────────────────┐
│ tests/  (one executable per module + translation checker)    │
└──────────────────────────────────────────────────────────────┘
```

The single most important rule: **`src/core/` never depends on SDL** (nor on
the file system, wall-clock time or global random state). This keeps the
simulation:

- **testable**: tests link only `colony_core` and run headless in milliseconds;
- **deterministic**: identical seed and inputs give identical results, which
  enables reproducible bug reports, replays, save validation and, later,
  lockstep multiplayer if ever wanted;
- **portable**: the frontend could be replaced (another renderer, a dedicated
  server, a benchmark tool) without touching game logic.

## Core modules (`src/core/`)

| Module       | Responsibility                                                               |
|--------------|------------------------------------------------------------------------------|
| `game`       | Simulation root (`Game`): owns map, clock, RNG, pathfinder, colonists; spawning, picking, `game_hash()` |
| `colonist`   | Colonist data, needs, decision making, movement along paths, work            |
| `designation`| Player work orders per tile (chop, mine) and their work progress             |
| `reservation`| Who is busy with which tile (job, food): one claimant per tile and kind     |
| `item`       | Items on the ground: definitions, categories, quality grades and hit points, one stack per tile, dropping |
| `stockpile`  | Storage zones drawn by the player: tiles, priority (1-10), allowed item types, quality and hit point ranges |
| `hauling`    | Queries: which stack needs hauling, best storage tile for it                 |
| `work`       | Work types (mining, woodcutting, gathering, hauling), their skill, priority helpers |
| `skill`      | Skills, levels and experience thresholds; quality and condition of what a worker produces |
| `pathfinding`| A* to a goal, Dijkstra to the nearest tile matching a predicate             |
| `region`     | Connectivity regions: which tiles can reach each other on foot (a cache)    |
| `pool`       | Fixed-capacity slot allocator with generational handles (`EntityId`)        |
| `game_clock` | Ticks, speed, pause, calendar (`GameDate`), daylight curve                   |
| `map`        | Tile layers (terrain, rock, plants) and static definitions (`TerrainDef`...) |
| `worldgen`   | Procedural map generation from a seed                                        |
| `noise`      | Deterministic value noise and fBm                                            |
| `rng`        | PCG32 random number generator                                                |
| `i18n`       | Translation catalogs, `tr()`, plurals, placeholders, number formatting       |
| `camera`     | World ↔ screen transforms, zoom, visible tile range                          |
| `cli`        | Command-line parsing                                                         |
| `log`        | Logging with a pluggable sink                                                |
| `util`       | Small helpers (clamp, lerp, FNV hashes)                                      |

## Frontend modules (`src/game/`)

| Module         | Responsibility                                                          |
|----------------|-------------------------------------------------------------------------|
| `main`         | Parses the command line, runs the app                                   |
| `app`          | SDL init/shutdown, main loop, input → camera/clock, frame orchestration |
| `render`       | Draws the map (placeholder colored tiles), daylight overlay, highlights |
| `hud`          | Builds and draws the HUD panels; all text goes through `tr()`           |
| `ui`           | Immediate-mode widgets (panel, button, check box) and click capture     |
| `stockpile_panel` | Inspector of the selected stockpile zone (priority, filters, delete) |
| `work_panel`   | Work priorities grid (colonists × work types)                           |
| `text`         | SDL3_ttf text rendering with an LRU cache of laid-out strings           |
| `localization` | Discovers `.lang` files, selects the language, loads catalogs           |

## Time and the game loop

```
every frame:
    dt = real time since last frame (clamped to 0.25 s)
    poll SDL events → camera, speed, language...
    game_update(game, dt)
        due = game_clock_consume(clock, dt)  // dt × 60 ticks/s × speed, fractional part kept
        repeat due times: game_tick(game)    // the only place where the world changes
    render (map, lighting, HUD)
```

- One tick = 1/60 s of game time at ×1. Speeds are ×1, ×3, ×6.
- At most `GAME_CLOCK_MAX_TICKS_PER_UPDATE` ticks run per frame: if the machine
  cannot keep up, the game slows down instead of freezing.
- Calendar: 2,500 ticks per hour, 24 hours per day, 15 days per season, 4
  seasons per year. The game starts on day 1 of spring, year 1, at 06:00.
- Expensive systems run at intervals rather than every tick, and spread their
  work across ticks: each plant grows once per in-game hour, but a tick only
  handles a slice of the map (`update_plants()`), so even a 1024 × 1024 map
  never stalls. Future systems should do the same (e.g. needs every 60 ticks,
  temperature every 250 ticks).

## World representation

The map stores **dense layers**, one array per layer indexed by
`y * width + x`:

- `terrain[]`: `Terrain` (water, sand, soil...), each with a `TerrainDef`
  (movement cost, fertility, buildable);
- `rock[]`: natural rock walls (`RockType`, 0 = none), impassable;
- `plants[]`: at most one plant per tile (`Plant`: type + growth).

Terrain and rock decide where one can walk. Once the map is in use they are
changed through `map_set_terrain()` / `map_set_rock()`, which bump
`Map.topology_revision` and log the change, so that caches derived from
passability (connectivity regions) know when they are out of date. Plants
never block movement.

Dense layers are cache-friendly, trivially serializable and hashable
(`map_hash()`). Upcoming layers: floors, buildings/walls, zones, room IDs,
temperature, fog of war.

Player orders are not part of the map (which only describes the physical
world): `Designations` (`core/designation.h`, owned by `Game`) has its own
dense layers of the same size: order type and work progress per tile.

Items on the ground are a dense layer too (`Items`, `core/item.h`, owned by
`Game`): at most one stack per tile (one item type, up to its stack limit),
plus map-wide totals per type. Each `ItemDef` has a realistic `weight` (kg per
unit: a 7 kg log, a 20 kg stone, a 300 g portion of berries) and a `bulk`
(room taken, about half a liter per unit, gaps included). A tile holds
`ITEM_TILE_BULK` (1000) of bulk, so the stack limit is derived:
`item_stack_limit() = ITEM_TILE_BULK / bulk` (1,000 berries, 50 logs, 50
stones). `items_drop()` fills the target tile, then
spreads the rest breadth-first over nearby passable tiles (never behind a
wall). Products are data: `PlantDef.cut_item/cut_yield` (scaled by growth) and
`RockDef.mined_item/mined_yield`, `PlantDef.harvest_item/harvest_yield`
(harvesting keeps the plant, which grows back).

Every item has a **quality grade** (`ItemQuality`: crude, rough, standard,
fine, superb) and **hit points** (`hit_points`, up to `ItemDef.max_hit_points`).
Some types are always perfect (`has_quality` / `damageable` false: raw stone
is always superb and at full hit points); `item_stack_make()` enforces it, so
no code path can create a damaged stone. Only items of the same type **and
grade** share a tile (`item_stacks_match()`); a stack keeps the count-weighted
average hit points of its items. Grades are few on purpose: they keep stacks
large. Averaging grades instead would have broken storage filters, because
two stacks a zone rejects for different reasons (a crude one and a broken one)
could merge into one it accepts. Hit points averaging can only do that with a
two-sided hit point range, which is rare and harmless. Quality is meant to
carry over to what is made from the items (M3).

Completed orders produce items through `skill_product()` (`core/skill.h`):
the worker's level in the order's skill (`WorkTypeDef.skill`) sets a quality
score (grade = score / 20) and a condition in percent of the hit points, both
with a random spread drawn from the game RNG. Novices bring back crude or
rough, damaged logs; experts superb, intact ones. Randomness is only drawn for
item types that have a quality or can be damaged. Skill levels go from 0 to 20
and are rolled when a colonist is created (sum of two dice: mostly average,
a few novices and experts). The level also sets the work speed
(`colonist_work_speed()`): ×0.5 at level 0, ×1 at level 10, ×1.5 at level 20.
Colonists learn by doing: each tick spent working on an order adds
`COLONIST_WORK_XP_PER_TICK` to `Colonist.skill_xp` for the order's skill
(`colonist_gain_skill_xp()`), whatever their speed, and level L needs `SKILL_XP_PER_LEVEL × (L + 1)`
to reach L + 1 (one in-game hour of work for level 1, twenty hours for level
20). The products of a tick use the level before that tick's experience.
Hauling needs no skill and teaches nothing. The work grid shows each
colonist's level next to the priority.

Food items have a
`nutrition` per unit; a hungry colonist goes to the nearest food, on the
ground or on a ripe bush, and eats just what it takes to be sated. Items in
hand are stored on the colonist (`Colonist.carried`), not in the layer.

### Stockpiles and hauling

`Stockpiles` (`core/stockpile.h`, owned by `Game`) is another dense layer: the
zone index of each tile (0 = none) plus a fixed table of zones (number shown to
the player, priority from 1 = highest to 10 = lowest, default 5, and a bit mask
of allowed item types, a range of accepted grades and a range of accepted hit
points in percent of the maximum). `stockpile_accepts()` checks all three;
items that are always perfect count as superb and 100 %. Zones only cover
passable, buildable tiles. Every change bumps a `revision` counter.

A stack **needs hauling** when no zone on its tile accepts it (narrowing a
filter makes stored items move out), or when a zone of strictly better
priority accepts it and has a free tile with room. A storage tile has room for
a stack when it is empty or holds a matching stack that the zone still
accepts (`haul_destination_room()`). Hauling
is the `WORK_HAULING` work type: at each priority level, designations come
first, then hauling. `haul_find_job()` runs a Dijkstra search for the nearest
such stack, then one search per priority level (best first) for the nearest
storage tile from the stack. Once per search, the tiles of the zones are
visited (each zone keeps its bounding box: the map is never scanned, and a
zone with an empty tile is not scanned further) to record which zones have
room for which type and grade, plus a per-type bit set of "priority levels
with room". The latter is a quick upper bound that lets idle colonists skip
the search entirely when nothing can be stored; the per-zone data, checked
against the zone filters, decides for a given stack.

The hauler reserves the source stack (`RESERVATION_HAUL_SOURCE`) and the
destination tile (`RESERVATION_STORAGE`), walks to the stack and picks up what
the destination can take and it can carry. With room left in hand, it then
collects more on the way (`haul_find_extra_source()`): the nearest stack
within `COLONIST_HAUL_COLLECT_MAX_COST` that matches the load (same type and
grade), that the destination's zone accepts and that no better zone with room
wants (otherwise good wood heading for an ordinary zone would drag along wood
that belongs in a better one). Only one stack is reserved at a time; if it
vanishes, the hauler delivers what it holds (`Colonist.haul_delivering`
switches from collecting to delivering). Each
colonist has a `max_carry_bulk` and a `max_carry_weight` (60 and 40 kg by
default, by hand): `colonist_carry_capacity()` is the number of units within
both limits, at least one (an item too big is dragged). Above the weight
limit, walking speed is multiplied by limit / load (at least
`COLONIST_MIN_LOAD_SPEED`); it does not happen with today's items, but will
with huge ones and equipment. If the destination stops
accepting the items on the way (filter changed, zone removed), it looks for
another storage tile, else drops its load. Any interruption (eating,
sleeping...) goes through `begin_task()`, which drops carried items where the
colonist stands, so items are never lost. A failed haul search backs off; its
stamp is the stockpile revision + item additions + reservation releases.

Static definitions (`TerrainDef`, `RockDef`, `PlantDef`) carry a stable `id`
(used by save files, never translated) and a `name_key` (translation key).
They are C tables for now; moving them to data files (moddable) is a roadmap
item.

### Entities

Mobile or individually tracked things live in typed storage: a `Pool`
(`core/pool.h`) allocates slots, and the owner keeps the data in an array of
the same capacity (e.g. `Game.colonists[COLONIST_MAX]`). They are referenced
by **generational handles** (`EntityId` = slot index + generation): once an
entity is removed, every old handle to it is detectably stale
(`game_colonist()` returns NULL). Systems iterate slots in index order (which
keeps updates deterministic); no pointers between entities are stored, and
the frontend keeps handles only (e.g. the selection).

Colonists are the first entity type; animals, items and buildings with state
will follow the same pattern.

### Colonist behavior

Each tick, `colonist_tick()`:

1. updates needs (food and water always decrease; rest decreases, or
   increases while sleeping);
2. when standing on a tile center, decides: hungry or thirsty → walk to the
   nearest edible plant or drinking spot (Dijkstra search), most urgent need
   first; tired, or night and not fully rested → eat/drink first if below
   50 %, then sleep in place; otherwise wander nearby after a random pause.
   Severe hunger or thirst wakes a sleeping colonist up;
3. acts: walks one tick along its path (speed / tile cost), eats, drinks, or sleeps.

A drinking spot is a passable tile that is shallow water or touches water
(`map_is_drinking_spot()`), so colonists stop on the shore rather than wade
in. A failed food or water search floods everything within its cost limit, so
retries back off exponentially (½ h, 1 h, 2 h, then every 4 in-game hours)
and reset on success. The starting position prefers land with water within
reach.

Decisions only happen on tile centers, so a colonist never jumps back when it
changes its mind.

### Work and reservations

Every designation belongs to a **work type** (`core/work.h`: mining,
woodcutting, gathering...). Each colonist has a **priority** per work type: 0 = never,
otherwise 1 (highest) to 4 (default 3).

The player edits priorities in the work grid (`game/work_panel.c`), which
only calls `game_set_work_priority()`; the stepping rules of the grid
(`work_priority_raised()` / `work_priority_lowered()`) live in the core and
are unit-tested. Changing a priority clears the colonist's failed-search
backoff, so newly allowed work is picked up at once, and a job whose work
type got disabled is given up at the next decision (a hauler first delivers
what it already holds).

When nothing more urgent is needed, an idle or wandering colonist looks for a
job, one priority level at a time (1 first): a Dijkstra search for the nearest
passable tile next to an open job (designated, work type at that level, not
reserved), 4-neighborhood, fixed neighbor order. Priority therefore beats
distance. The colonist reserves the job, walks there and adds
`colonist_work_speed()` work per tick (`COLONIST_WORK_SPEED` scaled by its
skill level). When the work needed is reached the order
is carried out on the map (tree removed, rock dug out, leaving the rough stone
floor), its products are dropped (wood, stone) and the designation
disappears.

**Reservations** (`core/reservation.h`, owned by `Game`) record who is busy with
what: (kind, tile, owner), at most one owner per tile and kind. Kinds today:
work (a designated tile), food (a bush about to be eaten), haul source and
storage tile, so two colonists never walk to the same job, bush, stack or
storage tile. Owners are colonist slot + 1.

- Every task change goes through one function (`begin_task()`), which releases
  everything the colonist holds: reservations never outlive the task that made
  them, including when a colonist leaves to eat or sleep or is removed. Work
  done stays on the tile, so the next worker resumes it.
- At each tile center, a colonist heading to a job or to food checks that the
  target is still valid (order not cancelled, fruit still there); a worker
  also checks every tick that it still owns its job.
- When every order is reserved (`designations.count` minus work reservations
  is zero) idle colonists skip the search entirely. Failed searches back off
  like food and water searches, but a stamp (designation revision + number of
  reservation releases) lets them look again as soon as work may have become
  available.

The table is a small array kept in creation order (which is what gets
hashed), with a hash index from (kind, tile) to the entry: every working
colonist checks every tick that it still owns its job, so lookups must not
scan the table. The index is rebuilt when reservations are released, which is
rare next to lookups.

## Pathfinding

- 8-directional grid, cost of entering a tile = `map_move_cost()` (× √2
  diagonally), no diagonal move past an impassable corner.
- A* with the octile heuristic scaled by `PATH_MIN_TILE_COST` (admissible as
  long as no passable tile is cheaper: checked by a test).
- `pathfinder_find_nearest()` runs Dijkstra until a predicate matches (food,
  free tile...), with a cost limit to bound the search.
- A `Pathfinder` owns per-tile scratch arrays reused across searches (stamps
  instead of clearing), so searches do not allocate.
- Benchmark: `bench/bench_pathfinding [size] [searches] [seed]`; about 0.7 ms
  per cross-map search on a 200 × 200 map in Release.

### Connectivity regions

A search for something that cannot be reached explores everything within its
cost limit before giving up. `Regions` (`core/region.h`, owned by `Game`)
avoids that: every passable tile gets the id of its region, two tiles being in
the same region exactly when one can walk between them (flood fill over
orthogonal neighbors; diagonal moves never connect more because corners
cannot be cut). Each region also knows whether it contains a drinking spot.

- Regions are a **cache**, not simulation state: they are not hashed, and
  every use is an exact shortcut for a search that would fail anyway, so they
  never change what the simulation does (the state hashes of the benchmark
  scenarios are identical with and without them).
- They are refreshed at the start of each tick when `Map.topology_revision`
  changed. A dug-out tile (the common case) is added in place from the map's
  change log; a change that joins or splits regions rebuilds everything.
- Queries check the revision: if the map changed since the last refresh
  (mining earlier in the same tick), they answer "maybe connected", never a
  stale "no".
- Uses today: wandering does not path to the other side of a wall; a thirsty
  colonist does not search when its region has no water; before a work search,
  a scan of the designation layer checks that some open job has a tile to
  stand on in the colonist's region (orders deep inside rock, on another
  island, or all reserved would otherwise cost a full exploration per idle
  colonist).

### Measuring

`bench/bench_simulation [colonists] [days] [scenario] [size] [seed]` runs a
colony without a window and prints the average and worst tick time and the
final state hash. An optimization that is not supposed to change behavior
must leave that hash unchanged. Reference (Release, 200 × 200, 64 colonists,
10 in-game days): about 8 µs per tick left alone, 10 µs with every tree, rock
and bush designated; long food searches when berries run out are the
remaining spikes (1 to 4 ms).

## Randomness

- `Rng` (PCG32) instances are seeded explicitly. World generation uses its own
  stream; the simulation uses `game->rng`. New systems that need randomness
  either use `game->rng` (in a fixed order) or their own derived stream.
- `rand()`, `time()` and friends are forbidden in `src/core/`.

## Internationalization

See [I18N.md](I18N.md). In short: the frontend reads `assets/lang/*.lang` with
`SDL_LoadFile`, the core parses them into `I18nCatalog`s, and `tr()` looks keys
up in the active catalog, then in English. Language can be switched at
runtime; strings returned by `tr()` are therefore not cached across frames.

## Rendering

- SDL3 2D renderer (`SDL_Renderer`), VSync on, alpha blending enabled.
- Only visible tiles are drawn (`camera_visible_tiles()`); tile edges are
  snapped to whole pixels to avoid seams.
- Tiles, plants and item shapes are batched: they are accumulated as quads
  and sent with a few `SDL_RenderGeometry()` calls instead of one fill per
  rectangle (zoomed out, a 1280 × 720 window shows about 57,000 tiles).
- Placeholder art: tiles are colored rectangles with a per-tile brightness
  variation. Replacing them with a texture atlas is a roadmap item.
- Text: SDL3_ttf renderer text engine, with a cache keyed by string content;
  entries unused for 120 frames are evicted.

## Frontend state

UI state that is not part of the simulation (camera, selection, selected
stockpile, active tool, drag rectangle, debug toggle) lives in `App`.

The UI is immediate-mode (`game/ui.h`): panels and widgets are laid out and
drawn every frame. A left or right click on a panel drawn during the previous
frame is captured before it reaches the map and consumed by the first widget
that contains it; the mouse wheel over a panel scrolls it instead of zooming
the map. Widgets act on the game directly through core functions
(`stockpile_set_priority()`...). It never influences the simulation,
so the same seed gives the same game whatever the player looks at.

## Assets

`assets/` is copied next to the executable after each build (`bin/assets/`).
At startup the game looks for `<executable dir>/assets/`, then falls back to
the source tree (`COLONY_SOURCE_ASSETS_DIR`, development builds).

## Build

- CMake ≥ 3.25, presets in `CMakePresets.json`.
- SDL3 / SDL3_ttf: system packages if found, else `FetchContent` from the
  official Git repositories (pinned release tags, static libraries).
- Our targets get strict warnings via `colony_configure_target()`
  (`cmake/ProjectOptions.cmake`); third-party code does not.
