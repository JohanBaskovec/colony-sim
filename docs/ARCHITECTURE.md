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
| `colonist`   | Colonist data, needs, decision making, movement along paths, work, what its mood thinks of (states, sleep, meals) and its breaks, facial expression from the mood, the action its hands are doing |
| `appearance` | Looks of a colonist (skin, body shape, hair style and color, beard), rolled at creation |
| `designation`| Player work orders per tile (dig earth, cut a ramp, mine rock, take a structure apart), the knowledge, level and tool each needs, work progress |
| `dirt`       | How dirty each tile is (0-100 %): adding, spreading around a tile, cleaning; only on open ground |
| `reservation`| Who is busy with which tile (job, food): one claimant per tile and kind     |
| `item`       | Items on the ground: definitions, categories, quality grades and hit points, one stack per tile, dropping, transformations (drying), rot |
| `stockpile`  | Storage zones drawn by the player: tiles, priority (1-10), allowed item types, quality and hit point ranges |
| `tool`       | Tool speed factors by kind of work (bare hands included) and grade, best carried tool, wear and breaking |
| `gather_zone`| Gathering zones: tiles, one order per resource (wood, each wild food, branches, twigs, grass, flint, cobbles: gather N, until N in the colony or N days of food, forever; full harvest only) and per group of resources (wood, food, stones: limits each of its resources), a zone priority, the jobs tiles offer (picking and digging, careful ones for skilled gatherers), what they give |
| `hauling`    | Queries: which stack needs hauling, best storage tile for it                 |
| `recipe`     | Recipes: stations, inputs in preference order, tools, work, knowledge and minimum level; products, quality cap from materials, failures |
| `workstation`| Workstations (crafting spot, knapping spot, open hearth): definitions (materials, construction work), bills, the craft in progress, a hearth's fire state |
| `hearth`     | The fire of an open hearth: fuel values, fuel wanted, burning, lighting rules, light around burning hearths, the hours of fireside stories |
| `structure`  | Structure definitions (grass bed, grass and branch wall, branch screen): materials, work, knowledge, movement cost, rest factor, length in tiles and `Direction`, hit points, wear and refresh; hit points by grade, grade cap from the materials |
| `bed`        | Bed ownership: one bed per settler, one settler per bed, kept on the bed's head; beds gone from the map are forgotten |
| `construction`| Construction sites: blueprints of structures and of workstations, refresh sites on worn structures, materials delivered per slot, work, built grade; daily wear; built structures go to the map's structure layers, finished workstations to `Workstations` |
| `work`       | Work types (mining, fire, woodcutting, gathering, construction, crafting, hauling, cleaning), the knowledge shown for each, priority helpers |
| `knowledge`  | Knowledges (fire, stone knapping...), levels 0-100, mastery at 50, work speed, learning (rates, caps, fading), quality and condition of what a worker produces |
| `conversation`| Settlers nearby chatting now and then, the knowledge each brings up, what the other learns from it |
| `notice`     | Messages the simulation raises for the player (a knowledge about to be lost, a settler breaking down...), kept in a ring |
| `mood`       | Moods: thoughts (states setting in and growing worse, memories; all fading over time), the mood as their sum, its effects on work speed, quality and learning, breaks (sulking, going off alone, binging) |
| `pathfinding`| A* to a goal, Dijkstra to the nearest tile matching a predicate, on the start's level |
| `region`     | Connectivity regions: which tiles can reach each other on foot, per level (a cache) |
| `tree`       | Trees in the volume: a trunk of solid wood and floors of foliage built into the map as the tree grows, finding the tree of a tile, where a felled tree falls (aimed away from structures) and what it leaves along its length |
| `roof`       | Roofs falling in and galleries caving in when their last support goes, checked around what the map logged as changed |
| `room`       | Rooms: areas closed by walls, doors and rock, and whether roofs cover them (a cache) |
| `temperature`| Outdoor air through the seasons and the day, a temperature per tile: heat spreading, walls, roofs, caves, hearths |
| `weather`    | The sky every 6 hours (clear, cloudy, fog, rain, storm; snow when cold), warm and cold spells, snow cover, recent rain |
| `pool`       | Fixed-capacity slot allocator with generational handles (`EntityId`)        |
| `game_clock` | Ticks, speed, pause, calendar (`GameDate`), daylight curve                   |
| `map`        | The map as a volume of levels (`map_index()`, `map_ground_level()`), tile layers (terrain, rock, plants with their branch, twig and food stocks, cobbles and flint in the ground, branches and twigs lying on tiles), static definitions (`TerrainDef`, `PlantDef` with the plants' seasons...) and a plant's life over the year (`plant_advance()`) |
| `worldgen`   | Procedural map generation from a seed: relief (hills, slopes, rock outcrops behind cliffs), terrain, plants, stones |
| `noise`      | Deterministic value noise and fBm                                            |
| `rng`        | PCG32 random number generator                                                |
| `i18n`       | Translation catalogs, `tr()`, plurals, placeholders, number formatting       |
| `camera`     | World ↔ screen transforms, zoom, visible tile range, the level shown         |
| `cli`        | Command-line parsing (language, seed, size, colonists, test scenario, cheats) |
| `scenario`   | Ready-made test situations on a new game (`bench_simulation`, `--scenario`) |
| `log`        | Logging with a pluggable sink                                                |
| `util`       | Small helpers (clamp, lerp, FNV hashes)                                      |

## Frontend modules (`src/game/`)

| Module         | Responsibility                                                          |
|----------------|-------------------------------------------------------------------------|
| `main`         | Parses the command line, runs the app                                   |
| `app`          | SDL init/shutdown, main loop, input → camera/clock, frame orchestration |
| `render`       | Draws the map (ground and wall textures, colored tiles for the other terrains), zones, items, plants and colonists (sprites, depth sorted), daylight overlay, highlights |
| `sprites`      | Loads `assets/sprites/*.png` into a texture atlas with halved copies, batches sprite quads |
| `hud`          | Builds and draws the HUD panels and the windows in stacking order; all text goes through `tr()` |
| `ui`           | Immediate-mode widgets (panel, window, button, check box, slider, dropdown, tooltip) and click capture |
| `ui_layout`    | Rows of a window laid out twice: measured, then drawn (window sized to its content) |
| `menu_bar`     | Bottom left menus: Build (categories, icon buttons), Work, ? (help)     |
| `stockpile_panel` | Inspector of the selected stockpile zone (priority, filters, delete) |
| `gather_zone_panel` | Inspector of the selected gathering zone (priority; foldable groups and their resources: tri-state check boxes, how many are available, the full harvest icon, a mode dropdown with target, unit and progress; warnings) |
| `station_panel` | Inspector of the selected workstation (forbid, bills, warnings) |
| `tile_panel`   | Inspector of the selected tile: what lies there (structure, roof, items, plant) with a Forbid/Allow button each, and its construction site |
| `work_panel`   | Work priorities grid (colonists × work types)                           |
| `debug_panel`  | Debug menu (F4): a check box per cheat (`GameDebug`), one-shot commands (knowledge, time), the debug tool (spawn items, move settlers) |
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
  seasons per year. The game starts at the end of summer (day 11 of summer,
  `GAME_START_DAY`), year 1, at 06:00: wild food is plentiful, and there is
  time to build before winter.
- Expensive systems run at intervals rather than every tick, and spread their
  work across ticks: each plant grows once per in-game hour, but a tick only
  handles a slice of the map (`update_plants()`), so even a 1024 × 1024 map
  never stalls. Items on the ground are advanced the same way, once per hour
  per tile (`update_items()`, `items_advance()`). Future systems should do the same (e.g. needs every 60 ticks,
  temperature every 250 ticks).

## World representation

The map is a **volume**: `width × height` tiles on each of `depth` levels
(at most `MAP_MAX_SIZE` = 256 tiles a side and `MAP_MAX_LEVELS` = 32 levels,
one level = 2 m of height). It stores **dense layers**, one array per layer,
level after level: `map_index()` gives `(z * height + y) * width + x`, so a
one-level map has the same layout as a flat one. Every function that takes a
tile takes `x, y, z` (rectangles take `x0, y0, x1, y1, z`), every position
(`TilePos`, colonists, stations, sites, stockpiles) carries its level, and
every module that keeps a per-tile array (items, designations, dirt, beds,
stockpiles, zones, regions, rooms, temperature, pathfinder) sizes it for all
the levels.

A tile is **solid** (rock: a wall, or the inside of a hill), **open with a
floor** (its terrain) or **open space** (`TERRAIN_OPEN`: the air over lower
ground, where nobody can stand: `map_is_open()`). Rock tiles keep a floor
under the rock, which shows once it is dug out. A floor stays when the tile
below is dug out (as in a gallery under a hill): what holds it up comes with
digging and building (support, collapses). `map_ground_level()` is the
highest tile of a column one could stand on.

**Slopes** (`Map.slope`, `map_set_slope()`) link the levels: from a slope
with open space above it, one walks up to an orthogonal neighbor on the
level above where one can stand, and back down the same way
(`map_can_climb()`, `map_level_links()`), at `MAP_CLIMB_COST` (×2) times the
tile reached. Slopes are natural, cut as ramps, or left by holes dug down
(see *Digging*). A branch ladder (`STRUCTURE_LADDER`, walked past like open
ground) links the levels the same way from its tile, slower
(`MAP_LADDER_CLIMB_COST`, ×3): `map_climb_cost_at()` gives either cost, and
a ladder is only planned where it leads up (`map_ladder_fits()`: open space
over it, the ground of a neighbor one level up), at the foot of a one-level
cliff a ramp cannot be cut into (rock) or in a pit. A slope covered by a
floor or rock leads nowhere. Slopes are not buildable (`map_is_buildable()`).
Stairs come with floors and storeys.

A floor or rock **anywhere above** a tile covers it: `map_is_roofed()`
(caves and galleries dug into hills, and built roofs, which are floors too:
see *Roofs*); `map_natural_roof()` when that cover is not a built roof. It
replaced the earlier one-level rule (rock all around a tile).

Hashes only mix in a level when it is not 0 (or when the map has more than
one level), the slope layer once a slope exists.

**Relief** (`core/worldgen.h`): the elevation noise decides each column's
ground level. The plains lie on `Map.surface_level` (`underground_levels`,
4 by default; the player sees it as level 0, the levels below as negative);
lakes and rivers lie in hollows below it, shallows and beaches one level
down, deep water two; the ground rises one level every `level_step` of
elevation above `hill_level` (half as fast above `mountain_level`: hills,
not mountains), rock outcrops one level above the ground around them behind
cliffs. Under the ground's surface lie `WORLDGEN_EARTH_LEVELS` (2) of earth
(`ROCK_EARTH`: dug out, it gives nothing), then granite or sandstone; stony
ground is rock all the way down. Where soft ground or shallow water rises
one level to an orthogonal neighbor, the lower tile is a slope (one wades up
the bank to the beach, then walks up to the plains); rock outcrops, deep
water and steeper rises stay cliffs. The levels under the ground are solid
until dug (see *Digging*). The map has as many levels as the highest ground
needs, plus `TREE_MAX_LEVELS` (7) open levels over it where the
tallest trees rise and roofs go (about 18 on default maps, 4 of them
underground; 32 at most). `Map.top_ground_level` keeps the highest level
with ground: no plant grows above it, so the plant update stops there; items
may lie higher (on foliage), and the item update skips the levels without
any (`Items.level_stacks`). `WorldGenParams.flat` makes a world of one level of ground (and the
open levels over it), for tests and for benchmarks that compare with older
versions (`flat` argument of the benchmarks).

The layers:

- `terrain[]`: `Terrain` (water, sand, soil...), each with a `TerrainDef`
  (movement cost, fertility, buildable);
- `rock[]`: natural rock walls (`RockType`, 0 = none), impassable;
- `plants[]`: at most one plant per tile (`Plant`: type, growth, and stocks
  of what can be gathered from it without felling it: branches, twigs and
  food; a tree has none, see below). Stocks are counted in hours of
  regrowth (`PlantDef.stock_regrow_hours` per item), capped by
  `stock_capacity` scaled by growth, and grow back with the plant's hourly
  update, even on mature plants;
- `branches[]`, `twigs[]`: the dead branches and twigs lying on each tile (at
  most `MAP_BRANCHES_MAX` and `MAP_TWIGS_MAX`), on the ground and on the
  foliage of trees (`map_litter()`, `map_add_litter()`, `map_take_litter()`).

**Seasons** are days of the year: a `PlantDays` bit set (60 days fit in 64
bits) says when a plant grows (`growth_days`: spring to autumn for all of
them) and when its food ripens (`food_days`: blackberries at the end of
summer, hazelnuts in autumn, rhizomes all year...). Out of its food days the
food stock is gone; plants that die back (wild garlic after spring, nettles
in winter) wither to growth 0 and show nothing until their growing days come
back. `plant_advance()` does one step of all this; `plant_settle()` puts a
new plant in the state of a day (world generation, planted food). Eating or
picking takes from the food stock, so the plant itself is untouched (a wild
carrot dug up goes back to growth 0, a mushroom patch picked is gone).
Mushrooms appear on their own in autumn near mature trees: each tile gets a
daily roll from a hash of the seed, the tile and the day (`world_roll()` in
`game.c`), during the first in-game hour of the day, so that they draw
nothing from the game's random stream.

Terrain and rock decide where one can walk. Once the map is in use they are
changed through `map_set_terrain()` / `map_set_rock()`, which bump
`Map.topology_revision` and log the change, so that caches derived from
passability (connectivity regions) know when they are out of date. Plants
never block movement.

**Trees in the volume** (`core/tree.h`, step 3d of the levels): a sapling
(the first two growth stages) is a plant like the others, walked through.
From `TREE_VOLUME_MIN_STAGE` on, a tree takes room like a building: its
trunk is solid wood (`ROCK_WOOD`, over open terrain above the foot) from the
foot up, and its foliage is a floor of branches (`TERRAIN_FOLIAGE`, walked
on at ×1.5, nothing built on it) around the trunk on its upper levels, the
top one covering the trunk too. The plant stays on the foot's tile (the
north-west one of a wider trunk) with the growth, stocks and trunk width
(`Plant.size`, 0 meaning 1); the shape follows from them (`tree_shape()`):

| Tree | Trunk | Levels of trunk | Foliage from level | Reach | Levels |
|---|---|---|---|---|---|
| Stage 2 | 1 × 1 | 1 | 1 | 1 | 2 |
| Stage 3 | 1 × 1 | 2 | 1 | 1 | 3 |
| Grown (stage 4) | 1 × 1 | 3 | 1 | 1 | 4 |
| Old | 2 × 2 | 5 | 3 | 2 | 6 |
| Ancient | 3 × 3 | 6 | 3 | 3 | 7 (`TREE_MAX_LEVELS`) |

The crown is rounded (corners left out of the wider ones). Old and ancient
trees only come from world generation (`OLD_TREE_CHANCE` 4 %,
`ANCIENT_TREE_CHANCE` 1 % of the grown trees where the wider trunk fits);
their stocks scale with the trunk's area, as does the wood they give.
`tree_build()` writes a tree into the map through `map_set_rock()` /
`map_set_terrain()`: foliage only fills open space, not over a slope or a
ladder (whose way up it would close). When a growth stage changes the
shape, the game checks that the trunk has room (`tree_room_to_grow()`: no
rock, structure, item nor settler where it would go) and builds it, or
waits for the next growth step; stockpile cells under the new trunk go.
`tree_at()` finds the tree of any trunk or foliage tile (foliage two trees
share: the first found). Foliage is held by its tree, not by the ceiling
rule, is no ceiling for digging and closes no room (no shelter from rain
yet). A roof cannot be planned through a tree (the trunk and foliage are in
the way). Settlers climb up a trunk to the foliage above a tile beside it
(`map_can_climb_tree()`, `MAP_TREE_CLIMB_COST` ×3, both ways).

**Branches and twigs** are not stocks of the tree: a tree with a crown
sheds them at random (`tree_shed()`), once an hour per tree
(`TREE_SHED_TWIG_CHANCE`, `TREE_SHED_BRANCH_CHANCE` times the trunk's area,
rolled from the seed, the tile and the hour like the mushrooms, so that the
game's random stream is left alone). A young tree (not yet grown) sheds twigs
only, half as often; saplings shed nothing. Each one comes from a random tile
of the foliage and either stays caught there or falls to the first floor
below, where the crown's shadow lies; it is laid only if that tile can hold
it (no rock, water, wall nor trunk) and is not full. A new world lays
`TREE_SCATTER_DAYS` days of it around its trees (`tree_scatter_litter()`,
a random stream of its own). Gathering treats what lies on a tile like
anything on the ground: every tile follows the zone covering **it**, so a
zone drawn on the ground only gathers from the ground and nobody climbs for
the foliage's twigs; a zone drawn over the foliage sends settlers up to take
them there. What lay on foliage that goes with a felled tree falls to the
ground below (`tree_clear()`; what does not fit lies as items). Shrubs
(hazel) keep their own stock of branches and twigs, taken on the same jobs.
A felled tree falls full length
(`TREE_FALL_TILES_PER_LEVEL` = 2 tiles per level, stopping before rock and
the edge of the map) where the woodcutter aims it (`tree_fall_aim()`): the
side with the fewest structures in the way, then the longest, then south,
east, north, west. Structures it lands on lose `TREE_FALL_DAMAGE_PERCENT`
(50 %) of their hit points (1 kept at least) and the player gets a notice;
the fall is a band as wide as the trunk, `tree_fall_length()` tiles long;
the wood lies along the trunk's part of it, the branches and twigs where the
crown came down, what lands over a drop falls to the ground below, and what
lay on the foliage falls to the ground under it (`tree_fell()`,
`tree_clear()`). Settlers on the foliage fall with it on their own. Other
uses of climbing trees wait for vision and wildlife (a lookout, a refuge).

Dense layers are cache-friendly, trivially serializable and hashable
(`map_hash()`). Upcoming layers: floors, buildings/walls, zones, room IDs,
temperature, fog of war.

Player orders are not part of the map (which only describes the physical
world): `Designations` (`core/designation.h`, owned by `Game`) has its own
dense layers of the same size: order type and work progress per tile.

**Digging** (step 3 of the levels, first part): earth (`ROCK_EARTH`) is dug
with the shelter builders' knowledge (`DESIGNATION_DIG`, a pit dwelling or
a gallery into a hill; its ground stays overhead as a natural roof), rock is
mined (`DESIGNATION_MINE`, nobody can yet). Both are "Digging" work
(`WORK_MINING`). `DESIGNATION_DIG_RAMP` cuts a ramp into a bank of earth
one level high: the order goes on the bank's earth tile, from the level
below; the earth goes, the tile becomes a slope (`map_set_slope()`), and
the ground over it is taken away (open space; its plants go, the items on
it slide down onto the ramp). It needs bare ground over the bank (no tree,
structure, roof or slope; open air above it) and ground beside it to lead
up to (`designation_can_apply()`); construction sites, workstations and
stockpiles there refuse the order, or drop it when they come later
(`designation_ground_free()`). `DESIGNATION_DIG_DOWN`, ordered on bare
ground over earth, digs the ground and the earth under it out from beside
the hole: the hole's floor slopes up to the ground beside it (where its
digger stood: the way into a cellar, widened from inside with `DIG`), or
stays a pit when nothing up there can be stood on (a ladder gets out of
it); what lay on the ground falls in, settlers standing there fall too. It
takes the ground it is ordered on, so sites, stations and stockpiles there
refuse it. Orders with a `tool_use` (digging) are sped up by the tool and
send the worker to a better one lying nearby first, as digging jobs in
gathering zones do.

**Felling** (`DESIGNATION_CHOP`, "Woodcutting" work, nobody can yet: it
waits for the woodcutting knowledge and an axe): the order goes on a tree's
foot, or on a sapling. Any tile of a trunk, on any level, points to its foot
(`designation_target()`, through `tree_at()`), so a rectangle over a wide
trunk marks it once and cancelling works from any of its tiles. The work
and the wood are those of the gathering zones' felling (`tree_fell_work()`,
`tree_wood_yield()`: by growth and the trunk's area); once done the tree
falls as `tree_fell()` aims it, and a fall onto a structure is counted in
`Designations.harmful_tree_falls` (a notice, as for zones).

**Support** (step 3b): a tile without rock whose next level up has a floor
or rock right over it has a ceiling (`map_has_ceiling()`), held up by the
roofs' rule: rock or a structure that blocks movement (a wall, a branch
prop, `STRUCTURE_PROP`) within `ROOF_SUPPORT_RANGE` (2) tiles on its level
(`map_ceiling_supported()`): a gallery one or two tiles wide always holds, a
room up to 4 × 4 inside rock too, a wider one needs props. A built roof is
the ceiling of the tile under it, held up the same way. Diggers never dig
what would leave a
ceiling around without support (`map_dig_keeps_ceilings()`,
`designation_waits_for_support()`): such orders are skipped by the job
search and say so in the tile inspector; the work stamp includes
`Map.build_revision`, so a prop built nearby wakes the diggers up. When a
support goes anyway (a prop or wall taken apart or fallen apart), the
ceiling caves in (`roofs_collapse()`, see below): the tile fills up again,
with the rock right above it or with earth, what lay on it goes beside it,
what stood on it breaks (half its materials beside it), its order goes; the
game removes the sites, station and zones there (`clear_caved_in()`) and
raises a notice; a settler caught there steps out the next tick
(`step_out_of_blocked_tile()`), and one whose ground went from under its
feet (a ramp cut) falls to the ground below. Falls do no harm until there is
health (M6).

Items on the ground are a dense layer too (`Items`, `core/item.h`, owned by
`Game`): at most one stack per tile (one item type, up to its stack limit),
plus map-wide totals per type and a count of stacks per level
(`level_stacks`: haulers' searches skip the empty levels, underground ones
first). Each `ItemDef` has a realistic `weight` (kg per
unit: a 7 kg log, a 20 kg stone block, a 1 kg cobble, a 300 g handful of blackberries) and a `bulk`
(room taken, about half a liter per unit, gaps included). A tile holds
`ITEM_TILE_BULK` (1000) of bulk, so the stack limit is derived:
`item_stack_limit() = ITEM_TILE_BULK / bulk` (1,000 handfuls of blackberries, 50 logs, 50
stone blocks, 666 cobbles). `items_drop()` fills the target tile, then
spreads the rest breadth-first over nearby passable tiles (never behind a
wall). Products are data: `PlantDef.cut_item/cut_yield` (scaled by growth) and
`RockDef.mined_item/mined_yield`, `PlantDef.food_item` (the food stock);
gathering takes items from the plant's stocks (`plant_take_stock()`).

Some items change while they lie on the ground: `ItemDef.transforms_into`
after `transform_hours` (fresh grass dries into dry grass in 72 hours).
`ItemStack.progress` counts the hours spent so far; merging stacks averages
it by count, like hit points. A stack that transforms keeps its hit points
percentage, and its quality when the new type has one.

Food **rots** while it lies on a tile (`ItemDef.rot_days`: how long a
pristine item lasts in the open at 15 °C; a type never both rots and
transforms). For those types `ItemStack.progress` holds the rot toward the
next hit point lost, in 1/`ITEM_ROT_STEP` of one (averaged on merges like
the drying hours); the stack is gone when its hit points run out, so a
poorly gathered (damaged) stack spoils sooner. The speed comes from the
tile: `item_rot_rate()` doubles every 10 °C above 15 °C (at most ×4),
halves every 10 °C below, stops at or below 0 °C (frozen), ×0.75 under a
built or natural roof, ×1.5 under the open sky while rain or snow falls.
`items_advance()` asks for the conditions of each tile through a callback
(`ItemConditionsFn`: the rot rate, and whether what dries can dry: not out
in the rain), so that `item` depends neither on the temperature layer nor
on the weather; the game passes `tile_conditions()`, and the tile
inspector uses `game_item_rot_rate()` for its estimate. Items in hands or
in a workstation's input do not rot.

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

**Knowledge spreads** (`docs/KNOWLEDGE.md` §1.5 to §1.7). Every gain goes
through `colonist_learn()`: a rate, a duration, a cap, and a source counted
in the gains of the day (`Colonist.knowledge_gains`, for the inspector's
tooltip); it raises `knowledge_peak`.
- *Apprenticeship* (`TASK_ASSIST`). In `try_start_work()`, once a work type's
  own jobs are all out of reach, a colonist looks for a master busy with a job
  of that type (`colonist_job_need()`: a recipe, a construction site, an order
  or a gathering job) that it lacks the level for, with no apprentice yet, and
  follows them (`ColonistContext.colonists` and `colonist_pool` give it the
  others; tests that leave them NULL have no apprentices). Standing beside a
  master who is at the work (`colonist_at_work()`), it learns
  `KNOWLEDGE_APPRENTICE_RATE` × master level / 100 per hour, up to the
  master's level, and sets `assisted_until` on the master, whose work goes
  ×1.25 while it is current. It waits up to `COLONIST_ASSIST_WAIT_TICKS` for
  a master between two jobs, and checks every `COLONIST_ASSIST_SEARCH_TICKS`
  for its own work of a higher priority. Apprenticeship counts as practice
  for the master.
- *Conversation* (`core/conversation.h`): every `CONVERSATION_CHECK_TICKS`
  after the colonists act, `game_tick()` gives each pair within 3 tiles that
  is free to chat (`colonist_can_chat()`) a chance of one chat per hour
  together, drawn from `Game.rng` in slot order. A chat lasts 6 minutes
  (`chat_ticks`, a bubble over their heads); 40% are about a knowledge, each
  speaker picking one weighted by level (×2 if practised in the last day), and
  the listener gets the whole chat's gain at once (cap 30 and the speaker's
  level).
- *Fading*: once a day, `colonist_fade_knowledge()` takes a fifteenth of a
  point off each knowledge not practised (`knowledge_practice`, a tick: doing
  the work, having an apprentice) for 15 days, down to 80% of the peak.
- *Discovered knowledges and notices*: the inspector only lists knowledges
  some living colonist has at 1 or more (`game_knowledge_discovered()`).
  Every `GAME_KNOWLEDGE_CHECK_TICKS`, `game_count_knowledge()` counts the
  masters of each knowledge and raises a `Notice` (`core/notice.h`) when a
  knowledge is discovered, mastered again, mastered by a single settler, lost
  (nobody at 50) or forgotten (nobody at 1). The count made by `game_init()`
  raises none. Notices are not part of the simulation state (`game_hash()`);
  the frontend shows the new ones in the top right corner for 15 seconds.

Food items have a
`nutrition` per unit; a hungry colonist first looks for food lying on the
ground that rots away within a day at its tile (`temperature_rot_rate()`),
within a short walk (`COLONIST_SPOILING_FOOD_MAX_COST`), then goes to the
nearest food, on the ground or on a plant bearing food it may eat in place
(fruit, nuts, leaves; not roots, nor what could be mistaken for a poisonous
plant), and eats just what it takes to be sated. Items in
hand are stored on the colonist (`Colonist.carried`), not in the layer.

**Moods** (`core/mood.h`, `Colonist.mood`, part of `game_hash()`): the mood
(0 to 100) is 50 plus what each of the settler's thoughts weighs now
(`Mood.weight`), with no drift and no all-or-nothing thought: every thought
weighs more or less over time. States (hungry, thirsty, tired and worse; cold
or too hot while awake; soaked; out in the rain) are set by `update_mood()`
every `MOOD_UPDATE_TICKS`, the colonists spread over those ticks by their
slot, then `mood_advance()` moves every thought on. A state sets in over an
hour (`MOOD_STATE_RISE_HOURS`); hunger, thirst and exhaustion grow worse the
longer they last (`ThoughtDef.grow_hours`, `grow_max`: up to twice as bad);
once over, a state fades. A memory weighs its full value for
`ThoughtDef.plateau_hours`, then fades. Fading halves a thought every
`half_life_hours` until it weighs less than `MOOD_FORGET_BELOW`; `Mood.peak`
keeps the most it weighed, shown in the inspector ("fading, was −8"). A
memory that comes back is renewed to its full value, or adds up to
`max_stack` times it (chats). Memories: how the last sleep went
(`remember_sleep()` when a sleep of two hours or more ends: in a bed or on the
ground, under the open sky, too cold or hot for a quarter of it, or well),
meals (`mood_meal()`: cooked, spoiled, varied or monotonous among the last
five), a fire's warmth on a cold day, a fireside story (teller and
listeners), chats (adding up). The mood scales work speed
(`colonist_work_speed()`, ±20 %), the level products and constructions come
from (`colonist_work_level()`, ±10 levels: quality and condition) and
learning (`colonist_learn()`, ±25 %); it replaced the old slower work after a
night on the ground. Below 25 a break may start (`mood_break_chance()`, rolled
on the game's random stream, so only for unhappy settlers): a sulk (no work,
2 to 4 hours), and below 10 going off alone far away or a binge on the
colony's food (4 to 8 hours, a binge ends once eaten). During a break
`act_out_break()` replaces the decision: urgent needs and sleep still come
first, no work, no chat, no fireside. A break ends with relief (+10, fading
after 6 hours) and 12 hours without another one. The game raises a notice when one
starts (`Mood.break_started`). The face shown follows the mood.

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
pass the other kind's tile layer, the workstation layer and the tiles of
workstation sites (`Constructions.workstation_tiles`) as `blocked` layers (a
NULL-terminated list), so their tiles are left out (the low-level
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
colonist stands, so items are never lost. Before searching, a scan of the
item layer (rows outward from the hauler, tiles of its region only) checks
that some stack could be hauled now (`haul_any_job_possible()`: same rules as
the search, so a "no" is exact). A failed haul search backs off; its stamp is
the stockpile revision + item additions + releases of haul source and storage
reservations.

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
plants grown brambles (bearing fruit at the start) and hazels (nuts in
autumn) nearby until there are `GAME_SPAWN_BRAMBLES` and `GAME_SPAWN_HAZELS`
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
revision or the release of a cleaning reservation changes its stamp. The frontend draws dark
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
(construction: sites, workstation blueprints included; crafting: bills; gathering:
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
is carried out on the map (tree removed, rock or earth dug out leaving its
floor, a ramp cut, grass cut, branches or twigs taken from the plant's stock), its
products are dropped (wood, stone blocks, branches...) and the
designation disappears.

**Gathering zones** (`core/gather_zone.h`, owned by `Game`) are how the player
asks for what the land offers: felling trees, picking and digging up wild
food, taking branches and twigs, cutting grass and picking up stones. Designations are left for
single-tile orders (mining). A zone holds one order per resource
(`GatherOrder`): off by default, and when enabled it gathers N items and stops
(counted by the order itself), or gathers until the items lying in the colony
(`Items.totals`, as for bills; grass counts fresh and dry) reach a target, or
gathers forever. Food may count days of food instead of items
(`GatherUnit`): `gather_food_days()` divides the nutrition of the food lying
in the colony by what the settlers eat in a day (`gather_daily_food()`, from
`NEED_FOOD_DECAY_PER_TICK`; the game passes it to `gather_zones_update()`).
Resources belong to groups (`GatherGroup`: wood, food, stones; grass has
none), each with an order of its own (`GatherGroupOrder`, always on: food
until 5 days of food by default, the others forever): a resource is wanted
only when its own order and its group's both want it, and a group's
`done` counts what its resources gave. An order may wait for the full harvest
(`ripe_only`): food, branches and twigs from plants whose stock is full
(every fruit ripe, not a grown plant), trees and grass fully grown (on by
default for wood only), so that settlers do not walk to plants that give
little. `gather_zones_update()` recomputes each tick which resources every zone wants
and bumps the zones revision when it changes; it is also bumped once per
in-game hour, after every plant has grown, so that idle settlers look again.

A zone tile offers a **job** (`GatherJob`: branches, twigs, harvest, harvest
carefully, dig, dig carefully, cut grass, chop, stones, in that order of
preference on one tile: branches and twigs first, since felling would take
them away, stones last) when it holds something a wanted resource comes
from. Each wild food is a resource of the job that suits its plant: picked or
dug up, and "carefully" (Gathering 60, `GATHER_CAREFUL_MIN_LEVEL`) when easily
mistaken for a poisonous plant, so that the existing job masks decide who may
take it. Digging jobs (`GatherJobDef.fetches_tool`) send the gatherer to a
better digging tool lying within `COLONIST_TOOL_SWAP_MAX_COST` first
(`fetch_job_tool()`, the craft's fetch phase reused; orders that dig do the
same); it keeps it. Each job has its
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
work (a designated tile), food (a plant about to be eaten), haul source and
storage tile, so two colonists never walk to the same job, plant, stack or
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
  work reservation releases) lets them look again once work may have become
  available, no sooner than `COLONIST_SEARCH_MIN_RETRY_TICKS` (one second at
  ×1) after the failure: in a busy colony something changes almost every
  tick, and 64 idle colonists searching again each time cost milliseconds
  per tick. Each stamp only counts the reservation kinds its search cares
  about (`Reservations.releases_by_kind`).

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
workstations and trees). Materials come two ways: a hauler (hauling work,
`TASK_SUPPLY`, searched before stockpile hauling) brings what the nearest
site waiting for materials misses, and a colonist with the construction
work type and the knowledge (`TASK_CONSTRUCT`, `RESERVATION_CONSTRUCTION`
on the site's head) fetches what is still missing, then works from a
neighboring tile: a wall cannot be stood on once built. Each trip is for
one material, as much as the colonist can carry; before going, it promises
those items to the site (`ConstructionSite.promised`,
`construction_promise()`), so that several settlers never bring the same
items; the promise is released on delivery, and by `begin_task()` when the
task changes for any reason (like reservations). A builder only takes a site
that is ready for work or misses a material nobody brings yet. Before
searching the map, both check the list of sites (`any_site_job()`): the
search stamp moves with every item added anywhere, and a site waiting for a
material that does not exist yet (grass still drying) must not cost a
search each time. Grass
or another plant but a tree on the tile is cleared first (see *Clearing plants* below). When it
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

**Clearing plants**: blueprints (construction sites, of structures and workstations) may
be placed on grass, shrubs and low plants, not on trees
(`map_plant_blocks_building()`: felling needs an axe nobody has yet). Once the
materials are in, the builder first spends `gather_clear_work()` on the plant
(cutting grass or a low plant, pulling up a shrub, nothing for a withered
plant, no knowledge needed), kept in the blueprint's `clear_progress` if it
is interrupted; `gather_clear_plant()` then removes the plant and drops what
it gives (cut grass, its twigs and branches, its food, with quality from
the builder's gathering level). Only then does the construction work start.
`map_set_structure()` still clears any plant left, as a safety net.

**Roofs** work like floors: a built roof is the floor of the open tile over
a hut, on the level above its walls, a terrain of its own
(`TERRAIN_GRASS_ROOF`, whose `TerrainDef.built` is the `STRUCTURE_ROOF` it
was built as; `map_roof()`, `map_set_roof()`), with its grade in
`Map.roof_quality`. Being a floor, it covers what is under it (a bed, a
wall, a stockpile, a workstation: `map_is_roofed()`, `map_roof_over()`), and
nobody stands on it: a thatch of grass on branch rafters is too frail
(future plank floors will be walked on). There is no roof view: the player
goes up a level to plan roofs, see them and order them taken down.
`STRUCTURE_ROOF` is planned like any structure (a rectangle filled with
sites), on open tiles with open sky over them and a floor under them that
is no roof, slope or ladder (`construction_can_place_roof()`). Its sites lie
in the same index layer as the others, on the roof's tile, so a roof can be
planned over the site of a wall or a bed and the other way round. Builders
work from below, overhead, as for anything else from a tile next to the
one under the site: `construction_worked_from()` gives the site worked on
from beside a tile, the structure's on it or (`overhead`) the roof's over
it, and the searches look at both, the structure's first. A roof tile needs
a wall, a prop or rock within `ROOF_SUPPORT_RANGE` (2) tiles on the level
below (`map_roof_supported()`); a planned wall counts when planning
(`construction_roof_held()`), so a whole hut is planned at once, but the
work waits until a wall stands (`construction_can_work()`). *Take apart*
(`DESIGNATION_DECONSTRUCT`) takes roofs down too, ordered on the roof's tile:
the worker stands on the level below beside it (`work_reach()` in
`colonist.c`; the order search also looks at the tile over each neighbor,
and `open_job_reachable()` at the regions of the level below), half the
materials fall onto the floor under it. Since a roof is the ceiling of the
tile under it, it follows the ceilings' rule: when its last support goes (a
wall taken apart or fallen apart, a door in its place, rock dug out), the
roof falls in (`core/roof.h`): its tile is open again, half its materials
drop on the floor under it, an order to take it down goes, and the game
raises a notice; and diggers do not dig out rock that alone holds up a roof.
A branch prop holds roofs up as a wall does (open shelters).
`roofs_collapse()` runs every tick after the settlers and only checks the
tiles within `ROOF_SUPPORT_RANGE` of what the map logged as changed
(`Map.build_log`, `Map.topology_log`), or every tile of every level when the
logs overflowed; the `RoofWatch` it keeps is derived, not hashed. A light
grass roof hurts nobody. The roof itself is out in the open air, for heat
(`TEMPERATURE_MATERIAL_OPEN`). `map_hash()` only hashes the grade layer once
a roof exists, so older hashes stay valid.

**Rooms** (`core/room.h`, owned by `Game`): every tile without rock, a wall
or a door belongs to an area (flood fill over orthogonal neighbors, water
included); an area is a room when it does not reach the map's edge and has
at most `ROOM_MAX_TILES` tiles, roofed when all its tiles are
(`map_is_roofed()`). A cache like the regions, compared with
`Map.topology_revision` and `Map.build_revision` (bumped by every structure
or roof change); nothing in the simulation uses rooms yet, so they are only
rebuilt when asked for (`game_rooms()`, the tile inspector), never during a
tick.

**Temperature** (`core/temperature.h`, `Game.temperatures`, part of
`game_hash()`): `temperature_outdoor()` is a function of the tick (seasons
blended with a cosine between their middles, `temperature_season_mean()`,
plus a daily swing coldest at 5:00 and warmest at 15:00,
`temperature_daily_swing()`); the weather changes it (`weather_outdoor()`,
below), and `temperatures_tick()` takes the result. Each tile holds a
temperature; every tick `temperatures_tick()` updates one band of rows in
place, a whole pass every `TEMPERATURE_SWEEP_TICKS` (100), explicitly: a
tile moves toward its four neighbors at the lower conductance of the two
(air 4/h, door 1.5, wall and rock 0.5), toward the outdoor air at a rate
set by its cover (open sky 6/h, built roof 0.3, natural roof none; the
first floor above a tile decides) and,
under a natural roof, toward the ground (10 °C, 0.3/h). Rock stays at the
ground's temperature; open space under the sky is the outdoor air
(`TEMPERATURE_MATERIAL_OPEN`): a tile with a side open to it exchanges heat
with the outdoor air through that side instead (`Temperatures.exposure`:
the cover's rate plus the air's conductance for each open side). The step
is stable while a tile's rates add up to less than 25/h. The bands of rows
run through all the levels one after the other (`height × depth` rows),
and only the tiles of a bitset (`Temperatures.active`) are worked out: rock
and open space, most of a hilly map, are skipped 64 at a time. For now a
tile only exchanges heat with the tiles of its own level (the edges between
levels have a rate of 0). What each tile is made of and covered by, the
exposures and the rate on each edge are caches: updated for the tiles of
the map's change logs and the tiles below them, whose cover they may change
(`Map.topology_log`, and `Map.build_log` for structures, roofs and rock
where one could not walk anyway), rebuilt whole when the logs overflowed or
the layers were written directly (`MapTopologyChange.anywhere`). Edge rates
of 0 and a padded layer let tiles on the edges of the map go through the
same loop.

What settlers feel is not only the air: a burning fire radiates warmth
(`hearths_radiant_warmth()`: `HEARTH_RADIANT_WARMTH` one tile away, falling
off exponentially, nothing beyond `HEARTH_RADIANT_RADIUS`, less when low on
fuel, stopped by a wall, door or rock on the line between, several fires
adding up), which barely warms the air outdoors but makes one sweat beside
it. `hearths_felt_temperature()` adds it to the air's; it is computed when
asked for, never stored. A settler asleep where it feels colder than
`COLONIST_COLD_SLEEP_BELOW` or hotter than `COLONIST_HOT_SLEEP_ABOVE` rests
less (`colonist_sleep_temperature_factor()`, times the bed's or the ground's
rest factor). With no bed to go to, a settler who would sleep too cold or hot
walks to the nearest free tile beside a burning fire that feels right
(`is_warm_spot()`, within `COLONIST_WARM_SPOT_MAX_COST`, reserved like a bed
with `RESERVATION_BED`), and asleep on the ground it checks again every
`COLONIST_WARM_SPOT_CHECK_TICKS` (`SEARCH_WARM_SPOT`): the night gets colder,
fires burn low. Nothing of this runs while no fire burns. The frontend shows
the air and felt temperatures in the inspectors and a felt temperature view
(`render_temperature()`, key H).

**Weather** (`core/weather.h`, `Game.weather`, part of `game_hash()`): the
sky changes every `WEATHER_PERIOD_HOURS` (6): it stays with
`WEATHER_PERSISTENCE` (0.4), else a sky is drawn from the season's odds
(`WEATHER_SEASON_ODDS`: summer the sunniest and stormiest, autumn the
foggiest), moving one step at most along clear → cloudy (or fog) → rain →
storm (`weather_next()`, a pure function); fog only forms in periods
starting before noon. Rain and storms fall as snow at or below 1 °C
(`weather_precipitation()`). Each day a warm or cold spell drifts
(`Weather.spell`: keeps 0.75 of itself plus a change of about ±2 °C,
within ±8). The outdoor air is the season's mean plus the spell plus the
sky's offset (rain −2 °C, storm −4) plus the daily swing times the sky's
factor (clear ×1.3, cloudy ×0.6, rain ×0.4...). Every hour the snow cover
of open ground grows while it snows and melts above 0 °C (faster in the
rain), and `recent_rain` keeps 0.96 of itself plus one per hour of rain.
Draws come from `weather_roll()`, a hash of the seed, the period or day and
a salt, never from `game->rng`: the weather does not depend on what the
settlers do. The game starts on a clear sky.

What it changes: what falls wets settlers under the open sky
(`weather_wetting()`, 1 an hour in the rain, 2 in a storm, 0.25 in the snow,
into `ColonistContext.wetting`; `Colonist.wetness` dries by 0.25 an hour, 1
where it feels 20 °C or more), and a wet settler feels up to
`COLONIST_WET_CHILL` (8 °C) colder, which counts for sleep; settlers do not
run for shelter (that comes with hypothermia). A burning hearth under the
open sky soaks up the same rate (`Workstation.soaked`) and goes out at
`HEARTH_SOAKED_OUT` (2: two hours of rain), its fuel kept; nobody lights a
hearth rain or snow falls on (`hearth_rained_on()`). Food out in the rain
rots ×1.5 and fresh grass does not dry (`ItemConditions`). Mushrooms come
×(1 + recent rain / 4), at most ×4. The frontend shows the sky and snow
cover in the HUD, how wet a settler is, a hearth soaking, the snow cover on
open ground (`render_snow_cover()`), falling rain and snow, fog and
lightning over the view (`render_weather()`, animated by real time), and
clouds dim the daylight (`weather_light()`).

**Beds**: a colonist going to sleep walks to its own bed (`core/bed.h`),
else to the nearest free bed, which becomes its own (`RESERVATION_BED`,
within `COLONIST_BED_SEARCH_MAX_COST`), and sleeps on the ground otherwise.
`Beds` (owned by `Game`) keeps the owner id (colonist slot + 1) on the bed's
head tile and the bed of each owner; a bed owned by someone else is never
taken, even when its owner is awake. `game_tick()` calls
`beds_forget_missing()` (beds taken apart or fallen apart lose their owner)
and `game_remove_colonist()` frees the settler's bed. Rest comes back at `rest_factor` in a bed (1 for a grass
bed) and `COLONIST_GROUND_REST_FACTOR` (0.8) on the ground; a night on the
ground is a bad memory (moods, below).

### Workstations, bills and carried tools

Workstations (`core/workstation.h`, owned by `Game`, at most
`WORKSTATION_MAX`) sit on one tile each. The player places a **blueprint**
(`game_place_workstation()`, which takes the tile out of any zone): a
construction site of one tile (`ConstructionSite.workstation`, its type +
1; `constructions_place_workstation()`), built like a structure's. Haulers
bring its materials (a knapping spot needs a cobble as its anvil), a
colonist with the construction work type and the knowledge the station
needs (`colonist_can_build_site()`) fetches what is still missing and does
the work from a neighboring tile; `construction_add_work()` then places the
station in `Workstations`, ready for bills. A station made of nothing
(crafting spot, `workstation_needs_construction()` false) is placed ready,
unless a plant is in the way: its site then only has the plant to
clear. `game_can_place_workstation()` keeps room in the station list for
every site (`Constructions.workstation_count`); should a station still not
fit when its work is done (a tree grown on the tile), the site waits. The
cancel tool removes blueprints; removing a station drops the input of a
craft in progress.

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

Crafts (`TASK_CRAFT`) go through three phases (fetch, go to the station,
work) and reserve the station (`RESERVATION_STATION`).
At one priority level, designations are tried first, then construction,
crafting, gathering zones (a background job that never ends by itself) and
hauling. Failed station searches back off with a stamp made of the
workstations revision, item additions and reservation releases; gathering
searches with the zones revision and work reservation releases.

### Hearths and fire

An open hearth (`WORKSTATION_OPEN_HEARTH`, `WorkstationDef.hearth`) is a
workstation dug like a site (no material, 250 work, Fire 50) and walked over
like the other stations for now; crafters and refuellers work from a
4-neighbor tile. Its fire lives on the `Workstation` (`fuel` in kilograms,
`burning`, the player's `keep_burning` and `fuel_types`, and `fuel_promised`,
what haulers are bringing); `core/hearth.h` holds the rules and
`hearths_burn()` runs every tick in `game_tick()`, bumping the workstations
revision when a fire goes out or falls below the refuel mark so that
searches backed off on that revision look again.

- **Refuelling** (`TASK_REFUEL`) is hauling work, tried before bringing
  materials to sites: the hearth wants fuel once its fuel plus what is
  promised is below half, then enough to fill it; the hauler promises all
  of it, then only what it picked up, and releases the promise when the job
  ends in any way (`release_fuel()` in `begin_task()`; ignored if the
  station was replaced, its `number` changed).
- **Lighting** (`TASK_LIGHT`, the `WORK_FIRE` work type, Fire 50 or more)
  reserves the hearth (`RESERVATION_FIRE`) and goes through `LightStep`s:
  first, for a hearth with no fuel and none on its way
  (`hearth_needs_kindling()`, and some fuel the hearth takes lying
  somewhere), fuel fetched and put in (`LIGHT_FETCH_FUEL`,
  `LIGHT_BRING_FUEL`: no hauler needed); then embers from another burning hearth within `HEARTH_FIRE_CARRY_MAX_COST`
  of it (`Colonist.ember_ticks`, a flag rather than an item: they last
  4 hours and are dropped with the task), else a handful of grass fetched as
  tinder and an attempt by friction, which burns the tinder and succeeds by
  `hearth_friction_chance()` of the level before the attempt (an RNG draw).
- **Recipes that need fire** (`recipe_needs_fire()`: made at a hearth) are
  skipped by `choose_bill()` while the hearth is out, and a craft stops when
  it goes out (the input and progress stay on the station). Hardening keeps
  the spear's grade and hit point percentage (`recipe_craft()`).
- **Fireside stories**: in the evening (`hearth_story_time()`, 19:00 to
  22:00), an idle or wandering settler tries `try_fireside()` before looking
  for work: one who masters a knowledge the player marked to teach
  (`Game.teach`, a colony setting shown in the hearth's panel) goes beside a
  burning hearth nobody tells stories at (`TASK_STORY`, its best such
  knowledge in `story_topic`; telling is practice), others sit within 3
  tiles of a storyteller with room (`TASK_LISTEN`, at most 8, `mentor` is
  the teller) and learn while it tells, up to the teller's level and
  `KNOWLEDGE_STORY_CAP` (60), as `KNOWLEDGE_SOURCE_STORIES`. Both end at
  22:00 or when the fire goes out. The evening search backs off like the
  apprenticeship one.
- **Heat**: a burning hearth adds `HEARTH_HEAT_PER_HOUR` degrees to its
  tile every tick (`temperatures_add_heat()`, see below).
- **Rendering**: a ring of stones, sticks for the fuel, flames moving with
  real time; at night `render_daylight()` darkens plain rectangles around
  the area lit by burning hearths and a grid of per-corner alpha quads
  inside it (`hearths_light_at()`, linear over 6 tiles, fires of the level
  shown only), plus an additive warm glow.

Stones are two map layers (`Map.cobbles`, `Map.flint`): how many of each lie
about a tile, `MAP_STONES_MAX` (20) at most together, taken one by one by
gathering and never growing back. World generation gives every stony tile
(`TerrainDef.stony`: soil, rich soil, gravel, rough stone; not sand) 0 to 20
stones, from a random stream of their own so that the plants do not depend on
them; on flint patches (noise, denser on gravel and rough stone) each stone is
flint with the patch's share, up to 60 %. The spawn makes sure a tile with at
least `GAME_SPAWN_FLINT_STONES` flint lies near the settlers, laying a small
patch otherwise. The tile inspector shows the stones left in the ground.

### Forbidding

The player may forbid the settlers things in the colony; each flag lives
with the owner of the thing, so that it goes when the thing goes:
`Map.forbidden` (`MapForbid` bits: the plant of a tile, a structure on its
head tile, a roof; cleared by `set_tile_structure()`, `map_set_roof()` and
`map_remove_plant()`, the only way plants are taken away once a game runs),
`Items.forbidden` (per tile, cleared when the stack is gone) and
`Workstation.forbidden`. `game_set_forbidden()` (`ForbidTarget`) resolves a
tile of a tree's trunk or foliage to its foot; `game_set_forbidden_rect()`
(the Forbid and Allow tools) takes everything on a rectangle but grass, the
ground cover nearly everywhere.

Settlers see forbidden things as absent rather than through checks
scattered over every job: they read stacks through `items_get_allowed()`
(an empty stack where it is forbidden), so pickups, hauls, deliveries, meals
and fuel skip them; `items_drop()` and haul destinations never add to a
forbidden stack; `map_has_edible_plant()`, `covered_tile_holds()` (gathering),
`designation_forbidden()` (orders on a forbidden structure, roof or tree) and
`construction_forbidden()` (a refresh site on a forbidden structure, a plant
to clear) close the jobs on plants, orders and sites; `job_station()`,
`station_on()` and the station searches skip forbidden stations; beds are
looked up with `map_forbidden()`. The validity checks run every tick, so a
job under way on something forbidden meanwhile is dropped (a settler asleep
keeps sleeping). A forbidden door or ladder is locked: `map_locked_at()`
makes `map_move_cost_at()` and `map_climb_cost_at()` return 0, and
forbidding one logs a topology change. Allowing something counts as new work
for the searches that backed off (`Items.additions`, `Workstations.revision`,
`Designations.revision`, `Constructions.revision`, the gathering zones'
revision). The new layers enter the hashes only while something is
forbidden, so the hashes of games without any are unchanged.

### Debug cheats

`Game.debug` (`GameDebug`) holds cheats for testing, switched from the debug
menu (F4, `game/debug_panel.c`) through `game_set_instant_build()`,
`game_set_free_crafts()`, `game_set_no_tools()`,
`game_set_instant_deconstruct()` and `game_set_instant_fell_and_mine()`. They are simulation state: `game_hash()` includes
them, but only while one is on, so that hashes stay those of a game without
them (`bench_simulation` hashes are unchanged).

- **Instant build**: at the start of each tick, `game_tick()` finishes every
  site it can: plants on its tiles are cleared (`gather_clear_plant()`), then
  `construction_finish_now()` delivers the missing materials (standard
  grade) and does all the work at `GAME_DEBUG_BUILD_LEVEL` without drawing
  from the RNG (average grade). Zones leave the tiles it built on, as after a
  builder's work. A roof without support and a station that cannot be placed
  wait; sites are finished in list order, so walls placed with their roof
  hold it up in the same tick when they come first.
- **Free crafts**: `ColonistContext.free_crafts` lets `choose_bill()` skip
  the input check (`bill_free_input()`: the bill must allow some input type)
  and `plan_station_job()` skip fetching it; at the station,
  `workstation_start_free_craft()` starts the craft with the recipe's count of
  the first allowed input type, standard grade at least. `Workstation.free_input`
  marks it so that removing the station or the bill drops nothing. Tools, the
  recipe's knowledge and a burning fire are still needed.
- **No tools**: `ColonistContext.no_tools` lets crafts go without their tools
  (no tool search nor detour for a better one, `craft_tool_factor()` does not
  stop) and raises the speed of work done without a tool to a standard
  tool's (×1, `work_with_tool()`); carried tools still help and wear when
  they do better. `game_recipe_missing_tools()` and `game_bill_has_input()`
  follow.
- **Instant deconstruct**: at the start of each tick, `game_tick()` carries
  out every order to take a structure apart or a roof down
  (`designation_add_work()` with all the work needed: half the materials
  back), removing the refresh site of a structure taken apart; roofs left
  without support fall in later in the same tick.
- **Instant felling and mining**: the same for orders to fell trees and to
  mine rock (`fell_and_mine_now()`), which nobody can carry out yet: rock
  holding a ceiling up waits, orders left with nothing to do go.
  `game_designation_available()` accepts both orders while it is on, so that
  the player can give them. Hashed apart from the other cheats (earlier
  hashes with cheats on stay as they were).
- **Weather**: `game_debug_next_weather()` switches to the next sky at once,
  `game_debug_cold_snap()` sets the coldest spell (until the next day's
  draw). They change `Game.weather` directly (always in the hash).
- **One-shot commands** change the state directly (hashed as any state):
  `game_debug_learn_all()` (every knowledge at least at
  `GAME_DEBUG_KNOWLEDGE_LEVEL`, practised now), `game_debug_skip()` (ticks
  run at once: an hour or a day from the menu, the frame waits),
  `game_debug_next_season()` (the calendar jumps, nothing simulated in
  between), `game_debug_spawn_items()` (a stack dropped as `items_drop()`
  would) and `game_debug_teleport()` (the colonist gives up its task, its
  search delays are reset). The debug tool (`APP_TOOL_DEBUG`) applies the
  last two where the player clicks.
- **From the command line**: `--debug instant_build,learn_all,...` (or
  `all`) turns cheats on from the start, `--scenario <n>` sets up a test
  situation of `core/scenario.h` (the benchmark's), `--colonists <n>` sets
  the number of settlers: a visual check starts where it needs to, without
  clicking its way there.

## Pathfinding

- 8-directional grid, cost of entering a tile = `map_move_cost()` (× √2
  diagonally), no diagonal move past an impassable corner.
- A* with the octile heuristic scaled by `PATH_MIN_TILE_COST` (admissible as
  long as no passable tile is cheaper: checked by a test).
- `pathfinder_find_nearest()` runs Dijkstra until a predicate matches (food,
  free tile...), with a cost limit to bound the search.
- A `Pathfinder` owns per-tile scratch arrays reused across searches (stamps
  instead of clearing), so searches do not allocate.
- Slopes and ladders lead to the levels above and below (orthogonal steps
  only, ×2 and ×3: see *World representation*); the heuristic adds the
  least those climbs cost (`MAP_CLIMB_COST - 1` per level), so it stays
  admissible (a test compares
  A* with Dijkstra on hilly maps).
- Benchmark: `bench/bench_pathfinding [size] [searches] [seed] [flat]`;
  about 0.8 ms per cross-map search on a flat 200 × 200 map in Release.

### Connectivity regions

A search for something that cannot be reached explores everything within its
cost limit before giving up. `Regions` (`core/region.h`, owned by `Game`)
avoids that: every passable tile gets the id of its region, two tiles being in
the same region exactly when one can walk between them (flood fill over
orthogonal neighbors and up and down slopes, `map_level_links()`; diagonal
moves never connect more because corners cannot be cut). Each region also
knows whether it contains a drinking spot.

- Regions are a **cache**, not simulation state: they are not hashed, and
  every use is an exact shortcut for a search that would fail anyway, so they
  never change what the simulation does (the state hashes of the benchmark
  scenarios are identical with and without them).
- They are refreshed at the start of each tick and before each colonist acts
  when `Map.topology_revision` changed (a tree felled by the first colonist
  would otherwise leave every later one with "maybe" answers, hence full
  failing searches, for the rest of the tick). A dug-out tile (the common case) is added in place from the map's
  change log; a change that joins or splits regions rebuilds everything.
- Queries check the revision: if the map changed since the last refresh
  (mining earlier in the same tick), they answer "maybe connected", never a
  stale "no".
- Uses today: wandering does not path to the other side of a wall; a thirsty
  colonist does not search when its region has no water; before a work search,
  a scan of the designation layer checks that some open job has a tile to
  stand on in the colonist's region (orders deep inside rock, on another
  island, or all reserved would otherwise cost a full exploration per idle
  colonist). The same kind of scan, limited to the colonist's region and to
  the square the search could reach at most (a step costs at least
  `PATH_MIN_TILE_COST`), runs before looking for food, hauling, fetching
  materials and looking for a free bed (`any_tile_reachable()` in
  `colonist.c`).

### Measuring

`bench/bench_simulation [colonists] [days] [scenario] [size] [seed]` runs a
colony without a window (scenarios: `core/scenario.h`, which the game can
also start on with `--scenario`) and prints the average and worst tick time and the
final state hash. An optimization that is not supposed to change behavior
must leave that hash unchanged. Reference (Release, 200 × 200, 64 colonists,
10 in-game days): about 8 µs per tick left alone, 10 µs with every tree, rock
and bush designated; food searches when no food is within reach are
answered by the scan. Scenario 3 (construction, a gathering zone
and a small stockpile that fills up): about 8 µs per tick with 16 colonists
over 10 days (worst tick 4 ms), 40 µs with 64 colonists over 5 days (worst
tick 15 ms; it was 2.8 ms per tick and 1.4 s before the exact pre-checks,
narrowed stamps and minimum retry delay). Its spikes now come from every
colonist waking up at the same tick and searching the gathering zone for a
job (1 to 3 ms each, a search that succeeds far away). Failing searches dominated: a
search for something out of reach explores everything within its limit, so
the cheap answer is a scan that proves nothing is there.

## Randomness

- `Rng` (PCG32) instances are seeded explicitly. World generation uses its own
  stream; the simulation uses `game->rng`. New systems that need randomness
  either use `game->rng` (in a fixed order) or their own derived stream.
- The weather (`weather_roll()`) and mushrooms (`world_roll()`) come from
  hashes of the seed and the period, day or tile, never from `game->rng`.
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
- Ground tiles: tile textures (`sprite_ground()`, `sprite_wall()`,
  `sprite_grass()`) for the terrains seen from
  above (also sand, gravel, rough stone, the foliage of trees and the thatch
  of built roofs, which are terrains), and the cut face of granite, sandstone
  and earth walls seen from inside them; terrains without a texture would be
  colored rectangles with a per-tile brightness variation. A texture is the same on every tile of its
  kind (no transitions between terrains yet), so it tiles seamlessly: the
  generator uses periodic noise and draws shapes wrapped around the edges,
  and each file is the 128 × 128 texture with a 32-pixel wrapped border, so
  that filtering at a tile's edge blends with the next tile, never with a
  neighbor in the atlas (`sprites_draw_tile()` draws the inside only). The
  ground is loose and lit from the upper left (crumbs, pebbles with their
  shadow); walls are flat, dense and much darker, so that they never read as
  ground. Grass is a transparent texture over the ground, in 4 stages by
  growth (short sparse blades to a cover hiding the soil). `render_map()`
  draws in three passes: flat colors (trunks), then textures and grass (one
  sprite batch), then what goes over the ground (slopes, drop edges).
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
  overflow its tile: grown hazels reach 2 tiles high and 1.5 wide. Plants and
  colonists are drawn together from the back (top of the screen) to the
  front (`render_world_sprites()`): rows of plants merged with colonists
  sorted by the depth of their feet. A plant (a hazel, a sapling) that
  would hide a colonist standing behind it is drawn see-through.
- Trees that take room are drawn like walls and floors, no sprite: a trunk
  tile shows its cut face (wood with growth rings on a 1 × 1 trunk, bark on
  the sides that face out, `render_trunk()`), a foliage tile a floor of
  leaf clumps (`render_foliage()`), and the ground under foliage is shaded
  (`under_foliage()`). Viewed from above, the levels below are blurred as
  usual.
- Plants: a table in `sprites.c` gives each plant type its sprites: trees
  (saplings, the first two stages only), hazels and brambles one per growth
  stage (`plant_growth_stage()`, 5
  stages), low plants three sizes the stages are shared out among, mushrooms
  one; hazels and brambles have a second set showing their food
  (`plant_has_food()`). Withered plants are not drawn. Sprites are mirrored
  on some tiles for variety.
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
- Plant see-through test: the head and chest of a colonist behind the middle
  of a plant sprite (its canvas has empty corners).
- Text: SDL3_ttf renderer text engine, with a cache keyed by string content;
  entries unused for 120 frames are evicted.

## Frontend state

UI state that is not part of the simulation (camera, selection, selected
stockpile, gathering zone, workstation or tile, active tool, drag
rectangle, debug overlay and menu toggles) lives in `App`. The level shown is
`Camera.level` (Page Up / Page Down, or Shift + wheel; it starts on the
level of the first settler; the HUD numbers it from `Map.surface_level`): the map, the entities and the overlays are
drawn for that level, and clicks and tools act on it (a selected structure
keeps its level in `App.selected_tile_z`). A click on the map selects a
colonist, else the first of what lies on the tile (`ClickTarget`): the
station, the tile (a structure, a roof, a site, items or a plant: its window,
`game/tile_panel.h`), the stockpile, the gathering zone, the tile for its
grass alone; a click on what is selected goes to the next one. What is
forbidden carries a "no entry" sign (`render_forbidden_markers()`). Where the level shown is
open space, the levels below show through (`render_below_begin()`): up to
`APP_LEVELS_SHOWN_BELOW` of them, deepest first, each with its ground,
structures, stations, items and sprites, drawn on an offscreen image three
times smaller then stretched back with smoothing (a cheap blur), each level
darkened once more than the one above. The ground of the level shown has a
dark edge over each drop, and slopes are lit towards the higher side.
Ramps (natural slopes and dug ones, not ladders) carry a triangle on the
level shown only (`render_ramp_markers()`, from `map_ramp_at()`): pointing up
on a ramp leading up from it, down on the open space over one leading down
from it; see-through, over the items, plants and settlers there.
Escape and a right click on the map
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
Built roofs are drawn as the floor of their level (thatch, by
`render_structures()`), their sites and orders on that level too: seen from
the hut's level they do not hide it, seen from the level above they cover
it. While planning roofs, the open tiles a roof could rest on are shown
(`render_roof_support()`). A click on a roof selects it like a structure,
and the cancel tool works on the level shown. The tile inspector tells
about the roof a tile is, or the roof over it (`map_roof_over()`). (The
earlier roof view, with roofs in a layer of their own, is gone.)

The UI is immediate-mode (`game/ui.h`): panels and widgets are laid out and
drawn every frame. Everything drawn belongs to an owner: a window (an
`AppWindow` id) or a plain panel. A left or right click on a panel or window
drawn during the previous frame is captured before it reaches the map and
handed to the topmost owner under it, whose first widget containing it
consumes it; only that owner's widgets light up under the mouse. The mouse
wheel over a window scrolls it (over any panel, it never zooms the map).
A dropdown's list is drawn at the end of the frame, over the windows, as an
owner of its own (`UI_POPUP_OWNER`), so that it captures the clicks on it;
the option picked is returned by the dropdown at the next frame, and a click
anywhere else closes the list (on the map, it does nothing else).

Windows (inspectors, work grid, controls help) keep their position, stacking
order and scroll in `Ui` from one frame to the next. They are sized to their
content: `ui_layout_window()` runs the rows once to measure them, then again
to draw them. A window taller than the screen allows scrolls, with its
content clipped. A click brings a window to the front, and a window opened
again comes to the front; the HUD draws the open windows back to front
(`hud_open_windows()`), then the menus on top, then an open dropdown list,
then the tooltip. Widgets act on the game directly through core functions
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
