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
│    │        ├─────► designation ─► map, work ─► knowledge ─► item │
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
| `colonist`   | Colonist data, needs, decision making, movement along paths, work, facial expression from the needs, the action its hands are doing |
| `appearance` | Looks of a colonist (skin, body shape, hair style and color, beard), rolled at creation |
| `designation`| Player work orders per tile (mine rock, take a structure apart), the knowledge and level each needs, work progress |
| `dirt`       | How dirty each tile is (0-100 %): adding, spreading around a tile, cleaning; only on open ground |
| `reservation`| Who is busy with which tile (job, food): one claimant per tile and kind     |
| `item`       | Items on the ground: definitions, categories, quality grades and hit points, one stack per tile, dropping, transformations (drying) |
| `stockpile`  | Storage zones drawn by the player: tiles, priority (1-10), allowed item types, quality and hit point ranges |
| `tool`       | Tool speed factors by kind of work (bare hands included) and grade, best carried tool, wear and breaking |
| `gather_zone`| Gathering zones: tiles, one order per resource (wood, berries, branches, twigs, grass, flint, cobbles: gather N, until N in the colony, forever; fully grown plants only), a zone priority, the jobs tiles offer, what they give |
| `hauling`    | Queries: which stack needs hauling, best storage tile for it                 |
| `recipe`     | Recipes: stations, inputs in preference order, tools, work, knowledge and minimum level; products, quality cap from materials, failures |
| `workstation`| Workstations (crafting spot, knapping spot): blueprints, materials, construction, bills, the craft in progress |
| `structure`  | Structure definitions (grass bed, grass and branch wall, branch screen): materials, work, knowledge, movement cost, rest factor, length in tiles and `Direction`, hit points, wear and refresh; hit points by grade, grade cap from the materials |
| `bed`        | Bed ownership: one bed per settler, one settler per bed, kept on the bed's head; beds gone from the map are forgotten |
| `construction`| Construction sites: blueprints of structures and refresh sites on worn ones, materials delivered per slot, work, built grade; daily wear; built structures go to the map's structure layers |
| `work`       | Work types (mining, woodcutting, gathering, construction, crafting, hauling, cleaning), the knowledge shown for each, priority helpers |
| `knowledge`  | Knowledges (fire, stone knapping...), levels 0-100, mastery at 50, work speed, learning, quality and condition of what a worker produces |
| `pathfinding`| A* to a goal, Dijkstra to the nearest tile matching a predicate             |
| `region`     | Connectivity regions: which tiles can reach each other on foot (a cache)    |
| `pool`       | Fixed-capacity slot allocator with generational handles (`EntityId`)        |
| `game_clock` | Ticks, speed, pause, calendar (`GameDate`), daylight curve                   |
| `map`        | Tile layers (terrain, rock, plants with their branch and twig stocks, cobbles and flint in the ground) and static definitions (`TerrainDef`...) |
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
| `render`       | Draws the map (colored ground tiles), zones, items, plants and colonists (sprites, depth sorted), daylight overlay, highlights |
| `sprites`      | Loads `assets/sprites/*.png` into a texture atlas with halved copies, batches sprite quads |
| `hud`          | Builds and draws the HUD panels and the windows in stacking order; all text goes through `tr()` |
| `ui`           | Immediate-mode widgets (panel, window, button, check box, slider, tooltip) and click capture |
| `ui_layout`    | Rows of a window laid out twice: measured, then drawn (window sized to its content) |
| `menu_bar`     | Bottom left menus: Build (categories, icon buttons), Work, ? (help)     |
| `stockpile_panel` | Inspector of the selected stockpile zone (priority, filters, delete) |
| `gather_zone_panel` | Inspector of the selected gathering zone (priority, a check box per resource with how many are available, mode, target, progress, fully grown option, warnings) |
| `station_panel` | Inspector of the selected workstation (blueprint progress, bills, warnings) |
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
  never stalls. Items on the ground are advanced the same way, once per hour
  per tile (`update_items()`, `items_advance()`). Future systems should do the same (e.g. needs every 60 ticks,
  temperature every 250 ticks).

## World representation

The map stores **dense layers**, one array per layer indexed by
`y * width + x`:

- `terrain[]`: `Terrain` (water, sand, soil...), each with a `TerrainDef`
  (movement cost, fertility, buildable);
- `rock[]`: natural rock walls (`RockType`, 0 = none), impassable;
- `plants[]`: at most one plant per tile (`Plant`: type, growth, and stocks
  of what can be gathered from it without felling it: branches and twigs).
  Stocks are counted in hours of regrowth (`PlantDef.stock_regrow_hours` per
  item), capped by `stock_capacity` scaled by growth, and grow back with the
  plant's hourly update, even on mature plants.

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
unit: a 7 kg log, a 20 kg stone block, a 1 kg cobble, a 300 g portion of berries) and a `bulk`
(room taken, about half a liter per unit, gaps included). A tile holds
`ITEM_TILE_BULK` (1000) of bulk, so the stack limit is derived:
`item_stack_limit() = ITEM_TILE_BULK / bulk` (1,000 berries, 50 logs, 50
stone blocks, 666 cobbles). `items_drop()` fills the target tile, then
spreads the rest breadth-first over nearby passable tiles (never behind a
wall). Products are data: `PlantDef.cut_item/cut_yield` (scaled by growth) and
`RockDef.mined_item/mined_yield`, `PlantDef.harvest_item/harvest_yield`
(harvesting keeps the plant, which grows back); gathering takes items from
the plant's stocks (`plant_take_stock()`).

Some items change while they lie on the ground: `ItemDef.transforms_into`
after `transform_hours` (fresh grass dries into dry grass in 72 hours).
`ItemStack.progress` counts the hours spent so far; merging stacks averages
it by count, like hit points. A stack that transforms keeps its hit points
percentage, and its quality when the new type has one.

Every item has a **quality grade** (`ItemQuality`: crude, rough, standard,
fine, superb) and **hit points** (`hit_points`, up to `ItemDef.max_hit_points`).
Some types are always perfect (`has_quality` / `damageable` false: stone
blocks are always superb and at full hit points); `item_stack_make()`
enforces it, so no code path can create a damaged stone block. Only items of the same type **and
grade** share a tile (`item_stacks_match()`); a stack keeps the count-weighted
average hit points of its items. Grades are few on purpose: they keep stacks
large. Averaging grades instead would have broken storage filters, because
two stacks a zone rejects for different reasons (a crude one and a broken one)
could merge into one it accepts. Hit points averaging can only do that with a
two-sided hit point range, which is rare and harmless. Quality is meant to
carry over to what is made from the items (M3).

Settlers have a **level in each knowledge** (`Colonist.knowledge`, a `double`
from 0 to 100, `core/knowledge.h`, design in `docs/KNOWLEDGE.md`). There is no
tree: below `KNOWLEDGE_MASTERY_LEVEL` (50) a knowledge unlocks nothing; from
there each order needs its own minimum level (`DesignationDef.knowledge` and
`min_level`, checked by `colonist_can_do()`; gathering jobs: `GatherJobDef`,
`colonist_can_gather()`). An order or job whose knowledge is `KNOWLEDGE_NONE`
cannot be done by anyone yet: felling and mining wait for tools and their own
knowledges. The frontend refuses to place orders nobody can carry out
(`game_designation_available()`), and the zone inspector warns under a
resource nobody can gather (`game_gather_job_known()`). The first settlers get
50 to 80 in each starting knowledge.

Completed orders produce items through `knowledge_product()`: the worker's
level sets a quality score (25 + 1.4 × (level − 50), grade = score / 20) and a
condition in percent of the hit points (50 + 1.6 × (level − 50)), both with a
random spread of ±10 drawn from the game RNG. Randomness is only drawn for
item types that have a quality or can be damaged. The level also sets the work
speed (`colonist_work_speed()`): ×0.5 at 50, ×1 at 75, ×1.5 at 100.
Colonists learn by doing: each tick spent working adds to the order's
knowledge (`colonist_learn_by_doing()`) `KNOWLEDGE_DOING_RATE` points per hour
times a difficulty that falls as the level rises ((110 − level) / 60) and an
age factor (children ×2, elders ×0.5). The products of a tick use the level
before that tick's learning. Hauling needs no knowledge and teaches nothing.
The work grid shows each colonist's level in the work type's knowledge,
greyed below mastery.

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

The zone tools (stockpiles and gathering zones alike) add a dragged rectangle
with `stockpiles_add_rect()` / `gather_zones_add_rect()`: the selected zone and
every zone of the same kind the rectangle overlaps are merged into one, which
keeps the settings of the selected zone, else of the oldest; work under way on
gathering tiles is kept. With no zone involved, a new one is created. Zones of
different kinds never overlap, and no zone lies under a workstation: the tools
go through `game_add_stockpile_rect()` / `game_add_gather_zone_rect()`, which
pass the other kind's tile layer and the workstation layer as `blocked`
layers (a NULL-terminated list), so their tiles are left out (the low-level
`*_create()` / `*_extend()` do not check, for tests and benchmark scenarios).
The other way round, `game_place_workstation()` takes the station's tile out
of the zone it was in (blueprints included). Tiles
are taken out of zones with Shift+drag or the removal tools of the build menu;
a zone that keeps some tiles stays, even in several pieces.

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
colonist has a `max_carry_bulk` (60, by hand) and a `max_carry_weight` that
follows from its **strength** (1 to 20, rolled at creation: 10 on average,
a point less per 6 years past 40; 40 kg at strength 10, 2 kg per point, so
22 to 60 kg; `colonist_set_strength()` keeps both in sync). Health will later
lower what can be carried without changing the strength.
`colonist_carry_capacity()` is the number of units within both limits, at least one (an item too big is dragged). Above the weight
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
and reset on success. The starting position prefers fertile land with water
within reach. The first settlers bring nothing, so world generation then
plants ripe berry bushes nearby until there are `GAME_SPAWN_RIPE_BUSHES`
within `GAME_SPAWN_FOOD_MAX_COST` of travel.

Decisions only happen on tile centers, so a colonist never jumps back when it
changes its mind.

### Dirt and cleaning

`core/dirt.h` keeps a dirt level per tile (0..100 %, owned by `Game`, part
of `game_hash()`), with a count of dirty tiles and a revision. Completing a
craft dirties the station's tile by the recipe's `dirt` (chips, shavings) and
each neighbour by half (`dirt_spread()`); dirt never lies on water, rock or
built walls. Later it will lower beauty and help diseases spread.

Cleaning is the `WORK_CLEANING` work type, the last column, at the lowest
priority by default: an idle colonist walks to the nearest dirty tile nobody
else is cleaning (`RESERVATION_CLEAN`), stands on it and removes 1 % every
`COLONIST_CLEAN_TICKS_PER_PERCENT` ticks (no knowledge, no tool), then looks
again (the next dirty tile is usually next to it). The search is skipped
when every dirty tile is reserved, and backs off on failure until the dirt
revision or a reservation release changes its stamp. The frontend draws dark
spots on dirty tiles and shows the level in the tile inspector.

### Work and reservations

Every designation belongs to a **work type** (`core/work.h`: mining,
woodcutting, gathering...). Each colonist has a **priority** per work type: 0 = never,
otherwise 1 (highest) to 4 (default 3; cleaning defaults to 4).

Within one priority level, `try_start_work()` goes through the work types in
the order of the grid's columns, left to right (the order of `WorkType`):
mining, construction, crafting, woodcutting, gathering, hauling, cleaning. A
settler does the leftmost work it can find before looking at the next column;
a higher priority always wins over the column order. For each work type it
tries the nearest designation of that type, then the type's own jobs
(construction: sites, then workstation blueprints; crafting: bills; gathering:
gathering zones; hauling; cleaning). Gathering sits right of construction and
crafting: a zone gathering "forever" would otherwise starve them at the same
level.

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
knowledge level). When the work needed is reached the order
is carried out on the map (tree removed, rock dug out leaving the rough stone
floor, grass cut, branches or twigs taken from the plant's stock), its
products are dropped (wood, stone blocks, branches...) and the
designation disappears.

**Gathering zones** (`core/gather_zone.h`, owned by `Game`) are how the player
asks for what the land offers: felling trees, picking berries, taking branches
and twigs, cutting grass and picking up stones. Designations are left for
single-tile orders (mining). A zone holds one order per resource
(`GatherOrder`): off by default, and when enabled it gathers N items and stops
(counted by the order itself), or gathers until the items lying in the colony
(`Items.totals`, as for bills; grass counts fresh and dry) reach a target, or
gathers forever. Wood and berries may wait for fully grown plants (on by
default) so that settlers do not walk to young plants that give little.
`gather_zones_update()` recomputes each tick which resources every zone wants
and bumps the zones revision when it changes; it is also bumped once per
in-game hour, after every plant has grown, so that idle settlers look again.

A zone tile offers a **job** (`GatherJob`: branches, twigs, harvest, cut grass,
chop, stones, in that order of preference on one tile: the plant's stocks
first, since picking its fruit or felling it would take them away, stones
last) when it holds something a wanted resource comes from. Each job has its
work type (felling is woodcutting, the rest gathering) and knowledge; the
settler takes the first job of the tile among those it may do
(`Colonist.work_type` holds the job while `gathering`). Stones are limited:
each tile holds a number of cobbles and flint nodules (`Map.cobbles`,
`Map.flint`), and a stone search takes one of the wanted types the tile
holds, at random in proportion to their numbers (only flint wanted: tiles
without flint offer nothing, and their cobbles stay). A zone runs out of
stones once its tiles are emptied. The zone inspector shows how many items of
each resource the zone holds now (`gather_zone_available()`: wood of the
trees the orders allow, ripe fruit, stocks, grass, stones), and warns about an
enabled resource with none. The job search reuses the designation search
(`WorkSearch.zones`), the work progress of the tile's current job lives in
the zone layer (work on another job there starts over), and a gatherer
chooses again after each job done, so that a more important job is noticed
(usually it takes the same tile again).

Each zone has a **priority** (1 to 10, default 5, like stockpiles): for one
work type, the search goes through the priorities of the zones that want
something (`gather_zones_wanting_priorities()`), highest first, and only
takes the nearest job among the zones of that priority
(`WorkSearch.zone_priority`); a level with nothing open or reachable is
skipped by the cheap `open_job_reachable()` scan before any path search.
Work priority and the grid's column still come first. The tile inspector
shows the job under way on a zone tile (`game_gather_work_at()`): the settler
who took it and the work done, or the work left off.

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

### Structures and construction sites

Built **structures** (`core/structure.h`) are a layer of the map
(`Map.structure`, one `StructureType` per tile, with counts per type), like
rock: `map_set_structure()` keeps `topology_revision` right (a wall blocks
movement, a door multiplies the movement cost, a bed too) and clears the
tile's plant. Blueprints are **construction sites** (`core/construction.h`,
owned by `Game`, at most `CONSTRUCTION_MAX`, a dense index layer): the
player places them (`game_place_structure_rect()`: along the edges of the
rectangle for walls, filling it otherwise; refused on stockpiles,
workstations and trees). A colonist with the construction work type
and the knowledge (`TASK_CONSTRUCT`, `RESERVATION_CONSTRUCTION`) fetches each
material in turn, as much as it can carry, delivers it from a neighboring
tile, then works from there: a wall cannot be stood on once built. Grass
or a bush on the tile is cleared first (see *Clearing plants* below). When it
is done the site is gone, the structure appears; a blocking one pushes the
items of its tile next door and takes the tile out of stockpiles and
gathering zones, and a colonist left standing on it steps aside at its next
tick. The cancel tool removes sites (their materials are dropped); the
*take apart* order (`DESIGNATION_DECONSTRUCT`, construction work) removes a
built structure and gives back half of its materials.

Material counts come from real structures, for a tile of about 1 m²: see
`structure.c` and `docs/KNOWLEDGE.md` §10.

**Long structures**: a grass bed takes two tiles (`StructureDef.length`),
its head and its foot in one of four `Direction`s. Every tile holds the
type (movement cost, blocking and the inspector work per tile as before);
`Map.structure_part` holds the direction and the tile's place in the
structure, so `map_structure_origin()` finds the head from any tile. Grade
and hit points live on the head. `map_place_structure()` writes every tile
(removing whole any structure in the way), `map_remove_structure()` (and
`map_set_structure(..., STRUCTURE_NONE)`) removes every tile, from any of
them. A construction site has a direction too and takes every tile in the
index layer; builders reserve it on its head (`RESERVATION_CONSTRUCTION`),
work it from next to any of its tiles and clear the plants of each tile in
turn. `game_plan_structure_rect()` lays out what a drag plans (beds side by
side in rows of their length, the rectangle stretched to hold one, the
clicked tile always a head); placing and the preview both use it. The
player turns beds with `APP_KEY_ROTATE` (`App.structure_direction`).

**Grade, hit points, wear**: two more map layers hold each built
structure's grade (`Map.structure_quality`) and hit points
(`Map.structure_hit_points`). `map_set_structure()` gives a standard grade
and full hit points (worldgen, tests); a finished site uses
`map_build_structure()` with the builder's grade
(`knowledge_quality_of_score()` of a roll at their level, through the
game's `Rng`), capped by `structure_quality_cap()`: the average grade of the
graded materials delivered (branches; grass and twigs do not cap) + 1, as
for crafts. `structure_max_hit_points()` scales the definition's hit points
by the grade (×0.6 crude to ×1.5 superb). Once a day (`game_tick()` on the
day boundary) `constructions_wear_day()` takes `wear_per_day` hit points from
structures that wear by themselves (a grass bed: 1); one at 0 falls apart
(with a refresh site on it, whose materials are dropped), one below
`STRUCTURE_REFRESH_BELOW` (half) gets a **refresh site**: a construction site
with `refresh` set, on the built structure, wanting `refresh_material` and
`refresh_work` instead of the building ones (`construction_material()`,
`construction_material_count()`, `construction_work_needed()`). Builders
handle it like any site; finishing it brings the hit points back to full and
keeps the grade. Taking the structure apart removes its refresh site.

**Clearing plants**: blueprints (construction sites and workstations) may
be placed on grass and bushes, not on trees (`map_plant_blocks_building()`:
felling needs an axe nobody has yet). Once the materials are in, the builder
first spends `gather_clear_work()` on the plant (cutting grass, pulling up a
bush, no knowledge needed), kept in the blueprint's `clear_progress` if it
is interrupted; `gather_clear_plant()` then removes the plant and drops what
it gives (cut grass, its twigs and branches, ripe berries, with quality from
the builder's gathering level). Only then does the construction work start.
`map_set_structure()` still clears any plant left, as a safety net.

**Beds**: a colonist going to sleep walks to its own bed (`core/bed.h`),
else to the nearest free bed, which becomes its own (`RESERVATION_BED`,
within `COLONIST_BED_SEARCH_MAX_COST`), and sleeps on the ground otherwise.
`Beds` (owned by `Game`) keeps the owner id (colonist slot + 1) on the bed's
head tile and the bed of each owner; a bed owned by someone else is never
taken, even when its owner is awake. `game_tick()` calls
`beds_forget_missing()` (beds taken apart or fallen apart lose their owner)
and `game_remove_colonist()` frees the settler's bed. Rest comes back at `rest_factor` in a bed (1 for a grass
bed) and `COLONIST_GROUND_REST_FACTOR` (0.8) on the ground; waking up from a
night on the ground leaves `ground_sleep_ticks` of slower work
(`COLONIST_GROUND_SLEEP_WORK_FACTOR`), to become a mood once moods exist.

### Workstations, bills and carried tools

Workstations (`core/workstation.h`, owned by `Game`, at most
`WORKSTATION_MAX`) sit on one tile each. The player places a **blueprint**
(`game_place_workstation()`, which takes the tile out of any zone); a colonist with
the construction work type, and the knowledge the station needs, fetches the
materials (a knapping spot needs a cobble as its anvil) and does the
construction work. A station made of nothing (crafting spot) is placed
ready, unless grass or a bush is in the way: it is then a blueprint until a
builder has cleared the tile. The cancel tool also removes blueprints; removing a
station drops what it held.

A built station carries up to `WORKSTATION_BILL_MAX` **bills**: a recipe
(`core/recipe.h`), a mode (do N times, until N in stock, forever), the input
types allowed, how far inputs are searched for (`input_radius`, a travel cost
from the station, 80 by default), a range of worker levels in the recipe's
knowledge (`min_level`..`max_level`, inclusive, 0..100 by default: beginners
for cheap things, masters for the best ones) and a pause flag.

"Until N" counts `Game.stock`: items lying in a stockpile that accepts them,
plus those on their way there, lying on a workstation tile (where products
are dropped) or carried by a hauler. Counting only stored items would let a
crafter make more while the last ones wait for a hauler; counting every item
on the map (as gathering zones do) would count tools lost in the wild.
`game_update_stock()` scans the stockpiles (their bounding boxes), the
stations and the colonists; `game_tick()` only calls it while some bill is in
"until N" mode (it costs as much as the rest of a small colony's tick).

Bills are worked through from the top: a crafter takes the first one that
is not paused, wants more, whose recipe it knows at a level the bill accepts
(`colonist_level_fits_bill()`), whose tools can be had (carried, or lying
somewhere) and with an allowed input lying in the colony. Inputs are searched
within the bill's radius of the station, in the recipe's
preference order (flint first for flakes, a cobble first for a chopper).
In recipes and stations `KNOWLEDGE_NONE` means anyone can (in designations
it means nobody can yet).

A craft in progress lives on the station (`work_recipe`, `work_input`,
`work_progress`), not on the colonist: if the crafter leaves to eat, the next
one resumes it, and while it is unfinished only bills of the same recipe are
taken, paused or not (a paused bill would otherwise block the station). A
crafter already working finishes the craft even if the bill no longer accepts
its level (it learned, or the range changed) or was paused. Products are
dropped on the station's tile. Quality comes from the
worker's level, capped by the material (`recipe_quality_cap()`: grade + 1,
a cobble knaps like crude flint); shaping a hand axe can fail
(`recipe_failure_chance()`), leaving a few flakes.

**Carried tools**: a colonist has no equipment slots but keeps up to
`COLONIST_TOOL_MAX` tools (`Colonist.tools`, at most one per type), which
count in the carried weight and bulk. Any item can be a tool when a recipe
lists it: knapping uses a plain cobble as its hammer. When a tool to fetch
is of the input's type, `bill_has_input()` makes sure enough is left to
work on (two cobbles to knap a cobble). A recipe's tools are fetched like its
input (same `RESERVATION_HAUL_SOURCE` reservation) and then kept; a removed
colonist drops them.

**Tool speed and wear** (`core/tool.h`): each kind of work is a `ToolUse`
(cutting grass, knapping, woodworking, building; `TOOL_USE_NONE` for work no
tool helps) with a speed factor per item type and one for bare hands (0:
impossible), from the table of `docs/KNOWLEDGE.md` §2.3, times the tool's
grade factor (×0.8 crude to ×1.2 superb). Every tick of work multiplies the
colonist's knowledge speed by the factor of its best carried tool for the
work (`colonist_tool_factor()`; bare hands when no tool does better):
gathering jobs (`GatherJobDef.tool_use`), clearing a blueprint's tile
(`gather_clear_tool_use()`), building sites and stations, and crafts
(`RecipeDef.tool_use`, the tool of the recipe's first set; a recipe's tools
are required). The tool used wears: `Colonist.tool_wear` counts its ticks of
work, and every `TOOL_WEAR_TICKS` (an hour) it loses a hit point; at 0 it
breaks (`Colonist.tools_broken`, shown in the inspector until notifications
exist). A craft whose tool broke is left (its progress stays on the
station) until a new tool is fetched. Cobbles are damageable for this (200
hit points: a hammer stone lasts 200 hours of knapping). Optional tools
(grass, building) are used when carried but not fetched yet.

**Swapping**: before a craft starts, a crafter that carries a tool for the
recipe looks for a faster one within `COLONIST_TOOL_SWAP_MAX_COST` (30) of
where it stands (`FetchSearch.min_factor`), fetches it and leaves the old one
of the same type on that tile (`colonist_swap_tool()`); a tool of another
type is simply added. A worse tool is never taken in exchange.

Station jobs (`TASK_BUILD`, `TASK_CRAFT`) go through three phases (fetch,
go to the station, work) and reserve the station (`RESERVATION_STATION`).
At one priority level, designations are tried first, then construction,
crafting, gathering zones (a background job that never ends by itself) and
hauling. Failed station searches back off with a stamp made of the
workstations revision, item additions and reservation releases; gathering
searches with the zones revision and reservation releases.

Stones are two map layers (`Map.cobbles`, `Map.flint`): how many of each lie
about a tile, `MAP_STONES_MAX` (20) at most together, taken one by one by
gathering and never growing back. World generation gives every stony tile
(`TerrainDef.stony`: soil, rich soil, gravel, rough stone; not sand) 0 to 20
stones, from a random stream of their own so that the plants do not depend on
them; on flint patches (noise, denser on gravel and rough stone) each stone is
flint with the patch's share, up to 60 %. The spawn makes sure a tile with at
least `GAME_SPAWN_FLINT_STONES` flint lies near the settlers, laying a small
patch otherwise. The tile inspector shows the stones left in the ground.

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
- Colonist looks (`appearance_roll()`) come from a stream derived from the
  game seed and the colonist's handle, never from `game->rng`: adding a hair
  style or changing the odds must not change what the simulation does (the
  `bench_simulation` state hash stays the same).
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
- Tiles and item shapes are batched: they are accumulated as quads
  and sent with a few `SDL_RenderGeometry()` calls instead of one fill per
  rectangle (zoomed out, a 1280 × 720 window shows about 57,000 tiles).
- The ground is still colored rectangles with a per-tile brightness
  variation; grass tints the soil.
- Sprites (`game/sprites.h`): 128 sprite pixels per tile (the largest zoom
  shows them 1:1). `tools/generate_sprites.py` draws them procedurally
  (Pillow, 4× supersampling) into `assets/sprites/`; run it again after
  changing it and commit the PNG files. At startup they are packed into one
  atlas (shelf packing, sizes and places multiples of 32 with 32-pixel gutters),
  and five halved copies are computed (opacity-weighted averages; SDL has no
  mipmaps). A batch uses the smallest copy at least as detailed as the
  screen, so trees do not shimmer zoomed out. Sprites are queued as textured
  quads and drawn with `SDL_RenderGeometry()`; the vertex color tints them
  (hair color, darker when asleep) or fades them.
- A sprite is anchored by the middle of its bottom edge on a point of the
  world (the bottom middle of a plant's tile, a colonist's feet) and may
  overflow its tile: trees reach up to 3 tiles high and 2 wide. Plants and
  colonists are drawn together from the back (top of the screen) to the
  front (`render_world_sprites()`): rows of plants merged with colonists
  sorted by the depth of their feet. A tree that would hide a colonist
  standing behind it is drawn see-through.
- Plants: one sprite per growth stage (`plant_growth_stage()`, 5 stages);
  bushes have a second set with berries (`plant_has_fruit()`). Sprites are
  mirrored on some tiles for variety.
- Colonists: four layers drawn in order on the same 128 × 192 canvas: body
  (naked, without arms nor legs; thin, average or heavy, per skin tone), head
  (per skin tone and expression), beard, hair. Clothes will be extra layers
  between the body and the head.
  Hair and beard sprites are light gray, tinted with the hair color. The
  expression comes from the needs (`colonist_expression()`). Front view
  only.
- Hands: while a colonist works (`colonist_action()`: blows, cutting,
  picking, eating), a fist moves beside it on the side of its job, holding
  the tool of the job when there is one (only crafting uses tools so far:
  hammerstone for knapping, flake or hand axe for spears). The tool turns
  around the grip (`sprites_draw_rotated()`). The movement depends on the
  game tick, so it follows the game speed and stops when paused; colonists
  are out of step (offset by slot).
- Tree see-through test: the head and chest of a colonist behind the middle
  of a tree sprite (its canvas has empty corners).
- Text: SDL3_ttf renderer text engine, with a cache keyed by string content;
  entries unused for 120 frames are evicted.

## Frontend state

UI state that is not part of the simulation (camera, selection, selected
stockpile, gathering zone, workstation or structure, active tool, drag
rectangle, debug toggle) lives in `App`. Escape and a right click on the map
(released where it was pressed: otherwise it pans) step back one level
(`step_back()`). Every keyboard control is a physical position (`APP_KEY_*`
scancodes in `game/app.h`): the controls are laid out the same on any
keyboard (WASD on QWERTY is ZQSD on AZERTY), and the help and tooltips show
the labels of the active layout (`app_key_name()`). The key at the physical position of A on AZERTY (Q on QWERTY,
`APP_KEY_BUILD_LIKE`) turns on the tool that builds another of what is
selected (`build_like_selection()`).
The structure tool (`APP_TOOL_STRUCTURE`) previews the sites a drag would
place (`render_structure_preview()`); structures and sites are drawn by
`render_structures()`.

The UI is immediate-mode (`game/ui.h`): panels and widgets are laid out and
drawn every frame. Everything drawn belongs to an owner: a window (an
`AppWindow` id) or a plain panel. A left or right click on a panel or window
drawn during the previous frame is captured before it reaches the map and
handed to the topmost owner under it, whose first widget containing it
consumes it; only that owner's widgets light up under the mouse. The mouse
wheel over a window scrolls it (over any panel, it never zooms the map).

Windows (inspectors, work grid, controls help) keep their position, stacking
order and scroll in `Ui` from one frame to the next. They are sized to their
content: `ui_layout_window()` runs the rows once to measure them, then again
to draw them. A window taller than the screen allows scrolls, with its
content clipped. A click brings a window to the front, and a window opened
again comes to the front; the HUD draws the open windows back to front
(`hud_open_windows()`), then the menus on top, then the tooltip. Widgets act on the game directly through core functions
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
